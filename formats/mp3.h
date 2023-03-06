#ifndef PRECOMP_MP3_HANDLER_H
#define PRECOMP_MP3_HANDLER_H
#include "precomp_dll.h"

#include <span>

const char* packmp3_version_info();

bool mp3_header_check(const std::span<unsigned char> checkbuf_span);

precompression_result precompress_mp3(Precomp& precomp_mgr, long long original_input_pos, const std::span<unsigned char> checkbuf_span);

void recompress_mp3(RecursionContext& context);
#endif // PRECOMP_MP3_HANDLER_H
