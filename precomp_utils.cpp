#include "precomp_utils.h"

#ifndef __unix
#include <conio.h>
#endif

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
