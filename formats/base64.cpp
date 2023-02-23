#include "base64.h"

#include <cstring>
#include <memory>

bool base64_header_check(const unsigned char* checkbuf) {
  if ((*(checkbuf + 1) == 'o') && (*(checkbuf + 2) == 'n') && (*(checkbuf + 3) == 't') && (*(checkbuf + 4) == 'e')) {
    unsigned char cte_detect[33];
    for (int i = 0; i < 33; i++) {
      cte_detect[i] = tolower(*(checkbuf + i));
    }
    return memcmp(cte_detect, "content-transfer-encoding: base64", 33) == 0;
  }
  return false;
}

base64_precompression_result::base64_precompression_result() : precompression_result(D_BASE64) {}
void base64_precompression_result::dump_base64_header(Precomp& precomp_mgr) {
  // write "header", but change first char to prevent re-detection
  fout_fput_vlint(*precomp_mgr.ctx->fout, base64_header.size());
  precomp_mgr.ctx->fout->put(base64_header[0] - 1);
  precomp_mgr.ctx->fout->write(reinterpret_cast<char*>(base64_header.data() + 1), base64_header.size() - 1);

  fout_fput_vlint(*precomp_mgr.ctx->fout, base64_line_len.size());
  if (line_case == 2) {
    for (auto chr : base64_line_len) {
      precomp_mgr.ctx->fout->put(chr);
    }
  }
  else {
    precomp_mgr.ctx->fout->put(base64_line_len[0]);
    if (line_case == 1) precomp_mgr.ctx->fout->put(base64_line_len[base64_line_len.size() - 1]);
  }
}
void base64_precompression_result::dump_precompressed_data_to_outfile(Precomp& precomp_mgr) {
  if (recursion_used) fout_fput_vlint(*precomp_mgr.ctx->fout, recursion_filesize);
  auto out_size = recursion_used ? recursion_filesize : precompressed_size;
  fast_copy(precomp_mgr, *precompressed_stream, *precomp_mgr.ctx->fout, out_size);
}
void base64_precompression_result::dump_to_outfile(Precomp& precomp_mgr) {
  dump_header_to_outfile(precomp_mgr);
  dump_base64_header(precomp_mgr);
  dump_penaltybytes_to_outfile(precomp_mgr);
  dump_stream_sizes_to_outfile(precomp_mgr);
  dump_precompressed_data_to_outfile(precomp_mgr);
}

// Base64 alphabet
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

unsigned char base64_char_decode(unsigned char c) {
  if ((c >= 'A') && (c <= 'Z')) {
    return (c - 'A');
  }
  if ((c >= 'a') && (c <= 'z')) {
    return (c - 'a' + 26);
  }
  if ((c >= '0') && (c <= '9')) {
    return (c - '0' + 52);
  }
  if (c == '+') return 62;
  if (c == '/') return 63;

  if (c == '=') return 64; // padding
  return 65; // invalid
}

void base64_reencode(Precomp& precomp_mgr, IStreamLike& file_in, OStreamLike& file_out, size_t line_count, const unsigned int* base64_line_len, long long max_in_count = 0x7FFFFFFFFFFFFFFF, long long max_byte_count = 0x7FFFFFFFFFFFFFFF) {
  int line_nr = 0;
  unsigned int act_line_len = 0;
  long long avail_in;
  unsigned char a, b, c;
  int i;
  long long act_byte_count = 0;

  long long remaining_bytes = max_in_count;

  do {
    if (remaining_bytes > DIV3CHUNK) {
      file_in.read(reinterpret_cast<char*>(precomp_mgr.in), DIV3CHUNK);
      avail_in = file_in.gcount();
    }
    else {
      file_in.read(reinterpret_cast<char*>(precomp_mgr.in), remaining_bytes);
      avail_in = file_in.gcount();
    }
    remaining_bytes -= avail_in;

    // make sure avail_in mod 3 = 0, pad with 0 bytes
    while ((avail_in % 3) != 0) {
      precomp_mgr.in[avail_in] = 0;
      avail_in++;
    }

    for (i = 0; i < (avail_in / 3); i++) {
      a = precomp_mgr.in[i * 3];
      b = precomp_mgr.in[i * 3 + 1];
      c = precomp_mgr.in[i * 3 + 2];
      if (act_byte_count < max_byte_count) file_out.put(b64[a >> 2]);
      act_byte_count++;
      act_line_len++;
      if (act_line_len == base64_line_len[line_nr]) { // end of line, write CRLF
        if (act_byte_count < max_byte_count) file_out.put(13);
        act_byte_count++;
        if (act_byte_count < max_byte_count) file_out.put(10);
        act_byte_count++;
        act_line_len = 0;
        line_nr++;
        if (line_nr == line_count) break;
      }
      if (act_byte_count < max_byte_count) file_out.put(b64[((a & 0x03) << 4) | (b >> 4)]);
      act_byte_count++;
      act_line_len++;
      if (act_line_len == base64_line_len[line_nr]) { // end of line, write CRLF
        if (act_byte_count < max_byte_count) file_out.put(13);
        act_byte_count++;
        if (act_byte_count < max_byte_count) file_out.put(10);
        act_byte_count++;
        act_line_len = 0;
        line_nr++;
        if (line_nr == line_count) break;
      }
      if (act_byte_count < max_byte_count) file_out.put(b64[((b & 0x0F) << 2) | (c >> 6)]);
      act_byte_count++;
      act_line_len++;
      if (act_line_len == base64_line_len[line_nr]) { // end of line, write CRLF
        if (act_byte_count < max_byte_count) file_out.put(13);
        act_byte_count++;
        if (act_byte_count < max_byte_count) file_out.put(10);
        act_byte_count++;
        act_line_len = 0;
        line_nr++;
        if (line_nr == line_count) break;
      }
      if (act_byte_count < max_byte_count) file_out.put(b64[c & 63]);
      act_byte_count++;
      act_line_len++;
      if (act_line_len == base64_line_len[line_nr]) { // end of line, write CRLF
        if (act_byte_count < max_byte_count) file_out.put(13);
        act_byte_count++;
        if (act_byte_count < max_byte_count) file_out.put(10);
        act_byte_count++;
        act_line_len = 0;
        line_nr++;
        if (line_nr == line_count) break;
      }
    }
    if (line_nr == line_count) break;
  } while ((remaining_bytes > 0) && (avail_in > 0));
}

base64_precompression_result try_decompression_base64(Precomp& precomp_mgr, int base64_header_length) {
  base64_precompression_result result = base64_precompression_result();
  std::unique_ptr<PrecompTmpFile> tmpfile = std::make_unique<PrecompTmpFile>();
  tmpfile->open(temp_files_tag() + "_decomp_base64", std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);

  init_decompression_variables(*precomp_mgr.ctx);
  tmpfile->close();

  // try to decode at current position
  remove(tmpfile->file_path.c_str());
  precomp_mgr.ctx->fin->seekg(precomp_mgr.ctx->input_file_pos, std::ios_base::beg);

  unsigned char base64_data[CHUNK >> 2];

  std::streamsize avail_in = 0;
  int i, j, k;
  unsigned char a, b, c, d;
  int cr_count = 0;
  bool decoding_failed = false;
  bool stream_finished = false;
  k = 0;

  unsigned int act_line_len = 0;

  {
    std::fstream ftempout;
    ftempout.open(tmpfile->file_path, std::ios_base::out | std::ios_base::binary);
    do {
      precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), CHUNK);
      avail_in = precomp_mgr.ctx->fin->gcount();
      for (i = 0; i < (avail_in >> 2); i++) {
        // are these valid base64 chars?
        for (j = (i << 2); j < ((i << 2) + 4); j++) {
          c = base64_char_decode(precomp_mgr.in[j]);
          if (c < 64) {
            base64_data[k] = c;
            k++;
            cr_count = 0;
            act_line_len++;
            continue;
          }
          if ((precomp_mgr.in[j] == 13) || (precomp_mgr.in[j] == 10)) {
            if (precomp_mgr.in[j] == 13) {
              cr_count++;
              if (cr_count == 2) { // double CRLF -> base64 end
                stream_finished = true;
                break;
              }
              result.base64_line_len.push_back(act_line_len);
              if (result.base64_line_len.size() == 65534) stream_finished = true;
              act_line_len = 0;
            }
            continue;
          }
          else {
            cr_count = 0;
          }
          stream_finished = true;
          result.base64_line_len.push_back(act_line_len);
          act_line_len = 0;
          // "=" -> Padding
          if (precomp_mgr.in[j] == '=') {
            while ((k % 4) != 0) {
              base64_data[k] = 0;
              k++;
            }
            break;
          }
          // "-" -> base64 end
          if (precomp_mgr.in[j] == '-') break;
          // invalid char found -> decoding failed
          decoding_failed = true;
          break;
        }
        if (decoding_failed) break;

        for (j = 0; j < (k >> 2); j++) {
          a = base64_data[(j << 2)];
          b = base64_data[(j << 2) + 1];
          c = base64_data[(j << 2) + 2];
          d = base64_data[(j << 2) + 3];
          ftempout.put((a << 2) | (b >> 4));
          ftempout.put(((b << 4) & 0xFF) | (c >> 2));
          ftempout.put(((c << 6) & 0xFF) | d);
        }
        if (stream_finished) break;
        for (j = 0; j < (k % 4); j++) {
          base64_data[j] = base64_data[((k >> 2) << 2) + j];
        }
        k = k % 4;
      }
    } while ((avail_in == CHUNK) && (!decoding_failed) && (!stream_finished));

    // if one of the lines is longer than 255 characters -> decoding failed
    for (i = 0; i < result.base64_line_len.size(); i++) {
      if (result.base64_line_len[i] > 255) {
        decoding_failed = true;
        break;
      }
    }
    ftempout.close();
  }

  if (!decoding_failed) {
    int line_case = -1;
    // check line case
    if (result.base64_line_len.size() == 1) {
      line_case = 0; // one length for all lines
    }
    else {
      for (i = 1; i < (result.base64_line_len.size() - 1); i++) {
        if (result.base64_line_len[i] != result.base64_line_len[0]) {
          line_case = 2; // save complete line length list
          break;
        }
      }
      if (line_case == -1) {
        // check last line
        if (result.base64_line_len[result.base64_line_len.size() - 1] == result.base64_line_len[0]) {
          line_case = 0; // one length for all lines
        }
        else {
          line_case = 1; // first length for all lines, second length for last line
        }
      }
    }

    precomp_mgr.statistics.decompressed_streams_count++;
    precomp_mgr.statistics.decompressed_base64_count++;

    {
      WrappedFStream ftempout;
      ftempout.open(tmpfile->file_path, std::ios_base::in | std::ios_base::binary);
      ftempout.seekg(0, std::ios_base::end);
      precomp_mgr.ctx->identical_bytes = ftempout.tellg();
    }

    print_to_log(PRECOMP_DEBUG_LOG, "Possible Base64-Stream (line_case %i, line_count %i) found at position %lli\n", line_case, result.base64_line_len.size(), precomp_mgr.ctx->saved_input_file_pos);
    print_to_log(PRECOMP_DEBUG_LOG, "Can be decoded to %lli bytes\n", precomp_mgr.ctx->identical_bytes);

    // try to re-encode Base64 data
    {
      WrappedFStream ftempout;
      ftempout.open(tmpfile->file_path, std::ios_base::in | std::ios_base::binary);
      if (!ftempout.is_open()) {
        throw PrecompError(ERR_TEMP_FILE_DISAPPEARED);
      }

      std::string frecomp_filename = tmpfile->file_path + "_rec";
      remove(frecomp_filename.c_str());
      PrecompTmpFile frecomp;
      frecomp.open(frecomp_filename, std::ios_base::in | std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
      base64_reencode(precomp_mgr, ftempout, frecomp, result.base64_line_len.size(), result.base64_line_len.data());

      ftempout.close();
      precomp_mgr.ctx->identical_bytes_decomp = compare_files(precomp_mgr, *precomp_mgr.ctx->fin, frecomp, precomp_mgr.ctx->input_file_pos, 0);
    }

    if (precomp_mgr.ctx->identical_bytes_decomp > precomp_mgr.switches.min_ident_size) {
      precomp_mgr.statistics.recompressed_streams_count++;
      precomp_mgr.statistics.recompressed_base64_count++;
      print_to_log(PRECOMP_DEBUG_LOG, "Match: encoded to %lli bytes\n");

      result.success = true;

      // check recursion
      tmpfile->reopen();
      recursion_result r = recursion_compress(precomp_mgr, precomp_mgr.ctx->identical_bytes_decomp, precomp_mgr.ctx->identical_bytes, *tmpfile);

      // write compressed data header (Base64)
      int header_byte = 1 + (line_case << 2);
      if (r.success) {
        header_byte += 128;
      }

      result.flags = header_byte;
      result.base64_header = std::vector(&precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb], &precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb] + base64_header_length);

      result.original_size = precomp_mgr.ctx->identical_bytes;
      result.precompressed_size = precomp_mgr.ctx->identical_bytes_decomp;

      // write decompressed data
      if (r.success) {
        auto rec_tmpfile = new PrecompTmpFile();
        rec_tmpfile->open(r.file_name, std::ios_base::in | std::ios_base::binary);
        result.precompressed_stream = std::unique_ptr<IStreamLike>(rec_tmpfile);
        result.recursion_filesize = r.file_length;
        result.recursion_used = true;
      }
      else {
        tmpfile->reopen();
        result.precompressed_stream = std::move(tmpfile);
      }
    }
    else {
      print_to_log(PRECOMP_DEBUG_LOG, "No match\n");
    }
  }
  return result;
}

base64_precompression_result precompress_base64(Precomp& precomp_mgr) {
  base64_precompression_result result = base64_precompression_result();
  // search for double CRLF, all between is "header"
  int base64_header_length = 33;
  bool found_double_crlf = false;
  do {
    if ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + base64_header_length] == 13) && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + base64_header_length + 1] == 10)) {
      if ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + base64_header_length + 2] == 13) && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + base64_header_length + 3] == 10)) {
        found_double_crlf = true;
        base64_header_length += 4;
        // skip additional CRLFs
        while ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + base64_header_length] == 13) && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + base64_header_length + 1] == 10)) {
          base64_header_length += 2;
        }
        break;
      }
    }
    base64_header_length++;
  } while (base64_header_length < (CHECKBUF_SIZE - 2));

  if (found_double_crlf) {
    precomp_mgr.ctx->input_file_pos += base64_header_length; // skip "header"

    result = try_decompression_base64(precomp_mgr, base64_header_length);

    precomp_mgr.ctx->cb += base64_header_length;
  }
  return result;
}

void recompress_base64(Precomp& precomp_mgr, unsigned char precomp_hdr_flags) {
  print_to_log(PRECOMP_DEBUG_LOG, "Decompressed data - Base64\n");

  int line_case = (precomp_hdr_flags >> 2) & 3;
  bool recursion_used = ((precomp_hdr_flags & 128) == 128);

  // restore Base64 "header"
  auto base64_header_length = fin_fget_vlint(*precomp_mgr.ctx->fin);

  print_to_log(PRECOMP_DEBUG_LOG, "Base64 header length: %i\n", base64_header_length);
  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), base64_header_length);
  precomp_mgr.ctx->fout->put(*(precomp_mgr.in) + 1); // first char was decreased
  precomp_mgr.ctx->fout->write(reinterpret_cast<char*>(precomp_mgr.in + 1), base64_header_length - 1);

  // read line length list
  auto line_count = fin_fget_vlint(*precomp_mgr.ctx->fin);

  auto base64_line_len = new unsigned int[line_count];

  if (line_case == 2) {
    for (int i = 0; i < line_count; i++) {
      base64_line_len[i] = precomp_mgr.ctx->fin->get();
    }
  }
  else {
    base64_line_len[0] = precomp_mgr.ctx->fin->get();
    for (int i = 1; i < line_count; i++) {
      base64_line_len[i] = base64_line_len[0];
    }
    if (line_case == 1) base64_line_len[line_count - 1] = precomp_mgr.ctx->fin->get();
  }

  long long recompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);
  long long decompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);

  long long recursion_data_length = 0;
  if (recursion_used) {
    recursion_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);
  }

  if (recursion_used) {
    print_to_log(PRECOMP_DEBUG_LOG, "Recursion data length: %lli\n", recursion_data_length);
  }
  else {
    print_to_log(PRECOMP_DEBUG_LOG, "Encoded length: %lli - decoded length: %lli\n", recompressed_data_length, decompressed_data_length);
  }

  // re-encode Base64

  if (recursion_used) {
    PrecompTmpFile tmp_base64;
    tmp_base64.open(temp_files_tag() + "_recomp_base64", std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
    recursion_result r = recursion_decompress(precomp_mgr, recursion_data_length, tmp_base64);
    auto wrapped_istream_frecurse = WrappedIStream(r.frecurse.get(), false);
    base64_reencode(precomp_mgr, wrapped_istream_frecurse, *precomp_mgr.ctx->fout, line_count, base64_line_len, r.file_length, decompressed_data_length);
    r.frecurse->close();
    remove(r.file_name.c_str());
  }
  else {
    base64_reencode(precomp_mgr, *precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, line_count, base64_line_len, recompressed_data_length, decompressed_data_length);
  }

  delete[] base64_line_len;
}
