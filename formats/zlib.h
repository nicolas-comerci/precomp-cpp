#ifndef PRECOMP_ZLIB_HANDLER_H
#define PRECOMP_ZLIB_HANDLER_H
#include "precomp_dll.h"

bool zlib_header_check(unsigned char* checkbuf)
{
  bool looks_like_zlib_header = ((((*checkbuf << 8) + *(checkbuf+1)) % 31) == 0) && ((*(checkbuf + 1) & 32) == 0);  // FDICT must not be set
  if (!looks_like_zlib_header) return false;
  int compression_method = (*checkbuf & 15);
  return compression_method == 8;
}

deflate_precompression_result try_decompression_zlib(Precomp& precomp_mgr) {
  deflate_precompression_result result = deflate_precompression_result(D_RAW);
  int windowbits = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb] >> 4) + 8;

  if (check_inflate_result(precomp_mgr, precomp_mgr.ctx->in_buf, precomp_mgr.out, precomp_mgr.ctx->cb + 2, -windowbits)) {
    precomp_mgr.ctx->input_file_pos += 2; // skip zLib header

    result = try_decompression_deflate_type_internal(precomp_mgr, precomp_mgr.statistics.decompressed_zlib_count, precomp_mgr.statistics.recompressed_zlib_count,
      D_RAW, precomp_mgr.ctx->in_buf + precomp_mgr.ctx->cb, 2, true,
      "(intense mode)", temp_files_tag() + "_original_zlib");

    precomp_mgr.ctx->cb += 2;
  }
  return result;
}

void recompress_zlib(Precomp& precomp_mgr, unsigned char precomp_hdr_flags) {
  recompress_deflate(precomp_mgr, precomp_hdr_flags, true, temp_files_tag() + "_recomp_zlib", "raw zLib");
}

#endif //PRECOMP_ZLIB_HANDLER_H