#include "pdf.h"

#include <cstddef>
#include <cstring>
#include <memory>

bool pdf_header_check(std::span<unsigned char> checkbuf_span) {
  return memcmp(checkbuf_span.data(), "/FlateDecode", 12) == 0;
}

pdf_precompression_result::pdf_precompression_result(unsigned int img_width, unsigned int img_height)
  : deflate_precompression_result(D_PDF), img_width(img_width), img_height(img_height) {}

void pdf_precompression_result::dump_precompressed_data_to_outfile(Precomp& precomp_mgr) {
  bool must_pad_bmp = false;
  if ((bmp_header_type != BMP_HEADER_NONE) && ((img_width % 4) != 0)) {
    must_pad_bmp = true;
  }
  if (!must_pad_bmp) {
    deflate_precompression_result::dump_precompressed_data_to_outfile(precomp_mgr);
  }
  else {
    for (int y = 0; y < img_height; y++) {
      fast_copy(precomp_mgr, *precompressed_stream, *precomp_mgr.ctx->fout, img_width);

      for (int i = 0; i < (4 - (img_width % 4)); i++) {
        precomp_mgr.ctx->fout->put(0);
      }
    }
  }
}

void pdf_precompression_result::dump_bmp_hdr_to_outfile(Precomp& precomp_mgr) {
  if (bmp_header_type == BMP_HEADER_NONE) return;
  int i;

  precomp_mgr.ctx->fout->put('B');
  precomp_mgr.ctx->fout->put('M');
  // BMP size in bytes
  unsigned int bmp_size = ((img_width + 3) & -4) * img_height;
  if (bmp_header_type == BMP_HEADER_24BPP) bmp_size *= 3;
  if (bmp_header_type == BMP_HEADER_8BPP) {
    bmp_size += 54 + 1024;
  }
  else {
    bmp_size += 54;
  }
  fout_fput32_little_endian(*precomp_mgr.ctx->fout, bmp_size);

  for (i = 0; i < 4; i++) {
    precomp_mgr.ctx->fout->put(0);
  }
  precomp_mgr.ctx->fout->put(54);
  if (bmp_header_type == BMP_HEADER_8BPP) {
    precomp_mgr.ctx->fout->put(4);
  }
  else {
    precomp_mgr.ctx->fout->put(0);
  }
  precomp_mgr.ctx->fout->put(0);
  precomp_mgr.ctx->fout->put(0);
  precomp_mgr.ctx->fout->put(40);
  precomp_mgr.ctx->fout->put(0);
  precomp_mgr.ctx->fout->put(0);
  precomp_mgr.ctx->fout->put(0);

  fout_fput32_little_endian(*precomp_mgr.ctx->fout, img_width);
  fout_fput32_little_endian(*precomp_mgr.ctx->fout, img_height);

  precomp_mgr.ctx->fout->put(1);
  precomp_mgr.ctx->fout->put(0);

  if (bmp_header_type == BMP_HEADER_8BPP) {
    precomp_mgr.ctx->fout->put(8);
  }
  else {
    precomp_mgr.ctx->fout->put(24);
  }
  precomp_mgr.ctx->fout->put(0);

  for (i = 0; i < 4; i++) {
    precomp_mgr.ctx->fout->put(0);
  }

  if (bmp_header_type == BMP_HEADER_24BPP)  img_width *= 3;

  auto datasize = ((img_width + 3) & -4) * img_height;
  if (bmp_header_type == BMP_HEADER_24BPP) datasize *= 3;
  fout_fput32_little_endian(*precomp_mgr.ctx->fout, datasize);

  for (i = 0; i < 16; i++) {
    precomp_mgr.ctx->fout->put(0);
  }

  if (bmp_header_type == BMP_HEADER_8BPP) {
    // write BMP palette
    for (i = 0; i < 1024; i++) {
      precomp_mgr.ctx->fout->put(0);
    }
  }
}

void pdf_precompression_result::dump_to_outfile(Precomp& precomp_mgr) {
  dump_header_to_outfile(precomp_mgr);
  dump_penaltybytes_to_outfile(precomp_mgr);
  dump_recon_data_to_outfile(precomp_mgr);
  dump_stream_sizes_to_outfile(precomp_mgr);
  dump_bmp_hdr_to_outfile(precomp_mgr);
  dump_precompressed_data_to_outfile(precomp_mgr);
}

pdf_precompression_result try_decompression_pdf(Precomp& precomp_mgr, unsigned char* checkbuf, unsigned int pdf_header_length, unsigned int img_width, unsigned int img_height, int img_bpc) {
  pdf_precompression_result result = pdf_precompression_result(img_width, img_height);
  precomp_mgr.ctx->input_file_pos += pdf_header_length; // skip PDF part

  std::unique_ptr<PrecompTmpFile> tmpfile = std::make_unique<PrecompTmpFile>();
  tmpfile->open(temp_files_tag() + "_decomp_pdf", std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
  init_decompression_variables(*precomp_mgr.ctx);

  // try to decompress at current position
  recompress_deflate_result rdres = try_recompression_deflate(precomp_mgr, *precomp_mgr.ctx->fin, *tmpfile);

  if (rdres.uncompressed_stream_size > 0) { // seems to be a zLib-Stream

    precomp_mgr.statistics.decompressed_streams_count++;
    if (img_bpc == 8) {
      precomp_mgr.statistics.decompressed_pdf_count_8_bit++;
    }
    else {
      precomp_mgr.statistics.decompressed_pdf_count++;
    }

    debug_deflate_detected(*precomp_mgr.ctx, rdres, "in PDF");

    if (rdres.accepted) {
      result.success = true;

      result.original_size = rdres.compressed_stream_size;
      result.precompressed_size = rdres.uncompressed_stream_size;

      precomp_mgr.statistics.recompressed_streams_count++;
      precomp_mgr.statistics.recompressed_pdf_count++;

      precomp_mgr.ctx->non_zlib_was_used = true;
      debug_sums(precomp_mgr, rdres);

      if (img_bpc == 8) {
        if (rdres.uncompressed_stream_size == (img_width * img_height)) {
          result.bmp_header_type = BMP_HEADER_8BPP;
          print_to_log(PRECOMP_DEBUG_LOG, "Image size did match (8 bit)\n");
          precomp_mgr.statistics.recompressed_pdf_count_8_bit++;
          precomp_mgr.statistics.recompressed_pdf_count--;
        }
        else if (rdres.uncompressed_stream_size == (img_width * img_height * 3)) {
          result.bmp_header_type = BMP_HEADER_24BPP;
          print_to_log(PRECOMP_DEBUG_LOG, "Image size did match (24 bit)\n");
          precomp_mgr.statistics.decompressed_pdf_count_8_bit--;
          precomp_mgr.statistics.decompressed_pdf_count_24_bit++;
          precomp_mgr.statistics.recompressed_pdf_count_24_bit++;
          precomp_mgr.statistics.recompressed_pdf_count--;
        }
        else {
          print_to_log(PRECOMP_DEBUG_LOG, "Image size didn't match with stream size\n");
          precomp_mgr.statistics.decompressed_pdf_count_8_bit--;
          precomp_mgr.statistics.decompressed_pdf_count++;
        }
      }

      debug_pos(precomp_mgr);

      // write compressed data header (PDF) without 12 first bytes
      //   (/FlateDecode)

      std::byte bmp_c{ 0 };
      if (result.bmp_header_type == BMP_HEADER_8BPP) {
        // 8 Bit, Bit 7,6 = 01
        bmp_c = std::byte{ 0b01000000 };
      }
      else if (result.bmp_header_type == BMP_HEADER_24BPP) {
        // 24 Bit, Bit 7,6 = 10
        bmp_c = std::byte{ 0b10000000 };
      }

      result.flags = bmp_c;
      result.inc_last_hdr_byte = false;
      result.zlib_header = std::vector(checkbuf + 12, checkbuf + pdf_header_length);

      // write decompressed data
      unsigned char* buf_ptr = precomp_mgr.ctx->decomp_io_buf.data();
      if (rdres.uncompressed_in_memory) {
        result.precompressed_stream = memiostream::make_copy(buf_ptr, buf_ptr + result.precompressed_size);
      }
      else {
        tmpfile->reopen();
        result.precompressed_stream = std::move(tmpfile);
      }
        result.rdres = std::move(rdres);

      debug_pos(precomp_mgr);
    }
    else {
      if (intense_mode_is_active(precomp_mgr)) precomp_mgr.ctx->intense_ignore_offsets.insert(precomp_mgr.ctx->input_file_pos - 2);
      if (brute_mode_is_active(precomp_mgr)) precomp_mgr.ctx->brute_ignore_offsets.insert(precomp_mgr.ctx->input_file_pos);
      print_to_log(PRECOMP_DEBUG_LOG, "No matches\n");
    }
  }

  precomp_mgr.ctx->input_file_pos -= pdf_header_length; // restore input pos
  result.input_pos_extra_add += pdf_header_length;  // Add PDF header length to the deflate stream length for the actual PDF stream size
  return result;
}

pdf_precompression_result precompress_pdf(Precomp& precomp_mgr, std::span<unsigned char> checkbuf_span) {
  auto checkbuf = checkbuf_span.data();
  pdf_precompression_result result = pdf_precompression_result(0, 0);
  long long act_search_pos = 12;
  bool found_stream = false;
  do {
    if (*(checkbuf + act_search_pos) == 's') {
      if (memcmp(checkbuf + act_search_pos, "stream", 6) == 0) {
        found_stream = true;
        break;
      }
    }
    act_search_pos++;
  } while (act_search_pos < (CHECKBUF_SIZE - 6));
  if (!found_stream) return result;

  // check if the stream is an image and width and height are given

  // read 4096 bytes before stream

  unsigned char type_buf[4097];
  long long int type_buf_length;

  type_buf[4096] = 0;

  if ((precomp_mgr.ctx->input_file_pos + act_search_pos) >= 4096) {
    precomp_mgr.ctx->fin->seekg((precomp_mgr.ctx->input_file_pos + act_search_pos) - 4096, std::ios_base::beg);
    precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(type_buf), 4096);
    type_buf_length = 4096;
  }
  else {
    precomp_mgr.ctx->fin->seekg(0, std::ios_base::beg);
    precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(type_buf), precomp_mgr.ctx->input_file_pos + act_search_pos);
    type_buf_length = precomp_mgr.ctx->input_file_pos + act_search_pos;
  }

  // find "<<"

  long long start_pos = -1;

  for (long long i = type_buf_length; i > 0; i--) {
    if ((type_buf[i] == '<') && (type_buf[i - 1] == '<')) {
      start_pos = i;
      break;
    }
  }

  int width_val = 0, height_val = 0, bpc_val = 0;

  if ((start_pos > -1) && (precomp_mgr.switches.pdf_bmp_mode)) {

    unsigned int width_pos, height_pos, bpc_pos;

    // find "/Width"
    width_pos = (unsigned char*)strstr((const char*)(type_buf + start_pos), "/Width") - type_buf;

    if (width_pos > 0)
      for (auto i = width_pos + 7; i < type_buf_length; i++) {
        if (((type_buf[i] >= '0') && (type_buf[i] <= '9')) || (type_buf[i] == ' ')) {
          if (type_buf[i] != ' ') {
            width_val = width_val * 10 + (type_buf[i] - '0');
          }
        }
        else {
          break;
        }
      }

    // find "/Height"
    height_pos = (unsigned char*)strstr((const char*)(type_buf + start_pos), "/Height") - type_buf;

    if (height_pos > 0)
      for (auto i = height_pos + 8; i < type_buf_length; i++) {
        if (((type_buf[i] >= '0') && (type_buf[i] <= '9')) || (type_buf[i] == ' ')) {
          if (type_buf[i] != ' ') {
            height_val = height_val * 10 + (type_buf[i] - '0');
          }
        }
        else {
          break;
        }
      }

    // find "/BitsPerComponent"
    bpc_pos = (unsigned char*)strstr((const char*)(type_buf + start_pos), "/BitsPerComponent") - type_buf;

    if (bpc_pos > 0)
      for (auto i = bpc_pos + 18; i < type_buf_length; i++) {
        if (((type_buf[i] >= '0') && (type_buf[i] <= '9')) || (type_buf[i] == ' ')) {
          if (type_buf[i] != ' ') {
            bpc_val = bpc_val * 10 + (type_buf[i] - '0');
          }
        }
        else {
          break;
        }
      }

    if ((width_val != 0) && (height_val != 0) && (bpc_val != 0)) {
      print_to_log(PRECOMP_DEBUG_LOG, "Possible image in PDF found: %i * %i, %i bit\n", width_val, height_val, bpc_val);
    }
  }

  if ((*(checkbuf + act_search_pos + 6) == 13) || (*(checkbuf + act_search_pos + 6) == 10)) {
    if ((*(checkbuf + act_search_pos + 7) == 13) || (*(checkbuf + act_search_pos + 7) == 10)) {
      // seems to be two byte EOL - zLib Header present?
      if (((((*(checkbuf + act_search_pos + 8) << 8) + *(checkbuf + act_search_pos + 9)) % 31) == 0) &&
        ((*(checkbuf + act_search_pos + 9) & 32) == 0)) { // FDICT must not be set
        int compression_method = (*(checkbuf + act_search_pos + 8) & 15);
        if (compression_method == 8) {
          return try_decompression_pdf(precomp_mgr, checkbuf, act_search_pos + 10, width_val, height_val, bpc_val);
        }
      }
    }
    else {
      // seems to be one byte EOL - zLib Header present?
      if ((((*(checkbuf + act_search_pos + 7) << 8) + *(checkbuf + act_search_pos + 8)) % 31) == 0) {
        int compression_method = (*(checkbuf + act_search_pos + 7) & 15);
        if (compression_method == 8) {
          return try_decompression_pdf(precomp_mgr, checkbuf, act_search_pos + 9, width_val, height_val, bpc_val);
        }
      }
    }
  }

  return result;
}

void recompress_pdf(Precomp& precomp_mgr, std::byte precomp_hdr_flags) {
  recompress_deflate_result rdres;
  unsigned hdr_length;
  // restore PDF header
  ostream_printf(*precomp_mgr.ctx->fout, "/FlateDecode");
  fin_fget_deflate_rec(precomp_mgr, rdres, precomp_hdr_flags, precomp_mgr.in, hdr_length, false);
  debug_deflate_reconstruct(rdres, "PDF", hdr_length, 0);

  int bmp_c = static_cast<int>(precomp_hdr_flags & std::byte{ 0b11000000 });
  if (bmp_c == 1) print_to_log(PRECOMP_DEBUG_LOG, "Skipping BMP header (8-Bit)\n");
  if (bmp_c == 2) print_to_log(PRECOMP_DEBUG_LOG, "Skipping BMP header (24-Bit)\n");

  // read BMP header
  int bmp_width = 0;
  switch (bmp_c) {
  case 1:
    precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 54 + 1024);
    break;
  case 2:
    precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 54);
    break;
  }
  if (bmp_c > 0) {
    bmp_width = precomp_mgr.in[18] + (precomp_mgr.in[19] << 8) + (precomp_mgr.in[20] << 16) + (precomp_mgr.in[21] << 24);
    if (bmp_c == 2) bmp_width *= 3;
  }

  uint64_t read_part, skip_part;
  if ((bmp_c == 0) || ((bmp_width % 4) == 0)) {
    // recompress directly to fout
    read_part = rdres.uncompressed_stream_size;
    skip_part = 0;
  }
  else { // lines aligned to 4 byte, skip those bytes
    // recompress directly to fout, but skipping bytes
    read_part = bmp_width;
    skip_part = (-bmp_width) & 3;
  }
  if (!try_reconstructing_deflate_skip(precomp_mgr, *precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, rdres, read_part, skip_part)) {
    throw PrecompError(ERR_DURING_RECOMPRESSION);
  }
}
