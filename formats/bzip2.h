#ifndef PRECOMP_BZip2_HANDLER_H
#define PRECOMP_BZip2_HANDLER_H
#include "precomp_dll.h"

#include <span>

class bzip2_precompression_result : public precompression_result {
public:
  bool recursion_used = false;
  long long recursion_filesize;
  int compression_level;

  explicit bzip2_precompression_result(int compression_level);

  void dump_precompressed_data_to_outfile(Precomp& precomp_mgr) override;
  void dump_to_outfile(Precomp& precomp_mgr) override;
};

bool bzip2_header_check(const std::span<unsigned char> checkbuf_span);

bzip2_precompression_result try_decompression_bzip2(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span, const long long original_input_pos);

void recompress_bzip2(RecursionContext& context, std::byte precomp_hdr_flags);

#endif //PRECOMP_BZip2_HANDLER_H