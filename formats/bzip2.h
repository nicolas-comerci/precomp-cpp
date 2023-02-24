#ifndef PRECOMP_BZip2_HANDLER_H
#define PRECOMP_BZip2_HANDLER_H
#include "precomp_dll.h"

class bzip2_precompression_result : public precompression_result {
public:
  bool recursion_used = false;
  long long recursion_filesize;
  int compression_level;

  explicit bzip2_precompression_result(int compression_level);

  void dump_precompressed_data_to_outfile(Precomp& precomp_mgr) override;
  void dump_to_outfile(Precomp& precomp_mgr) override;
};

bool bzip2_header_check(const unsigned char* checkbuf);

bzip2_precompression_result try_decompression_bzip2(Precomp& precomp_mgr);

void recompress_bzip2(Precomp& precomp_mgr, std::byte precomp_hdr_flags);

#endif //PRECOMP_BZip2_HANDLER_H