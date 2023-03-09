#include "libprecomp.h"
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

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <string>
#include <array>
#include <csignal>
#include <random>
#include <filesystem>

#ifdef __unix
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#endif

#include "precomp_io.h"
#include "precomp_utils.h"

std::string input_file_name;
std::string output_file_name;
int otf_xz_filter_used_count = 0;

std::string libprecomp_error_msg(int error_code)
{
  return make_cstyle_format_string("\nERROR %i: %s", error_code, precomp_error_msg(error_code).c_str());
}

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
  auto l = strlen(ref);
  if (c[l] == '+' && !c[l + 1]) {
    val = true;
    return true;
  } else if (c[l] == '-' && !c[l + 1]) {
    val = false;
    return true;
  }
  throw std::runtime_error(make_cstyle_format_string("ERROR: Only + or - for this switch (%s) allowed\n", c));
}

int parseInt(const char*& c, const char* context, int too_big_error_code = 0) {
  if (*c < '0' || *c > '9') {
    throw std::runtime_error(make_cstyle_format_string("ERROR: Number needed to set %s\n", context));
  }
  int val = *c++ - '0';
  while (*c >= '0' && *c <= '9') {
    if (val >= INT_MAX / 10 - 1) {
      if (too_big_error_code != 0) {
        throw std::runtime_error(libprecomp_error_msg(too_big_error_code));
      }
      throw std::runtime_error(make_cstyle_format_string("ERROR: Number too big for %s\n", context));
    }
    val = val * 10 + *c++ - '0';
  }
  return val;
}
int parseIntUntilEnd(const char* c, const char* context, int too_big_error_code = 0) {
  for (int i = 0; c[i]; ++i) {
    if (c[i] < '0' || c[i] > '9') {
      throw std::runtime_error(make_cstyle_format_string("ERROR: Only numbers allowed for %s\n", context));
    }
  }
  const char* x = c;
  return parseInt(x, context, too_big_error_code);
}
int64_t parseInt64(const char*& c, const char* context, int too_big_error_code = 0) {
  if (*c < '0' || *c > '9') {
    throw std::runtime_error(make_cstyle_format_string("ERROR: Number needed to set %s\n", context));
  }
  int64_t val = *c++ - '0';
  while (*c >= '0' && *c <= '9') {
    if (val >= INT64_MAX / 10 - 1) {
      if (too_big_error_code != 0) {
        throw std::runtime_error(libprecomp_error_msg(too_big_error_code));
      }
      throw std::runtime_error(make_cstyle_format_string("ERROR: Number too big for %s\n", context));
    }
    val = val * 10 + *c++ - '0';
  }
  return val;
}
int64_t parseInt64UntilEnd(const char* c, const char* context, int too_big_error_code = 0) {
  for (int i = 0; c[i]; ++i) {
    if (c[i] < '0' || c[i] > '9') {
      throw std::runtime_error(make_cstyle_format_string("ERROR: Only numbers allowed for %s\n", context));
    }
  }
  const char* x = c;
  return parseInt64(x, context, too_big_error_code);
}

bool file_exists(const char* filename) {
  std::fstream fin;

  fin.open(filename, std::ios::in);
  bool retval = fin.is_open();
  fin.close();

  return retval;
}

[[noreturn]] void ctrl_c_handler(int sig) {
  print_to_console("\n\nCTRL-C detected\n");
  (void) signal(SIGINT, SIG_DFL);

  throw std::runtime_error(libprecomp_error_msg(ERR_CTRL_C));
}

// nice time output, input t in ms
// 2^32 ms maximum, so will display incorrect negative values after about 49 days
void printf_time(long long t) {
  print_to_console("Time: ");
  if (t < 1000) { // several milliseconds
    print_to_console("%li millisecond(s)\n", (long)t);
  }
  else if (t < 1000 * 60) { // several seconds
    print_to_console("%li second(s), %li millisecond(s)\n", (long)(t / 1000), (long)(t % 1000));
  }
  else if (t < 1000 * 60 * 60) { // several minutes
    print_to_console("%li minute(s), %li second(s)\n", (long)(t / (1000 * 60)), (long)((t / 1000) % 60));
  }
  else if (t < 1000 * 60 * 60 * 24) { // several hours
    print_to_console("%li hour(s), %li minute(s), %li second(s)\n", (long)(t / (1000 * 60 * 60)), (long)((t / (1000 * 60)) % 60), (long)((t / 1000) % 60));
  }
  else {
    print_to_console("%li day(s), %li hour(s), %li minute(s)\n", (long)(t / (1000 * 60 * 60 * 24)), (long)((t / (1000 * 60 * 60)) % 24), (long)((t / (1000 * 60)) % 60));
  }
}

int work_sign_var = 0;
static char work_signs[5] = "|/-\\";
std::string next_work_sign() {
  work_sign_var = (work_sign_var + 1) % 4;
  return make_cstyle_format_string("%c     ", work_signs[work_sign_var]);
}

std::string current_progress_txt;
void delete_current_progress_text() {
  print_to_console(std::string(current_progress_txt.length(), '\b'));
}

long long sec_time;
bool get_progress_txt(float percent) {
  if ((get_time_ms() - sec_time) < 250) return false;  // not enough time passed since last progress update, quit to not spam

  std::string new_percent_progress_txt = make_cstyle_format_string("%6.2f%% ", percent);
  std::string new_work_sign = next_work_sign();

  delete_current_progress_text();
  sec_time = get_time_ms();
  current_progress_txt = new_percent_progress_txt + new_work_sign;
  return true;
}

void print_results(CPrecomp& precomp_mgr, bool print_new_size) {
  delete_current_progress_text();
  if (print_new_size && output_file_name != "stdout") {
    auto fout_length = std::filesystem::file_size(output_file_name.c_str());
    std::string result_print = "New size: " + std::to_string(fout_length) + " instead of " + std::to_string(PrecompGetRecursionContext(&precomp_mgr)->fin_length) + "     \n";
    print_to_console("100.00% - " + result_print);
  }
  else {
    print_to_console("100.00%%");
  }
  print_to_console("\nDone.\n");
  printf_time(get_time_ms() - precomp_mgr.start_time);
}

void show_used_levels(CPrecomp& precomp_mgr, CSwitches& precomp_switches) {
  auto precomp_context = PrecompGetRecursionContext(&precomp_mgr);
  auto precomp_statistics = PrecompGetResultStatistics(&precomp_mgr);

  if (!precomp_context->anything_was_used) {
    if (!precomp_context->non_zlib_was_used) {
      print_to_console("\nNone of the given compression and memory levels could be used.\n");
      print_to_console("There will be no gain compressing the output file.\n");
    } else {
      if ((!precomp_mgr.max_recursion_depth_reached) && (precomp_mgr.max_recursion_depth_used != precomp_mgr.max_recursion_depth)) {
          print_to_console("\nYou can speed up Precomp for THIS FILE with these parameters:\n");
          print_to_console("-d");
          print_to_console("%i\n", precomp_mgr.max_recursion_depth_used);
      }
    }
    if (precomp_mgr.max_recursion_depth_reached) {
      print_to_console("\nMaximal recursion depth %i reached, increasing it could give better results.\n", precomp_mgr.max_recursion_depth);
    }
    return;
  }
  
  int level_count = 0;
  print_to_console("\nYou can speed up Precomp for THIS FILE with these parameters:\n");
  print_to_console("-zl");

  std::string disable_methods;
  std::array<std::tuple<bool, unsigned int, unsigned int, std::string>, 10> disable_formats{{
    {precomp_switches.use_pdf, precomp_statistics->recompressed_pdf_count, precomp_statistics->decompressed_pdf_count, "p"},
    {precomp_switches.use_zip, precomp_statistics->recompressed_zip_count, precomp_statistics->decompressed_zip_count, "z"},
    {precomp_switches.use_gzip, precomp_statistics->recompressed_gzip_count, precomp_statistics->decompressed_gzip_count, "g"},
    {precomp_switches.use_png, precomp_statistics->recompressed_png_count + precomp_statistics->recompressed_png_multi_count, precomp_statistics->decompressed_png_count + precomp_statistics->decompressed_png_multi_count, "n"},
    {precomp_switches.use_gif, precomp_statistics->recompressed_gif_count, precomp_statistics->decompressed_gif_count, "f"},
    {precomp_switches.use_jpg, precomp_statistics->recompressed_jpg_count + precomp_statistics->recompressed_jpg_prog_count, precomp_statistics->decompressed_jpg_count + precomp_statistics->decompressed_jpg_prog_count, "j"},
    {precomp_switches.use_swf, precomp_statistics->recompressed_swf_count, precomp_statistics->decompressed_swf_count, "s"},
    {precomp_switches.use_base64, precomp_statistics->recompressed_base64_count, precomp_statistics->decompressed_base64_count, "m"},
    {precomp_switches.use_bzip2, precomp_statistics->recompressed_bzip2_count, precomp_statistics->decompressed_bzip2_count, "b"},
    {precomp_switches.use_mp3, precomp_statistics->recompressed_mp3_count, precomp_statistics->decompressed_mp3_count, "3"},
  }};
  for (auto disable_format : disable_formats) {
      if ((std::get<0>(disable_format) && ((std::get<1>(disable_format) == 0) && (std::get<2>(disable_format) > 0)))) disable_methods += std::get<3>(disable_format);
  }
  if ( disable_methods.length() > 0 ) {
      print_to_console(" -t-%s",disable_methods.c_str());
  }

  if (precomp_mgr.max_recursion_depth_reached) {
      print_to_console("\n\nMaximal recursion depth %i reached, increasing it could give better results.\n", precomp_mgr.max_recursion_depth);
  } else if (precomp_mgr.max_recursion_depth_used != precomp_mgr.max_recursion_depth) {
      print_to_console(" -d");
      print_to_console("%i", precomp_mgr.max_recursion_depth_used);
  }

  if ((level_count == 1) && (!precomp_switches.fast_mode)) {
      print_to_console("\n\nFast mode does exactly the same for this file, only faster.\n");
  }

  print_to_console("\n");
}

void print_statistics(CPrecomp& precomp_mgr, CSwitches& precomp_switches) {
  auto precomp_statistics = PrecompGetResultStatistics(&precomp_mgr);
  print_to_console("\nRecompressed streams: %i/%i\n", precomp_statistics->recompressed_streams_count, precomp_statistics->decompressed_streams_count);

  if ((precomp_statistics->recompressed_streams_count > 0) || (precomp_statistics->decompressed_streams_count > 0)) {
    std::array<std::tuple<bool, unsigned int, unsigned int, std::string>, 16> format_statistics{ {
      {precomp_switches.use_pdf, precomp_statistics->decompressed_pdf_count, precomp_statistics->recompressed_pdf_count, "PDF"},
      {precomp_switches.pdf_bmp_mode && precomp_switches.use_pdf, precomp_statistics->decompressed_pdf_count_8_bit, precomp_statistics->recompressed_pdf_count_8_bit, "PDF image (8-bit)"},
      {precomp_switches.pdf_bmp_mode && precomp_switches.use_pdf, precomp_statistics->decompressed_pdf_count_24_bit, precomp_statistics->recompressed_pdf_count_24_bit, "PDF image (24-bit)"},
      {precomp_switches.use_zip, precomp_statistics->decompressed_zip_count, precomp_statistics->recompressed_zip_count, "ZIP"},
      {precomp_switches.use_gzip, precomp_statistics->decompressed_gzip_count, precomp_statistics->recompressed_gzip_count, "GZip"},
      {precomp_switches.use_png, precomp_statistics->decompressed_png_count, precomp_statistics->recompressed_png_count, "PNG"},
      {precomp_switches.use_png, precomp_statistics->decompressed_png_multi_count, precomp_statistics->recompressed_png_multi_count, "PNG (multi)"},
      {precomp_switches.use_gif, precomp_statistics->decompressed_gif_count, precomp_statistics->recompressed_gif_count, "GIF"},
      {precomp_switches.use_jpg, precomp_statistics->decompressed_jpg_count, precomp_statistics->recompressed_jpg_count, "JPG"},
      {precomp_switches.use_jpg, precomp_statistics->decompressed_jpg_prog_count, precomp_statistics->recompressed_jpg_prog_count, "JPG (progressive)"},
      {precomp_switches.use_mp3, precomp_statistics->decompressed_mp3_count, precomp_statistics->recompressed_mp3_count, "MP3"},
      {precomp_switches.use_swf, precomp_statistics->decompressed_swf_count, precomp_statistics->recompressed_swf_count, "SWF"},
      {precomp_switches.use_base64, precomp_statistics->decompressed_base64_count, precomp_statistics->recompressed_base64_count, "Base64"},
      {precomp_switches.use_bzip2, precomp_statistics->decompressed_bzip2_count, precomp_statistics->recompressed_bzip2_count, "bZip2"},
      {precomp_switches.intense_mode, precomp_statistics->decompressed_zlib_count, precomp_statistics->recompressed_zlib_count, "zLib (intense mode)"},
      {precomp_switches.brute_mode, precomp_statistics->decompressed_brute_count, precomp_statistics->recompressed_brute_count, "Brute mode"},
    } };
    for (auto format_stats : format_statistics) {
      bool condition = std::get<0>(format_stats);
      unsigned int decompressed_count = std::get<1>(format_stats);
      unsigned int recompressed_count = std::get<2>(format_stats);
      std::string format_tag = std::get<3>(format_stats);
      if (condition && ((recompressed_count > 0) || (decompressed_count > 0)))
        print_to_console(format_tag + " streams: " + std::to_string(recompressed_count) + "/" + std::to_string(decompressed_count) + "\n");
    }
  }

  if (!precomp_switches.level_switch_used) show_used_levels(precomp_mgr, precomp_switches);
}

void log_handler(PrecompLoggingLevels level, char* msg) {
  if (level >= PRECOMP_DEBUG_LOG) {
    // This way even with debug logging we keep the progress percent with work sign at the end
    print_to_console("\n" + std::string(msg) + "\n" + current_progress_txt);
  } else {
    print_to_console(msg);
  }
}

void setSwitchesIgnoreList(CSwitches& precomp_switches, const std::vector<long long>& ignore_list) {
  PrecompSwitchesSetIgnoreList(&precomp_switches, ignore_list.data(), ignore_list.size());
}

int init(CPrecomp& precomp_mgr, CSwitches& precomp_switches, int argc, char* argv[]) {
  auto precomp_context = PrecompGetRecursionContext(&precomp_mgr);

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

  bool valid_syntax = false;
  bool input_file_given = false;
  bool output_file_given = false;
  int operation = P_PRECOMPRESS;
  bool parse_on = true;
  bool level_switch = false;
  bool min_ident_size_set = false;
  bool recursion_depth_set = false;
  bool lzma_max_memory_set = false;
  bool lzma_thread_count_set = false;
  bool lzma_filters_set = false;
  bool long_help = false;
  bool preserve_extension = false;
  bool comfort_mode = false;

  std::vector<long long> ignore_list;

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
          precomp_switches.intense_mode = true;
          if (strlen(argv[i]) > 8) {
            precomp_switches.intense_mode_depth_limit = parseIntUntilEnd(argv[i] + 8, "intense mode level limit", ERR_INTENSE_MODE_LIMIT_TOO_BIG);
          }
        }
        else {
          long long ignore_pos = parseInt64UntilEnd(argv[i] + 2, "ignore position", ERR_IGNORE_POS_TOO_BIG);
          ignore_list.push_back(ignore_pos);
        }
        break;
      }
      case 'D':
      {
        if (recursion_depth_set) {
          throw std::runtime_error(libprecomp_error_msg(ERR_ONLY_SET_RECURSION_DEPTH_ONCE));
        }
        precomp_mgr.max_recursion_depth = parseIntUntilEnd(argv[i] + 2, "maximal recursion depth", ERR_RECURSION_DEPTH_TOO_BIG);
        recursion_depth_set = true;
        break;
      }
      case 'S':
      {
        if (min_ident_size_set) {
          throw std::runtime_error(libprecomp_error_msg(ERR_ONLY_SET_MIN_SIZE_ONCE));
        }
        precomp_switches.min_ident_size = parseIntUntilEnd(argv[i] + 2, "minimal identical byte size", ERR_IDENTICAL_BYTE_SIZE_TOO_BIG);
        min_ident_size_set = true;
        break;
      }
      case 'B':
      {
        if (parsePrefixText(argv[i] + 1, "brute")) { // brute mode
          precomp_switches.brute_mode = true;
          if (strlen(argv[i]) > 6) {
            precomp_switches.brute_mode_depth_limit = parseIntUntilEnd(argv[i] + 6, "brute mode level limit", ERR_BRUTE_MODE_LIMIT_TOO_BIG);
          }
        }
        else if (!parseSwitch(precomp_switches.use_brunsli, argv[i] + 1, "brunsli")
          && !parseSwitch(precomp_switches.use_brotli, argv[i] + 1, "brotli")) {
          throw std::runtime_error(make_cstyle_format_string("ERROR: Unknown switch \"%s\"\n", argv[i]));
        }
        break;
      }
      case 'L':
      {
        if (parsePrefixText(argv[i] + 1, "longhelp")) {
          long_help = true;
        }
        else {
          throw std::runtime_error(make_cstyle_format_string("ERROR: Unknown switch \"%s\"\n", argv[i]));
        }
        break;
      }
      case 'P':
      {
        if (!parseSwitch(precomp_switches.pdf_bmp_mode, argv[i] + 1, "pdfbmp")
          && !parseSwitch(precomp_switches.prog_only, argv[i] + 1, "progonly")
          && !parseSwitch(precomp_switches.preflate_verify, argv[i] + 1, "pfverify")
          && !parseSwitch(precomp_switches.use_packjpg_fallback, argv[i] + 1, "packjpg")) {
          if (parsePrefixText(argv[i] + 1, "pfmeta")) {
            size_t mbsize = parseIntUntilEnd(argv[i] + 7, "preflate meta block size");
            if (mbsize >= INT_MAX / 1024) {
              throw std::runtime_error(make_cstyle_format_string("preflate meta block size set too big\n"));
            }
            precomp_switches.preflate_meta_block_size = mbsize * 1024;
          }
          else {
            throw std::runtime_error(make_cstyle_format_string("ERROR: Unknown switch \"%s\"\n", argv[i]));
          }
        }
        break;
      }
      case 'T':
      {
        bool set_to;
        switch (argv[i][2]) {
        case '+':
          precomp_switches.use_pdf = false;
          precomp_switches.use_zip = false;
          precomp_switches.use_gzip = false;
          precomp_switches.use_png = false;
          precomp_switches.use_gif = false;
          precomp_switches.use_jpg = false;
          precomp_switches.use_mp3 = false;
          precomp_switches.use_swf = false;
          precomp_switches.use_base64 = false;
          precomp_switches.use_bzip2 = false;
          set_to = true;
          break;
        case '-':
          precomp_switches.use_pdf = true;
          precomp_switches.use_zip = true;
          precomp_switches.use_gzip = true;
          precomp_switches.use_png = true;
          precomp_switches.use_gif = true;
          precomp_switches.use_jpg = true;
          precomp_switches.use_mp3 = true;
          precomp_switches.use_swf = true;
          precomp_switches.use_base64 = true;
          precomp_switches.use_bzip2 = true;
          set_to = false;
          break;
        default:
          throw std::runtime_error(make_cstyle_format_string("ERROR: Only + or - for type switch allowed\n"));
        }
        for (j = 3; j < (int)strlen(argv[i]); j++) {
          switch (toupper(argv[i][j])) {
          case 'P': // PDF
            precomp_switches.use_pdf = set_to;
            break;
          case 'Z': // ZIP
            precomp_switches.use_zip = set_to;
            break;
          case 'G': // GZip
            precomp_switches.use_gzip = set_to;
            break;
          case 'N': // PNG
            precomp_switches.use_png = set_to;
            break;
          case 'F': // GIF
            precomp_switches.use_gif = set_to;
            break;
          case 'J': // JPG
            precomp_switches.use_jpg = set_to;
            break;
          case '3': // MP3
            precomp_switches.use_mp3 = set_to;
            break;
          case 'S': // SWF
            precomp_switches.use_swf = set_to;
            break;
          case 'M': // MIME Base64
            precomp_switches.use_base64 = set_to;
            break;
          case 'B': // bZip2
            precomp_switches.use_bzip2 = set_to;
            break;
          default:
            throw std::runtime_error(make_cstyle_format_string("ERROR: Invalid compression type %c\n", argv[i][j]));
          }
        }
        break;
      }
      case 'C':
      {
        if (
          strlen(argv[i]) == 8 &&
          toupper(argv[i][2]) == 'O' &&
          toupper(argv[i][3]) == 'M' &&
          toupper(argv[i][4]) == 'F' &&
          toupper(argv[i][5]) == 'O' &&
          toupper(argv[i][6]) == 'R' &&
          toupper(argv[i][7]) == 'T'
          ) {
          comfort_mode = true;
        }
        if (argv[i][2] != 0 && !comfort_mode) { // Extra Parameters?
          throw std::runtime_error(make_cstyle_format_string("ERROR: Unknown switch \"%s\"\n", argv[i]));
        }
        break;
      }
      case 'V':
      {
        PRECOMP_VERBOSITY_LEVEL = PRECOMP_DEBUG_LOG;
        if (argv[i][2] != 0) { // Extra Parameters?
          throw std::runtime_error(make_cstyle_format_string("ERROR: Unknown switch \"%s\"\n", argv[i]));
        }
        break;
      }
      case 'R':
      {
        operation = P_RECOMPRESS;
        if (argv[i][2] != 0) { // Extra Parameters?
          throw std::runtime_error(make_cstyle_format_string("ERROR: Unknown switch \"%s\"\n", argv[i]));
        }
        break;
      }
      case 'Z':
      {
        if (toupper(argv[i][2]) == 'L') {
          level_switch = true;

          for (j = 0; j < ((int)strlen(argv[i]) - 3); j += 3) {
            if ((j + 5) < (int)strlen(argv[i])) {
              if (argv[i][j + 5] != ',') {
                throw std::runtime_error(make_cstyle_format_string("ERROR: zLib levels have to be separated with commas\n"));
              }
            }
            if ((j + 4) >= (int)strlen(argv[i])) {
              throw std::runtime_error(make_cstyle_format_string("ERROR: Last zLib level is incomplete\n"));
            }
            int comp_level_to_use = (char(argv[i][j + 3]) - '1');
            int mem_level_to_use = (char(argv[i][j + 4]) - '1');
            if (((comp_level_to_use >= 0) && (comp_level_to_use <= 8))
              && ((mem_level_to_use >= 0) && (mem_level_to_use <= 8))) {
            }
            else {
              throw std::runtime_error(make_cstyle_format_string("ERROR: Invalid zlib level %c%c\n", argv[i][j + 3], argv[i][j + 4]));
            }
          }
          break;
        }
        else {
          throw std::runtime_error(make_cstyle_format_string("ERROR: Unknown switch \"%s\"\n", argv[i]));
        }
      }
      case 'O':
      {
        if (output_file_given) {
          throw std::runtime_error(libprecomp_error_msg(ERR_MORE_THAN_ONE_OUTPUT_FILE));
        }

        if (strlen(argv[i]) == 2) {
          throw std::runtime_error(libprecomp_error_msg(ERR_DONT_USE_SPACE));
        }

        output_file_given = true;
        output_file_name = argv[i] + 2;

        // check for backslash in file name
        const char* backslash_at_pos = strrchr(output_file_name.c_str(), PATH_DELIM);

        // dot in output file name? If not, use .pcf extension
        const char* dot_at_pos = strrchr(output_file_name.c_str(), '.');
        if (output_file_name != "stdout" && (dot_at_pos == nullptr) || ((backslash_at_pos != nullptr) && (backslash_at_pos > dot_at_pos))) {
          output_file_name += ".pcf";
          appended_pcf = true;
        }

        break;
      }

      case 'M':
      {
        if (!parseSwitch(precomp_switches.use_mjpeg, argv[i] + 1, "mjpeg")) {
          throw std::runtime_error(make_cstyle_format_string("ERROR: Unknown switch \"%s\"\n", argv[i]));
        }
        break;
      }

      case 'F':
      {
        precomp_switches.fast_mode = true;
        if (argv[i][2] != 0) { // Extra Parameters?
          throw std::runtime_error(make_cstyle_format_string("ERROR: Unknown switch \"%s\"\n", argv[i]));
        }
        break;
      }
      case 'E':
      {
        preserve_extension = true;
        if (argv[i][2] != 0) { // Extra Parameters?
          throw std::runtime_error(make_cstyle_format_string("ERROR: Unknown switch \"%s\"\n", argv[i]));
        }
        break;
      }
      default:
      {
        throw std::runtime_error(make_cstyle_format_string("ERROR: Unknown switch \"%s\"\n", argv[i]));
      }
      }
    }
    else { // no switch
      if (input_file_given) {
        throw std::runtime_error(libprecomp_error_msg(ERR_MORE_THAN_ONE_INPUT_FILE));
      }

      input_file_given = true;
      input_file_name = argv[i];
      std::istream* input_stream = nullptr;

      if (input_file_name == "stdin") {
        if (operation != P_RECOMPRESS) {
          throw std::runtime_error(make_cstyle_format_string("ERROR: Reading from stdin or writing to stdout only supported for recompressing.\n"));
        }
        input_stream = &std::cin;
      }
      else {
        precomp_context->fin_length = std::filesystem::file_size(argv[i]);

        auto fin = new std::ifstream();
        fin->open(argv[i], std::ios_base::in | std::ios_base::binary);
        if (!fin->is_open()) {
          throw std::runtime_error(make_cstyle_format_string("ERROR: Input file \"%s\" doesn't exist\n", input_file_name.c_str()));
        }
        input_stream = fin;
      }
      PrecompSetInputStream(&precomp_mgr, input_stream, input_file_name.c_str());

      // output file given? If not, use input filename with .pcf extension
      if (operation == P_RECOMPRESS || comfort_mode) {
        // if .pcf was appended, remove it
        if (appended_pcf) {
          output_file_name = output_file_name.substr(0, output_file_name.length() - 4);
        }

        auto err_code = PrecompReadHeader(&precomp_mgr, false);
        if (err_code == 0) {
          // successfully read PCF header, if already recompressing doesn't matter, but if in comfort_mode we set it to perform recompression
          operation = P_RECOMPRESS;

          auto header_output_filename = PrecompGetOutputFilename(&precomp_mgr);
          if (output_file_name.empty()) {
            if (comfort_mode) {
              // append output filename to the executable directory
              std::string exec_dir = std::filesystem::current_path().string();
              output_file_name = exec_dir;
              output_file_name += PATH_DELIM;
              output_file_name += header_output_filename;
            } else {
              output_file_name = header_output_filename;
            }
          }
        }
        else if (operation == P_RECOMPRESS) {
          // Not in comfort mode, we needed to read the header for recompression to be possible, but that failed
          throw std::runtime_error(libprecomp_error_msg(err_code));
        }
      }
      if ((!output_file_given) && (operation == P_PRECOMPRESS)) {
        if (!preserve_extension) {
          output_file_name = input_file_name;
          const char* backslash_at_pos = strrchr(output_file_name.c_str(), PATH_DELIM);
          const char* dot_at_pos = strrchr(output_file_name.c_str(), '.');
          if ((dot_at_pos == nullptr) || ((backslash_at_pos != nullptr) && (dot_at_pos < backslash_at_pos))) {
            output_file_name += ".pcf";
          }
          else {
            output_file_name = std::string(
              output_file_name.c_str(),
              dot_at_pos - output_file_name.c_str()
            );
            // same as output file because input file had .pcf extension?
            if (input_file_name == output_file_name + ".pcf") {
              output_file_name += "_pcf.pcf";
            }
            else {
              output_file_name += ".pcf";
            }
          }
        }
        else {
          output_file_name = input_file_name + ".pcf";
        }
        output_file_given = true;
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
    print_to_console("  comfort      Read input stream for a PCF header and recompress original stream if found\n");
    print_to_console("               (ignoring any compression parameters), if not precompress the stream instead\n");
    print_to_console("  r            \"Recompress\" PCF file (restore original file)\n");
    print_to_console("  o[filename]  Write output to [filename] <[input_file].pcf or file in header>\n");
    print_to_console("  e            preserve original extension of input name for output name <off>\n");
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

  std::ostream* output_stream;
  if (output_file_given && output_file_name == "stdout") {
    output_stream = &std::cout;
  }
  else {
    if (file_exists(output_file_name.c_str())) {
      print_to_console("Output file \"%s\" exists. Overwrite (y/n)? ", output_file_name.c_str());
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

    auto fout = new std::ofstream();
    fout->open(output_file_name.c_str(), std::ios_base::out | std::ios_base::binary);
    if (!fout->is_open()) {
      throw std::runtime_error(make_cstyle_format_string("ERROR: Can't create output file \"%s\"\n", output_file_name.c_str()));
    }
    output_stream = fout;
  }
  PrecompSetOutStream(&precomp_mgr, output_stream, output_file_name.c_str());


  print_to_console("Input file: %s\n", input_file_name.c_str());
  print_to_console("Output file: %s\n\n", output_file_name.c_str());
  if (PRECOMP_VERBOSITY_LEVEL == PRECOMP_DEBUG_LOG) {
    if (min_ident_size_set) {
      print_to_console("\n");
      print_to_console("Minimal ident size set to %i bytes\n", precomp_switches.min_ident_size);
    }
    if (!ignore_list.empty()) {
      print_to_console("\n");
      print_to_console("Ignore position list:\n");
      for (auto ignore_pos : ignore_list) {
        std::cout << ignore_pos << std::endl;
      }
      print_to_console("\n");
    }
  }

  if (level_switch) { precomp_switches.level_switch_used = true; }

  packjpg_mp3_dll_msg();
  setSwitchesIgnoreList(precomp_switches, ignore_list);

  return operation;
}

int main(int argc, char* argv[])
{
  auto precomp_mgr = std::unique_ptr<CPrecomp, std::function<void(CPrecomp*)>>(PrecompCreate(), [](CPrecomp* ptr) { PrecompDestroy(ptr); });
  CSwitches* precomp_switches = PrecompGetSwitches(precomp_mgr.get());
  PrecompSetProgressCallback(precomp_mgr.get(), [](float percent) {
    auto new_progress_txt = get_progress_txt(percent);
    if (!new_progress_txt) return;
    print_to_console(current_progress_txt);
  });
  PrecompSetLoggingCallback(&log_handler);
  int return_errorlevel = 0;

  // register CTRL-C handler
  (void)signal(SIGINT, ctrl_c_handler);

  try {
    int op = init(*precomp_mgr, *precomp_switches, argc, argv);
    switch (op) {

    case P_PRECOMPRESS:
    {
      return_errorlevel = PrecompPrecompress(precomp_mgr.get());
      break;
    }

    case P_RECOMPRESS:
    {
      return_errorlevel = PrecompRecompress(precomp_mgr.get());
      break;
    }

    }
    if (return_errorlevel != 0) throw std::runtime_error(libprecomp_error_msg(return_errorlevel));

    switch (op) {

      case P_PRECOMPRESS:
      {
        print_results(*precomp_mgr, true);
        print_statistics(*precomp_mgr, *precomp_switches);
        break;
      }
      case P_RECOMPRESS:
      {
        print_results(*precomp_mgr, false);
        break;
      }
    }
  }
  catch (const std::runtime_error& err)
  {
    print_to_console(err.what());
    print_to_console("\n");
    return_errorlevel = return_errorlevel == 0 ? 1 : return_errorlevel;
  }

  return return_errorlevel;
}
