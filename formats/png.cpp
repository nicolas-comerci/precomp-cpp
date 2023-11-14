#include "png.h"
#include "formats/deflate.h"

#include <memory>

#include "contrib/preflate/preflate.h"

class png_precompression_result : public deflate_precompression_result {
protected:
    void dump_idat_to_outfile(OStreamLike& outfile) const {
        // store IDAT CRCs and lengths
        fout_fput_vlint(outfile, idat_lengths[0] - 2); // zLib header length
        if (format != D_MULTIPNG) {
          // Apart from the length we need any trailing data after the deflate stream on IDAT data, AND CRC data for the PNG chunk
          fout_fput_vlint(outfile, trailing_data.size());
          outfile.write(reinterpret_cast<const char*>(trailing_data.data()), trailing_data.size());
          return;
        }

        // store IDAT pairs count
        fout_fput_vlint(outfile, idat_pairs_written_count);

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
    // for single IDAT only, any trailing data after the deflate data on IDAT data, INCLUDING CRC data for the PNG chunk
    std::vector<std::byte> trailing_data;

    std::vector<unsigned int> idat_crcs;
    std::vector<unsigned int> idat_lengths;
    int idat_count;
    unsigned int idat_pairs_written_count = 0;

    explicit png_precompression_result(Tools* _tools) : deflate_precompression_result(D_PNG, _tools) {}

    long long complete_original_size() const override {
        // 4 bytes for the first IDAT Length, needed regardless of single or multi png
        unsigned int idat_add_offset = 4;
        if (format == D_MULTIPNG) {
            // add IDAT chunk overhead
            idat_add_offset += idat_pairs_written_count * 12;
        }
        else {
          idat_add_offset += 4;  // IDAT Magic
          idat_add_offset += 2;  // ZLIB Header
          idat_add_offset += trailing_data.size();  // any trailing IDAT data after deflate + single IDAT CRC size
          // deflate stream size added by deflate_precompression_result::complete_original_size()
        }
        return deflate_precompression_result::complete_original_size() + idat_add_offset;
    }

    void dump_to_outfile(OStreamLike& outfile) const override {
        dump_header_to_outfile(outfile);
        dump_penaltybytes_to_outfile(outfile);
        dump_recon_data_to_outfile(outfile);
        dump_stream_sizes_to_outfile(outfile);
        dump_idat_to_outfile(outfile);
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

    void increase_detected_count() override { tools->increase_detected_count(format == D_MULTIPNG ? "PNG (multi)" : "PNG"); }
    void increase_precompressed_count() override { tools->increase_precompressed_count(format == D_MULTIPNG ? "PNG (multi)" : "PNG"); }
};

bool PngFormatHandler::quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) {
  return memcmp(buffer.data() + 4, "IDAT", 4) == 0;
}

std::unique_ptr<precompression_result> try_decompression_png(Tools& precomp_tools, IStreamLike& input, OStreamLike& output, IStreamLike& fpng, long long fpng_deflate_stream_pos, long long deflate_stream_original_pos, int idat_count,
  std::vector<unsigned int>& idat_lengths, std::vector<unsigned int>& idat_crcs, std::array<unsigned char, 2>& zlib_header) {
  std::unique_ptr<png_precompression_result> result = std::make_unique<png_precompression_result>(&precomp_tools);

  std::unique_ptr<PrecompTmpFile> tmpfile = std::make_unique<PrecompTmpFile>();
  tmpfile->open(precomp_tools.get_tempfile_name("precompressed_png", true), std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);

  // try to decompress at current position
  recompress_deflate_result rdres = try_recompression_deflate(precomp_tools, fpng, fpng_deflate_stream_pos, *tmpfile);

  result->original_size = rdres.compressed_stream_size;
  result->precompressed_size = rdres.uncompressed_stream_size;
  if (result->precompressed_size > 0) { // seems to be a zLib-Stream
    debug_deflate_detected(rdres, "in PNG", deflate_stream_original_pos);

    if (rdres.accepted) {
      debug_sums(input, output, rdres);

      result->success = true;
      result->format = idat_count > 1 ? D_MULTIPNG : D_PNG;
      result->flags |= idat_count > 1 ? std::byte{ 0b01000000 } : std::byte{ 0b0 };

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
      precomp_tools.add_ignore_offset(D_RAW, deflate_stream_original_pos - 2);
      precomp_tools.add_ignore_offset(D_BRUTE, deflate_stream_original_pos);
      print_to_log(PRECOMP_DEBUG_LOG, "No matches\n");
    }
  }
  return result;
}

std::unique_ptr<precompression_result>
PngFormatHandler::attempt_precompression(IStreamLike &input, OStreamLike &output, std::span<unsigned char> checkbuf,
                                         long long original_input_pos, const Switches &precomp_switches) {
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

  const long long idat_magic_pos = original_input_pos + 4;
  auto deflate_stream_pos = idat_magic_pos + 6;

  auto checkbuf_at_idat = std::span(checkbuf.data() + 4, checkbuf.size() - 4);
  auto memstream = memiostream::make(checkbuf_at_idat.data(), checkbuf_at_idat.data() + checkbuf_at_idat.size());

  // get preceding length bytes
  std::array<unsigned char, 12> in_buf{};
  while (true) {
    input.seekg(idat_magic_pos - 4, std::ios_base::beg);
    input.read(reinterpret_cast<char*>(in_buf.data()), 4);
    if (input.gcount() != 4) break;
    auto cur_pos = idat_magic_pos;

    if (!read_with_memstream_buffer(input, memstream, reinterpret_cast<char*>(in_buf.data() + 4), 6, cur_pos)) break;
    cur_pos -= 2;
    input.seekg(cur_pos, std::ios_base::beg);
    memstream->seekg(-2, std::ios_base::cur);

    idat_lengths[0] = (in_buf[0] << 24) + (in_buf[1] << 16) + (in_buf[2] << 8) + in_buf[3];

    if (idat_lengths[0] <= 2) break;
    // check zLib header and get windowbits
    zlib_header[0] = in_buf[8];
    zlib_header[1] = in_buf[9];
    if ((((zlib_header[0] << 8) + zlib_header[1]) % 31) == 0) {
      if ((zlib_header[0] & 15) == 8) {
        if ((zlib_header[1] & 32) == 0) { // FDICT must not be set
          //windowbits = (zlib_header[0] >> 4) + 8;
          zlib_header_correct = true;
        }
      }
    }
    if (!zlib_header_correct) break;

    // go through additional IDATs
    for (;;) {
      input.seekg(idat_lengths.back(), std::ios_base::cur);
      memstream->seekg(idat_lengths.back(), std::ios_base::cur);
      cur_pos += idat_lengths.back();

      // Read 4bytes (CRC32) of PREVIOUS! PNG chunk
      if (!read_with_memstream_buffer(input, memstream, reinterpret_cast<char*>(in_buf.data()), 4, cur_pos)) {
        if (idat_count > 0) {
          // We had started reading a PNG IDAT Chunk but it appears its not complete, we will stop at the previous IDAT chunk
          idat_lengths.pop_back();
        }
        break;
      }
      // TODO: maybe we don't need to store CRC32 at all? We could recalculate it
      idat_crcs.push_back((in_buf[0] << 24) + (in_buf[1] << 16) + (in_buf[2] << 8) + in_buf[3]);

      // At this point we have a full PNG IDAT Chunk
      idat_count++;

      // 8 = 4 bytes length, 4 bytes "IDAT"
      if (!read_with_memstream_buffer(input, memstream, reinterpret_cast<char*>(in_buf.data()), 8, cur_pos)) {
        // If we reached EOF, no problem, we will just process as many IDAT chunks as we were able to find so far
        break;
      }

      if (memcmp(in_buf.data() + 4, "IDAT", 4) == 0) {
        idat_lengths.push_back((in_buf[0] << 24) + (in_buf[1] << 16) + (in_buf[2] << 8) + in_buf[3]);

        // WTF is this check supposed to protect us from? Like, what would ever realistically trigger it?
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
    png_input = &input;
    png_input_deflate_stream_pos = deflate_stream_pos;
  }
  else {
    // copy to tempfile before trying to recompress
    std::string png_tmp_filename = precomp_tools->get_tempfile_name("original_png", true);
    remove(png_tmp_filename.c_str());

    input.seekg(deflate_stream_pos, std::ios_base::beg); // start after zLib header

    auto png_length = idat_lengths[0] - 2; // 2 = zLib header length
    for (int i = 1; i < idat_count; i++) {
      png_length += idat_lengths[i];
    }

    auto copy_to_temp = [&idat_lengths, &idat_count](IStreamLike& src, OStreamLike& dst)  {
      idat_lengths[0] -= 2; // zLib header length
      for (int i = 0; i < idat_count; i++) {
        fast_copy(src, dst, idat_lengths[i]);
        src.seekg(12, std::ios_base::cur);
      }
      idat_lengths[0] += 2;
    };

    temp_png = make_temporary_stream(
      deflate_stream_pos, png_length, checkbuf_at_idat, 
      input, idat_magic_pos, png_tmp_filename,
      copy_to_temp, 50 * 1024 * 1024  // 50mb
    );

    png_input = temp_png.get();
    png_input_deflate_stream_pos = 0;
  }

  auto result = try_decompression_png(*precomp_tools, input, output, *png_input, png_input_deflate_stream_pos, deflate_stream_pos, idat_count, idat_lengths, idat_crcs, zlib_header);

  if (result->format == D_MULTIPNG) {
    result->original_size_extra = 6;  // add header length to the deflate stream size for full input size
  }
  else if (result->success) {
    auto png_result = static_cast<png_precompression_result*>(result.get());
    // check if there is trailing_data after the deflate stream on the IDAT data
    const auto deflate_and_hdr_size = png_result->original_size + 2;
    if (deflate_and_hdr_size < png_result->idat_lengths[0]) {
      // The whole PNG IDAT chunk layout
      // 4 Len | 4 IDAT | deflate_and_hdr_size DEFLATE STREAM with Zlib header | Trailing data | 4 bytes CRC
      input.seekg(original_input_pos + 4 + 4 + deflate_and_hdr_size, std::ios_base::beg);
      const auto trailing_data_size = png_result->idat_lengths[0] - deflate_and_hdr_size;
      std::vector<std::byte> trailing_data;
      trailing_data.resize(trailing_data_size);
      input.read(reinterpret_cast<char*>(trailing_data.data()), trailing_data_size);
      for (const auto& byte : trailing_data) {
        png_result->trailing_data.push_back(byte);
      }
    }
    png_result->trailing_data.push_back(static_cast<std::byte>((png_result->idat_crcs[1] >> 24) % 256));
    png_result->trailing_data.push_back(static_cast<std::byte>((png_result->idat_crcs[1] >> 16) % 256));
    png_result->trailing_data.push_back(static_cast<std::byte>((png_result->idat_crcs[1] >> 8) % 256));
    png_result->trailing_data.push_back(static_cast<std::byte>(png_result->idat_crcs[1] % 256));
  }
  return result;
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
  long long idat_count = 0;
  std::vector<unsigned int> idat_crcs;
  std::vector<unsigned int> idat_lengths;

  // only used for single IDAT, includes PNG CRC
  std::vector<std::byte> trailing_data;
};

void recompress_png(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, MultiPngFormatHeaderData& png_precomp_hdr_data, const Tools& tools) {
  // restore Chunk Length
  const auto idat_len = png_precomp_hdr_data.idat_lengths[0] + 2; // 2byte Zlib header not included
  unsigned char len_out[4] = { (unsigned char)(idat_len >> 24), (unsigned char)(idat_len >> 16), (unsigned char)(idat_len >> 8), (unsigned char)(idat_len >> 0) };
  recompressed_stream.write(reinterpret_cast<char*>(len_out), 4);
  // restore IDAT
  ostream_printf(recompressed_stream, "IDAT");
  // Write zlib_header
  recompressed_stream.write(reinterpret_cast<char*>(png_precomp_hdr_data.stream_hdr.data()), png_precomp_hdr_data.stream_hdr.size());
  // Write recompressed deflate stream
  recompress_deflate(precompressed_input, recompressed_stream, png_precomp_hdr_data, tools.get_tempfile_name("recomp_png", true), "PNG", tools);
  // Write any trailing data on IDAT data after deflate stream and the PNG Chunk CRC
  recompressed_stream.write(reinterpret_cast<char*>(png_precomp_hdr_data.trailing_data.data()), png_precomp_hdr_data.trailing_data.size());
}

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

void recompress_multipng(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, MultiPngFormatHeaderData& multipng_precomp_hdr_data, const Tools& tools) {
  // restore Chunk Length
  const auto idat_len = multipng_precomp_hdr_data.idat_lengths[0] + 2;
  unsigned char len_out[4] = { (unsigned char)(idat_len >> 24), (unsigned char)(idat_len >> 16), (unsigned char)(idat_len >> 8), (unsigned char)(idat_len >> 0) };
  recompressed_stream.write(reinterpret_cast<char*>(len_out), 4);
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

std::unique_ptr<PrecompFormatHeaderData> PngFormatHandler::read_format_header(IStreamLike &input, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) {
  const bool is_multi_idat = (precomp_hdr_flags & std::byte{ 0b01000000 }) == std::byte{ 0b01000000 };
  auto fmt_hdr = std::make_unique<MultiPngFormatHeaderData>();
  fmt_hdr->read_data(input, precomp_hdr_flags, true);

  // get first IDAT length
  fmt_hdr->idat_lengths.push_back(fin_fget_vlint(input));
  if (!is_multi_idat) {
    fmt_hdr->idat_count = 1;
    const auto trailing_data_size = fin_fget_vlint(input);
    fmt_hdr->trailing_data.resize(trailing_data_size);
    input.read(reinterpret_cast<char*>(fmt_hdr->trailing_data.data()), trailing_data_size);
  }
  else {
    // get IDAT count
    fmt_hdr->idat_count = fin_fget_vlint(input) + 1;
    if (is_multi_idat) {
      fmt_hdr->idat_crcs.resize(fmt_hdr->idat_count * sizeof(unsigned int));
      fmt_hdr->idat_lengths.resize(fmt_hdr->idat_count * sizeof(unsigned int));

      // get IDAT chunk lengths and CRCs
      for (int i = 1; i < fmt_hdr->idat_count; i++) {
        fmt_hdr->idat_crcs[i] = fin_fget32(input);
        fmt_hdr->idat_lengths[i] = fin_fget_vlint(input);
      }
    }
  }

  return fmt_hdr;
}

void PngFormatHandler::recompress(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format) {
    auto& multipng_precomp_hdr_data = static_cast<MultiPngFormatHeaderData&>(precomp_hdr_data);
    if (multipng_precomp_hdr_data.idat_count == 1) {
      recompress_png(precompressed_input, recompressed_stream, multipng_precomp_hdr_data, *precomp_tools);
    }
    else {
      recompress_multipng(precompressed_input, recompressed_stream, multipng_precomp_hdr_data, *precomp_tools);
    }
}
