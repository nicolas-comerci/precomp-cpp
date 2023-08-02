#ifndef PRECOMP_GZIP_HANDLER_H
#define PRECOMP_GZIP_HANDLER_H
#include "precomp_dll.h"
#include "formats/deflate.h"

#include <span>

bool gzip_header_check(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span);

std::unique_ptr<deflate_precompression_result> try_decompression_gzip(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span, long long input_stream_pos);

void recompress_gzip(RecursionContext& context, std::byte precomp_hdr_flags);

#endif //PRECOMP_GZIP_HANDLER_H