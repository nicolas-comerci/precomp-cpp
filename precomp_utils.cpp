#include "precomp_utils.h"

#ifndef __unix
#include <conio.h>
#include <windows.h>
#else
#include <time.h>
#include <sys/time.h>
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

#ifdef COMFORT
void wait_for_key() {
  print_to_console("\nPress any key to continue\n");
  get_char_with_echo();
}
#endif

void error(int error_nr, std::string tmp_filename) {
  print_to_console("\nERROR %i: ", error_nr);
  switch (error_nr) {
  case ERR_IGNORE_POS_TOO_BIG:
    print_to_console("Ignore position too big");
    break;
  case ERR_IDENTICAL_BYTE_SIZE_TOO_BIG:
    print_to_console("Identical bytes size bigger than 4 GB");
    break;
  case ERR_ONLY_SET_MIN_SIZE_ONCE:
    print_to_console("Minimal identical size can only be set once");
    break;
  case ERR_MORE_THAN_ONE_OUTPUT_FILE:
    print_to_console("More than one output file given");
    break;
  case ERR_MORE_THAN_ONE_INPUT_FILE:
    print_to_console("More than one input file given");
    break;
  case ERR_DONT_USE_SPACE:
    print_to_console("Please don't use a space between the -o switch and the output filename");
    break;
  case ERR_TEMP_FILE_DISAPPEARED:
    print_to_console("Temporary file %s disappeared", tmp_filename.c_str());
    break;
  case ERR_DISK_FULL:
    print_to_console("There is not enough space on disk");
    // delete output file
    //g_precomp.ctx->fout->close();
    //remove(g_precomp.ctx->output_file_name.c_str());
    break;
  case ERR_RECURSION_DEPTH_TOO_BIG:
    print_to_console("Recursion depth too big");
    break;
  case ERR_ONLY_SET_RECURSION_DEPTH_ONCE:
    print_to_console("Recursion depth can only be set once");
    break;
  case ERR_CTRL_C:
    print_to_console("CTRL-C detected");
    break;
  case ERR_INTENSE_MODE_LIMIT_TOO_BIG:
    print_to_console("Intense mode level limit too big");
    break;
  case ERR_BRUTE_MODE_LIMIT_TOO_BIG:
    print_to_console("Brute mode level limit too big");
    break;
  case ERR_ONLY_SET_LZMA_MEMORY_ONCE:
    print_to_console("LZMA maximal memory can only be set once");
    break;
  case ERR_ONLY_SET_LZMA_THREAD_ONCE:
    print_to_console("LZMA thread count can only be set once");
    break;
  default:
    print_to_console("Unknown error");
  }
  print_to_console("\n");

#ifdef COMFORT
  wait_for_key();
#endif

  exit(error_nr);
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
void print_work_sign(bool with_backspace) {
  if (DEBUG_MODE) return;
  if ((get_time_ms() - work_sign_start_time) >= 250) {
    work_sign_var = (work_sign_var + 1) % 4;
    work_sign_start_time = get_time_ms();
    if (with_backspace) print_to_console("\b\b\b\b\b\b");
    print_to_console("%c     ", work_signs[work_sign_var]);
  }
  else if (!with_backspace) {
    print_to_console("%c     ", work_signs[work_sign_var]);
  }
}

int old_lzma_progress_text_length = -1;
long long sec_time;
void show_progress(float percent, bool use_backspaces, bool check_time, std::optional<int> lzma_mib_total, std::optional<int> lzma_mib_written) {
  if (check_time && ((get_time_ms() - sec_time) < 250)) return;
  char lzma_progress_text[70];
  if (use_backspaces) {
    print_to_console("%s", std::string(6, '\b').c_str()); // backspace to remove work sign and 5 extra spaces
  }

  bool new_lzma_text = false;
  if (lzma_mib_total.has_value() && lzma_mib_written.has_value()) {
    int snprintf_ret = snprintf(lzma_progress_text, 70, "lzma total/written/left: %i/%i/%i MiB ", lzma_mib_total.value(), lzma_mib_written.value(), lzma_mib_total.value() - lzma_mib_written.value());
    if ((snprintf_ret > -1) && (snprintf_ret < 70)) {
      new_lzma_text = true;
      if ((old_lzma_progress_text_length > -1) && (use_backspaces)) {
        print_to_console("%s", std::string(old_lzma_progress_text_length, '\b').c_str()); // backspaces to remove old lzma progress text
      }
      old_lzma_progress_text_length = snprintf_ret;
    }
  }

  if (use_backspaces) {
    print_to_console("%s", std::string(8, '\b').c_str()); // backspaces to remove output from %6.2f%
  }
  print_to_console("%6.2f%% ", percent);

  if (new_lzma_text) {
    print_to_console("%s", lzma_progress_text);
  }

  print_work_sign(false);
  sec_time = get_time_ms();
}
