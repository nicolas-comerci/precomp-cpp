#ifndef PRECOMP_ZIP_HANDLER_H
#define PRECOMP_ZIP_HANDLER_H
#include "precomp_dll.h"
#include "formats/deflate.h"

bool zip_header_check(Precomp& precomp_mgr, const unsigned char* checkbuf);

deflate_precompression_result try_decompression_zip(Precomp& precomp_mgr);

void recompress_zip(Precomp& precomp_mgr, std::byte precomp_hdr_flags);

#endif //PRECOMP_ZIP_HANDLER_H