#ifndef PRECOMP_SWF_HANDLER_H
#define PRECOMP_SWF_HANDLER_H
#include "precomp_dll.h"
#include "formats/deflate.h"

#include <span>

bool swf_header_check(const std::span<unsigned char> checkbuf_span);

deflate_precompression_result try_decompression_swf(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span);

void recompress_swf(Precomp& precomp_mgr, std::byte precomp_hdr_flags);

#endif //PRECOMP_SWF_HANDLER_H