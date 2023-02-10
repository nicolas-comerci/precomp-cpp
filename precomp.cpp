#include "precomp.h"
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
  print_to_console("ERROR: Only + or - for this switch (%s) allowed\n", c);
  exit(1);
  return false;
}

int parseInt(const char*& c, const char* context, int too_big_error_code = 0) {
  if (*c < '0' || *c > '9') {
    print_to_console("ERROR: Number needed to set %s\n", context);
    exit(1);
  }
  int val = *c++ - '0';
  while (*c >= '0' && *c <= '9') {
    if (val >= INT_MAX / 10 - 1) {
      if (too_big_error_code != 0) {
        error(too_big_error_code);
      }
      print_to_console("ERROR: Number too big for %s\n", context);
      exit(1);
    }
    val = val * 10 + *c++ - '0';
  }
  return val;
}
int parseIntUntilEnd(const char* c, const char* context, int too_big_error_code = 0) {
  for (int i = 0; c[i]; ++i) {
    if (c[i] < '0' || c[i] > '9') {
      print_to_console("ERROR: Only numbers allowed for %s\n", context);
      exit(1);
    }
  }
  const char* x = c;
  return parseInt(x, context, too_big_error_code);
}
int64_t parseInt64(const char*& c, const char* context, int too_big_error_code = 0) {
  if (*c < '0' || *c > '9') {
    print_to_console("ERROR: Number needed to set %s\n", context);
    exit(1);
  }
  int64_t val = *c++ - '0';
  while (*c >= '0' && *c <= '9') {
    if (val >= INT64_MAX / 10 - 1) {
      if (too_big_error_code != 0) {
        error(too_big_error_code);
      }
      print_to_console("ERROR: Number too big for %s\n", context);
      exit(1);
    }
    val = val * 10 + *c++ - '0';
  }
  return val;
}
int64_t parseInt64UntilEnd(const char* c, const char* context, int too_big_error_code = 0) {
  for (int i = 0; c[i]; ++i) {
    if (c[i] < '0' || c[i] > '9') {
      print_to_console("ERROR: Only numbers allowed for %s\n", context);
      exit(1);
    }
  }
  const char* x = c;
  return parseInt64(x, context, too_big_error_code);
}

#ifdef COMFORT
bool check_for_pcf_file(Precomp& precomp_mgr) {
  force_seekg(*precomp_mgr.ctx->fin, 0, std::ios_base::beg);

  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 3);
  if ((precomp_mgr.in[0] == 'P') && (precomp_mgr.in[1] == 'C') && (precomp_mgr.in[2] == 'F')) {
  } else {
    return false;
  }

  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 3);
  if ((precomp_mgr.in[0] == V_MAJOR) && (precomp_mgr.in[1] == V_MINOR) && (precomp_mgr.in[2] == V_MINOR2)) {
  } else {
    print_to_console("Input file %s was made with a different Precomp version\n", precomp_mgr.ctx->input_file_name.c_str());
    print_to_console("PCF version info: %i.%i.%i\n", precomp_mgr.in[0], precomp_mgr.in[1], precomp_mgr.in[2]);
    exit(1);
  }

  // skip compression method
  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 1);

  std::string header_filename = "";
  char c;
  do {
    c = precomp_mgr.ctx->fin->get();
    if (c != 0) header_filename += c;
  } while (c != 0);

  // append output filename to the executable directory
  std::string exec_dir = std::filesystem::current_path().string();
  std::string header_path = exec_dir;
  header_path += PATH_DELIM;
  header_path += header_filename;

  if (precomp_mgr.ctx->output_file_name.empty()) {
    precomp_mgr.ctx->output_file_name = header_path;
  }

  return true;
}
#endif

bool file_exists(const char* filename) {
  std::fstream fin;
  bool retval = false;

  fin.open(filename, std::ios::in);
  retval = fin.is_open();
  fin.close();

  return retval;
}

void ctrl_c_handler(int sig) {
  print_to_console("\n\nCTRL-C detected\n");
  (void) signal(SIGINT, SIG_DFL);

  error(ERR_CTRL_C);
}

#ifndef PRECOMPDLL
int main(int argc, char* argv[])
{
  Precomp precomp_mgr;
  int return_errorlevel = 0;

  // register CTRL-C handler
  (void)signal(SIGINT, ctrl_c_handler);

#ifndef COMFORT
  switch (init(precomp_mgr, argc, argv)) {
#else
  switch (init_comfort(precomp_mgr, argc, argv)) {
#endif

  case P_COMPRESS:
  {
    precomp_mgr.start_time = get_time_ms();
    if (!compress_file(precomp_mgr)) { // none of the streams could be decompressed
      return_errorlevel = RETURN_NOTHING_DECOMPRESSED;
    }
    break;
  }

  case P_DECOMPRESS:
  {
    precomp_mgr.start_time = get_time_ms();
    decompress_file(precomp_mgr);
    break;
  }

  case P_CONVERT:
  {
    precomp_mgr.start_time = get_time_ms();
    convert_file(precomp_mgr);
    break;
  }
  }

#ifdef COMFORT
  wait_for_key();
#endif

  return return_errorlevel;
  }
#endif

#ifndef PRECOMPDLL
#ifndef COMFORT
int init(Precomp& precomp_mgr, int argc, char* argv[]) {
  int i, j;
  bool appended_pcf = false;

  print_to_console("\n");
  if (V_MINOR2 == 0) {
    print_to_console("Precomp v%i.%i %s %s - %s version", V_MAJOR, V_MINOR, V_OS, V_BIT, V_STATE);
  }
  else {
    print_to_console("Precomp v%i.%i.%i %s %s - %s version", V_MAJOR, V_MINOR, V_MINOR2, V_OS, V_BIT, V_STATE);
  }
  print_to_console(" - %s\n", V_MSG);
  print_to_console("Apache 2.0 License - Copyright 2006-2021 by Christian Schneider\n");
  print_to_console("  preflate v0.3.5 support - Copyright 2018 by Dirk Steinke\n");
  print_to_console("  stdin/stdout support fork - Copyright 2022 by Nicolas Comerci\n\n");

  // init compression and memory level count
  bool use_zlib_level[81];
  for (i = 0; i < 81; i++) {
    precomp_mgr.obsolete.comp_mem_level_count[i] = 0;
    precomp_mgr.obsolete.zlib_level_was_used[i] = false;
    use_zlib_level[i] = true;
  }

  // init MP3 suppression
  for (i = 0; i < 16; i++) {
    precomp_mgr.ctx->suppress_mp3_type_until[i] = -1;
  }
  precomp_mgr.ctx->suppress_mp3_big_value_pairs_sum = -1;
  precomp_mgr.ctx->suppress_mp3_non_zero_padbits_sum = -1;
  precomp_mgr.ctx->suppress_mp3_inconsistent_emphasis_sum = -1;
  precomp_mgr.ctx->suppress_mp3_inconsistent_original_bit = -1;
  precomp_mgr.ctx->mp3_parsing_cache_second_frame = -1;

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
          precomp_mgr.switches.intense_mode = true;
          if (strlen(argv[i]) > 8) {
            precomp_mgr.switches.intense_mode_depth_limit = parseIntUntilEnd(argv[i] + 8, "intense mode level limit", ERR_INTENSE_MODE_LIMIT_TOO_BIG);
          }
        }
        else {
          long long ignore_pos = parseInt64UntilEnd(argv[i] + 2, "ignore position", ERR_IGNORE_POS_TOO_BIG);
          precomp_mgr.switches.ignore_list.push_back(ignore_pos);
        }
        break;
      }
      case 'D':
      {
        if (recursion_depth_set) {
          error(ERR_ONLY_SET_RECURSION_DEPTH_ONCE);
        }
        precomp_mgr.max_recursion_depth = parseIntUntilEnd(argv[i] + 2, "maximal recursion depth", ERR_RECURSION_DEPTH_TOO_BIG);
        recursion_depth_set = true;
        break;
      }
      case 'S':
      {
        if (min_ident_size_set) {
          error(ERR_ONLY_SET_MIN_SIZE_ONCE);
        }
        precomp_mgr.switches.min_ident_size = parseIntUntilEnd(argv[i] + 2, "minimal identical byte size", ERR_IDENTICAL_BYTE_SIZE_TOO_BIG);
        min_ident_size_set = true;
        break;
      }
      case 'B':
      {
        if (parsePrefixText(argv[i] + 1, "brute")) { // brute mode
          precomp_mgr.switches.brute_mode = true;
          if (strlen(argv[i]) > 6) {
            precomp_mgr.switches.brute_mode_depth_limit = parseIntUntilEnd(argv[i] + 6, "brute mode level limit", ERR_BRUTE_MODE_LIMIT_TOO_BIG);
          }
        }
        else if (!parseSwitch(precomp_mgr.switches.use_brunsli, argv[i] + 1, "brunsli")
          && !parseSwitch(precomp_mgr.switches.use_brotli, argv[i] + 1, "brotli")) {
          print_to_console("ERROR: Unknown switch \"%s\"\n", argv[i]);
          exit(1);
        }
        break;
      }
      case 'L':
      {
        if (parsePrefixText(argv[i] + 1, "longhelp")) {
          long_help = true;
        }
        else if (toupper(argv[i][2]) == 'M') { // LZMA max. memory
          if (lzma_max_memory_set) {
            error(ERR_ONLY_SET_LZMA_MEMORY_ONCE);
          }
          precomp_mgr.switches.compression_otf_max_memory = parseIntUntilEnd(argv[i] + 3, "LZMA maximal memory");
          lzma_max_memory_set = true;
        }
        else if (toupper(argv[i][2]) == 'T') { // LZMA thread count
          if (lzma_thread_count_set) {
            error(ERR_ONLY_SET_LZMA_THREAD_ONCE);
          }
          precomp_mgr.switches.compression_otf_thread_count = parseIntUntilEnd(argv[i] + 3, "LZMA thread count");
          lzma_thread_count_set = true;
        }
        else if (toupper(argv[i][2]) == 'L') {
          if (toupper(argv[i][3]) == 'C') {
            precomp_mgr.otf_xz_extra_params->lc = 1 + parseIntUntilEnd(argv[i] + 4, "LZMA literal context bits");
            int lclp = (precomp_mgr.otf_xz_extra_params->lc != 0 ? precomp_mgr.otf_xz_extra_params->lc - 1 : LZMA_LC_DEFAULT)
              + (precomp_mgr.otf_xz_extra_params->lp != 0 ? precomp_mgr.otf_xz_extra_params->lp - 1 : LZMA_LP_DEFAULT);
            if (lclp < LZMA_LCLP_MIN || lclp > LZMA_LCLP_MAX) {
              print_to_console("sum of LZMA lc (default %d) and lp (default %d) must be inside %d..%d\n",
                LZMA_LC_DEFAULT, LZMA_LP_DEFAULT, LZMA_LCLP_MIN, LZMA_LCLP_MAX);
              exit(1);
            }
          }
          else if (toupper(argv[i][3]) == 'P') {
            precomp_mgr.otf_xz_extra_params->lp = 1 + parseIntUntilEnd(argv[i] + 4, "LZMA literal position bits");
            int lclp = (precomp_mgr.otf_xz_extra_params->lc != 0 ? precomp_mgr.otf_xz_extra_params->lc - 1 : LZMA_LC_DEFAULT)
              + (precomp_mgr.otf_xz_extra_params->lp != 0 ? precomp_mgr.otf_xz_extra_params->lp - 1 : LZMA_LP_DEFAULT);
            if (lclp < LZMA_LCLP_MIN || lclp > LZMA_LCLP_MAX) {
              print_to_console("sum of LZMA lc (default %d) and lp (default %d) must be inside %d..%d\n",
                LZMA_LC_DEFAULT, LZMA_LP_DEFAULT, LZMA_LCLP_MIN, LZMA_LCLP_MAX);
              exit(1);
            }
          }
          else {
            print_to_console("ERROR: Unknown switch \"%s\"\n", argv[i]);
            exit(1);
          }
        }
        else if (toupper(argv[i][2]) == 'P') {
          if (toupper(argv[i][3]) == 'B') {
            precomp_mgr.otf_xz_extra_params->pb = 1 + parseIntUntilEnd(argv[i] + 4, "LZMA position bits");
            int pb = precomp_mgr.otf_xz_extra_params->pb != 0 ? precomp_mgr.otf_xz_extra_params->pb - 1 : LZMA_PB_DEFAULT;
            if (pb < LZMA_PB_MIN || pb > LZMA_PB_MAX) {
              print_to_console("LZMA pb (default %d) must be inside %d..%d\n",
                LZMA_PB_DEFAULT, LZMA_PB_MIN, LZMA_PB_MAX);
              exit(1);
            }
          }
          else {
            print_to_console("ERROR: Unknown switch \"%s\"\n", argv[i]);
            exit(1);
          }
        }
        else if (toupper(argv[i][2]) == 'F') { // LZMA filters
          if (lzma_filters_set) {
            error(ERR_ONLY_SET_LZMA_FILTERS_ONCE);
          }
          switch (argv[i][3]) {
          case '+':
            break;
          case '-':
            if (argv[i][4] != 0) {
              print_to_console("ERROR: \"-lf-\" must not be followed by anything\n");
              exit(1);
              break;
            }
            break;
          default:
            print_to_console("ERROR: Only + or - after \"-lf\" allowed\n");
            exit(1);
            break;
          }

          int argindex = 4;
          while (argv[i][argindex] != 0) {
            switch (toupper(argv[i][argindex])) {
            case 'X':
              precomp_mgr.otf_xz_extra_params->enable_filter_x86 = true;
              break;
            case 'P':
              precomp_mgr.otf_xz_extra_params->enable_filter_powerpc = true;
              break;
            case 'I':
              precomp_mgr.otf_xz_extra_params->enable_filter_ia64 = true;
              break;
            case 'A':
              precomp_mgr.otf_xz_extra_params->enable_filter_arm = true;
              break;
            case 'T':
              precomp_mgr.otf_xz_extra_params->enable_filter_armthumb = true;
              break;
            case 'S':
              precomp_mgr.otf_xz_extra_params->enable_filter_sparc = true;
              break;
            case 'D':
            {
              argindex++;
              char nextchar = argv[i][argindex];
              if ((nextchar < '0') || (nextchar > '9')) {
                print_to_console("ERROR: LZMA delta filter must be followed by a distance (%d..%d)\n",
                  LZMA_DELTA_DIST_MIN, LZMA_DELTA_DIST_MAX);
                exit(1);
              }
              precomp_mgr.otf_xz_extra_params->enable_filter_delta = true;
              while ((argv[i][argindex] > '0') && (argv[i][argindex] < '9')) {
                precomp_mgr.otf_xz_extra_params->filter_delta_distance *= 10;
                precomp_mgr.otf_xz_extra_params->filter_delta_distance += (argv[i][argindex] - '0');
                argindex++;
              }
              if (precomp_mgr.otf_xz_extra_params->filter_delta_distance < LZMA_DELTA_DIST_MIN
                || precomp_mgr.otf_xz_extra_params->filter_delta_distance > LZMA_DELTA_DIST_MAX) {
                print_to_console("ERROR: LZMA delta filter distance must be in range %d..%d\n",
                  LZMA_DELTA_DIST_MIN, LZMA_DELTA_DIST_MAX);
                exit(1);
              }
              argindex--;
            }
            break;
            default:
              print_to_console("ERROR: Unknown LZMA filter type \"%c\"\n", argv[i][argindex]);
              exit(1);
              break;
            }
            precomp_mgr.switches.otf_xz_filter_used_count++;
            if (precomp_mgr.switches.otf_xz_filter_used_count > LZMA_FILTERS_MAX - 1) {
              print_to_console("ERROR: Only up to %d LZMA filters can be used at the same time\n",
                LZMA_FILTERS_MAX - 1);
              exit(1);
            }
            argindex++;
          }
          lzma_filters_set = true;
          break;
        }
        else {
          print_to_console("ERROR: Unknown switch \"%s\"\n", argv[i]);
          exit(1);
        }
        break;
      }
      case 'P':
      {
        if (!parseSwitch(precomp_mgr.switches.pdf_bmp_mode, argv[i] + 1, "pdfbmp")
          && !parseSwitch(precomp_mgr.switches.prog_only, argv[i] + 1, "progonly")
          && !parseSwitch(precomp_mgr.switches.preflate_verify, argv[i] + 1, "pfverify")
          && !parseSwitch(precomp_mgr.switches.use_packjpg_fallback, argv[i] + 1, "packjpg")) {
          if (parsePrefixText(argv[i] + 1, "pfmeta")) {
            int mbsize = parseIntUntilEnd(argv[i] + 7, "preflate meta block size");
            if (mbsize >= INT_MAX / 1024) {
              print_to_console("preflate meta block size set too big\n");
              exit(1);
            }
            precomp_mgr.switches.preflate_meta_block_size = mbsize * 1024;
          }
          else {
            print_to_console("ERROR: Unknown switch \"%s\"\n", argv[i]);
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
          precomp_mgr.switches.use_pdf = false;
          precomp_mgr.switches.use_zip = false;
          precomp_mgr.switches.use_gzip = false;
          precomp_mgr.switches.use_png = false;
          precomp_mgr.switches.use_gif = false;
          precomp_mgr.switches.use_jpg = false;
          precomp_mgr.switches.use_mp3 = false;
          precomp_mgr.switches.use_swf = false;
          precomp_mgr.switches.use_base64 = false;
          precomp_mgr.switches.use_bzip2 = false;
          set_to = true;
          break;
        case '-':
          precomp_mgr.switches.use_pdf = true;
          precomp_mgr.switches.use_zip = true;
          precomp_mgr.switches.use_gzip = true;
          precomp_mgr.switches.use_png = true;
          precomp_mgr.switches.use_gif = true;
          precomp_mgr.switches.use_jpg = true;
          precomp_mgr.switches.use_mp3 = true;
          precomp_mgr.switches.use_swf = true;
          precomp_mgr.switches.use_base64 = true;
          precomp_mgr.switches.use_bzip2 = true;
          set_to = false;
          break;
        default:
          print_to_console("ERROR: Only + or - for type switch allowed\n");
          exit(1);
          break;
        }
        for (j = 3; j < (int)strlen(argv[i]); j++) {
          switch (toupper(argv[i][j])) {
          case 'P': // PDF
            precomp_mgr.switches.use_pdf = set_to;
            break;
          case 'Z': // ZIP
            precomp_mgr.switches.use_zip = set_to;
            break;
          case 'G': // GZip
            precomp_mgr.switches.use_gzip = set_to;
            break;
          case 'N': // PNG
            precomp_mgr.switches.use_png = set_to;
            break;
          case 'F': // GIF
            precomp_mgr.switches.use_gif = set_to;
            break;
          case 'J': // JPG
            precomp_mgr.switches.use_jpg = set_to;
            break;
          case '3': // MP3
            precomp_mgr.switches.use_mp3 = set_to;
            break;
          case 'S': // SWF
            precomp_mgr.switches.use_swf = set_to;
            break;
          case 'M': // MIME Base64
            precomp_mgr.switches.use_base64 = set_to;
            break;
          case 'B': // bZip2
            precomp_mgr.switches.use_bzip2 = set_to;
            break;
          default:
            print_to_console("ERROR: Invalid compression type %c\n", argv[i][j]);
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
          precomp_mgr.ctx->compression_otf_method = OTF_NONE;
          break;
        case 'B': // bZip2
          precomp_mgr.ctx->compression_otf_method = OTF_BZIP2;
          break;
        case 'L': // lzma2 multithreaded
          precomp_mgr.ctx->compression_otf_method = OTF_XZ_MT;
          break;
        default:
          print_to_console("ERROR: Invalid compression method %c\n", argv[i][2]);
          exit(1);
          break;
        }
        if (argv[i][3] != 0) { // Extra Parameters?
          print_to_console("ERROR: Unknown switch \"%s\"\n", argv[i]);
          exit(1);
        }
        break;
      }
      case 'N':
      {
        switch (toupper(argv[i][2])) {
        case 'N': // no compression
          precomp_mgr.conversion_to_method = OTF_NONE;
          break;
        case 'B': // bZip2
          precomp_mgr.conversion_to_method = OTF_BZIP2;
          break;
        case 'L': // lzma2 multithreaded
          precomp_mgr.conversion_to_method = OTF_XZ_MT;
          break;
        default:
          print_to_console("ERROR: Invalid conversion method %c\n", argv[i][2]);
          exit(1);
          break;
        }
        if (argv[i][3] != 0) { // Extra Parameters?
          print_to_console("ERROR: Unknown switch \"%s\"\n", argv[i]);
          exit(1);
        }
        operation = P_CONVERT;
        break;
      }
      case 'V':
      {
        DEBUG_MODE = true;
        if (argv[i][2] != 0) { // Extra Parameters?
          print_to_console("ERROR: Unknown switch \"%s\"\n", argv[i]);
          exit(1);
        }
        break;
      }
      case 'R':
      {
        operation = P_DECOMPRESS;
        if (argv[i][2] != 0) { // Extra Parameters?
          print_to_console("ERROR: Unknown switch \"%s\"\n", argv[i]);
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

          for (j = 0; j < ((int)strlen(argv[i]) - 3); j += 3) {
            if ((j + 5) < (int)strlen(argv[i])) {
              if (argv[i][j + 5] != ',') {
                print_to_console("ERROR: zLib levels have to be separated with commas\n");
                exit(1);
              }
            }
            if ((j + 4) >= (int)strlen(argv[i])) {
              print_to_console("ERROR: Last zLib level is incomplete\n");
              exit(1);
            }
            int comp_level_to_use = (char(argv[i][j + 3]) - '1');
            int mem_level_to_use = (char(argv[i][j + 4]) - '1');
            if (((comp_level_to_use >= 0) && (comp_level_to_use <= 8))
              && ((mem_level_to_use >= 0) && (mem_level_to_use <= 8))) {
              use_zlib_level[comp_level_to_use + mem_level_to_use * 9] = true;
            }
            else {
              print_to_console("ERROR: Invalid zlib level %c%c\n", argv[i][j + 3], argv[i][j + 4]);
              exit(1);
            }
          }
          break;
        }
        else {
          print_to_console("ERROR: Unknown switch \"%s\"\n", argv[i]);
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
        precomp_mgr.ctx->output_file_name = argv[i] + 2;

        // check for backslash in file name
        const char* backslash_at_pos = strrchr(precomp_mgr.ctx->output_file_name.c_str(), PATH_DELIM);

        // dot in output file name? If not, use .pcf extension
        const char* dot_at_pos = strrchr(precomp_mgr.ctx->output_file_name.c_str(), '.');
        if (precomp_mgr.ctx->output_file_name.compare("stdout") != 0 && (dot_at_pos == NULL) || ((backslash_at_pos != NULL) && (backslash_at_pos > dot_at_pos))) {
          precomp_mgr.ctx->output_file_name += ".pcf";
          appended_pcf = true;
        }

        break;
      }

      case 'M':
      {
        if (!parseSwitch(precomp_mgr.switches.use_mjpeg, argv[i] + 1, "mjpeg")) {
          print_to_console("ERROR: Unknown switch \"%s\"\n", argv[i]);
          exit(1);
        }
        break;
      }

      case 'F':
      {
        precomp_mgr.switches.fast_mode = true;
        if (argv[i][2] != 0) { // Extra Parameters?
          print_to_console("ERROR: Unknown switch \"%s\"\n", argv[i]);
          exit(1);
        }
        break;
      }
      case 'E':
      {
        preserve_extension = true;
        if (argv[i][2] != 0) { // Extra Parameters?
          print_to_console("ERROR: Unknown switch \"%s\"\n", argv[i]);
          exit(1);
        }
        break;
      }
      default:
      {
        print_to_console("ERROR: Unknown switch \"%s\"\n", argv[i]);
        exit(1);
      }
      }
    }
    else { // no switch
      if (input_file_given) {
        error(ERR_MORE_THAN_ONE_INPUT_FILE);
      }

      input_file_given = true;
      precomp_mgr.ctx->input_file_name = argv[i];

      if (precomp_mgr.ctx->input_file_name.compare("stdin") == 0) {
        if (operation != P_DECOMPRESS) {
          print_to_console("ERROR: Reading from stdin or writing to stdout only supported for recompressing.\n");
          exit(1);
        }
        // Read binary from stdin
        SET_BINARY_MODE(STDIN);
        precomp_mgr.ctx->fin->rdbuf(std::cin.rdbuf());
      }
      else {
        precomp_mgr.ctx->fin_length = std::filesystem::file_size(argv[i]);

        auto fin = std::unique_ptr<std::ifstream>(new std::ifstream());
        fin->open(argv[i], std::ios_base::in | std::ios_base::binary);
        if (!fin->is_open()) {
          print_to_console("ERROR: Input file \"%s\" doesn't exist\n", precomp_mgr.ctx->input_file_name.c_str());

          exit(1);
        }
        precomp_mgr.ctx->fin = std::move(fin);
      }

      // output file given? If not, use input filename with .pcf extension
      if ((!output_file_given) && (operation == P_COMPRESS)) {
        if (!preserve_extension) {
          precomp_mgr.ctx->output_file_name = precomp_mgr.ctx->input_file_name;
          const char* backslash_at_pos = strrchr(precomp_mgr.ctx->output_file_name.c_str(), PATH_DELIM);
          const char* dot_at_pos = strrchr(precomp_mgr.ctx->output_file_name.c_str(), '.');
          if ((dot_at_pos == NULL) || ((backslash_at_pos != NULL) && (dot_at_pos < backslash_at_pos))) {
            precomp_mgr.ctx->output_file_name += ".pcf";
          }
          else {
            precomp_mgr.ctx->output_file_name = std::string(
              precomp_mgr.ctx->output_file_name.c_str(),
              dot_at_pos - precomp_mgr.ctx->output_file_name.c_str()
            );
            // same as output file because input file had .pcf extension?
            if (precomp_mgr.ctx->input_file_name.compare(precomp_mgr.ctx->output_file_name + ".pcf") == 0) {
              precomp_mgr.ctx->output_file_name += "_pcf.pcf";
            }
            else {
              precomp_mgr.ctx->output_file_name += ".pcf";
            }
          }
        }
        else {
          precomp_mgr.ctx->output_file_name = precomp_mgr.ctx->input_file_name + ".pcf";
        }
        output_file_given = true;
      }
      else if ((!output_file_given) && (operation == P_CONVERT)) {
        print_to_console("ERROR: Please specify an output file for conversion\n");
        exit(1);
      }

      valid_syntax = true;
    }

  }

  if (!valid_syntax) {
    print_to_console("Usage: precomp [-switches] input_file\n\n");
    if (long_help) {
      print_to_console("Switches (and their <default values>):\n");
    }
    else {
      print_to_console("Common switches (and their <default values>):\n");
    }
    print_to_console("  r            \"Recompress\" PCF file (restore original file)\n");
    print_to_console("  o[filename]  Write output to [filename] <[input_file].pcf or file in header>\n");
    print_to_console("  e            preserve original extension of input name for output name <off>\n");
    print_to_console("  c[lbn]       Compression method to use, l = lzma2, b = bZip2, n = none <l>\n");
    print_to_console("  lm[amount]   Set maximal LZMA memory in MiB <%i>\n", lzma_max_memory_default());
    print_to_console("  lt[count]    Set LZMA thread count <auto-detect: %i>\n", auto_detected_thread_count());
    if (long_help) {
      print_to_console("  lf[+-][xpiatsd] Set LZMA filters (up to %d of them can be combined) <none>\n",
        LZMA_FILTERS_MAX - 1);
      print_to_console("                  lf+[xpiatsd] = enable these filters, lf- = disable all\n");
      print_to_console("                  X = x86, P = PowerPC, I = IA-64, A = ARM, T = ARM-Thumb\n");
      print_to_console("                  S = SPARC, D = delta (must be followed by distance %d..%d)\n",
        LZMA_DELTA_DIST_MIN, LZMA_DELTA_DIST_MAX);
      print_to_console("  llc[bits]    Set LZMA literal context bits <%d>\n", LZMA_LC_DEFAULT);
      print_to_console("  llp[bits]    Set LZMA literal position bits <%d>\n", LZMA_LP_DEFAULT);
      print_to_console("               The sum of lc and lp must be inside %d..%d\n", LZMA_LCLP_MIN, LZMA_LCLP_MAX);
      print_to_console("  lpb[bits]    Set LZMA position bits, must be inside %d..%d <%d>\n",
        LZMA_PB_MIN, LZMA_PB_MAX, LZMA_PB_DEFAULT);
    }
    else {
      print_to_console("  lf[+-][xpiatsd] Set LZMA filters (up to 3, see long help for details) <none>\n");
    }
    print_to_console("  n[lbn]       Convert a PCF file to this compression (same as above) <off>\n");
    print_to_console("  v            Verbose (debug) mode <off>\n");
    print_to_console("  d[depth]     Set maximal recursion depth <10>\n");
    //print_to_console("  zl[1..9][1..9] zLib levels to try for compression (comma separated) <all>\n");
    if (long_help) {
      print_to_console("  pfmeta[amount] Split deflate streams into meta blocks of this size in KiB <2048>\n");
      print_to_console("  pfverify       Force preflate to verify its generated reconstruction data\n");
    }
    print_to_console("  intense      Detect raw zLib headers, too. Slower and more sensitive <off>\n");
    if (long_help) {
      print_to_console("  brute        Brute force zLib detection. VERY Slow and most sensitive <off>\n");
    }
    print_to_console("  t[+-][pzgnfjsmb3] Compression type switch <all enabled>\n");
    print_to_console("              t+ = enable these types only, t- = enable all types except these\n");
    print_to_console("              P = PDF, Z = ZIP, G = GZip, N = PNG, F = GIF, J = JPG\n");
    print_to_console("              S = SWF, M = MIME Base64, B = bZip2, 3 = MP3\n");

    if (!long_help) {
      print_to_console("  longhelp     Show long help\n");
    }
    else {
      print_to_console("  f            Fast mode, use first found compression lvl for all streams <off>\n");
      print_to_console("  i[pos]       Ignore stream at input file position [pos] <none>\n");
      print_to_console("  s[size]      Set minimal identical byte size to [size] <4 (64 intense mode)>\n");
      print_to_console("  pdfbmp[+-]   Wrap a BMP header around PDF images <off>\n");
      print_to_console("  progonly[+-] Recompress progressive JPGs only (useful for PAQ) <off>\n");
      print_to_console("  mjpeg[+-]    Insert huffman table for MJPEG recompression <on>\n");
      print_to_console("  brunsli[+-]  Prefer brunsli to packJPG for JPG streams <on>\n");
      print_to_console("  brotli[+-]   Use brotli to compress metadata in JPG streams <off>\n");
      print_to_console("  packjpg[+-]  Use packJPG for JPG streams and fallback if brunsli fails <on>\n");
      print_to_console("\n");
      print_to_console("  You can use an optional number following -intense and -brute to set a\n");
      print_to_console("  limit for how deep in recursion they should be used. E.g. -intense0 means\n");
      print_to_console("  that intense mode will be used but not in recursion, -intense2 that only\n");
      print_to_console("  streams up to recursion depth 2 will be treated intense (3 or higher in\n");
      print_to_console("  this case won't). Using a sensible setting here can save you some time.\n");
    }

    exit(1);
  }

  if (operation == P_DECOMPRESS) {
    // if .pcf was appended, remove it
    if (appended_pcf) {
      precomp_mgr.ctx->output_file_name = precomp_mgr.ctx->output_file_name.substr(0, precomp_mgr.ctx->output_file_name.length() - 4);
    }
    read_header(precomp_mgr);
  }

  if (output_file_given && precomp_mgr.ctx->output_file_name.compare("stdout") == 0) {
    // Write binary to stdout
    SET_BINARY_MODE(STDOUT);
    precomp_mgr.ctx->fout->rdbuf(std::cout.rdbuf());
  }
  else {
    if (file_exists(precomp_mgr.ctx->output_file_name.c_str())) {
      print_to_console("Output file \"%s\" exists. Overwrite (y/n)? ", precomp_mgr.ctx->output_file_name.c_str());
      char ch = get_char_with_echo();
      if ((ch != 'Y') && (ch != 'y')) {
        print_to_console("\n");
        exit(0);
      }
      else {
#ifndef __unix
        print_to_console("\n\n");
#else
        print_to_console("\n");
#endif
      }
    }

    auto fout = std::unique_ptr<std::ofstream>(new std::ofstream());
    fout->open(precomp_mgr.ctx->output_file_name.c_str(), std::ios_base::out | std::ios_base::binary);
    if (!fout->is_open()) {
      print_to_console("ERROR: Can't create output file \"%s\"\n", precomp_mgr.ctx->output_file_name.c_str());
      exit(1);
    }
    precomp_mgr.ctx->fout = std::move(fout);
  }

  print_to_console("Input file: %s\n", precomp_mgr.ctx->input_file_name.c_str());
  print_to_console("Output file: %s\n\n", precomp_mgr.ctx->output_file_name.c_str());
  if (DEBUG_MODE) {
    if (min_ident_size_set) {
      print_to_console("\n");
      print_to_console("Minimal ident size set to %i bytes\n", precomp_mgr.switches.min_ident_size);
    }
    if (!precomp_mgr.switches.ignore_list.empty()) {
      print_to_console("\n");
      print_to_console("Ignore position list:\n");
      for (auto ignore_pos : precomp_mgr.switches.ignore_list) {
        std::cout << ignore_pos << std::endl;
      }
      print_to_console("\n");
    }
  }

  if (operation == P_CONVERT) convert_header(precomp_mgr);

  if (level_switch) {

    for (i = 0; i < 81; i++) {
      if (use_zlib_level[i]) {
        precomp_mgr.obsolete.comp_mem_level_count[i] = 0;
      }
      else {
        precomp_mgr.obsolete.comp_mem_level_count[i] = -1;
      }
    }

    precomp_mgr.switches.level_switch_used = true;

  }

  packjpg_mp3_dll_msg();

  return operation;
}
#else
int init_comfort(Precomp& precomp_mgr, int argc, char* argv[]) {
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

  print_to_console("\n");
  if (V_MINOR2 == 0) {
    print_to_console("Precomp Comfort v%i.%i %s %s - %s version", V_MAJOR, V_MINOR, V_OS, V_BIT, V_STATE);
  }
  else {
    print_to_console("Precomp Comfort v%i.%i.%i %s %s - %s version", V_MAJOR, V_MINOR, V_MINOR2, V_OS, V_BIT, V_STATE);
  }
  print_to_console(" - %s\n", V_MSG);
  print_to_console("Apache 2.0 License - Copyright 2006-2021 by Christian Schneider\n\n");

  // init compression and memory level count
  bool use_zlib_level[81];
  for (i = 0; i < 81; i++) {
    precomp_mgr.obsolete.comp_mem_level_count[i] = 0;
    precomp_mgr.obsolete.zlib_level_was_used[i] = false;
    use_zlib_level[i] = true;
  }

  // init MP3 suppression
  for (i = 0; i < 16; i++) {
    precomp_mgr.ctx->suppress_mp3_type_until[i] = -1;
  }
  precomp_mgr.ctx->suppress_mp3_big_value_pairs_sum = -1;
  precomp_mgr.ctx->suppress_mp3_non_zero_padbits_sum = -1;
  precomp_mgr.ctx->suppress_mp3_inconsistent_emphasis_sum = -1;
  precomp_mgr.ctx->suppress_mp3_inconsistent_original_bit = -1;
  precomp_mgr.ctx->mp3_parsing_cache_second_frame = -1;

  // parse parameters (should be input file only)
  if (argc == 1) {
    print_to_console("Usage:\n");
    print_to_console("Drag and drop a file on the executable to precompress/restore it.\n");
    print_to_console("Edit INI file for parameters.\n");
    wait_for_key();
    exit(1);
  }
  if (argc > 2) {
    error(ERR_MORE_THAN_ONE_INPUT_FILE);
  }
  else {
    precomp_mgr.ctx->input_file_name = argv[1];

    precomp_mgr.ctx->fin_length = std::filesystem::file_size(precomp_mgr.ctx->input_file_name.c_str());

    auto fin = std::unique_ptr<std::ifstream>(new std::ifstream());
    fin->open(precomp_mgr.ctx->input_file_name.c_str(), std::ios_base::in | std::ios_base::binary);
    if (!fin->is_open()) {
      print_to_console("ERROR: Input file \"%s\" doesn't exist\n", precomp_mgr.ctx->input_file_name.c_str());
      wait_for_key();
      exit(1);
    }
    precomp_mgr.ctx->fin = std::move(fin);

    if (check_for_pcf_file(precomp_mgr)) {
      operation = P_DECOMPRESS;
    }
  }

  // precomf.ini in EXE directory?
  std::string precomf_ini_path = std::filesystem::current_path().string();
  precomf_ini_path += PATH_DELIM;
  precomf_ini_path += "precomf.ini";
  print_to_console("INI file: %s\n", precomf_ini_path.c_str());

  if (!file_exists(precomf_ini_path.c_str())) {
    print_to_console("INI file not found. Create it (y/n)?");
    char ch = get_char_with_echo();
    print_to_console("\n");
    if ((ch != 'Y') && (ch != 'y')) {
      wait_for_key();
      exit(1);
    }
    else {
      std::fstream fnewini;
      fnewini.open(precomf_ini_path.c_str(), std::ios_base::out);
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
      ostream_printf(fnewini, "JPG_brunsli=on\n\n");
      ostream_printf(fnewini, ";; Prefer brunsli to packJPG for JPG streams (on/off)");
      ostream_printf(fnewini, "JPG_brotli=off\n\n");
      ostream_printf(fnewini, ";; Use brotli to compress metadata in JPG streams (on/off)");
      ostream_printf(fnewini, "JPG_packjpg=on\n\n");
      ostream_printf(fnewini, ";; Use packJPG for JPG streams (fallback if brunsli fails) (on/off)");
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
      precomp_mgr.switches.min_ident_size = 4;
      min_ident_size_set = true;
      precomp_mgr.switches.compression_otf_max_memory = 2048;
      lzma_max_memory_set = true;
      lzma_thread_count_set = true;
      parse_ini_file = false;
    }
  }

  if (parse_ini_file) {
    // parse INI file
    bool print_ignore_positions_message = true;
    bool compression_type_line_used = false;

    std::ifstream ini_file(precomf_ini_path.c_str());
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
          }
          else {
            *it = tolower(*it);
          }
        }
        if (!parName.empty()) {
          it = parName.begin();
          if (*it == ' ') {
            parName.erase(it);
          }
          else {
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
          }
          else {
            *it = tolower(*it);
          }
        }
        if (!valuestr.empty()) {
          it = valuestr.begin();
          if (*it == ' ') {
            valuestr.erase(it);
          }
          else {
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
            for (j = (strlen(value) - 1); j >= 0; j--) {
              ident_size += ((unsigned int)(value[j]) - '0') * multiplicator;
              if ((multiplicator * 10) < multiplicator) {
                error(ERR_IDENTICAL_BYTE_SIZE_TOO_BIG);
              }
              multiplicator *= 10;
            }
            precomp_mgr.switches.min_ident_size = ident_size;
            min_ident_size_set = true;

            print_to_console("INI: Set minimal identical byte size to %i\n", precomp_mgr.switches.min_ident_size);

            valid_param = true;
          }

          if (strcmp(param, "verbose") == 0) {
            if (strcmp(value, "off") == 0) {
              print_to_console("INI: Disabled verbose mode\n");
              valid_param = true;
            }

            if (strcmp(value, "on") == 0) {
              print_to_console("INI: Enabled verbose mode\n");
              DEBUG_MODE = true;
              valid_param = true;
            }

            if (!valid_param) {
              print_to_console("ERROR: Invalid verbose value: %s\n", value);
              wait_for_key();
              exit(1);
            }
          }

          if (strcmp(param, "compression_method") == 0) {
            if (strcmp(value, "0") == 0) {
              print_to_console("INI: Using no compression method\n");
              precomp_mgr.ctx->compression_otf_method = OTF_NONE;
              valid_param = true;
            }

            if (strcmp(value, "1") == 0) {
              print_to_console("INI: Using bZip2 compression method\n");
              precomp_mgr.ctx->compression_otf_method = OTF_BZIP2;
              valid_param = true;
            }

            if (strcmp(value, "2") == 0) {
              print_to_console("INI: Using lzma2 multithreaded compression method\n");
              precomp_mgr.ctx->compression_otf_method = OTF_XZ_MT;
              valid_param = true;
            }

            if (!valid_param) {
              print_to_console("ERROR: Invalid compression method value: %s\n", value);
              wait_for_key();
              exit(1);
            }
          }

          if (strcmp(param, "lzma_maximal_memory") == 0) {
            if (lzma_max_memory_set) {
              error(ERR_ONLY_SET_LZMA_MEMORY_ONCE);
            }
            unsigned int multiplicator = 1;
            for (j = (strlen(value) - 1); j >= 0; j--) {
              precomp_mgr.switches.compression_otf_max_memory += ((unsigned int)(value[j]) - '0') * multiplicator;
              if ((multiplicator * 10) < multiplicator) {
                exit(1);
              }
              multiplicator *= 10;
            }
            lzma_max_memory_set = true;

            if (precomp_mgr.switches.compression_otf_max_memory > 0) {
              print_to_console("INI: Set LZMA maximal memory to %i MiB\n", (int)precomp_mgr.switches.compression_otf_max_memory);
            }

            valid_param = true;
          }

          if (strcmp(param, "lzma_thread_count") == 0) {
            if (lzma_thread_count_set) {
              error(ERR_ONLY_SET_LZMA_THREAD_ONCE);
            }
            unsigned int multiplicator = 1;
            for (j = (strlen(value) - 1); j >= 0; j--) {
              precomp_mgr.switches.compression_otf_thread_count += ((unsigned int)(value[j]) - '0') * multiplicator;
              if ((multiplicator * 10) < multiplicator) {
                exit(1);
              }
              multiplicator *= 10;
            }
            lzma_thread_count_set = true;

            if (precomp_mgr.switches.compression_otf_thread_count > 0) {
              print_to_console("INI: Set LZMA thread count to %i\n", precomp_mgr.switches.compression_otf_thread_count);
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
                precomp_mgr.otf_xz_extra_params->enable_filter_x86 = true;
                break;
              case 'P':
                precomp_mgr.otf_xz_extra_params->enable_filter_powerpc = true;
                break;
              case 'I':
                precomp_mgr.otf_xz_extra_params->enable_filter_ia64 = true;
                break;
              case 'A':
                precomp_mgr.otf_xz_extra_params->enable_filter_arm = true;
                break;
              case 'T':
                precomp_mgr.otf_xz_extra_params->enable_filter_armthumb = true;
                break;
              case 'S':
                precomp_mgr.otf_xz_extra_params->enable_filter_sparc = true;
                break;
              case 'D':
              {
                j++;
                char nextchar = value[j];
                if ((nextchar < '0') || (nextchar > '9')) {
                  print_to_console("ERROR: LZMA delta filter must be followed by a distance (%d..%d)\n",
                    LZMA_DELTA_DIST_MIN, LZMA_DELTA_DIST_MAX);
                  wait_for_key();
                  exit(1);
                }
                precomp_mgr.otf_xz_extra_params->enable_filter_delta = true;
                while ((value[j] > '0') && (value[j] < '9')) {
                  precomp_mgr.otf_xz_extra_params->filter_delta_distance *= 10;
                  precomp_mgr.otf_xz_extra_params->filter_delta_distance += (value[j] - '0');
                  j++;
                }
                if (precomp_mgr.otf_xz_extra_params->filter_delta_distance < LZMA_DELTA_DIST_MIN
                  || precomp_mgr.otf_xz_extra_params->filter_delta_distance > LZMA_DELTA_DIST_MAX) {
                  print_to_console("ERROR: LZMA delta filter distance must be in range %d..%d\n",
                    LZMA_DELTA_DIST_MIN, LZMA_DELTA_DIST_MAX);
                  wait_for_key();
                  exit(1);
                }
                j--;
                break;
              }
              default:
                print_to_console("ERROR: Unknown LZMA filter type \"%c\"\n", value[j]);
                wait_for_key();
                exit(1);
                break;
              }
              precomp_mgr.switches.otf_xz_filter_used_count++;
              if (precomp_mgr.switches.otf_xz_filter_used_count > 3) {
                print_to_console("ERROR: Only up to 3 LZMA filters can be used at the same time\n");
                wait_for_key();
                exit(1);
              }
            }

            lzma_filters_set = true;
            valid_param = true;
          }

          if (strcmp(param, "fast_mode") == 0) {
            if (strcmp(value, "off") == 0) {
              print_to_console("INI: Disabled fast mode\n");
              valid_param = true;
            }

            if (strcmp(value, "on") == 0) {
              print_to_console("INI: Enabled fast mode\n");
              precomp_mgr.switches.fast_mode = true;
              valid_param = true;
            }

            if (!valid_param) {
              print_to_console("ERROR: Invalid fast mode value: %s\n", value);
              wait_for_key();
              exit(1);
            }
          }
          // future note: params should be in lowercase for comparisons here only
          if (strcmp(param, "preserve_extension") == 0) {
            if (strcmp(value, "off") == 0) {
              print_to_console("INI: Not preserve extension\n");
              preserve_extension = false;
            }
            else if (strcmp(value, "on") == 0) {
              print_to_console("INI: Preserve extension\n");
              preserve_extension = true;
            }
            else {
              print_to_console("ERROR: Invalid Preserve extension mode: %s\n", value);
              wait_for_key();
              exit(1);
            }
            valid_param = true;
          }

          if (strcmp(param, "intense_mode") == 0) {
            if (strcmp(value, "off") == 0) {
              print_to_console("INI: Disabled intense mode\n");
              valid_param = true;
            }

            if (strcmp(value, "on") == 0) {
              print_to_console("INI: Enabled intense mode\n");
              precomp_mgr.switches.intense_mode = true;
              valid_param = true;
            }

            if (!valid_param) {
              print_to_console("ERROR: Invalid intense mode value: %s\n", value);
              wait_for_key();
              exit(1);
            }
          }

          if (strcmp(param, "brute_mode") == 0) {
            if (strcmp(value, "off") == 0) {
              print_to_console("INI: Disabled brute mode\n");
              valid_param = true;
            }

            if (strcmp(value, "on") == 0) {
              print_to_console("INI: Enabled brute mode\n");
              precomp_mgr.switches.brute_mode = true;
              valid_param = true;
            }

            if (!valid_param) {
              print_to_console("ERROR: Invalid brute mode value: %s\n", value);
              wait_for_key();
              exit(1);
            }
          }

          if (strcmp(param, "pdf_bmp_mode") == 0) {
            if (strcmp(value, "off") == 0) {
              print_to_console("INI: Disabled PDF BMP mode\n");
              precomp_mgr.switches.pdf_bmp_mode = false;
              valid_param = true;
            }

            if (strcmp(value, "on") == 0) {
              print_to_console("INI: Enabled PDF BMP mode\n");
              precomp_mgr.switches.pdf_bmp_mode = true;
              valid_param = true;
            }

            if (!valid_param) {
              print_to_console("ERROR: Invalid PDF BMP mode value: %s\n", value);
              wait_for_key();
              exit(1);
            }
          }

          if (strcmp(param, "jpg_progressive_only") == 0) {
            if (strcmp(value, "off") == 0) {
              print_to_console("INI: Disabled progressive only JPG mode\n");
              precomp_mgr.switches.prog_only = false;
              valid_param = true;
            }

            if (strcmp(value, "on") == 0) {
              print_to_console("INI: Enabled progressive only JPG mode\n");
              precomp_mgr.switches.prog_only = true;
              valid_param = true;
            }

            if (!valid_param) {
              print_to_console("ERROR: Invalid progressive only JPG mode value: %s\n", value);
              wait_for_key();
              exit(1);
            }
          }

          if (strcmp(param, "mjpeg_recompression") == 0) {
            if (strcmp(value, "off") == 0) {
              print_to_console("INI: Disabled MJPEG recompression\n");
              precomp_mgr.switches.use_mjpeg = false;
              valid_param = true;
            }

            if (strcmp(value, "on") == 0) {
              print_to_console("INI: Enabled MJPEG recompression\n");
              precomp_mgr.switches.use_mjpeg = true;
              valid_param = true;
            }

            if (!valid_param) {
              print_to_console("ERROR: Invalid MJPEG recompression value: %s\n", value);
              wait_for_key();
              exit(1);
            }
          }

          if (strcmp(param, "jpg_brunsli") == 0) {
            if (strcmp(value, "off") == 0) {
              print_to_console("INI: Disabled brunsli for JPG commpression\n");
              precomp_mgr.switches.use_brunsli = false;
              valid_param = true;
            }

            if (strcmp(value, "on") == 0) {
              print_to_console("INI: Enabled brunsli for JPG compression\n");
              precomp_mgr.switches.use_brunsli = true;
              valid_param = true;
            }

            if (!valid_param) {
              print_to_console("ERROR: Invalid brunsli compression value: %s\n", value);
              wait_for_key();
              exit(1);
            }
          }

          if (strcmp(param, "jpg_brotli") == 0) {
            if (strcmp(value, "off") == 0) {
              print_to_console("INI: Disabled brotli for JPG metadata compression\n");
              precomp_mgr.switches.use_brotli = false;
              valid_param = true;
            }

            if (strcmp(value, "on") == 0) {
              print_to_console("INI: Enabled brotli for JPG metadata compression\n");
              precomp_mgr.switches.use_brotli = true;
              valid_param = true;
            }

            if (!valid_param) {
              print_to_console("ERROR: Invalid brotli for metadata compression value: %s\n", value);
              wait_for_key();
              exit(1);
            }
          }

          if (strcmp(param, "jpg_packjpg") == 0) {
            if (strcmp(value, "off") == 0) {
              print_to_console("INI: Disabled packJPG for JPG compression\n");
              precomp_mgr.switches.use_brotli = false;
              valid_param = true;
            }

            if (strcmp(value, "on") == 0) {
              print_to_console("INI: Enabled packJPG for JPG compression\n");
              precomp_mgr.switches.use_brotli = true;
              valid_param = true;
            }

            if (!valid_param) {
              print_to_console("ERROR: Invalid packJPG for JPG compression value: %s\n", value);
              wait_for_key();
              exit(1);
            }
          }

          if (strcmp(param, "compression_types_enable") == 0) {
            if (compression_type_line_used) {
              print_to_console("ERROR: Both Compression_types_enable and Compression_types_disable used.\n");
              wait_for_key();
              exit(1);
            }
            compression_type_line_used = true;

            precomp_mgr.switches.use_pdf = false;
            precomp_mgr.switches.use_zip = false;
            precomp_mgr.switches.use_gzip = false;
            precomp_mgr.switches.use_png = false;
            precomp_mgr.switches.use_gif = false;
            precomp_mgr.switches.use_jpg = false;
            precomp_mgr.switches.use_mp3 = false;
            precomp_mgr.switches.use_swf = false;
            precomp_mgr.switches.use_base64 = false;
            precomp_mgr.switches.use_bzip2 = false;

            for (j = 0; j < (int)strlen(value); j++) {
              switch (toupper(value[j])) {
              case 'P': // PDF
                precomp_mgr.switches.use_pdf = true;
                break;
              case 'Z': // ZIP
                precomp_mgr.switches.use_zip = true;
                break;
              case 'G': // GZip
                precomp_mgr.switches.use_gzip = true;
                break;
              case 'N': // PNG
                precomp_mgr.switches.use_png = true;
                break;
              case 'F': // GIF
                precomp_mgr.switches.use_gif = true;
                break;
              case 'J': // JPG
                precomp_mgr.switches.use_jpg = true;
                break;
              case '3': // MP3
                precomp_mgr.switches.use_mp3 = true;
                break;
              case 'S': // SWF
                precomp_mgr.switches.use_swf = true;
                break;
              case 'M': // MIME Base64
                precomp_mgr.switches.use_base64 = true;
                break;
              case 'B': // bZip2
                precomp_mgr.switches.use_bzip2 = true;
                break;
              default:
                print_to_console("ERROR: Invalid compression type %c\n", value[j]);
                exit(1);
                break;
              }
            }

            if (precomp_mgr.switches.use_pdf) {
              print_to_console("INI: PDF compression enabled\n");
            }
            else {
              print_to_console("INI: PDF compression disabled\n");
            }

            if (precomp_mgr.switches.use_zip) {
              print_to_console("INI: ZIP compression enabled\n");
            }
            else {
              print_to_console("INI: ZIP compression disabled\n");
            }

            if (precomp_mgr.switches.use_gzip) {
              print_to_console("INI: GZip compression enabled\n");
            }
            else {
              print_to_console("INI: GZip compression disabled\n");
            }

            if (precomp_mgr.switches.use_png) {
              print_to_console("INI: PNG compression enabled\n");
            }
            else {
              print_to_console("INI: PNG compression disabled\n");
            }

            if (precomp_mgr.switches.use_gif) {
              print_to_console("INI: GIF compression enabled\n");
            }
            else {
              print_to_console("INI: GIF compression disabled\n");
            }

            if (precomp_mgr.switches.use_jpg) {
              print_to_console("INI: JPG compression enabled\n");
            }
            else {
              print_to_console("INI: JPG compression disabled\n");
            }

            if (precomp_mgr.switches.use_mp3) {
              print_to_console("INI: MP3 compression enabled\n");
            }
            else {
              print_to_console("INI: MP3 compression disabled\n");
            }

            if (precomp_mgr.switches.use_swf) {
              print_to_console("INI: SWF compression enabled\n");
            }
            else {
              print_to_console("INI: SWF compression disabled\n");
            }

            if (precomp_mgr.switches.use_base64) {
              print_to_console("INI: Base64 compression enabled\n");
            }
            else {
              print_to_console("INI: Base64 compression disabled\n");
            }

            if (precomp_mgr.switches.use_bzip2) {
              print_to_console("INI: bZip2 compression enabled\n");
            }
            else {
              print_to_console("INI: bZip2 compression disabled\n");
            }

            valid_param = true;
          }

          if (strcmp(param, "compression_types_disable") == 0) {
            if (compression_type_line_used) {
              print_to_console("ERROR: Both Compression_types_enable and Compression_types_disable used.\n");
              wait_for_key();
              exit(1);
            }
            compression_type_line_used = true;

            precomp_mgr.switches.use_pdf = true;
            precomp_mgr.switches.use_zip = true;
            precomp_mgr.switches.use_gzip = true;
            precomp_mgr.switches.use_png = true;
            precomp_mgr.switches.use_gif = true;
            precomp_mgr.switches.use_jpg = true;
            precomp_mgr.switches.use_mp3 = true;
            precomp_mgr.switches.use_swf = true;
            precomp_mgr.switches.use_base64 = true;
            precomp_mgr.switches.use_bzip2 = true;

            for (j = 0; j < (int)strlen(value); j++) {
              switch (toupper(value[j])) {
              case 'P': // PDF
                precomp_mgr.switches.use_pdf = false;
                break;
              case 'Z': // ZIP
                precomp_mgr.switches.use_zip = false;
                break;
              case 'G': // GZip
                precomp_mgr.switches.use_gzip = false;
                break;
              case 'N': // PNG
                precomp_mgr.switches.use_png = false;
                break;
              case 'F': // GIF
                precomp_mgr.switches.use_gif = false;
                break;
              case 'J': // JPG
                precomp_mgr.switches.use_jpg = false;
                break;
              case '3': // MP3
                precomp_mgr.switches.use_mp3 = false;
                break;
              case 'S': // SWF
                precomp_mgr.switches.use_swf = false;
                break;
              case 'M': // MIME Base64
                precomp_mgr.switches.use_base64 = false;
                break;
              case 'B': // bZip2
                precomp_mgr.switches.use_bzip2 = false;
                break;
              default:
                print_to_console("ERROR: Invalid compression type %c\n", value[j]);
                exit(1);
                break;
              }
            }

            if (precomp_mgr.switches.use_pdf) {
              print_to_console("INI: PDF compression enabled\n");
            }
            else {
              print_to_console("INI: PDF compression disabled\n");
            }

            if (precomp_mgr.switches.use_zip) {
              print_to_console("INI: ZIP compression enabled\n");
            }
            else {
              print_to_console("INI: ZIP compression disabled\n");
            }

            if (precomp_mgr.switches.use_gzip) {
              print_to_console("INI: GZip compression enabled\n");
            }
            else {
              print_to_console("INI: GZip compression disabled\n");
            }

            if (precomp_mgr.switches.use_png) {
              print_to_console("INI: PNG compression enabled\n");
            }
            else {
              print_to_console("INI: PNG compression disabled\n");
            }

            if (precomp_mgr.switches.use_gif) {
              print_to_console("INI: GIF compression enabled\n");
            }
            else {
              print_to_console("INI: GIF compression disabled\n");
            }

            if (precomp_mgr.switches.use_jpg) {
              print_to_console("INI: JPG compression enabled\n");
            }
            else {
              print_to_console("INI: JPG compression disabled\n");
            }

            if (precomp_mgr.switches.use_mp3) {
              print_to_console("INI: MP3 compression enabled\n");
            }
            else {
              print_to_console("INI: MP3 compression disabled\n");
            }

            if (precomp_mgr.switches.use_swf) {
              print_to_console("INI: SWF compression enabled\n");
            }
            else {
              print_to_console("INI: SWF compression disabled\n");
            }

            if (precomp_mgr.switches.use_base64) {
              print_to_console("INI: Base64 compression enabled\n");
            }
            else {
              print_to_console("INI: Base64 compression disabled\n");
            }

            if (precomp_mgr.switches.use_bzip2) {
              print_to_console("INI: bZip2 compression enabled\n");
            }
            else {
              print_to_console("INI: bZip2 compression disabled\n");
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
              if ((j + 2) < (int)strlen(value)) {
                if (value[j + 2] != ',') {
                  print_to_console("ERROR: zLib levels have to be separated with commas\n");
                  exit(1);
                }
              }
              if ((j + 1) >= (int)strlen(value)) {
                print_to_console("ERROR: Last zLib level is incomplete\n");
                exit(1);
              }
              int comp_level_to_use = (char(value[j]) - '1');
              int mem_level_to_use = (char(value[j + 1]) - '1');
              if (((comp_level_to_use >= 0) && (comp_level_to_use <= 8))
                && ((mem_level_to_use >= 0) && (mem_level_to_use <= 8))) {
                use_zlib_level[comp_level_to_use + mem_level_to_use * 9] = true;
              }
              else {
                print_to_console("ERROR: Invalid zlib level %c%c\n", value[j], value[j + 1]);
                wait_for_key();
                exit(1);
              }
            }

            print_to_console("INI: Set zLib levels\n");

            valid_param = true;
          }

          if (strcmp(param, "maximal_recursion_depth") == 0) {
            if (recursion_depth_set) {
              error(ERR_ONLY_SET_RECURSION_DEPTH_ONCE);
            }

            unsigned int max_recursion_d = 0;
            unsigned int multiplicator = 1;
            for (j = (strlen(value) - 1); j >= 0; j--) {
              max_recursion_d += ((unsigned int)(value[j]) - '0') * multiplicator;
              if ((multiplicator * 10) < multiplicator) {
                error(ERR_RECURSION_DEPTH_TOO_BIG);
              }
              multiplicator *= 10;
            }
            precomp_mgr.max_recursion_depth = max_recursion_d;
            recursion_depth_set = true;

            print_to_console("INI: Set maximal recursion depth to %i\n", precomp_mgr.max_recursion_depth);

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
                }
                else {
                  if ((act_ignore_pos * 10) < act_ignore_pos) {
                    error(ERR_IGNORE_POS_TOO_BIG);
                  }
                  act_ignore_pos = (act_ignore_pos * 10) + (value[j] - '0');
                }
                break;
              case ',':
                if (act_ignore_pos != -1) {
                  precomp_mgr.switches.ignore_list.push_back(act_ignore_pos);
                  act_ignore_pos = -1;
                }
                break;
              case ' ':
                break;
              default:
                print_to_console("ERROR: Invalid char in ignore_positions: %c\n", value[j]);
                wait_for_key();
                exit(1);
              }
            }
            if (act_ignore_pos != -1) {
              precomp_mgr.switches.ignore_list.push_back(act_ignore_pos);
            }

            if (print_ignore_positions_message) {
              print_to_console("INI: Set ignore positions\n");
              print_ignore_positions_message = false;
            }

            valid_param = true;
          }

          if (!valid_param) {
            print_to_console("ERROR: Invalid INI parameter: %s\n", param);
            wait_for_key();
            exit(1);
          }
        }
      }
    }
    ini_file.close();
  }

  if (operation == P_COMPRESS) {
    if (!preserve_extension) {
      precomp_mgr.ctx->output_file_name = precomp_mgr.ctx->input_file_name;
      const char* backslash_at_pos = strrchr(precomp_mgr.ctx->output_file_name.c_str(), PATH_DELIM);
      const char* dot_at_pos = strrchr(precomp_mgr.ctx->output_file_name.c_str(), '.');
      if ((dot_at_pos == NULL) || ((backslash_at_pos != NULL) && (dot_at_pos < backslash_at_pos))) {
        precomp_mgr.ctx->output_file_name += ".pcf";
      }
      else {
        precomp_mgr.ctx->output_file_name = std::string(
          precomp_mgr.ctx->output_file_name.c_str(),
          dot_at_pos - precomp_mgr.ctx->output_file_name.c_str()
        );
        // same as output file because input file had .pcf extension?
        if (precomp_mgr.ctx->input_file_name.compare(precomp_mgr.ctx->output_file_name + ".pcf") == 0) {
          precomp_mgr.ctx->output_file_name += "_pcf.pcf";
        }
        else {
          precomp_mgr.ctx->output_file_name += ".pcf";
        }
      }
    }
    else {
      precomp_mgr.ctx->output_file_name = precomp_mgr.ctx->input_file_name + ".pcf";
    }
  }

  if (file_exists(precomp_mgr.ctx->output_file_name.c_str())) {
    print_to_console("Output file \"%s\" exists. Overwrite (y/n)? ", precomp_mgr.ctx->output_file_name.c_str());
    char ch = get_char_with_echo();
    if ((ch != 'Y') && (ch != 'y')) {
      print_to_console("\n");
      wait_for_key();
      exit(0);
    }
    else {
      print_to_console("\n\n");
    }
  }
  else {
    print_to_console("\n");
  }

  auto fout = std::unique_ptr<std::ofstream>(new std::ofstream());
  fout->open(precomp_mgr.ctx->output_file_name.c_str(), std::ios_base::out | std::ios_base::binary);
  if (!fout->is_open()) {
    print_to_console("ERROR: Can't create output file \"%s\"\n", precomp_mgr.ctx->output_file_name.c_str());
    wait_for_key();
    exit(1);
  }
  precomp_mgr.ctx->fout = std::move(fout);

  print_to_console("Input file: %s\n", precomp_mgr.ctx->input_file_name.c_str());
  print_to_console("Output file: %s\n\n", precomp_mgr.ctx->output_file_name.c_str());
  if (DEBUG_MODE) {
    if (min_ident_size_set) {
      print_to_console("\n");
      print_to_console("Minimal ident size set to %i bytes\n", precomp_mgr.switches.min_ident_size);
    }
    if (!precomp_mgr.switches.ignore_list.empty()) {
      print_to_console("\n");
      print_to_console("Ignore position list:\n");
      for (auto ignore_pos : precomp_mgr.switches.ignore_list) {
        std::cout << ignore_pos << std::endl;
      }
      print_to_console("\n");
    }
  }

  if (level_switch) {

    for (i = 0; i < 81; i++) {
      if (use_zlib_level[i]) {
        precomp_mgr.obsolete.comp_mem_level_count[i] = 0;
      }
      else {
        precomp_mgr.obsolete.comp_mem_level_count[i] = -1;
      }
    }

    precomp_mgr.switches.level_switch_used = true;

  }

  packjpg_mp3_dll_msg();

  return operation;
}
#endif
#endif
