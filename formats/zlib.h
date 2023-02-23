#ifndef PRECOMP_ZLIB_HANDLER_H
#define PRECOMP_ZLIB_HANDLER_H
#include "precomp_dll.h"
#include "deflate.h"

bool zlib_header_check(const unsigned char* checkbuf);

deflate_precompression_result try_decompression_zlib(Precomp& precomp_mgr);

void recompress_zlib(Precomp& precomp_mgr, unsigned char precomp_hdr_flags);

#endif //PRECOMP_ZLIB_HANDLER_H