#ifndef PRECOMP_PNG_HANDLER_H
#define PRECOMP_PNG_HANDLER_H
#include "precomp_dll.h"
#include "formats/deflate.h"

#include <span>

bool png_header_check(std::span<unsigned char> checkbuf);

class png_precompression_result: public deflate_precompression_result {
protected:
  void dump_idat_to_outfile(Precomp& precomp_mgr);
public:
  std::vector<unsigned int> idat_crcs;
  std::vector<unsigned int> idat_lengths;
  int idat_count;
  unsigned int idat_pairs_written_count = 0;

  png_precompression_result();

  long long input_pos_add_offset() override;

  void dump_to_outfile(Precomp& precomp_mgr) override;
};

png_precompression_result precompress_png(Precomp& precomp_mgr, std::span<unsigned char> checkbuf, long long original_input_pos);

void recompress_png(Precomp& precomp_mgr, std::byte precomp_hdr_flags);

void recompress_multipng(Precomp& precomp_mgr, std::byte precomp_hdr_flags);

#endif  // PRECOMP_PNG_HANDLER_H