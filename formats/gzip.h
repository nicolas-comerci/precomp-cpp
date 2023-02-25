#ifndef PRECOMP_GZIP_HANDLER_H
#define PRECOMP_GZIP_HANDLER_H
#include "precomp_dll.h"
#include "formats/deflate.h"

#include <span>

bool gzip_header_check(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span);

deflate_precompression_result try_decompression_gzip(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span);

void recompress_gzip(Precomp& precomp_mgr, std::byte precomp_hdr_flags);

#endif //PRECOMP_GZIP_HANDLER_H