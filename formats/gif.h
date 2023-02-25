#ifndef PRECOMP_GIF_HANDLER_H
#define PRECOMP_GIF_HANDLER_H
#include "precomp_dll.h"

#include <span>

class gif_precompression_result : public precompression_result {
  void dump_gif_diff_to_outfile(Precomp& precomp_mgr);
public:

  std::vector<unsigned char> gif_diff;

  gif_precompression_result();

  void dump_to_outfile(Precomp& precomp_mgr) override;
};

bool gif_header_check(const std::span<unsigned char> checkbuf_span);

gif_precompression_result precompress_gif(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span);

void try_recompression_gif(Precomp& precomp_mgr, std::byte header1, std::string& tempfile, std::string& tempfile2);

#endif // PRECOMP_GIF_HANDLER_H