#ifndef PRECOMP_ZIP_HANDLER_H
#define PRECOMP_ZIP_HANDLER_H
#include "precomp_dll.h"
#include "formats/deflate.h"

#include <span>

bool zip_header_check(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span, long long input_stream_pos);

deflate_precompression_result try_decompression_zip(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span, long long input_stream_pos);

void recompress_zip(RecursionContext& context, std::byte precomp_hdr_flags);

#endif //PRECOMP_ZIP_HANDLER_H