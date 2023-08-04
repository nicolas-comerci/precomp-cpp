#include "png.h"
#include "formats/deflate.h"

#include <memory>

#include "contrib/preflate/preflate.h"

class png_precompression_result : public deflate_precompression_result {
protected:
    void dump_idat_to_outfile(Precomp& precomp_mgr);
public:
    std::vector<unsigned int> idat_crcs;
    std::vector<unsigned int> idat_lengths;
    int idat_count;
    unsigned int idat_pairs_written_count = 0;

    png_precompression_result();

    long long input_pos_add_offset() override;

    void dump_to_outfile(Precomp& precomp_mgr) override;
};

bool PngFormatHandler::quick_check(std::span<unsigned char> checkbuf) {
  return memcmp(checkbuf.data(), "IDAT", 4) == 0;
}

png_precompression_result::png_precompression_result() : deflate_precompression_result(D_PNG) {}

void png_precompression_result::dump_idat_to_outfile(Precomp & precomp_mgr) {
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
  // store IDAT pairs count
  fout_fput_vlint(*precomp_mgr.ctx->fout, idat_pairs_written_count);

  // store IDAT CRCs and lengths
  fout_fput_vlint(*precomp_mgr.ctx->fout, idat_lengths[0]);

  // store IDAT CRCs and lengths
  i = 1;
  idat_pos = idat_lengths[0] - 2;
  idat_pairs_written_count = 0;
  if (idat_pos < original_size) {
    do {
      fout_fput32(*precomp_mgr.ctx->fout, idat_crcs[i]);
      fout_fput_vlint(*precomp_mgr.ctx->fout, idat_lengths[i]);

      idat_pairs_written_count++;

      idat_pos += idat_lengths[i];
      if (idat_pos >= original_size) break;

      i++;
    } while (i < idat_count);
  }
}

long long png_precompression_result::input_pos_add_offset() {
  unsigned int idat_add_offset = 0;
  if (format == D_MULTIPNG) {
    // add IDAT chunk overhead
    idat_add_offset += idat_pairs_written_count * 12;
  }
  return deflate_precompression_result::input_pos_add_offset() + idat_add_offset;
}

void png_precompression_result::dump_to_outfile(Precomp& precomp_mgr) {
  dump_header_to_outfile(precomp_mgr);
  dump_penaltybytes_to_outfile(precomp_mgr);
  dump_idat_to_outfile(precomp_mgr);
  dump_recon_data_to_outfile(precomp_mgr);
  dump_stream_sizes_to_outfile(precomp_mgr);
  dump_precompressed_data_to_outfile(precomp_mgr);
}

std::unique_ptr<precompression_result> try_decompression_png_multi(Precomp& precomp_mgr, IStreamLike& fpng, long long fpng_deflate_stream_pos, long long deflate_stream_original_pos, int idat_count,
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

      debug_sums(*precomp_mgr.ctx, rdres);

      result->success = true;
      result->format = idat_count > 1 ? D_MULTIPNG : D_PNG;

      debug_pos(*precomp_mgr.ctx);

      result->idat_count = idat_count;
      result->idat_lengths = std::move(idat_lengths);
      result->idat_crcs = std::move(idat_crcs);

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

      debug_pos(*precomp_mgr.ctx);
    }
    else {
      if (intense_mode_is_active(precomp_mgr)) precomp_mgr.ctx->intense_ignore_offsets.insert(deflate_stream_original_pos - 2);
      if (brute_mode_is_active(precomp_mgr)) precomp_mgr.ctx->brute_ignore_offsets.insert(deflate_stream_original_pos);
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

  std::unique_ptr<IStreamLike> temp_png = make_temporary_stream(
    deflate_stream_pos, png_length, checkbuf, 
    *precomp_mgr.ctx->fin, original_input_pos, png_tmp_filename,
    copy_to_temp, 50 * 1024 * 1024  // 50mb
  );

  auto result = try_decompression_png_multi(precomp_mgr, *temp_png, 0, deflate_stream_pos, idat_count, idat_lengths, idat_crcs, zlib_header);

  result->input_pos_extra_add = 6;  // add header length to the deflate stream size for full input size
  return result;
}

void recompress_png(RecursionContext& context, std::byte precomp_hdr_flags) {
  // restore IDAT
  ostream_printf(*context.fout, "IDAT");
  recompress_deflate(context, precomp_hdr_flags, true, context.precomp.get_tempfile_name("recomp_png"), "PNG");
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

bool try_reconstructing_deflate_multipng(Precomp& precomp_mgr, IStreamLike& fin, OStreamLike& fout, const recompress_deflate_result& rdres,
  const size_t idat_count, const uint32_t* idat_crcs, const uint32_t* idat_lengths) {
  std::vector<unsigned char> unpacked_output;
  unpacked_output.resize(rdres.uncompressed_stream_size);
  fin.read(reinterpret_cast<char*>(unpacked_output.data()), rdres.uncompressed_stream_size);
  if ((int64_t)fin.gcount() != rdres.uncompressed_stream_size) {
    return false;
  }
  OwnOStreamMultiPNG os(&fout, idat_count, idat_crcs, idat_lengths);
  return preflate_reencode(os, rdres.recon_data, unpacked_output, [&precomp_mgr]() { precomp_mgr.call_progress_callback(); });
}

void recompress_multipng(RecursionContext& context, std::byte precomp_hdr_flags) {
  // restore first IDAT
  ostream_printf(*context.fout, "IDAT");

  recompress_deflate_result rdres;
  unsigned hdr_length;
  std::vector<unsigned char> in_buf{};
  in_buf.resize(CHUNK);
  fin_fget_deflate_hdr(*context.fin, *context.fout, rdres, precomp_hdr_flags, in_buf.data(), hdr_length, true);

  // get IDAT count
  auto idat_count = fin_fget_vlint(*context.fin) + 1;

  std::vector<unsigned int> idat_crcs;
  idat_crcs.resize(idat_count * sizeof(unsigned int));
  std::vector<unsigned int> idat_lengths;
  idat_lengths.resize(idat_count * sizeof(unsigned int));

  // get first IDAT length
  idat_lengths[0] = fin_fget_vlint(*context.fin) - 2; // zLib header length

  // get IDAT chunk lengths and CRCs
  for (int i = 1; i < idat_count; i++) {
    idat_crcs[i] = fin_fget32(*context.fin);
    idat_lengths[i] = fin_fget_vlint(*context.fin);
  }

  fin_fget_recon_data(*context.fin, rdres);
  debug_sums(context, rdres);
  debug_pos(context);

  debug_deflate_reconstruct(rdres, "PNG multi", hdr_length, 0);

  if (!try_reconstructing_deflate_multipng(context.precomp, *context.fin, *context.fout, rdres, idat_count, idat_crcs.data(), idat_lengths.data())) {
    throw PrecompError(ERR_DURING_RECOMPRESSION);
  }
}

void PngFormatHandler::recompress(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) {
    switch (precomp_hdr_format) {
    case D_PNG: {
        recompress_png(context, precomp_hdr_flags);
        break;
    }
    case D_MULTIPNG: {
        recompress_multipng(context, precomp_hdr_flags);
        break;
    }
    default:
        throw PrecompError(ERR_DURING_RECOMPRESSION);
    }
}
