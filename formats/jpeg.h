#ifndef PRECOMP_JPEG_HANDLER_H
#define PRECOMP_JPEG_HANDLER_H
#include "precomp_dll.h"

#include <span>

const char* packjpg_version_info();

bool jpeg_header_check(const std::span<unsigned char> checkbuf_span);

precompression_result precompress_jpeg(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span);

void recompress_jpg(Precomp& precomp_mgr, std::byte flags);

#endif //PRECOMP_JPEG_HANDLER_H