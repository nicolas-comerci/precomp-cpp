#ifndef PRECOMP_B64_HANDLER_H
#define PRECOMP_B64_HANDLER_H
#include "precomp_dll.h"

class base64_precompression_result : public precompression_result {
public:
  std::vector<unsigned char> base64_header;
  int line_case;
  std::vector<unsigned int> base64_line_len;
  bool recursion_used = false;
  long long recursion_filesize;

  base64_precompression_result();

  void dump_base64_header(Precomp& precomp_mgr);

  void dump_precompressed_data_to_outfile(Precomp& precomp_mgr) override;

  void dump_to_outfile(Precomp& precomp_mgr) override;
};

bool base64_header_check(const unsigned char* checkbuf);

base64_precompression_result precompress_base64(Precomp& precomp_mgr);

void recompress_base64(Precomp& precomp_mgr, unsigned char precomp_hdr_flags);

#endif //PRECOMP_B64_HANDLER_H