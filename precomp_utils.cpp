#include "precomp_utils.h"

#include <random>
#include <sstream>

#ifndef __unix
#include <conio.h>
#include <windows.h>
#else
#include <time.h>
#include <sys/time.h>
#endif
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

#ifndef STDTHREAD_IMPORTED
#define STDTHREAD_IMPORTED
#include <thread>
#endif
#ifdef MINGW
#ifndef _GLIBCXX_HAS_GTHREADS
#include "contrib\mingw_std_threads\mingw.thread.h"
#endif // _GLIBCXX_HAS_GTHREADS
#endif

std::string temp_files_tag() {
  // Generate a random 8digit tag for the temp files of a recursion level, so they don't overwrite each other
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

unsigned int auto_detected_thread_count() {
  auto threads = std::thread::hardware_concurrency();
  if (threads == 0) threads = 2;

  return threads;
}

#ifndef _WIN32
int ttyfd = -1;
#endif

void print_to_console(const std::string& format) {
#ifdef _WIN32
  for (char chr : format) {
    putch(chr);
  }
#else
  if (ttyfd < 0)
    ttyfd = open("/dev/tty", O_RDWR);
  write(ttyfd, format.c_str(), format.length());
#endif
}

char get_char_with_echo() {
#ifndef __unix
  return getche();
#else
  return fgetc(stdin);
#endif
}

std::string precomp_error_msg(int error_nr, const char* extra_info) {
  switch (error_nr) {
  case ERR_IGNORE_POS_TOO_BIG:
    return "Ignore position too big";
  case ERR_IDENTICAL_BYTE_SIZE_TOO_BIG:
    return "Identical bytes size bigger than 4 GB";
  case ERR_ONLY_SET_MIN_SIZE_ONCE:
    return "Minimal identical size can only be set once";
  case ERR_MORE_THAN_ONE_OUTPUT_FILE:
    return "More than one output file given";
  case ERR_MORE_THAN_ONE_INPUT_FILE:
    return "More than one input file given";
  case ERR_DONT_USE_SPACE:
    return "Please don't use a space between the -o switch and the output filename";
  case ERR_TEMP_FILE_DISAPPEARED:
    return "Temporary file has disappeared";
  case ERR_DISK_FULL:
    return "There is not enough space on disk";
  case ERR_RECURSION_DEPTH_TOO_BIG:
    return "Recursion depth too big";
  case ERR_ONLY_SET_RECURSION_DEPTH_ONCE:
    return "Recursion depth can only be set once";
  case ERR_CTRL_C:
    return "CTRL-C detected";
  case ERR_INTENSE_MODE_LIMIT_TOO_BIG:
    return "Intense mode level limit too big";
  case ERR_BRUTE_MODE_LIMIT_TOO_BIG:
    return "Brute mode level limit too big";
  case ERR_ONLY_SET_LZMA_MEMORY_ONCE:
    return "LZMA maximal memory can only be set once";
  case ERR_ONLY_SET_LZMA_THREAD_ONCE:
    return "LZMA thread count can only be set once";
  case ERR_DURING_RECOMPRESSION:
    return "Error recompressing data!";
  case ERR_NO_PCF_HEADER:
    return "Input stream has no valid PCF header";
  case ERR_PCF_HEADER_INCOMPATIBLE_VERSION: {
    std::string txt = "Input stream was made with an incompatible Precomp version";
    if (extra_info != nullptr) txt += "\n" + std::string(extra_info);
    return txt;
  }
  case ERR_BROTLI_NO_LONGER_SUPPORTED:
    return "Precompressed stream has a precompressed JPG using Brunsli with Brotli metadata compression, Brotli is no longer supported by precomp";
  default:
    return "Unknown error";
  }
}

PrecompError::PrecompError(int error_code, std::string extra_info) : error_code(error_code), extra_info(std::move(extra_info)) {}

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
