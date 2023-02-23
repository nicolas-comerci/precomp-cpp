#ifndef PRECOMP_UTILS_H
#define PRECOMP_UTILS_H
#include <cassert>
#include <string>


std::string temp_files_tag();

unsigned int auto_detected_thread_count();

// This is to be able to print to the console during stdout mode, as prints would get mixed with actual data otherwise, and not be displayed anyway
void print_to_console(const std::string& format);

template< typename... Args >
std::string make_cstyle_format_string(const char* format, Args... args) {
  int length = std::snprintf(nullptr, 0, format, args...);
  assert(length >= 0);

  char* buf = new char[length + 1];
  std::snprintf(buf, length + 1, format, args...);

  std::string str(buf);
  delete[] buf;
  return str;
}

template< typename... Args >
void print_to_console(const char* format, Args... args) {
  print_to_console(make_cstyle_format_string(format, args...));
}

char get_char_with_echo();

// batch error levels
constexpr auto RETURN_SUCCESS = 0;
constexpr auto ERR_GENERIC_OR_UNKNOWN = 1;
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
constexpr auto ERR_DURING_RECOMPRESSION = 19;
constexpr auto ERR_NO_PCF_HEADER = 20;
constexpr auto ERR_PCF_HEADER_INCOMPATIBLE_VERSION = 21;

class PrecompError: public std::exception {
public:
  int error_code;
  std::string extra_info;

  explicit PrecompError(int error_code, std::string extra_info = "");
};

std::string precomp_error_msg(int error_nr, const char* extra_info = nullptr);

long long get_time_ms();
#endif // PRECOMP_UTILS_H
