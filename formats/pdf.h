#ifndef PRECOMP_PDF_HANDLER_H
#define PRECOMP_PDF_HANDLER_H
#include "precomp_dll.h"
#include "deflate.h"

enum BMP_HEADER_TYPE {
  BMP_HEADER_NONE = 0,
  BMP_HEADER_8BPP = 1,
  BMP_HEADER_24BPP = 2
};

bool pdf_header_check(unsigned char* checkbuf);

class pdf_precompression_result: public deflate_precompression_result {
public:
  unsigned int img_width;
  unsigned int img_height;
  BMP_HEADER_TYPE bmp_header_type = BMP_HEADER_NONE;

  pdf_precompression_result(unsigned int img_width, unsigned int img_height);

  void dump_precompressed_data_to_outfile(Precomp& precomp_mgr) override;
  void dump_bmp_hdr_to_outfile(Precomp& precomp_mgr);
  void dump_to_outfile(Precomp& precomp_mgr) override;
};

pdf_precompression_result precompress_pdf(Precomp& precomp_mgr);

void recompress_pdf(Precomp& precomp_mgr, unsigned char precomp_hdr_flags);

#endif //PRECOMP_PDF_HANDLER_H