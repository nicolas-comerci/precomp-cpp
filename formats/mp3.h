#ifndef PRECOMP_MP3_HANDLER_H
#define PRECOMP_MP3_HANDLER_H
#include "precomp_dll.h"

const char* packmp3_version_info();

bool mp3_header_check(const unsigned char* checkbuf);

precompression_result precompress_mp3(Precomp& precomp_mgr);

void recompress_mp3(Precomp& precomp_mgr);
#endif // PRECOMP_MP3_HANDLER_H
