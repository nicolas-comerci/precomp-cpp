#ifndef PRECOMP_GZIP_HANDLER_H
#define PRECOMP_GZIP_HANDLER_H
#include "precomp_dll.h"
#include "formats/deflate.h"

bool gzip_header_check(Precomp& precomp_mgr, const unsigned char* checkbuf);

deflate_precompression_result try_decompression_gzip(Precomp& precomp_mgr);

void recompress_gzip(Precomp& precomp_mgr, std::byte precomp_hdr_flags);

#endif //PRECOMP_GZIP_HANDLER_H