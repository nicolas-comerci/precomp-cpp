#include "precomp_utils.h"

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

bool DEBUG_MODE = false;

int auto_detected_thread_count() {
  int threads = std::thread::hardware_concurrency();
  if (threads == 0) threads = 2;

  return threads;
}

#ifndef _WIN32
int ttyfd = -1;
#endif

void print_to_console(std::string format) {
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

void wait_for_key() {
  print_to_console("\nPress any key to continue\n");
  get_char_with_echo();
}

const char* error_msg(int error_nr) {
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
  default:
    return "Unknown error";
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

long long work_sign_start_time = get_time_ms();
int work_sign_var = 0;
static char work_signs[5] = "|/-\\";
std::string next_work_sign() {
  work_sign_var = (work_sign_var + 1) % 4;
  work_sign_start_time = get_time_ms();
  return make_cstyle_format_string("%c     ", work_signs[work_sign_var]);
}

long long sec_time;
std::string current_percent_progress_txt = "  0.00% ";
void delete_current_progress_text() {
  const auto old_text_length = current_percent_progress_txt.length() + 6;  // we know the work sign is always 6 chars
  print_to_console(std::string(old_text_length, '\b'));
}

void show_progress(std::optional<float> percent, bool clean_prior_progress, bool check_time) {
  if (check_time && ((get_time_ms() - sec_time) < 250)) return;  // not enough time passed since last progress update, quit to not spam

  std::string new_percent_progress_txt = percent.has_value() ? make_cstyle_format_string("%6.2f%% ", percent.value()) : current_percent_progress_txt;
  std::string new_work_sign = next_work_sign();

  if (clean_prior_progress) {
    delete_current_progress_text();
  }
  else {
    print_to_console("\n");  // print on next line so progress is legible on a separate line than the prior one
  }

  current_percent_progress_txt = new_percent_progress_txt;
  print_to_console(current_percent_progress_txt);
  print_to_console(new_work_sign.c_str());

  sec_time = get_time_ms();
}
