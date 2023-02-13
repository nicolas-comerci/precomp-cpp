/* Copyright 2006-2021 Christian Schneider

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License. */

// version information
#define V_MAJOR 0
#define V_MINOR 4
#define V_MINOR2 8
//#define V_STATE "ALPHA"
#define V_STATE "DEVELOPMENT"
//#define V_MSG "USE FOR TESTING ONLY"
#define V_MSG "USE AT YOUR OWN RISK!"
#ifdef __unix
  #define V_OS "Unix"
#else
  #define V_OS "Windows"
#endif
#ifdef BIT64
  #define V_BIT "64-bit"
#else
  #define V_BIT "32-bit"
#endif

#define NOMINMAX

#include <stdio.h>
#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <fstream>
#include <sstream>
#include <string>
#include <array>
#include <signal.h>
#include <random>
#include <fcntl.h>
#include <filesystem>
#include <set>
#ifdef _MSC_VER
#include <io.h>
#define ftruncate _chsize_s
#endif

#ifndef __unix
#include <conio.h>
#include <windows.h>
#include <io.h>
#else
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#endif

#include "contrib/giflib/precomp_gif.h"
#include "contrib/packjpg/precomp_jpg.h"
#include "contrib/packmp3/precomp_mp3.h"
#include "contrib/zlib/zlib.h"
#include "contrib/preflate/preflate.h"
#include "contrib/brunsli/c/include/brunsli/brunsli_encode.h"
#include "contrib/brunsli/c/include/brunsli/brunsli_decode.h"
#include "contrib/brunsli/c/include/brunsli/jpeg_data_reader.h"
#include "contrib/brunsli/c/include/brunsli/jpeg_data_writer.h"

#include "precomp_dll.h"

// This I shamelessly lifted from https://web.archive.org/web/20090907131154/http://www.cs.toronto.edu:80/~ramona/cosmin/TA/prog/sysconf/
// (credit to this StackOverflow answer for pointing me to it https://stackoverflow.com/a/1613677)
// It allows us to portably (at least for Windows/Linux/Mac) set a std stream as binary
#define STDIN  0
#define STDOUT 1
#define STDERR 2
#ifndef __unix
# define SET_BINARY_MODE(handle) setmode(handle, O_BINARY)
#else
# define SET_BINARY_MODE(handle) ((void)0)
#endif

zLibMTF MTF;

int min_ident_size_intense_brute_mode = 64;

unsigned char zlib_header[2];
unsigned int* idat_lengths = NULL;
unsigned int* idat_crcs = NULL;
int idat_count;

enum {
  D_PDF      = 0,
  D_ZIP      = 1,
  D_GZIP     = 2,
  D_PNG      = 3,
  D_MULTIPNG = 4,
  D_GIF      = 5,
  D_JPG      = 6,
  D_SWF      = 7,
  D_BASE64   = 8,
  D_BZIP2    = 9,
  D_MP3      = 10,
  D_RAW      = 255,
  D_BRUTE    = 254,
};

//Switches constructor
Switches::Switches() {
  compression_method = 2;
  compression_otf_max_memory = 2048;
  compression_otf_thread_count = std::thread::hardware_concurrency();
  if (compression_otf_thread_count == 0) {
    compression_otf_thread_count = 2;
  }

  intense_mode = false;
  fast_mode = false;
  brute_mode = false;
  pdf_bmp_mode = false;
  prog_only = false;
  use_mjpeg = true;
  use_brunsli = true;
  use_brotli = false;
  use_packjpg_fallback = true;
  DEBUG_MODE = false;
  min_ident_size = 4;

  use_pdf = true;
  use_zip = true;
  use_gzip = true;
  use_png = true;
  use_gif = true;
  use_jpg = true;
  use_mp3 = true;
  use_swf = true;
  use_base64 = true;
  use_bzip2 = true;
  level_switch_used = false;
  for (int i = 0; i < 81; i++) {
    use_zlib_level[i] = true;
  }
}

void RecursionContext::set_input_stream(std::istream* istream, bool take_ownership) {
  this->fin = std::unique_ptr<WrappedIStream>(new WrappedIStream(istream, take_ownership));
}

void RecursionContext::set_output_stream(std::ostream* ostream, bool take_ownership) {
  this->fout = std::unique_ptr<ObservableOStream>(new ObservableOStream(ostream, take_ownership));
}

std::unique_ptr<RecursionContext>&  Precomp::get_original_context() {
  if (recursion_contexts_stack.empty()) return ctx;
  return recursion_contexts_stack[0];
}

void Precomp::set_input_stream(std::istream* istream, bool take_ownership) {
  auto& orig_context = this->get_original_context();
  if (istream == &std::cin) {
    // Read binary to stdin
    SET_BINARY_MODE(STDIN);
    orig_context->fin->rdbuf(std::cin.rdbuf());
  }
  else {
    this->get_original_context()->set_input_stream(istream, take_ownership);
  }
}

void Precomp::set_output_stream(std::ostream* ostream, bool take_ownership) {
  auto& orig_context = this->get_original_context();
  if (ostream == &std::cout) {
    // Write binary to stdout
    SET_BINARY_MODE(STDOUT);
    orig_context->fout->rdbuf(std::cout.rdbuf());
  } else {
    orig_context->set_output_stream(ostream, take_ownership);
  }

  // Set write observer to update progress when writing to output file, based on how much of the input file we have read
  orig_context->fout->register_observer(ObservableOStream::observable_methods::write_method, [this]()
  {
    this->call_progress_callback();
  });
}

void Precomp::enable_input_stream_otf_decompression() {
  const auto& orig_context = this->get_original_context();
  if (&orig_context != &ctx || orig_context->compression_otf_method == OTF_NONE) return;
  const bool take_ownership = orig_context->fin->is_owns_wrapped_stream();
  set_input_stream(
    wrap_istream_otf_compression(
      std::unique_ptr<std::istream>(orig_context->fin->release()), orig_context->compression_otf_method, false
    ).release(),
    take_ownership
  );
}

void Precomp::enable_output_stream_otf_compression(int otf_compression_method) {
  const auto& orig_context = this->get_original_context();
  if (&orig_context != &ctx || otf_compression_method == OTF_NONE) return;
  const bool take_ownership = orig_context->fout->is_owns_wrapped_stream();
  set_output_stream(
    wrap_ostream_otf_compression(
      std::unique_ptr<std::ostream>(orig_context->fout->release()),
      otf_compression_method,
      std::move(this->otf_xz_extra_params),
      this->switches.compression_otf_max_memory,
      this->switches.compression_otf_thread_count,
      false
    ).release(),
    take_ownership
  );
}

void Precomp::set_progress_callback(std::function<void(float)> callback) {
  progress_callback = callback;
}
void Precomp::call_progress_callback() {
  if (!this->progress_callback) return;
  const auto percent = (this->get_original_context()->fin->tellg() / static_cast<float>(this->get_original_context()->fin_length)) * 100;
  this->progress_callback(percent);
}

// get copyright message
// msg = Buffer for error messages (256 bytes buffer size are enough)
LIBPRECOMP void get_copyright_msg(char* msg) {
  if (V_MINOR2 == 0) {
    sprintf(msg, "Precomp DLL v%i.%i (c) 2006-2021 by Christian Schneider",V_MAJOR,V_MINOR);
  } else {
    sprintf(msg, "Precomp DLL v%i.%i.%i (c) 2006-2021 by Christian Schneider",V_MAJOR,V_MINOR,V_MINOR2);
  }
}

void setSwitches(Precomp& precomp_mgr, Switches switches) {
  DEBUG_MODE = switches.DEBUG_MODE;
  precomp_mgr.switches = switches;
  precomp_mgr.ctx->compression_otf_method = switches.compression_method;
  if (switches.level_switch_used) {

    for (int i = 0; i < 81; i++) {
      if (switches.use_zlib_level[i]) {
        precomp_mgr.obsolete.comp_mem_level_count[i] = 0;
      } else {
        precomp_mgr.obsolete.comp_mem_level_count[i] = -1;
      }
    }
  }
}

// precompress a file
// in_file = input filename
// out_file = output filename
// msg = Buffer for error messages (256 bytes buffer size are enough)
LIBPRECOMP bool precompress_file(char* in_file, char* out_file, char* msg, Switches switches) {
  Precomp precomp_mgr;

  // init compression and memory level count
  for (int i = 0; i < 81; i++) {
    precomp_mgr.obsolete.comp_mem_level_count[i] = 0;
    precomp_mgr.obsolete.zlib_level_was_used[i] = false;
  }

  precomp_mgr.ctx->fin_length = fileSize64(in_file);

  auto fin = new std::ifstream();
  fin->open(in_file, std::ios_base::in | std::ios_base::binary);
  if (!fin->is_open()) {
    sprintf(msg, "ERROR: Input file \"%s\" doesn't exist", in_file);

    return false;
  }
  precomp_mgr.set_input_stream(fin);

  auto fout = new std::ofstream();
  fout->open(out_file, std::ios_base::out | std::ios_base::binary);
  if (!fout->is_open()) {
    sprintf(msg, "ERROR: Can't create output file \"%s\"", out_file);
    return false;
  }
  precomp_mgr.set_output_stream(fout, true);

  setSwitches(precomp_mgr, switches);

  precomp_mgr.input_file_name = in_file;

  precomp_mgr.start_time = get_time_ms();

  compress_file(precomp_mgr);

  return true;
}

// recompress a file
// in_file = input filename
// out_file = output filename
// msg = Buffer for error messages (256 bytes buffer size are enough)
LIBPRECOMP bool recompress_file(char* in_file, char* out_file, char* msg, Switches switches) {
  Precomp precomp_mgr;

  // init compression and memory level count
  for (int i = 0; i < 81; i++) {
    precomp_mgr.obsolete.comp_mem_level_count[i] = 0;
    precomp_mgr.obsolete.zlib_level_was_used[i] = false;
  }

  precomp_mgr.ctx->fin_length = fileSize64(in_file);

  auto fin = new std::ifstream();
  fin->open(in_file, std::ios_base::in | std::ios_base::binary);
  if (!fin->is_open()) {
    sprintf(msg, "ERROR: Input file \"%s\" doesn't exist", in_file);

    return false;
  }
  precomp_mgr.set_input_stream(fin);

  auto fout = new std::ofstream();
  fout->open(out_file, std::ios_base::out | std::ios_base::binary);
  if (!fout->is_open()) {
    sprintf(msg, "ERROR: Can't create output file \"%s\"", out_file);

    return false;
  }
  precomp_mgr.set_output_stream(fout, true);

  setSwitches(precomp_mgr, switches);

  precomp_mgr.input_file_name = in_file;

  precomp_mgr.start_time = get_time_ms();

  read_header(precomp_mgr);
  decompress_file(precomp_mgr);

  return true;
}

// test if a file contains streams that can be precompressed
LIBPRECOMP bool file_precompressable(char* in, char* msg) {
  return false;
}

// Brute mode detects a bit less than intense mode to avoid false positives
// and slowdowns, so both can be active. Also, both of them can have a level
// limit, so two helper functions make things easier to handle.

bool intense_mode_is_active(Precomp& precomp_mgr) {
  if (!precomp_mgr.switches.intense_mode) return false;
  if ((precomp_mgr.switches.intense_mode_depth_limit == -1) || (precomp_mgr.recursion_depth <= precomp_mgr.switches.intense_mode_depth_limit)) return true;

  return false;
}

bool brute_mode_is_active(Precomp& precomp_mgr) {
  if (!precomp_mgr.switches.brute_mode) return false;
  if ((precomp_mgr.switches.brute_mode_depth_limit == -1) || (precomp_mgr.recursion_depth <= precomp_mgr.switches.brute_mode_depth_limit)) return true;

  return false;
}

void copy_penalty_bytes(RecursionContext& context, long long& rek_penalty_bytes_len, bool& use_penalty_bytes) {
  if ((rek_penalty_bytes_len > 0) && (use_penalty_bytes)) {
    std::copy(context.local_penalty_bytes.data(), context.local_penalty_bytes.data() + rek_penalty_bytes_len, context.penalty_bytes.begin());
    context.penalty_bytes_len = rek_penalty_bytes_len;
  } else {
    context.penalty_bytes_len = 0;
  }
}

#define DEF_COMPARE_CHUNK 512
long long def_compare_bzip2(Precomp& precomp_mgr, WrappedIStream& source, WrappedIStream& compfile, int level, long long& decompressed_bytes_used) {
  int ret, flush;
  unsigned have;
  bz_stream strm;
  long long identical_bytes_compare = 0;

  long long comp_pos = 0;
  decompressed_bytes_used = 0;

  /* allocate deflate state */
  strm.bzalloc = NULL;
  strm.bzfree = NULL;
  strm.opaque = NULL;
  ret = BZ2_bzCompressInit(&strm, level, 0, 0);
  if (ret != BZ_OK)
    return ret;

  long long total_same_byte_count = 0;
  long long total_same_byte_count_penalty = 0;
  long long rek_same_byte_count = 0;
  long long rek_same_byte_count_penalty = -1;
  long long rek_penalty_bytes_len = 0;
  long long local_penalty_bytes_len = 0;
  bool use_penalty_bytes = false;

  /* compress until end of file */
  do {
    precomp_mgr.call_progress_callback();

    source.read(reinterpret_cast<char*>(precomp_mgr.in), DEF_COMPARE_CHUNK);
    strm.avail_in = source.gcount();
    if (source.bad()) {
      (void)BZ2_bzCompressEnd(&strm);
      return BZ_PARAM_ERROR;
    }
    flush = source.eof() ? BZ_FINISH : BZ_RUN;
    strm.next_in = (char*)precomp_mgr.in;
    decompressed_bytes_used += strm.avail_in;

    do {
      strm.avail_out = DEF_COMPARE_CHUNK;
      strm.next_out = (char*)precomp_mgr.out;

      ret = BZ2_bzCompress(&strm, flush);

      have = DEF_COMPARE_CHUNK - strm.avail_out;

      if (have > 0) {
        if (&compfile == precomp_mgr.ctx->fin.get()) {
          identical_bytes_compare = compare_file_mem_penalty(*precomp_mgr.ctx, compfile, precomp_mgr.out, precomp_mgr.ctx->input_file_pos + comp_pos, have, total_same_byte_count, total_same_byte_count_penalty, rek_same_byte_count, rek_same_byte_count_penalty, rek_penalty_bytes_len, local_penalty_bytes_len, use_penalty_bytes);
        } else {
          identical_bytes_compare = compare_file_mem_penalty(*precomp_mgr.ctx, compfile, precomp_mgr.out, comp_pos, have, total_same_byte_count, total_same_byte_count_penalty, rek_same_byte_count, rek_same_byte_count_penalty, rek_penalty_bytes_len, local_penalty_bytes_len, use_penalty_bytes);
        }
      }

      if (have > 0) {
        if ((unsigned int)identical_bytes_compare < (have >> 1)) {
          (void)BZ2_bzCompressEnd(&strm);
          copy_penalty_bytes(*precomp_mgr.ctx, rek_penalty_bytes_len, use_penalty_bytes);
          return rek_same_byte_count;
        }
      }

      comp_pos += have;

    } while (strm.avail_out == 0);

  } while (flush != BZ_FINISH);

  (void)BZ2_bzCompressEnd(&strm);
  copy_penalty_bytes(*precomp_mgr.ctx, rek_penalty_bytes_len, use_penalty_bytes);
  return rek_same_byte_count;
}

int def_part_bzip2(Precomp& precomp_mgr, WrappedIStream& source, WrappedOStream& dest, int level, long long stream_size_in, long long stream_size_out) {
  int ret, flush;
  unsigned have;
  bz_stream strm;

  /* allocate deflate state */
  strm.bzalloc = NULL;
  strm.bzfree = NULL;
  strm.opaque = NULL;
  ret = BZ2_bzCompressInit(&strm, level, 0, 0);
  if (ret != BZ_OK)
    return ret;

  long long pos_in = 0;
  long long pos_out = 0;

  /* compress until end of file */
  do {
    if ((stream_size_in - pos_in) > CHUNK) {
      precomp_mgr.call_progress_callback();

      source.read(reinterpret_cast<char*>(precomp_mgr.in), CHUNK);
      strm.avail_in = source.gcount();
      pos_in += CHUNK;
      flush = BZ_RUN;
    } else {
      source.read(reinterpret_cast<char*>(precomp_mgr.in), stream_size_in - pos_in);
      strm.avail_in = source.gcount();
      flush = BZ_FINISH;
    }
    if (source.bad()) {
      (void)BZ2_bzCompressEnd(&strm);
      return BZ_PARAM_ERROR;
    }
    strm.next_in = (char*)precomp_mgr.in;

    do {
      strm.avail_out = CHUNK;
      strm.next_out = (char*)precomp_mgr.out;

      ret = BZ2_bzCompress(&strm, flush);

      have = CHUNK - strm.avail_out;

      if ((pos_out + (signed)have) > stream_size_out) {
        have = stream_size_out - pos_out;
      }
      pos_out += have;

      dest.write(reinterpret_cast<char*>(precomp_mgr.out), have);
      if (dest.bad()) {
        (void)BZ2_bzCompressEnd(&strm);
        return BZ_DATA_ERROR;
      }
    } while (strm.avail_out == 0);

  } while (flush != BZ_FINISH);

  (void)BZ2_bzCompressEnd(&strm);
  return BZ_OK;
}

// fread_skip variables, shared with def_part_skip
unsigned int frs_offset;
unsigned int frs_line_len;
unsigned int frs_skip_len;
unsigned char frs_skipbuf[4];

size_t fread_skip(unsigned char *ptr, size_t size, size_t count, WrappedIStream& stream) {
  size_t bytes_read = 0;
  unsigned int read_tmp;

  do {
    if ((count - bytes_read) >= (frs_line_len - frs_offset)) {
      if ((frs_line_len - frs_offset) > 0) {
        stream.read(reinterpret_cast<char*>(ptr + bytes_read), size * (frs_line_len - frs_offset));
        read_tmp = stream.gcount();
         if (read_tmp == 0) return bytes_read;
        bytes_read += read_tmp;
      }
      // skip padding bytes
      stream.read(reinterpret_cast<char*>(frs_skipbuf), size * frs_skip_len);
      read_tmp = stream.gcount();
      if (read_tmp == 0) return bytes_read;
      frs_offset = 0;
    } else {
      stream.read(reinterpret_cast<char*>(ptr + bytes_read), size * (count - bytes_read));
      read_tmp = stream.gcount();
      if (read_tmp == 0) return bytes_read;
      bytes_read += read_tmp;
      frs_offset += read_tmp;
    }
  } while (bytes_read < count);

  return bytes_read;
}

int histogram[256];

bool check_inf_result(Precomp& precomp_mgr, unsigned char* in_buf, unsigned char* out_buf, int cb_pos, int windowbits, bool use_brute_parameters = false) {
  // first check BTYPE bits, skip 11 ("reserved (error)")
  int btype = (in_buf[cb_pos] & 0x07) >> 1;
  if (btype == 3) return false;
  // skip BTYPE = 00 ("uncompressed") only in brute mode, because these can be useful for recursion
  // and often occur in combination with static/dynamic BTYPE blocks
  if (use_brute_parameters) {
    if (btype == 0) return false;

    // use a histogram to see if the first 64 bytes are too redundant for a deflate stream,
    // if a byte is present 8 or more times, it's most likely not a deflate stream
    // and could slow down the process (e.g. repeated patterns of "0xEBE1F1" or "0xEBEBEBFF"
    // did this before)
    memset(&histogram[0], 0, sizeof(histogram));
    int maximum=0, used=0, offset=cb_pos;
    for (int i=0;i<4;i++,offset+=64){
      for (int j=0;j<64;j++){
        int* freq = &histogram[in_buf[offset+j]];
        used+=((*freq)==0);
        maximum+=(++(*freq))>maximum;
      }
      if (maximum>=((12+i)<<i) || used*(7-(i+(i/2)))<(i+1)*64)
        return false;
    }
  }

  int ret;
  unsigned have = 0;
  z_stream strm;

  /* allocate inflate state */
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  strm.avail_in = 0;
  strm.next_in = Z_NULL;
  ret = inflateInit2(&strm, windowbits);
  if (ret != Z_OK)
    return false;

  precomp_mgr.call_progress_callback();

  strm.avail_in = 2048;
  strm.next_in = in_buf + cb_pos;

  /* run inflate() on input until output buffer not full */
  do {
    strm.avail_out = CHUNK;
    strm.next_out = out_buf;

    ret = inflate(&strm, Z_NO_FLUSH);
    switch (ret) {
      case Z_NEED_DICT:
        ret = Z_DATA_ERROR;
      case Z_DATA_ERROR:
      case Z_MEM_ERROR:
        (void)inflateEnd(&strm);
        return false;
    }

    have += CHUNK - strm.avail_out;
  } while (strm.avail_out == 0);


  /* clean up and return */
  (void)inflateEnd(&strm);
  switch (ret) {
      case Z_OK:
        return true;
      case Z_STREAM_END:
        // Skip short streams - most likely false positives
        unsigned less_than_skip = 32;
        if (use_brute_parameters) less_than_skip = 1024;
        return (have >= less_than_skip);
  }

  return false;
}

int inf_bzip2(Precomp& precomp_mgr, WrappedIStream& source, WrappedOStream& dest, long long& compressed_stream_size, long long& decompressed_stream_size) {
  int ret;
  unsigned have;
  bz_stream strm;

  strm.bzalloc = NULL;
  strm.bzfree = NULL;
  strm.opaque = NULL;
  strm.avail_in = 0;
  strm.next_in = NULL;
  ret = BZ2_bzDecompressInit(&strm, 0, 0);
  if (ret != BZ_OK)
    return ret;

  compressed_stream_size = 0;
  decompressed_stream_size = 0;
  int avail_in_before;
  
  do {
    precomp_mgr.call_progress_callback();

    source.read(reinterpret_cast<char*>(precomp_mgr.in), CHUNK);
    strm.avail_in = source.gcount();
    avail_in_before = strm.avail_in;

    if (source.bad()) {
      (void)BZ2_bzDecompressEnd(&strm);
      return BZ_PARAM_ERROR;
    }
    if (strm.avail_in == 0)
      break;
    strm.next_in = (char*)precomp_mgr.in;

    do {
      strm.avail_out = CHUNK;
      strm.next_out = (char*)precomp_mgr.out;

      ret = BZ2_bzDecompress(&strm);
      if ((ret != BZ_OK) && (ret != BZ_STREAM_END)) {
        (void)BZ2_bzDecompressEnd(&strm);
        return ret;
      }

      compressed_stream_size += (avail_in_before - strm.avail_in);
      avail_in_before = strm.avail_in;
      
      have = CHUNK - strm.avail_out;
      dest.write(reinterpret_cast<char*>(precomp_mgr.out), have);
      if (dest.bad()) {
        (void)BZ2_bzDecompressEnd(&strm);
        return BZ_DATA_ERROR;
      }
      decompressed_stream_size += have;

    } while (strm.avail_out == 0);

    /* done when inflate() says it's done */
  } while (ret != BZ_STREAM_END);

  /* clean up and return */

  (void)BZ2_bzDecompressEnd(&strm);
  return ret == BZ_STREAM_END ? BZ_OK : BZ_DATA_ERROR;

}


int def_bzip2(Precomp& precomp_mgr, std::istream& source, std::ostream& dest, int level) {
  int ret, flush;
  unsigned have;
  bz_stream strm;

  /* allocate deflate state */
  strm.bzalloc = NULL;
  strm.bzfree = NULL;
  strm.opaque = NULL;
  ret = BZ2_bzCompressInit(&strm, level, 0, 0);
  if (ret != BZ_OK)
    return ret;

  /* compress until end of file */
  do {
    precomp_mgr.call_progress_callback();

    source.read(reinterpret_cast<char*>(precomp_mgr.in), CHUNK);
    strm.avail_in = source.gcount();
    if (source.bad()) {
      (void)BZ2_bzCompressEnd(&strm);
      return BZ_PARAM_ERROR;
    }
    flush = source.eof() ? BZ_FINISH : BZ_RUN;
    strm.next_in = (char*)precomp_mgr.in;

    do {
      strm.avail_out = CHUNK;
      strm.next_out = (char*)precomp_mgr.out;

      ret = BZ2_bzCompress(&strm, flush);

      have = CHUNK - strm.avail_out;

      dest.write(reinterpret_cast<char*>(precomp_mgr.out), have);
      if (dest.bad()) {
        (void)BZ2_bzCompressEnd(&strm);
        return BZ_DATA_ERROR;
      }
    } while (strm.avail_out == 0);

  } while (flush != BZ_FINISH);

  (void)BZ2_bzCompressEnd(&strm);
  return BZ_OK;
}

long long file_recompress_bzip2(Precomp& precomp_mgr, WrappedIStream& origfile, int level, long long& decompressed_bytes_used, long long& decompressed_bytes_total, PrecompTmpFile& tmpfile) {
  long long retval;

  force_seekg(tmpfile, 0, std::ios_base::end);
  decompressed_bytes_total = tmpfile.tellg();
  if (!tmpfile.is_open()) {
    throw PrecompError(ERR_TEMP_FILE_DISAPPEARED);
  }

  force_seekg(tmpfile, 0, std::ios_base::beg);
  tmpfile.close();
  WrappedFStream tmpfile2;
  tmpfile2.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
  retval = def_compare_bzip2(precomp_mgr, tmpfile2, origfile, level, decompressed_bytes_used);
  tmpfile2.close();
  return retval < 0 ? -1 : retval;
}

void write_decompressed_data(Precomp& precomp_mgr, WrappedOStream& ostream, long long byte_count, const char* decompressed_file_name) {
  WrappedFStream ftempout;
  ftempout.open(decompressed_file_name, std::ios_base::in | std::ios_base::binary);
  if (!ftempout.is_open()) throw PrecompError(ERR_TEMP_FILE_DISAPPEARED);

  force_seekg(ftempout, 0, std::ios_base::beg);

  fast_copy(precomp_mgr, ftempout, ostream, byte_count);
  ftempout.close();
}

void write_decompressed_data_io_buf(Precomp& precomp_mgr, long long byte_count, bool in_memory, const char* decompressed_file_name) {
    if (in_memory) {
      auto decomp_io_buf_ptr = precomp_mgr.ctx->decomp_io_buf.data();
      memiostream memstream = memiostream::make(decomp_io_buf_ptr, decomp_io_buf_ptr + byte_count);
      fast_copy(precomp_mgr, memstream, *precomp_mgr.ctx->fout, byte_count);
    } else {
      write_decompressed_data(precomp_mgr, *precomp_mgr.ctx->fout, byte_count, decompressed_file_name);
    }
}

unsigned long long compare_files(Precomp& precomp_mgr, WrappedIStream& file1, WrappedIStream& file2, unsigned int pos1, unsigned int pos2) {
  unsigned char input_bytes1[COMP_CHUNK];
  unsigned char input_bytes2[COMP_CHUNK];
  long long same_byte_count = 0;
  int size1, size2, minsize;
  int i;
  bool endNow = false;

  force_seekg(file1, pos1, std::ios_base::beg);
  force_seekg(file2, pos2, std::ios_base::beg);

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

unsigned char input_bytes1[DEF_COMPARE_CHUNK];

long long compare_file_mem_penalty(RecursionContext& context, WrappedIStream& file1, unsigned char* input_bytes2, long long pos1, long long bytecount, long long& total_same_byte_count, long long& total_same_byte_count_penalty, long long& rek_same_byte_count, long long& rek_same_byte_count_penalty, long long& rek_penalty_bytes_len, long long& local_penalty_bytes_len, bool& use_penalty_bytes) {
  int same_byte_count = 0;
  int size1;
  int i;

  unsigned long long old_pos = file1.tellg();
  force_seekg(file1, pos1, std::ios_base::beg);

  file1.read(reinterpret_cast<char*>(input_bytes1), bytecount);
  size1 = file1.gcount();

  for (i = 0; i < size1; i++) {
    if (input_bytes1[i] == input_bytes2[i]) {
      same_byte_count++;
      total_same_byte_count_penalty++;
    } else {
      total_same_byte_count_penalty -= 5; // 4 bytes = position, 1 byte = new byte

      // stop, if local_penalty_bytes_len gets too big
      if ((local_penalty_bytes_len + 5) >= MAX_PENALTY_BYTES) {
        break;
      }

      local_penalty_bytes_len += 5;
      // position
      context.local_penalty_bytes[local_penalty_bytes_len-5] = (total_same_byte_count >> 24) % 256;
      context.local_penalty_bytes[local_penalty_bytes_len-4] = (total_same_byte_count >> 16) % 256;
      context.local_penalty_bytes[local_penalty_bytes_len-3] = (total_same_byte_count >> 8) % 256;
      context.local_penalty_bytes[local_penalty_bytes_len-2] = total_same_byte_count % 256;
      // new byte
      context.local_penalty_bytes[local_penalty_bytes_len-1] = input_bytes1[i];
    }
    total_same_byte_count++;

    if (total_same_byte_count_penalty > rek_same_byte_count_penalty) {
      use_penalty_bytes = true;
      rek_penalty_bytes_len = local_penalty_bytes_len;

      rek_same_byte_count = total_same_byte_count;
      rek_same_byte_count_penalty = total_same_byte_count_penalty;
    }
  }

  force_seekg(file1, old_pos, std::ios_base::beg);

  return same_byte_count;
}

void start_uncompressed_data(RecursionContext& context) {
  context.uncompressed_length = 0;
  context.uncompressed_pos = context.input_file_pos;

  // uncompressed data
  context.fout->put(0);

  context.uncompressed_data_in_work = true;
}

void end_uncompressed_data(Precomp& precomp_mgr) {

  if (!precomp_mgr.ctx->uncompressed_data_in_work) return;

  fout_fput_vlint(*precomp_mgr.ctx->fout, precomp_mgr.ctx->uncompressed_length);

  // fast copy of uncompressed data
  force_seekg(*precomp_mgr.ctx->fin, precomp_mgr.ctx->uncompressed_pos, std::ios_base::beg);
  fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, precomp_mgr.ctx->uncompressed_length, true);

  precomp_mgr.ctx->uncompressed_length = -1;

  precomp_mgr.ctx->uncompressed_data_in_work = false;
}

int best_windowbits = -1;

void init_decompression_variables(RecursionContext& context) {
  context.identical_bytes = -1;
  context.best_identical_bytes = -1;
  context.best_penalty_bytes_len = 0;
  context.best_identical_bytes_decomp = -1;
  context.identical_bytes_decomp = -1;
}

struct recompress_deflate_result {
  long long compressed_stream_size;
  long long uncompressed_stream_size;
  std::vector<unsigned char> recon_data;
  bool accepted;
  bool uncompressed_in_memory;
  bool zlib_perfect;
  char zlib_comp_level;
  char zlib_mem_level;
  char zlib_window_bits;
};

void debug_deflate_detected(RecursionContext& context, const recompress_deflate_result& rdres, const char* type) {
  if (DEBUG_MODE) {
    std::cout << "Possible zLib-Stream " << type << " found at position " << context.saved_input_file_pos << std::endl;
    std::cout << "Compressed size: " << rdres.compressed_stream_size << std::endl;
    std::cout << "Can be decompressed to " << rdres.uncompressed_stream_size << " bytes" << std::endl;

    if (rdres.accepted) {
      if (rdres.zlib_perfect) {
        std::cout << "Detect ZLIB parameters: comp level " << rdres.zlib_comp_level << ", mem level " << rdres.zlib_mem_level << ", " << rdres.zlib_window_bits << "window bits" << std::endl;
      } else {
        std::cout << "Non-ZLIB reconstruction data size: " << rdres.recon_data.size() << " bytes" << std::endl;
      }
    }
  }
}
void debug_deflate_reconstruct(const recompress_deflate_result& rdres, const char* type,
                               const unsigned hdr_length, const uint64_t rec_length) {
  if (DEBUG_MODE) {
    std::cout << "Decompressed data - " << type << std::endl;
    std::cout << "Header length: " << hdr_length << std::endl;
    if (rdres.zlib_perfect) {
      std::cout << "ZLIB Parameters: compression level " << rdres.zlib_comp_level 
                            << " memory level " << rdres.zlib_mem_level
                            << " window bits " << rdres.zlib_window_bits << std::endl;
    } else {
      std::cout << "Reconstruction data size: " << rdres.recon_data.size() << std::endl;
    }
    if (rec_length > 0) {
      std::cout << "Recursion data length: " << rec_length << std::endl;
    } else {
      std::cout << "Recompressed length: " << rdres.compressed_stream_size << " - decompressed length: " << rdres.uncompressed_stream_size << std::endl;
    }
  }
}

class OwnIStream : public InputStream {
public:
  OwnIStream(WrappedIStream* f) : _f(f), _eof(false) {}

  virtual bool eof() const {
    return _eof;
  }
  virtual size_t read(unsigned char* buffer, const size_t size) {
    _f->read(reinterpret_cast<char*>(buffer), size);
    size_t res = _f->gcount();
    _eof |= res < size;
    return res;
  }
private:
  WrappedIStream* _f;
  bool _eof;
};
class UncompressedOutStream : public OutputStream {
public:
  WrappedOStream& ftempout;
  Precomp* precomp_mgr;

  UncompressedOutStream(bool& in_memory, WrappedOStream& tmpfile, Precomp* precomp_mgr)
    : ftempout(tmpfile), precomp_mgr(precomp_mgr), _written(0), _in_memory(in_memory) {}
  ~UncompressedOutStream() {}

  virtual size_t write(const unsigned char* buffer, const size_t size) {
    precomp_mgr->call_progress_callback();
    if (_in_memory) {
      auto decomp_io_buf_ptr = precomp_mgr->ctx->decomp_io_buf.data();
      if (_written + size >= MAX_IO_BUFFER_SIZE) {
        _in_memory = false;
        memiostream memstream = memiostream::make(decomp_io_buf_ptr, decomp_io_buf_ptr + _written);
        fast_copy(*precomp_mgr, memstream, ftempout, _written);
      }
      else {
        memcpy(decomp_io_buf_ptr + _written, buffer, size);
        _written += size;
        return size;
      }
    }
    _written += size;
    ftempout.write(reinterpret_cast<char*>(const_cast<unsigned char*>(buffer)), size);
    return ftempout.bad() ? 0 : size;
  }

  uint64_t written() const {
    return _written;
  }

private:
  uint64_t _written;
  bool& _in_memory;
};

recompress_deflate_result try_recompression_deflate(Precomp& precomp_mgr, WrappedIStream& file, PrecompTmpFile& tmpfile) {
  force_seekg(file, &file == precomp_mgr.ctx->fin.get() ? precomp_mgr.ctx->input_file_pos : 0, std::ios_base::beg);

  recompress_deflate_result result;
  memset(&result, 0, sizeof(result));
  
  OwnIStream is(&file);

  {
    result.uncompressed_in_memory = true;
    UncompressedOutStream uos(result.uncompressed_in_memory, tmpfile, &precomp_mgr);
    uint64_t compressed_stream_size = 0;
    result.accepted = preflate_decode(uos, result.recon_data,
                                      compressed_stream_size, is, [&precomp_mgr]() { precomp_mgr.call_progress_callback(); },
                                      0,
                                      precomp_mgr.switches.preflate_meta_block_size); // you can set a minimum deflate stream size here
    result.compressed_stream_size = compressed_stream_size;
    result.uncompressed_stream_size = uos.written();

    if (precomp_mgr.switches.preflate_verify && result.accepted) {
      force_seekg(file, &file == precomp_mgr.ctx->fin.get() ? precomp_mgr.ctx->input_file_pos : 0, std::ios_base::beg);
      OwnIStream is2(&file);
      std::vector<uint8_t> orgdata(result.compressed_stream_size);
      is2.read(orgdata.data(), orgdata.size());

      MemStream reencoded_deflate;
      auto decomp_io_buf_ptr = precomp_mgr.ctx->decomp_io_buf.data();
      MemStream uncompressed_mem(result.uncompressed_in_memory ? std::vector<uint8_t>(decomp_io_buf_ptr, decomp_io_buf_ptr + result.uncompressed_stream_size) : std::vector<uint8_t>());
      OwnIStream uncompressed_file(result.uncompressed_in_memory ? nullptr : &tmpfile);
      if (!preflate_reencode(reencoded_deflate, result.recon_data, 
                             result.uncompressed_in_memory ? (InputStream&)uncompressed_mem : (InputStream&)uncompressed_file, 
                             result.uncompressed_stream_size,
                             [] {})
          || orgdata != reencoded_deflate.data()) {
        result.accepted = false;
        static size_t counter = 0;
        char namebuf[50];
        while (true) {
          snprintf(namebuf, 49, "preflate_error_%04d.raw", counter++);
          std::fstream f;
          f.open(namebuf, std::ios_base::in | std::ios_base::binary);
          if (f.is_open()) {
            continue;
          }
          f.open(namebuf, std::ios_base::out | std::ios_base::binary);
          f.write(reinterpret_cast<char*>(orgdata.data()), orgdata.size());
          break;
        }
      }
    }
  }
  return std::move(result);
}

class OwnOStream : public OutputStream {
public:
  OwnOStream(WrappedOStream* f) : _f(f) {}

  virtual size_t write(const unsigned char* buffer, const size_t size) {
    _f->write(reinterpret_cast<char*>(const_cast<unsigned char*>(buffer)), size);
    return _f->bad() ? 0 : size;
  }
private:
  WrappedOStream* _f;
};

bool try_reconstructing_deflate(Precomp& precomp_mgr, WrappedIStream& fin, WrappedOStream& fout, const recompress_deflate_result& rdres) {
  OwnOStream os(&fout);
  OwnIStream is(&fin);
  bool result = preflate_reencode(os, rdres.recon_data, is, rdres.uncompressed_stream_size, [&precomp_mgr]() { precomp_mgr.call_progress_callback(); });
  return result;
}
bool try_reconstructing_deflate_skip(Precomp& precomp_mgr, WrappedIStream& fin, WrappedOStream& fout, const recompress_deflate_result& rdres, const size_t read_part, const size_t skip_part) {
  std::vector<unsigned char> unpacked_output;
  unpacked_output.resize(rdres.uncompressed_stream_size);
  frs_offset = 0;
  frs_skip_len = skip_part;
  frs_line_len = read_part;
  if ((int64_t)fread_skip(unpacked_output.data(), 1, rdres.uncompressed_stream_size, fin) != rdres.uncompressed_stream_size) {
    return false;
  }
  OwnOStream os(&fout);
  return preflate_reencode(os, rdres.recon_data, unpacked_output, [&precomp_mgr]() { precomp_mgr.call_progress_callback(); });
}
class OwnOStreamMultiPNG : public OutputStream {
public:
  OwnOStreamMultiPNG(WrappedOStream* f,
                      const size_t idat_count, 
                      const uint32_t* idat_crcs, 
                      const uint32_t* idat_lengths) 
  : _f(f), _idat_count(idat_count), _idat_crcs(idat_crcs), _idat_lengths(idat_lengths)
  , _idat_idx(0), _to_read(_idat_lengths[0])
  {}

  virtual size_t write(const unsigned char* buffer_, const size_t size_) {
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
      unsigned char crc_out[4] = {(unsigned char)(_idat_crcs[_idat_idx] >> 24), (unsigned char)(_idat_crcs[_idat_idx] >> 16), (unsigned char)(_idat_crcs[_idat_idx] >> 8), (unsigned char)(_idat_crcs[_idat_idx] >> 0)};
      _f->write(reinterpret_cast<char*>(crc_out), 4);
      _to_read = _idat_lengths[_idat_idx];
      unsigned char len_out[4] = {(unsigned char)(_to_read >> 24), (unsigned char)(_to_read >> 16), (unsigned char)(_to_read >> 8), (unsigned char)(_to_read >> 0)};
      _f->write(reinterpret_cast<char*>(len_out), 4);
      _f->write("IDAT", 4);
    }
    _f->write(reinterpret_cast<char*>(const_cast<unsigned char*>(buffer)), size);
    written += size;
    _to_read -= size;
    return written;
  }
private:
  WrappedOStream* _f;
  const size_t _idat_count;
  const uint32_t* _idat_crcs;
  const uint32_t* _idat_lengths;
  size_t _idat_idx, _to_read;
};
bool try_reconstructing_deflate_multipng(Precomp& precomp_mgr, WrappedIStream& fin, WrappedOStream& fout, const recompress_deflate_result& rdres,
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

static uint64_t sum_compressed = 0, sum_uncompressed = 0, sum_recon = 0, sum_expansion = 0;
void debug_sums(const recompress_deflate_result& rdres) {
  if (DEBUG_MODE) {
    sum_compressed += rdres.compressed_stream_size;
    sum_uncompressed += rdres.uncompressed_stream_size;
    sum_expansion += rdres.uncompressed_stream_size - rdres.compressed_stream_size;
    sum_recon += rdres.recon_data.size();
    //print_to_console("deflate sums: c %I64d, u %I64d, x %I64d, r %I64d, i %I64d, o %I64d\n",
    //       sum_compressed, sum_uncompressed, sum_expansion, sum_recon, (uint64_t)precomp_mgr.ctx->fin->tellg(), (uint64_t)precomp_mgr.ctx->fout->tellp());
  }
}
void debug_pos() {
  if (DEBUG_MODE) {
    //print_to_console("deflate pos: i %I64d, o %I64d\n", (uint64_t)precomp_mgr.ctx->fin->tellg(), (uint64_t)precomp_mgr.ctx->fout->tellp());
  }
}
void try_decompression_pdf(Precomp& precomp_mgr, int windowbits, int pdf_header_length, int img_width, int img_height, int img_bpc, PrecompTmpFile& tmpfile) {
  init_decompression_variables(*precomp_mgr.ctx);

  int bmp_header_type = 0; // 0 = none, 1 = 8-bit, 2 = 24-bit

  // try to decompress at current position
  recompress_deflate_result rdres = try_recompression_deflate(precomp_mgr, *precomp_mgr.ctx->fin, tmpfile);

  if (rdres.uncompressed_stream_size > 0) { // seems to be a zLib-Stream

    precomp_mgr.statistics.decompressed_streams_count++;
    if (img_bpc == 8) {
      precomp_mgr.statistics.decompressed_pdf_count_8_bit++;
    } else {
      precomp_mgr.statistics.decompressed_pdf_count++;
    }
    
    debug_deflate_detected(*precomp_mgr.ctx, rdres, "in PDF");

    if (rdres.accepted) {
      precomp_mgr.statistics.recompressed_streams_count++;
      precomp_mgr.statistics.recompressed_pdf_count++;

      precomp_mgr.ctx->non_zlib_was_used = true;
      debug_sums(rdres);

      if (img_bpc == 8) {
        if (rdres.uncompressed_stream_size == (img_width * img_height)) {
          bmp_header_type = 1;
          if (DEBUG_MODE) {
            print_to_console("Image size did match (8 bit)\n");
          }
          precomp_mgr.statistics.recompressed_pdf_count_8_bit++;
          precomp_mgr.statistics.recompressed_pdf_count--;
        } else if (rdres.uncompressed_stream_size == (img_width * img_height * 3)) {
          bmp_header_type = 2;
          if (DEBUG_MODE) {
            print_to_console("Image size did match (24 bit)\n");
          }
          precomp_mgr.statistics.decompressed_pdf_count_8_bit--;
          precomp_mgr.statistics.decompressed_pdf_count_24_bit++;
          precomp_mgr.statistics.recompressed_pdf_count_24_bit++;
          precomp_mgr.statistics.recompressed_pdf_count--;
        } else {
          if (DEBUG_MODE) {
            print_to_console("Image size didn't match with stream size\n");
          }
          precomp_mgr.statistics.decompressed_pdf_count_8_bit--;
          precomp_mgr.statistics.decompressed_pdf_count++;
        }
      }

      // end uncompressed data

      precomp_mgr.ctx->compressed_data_found = true;
      end_uncompressed_data(precomp_mgr);

      debug_pos();

      // write compressed data header (PDF) without 12 first bytes
      //   (/FlateDecode)

      unsigned char bmp_c = 0;

      if (bmp_header_type == 1) {
        // 8 Bit, Bit 7,6 = 01
        bmp_c = 64;
      } else if (bmp_header_type == 2) {
        // 24 Bit, Bit 7,6 = 10
        bmp_c = 128;
      }

      fout_fput_deflate_hdr(*precomp_mgr.ctx->fout, D_PDF, bmp_c, rdres, precomp_mgr.ctx->in_buf + precomp_mgr.ctx->cb + 12, pdf_header_length - 12, false);
      fout_fput_recon_data(*precomp_mgr.ctx->fout, rdres);

      // eventually write BMP header

      if (bmp_header_type > 0) {

        int i;

        precomp_mgr.ctx->fout->put('B');
        precomp_mgr.ctx->fout->put('M');
        // BMP size in bytes
        int bmp_size = ((img_width+3) & -4) * img_height;
        if (bmp_header_type == 2) bmp_size *= 3;
        if (bmp_header_type == 1) {
          bmp_size += 54 + 1024;
        } else {
          bmp_size += 54;
        }
        fout_fput32_little_endian(*precomp_mgr.ctx->fout, bmp_size);

        for (i = 0; i < 4; i++) {
          precomp_mgr.ctx->fout->put(0);
        }
        precomp_mgr.ctx->fout->put(54);
        if (bmp_header_type == 1) {
          precomp_mgr.ctx->fout->put(4);
        } else {
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

        if (bmp_header_type == 1) {
          precomp_mgr.ctx->fout->put(8);
        } else {
          precomp_mgr.ctx->fout->put(24);
        }
        precomp_mgr.ctx->fout->put(0);

        for (i = 0; i < 4; i++) {
          precomp_mgr.ctx->fout->put(0);
        }

        if (bmp_header_type == 2)  img_width *= 3;

        int datasize = ((img_width+3) & -4) * img_height;
        if (bmp_header_type == 2) datasize *= 3;
        fout_fput32_little_endian(*precomp_mgr.ctx->fout, datasize);

        for (i = 0; i < 16; i++) {
          precomp_mgr.ctx->fout->put(0);
        }

        if (bmp_header_type == 1) {
          // write BMP palette
          for (i = 0; i < 1024; i++) {
            precomp_mgr.ctx->fout->put(0);
          }
        }
      }

      // write decompressed data

      if ((bmp_header_type == 0) || ((img_width % 4) == 0)) {
        tmpfile.reopen();
        fout_fput_uncompressed(precomp_mgr, rdres, tmpfile);
      } else {
        WrappedFStream ftempout;
        if (!rdres.uncompressed_in_memory) {
          ftempout.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
          if (!ftempout.is_open()) {
            throw PrecompError(ERR_TEMP_FILE_DISAPPEARED);
          }

          force_seekg(ftempout, 0, std::ios_base::beg);
        }
        ftempout.close();

        WrappedFStream ftempout2;
        ftempout2.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
        unsigned char* buf_ptr = precomp_mgr.ctx->decomp_io_buf.data();
        for (int y = 0; y < img_height; y++) {

          if (rdres.uncompressed_in_memory) {
            memiostream memstream = memiostream::make(buf_ptr, buf_ptr + img_width);
            fast_copy(precomp_mgr, memstream, *precomp_mgr.ctx->fout, img_width);
            buf_ptr += img_width;
          } else {
            fast_copy(precomp_mgr, ftempout2, *precomp_mgr.ctx->fout, img_width);
          }

          for (int i = 0; i < (4 - (img_width % 4)); i++) {
            precomp_mgr.ctx->fout->put(0);
          }

        }
      }

      // start new uncompressed data
      debug_pos();

      // set input file pointer after recompressed data
      precomp_mgr.ctx->input_file_pos += rdres.compressed_stream_size - 1;
      precomp_mgr.ctx->cb += rdres.compressed_stream_size - 1;

    } else {
      if (intense_mode_is_active(precomp_mgr)) precomp_mgr.ctx->intense_ignore_offsets->insert(precomp_mgr.ctx->input_file_pos - 2);
      if (brute_mode_is_active(precomp_mgr)) precomp_mgr.ctx->brute_ignore_offsets->insert(precomp_mgr.ctx->input_file_pos);
      if (DEBUG_MODE) {
        print_to_console("No matches\n");
      }
    }
  }
}

void try_decompression_deflate_type(Precomp& precomp_mgr, unsigned& dcounter, unsigned& rcounter,
                                    const unsigned char type, 
                                    const unsigned char* hdr, const int hdr_length, const bool inc_last, 
                                    const char* debugname, PrecompTmpFile& tmpfile) {
  init_decompression_variables(*precomp_mgr.ctx);

  // try to decompress at current position
  recompress_deflate_result rdres = try_recompression_deflate(precomp_mgr, *precomp_mgr.ctx->fin, tmpfile);

  if (rdres.uncompressed_stream_size > 0) { // seems to be a zLib-Stream
    precomp_mgr.statistics.decompressed_streams_count++;
    dcounter++;

    debug_deflate_detected(*precomp_mgr.ctx, rdres, debugname);

    if (rdres.accepted) {
      precomp_mgr.statistics.recompressed_streams_count++;
      rcounter++;

      precomp_mgr.ctx->non_zlib_was_used = true;

      debug_sums(rdres);

      // end uncompressed data

      debug_pos();

      precomp_mgr.ctx->compressed_data_found = true;
      end_uncompressed_data(precomp_mgr);

      // check recursion
      tmpfile.reopen();
      recursion_result r = recursion_write_file_and_compress(precomp_mgr, rdres, tmpfile);

#if 0
      // Do we really want to allow uncompressed streams that are smaller than the compressed
      // ones? (It makes sense if the uncompressed stream contains a JPEG, or something similar.
      if (rdres.uncompressed_stream_size <= rdres.compressed_stream_size && !r.success) {
        precomp_mgr.statistics.recompressed_streams_count--;
        compressed_data_found = false;
        return;
      }
#endif

      debug_pos();

      // write compressed data header without first bytes
      tmpfile.reopen();
      fout_fput_deflate_rec(precomp_mgr, type, rdres, hdr, hdr_length, inc_last, r, tmpfile);

      debug_pos();

      // set input file pointer after recompressed data
      precomp_mgr.ctx->input_file_pos += rdres.compressed_stream_size - 1;
      precomp_mgr.ctx->cb += rdres.compressed_stream_size - 1;

    } else {
      if (type == D_SWF && intense_mode_is_active(precomp_mgr)) precomp_mgr.ctx->intense_ignore_offsets->insert(precomp_mgr.ctx->input_file_pos - 2);
      if (type != D_BRUTE && brute_mode_is_active(precomp_mgr)) precomp_mgr.ctx->brute_ignore_offsets->insert(precomp_mgr.ctx->input_file_pos);
      if (DEBUG_MODE) {
        print_to_console("No matches\n");
      }
    }

  }
}

void try_decompression_zip(Precomp& precomp_mgr, int zip_header_length, PrecompTmpFile& tmpfile) {
  try_decompression_deflate_type(precomp_mgr, precomp_mgr.statistics.decompressed_zip_count, precomp_mgr.statistics.recompressed_zip_count,
                                 D_ZIP, precomp_mgr.ctx->in_buf + precomp_mgr.ctx->cb + 4, zip_header_length - 4, false,
                                 "in ZIP", tmpfile);
}

void show_used_levels(Precomp& precomp_mgr) {
  if (!precomp_mgr.ctx->anything_was_used) {
    if (!precomp_mgr.ctx->non_zlib_was_used) {
      if (precomp_mgr.ctx->compression_otf_method == OTF_NONE) {
        print_to_console("\nNone of the given compression and memory levels could be used.\n");
        print_to_console("There will be no gain compressing the output file.\n");
      }
    } else {
      if ((!precomp_mgr.max_recursion_depth_reached) && (precomp_mgr.max_recursion_depth_used != precomp_mgr.max_recursion_depth)) {
        #ifdef COMFORT
          print_to_console("\nYou can speed up Precomp for THIS FILE with these INI parameters:\n");
          print_to_console("Maximal_Recursion_Depth=");
        #else
          print_to_console("\nYou can speed up Precomp for THIS FILE with these parameters:\n");
          print_to_console("-d");
        #endif
        print_to_console("%i\n", precomp_mgr.max_recursion_depth_used);
      }
    }
    if (precomp_mgr.max_recursion_depth_reached) {
      print_to_console("\nMaximal recursion depth %i reached, increasing it could give better results.\n", precomp_mgr.max_recursion_depth);
    }
    return;
  }

  int i, i_sort;
  int level_count = 0;
  #ifdef COMFORT
    print_to_console("\nYou can speed up Precomp for THIS FILE with these INI parameters:\n");
    print_to_console("zLib_Levels=");
  #else
    print_to_console("\nYou can speed up Precomp for THIS FILE with these parameters:\n");
    print_to_console("-zl");
  #endif

  bool first_one = true;
  for (i = 0; i < 81; i++) {
   i_sort = (i % 9) * 9 + (i / 9); // to get the displayed levels sorted
   if (precomp_mgr.obsolete.zlib_level_was_used[i_sort]) {
     if (!first_one) {
       print_to_console(",");
     } else {
       first_one = false;
     }
     print_to_console("%i%i", (i_sort%9) + 1, (i_sort/9) + 1);
     level_count++;
   }
  }

  std::string disable_methods("");
  std::array<std::tuple<bool, unsigned int, unsigned int, std::string>, 10> disable_formats{{
    {precomp_mgr.switches.use_pdf, precomp_mgr.statistics.recompressed_pdf_count, precomp_mgr.statistics.decompressed_pdf_count, "p"},
    {precomp_mgr.switches.use_zip, precomp_mgr.statistics.recompressed_zip_count, precomp_mgr.statistics.decompressed_zip_count, "z"},
    {precomp_mgr.switches.use_gzip, precomp_mgr.statistics.recompressed_gzip_count, precomp_mgr.statistics.decompressed_gzip_count, "g"},
    {precomp_mgr.switches.use_png, precomp_mgr.statistics.recompressed_png_count + precomp_mgr.statistics.recompressed_png_multi_count, precomp_mgr.statistics.decompressed_png_count + precomp_mgr.statistics.decompressed_png_multi_count, "n"},
    {precomp_mgr.switches.use_gif, precomp_mgr.statistics.recompressed_gif_count, precomp_mgr.statistics.decompressed_gif_count, "f"},
    {precomp_mgr.switches.use_jpg, precomp_mgr.statistics.recompressed_jpg_count + precomp_mgr.statistics.recompressed_jpg_prog_count, precomp_mgr.statistics.decompressed_jpg_count + precomp_mgr.statistics.decompressed_jpg_prog_count, "j"},
    {precomp_mgr.switches.use_swf, precomp_mgr.statistics.recompressed_swf_count, precomp_mgr.statistics.decompressed_swf_count, "s"},
    {precomp_mgr.switches.use_base64, precomp_mgr.statistics.recompressed_base64_count, precomp_mgr.statistics.decompressed_base64_count, "m"},
    {precomp_mgr.switches.use_bzip2, precomp_mgr.statistics.recompressed_bzip2_count, precomp_mgr.statistics.decompressed_bzip2_count, "b"},
    {precomp_mgr.switches.use_mp3, precomp_mgr.statistics.recompressed_mp3_count, precomp_mgr.statistics.decompressed_mp3_count, "3"},
  }};
  for (auto disable_format : disable_formats) {
    if ((std::get<0>(disable_format) && ((std::get<1>(disable_format) == 0) && (std::get<2>(disable_format) > 0)))) disable_methods += std::get<3>(disable_format);
  }
  if ( disable_methods.length() > 0 ) {
    #ifdef COMFORT
      print_to_console("\nCompression_Types_Disable=%s",disable_methods.c_str());
    #else
      print_to_console(" -t-%s",disable_methods.c_str());
    #endif
  }

  if (precomp_mgr.max_recursion_depth_reached) {
    print_to_console("\n\nMaximal recursion depth %i reached, increasing it could give better results.\n", precomp_mgr.max_recursion_depth);
  } else if (precomp_mgr.max_recursion_depth_used != precomp_mgr.max_recursion_depth) {
    #ifdef COMFORT
      print_to_console("\nMaximal_Recursion_Depth=");
    #else
      print_to_console(" -d");
    #endif
    print_to_console("%i", precomp_mgr.max_recursion_depth_used);
  }

  if ((level_count == 1) && (!precomp_mgr.switches.fast_mode)) {
    print_to_console("\n\nFast mode does exactly the same for this file, only faster.\n");
  }

  print_to_console("\n");
}

int compress_file_impl(Precomp& precomp_mgr, float min_percent, float max_percent) {

  precomp_mgr.ctx->comp_decomp_state = P_COMPRESS;
  if (precomp_mgr.recursion_depth == 0) write_header(precomp_mgr);
  precomp_mgr.enable_output_stream_otf_compression(precomp_mgr.ctx->compression_otf_method);

  precomp_mgr.ctx->global_min_percent = min_percent;
  precomp_mgr.ctx->global_max_percent = max_percent;

  precomp_mgr.ctx->uncompressed_length = -1;
  precomp_mgr.ctx->uncompressed_bytes_total = 0;
  precomp_mgr.ctx->uncompressed_bytes_written = 0;

  force_seekg(*precomp_mgr.ctx->fin, 0, std::ios_base::beg);
  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.ctx->in_buf), IN_BUF_SIZE);
  long long in_buf_pos = 0;
  precomp_mgr.ctx->cb = -1;

  precomp_mgr.ctx->anything_was_used = false;
  precomp_mgr.ctx->non_zlib_was_used = false;

  std::string tempfile_base = temp_files_tag() + "_decomp_";
  std::string tempfile;
  for (precomp_mgr.ctx->input_file_pos = 0; precomp_mgr.ctx->input_file_pos < precomp_mgr.ctx->fin_length; precomp_mgr.ctx->input_file_pos++) {
  tempfile = tempfile_base;

  precomp_mgr.ctx->compressed_data_found = false;

  bool ignore_this_pos = false;

  if ((in_buf_pos + IN_BUF_SIZE) <= (precomp_mgr.ctx->input_file_pos + CHECKBUF_SIZE)) {
    force_seekg(*precomp_mgr.ctx->fin, precomp_mgr.ctx->input_file_pos, std::ios_base::beg);
    precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.ctx->in_buf), IN_BUF_SIZE);
    in_buf_pos = precomp_mgr.ctx->input_file_pos;
    precomp_mgr.ctx->cb = 0;
  } else {
    precomp_mgr.ctx->cb++;
  }

  for (auto ignore_pos : precomp_mgr.switches.ignore_list) {
    ignore_this_pos = (ignore_pos == precomp_mgr.ctx->input_file_pos);
    if (ignore_this_pos) {
      break;
    }
  }

  if (!ignore_this_pos) {

    // ZIP header?
    if (((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb] == 'P') && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 1] == 'K')) && (precomp_mgr.switches.use_zip)) {
      // local file header?
      if ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 2] == 3) && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] == 4)) {
        if (DEBUG_MODE) {
        print_to_console("ZIP header detected\n");
        std::cout << "ZIP header detected at position " << precomp_mgr.ctx->input_file_pos << std::endl;
        }
        unsigned int compressed_size = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 21] << 24) + (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 20] << 16) + (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 19] << 8) + precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 18];
        unsigned int uncompressed_size = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 25] << 24) + (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 24] << 16) + (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 23] << 8) + precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 22];
        unsigned int filename_length = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 27] << 8) + precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 26];
        unsigned int extra_field_length = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 29] << 8) + precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 28];
        if (DEBUG_MODE) {
        print_to_console("compressed size: %i\n", compressed_size);
        print_to_console("uncompressed size: %i\n", uncompressed_size);
        print_to_console("file name length: %i\n", filename_length);
        print_to_console("extra field length: %i\n", extra_field_length);
        }

        if ((filename_length + extra_field_length) <= CHECKBUF_SIZE
            && precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 8] == 8 && precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 9] == 0) { // Compression method 8: Deflate

          int header_length = 30 + filename_length + extra_field_length;

          precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
          precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

          precomp_mgr.ctx->input_file_pos += header_length;

          tempfile += "zip";
          PrecompTmpFile tmp_zip;
          tmp_zip.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
          try_decompression_zip(precomp_mgr, header_length, tmp_zip);

          precomp_mgr.ctx->cb += header_length;

          if (!precomp_mgr.ctx->compressed_data_found) {
            precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
            precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
          }

        }
      }
    }

    if ((!precomp_mgr.ctx->compressed_data_found) && (precomp_mgr.switches.use_gzip)) { // no ZIP header -> GZip header?
      if ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb] == 31) && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 1] == 139)) {
        // check zLib header in GZip header
        int compression_method = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 2] & 15);
        if ((compression_method == 8) &&
           ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] & 224) == 0)  // reserved FLG bits must be zero
          ) {

          //((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 8] == 2) || (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 8] == 4)) { //XFL = 2 or 4
          //  TODO: Can be 0 also, check if other values are used, too.
          //
          //  TODO: compressed data is followed by CRC-32 and uncompressed
          //    size. Uncompressed size can be used to check if it is really
          //    a GZ stream.

          bool fhcrc = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] & 2) == 2;
          bool fextra = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] & 4) == 4;
          bool fname = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] & 8) == 8;
          bool fcomment = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] & 16) == 16;

          int header_length = 10;

          precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
          precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

          bool dont_compress = false;

          if (fhcrc || fextra || fname || fcomment) {
            int act_checkbuf_pos = 10;

            if (fextra) {
              int xlen = precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_checkbuf_pos] + (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_checkbuf_pos + 1] << 8);
              if ((act_checkbuf_pos + xlen) > CHECKBUF_SIZE) {
                dont_compress = true;
              } else {
                act_checkbuf_pos += 2;
                header_length += 2;
                act_checkbuf_pos += xlen;
                header_length += xlen;
              }
            }
            if ((fname) && (!dont_compress)) {
              do {
                act_checkbuf_pos ++;
                dont_compress = (act_checkbuf_pos == CHECKBUF_SIZE);
                header_length++;
              } while ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_checkbuf_pos - 1] != 0) && (!dont_compress));
            }
            if ((fcomment) && (!dont_compress)) {
              do {
                act_checkbuf_pos ++;
                dont_compress = (act_checkbuf_pos == CHECKBUF_SIZE);
                header_length++;
              } while ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_checkbuf_pos - 1] != 0) && (!dont_compress));
            }
            if ((fhcrc) && (!dont_compress)) {
              act_checkbuf_pos += 2;
              dont_compress = (act_checkbuf_pos > CHECKBUF_SIZE);
              header_length += 2;
            }
          }

          if (!dont_compress) {

            precomp_mgr.ctx->input_file_pos += header_length; // skip GZip header

            tempfile += "gzip";
            PrecompTmpFile tmp_gzip;
            tmp_gzip.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
            try_decompression_gzip(precomp_mgr, header_length, tmp_gzip);

            precomp_mgr.ctx->cb += header_length;

          }

          if (!precomp_mgr.ctx->compressed_data_found) {
            precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
            precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
          }
        }
      }
    }

    if ((!precomp_mgr.ctx->compressed_data_found) && (precomp_mgr.switches.use_pdf)) { // no Gzip header -> PDF FlateDecode?
      if (memcmp(precomp_mgr.ctx->in_buf + precomp_mgr.ctx->cb, "/FlateDecode", 12) == 0) {
        precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
        precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

        long long act_search_pos = 12;
        bool found_stream = false;
        do {
          if (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_search_pos] == 's') {
            if (memcmp(precomp_mgr.ctx->in_buf + precomp_mgr.ctx->cb + act_search_pos, "stream", 6) == 0) {
              found_stream = true;
              break;
            }
          }
          act_search_pos++;
        } while (act_search_pos < (CHECKBUF_SIZE - 6));

        if (found_stream) {

          // check if the stream is an image and width and height are given

          // read 4096 bytes before stream

          unsigned char type_buf[4097];
          int type_buf_length;

          type_buf[4096] = 0;

          if ((precomp_mgr.ctx->input_file_pos + act_search_pos) >= 4096) {
            force_seekg(*precomp_mgr.ctx->fin, (precomp_mgr.ctx->input_file_pos + act_search_pos) - 4096, std::ios_base::beg);
            precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(type_buf), 4096);
            type_buf_length = 4096;
          } else {
            force_seekg(*precomp_mgr.ctx->fin, 0, std::ios_base::beg);
            precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(type_buf), precomp_mgr.ctx->input_file_pos + act_search_pos);
            type_buf_length = precomp_mgr.ctx->input_file_pos + act_search_pos;
          }

          // find "<<"

          int start_pos = -1;

          for (int i = type_buf_length; i > 0; i--) {
            if ((type_buf[i] == '<') && (type_buf[i-1] == '<')) {
              start_pos = i;
              break;
            }
          }

          int width_val = 0, height_val = 0, bpc_val = 0;

          if ((start_pos > -1) && (precomp_mgr.switches.pdf_bmp_mode)) {

            int width_pos, height_pos, bpc_pos;

            // find "/Width"
            width_pos = (unsigned char*)strstr((const char*)(type_buf + start_pos), "/Width") - type_buf;

            if (width_pos > 0)
              for (int i = width_pos + 7; i < type_buf_length; i++) {
                if (((type_buf[i] >= '0') && (type_buf[i] <= '9')) || (type_buf[i] == ' ')) {
                  if (type_buf[i] != ' ') {
                    width_val = width_val * 10 + (type_buf[i] - '0');
                  }
                } else {
                  break;
                }
              }

            // find "/Height"
            height_pos = (unsigned char*)strstr((const char*)(type_buf + start_pos), "/Height") - type_buf;

            if (height_pos > 0)
              for (int i = height_pos + 8; i < type_buf_length; i++) {
                if (((type_buf[i] >= '0') && (type_buf[i] <= '9')) || (type_buf[i] == ' ')) {
                  if (type_buf[i] != ' ') {
                    height_val = height_val * 10 + (type_buf[i] - '0');
                  }
                } else {
                  break;
                }
              }

            // find "/BitsPerComponent"
            bpc_pos = (unsigned char*)strstr((const char*)(type_buf + start_pos), "/BitsPerComponent") - type_buf;

            if (bpc_pos > 0)
              for (int i = bpc_pos  + 18; i < type_buf_length; i++) {
                if (((type_buf[i] >= '0') && (type_buf[i] <= '9')) || (type_buf[i] == ' ')) {
                  if (type_buf[i] != ' ') {
                    bpc_val = bpc_val * 10 + (type_buf[i] - '0');
                  }
                } else {
                  break;
                }
              }

            if ((width_val != 0) && (height_val != 0) && (bpc_val != 0)) {
              if (DEBUG_MODE) {
                print_to_console("Possible image in PDF found: %i * %i, %i bit\n", width_val, height_val, bpc_val);
              }
            }
          }

          if ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_search_pos + 6] == 13) || (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_search_pos + 6] == 10)) {
            if ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_search_pos + 7] == 13) || (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_search_pos + 7] == 10)) {
              // seems to be two byte EOL - zLib Header present?
              if (((((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_search_pos + 8] << 8) + precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_search_pos + 9]) % 31) == 0) &&
                  ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_search_pos + 9] & 32) == 0)) { // FDICT must not be set
                int compression_method = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_search_pos + 8] & 15);
                if (compression_method == 8) {

                  int windowbits = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_search_pos + 8] >> 4) + 8;

                  precomp_mgr.ctx->input_file_pos += act_search_pos + 10; // skip PDF part

                  tempfile += "pdf";
                  PrecompTmpFile tmp_pdf;
                  tmp_pdf.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
                  try_decompression_pdf(precomp_mgr , -windowbits, act_search_pos + 10, width_val, height_val, bpc_val, tmp_pdf);

                  precomp_mgr.ctx->cb += act_search_pos + 10;
                }
              }
            } else {
              // seems to be one byte EOL - zLib Header present?
              if ((((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_search_pos + 7] << 8) + precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_search_pos + 8]) % 31) == 0) {
                int compression_method = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_search_pos + 7] & 15);
                if (compression_method == 8) {
                  int windowbits = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_search_pos + 7] >> 4) + 8;

                  precomp_mgr.ctx->input_file_pos += act_search_pos + 9; // skip PDF part

                  tempfile += "pdf";
                  PrecompTmpFile tmp_pdf;
                  tmp_pdf.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
                  try_decompression_pdf(precomp_mgr, -windowbits, act_search_pos + 9, width_val, height_val, bpc_val, tmp_pdf);

                  precomp_mgr.ctx->cb += act_search_pos + 9;
                }
              }
            }
          }
        }

        if (!precomp_mgr.ctx->compressed_data_found) {
          precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
          precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
        }
      }
    }

    if ((!precomp_mgr.ctx->compressed_data_found) && (precomp_mgr.switches.use_png)) { // no PDF header -> PNG IDAT?
      if (memcmp(precomp_mgr.ctx->in_buf + precomp_mgr.ctx->cb, "IDAT", 4) == 0) {

        // space for length and crc parts of IDAT chunks
        idat_lengths = (unsigned int*)(realloc(idat_lengths, 100 * sizeof(unsigned int)));
        idat_crcs = (unsigned int*)(realloc(idat_crcs, 100 * sizeof(unsigned int)));

        precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
        precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

        idat_count = 0;
        bool zlib_header_correct = false;
        int windowbits = 0;

        // get preceding length bytes
        if (precomp_mgr.ctx->input_file_pos >= 4) {
          force_seekg(*precomp_mgr.ctx->fin, precomp_mgr.ctx->input_file_pos - 4, std::ios_base::beg);

          precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 10);
         if (precomp_mgr.ctx->fin->gcount() == 10) {
          force_seekg(*precomp_mgr.ctx->fin, (long long)precomp_mgr.ctx->fin->tellg() - 2, std::ios_base::beg);

          idat_lengths[0] = (precomp_mgr.in[0] << 24) + (precomp_mgr.in[1] << 16) + (precomp_mgr.in[2] << 8) + precomp_mgr.in[3];
         if (idat_lengths[0] > 2) {

          // check zLib header and get windowbits
          zlib_header[0] = precomp_mgr.in[8];
          zlib_header[1] = precomp_mgr.in[9];
          if ((((precomp_mgr.in[8] << 8) + precomp_mgr.in[9]) % 31) == 0) {
            if ((precomp_mgr.in[8] & 15) == 8) {
              if ((precomp_mgr.in[9] & 32) == 0) { // FDICT must not be set
                windowbits = (precomp_mgr.in[8] >> 4) + 8;
                zlib_header_correct = true;
              }
            }
          }

          if (zlib_header_correct) {

            idat_count++;

            // go through additional IDATs
            for (;;) {
              force_seekg(*precomp_mgr.ctx->fin, (long long)precomp_mgr.ctx->fin->tellg() + idat_lengths[idat_count - 1], std::ios_base::beg);
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
                  idat_lengths = (unsigned int*)realloc(idat_lengths, (idat_count + 100) * sizeof(unsigned int));
                  idat_crcs = (unsigned int*)realloc(idat_crcs, (idat_count + 100) * sizeof(unsigned int));
                }

                if (idat_count > 65535) {
                  idat_count = 0;
                  break;
                }
              } else {
                break;
              }
            }
          }
         }
         }
        }

        if (idat_count == 1) {

          // try to recompress directly
          precomp_mgr.ctx->input_file_pos += 6;
          tempfile += "png";
          PrecompTmpFile tmp_png;
          tmp_png.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
          try_decompression_png(precomp_mgr, -windowbits, tmp_png);
          precomp_mgr.ctx->cb += 6;
        } else if (idat_count > 1) {
          // copy to temp0.dat before trying to recompress
          std::string png_tmp_filename = tempfile + "png";
          remove(png_tmp_filename.c_str());
          PrecompTmpFile tmp_png;
          tmp_png.open(png_tmp_filename, std::ios_base::in | std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);

          force_seekg(*precomp_mgr.ctx->fin, precomp_mgr.ctx->input_file_pos + 6, std::ios_base::beg); // start after zLib header

          idat_lengths[0] -= 2; // zLib header length
          for (int i = 0; i < idat_count; i++) {
            fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, tmp_png, idat_lengths[i]);
            force_seekg(*precomp_mgr.ctx->fin, (long long)precomp_mgr.ctx->fin->tellg() + 12, std::ios_base::beg);
          }
          idat_lengths[0] += 2;

          precomp_mgr.ctx->input_file_pos += 6;
          tempfile += "pngmulti";
          PrecompTmpFile tmp_pngmulti;
          tmp_pngmulti.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
          try_decompression_png_multi(precomp_mgr, tmp_png, -windowbits, tmp_pngmulti);
          precomp_mgr.ctx->cb += 6;
        }

        if (!precomp_mgr.ctx->compressed_data_found) {
          precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
          precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
        }

        free(idat_lengths);
        idat_lengths = NULL;
        free(idat_crcs);
        idat_crcs = NULL;
      }

    }

    if ((!precomp_mgr.ctx->compressed_data_found) && (precomp_mgr.switches.use_gif)) { // no PNG header -> GIF header?
      if ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb] == 'G') && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 1] == 'I') && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 2] == 'F')) {
        if ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] == '8') && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 5] == 'a')) {
          if ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 4] == '7') || (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 4] == '9')) {

            unsigned char version[5];

            for (int i = 0; i < 5; i++) {
              version[i] = precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + i];
            }

            precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
            precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

            tempfile += "gif";
            PrecompTmpFile tmp_gif;
            tmp_gif.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
            try_decompression_gif(precomp_mgr, version, tmp_gif);

            if (!precomp_mgr.ctx->compressed_data_found) {
              precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
              precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
            }
          }
        }
      }
    }

    if ((!precomp_mgr.ctx->compressed_data_found) && (precomp_mgr.switches.use_jpg)) { // no GIF header -> JPG header?
      if ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb] == 0xFF) && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 1] == 0xD8) && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 2] == 0xFF) && (
           (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] == 0xC0) || (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] == 0xC2) || (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] == 0xC4) || ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] >= 0xDB) && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] <= 0xFE))
         )) { // SOI (FF D8) followed by a valid marker for Baseline/Progressive JPEGs
        precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
        precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

        bool done = false, found = false;
        bool hasQuantTable = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] == 0xDB);
        bool progressive_flag = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] == 0xC2);
        precomp_mgr.ctx->input_file_pos+=2;

        do{
          force_seekg(*precomp_mgr.ctx->fin, precomp_mgr.ctx->input_file_pos, std::ios_base::beg);
          precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 5);
          if ((precomp_mgr.ctx->fin->gcount() != 5) || (precomp_mgr.in[0] != 0xFF))
              break;
          int length = (int)precomp_mgr.in[2]*256+(int)precomp_mgr.in[3];
          switch (precomp_mgr.in[1]){
            case 0xDB : {
              // FF DB XX XX QtId ...
              // Marker length (XX XX) must be = 2 + (multiple of 65 <= 260)
              // QtId:
              // bit 0..3: number of QT (0..3, otherwise error)
              // bit 4..7: precision of QT, 0 = 8 bit, otherwise 16 bit               
              if (length<=262 && ((length-2)%65)==0 && precomp_mgr.in[4]<=3) {
                hasQuantTable = true;
                precomp_mgr.ctx->input_file_pos += length+2;
              }
              else
                done = true;
              break;
            }
            case 0xC4 : {
              done = ((precomp_mgr.in[4]&0xF)>3 || (precomp_mgr.in[4]>>4)>1);
              precomp_mgr.ctx->input_file_pos += length+2;
              break;
            }
            case 0xDA : found = hasQuantTable;
            case 0xD9 : done = true; break; //EOI with no SOS?
            case 0xC2 : progressive_flag = true;
            case 0xC0 : done = (precomp_mgr.in[4] != 0x08);
            default: precomp_mgr.ctx->input_file_pos += length+2;
          }
        }
        while (!done);

        if (found){
          found = done = false;
          precomp_mgr.ctx->input_file_pos += 5;

          bool isMarker = (precomp_mgr.in[4] == 0xFF );
          size_t bytesRead = 0;
          for (;;) {
            if (done) break;
            precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), sizeof(precomp_mgr.in[0])* CHUNK);
            bytesRead = precomp_mgr.ctx->fin->gcount();
            if (!bytesRead) break;
            for (size_t i = 0; !done && (i < bytesRead); i++){
              precomp_mgr.ctx->input_file_pos++;
              if (!isMarker){
                isMarker = (precomp_mgr.in[i] == 0xFF );
              }
              else{
                done = (precomp_mgr.in[i] && ((precomp_mgr.in[i]&0xF8) != 0xD0) && ((progressive_flag)?(precomp_mgr.in[i] != 0xC4) && (precomp_mgr.in[i] != 0xDA):true));
                found = (precomp_mgr.in[i] == 0xD9);
                isMarker = false;
              }
            }
          }
        }

        if (found){
          long long jpg_length = precomp_mgr.ctx->input_file_pos - precomp_mgr.ctx->saved_input_file_pos;
          precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
          PrecompTmpFile tmp_jpg;
          tmp_jpg.open(tempfile + "jpg", std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
          try_decompression_jpg(precomp_mgr, jpg_length, progressive_flag, tmp_jpg);
        }
        if (!found || !precomp_mgr.ctx->compressed_data_found) {
          precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
          precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
        }
      }
      if (precomp_mgr.ctx->compressed_data_found) {  // If we did find jpg, we 'jpg' it to tempfile so we clean it afterwards
        tempfile += "jpg";
      }
    }

    if ((!precomp_mgr.ctx->compressed_data_found) && (precomp_mgr.switches.use_mp3)) { // no JPG header -> MP3 header?
      if ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb] == 0xFF) && ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 1] & 0xE0) == 0xE0)) { // frame start
        int mpeg = -1;
        int layer = -1;
        int samples = -1;
        int channels = -1;
        int protection = -1;
        int type = -1;

        int bits;
        int padding;
        int frame_size;
        int n = 0;
        long long mp3_parsing_cache_second_frame_candidate = -1;
        long long mp3_parsing_cache_second_frame_candidate_size = -1;

        long long mp3_length = 0;

        precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
        precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

        long long act_pos = precomp_mgr.ctx->input_file_pos;

        // parse frames until first invalid frame is found or end-of-file
        force_seekg(*precomp_mgr.ctx->fin, act_pos, std::ios_base::beg);
        
        for (;;) {
          precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 4);
          if (precomp_mgr.ctx->fin->gcount() != 4) break;
          // check syncword
          if ((precomp_mgr.in[0] != 0xFF) || ((precomp_mgr.in[1] & 0xE0) != 0xE0)) break;
          // compare data from header
          if (n == 0) {
            mpeg        = (precomp_mgr.in[1] >> 3) & 0x3;
            layer       = (precomp_mgr.in[1] >> 1) & 0x3;
            protection  = (precomp_mgr.in[1] >> 0) & 0x1;
            samples     = (precomp_mgr.in[2] >> 2) & 0x3;
            channels    = (precomp_mgr.in[3] >> 6) & 0x3;
            type = MBITS( precomp_mgr.in[1], 5, 1 );
            // avoid slowdown and multiple verbose messages on unsupported types that have already been detected
            if ((type != MPEG1_LAYER_III) && (precomp_mgr.ctx->saved_input_file_pos <= precomp_mgr.ctx->suppress_mp3_type_until[type])) {
                break;
            }
          } else {
            if (n == 1) {
              mp3_parsing_cache_second_frame_candidate = act_pos;
              mp3_parsing_cache_second_frame_candidate_size = act_pos - precomp_mgr.ctx->saved_input_file_pos;
            }
            if (type == MPEG1_LAYER_III) { // supported MP3 type, all header information must be identical to the first frame
              if (
                (mpeg       != ((precomp_mgr.in[1] >> 3) & 0x3)) ||
                (layer      != ((precomp_mgr.in[1] >> 1) & 0x3)) ||
                (protection != ((precomp_mgr.in[1] >> 0) & 0x1)) ||
                (samples    != ((precomp_mgr.in[2] >> 2) & 0x3)) ||
                (channels   != ((precomp_mgr.in[3] >> 6) & 0x3)) ||
                (type       != MBITS( precomp_mgr.in[1], 5, 1))) break;
            } else { // unsupported type, compare only type, ignore the other header information to get a longer stream
              if (type != MBITS( precomp_mgr.in[1], 5, 1)) break;
            }
          }

          bits     = (precomp_mgr.in[2] >> 4) & 0xF;
          padding  = (precomp_mgr.in[2] >> 1) & 0x1;
          // check for problems
          if ((mpeg == 0x1) || (layer == 0x0) ||
              (bits == 0x0) || (bits == 0xF) || (samples == 0x3)) break;
          // find out frame size
          frame_size = frame_size_table[mpeg][layer][samples][bits];
          if (padding) frame_size += (layer == LAYER_I) ? 4 : 1;

          // if this frame was part of a stream that already has been parsed, skip parsing
          if (n == 0) {
            if (act_pos == precomp_mgr.ctx->mp3_parsing_cache_second_frame) {
              n = precomp_mgr.ctx->mp3_parsing_cache_n;
              mp3_length = precomp_mgr.ctx->mp3_parsing_cache_mp3_length;

              // update values
              precomp_mgr.ctx->mp3_parsing_cache_second_frame = act_pos + frame_size;
              precomp_mgr.ctx->mp3_parsing_cache_n -= 1;
              precomp_mgr.ctx->mp3_parsing_cache_mp3_length -= frame_size;

              break;
            }
          }

          n++;
          mp3_length += frame_size;
          act_pos += frame_size;

          // if supported MP3 type, validate frames
          if ((type == MPEG1_LAYER_III) && (frame_size > 4)) {
            unsigned char header2 = precomp_mgr.in[2];
            unsigned char header3 = precomp_mgr.in[3];
            precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), frame_size - 4);
            if (precomp_mgr.ctx->fin->gcount() != (unsigned int)(frame_size - 4)) {
              // discard incomplete frame
              n--;
              mp3_length -= frame_size;
              break;
            }
            if (!is_valid_mp3_frame(precomp_mgr.in, header2, header3, protection)) {
                n = 0;
                break;
            }
          } else {
            force_seekg(*precomp_mgr.ctx->fin, act_pos, std::ios_base::beg);
          }
        }

        // conditions for proper first frame: 5 consecutive frames
        if (n >= 5) {
          if (mp3_parsing_cache_second_frame_candidate > -1) {
            precomp_mgr.ctx->mp3_parsing_cache_second_frame = mp3_parsing_cache_second_frame_candidate;
            precomp_mgr.ctx->mp3_parsing_cache_n = n - 1;
            precomp_mgr.ctx->mp3_parsing_cache_mp3_length = mp3_length - mp3_parsing_cache_second_frame_candidate_size;
          }

          long long position_length_sum = precomp_mgr.ctx->saved_input_file_pos + mp3_length;

          // type must be MPEG-1, Layer III, packMP3 won't process any other files
          if ( type == MPEG1_LAYER_III ) {
            // sums of position and length of last MP3 errors are suppressed to avoid slowdowns
            if    ((precomp_mgr.ctx->suppress_mp3_big_value_pairs_sum != position_length_sum)
               && (precomp_mgr.ctx->suppress_mp3_non_zero_padbits_sum != position_length_sum)
               && (precomp_mgr.ctx->suppress_mp3_inconsistent_emphasis_sum != position_length_sum)
               && (precomp_mgr.ctx->suppress_mp3_inconsistent_original_bit != position_length_sum)) {
              tempfile += "mp3";
              PrecompTmpFile tmp_mp3;
              tmp_mp3.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
              try_decompression_mp3(precomp_mgr, mp3_length, tmp_mp3);
            }
          } else if (type > 0) {
            precomp_mgr.ctx->suppress_mp3_type_until[type] = position_length_sum;
            if (DEBUG_MODE) {
              std::cout << "Unsupported MP3 type found at position " << precomp_mgr.ctx->saved_input_file_pos << ", length " << mp3_length << std::endl;
              print_to_console("Type: %s\n", filetype_description[type]);
            }
          }
        }

        if (!precomp_mgr.ctx->compressed_data_found) {
          precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
          precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
        }
      }
    }

    if ((!precomp_mgr.ctx->compressed_data_found) && (precomp_mgr.switches.use_swf)) { // no MP3 header -> SWF header?
      // CWS = Compressed SWF file
      if ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb] == 'C') && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 1] == 'W') && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 2] == 'S')) {
        // check zLib header
        if (((((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 8] << 8) + precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 9]) % 31) == 0) &&
           ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 9] & 32) == 0)) { // FDICT must not be set
          int compression_method = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 8] & 15);
          if (compression_method == 8) {
            int windowbits = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 8] >> 4) + 8;

            precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
            precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

            precomp_mgr.ctx->input_file_pos += 10; // skip CWS and zLib header

            tempfile += "swf";
            PrecompTmpFile tmp_swf;
            tmp_swf.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
            try_decompression_swf(precomp_mgr, -windowbits, tmp_swf);

            precomp_mgr.ctx->cb += 10;

            if (!precomp_mgr.ctx->compressed_data_found) {
              precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
              precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
            }
          }
        }
      }
    }

    if ((!precomp_mgr.ctx->compressed_data_found) && (precomp_mgr.switches.use_base64)) { // no SWF header -> Base64?
    if ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 1] == 'o') && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 2] == 'n') && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] == 't') && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 4] == 'e')) {
      unsigned char cte_detect[33];
      for (int i = 0; i < 33; i++) {
        cte_detect[i] = tolower(precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + i]);
      }
      if (memcmp(cte_detect, "content-transfer-encoding: base64", 33) == 0) {
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

          precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
          precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

          precomp_mgr.ctx->input_file_pos += base64_header_length; // skip "header"

          tempfile += "base64";
          PrecompTmpFile tmp_base64;
          tmp_base64.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
          try_decompression_base64(precomp_mgr, base64_header_length, tmp_base64);

          precomp_mgr.ctx->cb += base64_header_length;

          if (!precomp_mgr.ctx->compressed_data_found) {
            precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
            precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
          }
        }
      }
    }
    }

    if ((!precomp_mgr.ctx->compressed_data_found) && (precomp_mgr.switches.use_bzip2)) { // no Base64 header -> bZip2?
      // BZhx = header, x = compression level/blocksize (1-9)
      if ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb] == 'B') && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 1] == 'Z') && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 2] == 'h')) {
        int compression_level = precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] - '0';
        if ((compression_level >= 1) && (compression_level <= 9)) {
          precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
          precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

          tempfile += "bzip2";
          PrecompTmpFile tmp_bzip2;
          tmp_bzip2.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
          try_decompression_bzip2(precomp_mgr, compression_level, tmp_bzip2);

          if (!precomp_mgr.ctx->compressed_data_found) {
            precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
            precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
          }
        }
      }
    }


   // nothing so far -> if intense mode is active, look for raw zLib header
   if (intense_mode_is_active(precomp_mgr)) {
    if (!precomp_mgr.ctx->compressed_data_found) {
      bool ignore_this_position = false;
      if (precomp_mgr.ctx->intense_ignore_offsets->size() > 0) {
        auto first = precomp_mgr.ctx->intense_ignore_offsets->begin();
        while (*first < precomp_mgr.ctx->input_file_pos) {
          precomp_mgr.ctx->intense_ignore_offsets->erase(first);
          if (precomp_mgr.ctx->intense_ignore_offsets->size() == 0) break;
          first = precomp_mgr.ctx->intense_ignore_offsets->begin();
        }

        if (precomp_mgr.ctx->intense_ignore_offsets->size() > 0) {
          if (*first == precomp_mgr.ctx->input_file_pos) {
            ignore_this_position = true;
            precomp_mgr.ctx->intense_ignore_offsets->erase(first);
          }
        }
      }

      if (!ignore_this_position) {
        if (((((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb] << 8) + precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 1]) % 31) == 0) &&
            ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 1] & 32) == 0)) { // FDICT must not be set
          int compression_method = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb] & 15);
          if (compression_method == 8) {
            int windowbits = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb] >> 4) + 8;

            if (check_inf_result(precomp_mgr, precomp_mgr.ctx->in_buf, precomp_mgr.out, precomp_mgr.ctx->cb + 2, -windowbits)) {
              precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
              precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

              precomp_mgr.ctx->input_file_pos += 2; // skip zLib header

              tempfile += "zlib";
              PrecompTmpFile tmp_zlib;
              tmp_zlib.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
              try_decompression_zlib(precomp_mgr, -windowbits, tmp_zlib);

              precomp_mgr.ctx->cb += 2;

              if (!precomp_mgr.ctx->compressed_data_found) {
                precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
                precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
              }
            }
          }
        }
      }
    }
   }

   // nothing so far -> if brute mode is active, brute force for zLib streams
    if (brute_mode_is_active(precomp_mgr)) {
    if (!precomp_mgr.ctx->compressed_data_found) {
      bool ignore_this_position = false;
      if (precomp_mgr.ctx->brute_ignore_offsets->size() > 0) {
        auto first = precomp_mgr.ctx->brute_ignore_offsets->begin();
        while (*first < precomp_mgr.ctx->input_file_pos) {
          precomp_mgr.ctx->brute_ignore_offsets->erase(first);
          if (precomp_mgr.ctx->brute_ignore_offsets->size() == 0) break;
          first = precomp_mgr.ctx->brute_ignore_offsets->begin();
        }

        if (precomp_mgr.ctx->brute_ignore_offsets->size() > 0) {
          if (*first == precomp_mgr.ctx->input_file_pos) {
            ignore_this_position = true;
            precomp_mgr.ctx->brute_ignore_offsets->erase(first);
          }
        }
      }

      if (!ignore_this_position) {
        precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
        precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

        if (check_inf_result(precomp_mgr, precomp_mgr.ctx->in_buf, precomp_mgr.out, precomp_mgr.ctx->cb, -15, true)) {
          tempfile += "brute";
          PrecompTmpFile tmp_brute;
          tmp_brute.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
          try_decompression_brute(precomp_mgr, tmp_brute);
        }

        if (!precomp_mgr.ctx->compressed_data_found) {
          precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
          precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
        }
      }
    }
   }

  }

    if (!precomp_mgr.ctx->compressed_data_found) {
      if (precomp_mgr.ctx->uncompressed_length == -1) {
        start_uncompressed_data(*precomp_mgr.ctx);
      }
      precomp_mgr.ctx->uncompressed_length++;
      precomp_mgr.ctx->uncompressed_bytes_total++;
    }

  }

  end_uncompressed_data(precomp_mgr);

  precomp_mgr.ctx->fout = nullptr; // To close the outfile TODO: maybe we should just make sure the whole last context gets destroyed if at recursion_depth == 0?

  return (precomp_mgr.ctx->anything_was_used || precomp_mgr.ctx->non_zlib_was_used) ? RETURN_SUCCESS : RETURN_NOTHING_DECOMPRESSED;
}

int wrap_with_exception_catch(std::function<int()> func)
{
  // Didn't want to wrap those big functions on a try catch, and also didn't want to leak out our exceptions to the outside, because we want to mantain a C API
  try
  {
    return func();
  }
  catch (const PrecompError& exc)
  {
    return exc.error_code;
  }
  catch (const std::exception& exc)
  {
    return ERR_GENERIC_OR_UNKNOWN;
  }
}

int compress_file(Precomp& precomp_mgr, float min_percent, float max_percent)
{
  return wrap_with_exception_catch([&]() { return compress_file_impl(precomp_mgr, min_percent, max_percent); });
}

int BrunsliStringWriter(void* data, const uint8_t* buf, size_t count) {
	std::string* output = reinterpret_cast<std::string*>(data);
	output->append(reinterpret_cast<const char*>(buf), count);
	return count;
}

int decompress_file_impl(Precomp& precomp_mgr) {

  long long fin_pos;

  precomp_mgr.ctx->comp_decomp_state = P_DECOMPRESS;
  precomp_mgr.enable_input_stream_otf_decompression();

  std::string tmp_tag = temp_files_tag();
  std::string tempfile_base = tmp_tag + "_recomp_";
  std::string tempfile2_base = tmp_tag + "_recomp2_";
  std::string tempfile;
  std::string tempfile2;

  fin_pos = precomp_mgr.ctx->fin->tellg();

while (precomp_mgr.ctx->fin->good()) {
  tempfile = tempfile_base;
  tempfile2 = tempfile2_base;

  unsigned char header1 = precomp_mgr.ctx->fin->get();
  if (!precomp_mgr.ctx->fin->good()) break;
  if (header1 == 0) { // uncompressed data
    long long uncompressed_data_length;
    uncompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);

    if (uncompressed_data_length == 0) break; // end of PCF file, used by bZip2 compress-on-the-fly

    if (DEBUG_MODE) {
    std::cout << "Uncompressed data, length=" << uncompressed_data_length << std::endl;
    }

    fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, uncompressed_data_length);

  } else { // decompressed data, recompress

    unsigned char headertype = precomp_mgr.ctx->fin->get();

    switch (headertype) {
    case D_PDF: { // PDF recompression
      recompress_deflate_result rdres;
      unsigned hdr_length;
      // restore PDF header
      ostream_printf(*precomp_mgr.ctx->fout, "/FlateDecode");
      fin_fget_deflate_hdr(*precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, rdres, header1, precomp_mgr.in, hdr_length, false);
      fin_fget_recon_data(*precomp_mgr.ctx->fin, rdres);
      int bmp_c = (header1 >> 6);

      debug_deflate_reconstruct(rdres, "PDF", hdr_length, 0);
      if (DEBUG_MODE) {
        if (bmp_c == 1) print_to_console("Skipping BMP header (8-Bit)\n");
        if (bmp_c == 2) print_to_console("Skipping BMP header (24-Bit)\n");
      }

      // read BMP header
      int bmp_width = 0;

      switch (bmp_c) {
        case 1:
          precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 54+1024);
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
      } else { // lines aligned to 4 byte, skip those bytes
        // recompress directly to fout, but skipping bytes
        read_part = bmp_width;
        skip_part = (-bmp_width) & 3;
      }
      if (!try_reconstructing_deflate_skip(precomp_mgr, *precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, rdres, read_part, skip_part)) {
        throw PrecompError(ERR_DURING_RECOMPRESSION);
      }
      break;
    }     
    case D_ZIP: { // ZIP recompression
      recompress_deflate_result rdres;
      unsigned hdr_length;
      int64_t recursion_data_length;
      precomp_mgr.ctx->fout->put('P');
      precomp_mgr.ctx->fout->put('K');
      precomp_mgr.ctx->fout->put(3);
      precomp_mgr.ctx->fout->put(4);
      tempfile += "zip";
      PrecompTmpFile tmp_zip;
      tmp_zip.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
      bool ok = fin_fget_deflate_rec(precomp_mgr, rdres, header1, precomp_mgr.in, hdr_length, false, recursion_data_length, tmp_zip);

      debug_deflate_reconstruct(rdres, "ZIP", hdr_length, recursion_data_length);

      if (!ok) {
        throw PrecompError(ERR_DURING_RECOMPRESSION);
      }
      break;
    }
    case D_GZIP: { // GZip recompression
      recompress_deflate_result rdres;
      unsigned hdr_length;
      int64_t recursion_data_length;
      precomp_mgr.ctx->fout->put(31);
      precomp_mgr.ctx->fout->put(139);
      tempfile += "gzip";
      PrecompTmpFile tmp_gzip;
      tmp_gzip.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
      bool ok = fin_fget_deflate_rec(precomp_mgr, rdres, header1, precomp_mgr.in, hdr_length, false, recursion_data_length, tmp_gzip);

      debug_deflate_reconstruct(rdres, "GZIP", hdr_length, recursion_data_length);

      if (!ok) {
        throw PrecompError(ERR_DURING_RECOMPRESSION);
      }
      break;
    }
    case D_PNG: { // PNG recompression
      recompress_deflate_result rdres;
      unsigned hdr_length;
      // restore IDAT
      ostream_printf(*precomp_mgr.ctx->fout, "IDAT");

      fin_fget_deflate_hdr(*precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, rdres, header1, precomp_mgr.in, hdr_length, true);
      fin_fget_recon_data(*precomp_mgr.ctx->fin, rdres);
      debug_sums(rdres);
      debug_pos();

      debug_deflate_reconstruct(rdres, "PNG", hdr_length, 0);

      if (!try_reconstructing_deflate(precomp_mgr, *precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, rdres)) {
        throw PrecompError(ERR_DURING_RECOMPRESSION);
      }
      debug_pos();
      break;
    }
    case D_MULTIPNG: { // PNG multi recompression
      recompress_deflate_result rdres;
      unsigned hdr_length;
      // restore first IDAT
      ostream_printf(*precomp_mgr.ctx->fout, "IDAT");
      
      fin_fget_deflate_hdr(*precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, rdres, header1, precomp_mgr.in, hdr_length, true);

      // get IDAT count
      idat_count = fin_fget_vlint(*precomp_mgr.ctx->fin) + 1;

      idat_crcs = (unsigned int*)(realloc(idat_crcs, idat_count * sizeof(unsigned int)));
      idat_lengths = (unsigned int*)(realloc(idat_lengths, idat_count * sizeof(unsigned int)));

      // get first IDAT length
      idat_lengths[0] = fin_fget_vlint(*precomp_mgr.ctx->fin) - 2; // zLib header length

      // get IDAT chunk lengths and CRCs
      for (int i = 1; i < idat_count; i++) {
        idat_crcs[i]    = fin_fget32(*precomp_mgr.ctx->fin);
        idat_lengths[i] = fin_fget_vlint(*precomp_mgr.ctx->fin);
      }

      fin_fget_recon_data(*precomp_mgr.ctx->fin, rdres);
      debug_sums(rdres);
      debug_pos();

      debug_deflate_reconstruct(rdres, "PNG multi", hdr_length, 0);

      if (!try_reconstructing_deflate_multipng(precomp_mgr, *precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, rdres, idat_count, idat_crcs, idat_lengths)) {
        throw PrecompError(ERR_DURING_RECOMPRESSION);
      }
      debug_pos();
      free(idat_lengths);
      idat_lengths = NULL;
      free(idat_crcs);
      idat_crcs = NULL;
      break;
    }
    case D_GIF: { // GIF recompression

      if (DEBUG_MODE) {
      print_to_console("Decompressed data - GIF\n");
      }

      try_recompression_gif(precomp_mgr, header1, tempfile, tempfile2);
      break;
    }
    case D_JPG: { // JPG recompression
      tempfile += "jpg";
      tempfile2 += "jpg";

      if (DEBUG_MODE) {
      print_to_console("Decompressed data - JPG\n");
      }

      bool mjpg_dht_used = ((header1 & 4) == 4);
	  bool brunsli_used = ((header1 & 8) == 8);
	  bool brotli_used = ((header1 & 16) == 16);

      long long recompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);
      long long decompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);

      if (DEBUG_MODE) {
      std::cout << "Recompressed length: " << recompressed_data_length << " - decompressed length: " << decompressed_data_length << std::endl;
      }

      char recompress_msg[256];
      unsigned char* jpg_mem_in = NULL;
      unsigned char* jpg_mem_out = NULL;
      unsigned int jpg_mem_out_size = -1;
      bool in_memory = (recompressed_data_length <= JPG_MAX_MEMORY_SIZE);
      bool recompress_success = false;

      if (in_memory) {
        jpg_mem_in = new unsigned char[decompressed_data_length];
        memiostream memstream = memiostream::make(jpg_mem_in, jpg_mem_in + decompressed_data_length);
        fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, memstream, decompressed_data_length);

		if (brunsli_used) {
			brunsli::JPEGData jpegData;
			if (brunsli::BrunsliDecodeJpeg(jpg_mem_in, decompressed_data_length, &jpegData, brotli_used) == brunsli::BRUNSLI_OK) {
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
		} else {
			pjglib_init_streams(jpg_mem_in, 1, decompressed_data_length, jpg_mem_out, 1);
			recompress_success = pjglib_convert_stream2mem(&jpg_mem_out, &jpg_mem_out_size, recompress_msg);
		}
      } else {
        remove(tempfile.c_str());

        {
          WrappedFStream ftempout;
          ftempout.open(tempfile, std::ios_base::out | std::ios_base::binary);
          fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, ftempout, decompressed_data_length);
          ftempout.close();
        }

        remove(tempfile2.c_str());

        recompress_success = pjglib_convert_file2file(const_cast<char*>(tempfile.c_str()), const_cast<char*>(tempfile2.c_str()), recompress_msg);
      }

      if (!recompress_success) {
        if (DEBUG_MODE) print_to_console("packJPG error: %s\n", recompress_msg);
        throw PrecompError(ERR_DURING_RECOMPRESSION);
      }

      PrecompTmpFile frecomp;
      if (!in_memory) {
        frecomp.open(tempfile2, std::ios_base::in | std::ios_base::binary);
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
              found_ffda = (jpg_mem_out[ffda_pos] == 0xDA);
              if (found_ffda) break;
              found_ff = false;
            } else {
              found_ff = (jpg_mem_out[ffda_pos] == 0xFF);
            }
          } while (!found_ffda);
        } else {
          do {
            ffda_pos++;
            frecomp.read(reinterpret_cast<char*>(precomp_mgr.in), 1);
            if (frecomp.gcount() != 1) break;
            if (found_ff) {
              found_ffda = (precomp_mgr.in[0] == 0xDA);
              if (found_ffda) break;
              found_ff = false;
            } else {
              found_ff = (precomp_mgr.in[0] == 0xFF);
            }
          } while (!found_ffda);
        }

        if ((!found_ffda) || ((ffda_pos - 1 - MJPGDHT_LEN) < 0)) {
          throw std::runtime_error(make_cstyle_format_string("ERROR: Motion JPG stream corrupted\n"));
        }

        // remove motion JPG huffman table
        if (in_memory) {
          memiostream memstream1 = memiostream::make(jpg_mem_out, jpg_mem_out + ffda_pos - 1 - MJPGDHT_LEN);
          fast_copy(precomp_mgr, memstream1, *precomp_mgr.ctx->fout, ffda_pos - 1 - MJPGDHT_LEN);
          memiostream memstream2 = memiostream::make(jpg_mem_out + (ffda_pos - 1), jpg_mem_out + (recompressed_data_length + MJPGDHT_LEN) - (ffda_pos - 1));
          fast_copy(precomp_mgr, memstream2, *precomp_mgr.ctx->fout, (recompressed_data_length + MJPGDHT_LEN) - (ffda_pos - 1));
        } else {
          force_seekg(frecomp, frecomp_pos, std::ios_base::beg);
          fast_copy(precomp_mgr, frecomp, *precomp_mgr.ctx->fout, ffda_pos - 1 - MJPGDHT_LEN);

          frecomp_pos += ffda_pos - 1;
          force_seekg(frecomp, frecomp_pos, std::ios_base::beg);
          fast_copy(precomp_mgr, frecomp, *precomp_mgr.ctx->fout, (recompressed_data_length + MJPGDHT_LEN) - (ffda_pos - 1));
        }
      } else {
        if (in_memory) {
          memiostream memstream = memiostream::make(jpg_mem_out, jpg_mem_out + recompressed_data_length);
          fast_copy(precomp_mgr, memstream, *precomp_mgr.ctx->fout, recompressed_data_length);
        } else {
          fast_copy(precomp_mgr, frecomp, *precomp_mgr.ctx->fout, recompressed_data_length);
        }
      }

      if (in_memory) {
        if (jpg_mem_in != NULL) delete[] jpg_mem_in;
        if (jpg_mem_out != NULL) delete[] jpg_mem_out;
      } else {
        frecomp.close();

        remove(tempfile2.c_str());
        remove(tempfile.c_str());
      }
      break;
    }
    case D_SWF: { // SWF recompression
      recompress_deflate_result rdres;
      unsigned hdr_length;
      int64_t recursion_data_length;
      precomp_mgr.ctx->fout->put('C');
      precomp_mgr.ctx->fout->put('W');
      precomp_mgr.ctx->fout->put('S');
      tempfile += "swf";
      PrecompTmpFile tmp_swf;
      tmp_swf.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
      bool ok = fin_fget_deflate_rec(precomp_mgr, rdres, header1, precomp_mgr.in, hdr_length, true, recursion_data_length, tmp_swf);

      debug_deflate_reconstruct(rdres, "SWF", hdr_length, recursion_data_length);

      if (!ok) {
        throw PrecompError(ERR_DURING_RECOMPRESSION);
      }
      break;
    }
    case D_BASE64: { // Base64 recompression
      tempfile += "base64";

      if (DEBUG_MODE) {
      print_to_console("Decompressed data - Base64\n");
      }

      int line_case = (header1 >> 2) & 3;
      bool recursion_used = ((header1 & 128) == 128);

      // restore Base64 "header"
      int base64_header_length = fin_fget_vlint(*precomp_mgr.ctx->fin);

      if (DEBUG_MODE) {
        print_to_console("Base64 header length: %i\n", base64_header_length);
      }
      precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), base64_header_length);
      precomp_mgr.ctx->fout->put(*(precomp_mgr.in)+1); // first char was decreased
      precomp_mgr.ctx->fout->write(reinterpret_cast<char*>(precomp_mgr.in + 1), base64_header_length - 1);

      // read line length list
      int line_count = fin_fget_vlint(*precomp_mgr.ctx->fin);

      unsigned int* base64_line_len = new unsigned int[line_count];

      if (line_case == 2) {
        for (int i = 0; i < line_count; i++) {
          base64_line_len[i] = precomp_mgr.ctx->fin->get();
        }
      } else {
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

      if (DEBUG_MODE) {
        if (recursion_used) {
          std::cout << "Recursion data length: " << recursion_data_length << std::endl;
        } else {
          std::cout << "Encoded length: " << recompressed_data_length << " - decoded length: " << decompressed_data_length << std::endl;
        }
      }

      // re-encode Base64

      if (recursion_used) {
        PrecompTmpFile tmp_base64;
        tmp_base64.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
        recursion_result r = recursion_decompress(precomp_mgr, recursion_data_length, tmp_base64);
        auto wrapped_istream_frecurse = WrappedIStream(r.frecurse.get(), false);
        base64_reencode(precomp_mgr, wrapped_istream_frecurse, *precomp_mgr.ctx->fout, line_count, base64_line_len, r.file_length, decompressed_data_length);
        r.frecurse->close();
        remove(r.file_name.c_str());
      } else {
        base64_reencode(precomp_mgr, *precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, line_count, base64_line_len, recompressed_data_length, decompressed_data_length);
      }

      delete[] base64_line_len;
      break;
    }
    case D_BZIP2: { // bZip2 recompression
      tempfile += "bzip2";

      if (DEBUG_MODE) {
      print_to_console("Decompressed data - bZip2\n");
      }

      unsigned char header2 = precomp_mgr.ctx->fin->get();

      bool penalty_bytes_stored = ((header1 & 2) == 2);
      bool recursion_used = ((header1 & 128) == 128);
      int level = header2;

      if (DEBUG_MODE) {
      print_to_console("Compression level: %i\n", level);
      }

      // read penalty bytes
      if (penalty_bytes_stored) {
        precomp_mgr.ctx->penalty_bytes_len = fin_fget_vlint(*precomp_mgr.ctx->fin);
        precomp_mgr.ctx->fin->read(precomp_mgr.ctx->penalty_bytes.data(), precomp_mgr.ctx->penalty_bytes_len);
      }

      long long recompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);
      long long decompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);

      long long recursion_data_length = 0;
      if (recursion_used) {
        recursion_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);
      }

      if (DEBUG_MODE) {
        if (recursion_used) {
          std::cout << "Recursion data length: " << recursion_data_length << std::endl;
        } else {
          std::cout << "Recompressed length: " << recompressed_data_length << " - decompressed length: " << decompressed_data_length << std::endl;
        }
      }

      long long old_fout_pos = precomp_mgr.ctx->fout->tellp();

      if (recursion_used) {
        PrecompTmpFile tmp_bzip2;
        tmp_bzip2.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
        recursion_result r = recursion_decompress(precomp_mgr, recursion_data_length, tmp_bzip2);
        auto wrapped_istream_frecurse = WrappedIStream(r.frecurse.get(), false);
        precomp_mgr.ctx->retval = def_part_bzip2(precomp_mgr, wrapped_istream_frecurse, *precomp_mgr.ctx->fout, level, decompressed_data_length, recompressed_data_length);
        r.frecurse->close();
        remove(r.file_name.c_str());
      } else {
        precomp_mgr.ctx->retval = def_part_bzip2(precomp_mgr, *precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, level, decompressed_data_length, recompressed_data_length);
      }

      if (precomp_mgr.ctx->retval != BZ_OK) {
        std::cout << "BZIP2 retval = " << precomp_mgr.ctx->retval << std::endl;
        throw PrecompError(ERR_DURING_RECOMPRESSION);
      }

      if (penalty_bytes_stored) {
        precomp_mgr.ctx->fout->flush();

        long long fsave_fout_pos = precomp_mgr.ctx->fout->tellp();
        int pb_pos = 0;
        for (int pbc = 0; pbc < precomp_mgr.ctx->penalty_bytes_len; pbc += 5) {
          pb_pos = ((unsigned char)precomp_mgr.ctx->penalty_bytes[pbc]) << 24;
          pb_pos += ((unsigned char)precomp_mgr.ctx->penalty_bytes[pbc + 1]) << 16;
          pb_pos += ((unsigned char)precomp_mgr.ctx->penalty_bytes[pbc + 2]) << 8;
          pb_pos += (unsigned char)precomp_mgr.ctx->penalty_bytes[pbc + 3];

          precomp_mgr.ctx->fout->seekp(old_fout_pos + pb_pos, std::ios_base::beg);
          precomp_mgr.ctx->fout->write(precomp_mgr.ctx->penalty_bytes.data() + pbc + 4, 1);
        }

        precomp_mgr.ctx->fout->seekp(fsave_fout_pos, std::ios_base::beg);
      }
      break;
    }
    case D_MP3: { // MP3 recompression
      tempfile += "mp3";
      tempfile2 += "mp3";

      if (DEBUG_MODE) {
      print_to_console("Decompressed data - MP3\n");
      }

      long long recompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);
      long long decompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);

      if (DEBUG_MODE) {
      std::cout << "Recompressed length: " << recompressed_data_length << " - decompressed length: " << decompressed_data_length << std::endl;
      }

      char recompress_msg[256];
      unsigned char* mp3_mem_in = NULL;
      unsigned char* mp3_mem_out = NULL;
      unsigned int mp3_mem_out_size = -1;
      bool in_memory = (recompressed_data_length <= MP3_MAX_MEMORY_SIZE);

      bool recompress_success = false;

      if (in_memory) {
        mp3_mem_in = new unsigned char[decompressed_data_length];
        memiostream memstream = memiostream::make(mp3_mem_in, mp3_mem_in + decompressed_data_length);
        fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, memstream, decompressed_data_length);

        pmplib_init_streams(mp3_mem_in, 1, decompressed_data_length, mp3_mem_out, 1);
        recompress_success = pmplib_convert_stream2mem(&mp3_mem_out, &mp3_mem_out_size, recompress_msg);
      } else {
        remove(tempfile.c_str());

        {
          WrappedFStream ftempout;
          ftempout.open(tempfile, std::ios_base::out | std::ios_base::binary);
          fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, ftempout, decompressed_data_length);
          ftempout.close();
        }

        remove(tempfile2.c_str());

        recompress_success = pmplib_convert_file2file(const_cast<char*>(tempfile.c_str()), const_cast<char*>(tempfile2.c_str()), recompress_msg);
      }

      if (!recompress_success) {
        if (DEBUG_MODE) print_to_console("packMP3 error: %s\n", recompress_msg);
        throw PrecompError(ERR_DURING_RECOMPRESSION);
      }

      if (in_memory) {
        memiostream memstream = memiostream::make(mp3_mem_out, mp3_mem_out + recompressed_data_length);
        fast_copy(precomp_mgr, memstream, *precomp_mgr.ctx->fout, recompressed_data_length);

        if (mp3_mem_in != NULL) delete[] mp3_mem_in;
        if (mp3_mem_out != NULL) delete[] mp3_mem_out;
      } else {
        {
          PrecompTmpFile frecomp;
          frecomp.open(tempfile2, std::ios_base::in | std::ios_base::binary);
          fast_copy(precomp_mgr, frecomp, *precomp_mgr.ctx->fout, recompressed_data_length);
        }

        remove(tempfile2.c_str());
        remove(tempfile.c_str());
      }
      break;
    }
    case D_BRUTE: { // brute mode recompression
      recompress_deflate_result rdres;
      unsigned hdr_length;
      int64_t recursion_data_length;
      tempfile += "brute";
      PrecompTmpFile tmp_brute;
      tmp_brute.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
      bool ok = fin_fget_deflate_rec(precomp_mgr, rdres, header1, precomp_mgr.in, hdr_length, false, recursion_data_length, tmp_brute);

      debug_deflate_reconstruct(rdres, "brute mode", hdr_length, recursion_data_length);

      if (!ok) {
        throw PrecompError(ERR_DURING_RECOMPRESSION);
      }
      break;
    }
    case D_RAW: { // raw zLib recompression
      recompress_deflate_result rdres;
      unsigned hdr_length;
      int64_t recursion_data_length;
      tempfile += "zlib";
      PrecompTmpFile tmp_zlib;
      tmp_zlib.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
      bool ok = fin_fget_deflate_rec(precomp_mgr, rdres, header1, precomp_mgr.in, hdr_length, true, recursion_data_length, tmp_zlib);

      debug_deflate_reconstruct(rdres, "raw zLib", hdr_length, recursion_data_length);

      if (!ok) {
        throw PrecompError(ERR_DURING_RECOMPRESSION);
      }
      break;
    }
    default:
      throw std::runtime_error(make_cstyle_format_string("ERROR: Unsupported stream type %i\n", headertype));
    }

  }

  fin_pos = precomp_mgr.ctx->fin->tellg();
  if (precomp_mgr.ctx->compression_otf_method != OTF_NONE) {
    if (precomp_mgr.ctx->fin->eof()) break;
    if (fin_pos >= precomp_mgr.ctx->fin_length) fin_pos = precomp_mgr.ctx->fin_length - 1;
  }
}

  return RETURN_SUCCESS;
}

int decompress_file(Precomp& precomp_mgr)
{
  return wrap_with_exception_catch([&]() { return decompress_file_impl(precomp_mgr); });
}

int convert_file_impl(Precomp& precomp_mgr) {
  int bytes_read;
  unsigned char convbuf[COPY_BUF_SIZE];
  int conv_bytes = -1;

  precomp_mgr.ctx->comp_decomp_state = P_CONVERT;
  precomp_mgr.enable_input_stream_otf_decompression();
  precomp_mgr.enable_output_stream_otf_compression(precomp_mgr.conversion_to_method);

  for (;;) {
    precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.copybuf), COPY_BUF_SIZE);
    bytes_read = precomp_mgr.ctx->fin->gcount();
    // truncate by 9 bytes (Precomp on-the-fly delimiter) if converting from compressed data
    if ((precomp_mgr.conversion_from_method > OTF_NONE) && (bytes_read < COPY_BUF_SIZE)) {
      bytes_read -= 9;
      if (bytes_read < 0) {
        conv_bytes += bytes_read;
        bytes_read = 0;
      }
    }
    if (conv_bytes > -1) precomp_mgr.ctx->fout->write(reinterpret_cast<char*>(convbuf), conv_bytes);
    for (int i = 0; i < bytes_read; i++) {
      convbuf[i] = precomp_mgr.copybuf[i];
    }
    conv_bytes = bytes_read;
    if (bytes_read < COPY_BUF_SIZE) {
      break;
    }

    precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->fin->tellg();
    precomp_mgr.call_progress_callback();
  }
  precomp_mgr.ctx->fout->write(reinterpret_cast<char*>(convbuf), conv_bytes);

  return RETURN_SUCCESS;
}

int convert_file(Precomp& precomp_mgr)
{
  return wrap_with_exception_catch([&]() { return convert_file_impl(precomp_mgr); });
}

long long try_to_decompress_bzip2(Precomp& precomp_mgr, WrappedIStream& file, int compression_level, long long& compressed_stream_size, PrecompTmpFile& tmpfile) {
  long long r, decompressed_stream_size;

  precomp_mgr.call_progress_callback();

  remove(tmpfile.file_path.c_str());
  WrappedFStream ftempout;
  ftempout.open(tmpfile.file_path, std::ios_base::out | std::ios_base::binary);

  force_seekg(file, &file == precomp_mgr.ctx->fin.get() ? precomp_mgr.ctx->input_file_pos : 0, std::ios_base::beg);

  r = inf_bzip2(precomp_mgr, file, ftempout, compressed_stream_size, decompressed_stream_size);
  ftempout.close();
  if (r == BZ_OK) return decompressed_stream_size;

  return r;
}

void try_recompress_bzip2(Precomp& precomp_mgr, WrappedIStream& origfile, int level, long long& compressed_stream_size, PrecompTmpFile& tmpfile) {
            precomp_mgr.call_progress_callback();

            long long decomp_bytes_total;
            precomp_mgr.ctx->identical_bytes = file_recompress_bzip2(precomp_mgr, origfile, level, precomp_mgr.ctx->identical_bytes_decomp, decomp_bytes_total, tmpfile);
            if (precomp_mgr.ctx->identical_bytes > -1) { // successfully recompressed?
              if ((precomp_mgr.ctx->identical_bytes > precomp_mgr.ctx->best_identical_bytes)  || ((precomp_mgr.ctx->identical_bytes == precomp_mgr.ctx->best_identical_bytes) && (precomp_mgr.ctx->penalty_bytes_len < precomp_mgr.ctx->best_penalty_bytes_len))) {
                if (precomp_mgr.ctx->identical_bytes > precomp_mgr.switches.min_ident_size) {
                  if (DEBUG_MODE) {
                  std::cout << "Identical recompressed bytes: " << precomp_mgr.ctx->identical_bytes << " of " << compressed_stream_size << std::endl;
                  std::cout << "Identical decompressed bytes: " << precomp_mgr.ctx->identical_bytes_decomp << " of " << decomp_bytes_total << std::endl;
                  }
                }

				// Partial matches sometimes need all the decompressed bytes, but there are much less 
				// identical recompressed bytes - in these cases, all the decompressed bytes have to
				// be stored together with the remaining recompressed bytes, so the result won't compress
				// better than the original stream. What's important here is the ratio between recompressed ratio
				// and decompressed ratio that shouldn't get too high.
				// Example: A stream has 5 of 1000 identical recompressed bytes, but needs 1000 of 1000 decompressed bytes,
				// so the ratio is (1000/1000)/(5/1000) = 200 which is too high. With 5 of 1000 decompressed bytes or
				// 1000 of 1000 identical recompressed bytes, ratio would've been 1 and we'd accept it.

				float partial_ratio = ((float)precomp_mgr.ctx->identical_bytes_decomp / decomp_bytes_total) / ((float)precomp_mgr.ctx->identical_bytes / compressed_stream_size);
				if (partial_ratio < 3.0f) {
                    precomp_mgr.ctx->best_identical_bytes_decomp = precomp_mgr.ctx->identical_bytes_decomp;
                    precomp_mgr.ctx->best_identical_bytes = precomp_mgr.ctx->identical_bytes;
					if (precomp_mgr.ctx->penalty_bytes_len > 0) {
						memcpy(precomp_mgr.ctx->best_penalty_bytes.data(), precomp_mgr.ctx->penalty_bytes.data(), precomp_mgr.ctx->penalty_bytes_len);
            precomp_mgr.ctx->best_penalty_bytes_len = precomp_mgr.ctx->penalty_bytes_len;
					}
					else {
            precomp_mgr.ctx->best_penalty_bytes_len = 0;
					}
				} else {
					if (DEBUG_MODE) {
					print_to_console("Not enough identical recompressed bytes\n");
					}
				}
    }
  }
}


void write_header(Precomp& precomp_mgr) {
  // write the PCF file header, beware that this needs to be done before wrapping the output file with a CompressedOStreamBuffer
  char* input_file_name_without_path = new char[precomp_mgr.input_file_name.length() + 1];

  ostream_printf(*precomp_mgr.ctx->fout, "PCF");

  // version number
  precomp_mgr.ctx->fout->put(V_MAJOR);
  precomp_mgr.ctx->fout->put(V_MINOR);
  precomp_mgr.ctx->fout->put(V_MINOR2);

  // compression-on-the-fly method used
  precomp_mgr.ctx->fout->put(precomp_mgr.ctx->compression_otf_method);

  // write input file name without path
  const char* last_backslash = strrchr(precomp_mgr.input_file_name.c_str(), PATH_DELIM);
  if (last_backslash != NULL) {
    strcpy(input_file_name_without_path, last_backslash + 1);
  } else {
    strcpy(input_file_name_without_path, precomp_mgr.input_file_name.c_str());
  }

  ostream_printf(*precomp_mgr.ctx->fout, input_file_name_without_path);
  precomp_mgr.ctx->fout->put(0);

  delete[] input_file_name_without_path;
}

void read_header(Precomp& precomp_mgr) {
  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 3);
  if ((precomp_mgr.in[0] == 'P') && (precomp_mgr.in[1] == 'C') && (precomp_mgr.in[2] == 'F')) {
  } else {
    throw PrecompError(ERR_NO_PCF_HEADER);
  }

  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 3);
  if ((precomp_mgr.in[0] == V_MAJOR) && (precomp_mgr.in[1] == V_MINOR) && (precomp_mgr.in[2] == V_MINOR2)) {
  } else {
    throw PrecompError(
      ERR_PCF_HEADER_INCOMPATIBLE_VERSION,
      make_cstyle_format_string("PCF version info: %i.%i.%i\n", precomp_mgr.in[0], precomp_mgr.in[1], precomp_mgr.in[2])
    );
  }

  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 1);
  precomp_mgr.ctx->compression_otf_method = precomp_mgr.in[0];

  std::string header_filename;
  char c;
  do {
    c = precomp_mgr.ctx->fin->get();
    if (c != 0) header_filename += c;
  } while (c != 0);

  if (precomp_mgr.output_file_name.empty()) {
    precomp_mgr.output_file_name = header_filename;
  }
}

void convert_header(Precomp& precomp_mgr) {
  force_seekg(*precomp_mgr.ctx->fin, 0, std::ios_base::beg);

  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 3);
  if ((precomp_mgr.in[0] == 'P') && (precomp_mgr.in[1] == 'C') && (precomp_mgr.in[2] == 'F')) {
  } else {
    throw std::runtime_error(make_cstyle_format_string("Input stream has no valid PCF header\n"));
  }
  precomp_mgr.ctx->fout->write(reinterpret_cast<char*>(precomp_mgr.in), 3);

  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 3);
  if ((precomp_mgr.in[0] == V_MAJOR) && (precomp_mgr.in[1] == V_MINOR) && (precomp_mgr.in[2] == V_MINOR2)) {
  } else {
    throw std::runtime_error(make_cstyle_format_string(
      "Input stream was made with a different Precomp version\n"
      "PCF version info: %i.%i.%i\n",
      precomp_mgr.in[0], precomp_mgr.in[1], precomp_mgr.in[2]
    ));
  }
  precomp_mgr.ctx->fout->write(reinterpret_cast<char*>(precomp_mgr.in), 3);

  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 1);
  precomp_mgr.conversion_from_method = precomp_mgr.in[0];
  if (precomp_mgr.conversion_from_method == precomp_mgr.conversion_to_method) {
    throw std::runtime_error(make_cstyle_format_string("Input file doesn't need to be converted\n"));
  }
  precomp_mgr.in[0] = precomp_mgr.conversion_to_method;
  precomp_mgr.ctx->fout->write(reinterpret_cast<char*>(precomp_mgr.in), 1);

  std::string header_filename = "";
  char c;
  do {
    c = precomp_mgr.ctx->fin->get();
    if (c != 0) header_filename += c;
  } while (c != 0);
  ostream_printf(*precomp_mgr.ctx->fout, header_filename);
  precomp_mgr.ctx->fout->put(0);
}

void fast_copy(Precomp& precomp_mgr, WrappedIStream& file1, WrappedOStream& file2, long long bytecount, bool update_progress) {
  if (bytecount == 0) return;

  long long i;
  int remaining_bytes = (bytecount % COPY_BUF_SIZE);
  long long maxi = (bytecount / COPY_BUF_SIZE);

  for (i = 1; i <= maxi; i++) {
    file1.read(reinterpret_cast<char*>(precomp_mgr.copybuf), COPY_BUF_SIZE);
    file2.write(reinterpret_cast<char*>(precomp_mgr.copybuf), COPY_BUF_SIZE);

    if (((i - 1) % FAST_COPY_WORK_SIGN_DIST) == 0) {
      precomp_mgr.call_progress_callback();
    }
  }
  if (remaining_bytes != 0) {
    file1.read(reinterpret_cast<char*>(precomp_mgr.copybuf), remaining_bytes);
    file2.write(reinterpret_cast<char*>(precomp_mgr.copybuf), remaining_bytes);
  }

  if ((update_progress) && (!DEBUG_MODE)) precomp_mgr.ctx->uncompressed_bytes_written += bytecount;
}

long long compare_files_penalty(Precomp& precomp_mgr, RecursionContext& context, WrappedIStream& file1, WrappedIStream& file2, long long pos1, long long pos2) {
  unsigned char input_bytes1[COMP_CHUNK];
  unsigned char input_bytes2[COMP_CHUNK];
  long long same_byte_count = 0;
  long long same_byte_count_penalty = 0;
  long long rek_same_byte_count = 0;
  long long rek_same_byte_count_penalty = -1;
  long long size1, size2, minsize;
  long long i;
  bool endNow = false;

  unsigned int local_penalty_bytes_len = 0;

  unsigned int rek_penalty_bytes_len = 0;
  bool use_penalty_bytes = false;

  long long compare_end;
  if (&file1 == context.fin.get()) {
    force_seekg(file2, 0, std::ios_base::end);
    compare_end = file2.tellg();
  } else {
    force_seekg(file1, 0, std::ios_base::end);
    force_seekg(file2, 0, std::ios_base::end);
    compare_end = std::min(file1.tellg(), file2.tellg());
  }

  force_seekg(file1, pos1, std::ios_base::beg);
  force_seekg(file2, pos2, std::ios_base::beg);

  do {
    precomp_mgr.call_progress_callback();

    file1.read(reinterpret_cast<char*>(input_bytes1), COMP_CHUNK);
    size1 = file1.gcount();
    file2.read(reinterpret_cast<char*>(input_bytes2), COMP_CHUNK);
    size2 = file2.gcount();

    minsize = std::min(size1, size2);
    for (i = 0; i < minsize; i++) {
      if (input_bytes1[i] != input_bytes2[i]) {

        same_byte_count_penalty -= 5; // 4 bytes = position, 1 byte = new byte

        // if same_byte_count_penalty is too low, stop
        if ((long long)(same_byte_count_penalty + (compare_end - same_byte_count)) < 0) {
          endNow = true;
          break;
        }
        // stop, if local_penalty_bytes_len gets too big
        if ((local_penalty_bytes_len + 5) >= MAX_PENALTY_BYTES) {
          endNow = true;
          break;
        }

        local_penalty_bytes_len += 5;
        // position
        context.local_penalty_bytes[local_penalty_bytes_len-5] = (same_byte_count >> 24) % 256;
        context.local_penalty_bytes[local_penalty_bytes_len-4] = (same_byte_count >> 16) % 256;
        context.local_penalty_bytes[local_penalty_bytes_len-3] = (same_byte_count >> 8) % 256;
        context.local_penalty_bytes[local_penalty_bytes_len-2] = same_byte_count % 256;
        // new byte
        context.local_penalty_bytes[local_penalty_bytes_len-1] = input_bytes1[i];
      } else {
        same_byte_count_penalty++;
      }

      same_byte_count++;

      if (same_byte_count_penalty > rek_same_byte_count_penalty) {

        use_penalty_bytes = true;
        rek_penalty_bytes_len = local_penalty_bytes_len;

        rek_same_byte_count = same_byte_count;
        rek_same_byte_count_penalty = same_byte_count_penalty;
      }

    }
  } while ((minsize == COMP_CHUNK) && (!endNow));

  if ((rek_penalty_bytes_len > 0) && (use_penalty_bytes)) {
    memcpy(context.penalty_bytes.data(), context.local_penalty_bytes.data(), rek_penalty_bytes_len);
    context.penalty_bytes_len = rek_penalty_bytes_len;
  } else {
    context.penalty_bytes_len = 0;
  }

  return rek_same_byte_count;
}

void try_decompression_gzip(Precomp& precomp_mgr, int gzip_header_length, PrecompTmpFile& tmpfile) {
  try_decompression_deflate_type(precomp_mgr, precomp_mgr.statistics.decompressed_gzip_count, precomp_mgr.statistics.recompressed_gzip_count,
                                 D_GZIP, precomp_mgr.ctx->in_buf + precomp_mgr.ctx->cb + 2, gzip_header_length - 2, false,
                                 "in GZIP", tmpfile);
}

void try_decompression_png(Precomp& precomp_mgr, int windowbits, PrecompTmpFile& tmpfile) {
  init_decompression_variables(*precomp_mgr.ctx);

  // try to decompress at current position
  recompress_deflate_result rdres = try_recompression_deflate(precomp_mgr, *precomp_mgr.ctx->fin, tmpfile);

  if (rdres.uncompressed_stream_size > 0) { // seems to be a zLib-Stream

    precomp_mgr.statistics.decompressed_streams_count++;
    precomp_mgr.statistics.decompressed_png_count++;

    debug_deflate_detected(*precomp_mgr.ctx, rdres, "in PNG");

    if (rdres.accepted) {
      precomp_mgr.statistics.recompressed_streams_count++;
      precomp_mgr.statistics.recompressed_png_count++;

      precomp_mgr.ctx->non_zlib_was_used = true;

      debug_sums(rdres);

      // end uncompressed data
      precomp_mgr.ctx->compressed_data_found = true;
      end_uncompressed_data(precomp_mgr);

      debug_pos();

      // write compressed data header (PNG)
      fout_fput_deflate_hdr(*precomp_mgr.ctx->fout, D_PNG, 0, rdres, zlib_header, 2, true);

      // write reconstruction and decompressed data
      fout_fput_recon_data(*precomp_mgr.ctx->fout, rdres);
      tmpfile.reopen();
      fout_fput_uncompressed(precomp_mgr, rdres, tmpfile);

      debug_pos();
      // set input file pointer after recompressed data
      precomp_mgr.ctx->input_file_pos += rdres.compressed_stream_size - 1;
      precomp_mgr.ctx->cb += rdres.compressed_stream_size - 1;

    } else {
      if (intense_mode_is_active(precomp_mgr)) precomp_mgr.ctx->intense_ignore_offsets->insert(precomp_mgr.ctx->input_file_pos - 2);
      if (brute_mode_is_active(precomp_mgr)) precomp_mgr.ctx->brute_ignore_offsets->insert(precomp_mgr.ctx->input_file_pos);
      if (DEBUG_MODE) {
        print_to_console("No matches\n");
      }
    }
  }
}

void try_decompression_png_multi(Precomp& precomp_mgr, WrappedIStream& fpng, int windowbits, PrecompTmpFile& tmpfile) {
  init_decompression_variables(*precomp_mgr.ctx);

  // try to decompress at current position
  recompress_deflate_result rdres = try_recompression_deflate(precomp_mgr, fpng, tmpfile);

  if (rdres.uncompressed_stream_size > 0) { // seems to be a zLib-Stream

    precomp_mgr.statistics.decompressed_streams_count++;
    precomp_mgr.statistics.decompressed_png_multi_count++;

    debug_deflate_detected(*precomp_mgr.ctx, rdres, "in multiPNG");

    if (rdres.accepted) {
      precomp_mgr.statistics.recompressed_streams_count++;
      precomp_mgr.statistics.recompressed_png_multi_count++;

      precomp_mgr.ctx->non_zlib_was_used = true;

      debug_sums(rdres);

      // end uncompressed data
      precomp_mgr.ctx->compressed_data_found = true;
      end_uncompressed_data(precomp_mgr);

      debug_pos();

      // write compressed data header (PNG)
      fout_fput_deflate_hdr(*precomp_mgr.ctx->fout, D_MULTIPNG, 0, rdres, zlib_header, 2, true);

      // simulate IDAT write to get IDAT pairs count
      int i = 1;
      int idat_pos = idat_lengths[0] - 2;
      unsigned int idat_pairs_written_count = 0;
      if (idat_pos < rdres.compressed_stream_size) {
        do {
          idat_pairs_written_count++;

          idat_pos += idat_lengths[i];
          if (idat_pos >= rdres.compressed_stream_size) break;

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
      if (idat_pos < rdres.compressed_stream_size) {
        do {
          fout_fput32(*precomp_mgr.ctx->fout, idat_crcs[i]);
          fout_fput_vlint(*precomp_mgr.ctx->fout, idat_lengths[i]);

          idat_pairs_written_count++;

          idat_pos += idat_lengths[i];
          if (idat_pos >= rdres.compressed_stream_size) break;

          i++;
        } while (i < idat_count);
      }

      // write reconstruction and decompressed data
      fout_fput_recon_data(*precomp_mgr.ctx->fout, rdres);
      tmpfile.reopen();
      fout_fput_uncompressed(precomp_mgr, rdres, tmpfile);

      debug_pos();

      // set input file pointer after recompressed data
      precomp_mgr.ctx->input_file_pos += rdres.compressed_stream_size - 1;
      precomp_mgr.ctx->cb += rdres.compressed_stream_size - 1;
      // now add IDAT chunk overhead
      precomp_mgr.ctx->input_file_pos += (idat_pairs_written_count * 12);
      precomp_mgr.ctx->cb += (idat_pairs_written_count * 12);

    } else {
      if (intense_mode_is_active(precomp_mgr)) precomp_mgr.ctx->intense_ignore_offsets->insert(precomp_mgr.ctx->input_file_pos - 2);
      if (brute_mode_is_active(precomp_mgr)) precomp_mgr.ctx->brute_ignore_offsets->insert(precomp_mgr.ctx->input_file_pos);
      if (DEBUG_MODE) {
        print_to_console("No matches\n");
      }
    }
  }
}

// GIF functions

bool newgif_may_write;
WrappedOStream* frecompress_gif = NULL;
WrappedIStream* freadfunc = NULL;

int readFunc(GifFileType* GifFile, GifByteType* buf, int count)
{
  freadfunc->read(reinterpret_cast<char*>(buf), count);
  return freadfunc->gcount();
}

int writeFunc(GifFileType* GifFile, const GifByteType* buf, int count)
{
  if (newgif_may_write) {
    frecompress_gif->write(reinterpret_cast<char*>(const_cast<GifByteType*>(buf)), count);
    return frecompress_gif->bad() ? 0 : count;
  } else {
    return count;
  }
}

int DGifGetLineByte(GifFileType *GifFile, GifPixelType *Line, int LineLen, GifCodeStruct *g)
{
    GifPixelType* LineBuf = new GifPixelType[LineLen];
    memcpy(LineBuf, Line, LineLen);
    int result = DGifGetLine(GifFile, LineBuf, g, LineLen);
    memcpy(Line, LineBuf, LineLen);
    delete[] LineBuf;

    return result;
}

unsigned char** alloc_gif_screenbuf(GifFileType* myGifFile) {
  if (myGifFile->SHeight <= 0 || myGifFile->SWidth <= 0) {
    return nullptr;
  }
  unsigned char** ScreenBuff = new unsigned char*[myGifFile->SHeight];
  for (int i = 0; i < myGifFile->SHeight; i++) {
    ScreenBuff[i] = new unsigned char[myGifFile->SWidth];
  }

  for (int i = 0; i < myGifFile->SWidth; i++)  /* Set its color to BackGround. */
    ScreenBuff[0][i] = myGifFile->SBackGroundColor;
  for (int i = 1; i < myGifFile->SHeight; i++) {
    memcpy(ScreenBuff[i], ScreenBuff[0], myGifFile->SWidth);
  }
  return ScreenBuff;
}
void free_gif_screenbuf(unsigned char** ScreenBuff, GifFileType* myGifFile) {
  if (ScreenBuff != NULL) {
    for (int i = 0; i < myGifFile->SHeight; i++) {
      delete[] ScreenBuff[i];
    }
    delete[] ScreenBuff;
  }
}

bool r_gif_result(unsigned char** ScreenBuff, GifFileType* myGifFile, GifFileType* newGifFile, bool result) {
  free_gif_screenbuf(ScreenBuff, myGifFile);
  DGifCloseFile(myGifFile);
  EGifCloseFile(newGifFile);
  return result;
}
bool r_gif_error(unsigned char** ScreenBuff, GifFileType* myGifFile, GifFileType* newGifFile) {
  return r_gif_result(ScreenBuff, myGifFile, newGifFile, false);
}
bool r_gif_ok(unsigned char** ScreenBuff, GifFileType* myGifFile, GifFileType* newGifFile) {
  return r_gif_result(ScreenBuff, myGifFile, newGifFile, true);
}

bool recompress_gif(Precomp& precomp_mgr, WrappedIStream& srcfile, WrappedOStream& dstfile, unsigned char block_size, GifCodeStruct* g, GifDiffStruct* gd) {
  int i, j;
  long long last_pos = -1;
  int Row, Col, Width, Height, ExtCode;
  long long src_pos, init_src_pos;

  GifFileType* myGifFile;
  GifFileType* newGifFile;
  GifRecordType RecordType;
  GifByteType *Extension;

  freadfunc = &srcfile;
  frecompress_gif = &dstfile;
  newgif_may_write = false;

  init_src_pos = srcfile.tellg();

  myGifFile = DGifOpenPCF(NULL, readFunc);
  if (myGifFile == NULL) {
    return false;
  }

  newGifFile = EGifOpen(NULL, writeFunc);

  newGifFile->BlockSize = block_size;

  if (newGifFile == NULL) {
    return false;
  }
  unsigned char** ScreenBuff = alloc_gif_screenbuf(myGifFile);
  if (!ScreenBuff) {
    DGifCloseFile(myGifFile);
    EGifCloseFile(newGifFile);
    return false;
  }

  EGifPutScreenDesc(newGifFile, myGifFile->SWidth, myGifFile->SHeight, myGifFile->SColorResolution, myGifFile->SBackGroundColor, myGifFile->SPixelAspectRatio, myGifFile->SColorMap);

  do {
    if (DGifGetRecordType(myGifFile, &RecordType) == GIF_ERROR) {
      return r_gif_error(ScreenBuff, myGifFile, newGifFile);
    }

    switch (RecordType) {
      case IMAGE_DESC_RECORD_TYPE:
        if (DGifGetImageDesc(myGifFile) == GIF_ERROR) {
          return r_gif_error(ScreenBuff, myGifFile, newGifFile);
        }

        src_pos = srcfile.tellg();
        if (last_pos != src_pos) {
          if (last_pos == -1) {
            force_seekg(srcfile, init_src_pos, std::ios_base::beg);
            fast_copy(precomp_mgr, srcfile, dstfile, src_pos - init_src_pos);
            force_seekg(srcfile, src_pos, std::ios_base::beg);

            long long dstfile_pos = dstfile.tellp();
            dstfile.seekp(0, std::ios_base::beg);
            // change PGF8xa to GIF8xa
            dstfile.put('G');
            dstfile.put('I');
            dstfile.seekp(dstfile_pos, std::ios_base::beg);
          } else {
            force_seekg(srcfile, last_pos, std::ios_base::beg);
            fast_copy(precomp_mgr, srcfile, dstfile, src_pos - last_pos);
            force_seekg(srcfile, src_pos, std::ios_base::beg);
          }
        }

        Row = myGifFile->Image.Top; /* Image Position relative to Screen. */
        Col = myGifFile->Image.Left;
        Width = myGifFile->Image.Width;
        Height = myGifFile->Image.Height;

        for (i = Row; i < (Row + Height); i++) {
          srcfile.read(reinterpret_cast<char*>(&ScreenBuff[i][Col]), Width);
        }

        // this does send a clear code, so we pass g and gd
        if (EGifPutImageDesc(newGifFile, g, gd, Row, Col, Width, Height, myGifFile->Image.Interlace, myGifFile->Image.ColorMap) == GIF_ERROR) {
          return r_gif_error(ScreenBuff, myGifFile, newGifFile);
        }

        newgif_may_write = true;

        if (myGifFile->Image.Interlace) {
          for (i = 0; i < 4; i++) {
            for (j = Row + InterlacedOffset[i]; j < (Row + Height); j += InterlacedJumps[i]) {
              EGifPutLine(newGifFile, &ScreenBuff[j][Col], g, gd, Width);
            }
          }
        } else {
          for (i = Row; i < (Row + Height); i++) {
            EGifPutLine(newGifFile, &ScreenBuff[i][Col], g, gd, Width);
          }
        }

        newgif_may_write = false;

        last_pos = srcfile.tellg();

        break;
      case EXTENSION_RECORD_TYPE:
        /* Skip any extension blocks in file: */

        if (DGifGetExtension(myGifFile, &ExtCode, &Extension) == GIF_ERROR) {
          return r_gif_error(ScreenBuff, myGifFile, newGifFile);
        }
        while (Extension != NULL) {
          if (DGifGetExtensionNext(myGifFile, &Extension) == GIF_ERROR) {
            return r_gif_error(ScreenBuff, myGifFile, newGifFile);
          }
        }
        break;
      case TERMINATE_RECORD_TYPE:
        break;
      default:                    /* Should be traps by DGifGetRecordType. */
        break;
    }
  } while (RecordType != TERMINATE_RECORD_TYPE);

  src_pos = srcfile.tellg();
  if (last_pos != src_pos) {
    force_seekg(srcfile, last_pos, std::ios_base::beg);
    fast_copy(precomp_mgr, srcfile, dstfile, src_pos - last_pos);
    force_seekg(srcfile, src_pos, std::ios_base::beg);
  }
  return r_gif_ok(ScreenBuff, myGifFile, newGifFile);
}

bool d_gif_result(unsigned char** ScreenBuff, GifFileType* myGifFile, bool result) {
  free_gif_screenbuf(ScreenBuff, myGifFile);
  DGifCloseFile(myGifFile);
  return result;
}
bool d_gif_error(unsigned char** ScreenBuff, GifFileType* myGifFile) {
  return d_gif_result(ScreenBuff, myGifFile, false);
}
bool d_gif_ok(unsigned char** ScreenBuff, GifFileType* myGifFile) {
  return d_gif_result(ScreenBuff, myGifFile, true);
}

bool decompress_gif(Precomp& precomp_mgr, WrappedIStream& srcfile, WrappedOStream& dstfile, long long src_pos, int& gif_length, long long& decomp_length, unsigned char& block_size, GifCodeStruct* g) {
  int i, j;
  GifFileType* myGifFile;
  int Row, Col, Width, Height, ExtCode;
  GifByteType *Extension;
  GifRecordType RecordType;

  long long srcfile_pos;
  long long last_pos = -1;

  freadfunc = &srcfile;
  myGifFile = DGifOpen(NULL, readFunc);
  if (myGifFile == NULL) {
    return false;
  }

  unsigned char** ScreenBuff = NULL;

  do {

    if (DGifGetRecordType(myGifFile, &RecordType) == GIF_ERROR) {
      DGifCloseFile(myGifFile);
      return false;
    }

    switch (RecordType) {
      case IMAGE_DESC_RECORD_TYPE:
        if (DGifGetImageDesc(myGifFile) == GIF_ERROR) {
          return d_gif_error(ScreenBuff, myGifFile);
        }

        if (ScreenBuff == NULL) {
          ScreenBuff = alloc_gif_screenbuf(myGifFile);
          if (!ScreenBuff) {
            DGifCloseFile(myGifFile);
            return false;
          }
        }

        srcfile_pos = srcfile.tellg();
        if (last_pos != srcfile_pos) {
          if (last_pos == -1) {
            force_seekg(srcfile, src_pos, std::ios_base::beg);
            fast_copy(precomp_mgr, srcfile, dstfile, srcfile_pos - src_pos);
            force_seekg(srcfile, srcfile_pos, std::ios_base::beg);

            long long dstfile_pos = dstfile.tellp();
            dstfile.seekp(0, std::ios_base::beg);
            // change GIF8xa to PGF8xa
            dstfile.put('P');
            dstfile.put('G');
            dstfile.seekp(dstfile_pos, std::ios_base::beg);
          } else {
            force_seekg(srcfile, last_pos, std::ios_base::beg);
            fast_copy(precomp_mgr, srcfile, dstfile, srcfile_pos - last_pos);
            force_seekg(srcfile, srcfile_pos, std::ios_base::beg);
          }
        }

        unsigned char c;
        c = srcfile.get();
        if (c == 254) {
          block_size = 254;
        }
        force_seekg(srcfile, srcfile_pos, std::ios_base::beg);

        Row = myGifFile->Image.Top; /* Image Position relative to Screen. */
        Col = myGifFile->Image.Left;
        Width = myGifFile->Image.Width;
        Height = myGifFile->Image.Height;

        if (((Col + Width) > myGifFile->SWidth) ||
            ((Row + Height) > myGifFile->SHeight)) {
          return d_gif_error(ScreenBuff, myGifFile);
        }

        if (myGifFile->Image.Interlace) {
          /* Need to perform 4 passes on the images: */
          for (i = 0; i < 4; i++) {
            for (j = Row + InterlacedOffset[i]; j < (Row + Height); j += InterlacedJumps[i]) {
              if (DGifGetLineByte(myGifFile, &ScreenBuff[j][Col], Width, g) == GIF_ERROR) {
                // TODO: If this fails, write as much rows to dstfile
                //       as possible to support second decompression.
                return d_gif_error(ScreenBuff, myGifFile);
              }
            }
          }
          // write to dstfile
          for (i = Row; i < (Row + Height); i++) {
            dstfile.write(reinterpret_cast<char*>(&ScreenBuff[i][Col]), Width);
          }
        } else {
          for (i = Row; i < (Row + Height); i++) {
            if (DGifGetLineByte(myGifFile, &ScreenBuff[i][Col], Width, g) == GIF_ERROR) {
              return d_gif_error(ScreenBuff, myGifFile);
            }
            // write to dstfile
            dstfile.write(reinterpret_cast<char*>(&ScreenBuff[i][Col]), Width);
          }
        }

        last_pos = srcfile.tellg();

        break;
      case EXTENSION_RECORD_TYPE:
        /* Skip any extension blocks in file: */

        if (DGifGetExtension(myGifFile, &ExtCode, &Extension) == GIF_ERROR) {
          return d_gif_error(ScreenBuff, myGifFile);
        }
        while (Extension != NULL) {
          if (DGifGetExtensionNext(myGifFile, &Extension) == GIF_ERROR) {
            return d_gif_error(ScreenBuff, myGifFile);
          }
        }
        break;
      case TERMINATE_RECORD_TYPE:
        break;
      default:                    /* Should be traps by DGifGetRecordType. */
        break;
    }
  } while (RecordType != TERMINATE_RECORD_TYPE);

  srcfile_pos = srcfile.tellg();
  if (last_pos != srcfile_pos) {
    force_seekg(srcfile, last_pos, std::ios_base::beg);
    fast_copy(precomp_mgr, srcfile, dstfile, srcfile_pos - last_pos);
    force_seekg(srcfile, srcfile_pos, std::ios_base::beg);
  }

  gif_length = srcfile_pos - src_pos;
  decomp_length = dstfile.tellp();

  return d_gif_ok(ScreenBuff, myGifFile);
}

void try_decompression_gif(Precomp& precomp_mgr, unsigned char version[5], PrecompTmpFile& tmpfile) {

  unsigned char block_size = 255;
  int gif_length = -1;
  long long decomp_length = -1;

  GifCodeStruct gCode;
  GifCodeInit(&gCode);
  GifDiffStruct gDiff;
  GifDiffInit(&gDiff);

  bool recompress_success_needed = true;

  if (DEBUG_MODE) {
  std::cout << "Possible GIF found at position " << precomp_mgr.ctx->input_file_pos << std::endl;;
  }

  force_seekg(*precomp_mgr.ctx->fin, precomp_mgr.ctx->input_file_pos, std::ios_base::beg);

  tmpfile.close();
  // read GIF file
  {
    WrappedFStream ftempout;
    ftempout.open(tmpfile.file_path, std::ios_base::out | std::ios_base::binary);

    if (!decompress_gif(precomp_mgr, *precomp_mgr.ctx->fin, ftempout, precomp_mgr.ctx->input_file_pos, gif_length, decomp_length, block_size, &gCode)) {
      ftempout.close();
      remove(tmpfile.file_path.c_str());
      GifDiffFree(&gDiff);
      GifCodeFree(&gCode);
      return;
    }

    if (DEBUG_MODE) {
      std::cout << "Can be decompressed to " << decomp_length << " bytes" << std::endl;
    }
  }

  precomp_mgr.statistics.decompressed_streams_count++;
  precomp_mgr.statistics.decompressed_gif_count++;

  std::string tempfile2 = tmpfile.file_path + "_rec_";
  WrappedFStream ftempout;
  ftempout.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
  PrecompTmpFile frecomp;
  frecomp.open(tempfile2, std::ios_base::out | std::ios_base::binary);
  if (recompress_gif(precomp_mgr, ftempout, frecomp, block_size, &gCode, &gDiff)) {

    frecomp.close();
    ftempout.close();

    WrappedFStream frecomp2;
    frecomp2.open(tempfile2, std::ios_base::in | std::ios_base::binary);
    precomp_mgr.ctx->best_identical_bytes = compare_files_penalty(precomp_mgr, *precomp_mgr.ctx, *precomp_mgr.ctx->fin, frecomp2, precomp_mgr.ctx->input_file_pos, 0);
    frecomp2.close();

    if (precomp_mgr.ctx->best_identical_bytes < gif_length) {
      if (DEBUG_MODE) {
      print_to_console("Recompression failed\n");
      }
    } else {
      if (DEBUG_MODE) {
      print_to_console("Recompression successful\n");
      }
      recompress_success_needed = true;

      if (precomp_mgr.ctx->best_identical_bytes > precomp_mgr.switches.min_ident_size) {
        precomp_mgr.statistics.recompressed_streams_count++;
        precomp_mgr.statistics.recompressed_gif_count++;
        precomp_mgr.ctx->non_zlib_was_used = true;

        if (!precomp_mgr.ctx->penalty_bytes.empty()) {
          memcpy(precomp_mgr.ctx->best_penalty_bytes.data(), precomp_mgr.ctx->penalty_bytes.data(), precomp_mgr.ctx->penalty_bytes_len);
          precomp_mgr.ctx->best_penalty_bytes_len = precomp_mgr.ctx->penalty_bytes_len;
        } else {
          precomp_mgr.ctx->best_penalty_bytes_len = 0;
        }

        // end uncompressed data

        precomp_mgr.ctx->compressed_data_found = true;
        end_uncompressed_data(precomp_mgr);

        // write compressed data header (GIF)
        unsigned char add_bits = 0;
        if (precomp_mgr.ctx->best_penalty_bytes_len != 0) add_bits += 2;
        if (block_size == 254) add_bits += 4;
        if (recompress_success_needed) add_bits += 128;

        precomp_mgr.ctx->fout->put(1 + add_bits);
        precomp_mgr.ctx->fout->put(D_GIF); // GIF

        // store diff bytes
        fout_fput_vlint(*precomp_mgr.ctx->fout, gDiff.GIFDiffIndex);
        if(DEBUG_MODE) {
          if (gDiff.GIFDiffIndex > 0)
            print_to_console("Diff bytes were used: %i bytes\n", gDiff.GIFDiffIndex);
        }
        for (int dbc = 0; dbc < gDiff.GIFDiffIndex; dbc++) {
          precomp_mgr.ctx->fout->put(gDiff.GIFDiff[dbc]);
        }

        // store penalty bytes, if any
        if (precomp_mgr.ctx->best_penalty_bytes_len != 0) {
          if (DEBUG_MODE) {
            print_to_console("Penalty bytes were used: %i bytes\n", precomp_mgr.ctx->best_penalty_bytes_len);
          }

          fout_fput_vlint(*precomp_mgr.ctx->fout, precomp_mgr.ctx->best_penalty_bytes_len);

          for (int pbc = 0; pbc < precomp_mgr.ctx->best_penalty_bytes_len; pbc++) {
            precomp_mgr.ctx->fout->put(precomp_mgr.ctx->best_penalty_bytes[pbc]);
          }
        }

        fout_fput_vlint(*precomp_mgr.ctx->fout, precomp_mgr.ctx->best_identical_bytes);
        fout_fput_vlint(*precomp_mgr.ctx->fout, decomp_length);

        // write decompressed data
        write_decompressed_data(precomp_mgr, *precomp_mgr.ctx->fout, decomp_length, tmpfile.file_path.c_str());

        // start new uncompressed data

        // set input file pointer after recompressed data
        precomp_mgr.ctx->input_file_pos += gif_length - 1;
        precomp_mgr.ctx->cb += gif_length - 1;
      }
    }


  } else {

    if (DEBUG_MODE) {
    print_to_console("No matches\n");
    }

    frecomp.close();
    ftempout.close();

  }

  GifDiffFree(&gDiff);
  GifCodeFree(&gCode);

  remove(tempfile2.c_str());
  remove(tmpfile.file_path.c_str());

}

void try_recompression_gif(Precomp& precomp_mgr, unsigned char& header1, std::string& tempfile, std::string& tempfile2)
{
  unsigned char block_size = 255;

  bool penalty_bytes_stored = ((header1 & 2) == 2);
  if ((header1 & 4) == 4) block_size = 254;
  bool recompress_success_needed = ((header1 & 128) == 128);

  GifDiffStruct gDiff;

  // read diff bytes
  gDiff.GIFDiffIndex = fin_fget_vlint(*precomp_mgr.ctx->fin);
  gDiff.GIFDiff = (unsigned char*)malloc(gDiff.GIFDiffIndex * sizeof(unsigned char));
  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(gDiff.GIFDiff), gDiff.GIFDiffIndex);
  if (DEBUG_MODE) {
    print_to_console("Diff bytes were used: %i bytes\n", gDiff.GIFDiffIndex);
  }
  gDiff.GIFDiffSize = gDiff.GIFDiffIndex;
  gDiff.GIFDiffIndex = 0;
  gDiff.GIFCodeCount = 0;

  // read penalty bytes
  if (penalty_bytes_stored) {
    precomp_mgr.ctx->penalty_bytes_len = fin_fget_vlint(*precomp_mgr.ctx->fin);
    precomp_mgr.ctx->fin->read(precomp_mgr.ctx->penalty_bytes.data(), precomp_mgr.ctx->penalty_bytes_len);
  }

  long long recompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);
  long long decompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);

  if (DEBUG_MODE) {
    std::cout << "Recompressed length: " << recompressed_data_length << " - decompressed length: " << decompressed_data_length << std::endl;
  }

  tempfile += "gif";
  tempfile2 += "gif";
  remove(tempfile.c_str());
  {
    WrappedFStream ftempout;
    ftempout.open(tempfile, std::ios_base::out | std::ios_base::binary);
    fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, ftempout, decompressed_data_length);
    ftempout.close();
  }

  bool recompress_success = false;

  {
    remove(tempfile2.c_str());
    WrappedFStream frecomp;
    frecomp.open(tempfile2, std::ios_base::out | std::ios_base::binary);

    // recompress data
    WrappedFStream ftempout;
    ftempout.open(tempfile, std::ios_base::in | std::ios_base::binary);
    recompress_success = recompress_gif(precomp_mgr, ftempout, frecomp, block_size, NULL, &gDiff);
    frecomp.close();
    ftempout.close();
  }

  if (recompress_success_needed) {
    if (!recompress_success) {
      GifDiffFree(&gDiff);
      throw PrecompError(ERR_DURING_RECOMPRESSION);
    }
  }

  long long old_fout_pos = precomp_mgr.ctx->fout->tellp();

  {
    PrecompTmpFile frecomp;
    frecomp.open(tempfile2, std::ios_base::in | std::ios_base::binary);
    fast_copy(precomp_mgr, frecomp, *precomp_mgr.ctx->fout, recompressed_data_length);
  }

  remove(tempfile2.c_str());
  remove(tempfile.c_str());

  if (penalty_bytes_stored) {
    precomp_mgr.ctx->fout->flush();

    long long fsave_fout_pos = precomp_mgr.ctx->fout->tellp();

    int pb_pos = 0;
    for (int pbc = 0; pbc < precomp_mgr.ctx->penalty_bytes_len; pbc += 5) {
      pb_pos = ((unsigned char)precomp_mgr.ctx->penalty_bytes[pbc]) << 24;
      pb_pos += ((unsigned char)precomp_mgr.ctx->penalty_bytes[pbc + 1]) << 16;
      pb_pos += ((unsigned char)precomp_mgr.ctx->penalty_bytes[pbc + 2]) << 8;
      pb_pos += (unsigned char)precomp_mgr.ctx->penalty_bytes[pbc + 3];

      precomp_mgr.ctx->fout->seekp(old_fout_pos + pb_pos, std::ios_base::beg);
      precomp_mgr.ctx->fout->write(precomp_mgr.ctx->penalty_bytes.data() + pbc + 4, 1);
    }

    precomp_mgr.ctx->fout->seekp(fsave_fout_pos, std::ios_base::beg);
  }

  GifDiffFree(&gDiff);
}

// JPG routines
void packjpg_mp3_dll_msg() {
  print_to_console("Using packJPG for JPG recompression, packMP3 for MP3 recompression.\n");
  print_to_console("%s\n", pjglib_version_info());
  print_to_console("%s\n", pmplib_version_info());
  print_to_console("More about packJPG and packMP3 here: http://www.matthiasstirner.com\n\n");

}

void try_decompression_jpg(Precomp& precomp_mgr, long long jpg_length, bool progressive_jpg, PrecompTmpFile& tmpfile) {
  std::string decompressed_jpg_filename = tmpfile.file_path + "_";
  tmpfile.close();

        if (DEBUG_MODE) {
          if (progressive_jpg) {
            print_to_console("Possible JPG (progressive) found at position ");
          } else {
            print_to_console("Possible JPG found at position ");
          }
          std::cout << precomp_mgr.ctx->saved_input_file_pos << ", length " << jpg_length << std::endl;
          // do not recompress non-progressive JPGs when prog_only is set
          if ((!progressive_jpg) && (precomp_mgr.switches.prog_only)) {
            print_to_console("Skipping (only progressive JPGs mode set)\n");
          }
        }

        // do not recompress non-progressive JPGs when prog_only is set
        if ((!progressive_jpg) && (precomp_mgr.switches.prog_only)) return;

        bool jpg_success = false;
        bool recompress_success = false;
        bool mjpg_dht_used = false;
		bool brunsli_used = false;
		bool brotli_used = precomp_mgr.switches.use_brotli;
        char recompress_msg[256];
        unsigned char* jpg_mem_in = NULL;
        unsigned char* jpg_mem_out = NULL;
        unsigned int jpg_mem_out_size = -1;
        bool in_memory = ((jpg_length + MJPGDHT_LEN) <= JPG_MAX_MEMORY_SIZE);

        if (in_memory) { // small stream => do everything in memory
          force_seekg(*precomp_mgr.ctx->fin, precomp_mgr.ctx->input_file_pos, std::ios_base::beg);
          jpg_mem_in = new unsigned char[jpg_length + MJPGDHT_LEN];
          memiostream memstream = memiostream::make(jpg_mem_in, jpg_mem_in + jpg_length);
          fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, memstream, jpg_length);

		  bool brunsli_success = false;

		  if (precomp_mgr.switches.use_brunsli) {
			  if (DEBUG_MODE) {
				  print_to_console("Trying to compress using brunsli...\n");
			  }
			  brunsli::JPEGData jpegData;
			  if (brunsli::ReadJpeg(jpg_mem_in, jpg_length, brunsli::JPEG_READ_ALL, &jpegData)) {
				  size_t output_size = brunsli::GetMaximumBrunsliEncodedSize(jpegData);
				  jpg_mem_out = new unsigned char[output_size];
				  if (brunsli::BrunsliEncodeJpeg(jpegData, jpg_mem_out, &output_size, precomp_mgr.switches.use_brotli)) {
					  recompress_success = true;
					  brunsli_success = true;
					  brunsli_used = true;
					  jpg_mem_out_size = output_size;
				  } else {
					  if (jpg_mem_out != NULL) delete[] jpg_mem_out;
					  jpg_mem_out = NULL;
				  }
			  }
			  else {
				  if (jpegData.error == brunsli::JPEGReadError::HUFFMAN_TABLE_NOT_FOUND) {
					  if (DEBUG_MODE) print_to_console("huffman table missing, trying to use Motion JPEG DHT\n");
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
								  if (jpg_mem_out != NULL) delete[] jpg_mem_out;
								  jpg_mem_out = NULL;
							  }
						  }

						  if (!brunsli_success) {
							  // revert DHT insertion
							  memmove(jpg_mem_in + (ffda_pos - 1), jpg_mem_in + (ffda_pos - 1) + MJPGDHT_LEN, jpg_length - (ffda_pos - 1));
						  }
					  }
				  }
			  }
			  if (DEBUG_MODE && !brunsli_success) {
				  if (precomp_mgr.switches.use_packjpg_fallback) {
					  print_to_console("Brunsli compression failed, using packJPG fallback...\n");
				  } else {
					  print_to_console("Brunsli compression failed\n");
				  }
			  }
		  }

		  if ((!precomp_mgr.switches.use_brunsli || !brunsli_success) && precomp_mgr.switches.use_packjpg_fallback) {
			  pjglib_init_streams(jpg_mem_in, 1, jpg_length, jpg_mem_out, 1);
			  recompress_success = pjglib_convert_stream2mem(&jpg_mem_out, &jpg_mem_out_size, recompress_msg);
			  brunsli_used = false;
			  brotli_used = false;
		  }
        } else if (precomp_mgr.switches.use_packjpg_fallback) { // large stream => use temporary files
		  if (DEBUG_MODE) {
			print_to_console("JPG too large for brunsli, using packJPG fallback...\n");
		  }
		  // try to decompress at current position
          {
            WrappedFStream decompressed_jpg;
            decompressed_jpg.open(decompressed_jpg_filename, std::ios_base::out | std::ios_base::binary);
            force_seekg(*precomp_mgr.ctx->fin, precomp_mgr.ctx->input_file_pos, std::ios_base::beg);
            fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, decompressed_jpg, jpg_length);
            decompressed_jpg.close();
          }

          // Workaround for JPG bugs. Sometimes tempfile1 is removed, but still
          // not accessible by packJPG, so we prevent that by opening it here
          // ourselves.
          {
            remove(tmpfile.file_path.c_str());
            std::fstream fworkaround;
            fworkaround.open(tmpfile.file_path, std::ios_base::out | std::ios_base::binary);
            fworkaround.close();
          }

          recompress_success = pjglib_convert_file2file(const_cast<char*>(decompressed_jpg_filename.c_str()), const_cast<char*>(tmpfile.file_path.c_str()), recompress_msg);
		  brunsli_used = false;
		  brotli_used = false;
        }

        if ((!recompress_success) && (strncmp(recompress_msg, "huffman table missing", 21) == 0) && (precomp_mgr.switches.use_mjpeg) && (precomp_mgr.switches.use_packjpg_fallback)) {
          if (DEBUG_MODE) print_to_console("huffman table missing, trying to use Motion JPEG DHT\n");
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
              } else {
                found_ff = (jpg_mem_in[ffda_pos] == 0xFF);
              }
            } while (!found_ffda);
            if (found_ffda) {
              memmove(jpg_mem_in + (ffda_pos - 1) + MJPGDHT_LEN, jpg_mem_in + (ffda_pos - 1), jpg_length - (ffda_pos - 1));
              memcpy(jpg_mem_in + (ffda_pos - 1), MJPGDHT, MJPGDHT_LEN);

              pjglib_init_streams(jpg_mem_in, 1, jpg_length + MJPGDHT_LEN, jpg_mem_out, 1);
              recompress_success = pjglib_convert_stream2mem(&jpg_mem_out, &jpg_mem_out_size, recompress_msg);
            }
          } else {
            WrappedFStream decompressed_jpg;
            decompressed_jpg.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
            do {
              ffda_pos++;
              decompressed_jpg.read(reinterpret_cast<char*>(precomp_mgr.in), 1);
              if (decompressed_jpg.gcount() != 1) break;
              if (found_ff) {
                found_ffda = (precomp_mgr.in[0] == 0xDA);
                if (found_ffda) break;
                found_ff = false;
              } else {
                found_ff = (precomp_mgr.in[0] == 0xFF);
              }
            } while (!found_ffda);
            std::string mjpgdht_tempfile = tmpfile.file_path + "_mjpgdht";
            if (found_ffda) {
              WrappedFStream decompressed_jpg_w_MJPGDHT;
              decompressed_jpg_w_MJPGDHT.open(mjpgdht_tempfile, std::ios_base::out | std::ios_base::binary);
              force_seekg(decompressed_jpg, 0, std::ios_base::beg);
              fast_copy(precomp_mgr, decompressed_jpg, decompressed_jpg_w_MJPGDHT, ffda_pos - 1);
              // insert MJPGDHT
              decompressed_jpg_w_MJPGDHT.write(reinterpret_cast<char*>(MJPGDHT), MJPGDHT_LEN);
              force_seekg(decompressed_jpg, ffda_pos - 1, std::ios_base::beg);
              fast_copy(precomp_mgr, decompressed_jpg, decompressed_jpg_w_MJPGDHT, jpg_length - (ffda_pos - 1));
            }
            decompressed_jpg.close();
            recompress_success = pjglib_convert_file2file(const_cast<char*>(mjpgdht_tempfile.c_str()), const_cast<char*>(tmpfile.file_path.c_str()), recompress_msg);
          }

          mjpg_dht_used = recompress_success;
        }

        precomp_mgr.statistics.decompressed_streams_count++;
        if (progressive_jpg) {
          precomp_mgr.statistics.decompressed_jpg_prog_count++;
        } else {
          precomp_mgr.statistics.decompressed_jpg_count++;
        }

        if ((!recompress_success) && (precomp_mgr.switches.use_packjpg_fallback)) {
          if (DEBUG_MODE) print_to_console("packJPG error: %s\n", recompress_msg);
        }

        if (!in_memory) {
          remove(decompressed_jpg_filename.c_str());
        }

        if (recompress_success) {
          int jpg_new_length = -1;

          if (in_memory) {
            jpg_new_length = jpg_mem_out_size;
          } else {
            WrappedFStream ftempout;
            ftempout.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
            force_seekg(ftempout, 0, std::ios_base::end);
            jpg_new_length = ftempout.tellg();
            ftempout.close();
          }

          if (jpg_new_length > 0) {
            precomp_mgr.statistics.recompressed_streams_count++;
            if (progressive_jpg) {
              precomp_mgr.statistics.recompressed_jpg_prog_count++;
            } else {
              precomp_mgr.statistics.recompressed_jpg_count++;
            }
            precomp_mgr.ctx->non_zlib_was_used = true;

            precomp_mgr.ctx->best_identical_bytes = jpg_length;
            precomp_mgr.ctx->best_identical_bytes_decomp = jpg_new_length;
            jpg_success = true;
          }
        }

        if (jpg_success) {

          if (DEBUG_MODE) {
          std::cout << "Best match: " << precomp_mgr.ctx->best_identical_bytes << " bytes, recompressed to " << precomp_mgr.ctx->best_identical_bytes_decomp << " bytes" << std::endl;
          }

          // end uncompressed data

          precomp_mgr.ctx->compressed_data_found = true;
          end_uncompressed_data(precomp_mgr);

          // write compressed data header (JPG)

		  char jpg_flags = 1; // no penalty bytes
		  if (mjpg_dht_used) jpg_flags += 4; // motion JPG DHT used
		  if (brunsli_used) jpg_flags += 8;
		  if (brotli_used) jpg_flags += 16;
      precomp_mgr.ctx->fout->put(jpg_flags);
      precomp_mgr.ctx->fout->put(D_JPG); // JPG

          fout_fput_vlint(*precomp_mgr.ctx->fout, precomp_mgr.ctx->best_identical_bytes);
          fout_fput_vlint(*precomp_mgr.ctx->fout, precomp_mgr.ctx->best_identical_bytes_decomp);

          // write compressed JPG
          if (in_memory) {
            memiostream memstream = memiostream::make(jpg_mem_out, jpg_mem_out + precomp_mgr.ctx->best_identical_bytes_decomp);
            fast_copy(precomp_mgr, memstream, *precomp_mgr.ctx->fout, precomp_mgr.ctx->best_identical_bytes_decomp);
          } else {
            write_decompressed_data(precomp_mgr, *precomp_mgr.ctx->fout, precomp_mgr.ctx->best_identical_bytes_decomp, tmpfile.file_path.c_str());
          }

          // start new uncompressed data

          // set input file pointer after recompressed data
          precomp_mgr.ctx->input_file_pos += precomp_mgr.ctx->best_identical_bytes - 1;
          precomp_mgr.ctx->cb += precomp_mgr.ctx->best_identical_bytes - 1;

        } else {
          if (DEBUG_MODE) {
          print_to_console("No matches\n");
          }
        }

        if (jpg_mem_in != NULL) delete[] jpg_mem_in;
        if (jpg_mem_out != NULL) delete[] jpg_mem_out;
}

void try_decompression_mp3(Precomp& precomp_mgr, long long mp3_length, PrecompTmpFile& tmpfile) {
  std::string decompressed_mp3_filename = tmpfile.file_path + "_";
  tmpfile.close();

        if (DEBUG_MODE) {
          std::cout << "Possible MP3 found at position " << precomp_mgr.ctx->saved_input_file_pos << ", length " << mp3_length << std::endl;
        }

        bool mp3_success = false;
        bool recompress_success = false;
        char recompress_msg[256];
        unsigned char* mp3_mem_in = NULL;
        unsigned char* mp3_mem_out = NULL;
        unsigned int mp3_mem_out_size = -1;
        bool in_memory = (mp3_length <= MP3_MAX_MEMORY_SIZE);

        if (in_memory) { // small stream => do everything in memory
          force_seekg(*precomp_mgr.ctx->fin, precomp_mgr.ctx->input_file_pos, std::ios_base::beg);
          mp3_mem_in = new unsigned char[mp3_length];
          memiostream memstream = memiostream::make(mp3_mem_in, mp3_mem_in + mp3_length);
          fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, memstream, mp3_length);

          pmplib_init_streams(mp3_mem_in, 1, mp3_length, mp3_mem_out, 1);
          recompress_success = pmplib_convert_stream2mem(&mp3_mem_out, &mp3_mem_out_size, recompress_msg);
        } else { // large stream => use temporary files
          // try to decompress at current position
          {
            WrappedFStream decompressed_mp3;
            decompressed_mp3.open(decompressed_mp3_filename, std::ios_base::out | std::ios_base::binary);
            force_seekg(*precomp_mgr.ctx->fin, precomp_mgr.ctx->input_file_pos, std::ios_base::beg);
            fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, decompressed_mp3, mp3_length);
            decompressed_mp3.close();
          }
          
          // workaround for bugs, similar to packJPG
          {
            remove(tmpfile.file_path.c_str());
            std::fstream fworkaround;
            fworkaround.open(tmpfile.file_path, std::ios_base::out | std::ios_base::binary);
            fworkaround.close();
          }

          recompress_success = pmplib_convert_file2file(const_cast<char*>(decompressed_mp3_filename.c_str()), const_cast<char*>(tmpfile.file_path.c_str()), recompress_msg);
        }

        if ((!recompress_success) && (strncmp(recompress_msg, "synching failure", 16) == 0)) {
          int frame_n;
          int pos;
          if (sscanf(recompress_msg, "synching failure (frame #%i at 0x%X)", &frame_n, &pos) == 2) {
            if ((pos > 0) && (pos < mp3_length)) {
              mp3_length = pos;

              if (DEBUG_MODE) print_to_console("Too much garbage data at the end, retry with new length %i\n", pos);

              if (in_memory) {
                pmplib_init_streams(mp3_mem_in, 1, mp3_length, mp3_mem_out, 1);
                recompress_success = pmplib_convert_stream2mem(&mp3_mem_out, &mp3_mem_out_size, recompress_msg);
              } else {
                std::filesystem::resize_file(decompressed_mp3_filename, pos);

                // workaround for bugs, similar to packJPG
                {
                  remove(tmpfile.file_path.c_str());
                  std::fstream fworkaround;
                  fworkaround.open(tmpfile.file_path, std::ios_base::out | std::ios_base::binary);
                }

                recompress_success = pmplib_convert_file2file(const_cast<char*>(decompressed_mp3_filename.c_str()), const_cast<char*>(tmpfile.file_path.c_str()), recompress_msg);
              }
            }
          }
        } else if ((!recompress_success) && (strncmp(recompress_msg, "big value pairs out of bounds", 29) == 0)) {
          precomp_mgr.ctx->suppress_mp3_big_value_pairs_sum = precomp_mgr.ctx->saved_input_file_pos + mp3_length;
          if (DEBUG_MODE) {
            std::cout << "Ignoring following streams with position/length sum " << precomp_mgr.ctx->suppress_mp3_big_value_pairs_sum << " to avoid slowdown" << std::endl;
          }
        } else if ((!recompress_success) && (strncmp(recompress_msg, "non-zero padbits found", 22) == 0)) {
          precomp_mgr.ctx->suppress_mp3_non_zero_padbits_sum = precomp_mgr.ctx->saved_input_file_pos + mp3_length;
          if (DEBUG_MODE) {
            std::cout << "Ignoring following streams with position/length sum " << precomp_mgr.ctx->suppress_mp3_non_zero_padbits_sum << " to avoid slowdown" << std::endl;
          }
        } else if ((!recompress_success) && (strncmp(recompress_msg, "inconsistent use of emphasis", 28) == 0)) {
          precomp_mgr.ctx->suppress_mp3_inconsistent_emphasis_sum = precomp_mgr.ctx->saved_input_file_pos + mp3_length;
          if (DEBUG_MODE) {
            std::cout << "Ignoring following streams with position/length sum " << precomp_mgr.ctx->suppress_mp3_inconsistent_emphasis_sum << " to avoid slowdown" << std::endl;
          }
        } else if ((!recompress_success) && (strncmp(recompress_msg, "inconsistent original bit", 25) == 0)) {
          precomp_mgr.ctx->suppress_mp3_inconsistent_original_bit = precomp_mgr.ctx->saved_input_file_pos + mp3_length;
          if (DEBUG_MODE) {
            std::cout << "Ignoring following streams with position/length sum " << precomp_mgr.ctx->suppress_mp3_inconsistent_original_bit << " to avoid slowdown" << std::endl;
          }
        }

        precomp_mgr.statistics.decompressed_streams_count++;
        precomp_mgr.statistics.decompressed_mp3_count++;

        if (!recompress_success) {
          if (DEBUG_MODE) print_to_console("packMP3 error: %s\n", recompress_msg);
        }

        if (!in_memory) {
          remove(decompressed_mp3_filename.c_str());
        }

        if (recompress_success) {
          int mp3_new_length = -1;

          if (in_memory) {
            mp3_new_length = mp3_mem_out_size;
          } else {
            WrappedFStream ftempout;
            ftempout.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
            force_seekg(ftempout, 0, std::ios_base::end);
            mp3_new_length = ftempout.tellg();
            ftempout.close();
          }

          if (mp3_new_length > 0) {
            precomp_mgr.statistics.recompressed_streams_count++;
            precomp_mgr.statistics.recompressed_mp3_count++;
            precomp_mgr.ctx->non_zlib_was_used = true;

            precomp_mgr.ctx->best_identical_bytes = mp3_length;
            precomp_mgr.ctx->best_identical_bytes_decomp = mp3_new_length;
            mp3_success = true;
          }
        }

        if (mp3_success) {

          if (DEBUG_MODE) {
          std::cout << "Best match: " << precomp_mgr.ctx->best_identical_bytes << " bytes, recompressed to " << precomp_mgr.ctx->best_identical_bytes_decomp << " bytes" << std::endl;
          }

          // end uncompressed data

          precomp_mgr.ctx->compressed_data_found = true;
          end_uncompressed_data(precomp_mgr);

          // write compressed data header (MP3)

          precomp_mgr.ctx->fout->put(1); // no penalty bytes
          precomp_mgr.ctx->fout->put(D_MP3); // MP3

          fout_fput_vlint(*precomp_mgr.ctx->fout, precomp_mgr.ctx->best_identical_bytes);
          fout_fput_vlint(*precomp_mgr.ctx->fout, precomp_mgr.ctx->best_identical_bytes_decomp);

          // write compressed MP3
          if (in_memory) {
            memiostream memstream = memiostream::make(mp3_mem_out, mp3_mem_out + precomp_mgr.ctx->best_identical_bytes_decomp);
            fast_copy(precomp_mgr, memstream, *precomp_mgr.ctx->fout, precomp_mgr.ctx->best_identical_bytes_decomp);
          } else {
            write_decompressed_data(precomp_mgr, *precomp_mgr.ctx->fout, precomp_mgr.ctx->best_identical_bytes_decomp, tmpfile.file_path.c_str());
          }

          // start new uncompressed data

          // set input file pointer after recompressed data
          precomp_mgr.ctx->input_file_pos += precomp_mgr.ctx->best_identical_bytes - 1;
          precomp_mgr.ctx->cb += precomp_mgr.ctx->best_identical_bytes - 1;

        } else {
          if (DEBUG_MODE) {
          print_to_console("No matches\n");
          }
        }

        if (mp3_mem_in != NULL) delete[] mp3_mem_in;
        if (mp3_mem_out != NULL) delete[] mp3_mem_out;
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

  abitreader* side_reader = new abitreader(sideinfo, nsb);

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
      } else {
        side_reader->read(22);
      }
      side_reader->read(3);
    }
  }

  delete side_reader;

  return true;
}

/* -----------------------------------------------
  calculate frame crc
  ----------------------------------------------- */
inline unsigned short mp3_calc_layer3_crc(unsigned char header2, unsigned char header3, unsigned char* sideinfo, int sidesize)
{
  // crc has a start value of 0xFFFF
  unsigned short crc = 0xFFFF;

  // process two last bytes from header...
  crc = (crc << 8) ^ crc_table[(crc>>8) ^ header2];
  crc = (crc << 8) ^ crc_table[(crc>>8) ^ header3];
  // ... and all the bytes from the side information
  for ( int i = 0; i < sidesize; i++ )
    crc = (crc << 8) ^ crc_table[(crc>>8) ^ sideinfo[i]];

  return crc;
}

void try_decompression_zlib(Precomp& precomp_mgr, int windowbits, PrecompTmpFile& tmpfile) {
  try_decompression_deflate_type(precomp_mgr, precomp_mgr.statistics.decompressed_zlib_count, precomp_mgr.statistics.recompressed_zlib_count,
                                 D_RAW, precomp_mgr.ctx->in_buf + precomp_mgr.ctx->cb, 2, true,
                                 "(intense mode)", tmpfile);
}

void try_decompression_brute(Precomp& precomp_mgr, PrecompTmpFile& tmpfile) {
  try_decompression_deflate_type(precomp_mgr, precomp_mgr.statistics.decompressed_brute_count, precomp_mgr.statistics.recompressed_brute_count,
                                 D_BRUTE, precomp_mgr.ctx->in_buf + precomp_mgr.ctx->cb, 0, false,
                                 "(brute mode)", tmpfile);
}

void try_decompression_swf(Precomp& precomp_mgr, int windowbits, PrecompTmpFile& tmpfile) {
  try_decompression_deflate_type(precomp_mgr, precomp_mgr.statistics.decompressed_swf_count, precomp_mgr.statistics.recompressed_swf_count,
                                 D_SWF, precomp_mgr.ctx->in_buf + precomp_mgr.ctx->cb + 3, 7, true,
                                 "in SWF", tmpfile);
}

void try_decompression_bzip2(Precomp& precomp_mgr, int compression_level, PrecompTmpFile& tmpfile) {
  init_decompression_variables(*precomp_mgr.ctx);

        // try to decompress at current position
        long long compressed_stream_size = -1;
        precomp_mgr.ctx->retval = try_to_decompress_bzip2(precomp_mgr, *precomp_mgr.ctx->fin, compression_level, compressed_stream_size, tmpfile);

        if (precomp_mgr.ctx->retval > 0) { // seems to be a zLib-Stream

          precomp_mgr.statistics.decompressed_streams_count++;
          precomp_mgr.statistics.decompressed_bzip2_count++;

          if (DEBUG_MODE) {
          std::cout << "Possible bZip2-Stream found at position " << precomp_mgr.ctx->saved_input_file_pos << ", compression level = " << compression_level << std::endl;
          std::cout << "Compressed size: " << compressed_stream_size << std::endl;

          WrappedFStream ftempout;
          ftempout.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
          force_seekg(ftempout, 0, std::ios_base::end);
          std::cout << "Can be decompressed to " << ftempout.tellg() << " bytes" << std::endl;
          ftempout.close();
          }

          tmpfile.reopen();
          try_recompress_bzip2(precomp_mgr, *precomp_mgr.ctx->fin, compression_level, compressed_stream_size, tmpfile);

          if ((precomp_mgr.ctx->best_identical_bytes > precomp_mgr.switches.min_ident_size) && (precomp_mgr.ctx->best_identical_bytes < precomp_mgr.ctx->best_identical_bytes_decomp)) {
            precomp_mgr.statistics.recompressed_streams_count++;
            precomp_mgr.statistics.recompressed_bzip2_count++;

            if (DEBUG_MODE) {
            std::cout << "Best match: " << precomp_mgr.ctx->best_identical_bytes << " bytes, decompressed to " << precomp_mgr.ctx->best_identical_bytes_decomp << " bytes" << std::endl;
            }

            precomp_mgr.ctx->non_zlib_was_used = true;

            // end uncompressed data

            precomp_mgr.ctx->compressed_data_found = true;
            end_uncompressed_data(precomp_mgr);

            // check recursion
            tmpfile.reopen();
            recursion_result r = recursion_compress(precomp_mgr, precomp_mgr.ctx->best_identical_bytes, precomp_mgr.ctx->best_identical_bytes_decomp, tmpfile);

            // write compressed data header (bZip2)

            int header_byte = 1;
            if (precomp_mgr.ctx->best_penalty_bytes_len != 0) {
              header_byte += 2;
            }
            if (r.success) {
              header_byte += 128;
            }
            precomp_mgr.ctx->fout->put(header_byte);
            precomp_mgr.ctx->fout->put(D_BZIP2); // Base64
            precomp_mgr.ctx->fout->put(compression_level);

            // store penalty bytes, if any
            if (precomp_mgr.ctx->best_penalty_bytes_len != 0) {
              if (DEBUG_MODE) {
                print_to_console("Penalty bytes were used: %i bytes\n", precomp_mgr.ctx->best_penalty_bytes_len);
              }
              fout_fput_vlint(*precomp_mgr.ctx->fout, precomp_mgr.ctx->best_penalty_bytes_len);
              for (int pbc = 0; pbc < precomp_mgr.ctx->best_penalty_bytes_len; pbc++) {
                precomp_mgr.ctx->fout->put(precomp_mgr.ctx->best_penalty_bytes[pbc]);
              }
            }

            fout_fput_vlint(*precomp_mgr.ctx->fout, precomp_mgr.ctx->best_identical_bytes);
            fout_fput_vlint(*precomp_mgr.ctx->fout, precomp_mgr.ctx->best_identical_bytes_decomp);

            if (r.success) {
              fout_fput_vlint(*precomp_mgr.ctx->fout, r.file_length);
            }

            // write decompressed data
            if (r.success) {
              write_decompressed_data(precomp_mgr, *precomp_mgr.ctx->fout, r.file_length, r.file_name.c_str());
              remove(r.file_name.c_str());
            } else {
              write_decompressed_data(precomp_mgr, *precomp_mgr.ctx->fout, precomp_mgr.ctx->best_identical_bytes_decomp, tmpfile.file_path.c_str());
            }

            // start new uncompressed data

            // set input file pointer after recompressed data
            precomp_mgr.ctx->input_file_pos += precomp_mgr.ctx->best_identical_bytes - 1;
            precomp_mgr.ctx->cb += precomp_mgr.ctx->best_identical_bytes - 1;

          } else {
            if (DEBUG_MODE) {
            print_to_console("No matches\n");
            }
          }

  }

}

// Base64 alphabet
static const char b64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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

void base64_reencode(Precomp& precomp_mgr, WrappedIStream& file_in, WrappedOStream& file_out, int line_count, unsigned int* base64_line_len, long long max_in_count, long long max_byte_count) {
          int line_nr = 0;
          unsigned int act_line_len = 0;
          int avail_in;
          unsigned char a,b,c;
          int i;
          long long act_byte_count = 0;

          long long remaining_bytes = max_in_count;

          do {
            if (remaining_bytes > DIV3CHUNK) {
              file_in.read(reinterpret_cast<char*>(precomp_mgr.in), DIV3CHUNK);
              avail_in = file_in.gcount();
            } else {
              file_in.read(reinterpret_cast<char*>(precomp_mgr.in), remaining_bytes);
              avail_in = file_in.gcount();
            }
            remaining_bytes -= avail_in;

            // make sure avail_in mod 3 = 0, pad with 0 bytes
            while ((avail_in % 3) != 0) {
              precomp_mgr.in[avail_in] = 0;
              avail_in++;
            }

            for (i = 0; i < (avail_in/3); i++) {
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

void try_decompression_base64(Precomp& precomp_mgr, int base64_header_length, PrecompTmpFile& tmpfile) {
  init_decompression_variables(*precomp_mgr.ctx);
  tmpfile.close();

  // try to decode at current position
  remove(tmpfile.file_path.c_str());
  force_seekg(*precomp_mgr.ctx->fin, precomp_mgr.ctx->input_file_pos, std::ios_base::beg);

  unsigned char base64_data[CHUNK >> 2];
  unsigned int* base64_line_len = new unsigned int[65536];

  int avail_in = 0;
  int i, j, k;
  unsigned char a, b, c, d;
  int cr_count = 0;
  bool decoding_failed = false;
  bool stream_finished = false;
  k = 0;

  unsigned int line_nr = 0;
  int line_count = 0;
  unsigned int act_line_len = 0;

  {
    std::fstream ftempout;
    ftempout.open(tmpfile.file_path, std::ios_base::out | std::ios_base::binary);
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
              base64_line_len[line_nr] = act_line_len;
              line_nr++;
              if (line_nr == 65534) stream_finished = true;
              act_line_len = 0;
            }
            continue;
          }
          else {
            cr_count = 0;
          }
          stream_finished = true;
          base64_line_len[line_nr] = act_line_len;
          line_nr++;
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

    line_count = line_nr;
    // if one of the lines is longer than 255 characters -> decoding failed
    for (i = 0; i < line_count; i++) {
      if (base64_line_len[i] > 255) {
        decoding_failed = true;
        break;
      }
    }
    ftempout.close();
  }

  if (!decoding_failed) {
    int line_case = -1;
    // check line case
    if (line_count == 1) {
      line_case = 0; // one length for all lines
    }
    else {
      for (i = 1; i < (line_count - 1); i++) {
        if (base64_line_len[i] != base64_line_len[0]) {
          line_case = 2; // save complete line length list
          break;
        }
      }
      if (line_case == -1) {
        // check last line
        if (base64_line_len[line_count - 1] == base64_line_len[0]) {
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
      ftempout.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
      force_seekg(ftempout, 0, std::ios_base::end);
      precomp_mgr.ctx->identical_bytes = ftempout.tellg();
    }

    if (DEBUG_MODE) {
      std::cout << "Possible Base64-Stream (line_case " << line_case << ", line_count " << line_count << ") found at position " << precomp_mgr.ctx->saved_input_file_pos << std::endl;
      std::cout << "Can be decoded to " << precomp_mgr.ctx->identical_bytes << " bytes" << std::endl;
    }

    // try to re-encode Base64 data
    {
      WrappedFStream ftempout;
      ftempout.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
      if (!ftempout.is_open()) {
        throw PrecompError(ERR_TEMP_FILE_DISAPPEARED);
      }

      std::string frecomp_filename = tmpfile.file_path + "_rec";
      remove(frecomp_filename.c_str());
      PrecompTmpFile frecomp;
      frecomp.open(frecomp_filename, std::ios_base::in | std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
      base64_reencode(precomp_mgr, ftempout, frecomp, line_count, base64_line_len);

      ftempout.close();
      precomp_mgr.ctx->identical_bytes_decomp = compare_files(precomp_mgr, *precomp_mgr.ctx->fin, frecomp, precomp_mgr.ctx->input_file_pos, 0);
    }

    if (precomp_mgr.ctx->identical_bytes_decomp > precomp_mgr.switches.min_ident_size) {
      precomp_mgr.statistics.recompressed_streams_count++;
      precomp_mgr.statistics.recompressed_base64_count++;
      if (DEBUG_MODE) {
        std::cout << "Match: encoded to " << precomp_mgr.ctx->identical_bytes_decomp << " bytes" << std::endl;
      }

      // end uncompressed data

      precomp_mgr.ctx->compressed_data_found = true;
      end_uncompressed_data(precomp_mgr);

      // check recursion
      tmpfile.reopen();
      recursion_result r = recursion_compress(precomp_mgr, precomp_mgr.ctx->identical_bytes_decomp, precomp_mgr.ctx->identical_bytes, tmpfile);

      // write compressed data header (Base64)
      int header_byte = 1 + (line_case << 2);
      if (r.success) {
        header_byte += 128;
      }
      precomp_mgr.ctx->fout->put(header_byte);
      precomp_mgr.ctx->fout->put(D_BASE64); // Base64

      fout_fput_vlint(*precomp_mgr.ctx->fout, base64_header_length);

      // write "header", but change first char to prevent re-detection
      precomp_mgr.ctx->fout->put(precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb] - 1);
      precomp_mgr.ctx->fout->write(reinterpret_cast<char*>(precomp_mgr.ctx->in_buf + precomp_mgr.ctx->cb + 1), base64_header_length - 1);

      fout_fput_vlint(*precomp_mgr.ctx->fout, line_count);
      if (line_case == 2) {
        for (i = 0; i < line_count; i++) {
          precomp_mgr.ctx->fout->put(base64_line_len[i]);
        }
      }
      else {
        precomp_mgr.ctx->fout->put(base64_line_len[0]);
        if (line_case == 1) precomp_mgr.ctx->fout->put(base64_line_len[line_count - 1]);
      }

      delete[] base64_line_len;

      fout_fput_vlint(*precomp_mgr.ctx->fout, precomp_mgr.ctx->identical_bytes);
      fout_fput_vlint(*precomp_mgr.ctx->fout, precomp_mgr.ctx->identical_bytes_decomp);

      if (r.success) {
        fout_fput_vlint(*precomp_mgr.ctx->fout, r.file_length);
      }

      // write decompressed data
      if (r.success) {
        write_decompressed_data(precomp_mgr, *precomp_mgr.ctx->fout, r.file_length, r.file_name.c_str());
        remove(r.file_name.c_str());
      } else {
        write_decompressed_data(precomp_mgr, *precomp_mgr.ctx->fout, precomp_mgr.ctx->identical_bytes, tmpfile.file_path.c_str());
      }

      // start new uncompressed data

      // set input file pointer after recompressed data
      precomp_mgr.ctx->input_file_pos += precomp_mgr.ctx->identical_bytes_decomp - 1;
      precomp_mgr.ctx->cb += precomp_mgr.ctx->identical_bytes_decomp - 1;
    }
    else {
      if (DEBUG_MODE) {
        print_to_console("No match\n");
      }
    }

  }

}

std::fstream& tryOpen(const char* filename, std::ios_base::openmode mode) {
  std::fstream fptr;
  fptr.open(filename,mode);

  if (fptr.is_open()) return fptr;

  long long timeoutstart = get_time_ms();
  while ((!fptr.is_open()) && ((get_time_ms() - timeoutstart) <= 15000)) {
    fptr.open(filename,mode);
  }
  if (!fptr.is_open()) {
    throw std::runtime_error(make_cstyle_format_string("ERROR: Access denied for %s\n", filename));
  }
  if (DEBUG_MODE) {
    print_to_console("Access problem for %s\n", filename);
    print_to_console("Time for getting access: %li ms\n", (long)(get_time_ms() - timeoutstart));
  }
  return fptr;
}

long long fileSize64(const char* filename) {
  std::error_code ec;
  return std::filesystem::file_size(filename, ec);
}

std::string temp_files_tag() {
  // Generate a random 8digit tag for the temp files of a recursion level so they don't overwrite each other
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> dis(0, 15);

  std::stringstream ss;
  ss << std::hex;
  for (int i = 0; i < 8; i++) {
    ss << dis(gen);
  }
  return ss.str();
}

void recursion_push(Precomp& precomp_mgr) {
  precomp_mgr.recursion_contexts_stack.push_back(std::move(precomp_mgr.ctx));
  precomp_mgr.ctx = std::unique_ptr<RecursionContext>(new RecursionContext(precomp_mgr));
}

void recursion_pop(Precomp& precomp_mgr) {
  precomp_mgr.ctx = std::move(precomp_mgr.recursion_contexts_stack.back());
  precomp_mgr.recursion_contexts_stack.pop_back();
}

recursion_result recursion_compress(Precomp& precomp_mgr, long long compressed_bytes, long long decompressed_bytes, PrecompTmpFile& tmpfile, bool deflate_type, bool in_memory) {
  recursion_result tmp_r;
  tmp_r.success = false;

  float recursion_min_percent = ((precomp_mgr.ctx->input_file_pos + precomp_mgr.ctx->uncompressed_bytes_written) / ((float)precomp_mgr.ctx->fin_length + precomp_mgr.ctx->uncompressed_bytes_total)) * (precomp_mgr.ctx->global_max_percent - precomp_mgr.ctx->global_min_percent) + precomp_mgr.ctx->global_min_percent;
  float recursion_max_percent = ((precomp_mgr.ctx->input_file_pos + precomp_mgr.ctx->uncompressed_bytes_written +(compressed_bytes - 1)) / ((float)precomp_mgr.ctx->fin_length + precomp_mgr.ctx->uncompressed_bytes_total)) * (precomp_mgr.ctx->global_max_percent - precomp_mgr.ctx->global_min_percent) + precomp_mgr.ctx->global_min_percent;

  bool rescue_anything_was_used = false;
  bool rescue_non_zlib_was_used = false;

  if ((precomp_mgr.recursion_depth + 1) > precomp_mgr.max_recursion_depth) {
    precomp_mgr.max_recursion_depth_reached = true;
    return tmp_r;
  }

  if (deflate_type && in_memory) {
    auto decomp_io_buf_ptr = precomp_mgr.ctx->decomp_io_buf.data();
    memiostream memstream = memiostream::make(decomp_io_buf_ptr, decomp_io_buf_ptr + decompressed_bytes);
    fast_copy(precomp_mgr, memstream, tmpfile, decompressed_bytes);
  }
  tmpfile.close();

  recursion_push(precomp_mgr);

  if (!deflate_type) {
    // shorten tempfile1 to decompressed_bytes
    std::filesystem::resize_file(tmpfile.file_path, decompressed_bytes);
  }

  precomp_mgr.ctx->fin_length = fileSize64(tmpfile.file_path.c_str());
  auto fin = new std::ifstream();
  fin->open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
  if (!fin->is_open()) {
    throw std::runtime_error(make_cstyle_format_string("ERROR: Recursion input file \"%s\" doesn't exist\n", tmpfile.file_path.c_str()));
  }
  precomp_mgr.ctx->set_input_stream(fin);

  tmp_r.file_name = tmpfile.file_path;
  tmp_r.file_name += '_';
  auto fout = new std::ofstream();
  fout->open(tmp_r.file_name.c_str(), std::ios_base::out | std::ios_base::binary);
  precomp_mgr.ctx->set_output_stream(fout, true);

  precomp_mgr.ctx->intense_ignore_offsets = new std::set<long long>();
  precomp_mgr.ctx->brute_ignore_offsets = new std::set<long long>();

  // init MP3 suppression
  for (int i = 0; i < 16; i++) {
    precomp_mgr.ctx->suppress_mp3_type_until[i] = -1;
  }
  precomp_mgr.ctx->suppress_mp3_big_value_pairs_sum = -1;
  precomp_mgr.ctx->suppress_mp3_non_zero_padbits_sum = -1;
  precomp_mgr.ctx->suppress_mp3_inconsistent_emphasis_sum = -1;
  precomp_mgr.ctx->suppress_mp3_inconsistent_original_bit = -1;

  precomp_mgr.ctx->mp3_parsing_cache_second_frame = -1;

  // disable compression-on-the-fly in recursion - we don't want compressed compressed streams
  precomp_mgr.ctx->compression_otf_method = OTF_NONE;

  precomp_mgr.recursion_depth++;
  if (DEBUG_MODE) {
    print_to_console("Recursion start - new recursion depth %i\n", precomp_mgr.recursion_depth);
  }
  const auto ret_code = compress_file(precomp_mgr, recursion_min_percent, recursion_max_percent);
  if (ret_code != RETURN_SUCCESS && ret_code != RETURN_NOTHING_DECOMPRESSED) throw PrecompError(ret_code);
  tmp_r.success = ret_code == RETURN_SUCCESS;

  delete precomp_mgr.ctx->intense_ignore_offsets;
  delete precomp_mgr.ctx->brute_ignore_offsets;
  // TODO CHECK: Delete ctx?

  if (precomp_mgr.ctx->anything_was_used)
    rescue_anything_was_used = true;

  if (precomp_mgr.ctx->non_zlib_was_used)
    rescue_non_zlib_was_used = true;

  precomp_mgr.recursion_depth--;
  recursion_pop(precomp_mgr);

  if (rescue_anything_was_used)
    precomp_mgr.ctx->anything_was_used = true;

  if (rescue_non_zlib_was_used)
    precomp_mgr.ctx->non_zlib_was_used = true;

  if (DEBUG_MODE) {
    if (tmp_r.success) {
      print_to_console("Recursion streams found\n");
    } else {
      print_to_console("No recursion streams found\n");
    }
    print_to_console("Recursion end - back to recursion depth %i\n", precomp_mgr.recursion_depth);
  }

  if (!tmp_r.success) {
    remove(tmp_r.file_name.c_str());
    tmp_r.file_name = "";
  } else {
    if ((precomp_mgr.recursion_depth + 1) > precomp_mgr.max_recursion_depth_used)
      precomp_mgr.max_recursion_depth_used = (precomp_mgr.recursion_depth + 1);
    // get recursion file size
    tmp_r.file_length = fileSize64(tmp_r.file_name.c_str());
  }

  return tmp_r;
}
recursion_result recursion_write_file_and_compress(Precomp& precomp_mgr, const recompress_deflate_result& rdres, PrecompTmpFile& tmpfile) {
  recursion_result r = recursion_compress(precomp_mgr, rdres.compressed_stream_size, rdres.uncompressed_stream_size, tmpfile, true, rdres.uncompressed_in_memory);
  return r;
}

recursion_result recursion_decompress(Precomp& precomp_mgr, long long recursion_data_length, PrecompTmpFile& tmpfile) {
  recursion_result tmp_r;

  fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, tmpfile, recursion_data_length);
  tmpfile.close();

  recursion_push(precomp_mgr);

  precomp_mgr.ctx->fin_length = fileSize64(tmpfile.file_path.c_str());
  auto fin = new std::ifstream();
  fin->open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
  if (!fin->is_open()) {
    throw std::runtime_error(make_cstyle_format_string("ERROR: Recursion input file \"%s\" doesn't exist\n", tmpfile.file_path.c_str()));
  }
  precomp_mgr.ctx->set_input_stream(fin);

  tmp_r.file_name = tmpfile.file_path;
  tmp_r.file_name += '_';
  auto fout = new std::ofstream();
  fout->open(tmp_r.file_name.c_str(), std::ios_base::out | std::ios_base::binary);
  precomp_mgr.ctx->set_output_stream(fout, true);

  // disable compression-on-the-fly in recursion - we don't want compressed compressed streams
  precomp_mgr.ctx->compression_otf_method = OTF_NONE;

  precomp_mgr.recursion_depth++;
  if (DEBUG_MODE) {
    print_to_console("Recursion start - new recursion depth %i\n", precomp_mgr.recursion_depth);
  }
  const auto ret_code = decompress_file(precomp_mgr);
  if (ret_code != RETURN_SUCCESS) throw PrecompError(ret_code);

  // TODO CHECK: Delete ctx?

  precomp_mgr.recursion_depth--;
  recursion_pop(precomp_mgr);

  if (DEBUG_MODE) {
    print_to_console("Recursion end - back to recursion depth %i\n", precomp_mgr.recursion_depth);
  }

  // get recursion file size
  tmp_r.file_length = fileSize64(tmp_r.file_name.c_str());

  tmp_r.frecurse->open(tmp_r.file_name, std::ios_base::in | std::ios_base::binary);

  return tmp_r;
}

void fout_fput32_little_endian(WrappedOStream& output, int v) {
  output.put(v % 256);
  output.put((v >> 8) % 256);
  output.put((v >> 16) % 256);
  output.put((v >> 24) % 256);
}

void fout_fput32(WrappedOStream& output, int v) {
  output.put((v >> 24) % 256);
  output.put((v >> 16) % 256);
  output.put((v >> 8) % 256);
  output.put(v % 256);
}

void fout_fput32(WrappedOStream& output, unsigned int v) {
  output.put((v >> 24) % 256);
  output.put((v >> 16) % 256);
  output.put((v >> 8) % 256);
  output.put(v % 256);
}

void fout_fput_vlint(WrappedOStream& output, unsigned long long v) {
  while (v >= 128) {
    output.put((v & 127) + 128);
    v = (v >> 7) - 1;
  }
  output.put(v);
}
void fout_fput_deflate_hdr(WrappedOStream& output, const unsigned char type, const unsigned char flags,
                           const recompress_deflate_result& rdres,
                           const unsigned char* hdr, const unsigned hdr_length,
                           const bool inc_last_hdr_byte) {
  output.put(1 + (rdres.zlib_perfect ? rdres.zlib_comp_level << 2 : 2) + flags);
  output.put(type); // PDF/PNG/...
  if (rdres.zlib_perfect) {
    output.put(((rdres.zlib_window_bits - 8) << 4) + rdres.zlib_mem_level);
  }
  fout_fput_vlint(output, hdr_length);
  if (!inc_last_hdr_byte) {
    output.write(reinterpret_cast<char*>(const_cast<unsigned char*>(hdr)), hdr_length);
  } else {
    output.write(reinterpret_cast<char*>(const_cast<unsigned char*>(hdr)), hdr_length - 1);
    output.put(hdr[hdr_length - 1] + 1);
  }
}
void fin_fget_deflate_hdr(WrappedIStream& input, WrappedOStream& output, recompress_deflate_result& rdres, const unsigned char flags,
                          unsigned char* hdr_data, unsigned& hdr_length, 
                          const bool inc_last_hdr_byte) {
  rdres.zlib_perfect = (flags & 2) == 0;
  if (rdres.zlib_perfect) {
    unsigned char zlib_params = input.get();
    rdres.zlib_comp_level  = (flags & 0x3c) >> 2;
    rdres.zlib_mem_level   = zlib_params & 0x0f;
    rdres.zlib_window_bits = ((zlib_params >> 4) & 0x7) + 8;
  }
  hdr_length = fin_fget_vlint(input);
  if (!inc_last_hdr_byte) {
    input.read(reinterpret_cast<char*>(hdr_data), hdr_length);
  } else {
    input.read(reinterpret_cast<char*>(hdr_data), hdr_length - 1);
    hdr_data[hdr_length - 1] = input.get() - 1;
  }
  output.write(reinterpret_cast<char*>(hdr_data), hdr_length);
}
void fout_fput_recon_data(WrappedOStream& output, const recompress_deflate_result& rdres) {
  if (!rdres.zlib_perfect) {
    fout_fput_vlint(output, rdres.recon_data.size());
    output.write(reinterpret_cast<char*>(const_cast<unsigned char*>(rdres.recon_data.data())), rdres.recon_data.size());
  }

  fout_fput_vlint(output, rdres.compressed_stream_size);
  fout_fput_vlint(output, rdres.uncompressed_stream_size);
}
void fin_fget_recon_data(WrappedIStream& input, recompress_deflate_result& rdres) {
  if (!rdres.zlib_perfect) {
    size_t sz = fin_fget_vlint(input);
    rdres.recon_data.resize(sz);
    input.read(reinterpret_cast<char*>(rdres.recon_data.data()), rdres.recon_data.size());
  }

  rdres.compressed_stream_size = fin_fget_vlint(input);
  rdres.uncompressed_stream_size = fin_fget_vlint(input);
}
void fout_fput_uncompressed(Precomp& precomp_mgr, const recompress_deflate_result& rdres, PrecompTmpFile& tmpfile) {
    write_decompressed_data_io_buf(precomp_mgr, rdres.uncompressed_stream_size, rdres.uncompressed_in_memory, tmpfile.file_path.c_str());
}
void fin_fget_uncompressed(const recompress_deflate_result&) {
}
void fout_fput_deflate_rec(Precomp& precomp_mgr, const unsigned char type,
                           const recompress_deflate_result& rdres,
                           const unsigned char* hdr, const unsigned hdr_length, const bool inc_last,
                           const recursion_result& recres, PrecompTmpFile& tmpfile) {
  fout_fput_deflate_hdr(*precomp_mgr.ctx->fout, type, recres.success ? 128 : 0, rdres, hdr, hdr_length, inc_last);
  fout_fput_recon_data(*precomp_mgr.ctx->fout, rdres);
  
  // write decompressed data
  if (recres.success) {
    fout_fput_vlint(*precomp_mgr.ctx->fout, recres.file_length);
    write_decompressed_data(precomp_mgr, *precomp_mgr.ctx->fout, recres.file_length, recres.file_name.c_str());
    remove(recres.file_name.c_str());
  } else {
    fout_fput_uncompressed(precomp_mgr, rdres, tmpfile);
  }
}
bool fin_fget_deflate_rec(Precomp& precomp_mgr, recompress_deflate_result& rdres, const unsigned char flags,
                          unsigned char* hdr, unsigned& hdr_length, const bool inc_last,
                          int64_t& recursion_length, PrecompTmpFile& tmpfile) {
  fin_fget_deflate_hdr(*precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, rdres, flags, hdr, hdr_length, inc_last);
  fin_fget_recon_data(*precomp_mgr.ctx->fin, rdres);

  debug_sums(rdres);
  
  // write decompressed data
  if (flags & 128) {
    recursion_length = fin_fget_vlint(*precomp_mgr.ctx->fin);
    recursion_result r = recursion_decompress(precomp_mgr, recursion_length, tmpfile);
    debug_pos();
    auto wrapped_istream_frecurse = WrappedIStream(r.frecurse.get(), false);
    bool result = try_reconstructing_deflate(precomp_mgr, wrapped_istream_frecurse, *precomp_mgr.ctx->fout, rdres);
    debug_pos();
    r.frecurse->close();
    remove(r.file_name.c_str());
    return result;
  } else {
    recursion_length = 0;
    debug_pos();
    bool result = try_reconstructing_deflate(precomp_mgr, *precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, rdres);
    debug_pos();
    return result;
  }
}

int32_t fin_fget32_little_endian(std::istream& input) {
  int32_t result = 0;
  result += ((long)input.get() << 0);
  result += ((long)input.get() << 8);
  result += ((long)input.get() << 16);
  result += ((long)input.get() << 24);
  return result;
}
int32_t fin_fget32(WrappedIStream& input) {
  int32_t result = 0;
  result += ((long)input.get() << 24);
  result += ((long)input.get() << 16);
  result += ((long)input.get() << 8);
  result += (long)input.get();
  return result;
}
long long fin_fget_vlint(WrappedIStream& input) {
  unsigned char c;
  long long v = 0, o = 0, s = 0;
  while ((c = input.get()) >= 128) {
    v += (((long long)(c & 127)) << s);
    s += 7;
    o = (o + 1) << 7;
  }
  return v + o + (((long long)c) << s);
}
