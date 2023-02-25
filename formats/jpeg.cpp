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

bool jpeg_header_check(const std::span<unsigned char> checkbuf_span) {
  auto checkbuf = checkbuf_span.data();
  // SOI (FF D8) followed by a valid marker for Baseline/Progressive JPEGs
  return
    *checkbuf == 0xFF && *(checkbuf + 1) == 0xD8 &&
    *(checkbuf + 2) == 0xFF && (
      *(checkbuf + 3) == 0xC0 || *(checkbuf + 3) == 0xC2 || *(checkbuf + 3) == 0xC4 || (*(checkbuf + 3) >= 0xDB && *(checkbuf + 3) <= 0xFE)
      );
}

precompression_result try_decompression_jpg(Precomp& precomp_mgr, long long jpg_length, bool progressive_jpg) {
  precompression_result result = precompression_result(D_JPG);
  auto random_tag = temp_files_tag();
  std::string original_jpg_filename = random_tag + "_original_" + "jpg";
  std::string decompressed_jpg_filename = random_tag + "_precompressed_" + "jpg";
  std::unique_ptr<PrecompTmpFile> tmpfile = std::make_unique<PrecompTmpFile>();
  tmpfile->open(original_jpg_filename, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
  tmpfile->close();

  if (progressive_jpg) {
    print_to_log(PRECOMP_DEBUG_LOG, "Possible JPG (progressive) found at position ");
  }
  else {
    print_to_log(PRECOMP_DEBUG_LOG, "Possible JPG found at position ");
  }
  print_to_log(PRECOMP_DEBUG_LOG, "%lli, length %lli\n", precomp_mgr.ctx->saved_input_file_pos, jpg_length);
  // do not recompress non-progressive JPGs when prog_only is set
  if ((!progressive_jpg) && (precomp_mgr.switches.prog_only)) {
    print_to_log(PRECOMP_DEBUG_LOG, "Skipping (only progressive JPGs mode set)\n");
    return result;
  }

  bool jpg_success = false;
  bool recompress_success = false;
  bool mjpg_dht_used = false;
  bool brunsli_used = false;
  bool brotli_used = precomp_mgr.switches.use_brotli;
  char recompress_msg[256];
  unsigned char* jpg_mem_in = nullptr;
  unsigned char* jpg_mem_out = nullptr;
  unsigned int jpg_mem_out_size = -1;
  bool in_memory = ((jpg_length + MJPGDHT_LEN) <= JPG_MAX_MEMORY_SIZE);

  if (in_memory) { // small stream => do everything in memory
    precomp_mgr.ctx->fin->seekg(precomp_mgr.ctx->input_file_pos, std::ios_base::beg);
    jpg_mem_in = new unsigned char[jpg_length + MJPGDHT_LEN];
    memiostream memstream = memiostream::make(jpg_mem_in, jpg_mem_in + jpg_length);
    fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, memstream, jpg_length);

    bool brunsli_success = false;

    if (precomp_mgr.switches.use_brunsli) {
      print_to_log(PRECOMP_DEBUG_LOG, "Trying to compress using brunsli...\n");
      brunsli::JPEGData jpegData;
      if (brunsli::ReadJpeg(jpg_mem_in, jpg_length, brunsli::JPEG_READ_ALL, &jpegData)) {
        size_t output_size = brunsli::GetMaximumBrunsliEncodedSize(jpegData);
        jpg_mem_out = new unsigned char[output_size];
        if (brunsli::BrunsliEncodeJpeg(jpegData, jpg_mem_out, &output_size, precomp_mgr.switches.use_brotli)) {
          recompress_success = true;
          brunsli_success = true;
          brunsli_used = true;
          jpg_mem_out_size = output_size;
        }
        else {
          delete[] jpg_mem_out;
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

            memmove(jpg_mem_in + (ffda_pos - 1) + MJPGDHT_LEN, jpg_mem_in + (ffda_pos - 1), jpg_length - (ffda_pos - 1));
            memcpy(jpg_mem_in + (ffda_pos - 1), MJPGDHT, MJPGDHT_LEN);

            if (brunsli::ReadJpeg(jpg_mem_in, jpg_length + MJPGDHT_LEN, brunsli::JPEG_READ_ALL, &jpegData)) {
              size_t output_size = brunsli::GetMaximumBrunsliEncodedSize(jpegData);
              jpg_mem_out = new unsigned char[output_size];
              if (brunsli::BrunsliEncodeJpeg(jpegData, jpg_mem_out, &output_size, precomp_mgr.switches.use_brotli)) {
                recompress_success = true;
                brunsli_success = true;
                brunsli_used = true;
                mjpg_dht_used = true;
                jpg_mem_out_size = output_size;
              }
              else {
                delete[] jpg_mem_out;
                jpg_mem_out = nullptr;
              }
            }

            if (!brunsli_success) {
              // revert DHT insertion
              memmove(jpg_mem_in + (ffda_pos - 1), jpg_mem_in + (ffda_pos - 1) + MJPGDHT_LEN, jpg_length - (ffda_pos - 1));
            }
          }
        }
      }
      if (!brunsli_success) {
        if (precomp_mgr.switches.use_packjpg_fallback) {
          print_to_log(PRECOMP_DEBUG_LOG, "Brunsli compression failed, using packJPG fallback...\n");
        }
        else {
          print_to_log(PRECOMP_DEBUG_LOG, "Brunsli compression failed\n");
        }
      }
    }

    if ((!precomp_mgr.switches.use_brunsli || !brunsli_success) && precomp_mgr.switches.use_packjpg_fallback) {
      pjglib_init_streams(jpg_mem_in, 1, jpg_length, jpg_mem_out, 1);
      recompress_success = pjglib_convert_stream2mem(&jpg_mem_out, &jpg_mem_out_size, recompress_msg);
      brunsli_used = false;
      brotli_used = false;
    }
  }
  else if (precomp_mgr.switches.use_packjpg_fallback) { // large stream => use temporary files
    print_to_log(PRECOMP_DEBUG_LOG, "JPG too large for brunsli, using packJPG fallback...\n");
    // try to decompress at current position
    {
      WrappedFStream decompressed_jpg;
      decompressed_jpg.open(decompressed_jpg_filename, std::ios_base::out | std::ios_base::binary);
      precomp_mgr.ctx->fin->seekg(precomp_mgr.ctx->input_file_pos, std::ios_base::beg);
      fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, decompressed_jpg, jpg_length);
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
    brotli_used = false;
  }

  if ((!recompress_success) && (strncmp(recompress_msg, "huffman table missing", 21) == 0) && (precomp_mgr.switches.use_mjpeg) && (precomp_mgr.switches.use_packjpg_fallback)) {
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
        memmove(jpg_mem_in + (ffda_pos - 1) + MJPGDHT_LEN, jpg_mem_in + (ffda_pos - 1), jpg_length - (ffda_pos - 1));
        memcpy(jpg_mem_in + (ffda_pos - 1), MJPGDHT, MJPGDHT_LEN);

        pjglib_init_streams(jpg_mem_in, 1, jpg_length + MJPGDHT_LEN, jpg_mem_out, 1);
        recompress_success = pjglib_convert_stream2mem(&jpg_mem_out, &jpg_mem_out_size, recompress_msg);
      }
    }
    else {
      WrappedFStream decompressed_jpg;
      decompressed_jpg.open(tmpfile->file_path, std::ios_base::in | std::ios_base::binary);
      do {
        ffda_pos++;
        decompressed_jpg.read(reinterpret_cast<char*>(precomp_mgr.in), 1);
        if (decompressed_jpg.gcount() != 1) break;
        if (found_ff) {
          found_ffda = (precomp_mgr.in[0] == 0xDA);
          if (found_ffda) break;
          found_ff = false;
        }
        else {
          found_ff = (precomp_mgr.in[0] == 0xFF);
        }
      } while (!found_ffda);
      std::string mjpgdht_tempfile = tmpfile->file_path + "_mjpgdht";
      if (found_ffda) {
        WrappedFStream decompressed_jpg_w_MJPGDHT;
        decompressed_jpg_w_MJPGDHT.open(mjpgdht_tempfile, std::ios_base::out | std::ios_base::binary);
        decompressed_jpg.seekg(0, std::ios_base::beg);
        fast_copy(precomp_mgr, decompressed_jpg, decompressed_jpg_w_MJPGDHT, ffda_pos - 1);
        // insert MJPGDHT
        decompressed_jpg_w_MJPGDHT.write(reinterpret_cast<char*>(MJPGDHT), MJPGDHT_LEN);
        decompressed_jpg.seekg(ffda_pos - 1, std::ios_base::beg);
        fast_copy(precomp_mgr, decompressed_jpg, decompressed_jpg_w_MJPGDHT, jpg_length - (ffda_pos - 1));
      }
      decompressed_jpg.close();
      recompress_success = pjglib_convert_file2file(const_cast<char*>(mjpgdht_tempfile.c_str()), const_cast<char*>(tmpfile->file_path.c_str()), recompress_msg);
    }

    mjpg_dht_used = recompress_success;
  }

  precomp_mgr.statistics.decompressed_streams_count++;
  if (progressive_jpg) {
    precomp_mgr.statistics.decompressed_jpg_prog_count++;
  }
  else {
    precomp_mgr.statistics.decompressed_jpg_count++;
  }

  if ((!recompress_success) && (precomp_mgr.switches.use_packjpg_fallback)) {
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
      WrappedFStream ftempout;
      ftempout.open(tmpfile->file_path, std::ios_base::in | std::ios_base::binary);
      ftempout.seekg(0, std::ios_base::end);
      jpg_new_length = ftempout.tellg();
      ftempout.close();
    }

    if (jpg_new_length.has_value()) {
      precomp_mgr.statistics.recompressed_streams_count++;
      if (progressive_jpg) {
        precomp_mgr.statistics.recompressed_jpg_prog_count++;
      }
      else {
        precomp_mgr.statistics.recompressed_jpg_count++;
      }
      precomp_mgr.ctx->non_zlib_was_used = true;

      result.original_size = jpg_length;
      result.precompressed_size = jpg_new_length.value();
      jpg_success = true;
    }
  }

  if (jpg_success) {
    print_to_log(PRECOMP_DEBUG_LOG, "Best match: %lli bytes, recompressed to %lli bytes\n", result.original_size, result.precompressed_size);

    result.success = true;
    std::byte jpg_flags{ 0b1 }; // no penalty bytes
    if (mjpg_dht_used) jpg_flags |= std::byte{ 0b100 }; // motion JPG DHT used
    if (brunsli_used) jpg_flags |= std::byte{ 0b1000 };
    if (brotli_used) jpg_flags |= std::byte{ 0b10000 };
    result.flags = jpg_flags;

    if (in_memory) {
      auto memstream = memiostream::make_copy(jpg_mem_out, jpg_mem_out + result.precompressed_size);
      result.precompressed_stream = std::move(memstream);
    }
    else {
      tmpfile->reopen();
      result.precompressed_stream = std::move(tmpfile);
    }
  }
  else {
    print_to_log(PRECOMP_DEBUG_LOG, "No matches\n");
  }

  delete[] jpg_mem_in;
  delete[] jpg_mem_out;
  return result;
}

precompression_result precompress_jpeg(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span) {
  precompression_result result = precompression_result(D_JPG);
  bool done = false, found = false;
  bool hasQuantTable = (*(checkbuf_span.data() + 3) == 0xDB);
  bool progressive_flag = (*(checkbuf_span.data() + 3) == 0xC2);
  precomp_mgr.ctx->input_file_pos += 2;

  do {
    precomp_mgr.ctx->fin->seekg(precomp_mgr.ctx->input_file_pos, std::ios_base::beg);
    precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 5);
    if ((precomp_mgr.ctx->fin->gcount() != 5) || (precomp_mgr.in[0] != 0xFF))
      break;
    int length = (int)precomp_mgr.in[2] * 256 + (int)precomp_mgr.in[3];
    switch (precomp_mgr.in[1]) {
    case 0xDB: {
      // FF DB XX XX QtId ...
      // Marker length (XX XX) must be = 2 + (multiple of 65 <= 260)
      // QtId:
      // bit 0..3: number of QT (0..3, otherwise error)
      // bit 4..7: precision of QT, 0 = 8 bit, otherwise 16 bit               
      if (length <= 262 && ((length - 2) % 65) == 0 && precomp_mgr.in[4] <= 3) {
        hasQuantTable = true;
        precomp_mgr.ctx->input_file_pos += length + 2;
      }
      else
        done = true;
      break;
    }
    case 0xC4: {
      done = ((precomp_mgr.in[4] & 0xF) > 3 || (precomp_mgr.in[4] >> 4) > 1);
      precomp_mgr.ctx->input_file_pos += length + 2;
      break;
    }
    case 0xDA: found = hasQuantTable;
    case 0xD9: done = true; break; //EOI with no SOS?
    case 0xC2: progressive_flag = true;
    case 0xC0: done = (precomp_mgr.in[4] != 0x08);
    default: precomp_mgr.ctx->input_file_pos += length + 2;
    }
  } while (!done);

  if (found) {
    found = done = false;
    precomp_mgr.ctx->input_file_pos += 5;

    bool isMarker = (precomp_mgr.in[4] == 0xFF);
    size_t bytesRead = 0;
    for (;;) {
      if (done) break;
      precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), sizeof(precomp_mgr.in[0]) * CHUNK);
      bytesRead = precomp_mgr.ctx->fin->gcount();
      if (!bytesRead) break;
      for (size_t i = 0; !done && (i < bytesRead); i++) {
        precomp_mgr.ctx->input_file_pos++;
        if (!isMarker) {
          isMarker = (precomp_mgr.in[i] == 0xFF);
        }
        else {
          done = (precomp_mgr.in[i] && ((precomp_mgr.in[i] & 0xF8) != 0xD0) && ((progressive_flag) ? (precomp_mgr.in[i] != 0xC4) && (precomp_mgr.in[i] != 0xDA) : true));
          found = (precomp_mgr.in[i] == 0xD9);
          isMarker = false;
        }
      }
    }
  }

  if (found) {
    long long jpg_length = precomp_mgr.ctx->input_file_pos - precomp_mgr.ctx->saved_input_file_pos;
    precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
    result = try_decompression_jpg(precomp_mgr, jpg_length, progressive_flag);
  }
  return result;
}

int BrunsliStringWriter(void* data, const uint8_t* buf, size_t count) {
  auto output = reinterpret_cast<std::string*>(data);
  output->append(reinterpret_cast<const char*>(buf), count);
  return count;
}

void recompress_jpg(Precomp& precomp_mgr, std::byte flags) {
  print_to_log(PRECOMP_DEBUG_LOG, "Decompressed data - JPG\n");

  std::string precompressed_filename = temp_files_tag() + "_precompressed_jpg";
  std::string recompressed_filename = temp_files_tag() + "_original_jpg";

  bool mjpg_dht_used = (flags & std::byte{ 0b100 }) == std::byte{ 0b100 };
  bool brunsli_used = (flags & std::byte{ 0b1000 }) == std::byte{ 0b1000 };
  bool brotli_used = (flags & std::byte{ 0b10000 }) == std::byte{ 0b10000 };

  long long recompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);
  long long precompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);

  print_to_log(PRECOMP_DEBUG_LOG, "Recompressed length: %lli - decompressed length: %lli\n", recompressed_data_length, precompressed_data_length);

  char recompress_msg[256];
  unsigned char* jpg_mem_in = nullptr;
  unsigned char* jpg_mem_out = nullptr;
  unsigned int jpg_mem_out_size = -1;
  bool in_memory = recompressed_data_length <= JPG_MAX_MEMORY_SIZE;
  bool recompress_success = false;

  if (in_memory) {
    jpg_mem_in = new unsigned char[precompressed_data_length];
    memiostream memstream = memiostream::make(jpg_mem_in, jpg_mem_in + precompressed_data_length);
    fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, memstream, precompressed_data_length);

    if (brunsli_used) {
      brunsli::JPEGData jpegData;
      if (brunsli::BrunsliDecodeJpeg(jpg_mem_in, precompressed_data_length, &jpegData, brotli_used) == brunsli::BRUNSLI_OK) {
        if (mjpg_dht_used) {
          jpg_mem_out = new unsigned char[recompressed_data_length + MJPGDHT_LEN];
        }
        else {
          jpg_mem_out = new unsigned char[recompressed_data_length];
        }
        std::string output;
        brunsli::JPEGOutput writer(BrunsliStringWriter, &output);
        if (brunsli::WriteJpeg(jpegData, writer)) {
          jpg_mem_out_size = output.length();
          memcpy(jpg_mem_out, output.data(), jpg_mem_out_size);
          recompress_success = true;
        }
      }
    }
    else {
      pjglib_init_streams(jpg_mem_in, 1, precompressed_data_length, jpg_mem_out, 1);
      recompress_success = pjglib_convert_stream2mem(&jpg_mem_out, &jpg_mem_out_size, recompress_msg);
    }
  }
  else {
    remove(precompressed_filename.c_str());

    {
      WrappedFStream ftempout;
      ftempout.open(precompressed_filename, std::ios_base::out | std::ios_base::binary);
      fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, ftempout, precompressed_data_length);
      ftempout.close();
    }

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

  if (mjpg_dht_used) {
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
        frecomp.read(reinterpret_cast<char*>(precomp_mgr.in), 1);
        if (frecomp.gcount() != 1) break;
        if (found_ff) {
          found_ffda = precomp_mgr.in[0] == 0xDA;
          if (found_ffda) break;
          found_ff = false;
        }
        else {
          found_ff = precomp_mgr.in[0] == 0xFF;
        }
      } while (!found_ffda);
    }

    if (!found_ffda || ffda_pos - 1 - MJPGDHT_LEN < 0) {
      throw std::runtime_error(make_cstyle_format_string("ERROR: Motion JPG stream corrupted\n"));
    }

    // remove motion JPG huffman table
    if (in_memory) {
      memiostream memstream1 = memiostream::make(jpg_mem_out, jpg_mem_out + ffda_pos - 1 - MJPGDHT_LEN);
      fast_copy(precomp_mgr, memstream1, *precomp_mgr.ctx->fout, ffda_pos - 1 - MJPGDHT_LEN);
      memiostream memstream2 = memiostream::make(jpg_mem_out + (ffda_pos - 1), jpg_mem_out + (recompressed_data_length + MJPGDHT_LEN) - (ffda_pos - 1));
      fast_copy(precomp_mgr, memstream2, *precomp_mgr.ctx->fout, recompressed_data_length + MJPGDHT_LEN - (ffda_pos - 1));
    }
    else {
      frecomp.seekg(frecomp_pos, std::ios_base::beg);
      fast_copy(precomp_mgr, frecomp, *precomp_mgr.ctx->fout, ffda_pos - 1 - MJPGDHT_LEN);

      frecomp_pos += ffda_pos - 1;
      frecomp.seekg(frecomp_pos, std::ios_base::beg);
      fast_copy(precomp_mgr, frecomp, *precomp_mgr.ctx->fout, recompressed_data_length + MJPGDHT_LEN - (ffda_pos - 1));
    }
  }
  else {
    if (in_memory) {
      memiostream memstream = memiostream::make(jpg_mem_out, jpg_mem_out + recompressed_data_length);
      fast_copy(precomp_mgr, memstream, *precomp_mgr.ctx->fout, recompressed_data_length);
    }
    else {
      fast_copy(precomp_mgr, frecomp, *precomp_mgr.ctx->fout, recompressed_data_length);
    }
  }

  if (in_memory) {
    delete[] jpg_mem_in;
    delete[] jpg_mem_out;
  }
  else {
    frecomp.close();

    remove(recompressed_filename.c_str());
    remove(precompressed_filename.c_str());
  }
}
