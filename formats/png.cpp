#include "png.h"
#include "formats/deflate.h"

#include <memory>

#include "contrib/preflate/preflate.h"

class png_precompression_result : public deflate_precompression_result {
protected:
    void dump_idat_to_outfile(OStreamLike& outfile) const {
        if (format != D_MULTIPNG) return;
        // store IDAT pairs count
        fout_fput_vlint(outfile, idat_pairs_written_count);

        // store IDAT CRCs and lengths
        fout_fput_vlint(outfile, idat_lengths[0]);

        // store IDAT CRCs and lengths
        int i = 1;
        auto idat_pos = idat_lengths[0] - 2;
        if (idat_pos < original_size) {
            do {
                fout_fput32(outfile, idat_crcs[i]);
                fout_fput_vlint(outfile, idat_lengths[i]);

                idat_pos += idat_lengths[i];
                if (idat_pos >= original_size) break;

                i++;
            } while (i < idat_count);
        }
    }
public:
    std::vector<unsigned int> idat_crcs;
    std::vector<unsigned int> idat_lengths;
    int idat_count;
    unsigned int idat_pairs_written_count = 0;

    explicit png_precompression_result() : deflate_precompression_result(D_PNG) {}

    long long complete_original_size() const override {
        unsigned int idat_add_offset = 0;
        if (format == D_MULTIPNG) {
            // add IDAT chunk overhead
            idat_add_offset += idat_pairs_written_count * 12;
        }
        return deflate_precompression_result::complete_original_size() + idat_add_offset;
    }

    void dump_to_outfile(OStreamLike& outfile) const override {
        dump_header_to_outfile(outfile);
        dump_penaltybytes_to_outfile(outfile);
        dump_idat_to_outfile(outfile);
        dump_recon_data_to_outfile(outfile);
        dump_stream_sizes_to_outfile(outfile);
        dump_precompressed_data_to_outfile(outfile);
    }

    void calculate_idat_count() {
        if (format != D_MULTIPNG) return;
        // simulate IDAT write to get IDAT pairs count
        int i = 1;
        auto idat_pos = idat_lengths[0] - 2;
        if (idat_pos < original_size) {
            do {
                idat_pairs_written_count++;

                idat_pos += idat_lengths[i];
                if (idat_pos >= original_size) break;

                i++;
            } while (i < idat_count);
        }
    }
};

bool PngFormatHandler::quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) {
  return memcmp(buffer.data(), "IDAT", 4) == 0;
}

std::unique_ptr<precompression_result> try_decompression_png(Precomp& precomp_mgr, IStreamLike& fpng, long long fpng_deflate_stream_pos, long long deflate_stream_original_pos, int idat_count,
  std::vector<unsigned int>& idat_lengths, std::vector<unsigned int>& idat_crcs, std::array<unsigned char, 2>& zlib_header) {
  std::unique_ptr<png_precompression_result> result = std::make_unique<png_precompression_result>();

  std::unique_ptr<PrecompTmpFile> tmpfile = std::make_unique<PrecompTmpFile>();
  tmpfile->open(precomp_mgr.get_tempfile_name("precompressed_png"), std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);

  // try to decompress at current position
  recompress_deflate_result rdres = try_recompression_deflate(precomp_mgr, fpng, fpng_deflate_stream_pos, *tmpfile);

  result->original_size = rdres.compressed_stream_size;
  result->precompressed_size = rdres.uncompressed_stream_size;
  if (result->precompressed_size > 0) { // seems to be a zLib-Stream

    precomp_mgr.statistics.decompressed_streams_count++;
    if (idat_count > 1)
    {
      precomp_mgr.statistics.decompressed_png_multi_count++;
    }
    else
    {
      precomp_mgr.statistics.decompressed_png_count++;
    }

    debug_deflate_detected(*precomp_mgr.ctx, rdres, "in PNG", deflate_stream_original_pos);

    if (rdres.accepted) {
      precomp_mgr.statistics.recompressed_streams_count++;
      if (idat_count > 1)
      {
        precomp_mgr.statistics.recompressed_png_multi_count++;
      }
      else
      {
        precomp_mgr.statistics.recompressed_png_count++;
      }

      precomp_mgr.ctx->non_zlib_was_used = true;

      debug_sums(*precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, rdres);

      result->success = true;
      result->format = idat_count > 1 ? D_MULTIPNG : D_PNG;

      result->idat_count = idat_count;
      result->idat_lengths = std::move(idat_lengths);
      result->idat_crcs = std::move(idat_crcs);
      result->calculate_idat_count();

      if (!rdres.uncompressed_stream_mem.empty()) {
        auto memstream = memiostream::make(rdres.uncompressed_stream_mem.data(), rdres.uncompressed_stream_mem.data() + rdres.uncompressed_stream_size);
        result->precompressed_stream = std::move(memstream);
      }
      else {
        tmpfile->reopen();
        result->precompressed_stream = std::move(tmpfile);
      }

      result->rdres = std::move(rdres);
      std::copy(zlib_header.begin(), zlib_header.end(), std::back_inserter(result->zlib_header));
      result->inc_last_hdr_byte = true;
    }
    else {
      if (precomp_mgr.is_format_handler_active(D_RAW)) precomp_mgr.ctx->ignore_offsets[D_RAW].insert(deflate_stream_original_pos - 2);
      if (precomp_mgr.is_format_handler_active(D_BRUTE)) precomp_mgr.ctx->ignore_offsets[D_BRUTE].insert(deflate_stream_original_pos);
      print_to_log(PRECOMP_DEBUG_LOG, "No matches\n");
    }
  }
  return result;
}

std::unique_ptr<precompression_result> PngFormatHandler::attempt_precompression(Precomp& precomp_mgr, std::span<unsigned char> checkbuf, long long original_input_pos) {
  // space for length and crc parts of IDAT chunks
  std::vector<unsigned int> idat_lengths {};
  idat_lengths.reserve(100 * sizeof(unsigned int));
  idat_lengths.push_back(0);
  std::vector<unsigned int> idat_crcs {};
  idat_crcs.reserve(100 * sizeof(unsigned int));
  idat_crcs.push_back(0);

  int idat_count = 0;
  bool zlib_header_correct = false;

  std::array<unsigned char, 2> zlib_header{};

  auto deflate_stream_pos = original_input_pos + 6;

  auto memstream = memiostream::make(checkbuf.data(), checkbuf.data() + checkbuf.size());

  // get preceding length bytes
  std::array<unsigned char, 12> in_buf{};
  while (true) {
    if (original_input_pos < 4) break;
    precomp_mgr.ctx->fin->seekg(original_input_pos - 4, std::ios_base::beg);
    precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(in_buf.data()), 4);
    if (precomp_mgr.ctx->fin->gcount() != 4) break;
    auto cur_pos = original_input_pos;

    if (!read_with_memstream_buffer(*precomp_mgr.ctx->fin, memstream, reinterpret_cast<char*>(in_buf.data() + 4), 6, cur_pos)) break;
    cur_pos -= 2;
    precomp_mgr.ctx->fin->seekg(cur_pos, std::ios_base::beg);
    memstream->seekg(-2, std::ios_base::cur);

    idat_lengths[0] = (in_buf[0] << 24) + (in_buf[1] << 16) + (in_buf[2] << 8) + in_buf[3];

    if (idat_lengths[0] <= 2) break;
    // check zLib header and get windowbits
    zlib_header[0] = in_buf[8];
    zlib_header[1] = in_buf[9];
    if ((((in_buf[8] << 8) + in_buf[9]) % 31) == 0) {
      if ((in_buf[8] & 15) == 8) {
        if ((in_buf[9] & 32) == 0) { // FDICT must not be set
          //windowbits = (in_buf[8] >> 4) + 8;
          zlib_header_correct = true;
        }
      }
    }
    if (!zlib_header_correct) break;

    idat_count++;

    // go through additional IDATs
    for (;;) {
      precomp_mgr.ctx->fin->seekg(idat_lengths.back(), std::ios_base::cur);
      memstream->seekg(idat_lengths.back(), std::ios_base::cur);
      cur_pos += idat_lengths.back();
      if (!read_with_memstream_buffer(*precomp_mgr.ctx->fin, memstream, reinterpret_cast<char*>(in_buf.data()), 12, cur_pos)) { // 12 = CRC, length, "IDAT"
        idat_count = 0;
        idat_lengths.resize(1);
        break;
      }

      if (memcmp(in_buf.data() + 8, "IDAT", 4) == 0) {
        idat_crcs.push_back((in_buf[0] << 24) + (in_buf[1] << 16) + (in_buf[2] << 8) + in_buf[3]);
        idat_lengths.push_back((in_buf[4] << 24) + (in_buf[5] << 16) + (in_buf[6] << 8) + in_buf[7]);
        idat_count++;

        if (idat_lengths.size() > 65535) {
          idat_count = 0;
          idat_lengths.resize(1);
          break;
        }
      }
      else {
        break;
      }
    }
    break;
  }

  if (idat_count == 0) return std::unique_ptr<precompression_result>{};

  IStreamLike* png_input;
  uint64_t png_input_deflate_stream_pos;
  std::unique_ptr<IStreamLike> temp_png;
  if (idat_count == 1) {
    png_input = precomp_mgr.ctx->fin.get();
    png_input_deflate_stream_pos = deflate_stream_pos;
  }
  else {
    // copy to tempfile before trying to recompress
    std::string png_tmp_filename = precomp_mgr.get_tempfile_name("original_png");
    remove(png_tmp_filename.c_str());

    precomp_mgr.ctx->fin->seekg(deflate_stream_pos, std::ios_base::beg); // start after zLib header

    auto png_length = idat_lengths[0] - 2; // 2 = zLib header length
    for (int i = 1; i < idat_count; i++) {
      png_length += idat_lengths[i];
    }

    auto copy_to_temp = [&idat_lengths, &idat_count, &precomp_mgr](IStreamLike& src, OStreamLike& dst)  {
      idat_lengths[0] -= 2; // zLib header length
      for (int i = 0; i < idat_count; i++) {
        fast_copy(src, dst, idat_lengths[i]);
        src.seekg(12, std::ios_base::cur);
      }
      idat_lengths[0] += 2;
    };

    temp_png = make_temporary_stream(
      deflate_stream_pos, png_length, checkbuf, 
      *precomp_mgr.ctx->fin, original_input_pos, png_tmp_filename,
      copy_to_temp, 50 * 1024 * 1024  // 50mb
    );

    png_input = temp_png.get();
    png_input_deflate_stream_pos = 0;
  }

  auto result = try_decompression_png(precomp_mgr, *png_input, png_input_deflate_stream_pos, deflate_stream_pos, idat_count, idat_lengths, idat_crcs, zlib_header);

  result->original_size_extra = 6;  // add header length to the deflate stream size for full input size
  return result;
}

void recompress_png(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, DeflateFormatHeaderData& deflate_precomp_hdr_data, const PrecompFormatHandler::Tools& tools) {
  // restore IDAT
  ostream_printf(recompressed_stream, "IDAT");
  // Write zlib_header
  recompressed_stream.write(reinterpret_cast<char*>(deflate_precomp_hdr_data.stream_hdr.data()), deflate_precomp_hdr_data.stream_hdr.size());
  recompress_deflate(precompressed_input, recompressed_stream, deflate_precomp_hdr_data, tools.get_tempfile_name("recomp_png", true), "PNG", tools);
}

class OwnOStreamMultiPNG : public OutputStream {
public:
  OwnOStreamMultiPNG(OStreamLike* f, const size_t idat_count, const uint32_t* idat_crcs, const uint32_t* idat_lengths)
    : _f(f), _idat_count(idat_count), _idat_crcs(idat_crcs), _idat_lengths(idat_lengths), _idat_idx(0), _to_read(_idat_lengths[0]) {}

  size_t write(const unsigned char* buffer_, const size_t size_) override {
    if (_idat_idx >= _idat_count) {
      return 0;
    }
    size_t written = 0;
    size_t size = size_;
    const unsigned char* buffer = buffer_;
    while (size > _to_read) {
      _f->write(reinterpret_cast<char*>(const_cast<unsigned char*>(buffer)), _to_read);
      written += _to_read;
      size -= _to_read;
      buffer += _to_read;
      ++_idat_idx;
      if (_idat_idx >= _idat_count) {
        return written;
      }
      unsigned char crc_out[4] = { (unsigned char)(_idat_crcs[_idat_idx] >> 24), (unsigned char)(_idat_crcs[_idat_idx] >> 16), (unsigned char)(_idat_crcs[_idat_idx] >> 8), (unsigned char)(_idat_crcs[_idat_idx] >> 0) };
      _f->write(reinterpret_cast<char*>(crc_out), 4);
      _to_read = _idat_lengths[_idat_idx];
      unsigned char len_out[4] = { (unsigned char)(_to_read >> 24), (unsigned char)(_to_read >> 16), (unsigned char)(_to_read >> 8), (unsigned char)(_to_read >> 0) };
      _f->write(reinterpret_cast<char*>(len_out), 4);
      _f->write("IDAT", 4);
    }
    _f->write(reinterpret_cast<char*>(const_cast<unsigned char*>(buffer)), size);
    written += size;
    _to_read -= size;
    return written;
  }
private:
  OStreamLike* _f;
  const size_t _idat_count;
  const uint32_t* _idat_crcs;
  const uint32_t* _idat_lengths;
  size_t _idat_idx, _to_read;
};

class MultiPngFormatHeaderData: public DeflateFormatHeaderData {
public:
  long long idat_count;
  std::vector<unsigned int> idat_crcs;
  std::vector<unsigned int> idat_lengths;
};

bool try_reconstructing_deflate_multipng(IStreamLike& fin, OStreamLike& fout, const recompress_deflate_result& rdres, MultiPngFormatHeaderData& multipng_precomp_hdr_data, const std::function<void()>& progress_callback) {
  std::vector<unsigned char> unpacked_output;
  unpacked_output.resize(rdres.uncompressed_stream_size);
  fin.read(reinterpret_cast<char*>(unpacked_output.data()), rdres.uncompressed_stream_size);
  if ((int64_t)fin.gcount() != rdres.uncompressed_stream_size) {
    return false;
  }
  OwnOStreamMultiPNG os(&fout, multipng_precomp_hdr_data.idat_count, multipng_precomp_hdr_data.idat_crcs.data(), multipng_precomp_hdr_data.idat_lengths.data());
  return preflate_reencode(os, rdres.recon_data, unpacked_output, progress_callback);
}

void recompress_multipng(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, MultiPngFormatHeaderData& multipng_precomp_hdr_data, const PrecompFormatHandler::Tools& tools) {
  // restore first IDAT
  ostream_printf(recompressed_stream, "IDAT");
  // Write zlib_header
  recompressed_stream.write(reinterpret_cast<char*>(multipng_precomp_hdr_data.stream_hdr.data()), multipng_precomp_hdr_data.stream_hdr.size());

  debug_sums(precompressed_input, recompressed_stream, multipng_precomp_hdr_data.rdres);

  debug_deflate_reconstruct(multipng_precomp_hdr_data.rdres, "PNG multi", multipng_precomp_hdr_data.stream_hdr.size(), 0);

  if (!try_reconstructing_deflate_multipng(precompressed_input, recompressed_stream, multipng_precomp_hdr_data.rdres, multipng_precomp_hdr_data, tools.progress_callback)) {
    throw PrecompError(ERR_DURING_RECOMPRESSION);
  }
}

std::unique_ptr<PrecompFormatHeaderData> PngFormatHandler::read_format_header(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) {
  switch (precomp_hdr_format) {
  case D_PNG: {
    return read_deflate_format_header(*context.fin, *context.fout, precomp_hdr_flags, true);
  }
  case D_MULTIPNG: {
    auto fmt_hdr = std::make_unique<MultiPngFormatHeaderData>();
    unsigned hdr_length;
    fmt_hdr->stream_hdr.resize(CHUNK);
    fin_fget_deflate_hdr(*context.fin, fmt_hdr->rdres, precomp_hdr_flags, fmt_hdr->stream_hdr.data(), hdr_length, true);
    fmt_hdr->stream_hdr.resize(hdr_length);

    // get IDAT count
    fmt_hdr->idat_count = fin_fget_vlint(*context.fin) + 1;
        
    fmt_hdr->idat_crcs.resize(fmt_hdr->idat_count * sizeof(unsigned int));
    fmt_hdr->idat_lengths.resize(fmt_hdr->idat_count * sizeof(unsigned int));

    // get first IDAT length
    fmt_hdr->idat_lengths[0] = fin_fget_vlint(*context.fin) - 2; // zLib header length

    // get IDAT chunk lengths and CRCs
    for (int i = 1; i < fmt_hdr->idat_count; i++) {
      fmt_hdr->idat_crcs[i] = fin_fget32(*context.fin);
      fmt_hdr->idat_lengths[i] = fin_fget_vlint(*context.fin);
    }

    fin_fget_recon_data(*context.fin, fmt_hdr->rdres);
    return fmt_hdr;
  }
  default:
    throw PrecompError(ERR_DURING_RECOMPRESSION);
  }
}

void PngFormatHandler::recompress(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format, const Tools& tools) {
    switch (precomp_hdr_format) {
    case D_PNG: {
        auto& deflate_precomp_hdr_data = static_cast<DeflateFormatHeaderData&>(precomp_hdr_data);
        recompress_png(precompressed_input, recompressed_stream, deflate_precomp_hdr_data, tools);
        break;
    }
    case D_MULTIPNG: {
        auto& multipng_precomp_hdr_data = static_cast<MultiPngFormatHeaderData&>(precomp_hdr_data);
        recompress_multipng(precompressed_input, recompressed_stream, multipng_precomp_hdr_data, tools);
        break;
    }
    default:
        throw PrecompError(ERR_DURING_RECOMPRESSION);
    }
}
