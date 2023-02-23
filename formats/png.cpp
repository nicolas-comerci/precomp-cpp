#include "png.h"

#include <memory>

#include "contrib/preflate/preflate.h"

bool png_header_check(unsigned char* checkbuf) {
  return memcmp(checkbuf, "IDAT", 4) == 0;
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
  return original_size + idat_add_offset - 1;
}

void png_precompression_result::dump_to_outfile(Precomp& precomp_mgr) {
  dump_header_to_outfile(precomp_mgr);
  dump_penaltybytes_to_outfile(precomp_mgr);
  dump_idat_to_outfile(precomp_mgr);
  dump_recon_data_to_outfile(precomp_mgr);
  dump_stream_sizes_to_outfile(precomp_mgr);
  dump_precompressed_data_to_outfile(precomp_mgr);
}

png_precompression_result try_decompression_png_multi(Precomp& precomp_mgr, IStreamLike& fpng, int idat_count,
  std::vector<unsigned int>& idat_lengths, std::vector<unsigned int>& idat_crcs, std::array<unsigned char, 2>& zlib_header) {
  png_precompression_result result;
  init_decompression_variables(*precomp_mgr.ctx);

  std::unique_ptr<PrecompTmpFile> tmpfile = std::make_unique<PrecompTmpFile>();
  tmpfile->open(temp_files_tag() + "_precompressed_png", std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);

  // try to decompress at current position
  recompress_deflate_result rdres = try_recompression_deflate(precomp_mgr, fpng, *tmpfile);

  result.original_size = rdres.compressed_stream_size;
  result.precompressed_size = rdres.uncompressed_stream_size;
  if (result.precompressed_size > 0) { // seems to be a zLib-Stream

    precomp_mgr.statistics.decompressed_streams_count++;
    if (idat_count > 1)
    {
      precomp_mgr.statistics.decompressed_png_multi_count++;
    }
    else
    {
      precomp_mgr.statistics.decompressed_png_count++;
    }

    debug_deflate_detected(*precomp_mgr.ctx, rdres, "in PNG");

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

      debug_sums(precomp_mgr, rdres);

      result.success = true;
      result.format = idat_count > 1 ? D_MULTIPNG : D_PNG;

      debug_pos(precomp_mgr);

      result.idat_count = idat_count;
      result.idat_lengths = std::move(idat_lengths);
      result.idat_crcs = std::move(idat_crcs);

      if (rdres.uncompressed_stream_size)
      {
        auto decomp_io_buf_ptr = precomp_mgr.ctx->decomp_io_buf.data();
        auto memstream = memiostream::make_copy(decomp_io_buf_ptr, decomp_io_buf_ptr + rdres.uncompressed_stream_size);
        result.precompressed_stream = std::move(memstream);
      }
      else {
        tmpfile->reopen();
        result.precompressed_stream = std::move(tmpfile);
      }

      result.rdres = std::move(rdres);
      std::copy(zlib_header.begin(), zlib_header.end(), std::back_inserter(result.zlib_header));
      result.inc_last_hdr_byte = true;

      debug_pos(precomp_mgr);
    }
    else {
      if (intense_mode_is_active(precomp_mgr)) precomp_mgr.ctx->intense_ignore_offsets.insert(precomp_mgr.ctx->input_file_pos - 2);
      if (brute_mode_is_active(precomp_mgr)) precomp_mgr.ctx->brute_ignore_offsets.insert(precomp_mgr.ctx->input_file_pos);
      print_to_log(PRECOMP_DEBUG_LOG, "No matches\n");
    }
  }
  return result;
}

png_precompression_result precompress_png(Precomp& precomp_mgr) {
  // space for length and crc parts of IDAT chunks
  std::vector<unsigned int> idat_lengths;
  idat_lengths.reserve(100 * sizeof(unsigned int));
  std::vector<unsigned int> idat_crcs;
  idat_crcs.reserve(100 * sizeof(unsigned int));

  precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
  precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

  int idat_count = 0;
  bool zlib_header_correct = false;

  std::array<unsigned char, 2> zlib_header{};

  // get preceding length bytes
  if (precomp_mgr.ctx->input_file_pos >= 4) {
    precomp_mgr.ctx->fin->seekg(precomp_mgr.ctx->input_file_pos - 4, std::ios_base::beg);

    precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 10);
    if (precomp_mgr.ctx->fin->gcount() == 10) {
      precomp_mgr.ctx->fin->seekg((long long)precomp_mgr.ctx->fin->tellg() - 2, std::ios_base::beg);

      idat_lengths[0] = (precomp_mgr.in[0] << 24) + (precomp_mgr.in[1] << 16) + (precomp_mgr.in[2] << 8) + precomp_mgr.in[3];

      if (idat_lengths[0] > 2) {
        // check zLib header and get windowbits
        zlib_header[0] = precomp_mgr.in[8];
        zlib_header[1] = precomp_mgr.in[9];
        if ((((precomp_mgr.in[8] << 8) + precomp_mgr.in[9]) % 31) == 0) {
          if ((precomp_mgr.in[8] & 15) == 8) {
            if ((precomp_mgr.in[9] & 32) == 0) { // FDICT must not be set
              //windowbits = (precomp_mgr.in[8] >> 4) + 8;
              zlib_header_correct = true;
            }
          }
        }

        if (zlib_header_correct) {

          idat_count++;

          // go through additional IDATs
          for (;;) {
            precomp_mgr.ctx->fin->seekg((long long)precomp_mgr.ctx->fin->tellg() + idat_lengths[idat_count - 1], std::ios_base::beg);
            precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 12);
            if (precomp_mgr.ctx->fin->gcount() != 12) { // CRC, length, "IDAT"
              idat_count = 0;
              break;
            }

            if (memcmp(precomp_mgr.in + 8, "IDAT", 4) == 0) {
              idat_crcs[idat_count] = (precomp_mgr.in[0] << 24) + (precomp_mgr.in[1] << 16) + (precomp_mgr.in[2] << 8) + precomp_mgr.in[3];
              idat_lengths[idat_count] = (precomp_mgr.in[4] << 24) + (precomp_mgr.in[5] << 16) + (precomp_mgr.in[6] << 8) + precomp_mgr.in[7];
              idat_count++;

              if ((idat_count % 100) == 0) {
                idat_lengths.reserve((idat_count + 100) * sizeof(unsigned int));
                idat_crcs.reserve((idat_count + 100) * sizeof(unsigned int));
              }

              if (idat_count > 65535) {
                idat_count = 0;
                break;
              }
            }
            else {
              break;
            }
          }
        }
      }
    }
  }

  // copy to tempfile before trying to recompress
  std::string png_tmp_filename = temp_files_tag() + "_original_png";
  remove(png_tmp_filename.c_str());
  PrecompTmpFile tmp_png;
  tmp_png.open(png_tmp_filename, std::ios_base::in | std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);

  precomp_mgr.ctx->fin->seekg(precomp_mgr.ctx->input_file_pos + 6, std::ios_base::beg); // start after zLib header

  idat_lengths[0] -= 2; // zLib header length
  for (int i = 0; i < idat_count; i++) {
    fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, tmp_png, idat_lengths[i]);
    precomp_mgr.ctx->fin->seekg((long long)precomp_mgr.ctx->fin->tellg() + 12, std::ios_base::beg);
  }
  idat_lengths[0] += 2;

  precomp_mgr.ctx->input_file_pos += 6;
  auto result = try_decompression_png_multi(precomp_mgr, tmp_png, idat_count, idat_lengths, idat_crcs, zlib_header);
  precomp_mgr.ctx->cb += 6;

  return result;
}

void recompress_png(Precomp& precomp_mgr, unsigned char precomp_hdr_flags) {
  // restore IDAT
  ostream_printf(*precomp_mgr.ctx->fout, "IDAT");
  recompress_deflate(precomp_mgr, precomp_hdr_flags, true, temp_files_tag() + "_recomp_png", "PNG");
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

void recompress_multipng(Precomp& precomp_mgr, unsigned char precomp_hdr_flags) {
  // restore first IDAT
  ostream_printf(*precomp_mgr.ctx->fout, "IDAT");

  recompress_deflate_result rdres;
  unsigned hdr_length;
  fin_fget_deflate_hdr(*precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, rdres, precomp_hdr_flags, precomp_mgr.in, hdr_length, true);

  // get IDAT count
  auto idat_count = fin_fget_vlint(*precomp_mgr.ctx->fin) + 1;

  std::vector<unsigned int> idat_crcs;
  idat_crcs.reserve(idat_count * sizeof(unsigned int));
  std::vector<unsigned int> idat_lengths;
  idat_lengths.reserve(idat_count * sizeof(unsigned int));

  // get first IDAT length
  idat_lengths[0] = fin_fget_vlint(*precomp_mgr.ctx->fin) - 2; // zLib header length

  // get IDAT chunk lengths and CRCs
  for (int i = 1; i < idat_count; i++) {
    idat_crcs[i] = fin_fget32(*precomp_mgr.ctx->fin);
    idat_lengths[i] = fin_fget_vlint(*precomp_mgr.ctx->fin);
  }

  fin_fget_recon_data(*precomp_mgr.ctx->fin, rdres);
  debug_sums(precomp_mgr, rdres);
  debug_pos(precomp_mgr);

  debug_deflate_reconstruct(rdres, "PNG multi", hdr_length, 0);

  if (!try_reconstructing_deflate_multipng(precomp_mgr, *precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, rdres, idat_count, idat_crcs.data(), idat_lengths.data())) {
    throw PrecompError(ERR_DURING_RECOMPRESSION);
  }
}
