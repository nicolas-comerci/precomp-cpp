#ifndef PRECOMP_XZ_PARAMS_H
#define PRECOMP_XZ_PARAMS_H

typedef struct {
  bool enable_filter_x86;
  bool enable_filter_powerpc;
  bool enable_filter_ia64;
  bool enable_filter_arm;
  bool enable_filter_armthumb;
  bool enable_filter_sparc;
  bool enable_filter_delta;
  
  int filter_delta_distance;

  int lc, lp, pb;
} lzma_init_mt_extra_parameters;

#endif /* ifndef PRECOMP_XZ_PARAMS_H */
