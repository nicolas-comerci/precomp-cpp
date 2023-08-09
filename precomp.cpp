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

void(*log_output_func)(const std::string&) = &print_to_console;

void print_to_stderr(const std::string& msg) {
  std::cerr << msg;
}

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
  log_output_func("\n\nCTRL-C detected\n");
  (void) signal(SIGINT, SIG_DFL);

  throw std::runtime_error(libprecomp_error_msg(ERR_CTRL_C));
}

// nice time output, input t in ms
// 2^32 ms maximum, so will display incorrect negative values after about 49 days
void printf_time(long long t) {
  log_output_func("Time: ");
  if (t < 1000) { // several milliseconds
    log_output_func(make_cstyle_format_string("%li millisecond(s)\n", (long)t));
  }
  else if (t < 1000 * 60) { // several seconds
    log_output_func(make_cstyle_format_string("%li second(s), %li millisecond(s)\n", (long)(t / 1000), (long)(t % 1000)));
  }
  else if (t < 1000 * 60 * 60) { // several minutes
    log_output_func(make_cstyle_format_string("%li minute(s), %li second(s)\n", (long)(t / (1000 * 60)), (long)((t / 1000) % 60)));
  }
  else if (t < 1000 * 60 * 60 * 24) { // several hours
    log_output_func(make_cstyle_format_string("%li hour(s), %li minute(s), %li second(s)\n", (long)(t / (1000 * 60 * 60)), (long)((t / (1000 * 60)) % 60), (long)((t / 1000) % 60)));
  }
  else {
    log_output_func(make_cstyle_format_string("%li day(s), %li hour(s), %li minute(s)\n", (long)(t / (1000 * 60 * 60 * 24)), (long)((t / (1000 * 60 * 60)) % 24), (long)((t / (1000 * 60)) % 60)));
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
  log_output_func(std::string(current_progress_txt.length(), '\b'));
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

void print_results(Precomp& precomp_mgr, bool print_new_size, long long start_time) {
  delete_current_progress_text();
  if (print_new_size && output_file_name != "stdout") {
    auto fout_length = std::filesystem::file_size(output_file_name.c_str());
    std::string result_print = "New size: " + std::to_string(fout_length) + " instead of " + std::to_string(PrecompGetRecursionContext(&precomp_mgr)->fin_length) + "     \n";
    log_output_func("100.00% - " + result_print);
  }
  else {
    log_output_func("100.00%%");
  }
  log_output_func("\nDone.\n");
  printf_time(get_time_ms() - start_time);
}

void show_used_levels(Precomp& precomp_mgr, CSwitches& precomp_switches) {
  auto precomp_context = PrecompGetRecursionContext(&precomp_mgr);
  auto precomp_statistics = PrecompGetResultStatistics(&precomp_mgr);

  if (!precomp_context->anything_was_used) {
    if (!precomp_context->non_zlib_was_used) {
      log_output_func("\nNone of the given compression and memory levels could be used.\n");
      log_output_func("There will be no gain compressing the output file.\n");
    } else {
      if ((!precomp_statistics->max_recursion_depth_reached) && (precomp_statistics->max_recursion_depth_used != precomp_switches.max_recursion_depth)) {
          log_output_func("\nYou can speed up Precomp for THIS FILE with these parameters:\n");
          log_output_func("-d");
          log_output_func(make_cstyle_format_string("%i\n", precomp_statistics->max_recursion_depth_used));
      }
    }
    if (precomp_statistics->max_recursion_depth_reached) {
      log_output_func(make_cstyle_format_string("\nMaximal recursion depth %i reached, increasing it could give better results.\n", precomp_switches.max_recursion_depth));
    }
    return;
  }
  
  int level_count = 0;
  log_output_func("\nYou can speed up Precomp for THIS FILE with these parameters:\n");
  log_output_func("-zl");

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
      log_output_func(make_cstyle_format_string(" -t-%s",disable_methods.c_str()));
  }

  if (precomp_statistics->max_recursion_depth_reached) {
      log_output_func(make_cstyle_format_string("\n\nMaximal recursion depth %i reached, increasing it could give better results.\n", precomp_switches.max_recursion_depth));
  } else if (precomp_statistics->max_recursion_depth_used != precomp_switches.max_recursion_depth) {
      log_output_func(" -d");
      log_output_func(make_cstyle_format_string("%i", precomp_statistics->max_recursion_depth_used));
  }

  log_output_func("\n");
}

void print_statistics(Precomp& precomp_mgr, CSwitches& precomp_switches) {
  auto precomp_statistics = PrecompGetResultStatistics(&precomp_mgr);
  log_output_func(make_cstyle_format_string("\nRecompressed streams: %i/%i\n", precomp_statistics->recompressed_streams_count, precomp_statistics->decompressed_streams_count));

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
        log_output_func(format_tag + " streams: " + std::to_string(recompressed_count) + "/" + std::to_string(decompressed_count) + "\n");
    }
  }

  if (!precomp_switches.level_switch_used) show_used_levels(precomp_mgr, precomp_switches);
}

void log_handler(PrecompLoggingLevels level, char* msg) {
  if (level >= PRECOMP_DEBUG_LOG) {
    // This way even with debug logging we keep the progress percent with work sign at the end
    log_output_func("\n" + std::string(msg) + "\n" + current_progress_txt);
  } else {
    log_output_func(msg);
  }
}

void setSwitchesIgnoreList(CSwitches& precomp_switches, const std::vector<long long>& ignore_list) {
  PrecompSwitchesSetIgnoreList(&precomp_switches, ignore_list.data(), ignore_list.size());
}

int init(Precomp& precomp_mgr, CSwitches& precomp_switches, int argc, char* argv[]) {
  auto precomp_context = PrecompGetRecursionContext(&precomp_mgr);

  int i, j;
  bool appended_pcf = false;

  log_output_func("\n");
  if (V_MINOR2 == 0) {
    log_output_func(make_cstyle_format_string("Precomp Neo v%i.%i %s %s - %s version", V_MAJOR, V_MINOR, V_OS, V_BIT, V_STATE));
  }
  else {
    log_output_func(make_cstyle_format_string("Precomp Neo v%i.%i.%i %s %s - %s version", V_MAJOR, V_MINOR, V_MINOR2, V_OS, V_BIT, V_STATE));
  }
  log_output_func(make_cstyle_format_string(" - %s\n", V_MSG));
  log_output_func("Apache 2.0 License - Precomp - Copyright 2006-2021 by Christian Schneider\n");
  log_output_func("  Preflate v0.3.5 support - Copyright 2018 by Dirk Steinke\n");
  log_output_func("  Precomp Neo fork - Copyright 2022-2023 by Nicolas Comerci\n\n");

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
        precomp_switches.max_recursion_depth = parseIntUntilEnd(argv[i] + 2, "maximal recursion depth", ERR_RECURSION_DEPTH_TOO_BIG);
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
        else if (!parseSwitch(precomp_switches.use_brunsli, argv[i] + 1, "brunsli")) {
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
        if (strlen(argv[i]) == 8 && parsePrefixText(argv[i] + 1, "comfort")) {
          comfort_mode = true;
        }
        else { // Extra Parameters?
          throw std::runtime_error(make_cstyle_format_string("ERROR: Unknown switch \"%s\"\n", argv[i]));
        }
        break;
      }
      case 'N':
      {
        if (strlen(argv[i]) == 10 && parsePrefixText(argv[i] + 1, "no-verify")) {
          precomp_switches.verify_precompressed = false;
        }
        else { // Extra Parameters?
          throw std::runtime_error(make_cstyle_format_string("ERROR: Unknown switch \"%s\"\n", argv[i]));
        }
        break;
      }
      case 'V':
      {
        if (strlen(argv[i]) == 2) {
          PRECOMP_VERBOSITY_LEVEL = PRECOMP_DEBUG_LOG;
        }
        else if (strlen(argv[i]) == 8 && parsePrefixText(argv[i] + 1, "vstderr")) {
          log_output_func = &print_to_stderr;
        }
        else { // Extra Parameters?
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
    log_output_func("Usage: precomp [-switches] input_file\n\n");
    if (long_help) {
      log_output_func("Switches (and their <default values>):\n");
    }
    else {
      log_output_func("Common switches (and their <default values>):\n");
    }
    log_output_func("  comfort      Read input stream for a PCF header and recompress original stream if found\n");
    log_output_func("               (ignoring any compression parameters), if not precompress the stream instead\n");
    log_output_func("  r            \"Recompress\" PCF file (restore original file)\n");
    log_output_func("  o[filename]  Write output to [filename] <[input_file].pcf or file in header>\n");
    log_output_func("  e            preserve original extension of input name for output name <off>\n");
    log_output_func("  v            Verbose (debug) mode <off>\n");
    log_output_func("  vstderr      Output messages to stderr instead of directly to the console <off>\n");
    log_output_func("  verify       Verify that precompressed data recompresses correctly with hash check <off>\n");
    log_output_func("  d[depth]     Set maximal recursion depth <10>\n");
    //log_output_func("  zl[1..9][1..9] zLib levels to try for compression (comma separated) <all>\n");
    if (long_help) {
      log_output_func("  pfmeta[amount] Split deflate streams into meta blocks of this size in KiB <2048>\n");
      log_output_func("  pfverify       Force preflate to verify its generated reconstruction data\n");
    }
    log_output_func("  intense      Detect raw zLib headers, too. Slower and more sensitive <off>\n");
    if (long_help) {
      log_output_func("  brute        Brute force zLib detection. VERY Slow and most sensitive <off>\n");
    }
    log_output_func("  t[+-][pzgnfjsmb3] Compression type switch <all enabled>\n");
    log_output_func("              t+ = enable these types only, t- = enable all types except these\n");
    log_output_func("              P = PDF, Z = ZIP, G = GZip, N = PNG, F = GIF, J = JPG\n");
    log_output_func("              S = SWF, M = MIME Base64, B = bZip2, 3 = MP3\n");

    if (!long_help) {
      log_output_func("  longhelp     Show long help\n");
    }
    else {
      log_output_func("  i[pos]       Ignore stream at input file position [pos] <none>\n");
      log_output_func("  s[size]      Set minimal identical byte size to [size] <4 (64 intense mode)>\n");
      log_output_func("  pdfbmp[+-]   Wrap a BMP header around PDF images <off>\n");
      log_output_func("  progonly[+-] Recompress progressive JPGs only (useful for PAQ) <off>\n");
      log_output_func("  mjpeg[+-]    Insert huffman table for MJPEG recompression <on>\n");
      log_output_func("  brunsli[+-]  Prefer brunsli to packJPG for JPG streams <on>\n");
      log_output_func("  packjpg[+-]  Use packJPG for JPG streams and fallback if brunsli fails <on>\n");
      log_output_func("\n");
      log_output_func("  You can use an optional number following -intense and -brute to set a\n");
      log_output_func("  limit for how deep in recursion they should be used. E.g. -intense0 means\n");
      log_output_func("  that intense mode will be used but not in recursion, -intense2 that only\n");
      log_output_func("  streams up to recursion depth 2 will be treated intense (3 or higher in\n");
      log_output_func("  this case won't). Using a sensible setting here can save you some time.\n");
    }

    exit(1);
  }

  std::ostream* output_stream;
  if (output_file_given && output_file_name == "stdout") {
    output_stream = &std::cout;
  }
  else {
    if (file_exists(output_file_name.c_str())) {
      log_output_func(make_cstyle_format_string("Output file \"%s\" exists. Overwrite (y/n)? ", output_file_name.c_str()));
      char ch = get_char_with_echo();
      if ((ch != 'Y') && (ch != 'y')) {
        log_output_func("\n");
        exit(0);
      }
      else {
#ifndef __unix
        log_output_func("\n\n");
#else
        log_output_func("\n");
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


  log_output_func(make_cstyle_format_string("Input file: %s\n", input_file_name.c_str()));
  log_output_func(make_cstyle_format_string("Output file: %s\n\n", output_file_name.c_str()));
  if (PRECOMP_VERBOSITY_LEVEL == PRECOMP_DEBUG_LOG) {
    if (min_ident_size_set) {
      log_output_func("\n");
      log_output_func(make_cstyle_format_string("Minimal ident size set to %i bytes\n", precomp_switches.min_ident_size));
    }
    if (!ignore_list.empty()) {
      log_output_func("\n");
      log_output_func("Ignore position list:\n");
      for (auto ignore_pos : ignore_list) {
        std::cout << ignore_pos << std::endl;
      }
      log_output_func("\n");
    }
  }

  if (level_switch) { precomp_switches.level_switch_used = true; }

  packjpg_mp3_dll_msg();
  setSwitchesIgnoreList(precomp_switches, ignore_list);

  return operation;
}

int main(int argc, char* argv[])
{
  auto precomp_mgr = std::unique_ptr<Precomp, std::function<void(Precomp*)>>(PrecompCreate(), [](Precomp* ptr) { PrecompDestroy(ptr); });
  CSwitches* precomp_switches = PrecompGetSwitches(precomp_mgr.get());
  PrecompSetProgressCallback(precomp_mgr.get(), [](float percent) {
    auto new_progress_txt = get_progress_txt(percent);
    if (!new_progress_txt) return;
    log_output_func(current_progress_txt);
  });
  PrecompSetLoggingCallback(&log_handler);
  int return_errorlevel = 0;

  // register CTRL-C handler
  (void)signal(SIGINT, ctrl_c_handler);

  try {
    int op = init(*precomp_mgr, *precomp_switches, argc, argv);
    auto start_time = get_time_ms();
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
    if (return_errorlevel != 0 && !(return_errorlevel == 2 && op == P_PRECOMPRESS)) throw std::runtime_error(libprecomp_error_msg(return_errorlevel));

    switch (op) {

      case P_PRECOMPRESS:
      {
        print_results(*precomp_mgr, true, start_time);
        print_statistics(*precomp_mgr, *precomp_switches);
        break;
      }
      case P_RECOMPRESS:
      {
        print_results(*precomp_mgr, false, start_time);
        break;
      }
    }
  }
  catch (const std::runtime_error& err)
  {
    log_output_func(err.what());
    log_output_func("\n");
    return_errorlevel = return_errorlevel == 0 ? 1 : return_errorlevel;
  }

  return return_errorlevel;
}
