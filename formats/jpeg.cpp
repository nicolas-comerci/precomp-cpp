#include "jpeg.h"

#include "contrib/packjpg/precomp_jpg.h"

#include "contrib/brunsli/c/include/brunsli/brunsli_encode.h"
#include "contrib/brunsli/c/include/brunsli/brunsli_decode.h"
#include "contrib/brunsli/c/include/brunsli/jpeg_data_reader.h"
#include "contrib/brunsli/c/include/brunsli/jpeg_data_writer.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>

const char* packjpg_version_info() {
  return pjglib_version_info();
}

bool JpegFormatHandler::quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) {
  auto checkbuf = buffer.data();
  // SOI (FF D8) followed by a valid marker for Baseline/Progressive JPEGs
  return
    *checkbuf == 0xFF && *(checkbuf + 1) == 0xD8 &&
    *(checkbuf + 2) == 0xFF && (
      *(checkbuf + 3) == 0xC0 || *(checkbuf + 3) == 0xC2 || *(checkbuf + 3) == 0xC4 || (*(checkbuf + 3) >= 0xDB && *(checkbuf + 3) <= 0xFE)
      );
}

class jpeg_precompression_result: public precompression_result {
  bool is_progressive_jpeg;
public:
  explicit jpeg_precompression_result(Tools* tools, bool _is_progressive_jpeg): precompression_result(D_JPG, tools), is_progressive_jpeg(_is_progressive_jpeg) {}

  void increase_detected_count() override { tools->increase_detected_count(is_progressive_jpeg ? "JPG (progressive)" : "JPG"); }
  void increase_precompressed_count() override { tools->increase_precompressed_count(is_progressive_jpeg ? "JPG (progressive)" : "JPG"); }
};

std::unique_ptr<precompression_result> try_decompression_jpg(Tools& precomp_tools, const Switches& precomp_switches, IStreamLike& input, long long jpg_start_pos, long long jpg_length, bool progressive_jpg) {
  std::unique_ptr<precompression_result> result = std::make_unique<jpeg_precompression_result>(&precomp_tools, progressive_jpg);
  auto random_tag = temp_files_tag();
  std::string original_jpg_filename = precomp_tools.get_tempfile_name(random_tag + "_original_jpg", false);
  std::string decompressed_jpg_filename = precomp_tools.get_tempfile_name(random_tag + "_precompressed_jpg", false);
  std::unique_ptr<PrecompTmpFile> tmpfile = std::make_unique<PrecompTmpFile>();
  tmpfile->open(original_jpg_filename, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
  tmpfile->close();

  if (progressive_jpg) {
    print_to_log(PRECOMP_DEBUG_LOG, "Possible JPG (progressive) found at position ");
  }
  else {
    print_to_log(PRECOMP_DEBUG_LOG, "Possible JPG found at position ");
  }
  print_to_log(PRECOMP_DEBUG_LOG, "%lli, length %lli\n", jpg_start_pos, jpg_length);
  // do not recompress non-progressive JPGs when prog_only is set
  if ((!progressive_jpg) && (precomp_switches.prog_only)) {
    print_to_log(PRECOMP_DEBUG_LOG, "Skipping (only progressive JPGs mode set)\n");
    return result;
  }

  bool jpg_success = false;
  bool recompress_success = false;
  bool mjpg_dht_used = false;
  bool brunsli_used = false;
  char recompress_msg[256];
  std::vector<unsigned char> jpg_mem_in {};
  std::unique_ptr<unsigned char[]> jpg_mem_out;
  unsigned int jpg_mem_out_size = -1;
  bool in_memory = ((jpg_length + MJPGDHT_LEN) <= JPG_MAX_MEMORY_SIZE);

  if (in_memory) { // small stream => do everything in memory
    input.seekg(jpg_start_pos, std::ios_base::beg);
    jpg_mem_in.resize(jpg_length + MJPGDHT_LEN);
    auto memstream = memiostream::make(jpg_mem_in.data(), jpg_mem_in.data() + jpg_length);
    fast_copy(input, *memstream, jpg_length);

    bool brunsli_success = false;

    if (precomp_switches.use_brunsli) {
      print_to_log(PRECOMP_DEBUG_LOG, "Trying to compress using brunsli...\n");
      brunsli::JPEGData jpegData;
      if (brunsli::ReadJpeg(jpg_mem_in.data(), jpg_length, brunsli::JPEG_READ_ALL, &jpegData)) {
        size_t output_size = brunsli::GetMaximumBrunsliEncodedSize(jpegData);
        jpg_mem_out = std::unique_ptr<unsigned char[]>(new unsigned char[output_size]);
        if (brunsli::BrunsliEncodeJpeg(jpegData, jpg_mem_out.get(), &output_size)) {
          recompress_success = true;
          brunsli_success = true;
          brunsli_used = true;
          jpg_mem_out_size = output_size;
        }
        else {
          jpg_mem_out = nullptr;
        }
      }
      else {
        if (jpegData.error == brunsli::JPEGReadError::HUFFMAN_TABLE_NOT_FOUND) {
          print_to_log(PRECOMP_DEBUG_LOG, "huffman table missing, trying to use Motion JPEG DHT\n");
          // search 0xFF 0xDA, insert MJPGDHT (MJPGDHT_LEN bytes)
          bool found_ffda = false;
          bool found_ff = false;
          int ffda_pos = -1;

          do {
            ffda_pos++;
            if (ffda_pos >= jpg_length) break;
            if (found_ff) {
              found_ffda = (jpg_mem_in[ffda_pos] == 0xDA);
              if (found_ffda) break;
              found_ff = false;
            }
            else {
              found_ff = (jpg_mem_in[ffda_pos] == 0xFF);
            }
          } while (!found_ffda);
          if (found_ffda) {
            // reinitialise jpegData
            brunsli::JPEGData newJpegData;
            jpegData = newJpegData;

            memmove(jpg_mem_in.data() + (ffda_pos - 1) + MJPGDHT_LEN, jpg_mem_in.data() + (ffda_pos - 1), jpg_length - (ffda_pos - 1));
            memcpy(jpg_mem_in.data() + (ffda_pos - 1), MJPGDHT, MJPGDHT_LEN);

            if (brunsli::ReadJpeg(jpg_mem_in.data(), jpg_length + MJPGDHT_LEN, brunsli::JPEG_READ_ALL, &jpegData)) {
              size_t output_size = brunsli::GetMaximumBrunsliEncodedSize(jpegData);
              jpg_mem_out = std::unique_ptr<unsigned char[]>(new unsigned char[output_size]);
              if (brunsli::BrunsliEncodeJpeg(jpegData, jpg_mem_out.get(), &output_size)) {
                recompress_success = true;
                brunsli_success = true;
                brunsli_used = true;
                mjpg_dht_used = true;
                jpg_mem_out_size = output_size;
              }
              else {
                jpg_mem_out = nullptr;
              }
            }

            if (!brunsli_success) {
              // revert DHT insertion
              memmove(jpg_mem_in.data() + (ffda_pos - 1), jpg_mem_in.data() + (ffda_pos - 1) + MJPGDHT_LEN, jpg_length - (ffda_pos - 1));
            }
          }
        }
      }
      if (!brunsli_success) {
        if (precomp_switches.use_packjpg_fallback) {
          print_to_log(PRECOMP_DEBUG_LOG, "Brunsli compression failed, using packJPG fallback...\n");
        }
        else {
          print_to_log(PRECOMP_DEBUG_LOG, "Brunsli compression failed\n");
        }
      }
    }

    if ((!precomp_switches.use_brunsli || !brunsli_success) && precomp_switches.use_packjpg_fallback) {
      unsigned char* mem = nullptr;
      pjglib_init_streams(jpg_mem_in.data(), 1, jpg_length, mem, 1);
      recompress_success = pjglib_convert_stream2mem(&mem, &jpg_mem_out_size, recompress_msg);
      brunsli_used = false;
      jpg_mem_out = std::unique_ptr<unsigned char[]>(mem);
    }
  }
  else if (precomp_switches.use_packjpg_fallback) { // large stream => use temporary files
    print_to_log(PRECOMP_DEBUG_LOG, "JPG too large for brunsli, using packJPG fallback...\n");
    // try to decompress at current position
    {
      WrappedFStream decompressed_jpg;
      decompressed_jpg.open(decompressed_jpg_filename, std::ios_base::out | std::ios_base::binary);
      input.seekg(jpg_start_pos, std::ios_base::beg);
      fast_copy(input, decompressed_jpg, jpg_length);
      decompressed_jpg.close();
    }

    // Workaround for JPG bugs. Sometimes tempfile1 is removed, but still
    // not accessible by packJPG, so we prevent that by opening it here
    // ourselves.
    {
      remove(tmpfile->file_path.c_str());
      std::fstream fworkaround;
      fworkaround.open(tmpfile->file_path, std::ios_base::out | std::ios_base::binary);
      fworkaround.close();
    }

    recompress_success = pjglib_convert_file2file(const_cast<char*>(decompressed_jpg_filename.c_str()), const_cast<char*>(tmpfile->file_path.c_str()), recompress_msg);
    brunsli_used = false;
  }

  if ((!recompress_success) && (strncmp(recompress_msg, "huffman table missing", 21) == 0) && (precomp_switches.use_mjpeg) && (precomp_switches.use_packjpg_fallback)) {
    print_to_log(PRECOMP_DEBUG_LOG, "huffman table missing, trying to use Motion JPEG DHT\n");
    // search 0xFF 0xDA, insert MJPGDHT (MJPGDHT_LEN bytes)
    bool found_ffda = false;
    bool found_ff = false;
    int ffda_pos = -1;

    if (in_memory) {
      do {
        ffda_pos++;
        if (ffda_pos >= jpg_length) break;
        if (found_ff) {
          found_ffda = (jpg_mem_in[ffda_pos] == 0xDA);
          if (found_ffda) break;
          found_ff = false;
        }
        else {
          found_ff = (jpg_mem_in[ffda_pos] == 0xFF);
        }
      } while (!found_ffda);
      if (found_ffda) {
        memmove(jpg_mem_in.data() + (ffda_pos - 1) + MJPGDHT_LEN, jpg_mem_in.data() + (ffda_pos - 1), jpg_length - (ffda_pos - 1));
        memcpy(jpg_mem_in.data() + (ffda_pos - 1), MJPGDHT, MJPGDHT_LEN);

        unsigned char* mem = nullptr;
        pjglib_init_streams(jpg_mem_in.data(), 1, jpg_length + MJPGDHT_LEN, mem, 1);
        recompress_success = pjglib_convert_stream2mem(&mem, &jpg_mem_out_size, recompress_msg);
        jpg_mem_out = std::unique_ptr<unsigned char[]>(mem);
      }
    }
    else {
      WrappedFStream decompressed_jpg;
      decompressed_jpg.open(tmpfile->file_path, std::ios_base::in | std::ios_base::binary);
      do {
        ffda_pos++;
        unsigned char chr[1];
        decompressed_jpg.read(reinterpret_cast<char*>(chr), 1);
        if (decompressed_jpg.gcount() != 1) break;
        if (found_ff) {
          found_ffda = (chr[0] == 0xDA);
          if (found_ffda) break;
          found_ff = false;
        }
        else {
          found_ff = (chr[0] == 0xFF);
        }
      } while (!found_ffda);
      std::string mjpgdht_tempfile = tmpfile->file_path + "_mjpgdht";
      if (found_ffda) {
        WrappedFStream decompressed_jpg_w_MJPGDHT;
        decompressed_jpg_w_MJPGDHT.open(mjpgdht_tempfile, std::ios_base::out | std::ios_base::binary);
        decompressed_jpg.seekg(0, std::ios_base::beg);
        fast_copy(decompressed_jpg, decompressed_jpg_w_MJPGDHT, ffda_pos - 1);
        // insert MJPGDHT
        decompressed_jpg_w_MJPGDHT.write(reinterpret_cast<char*>(MJPGDHT), MJPGDHT_LEN);
        decompressed_jpg.seekg(ffda_pos - 1, std::ios_base::beg);
        fast_copy(decompressed_jpg, decompressed_jpg_w_MJPGDHT, jpg_length - (ffda_pos - 1));
      }
      decompressed_jpg.close();
      recompress_success = pjglib_convert_file2file(const_cast<char*>(mjpgdht_tempfile.c_str()), const_cast<char*>(tmpfile->file_path.c_str()), recompress_msg);
    }

    mjpg_dht_used = recompress_success;
  }

  if ((!recompress_success) && (precomp_switches.use_packjpg_fallback)) {
    print_to_log(PRECOMP_DEBUG_LOG, "packJPG error: %s\n", recompress_msg);
  }

  if (!in_memory) {
    remove(decompressed_jpg_filename.c_str());
  }

  if (recompress_success) {
    std::optional<unsigned int> jpg_new_length;

    if (in_memory) {
      jpg_new_length = jpg_mem_out_size;
    }
    else {
      tmpfile->reopen();
      tmpfile->seekg(0, std::ios_base::end);
      jpg_new_length = tmpfile->tellg();
      tmpfile->close();
    }

    if (jpg_new_length.has_value()) {
      result->original_size = jpg_length;
      result->precompressed_size = jpg_new_length.value();
      jpg_success = true;
    }
  }

  if (jpg_success) {
    print_to_log(PRECOMP_DEBUG_LOG, "Best match: %lli bytes, recompressed to %lli bytes\n", result->original_size, result->precompressed_size);

    result->success = true;
    std::byte jpg_flags{ 0b1 }; // no penalty bytes
    if (mjpg_dht_used) jpg_flags |= std::byte{ 0b100 }; // motion JPG DHT used
    if (brunsli_used) jpg_flags |= std::byte{ 0b1000 };
    result->flags = jpg_flags;

    if (in_memory) {
      auto mem = jpg_mem_out.release();
      auto memstream = memiostream::make(mem, mem + result->precompressed_size, true);
      result->precompressed_stream = std::move(memstream);
    }
    else {
      tmpfile->reopen();
      result->precompressed_stream = std::move(tmpfile);
    }
  }
  else {
    print_to_log(PRECOMP_DEBUG_LOG, "No matches\n");
  }

  return result;
}

std::unique_ptr<precompression_result>
JpegFormatHandler::attempt_precompression(IStreamLike &input, OStreamLike &output,
                                          std::span<unsigned char> checkbuf_span,
                                          long long jpg_start_pos, const Switches &precomp_switches) {
  bool done = false, found = false;
  bool hasQuantTable = (*(checkbuf_span.data() + 3) == 0xDB);
  bool progressive_flag = (*(checkbuf_span.data() + 3) == 0xC2);

  long long jpg_end_pos = jpg_start_pos + 2;  // skip header

  unsigned char in_buf[5];
  do {
    input.seekg(jpg_end_pos, std::ios_base::beg);
    input.read(reinterpret_cast<char*>(in_buf), 5);
    if ((input.gcount() != 5) || (in_buf[0] != 0xFF))
      break;
    int length = (int)in_buf[2] * 256 + (int)in_buf[3];
    switch (in_buf[1]) {
    case 0xDB: {
      // FF DB XX XX QtId ...
      // Marker length (XX XX) must be = 2 + (multiple of 65 <= 260)
      // QtId:
      // bit 0..3: number of QT (0..3, otherwise error)
      // bit 4..7: precision of QT, 0 = 8 bit, otherwise 16 bit               
      if (length <= 262 && ((length - 2) % 65) == 0 && in_buf[4] <= 3) {
        hasQuantTable = true;
        jpg_end_pos += length + 2;
      }
      else
        done = true;
      break;
    }
    case 0xC4: {
      done = ((in_buf[4] & 0xF) > 3 || (in_buf[4] >> 4) > 1);
      jpg_end_pos += length + 2;
      break;
    }
    case 0xDA: found = hasQuantTable;
    case 0xD9: done = true; break; //EOI with no SOS?
    case 0xC2: progressive_flag = true;
    case 0xC0: done = (in_buf[4] != 0x08);
    default: jpg_end_pos += length + 2;
    }
  } while (!done);

  if (found) {
    found = done = false;
    jpg_end_pos += 5;

    bool isMarker = (in_buf[4] == 0xFF);
    size_t bytesRead = 0;
    std::vector<unsigned char> in_buf_chunk{};
    in_buf_chunk.resize(CHUNK);
    for (;;) {
      if (done) break;
      input.read(reinterpret_cast<char*>(in_buf_chunk.data()), CHUNK);
      bytesRead = input.gcount();
      if (!bytesRead) break;
      for (size_t i = 0; !done && (i < bytesRead); i++) {
        jpg_end_pos++;
        if (!isMarker) {
          isMarker = (in_buf_chunk[i] == 0xFF);
        }
        else {
          done = (in_buf_chunk[i] && ((in_buf_chunk[i] & 0xF8) != 0xD0) && ((progressive_flag) ? (in_buf_chunk[i] != 0xC4) && (in_buf_chunk[i] != 0xDA) : true));
          found = (in_buf_chunk[i] == 0xD9);
          isMarker = false;
        }
      }
    }
  }

  if (found) {
    const long long jpg_length = jpg_end_pos - jpg_start_pos;
    return try_decompression_jpg(*precomp_tools, precomp_switches, input, jpg_start_pos, jpg_length, progressive_flag);
  }
  return std::unique_ptr<precompression_result>{};
}

int BrunsliStringWriter(void* data, const uint8_t* buf, size_t count) {
  auto output = reinterpret_cast<std::string*>(data);
  output->append(reinterpret_cast<const char*>(buf), count);
  return count;
}

class JpegFormatHeaderData: public PrecompFormatHeaderData {
public:
  bool mjpg_dht_used = false;
  bool brunsli_used = false;
};

std::unique_ptr<PrecompFormatHeaderData> JpegFormatHandler::read_format_header(IStreamLike &input, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) {
  auto fmt_hdr = std::make_unique<JpegFormatHeaderData>();

  fmt_hdr->mjpg_dht_used = (precomp_hdr_flags & std::byte{ 0b100 }) == std::byte{ 0b100 };
  fmt_hdr->brunsli_used = (precomp_hdr_flags & std::byte{ 0b1000 }) == std::byte{ 0b1000 };
  {
    const bool brotli_used = (precomp_hdr_flags & std::byte{ 0b10000 }) == std::byte{ 0b10000 };
    if (brotli_used) {
      throw PrecompError(ERR_BROTLI_NO_LONGER_SUPPORTED);
    }
  }

  fmt_hdr->original_size = fin_fget_vlint(input);
  fmt_hdr->precompressed_size = fin_fget_vlint(input);

  return fmt_hdr;
}

void JpegFormatHandler::recompress(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format) {
  print_to_log(PRECOMP_DEBUG_LOG, "Decompressed data - JPG\n");
  auto& jpeg_format_hdr_data = static_cast<JpegFormatHeaderData&>(precomp_hdr_data);

  auto random_tag = temp_files_tag();
  std::string precompressed_filename = precomp_tools->get_tempfile_name(random_tag + "_precompressed_jpg", false);
  std::string recompressed_filename = precomp_tools->get_tempfile_name(random_tag + "_original_jpg", false);

  print_to_log(PRECOMP_DEBUG_LOG, "Recompressed length: %lli - decompressed length: %lli\n", jpeg_format_hdr_data.original_size, jpeg_format_hdr_data.precompressed_size);

  char recompress_msg[256];
  std::vector<unsigned char> jpg_mem_in {};
  std::unique_ptr<unsigned char[]> jpg_mem_out;
  unsigned int jpg_mem_out_size = -1;
  bool in_memory = jpeg_format_hdr_data.original_size <= JPG_MAX_MEMORY_SIZE;
  bool recompress_success = false;

  if (in_memory) {
    jpg_mem_in.resize(jpeg_format_hdr_data.precompressed_size);
    auto memstream = memiostream::make(jpg_mem_in.data(), jpg_mem_in.data() + jpeg_format_hdr_data.precompressed_size);
    fast_copy(precompressed_input, *memstream, jpeg_format_hdr_data.precompressed_size);

    if (jpeg_format_hdr_data.brunsli_used) {
      brunsli::JPEGData jpegData;
      if (brunsli::BrunsliDecodeJpeg(jpg_mem_in.data(), jpeg_format_hdr_data.precompressed_size, &jpegData) == brunsli::BRUNSLI_OK) {
        if (jpeg_format_hdr_data.mjpg_dht_used) {
          auto mem = new unsigned char[jpeg_format_hdr_data.original_size + MJPGDHT_LEN];
          jpg_mem_out = std::unique_ptr<unsigned char[]>(mem);
        }
        else {
          auto mem = new unsigned char[jpeg_format_hdr_data.original_size];
          jpg_mem_out = std::unique_ptr<unsigned char[]>(mem);
        }
        std::string output;
        brunsli::JPEGOutput writer(BrunsliStringWriter, &output);
        if (brunsli::WriteJpeg(jpegData, writer)) {
          jpg_mem_out_size = output.length();
          memcpy(jpg_mem_out.get(), output.data(), jpg_mem_out_size);
          recompress_success = true;
        }
      }
    }
    else {
      unsigned char* mem = nullptr;
      pjglib_init_streams(jpg_mem_in.data(), 1, jpeg_format_hdr_data.precompressed_size, mem, 1);
      recompress_success = pjglib_convert_stream2mem(&mem, &jpg_mem_out_size, recompress_msg);
      jpg_mem_out = std::unique_ptr<unsigned char[]>(mem);
    }
  }
  else {
    dump_to_file(precompressed_input, precompressed_filename, jpeg_format_hdr_data.precompressed_size);

    remove(recompressed_filename.c_str());

    recompress_success = pjglib_convert_file2file(const_cast<char*>(precompressed_filename.c_str()), const_cast<char*>(recompressed_filename.c_str()), recompress_msg);
  }

  if (!recompress_success) {
    print_to_log(PRECOMP_DEBUG_LOG, "packJPG error: %s\n", recompress_msg);
    throw PrecompError(ERR_DURING_RECOMPRESSION);
  }

  PrecompTmpFile frecomp;
  if (!in_memory) {
    frecomp.open(recompressed_filename, std::ios_base::in | std::ios_base::binary);
  }

  if (jpeg_format_hdr_data.mjpg_dht_used) {
    long long frecomp_pos = 0;
    bool found_ffda = false;
    bool found_ff = false;
    int ffda_pos = -1;

    if (in_memory) {
      do {
        ffda_pos++;
        if (ffda_pos >= (int)jpg_mem_out_size) break;
        if (found_ff) {
          found_ffda = jpg_mem_out[ffda_pos] == 0xDA;
          if (found_ffda) break;
          found_ff = false;
        }
        else {
          found_ff = jpg_mem_out[ffda_pos] == 0xFF;
        }
      } while (!found_ffda);
    }
    else {
      do {
        ffda_pos++;
        unsigned char chr[1];
        frecomp.read(reinterpret_cast<char*>(chr), 1);
        if (frecomp.gcount() != 1) break;
        if (found_ff) {
          found_ffda = chr[0] == 0xDA;
          if (found_ffda) break;
          found_ff = false;
        }
        else {
          found_ff = chr[0] == 0xFF;
        }
      } while (!found_ffda);
    }

    if (!found_ffda || ffda_pos - 1 - MJPGDHT_LEN < 0) {
      throw std::runtime_error(make_cstyle_format_string("ERROR: Motion JPG stream corrupted\n"));
    }

    // remove motion JPG huffman table
    if (in_memory) {
      auto memstream1 = memiostream::make(jpg_mem_out.get(), jpg_mem_out.get() + ffda_pos - 1 - MJPGDHT_LEN);
      fast_copy(*memstream1, recompressed_stream, ffda_pos - 1 - MJPGDHT_LEN);
      auto memstream2 = memiostream::make(jpg_mem_out.get() + (ffda_pos - 1), jpg_mem_out.get() + (jpeg_format_hdr_data.original_size + MJPGDHT_LEN) - (ffda_pos - 1));
      fast_copy(*memstream2, recompressed_stream, jpeg_format_hdr_data.original_size + MJPGDHT_LEN - (ffda_pos - 1));
    }
    else {
      frecomp.seekg(frecomp_pos, std::ios_base::beg);
      fast_copy(frecomp, recompressed_stream, ffda_pos - 1 - MJPGDHT_LEN);

      frecomp_pos += ffda_pos - 1;
      frecomp.seekg(frecomp_pos, std::ios_base::beg);
      fast_copy(frecomp, recompressed_stream, jpeg_format_hdr_data.original_size + MJPGDHT_LEN - (ffda_pos - 1));
    }
  }
  else {
    if (in_memory) {
      auto memstream = memiostream::make(jpg_mem_out.get(), jpg_mem_out.get() + jpeg_format_hdr_data.original_size);
      fast_copy(*memstream, recompressed_stream, jpeg_format_hdr_data.original_size);
    }
    else {
      fast_copy(frecomp, recompressed_stream, jpeg_format_hdr_data.original_size);
    }
  }

  if (!in_memory) {
    frecomp.close();
  }
}
