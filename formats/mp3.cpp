#include "mp3.h"

#include <cstring>
#include <filesystem>
#include <memory>

#include "contrib/packmp3/precomp_mp3.h"

const char* packmp3_version_info() {
  return pmplib_version_info();
}

bool mp3_header_check(const std::span<unsigned char> checkbuf_span) {
  return *checkbuf_span.data() == 0xFF && (*(checkbuf_span.data() + 1) & 0xE0) == 0xE0;
}

class mp3_suppression_vars {
public:
  mp3_suppression_vars() {
    // init MP3 suppression
    for (long long& i : suppress_mp3_type_until) {
      i = -1;
    }
  }

  std::array<long long, 16> suppress_mp3_type_until{};
  long long suppress_mp3_big_value_pairs_sum = -1;
  long long suppress_mp3_non_zero_padbits_sum = -1;
  long long suppress_mp3_inconsistent_emphasis_sum = -1;
  long long suppress_mp3_inconsistent_original_bit = -1;
  long long mp3_parsing_cache_second_frame = -1;
  long long mp3_parsing_cache_n;
  long long mp3_parsing_cache_mp3_length;
};

precompression_result try_precompression_mp3(Precomp& precomp_mgr, long long original_input_pos, long long mp3_length, std::string tmp_filename, mp3_suppression_vars& suppression) {
  precompression_result result = precompression_result(D_MP3);
  std::unique_ptr<PrecompTmpFile> tmpfile = std::make_unique<PrecompTmpFile>();
  tmpfile->open(tmp_filename, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
  std::string decompressed_mp3_filename = tmp_filename + "_";
  tmpfile->close();

  print_to_log(PRECOMP_DEBUG_LOG, "Possible MP3 found at position %lli, length %lli\n", original_input_pos, mp3_length);

  bool mp3_success = false;
  bool recompress_success;
  char recompress_msg[256];
  unsigned char* mp3_mem_in = nullptr;
  std::unique_ptr<unsigned char[]> mp3_mem_out = nullptr;
  unsigned int mp3_mem_out_size = -1;
  bool in_memory = (mp3_length <= MP3_MAX_MEMORY_SIZE);

  if (in_memory) { // small stream => do everything in memory
    precomp_mgr.ctx->fin->seekg(original_input_pos, std::ios_base::beg);
    mp3_mem_in = new unsigned char[mp3_length];
    auto memstream = memiostream::make(mp3_mem_in, mp3_mem_in + mp3_length);
    fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, *memstream, mp3_length);

    unsigned char* mem = nullptr;
    pmplib_init_streams(mp3_mem_in, 1, mp3_length, mem, 1);
    recompress_success = pmplib_convert_stream2mem(&mem, &mp3_mem_out_size, recompress_msg);
    mp3_mem_out = std::unique_ptr<unsigned char[]>(mem);
  }
  else { // large stream => use temporary files
    // try to decompress at current position
    {
      WrappedFStream decompressed_mp3;
      decompressed_mp3.open(decompressed_mp3_filename, std::ios_base::out | std::ios_base::binary);
      precomp_mgr.ctx->fin->seekg(original_input_pos, std::ios_base::beg);
      fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, decompressed_mp3, mp3_length);
      decompressed_mp3.close();
    }

    // workaround for bugs, similar to packJPG
    {
      remove(tmpfile->file_path.c_str());
      std::fstream fworkaround;
      fworkaround.open(tmpfile->file_path, std::ios_base::out | std::ios_base::binary);
      fworkaround.close();
    }

    recompress_success = pmplib_convert_file2file(const_cast<char*>(decompressed_mp3_filename.c_str()), const_cast<char*>(tmpfile->file_path.c_str()), recompress_msg);
  }

  if ((!recompress_success) && (strncmp(recompress_msg, "synching failure", 16) == 0)) {
    int frame_n;
    int pos;
    if (sscanf(recompress_msg, "synching failure (frame #%i at 0x%X)", &frame_n, &pos) == 2) {
      if ((pos > 0) && (pos < mp3_length)) {
        mp3_length = pos;

        print_to_log(PRECOMP_DEBUG_LOG, "Too much garbage data at the end, retry with new length %i\n", pos);

        if (in_memory) {
          unsigned char* mem = nullptr;
          pmplib_init_streams(mp3_mem_in, 1, mp3_length, mem, 1);
          recompress_success = pmplib_convert_stream2mem(&mem, &mp3_mem_out_size, recompress_msg);
          mp3_mem_out = std::unique_ptr<unsigned char[]>(mem);
        }
        else {
          std::filesystem::resize_file(decompressed_mp3_filename, pos);

          // workaround for bugs, similar to packJPG
          {
            remove(tmpfile->file_path.c_str());
            std::fstream fworkaround;
            fworkaround.open(tmpfile->file_path, std::ios_base::out | std::ios_base::binary);
          }

          recompress_success = pmplib_convert_file2file(const_cast<char*>(decompressed_mp3_filename.c_str()), const_cast<char*>(tmpfile->file_path.c_str()), recompress_msg);
        }
      }
    }
  }
  else if ((!recompress_success) && (strncmp(recompress_msg, "big value pairs out of bounds", 29) == 0)) {
    suppression.suppress_mp3_big_value_pairs_sum = original_input_pos + mp3_length;
    print_to_log(PRECOMP_DEBUG_LOG, "Ignoring following streams with position/length sum %lli to avoid slowdown\n", suppression.suppress_mp3_big_value_pairs_sum);
  }
  else if ((!recompress_success) && (strncmp(recompress_msg, "non-zero padbits found", 22) == 0)) {
    suppression.suppress_mp3_non_zero_padbits_sum = original_input_pos + mp3_length;
    print_to_log(PRECOMP_DEBUG_LOG, "Ignoring following streams with position/length sum %lli to avoid slowdown\n", suppression.suppress_mp3_non_zero_padbits_sum);
  }
  else if ((!recompress_success) && (strncmp(recompress_msg, "inconsistent use of emphasis", 28) == 0)) {
    suppression.suppress_mp3_inconsistent_emphasis_sum = original_input_pos + mp3_length;
    print_to_log(PRECOMP_DEBUG_LOG, "Ignoring following streams with position/length sum %lli to avoid slowdown\n", suppression.suppress_mp3_inconsistent_emphasis_sum);
  }
  else if ((!recompress_success) && (strncmp(recompress_msg, "inconsistent original bit", 25) == 0)) {
    suppression.suppress_mp3_inconsistent_original_bit = original_input_pos + mp3_length;
    print_to_log(PRECOMP_DEBUG_LOG, "Ignoring following streams with position/length sum %lli to avoid slowdown\n", suppression.suppress_mp3_inconsistent_original_bit);
  }

  precomp_mgr.statistics.decompressed_streams_count++;
  precomp_mgr.statistics.decompressed_mp3_count++;

  if (!recompress_success) {
    print_to_log(PRECOMP_DEBUG_LOG, "packMP3 error: %s\n", recompress_msg);
  }

  if (recompress_success) {
    auto mp3_new_length = in_memory ? mp3_mem_out_size : std::filesystem::file_size(tmpfile->file_path);

    if (mp3_new_length > 0) {
      precomp_mgr.statistics.recompressed_streams_count++;
      precomp_mgr.statistics.recompressed_mp3_count++;
      precomp_mgr.ctx->non_zlib_was_used = true;

      result.original_size = mp3_length;
      result.precompressed_size = mp3_new_length;
      mp3_success = true;
    }
  }

  if (mp3_success) {
    result.success = true;
    print_to_log(PRECOMP_DEBUG_LOG, "Best match: %lli bytes, recompressed to %lli\n", result.original_size, result.precompressed_size);

    if (in_memory) {
      auto mem = mp3_mem_out.release();
      auto memstream = memiostream::make(mem, mem + result.precompressed_size, true);
      result.precompressed_stream = std::move(memstream);
    }
    else {
      tmpfile->reopen();
      result.precompressed_stream = std::move(tmpfile);
    }

    result.flags = std::byte{ 0b1 }; // no penalty bytes
  }
  else {
    print_to_log(PRECOMP_DEBUG_LOG, "No matches\n");
  }

  delete[] mp3_mem_in;
  return result;
}

/* -----------------------------------------------
  calculate frame crc
  ----------------------------------------------- */
inline unsigned short mp3_calc_layer3_crc(unsigned char header2, unsigned char header3, const unsigned char* sideinfo, int sidesize)
{
  // crc has a start value of 0xFFFF
  unsigned short crc = 0xFFFF;

  // process two last bytes from header...
  crc = (crc << 8) ^ crc_table[(crc >> 8) ^ header2];
  crc = (crc << 8) ^ crc_table[(crc >> 8) ^ header3];
  // ... and all the bytes from the side information
  for (int i = 0; i < sidesize; i++)
    crc = (crc << 8) ^ crc_table[(crc >> 8) ^ sideinfo[i]];

  return crc;
}

bool is_valid_mp3_frame(unsigned char* frame_data, unsigned char header2, unsigned char header3, int protection) {
  unsigned char channels = (header3 >> 6) & 0x3;
  int nch = (channels == MP3_MONO) ? 1 : 2;
  int nsb, gr, ch;
  unsigned short crc;
  unsigned char* sideinfo;

  nsb = (nch == 1) ? 17 : 32;

  sideinfo = frame_data;
  if (protection == 0x0) {
    sideinfo += 2;
    // if there is a crc: check and discard
    crc = (frame_data[0] << 8) + frame_data[1];
    if (crc != mp3_calc_layer3_crc(header2, header3, sideinfo, nsb)) {
      // crc checksum mismatch
      return false;
    }
  }

  auto side_reader = new abitreader(sideinfo, nsb);

  side_reader->read((nch == 1) ? 18 : 20);

  // granule specific side info
  char window_switching, region0_size, region1_size;
  for (gr = 0; gr < 2; gr++) {
    for (ch = 0; ch < nch; ch++) {
      side_reader->read(32);
      side_reader->read(1);
      window_switching = (char)side_reader->read(1);
      if (window_switching == 0) {
        side_reader->read(15);
        region0_size = (char)side_reader->read(4);
        region1_size = (char)side_reader->read(3);
        if (region0_size + region1_size > 20) {
          // region size out of bounds
          delete side_reader;
          return false;
        }
      }
      else {
        side_reader->read(22);
      }
      side_reader->read(3);
    }
  }

  delete side_reader;

  return true;
}

precompression_result precompress_mp3(Precomp& precomp_mgr, long long original_input_pos, const std::span<unsigned char> checkbuf_span) {
  mp3_suppression_vars suppression;
  precompression_result result = precompression_result(D_MP3);
  int mpeg = -1;
  int layer = -1;
  int samples = -1;
  int channels = -1;
  int protection = -1;
  int type = -1;

  int bits;
  int padding;
  int frame_size;
  long long n = 0;
  long long mp3_parsing_cache_second_frame_candidate = -1;
  long long mp3_parsing_cache_second_frame_candidate_size = -1;

  long long mp3_length = 0;

  long long act_pos = original_input_pos;

  // parse frames until first invalid frame is found or end-of-file
  std::array<unsigned char, 4> frame_hdr{};
  auto memstream = memiostream::make(checkbuf_span.data(), checkbuf_span.data() + checkbuf_span.size());

  for (;;) {
    if (!read_with_memstream_buffer(*precomp_mgr.ctx->fin, memstream, reinterpret_cast<char*>(frame_hdr.data()), 4, act_pos)) break;
    act_pos -= 4;

    // check syncword
    if ((frame_hdr[0] != 0xFF) || ((frame_hdr[1] & 0xE0) != 0xE0)) break;
    // compare data from header
    if (n == 0) {
      mpeg = (frame_hdr[1] >> 3) & 0x3;
      layer = (frame_hdr[1] >> 1) & 0x3;
      protection = (frame_hdr[1] >> 0) & 0x1;
      samples = (frame_hdr[2] >> 2) & 0x3;
      channels = (frame_hdr[3] >> 6) & 0x3;
      type = MBITS(frame_hdr[1], 5, 1);
      // avoid slowdown and multiple verbose messages on unsupported types that have already been detected
      if ((type != MPEG1_LAYER_III) && (original_input_pos <= suppression.suppress_mp3_type_until[type])) {
        break;
      }
    }
    else {
      if (n == 1) {
        mp3_parsing_cache_second_frame_candidate = act_pos;
        mp3_parsing_cache_second_frame_candidate_size = act_pos - original_input_pos;
      }
      if (type == MPEG1_LAYER_III) { // supported MP3 type, all header information must be identical to the first frame
        if (
          (mpeg != ((frame_hdr[1] >> 3) & 0x3)) ||
          (layer != ((frame_hdr[1] >> 1) & 0x3)) ||
          (protection != ((frame_hdr[1] >> 0) & 0x1)) ||
          (samples != ((frame_hdr[2] >> 2) & 0x3)) ||
          (channels != ((frame_hdr[3] >> 6) & 0x3)) ||
          (type != MBITS(frame_hdr[1], 5, 1))) break;
      }
      else { // unsupported type, compare only type, ignore the other header information to get a longer stream
        if (type != MBITS(frame_hdr[1], 5, 1)) break;
      }
    }

    bits = (frame_hdr[2] >> 4) & 0xF;
    padding = (frame_hdr[2] >> 1) & 0x1;
    // check for problems
    if ((mpeg == 0x1) || (layer == 0x0) ||
      (bits == 0x0) || (bits == 0xF) || (samples == 0x3)) break;
    // find out frame size
    frame_size = frame_size_table[mpeg][layer][samples][bits];
    if (padding) frame_size += (layer == LAYER_I) ? 4 : 1;

    // if this frame was part of a stream that already has been parsed, skip parsing
    if (n == 0) {
      if (act_pos == suppression.mp3_parsing_cache_second_frame) {
        n = suppression.mp3_parsing_cache_n;
        mp3_length = suppression.mp3_parsing_cache_mp3_length;

        // update values
        suppression.mp3_parsing_cache_second_frame = act_pos + frame_size;
        suppression.mp3_parsing_cache_n -= 1;
        suppression.mp3_parsing_cache_mp3_length -= frame_size;

        break;
      }
    }

    n++;
    mp3_length += frame_size;
    act_pos += frame_size;

    // if supported MP3 type, validate frames
    if ((type == MPEG1_LAYER_III) && (frame_size > 4)) {
      unsigned char header2 = frame_hdr[2];
      unsigned char header3 = frame_hdr[3];

      act_pos = act_pos - frame_size + 4;
      if (!read_with_memstream_buffer(*precomp_mgr.ctx->fin, memstream, reinterpret_cast<char*>(precomp_mgr.in), frame_size - 4, act_pos)) {
        // discard incomplete frame
        n--;
        mp3_length -= frame_size;
        break;
      }
      if (!is_valid_mp3_frame(precomp_mgr.in, header2, header3, protection)) {
        n = 0;
        break;
      }
    }
  }

  // conditions for proper first frame: 5 consecutive frames
  if (n >= 5) {
    if (mp3_parsing_cache_second_frame_candidate > -1) {
      suppression.mp3_parsing_cache_second_frame = mp3_parsing_cache_second_frame_candidate;
      suppression.mp3_parsing_cache_n = n - 1;
      suppression.mp3_parsing_cache_mp3_length = mp3_length - mp3_parsing_cache_second_frame_candidate_size;
    }

    long long position_length_sum = original_input_pos + mp3_length;

    // type must be MPEG-1, Layer III, packMP3 won't process any other files
    if (type == MPEG1_LAYER_III) {
      // sums of position and length of last MP3 errors are suppressed to avoid slowdowns
      if ((suppression.suppress_mp3_big_value_pairs_sum != position_length_sum)
        && (suppression.suppress_mp3_non_zero_padbits_sum != position_length_sum)
        && (suppression.suppress_mp3_inconsistent_emphasis_sum != position_length_sum)
        && (suppression.suppress_mp3_inconsistent_emphasis_sum != position_length_sum)) {
        return try_precompression_mp3(precomp_mgr, original_input_pos, mp3_length, temp_files_tag() + "_decomp_mp3", suppression);
      }
    }
    else if (type > 0) {
      suppression.suppress_mp3_type_until[type] = position_length_sum;
      print_to_log(PRECOMP_DEBUG_LOG, "Unsupported MP3 type found at position %lli, length %lli\n", original_input_pos, mp3_length);
      print_to_log(PRECOMP_DEBUG_LOG, "Type: %s\n", filetype_description[type]);
    }
  }
  return result;
}

void recompress_mp3(Precomp& precomp_mgr) {
  print_to_log(PRECOMP_DEBUG_LOG, "Prcompressed data - MP3\n");

  long long recompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);
  long long precompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);

  print_to_log(PRECOMP_DEBUG_LOG, "Recompressed length: %lli  - precompressed length: %lli\n", recompressed_data_length, precompressed_data_length);

  char recompress_msg[256];
  unsigned char* mp3_mem_in = nullptr;
  unsigned char* mp3_mem_out = nullptr;
  unsigned int mp3_mem_out_size = -1;
  bool in_memory = (recompressed_data_length <= MP3_MAX_MEMORY_SIZE);

  bool recompress_success = false;

  std::string precompressed_filename = temp_files_tag() + "_precompressed_mp3";
  std::string recompressed_filename = temp_files_tag() + "_original_mp3";

  if (in_memory) {
    mp3_mem_in = new unsigned char[precompressed_data_length];
    auto memstream = memiostream::make(mp3_mem_in, mp3_mem_in + precompressed_data_length);
    fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, *memstream, precompressed_data_length);

    pmplib_init_streams(mp3_mem_in, 1, precompressed_data_length, mp3_mem_out, 1);
    recompress_success = pmplib_convert_stream2mem(&mp3_mem_out, &mp3_mem_out_size, recompress_msg);
  }
  else {
    {
      WrappedFStream ftempout;
      ftempout.open(precompressed_filename, std::ios_base::out | std::ios_base::binary);
      fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, ftempout, precompressed_data_length);
      ftempout.close();
    }

    recompress_success = pmplib_convert_file2file(const_cast<char*>(precompressed_filename.c_str()), const_cast<char*>(recompressed_filename.c_str()), recompress_msg);
  }

  if (!recompress_success) {
    print_to_log(PRECOMP_DEBUG_LOG, "packMP3 error: %s\n", recompress_msg);
    throw PrecompError(ERR_DURING_RECOMPRESSION);
  }

  if (in_memory) {
    auto memstream = memiostream::make(mp3_mem_out, mp3_mem_out + recompressed_data_length);
    fast_copy(precomp_mgr, *memstream, *precomp_mgr.ctx->fout, recompressed_data_length);

    delete[] mp3_mem_in;
    delete[] mp3_mem_out;
  }
  else {
    {
      PrecompTmpFile frecomp;
      frecomp.open(recompressed_filename, std::ios_base::in | std::ios_base::binary);
      fast_copy(precomp_mgr, frecomp, *precomp_mgr.ctx->fout, recompressed_data_length);
    }
  }
}
