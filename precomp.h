#ifndef PRECOMP_H
#define PRECOMP_H
#include "precomp_dll.h"

#ifndef COMFORT
int init(Precomp& precomp_mgr, int argc, char* argv[]);
#else
int init_comfort(Precomp& precomp_mgr, int argc, char* argv[]);
#endif
#ifdef COMFORT
bool check_for_pcf_file(Precomp& precomp_mgr);
#endif
#endif // PRECOMP_H
