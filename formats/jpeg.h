#ifndef PRECOMP_JPEG_HANDLER_H
#define PRECOMP_JPEG_HANDLER_H
#include "precomp_dll.h"

const char* packjpg_version_info();

bool jpeg_header_check(const unsigned char* checkbuf);

precompression_result precompress_jpeg(Precomp& precomp_mgr);

void recompress_jpg(Precomp& precomp_mgr, std::byte flags);

#endif //PRECOMP_JPEG_HANDLER_H