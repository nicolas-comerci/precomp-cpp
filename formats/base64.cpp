#include "base64.h"

#include <cstddef>
#include <cstring>
#include <memory>

class base64_precompression_result : public precompression_result {
    void dump_base64_header(Precomp& precomp_mgr) const {
        // write "header", but change first char to prevent re-detection
        fout_fput_vlint(*precomp_mgr.ctx->fout, base64_header.size());
        precomp_mgr.ctx->fout->put(base64_header[0] - 1);
        precomp_mgr.ctx->fout->write(reinterpret_cast<const char*>(base64_header.data() + 1), base64_header.size() - 1);

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
public:
    std::vector<unsigned char> base64_header;
    int line_case = 0;
    std::vector<unsigned int> base64_line_len;
    bool recursion_used = false;
    long long recursion_filesize = 0;

    explicit base64_precompression_result() : precompression_result(D_BASE64) {}

    void dump_precompressed_data_to_outfile(Precomp& precomp_mgr) override {
        if (recursion_used) fout_fput_vlint(*precomp_mgr.ctx->fout, recursion_filesize);
        auto out_size = recursion_used ? recursion_filesize : precompressed_size;
        fast_copy(*precompressed_stream, *precomp_mgr.ctx->fout, out_size);
    }

    void dump_to_outfile(Precomp& precomp_mgr) override {
        dump_header_to_outfile(precomp_mgr);
        dump_base64_header(precomp_mgr);
        dump_penaltybytes_to_outfile(precomp_mgr);
        dump_stream_sizes_to_outfile(precomp_mgr);
        dump_precompressed_data_to_outfile(precomp_mgr);
    }
};

bool Base64FormatHandler::quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) {
    auto checkbuf = buffer.data();
    if ((*(checkbuf + 1) == 'o') && (*(checkbuf + 2) == 'n') && (*(checkbuf + 3) == 't') && (*(checkbuf + 4) == 'e')) {
        unsigned char cte_detect[33];
        for (int i = 0; i < 33; i++) {
            cte_detect[i] = tolower(*(checkbuf + i));
        }
        return memcmp(cte_detect, "content-transfer-encoding: base64", 33) == 0;
    }
    return false;
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

void base64_reencode(IStreamLike& file_in, OStreamLike& file_out, const std::vector<unsigned int>& base64_line_len, long long max_in_count = 0x7FFFFFFFFFFFFFFF, long long max_byte_count = 0x7FFFFFFFFFFFFFFF) {
  int line_nr = 0;
  unsigned int act_line_len = 0;
  long long avail_in;
  unsigned char a, b, c;
  int i;
  long long act_byte_count = 0;

  long long remaining_bytes = max_in_count;

  std::vector<unsigned char> in_buf{};
  in_buf.resize(DIV3CHUNK);
  do {
    if (remaining_bytes > DIV3CHUNK) {
      file_in.read(reinterpret_cast<char*>(in_buf.data()), DIV3CHUNK);
      avail_in = file_in.gcount();
    }
    else {
      file_in.read(reinterpret_cast<char*>(in_buf.data()), remaining_bytes);
      avail_in = file_in.gcount();
    }
    remaining_bytes -= avail_in;

    // make sure avail_in mod 3 = 0, pad with 0 bytes
    while ((avail_in % 3) != 0) {
      in_buf[avail_in] = 0;
      avail_in++;
    }

    auto process_byte_check_end = [&]() {
      act_byte_count++;
      act_line_len++;
      if (act_line_len == base64_line_len[line_nr]) { // end of line, write CRLF
        if (act_byte_count < max_byte_count) file_out.put(13);
        act_byte_count++;
        if (act_byte_count < max_byte_count) file_out.put(10);
        act_byte_count++;
        act_line_len = 0;
        line_nr++;
        if (line_nr == base64_line_len.size()) return true;
      }
      return false;
    };

    for (i = 0; i < (avail_in / 3); i++) {
      a = in_buf[i * 3];
      b = in_buf[i * 3 + 1];
      c = in_buf[i * 3 + 2];
      if (act_byte_count < max_byte_count) file_out.put(b64[a >> 2]);
      if (process_byte_check_end()) break;
      if (act_byte_count < max_byte_count) file_out.put(b64[((a & 0x03) << 4) | (b >> 4)]);
      if (process_byte_check_end()) break;
      if (act_byte_count < max_byte_count) file_out.put(b64[((b & 0x0F) << 2) | (c >> 6)]);
      if (process_byte_check_end()) break;
      if (act_byte_count < max_byte_count) file_out.put(b64[c & 63]);
      if (process_byte_check_end()) break;
    }
    if (line_nr == base64_line_len.size()) break;
  } while ((remaining_bytes > 0) && (avail_in > 0));
}

unsigned long long compare_files(Precomp& precomp_mgr, IStreamLike& file1, IStreamLike& file2, unsigned int pos1, unsigned int pos2) {
    unsigned char input_bytes1[COMP_CHUNK];
    unsigned char input_bytes2[COMP_CHUNK];
    long long same_byte_count = 0;
    long long size1, size2, minsize;
    int i;
    bool endNow = false;

    file1.seekg(pos1, std::ios_base::beg);
    file2.seekg(pos2, std::ios_base::beg);

    do {
        precomp_mgr.call_progress_callback();

        file1.read(reinterpret_cast<char*>(input_bytes1), COMP_CHUNK);
        size1 = file1.gcount();
        file2.read(reinterpret_cast<char*>(input_bytes2), COMP_CHUNK);
        size1 = file2.gcount();

        minsize = std::min(size1, size2);
        for (i = 0; i < minsize; i++) {
            if (input_bytes1[i] != input_bytes2[i]) {
                endNow = true;
                break;
            }
            same_byte_count++;
        }
    } while ((minsize == COMP_CHUNK) && (!endNow));

    return same_byte_count;
}

std::unique_ptr<precompression_result> try_decompression_base64(Precomp& precomp_mgr, long long original_input_pos, int base64_header_length, const std::span<unsigned char> checkbuf_span) {
  auto checkbuf = checkbuf_span.data();
  std::unique_ptr<base64_precompression_result> result = std::make_unique<base64_precompression_result>();
  std::unique_ptr<PrecompTmpFile> tmpfile = std::make_unique<PrecompTmpFile>();
  tmpfile->open(precomp_mgr.get_tempfile_name("decomp_base64"), std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
  tmpfile->close();
  remove(tmpfile->file_path.c_str());

  auto base64_stream_pos = original_input_pos + base64_header_length;

  // try to decode at current position
  precomp_mgr.ctx->fin->seekg(base64_stream_pos, std::ios_base::beg);

  unsigned char base64_data[CHUNK >> 2];

  std::streamsize avail_in = 0;
  unsigned char a, b, c, d;
  int cr_count = 0;
  bool decoding_failed = false;
  bool stream_finished = false;

  unsigned int act_line_len = 0;

  {
    uint16_t k = 0;
    tmpfile->open(tmpfile->file_path, std::ios_base::in | std::ios_base::out | std::ios_base::binary);
    std::vector<unsigned char> in_buf{};
    in_buf.resize(CHUNK);
    do {
      precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(in_buf.data()), CHUNK);
      avail_in = precomp_mgr.ctx->fin->gcount();
      for (auto i = 0; i < (avail_in >> 2); i++) {
        // are these valid base64 chars?
        for (auto j = (i << 2); j < ((i << 2) + 4); j++) {
          c = base64_char_decode(in_buf[j]);
          if (c < 64) {
            base64_data[k] = c;
            k++;
            cr_count = 0;
            act_line_len++;
            continue;
          }
          if ((in_buf[j] == 13) || (in_buf[j] == 10)) {
            if (in_buf[j] == 13) {
              cr_count++;
              if (cr_count == 2) { // double CRLF -> base64 end
                stream_finished = true;
                break;
              }
              result->base64_line_len.push_back(act_line_len);
              if (result->base64_line_len.size() == 65534) stream_finished = true;
              act_line_len = 0;
            }
            continue;
          }
          else {
            cr_count = 0;
          }
          stream_finished = true;
          result->base64_line_len.push_back(act_line_len);
          act_line_len = 0;
          // "=" -> Padding
          if (in_buf[j] == '=') {
            while ((k % 4) != 0) {
              base64_data[k] = 0;
              k++;
            }
            break;
          }
          // "-" -> base64 end
          if (in_buf[j] == '-') break;
          // invalid char found -> decoding failed
          decoding_failed = true;
          break;
        }
        if (decoding_failed) break;

        for (auto j = 0; j < (k >> 2); j++) {
          a = base64_data[(j << 2)];
          b = base64_data[(j << 2) + 1];
          c = base64_data[(j << 2) + 2];
          d = base64_data[(j << 2) + 3];
          tmpfile->put((a << 2) | (b >> 4));
          tmpfile->put(((b << 4) & 0xFF) | (c >> 2));
          tmpfile->put(((c << 6) & 0xFF) | d);
        }
        if (stream_finished) break;
        for (auto j = 0; j < (k % 4); j++) {
          base64_data[j] = base64_data[((k >> 2) << 2) + j];
        }
        k = k % 4;
      }
    } while ((avail_in == CHUNK) && (!decoding_failed) && (!stream_finished));

    // if one of the lines is longer than 255 characters -> decoding failed
    for (size_t i = 0; i < result->base64_line_len.size(); i++) {
      if (result->base64_line_len[i] > 255) {
        decoding_failed = true;
        break;
      }
    }
    tmpfile->close();
  }

  if (!decoding_failed) {
    std::byte line_case{ 0 }; // one length for all lines
    // check line case
    if (result->base64_line_len.size() != 1) {
      for (size_t i = 1; i < (result->base64_line_len.size() - 1); i++) {
        if (result->base64_line_len[i] != result->base64_line_len[0]) {
          line_case = std::byte{ 0b10 }; // save complete line length list
          break;
        }
      }
      if (line_case == std::byte{ 0 }) {
        // check last line
        if (result->base64_line_len[result->base64_line_len.size() - 1] != result->base64_line_len[0]) {
          line_case = std::byte{ 0b1 }; // first length for all lines, second length for last line
        }
      }
    }

    precomp_mgr.statistics.decompressed_streams_count++;
    precomp_mgr.statistics.decompressed_base64_count++;

    long long decoded_size;
    tmpfile->reopen();
    tmpfile->seekg(0, std::ios_base::end);
    decoded_size = tmpfile->tellg();

    print_to_log(PRECOMP_DEBUG_LOG, "Possible Base64-Stream (line_case %i, line_count %i) found at position %lli\n", line_case, result->base64_line_len.size(), original_input_pos);
    print_to_log(PRECOMP_DEBUG_LOG, "Can be decoded to %lli bytes\n", decoded_size);

    long long compressed_size;
    // try to re-encode Base64 data
    {
      tmpfile->reopen();
      if (!tmpfile->is_open()) {
        throw PrecompError(ERR_TEMP_FILE_DISAPPEARED);
      }

      std::string frecomp_filename = tmpfile->file_path + "_rec";
      remove(frecomp_filename.c_str());
      PrecompTmpFile frecomp;
      frecomp.open(frecomp_filename, std::ios_base::in | std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
      base64_reencode(*tmpfile, frecomp, result->base64_line_len);

      tmpfile->close();
      compressed_size = compare_files(precomp_mgr, *precomp_mgr.ctx->fin, frecomp, base64_stream_pos, 0);
    }

    if (compressed_size > precomp_mgr.switches.min_ident_size) {
      precomp_mgr.statistics.recompressed_streams_count++;
      precomp_mgr.statistics.recompressed_base64_count++;
      print_to_log(PRECOMP_DEBUG_LOG, "Match: encoded to %lli bytes\n");

      result->success = true;

      // check recursion
      tmpfile->close();
      tmpfile->open(tmpfile->file_path, std::ios_base::in | std::ios_base::binary);
      recursion_result r = recursion_compress(precomp_mgr, compressed_size, decoded_size, *tmpfile, tmpfile->file_path + "_");
      tmpfile->close();

      // write compressed data header (Base64)
      std::byte header_byte = std::byte{ 0b1 } | (line_case << 2);
      if (r.success) {
        header_byte |= std::byte{ 0b10000000 };
      }

      result->flags = header_byte;
      result->base64_header = std::vector(checkbuf, checkbuf + base64_header_length);

      // TODO: check, aren't these two switched?
      result->original_size = decoded_size;
      result->precompressed_size = compressed_size;

      // write decompressed data
      if (r.success) {
        auto rec_tmpfile = new PrecompTmpFile();
        rec_tmpfile->open(r.file_name, std::ios_base::in | std::ios_base::binary);
        result->precompressed_stream = std::unique_ptr<IStreamLike>(rec_tmpfile);
        result->recursion_filesize = r.file_length;
        result->recursion_used = true;
      }
      else {
        tmpfile->reopen();
        result->precompressed_stream = std::move(tmpfile);
      }
    }
    else {
      print_to_log(PRECOMP_DEBUG_LOG, "No match\n");
    }
  }
  return result;
}

std::unique_ptr<precompression_result> Base64FormatHandler::attempt_precompression(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span, long long original_input_pos) {
  auto checkbuf = checkbuf_span.data();
  std::unique_ptr<precompression_result> result = std::make_unique<base64_precompression_result>();
  // search for double CRLF, all between is "header"
  int base64_header_length = 33;
  bool found_double_crlf = false;
  do {
    if ((*(checkbuf + base64_header_length) == 13) && (*(checkbuf + base64_header_length + 1) == 10)) {
      if ((*(checkbuf + base64_header_length + 2) == 13) && (*(checkbuf + base64_header_length + 3) == 10)) {
        found_double_crlf = true;
        base64_header_length += 4;
        // skip additional CRLFs
        while ((*(checkbuf + base64_header_length) == 13) && (*(checkbuf + base64_header_length + 1) == 10)) {
          base64_header_length += 2;
        }
        break;
      }
    }
    base64_header_length++;
  } while (base64_header_length < (CHECKBUF_SIZE - 2));

  if (found_double_crlf) {
    result = try_decompression_base64(precomp_mgr, original_input_pos, base64_header_length, checkbuf_span);

    result->input_pos_extra_add += base64_header_length;
  }
  return result;
}

void Base64FormatHandler::recompress(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) {
  print_to_log(PRECOMP_DEBUG_LOG, "Decompressed data - Base64\n");

  int line_case = static_cast<int>(precomp_hdr_flags & std::byte{ 0b1100 });
  bool recursion_used = (precomp_hdr_flags & std::byte{ 0b10000000 }) == std::byte{ 0b10000000 };

  // restore Base64 "header"
  auto base64_header_length = fin_fget_vlint(*context.fin);

  print_to_log(PRECOMP_DEBUG_LOG, "Base64 header length: %i\n", base64_header_length);
  std::vector<unsigned char> in_buf{};
  in_buf.resize(base64_header_length);
  context.fin->read(reinterpret_cast<char*>(in_buf.data()), base64_header_length);
  context.fout->put(in_buf[0] + 1); // first char was decreased
  context.fout->write(reinterpret_cast<char*>(in_buf.data() + 1), base64_header_length - 1);

  // read line length list
  auto line_count = fin_fget_vlint(*context.fin);

  auto base64_line_len = std::vector<unsigned int> {};
  base64_line_len.resize(line_count);

  if (line_case == 2) {
    for (int i = 0; i < line_count; i++) {
      base64_line_len[i] = context.fin->get();
    }
  }
  else {
    base64_line_len[0] = context.fin->get();
    for (int i = 1; i < line_count; i++) {
      base64_line_len[i] = base64_line_len[0];
    }
    if (line_case == 1) base64_line_len[line_count - 1] = context.fin->get();
  }

  long long recompressed_data_length = fin_fget_vlint(*context.fin);
  long long decompressed_data_length = fin_fget_vlint(*context.fin);

  long long recursion_data_length = 0;
  if (recursion_used) {
    recursion_data_length = fin_fget_vlint(*context.fin);
  }

  if (recursion_used) {
    print_to_log(PRECOMP_DEBUG_LOG, "Recursion data length: %lli\n", recursion_data_length);
  }
  else {
    print_to_log(PRECOMP_DEBUG_LOG, "Encoded length: %lli - decoded length: %lli\n", recompressed_data_length, decompressed_data_length);
  }

  // re-encode Base64

  if (recursion_used) {
    auto r = recursion_decompress(context, recursion_data_length, context.precomp.get_tempfile_name("recomp_base64"));
    base64_reencode(*r, *context.fout, base64_line_len, 0x7FFFFFFFFFFFFFFF, decompressed_data_length);
    r->get_recursion_return_code();
  }
  else {
    base64_reencode(*context.fin, *context.fout, base64_line_len, recompressed_data_length, decompressed_data_length);
  }
}
