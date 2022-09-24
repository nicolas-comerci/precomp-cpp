#ifndef PRECOMP_UTILS_H
#define PRECOMP_UTILS_H
#include <cassert>
#include <string>

// This is to be able to print to the console during stdout mode, as prints would get mixed with actual data otherwise, and not be displayed anyways
void print_to_console(std::string format);

template< typename... Args >
void print_to_console(const char* format, Args... args) {
  int length = std::snprintf(nullptr, 0, format, args...);
  assert(length >= 0);

  char* buf = new char[length + 1];
  std::snprintf(buf, length + 1, format, args...);

  std::string str(buf);
  delete[] buf;
  print_to_console(str);
}
#endif // PRECOMP_UTILS_H
