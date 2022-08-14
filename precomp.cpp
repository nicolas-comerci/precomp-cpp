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

#ifdef PRECOMPDLL
#define DLL __declspec(dllexport)
#endif

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

// batch error levels
constexpr auto RETURN_NOTHING_DECOMPRESSED = 2;
constexpr auto ERR_DISK_FULL = 3;
constexpr auto ERR_TEMP_FILE_DISAPPEARED = 4;
constexpr auto ERR_IGNORE_POS_TOO_BIG = 5;
constexpr auto ERR_IDENTICAL_BYTE_SIZE_TOO_BIG = 6;
constexpr auto ERR_RECURSION_DEPTH_TOO_BIG = 7;
constexpr auto ERR_ONLY_SET_RECURSION_DEPTH_ONCE = 8;
constexpr auto ERR_ONLY_SET_MIN_SIZE_ONCE = 9;
constexpr auto ERR_DONT_USE_SPACE = 10;
constexpr auto ERR_MORE_THAN_ONE_OUTPUT_FILE = 11;
constexpr auto ERR_MORE_THAN_ONE_INPUT_FILE = 12;
constexpr auto ERR_CTRL_C = 13;
constexpr auto ERR_INTENSE_MODE_LIMIT_TOO_BIG = 14;
constexpr auto ERR_BRUTE_MODE_LIMIT_TOO_BIG = 15;
constexpr auto ERR_ONLY_SET_LZMA_MEMORY_ONCE = 16;
constexpr auto ERR_ONLY_SET_LZMA_THREAD_ONCE = 17;
constexpr auto ERR_ONLY_SET_LZMA_FILTERS_ONCE = 18;

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
// I have no idea why std::filesystem is not available when importing <filesystem> even after I set CMAKE_CXX_STANDARD 17
// For now we just use the deprecated one on <experimental/filesystem>
#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING
#include <experimental/filesystem>
#ifndef STDTHREAD_IMPORTED
#define STDTHREAD_IMPORTED
#include <thread>
#endif
#include <set>
#ifdef MINGW
#ifndef _GLIBCXX_HAS_GTHREADS
#include "contrib\mingw_std_threads\mingw.thread.h"
#endif // _GLIBCXX_HAS_GTHREADS
#endif
#ifdef _MSC_VER
#include <io.h>
#define ftruncate _chsize_s
#else
#include <unistd.h>
#endif

#ifndef __unix
#include <conio.h>
#include <windows.h>
#define PATH_DELIM '\\'
#else
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#define PATH_DELIM '/'
#endif

#include "contrib/bzip2/bzlib.h"
#include "contrib/giflib/precomp_gif.h"
#include "contrib/packjpg/precomp_jpg.h"
#include "contrib/packmp3/precomp_mp3.h"
#include "contrib/zlib/zlib.h"
#include "contrib/preflate/preflate.h"
#include "contrib/brunsli/c/include/brunsli/brunsli_encode.h"
#include "contrib/brunsli/c/include/brunsli/brunsli_decode.h"
#include "contrib/brunsli/c/include/brunsli/jpeg_data_reader.h"
#include "contrib/brunsli/c/include/brunsli/jpeg_data_writer.h"

constexpr auto CHUNK = 262144; // 256 KB buffersize
constexpr auto DIV3CHUNK = 262143; // DIV3CHUNK is a bit smaller/larger than CHUNK, so that DIV3CHUNK mod 3 = 0
constexpr auto CHECKBUF_SIZE = 4096;
constexpr auto COPY_BUF_SIZE = 512;
constexpr auto FAST_COPY_WORK_SIGN_DIST = 64; // update work sign after (FAST_COPY_WORK_SIGN_DIST * COPY_BUF_SIZE) bytes
constexpr auto COMP_CHUNK = 512;
constexpr auto IN_BUF_SIZE = 65536; //input buffer
constexpr auto PENALTY_BYTES_TOLERANCE = 160;
constexpr auto IDENTICAL_COMPRESSED_BYTES_TOLERANCE = 32;

constexpr auto MAX_IO_BUFFER_SIZE = 64 * 1024 * 1024;

unsigned char copybuf[COPY_BUF_SIZE];

unsigned char in[CHUNK];
unsigned char out[CHUNK];

#include "precomp.h"

static char work_signs[5] = "|/-\\";
int work_sign_var = 0;
long long work_sign_start_time = get_time_ms();

// recursion
int recursion_depth = 0;
int max_recursion_depth = 10;
int max_recursion_depth_used = 0;
bool max_recursion_depth_reached = false;
Precomp g_precomp;
void recursion_push();
void recursion_pop();

// compression-on-the-fly
unsigned char otf_in[CHUNK];
unsigned char otf_out[CHUNK];

#include "contrib/liblzma/precomp_xz.h"
lzma_stream otf_xz_stream_c = LZMA_STREAM_INIT, otf_xz_stream_d = LZMA_STREAM_INIT;
lzma_init_mt_extra_parameters otf_xz_extra_params;
int otf_xz_filter_used_count = 0;

int conversion_from_method;
int conversion_to_method;
bz_stream otf_bz2_stream_c, otf_bz2_stream_d;

long long start_time;
char lzma_progress_text[70];
int old_lzma_progress_text_length = -1;
int lzma_mib_total = 0, lzma_mib_written = 0;

zLibMTF MTF;

// preflate config
size_t preflate_meta_block_size = 1 << 21; // 2 MB blocks by default
bool preflate_verify = false;

int min_ident_size_intense_brute_mode = 64;

unsigned char zlib_header[2];
unsigned int* idat_lengths = NULL;
unsigned int* idat_crcs = NULL;
int idat_count;

int intense_mode_depth_limit = -1;
int brute_mode_depth_limit = -1;

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

int ostream_printf(std::ostream& out, std::string str) {
  for (char character : str) {
    out.put(character);
    if (out.bad()) return 0;
  }
  return str.length();
}

// istreams don't allow seeking once eof/failbit is set, which happens if we read a file to the end.
// This behaves more like std::fseek by just clearing the eof and failbit to allow the seek operation to happen.
void force_seekg(std::istream& stream, long long offset, int origin) {
  if (stream.bad()) {
    printf("Input stream went bad");
    exit(1);
  }
  stream.clear();
  stream.seekg(offset, origin);
}

// Precomp DLL things
#ifdef PRECOMPDLL
// get copyright message
// msg = Buffer for error messages (256 bytes buffer size are enough)
DLL void get_copyright_msg(char* msg) {
  if (V_MINOR2 == 0) {
    sprintf(msg, "Precomp DLL v%i.%i (c) 2006-2021 by Christian Schneider",V_MAJOR,V_MINOR);
  } else {
    sprintf(msg, "Precomp DLL v%i.%i.%i (c) 2006-2021 by Christian Schneider",V_MAJOR,V_MINOR,V_MINOR2);
  }
}

void setSwitches(Switches switches) {
  g_precomp.switches = switches;
  g_precomp.ctx->compression_otf_method = switches.compression_method;
  if (switches.level_switch_used) {

    for (int i = 0; i < 81; i++) {
      if (switches.use_zlib_level[i]) {
        g_precomp.obsolete.comp_mem_level_count[i] = 0;
      } else {
        g_precomp.obsolete.comp_mem_level_count[i] = -1;
      }
    }
  }
}

// precompress a file
// in_file = input filename
// out_file = output filename
// msg = Buffer for error messages (256 bytes buffer size are enough)
DLL bool precompress_file(char* in_file, char* out_file, char* msg, Switches switches) {

  // init compression and memory level count
  for (int i = 0; i < 81; i++) {
    g_precomp.obsolete.comp_mem_level_count[i] = 0;
    g_precomp.obsolete.zlib_level_was_used[i] = false;
  }

  g_precomp.ctx->fin_length = fileSize64(in_file);

  g_precomp.ctx->fin.open(in_file, std::ios_base::in | std::ios_base::binary);
  if (!g_precomp.ctx->fin.is_open()) {
    sprintf(msg, "ERROR: Input file \"%s\" doesn't exist", in_file);

    return false;
  }

  g_precomp.ctx->fout.open(out_file, std::ios_base::out | std::ios_base::binary);
  if (!g_precomp.ctx->fout.is_open()) {
    sprintf(msg, "ERROR: Can't create output file \"%s\"", out_file);
    return false;
  }

  setSwitches(switches);

  g_precomp.ctx->input_file_name = in_file;
  g_precomp.ctx->output_file_name = out_file;

  start_time = get_time_ms();

  compress_file();

  return true;
}

// recompress a file
// in_file = input filename
// out_file = output filename
// msg = Buffer for error messages (256 bytes buffer size are enough)
DLL bool recompress_file(char* in_file, char* out_file, char* msg, Switches switches) {

  // init compression and memory level count
  for (int i = 0; i < 81; i++) {
    g_precomp.obsolete.comp_mem_level_count[i] = 0;
    g_precomp.obsolete.zlib_level_was_used[i] = false;
  }

  g_precomp.ctx->fin_length = fileSize64(in_file);

  g_precomp.ctx->fin.open(in_file, std::ios_base::in | std::ios_base::binary);
  if (!g_precomp.ctx->fin.is_open()) {
    sprintf(msg, "ERROR: Input file \"%s\" doesn't exist", in_file);

    return false;
  }

  g_precomp.ctx->fout.open(out_file, std::ios_base::out | std::ios_base::binary);
  if (!g_precomp.ctx->fout.is_open()) {
    sprintf(msg, "ERROR: Can't create output file \"%s\"", out_file);

    return false;
  }

  setSwitches(switches);

  g_precomp.ctx->input_file_name = in_file;
  g_precomp.ctx->output_file_name = out_file;

  start_time = get_time_ms();

  decompress_file();

  return true;
}

// test if a file contains streams that can be precompressed
DLL bool file_precompressable(char* in, char* msg) {
  return false;
}

#else

int main(int argc, char* argv[])
{
  int return_errorlevel = 0;

  // register CTRL-C handler
  (void) signal(SIGINT, ctrl_c_handler);

  #ifndef COMFORT
  switch (init(argc, argv)) {
  #else
  switch (init_comfort(argc, argv)) {
  #endif

    case P_COMPRESS:
      {
        start_time = get_time_ms();
        if (!compress_file()) { // none of the streams could be decompressed
          return_errorlevel = RETURN_NOTHING_DECOMPRESSED;
        }
        break;
      }

    case P_DECOMPRESS:
      {
        start_time = get_time_ms();
        decompress_file();
        break;
      }

    case P_CONVERT:
      {
        start_time = get_time_ms();
        convert_file();
        break;
      }
  }

  #ifdef COMFORT
    wait_for_key();
  #endif

  return return_errorlevel;
}

#endif

bool parsePrefixText(const char* c, const char* ref) {
  while (*ref && tolower(*c) == *ref) {
    ++c;
    ++ref;
  }
  return *ref == 0;
}
bool parseSwitch(bool& val, const char* c, const char* ref) {
  if (!parsePrefixText(c, ref)) {
    return false;
  }
  int l = strlen(ref);
  if (c[l] == '+' && !c[l + 1]) {
    val = true;
    return true;
  } else if (c[l] == '-' && !c[l + 1]) {
    val = false;
    return true;
  }
  printf("ERROR: Only + or - for this switch (%s) allowed\n", c);
  exit(1);
  return false;
}

int parseInt(const char*& c, const char* context, int too_big_error_code = 0) {
  if (*c < '0' || *c > '9') {
    printf("ERROR: Number needed to set %s\n", context);
    exit(1);
  }
  int val = *c++ - '0';
  while (*c >= '0' && *c <= '9') {
    if (val >= INT_MAX / 10 - 1) {
      if (too_big_error_code != 0) {
        error(too_big_error_code);
      }
      printf("ERROR: Number too big for %s\n", context);
      exit(1);
    }
    val = val * 10 + *c++ - '0';
  }
  return val;
}
int parseIntUntilEnd(const char* c, const char* context, int too_big_error_code = 0) {
  for (int i = 0; c[i]; ++i) {
    if (c[i] < '0' || c[i] > '9') {
      printf("ERROR: Only numbers allowed for %s\n", context);
      exit(1);
    }
  }
  const char* x = c;
  return parseInt(x, context, too_big_error_code);
}
int64_t parseInt64(const char*& c, const char* context, int too_big_error_code = 0) {
  if (*c < '0' || *c > '9') {
    printf("ERROR: Number needed to set %s\n", context);
    exit(1);
  }
  int64_t val = *c++ - '0';
  while (*c >= '0' && *c <= '9') {
    if (val >= INT64_MAX / 10 - 1) {
      if (too_big_error_code != 0) {
        error(too_big_error_code);
      }
      printf("ERROR: Number too big for %s\n", context);
      exit(1);
    }
    val = val * 10 + *c++ - '0';
  }
  return val;
}
int64_t parseInt64UntilEnd(const char* c, const char* context, int too_big_error_code = 0) {
  for (int i = 0; c[i]; ++i) {
    if (c[i] < '0' || c[i] > '9') {
      printf("ERROR: Only numbers allowed for %s\n", context);
      exit(1);
    }
  }
  const char* x = c;
  return parseInt64(x, context, too_big_error_code);
}

#ifndef PRECOMPDLL
#ifndef COMFORT
int init(int argc, char* argv[]) {
  int i, j;
  bool appended_pcf = false;

  printf("\n");
  if (V_MINOR2 == 0) {
    printf("Precomp v%i.%i %s %s - %s version",V_MAJOR,V_MINOR,V_OS,V_BIT,V_STATE);
  } else {
    printf("Precomp v%i.%i.%i %s %s - %s version",V_MAJOR,V_MINOR,V_MINOR2,V_OS,V_BIT,V_STATE);
  }
  printf(" - %s\n",V_MSG);
  printf("Free for non-commercial use - Copyright 2006-2021 by Christian Schneider\n");
  printf("  preflate v0.3.5 support - Copyright 2018 by Dirk Steinke\n\n");

  // init compression and memory level count
  bool use_zlib_level[81];
  for (i = 0; i < 81; i++) {
    g_precomp.obsolete.comp_mem_level_count[i] = 0;
    g_precomp.obsolete.zlib_level_was_used[i] = false;
    use_zlib_level[i] = true;
  }

  // init MP3 suppression
  for (i = 0; i < 16; i++) {
    g_precomp.ctx->suppress_mp3_type_until[i] = -1;
  }
  g_precomp.ctx->suppress_mp3_big_value_pairs_sum = -1;
  g_precomp.ctx->suppress_mp3_non_zero_padbits_sum = -1;
  g_precomp.ctx->suppress_mp3_inconsistent_emphasis_sum = -1;
  g_precomp.ctx->suppress_mp3_inconsistent_original_bit = -1;
  g_precomp.ctx->mp3_parsing_cache_second_frame = -1;
  
  // init LZMA filters
  memset(&otf_xz_extra_params, 0, sizeof(otf_xz_extra_params));

  bool valid_syntax = false;
  bool input_file_given = false;
  bool output_file_given = false;
  int operation = P_COMPRESS;
  bool parse_on = true;
  bool level_switch = false;
  bool min_ident_size_set = false;
  bool recursion_depth_set = false;
  bool lzma_max_memory_set = false;
  bool lzma_thread_count_set = false;
  bool lzma_filters_set = false;
  bool long_help = false;
  bool preserve_extension = false;

  for (i = 1; (i < argc) && (parse_on); i++) {
    if (argv[i][0] == '-') { // switch
      if (input_file_given) {
        valid_syntax = false;
        parse_on = false;
        break;
      }
      switch (toupper(argv[i][1])) {
        case 0:
          {
            valid_syntax = false;
            parse_on = false;
            break;
          }
        case 'I':
          {
            if (parsePrefixText(argv[i] + 1, "intense")) { // intense mode
              g_precomp.switches.intense_mode = true;
              if (strlen(argv[i]) > 8) {
                intense_mode_depth_limit = parseIntUntilEnd(argv[i] + 8, "intense mode level limit", ERR_INTENSE_MODE_LIMIT_TOO_BIG);
              }
            } else {
              long long ignore_pos = parseInt64UntilEnd(argv[i] + 2, "ignore position", ERR_IGNORE_POS_TOO_BIG);
              g_precomp.switches.ignore_list.push_back(ignore_pos);
            }
            break;
          }
        case 'D':
          {
            if (recursion_depth_set) {
              error(ERR_ONLY_SET_RECURSION_DEPTH_ONCE);
            }
            max_recursion_depth = parseIntUntilEnd(argv[i] + 2, "maximal recursion depth", ERR_RECURSION_DEPTH_TOO_BIG);
            recursion_depth_set = true;
            break;
          }
        case 'S':
          {
            if (min_ident_size_set) {
              error(ERR_ONLY_SET_MIN_SIZE_ONCE);
            }
            g_precomp.switches.min_ident_size = parseIntUntilEnd(argv[i] + 2, "minimal identical byte size", ERR_IDENTICAL_BYTE_SIZE_TOO_BIG);
            min_ident_size_set = true;
            break;
          }
        case 'B':
          {
			if (parsePrefixText(argv[i] + 1, "brute")) { // brute mode
        g_precomp.switches.brute_mode = true;
			  if (strlen(argv[i]) > 6) {
			    brute_mode_depth_limit = parseIntUntilEnd(argv[i] + 6, "brute mode level limit", ERR_BRUTE_MODE_LIMIT_TOO_BIG);
			  }
			}
			else if (!parseSwitch(g_precomp.switches.use_brunsli, argv[i] + 1, "brunsli")
				  && !parseSwitch(g_precomp.switches.use_brotli, argv[i] + 1, "brotli")) {
			  printf("ERROR: Unknown switch \"%s\"\n", argv[i]);
			  exit(1);
			}
            break;
          }
        case 'L':
          {
            if (parsePrefixText(argv[i] + 1, "longhelp")) {
              long_help = true;
            } else if (toupper(argv[i][2]) == 'M') { // LZMA max. memory
              if (lzma_max_memory_set) {
                error(ERR_ONLY_SET_LZMA_MEMORY_ONCE);
              }
              g_precomp.switches.compression_otf_max_memory = parseIntUntilEnd(argv[i] + 3, "LZMA maximal memory");
              lzma_max_memory_set = true;
            } else if (toupper(argv[i][2]) == 'T') { // LZMA thread count
              if (lzma_thread_count_set) {
                error(ERR_ONLY_SET_LZMA_THREAD_ONCE);
              }
              g_precomp.switches.compression_otf_thread_count = parseIntUntilEnd(argv[i] + 3, "LZMA thread count");
              lzma_thread_count_set = true;
            } else if (toupper(argv[i][2]) == 'L') {
              if (toupper(argv[i][3]) == 'C') {
                otf_xz_extra_params.lc = 1 + parseIntUntilEnd(argv[i] + 4, "LZMA literal context bits");
                int lclp = (otf_xz_extra_params.lc != 0 ? otf_xz_extra_params.lc - 1 : LZMA_LC_DEFAULT)
                  + (otf_xz_extra_params.lp != 0 ? otf_xz_extra_params.lp - 1 : LZMA_LP_DEFAULT);
                if (lclp < LZMA_LCLP_MIN || lclp > LZMA_LCLP_MAX) {
                  printf("sum of LZMA lc (default %d) and lp (default %d) must be inside %d..%d\n",
                         LZMA_LC_DEFAULT, LZMA_LP_DEFAULT, LZMA_LCLP_MIN, LZMA_LCLP_MAX);
                  exit(1);
                }
              } else if (toupper(argv[i][3]) == 'P') {
                  otf_xz_extra_params.lp = 1 + parseIntUntilEnd(argv[i] + 4, "LZMA literal position bits");
                  int lclp = (otf_xz_extra_params.lc != 0 ? otf_xz_extra_params.lc - 1 : LZMA_LC_DEFAULT)
                    + (otf_xz_extra_params.lp != 0 ? otf_xz_extra_params.lp - 1 : LZMA_LP_DEFAULT);
                  if (lclp < LZMA_LCLP_MIN || lclp > LZMA_LCLP_MAX) {
                    printf("sum of LZMA lc (default %d) and lp (default %d) must be inside %d..%d\n",
                           LZMA_LC_DEFAULT, LZMA_LP_DEFAULT, LZMA_LCLP_MIN, LZMA_LCLP_MAX);
                    exit(1);
                  }
              } else {
                printf("ERROR: Unknown switch \"%s\"\n", argv[i]);
                exit(1);
              }
            } else if (toupper(argv[i][2]) == 'P') {
              if (toupper(argv[i][3]) == 'B') {
                otf_xz_extra_params.pb = 1 + parseIntUntilEnd(argv[i] + 4, "LZMA position bits");
                int pb = otf_xz_extra_params.pb != 0 ? otf_xz_extra_params.pb - 1 : LZMA_PB_DEFAULT;
                if (pb < LZMA_PB_MIN || pb > LZMA_PB_MAX) {
                  printf("LZMA pb (default %d) must be inside %d..%d\n",
                         LZMA_PB_DEFAULT, LZMA_PB_MIN, LZMA_PB_MAX);
                  exit(1);
                }
              } else {
                printf("ERROR: Unknown switch \"%s\"\n", argv[i]);
                exit(1);
              }
            } else if (toupper(argv[i][2]) == 'F') { // LZMA filters
              if (lzma_filters_set) {
                error(ERR_ONLY_SET_LZMA_FILTERS_ONCE);
              }
              switch (argv[i][3]) {
                case '+':
                  break;
                case '-':
                  if (argv[i][4] != 0) {
                    printf("ERROR: \"-lf-\" must not be followed by anything\n");
                    exit(1);
                    break;
                  }
                  break;
                default:
                  printf("ERROR: Only + or - after \"-lf\" allowed\n");
                  exit(1);
                  break;
              }

              int argindex = 4;
              while (argv[i][argindex] != 0) {
                switch (toupper(argv[i][argindex])) {
                  case 'X':
                    otf_xz_extra_params.enable_filter_x86 = true;
                    break;
                  case 'P':
                    otf_xz_extra_params.enable_filter_powerpc = true;
                    break;
                  case 'I':
                    otf_xz_extra_params.enable_filter_ia64 = true;
                    break;
                  case 'A':
                    otf_xz_extra_params.enable_filter_arm = true;
                    break;
                  case 'T':
                    otf_xz_extra_params.enable_filter_armthumb = true;
                    break;
                  case 'S':
                    otf_xz_extra_params.enable_filter_sparc = true;
                    break;
                  case 'D':
                    {
                      argindex++;
                      char nextchar = argv[i][argindex];
                      if ((nextchar < '0') || (nextchar > '9')) {
                        printf("ERROR: LZMA delta filter must be followed by a distance (%d..%d)\n",
                               LZMA_DELTA_DIST_MIN, LZMA_DELTA_DIST_MAX);
                        exit(1);
                      }
                      otf_xz_extra_params.enable_filter_delta = true;
                      while ((argv[i][argindex] > '0') && (argv[i][argindex] < '9')) {
                        otf_xz_extra_params.filter_delta_distance *= 10;
                        otf_xz_extra_params.filter_delta_distance += (argv[i][argindex] - '0');
                        argindex++;
                      }
                      if (otf_xz_extra_params.filter_delta_distance < LZMA_DELTA_DIST_MIN 
                           || otf_xz_extra_params.filter_delta_distance > LZMA_DELTA_DIST_MAX) {
                        printf("ERROR: LZMA delta filter distance must be in range %d..%d\n",
                               LZMA_DELTA_DIST_MIN, LZMA_DELTA_DIST_MAX);
                        exit(1);
                      }
                      argindex--;
                    }
                    break;
                  default:
                    printf("ERROR: Unknown LZMA filter type \"%c\"\n", argv[i][argindex]);
                    exit(1);
                    break;
                }
                otf_xz_filter_used_count++;
                if (otf_xz_filter_used_count > LZMA_FILTERS_MAX - 1) {
                  printf("ERROR: Only up to %d LZMA filters can be used at the same time\n",
                         LZMA_FILTERS_MAX - 1);
                  exit(1);
                }
                argindex++;
              }
              lzma_filters_set = true;
              break;
            } else {
              printf("ERROR: Unknown switch \"%s\"\n", argv[i]);
              exit(1);
            }
            break;
          }
        case 'P':
          {
            if (!parseSwitch(g_precomp.switches.pdf_bmp_mode, argv[i] + 1, "pdfbmp")
                && !parseSwitch(g_precomp.switches.prog_only, argv[i] + 1, "progonly")
                && !parseSwitch(preflate_verify, argv[i] + 1, "pfverify")
				&& !parseSwitch(g_precomp.switches.use_packjpg_fallback, argv[i] + 1, "packjpg")) {
              if (parsePrefixText(argv[i] + 1, "pfmeta")) {
                int mbsize = parseIntUntilEnd(argv[i] + 7, "preflate meta block size");
                if (mbsize >= INT_MAX / 1024) {
                  printf("preflate meta block size set too big\n");
                  exit(1);
                }
                preflate_meta_block_size = mbsize * 1024;
              } else {
                printf("ERROR: Unknown switch \"%s\"\n", argv[i]);
                exit(1);
              }
            }
            break;
          }
        case 'T':
          {
            bool set_to;
            switch (argv[i][2]) {
              case '+':
                g_precomp.switches.use_pdf = false;
                g_precomp.switches.use_zip = false;
                g_precomp.switches.use_gzip = false;
                g_precomp.switches.use_png = false;
                g_precomp.switches.use_gif = false;
                g_precomp.switches.use_jpg = false;
                g_precomp.switches.use_mp3 = false;
                g_precomp.switches.use_swf = false;
                g_precomp.switches.use_base64 = false;
                g_precomp.switches.use_bzip2 = false;
                set_to = true;
                break;
              case '-':
                g_precomp.switches.use_pdf = true;
                g_precomp.switches.use_zip = true;
                g_precomp.switches.use_gzip = true;
                g_precomp.switches.use_png = true;
                g_precomp.switches.use_gif = true;
                g_precomp.switches.use_jpg = true;
                g_precomp.switches.use_mp3 = true;
                g_precomp.switches.use_swf = true;
                g_precomp.switches.use_base64 = true;
                g_precomp.switches.use_bzip2 = true;
                set_to = false;
                break;
              default:
                printf("ERROR: Only + or - for type switch allowed\n");
                exit(1);
                break;
            }
            for (j = 3; j < (int)strlen(argv[i]); j++) {
              switch (toupper(argv[i][j])) {
                case 'P': // PDF
                  g_precomp.switches.use_pdf = set_to;
                  break;
                case 'Z': // ZIP
                  g_precomp.switches.use_zip = set_to;
                  break;
                case 'G': // GZip
                  g_precomp.switches.use_gzip = set_to;
                  break;
                case 'N': // PNG
                  g_precomp.switches.use_png = set_to;
                  break;
                case 'F': // GIF
                  g_precomp.switches.use_gif = set_to;
                  break;
                case 'J': // JPG
                  g_precomp.switches.use_jpg = set_to;
                  break;
                case '3': // MP3
                  g_precomp.switches.use_mp3 = set_to;
                  break;
                case 'S': // SWF
                  g_precomp.switches.use_swf = set_to;
                  break;
                case 'M': // MIME Base64
                  g_precomp.switches.use_base64 = set_to;
                  break;
                case 'B': // bZip2
                  g_precomp.switches.use_bzip2 = set_to;
                  break;
                default:
                  printf("ERROR: Invalid compression type %c\n", argv[i][j]);
                  exit(1);
                  break;
              }
            }
            break;
          }
        case 'C':
          {
            switch (toupper(argv[i][2])) {
              case 'N': // no compression
                g_precomp.ctx->compression_otf_method = OTF_NONE;
                break;
              case 'B': // bZip2
                g_precomp.ctx->compression_otf_method = OTF_BZIP2;
                break;
              case 'L': // lzma2 multithreaded
                g_precomp.ctx->compression_otf_method = OTF_XZ_MT;
                break;
              default:
                printf("ERROR: Invalid compression method %c\n", argv[i][2]);
                exit(1);
                break;
            }
            if (argv[i][3] != 0) { // Extra Parameters?
                printf("ERROR: Unknown switch \"%s\"\n", argv[i]);
                exit(1);
            }
            break;
          }
        case 'N':
          {
            switch (toupper(argv[i][2])) {
              case 'N': // no compression
                conversion_to_method = OTF_NONE;
                break;
              case 'B': // bZip2
                conversion_to_method = OTF_BZIP2;
                break;
              case 'L': // lzma2 multithreaded
                conversion_to_method = OTF_XZ_MT;
                break;
              default:
                printf("ERROR: Invalid conversion method %c\n", argv[i][2]);
                exit(1);
                break;
            }
            if (argv[i][3] != 0) { // Extra Parameters?
                printf("ERROR: Unknown switch \"%s\"\n", argv[i]);
                exit(1);
            }
            operation = P_CONVERT;
            break;
          }
        case 'V':
          {
            g_precomp.switches.DEBUG_MODE = true;
            if (argv[i][2] != 0) { // Extra Parameters?
                printf("ERROR: Unknown switch \"%s\"\n", argv[i]);
                exit(1);
            }
            break;
          }
        case 'R':
          {
            operation = P_DECOMPRESS;
            if (argv[i][2] != 0) { // Extra Parameters?
                printf("ERROR: Unknown switch \"%s\"\n", argv[i]);
                exit(1);
            }
            break;
          }
        case 'Z':
          {
           if (toupper(argv[i][2]) == 'L') {
            for (j = 0; j < 81; j++) {
              use_zlib_level[j] = false;
            }

            level_switch = true;

            for (j = 0; j < ((int)strlen(argv[i])-3); j += 3) {
              if ((j+5) < (int)strlen(argv[i])) {
                if (argv[i][j+5] != ',') {
                  printf("ERROR: zLib levels have to be separated with commas\n");
                  exit(1);
                }
              }
              if ((j+4) >= (int)strlen(argv[i])) {
                printf("ERROR: Last zLib level is incomplete\n");
                exit(1);
              }
              int comp_level_to_use = (char(argv[i][j+3]) - '1');
              int mem_level_to_use = (char(argv[i][j+4]) - '1');
              if (   ((comp_level_to_use >= 0) && (comp_level_to_use <= 8))
                  && ((mem_level_to_use >= 0) && (mem_level_to_use <= 8))) {
                use_zlib_level[comp_level_to_use + mem_level_to_use * 9] = true;
              } else {
                printf("ERROR: Invalid zlib level %c%c\n", argv[i][j+3], argv[i][j+4]);
                exit(1);
              }
            }
            break;
           } else {
             printf("ERROR: Unknown switch \"%s\"\n", argv[i]);
             exit(1);
           }
          }
        case 'O':
          {
            if (output_file_given) {
              error(ERR_MORE_THAN_ONE_OUTPUT_FILE);
            }

            if (strlen(argv[i]) == 2) {
              error(ERR_DONT_USE_SPACE);
            }

            output_file_given = true;
            g_precomp.ctx->output_file_name = argv[i] + 2;

            // check for backslash in file name
            const char* backslash_at_pos = strrchr(g_precomp.ctx->output_file_name.c_str(), PATH_DELIM);

            // dot in output file name? If not, use .pcf extension
            const char* dot_at_pos = strrchr(g_precomp.ctx->output_file_name.c_str(), '.');
            if ((dot_at_pos == NULL) || ((backslash_at_pos != NULL) && (backslash_at_pos > dot_at_pos))) {
              g_precomp.ctx->output_file_name += ".pcf";
              appended_pcf = true;
            }

            break;
          }

        case 'M':
          {
            if (!parseSwitch(g_precomp.switches.use_mjpeg, argv[i] + 1, "mjpeg")) {
              printf("ERROR: Unknown switch \"%s\"\n", argv[i]);
              exit(1);
            }
            break;
          }

        case 'F':
          {
             g_precomp.switches.fast_mode = true;
             if (argv[i][2] != 0) { // Extra Parameters?
                printf("ERROR: Unknown switch \"%s\"\n", argv[i]);
                exit(1);
            }
            break;
          }
        case 'E':
            {
                preserve_extension = true;
                if (argv[i][2] != 0) { // Extra Parameters?
                  printf("ERROR: Unknown switch \"%s\"\n", argv[i]);
                  exit(1);
                }
                break;
            }
        default:
          {
            printf("ERROR: Unknown switch \"%s\"\n", argv[i]);
            exit(1);
          }
      }
    } else { // no switch
      if (input_file_given) {
        error(ERR_MORE_THAN_ONE_INPUT_FILE);
      }

      input_file_given = true;
      g_precomp.ctx->input_file_name = argv[i];

      g_precomp.ctx->fin_length = fileSize64(argv[i]);

      g_precomp.ctx->fin.open(argv[i],std::ios_base::in | std::ios_base::binary);
      if (!g_precomp.ctx->fin.is_open()) {
        printf("ERROR: Input file \"%s\" doesn't exist\n", g_precomp.ctx->input_file_name.c_str());

        exit(1);
      }

      // output file given? If not, use input filename with .pcf extension
      if ((!output_file_given) && (operation == P_COMPRESS)) {
        if(!preserve_extension) {
          g_precomp.ctx->output_file_name = g_precomp.ctx->input_file_name;
          const char* backslash_at_pos = strrchr(g_precomp.ctx->output_file_name.c_str(), PATH_DELIM);
          const char* dot_at_pos = strrchr(g_precomp.ctx->output_file_name.c_str(), '.');
          if ((dot_at_pos == NULL) || ((backslash_at_pos != NULL) && (dot_at_pos < backslash_at_pos))) {
            g_precomp.ctx->output_file_name += ".pcf";
          } else {
            g_precomp.ctx->output_file_name = std::string(
              g_precomp.ctx->output_file_name.c_str(),
              dot_at_pos - g_precomp.ctx->output_file_name.c_str()
            );
            // same as output file because input file had .pcf extension?
            if (g_precomp.ctx->input_file_name.compare(g_precomp.ctx->output_file_name + ".pcf") == 0) {
              g_precomp.ctx->output_file_name += "_pcf.pcf";
            } else {
              g_precomp.ctx->output_file_name += ".pcf";
            }
          }
        } else {
          g_precomp.ctx->output_file_name = g_precomp.ctx->input_file_name + ".pcf";
        }
        output_file_given = true;
      } else if ((!output_file_given) && (operation == P_CONVERT)) {
        printf("ERROR: Please specify an output file for conversion\n");
        exit(1);
      }

      valid_syntax = true;
    }

  }

  if (!valid_syntax) {
    printf("Usage: precomp [-switches] input_file\n\n");
    if (long_help) {
      printf("Switches (and their <default values>):\n");
    } else {
      printf("Common switches (and their <default values>):\n");
    }
    printf("  r            \"Recompress\" PCF file (restore original file)\n");
    printf("  o[filename]  Write output to [filename] <[input_file].pcf or file in header>\n");
    printf("  e            preserve original extension of input name for output name <off>\n");
    printf("  c[lbn]       Compression method to use, l = lzma2, b = bZip2, n = none <l>\n");
    printf("  lm[amount]   Set maximal LZMA memory in MiB <%i>\n", lzma_max_memory_default());
    printf("  lt[count]    Set LZMA thread count <auto-detect: %i>\n", auto_detected_thread_count());
    if (long_help) {
      printf("  lf[+-][xpiatsd] Set LZMA filters (up to %d of them can be combined) <none>\n",
                                LZMA_FILTERS_MAX - 1);
      printf("                  lf+[xpiatsd] = enable these filters, lf- = disable all\n");
      printf("                  X = x86, P = PowerPC, I = IA-64, A = ARM, T = ARM-Thumb\n");
      printf("                  S = SPARC, D = delta (must be followed by distance %d..%d)\n",
                                LZMA_DELTA_DIST_MIN, LZMA_DELTA_DIST_MAX);
      printf("  llc[bits]    Set LZMA literal context bits <%d>\n", LZMA_LC_DEFAULT);
      printf("  llp[bits]    Set LZMA literal position bits <%d>\n", LZMA_LP_DEFAULT);
      printf("               The sum of lc and lp must be inside %d..%d\n", LZMA_LCLP_MIN, LZMA_LCLP_MAX);
      printf("  lpb[bits]    Set LZMA position bits, must be inside %d..%d <%d>\n", 
                            LZMA_PB_MIN, LZMA_PB_MAX, LZMA_PB_DEFAULT);
    } else {
      printf("  lf[+-][xpiatsd] Set LZMA filters (up to 3, see long help for details) <none>\n");
    }
    printf("  n[lbn]       Convert a PCF file to this compression (same as above) <off>\n");
    printf("  v            Verbose (debug) mode <off>\n");
    printf("  d[depth]     Set maximal recursion depth <10>\n");
    //printf("  zl[1..9][1..9] zLib levels to try for compression (comma separated) <all>\n");
    if (long_help) {
      printf("  pfmeta[amount] Split deflate streams into meta blocks of this size in KiB <2048>\n");
      printf("  pfverify       Force preflate to verify its generated reconstruction data\n");
    }
    printf("  intense      Detect raw zLib headers, too. Slower and more sensitive <off>\n");
    if (long_help) {
      printf("  brute        Brute force zLib detection. VERY Slow and most sensitive <off>\n");
    }
    printf("  t[+-][pzgnfjsmb3] Compression type switch <all enabled>\n");
    printf("              t+ = enable these types only, t- = enable all types except these\n");
    printf("              P = PDF, Z = ZIP, G = GZip, N = PNG, F = GIF, J = JPG\n");
    printf("              S = SWF, M = MIME Base64, B = bZip2, 3 = MP3\n");

    if (!long_help) {
      printf("  longhelp     Show long help\n");
    } else {
      printf("  f            Fast mode, use first found compression lvl for all streams <off>\n");
      printf("  i[pos]       Ignore stream at input file position [pos] <none>\n");
      printf("  s[size]      Set minimal identical byte size to [size] <4 (64 intense mode)>\n");
      printf("  pdfbmp[+-]   Wrap a BMP header around PDF images <off>\n");
      printf("  progonly[+-] Recompress progressive JPGs only (useful for PAQ) <off>\n");
      printf("  mjpeg[+-]    Insert huffman table for MJPEG recompression <on>\n");
	  printf("  brunsli[+-]  Prefer brunsli to packJPG for JPG streams <on>\n");
	  printf("  brotli[+-]   Use brotli to compress metadata in JPG streams <off>\n");
	  printf("  packjpg[+-]  Use packJPG for JPG streams and fallback if brunsli fails <on>\n");
	  printf("\n");
      printf("  You can use an optional number following -intense and -brute to set a\n");
      printf("  limit for how deep in recursion they should be used. E.g. -intense0 means\n");
      printf("  that intense mode will be used but not in recursion, -intense2 that only\n");
      printf("  streams up to recursion depth 2 will be treated intense (3 or higher in\n");
      printf("  this case won't). Using a sensible setting here can save you some time.\n");
    }

    exit(1);
  } else {
    if (operation == P_DECOMPRESS) {
      // if .pcf was appended, remove it
      if (appended_pcf) {
        g_precomp.ctx->output_file_name = g_precomp.ctx->output_file_name.substr(0, g_precomp.ctx->output_file_name.length() - 4);
      }
      read_header();
    }

    if (file_exists(g_precomp.ctx->output_file_name.c_str())) {
      printf("Output file \"%s\" exists. Overwrite (y/n)? ", g_precomp.ctx->output_file_name.c_str());
      char ch = get_char_with_echo();
      if ((ch != 'Y') && (ch != 'y')) {
        printf("\n");
        exit(0);
      } else {
        #ifndef __unix
        printf("\n\n");
        #else
        printf("\n");
        #endif
      }
    }
    g_precomp.ctx->fout.open(g_precomp.ctx->output_file_name.c_str(), std::ios_base::out | std::ios_base::binary);
    if (!g_precomp.ctx->fout.is_open()) {
      printf("ERROR: Can't create output file \"%s\"\n", g_precomp.ctx->output_file_name.c_str());
      exit(1);
    }

    printf("Input file: %s\n", g_precomp.ctx->input_file_name.c_str());
    printf("Output file: %s\n\n", g_precomp.ctx->output_file_name.c_str());
    if (g_precomp.switches.DEBUG_MODE) {
      if (min_ident_size_set) {
        printf("\n");
        printf("Minimal ident size set to %i bytes\n", g_precomp.switches.min_ident_size);
      }
      if (!g_precomp.switches.ignore_list.empty()) {
        printf("\n");
        printf("Ignore position list:\n");
        for (auto ignore_pos : g_precomp.switches.ignore_list) {
          std::cout << ignore_pos << std::endl;
        }
        printf("\n");
      }
    }

    if (operation == P_CONVERT) convert_header();
  }

  if (level_switch) {

    for (i = 0; i < 81; i++) {
      if (use_zlib_level[i]) {
        g_precomp.obsolete.comp_mem_level_count[i] = 0;
      } else {
        g_precomp.obsolete.comp_mem_level_count[i] = -1;
      }
    }

    g_precomp.switches.level_switch_used = true;

  }

  packjpg_mp3_dll_msg();

  return operation;
}
#else
int init_comfort(int argc, char* argv[]) {
  int i, j;
  int operation = P_COMPRESS;
  bool parse_ini_file = true;
  bool min_ident_size_set = false;
  bool recursion_depth_set = false;
  bool level_switch = false;
  bool lzma_max_memory_set = false;
  bool lzma_thread_count_set = false;
  bool lzma_filters_set = false;
  bool preserve_extension = false;

  printf("\n");
  if (V_MINOR2 == 0) {
    printf("Precomp Comfort v%i.%i %s %s - %s version",V_MAJOR,V_MINOR,V_OS,V_BIT,V_STATE);
  } else {
    printf("Precomp Comfort v%i.%i.%i %s %s - %s version",V_MAJOR,V_MINOR,V_MINOR2,V_OS,V_BIT,V_STATE);
  }
  printf(" - %s\n",V_MSG);
  printf("Free for non-commercial use - Copyright 2006-2021 by Christian Schneider\n\n");

  // init compression and memory level count
  bool use_zlib_level[81];
  for (i = 0; i < 81; i++) {
    g_precomp.obsolete.comp_mem_level_count[i] = 0;
    g_precomp.obsolete.zlib_level_was_used[i] = false;
    use_zlib_level[i] = true;
  }

  // init MP3 suppression
  for (i = 0; i < 16; i++) {
    g_precomp.ctx->suppress_mp3_type_until[i] = -1;
  }
  g_precomp.ctx->suppress_mp3_big_value_pairs_sum = -1;
  g_precomp.ctx->suppress_mp3_non_zero_padbits_sum = -1;
  g_precomp.ctx->suppress_mp3_inconsistent_emphasis_sum = -1;
  g_precomp.ctx->suppress_mp3_inconsistent_original_bit = -1;
  g_precomp.ctx->mp3_parsing_cache_second_frame = -1;

  // init LZMA filters
  memset(&otf_xz_extra_params, 0, sizeof(otf_xz_extra_params));

  // parse parameters (should be input file only)
  if (argc == 1) {
    printf("Usage:\n");
    printf("Drag and drop a file on the executable to precompress/restore it.\n");
    printf("Edit INI file for parameters.\n");
    wait_for_key();
    exit(1);
  }
  if (argc > 2) {
    error(ERR_MORE_THAN_ONE_INPUT_FILE);
  } else {
    g_precomp.ctx->input_file_name = argv[1];

    g_precomp.ctx->fin_length = fileSize64(g_precomp.ctx->input_file_name.c_str());

    g_precomp.ctx->fin.open(g_precomp.ctx->input_file_name.c_str(), std::ios_base::in | std::ios_base::binary);
    if (!g_precomp.ctx->fin.is_open()) {
      printf("ERROR: Input file \"%s\" doesn't exist\n", g_precomp.ctx->input_file_name.c_str());
      wait_for_key();
      exit(1);
    }

    if (g_precomp.ctx->fin_length > 6) {
      if (check_for_pcf_file()) {
        operation = P_DECOMPRESS;
      }
    }
  }

  // precomf.ini in EXE directory?
  char precomf_ini[1024];
#ifdef _MSC_VER
#ifdef UNICODE
#define GetModuleFileName GetModuleFileNameA
#endif // UNICODE
#endif // _MSC_VER
  GetModuleFileName(NULL, precomf_ini, 1024);
  // truncate to get directory of executable only
  char* lastslash = strrchr(precomf_ini, PATH_DELIM) + 1;
  strcpy(lastslash, "precomf.ini");
  printf("INI file: %s\n", precomf_ini);

  if (!file_exists(precomf_ini)) {
    printf("INI file not found. Create it (y/n)?");
    char ch = getche();
    printf("\n");
    if ((ch != 'Y') && (ch != 'y')) {
      wait_for_key();
      exit(1);
    } else {
      FileWrapper fnewini;
      fnewini.open(precomf_ini, std::ios_base::out);
      std::stringstream title;
      title << ";; Precomp Comfort v" << V_MAJOR << "." << V_MINOR << "." << V_MINOR2 << " " << V_OS << " " << V_BIT << " - " << V_STATE << " version - INI file\n";
      ostream_printf(fnewini, title.str());
      ostream_printf(fnewini, ";; Use a semicolon (;) for comments\n\n");
      ostream_printf(fnewini, ";; Compression method to use\n");
      ostream_printf(fnewini, ";; 0 = none, 1 = bZip2, 2 = lzma2 multi-threaded\n");
      ostream_printf(fnewini, "Compression_Method=2\n");
      ostream_printf(fnewini, ";; Maximal memory (in MiB) for LZMA compression method\n");
      ostream_printf(fnewini, "LZMA_Maximal_Memory=2048\n");
      ostream_printf(fnewini, ";; Thread count for LZMA compression method\n");
      ostream_printf(fnewini, ";; 0 = auto-detect\n");
      ostream_printf(fnewini, "LZMA_Thread_Count=0\n");
      ostream_printf(fnewini, ";; LZMA filters to use (up to 3 of the them can be combined)\n");
      ostream_printf(fnewini, ";; X = x86, P = PowerPC, I = IA-64, A = ARM, T = ARM-Thumb\n");
      ostream_printf(fnewini, ";; S = SPARC, D = delta (must be followed by distance 1..256))\n");
      ostream_printf(fnewini, ";; The default is to disable all filters\n");
      ostream_printf(fnewini, "; LZMA_Filters=XPIATSD2\n\n");
      ostream_printf(fnewini, ";; Fast mode (on/off)\n");
      ostream_printf(fnewini, "Fast_Mode=off\n\n");
      ostream_printf(fnewini, ";; Intense mode (on/off)\n");
      ostream_printf(fnewini, "Intense_Mode=off\n\n");
      ostream_printf(fnewini, ";; Brute mode (on/off)\n");
      ostream_printf(fnewini, "Brute_Mode=off\n\n");
      ostream_printf(fnewini, ";; Preserve Input's file extension (on/off)\n");
      ostream_printf(fnewini, "Preserve_Extension=off\n\n");
      ostream_printf(fnewini, ";; Wrap BMP header around PDF images (on/off)\n");
      ostream_printf(fnewini, "PDF_BMP_Mode=off\n\n");
      ostream_printf(fnewini, ";; Recompress progressive JPGs only (on/off)\n");
      ostream_printf(fnewini, "JPG_progressive_only=off\n\n");
      ostream_printf(fnewini, ";; MJPEG recompression (on/off)\n");
      ostream_printf(fnewini, "MJPEG_recompression=on\n\n");
      ostream_printf(fnewini, ";; Minimal identical byte size\n");
	  ostream_printf(fnewini,  "JPG_brunsli=on\n\n");
	  ostream_printf(fnewini,  ";; Prefer brunsli to packJPG for JPG streams (on/off)");
	  ostream_printf(fnewini,  "JPG_brotli=off\n\n");
	  ostream_printf(fnewini,  ";; Use brotli to compress metadata in JPG streams (on/off)");
	  ostream_printf(fnewini,  "JPG_packjpg=on\n\n");
	  ostream_printf(fnewini,  ";; Use packJPG for JPG streams (fallback if brunsli fails) (on/off)");
      ostream_printf(fnewini, "Minimal_Size=4\n\n");
      ostream_printf(fnewini, ";; Verbose mode (on/off)\n");
      ostream_printf(fnewini, "Verbose=off\n\n");
      ostream_printf(fnewini, ";; Compression types to use\n");
      ostream_printf(fnewini, ";; P = PDF, Z = ZIP, G = GZip, N = PNG, F = GIF, J = JPG, S = SWF\n");
      ostream_printf(fnewini, ";; M = MIME Base64, B = bZip2, 3 = MP3\n");
      ostream_printf(fnewini, "; Compression_Types_Enable=PZGNFJSMB3\n");
      ostream_printf(fnewini, "; Compression_Types_Disable=PZGNFJSMB3\n\n");
      ostream_printf(fnewini, ";; zLib levels to use\n");
      ostream_printf(fnewini, "; zLib_Levels=\n");
      ostream_printf(fnewini, ";; Maximal recursion depth to use\n");
      ostream_printf(fnewini, "Maximal_Recursion_Depth=10\n\n");
      ostream_printf(fnewini, ";; Use this to ignore streams at certain positions in the file\n");
      ostream_printf(fnewini, ";; Separate positions with commas (,) or use multiple Ignore_Positions\n");
      ostream_printf(fnewini, "; Ignore_Positions=0\n");
      g_precomp.switches.min_ident_size = 4;
      min_ident_size_set = true;
      g_precomp.switches.compression_otf_max_memory = 2048;
      lzma_max_memory_set = true;
      lzma_thread_count_set = true;
      parse_ini_file = false;
    }
  }

 if (parse_ini_file) {
  // parse INI file
  bool print_ignore_positions_message = true;
  bool compression_type_line_used = false;

  std::ifstream ini_file(precomf_ini);
  std::string line;
  std::string parName, valuestr;
  std::string::iterator it;
  char param[256], value[256];

  while (getline(ini_file, line)) {
    std::string::size_type semicolon_at_pos = line.find(";", 0);
    if (semicolon_at_pos != std::string::npos) {
      line.erase(semicolon_at_pos, line.length() - semicolon_at_pos);
    }

    // valid line must contain an equal sign (=)
    std::string::size_type equal_at_pos = line.find("=", 0);
    if (line.empty()) {
      equal_at_pos = std::string::npos;
    }
    if (equal_at_pos != std::string::npos) {

      std::stringstream ss;
      ss << line;
      // get parameter name
      getline(ss, parName, '=');
      // remove spaces
      for (it = parName.begin(); it != parName.end(); it++) {
        if (*it == ' ') {
          parName.erase(it);
          if (parName.empty()) break;
          it = parName.begin();
        } else {
          *it = tolower(*it);
        }
      }
      if (!parName.empty()) {
        it = parName.begin();
        if (*it == ' ') {
          parName.erase(it);
        } else {
          *it = tolower(*it);
        }
      }
      memset(param, '\0', 256);
      parName.copy(param, 256);

      // get value
      getline(ss, valuestr);
      // remove spaces
      for (it = valuestr.begin(); it != valuestr.end(); it++) {
        if (*it == ' ') {
          valuestr.erase(it);
          if (valuestr.empty()) break;
          it = valuestr.begin();
        } else {
          *it = tolower(*it);
        }
      }
      if (!valuestr.empty()) {
        it = valuestr.begin();
        if (*it == ' ') {
          valuestr.erase(it);
        } else {
          *it = tolower(*it);
        }
      }
      memset(value, '\0', 256);
      valuestr.copy(value, 256);

      if (strcmp(param, "") != 0) {
        bool valid_param = false;

        if (strcmp(param, "minimal_size") == 0) {
          if (min_ident_size_set) {
            error(ERR_ONLY_SET_MIN_SIZE_ONCE);
          }
          unsigned int ident_size = 0;
          unsigned int multiplicator = 1;
          for (j = (strlen(value)-1); j >= 0; j--) {
            ident_size += ((unsigned int)(value[j])-'0') * multiplicator;
            if ((multiplicator * 10) < multiplicator) {
              error(ERR_IDENTICAL_BYTE_SIZE_TOO_BIG);
            }
            multiplicator *= 10;
          }
          g_precomp.switches.min_ident_size = ident_size;
          min_ident_size_set = true;

          printf("INI: Set minimal identical byte size to %i\n", g_precomp.switches.min_ident_size);

          valid_param = true;
        }

        if (strcmp(param, "verbose") == 0) {
          if (strcmp(value, "off") == 0) {
            printf("INI: Disabled verbose mode\n");
            valid_param = true;
          }

          if (strcmp(value, "on") == 0) {
            printf("INI: Enabled verbose mode\n");
            g_precomp.switches.DEBUG_MODE = true;
            valid_param = true;
          }

          if (!valid_param) {
            printf("ERROR: Invalid verbose value: %s\n", value);
            wait_for_key();
            exit(1);
          }
        }

        if (strcmp(param, "compression_method") == 0) {
          if (strcmp(value, "0") == 0) {
            printf("INI: Using no compression method\n");
            g_precomp.ctx->compression_otf_method = OTF_NONE;
            valid_param = true;
          }

          if (strcmp(value, "1") == 0) {
            printf("INI: Using bZip2 compression method\n");
            g_precomp.ctx->compression_otf_method = OTF_BZIP2;
            valid_param = true;
          }

          if (strcmp(value, "2") == 0) {
            printf("INI: Using lzma2 multithreaded compression method\n");
            g_precomp.ctx->compression_otf_method = OTF_XZ_MT;
            valid_param = true;
          }

          if (!valid_param) {
            printf("ERROR: Invalid compression method value: %s\n", value);
            wait_for_key();
            exit(1);
          }
        }

        if (strcmp(param, "lzma_maximal_memory") == 0) {
          if (lzma_max_memory_set) {
            error(ERR_ONLY_SET_LZMA_MEMORY_ONCE);
          }
          unsigned int multiplicator = 1;
          for (j = (strlen(value)-1); j >= 0; j--) {
            g_precomp.switches.compression_otf_max_memory += ((unsigned int)(value[j])-'0') * multiplicator;
            if ((multiplicator * 10) < multiplicator) {
              exit(1);
            }
            multiplicator *= 10;
          }
          lzma_max_memory_set = true;

          if (g_precomp.switches.compression_otf_max_memory > 0) {
            printf("INI: Set LZMA maximal memory to %i MiB\n", (int)g_precomp.switches.compression_otf_max_memory);
          }

          valid_param = true;
        }

        if (strcmp(param, "lzma_thread_count") == 0) {
          if (lzma_thread_count_set) {
            error(ERR_ONLY_SET_LZMA_THREAD_ONCE);
          }
          unsigned int multiplicator = 1;
          for (j = (strlen(value)-1); j >= 0; j--) {
            g_precomp.switches.compression_otf_thread_count += ((unsigned int)(value[j])-'0') * multiplicator;
            if ((multiplicator * 10) < multiplicator) {
              exit(1);
            }
            multiplicator *= 10;
          }
          lzma_thread_count_set = true;

          if (g_precomp.switches.compression_otf_thread_count > 0) {
            printf("INI: Set LZMA thread count to %i\n", g_precomp.switches.compression_otf_thread_count);
          }

          valid_param = true;
        }
        
        if (strcmp(param, "lzma_filters") == 0) {
          if (lzma_filters_set) {
            error(ERR_ONLY_SET_LZMA_FILTERS_ONCE);
          }

          for (j = 0; j < (int)strlen(value); j++) {
            switch (toupper(value[j])) {
              case 'X':
                otf_xz_extra_params.enable_filter_x86 = true;
                break;
              case 'P':
                otf_xz_extra_params.enable_filter_powerpc = true;
                break;
              case 'I':
                otf_xz_extra_params.enable_filter_ia64 = true;
                break;
              case 'A':
                otf_xz_extra_params.enable_filter_arm = true;
                break;
              case 'T':
                otf_xz_extra_params.enable_filter_armthumb = true;
                break;
              case 'S':
                otf_xz_extra_params.enable_filter_sparc = true;
                break;
              case 'D':
                {
                  j++;
                  char nextchar = value[j];
                  if ((nextchar < '0') || (nextchar > '9')) {
                    printf("ERROR: LZMA delta filter must be followed by a distance (%d..%d)\n",
                        LZMA_DELTA_DIST_MIN, LZMA_DELTA_DIST_MAX);
                    wait_for_key();
                    exit(1);
                  }
                  otf_xz_extra_params.enable_filter_delta = true;
                  while ((value[j] > '0') && (value[j] < '9')) {
                    otf_xz_extra_params.filter_delta_distance *= 10;
                    otf_xz_extra_params.filter_delta_distance += (value[j] - '0');
                    j++;
                  }
                  if (otf_xz_extra_params.filter_delta_distance < LZMA_DELTA_DIST_MIN 
                       || otf_xz_extra_params.filter_delta_distance > LZMA_DELTA_DIST_MAX) {
                    printf("ERROR: LZMA delta filter distance must be in range %d..%d\n",
                        LZMA_DELTA_DIST_MIN, LZMA_DELTA_DIST_MAX);
                    wait_for_key();
                    exit(1);
                  }
                  j--;
                  break;
                }
              default:
                printf("ERROR: Unknown LZMA filter type \"%c\"\n", value[j]);
                wait_for_key();
                exit(1);
                break;
            }
            otf_xz_filter_used_count++;
            if (otf_xz_filter_used_count > 3) {
              printf("ERROR: Only up to 3 LZMA filters can be used at the same time\n");
              wait_for_key();
              exit(1);
            }
          }
          
          lzma_filters_set = true;
          valid_param = true;
        }

        if (strcmp(param, "fast_mode") == 0) {
          if (strcmp(value, "off") == 0) {
            printf("INI: Disabled fast mode\n");
            valid_param = true;
          }

          if (strcmp(value, "on") == 0) {
            printf("INI: Enabled fast mode\n");
            g_precomp.switches.fast_mode = true;
            valid_param = true;
          }

          if (!valid_param) {
            printf("ERROR: Invalid fast mode value: %s\n", value);
            wait_for_key();
            exit(1);
          }
        }
        // future note: params should be in lowercase for comparisons here only
        if (strcmp(param,"preserve_extension") == 0) {
            if (strcmp(value,"off") == 0) {
                printf("INI: Not preserve extension\n");
                preserve_extension = false;
            } else if (strcmp(value,"on") == 0) {
                printf("INI: Preserve extension\n");
                preserve_extension = true;
            } else {
                printf("ERROR: Invalid Preserve extension mode: %s\n",value);
                wait_for_key();
                exit(1);
            }
            valid_param = true;
        }

        if (strcmp(param, "intense_mode") == 0) {
          if (strcmp(value, "off") == 0) {
            printf("INI: Disabled intense mode\n");
            valid_param = true;
          }

          if (strcmp(value, "on") == 0) {
            printf("INI: Enabled intense mode\n");
            g_precomp.switches.intense_mode = true;
            valid_param = true;
          }

          if (!valid_param) {
            printf("ERROR: Invalid intense mode value: %s\n", value);
            wait_for_key();
            exit(1);
          }
        }

        if (strcmp(param, "brute_mode") == 0) {
          if (strcmp(value, "off") == 0) {
            printf("INI: Disabled brute mode\n");
            valid_param = true;
          }

          if (strcmp(value, "on") == 0) {
            printf("INI: Enabled brute mode\n");
            g_precomp.switches.brute_mode = true;
            valid_param = true;
          }

          if (!valid_param) {
            printf("ERROR: Invalid brute mode value: %s\n", value);
            wait_for_key();
            exit(1);
          }
        }

        if (strcmp(param, "pdf_bmp_mode") == 0) {
          if (strcmp(value, "off") == 0) {
            printf("INI: Disabled PDF BMP mode\n");
            g_precomp.switches.pdf_bmp_mode = false;
            valid_param = true;
          }

          if (strcmp(value, "on") == 0) {
            printf("INI: Enabled PDF BMP mode\n");
            g_precomp.switches.pdf_bmp_mode = true;
            valid_param = true;
          }

          if (!valid_param) {
            printf("ERROR: Invalid PDF BMP mode value: %s\n", value);
            wait_for_key();
            exit(1);
          }
        }

        if (strcmp(param, "jpg_progressive_only") == 0) {
          if (strcmp(value, "off") == 0) {
            printf("INI: Disabled progressive only JPG mode\n");
            g_precomp.switches.prog_only = false;
            valid_param = true;
          }

          if (strcmp(value, "on") == 0) {
            printf("INI: Enabled progressive only JPG mode\n");
            g_precomp.switches.prog_only = true;
            valid_param = true;
          }

          if (!valid_param) {
            printf("ERROR: Invalid progressive only JPG mode value: %s\n", value);
            wait_for_key();
            exit(1);
          }
        }

        if (strcmp(param, "mjpeg_recompression") == 0) {
          if (strcmp(value, "off") == 0) {
            printf("INI: Disabled MJPEG recompression\n");
            g_precomp.switches.use_mjpeg = false;
            valid_param = true;
          }

          if (strcmp(value, "on") == 0) {
            printf("INI: Enabled MJPEG recompression\n");
            g_precomp.switches.use_mjpeg = true;
            valid_param = true;
          }

          if (!valid_param) {
            printf("ERROR: Invalid MJPEG recompression value: %s\n", value);
            wait_for_key();
            exit(1);
          }
        }

		if (strcmp(param, "jpg_brunsli") == 0) {
			if (strcmp(value, "off") == 0) {
				printf("INI: Disabled brunsli for JPG commpression\n");
        g_precomp.switches.use_brunsli = false;
				valid_param = true;
			}

			if (strcmp(value, "on") == 0) {
				printf("INI: Enabled brunsli for JPG compression\n");
        g_precomp.switches.use_brunsli = true;
				valid_param = true;
			}

			if (!valid_param) {
				printf("ERROR: Invalid brunsli compression value: %s\n", value);
				wait_for_key();
				exit(1);
			}
		}

		if (strcmp(param, "jpg_brotli") == 0) {
			if (strcmp(value, "off") == 0) {
				printf("INI: Disabled brotli for JPG metadata compression\n");
        g_precomp.switches.use_brotli = false;
				valid_param = true;
			}

			if (strcmp(value, "on") == 0) {
				printf("INI: Enabled brotli for JPG metadata compression\n");
        g_precomp.switches.use_brotli = true;
				valid_param = true;
			}

			if (!valid_param) {
				printf("ERROR: Invalid brotli for metadata compression value: %s\n", value);
				wait_for_key();
				exit(1);
			}
		}

		if (strcmp(param, "jpg_packjpg") == 0) {
			if (strcmp(value, "off") == 0) {
				printf("INI: Disabled packJPG for JPG compression\n");
        g_precomp.switches.use_brotli = false;
				valid_param = true;
			}

			if (strcmp(value, "on") == 0) {
				printf("INI: Enabled packJPG for JPG compression\n");
        g_precomp.switches.use_brotli = true;
				valid_param = true;
			}

			if (!valid_param) {
				printf("ERROR: Invalid packJPG for JPG compression value: %s\n", value);
				wait_for_key();
				exit(1);
			}
		}

		if (strcmp(param, "compression_types_enable") == 0) {
          if (compression_type_line_used) {
            printf("ERROR: Both Compression_types_enable and Compression_types_disable used.\n");
            wait_for_key();
            exit(1);
          }
          compression_type_line_used = true;

          g_precomp.switches.use_pdf = false;
          g_precomp.switches.use_zip = false;
          g_precomp.switches.use_gzip = false;
          g_precomp.switches.use_png = false;
          g_precomp.switches.use_gif = false;
          g_precomp.switches.use_jpg = false;
          g_precomp.switches.use_mp3 = false;
          g_precomp.switches.use_swf = false;
          g_precomp.switches.use_base64 = false;
          g_precomp.switches.use_bzip2 = false;

          for (j = 0; j < (int)strlen(value); j++) {
              switch (toupper(value[j])) {
                case 'P': // PDF
                  g_precomp.switches.use_pdf = true;
                  break;
                case 'Z': // ZIP
                  g_precomp.switches.use_zip = true;
                  break;
                case 'G': // GZip
                  g_precomp.switches.use_gzip = true;
                  break;
                case 'N': // PNG
                  g_precomp.switches.use_png = true;
                  break;
                case 'F': // GIF
                  g_precomp.switches.use_gif = true;
                  break;
                case 'J': // JPG
                  g_precomp.switches.use_jpg = true;
                  break;
                case '3': // MP3
                  g_precomp.switches.use_mp3 = true;
                  break;
                case 'S': // SWF
                  g_precomp.switches.use_swf = true;
                  break;
                case 'M': // MIME Base64
                  g_precomp.switches.use_base64 = true;
                  break;
                case 'B': // bZip2
                  g_precomp.switches.use_bzip2 = true;
                  break;
                default:
                  printf("ERROR: Invalid compression type %c\n", value[j]);
                  exit(1);
                  break;
              }
          }

          if (g_precomp.switches.use_pdf) {
            printf("INI: PDF compression enabled\n");
          } else {
            printf("INI: PDF compression disabled\n");
          }

          if (g_precomp.switches.use_zip) {
            printf("INI: ZIP compression enabled\n");
          } else {
            printf("INI: ZIP compression disabled\n");
          }

          if (g_precomp.switches.use_gzip) {
            printf("INI: GZip compression enabled\n");
          } else {
            printf("INI: GZip compression disabled\n");
          }

          if (g_precomp.switches.use_png) {
            printf("INI: PNG compression enabled\n");
          } else {
            printf("INI: PNG compression disabled\n");
          }

          if (g_precomp.switches.use_gif) {
            printf("INI: GIF compression enabled\n");
          } else {
            printf("INI: GIF compression disabled\n");
          }

          if (g_precomp.switches.use_jpg) {
            printf("INI: JPG compression enabled\n");
          } else {
            printf("INI: JPG compression disabled\n");
          }

          if (g_precomp.switches.use_mp3) {
            printf("INI: MP3 compression enabled\n");
          } else {
            printf("INI: MP3 compression disabled\n");
          }

          if (g_precomp.switches.use_swf) {
            printf("INI: SWF compression enabled\n");
          } else {
            printf("INI: SWF compression disabled\n");
          }

          if (g_precomp.switches.use_base64) {
            printf("INI: Base64 compression enabled\n");
          } else {
            printf("INI: Base64 compression disabled\n");
          }

          if (g_precomp.switches.use_bzip2) {
            printf("INI: bZip2 compression enabled\n");
          } else {
            printf("INI: bZip2 compression disabled\n");
          }

          valid_param = true;
        }

        if (strcmp(param, "compression_types_disable") == 0) {
          if (compression_type_line_used) {
            printf("ERROR: Both Compression_types_enable and Compression_types_disable used.\n");
            wait_for_key();
            exit(1);
          }
          compression_type_line_used = true;

          g_precomp.switches.use_pdf = true;
          g_precomp.switches.use_zip = true;
          g_precomp.switches.use_gzip = true;
          g_precomp.switches.use_png = true;
          g_precomp.switches.use_gif = true;
          g_precomp.switches.use_jpg = true;
          g_precomp.switches.use_mp3 = true;
          g_precomp.switches.use_swf = true;
          g_precomp.switches.use_base64 = true;
          g_precomp.switches.use_bzip2 = true;

          for (j = 0; j < (int)strlen(value); j++) {
              switch (toupper(value[j])) {
                case 'P': // PDF
                  g_precomp.switches.use_pdf = false;
                  break;
                case 'Z': // ZIP
                  g_precomp.switches.use_zip = false;
                  break;
                case 'G': // GZip
                  g_precomp.switches.use_gzip = false;
                  break;
                case 'N': // PNG
                  g_precomp.switches.use_png = false;
                  break;
                case 'F': // GIF
                  g_precomp.switches.use_gif = false;
                  break;
                case 'J': // JPG
                  g_precomp.switches.use_jpg = false;
                  break;
                case '3': // MP3
                  g_precomp.switches.use_mp3 = false;
                  break;
                case 'S': // SWF
                  g_precomp.switches.use_swf = false;
                  break;
                case 'M': // MIME Base64
                  g_precomp.switches.use_base64 = false;
                  break;
                case 'B': // bZip2
                  g_precomp.switches.use_bzip2 = false;
                  break;
                default:
                  printf("ERROR: Invalid compression type %c\n", value[j]);
                  exit(1);
                  break;
              }
          }

          if (g_precomp.switches.use_pdf) {
            printf("INI: PDF compression enabled\n");
          } else {
            printf("INI: PDF compression disabled\n");
          }

          if (g_precomp.switches.use_zip) {
            printf("INI: ZIP compression enabled\n");
          } else {
            printf("INI: ZIP compression disabled\n");
          }

          if (g_precomp.switches.use_gzip) {
            printf("INI: GZip compression enabled\n");
          } else {
            printf("INI: GZip compression disabled\n");
          }

          if (g_precomp.switches.use_png) {
            printf("INI: PNG compression enabled\n");
          } else {
            printf("INI: PNG compression disabled\n");
          }

          if (g_precomp.switches.use_gif) {
            printf("INI: GIF compression enabled\n");
          } else {
            printf("INI: GIF compression disabled\n");
          }

          if (g_precomp.switches.use_jpg) {
            printf("INI: JPG compression enabled\n");
          } else {
            printf("INI: JPG compression disabled\n");
          }

          if (g_precomp.switches.use_mp3) {
            printf("INI: MP3 compression enabled\n");
          } else {
            printf("INI: MP3 compression disabled\n");
          }

          if (g_precomp.switches.use_swf) {
            printf("INI: SWF compression enabled\n");
          } else {
            printf("INI: SWF compression disabled\n");
          }

          if (g_precomp.switches.use_base64) {
            printf("INI: Base64 compression enabled\n");
          } else {
            printf("INI: Base64 compression disabled\n");
          }

          if (g_precomp.switches.use_bzip2) {
            printf("INI: bZip2 compression enabled\n");
          } else {
            printf("INI: bZip2 compression disabled\n");
          }

          valid_param = true;
        }

        // zLib levels
        if (strcmp(param, "zlib_levels") == 0) {
          for (j = 0; j < 81; j++) {
            use_zlib_level[j] = false;
          }

          level_switch = true;

          for (j = 0; j < ((int)strlen(value)); j += 3) {
            if ((j+2) < (int)strlen(value)) {
              if (value[j+2] != ',') {
                printf("ERROR: zLib levels have to be separated with commas\n");
                exit(1);
              }
            }
            if ((j+1) >= (int)strlen(value)) {
              printf("ERROR: Last zLib level is incomplete\n");
              exit(1);
            }
            int comp_level_to_use = (char(value[j]) - '1');
            int mem_level_to_use = (char(value[j+1]) - '1');
            if (   ((comp_level_to_use >= 0) && (comp_level_to_use <= 8))
                && ((mem_level_to_use >= 0) && (mem_level_to_use <= 8))) {
              use_zlib_level[comp_level_to_use + mem_level_to_use * 9] = true;
            } else {
              printf("ERROR: Invalid zlib level %c%c\n", value[j], value[j+1]);
              wait_for_key();
              exit(1);
            }
          }

          printf("INI: Set zLib levels\n");

          valid_param = true;
        }

        if (strcmp(param, "maximal_recursion_depth") == 0) {
          if (recursion_depth_set) {
            error(ERR_ONLY_SET_RECURSION_DEPTH_ONCE);
          }

          unsigned int max_recursion_d = 0;
          unsigned int multiplicator = 1;
          for (j = (strlen(value)-1); j >= 0; j--) {
            max_recursion_d += ((unsigned int)(value[j])-'0') * multiplicator;
            if ((multiplicator * 10) < multiplicator) {
              error(ERR_RECURSION_DEPTH_TOO_BIG);
            }
            multiplicator *= 10;
          }
          max_recursion_depth = max_recursion_d;
          recursion_depth_set = true;

          printf("INI: Set maximal recursion depth to %i\n", max_recursion_depth);

          valid_param = true;
        }

        if (strcmp(param, "ignore_positions") == 0) {

          long long act_ignore_pos = -1;

          for (j = 0; j < (int)strlen(value); j++) {
            switch (value[j]) {
              case '0':
              case '1':
              case '2':
              case '3':
              case '4':
              case '5':
              case '6':
              case '7':
              case '8':
              case '9':
                if (act_ignore_pos == -1) {
                  act_ignore_pos = (value[j] - '0');
                } else {
                  if ((act_ignore_pos * 10) < act_ignore_pos) {
                    error(ERR_IGNORE_POS_TOO_BIG);
                  }
                  act_ignore_pos = (act_ignore_pos * 10) + (value[j] - '0');
                }
                break;
              case ',':
                if (act_ignore_pos != -1) {
                  g_precomp.switches.ignore_list.push_back(act_ignore_pos);
                  act_ignore_pos = -1;
                }
                break;
              case ' ':
                break;
              default:
                printf("ERROR: Invalid char in ignore_positions: %c\n", value[j]);
                wait_for_key();
                exit(1);
            }
          }
          if (act_ignore_pos != -1) {
            g_precomp.switches.ignore_list.push_back(act_ignore_pos);
          }

          if (print_ignore_positions_message) {
            printf("INI: Set ignore positions\n");
            print_ignore_positions_message = false;
          }

          valid_param = true;
        }

        if (!valid_param) {
          printf("ERROR: Invalid INI parameter: %s\n", param);
          wait_for_key();
          exit(1);
        }
      }
    }
  }
  ini_file.close();
 }

  if (operation == P_COMPRESS) {
    if(!preserve_extension) {
      g_precomp.ctx->output_file_name = g_precomp.ctx->input_file_name;
      const char* backslash_at_pos = strrchr(g_precomp.ctx->output_file_name.c_str(), PATH_DELIM);
      const char* dot_at_pos = strrchr(g_precomp.ctx->output_file_name.c_str(), '.');
      if ((dot_at_pos == NULL) || ((backslash_at_pos != NULL) && (dot_at_pos < backslash_at_pos))) {
        g_precomp.ctx->output_file_name += ".pcf";
      } else {
        g_precomp.ctx->output_file_name = std::string(
          g_precomp.ctx->output_file_name.c_str(),
          dot_at_pos - g_precomp.ctx->output_file_name.c_str()
        );
        // same as output file because input file had .pcf extension?
        if (g_precomp.ctx->input_file_name.compare(g_precomp.ctx->output_file_name + ".pcf") == 0) {
          g_precomp.ctx->output_file_name += "_pcf.pcf";
        }
        else {
          g_precomp.ctx->output_file_name += ".pcf";
        }
      }
    } else {
      g_precomp.ctx->output_file_name = g_precomp.ctx->input_file_name + ".pcf";
    }
  }

  if (file_exists(g_precomp.ctx->output_file_name.c_str())) {
    printf("Output file \"%s\" exists. Overwrite (y/n)? ", g_precomp.ctx->output_file_name.c_str());
    char ch = getche();
    if ((ch != 'Y') && (ch != 'y')) {
      printf("\n");
      wait_for_key();
      exit(0);
    } else {
      printf("\n\n");
    }
  } else {
    printf("\n");
  }
  g_precomp.ctx->fout.open(g_precomp.ctx->output_file_name.c_str(), std::ios_base::out | std::ios_base::binary);
  if (!g_precomp.ctx->fout.is_open()) {
    printf("ERROR: Can't create output file \"%s\"\n", g_precomp.ctx->output_file_name.c_str());
    wait_for_key();
    exit(1);
  }

  printf("Input file: %s\n", g_precomp.ctx->input_file_name.c_str());
  printf("Output file: %s\n\n", g_precomp.ctx->output_file_name.c_str());
  if (g_precomp.switches.DEBUG_MODE) {
    if (min_ident_size_set) {
      printf("\n");
      printf("Minimal ident size set to %i bytes\n", g_precomp.switches.min_ident_size);
    }
    if (!g_precomp.switches.ignore_list.empty()) {
      printf("\n");
      printf("Ignore position list:\n");
      for (auto ignore_pos : g_precomp.switches.ignore_list) {
        std::cout << ignore_pos << std::endl;
      }
      printf("\n");
    }
  }

  if (level_switch) {

    for (i = 0; i < 81; i++) {
      if (use_zlib_level[i]) {
        g_precomp.obsolete.comp_mem_level_count[i] = 0;
      } else {
        g_precomp.obsolete.comp_mem_level_count[i] = -1;
      }
    }

    g_precomp.switches.level_switch_used = true;

  }

  packjpg_mp3_dll_msg();

  return operation;
}
#endif
#endif

void denit_compress(std::string tmp_filename) {

  if (g_precomp.ctx->compression_otf_method != OTF_NONE) {
    denit_compress_otf();
  }

  g_precomp.ctx->fin.close();
  g_precomp.ctx->fout.close();

  if ((recursion_depth == 0) && (!g_precomp.switches.DEBUG_MODE) && g_precomp.ctx->is_show_lzma_progress() && (old_lzma_progress_text_length > -1)) {
    printf("%s", std::string(old_lzma_progress_text_length, '\b').c_str()); // backspaces to remove old lzma progress text
  }

  #ifndef PRECOMPDLL
   long long fout_length = fileSize64(g_precomp.ctx->output_file_name.c_str());
   if (recursion_depth == 0) {
    if (!g_precomp.switches.DEBUG_MODE) {
    printf("%s", std::string(14,'\b').c_str());
    std::cout << "100.00% - New size: " << fout_length << " instead of " << g_precomp.ctx->fin_length << "     " << std::endl;
    } else {
    std::cout << "New size: " << fout_length << " instead of " << g_precomp.ctx->fin_length << "     " << std::endl;
    }
   }
  #else
   if (recursion_depth == 0) {
    if (!g_precomp.switches.DEBUG_MODE) {
    printf(std::string(14,'\b').c_str());
    printf("100.00%% - ");
    printf_time(get_time_ms() - start_time);
    }
   }
  #endif

  #ifndef PRECOMPDLL
   if (recursion_depth == 0) {
    printf("\nDone.\n");
    printf_time(get_time_ms() - start_time);

    // statistics
    printf("\nRecompressed streams: %i/%i\n", g_precomp.statistics.recompressed_streams_count, g_precomp.statistics.decompressed_streams_count);

    if ((g_precomp.statistics.recompressed_streams_count > 0) || (g_precomp.statistics.decompressed_streams_count > 0)) {
      std::array<std::tuple<bool, unsigned int, unsigned int, std::string>, 16> format_statistics{{
        {g_precomp.switches.use_pdf, g_precomp.statistics.decompressed_pdf_count, g_precomp.statistics.recompressed_pdf_count, "PDF"},
        {g_precomp.switches.pdf_bmp_mode && g_precomp.switches.use_pdf, g_precomp.statistics.decompressed_pdf_count_8_bit, g_precomp.statistics.recompressed_pdf_count_8_bit, "PDF image (8-bit)"},
        {g_precomp.switches.pdf_bmp_mode && g_precomp.switches.use_pdf, g_precomp.statistics.decompressed_pdf_count_24_bit, g_precomp.statistics.recompressed_pdf_count_24_bit, "PDF image (24-bit)"},
        {g_precomp.switches.use_zip, g_precomp.statistics.decompressed_zip_count, g_precomp.statistics.recompressed_zip_count, "ZIP"},
        {g_precomp.switches.use_gzip, g_precomp.statistics.decompressed_gzip_count, g_precomp.statistics.recompressed_gzip_count, "GZip"},
        {g_precomp.switches.use_png, g_precomp.statistics.decompressed_png_count, g_precomp.statistics.recompressed_png_count, "PNG"},
        {g_precomp.switches.use_png, g_precomp.statistics.decompressed_png_multi_count, g_precomp.statistics.recompressed_png_multi_count, "PNG (multi)"},
        {g_precomp.switches.use_gif, g_precomp.statistics.decompressed_gif_count, g_precomp.statistics.recompressed_gif_count, "GIF"},
        {g_precomp.switches.use_jpg, g_precomp.statistics.decompressed_jpg_count, g_precomp.statistics.recompressed_jpg_count, "JPG"},
        {g_precomp.switches.use_jpg, g_precomp.statistics.decompressed_jpg_prog_count, g_precomp.statistics.recompressed_jpg_prog_count, "JPG (progressive)"},
        {g_precomp.switches.use_mp3, g_precomp.statistics.decompressed_mp3_count, g_precomp.statistics.recompressed_mp3_count, "MP3"},
        {g_precomp.switches.use_swf, g_precomp.statistics.decompressed_swf_count, g_precomp.statistics.recompressed_swf_count, "SWF"},
        {g_precomp.switches.use_base64, g_precomp.statistics.decompressed_base64_count, g_precomp.statistics.recompressed_base64_count, "Base64"},
        {g_precomp.switches.use_bzip2, g_precomp.statistics.decompressed_bzip2_count, g_precomp.statistics.recompressed_bzip2_count, "bZip2"},
        {g_precomp.switches.intense_mode, g_precomp.statistics.decompressed_zlib_count, g_precomp.statistics.recompressed_zlib_count, "zLib (intense mode)"},
        {g_precomp.switches.brute_mode, g_precomp.statistics.decompressed_brute_count, g_precomp.statistics.recompressed_brute_count, "Brute mode"},
      }};
      for (auto format_stats : format_statistics) {
        bool condition = std::get<0>(format_stats);
        unsigned int decompressed_count = std::get<1>(format_stats);
        unsigned int recompressed_count = std::get<2>(format_stats);
        std::string format_tag = std::get<3>(format_stats);
        if (condition && ((recompressed_count > 0) || (decompressed_count > 0)))
          std::cout << format_tag << " streams: " << recompressed_count << "/" << decompressed_count << std::endl;
      }
    }

    if (!g_precomp.switches.level_switch_used) show_used_levels();

   }
  #endif

  if (g_precomp.ctx->decomp_io_buf != NULL) delete[] g_precomp.ctx->decomp_io_buf;
  g_precomp.ctx->decomp_io_buf = NULL;

  denit();
}

void denit_decompress(std::string tmp_filename) {
  #ifndef PRECOMPDLL
   if (recursion_depth == 0) {
    if (!g_precomp.switches.DEBUG_MODE) {
    printf("%s", std::string(14,'\b').c_str());
    printf("100.00%%\n");
    }
    printf("\nDone.\n");
    printf_time(get_time_ms() - start_time);
   }
  #else
   if (recursion_depth == 0) {
    if (!g_precomp.switches.DEBUG_MODE) {
    printf(std::string(14,'\b').c_str());
    printf("100.00%% - ");
    printf_time(get_time_ms() - start_time);
    }
   }
  #endif

  if (g_precomp.ctx->compression_otf_method != OTF_NONE) {
    denit_decompress_otf();
  }

  denit();
}

void denit_convert() {
  g_precomp.ctx->fin.close();
  g_precomp.ctx->fout.close();

  if ((!g_precomp.switches.DEBUG_MODE) && g_precomp.ctx->is_show_lzma_progress() && (conversion_to_method == OTF_XZ_MT) && (old_lzma_progress_text_length > -1)) {
    printf("%s", std::string(old_lzma_progress_text_length, '\b').c_str()); // backspaces to remove old lzma progress text
  }

  long long fout_length = fileSize64(g_precomp.ctx->output_file_name.c_str());
  #ifndef PRECOMPDLL
   if (!g_precomp.switches.DEBUG_MODE) {
   printf("%s", std::string(14,'\b').c_str());
   std::cout << "100.00% - New size: " << fout_length << " instead of " << g_precomp.ctx->fin_length << "     " << std::endl;
   } else {
   std::cout << "New size: " << fout_length << " instead of " << g_precomp.ctx->fin_length << "     " << std::endl;
   }
   printf("\nDone.\n");
   printf_time(get_time_ms() - start_time);
  #else
   if (!g_precomp.switches.DEBUG_MODE) {
   printf(std::string(14,'\b').c_str());
   std::cout << "100.00% - New size: " << fout_length << " instead of " << g_precomp.ctx->fin_length << "     " << std::endl;
   printf_time(get_time_ms() - start_time);
   }
  #endif

  denit();
}

void denit() {
  g_precomp.ctx->fin.close();
  g_precomp.ctx->fout.close();
}

// Brute mode detects a bit less than intense mode to avoid false positives
// and slowdowns, so both can be active. Also, both of them can have a level
// limit, so two helper functions make things easier to handle.

bool intense_mode_is_active() {
  if (!g_precomp.switches.intense_mode) return false;
  if ((intense_mode_depth_limit == -1) || (recursion_depth <= intense_mode_depth_limit)) return true;

  return false;
}

bool brute_mode_is_active() {
  if (!g_precomp.switches.brute_mode) return false;
  if ((brute_mode_depth_limit == -1) || (recursion_depth <= brute_mode_depth_limit)) return true;

  return false;
}

void copy_penalty_bytes(long long& rek_penalty_bytes_len, bool& use_penalty_bytes) {
  if ((rek_penalty_bytes_len > 0) && (use_penalty_bytes)) {
    std::copy(g_precomp.ctx->local_penalty_bytes.data(), g_precomp.ctx->local_penalty_bytes.data() + rek_penalty_bytes_len, g_precomp.ctx->penalty_bytes.begin());
    g_precomp.ctx->penalty_bytes_len = rek_penalty_bytes_len;
  } else {
    g_precomp.ctx->penalty_bytes_len = 0;
  }
}

#define DEF_COMPARE_CHUNK 512
long long def_compare_bzip2(std::istream& source, std::istream& compfile, int level, long long& decompressed_bytes_used) {
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
    print_work_sign(true);

    strm.avail_in = own_fread(in, 1, DEF_COMPARE_CHUNK, source);
    if (source.bad()) {
      (void)BZ2_bzCompressEnd(&strm);
      return BZ_PARAM_ERROR;
    }
    flush = source.eof() ? BZ_FINISH : BZ_RUN;
    strm.next_in = (char*)in;
    decompressed_bytes_used += strm.avail_in;

    do {
      strm.avail_out = DEF_COMPARE_CHUNK;
      strm.next_out = (char*)out;

      ret = BZ2_bzCompress(&strm, flush);

      have = DEF_COMPARE_CHUNK - strm.avail_out;

      if (have > 0) {
        if (&compfile == &g_precomp.ctx->fin) {
          identical_bytes_compare = compare_file_mem_penalty(compfile, out, g_precomp.ctx->input_file_pos + comp_pos, have, total_same_byte_count, total_same_byte_count_penalty, rek_same_byte_count, rek_same_byte_count_penalty, rek_penalty_bytes_len, local_penalty_bytes_len, use_penalty_bytes);
        } else {
          identical_bytes_compare = compare_file_mem_penalty(compfile, out, comp_pos, have, total_same_byte_count, total_same_byte_count_penalty, rek_same_byte_count, rek_same_byte_count_penalty, rek_penalty_bytes_len, local_penalty_bytes_len, use_penalty_bytes);
        }
      }

      if (have > 0) {
        if ((unsigned int)identical_bytes_compare < (have >> 1)) {
          (void)BZ2_bzCompressEnd(&strm);
          copy_penalty_bytes(rek_penalty_bytes_len, use_penalty_bytes);
          return rek_same_byte_count;
        }
      }

      comp_pos += have;

    } while (strm.avail_out == 0);

  } while (flush != BZ_FINISH);

  (void)BZ2_bzCompressEnd(&strm);
  copy_penalty_bytes(rek_penalty_bytes_len, use_penalty_bytes);
  return rek_same_byte_count;
}

int def_part_bzip2(std::istream& source, std::ostream& dest, int level, long long stream_size_in, long long stream_size_out) {
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
      print_work_sign(true);

      strm.avail_in = own_fread(in, 1, CHUNK, source);
      pos_in += CHUNK;
      flush = BZ_RUN;
    } else {
      strm.avail_in = own_fread(in, 1, stream_size_in - pos_in, source);
      flush = BZ_FINISH;
    }
    if (source.bad()) {
      (void)BZ2_bzCompressEnd(&strm);
      return BZ_PARAM_ERROR;
    }
    strm.next_in = (char*)in;

    do {
      strm.avail_out = CHUNK;
      strm.next_out = (char*)out;

      ret = BZ2_bzCompress(&strm, flush);

      have = CHUNK - strm.avail_out;

      if ((pos_out + (signed)have) > stream_size_out) {
        have = stream_size_out - pos_out;
      }
      pos_out += have;

      own_fwrite(out, 1, have, dest);
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

size_t fread_skip(unsigned char *ptr, size_t size, size_t count, std::istream& stream) {
  size_t bytes_read = 0;
  unsigned int read_tmp;

  do {
    if ((count - bytes_read) >= (frs_line_len - frs_offset)) {
      if ((frs_line_len - frs_offset) > 0) {
        read_tmp = own_fread(ptr + bytes_read, size, frs_line_len - frs_offset, stream);
         if (read_tmp == 0) return bytes_read;
        bytes_read += read_tmp;
      }
      // skip padding bytes
      read_tmp = own_fread(frs_skipbuf, size, frs_skip_len, stream);
      if (read_tmp == 0) return bytes_read;
      frs_offset = 0;
    } else {
      read_tmp = own_fread(ptr + bytes_read, size, count - bytes_read, stream);
      if (read_tmp == 0) return bytes_read;
      bytes_read += read_tmp;
      frs_offset += read_tmp;
    }
  } while (bytes_read < count);

  return bytes_read;
}

int histogram[256];

bool check_inf_result(int cb_pos, int windowbits, bool use_brute_parameters = false) {
  // first check BTYPE bits, skip 11 ("reserved (error)")
  int btype = (g_precomp.ctx->in_buf[cb_pos] & 0x07) >> 1;
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
        int* freq = &histogram[g_precomp.ctx->in_buf[offset+j]];
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

  print_work_sign(true);

  strm.avail_in = 2048;
  strm.next_in = g_precomp.ctx->in_buf + cb_pos;

  /* run inflate() on input until output buffer not full */
  do {
    strm.avail_out = CHUNK;
    strm.next_out = out;

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

int inf_bzip2(std::istream& source, std::ostream& dest, long long& compressed_stream_size, long long& decompressed_stream_size) {
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
    print_work_sign(true);

    strm.avail_in = own_fread(in, 1, CHUNK, source);
    avail_in_before = strm.avail_in;

    if (source.bad()) {
      (void)BZ2_bzDecompressEnd(&strm);
      return BZ_PARAM_ERROR;
    }
    if (strm.avail_in == 0)
      break;
    strm.next_in = (char*)in;

    do {
      strm.avail_out = CHUNK;
      strm.next_out = (char*)out;

      ret = BZ2_bzDecompress(&strm);
      if ((ret != BZ_OK) && (ret != BZ_STREAM_END)) {
        (void)BZ2_bzDecompressEnd(&strm);
        return ret;
      }

      compressed_stream_size += (avail_in_before - strm.avail_in);
      avail_in_before = strm.avail_in;
      
      have = CHUNK - strm.avail_out;
      own_fwrite(out, 1, have, dest);
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


int def_bzip2(std::istream& source, std::ostream& dest, int level) {
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
    print_work_sign(true);

    strm.avail_in = own_fread(in, 1, CHUNK, source);
    if (source.bad()) {
      (void)BZ2_bzCompressEnd(&strm);
      return BZ_PARAM_ERROR;
    }
    flush = source.eof() ? BZ_FINISH : BZ_RUN;
    strm.next_in = (char*)in;

    do {
      strm.avail_out = CHUNK;
      strm.next_out = (char*)out;

      ret = BZ2_bzCompress(&strm, flush);

      have = CHUNK - strm.avail_out;

      own_fwrite(out, 1, have, dest);
      if (dest.bad()) {
        (void)BZ2_bzCompressEnd(&strm);
        return BZ_DATA_ERROR;
      }
    } while (strm.avail_out == 0);

  } while (flush != BZ_FINISH);

  (void)BZ2_bzCompressEnd(&strm);
  return BZ_OK;
}

long long file_recompress_bzip2(std::istream& origfile, int level, long long& decompressed_bytes_used, long long& decompressed_bytes_total, PrecompTmpFile& tmpfile) {
  long long retval;

  force_seekg(tmpfile, 0, SEEK_END);
  decompressed_bytes_total = tmpfile.tellg();
  if (!tmpfile.is_open()) {
    error(ERR_TEMP_FILE_DISAPPEARED, tmpfile.file_path);
  }

  force_seekg(tmpfile, 0, SEEK_SET);
  retval = def_compare_bzip2(tmpfile, origfile, level, decompressed_bytes_used);
  tmpfile.close();
  return retval < 0 ? -1 : retval;
}

void write_decompressed_data(long long byte_count, const char* decompressed_file_name) {
  FileWrapper ftempout;
  ftempout.open(decompressed_file_name, std::ios_base::in | std::ios_base::binary);
  if (!ftempout.is_open()) error(ERR_TEMP_FILE_DISAPPEARED, decompressed_file_name);

  force_seekg(ftempout, 0, SEEK_SET);

  fast_copy(ftempout, g_precomp.ctx->fout, byte_count);
  ftempout.close();
}

void write_decompressed_data_io_buf(long long byte_count, bool in_memory, const char* decompressed_file_name) {
    if (in_memory) {
      fast_copy(g_precomp.ctx->decomp_io_buf, g_precomp.ctx->fout, byte_count);
    } else {
      write_decompressed_data(byte_count, decompressed_file_name);
    }
}

unsigned long long compare_files(std::istream& file1, std::istream& file2, unsigned int pos1, unsigned int pos2) {
  unsigned char input_bytes1[COMP_CHUNK];
  unsigned char input_bytes2[COMP_CHUNK];
  long long same_byte_count = 0;
  int size1, size2, minsize;
  int i;
  bool endNow = false;

  force_seekg(file1, pos1, SEEK_SET);
  force_seekg(file2, pos2, SEEK_SET);

  do {
    print_work_sign(true);

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

long long compare_file_mem_penalty(std::istream& file1, unsigned char* input_bytes2, long long pos1, long long bytecount, long long& total_same_byte_count, long long& total_same_byte_count_penalty, long long& rek_same_byte_count, long long& rek_same_byte_count_penalty, long long& rek_penalty_bytes_len, long long& local_penalty_bytes_len, bool& use_penalty_bytes) {
  int same_byte_count = 0;
  int size1;
  int i;

  unsigned long long old_pos = file1.tellg();
  force_seekg(file1, pos1, SEEK_SET);

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
      g_precomp.ctx->local_penalty_bytes[local_penalty_bytes_len-5] = (total_same_byte_count >> 24) % 256;
      g_precomp.ctx->local_penalty_bytes[local_penalty_bytes_len-4] = (total_same_byte_count >> 16) % 256;
      g_precomp.ctx->local_penalty_bytes[local_penalty_bytes_len-3] = (total_same_byte_count >> 8) % 256;
      g_precomp.ctx->local_penalty_bytes[local_penalty_bytes_len-2] = total_same_byte_count % 256;
      // new byte
      g_precomp.ctx->local_penalty_bytes[local_penalty_bytes_len-1] = input_bytes1[i];
    }
    total_same_byte_count++;

    if (total_same_byte_count_penalty > rek_same_byte_count_penalty) {
      use_penalty_bytes = true;
      rek_penalty_bytes_len = local_penalty_bytes_len;

      rek_same_byte_count = total_same_byte_count;
      rek_same_byte_count_penalty = total_same_byte_count_penalty;
    }
  }

  force_seekg(file1, old_pos, SEEK_SET);

  return same_byte_count;
}

void start_uncompressed_data() {
  g_precomp.ctx->uncompressed_length = 0;
  g_precomp.ctx->uncompressed_pos = g_precomp.ctx->input_file_pos;

  // uncompressed data
  fout_fputc(0);

  g_precomp.ctx->uncompressed_data_in_work = true;
}

void end_uncompressed_data() {

  if (!g_precomp.ctx->uncompressed_data_in_work) return;

  fout_fput_vlint(g_precomp.ctx->uncompressed_length);

  // fast copy of uncompressed data
  force_seekg(g_precomp.ctx->fin, g_precomp.ctx->uncompressed_pos, SEEK_SET);
  fast_copy(g_precomp.ctx->fin, g_precomp.ctx->fout, g_precomp.ctx->uncompressed_length, true);

  g_precomp.ctx->uncompressed_length = -1;

  g_precomp.ctx->uncompressed_data_in_work = false;
}

int best_windowbits = -1;

void init_decompression_variables() {
  g_precomp.ctx->identical_bytes = -1;
  g_precomp.ctx->best_identical_bytes = -1;
  g_precomp.ctx->best_penalty_bytes_len = 0;
  g_precomp.ctx->best_identical_bytes_decomp = -1;
  g_precomp.ctx->identical_bytes_decomp = -1;
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

void debug_deflate_detected(const recompress_deflate_result& rdres, const char* type) {
  if (g_precomp.switches.DEBUG_MODE) {
    print_debug_percent();
    std::cout << "Possible zLib-Stream " << type << " found at position " << g_precomp.ctx->saved_input_file_pos << std::endl;
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
  if (g_precomp.switches.DEBUG_MODE) {
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
  OwnIStream(std::istream* f) : _f(f), _eof(false) {}

  virtual bool eof() const {
    return _eof;
  }
  virtual size_t read(unsigned char* buffer, const size_t size) {
    size_t res = own_fread(buffer, 1, size, *_f);
    _eof |= res < size;
    return res;
  }
private:
  std::istream* _f;
  bool _eof;
};
class UncompressedOutStream : public OutputStream {
public:
  std::string tmp_filename;
  FileWrapper ftempout;
  UncompressedOutStream(bool& in_memory, std::string tmp_filename) : _written(0), _in_memory(in_memory), tmp_filename(tmp_filename) {
    ftempout.open(tmp_filename, std::ios_base::out | std::ios_base::binary);
  }
  ~UncompressedOutStream() {
    if (!_in_memory) {
      ftempout.close();
    }
  }

  virtual size_t write(const unsigned char* buffer, const size_t size) {
    print_work_sign(true);
    if (_in_memory) {
      if (_written + size >= MAX_IO_BUFFER_SIZE) {
        _in_memory = false;
        fast_copy(g_precomp.ctx->decomp_io_buf, ftempout, _written);
      }
      else {
        memcpy(g_precomp.ctx->decomp_io_buf + _written, buffer, size);
        _written += size;
        return size;
      }
    }
    _written += size;
    own_fwrite(buffer, 1, size, ftempout);
    return ftempout.bad() ? 0 : size;
  }

  uint64_t written() const {
    return _written;
  }

private:
  uint64_t _written;
  bool& _in_memory;
};

recompress_deflate_result try_recompression_deflate(std::istream& file, std::string tmp_filename) {
  force_seekg(file, &file == &g_precomp.ctx->fin ? g_precomp.ctx->input_file_pos : 0, SEEK_SET);

  recompress_deflate_result result;
  memset(&result, 0, sizeof(result));
  
  OwnIStream is(&file);

  {
    result.uncompressed_in_memory = true;
    UncompressedOutStream uos(result.uncompressed_in_memory, tmp_filename);
    uint64_t compressed_stream_size = 0;
    result.accepted = preflate_decode(uos, result.recon_data,
                                      compressed_stream_size, is, []() { print_work_sign(true); },
                                      0,
                                      preflate_meta_block_size); // you can set a minimum deflate stream size here
    result.compressed_stream_size = compressed_stream_size;
    result.uncompressed_stream_size = uos.written();

    if (preflate_verify && result.accepted) {
      force_seekg(file, &file == &g_precomp.ctx->fin ? g_precomp.ctx->input_file_pos : 0, SEEK_SET);
      OwnIStream is2(&file);
      std::vector<uint8_t> orgdata(result.compressed_stream_size);
      is2.read(orgdata.data(), orgdata.size());

      MemStream reencoded_deflate;
      MemStream uncompressed_mem(result.uncompressed_in_memory ? std::vector<uint8_t>(g_precomp.ctx->decomp_io_buf, g_precomp.ctx->decomp_io_buf + result.uncompressed_stream_size) : std::vector<uint8_t>());
      OwnIStream uncompressed_file(result.uncompressed_in_memory ? NULL : &uos.ftempout);
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
          FileWrapper f;
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
  OwnOStream(std::ostream* f) : _f(f) {}

  virtual size_t write(const unsigned char* buffer, const size_t size) {
    own_fwrite(buffer, 1, size, *_f);
    return _f->bad() ? 0 : size;
  }
private:
  std::ostream* _f;
};

bool try_reconstructing_deflate(std::istream& fin, std::ostream& fout, const recompress_deflate_result& rdres) {
  OwnOStream os(&fout);
  OwnIStream is(&fin);
  bool result = preflate_reencode(os, rdres.recon_data, is, rdres.uncompressed_stream_size, []() { print_work_sign(true); });
  return result;
}
bool try_reconstructing_deflate_skip(std::istream& fin, std::ostream& fout, const recompress_deflate_result& rdres, const size_t read_part, const size_t skip_part) {
  std::vector<unsigned char> unpacked_output;
  unpacked_output.resize(rdres.uncompressed_stream_size);
  frs_offset = 0;
  frs_skip_len = skip_part;
  frs_line_len = read_part;
  if ((int64_t)fread_skip(unpacked_output.data(), 1, rdres.uncompressed_stream_size, fin) != rdres.uncompressed_stream_size) {
    return false;
  }
  OwnOStream os(&fout);
  return preflate_reencode(os, rdres.recon_data, unpacked_output, []() { print_work_sign(true); });
}
class OwnOStreamMultiPNG : public OutputStream {
public:
  OwnOStreamMultiPNG(std::ostream* f,
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
      own_fwrite(buffer, 1, _to_read, *_f);
      written += _to_read;
      size -= _to_read;
      buffer += _to_read;
      ++_idat_idx;
      if (_idat_idx >= _idat_count) {
        return written;
      }
      unsigned char crc_out[4] = {(unsigned char)(_idat_crcs[_idat_idx] >> 24), (unsigned char)(_idat_crcs[_idat_idx] >> 16), (unsigned char)(_idat_crcs[_idat_idx] >> 8), (unsigned char)(_idat_crcs[_idat_idx] >> 0)};
      own_fwrite(crc_out, 1, 4, *_f);
      _to_read = _idat_lengths[_idat_idx];
      unsigned char len_out[4] = {(unsigned char)(_to_read >> 24), (unsigned char)(_to_read >> 16), (unsigned char)(_to_read >> 8), (unsigned char)(_to_read >> 0)};
      own_fwrite(len_out, 1, 4, *_f);
      own_fwrite("IDAT", 1, 4, *_f);
    }
    own_fwrite(buffer, 1, size, *_f);
    written += size;
    _to_read -= size;
    return written;
  }
private:
  std::ostream* _f;
  const size_t _idat_count;
  const uint32_t* _idat_crcs;
  const uint32_t* _idat_lengths;
  size_t _idat_idx, _to_read;
};
bool try_reconstructing_deflate_multipng(std::istream& fin, std::ostream& fout, const recompress_deflate_result& rdres,
                                const size_t idat_count, const uint32_t* idat_crcs, const uint32_t* idat_lengths) {
  std::vector<unsigned char> unpacked_output;
  unpacked_output.resize(rdres.uncompressed_stream_size);
  if ((int64_t)own_fread(unpacked_output.data(), 1, rdres.uncompressed_stream_size, fin) != rdres.uncompressed_stream_size) {
    return false;
  }
  OwnOStreamMultiPNG os(&fout, idat_count, idat_crcs, idat_lengths);
  return preflate_reencode(os, rdres.recon_data, unpacked_output, []() { print_work_sign(true); });
}

static uint64_t sum_compressed = 0, sum_uncompressed = 0, sum_recon = 0, sum_expansion = 0;
void debug_sums(const recompress_deflate_result& rdres) {
  if (g_precomp.switches.DEBUG_MODE) {
    sum_compressed += rdres.compressed_stream_size;
    sum_uncompressed += rdres.uncompressed_stream_size;
    sum_expansion += rdres.uncompressed_stream_size - rdres.compressed_stream_size;
    sum_recon += rdres.recon_data.size();
    //printf("deflate sums: c %I64d, u %I64d, x %I64d, r %I64d, i %I64d, o %I64d\n",
    //       sum_compressed, sum_uncompressed, sum_expansion, sum_recon, (uint64_t)g_precomp.ctx->fin.tellg(), (uint64_t)g_precomp.ctx->fout.tellp());
  }
}
void debug_pos() {
  if (g_precomp.switches.DEBUG_MODE) {
    //printf("deflate pos: i %I64d, o %I64d\n", (uint64_t)g_precomp.ctx->fin.tellg(), (uint64_t)g_precomp.ctx->fout.tellp());
  }
}
void try_decompression_pdf(int windowbits, int pdf_header_length, int img_width, int img_height, int img_bpc, PrecompTmpFile& tmpfile) {
  init_decompression_variables();

  int bmp_header_type = 0; // 0 = none, 1 = 8-bit, 2 = 24-bit

  // try to decompress at current position
  tmpfile.close();
  recompress_deflate_result rdres = try_recompression_deflate(g_precomp.ctx->fin, tmpfile.file_path);

  if (rdres.uncompressed_stream_size > 0) { // seems to be a zLib-Stream

    g_precomp.statistics.decompressed_streams_count++;
    if (img_bpc == 8) {
      g_precomp.statistics.decompressed_pdf_count_8_bit++;
    } else {
      g_precomp.statistics.decompressed_pdf_count++;
    }
    
    debug_deflate_detected(rdres, "in PDF");

    if (rdres.accepted) {
      g_precomp.statistics.recompressed_streams_count++;
      g_precomp.statistics.recompressed_pdf_count++;

      g_precomp.ctx->non_zlib_was_used = true;
      debug_sums(rdres);

      if (img_bpc == 8) {
        if (rdres.uncompressed_stream_size == (img_width * img_height)) {
          bmp_header_type = 1;
          if (g_precomp.switches.DEBUG_MODE) {
            printf("Image size did match (8 bit)\n");
          }
          g_precomp.statistics.recompressed_pdf_count_8_bit++;
          g_precomp.statistics.recompressed_pdf_count--;
        } else if (rdres.uncompressed_stream_size == (img_width * img_height * 3)) {
          bmp_header_type = 2;
          if (g_precomp.switches.DEBUG_MODE) {
            printf("Image size did match (24 bit)\n");
          }
          g_precomp.statistics.decompressed_pdf_count_8_bit--;
          g_precomp.statistics.decompressed_pdf_count_24_bit++;
          g_precomp.statistics.recompressed_pdf_count_24_bit++;
          g_precomp.statistics.recompressed_pdf_count--;
        } else {
          if (g_precomp.switches.DEBUG_MODE) {
            printf("Image size didn't match with stream size\n");
          }
          g_precomp.statistics.decompressed_pdf_count_8_bit--;
          g_precomp.statistics.decompressed_pdf_count++;
        }
      }

      // end uncompressed data

      g_precomp.ctx->compressed_data_found = true;
      end_uncompressed_data();

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

      fout_fput_deflate_hdr(D_PDF, bmp_c, rdres, g_precomp.ctx->in_buf + g_precomp.ctx->cb + 12, pdf_header_length - 12, false);
      fout_fput_recon_data(rdres);

      // eventually write BMP header

      if (bmp_header_type > 0) {

        int i;

        fout_fputc('B');
        fout_fputc('M');
        // BMP size in bytes
        int bmp_size = ((img_width+3) & -4) * img_height;
        if (bmp_header_type == 2) bmp_size *= 3;
        if (bmp_header_type == 1) {
          bmp_size += 54 + 1024;
        } else {
          bmp_size += 54;
        }
        fout_fput32_little_endian(bmp_size);

        for (i = 0; i < 4; i++) {
          fout_fputc(0);
        }
        fout_fputc(54);
        if (bmp_header_type == 1) {
          fout_fputc(4);
        } else {
          fout_fputc(0);
        }
        fout_fputc(0);
        fout_fputc(0);
        fout_fputc(40);
        fout_fputc(0);
        fout_fputc(0);
        fout_fputc(0);

        fout_fput32_little_endian(img_width);
        fout_fput32_little_endian(img_height);

        fout_fputc(1);
        fout_fputc(0);

        if (bmp_header_type == 1) {
          fout_fputc(8);
        } else {
          fout_fputc(24);
        }
        fout_fputc(0);

        for (i = 0; i < 4; i++) {
          fout_fputc(0);
        }

        if (bmp_header_type == 2)  img_width *= 3;

        int datasize = ((img_width+3) & -4) * img_height;
        if (bmp_header_type == 2) datasize *= 3;
        fout_fput32_little_endian(datasize);

        for (i = 0; i < 16; i++) {
          fout_fputc(0);
        }

        if (bmp_header_type == 1) {
          // write BMP palette
          for (i = 0; i < 1024; i++) {
            fout_fputc(0);
          }
        }
      }

      // write decompressed data

      if ((bmp_header_type == 0) || ((img_width % 4) == 0)) {
        tmpfile.reopen();
        fout_fput_uncompressed(rdres, tmpfile);
      } else {
        FileWrapper ftempout;
        if (!rdres.uncompressed_in_memory) {
          ftempout.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
          if (!ftempout.is_open()) {
            error(ERR_TEMP_FILE_DISAPPEARED, tmpfile.file_path);
          }

          force_seekg(ftempout, 0, SEEK_SET);
        }

        unsigned char* buf_ptr = g_precomp.ctx->decomp_io_buf;
        for (int y = 0; y < img_height; y++) {

          if (rdres.uncompressed_in_memory) {
            fast_copy(buf_ptr, g_precomp.ctx->fout, img_width);
            buf_ptr += img_width;
          } else {
            fast_copy(ftempout, g_precomp.ctx->fout, img_width);
          }

          for (int i = 0; i < (4 - (img_width % 4)); i++) {
            fout_fputc(0);
          }

        }

        ftempout.close();
      }

      // start new uncompressed data
      debug_pos();

      // set input file pointer after recompressed data
      g_precomp.ctx->input_file_pos += rdres.compressed_stream_size - 1;
      g_precomp.ctx->cb += rdres.compressed_stream_size - 1;

    } else {
      if (intense_mode_is_active()) g_precomp.ctx->intense_ignore_offsets->insert(g_precomp.ctx->input_file_pos - 2);
      if (brute_mode_is_active()) g_precomp.ctx->brute_ignore_offsets->insert(g_precomp.ctx->input_file_pos);
      if (g_precomp.switches.DEBUG_MODE) {
        printf("No matches\n");
      }
    }
  }
}

void try_decompression_deflate_type(unsigned& dcounter, unsigned& rcounter, 
                                    const unsigned char type, 
                                    const unsigned char* hdr, const int hdr_length, const bool inc_last, 
                                    const char* debugname, PrecompTmpFile& tmpfile) {
  init_decompression_variables();

  // try to decompress at current position
  tmpfile.close();
  recompress_deflate_result rdres = try_recompression_deflate(g_precomp.ctx->fin, tmpfile.file_path);

  if (rdres.uncompressed_stream_size > 0) { // seems to be a zLib-Stream
    g_precomp.statistics.decompressed_streams_count++;
    dcounter++;

    debug_deflate_detected(rdres, debugname);

    if (rdres.accepted) {
      g_precomp.statistics.recompressed_streams_count++;
      rcounter++;

      g_precomp.ctx->non_zlib_was_used = true;

      debug_sums(rdres);

      // end uncompressed data

      debug_pos();

      g_precomp.ctx->compressed_data_found = true;
      end_uncompressed_data();

      // check recursion
      tmpfile.reopen();
      recursion_result r = recursion_write_file_and_compress(rdres, tmpfile);

#if 0
      // Do we really want to allow uncompressed streams that are smaller than the compressed
      // ones? (It makes sense if the uncompressed stream contains a JPEG, or something similar.
      if (rdres.uncompressed_stream_size <= rdres.compressed_stream_size && !r.success) {
        g_precomp.statistics.recompressed_streams_count--;
        compressed_data_found = false;
        return;
      }
#endif

      debug_pos();

      // write compressed data header without first bytes
      tmpfile.reopen();
      fout_fput_deflate_rec(type, rdres, hdr, hdr_length, inc_last, r, tmpfile);

      debug_pos();

      // set input file pointer after recompressed data
      g_precomp.ctx->input_file_pos += rdres.compressed_stream_size - 1;
      g_precomp.ctx->cb += rdres.compressed_stream_size - 1;

    } else {
      if (type == D_SWF && intense_mode_is_active()) g_precomp.ctx->intense_ignore_offsets->insert(g_precomp.ctx->input_file_pos - 2);
      if (type != D_BRUTE && brute_mode_is_active()) g_precomp.ctx->brute_ignore_offsets->insert(g_precomp.ctx->input_file_pos);
      if (g_precomp.switches.DEBUG_MODE) {
        printf("No matches\n");
      }
    }

  }
}

void try_decompression_zip(int zip_header_length, PrecompTmpFile& tmpfile) {
  try_decompression_deflate_type(g_precomp.statistics.decompressed_zip_count, g_precomp.statistics.recompressed_zip_count, 
                                 D_ZIP, g_precomp.ctx->in_buf + g_precomp.ctx->cb + 4, zip_header_length - 4, false,
                                 "in ZIP", tmpfile);
}

void show_used_levels() {
  if (!g_precomp.ctx->anything_was_used) {
    if (!g_precomp.ctx->non_zlib_was_used) {
      if (g_precomp.ctx->compression_otf_method == OTF_NONE) {
        printf("\nNone of the given compression and memory levels could be used.\n");
        printf("There will be no gain compressing the output file.\n");
      }
    } else {
      if ((!max_recursion_depth_reached) && (max_recursion_depth_used != max_recursion_depth)) {
        #ifdef COMFORT
          printf("\nYou can speed up Precomp for THIS FILE with these INI parameters:\n");
          printf("Maximal_Recursion_Depth=");
        #else
          printf("\nYou can speed up Precomp for THIS FILE with these parameters:\n");
          printf("-d");
        #endif
        printf("%i\n", max_recursion_depth_used);
      }
    }
    if (max_recursion_depth_reached) {
      printf("\nMaximal recursion depth %i reached, increasing it could give better results.\n", max_recursion_depth);
    }
    return;
  }

  int i, i_sort;
  int level_count = 0;
  #ifdef COMFORT
    printf("\nYou can speed up Precomp for THIS FILE with these INI parameters:\n");
    printf("zLib_Levels=");
  #else
    printf("\nYou can speed up Precomp for THIS FILE with these parameters:\n");
    printf("-zl");
  #endif

  bool first_one = true;
  for (i = 0; i < 81; i++) {
   i_sort = (i % 9) * 9 + (i / 9); // to get the displayed levels sorted
   if (g_precomp.obsolete.zlib_level_was_used[i_sort]) {
     if (!first_one) {
       printf(",");
     } else {
       first_one = false;
     }
     printf("%i%i", (i_sort%9) + 1, (i_sort/9) + 1);
     level_count++;
   }
  }

  std::string disable_methods("");
  std::array<std::tuple<bool, unsigned int, unsigned int, std::string>, 10> disable_formats{{
    {g_precomp.switches.use_pdf, g_precomp.statistics.recompressed_pdf_count, g_precomp.statistics.decompressed_pdf_count, "p"},
    {g_precomp.switches.use_zip, g_precomp.statistics.recompressed_zip_count, g_precomp.statistics.decompressed_zip_count, "z"},
    {g_precomp.switches.use_gzip, g_precomp.statistics.recompressed_gzip_count, g_precomp.statistics.decompressed_gzip_count, "g"},
    {g_precomp.switches.use_png, g_precomp.statistics.recompressed_png_count + g_precomp.statistics.recompressed_png_multi_count, g_precomp.statistics.decompressed_png_count + g_precomp.statistics.decompressed_png_multi_count, "n"},
    {g_precomp.switches.use_gif, g_precomp.statistics.recompressed_gif_count, g_precomp.statistics.decompressed_gif_count, "f"},
    {g_precomp.switches.use_jpg, g_precomp.statistics.recompressed_jpg_count + g_precomp.statistics.recompressed_jpg_prog_count, g_precomp.statistics.decompressed_jpg_count + g_precomp.statistics.decompressed_jpg_prog_count, "j"},
    {g_precomp.switches.use_swf, g_precomp.statistics.recompressed_swf_count, g_precomp.statistics.decompressed_swf_count, "s"},
    {g_precomp.switches.use_base64, g_precomp.statistics.recompressed_base64_count, g_precomp.statistics.decompressed_base64_count, "m"},
    {g_precomp.switches.use_bzip2, g_precomp.statistics.recompressed_bzip2_count, g_precomp.statistics.decompressed_bzip2_count, "b"},
    {g_precomp.switches.use_mp3, g_precomp.statistics.recompressed_mp3_count, g_precomp.statistics.decompressed_mp3_count, "3"},
  }};
  for (auto disable_format : disable_formats) {
    if ((std::get<0>(disable_format) && ((std::get<1>(disable_format) == 0) && (std::get<2>(disable_format) > 0)))) disable_methods += std::get<3>(disable_format);
  }
  if ( disable_methods.length() > 0 ) {
    #ifdef COMFORT
      printf("\nCompression_Types_Disable=%s",disable_methods.c_str());
    #else
      printf(" -t-%s",disable_methods.c_str());
    #endif
  }

  if (max_recursion_depth_reached) {
    printf("\n\nMaximal recursion depth %i reached, increasing it could give better results.\n", max_recursion_depth);
  } else if (max_recursion_depth_used != max_recursion_depth) {
    #ifdef COMFORT
      printf("\nMaximal_Recursion_Depth=");
    #else
      printf(" -d");
    #endif
    printf("%i", max_recursion_depth_used);
  }

  if ((level_count == 1) && (!g_precomp.switches.fast_mode)) {
    printf("\n\nFast mode does exactly the same for this file, only faster.\n");
  }

  printf("\n");
}

bool compress_file(float min_percent, float max_percent) {

  g_precomp.ctx->comp_decomp_state = P_COMPRESS;

  g_precomp.ctx->decomp_io_buf = new unsigned char[MAX_IO_BUFFER_SIZE];

  g_precomp.ctx->global_min_percent = min_percent;
  g_precomp.ctx->global_max_percent = max_percent;

  if (recursion_depth == 0) write_header();
  g_precomp.ctx->uncompressed_length = -1;
  g_precomp.ctx->uncompressed_bytes_total = 0;
  g_precomp.ctx->uncompressed_bytes_written = 0;

  if (!g_precomp.switches.DEBUG_MODE) show_progress(min_percent, (recursion_depth > 0), false);

  force_seekg(g_precomp.ctx->fin, 0, SEEK_SET);
  g_precomp.ctx->fin.read(reinterpret_cast<char*>(g_precomp.ctx->in_buf), IN_BUF_SIZE);
  long long in_buf_pos = 0;
  g_precomp.ctx->cb = -1;

  g_precomp.ctx->anything_was_used = false;
  g_precomp.ctx->non_zlib_was_used = false;

  std::string tempfile_base = temp_files_tag() + "_decomp_";
  std::string tempfile;
  for (g_precomp.ctx->input_file_pos = 0; g_precomp.ctx->input_file_pos < g_precomp.ctx->fin_length; g_precomp.ctx->input_file_pos++) {
  tempfile = tempfile_base;

  g_precomp.ctx->compressed_data_found = false;

  bool ignore_this_pos = false;

  if ((in_buf_pos + IN_BUF_SIZE) <= (g_precomp.ctx->input_file_pos + CHECKBUF_SIZE)) {
    force_seekg(g_precomp.ctx->fin, g_precomp.ctx->input_file_pos, SEEK_SET);
    g_precomp.ctx->fin.read(reinterpret_cast<char*>(g_precomp.ctx->in_buf), IN_BUF_SIZE);
    in_buf_pos = g_precomp.ctx->input_file_pos;
    g_precomp.ctx->cb = 0;

    if (!g_precomp.switches.DEBUG_MODE) {
      float percent = ((g_precomp.ctx->input_file_pos + g_precomp.ctx->uncompressed_bytes_written) / ((float)g_precomp.ctx->fin_length + g_precomp.ctx->uncompressed_bytes_total)) * (max_percent - min_percent) + min_percent;
      show_progress(percent, true, true);
    }
  } else {
    g_precomp.ctx->cb++;
  }

  for (auto ignore_pos : g_precomp.switches.ignore_list) {
    ignore_this_pos = (ignore_pos == g_precomp.ctx->input_file_pos);
    if (ignore_this_pos) {
      break;
    }
  }

  if (!ignore_this_pos) {

    // ZIP header?
    if (((g_precomp.ctx->in_buf[g_precomp.ctx->cb] == 'P') && (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 1] == 'K')) && (g_precomp.switches.use_zip)) {
      // local file header?
      if ((g_precomp.ctx->in_buf[g_precomp.ctx->cb + 2] == 3) && (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 3] == 4)) {
        if (g_precomp.switches.DEBUG_MODE) {
        printf("ZIP header detected\n");
        print_debug_percent();
        std::cout << "ZIP header detected at position " << g_precomp.ctx->input_file_pos << std::endl;
        }
        unsigned int compressed_size = (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 21] << 24) + (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 20] << 16) + (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 19] << 8) + g_precomp.ctx->in_buf[g_precomp.ctx->cb + 18];
        unsigned int uncompressed_size = (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 25] << 24) + (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 24] << 16) + (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 23] << 8) + g_precomp.ctx->in_buf[g_precomp.ctx->cb + 22];
        unsigned int filename_length = (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 27] << 8) + g_precomp.ctx->in_buf[g_precomp.ctx->cb + 26];
        unsigned int extra_field_length = (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 29] << 8) + g_precomp.ctx->in_buf[g_precomp.ctx->cb + 28];
        if (g_precomp.switches.DEBUG_MODE) {
        printf("compressed size: %i\n", compressed_size);
        printf("uncompressed size: %i\n", uncompressed_size);
        printf("file name length: %i\n", filename_length);
        printf("extra field length: %i\n", extra_field_length);
        }

        if ((filename_length + extra_field_length) <= CHECKBUF_SIZE
            && g_precomp.ctx->in_buf[g_precomp.ctx->cb + 8] == 8 && g_precomp.ctx->in_buf[g_precomp.ctx->cb + 9] == 0) { // Compression method 8: Deflate

          int header_length = 30 + filename_length + extra_field_length;

          g_precomp.ctx->saved_input_file_pos = g_precomp.ctx->input_file_pos;
          g_precomp.ctx->saved_cb = g_precomp.ctx->cb;

          g_precomp.ctx->input_file_pos += header_length;

          tempfile += "zip";
          PrecompTmpFile tmp_zip;
          tmp_zip.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
          try_decompression_zip(header_length, tmp_zip);

          g_precomp.ctx->cb += header_length;

          if (!g_precomp.ctx->compressed_data_found) {
            g_precomp.ctx->input_file_pos = g_precomp.ctx->saved_input_file_pos;
            g_precomp.ctx->cb = g_precomp.ctx->saved_cb;
          }

        }
      }
    }

    if ((!g_precomp.ctx->compressed_data_found) && (g_precomp.switches.use_gzip)) { // no ZIP header -> GZip header?
      if ((g_precomp.ctx->in_buf[g_precomp.ctx->cb] == 31) && (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 1] == 139)) {
        // check zLib header in GZip header
        int compression_method = (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 2] & 15);
        if ((compression_method == 8) &&
           ((g_precomp.ctx->in_buf[g_precomp.ctx->cb + 3] & 224) == 0)  // reserved FLG bits must be zero
          ) {

          //((g_precomp.ctx->in_buf[g_precomp.ctx->cb + 8] == 2) || (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 8] == 4)) { //XFL = 2 or 4
          //  TODO: Can be 0 also, check if other values are used, too.
          //
          //  TODO: compressed data is followed by CRC-32 and uncompressed
          //    size. Uncompressed size can be used to check if it is really
          //    a GZ stream.

          bool fhcrc = (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 3] & 2) == 2;
          bool fextra = (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 3] & 4) == 4;
          bool fname = (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 3] & 8) == 8;
          bool fcomment = (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 3] & 16) == 16;

          int header_length = 10;

          g_precomp.ctx->saved_input_file_pos = g_precomp.ctx->input_file_pos;
          g_precomp.ctx->saved_cb = g_precomp.ctx->cb;

          bool dont_compress = false;

          if (fhcrc || fextra || fname || fcomment) {
            int act_checkbuf_pos = 10;

            if (fextra) {
              int xlen = g_precomp.ctx->in_buf[g_precomp.ctx->cb + act_checkbuf_pos] + (g_precomp.ctx->in_buf[g_precomp.ctx->cb + act_checkbuf_pos + 1] << 8);
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
              } while ((g_precomp.ctx->in_buf[g_precomp.ctx->cb + act_checkbuf_pos - 1] != 0) && (!dont_compress));
            }
            if ((fcomment) && (!dont_compress)) {
              do {
                act_checkbuf_pos ++;
                dont_compress = (act_checkbuf_pos == CHECKBUF_SIZE);
                header_length++;
              } while ((g_precomp.ctx->in_buf[g_precomp.ctx->cb + act_checkbuf_pos - 1] != 0) && (!dont_compress));
            }
            if ((fhcrc) && (!dont_compress)) {
              act_checkbuf_pos += 2;
              dont_compress = (act_checkbuf_pos > CHECKBUF_SIZE);
              header_length += 2;
            }
          }

          if (!dont_compress) {

            g_precomp.ctx->input_file_pos += header_length; // skip GZip header

            tempfile += "gzip";
            PrecompTmpFile tmp_gzip;
            tmp_gzip.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
            try_decompression_gzip(header_length, tmp_gzip);

            g_precomp.ctx->cb += header_length;

          }

          if (!g_precomp.ctx->compressed_data_found) {
            g_precomp.ctx->input_file_pos = g_precomp.ctx->saved_input_file_pos;
            g_precomp.ctx->cb = g_precomp.ctx->saved_cb;
          }
        }
      }
    }

    if ((!g_precomp.ctx->compressed_data_found) && (g_precomp.switches.use_pdf)) { // no Gzip header -> PDF FlateDecode?
      if (memcmp(g_precomp.ctx->in_buf + g_precomp.ctx->cb, "/FlateDecode", 12) == 0) {
        g_precomp.ctx->saved_input_file_pos = g_precomp.ctx->input_file_pos;
        g_precomp.ctx->saved_cb = g_precomp.ctx->cb;

        long long act_search_pos = 12;
        bool found_stream = false;
        do {
          if (g_precomp.ctx->in_buf[g_precomp.ctx->cb + act_search_pos] == 's') {
            if (memcmp(g_precomp.ctx->in_buf + g_precomp.ctx->cb + act_search_pos, "stream", 6) == 0) {
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

          if ((g_precomp.ctx->input_file_pos + act_search_pos) >= 4096) {
            force_seekg(g_precomp.ctx->fin, (g_precomp.ctx->input_file_pos + act_search_pos) - 4096, SEEK_SET);
            g_precomp.ctx->fin.read(reinterpret_cast<char*>(type_buf), 4096);
            type_buf_length = 4096;
          } else {
            force_seekg(g_precomp.ctx->fin, 0, SEEK_SET);
            g_precomp.ctx->fin.read(reinterpret_cast<char*>(type_buf), g_precomp.ctx->input_file_pos + act_search_pos);
            type_buf_length = g_precomp.ctx->input_file_pos + act_search_pos;
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

          if ((start_pos > -1) && (g_precomp.switches.pdf_bmp_mode)) {

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
              if (g_precomp.switches.DEBUG_MODE) {
                printf("Possible image in PDF found: %i * %i, %i bit\n", width_val, height_val, bpc_val);
              }
            }
          }

          if ((g_precomp.ctx->in_buf[g_precomp.ctx->cb + act_search_pos + 6] == 13) || (g_precomp.ctx->in_buf[g_precomp.ctx->cb + act_search_pos + 6] == 10)) {
            if ((g_precomp.ctx->in_buf[g_precomp.ctx->cb + act_search_pos + 7] == 13) || (g_precomp.ctx->in_buf[g_precomp.ctx->cb + act_search_pos + 7] == 10)) {
              // seems to be two byte EOL - zLib Header present?
              if (((((g_precomp.ctx->in_buf[g_precomp.ctx->cb + act_search_pos + 8] << 8) + g_precomp.ctx->in_buf[g_precomp.ctx->cb + act_search_pos + 9]) % 31) == 0) &&
                  ((g_precomp.ctx->in_buf[g_precomp.ctx->cb + act_search_pos + 9] & 32) == 0)) { // FDICT must not be set
                int compression_method = (g_precomp.ctx->in_buf[g_precomp.ctx->cb + act_search_pos + 8] & 15);
                if (compression_method == 8) {

                  int windowbits = (g_precomp.ctx->in_buf[g_precomp.ctx->cb + act_search_pos + 8] >> 4) + 8;

                  g_precomp.ctx->input_file_pos += act_search_pos + 10; // skip PDF part

                  tempfile += "pdf";
                  PrecompTmpFile tmp_pdf;
                  tmp_pdf.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
                  try_decompression_pdf(-windowbits, act_search_pos + 10, width_val, height_val, bpc_val, tmp_pdf);

                  g_precomp.ctx->cb += act_search_pos + 10;
                }
              }
            } else {
              // seems to be one byte EOL - zLib Header present?
              if ((((g_precomp.ctx->in_buf[g_precomp.ctx->cb + act_search_pos + 7] << 8) + g_precomp.ctx->in_buf[g_precomp.ctx->cb + act_search_pos + 8]) % 31) == 0) {
                int compression_method = (g_precomp.ctx->in_buf[g_precomp.ctx->cb + act_search_pos + 7] & 15);
                if (compression_method == 8) {
                  int windowbits = (g_precomp.ctx->in_buf[g_precomp.ctx->cb + act_search_pos + 7] >> 4) + 8;

                  g_precomp.ctx->input_file_pos += act_search_pos + 9; // skip PDF part

                  tempfile += "pdf";
                  PrecompTmpFile tmp_pdf;
                  tmp_pdf.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
                  try_decompression_pdf(-windowbits, act_search_pos + 9, width_val, height_val, bpc_val, tmp_pdf);

                  g_precomp.ctx->cb += act_search_pos + 9;
                }
              }
            }
          }
        }

        if (!g_precomp.ctx->compressed_data_found) {
          g_precomp.ctx->input_file_pos = g_precomp.ctx->saved_input_file_pos;
          g_precomp.ctx->cb = g_precomp.ctx->saved_cb;
        }
      }
    }

    if ((!g_precomp.ctx->compressed_data_found) && (g_precomp.switches.use_png)) { // no PDF header -> PNG IDAT?
      if (memcmp(g_precomp.ctx->in_buf + g_precomp.ctx->cb, "IDAT", 4) == 0) {

        // space for length and crc parts of IDAT chunks
        idat_lengths = (unsigned int*)(realloc(idat_lengths, 100 * sizeof(unsigned int)));
        idat_crcs = (unsigned int*)(realloc(idat_crcs, 100 * sizeof(unsigned int)));

        g_precomp.ctx->saved_input_file_pos = g_precomp.ctx->input_file_pos;
        g_precomp.ctx->saved_cb = g_precomp.ctx->cb;

        idat_count = 0;
        bool zlib_header_correct = false;
        int windowbits = 0;

        // get preceding length bytes
        if (g_precomp.ctx->input_file_pos >= 4) {
          force_seekg(g_precomp.ctx->fin, g_precomp.ctx->input_file_pos - 4, SEEK_SET);

          g_precomp.ctx->fin.read(reinterpret_cast<char*>(in), 10);
         if (g_precomp.ctx->fin.gcount() == 10) {
          force_seekg(g_precomp.ctx->fin, (long long)g_precomp.ctx->fin.tellg() - 2, SEEK_SET);

          idat_lengths[0] = (in[0] << 24) + (in[1] << 16) + (in[2] << 8) + in[3];
         if (idat_lengths[0] > 2) {

          // check zLib header and get windowbits
          zlib_header[0] = in[8];
          zlib_header[1] = in[9];
          if ((((in[8] << 8) + in[9]) % 31) == 0) {
            if ((in[8] & 15) == 8) {
              if ((in[9] & 32) == 0) { // FDICT must not be set
                windowbits = (in[8] >> 4) + 8;
                zlib_header_correct = true;
              }
            }
          }

          if (zlib_header_correct) {

            idat_count++;

            // go through additional IDATs
            for (;;) {
              force_seekg(g_precomp.ctx->fin, (long long)g_precomp.ctx->fin.tellg() + idat_lengths[idat_count - 1], SEEK_SET);
              g_precomp.ctx->fin.read(reinterpret_cast<char*>(in), 12);
              if (g_precomp.ctx->fin.gcount() != 12) { // CRC, length, "IDAT"
                idat_count = 0;
                break;
              }

              if (memcmp(in + 8, "IDAT", 4) == 0) {
                idat_crcs[idat_count] = (in[0] << 24) + (in[1] << 16) + (in[2] << 8) + in[3];
                idat_lengths[idat_count] = (in[4] << 24) + (in[5] << 16) + (in[6] << 8) + in[7];
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
          g_precomp.ctx->input_file_pos += 6;
          tempfile += "png";
          PrecompTmpFile tmp_png;
          tmp_png.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
          try_decompression_png(-windowbits, tmp_png);
          g_precomp.ctx->cb += 6;
        } else if (idat_count > 1) {
          // copy to temp0.dat before trying to recompress
          std::string png_tmp_filename = tempfile + "png";
          remove(png_tmp_filename.c_str());
          PrecompTmpFile tmp_png;
          tmp_png.open(png_tmp_filename, std::ios_base::in | std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);

          force_seekg(g_precomp.ctx->fin, g_precomp.ctx->input_file_pos + 6, SEEK_SET); // start after zLib header

          idat_lengths[0] -= 2; // zLib header length
          for (int i = 0; i < idat_count; i++) {
            fast_copy(g_precomp.ctx->fin, tmp_png, idat_lengths[i]);
            force_seekg(g_precomp.ctx->fin, (long long)g_precomp.ctx->fin.tellg() + 12, SEEK_SET);
          }
          idat_lengths[0] += 2;

          g_precomp.ctx->input_file_pos += 6;
          tempfile += "pngmulti";
          PrecompTmpFile tmp_pngmulti;
          tmp_pngmulti.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
          try_decompression_png_multi(tmp_png, -windowbits, tmp_pngmulti);
          g_precomp.ctx->cb += 6;

          tmp_png.close();
        }

        if (!g_precomp.ctx->compressed_data_found) {
          g_precomp.ctx->input_file_pos = g_precomp.ctx->saved_input_file_pos;
          g_precomp.ctx->cb = g_precomp.ctx->saved_cb;
        }

        free(idat_lengths);
        idat_lengths = NULL;
        free(idat_crcs);
        idat_crcs = NULL;
      }

    }

    if ((!g_precomp.ctx->compressed_data_found) && (g_precomp.switches.use_gif)) { // no PNG header -> GIF header?
      if ((g_precomp.ctx->in_buf[g_precomp.ctx->cb] == 'G') && (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 1] == 'I') && (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 2] == 'F')) {
        if ((g_precomp.ctx->in_buf[g_precomp.ctx->cb + 3] == '8') && (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 5] == 'a')) {
          if ((g_precomp.ctx->in_buf[g_precomp.ctx->cb + 4] == '7') || (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 4] == '9')) {

            unsigned char version[5];

            for (int i = 0; i < 5; i++) {
              version[i] = g_precomp.ctx->in_buf[g_precomp.ctx->cb + i];
            }

            g_precomp.ctx->saved_input_file_pos = g_precomp.ctx->input_file_pos;
            g_precomp.ctx->saved_cb = g_precomp.ctx->cb;

            tempfile += "gif";
            PrecompTmpFile tmp_gif;
            tmp_gif.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
            try_decompression_gif(version, tmp_gif);

            if (!g_precomp.ctx->compressed_data_found) {
              g_precomp.ctx->input_file_pos = g_precomp.ctx->saved_input_file_pos;
              g_precomp.ctx->cb = g_precomp.ctx->saved_cb;
            }
          }
        }
      }
    }

    if ((!g_precomp.ctx->compressed_data_found) && (g_precomp.switches.use_jpg)) { // no GIF header -> JPG header?
      if ((g_precomp.ctx->in_buf[g_precomp.ctx->cb] == 0xFF) && (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 1] == 0xD8) && (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 2] == 0xFF) && (
           (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 3] == 0xC0) || (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 3] == 0xC2) || (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 3] == 0xC4) || ((g_precomp.ctx->in_buf[g_precomp.ctx->cb + 3] >= 0xDB) && (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 3] <= 0xFE))
         )) { // SOI (FF D8) followed by a valid marker for Baseline/Progressive JPEGs
        g_precomp.ctx->saved_input_file_pos = g_precomp.ctx->input_file_pos;
        g_precomp.ctx->saved_cb = g_precomp.ctx->cb;

        bool done = false, found = false;
        bool hasQuantTable = (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 3] == 0xDB);
        bool progressive_flag = (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 3] == 0xC2);
        g_precomp.ctx->input_file_pos+=2;

        do{
          force_seekg(g_precomp.ctx->fin, g_precomp.ctx->input_file_pos, SEEK_SET);
          g_precomp.ctx->fin.read(reinterpret_cast<char*>(in), 5);
          if ((g_precomp.ctx->fin.gcount() != 5) || (in[0] != 0xFF))
              break;
          int length = (int)in[2]*256+(int)in[3];
          switch (in[1]){
            case 0xDB : {
              // FF DB XX XX QtId ...
              // Marker length (XX XX) must be = 2 + (multiple of 65 <= 260)
              // QtId:
              // bit 0..3: number of QT (0..3, otherwise error)
              // bit 4..7: precision of QT, 0 = 8 bit, otherwise 16 bit               
              if (length<=262 && ((length-2)%65)==0 && in[4]<=3) {
                hasQuantTable = true;
                g_precomp.ctx->input_file_pos += length+2;
              }
              else
                done = true;
              break;
            }
            case 0xC4 : {
              done = ((in[4]&0xF)>3 || (in[4]>>4)>1);
              g_precomp.ctx->input_file_pos += length+2;
              break;
            }
            case 0xDA : found = hasQuantTable;
            case 0xD9 : done = true; break; //EOI with no SOS?
            case 0xC2 : progressive_flag = true;
            case 0xC0 : done = (in[4] != 0x08);
            default: g_precomp.ctx->input_file_pos += length+2;
          }
        }
        while (!done);

        if (found){
          found = done = false;
          g_precomp.ctx->input_file_pos += 5;

          bool isMarker = ( in[4] == 0xFF );
          size_t bytesRead = 0;
          for (;;) {
            if (done) break;
            g_precomp.ctx->fin.read(reinterpret_cast<char*>(in), sizeof(in[0])* CHUNK);
            bytesRead = g_precomp.ctx->fin.gcount();
            if (!bytesRead) break;
            for (size_t i = 0; !done && (i < bytesRead); i++){
              g_precomp.ctx->input_file_pos++;
              if (!isMarker){
                isMarker = ( in[i] == 0xFF );
              }
              else{
                done = (in[i] && ((in[i]&0xF8) != 0xD0) && ((progressive_flag)?(in[i] != 0xC4) && (in[i] != 0xDA):true));
                found = (in[i] == 0xD9);
                isMarker = false;
              }
            }
          }
        }

        if (found){
          long long jpg_length = g_precomp.ctx->input_file_pos - g_precomp.ctx->saved_input_file_pos;
          g_precomp.ctx->input_file_pos = g_precomp.ctx->saved_input_file_pos;
          PrecompTmpFile tmp_jpg;
          tmp_jpg.open(tempfile + "jpg", std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
          try_decompression_jpg(jpg_length, progressive_flag, tmp_jpg);
        }
        if (!found || !g_precomp.ctx->compressed_data_found) {
          g_precomp.ctx->input_file_pos = g_precomp.ctx->saved_input_file_pos;
          g_precomp.ctx->cb = g_precomp.ctx->saved_cb;
        }
      }
      if (g_precomp.ctx->compressed_data_found) {  // If we did find jpg, we 'jpg' it to tempfile so we clean it afterwards
        tempfile += "jpg";
      }
    }

    if ((!g_precomp.ctx->compressed_data_found) && (g_precomp.switches.use_mp3)) { // no JPG header -> MP3 header?
      if ((g_precomp.ctx->in_buf[g_precomp.ctx->cb] == 0xFF) && ((g_precomp.ctx->in_buf[g_precomp.ctx->cb + 1] & 0xE0) == 0xE0)) { // frame start
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

        g_precomp.ctx->saved_input_file_pos = g_precomp.ctx->input_file_pos;
        g_precomp.ctx->saved_cb = g_precomp.ctx->cb;

        long long act_pos = g_precomp.ctx->input_file_pos;

        // parse frames until first invalid frame is found or end-of-file
        force_seekg(g_precomp.ctx->fin, act_pos, SEEK_SET);
        
        for (;;) {
          g_precomp.ctx->fin.read(reinterpret_cast<char*>(in), 4);
          if (g_precomp.ctx->fin.gcount() != 4) break;
          // check syncword
          if ((in[0] != 0xFF) || ((in[1] & 0xE0) != 0xE0)) break;
          // compare data from header
          if (n == 0) {
            mpeg        = (in[1] >> 3) & 0x3;
            layer       = (in[1] >> 1) & 0x3;
            protection  = (in[1] >> 0) & 0x1;
            samples     = (in[2] >> 2) & 0x3;
            channels    = (in[3] >> 6) & 0x3;
            type = MBITS( in[1], 5, 1 );
            // avoid slowdown and multiple verbose messages on unsupported types that have already been detected
            if ((type != MPEG1_LAYER_III) && (g_precomp.ctx->saved_input_file_pos <= g_precomp.ctx->suppress_mp3_type_until[type])) {
                break;
            }
          } else {
            if (n == 1) {
              mp3_parsing_cache_second_frame_candidate = act_pos;
              mp3_parsing_cache_second_frame_candidate_size = act_pos - g_precomp.ctx->saved_input_file_pos;
            }
            if (type == MPEG1_LAYER_III) { // supported MP3 type, all header information must be identical to the first frame
              if (
                (mpeg       != ((in[1] >> 3) & 0x3)) ||
                (layer      != ((in[1] >> 1) & 0x3)) ||
                (protection != ((in[1] >> 0) & 0x1)) ||
                (samples    != ((in[2] >> 2) & 0x3)) ||
                (channels   != ((in[3] >> 6) & 0x3)) ||
                (type       != MBITS( in[1], 5, 1))) break;
            } else { // unsupported type, compare only type, ignore the other header information to get a longer stream
              if (type != MBITS( in[1], 5, 1)) break;
            }
          }

          bits     = (in[2] >> 4) & 0xF;
          padding  = (in[2] >> 1) & 0x1;
          // check for problems
          if ((mpeg == 0x1) || (layer == 0x0) ||
              (bits == 0x0) || (bits == 0xF) || (samples == 0x3)) break;
          // find out frame size
          frame_size = frame_size_table[mpeg][layer][samples][bits];
          if (padding) frame_size += (layer == LAYER_I) ? 4 : 1;

          // if this frame was part of a stream that already has been parsed, skip parsing
          if (n == 0) {
            if (act_pos == g_precomp.ctx->mp3_parsing_cache_second_frame) {
              n = g_precomp.ctx->mp3_parsing_cache_n;
              mp3_length = g_precomp.ctx->mp3_parsing_cache_mp3_length;

              // update values
              g_precomp.ctx->mp3_parsing_cache_second_frame = act_pos + frame_size;
              g_precomp.ctx->mp3_parsing_cache_n -= 1;
              g_precomp.ctx->mp3_parsing_cache_mp3_length -= frame_size;

              break;
            }
          }

          n++;
          mp3_length += frame_size;
          act_pos += frame_size;

          // if supported MP3 type, validate frames
          if ((type == MPEG1_LAYER_III) && (frame_size > 4)) {
            unsigned char header2 = in[2];
            unsigned char header3 = in[3];
            g_precomp.ctx->fin.read(reinterpret_cast<char*>(in), frame_size - 4);
            if (g_precomp.ctx->fin.gcount() != (unsigned int)(frame_size - 4)) {
              // discard incomplete frame
              n--;
              mp3_length -= frame_size;
              break;
            }
            if (!is_valid_mp3_frame(in, header2, header3, protection)) {
                n = 0;
                break;
            }
          } else {
            force_seekg(g_precomp.ctx->fin, act_pos, SEEK_SET);
          }
        }

        // conditions for proper first frame: 5 consecutive frames
        if (n >= 5) {
          if (mp3_parsing_cache_second_frame_candidate > -1) {
            g_precomp.ctx->mp3_parsing_cache_second_frame = mp3_parsing_cache_second_frame_candidate;
            g_precomp.ctx->mp3_parsing_cache_n = n - 1;
            g_precomp.ctx->mp3_parsing_cache_mp3_length = mp3_length - mp3_parsing_cache_second_frame_candidate_size;
          }

          long long position_length_sum = g_precomp.ctx->saved_input_file_pos + mp3_length;

          // type must be MPEG-1, Layer III, packMP3 won't process any other files
          if ( type == MPEG1_LAYER_III ) {
            // sums of position and length of last MP3 errors are suppressed to avoid slowdowns
            if    ((g_precomp.ctx->suppress_mp3_big_value_pairs_sum != position_length_sum)
               && (g_precomp.ctx->suppress_mp3_non_zero_padbits_sum != position_length_sum)
               && (g_precomp.ctx->suppress_mp3_inconsistent_emphasis_sum != position_length_sum)
               && (g_precomp.ctx->suppress_mp3_inconsistent_original_bit != position_length_sum)) {
              tempfile += "mp3";
              PrecompTmpFile tmp_mp3;
              tmp_mp3.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
              try_decompression_mp3(mp3_length, tmp_mp3);
            }
          } else if (type > 0) {
            g_precomp.ctx->suppress_mp3_type_until[type] = position_length_sum;
            if (g_precomp.switches.DEBUG_MODE) {
              print_debug_percent();
              std::cout << "Unsupported MP3 type found at position " << g_precomp.ctx->saved_input_file_pos << ", length " << mp3_length << std::endl;
              printf ("Type: %s\n", filetype_description[type]);
            }
          }
        }

        if (!g_precomp.ctx->compressed_data_found) {
          g_precomp.ctx->input_file_pos = g_precomp.ctx->saved_input_file_pos;
          g_precomp.ctx->cb = g_precomp.ctx->saved_cb;
        }
      }
    }

    if ((!g_precomp.ctx->compressed_data_found) && (g_precomp.switches.use_swf)) { // no MP3 header -> SWF header?
      // CWS = Compressed SWF file
      if ((g_precomp.ctx->in_buf[g_precomp.ctx->cb] == 'C') && (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 1] == 'W') && (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 2] == 'S')) {
        // check zLib header
        if (((((g_precomp.ctx->in_buf[g_precomp.ctx->cb + 8] << 8) + g_precomp.ctx->in_buf[g_precomp.ctx->cb + 9]) % 31) == 0) &&
           ((g_precomp.ctx->in_buf[g_precomp.ctx->cb + 9] & 32) == 0)) { // FDICT must not be set
          int compression_method = (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 8] & 15);
          if (compression_method == 8) {
            int windowbits = (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 8] >> 4) + 8;

            g_precomp.ctx->saved_input_file_pos = g_precomp.ctx->input_file_pos;
            g_precomp.ctx->saved_cb = g_precomp.ctx->cb;

            g_precomp.ctx->input_file_pos += 10; // skip CWS and zLib header

            tempfile += "swf";
            PrecompTmpFile tmp_swf;
            tmp_swf.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
            try_decompression_swf(-windowbits, tmp_swf);

            g_precomp.ctx->cb += 10;

            if (!g_precomp.ctx->compressed_data_found) {
              g_precomp.ctx->input_file_pos = g_precomp.ctx->saved_input_file_pos;
              g_precomp.ctx->cb = g_precomp.ctx->saved_cb;
            }
          }
        }
      }
    }

    if ((!g_precomp.ctx->compressed_data_found) && (g_precomp.switches.use_base64)) { // no SWF header -> Base64?
    if ((g_precomp.ctx->in_buf[g_precomp.ctx->cb + 1] == 'o') && (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 2] == 'n') && (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 3] == 't') && (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 4] == 'e')) {
      unsigned char cte_detect[33];
      for (int i = 0; i < 33; i++) {
        cte_detect[i] = tolower(g_precomp.ctx->in_buf[g_precomp.ctx->cb + i]);
      }
      if (memcmp(cte_detect, "content-transfer-encoding: base64", 33) == 0) {
        // search for double CRLF, all between is "header"
        int base64_header_length = 33;
        bool found_double_crlf = false;
        do {
          if ((g_precomp.ctx->in_buf[g_precomp.ctx->cb + base64_header_length] == 13) && (g_precomp.ctx->in_buf[g_precomp.ctx->cb + base64_header_length + 1] == 10)) {
            if ((g_precomp.ctx->in_buf[g_precomp.ctx->cb + base64_header_length + 2] == 13) && (g_precomp.ctx->in_buf[g_precomp.ctx->cb + base64_header_length + 3] == 10)) {
              found_double_crlf = true;
              base64_header_length += 4;
              // skip additional CRLFs
              while ((g_precomp.ctx->in_buf[g_precomp.ctx->cb + base64_header_length] == 13) && (g_precomp.ctx->in_buf[g_precomp.ctx->cb + base64_header_length + 1] == 10)) {
                base64_header_length += 2;
              }
              break;
            }
          }
          base64_header_length++;
        } while (base64_header_length < (CHECKBUF_SIZE - 2));

        if (found_double_crlf) {

          g_precomp.ctx->saved_input_file_pos = g_precomp.ctx->input_file_pos;
          g_precomp.ctx->saved_cb = g_precomp.ctx->cb;

          g_precomp.ctx->input_file_pos += base64_header_length; // skip "header"

          tempfile += "base64";
          PrecompTmpFile tmp_base64;
          tmp_base64.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
          try_decompression_base64(base64_header_length, tmp_base64);

          g_precomp.ctx->cb += base64_header_length;

          if (!g_precomp.ctx->compressed_data_found) {
            g_precomp.ctx->input_file_pos = g_precomp.ctx->saved_input_file_pos;
            g_precomp.ctx->cb = g_precomp.ctx->saved_cb;
          }
        }
      }
    }
    }

    if ((!g_precomp.ctx->compressed_data_found) && (g_precomp.switches.use_bzip2)) { // no Base64 header -> bZip2?
      // BZhx = header, x = compression level/blocksize (1-9)
      if ((g_precomp.ctx->in_buf[g_precomp.ctx->cb] == 'B') && (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 1] == 'Z') && (g_precomp.ctx->in_buf[g_precomp.ctx->cb + 2] == 'h')) {
        int compression_level = g_precomp.ctx->in_buf[g_precomp.ctx->cb + 3] - '0';
        if ((compression_level >= 1) && (compression_level <= 9)) {
          g_precomp.ctx->saved_input_file_pos = g_precomp.ctx->input_file_pos;
          g_precomp.ctx->saved_cb = g_precomp.ctx->cb;

          tempfile += "bzip2";
          PrecompTmpFile tmp_bzip2;
          tmp_bzip2.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
          try_decompression_bzip2(compression_level, tmp_bzip2);

          if (!g_precomp.ctx->compressed_data_found) {
            g_precomp.ctx->input_file_pos = g_precomp.ctx->saved_input_file_pos;
            g_precomp.ctx->cb = g_precomp.ctx->saved_cb;
          }
        }
      }
    }


   // nothing so far -> if intense mode is active, look for raw zLib header
   if (intense_mode_is_active()) {
    if (!g_precomp.ctx->compressed_data_found) {
      bool ignore_this_position = false;
      if (g_precomp.ctx->intense_ignore_offsets->size() > 0) {
        auto first = g_precomp.ctx->intense_ignore_offsets->begin();
        while (*first < g_precomp.ctx->input_file_pos) {
          g_precomp.ctx->intense_ignore_offsets->erase(first);
          if (g_precomp.ctx->intense_ignore_offsets->size() == 0) break;
          first = g_precomp.ctx->intense_ignore_offsets->begin();
        }

        if (g_precomp.ctx->intense_ignore_offsets->size() > 0) {
          if (*first == g_precomp.ctx->input_file_pos) {
            ignore_this_position = true;
            g_precomp.ctx->intense_ignore_offsets->erase(first);
          }
        }
      }

      if (!ignore_this_position) {
        if (((((g_precomp.ctx->in_buf[g_precomp.ctx->cb] << 8) + g_precomp.ctx->in_buf[g_precomp.ctx->cb + 1]) % 31) == 0) &&
            ((g_precomp.ctx->in_buf[g_precomp.ctx->cb + 1] & 32) == 0)) { // FDICT must not be set
          int compression_method = (g_precomp.ctx->in_buf[g_precomp.ctx->cb] & 15);
          if (compression_method == 8) {
            int windowbits = (g_precomp.ctx->in_buf[g_precomp.ctx->cb] >> 4) + 8;

            if (check_inf_result(g_precomp.ctx->cb + 2, -windowbits)) {
              g_precomp.ctx->saved_input_file_pos = g_precomp.ctx->input_file_pos;
              g_precomp.ctx->saved_cb = g_precomp.ctx->cb;

              g_precomp.ctx->input_file_pos += 2; // skip zLib header

              tempfile += "zlib";
              PrecompTmpFile tmp_zlib;
              tmp_zlib.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
              try_decompression_zlib(-windowbits, tmp_zlib);

              g_precomp.ctx->cb += 2;

              if (!g_precomp.ctx->compressed_data_found) {
                g_precomp.ctx->input_file_pos = g_precomp.ctx->saved_input_file_pos;
                g_precomp.ctx->cb = g_precomp.ctx->saved_cb;
              }
            }
          }
        }
      }
    }
   }

   // nothing so far -> if brute mode is active, brute force for zLib streams
    if (brute_mode_is_active()) {
    if (!g_precomp.ctx->compressed_data_found) {
      bool ignore_this_position = false;
      if (g_precomp.ctx->brute_ignore_offsets->size() > 0) {
        auto first = g_precomp.ctx->brute_ignore_offsets->begin();
        while (*first < g_precomp.ctx->input_file_pos) {
          g_precomp.ctx->brute_ignore_offsets->erase(first);
          if (g_precomp.ctx->brute_ignore_offsets->size() == 0) break;
          first = g_precomp.ctx->brute_ignore_offsets->begin();
        }

        if (g_precomp.ctx->brute_ignore_offsets->size() > 0) {
          if (*first == g_precomp.ctx->input_file_pos) {
            ignore_this_position = true;
            g_precomp.ctx->brute_ignore_offsets->erase(first);
          }
        }
      }

      if (!ignore_this_position) {
        g_precomp.ctx->saved_input_file_pos = g_precomp.ctx->input_file_pos;
        g_precomp.ctx->saved_cb = g_precomp.ctx->cb;

        if (check_inf_result(g_precomp.ctx->cb, -15, true)) {
          tempfile += "brute";
          PrecompTmpFile tmp_brute;
          tmp_brute.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
          try_decompression_brute(tmp_brute);
        }

        if (!g_precomp.ctx->compressed_data_found) {
          g_precomp.ctx->input_file_pos = g_precomp.ctx->saved_input_file_pos;
          g_precomp.ctx->cb = g_precomp.ctx->saved_cb;
        }
      }
    }
   }

  }

    if (!g_precomp.ctx->compressed_data_found) {
      if (g_precomp.ctx->uncompressed_length == -1) {
        start_uncompressed_data();
      }
      g_precomp.ctx->uncompressed_length++;
      g_precomp.ctx->uncompressed_bytes_total++;
    }

  }

  end_uncompressed_data();

  denit_compress(tempfile);

  return (g_precomp.ctx->anything_was_used || g_precomp.ctx->non_zlib_was_used);
}

int BrunsliStringWriter(void* data, const uint8_t* buf, size_t count) {
	std::string* output = reinterpret_cast<std::string*>(data);
	output->append(reinterpret_cast<const char*>(buf), count);
	return count;
}

void decompress_file() {

  long long fin_pos;

  g_precomp.ctx->comp_decomp_state = P_DECOMPRESS;

  std::string tmp_tag = temp_files_tag();
  std::string tempfile_base = tmp_tag + "_recomp_";
  std::string tempfile2_base = tmp_tag + "_recomp2_";
  std::string tempfile;
  std::string tempfile2;
  
  if (g_precomp.ctx->compression_otf_method != OTF_NONE) {
    init_decompress_otf();
  }

  if (recursion_depth == 0) {
    if (!g_precomp.switches.DEBUG_MODE) show_progress(0, false, false);
    read_header();
  }

  fin_pos = g_precomp.ctx->fin.tellg();

while (fin_pos < g_precomp.ctx->fin_length) {
  tempfile = tempfile_base;
  tempfile2 = tempfile2_base;

  if ((recursion_depth == 0) && (!g_precomp.switches.DEBUG_MODE)) {
    float percent = (fin_pos / (float)g_precomp.ctx->fin_length) * 100;
    show_progress(percent, true, true);
  }

  unsigned char header1 = fin_fgetc();
  if (header1 == 0) { // uncompressed data
    long long uncompressed_data_length;
    uncompressed_data_length = fin_fget_vlint();

    if (uncompressed_data_length == 0) break; // end of PCF file, used by bZip2 compress-on-the-fly

    if (g_precomp.switches.DEBUG_MODE) {
    std::cout << "Uncompressed data, length=" << uncompressed_data_length << std::endl;
    }

    fast_copy(g_precomp.ctx->fin, g_precomp.ctx->fout, uncompressed_data_length);

  } else { // decompressed data, recompress

    unsigned char headertype = fin_fgetc();

    switch (headertype) {
    case D_PDF: { // PDF recompression
      recompress_deflate_result rdres;
      unsigned hdr_length;
      // restore PDF header
      ostream_printf(g_precomp.ctx->fout, "/FlateDecode");
      fin_fget_deflate_hdr(rdres, header1, in, hdr_length, false);
      fin_fget_recon_data(rdres);
      int bmp_c = (header1 >> 6);

      debug_deflate_reconstruct(rdres, "PDF", hdr_length, 0);
      if (g_precomp.switches.DEBUG_MODE) {
        if (bmp_c == 1) printf("Skipping BMP header (8-Bit)\n");
        if (bmp_c == 2) printf("Skipping BMP header (24-Bit)\n");
      }

      // read BMP header
      int bmp_width = 0;

      switch (bmp_c) {
        case 1:
          own_fread(in, 1, 54+1024, g_precomp.ctx->fin);
          break;
        case 2:
          own_fread(in, 1, 54, g_precomp.ctx->fin);
          break;
      }
      if (bmp_c > 0) {
        bmp_width = in[18] + (in[19] << 8) + (in[20] << 16) + (in[21] << 24);
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
      if (!try_reconstructing_deflate_skip(g_precomp.ctx->fin, g_precomp.ctx->fout, rdres, read_part, skip_part)) {
        printf("Error recompressing data!");
        exit(0);
      }
      break;
    }     
    case D_ZIP: { // ZIP recompression
      recompress_deflate_result rdres;
      unsigned hdr_length;
      int64_t recursion_data_length;
      g_precomp.ctx->fout.put('P');
      g_precomp.ctx->fout.put('K');
      g_precomp.ctx->fout.put(3);
      g_precomp.ctx->fout.put(4);
      tempfile += "zip";
      PrecompTmpFile tmp_zip;
      tmp_zip.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
      bool ok = fin_fget_deflate_rec(rdres, header1, in, hdr_length, false, recursion_data_length, tmp_zip);

      debug_deflate_reconstruct(rdres, "ZIP", hdr_length, recursion_data_length);

      if (!ok) {
        printf("Error recompressing data!");
        exit(0);
      }
      break;
    }
    case D_GZIP: { // GZip recompression
      recompress_deflate_result rdres;
      unsigned hdr_length;
      int64_t recursion_data_length;
      g_precomp.ctx->fout.put(31);
      g_precomp.ctx->fout.put(139);
      tempfile += "gzip";
      PrecompTmpFile tmp_gzip;
      tmp_gzip.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
      bool ok = fin_fget_deflate_rec(rdres, header1, in, hdr_length, false, recursion_data_length, tmp_gzip);

      debug_deflate_reconstruct(rdres, "GZIP", hdr_length, recursion_data_length);

      if (!ok) {
        printf("Error recompressing data!");
        exit(0);
      }
      break;
    }
    case D_PNG: { // PNG recompression
      recompress_deflate_result rdres;
      unsigned hdr_length;
      // restore IDAT
      ostream_printf(g_precomp.ctx->fout, "IDAT");

      fin_fget_deflate_hdr(rdres, header1, in, hdr_length, true);
      fin_fget_recon_data(rdres);
      debug_sums(rdres);
      debug_pos();

      debug_deflate_reconstruct(rdres, "PNG", hdr_length, 0);

      if (!try_reconstructing_deflate(g_precomp.ctx->fin, g_precomp.ctx->fout, rdres)) {
        printf("Error recompressing data!");
        exit(0);
      }
      debug_pos();
      break;
    }
    case D_MULTIPNG: { // PNG multi recompression
      recompress_deflate_result rdres;
      unsigned hdr_length;
      // restore first IDAT
      ostream_printf(g_precomp.ctx->fout, "IDAT");
      
      fin_fget_deflate_hdr(rdres, header1, in, hdr_length, true);

      // get IDAT count
      idat_count = fin_fget_vlint() + 1;

      idat_crcs = (unsigned int*)(realloc(idat_crcs, idat_count * sizeof(unsigned int)));
      idat_lengths = (unsigned int*)(realloc(idat_lengths, idat_count * sizeof(unsigned int)));

      // get first IDAT length
      idat_lengths[0] = fin_fget_vlint() - 2; // zLib header length

      // get IDAT chunk lengths and CRCs
      for (int i = 1; i < idat_count; i++) {
        idat_crcs[i]    = fin_fget32();
        idat_lengths[i] = fin_fget_vlint();
      }

      fin_fget_recon_data(rdres);
      debug_sums(rdres);
      debug_pos();

      debug_deflate_reconstruct(rdres, "PNG multi", hdr_length, 0);

      if (!try_reconstructing_deflate_multipng(g_precomp.ctx->fin, g_precomp.ctx->fout, rdres, idat_count, idat_crcs, idat_lengths)) {
        printf("Error recompressing data!");
        exit(0);
      }
      debug_pos();
      free(idat_lengths);
      idat_lengths = NULL;
      free(idat_crcs);
      idat_crcs = NULL;
      break;
    }
    case D_GIF: { // GIF recompression

      if (g_precomp.switches.DEBUG_MODE) {
      printf("Decompressed data - GIF\n");
      }

      unsigned char block_size = 255;

      bool penalty_bytes_stored = ((header1 & 2) == 2);
      if ((header1 & 4) == 4) block_size = 254;
      bool recompress_success_needed = ((header1 & 128) == 128);

      GifDiffStruct gDiff;

      // read diff bytes
      gDiff.GIFDiffIndex = fin_fget_vlint();
      gDiff.GIFDiff = (unsigned char*)malloc(gDiff.GIFDiffIndex * sizeof(unsigned char));
      own_fread(gDiff.GIFDiff, 1, gDiff.GIFDiffIndex, g_precomp.ctx->fin);
      if (g_precomp.switches.DEBUG_MODE) {
        printf("Diff bytes were used: %i bytes\n", gDiff.GIFDiffIndex);
      }
      gDiff.GIFDiffSize = gDiff.GIFDiffIndex;
      gDiff.GIFDiffIndex = 0;
      gDiff.GIFCodeCount = 0;

      // read penalty bytes
      if (penalty_bytes_stored) {
        g_precomp.ctx->penalty_bytes_len = fin_fget_vlint();
        own_fread(g_precomp.ctx->penalty_bytes.data(), 1, g_precomp.ctx->penalty_bytes_len, g_precomp.ctx->fin);
      }

      long long recompressed_data_length = fin_fget_vlint();
      long long decompressed_data_length = fin_fget_vlint();

      if (g_precomp.switches.DEBUG_MODE) {
      std::cout << "Recompressed length: " << recompressed_data_length << " - decompressed length: " << decompressed_data_length << std::endl;
      }

      tempfile += "gif";
      tempfile2 += "gif";
      remove(tempfile.c_str());
      {
        FileWrapper ftempout;
        ftempout.open(tempfile, std::ios_base::out | std::ios_base::binary);
        fast_copy(g_precomp.ctx->fin, ftempout, decompressed_data_length);
        ftempout.close();
      }

      bool recompress_success = false;

      {
        FileWrapper ftempout;
        ftempout.open(tempfile, std::ios_base::in | std::ios_base::binary);
        remove(tempfile2.c_str());
        FileWrapper frecomp;
        frecomp.open(tempfile2, std::ios_base::out | std::ios_base::binary);

        // recompress data
        recompress_success = recompress_gif(ftempout, frecomp, block_size, NULL, &gDiff);
        frecomp.close();
        ftempout.close();
      }

      if (recompress_success_needed) {
        if (!recompress_success) {
          printf("Error recompressing data!");
          GifDiffFree(&gDiff);
          exit(0);
        }
      }

      long long old_fout_pos = g_precomp.ctx->fout.tellp();

      {
        PrecompTmpFile frecomp;
        frecomp.open(tempfile2, std::ios_base::in | std::ios_base::binary);
        fast_copy(frecomp, g_precomp.ctx->fout, recompressed_data_length);
        frecomp.close();
      }

      remove(tempfile2.c_str());
      remove(tempfile.c_str());

      if (penalty_bytes_stored) {
        g_precomp.ctx->fout.flush();

        long long fsave_fout_pos = g_precomp.ctx->fout.tellp();

        int pb_pos = 0;
        for (int pbc = 0; pbc < g_precomp.ctx->penalty_bytes_len; pbc += 5) {
          pb_pos = ((unsigned char)g_precomp.ctx->penalty_bytes[pbc]) << 24;
          pb_pos += ((unsigned char)g_precomp.ctx->penalty_bytes[pbc + 1]) << 16;
          pb_pos += ((unsigned char)g_precomp.ctx->penalty_bytes[pbc + 2]) << 8;
          pb_pos += (unsigned char)g_precomp.ctx->penalty_bytes[pbc + 3];

          g_precomp.ctx->fout.seekp(old_fout_pos + pb_pos, SEEK_SET);
          own_fwrite(g_precomp.ctx->penalty_bytes.data() + pbc + 4, 1, 1, g_precomp.ctx->fout);
        }

        g_precomp.ctx->fout.seekp(fsave_fout_pos, SEEK_SET);
      }

      GifDiffFree(&gDiff);
      break;
    }
    case D_JPG: { // JPG recompression
      tempfile += "jpg";
      tempfile2 += "jpg";

      if (g_precomp.switches.DEBUG_MODE) {
      printf("Decompressed data - JPG\n");
      }

      bool mjpg_dht_used = ((header1 & 4) == 4);
	  bool brunsli_used = ((header1 & 8) == 8);
	  bool brotli_used = ((header1 & 16) == 16);

      long long recompressed_data_length = fin_fget_vlint();
      long long decompressed_data_length = fin_fget_vlint();

      if (g_precomp.switches.DEBUG_MODE) {
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

        fast_copy(g_precomp.ctx->fin, jpg_mem_in, decompressed_data_length);

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
          FileWrapper ftempout;
          ftempout.open(tempfile, std::ios_base::out | std::ios_base::binary);
          fast_copy(g_precomp.ctx->fin, ftempout, decompressed_data_length);
          ftempout.close();
        }

        remove(tempfile2.c_str());

        recompress_success = pjglib_convert_file2file(const_cast<char*>(tempfile.c_str()), const_cast<char*>(tempfile2.c_str()), recompress_msg);
      }

      if (!recompress_success) {
        if (g_precomp.switches.DEBUG_MODE) printf ("packJPG error: %s\n", recompress_msg);
        printf("Error recompressing data!");
        exit(1);
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
            frecomp.read(reinterpret_cast<char*>(in), 1);
            if (frecomp.gcount() != 1) break;
            if (found_ff) {
              found_ffda = (in[0] == 0xDA);
              if (found_ffda) break;
              found_ff = false;
            } else {
              found_ff = (in[0] == 0xFF);
            }
          } while (!found_ffda);
        }

        if ((!found_ffda) || ((ffda_pos - 1 - MJPGDHT_LEN) < 0)) {
          printf("ERROR: Motion JPG stream corrupted\n");
          exit(1);
        }

        // remove motion JPG huffman table
        if (in_memory) {
          fast_copy(jpg_mem_out, g_precomp.ctx->fout, ffda_pos - 1 - MJPGDHT_LEN);
          fast_copy(jpg_mem_out + (ffda_pos - 1), g_precomp.ctx->fout, (recompressed_data_length + MJPGDHT_LEN) - (ffda_pos - 1));
        } else {
          force_seekg(frecomp, frecomp_pos, SEEK_SET);
          fast_copy(frecomp, g_precomp.ctx->fout, ffda_pos - 1 - MJPGDHT_LEN);

          frecomp_pos += ffda_pos - 1;
          force_seekg(frecomp, frecomp_pos, SEEK_SET);
          fast_copy(frecomp, g_precomp.ctx->fout, (recompressed_data_length + MJPGDHT_LEN) - (ffda_pos - 1));
        }
      } else {
        if (in_memory) {
          fast_copy(jpg_mem_out, g_precomp.ctx->fout, recompressed_data_length);
        } else {
          fast_copy(frecomp, g_precomp.ctx->fout, recompressed_data_length);
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
      g_precomp.ctx->fout.put('C');
      g_precomp.ctx->fout.put('W');
      g_precomp.ctx->fout.put('S');
      tempfile += "swf";
      PrecompTmpFile tmp_swf;
      tmp_swf.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
      bool ok = fin_fget_deflate_rec(rdres, header1, in, hdr_length, true, recursion_data_length, tmp_swf);

      debug_deflate_reconstruct(rdres, "SWF", hdr_length, recursion_data_length);

      if (!ok) {
        printf("Error recompressing data!");
        exit(0);
      }
      break;
    }
    case D_BASE64: { // Base64 recompression
      tempfile += "base64";

      if (g_precomp.switches.DEBUG_MODE) {
      printf("Decompressed data - Base64\n");
      }

      int line_case = (header1 >> 2) & 3;
      bool recursion_used = ((header1 & 128) == 128);

      // restore Base64 "header"
      int base64_header_length = fin_fget_vlint();

      if (g_precomp.switches.DEBUG_MODE) {
        printf("Base64 header length: %i\n", base64_header_length);
      }
      own_fread(in, 1, base64_header_length, g_precomp.ctx->fin);
      g_precomp.ctx->fout.put(*(in)+1); // first char was decreased
      own_fwrite(in + 1, 1, base64_header_length - 1, g_precomp.ctx->fout);

      // read line length list
      int line_count = fin_fget_vlint();

      unsigned int* base64_line_len = new unsigned int[line_count];

      if (line_case == 2) {
        for (int i = 0; i < line_count; i++) {
          base64_line_len[i] = fin_fgetc();
        }
      } else {
        base64_line_len[0] = fin_fgetc();
        for (int i = 1; i < line_count; i++) {
          base64_line_len[i] = base64_line_len[0];
        }
        if (line_case == 1) base64_line_len[line_count - 1] = fin_fgetc();
      }

      long long recompressed_data_length = fin_fget_vlint();
      long long decompressed_data_length = fin_fget_vlint();

      long long recursion_data_length = 0;
      if (recursion_used) {
        recursion_data_length = fin_fget_vlint();
      }

      if (g_precomp.switches.DEBUG_MODE) {
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
        tmp_base64.close();
        recursion_result r = recursion_decompress(recursion_data_length, tmp_base64);
        base64_reencode(*r.frecurse, g_precomp.ctx->fout, line_count, base64_line_len, r.file_length, decompressed_data_length);
        r.frecurse->close();
        remove(r.file_name.c_str());
      } else {
        base64_reencode(g_precomp.ctx->fin, g_precomp.ctx->fout, line_count, base64_line_len, recompressed_data_length, decompressed_data_length);
      }

      delete[] base64_line_len;
      break;
    }
    case D_BZIP2: { // bZip2 recompression
      tempfile += "bzip2";

      if (g_precomp.switches.DEBUG_MODE) {
      printf("Decompressed data - bZip2\n");
      }

      unsigned char header2 = fin_fgetc();

      bool penalty_bytes_stored = ((header1 & 2) == 2);
      bool recursion_used = ((header1 & 128) == 128);
      int level = header2;

      if (g_precomp.switches.DEBUG_MODE) {
      printf("Compression level: %i\n", level);
      }

      // read penalty bytes
      if (penalty_bytes_stored) {
        g_precomp.ctx->penalty_bytes_len = fin_fget_vlint();
        own_fread(g_precomp.ctx->penalty_bytes.data(), 1, g_precomp.ctx->penalty_bytes_len, g_precomp.ctx->fin);
      }

      long long recompressed_data_length = fin_fget_vlint();
      long long decompressed_data_length = fin_fget_vlint();

      long long recursion_data_length = 0;
      if (recursion_used) {
        recursion_data_length = fin_fget_vlint();
      }

      if (g_precomp.switches.DEBUG_MODE) {
        if (recursion_used) {
          std::cout << "Recursion data length: " << recursion_data_length << std::endl;
        } else {
          std::cout << "Recompressed length: " << recompressed_data_length << " - decompressed length: " << decompressed_data_length << std::endl;
        }
      }

      long long old_fout_pos = g_precomp.ctx->fout.tellp();

      if (recursion_used) {
        PrecompTmpFile tmp_bzip2;
        tmp_bzip2.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
        tmp_bzip2.close();
        recursion_result r = recursion_decompress(recursion_data_length, tmp_bzip2);
        g_precomp.ctx->retval = def_part_bzip2(*r.frecurse, g_precomp.ctx->fout, level, decompressed_data_length, recompressed_data_length);
        r.frecurse->close();
        remove(r.file_name.c_str());
      } else {
        g_precomp.ctx->retval = def_part_bzip2(g_precomp.ctx->fin, g_precomp.ctx->fout, level, decompressed_data_length, recompressed_data_length);
      }

      if (g_precomp.ctx->retval != BZ_OK) {
        printf("Error recompressing data!");
        std::cout << "retval = " << g_precomp.ctx->retval << std::endl;
        exit(0);
      }

      if (penalty_bytes_stored) {
        g_precomp.ctx->fout.flush();

        long long fsave_fout_pos = g_precomp.ctx->fout.tellp();
        int pb_pos = 0;
        for (int pbc = 0; pbc < g_precomp.ctx->penalty_bytes_len; pbc += 5) {
          pb_pos = ((unsigned char)g_precomp.ctx->penalty_bytes[pbc]) << 24;
          pb_pos += ((unsigned char)g_precomp.ctx->penalty_bytes[pbc + 1]) << 16;
          pb_pos += ((unsigned char)g_precomp.ctx->penalty_bytes[pbc + 2]) << 8;
          pb_pos += (unsigned char)g_precomp.ctx->penalty_bytes[pbc + 3];

          g_precomp.ctx->fout.seekp(old_fout_pos + pb_pos, SEEK_SET);
          own_fwrite(g_precomp.ctx->penalty_bytes.data() + pbc + 4, 1, 1, g_precomp.ctx->fout);
        }

        g_precomp.ctx->fout.seekp(fsave_fout_pos, SEEK_SET);
      }
      break;
    }
    case D_MP3: { // MP3 recompression
      tempfile += "mp3";
      tempfile2 += "mp3";

      if (g_precomp.switches.DEBUG_MODE) {
      printf("Decompressed data - MP3\n");
      }

      long long recompressed_data_length = fin_fget_vlint();
      long long decompressed_data_length = fin_fget_vlint();

      if (g_precomp.switches.DEBUG_MODE) {
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

        fast_copy(g_precomp.ctx->fin, mp3_mem_in, decompressed_data_length);

        pmplib_init_streams(mp3_mem_in, 1, decompressed_data_length, mp3_mem_out, 1);
        recompress_success = pmplib_convert_stream2mem(&mp3_mem_out, &mp3_mem_out_size, recompress_msg);
      } else {
        remove(tempfile.c_str());

        {
          FileWrapper ftempout;
          ftempout.open(tempfile, std::ios_base::out | std::ios_base::binary);
          fast_copy(g_precomp.ctx->fin, ftempout, decompressed_data_length);
          ftempout.close();
        }

        remove(tempfile2.c_str());

        recompress_success = pmplib_convert_file2file(const_cast<char*>(tempfile.c_str()), const_cast<char*>(tempfile2.c_str()), recompress_msg);
      }

      if (!recompress_success) {
        if (g_precomp.switches.DEBUG_MODE) printf ("packMP3 error: %s\n", recompress_msg);
        printf("Error recompressing data!");
        exit(1);
      }

      if (in_memory) {
        fast_copy(mp3_mem_out, g_precomp.ctx->fout, recompressed_data_length);

        if (mp3_mem_in != NULL) delete[] mp3_mem_in;
        if (mp3_mem_out != NULL) delete[] mp3_mem_out;
      } else {
        {
          PrecompTmpFile frecomp;
          frecomp.open(tempfile2, std::ios_base::in | std::ios_base::binary);
          fast_copy(frecomp, g_precomp.ctx->fout, recompressed_data_length);
          frecomp.close();
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
      bool ok = fin_fget_deflate_rec(rdres, header1, in, hdr_length, false, recursion_data_length, tmp_brute);

      debug_deflate_reconstruct(rdres, "brute mode", hdr_length, recursion_data_length);

      if (!ok) {
        printf("Error recompressing data!");
        exit(0);
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
      bool ok = fin_fget_deflate_rec(rdres, header1, in, hdr_length, true, recursion_data_length, tmp_zlib);

      debug_deflate_reconstruct(rdres, "raw zLib", hdr_length, recursion_data_length);

      if (!ok) {
        printf("Error recompressing data!");
        exit(0);
      }
      break;
    }
    default:
      printf("ERROR: Unsupported stream type %i\n", headertype);
      exit(0);
    }

  }

  fin_pos = g_precomp.ctx->fin.tellg();
  if (g_precomp.ctx->compression_otf_method != OTF_NONE) {
    if (g_precomp.ctx->decompress_otf_end) break;
    if (fin_pos >= g_precomp.ctx->fin_length) fin_pos = g_precomp.ctx->fin_length - 1;
  }
}

  denit_decompress(tempfile);
}

void convert_file() {
  int bytes_read;
  unsigned char convbuf[COPY_BUF_SIZE];
  int conv_bytes = -1;

  g_precomp.ctx->comp_decomp_state = P_CONVERT;

  init_compress_otf();
  init_decompress_otf();

  if (!g_precomp.switches.DEBUG_MODE) show_progress(0, false, false);

  for (;;) {
    bytes_read = own_fread(copybuf, 1, COPY_BUF_SIZE, g_precomp.ctx->fin);
    // truncate by 9 bytes (Precomp on-the-fly delimiter) if converting from compressed data
    if ((conversion_from_method > OTF_NONE) && (bytes_read < COPY_BUF_SIZE)) {
      bytes_read -= 9;
      if (bytes_read < 0) {
        conv_bytes += bytes_read;
        bytes_read = 0;
      }
    }
    if (conv_bytes > -1) own_fwrite(convbuf, 1, conv_bytes, g_precomp.ctx->fout);
    for (int i = 0; i < bytes_read; i++) {
      convbuf[i] = copybuf[i];
    }
    conv_bytes = bytes_read;
    if (bytes_read < COPY_BUF_SIZE) {
      break;
    }

    g_precomp.ctx->input_file_pos = g_precomp.ctx->fin.tellg();
    print_work_sign(true);
    if (!g_precomp.switches.DEBUG_MODE) {
      float percent = (g_precomp.ctx->input_file_pos / (float)g_precomp.ctx->fin_length) * 100;
      show_progress(percent, true, true);
    }
  }
  own_fwrite(convbuf, 1, conv_bytes, g_precomp.ctx->fout);

  denit_compress_otf();
  denit_decompress_otf();

  denit_convert();
}

long long try_to_decompress_bzip2(std::istream& file, int compression_level, long long& compressed_stream_size, PrecompTmpFile& tmpfile) {
  long long r, decompressed_stream_size;

  print_work_sign(true);

  remove(tmpfile.file_path.c_str());
  FileWrapper ftempout;
  ftempout.open(tmpfile.file_path, std::ios_base::out | std::ios_base::binary);

  force_seekg(file, &file == &g_precomp.ctx->fin ? g_precomp.ctx->input_file_pos : 0, SEEK_SET);

  r = inf_bzip2(file, ftempout, compressed_stream_size, decompressed_stream_size);
  ftempout.close();
  if (r == BZ_OK) return decompressed_stream_size;

  return r;
}

void try_recompress_bzip2(std::istream& origfile, int level, long long& compressed_stream_size, PrecompTmpFile& tmpfile) {
            print_work_sign(true);

            long long decomp_bytes_total;
            g_precomp.ctx->identical_bytes = file_recompress_bzip2(origfile, level, g_precomp.ctx->identical_bytes_decomp, decomp_bytes_total, tmpfile);
            if (g_precomp.ctx->identical_bytes > -1) { // successfully recompressed?
              if ((g_precomp.ctx->identical_bytes > g_precomp.ctx->best_identical_bytes)  || ((g_precomp.ctx->identical_bytes == g_precomp.ctx->best_identical_bytes) && (g_precomp.ctx->penalty_bytes_len < g_precomp.ctx->best_penalty_bytes_len))) {
                if (g_precomp.ctx->identical_bytes > g_precomp.switches.min_ident_size) {
                  if (g_precomp.switches.DEBUG_MODE) {
                  std::cout << "Identical recompressed bytes: " << g_precomp.ctx->identical_bytes << " of " << compressed_stream_size << std::endl;
                  std::cout << "Identical decompressed bytes: " << g_precomp.ctx->identical_bytes_decomp << " of " << decomp_bytes_total << std::endl;
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

				float partial_ratio = ((float)g_precomp.ctx->identical_bytes_decomp / decomp_bytes_total) / ((float)g_precomp.ctx->identical_bytes / compressed_stream_size);
				if (partial_ratio < 3.0f) {
                    g_precomp.ctx->best_identical_bytes_decomp = g_precomp.ctx->identical_bytes_decomp;
                    g_precomp.ctx->best_identical_bytes = g_precomp.ctx->identical_bytes;
					if (g_precomp.ctx->penalty_bytes_len > 0) {
						memcpy(g_precomp.ctx->best_penalty_bytes.data(), g_precomp.ctx->penalty_bytes.data(), g_precomp.ctx->penalty_bytes_len);
            g_precomp.ctx->best_penalty_bytes_len = g_precomp.ctx->penalty_bytes_len;
					}
					else {
            g_precomp.ctx->best_penalty_bytes_len = 0;
					}
				} else {
					if (g_precomp.switches.DEBUG_MODE) {
					printf("Not enough identical recompressed bytes\n");
					}
				}
              }
            }
}


void write_header() {
  char* input_file_name_without_path = new char[g_precomp.ctx->input_file_name.length() + 1];

  ostream_printf(g_precomp.ctx->fout, "PCF");

  // version number
  g_precomp.ctx->fout.put(V_MAJOR);
  g_precomp.ctx->fout.put(V_MINOR);
  g_precomp.ctx->fout.put(V_MINOR2);

  // compression-on-the-fly method used
  g_precomp.ctx->fout.put(g_precomp.ctx->compression_otf_method);

  // write input file name without path
  const char* last_backslash = strrchr(g_precomp.ctx->input_file_name.c_str(), PATH_DELIM);
  if (last_backslash != NULL) {
    strcpy(input_file_name_without_path, last_backslash + 1);
  } else {
    strcpy(input_file_name_without_path, g_precomp.ctx->input_file_name.c_str());
  }

  ostream_printf(g_precomp.ctx->fout, input_file_name_without_path);
  g_precomp.ctx->fout.put(0);

  delete[] input_file_name_without_path;

  // initialize compression-on-the-fly now
  if (g_precomp.ctx->compression_otf_method != OTF_NONE) {
    init_compress_otf();
  }
}

#ifdef COMFORT
bool check_for_pcf_file() {
  force_seekg(g_precomp.ctx->fin, 0, SEEK_SET);

  g_precomp.ctx->fin.read(reinterpret_cast<char*>(in), 3);
  if ((in[0] == 'P') && (in[1] == 'C') && (in[2] == 'F')) {
  } else {
    return false;
  }

  g_precomp.ctx->fin.read(reinterpret_cast<char*>(in), 3);
  if ((in[0] == V_MAJOR) && (in[1] == V_MINOR) && (in[2] == V_MINOR2)) {
  } else {
    printf("Input file %s was made with a different Precomp version\n", g_precomp.ctx->input_file_name.c_str());
    printf("PCF version info: %i.%i.%i\n", in[0], in[1], in[2]);
    exit(1);
  }

  // skip compression method
  g_precomp.ctx->fin.read(reinterpret_cast<char*>(in), 1);

  std::string header_filename = "";
  char c;
  do {
    c = g_precomp.ctx->fin.get();
    if (c != 0) header_filename += c;
  } while (c != 0);

  // append output filename to the executable directory
  char exec_dir[1024];
  GetModuleFileName(NULL, exec_dir, 1024);
  // truncate to get directory of executable only
  char* lastslash = strrchr(exec_dir, PATH_DELIM) + 1;
  strcpy(lastslash, "");
  header_filename = exec_dir + header_filename;

  if (g_precomp.ctx->output_file_name.empty()) {
    g_precomp.ctx->output_file_name = header_filename;
  }

  return true;
}
#endif

void read_header() {
  force_seekg(g_precomp.ctx->fin, 0, SEEK_SET);

  g_precomp.ctx->fin.read(reinterpret_cast<char*>(in), 3);
  if ((in[0] == 'P') && (in[1] == 'C') && (in[2] == 'F')) {
  } else {
    printf("Input file %s has no valid PCF header\n", g_precomp.ctx->input_file_name.c_str());
    exit(1);
  }

  g_precomp.ctx->fin.read(reinterpret_cast<char*>(in), 3);
  if ((in[0] == V_MAJOR) && (in[1] == V_MINOR) && (in[2] == V_MINOR2)) {
  } else {
    printf("Input file %s was made with a different Precomp version\n", g_precomp.ctx->input_file_name.c_str());
    printf("PCF version info: %i.%i.%i\n", in[0], in[1], in[2]);
    exit(1);
  }

  g_precomp.ctx->fin.read(reinterpret_cast<char*>(in), 1);
  g_precomp.ctx->compression_otf_method = in[0];

  std::string header_filename = "";
  char c;
  do {
    c = g_precomp.ctx->fin.get();
    if (c != 0) header_filename += c;
  } while (c != 0);

  if (g_precomp.ctx->output_file_name.empty()) {
    g_precomp.ctx->output_file_name = header_filename;
  }
}

void convert_header() {
  force_seekg(g_precomp.ctx->fin, 0, SEEK_SET);

  g_precomp.ctx->fin.read(reinterpret_cast<char*>(in), 3);
  if ((in[0] == 'P') && (in[1] == 'C') && (in[2] == 'F')) {
  } else {
    printf("Input file %s has no valid PCF header\n", g_precomp.ctx->input_file_name.c_str());
    exit(1);
  }
  g_precomp.ctx->fout.write(reinterpret_cast<char*>(in), 3);

  g_precomp.ctx->fin.read(reinterpret_cast<char*>(in), 3);
  if ((in[0] == V_MAJOR) && (in[1] == V_MINOR) && (in[2] == V_MINOR2)) {
  } else {
    printf("Input file %s was made with a different Precomp version\n", g_precomp.ctx->input_file_name);
    printf("PCF version info: %i.%i.%i\n", in[0], in[1], in[2]);
    exit(1);
  }
  g_precomp.ctx->fout.write(reinterpret_cast<char*>(in), 3);

  g_precomp.ctx->fin.read(reinterpret_cast<char*>(in), 1);
  conversion_from_method = in[0];
  if (conversion_from_method == conversion_to_method) {
    printf("Input file doesn't need to be converted\n");
    exit(1);
  }
  in[0] = conversion_to_method;
  g_precomp.ctx->fout.write(reinterpret_cast<char*>(in), 1);

  std::string header_filename = "";
  char c;
  do {
    c = g_precomp.ctx->fin.get();
    if (c != 0) header_filename += c;
  } while (c != 0);
  ostream_printf(g_precomp.ctx->fout, header_filename);
  g_precomp.ctx->fout.put(0);
}

void progress_update(long long bytes_written) {
  float percent = ((g_precomp.ctx->input_file_pos + g_precomp.ctx->uncompressed_bytes_written + bytes_written) / ((float)g_precomp.ctx->fin_length + g_precomp.ctx->uncompressed_bytes_total)) * (g_precomp.ctx->global_max_percent - g_precomp.ctx->global_min_percent) + g_precomp.ctx->global_min_percent;
  show_progress(percent, true, true);
}

void lzma_progress_update() {
  float percent = ((g_precomp.ctx->input_file_pos + g_precomp.ctx->uncompressed_bytes_written) / ((float)g_precomp.ctx->fin_length + g_precomp.ctx->uncompressed_bytes_total)) * (g_precomp.ctx->global_max_percent - g_precomp.ctx->global_min_percent) + g_precomp.ctx->global_min_percent;

  uint64_t progress_in = 0, progress_out = 0;

  lzma_get_progress(&otf_xz_stream_c, &progress_in, &progress_out);

  lzma_mib_total = otf_xz_stream_c.total_in / (1024 * 1024);
  lzma_mib_written = progress_in / (1024 * 1024);
  show_progress(percent, true, true);
}

void fast_copy(std::istream& file1, std::ostream& file2, long long bytecount, bool update_progress) {
  if (bytecount == 0) return;

  long long i;
  int remaining_bytes = (bytecount % COPY_BUF_SIZE);
  long long maxi = (bytecount / COPY_BUF_SIZE);

  for (i = 1; i <= maxi; i++) {
    own_fread(copybuf, 1, COPY_BUF_SIZE, file1);
    own_fwrite(copybuf, 1, COPY_BUF_SIZE, file2, false, update_progress);

    if (((i - 1) % FAST_COPY_WORK_SIGN_DIST) == 0) {
      print_work_sign(true);
      if ((update_progress) && (!g_precomp.switches.DEBUG_MODE)) progress_update(i * COPY_BUF_SIZE);
    }
  }
  if (remaining_bytes != 0) {
    own_fread(copybuf, 1, remaining_bytes, file1);
    own_fwrite(copybuf, 1, remaining_bytes, file2, false, update_progress);
  }

  if ((update_progress) && (!g_precomp.switches.DEBUG_MODE)) g_precomp.ctx->uncompressed_bytes_written += bytecount;
}

void fast_copy(std::istream& file, unsigned char* mem, long long bytecount) {
    if (bytecount == 0) return;

  long long i;
  int remaining_bytes = (bytecount % COPY_BUF_SIZE);
  long long maxi = (bytecount / COPY_BUF_SIZE);

  for (i = 1; i <= maxi; i++) {
    own_fread(mem + (i - 1) * COPY_BUF_SIZE, 1, COPY_BUF_SIZE, file);

    if (((i - 1) % FAST_COPY_WORK_SIGN_DIST) == 0) print_work_sign(true);
  }
  if (remaining_bytes != 0) {
    own_fread(mem + maxi * COPY_BUF_SIZE, 1, remaining_bytes, file);
  }
}

void fast_copy(unsigned char* mem, std::ostream& file, long long bytecount) {
    if (bytecount == 0) return;

  long long i;
  int remaining_bytes = (bytecount % COPY_BUF_SIZE);
  long long maxi = (bytecount / COPY_BUF_SIZE);

  for (i = 1; i <= maxi; i++) {
    own_fwrite(mem + (i - 1) * COPY_BUF_SIZE, 1, COPY_BUF_SIZE, file);

    if (((i - 1) % FAST_COPY_WORK_SIGN_DIST) == 0) print_work_sign(true);
  }
  if (remaining_bytes != 0) {
    own_fwrite(mem + maxi * COPY_BUF_SIZE, 1, remaining_bytes, file);
  }
}

void own_fwrite(const void *ptr, size_t size, size_t count, std::ostream& stream, bool final_byte, bool update_lzma_progress) {
  bool use_otf = false;

  if (g_precomp.ctx->comp_decomp_state == P_CONVERT) {
    use_otf = (conversion_to_method > OTF_NONE);
    if (use_otf) g_precomp.ctx->compression_otf_method = conversion_to_method;
  } else {
    if ((&stream != &g_precomp.ctx->fout) || (g_precomp.ctx->compression_otf_method == OTF_NONE) || (g_precomp.ctx->comp_decomp_state != P_COMPRESS)) {
      use_otf = false;
    } else {
      use_otf = true;
    }
  }

  if (!use_otf) {
    stream.write(reinterpret_cast<const char*>(ptr), size * count);
    if (stream.bad()) {
      error(ERR_DISK_FULL);
    }
  } else {
    switch (g_precomp.ctx->compression_otf_method) {
      case OTF_BZIP2: { // bZip2
        int flush, ret;
        unsigned have;

        print_work_sign(true);

        flush = final_byte ? BZ_FINISH : BZ_RUN;

        otf_bz2_stream_c.avail_in = size * count;
        otf_bz2_stream_c.next_in = (char*)ptr;
        do {
          otf_bz2_stream_c.avail_out = CHUNK;
          otf_bz2_stream_c.next_out = (char*)otf_out;
          ret = BZ2_bzCompress(&otf_bz2_stream_c, flush);
          have = CHUNK - otf_bz2_stream_c.avail_out;
          stream.write(reinterpret_cast<char*>(otf_out), have);
          if (stream.bad()) {
            error(ERR_DISK_FULL);
          }
        } while (otf_bz2_stream_c.avail_out == 0);
        if (ret < 0) {
          printf("ERROR: bZip2 compression failed - return value %i\n", ret);
          exit(1);
        }
        break;
      }
      case OTF_XZ_MT: {
        lzma_action action = final_byte ? LZMA_FINISH : LZMA_RUN;
        lzma_ret ret;
        unsigned have;

        otf_xz_stream_c.avail_in = size * count;
        otf_xz_stream_c.next_in = (uint8_t *)ptr;
        do {
          print_work_sign(true);
          otf_xz_stream_c.avail_out = CHUNK;
          otf_xz_stream_c.next_out = (uint8_t *)otf_out;
          ret = lzma_code(&otf_xz_stream_c, action);
          have = CHUNK - otf_xz_stream_c.avail_out;
          stream.write(reinterpret_cast<char*>(otf_out), have);
          if (stream.bad()) {
            error(ERR_DISK_FULL);
          }
          if (ret != LZMA_OK && ret != LZMA_STREAM_END) {
            const char *msg;
            switch (ret) {
            case LZMA_MEM_ERROR:
              msg = "Memory allocation failed";
              break;

            case LZMA_DATA_ERROR:
              msg = "File size limits exceeded";
              break;

            default:
              msg = "Unknown error, possibly a bug";
              break;
            }

            printf("ERROR: liblzma error: %s (error code %u)\n", msg, ret);
#ifdef COMFORT
            wait_for_key();
#endif // COMFORT
            exit(1);
          } // .avail_out == 0
          if ((!g_precomp.switches.DEBUG_MODE) && (update_lzma_progress)) lzma_progress_update();
        } while ((otf_xz_stream_c.avail_in > 0) || (final_byte && (ret != LZMA_STREAM_END)));
        break;
      }
    }
  }
}

size_t own_fread(void *ptr, size_t size, size_t count, std::istream& stream) {
  bool use_otf = false;

  if (g_precomp.ctx->comp_decomp_state == P_CONVERT) {
    use_otf = (conversion_from_method > OTF_NONE);
    if (use_otf) g_precomp.ctx->compression_otf_method = conversion_from_method;
  } else {
    if ((&stream != &g_precomp.ctx->fin) || (g_precomp.ctx->compression_otf_method == OTF_NONE) || (g_precomp.ctx->comp_decomp_state != P_DECOMPRESS)) {
      use_otf = false;
    } else {
      use_otf = true;
    }
  }

  if (!use_otf) {
    stream.read(reinterpret_cast<char*>(ptr), size * count);
    return stream.gcount();
  } else {
    switch (g_precomp.ctx->compression_otf_method) {
      case 1: { // bZip2
        int ret;
        int bytes_read = 0;

        print_work_sign(true);

        otf_bz2_stream_d.avail_out = size * count;
        otf_bz2_stream_d.next_out = (char*)ptr;

        do {

          if (otf_bz2_stream_d.avail_in == 0) {
            g_precomp.ctx->fin.read(reinterpret_cast<char*>(otf_in), CHUNK);
            otf_bz2_stream_d.avail_in = g_precomp.ctx->fin.gcount();
            otf_bz2_stream_d.next_in = (char*)otf_in;
            if (otf_bz2_stream_d.avail_in == 0) break;
          }

          ret = BZ2_bzDecompress(&otf_bz2_stream_d);
          if ((ret != BZ_OK) && (ret != BZ_STREAM_END)) {
            (void)BZ2_bzDecompressEnd(&otf_bz2_stream_d);
            printf("ERROR: bZip2 stream corrupted - return value %i\n", ret);
            exit(1);
          }

          if (ret == BZ_STREAM_END) g_precomp.ctx->decompress_otf_end = true;

        } while (otf_bz2_stream_d.avail_out > 0);

        bytes_read = (size * count - otf_bz2_stream_d.avail_out);

        return bytes_read;
      }
      case OTF_XZ_MT: {
        lzma_action action = LZMA_RUN;
        lzma_ret ret;

        otf_xz_stream_d.avail_out = size * count;
        otf_xz_stream_d.next_out = (uint8_t *)ptr;

        do {
          print_work_sign(true);
          if ((otf_xz_stream_d.avail_in == 0) && !g_precomp.ctx->fin.eof()) {
            otf_xz_stream_d.next_in = (uint8_t *)otf_in;
            g_precomp.ctx->fin.read(reinterpret_cast<char*>(otf_in), CHUNK);
            otf_xz_stream_d.avail_in = g_precomp.ctx->fin.gcount();

            if (g_precomp.ctx->fin.bad()) {
              printf("ERROR: Could not read input file\n");
              exit(1);
            }
          }

          ret = lzma_code(&otf_xz_stream_d, action);

          if (ret == LZMA_STREAM_END) {
            g_precomp.ctx->decompress_otf_end = true;
            break;
          }

          if (ret != LZMA_OK) {
            const char *msg;
            switch (ret) {
            case LZMA_MEM_ERROR:
              msg = "Memory allocation failed";
              break;
            case LZMA_FORMAT_ERROR:
              msg = "Wrong file format";
              break;
            case LZMA_OPTIONS_ERROR:
              msg = "Unsupported compression options";
              break;
            case LZMA_DATA_ERROR:
            case LZMA_BUF_ERROR:
              msg = "Compressed file is corrupt";
              break;
            default:
              msg = "Unknown error, possibly a bug";
              break;
            }

            printf("ERROR: liblzma error: %s (error code %u)\n", msg, ret);
#ifdef COMFORT
            wait_for_key();
#endif // COMFORT
            exit(1);
          }
        } while (otf_xz_stream_d.avail_out > 0);

        return size * count - otf_xz_stream_d.avail_out;
      }
    }
  }

  return 0;
}

bool file_exists(const char* filename) {
  std::fstream fin;
  bool retval = false;

  fin.open(filename, std::ios::in);
  retval = fin.is_open();
  fin.close();

  return retval;
}

long long compare_files_penalty(std::istream& file1, std::istream& file2, long long pos1, long long pos2) {
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
  if (&file1 == &g_precomp.ctx->fin) {
    force_seekg(file2, 0, SEEK_END);
    compare_end = file2.tellg();
  } else {
    force_seekg(file1, 0, SEEK_END);
    force_seekg(file2, 0, SEEK_END);
    compare_end = std::min(file1.tellg(), file2.tellg());
  }

  force_seekg(file1, pos1, SEEK_SET);
  force_seekg(file2, pos2, SEEK_SET);

  do {
    print_work_sign(true);

    size1 = own_fread(input_bytes1, 1, COMP_CHUNK, file1);
    size2 = own_fread(input_bytes2, 1, COMP_CHUNK, file2);

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
        g_precomp.ctx->local_penalty_bytes[local_penalty_bytes_len-5] = (same_byte_count >> 24) % 256;
        g_precomp.ctx->local_penalty_bytes[local_penalty_bytes_len-4] = (same_byte_count >> 16) % 256;
        g_precomp.ctx->local_penalty_bytes[local_penalty_bytes_len-3] = (same_byte_count >> 8) % 256;
        g_precomp.ctx->local_penalty_bytes[local_penalty_bytes_len-2] = same_byte_count % 256;
        // new byte
        g_precomp.ctx->local_penalty_bytes[local_penalty_bytes_len-1] = input_bytes1[i];
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
    memcpy(g_precomp.ctx->penalty_bytes.data(), g_precomp.ctx->local_penalty_bytes.data(), rek_penalty_bytes_len);
    g_precomp.ctx->penalty_bytes_len = rek_penalty_bytes_len;
  } else {
    g_precomp.ctx->penalty_bytes_len = 0;
  }

  return rek_same_byte_count;
}

void try_decompression_gzip(int gzip_header_length, PrecompTmpFile& tmpfile) {
  try_decompression_deflate_type(g_precomp.statistics.decompressed_gzip_count, g_precomp.statistics.recompressed_gzip_count, 
                                 D_GZIP, g_precomp.ctx->in_buf + g_precomp.ctx->cb + 2, gzip_header_length - 2, false,
                                 "in GZIP", tmpfile);
}

void try_decompression_png (int windowbits, PrecompTmpFile& tmpfile) {
  init_decompression_variables();

  // try to decompress at current position
  tmpfile.close();
  recompress_deflate_result rdres = try_recompression_deflate(g_precomp.ctx->fin, tmpfile.file_path);

  if (rdres.uncompressed_stream_size > 0) { // seems to be a zLib-Stream

    g_precomp.statistics.decompressed_streams_count++;
    g_precomp.statistics.decompressed_png_count++;

    debug_deflate_detected(rdres, "in PNG");

    if (rdres.accepted) {
      g_precomp.statistics.recompressed_streams_count++;
      g_precomp.statistics.recompressed_png_count++;

      g_precomp.ctx->non_zlib_was_used = true;

      debug_sums(rdres);

      // end uncompressed data
      g_precomp.ctx->compressed_data_found = true;
      end_uncompressed_data();

      debug_pos();

      // write compressed data header (PNG)
      fout_fput_deflate_hdr(D_PNG, 0, rdres, zlib_header, 2, true);

      // write reconstruction and decompressed data
      fout_fput_recon_data(rdres);
      tmpfile.reopen();
      fout_fput_uncompressed(rdres, tmpfile);

      debug_pos();
      // set input file pointer after recompressed data
      g_precomp.ctx->input_file_pos += rdres.compressed_stream_size - 1;
      g_precomp.ctx->cb += rdres.compressed_stream_size - 1;

    } else {
      if (intense_mode_is_active()) g_precomp.ctx->intense_ignore_offsets->insert(g_precomp.ctx->input_file_pos - 2);
      if (brute_mode_is_active()) g_precomp.ctx->brute_ignore_offsets->insert(g_precomp.ctx->input_file_pos);
      if (g_precomp.switches.DEBUG_MODE) {
        printf("No matches\n");
      }
    }
  }
}

void try_decompression_png_multi(std::istream& fpng, int windowbits, PrecompTmpFile& tmpfile) {
  init_decompression_variables();

  // try to decompress at current position
  tmpfile.close();
  recompress_deflate_result rdres = try_recompression_deflate(fpng, tmpfile.file_path);

  if (rdres.uncompressed_stream_size > 0) { // seems to be a zLib-Stream

    g_precomp.statistics.decompressed_streams_count++;
    g_precomp.statistics.decompressed_png_multi_count++;

    debug_deflate_detected(rdres, "in multiPNG");

    if (rdres.accepted) {
      g_precomp.statistics.recompressed_streams_count++;
      g_precomp.statistics.recompressed_png_multi_count++;

      g_precomp.ctx->non_zlib_was_used = true;

      debug_sums(rdres);

      // end uncompressed data
      g_precomp.ctx->compressed_data_found = true;
      end_uncompressed_data();

      debug_pos();

      // write compressed data header (PNG)
      fout_fput_deflate_hdr(D_MULTIPNG, 0, rdres, zlib_header, 2, true);

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
      fout_fput_vlint(idat_pairs_written_count);

      // store IDAT CRCs and lengths
      fout_fput_vlint(idat_lengths[0]);

      // store IDAT CRCs and lengths
      i = 1;
      idat_pos = idat_lengths[0] - 2;
      idat_pairs_written_count = 0;
      if (idat_pos < rdres.compressed_stream_size) {
        do {
          fout_fput32(idat_crcs[i]);
          fout_fput_vlint(idat_lengths[i]);

          idat_pairs_written_count++;

          idat_pos += idat_lengths[i];
          if (idat_pos >= rdres.compressed_stream_size) break;

          i++;
        } while (i < idat_count);
      }

      // write reconstruction and decompressed data
      fout_fput_recon_data(rdres);
      tmpfile.reopen();
      fout_fput_uncompressed(rdres, tmpfile);

      debug_pos();

      // set input file pointer after recompressed data
      g_precomp.ctx->input_file_pos += rdres.compressed_stream_size - 1;
      g_precomp.ctx->cb += rdres.compressed_stream_size - 1;
      // now add IDAT chunk overhead
      g_precomp.ctx->input_file_pos += (idat_pairs_written_count * 12);
      g_precomp.ctx->cb += (idat_pairs_written_count * 12);

    } else {
      if (intense_mode_is_active()) g_precomp.ctx->intense_ignore_offsets->insert(g_precomp.ctx->input_file_pos - 2);
      if (brute_mode_is_active()) g_precomp.ctx->brute_ignore_offsets->insert(g_precomp.ctx->input_file_pos);
      if (g_precomp.switches.DEBUG_MODE) {
        printf("No matches\n");
      }
    }
  }
}

// GIF functions

bool newgif_may_write;
std::ostream* frecompress_gif = NULL;
std::istream* freadfunc = NULL;

int readFunc(GifFileType* GifFile, GifByteType* buf, int count)
{
  return own_fread(buf, 1, count, *freadfunc);
}

int writeFunc(GifFileType* GifFile, const GifByteType* buf, int count)
{
  if (newgif_may_write) {
    own_fwrite(buf, 1, count, *frecompress_gif);
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

bool recompress_gif(std::istream& srcfile, std::ostream& dstfile, unsigned char block_size, GifCodeStruct* g, GifDiffStruct* gd) {
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
            force_seekg(srcfile, init_src_pos, SEEK_SET);
            fast_copy(srcfile, dstfile, src_pos - init_src_pos);
            force_seekg(srcfile, src_pos, SEEK_SET);

            long long dstfile_pos = dstfile.tellp();
            dstfile.seekp(0, SEEK_SET);
            // change PGF8xa to GIF8xa
            dstfile.put('G');
            dstfile.put('I');
            dstfile.seekp(dstfile_pos, SEEK_SET);
          } else {
            force_seekg(srcfile, last_pos, SEEK_SET);
            fast_copy(srcfile, dstfile, src_pos - last_pos);
            force_seekg(srcfile, src_pos, SEEK_SET);
          }
        }

        Row = myGifFile->Image.Top; /* Image Position relative to Screen. */
        Col = myGifFile->Image.Left;
        Width = myGifFile->Image.Width;
        Height = myGifFile->Image.Height;

        for (i = Row; i < (Row + Height); i++) {
          own_fread(&ScreenBuff[i][Col], 1, Width, srcfile);
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
    force_seekg(srcfile, last_pos, SEEK_SET);
    fast_copy(srcfile, dstfile, src_pos - last_pos);
    force_seekg(srcfile, src_pos, SEEK_SET);
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

bool decompress_gif(std::istream& srcfile, std::ostream& dstfile, long long src_pos, int& gif_length, long long& decomp_length, unsigned char& block_size, GifCodeStruct* g) {
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
            force_seekg(srcfile, src_pos, SEEK_SET);
            fast_copy(srcfile, dstfile, srcfile_pos - src_pos);
            force_seekg(srcfile, srcfile_pos, SEEK_SET);

            long long dstfile_pos = dstfile.tellp();
            dstfile.seekp(0, SEEK_SET);
            // change GIF8xa to PGF8xa
            dstfile.put('P');
            dstfile.put('G');
            dstfile.seekp(dstfile_pos, SEEK_SET);
          } else {
            force_seekg(srcfile, last_pos, SEEK_SET);
            fast_copy(srcfile, dstfile, srcfile_pos - last_pos);
            force_seekg(srcfile, srcfile_pos, SEEK_SET);
          }
        }

        unsigned char c;
        c = srcfile.get();
        if (c == 254) {
          block_size = 254;
        }
        force_seekg(srcfile, srcfile_pos, SEEK_SET);

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
            own_fwrite(&ScreenBuff[i][Col], 1, Width, dstfile);
          }
        } else {
          for (i = Row; i < (Row + Height); i++) {
            if (DGifGetLineByte(myGifFile, &ScreenBuff[i][Col], Width, g) == GIF_ERROR) {
              return d_gif_error(ScreenBuff, myGifFile);
            }
            // write to dstfile
            own_fwrite(&ScreenBuff[i][Col], 1, Width, dstfile);
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
    force_seekg(srcfile, last_pos, SEEK_SET);
    fast_copy(srcfile, dstfile, srcfile_pos - last_pos);
    force_seekg(srcfile, srcfile_pos, SEEK_SET);
  }

  gif_length = srcfile_pos - src_pos;
  decomp_length = dstfile.tellp();

  return d_gif_ok(ScreenBuff, myGifFile);
}

void try_decompression_gif(unsigned char version[5], PrecompTmpFile& tmpfile) {

  unsigned char block_size = 255;
  int gif_length = -1;
  long long decomp_length = -1;

  GifCodeStruct gCode;
  GifCodeInit(&gCode);
  GifDiffStruct gDiff;
  GifDiffInit(&gDiff);

  bool recompress_success_needed = true;

  if (g_precomp.switches.DEBUG_MODE) {
  print_debug_percent();
  std::cout << "Possible GIF found at position " << g_precomp.ctx->input_file_pos << std::endl;;
  }

  force_seekg(g_precomp.ctx->fin, g_precomp.ctx->input_file_pos, SEEK_SET);

  tmpfile.close();
  // read GIF file
  {
    FileWrapper ftempout;
    ftempout.open(tmpfile.file_path, std::ios_base::out | std::ios_base::binary);

    if (!decompress_gif(g_precomp.ctx->fin, ftempout, g_precomp.ctx->input_file_pos, gif_length, decomp_length, block_size, &gCode)) {
      ftempout.close();
      remove(tmpfile.file_path.c_str());
      GifDiffFree(&gDiff);
      GifCodeFree(&gCode);
      return;
    }

    if (g_precomp.switches.DEBUG_MODE) {
      std::cout << "Can be decompressed to " << decomp_length << " bytes" << std::endl;
    }
  }

  g_precomp.statistics.decompressed_streams_count++;
  g_precomp.statistics.decompressed_gif_count++;

  FileWrapper ftempout;
  ftempout.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
  std::string tempfile2 = tmpfile.file_path + "_rec_";
  PrecompTmpFile frecomp;
  frecomp.open(tempfile2, std::ios_base::out | std::ios_base::binary);
  if (recompress_gif(ftempout, frecomp, block_size, &gCode, &gDiff)) {

    frecomp.close();
    ftempout.close();

    frecomp.close();
    frecomp.open(tempfile2, std::ios_base::in | std::ios_base::binary);
    g_precomp.ctx->best_identical_bytes = compare_files_penalty(g_precomp.ctx->fin, frecomp, g_precomp.ctx->input_file_pos, 0);
    frecomp.close();

    if (g_precomp.ctx->best_identical_bytes < gif_length) {
      if (g_precomp.switches.DEBUG_MODE) {
      printf ("Recompression failed\n");
      }
    } else {
      if (g_precomp.switches.DEBUG_MODE) {
      printf ("Recompression successful\n");
      }
      recompress_success_needed = true;

      if (g_precomp.ctx->best_identical_bytes > g_precomp.switches.min_ident_size) {
        g_precomp.statistics.recompressed_streams_count++;
        g_precomp.statistics.recompressed_gif_count++;
        g_precomp.ctx->non_zlib_was_used = true;

        if (!g_precomp.ctx->penalty_bytes.empty()) {
          memcpy(g_precomp.ctx->best_penalty_bytes.data(), g_precomp.ctx->penalty_bytes.data(), g_precomp.ctx->penalty_bytes_len);
          g_precomp.ctx->best_penalty_bytes_len = g_precomp.ctx->penalty_bytes_len;
        } else {
          g_precomp.ctx->best_penalty_bytes_len = 0;
        }

        // end uncompressed data

        g_precomp.ctx->compressed_data_found = true;
        end_uncompressed_data();

        // write compressed data header (GIF)
        unsigned char add_bits = 0;
        if (g_precomp.ctx->best_penalty_bytes_len != 0) add_bits += 2;
        if (block_size == 254) add_bits += 4;
        if (recompress_success_needed) add_bits += 128;

        fout_fputc(1 + add_bits);
        fout_fputc(D_GIF); // GIF

        // store diff bytes
        fout_fput_vlint(gDiff.GIFDiffIndex);
        if(g_precomp.switches.DEBUG_MODE) {
          if (gDiff.GIFDiffIndex > 0)
            printf("Diff bytes were used: %i bytes\n", gDiff.GIFDiffIndex);
        }
        for (int dbc = 0; dbc < gDiff.GIFDiffIndex; dbc++) {
          fout_fputc(gDiff.GIFDiff[dbc]);
        }

        // store penalty bytes, if any
        if (g_precomp.ctx->best_penalty_bytes_len != 0) {
          if (g_precomp.switches.DEBUG_MODE) {
            printf("Penalty bytes were used: %i bytes\n", g_precomp.ctx->best_penalty_bytes_len);
          }

          fout_fput_vlint(g_precomp.ctx->best_penalty_bytes_len);

          for (int pbc = 0; pbc < g_precomp.ctx->best_penalty_bytes_len; pbc++) {
            fout_fputc(g_precomp.ctx->best_penalty_bytes[pbc]);
          }
        }

        fout_fput_vlint(g_precomp.ctx->best_identical_bytes);
        fout_fput_vlint(decomp_length);

        // write decompressed data
        write_decompressed_data(decomp_length, tmpfile.file_path.c_str());

        // start new uncompressed data

        // set input file pointer after recompressed data
        g_precomp.ctx->input_file_pos += gif_length - 1;
        g_precomp.ctx->cb += gif_length - 1;
      }
    }


  } else {

    if (g_precomp.switches.DEBUG_MODE) {
    printf ("No matches\n");
    }

    frecomp.close();
    ftempout.close();

  }

  GifDiffFree(&gDiff);
  GifCodeFree(&gCode);

  remove(tempfile2.c_str());
  remove(tmpfile.file_path.c_str());

}

// JPG routines

void packjpg_mp3_dll_msg() {

  printf("Using packJPG for JPG recompression, packMP3 for MP3 recompression.\n");
  printf("%s\n", pjglib_version_info());
  printf("%s\n", pmplib_version_info());
  printf("More about packJPG and packMP3 here: http://www.matthiasstirner.com\n\n");

}

void try_decompression_jpg (long long jpg_length, bool progressive_jpg, PrecompTmpFile& tmpfile) {
  std::string decompressed_jpg_filename = tmpfile.file_path + "_";
  tmpfile.close();

        if (g_precomp.switches.DEBUG_MODE) {
          print_debug_percent();
          if (progressive_jpg) {
            printf ("Possible JPG (progressive) found at position ");
          } else {
            printf ("Possible JPG found at position ");
          }
          std::cout << g_precomp.ctx->saved_input_file_pos << ", length " << jpg_length << std::endl;
          // do not recompress non-progressive JPGs when prog_only is set
          if ((!progressive_jpg) && (g_precomp.switches.prog_only)) {
            printf("Skipping (only progressive JPGs mode set)\n");
          }
        }

        // do not recompress non-progressive JPGs when prog_only is set
        if ((!progressive_jpg) && (g_precomp.switches.prog_only)) return;

        bool jpg_success = false;
        bool recompress_success = false;
        bool mjpg_dht_used = false;
		bool brunsli_used = false;
		bool brotli_used = g_precomp.switches.use_brotli;
        char recompress_msg[256];
        unsigned char* jpg_mem_in = NULL;
        unsigned char* jpg_mem_out = NULL;
        unsigned int jpg_mem_out_size = -1;
        bool in_memory = ((jpg_length + MJPGDHT_LEN) <= JPG_MAX_MEMORY_SIZE);

        if (in_memory) { // small stream => do everything in memory
          jpg_mem_in = new unsigned char[jpg_length + MJPGDHT_LEN];
          force_seekg(g_precomp.ctx->fin, g_precomp.ctx->input_file_pos, SEEK_SET);
          fast_copy(g_precomp.ctx->fin, jpg_mem_in, jpg_length);

		  bool brunsli_success = false;

		  if (g_precomp.switches.use_brunsli) {
			  if (g_precomp.switches.DEBUG_MODE) {
				  printf("Trying to compress using brunsli...\n");
			  }
			  brunsli::JPEGData jpegData;
			  if (brunsli::ReadJpeg(jpg_mem_in, jpg_length, brunsli::JPEG_READ_ALL, &jpegData)) {
				  size_t output_size = brunsli::GetMaximumBrunsliEncodedSize(jpegData);
				  jpg_mem_out = new unsigned char[output_size];
				  if (brunsli::BrunsliEncodeJpeg(jpegData, jpg_mem_out, &output_size, g_precomp.switches.use_brotli)) {
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
					  if (g_precomp.switches.DEBUG_MODE) printf("huffman table missing, trying to use Motion JPEG DHT\n");
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
							  if (brunsli::BrunsliEncodeJpeg(jpegData, jpg_mem_out, &output_size, g_precomp.switches.use_brotli)) {
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
			  if (g_precomp.switches.DEBUG_MODE && !brunsli_success) {
				  if (g_precomp.switches.use_packjpg_fallback) {
					  printf("Brunsli compression failed, using packJPG fallback...\n");
				  } else {
					  printf("Brunsli compression failed\n");
				  }
			  }
		  }

		  if ((!g_precomp.switches.use_brunsli || !brunsli_success) && g_precomp.switches.use_packjpg_fallback) {
			  pjglib_init_streams(jpg_mem_in, 1, jpg_length, jpg_mem_out, 1);
			  recompress_success = pjglib_convert_stream2mem(&jpg_mem_out, &jpg_mem_out_size, recompress_msg);
			  brunsli_used = false;
			  brotli_used = false;
		  }
        } else if (g_precomp.switches.use_packjpg_fallback) { // large stream => use temporary files
		  if (g_precomp.switches.DEBUG_MODE) {
			printf("JPG too large for brunsli, using packJPG fallback...\n");
		  }
		  // try to decompress at current position
          {
            FileWrapper decompressed_jpg;
            decompressed_jpg.open(decompressed_jpg_filename, std::ios_base::out | std::ios_base::binary);
            force_seekg(g_precomp.ctx->fin, g_precomp.ctx->input_file_pos, SEEK_SET);
            fast_copy(g_precomp.ctx->fin, decompressed_jpg, jpg_length);
            decompressed_jpg.close();
          }

          // Workaround for JPG bugs. Sometimes tempfile1 is removed, but still
          // not accessible by packJPG, so we prevent that by opening it here
          // ourselves.
          {
            remove(tmpfile.file_path.c_str());
            FileWrapper fworkaround;
            fworkaround.open(tmpfile.file_path, std::ios_base::out | std::ios_base::binary);
            fworkaround.close();
          }

          recompress_success = pjglib_convert_file2file(const_cast<char*>(decompressed_jpg_filename.c_str()), const_cast<char*>(tmpfile.file_path.c_str()), recompress_msg);
		  brunsli_used = false;
		  brotli_used = false;
        }

        if ((!recompress_success) && (strncmp(recompress_msg, "huffman table missing", 21) == 0) && (g_precomp.switches.use_mjpeg) && (g_precomp.switches.use_packjpg_fallback)) {
          if (g_precomp.switches.DEBUG_MODE) printf("huffman table missing, trying to use Motion JPEG DHT\n");
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
            FileWrapper decompressed_jpg;
            decompressed_jpg.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
            do {
              ffda_pos++;
              decompressed_jpg.read(reinterpret_cast<char*>(in), 1);
              if (decompressed_jpg.gcount() != 1) break;
              if (found_ff) {
                found_ffda = (in[0] == 0xDA);
                if (found_ffda) break;
                found_ff = false;
              } else {
                found_ff = (in[0] == 0xFF);
              }
            } while (!found_ffda);
            std::string mjpgdht_tempfile = tmpfile.file_path + "_mjpgdht";
            if (found_ffda) {
              FileWrapper decompressed_jpg_w_MJPGDHT;
              decompressed_jpg_w_MJPGDHT.open(mjpgdht_tempfile, std::ios_base::out | std::ios_base::binary);
              force_seekg(decompressed_jpg, 0, SEEK_SET);
              fast_copy(decompressed_jpg, decompressed_jpg_w_MJPGDHT, ffda_pos - 1);
              // insert MJPGDHT
              own_fwrite(MJPGDHT, 1, MJPGDHT_LEN, decompressed_jpg_w_MJPGDHT);
              force_seekg(decompressed_jpg, ffda_pos - 1, SEEK_SET);
              fast_copy(decompressed_jpg, decompressed_jpg_w_MJPGDHT, jpg_length - (ffda_pos - 1));
            }
            decompressed_jpg.close();
            recompress_success = pjglib_convert_file2file(const_cast<char*>(mjpgdht_tempfile.c_str()), const_cast<char*>(tmpfile.file_path.c_str()), recompress_msg);
          }

          mjpg_dht_used = recompress_success;
        }

        g_precomp.statistics.decompressed_streams_count++;
        if (progressive_jpg) {
          g_precomp.statistics.decompressed_jpg_prog_count++;
        } else {
          g_precomp.statistics.decompressed_jpg_count++;
        }

        if ((!recompress_success) && (g_precomp.switches.use_packjpg_fallback)) {
          if (g_precomp.switches.DEBUG_MODE) printf ("packJPG error: %s\n", recompress_msg);
        }

        if (!in_memory) {
          remove(decompressed_jpg_filename.c_str());
        }

        if (recompress_success) {
          int jpg_new_length = -1;

          if (in_memory) {
            jpg_new_length = jpg_mem_out_size;
          } else {
            FileWrapper ftempout;
            ftempout.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
            force_seekg(ftempout, 0, SEEK_END);
            jpg_new_length = ftempout.tellg();
            ftempout.close();
          }

          if (jpg_new_length > 0) {
            g_precomp.statistics.recompressed_streams_count++;
            if (progressive_jpg) {
              g_precomp.statistics.recompressed_jpg_prog_count++;
            } else {
              g_precomp.statistics.recompressed_jpg_count++;
            }
            g_precomp.ctx->non_zlib_was_used = true;

            g_precomp.ctx->best_identical_bytes = jpg_length;
            g_precomp.ctx->best_identical_bytes_decomp = jpg_new_length;
            jpg_success = true;
          }
        }

        if (jpg_success) {

          if (g_precomp.switches.DEBUG_MODE) {
          std::cout << "Best match: " << g_precomp.ctx->best_identical_bytes << " bytes, recompressed to " << g_precomp.ctx->best_identical_bytes_decomp << " bytes" << std::endl;
          }

          // end uncompressed data

          g_precomp.ctx->compressed_data_found = true;
          end_uncompressed_data();

          // write compressed data header (JPG)

		  char jpg_flags = 1; // no penalty bytes
		  if (mjpg_dht_used) jpg_flags += 4; // motion JPG DHT used
		  if (brunsli_used) jpg_flags += 8;
		  if (brotli_used) jpg_flags += 16;
		  fout_fputc(jpg_flags);
          fout_fputc(D_JPG); // JPG

          fout_fput_vlint(g_precomp.ctx->best_identical_bytes);
          fout_fput_vlint(g_precomp.ctx->best_identical_bytes_decomp);

          // write compressed JPG
          if (in_memory) {
            fast_copy(jpg_mem_out, g_precomp.ctx->fout, g_precomp.ctx->best_identical_bytes_decomp);
          } else {
            write_decompressed_data(g_precomp.ctx->best_identical_bytes_decomp, tmpfile.file_path.c_str());
          }

          // start new uncompressed data

          // set input file pointer after recompressed data
          g_precomp.ctx->input_file_pos += g_precomp.ctx->best_identical_bytes - 1;
          g_precomp.ctx->cb += g_precomp.ctx->best_identical_bytes - 1;

        } else {
          if (g_precomp.switches.DEBUG_MODE) {
          printf("No matches\n");
          }
        }

        if (jpg_mem_in != NULL) delete[] jpg_mem_in;
        if (jpg_mem_out != NULL) delete[] jpg_mem_out;
}

void try_decompression_mp3 (long long mp3_length, PrecompTmpFile& tmpfile) {
  std::string decompressed_mp3_filename = tmpfile.file_path + "_";
  tmpfile.close();

        if (g_precomp.switches.DEBUG_MODE) {
          print_debug_percent();
          std::cout << "Possible MP3 found at position " << g_precomp.ctx->saved_input_file_pos << ", length " << mp3_length << std::endl;
        }

        bool mp3_success = false;
        bool recompress_success = false;
        char recompress_msg[256];
        unsigned char* mp3_mem_in = NULL;
        unsigned char* mp3_mem_out = NULL;
        unsigned int mp3_mem_out_size = -1;
        bool in_memory = (mp3_length <= MP3_MAX_MEMORY_SIZE);

        if (in_memory) { // small stream => do everything in memory
          mp3_mem_in = new unsigned char[mp3_length];
          force_seekg(g_precomp.ctx->fin, g_precomp.ctx->input_file_pos, SEEK_SET);
          fast_copy(g_precomp.ctx->fin, mp3_mem_in, mp3_length);

          pmplib_init_streams(mp3_mem_in, 1, mp3_length, mp3_mem_out, 1);
          recompress_success = pmplib_convert_stream2mem(&mp3_mem_out, &mp3_mem_out_size, recompress_msg);
        } else { // large stream => use temporary files
          // try to decompress at current position
          {
            FileWrapper decompressed_mp3;
            decompressed_mp3.open(decompressed_mp3_filename, std::ios_base::out | std::ios_base::binary);
            force_seekg(g_precomp.ctx->fin, g_precomp.ctx->input_file_pos, SEEK_SET);
            fast_copy(g_precomp.ctx->fin, decompressed_mp3, mp3_length);
            decompressed_mp3.close();
          }
          
          // workaround for bugs, similar to packJPG
          {
            remove(tmpfile.file_path.c_str());
            FileWrapper fworkaround;
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

              if (g_precomp.switches.DEBUG_MODE) printf ("Too much garbage data at the end, retry with new length %i\n", pos);

              if (in_memory) {
                pmplib_init_streams(mp3_mem_in, 1, mp3_length, mp3_mem_out, 1);
                recompress_success = pmplib_convert_stream2mem(&mp3_mem_out, &mp3_mem_out_size, recompress_msg);
              } else {
                std::experimental::filesystem::resize_file(decompressed_mp3_filename, pos);

                // workaround for bugs, similar to packJPG
                {
                  remove(tmpfile.file_path.c_str());
                  FileWrapper fworkaround;
                  fworkaround.open(tmpfile.file_path, std::ios_base::out | std::ios_base::binary);
                }

                recompress_success = pmplib_convert_file2file(const_cast<char*>(decompressed_mp3_filename.c_str()), const_cast<char*>(tmpfile.file_path.c_str()), recompress_msg);
              }
            }
          }
        } else if ((!recompress_success) && (strncmp(recompress_msg, "big value pairs out of bounds", 29) == 0)) {
          g_precomp.ctx->suppress_mp3_big_value_pairs_sum = g_precomp.ctx->saved_input_file_pos + mp3_length;
          if (g_precomp.switches.DEBUG_MODE) {
            std::cout << "Ignoring following streams with position/length sum " << g_precomp.ctx->suppress_mp3_big_value_pairs_sum << " to avoid slowdown" << std::endl;
          }
        } else if ((!recompress_success) && (strncmp(recompress_msg, "non-zero padbits found", 22) == 0)) {
          g_precomp.ctx->suppress_mp3_non_zero_padbits_sum = g_precomp.ctx->saved_input_file_pos + mp3_length;
          if (g_precomp.switches.DEBUG_MODE) {
            std::cout << "Ignoring following streams with position/length sum " << g_precomp.ctx->suppress_mp3_non_zero_padbits_sum << " to avoid slowdown" << std::endl;
          }
        } else if ((!recompress_success) && (strncmp(recompress_msg, "inconsistent use of emphasis", 28) == 0)) {
          g_precomp.ctx->suppress_mp3_inconsistent_emphasis_sum = g_precomp.ctx->saved_input_file_pos + mp3_length;
          if (g_precomp.switches.DEBUG_MODE) {
            std::cout << "Ignoring following streams with position/length sum " << g_precomp.ctx->suppress_mp3_inconsistent_emphasis_sum << " to avoid slowdown" << std::endl;
          }
        } else if ((!recompress_success) && (strncmp(recompress_msg, "inconsistent original bit", 25) == 0)) {
          g_precomp.ctx->suppress_mp3_inconsistent_original_bit = g_precomp.ctx->saved_input_file_pos + mp3_length;
          if (g_precomp.switches.DEBUG_MODE) {
            std::cout << "Ignoring following streams with position/length sum " << g_precomp.ctx->suppress_mp3_inconsistent_original_bit << " to avoid slowdown" << std::endl;
          }
        }

        g_precomp.statistics.decompressed_streams_count++;
        g_precomp.statistics.decompressed_mp3_count++;

        if (!recompress_success) {
          if (g_precomp.switches.DEBUG_MODE) printf ("packMP3 error: %s\n", recompress_msg);
        }

        if (!in_memory) {
          remove(decompressed_mp3_filename.c_str());
        }

        if (recompress_success) {
          int mp3_new_length = -1;

          if (in_memory) {
            mp3_new_length = mp3_mem_out_size;
          } else {
            FileWrapper ftempout;
            ftempout.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
            force_seekg(ftempout, 0, SEEK_END);
            mp3_new_length = ftempout.tellg();
            ftempout.close();
          }

          if (mp3_new_length > 0) {
            g_precomp.statistics.recompressed_streams_count++;
            g_precomp.statistics.recompressed_mp3_count++;
            g_precomp.ctx->non_zlib_was_used = true;

            g_precomp.ctx->best_identical_bytes = mp3_length;
            g_precomp.ctx->best_identical_bytes_decomp = mp3_new_length;
            mp3_success = true;
          }
        }

        if (mp3_success) {

          if (g_precomp.switches.DEBUG_MODE) {
          std::cout << "Best match: " << g_precomp.ctx->best_identical_bytes << " bytes, recompressed to " << g_precomp.ctx->best_identical_bytes_decomp << " bytes" << std::endl;
          }

          // end uncompressed data

          g_precomp.ctx->compressed_data_found = true;
          end_uncompressed_data();

          // write compressed data header (MP3)

          fout_fputc(1); // no penalty bytes
          fout_fputc(D_MP3); // MP3

          fout_fput_vlint(g_precomp.ctx->best_identical_bytes);
          fout_fput_vlint(g_precomp.ctx->best_identical_bytes_decomp);

          // write compressed MP3
          if (in_memory) {
            fast_copy(mp3_mem_out, g_precomp.ctx->fout, g_precomp.ctx->best_identical_bytes_decomp);
          } else {
            write_decompressed_data(g_precomp.ctx->best_identical_bytes_decomp, tmpfile.file_path.c_str());
          }

          // start new uncompressed data

          // set input file pointer after recompressed data
          g_precomp.ctx->input_file_pos += g_precomp.ctx->best_identical_bytes - 1;
          g_precomp.ctx->cb += g_precomp.ctx->best_identical_bytes - 1;

        } else {
          if (g_precomp.switches.DEBUG_MODE) {
          printf("No matches\n");
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

void try_decompression_zlib(int windowbits, PrecompTmpFile& tmpfile) {
  try_decompression_deflate_type(g_precomp.statistics.decompressed_zlib_count, g_precomp.statistics.recompressed_zlib_count, 
                                 D_RAW, g_precomp.ctx->in_buf + g_precomp.ctx->cb, 2, true,
                                 "(intense mode)", tmpfile);
}

void try_decompression_brute(PrecompTmpFile& tmpfile) {
  try_decompression_deflate_type(g_precomp.statistics.decompressed_brute_count, g_precomp.statistics.recompressed_brute_count, 
                                 D_BRUTE, g_precomp.ctx->in_buf + g_precomp.ctx->cb, 0, false,
                                 "(brute mode)", tmpfile);
}

void try_decompression_swf(int windowbits, PrecompTmpFile& tmpfile) {
  try_decompression_deflate_type(g_precomp.statistics.decompressed_swf_count, g_precomp.statistics.recompressed_swf_count, 
                                 D_SWF, g_precomp.ctx->in_buf + g_precomp.ctx->cb + 3, 7, true,
                                 "in SWF", tmpfile);
}

void try_decompression_bzip2(int compression_level, PrecompTmpFile& tmpfile) {
  init_decompression_variables();

        // try to decompress at current position
        long long compressed_stream_size = -1;
        g_precomp.ctx->retval = try_to_decompress_bzip2(g_precomp.ctx->fin, compression_level, compressed_stream_size, tmpfile);

        if (g_precomp.ctx->retval > 0) { // seems to be a zLib-Stream

          g_precomp.statistics.decompressed_streams_count++;
          g_precomp.statistics.decompressed_bzip2_count++;

          if (g_precomp.switches.DEBUG_MODE) {
          print_debug_percent();
          std::cout << "Possible bZip2-Stream found at position " << g_precomp.ctx->saved_input_file_pos << ", compression level = " << compression_level << std::endl;
          std::cout << "Compressed size: " << compressed_stream_size << std::endl;

          FileWrapper ftempout;
          ftempout.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
          force_seekg(ftempout, 0, SEEK_END);
          std::cout << "Can be decompressed to " << ftempout.tellg() << " bytes" << std::endl;
          ftempout.close();
          }

          tmpfile.reopen();
          try_recompress_bzip2(g_precomp.ctx->fin, compression_level, compressed_stream_size, tmpfile);

          if ((g_precomp.ctx->best_identical_bytes > g_precomp.switches.min_ident_size) && (g_precomp.ctx->best_identical_bytes < g_precomp.ctx->best_identical_bytes_decomp)) {
            g_precomp.statistics.recompressed_streams_count++;
            g_precomp.statistics.recompressed_bzip2_count++;

            if (g_precomp.switches.DEBUG_MODE) {
            std::cout << "Best match: " << g_precomp.ctx->best_identical_bytes << " bytes, decompressed to " << g_precomp.ctx->best_identical_bytes_decomp << " bytes" << std::endl;
            }

            g_precomp.ctx->non_zlib_was_used = true;

            // end uncompressed data

            g_precomp.ctx->compressed_data_found = true;
            end_uncompressed_data();

            // check recursion
            tmpfile.reopen();
            recursion_result r = recursion_compress(g_precomp.ctx->best_identical_bytes, g_precomp.ctx->best_identical_bytes_decomp, tmpfile);

            // write compressed data header (bZip2)

            int header_byte = 1;
            if (g_precomp.ctx->best_penalty_bytes_len != 0) {
              header_byte += 2;
            }
            if (r.success) {
              header_byte += 128;
            }
            fout_fputc(header_byte);
            fout_fputc(D_BZIP2); // Base64
            fout_fputc(compression_level);

            // store penalty bytes, if any
            if (g_precomp.ctx->best_penalty_bytes_len != 0) {
              if (g_precomp.switches.DEBUG_MODE) {
                printf("Penalty bytes were used: %i bytes\n", g_precomp.ctx->best_penalty_bytes_len);
              }
              fout_fput_vlint(g_precomp.ctx->best_penalty_bytes_len);
              for (int pbc = 0; pbc < g_precomp.ctx->best_penalty_bytes_len; pbc++) {
                fout_fputc(g_precomp.ctx->best_penalty_bytes[pbc]);
              }
            }

            fout_fput_vlint(g_precomp.ctx->best_identical_bytes);
            fout_fput_vlint(g_precomp.ctx->best_identical_bytes_decomp);

            if (r.success) {
              fout_fput_vlint(r.file_length);
            }

            // write decompressed data
            if (r.success) {
              write_decompressed_data(r.file_length, r.file_name.c_str());
              remove(r.file_name.c_str());
            } else {
              write_decompressed_data(g_precomp.ctx->best_identical_bytes_decomp, tmpfile.file_path.c_str());
            }

            // start new uncompressed data

            // set input file pointer after recompressed data
            g_precomp.ctx->input_file_pos += g_precomp.ctx->best_identical_bytes - 1;
            g_precomp.ctx->cb += g_precomp.ctx->best_identical_bytes - 1;

          } else {
            if (g_precomp.switches.DEBUG_MODE) {
            printf("No matches\n");
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

void base64_reencode(std::istream& file_in, std::ostream& file_out, int line_count, unsigned int* base64_line_len, long long max_in_count, long long max_byte_count) {
          int line_nr = 0;
          unsigned int act_line_len = 0;
          int avail_in;
          unsigned char a,b,c;
          int i;
          long long act_byte_count = 0;

          long long remaining_bytes = max_in_count;

          do {
            if (remaining_bytes > DIV3CHUNK) {
              avail_in = own_fread(in, 1, DIV3CHUNK, file_in);
            } else {
              avail_in = own_fread(in, 1, remaining_bytes, file_in);
            }
            remaining_bytes -= avail_in;

            // make sure avail_in mod 3 = 0, pad with 0 bytes
            while ((avail_in % 3) != 0) {
              in[avail_in] = 0;
              avail_in++;
            }

            for (i = 0; i < (avail_in/3); i++) {
              a = in[i * 3];
              b = in[i * 3 + 1];
              c = in[i * 3 + 2];
              if (act_byte_count < max_byte_count) own_fputc(b64[a >> 2], file_out);
              act_byte_count++;
              act_line_len++;
              if (act_line_len == base64_line_len[line_nr]) { // end of line, write CRLF
                if (act_byte_count < max_byte_count) own_fputc(13, file_out);
                act_byte_count++;
                if (act_byte_count < max_byte_count) own_fputc(10, file_out);
                act_byte_count++;
                act_line_len = 0;
                line_nr++;
                if (line_nr == line_count) break;
              }
              if (act_byte_count < max_byte_count) own_fputc(b64[((a & 0x03) << 4) | (b >> 4)], file_out);
              act_byte_count++;
              act_line_len++;
              if (act_line_len == base64_line_len[line_nr]) { // end of line, write CRLF
                if (act_byte_count < max_byte_count) own_fputc(13, file_out);
                act_byte_count++;
                if (act_byte_count < max_byte_count) own_fputc(10, file_out);
                act_byte_count++;
                act_line_len = 0;
                line_nr++;
                if (line_nr == line_count) break;
              }
              if (act_byte_count < max_byte_count) own_fputc(b64[((b & 0x0F) << 2) | (c >> 6)], file_out);
              act_byte_count++;
              act_line_len++;
              if (act_line_len == base64_line_len[line_nr]) { // end of line, write CRLF
                if (act_byte_count < max_byte_count) own_fputc(13, file_out);
                act_byte_count++;
                if (act_byte_count < max_byte_count) own_fputc(10, file_out);
                act_byte_count++;
                act_line_len = 0;
                line_nr++;
                if (line_nr == line_count) break;
              }
              if (act_byte_count < max_byte_count) own_fputc(b64[c & 63], file_out);
              act_byte_count++;
              act_line_len++;
              if (act_line_len == base64_line_len[line_nr]) { // end of line, write CRLF
                if (act_byte_count < max_byte_count) own_fputc(13, file_out);
                act_byte_count++;
                if (act_byte_count < max_byte_count) own_fputc(10, file_out);
                act_byte_count++;
                act_line_len = 0;
                line_nr++;
                if (line_nr == line_count) break;
              }
            }
            if (line_nr == line_count) break;
          } while ((remaining_bytes > 0) && (avail_in > 0));

}

void try_decompression_base64(int base64_header_length, PrecompTmpFile& tmpfile) {
  init_decompression_variables();
  tmpfile.close();

  // try to decode at current position
  remove(tmpfile.file_path.c_str());
  force_seekg(g_precomp.ctx->fin, g_precomp.ctx->input_file_pos, SEEK_SET);

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
    FileWrapper ftempout;
    ftempout.open(tmpfile.file_path, std::ios_base::out | std::ios_base::binary);
    do {
      g_precomp.ctx->fin.read(reinterpret_cast<char*>(in), CHUNK);
      avail_in = g_precomp.ctx->fin.gcount();
      for (i = 0; i < (avail_in >> 2); i++) {
        // are these valid base64 chars?
        for (j = (i << 2); j < ((i << 2) + 4); j++) {
          c = base64_char_decode(in[j]);
          if (c < 64) {
            base64_data[k] = c;
            k++;
            cr_count = 0;
            act_line_len++;
            continue;
          }
          if ((in[j] == 13) || (in[j] == 10)) {
            if (in[j] == 13) {
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
          if (in[j] == '=') {
            while ((k % 4) != 0) {
              base64_data[k] = 0;
              k++;
            }
            break;
          }
          // "-" -> base64 end
          if (in[j] == '-') break;
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

    g_precomp.statistics.decompressed_streams_count++;
    g_precomp.statistics.decompressed_base64_count++;

    {
      FileWrapper ftempout;
      ftempout.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
      force_seekg(ftempout, 0, SEEK_END);
      g_precomp.ctx->identical_bytes = ftempout.tellg();
    }

    if (g_precomp.switches.DEBUG_MODE) {
      print_debug_percent();
      std::cout << "Possible Base64-Stream (line_case " << line_case << ", line_count " << line_count << ") found at position " << g_precomp.ctx->saved_input_file_pos << std::endl;
      std::cout << "Can be decoded to " << g_precomp.ctx->identical_bytes << " bytes" << std::endl;
    }

    // try to re-encode Base64 data
    {
      FileWrapper ftempout;
      ftempout.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
      if (!ftempout.is_open()) {
        error(ERR_TEMP_FILE_DISAPPEARED, tmpfile.file_path);
      }

      std::string frecomp_filename = tmpfile.file_path + "_rec";
      remove(frecomp_filename.c_str());
      PrecompTmpFile frecomp;
      frecomp.open(frecomp_filename, std::ios_base::in | std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);

      base64_reencode(ftempout, frecomp, line_count, base64_line_len);

      ftempout.close();

      g_precomp.ctx->identical_bytes_decomp = compare_files(g_precomp.ctx->fin, frecomp, g_precomp.ctx->input_file_pos, 0);
    }

    if (g_precomp.ctx->identical_bytes_decomp > g_precomp.switches.min_ident_size) {
      g_precomp.statistics.recompressed_streams_count++;
      g_precomp.statistics.recompressed_base64_count++;
      if (g_precomp.switches.DEBUG_MODE) {
        std::cout << "Match: encoded to " << g_precomp.ctx->identical_bytes_decomp << " bytes" << std::endl;
      }

      // end uncompressed data

      g_precomp.ctx->compressed_data_found = true;
      end_uncompressed_data();

      // check recursion
      tmpfile.reopen();
      recursion_result r = recursion_compress(g_precomp.ctx->identical_bytes_decomp, g_precomp.ctx->identical_bytes, tmpfile);

      // write compressed data header (Base64)
      int header_byte = 1 + (line_case << 2);
      if (r.success) {
        header_byte += 128;
      }
      fout_fputc(header_byte);
      fout_fputc(D_BASE64); // Base64

      fout_fput_vlint(base64_header_length);

      // write "header", but change first char to prevent re-detection
      fout_fputc(g_precomp.ctx->in_buf[g_precomp.ctx->cb] - 1);
      own_fwrite(g_precomp.ctx->in_buf + g_precomp.ctx->cb + 1, 1, base64_header_length - 1, g_precomp.ctx->fout);

      fout_fput_vlint(line_count);
      if (line_case == 2) {
        for (i = 0; i < line_count; i++) {
          fout_fputc(base64_line_len[i]);
        }
      }
      else {
        fout_fputc(base64_line_len[0]);
        if (line_case == 1) fout_fputc(base64_line_len[line_count - 1]);
      }

      delete[] base64_line_len;

      fout_fput_vlint(g_precomp.ctx->identical_bytes);
      fout_fput_vlint(g_precomp.ctx->identical_bytes_decomp);

      if (r.success) {
        fout_fput_vlint(r.file_length);
      }

      // write decompressed data
      if (r.success) {
        write_decompressed_data(r.file_length, r.file_name.c_str());
        remove(r.file_name.c_str());
      } else {
        write_decompressed_data(g_precomp.ctx->identical_bytes, tmpfile.file_path.c_str());
      }

      // start new uncompressed data

      // set input file pointer after recompressed data
      g_precomp.ctx->input_file_pos += g_precomp.ctx->identical_bytes_decomp - 1;
      g_precomp.ctx->cb += g_precomp.ctx->identical_bytes_decomp - 1;
    }
    else {
      if (g_precomp.switches.DEBUG_MODE) {
        printf("No match\n");
      }
    }

  }

}

#ifdef COMFORT
void wait_for_key() {
  printf("\nPress any key to continue\n");
  // wait for key
  do {
    Sleep(55); // lower CPU cost
  } while (!kbhit());
}
#endif

void error(int error_nr, std::string tmp_filename) {
  printf("\nERROR %i: ", error_nr);
  switch (error_nr) {
    case ERR_IGNORE_POS_TOO_BIG:
      printf("Ignore position too big");
      break;
    case ERR_IDENTICAL_BYTE_SIZE_TOO_BIG:
      printf("Identical bytes size bigger than 4 GB");
      break;
    case ERR_ONLY_SET_MIN_SIZE_ONCE:
      printf("Minimal identical size can only be set once");
      break;
    case ERR_MORE_THAN_ONE_OUTPUT_FILE:
      printf("More than one output file given");
      break;
    case ERR_MORE_THAN_ONE_INPUT_FILE:
      printf("More than one input file given");
      break;
    case ERR_DONT_USE_SPACE:
      printf("Please don't use a space between the -o switch and the output filename");
      break;
    case ERR_TEMP_FILE_DISAPPEARED:
      printf("Temporary file %s disappeared", tmp_filename.c_str());
      break;
    case ERR_DISK_FULL:
      printf("There is not enough space on disk");
      // delete output file
      g_precomp.ctx->fout.close();
      remove(g_precomp.ctx->output_file_name.c_str());
      break;
    case ERR_RECURSION_DEPTH_TOO_BIG:
      printf("Recursion depth too big");
      break;
    case ERR_ONLY_SET_RECURSION_DEPTH_ONCE:
      printf("Recursion depth can only be set once");
      break;
    case ERR_CTRL_C:
      printf("CTRL-C detected");
      break;
    case ERR_INTENSE_MODE_LIMIT_TOO_BIG:
      printf("Intense mode level limit too big");
      break;
    case ERR_BRUTE_MODE_LIMIT_TOO_BIG:
      printf("Brute mode level limit too big");
      break;
    case ERR_ONLY_SET_LZMA_MEMORY_ONCE:
      printf("LZMA maximal memory can only be set once");
      break;
    case ERR_ONLY_SET_LZMA_THREAD_ONCE:
      printf("LZMA thread count can only be set once");
      break;
    default:
      printf("Unknown error");
  }
  printf("\n");

  #ifdef COMFORT
    wait_for_key();
  #endif

  exit(error_nr);
}

FileWrapper& tryOpen(const char* filename, std::ios_base::openmode mode) {
  FileWrapper fptr;
  fptr.open(filename,mode);

  if (fptr.is_open()) return fptr;

  long long timeoutstart = get_time_ms();
  while ((!fptr.is_open()) && ((get_time_ms() - timeoutstart) <= 15000)) {
    fptr.open(filename,mode);
  }
  if (!fptr.is_open()) {
    printf("ERROR: Access denied for %s\n", filename);

    exit(1);
  }
  if (g_precomp.switches.DEBUG_MODE) {
    printf("Access problem for %s\n", filename);
    printf("Time for getting access: %li ms\n", (long)(get_time_ms() - timeoutstart));
  }
  return fptr;
}

long long fileSize64(const char* filename) {
  std::error_code ec;
  return std::experimental::filesystem::file_size(filename, ec);
}

long long FileWrapper::filesize() {
  if (is_open()) close();
  long long size = fileSize64(file_path.c_str());
  reopen();
  return size;
}

void FileWrapper::resize(long long size) {
  if (is_open()) close();
  std::experimental::filesystem::resize_file(file_path, size);
  reopen();
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

void recursion_push() {
  g_precomp.recursion_contexts_stack.push_back(std::move(g_precomp.ctx));
  g_precomp.ctx = std::unique_ptr<RecursionContext>(new RecursionContext());
}

void recursion_pop() {
  g_precomp.ctx = std::move(g_precomp.recursion_contexts_stack.back());
  g_precomp.recursion_contexts_stack.pop_back();
}

void write_ftempout_if_not_present(long long byte_count, bool in_memory, PrecompTmpFile& tmpfile) {
  if (in_memory) {
    FileWrapper ftempout;
    ftempout.open(tmpfile.file_path, std::ios_base::out | std::ios_base::binary);
    fast_copy(g_precomp.ctx->decomp_io_buf, ftempout, byte_count);
    ftempout.close();
  }
}

recursion_result recursion_compress(long long compressed_bytes, long long decompressed_bytes, PrecompTmpFile& tmpfile, bool deflate_type, bool in_memory) {
  recursion_result tmp_r;
  tmp_r.success = false;

  float recursion_min_percent = ((g_precomp.ctx->input_file_pos + g_precomp.ctx->uncompressed_bytes_written) / ((float)g_precomp.ctx->fin_length + g_precomp.ctx->uncompressed_bytes_total)) * (g_precomp.ctx->global_max_percent - g_precomp.ctx->global_min_percent) + g_precomp.ctx->global_min_percent;
  float recursion_max_percent = ((g_precomp.ctx->input_file_pos + g_precomp.ctx->uncompressed_bytes_written +(compressed_bytes - 1)) / ((float)g_precomp.ctx->fin_length + g_precomp.ctx->uncompressed_bytes_total)) * (g_precomp.ctx->global_max_percent - g_precomp.ctx->global_min_percent) + g_precomp.ctx->global_min_percent;

  bool rescue_anything_was_used = false;
  bool rescue_non_zlib_was_used = false;

  if ((recursion_depth + 1) > max_recursion_depth) {
    max_recursion_depth_reached = true;
    return tmp_r;
  }

  tmpfile.close();
  if (deflate_type) {
    write_ftempout_if_not_present(decompressed_bytes, in_memory, tmpfile);
  }

  recursion_push();

  if (!deflate_type) {
    // shorten tempfile1 to decompressed_bytes
    std::experimental::filesystem::resize_file(tmpfile.file_path, decompressed_bytes);
  }

  g_precomp.ctx->fin_length = fileSize64(tmpfile.file_path.c_str());
  g_precomp.ctx->fin.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
  if (!g_precomp.ctx->fin.is_open()) {
    printf("ERROR: Recursion input file \"%s\" doesn't exist\n", tmpfile.file_path.c_str());

    exit(0);
  }
  g_precomp.ctx->input_file_name = tmpfile.file_path;
  g_precomp.ctx->output_file_name = tmpfile.file_path;
  g_precomp.ctx->output_file_name += '_';
  tmp_r.file_name = g_precomp.ctx->output_file_name;
  g_precomp.ctx->fout.open(g_precomp.ctx->output_file_name.c_str(), std::ios_base::out | std::ios_base::binary);

  g_precomp.ctx->intense_ignore_offsets = new std::set<long long>();
  g_precomp.ctx->brute_ignore_offsets = new std::set<long long>();

  // init MP3 suppression
  for (int i = 0; i < 16; i++) {
    g_precomp.ctx->suppress_mp3_type_until[i] = -1;
  }
  g_precomp.ctx->suppress_mp3_big_value_pairs_sum = -1;
  g_precomp.ctx->suppress_mp3_non_zero_padbits_sum = -1;
  g_precomp.ctx->suppress_mp3_inconsistent_emphasis_sum = -1;
  g_precomp.ctx->suppress_mp3_inconsistent_original_bit = -1;

  g_precomp.ctx->mp3_parsing_cache_second_frame = -1;

  // disable compression-on-the-fly in recursion - we don't want compressed compressed streams
  g_precomp.ctx->compression_otf_method = OTF_NONE;

  recursion_depth++;
  if (g_precomp.switches.DEBUG_MODE) {
    printf("Recursion start - new recursion depth %i\n", recursion_depth);
  }
  tmp_r.success = compress_file(recursion_min_percent, recursion_max_percent);

  delete g_precomp.ctx->intense_ignore_offsets;
  delete g_precomp.ctx->brute_ignore_offsets;
  // TODO CHECK: Delete ctx?

  if (g_precomp.ctx->anything_was_used)
    rescue_anything_was_used = true;

  if (g_precomp.ctx->non_zlib_was_used)
    rescue_non_zlib_was_used = true;

  recursion_depth--;
  recursion_pop();

  if (rescue_anything_was_used)
    g_precomp.ctx->anything_was_used = true;

  if (rescue_non_zlib_was_used)
    g_precomp.ctx->non_zlib_was_used = true;

  if (g_precomp.switches.DEBUG_MODE) {
    if (tmp_r.success) {
      printf("Recursion streams found\n");
    } else {
      printf("No recursion streams found\n");
    }
    printf("Recursion end - back to recursion depth %i\n", recursion_depth);
  }

  if (!tmp_r.success) {
    remove(tmp_r.file_name.c_str());
    tmp_r.file_name = "";
  } else {
    if ((recursion_depth + 1) > max_recursion_depth_used)
      max_recursion_depth_used = (recursion_depth + 1);
    // get recursion file size
    tmp_r.file_length = fileSize64(tmp_r.file_name.c_str());
  }

  return tmp_r;
}
recursion_result recursion_write_file_and_compress(const recompress_deflate_result& rdres, PrecompTmpFile& tmpfile) {
  recursion_result r = recursion_compress(rdres.compressed_stream_size, rdres.uncompressed_stream_size, tmpfile, true, rdres.uncompressed_in_memory);
  return r;
}

recursion_result recursion_decompress(long long recursion_data_length, PrecompTmpFile& tmpfile) {
  FileWrapper recursion_fin;
  recursion_result tmp_r;

  remove(tmpfile.file_path.c_str());
  recursion_fin.open(tmpfile.file_path, std::ios_base::out | std::ios_base::binary);

  fast_copy(g_precomp.ctx->fin, recursion_fin, recursion_data_length);

  recursion_fin.close();

  recursion_push();

  g_precomp.ctx->fin_length = fileSize64(tmpfile.file_path.c_str());
  g_precomp.ctx->fin.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
  if (!g_precomp.ctx->fin.is_open()) {
    printf("ERROR: Recursion input file \"%s\" doesn't exist\n", tmpfile.file_path.c_str());

    exit(0);
  }
  g_precomp.ctx->input_file_name = tmpfile.file_path;
  g_precomp.ctx->output_file_name = tmpfile.file_path;
  g_precomp.ctx->output_file_name += '_';
  tmp_r.file_name = g_precomp.ctx->output_file_name;
  g_precomp.ctx->fout.open(g_precomp.ctx->output_file_name.c_str(), std::ios_base::out | std::ios_base::binary);

  // disable compression-on-the-fly in recursion - we don't want compressed compressed streams
  g_precomp.ctx->compression_otf_method = OTF_NONE;

  recursion_depth++;
  if (g_precomp.switches.DEBUG_MODE) {
    printf("Recursion start - new recursion depth %i\n", recursion_depth);
  }
  decompress_file();

  // TODO CHECK: Delete ctx?

  recursion_depth--;
  recursion_pop();

  if (g_precomp.switches.DEBUG_MODE) {
    printf("Recursion end - back to recursion depth %i\n", recursion_depth);
  }

  // get recursion file size
  tmp_r.file_length = fileSize64(tmp_r.file_name.c_str());

  tmp_r.frecurse->open(tmp_r.file_name, std::ios_base::in | std::ios_base::binary);

  return tmp_r;
}

void own_fputc(char c, std::ostream& f) {
  bool use_otf = false;

  if (g_precomp.ctx->comp_decomp_state == P_CONVERT) {
    use_otf = (conversion_to_method > OTF_NONE);
    if (use_otf) g_precomp.ctx->compression_otf_method = conversion_to_method;
  } else {
    if ((&f != &g_precomp.ctx->fout) || (g_precomp.ctx->compression_otf_method == OTF_NONE)) {
      use_otf = false;
    } else {
      use_otf = true;
    }
  }

  if (!use_otf) {
    f.put(c);
  } else {
    fout_fputc(c);
  }
}

void fout_fputc(char c) {
  if (g_precomp.ctx->compression_otf_method == OTF_NONE) { // uncompressed
    g_precomp.ctx->fout.put(c);
  } else {
    unsigned char temp_buf[1];
    temp_buf[0] = c;
    own_fwrite(temp_buf, 1, 1, g_precomp.ctx->fout);
  }
}

void fout_fput32_little_endian(int v) {
  fout_fputc(v % 256);
  fout_fputc((v >> 8) % 256);
  fout_fputc((v >> 16) % 256);
  fout_fputc((v >> 24) % 256);
}

void fout_fput32(int v) {
  fout_fputc((v >> 24) % 256);
  fout_fputc((v >> 16) % 256);
  fout_fputc((v >> 8) % 256);
  fout_fputc(v % 256);
}

void fout_fput32(unsigned int v) {
  fout_fputc((v >> 24) % 256);
  fout_fputc((v >> 16) % 256);
  fout_fputc((v >> 8) % 256);
  fout_fputc(v % 256);
}

void fout_fput_vlint(unsigned long long v) {
  while (v >= 128) {
    fout_fputc((v & 127) + 128);
    v = (v >> 7) - 1;
  }
  fout_fputc(v);
}
void fout_fput_deflate_hdr(const unsigned char type, const unsigned char flags, 
                           const recompress_deflate_result& rdres,
                           const unsigned char* hdr, const unsigned hdr_length,
                           const bool inc_last_hdr_byte) {
  fout_fputc(1 + (rdres.zlib_perfect ? rdres.zlib_comp_level << 2 : 2) + flags);
  fout_fputc(type); // PDF/PNG/...
  if (rdres.zlib_perfect) {
    fout_fputc(((rdres.zlib_window_bits - 8) << 4) + rdres.zlib_mem_level);
  }
  fout_fput_vlint(hdr_length);
  if (!inc_last_hdr_byte) {
    own_fwrite(hdr, 1, hdr_length, g_precomp.ctx->fout);
  } else {
    own_fwrite(hdr, 1, hdr_length - 1, g_precomp.ctx->fout);
    fout_fputc(hdr[hdr_length - 1] + 1);
  }
}
void fin_fget_deflate_hdr(recompress_deflate_result& rdres, const unsigned char flags, 
                          unsigned char* hdr_data, unsigned& hdr_length, 
                          const bool inc_last_hdr_byte) {
  rdres.zlib_perfect = (flags & 2) == 0;
  if (rdres.zlib_perfect) {
    unsigned char zlib_params = fin_fgetc();
    rdres.zlib_comp_level  = (flags & 0x3c) >> 2;
    rdres.zlib_mem_level   = zlib_params & 0x0f;
    rdres.zlib_window_bits = ((zlib_params >> 4) & 0x7) + 8;
  }
  hdr_length = fin_fget_vlint();
  if (!inc_last_hdr_byte) {
    own_fread(hdr_data, 1, hdr_length, g_precomp.ctx->fin);
  } else {
    own_fread(hdr_data, 1, hdr_length - 1, g_precomp.ctx->fin);
    hdr_data[hdr_length - 1] = fin_fgetc() - 1;
  }
  own_fwrite(hdr_data, 1, hdr_length, g_precomp.ctx->fout);
}
void fout_fput_recon_data(const recompress_deflate_result& rdres) {
  if (!rdres.zlib_perfect) {
    fout_fput_vlint(rdres.recon_data.size());
    own_fwrite(rdres.recon_data.data(), 1, rdres.recon_data.size(), g_precomp.ctx->fout);
  }

  fout_fput_vlint(rdres.compressed_stream_size);
  fout_fput_vlint(rdres.uncompressed_stream_size);
}
void fin_fget_recon_data(recompress_deflate_result& rdres) {
  if (!rdres.zlib_perfect) {
    size_t sz = fin_fget_vlint();
    rdres.recon_data.resize(sz);
    own_fread(rdres.recon_data.data(), 1, rdres.recon_data.size(), g_precomp.ctx->fin);
  }

  rdres.compressed_stream_size = fin_fget_vlint();
  rdres.uncompressed_stream_size = fin_fget_vlint();
}
void fout_fput_uncompressed(const recompress_deflate_result& rdres, PrecompTmpFile& tmpfile) {
    write_decompressed_data_io_buf(rdres.uncompressed_stream_size, rdres.uncompressed_in_memory, tmpfile.file_path.c_str());
}
void fin_fget_uncompressed(const recompress_deflate_result&) {
}
void fout_fput_deflate_rec(const unsigned char type,
                           const recompress_deflate_result& rdres,
                           const unsigned char* hdr, const unsigned hdr_length, const bool inc_last,
                           const recursion_result& recres, PrecompTmpFile& tmpfile) {
  fout_fput_deflate_hdr(type, recres.success ? 128 : 0, rdres, hdr, hdr_length, inc_last);
  fout_fput_recon_data(rdres);
  
  // write decompressed data
  if (recres.success) {
    fout_fput_vlint(recres.file_length);
    write_decompressed_data(recres.file_length, recres.file_name.c_str());
    remove(recres.file_name.c_str());
  } else {
    fout_fput_uncompressed(rdres, tmpfile);
  }
}
bool fin_fget_deflate_rec(recompress_deflate_result& rdres, const unsigned char flags, 
                          unsigned char* hdr, unsigned& hdr_length, const bool inc_last,
                          int64_t& recursion_length, PrecompTmpFile& tmpfile) {
  fin_fget_deflate_hdr(rdres, flags, hdr, hdr_length, inc_last);
  fin_fget_recon_data(rdres);

  debug_sums(rdres);
  
  // write decompressed data
  if (flags & 128) {
    recursion_length = fin_fget_vlint();
    tmpfile.close();
    recursion_result r = recursion_decompress(recursion_length, tmpfile);
    debug_pos();
    bool result = try_reconstructing_deflate(*r.frecurse, g_precomp.ctx->fout, rdres);
    debug_pos();
    r.frecurse->close();
    remove(r.file_name.c_str());
    return result;
  } else {
    recursion_length = 0;
    debug_pos();
    bool result = try_reconstructing_deflate(g_precomp.ctx->fin, g_precomp.ctx->fout, rdres);
    debug_pos();
    return result;
  }
}

unsigned char fin_fgetc() {
  if (g_precomp.ctx->comp_decomp_state == P_CONVERT) g_precomp.ctx->compression_otf_method = conversion_from_method;

  if (g_precomp.ctx->compression_otf_method == OTF_NONE) {
    return g_precomp.ctx->fin.get();
  } else {
    unsigned char temp_buf[1];
    own_fread(temp_buf, 1, 1, g_precomp.ctx->fin);
    return temp_buf[0];
  }
}
int32_t fin_fget32_little_endian() {
  int32_t result = 0;
  result += ((long)fin_fgetc() << 0);
  result += ((long)fin_fgetc() << 8);
  result += ((long)fin_fgetc() << 16);
  result += ((long)fin_fgetc() << 24);
  return result;
}
int32_t fin_fget32() {
  int32_t result = 0;
  result += ((long)fin_fgetc() << 24);
  result += ((long)fin_fgetc() << 16);
  result += ((long)fin_fgetc() << 8);
  result += (long)fin_fgetc();
  return result;
}
long long fin_fget_vlint() {
  unsigned char c;
  long long v = 0, o = 0, s = 0;
  while ((c = fin_fgetc()) >= 128) {
    v += (((long long)(c & 127)) << s);
    s += 7;
    o = (o + 1) << 7;
  }
  return v + o + (((long long)c) << s);
}


void init_compress_otf() {
  if (g_precomp.ctx->comp_decomp_state == P_CONVERT) g_precomp.ctx->compression_otf_method = conversion_to_method;

  switch (g_precomp.ctx->compression_otf_method) {
    case OTF_BZIP2: { // bZip2
      otf_bz2_stream_c.bzalloc = NULL;
      otf_bz2_stream_c.bzfree = NULL;
      otf_bz2_stream_c.opaque = NULL;
      if (BZ2_bzCompressInit(&otf_bz2_stream_c, 9, 0, 0) != BZ_OK) {
        printf("ERROR: bZip2 init failed\n");
        exit(1);
      }
      break;
    }
    case OTF_XZ_MT: {
      uint64_t memory_usage = 0;
      uint64_t block_size = 0;
      uint64_t max_memory = g_precomp.switches.compression_otf_max_memory * 1024 * 1024LL;
      int threads = g_precomp.switches.compression_otf_thread_count;

      if (max_memory == 0) {
        max_memory = lzma_max_memory_default() * 1024 * 1024LL;
      }
      if (threads == 0) {
        threads = auto_detected_thread_count();
      }

      if (!init_encoder_mt(&otf_xz_stream_c, threads, max_memory, memory_usage, block_size, otf_xz_extra_params)) {
        printf("ERROR: xz Multi-Threaded init failed\n");
        exit(1);
      }

      std::string plural = "";
      if (threads > 1) {
        plural = "s";
      }
      std::cout << "Compressing with LZMA, " << threads << " thread" << plural.c_str() << ", memory usage: " << memory_usage / (1024 * 1024) << " MiB, block size: " << block_size / (1024 * 1024) << " MiB" << std::endl << std::endl;
      break;
    }
  }
}

int auto_detected_thread_count() {
  int threads = std::thread::hardware_concurrency();
  if (threads == 0) threads = 2;

  return threads;
}

// Return maximal memory to use per default for LZMA in MiB
// Use only 1 GiB in the 32-bit windows variant
// because of the 2 or 3 GiB limit on these systems
int lzma_max_memory_default() {
  int max_memory = 2048;
  #ifndef __unix
  #ifndef BIT64
  max_memory = 1024;
  #endif
  #endif
  return max_memory;
}

void denit_compress_otf() {
  if (g_precomp.ctx->comp_decomp_state == P_CONVERT) g_precomp.ctx->compression_otf_method = conversion_to_method;

  if (g_precomp.ctx->compression_otf_method > OTF_NONE) {

      // uncompressed data of length 0 ends compress-on-the-fly data
      char final_buf[9];
      for (int i = 0; i < 9; i++) {
        final_buf[i] = 0;
      }
      own_fwrite(final_buf, 1, 9, g_precomp.ctx->fout, true, true);
  }

  switch (g_precomp.ctx->compression_otf_method) {
    case OTF_BZIP2: { // bZip2

      (void)BZ2_bzCompressEnd(&otf_bz2_stream_c);
      break;
    }
    case OTF_XZ_MT: {
      (void)lzma_end(&otf_xz_stream_c);
      break;
    }
  }
}

void init_decompress_otf() {
  if (g_precomp.ctx->comp_decomp_state == P_CONVERT) g_precomp.ctx->compression_otf_method = conversion_from_method;

  switch (g_precomp.ctx->compression_otf_method) {
    case OTF_BZIP2: {
      otf_bz2_stream_d.bzalloc = NULL;
      otf_bz2_stream_d.bzfree = NULL;
      otf_bz2_stream_d.opaque = NULL;
      otf_bz2_stream_d.avail_in = 0;
      otf_bz2_stream_d.next_in = NULL;
      if (BZ2_bzDecompressInit(&otf_bz2_stream_d, 0, 0) != BZ_OK) {
        printf("ERROR: bZip2 init failed\n");
        exit(1);
      }
      break;
    }
    case OTF_XZ_MT: {
      if (!init_decoder(&otf_xz_stream_d)) {
        printf("ERROR: liblzma init failed\n");
        exit(1);
      }
    }
  }
  g_precomp.ctx->decompress_otf_end = false;
}

void denit_decompress_otf() {
  if (g_precomp.ctx->comp_decomp_state == P_CONVERT) g_precomp.ctx->compression_otf_method = conversion_from_method;

  switch (g_precomp.ctx->compression_otf_method) {
    case OTF_BZIP2: { // bZip2

      (void)BZ2_bzDecompressEnd(&otf_bz2_stream_d);
      break;
    }
    case OTF_XZ_MT: { // lzma2 multithreaded
      (void)lzma_end(&otf_xz_stream_d);
      break;
    }
  }
}

// get current time in ms
long long get_time_ms() {
  #ifndef __unix
    return GetTickCount();
  #else
    timeval t;
    gettimeofday(&t, NULL);
    return (t.tv_sec * 1000) + (t.tv_usec / 1000);
  #endif
}

// nice time output, input t in ms
// 2^32 ms maximum, so will display incorrect negative values after about 49 days
void printf_time(long long t) {
  printf("Time: ");
  if (t < 1000) { // several milliseconds
    printf("%li millisecond(s)\n", (long)t);
  } else if (t < 1000*60) { // several seconds
    printf("%li second(s), %li millisecond(s)\n", (long)(t / 1000), (long)(t % 1000));
  } else if (t < 1000*60*60) { // several minutes
    printf("%li minute(s), %li second(s)\n", (long)(t / (1000*60)), (long)((t / 1000) % 60));
  } else if (t < 1000*60*60*24) { // several hours
    printf("%li hour(s), %li minute(s), %li second(s)\n", (long)(t / (1000*60*60)), (long)((t / (1000*60)) % 60), (long)((t / 1000) % 60));
  } else {
    printf("%li day(s), %li hour(s), %li minute(s)\n", (long)(t / (1000*60*60*24)), (long)((t / (1000*60*60)) % 24), (long)((t / (1000*60)) % 60));
  }
}

char get_char_with_echo() {
  #ifndef __unix
    return getche();
  #else
    return fgetc(stdin);
  #endif
}

void print_work_sign(bool with_backspace) {
  if (g_precomp.switches.DEBUG_MODE) return;
  if ((get_time_ms() - work_sign_start_time) >= 250) {
    work_sign_var = (work_sign_var + 1) % 4;
    work_sign_start_time = get_time_ms();
    if (with_backspace) printf("\b\b\b\b\b\b");
    printf("%c     ", work_signs[work_sign_var]);
    fflush(stdout);
  } else if (!with_backspace) {
    printf("%c     ", work_signs[work_sign_var]);
    fflush(stdout);
  }
}

void print_debug_percent() {
  printf("(%.2f%%) ", (g_precomp.ctx->input_file_pos / (float)g_precomp.ctx->fin_length) * (g_precomp.ctx->global_max_percent - g_precomp.ctx->global_min_percent) + g_precomp.ctx->global_min_percent);
}

void show_progress(float percent, bool use_backspaces, bool check_time) {
  if (!check_time || ((get_time_ms() - g_precomp.ctx->sec_time) >= 250)) {
    if (use_backspaces) {
      printf("%s", std::string(6,'\b').c_str()); // backspace to remove work sign and 5 extra spaces
    }

    bool new_lzma_text = false;
    if ((g_precomp.ctx->is_show_lzma_progress()) && ((g_precomp.ctx->comp_decomp_state == P_COMPRESS) || ((g_precomp.ctx->comp_decomp_state == P_CONVERT) && (conversion_to_method == OTF_XZ_MT)))) {
      int snprintf_ret = snprintf(lzma_progress_text, 70, "lzma total/written/left: %i/%i/%i MiB ", lzma_mib_total, lzma_mib_written, lzma_mib_total - lzma_mib_written);
      if ((snprintf_ret > -1) && (snprintf_ret < 70)) {
        new_lzma_text = true;
        if ((old_lzma_progress_text_length > -1) && (use_backspaces)) {
          printf("%s", std::string(old_lzma_progress_text_length, '\b').c_str()); // backspaces to remove old lzma progress text
        }
        old_lzma_progress_text_length = snprintf_ret;
      }
    }

    if (use_backspaces) {
      printf("%s", std::string(8,'\b').c_str()); // backspaces to remove output from %6.2f%
    }
    printf("%6.2f%% ", percent);

    if (new_lzma_text) {
      printf("%s", lzma_progress_text);
    }

    print_work_sign(false);
    g_precomp.ctx->sec_time = get_time_ms();
  }
}

void ctrl_c_handler(int sig) {
  printf("\n\nCTRL-C detected\n");
  (void) signal(SIGINT, SIG_DFL);

  error(ERR_CTRL_C);
}
