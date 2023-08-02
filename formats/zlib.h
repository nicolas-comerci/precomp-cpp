#ifndef PRECOMP_ZLIB_HANDLER_H
#define PRECOMP_ZLIB_HANDLER_H
#include "precomp_dll.h"
#include "deflate.h"

#include <span>

bool zlib_header_check(const std::span<unsigned char> checkbuf_span);

std::unique_ptr<deflate_precompression_result> try_decompression_zlib(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span, const long long original_input_pos);

void recompress_zlib(RecursionContext& context, std::byte precomp_hdr_flags);

#endif //PRECOMP_ZLIB_HANDLER_H