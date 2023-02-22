#ifndef PRECOMP_SWF_HANDLER_H
#define PRECOMP_SWF_HANDLER_H
#include "precomp_dll.h"
#include "formats/zlib.h"

bool swf_header_check(unsigned char* checkbuf)
{
  // CWS = Compressed SWF file
  auto cws_hdr = (*checkbuf == 'C') && (*(checkbuf + 1) == 'W') && (*(checkbuf + 2) == 'S');
  return cws_hdr && zlib_header_check(checkbuf + 8);
}

deflate_precompression_result try_decompression_swf(Precomp& precomp_mgr) {
  deflate_precompression_result result = deflate_precompression_result(D_SWF);
  int windowbits = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 8] >> 4) + 8;

  precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
  precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

  precomp_mgr.ctx->input_file_pos += 10; // skip CWS and zLib header

  result = try_decompression_deflate_type_internal(precomp_mgr, precomp_mgr.statistics.decompressed_swf_count, precomp_mgr.statistics.recompressed_swf_count,
    D_SWF, precomp_mgr.ctx->in_buf + precomp_mgr.ctx->cb + 3, 7, true,
    "in SWF", temp_files_tag() + "_original_swf");

  precomp_mgr.ctx->cb += 10;
  return result;
}

void recompress_swf(Precomp& precomp_mgr, unsigned char precomp_hdr_flags) {
  precomp_mgr.ctx->fout->put('C');
  precomp_mgr.ctx->fout->put('W');
  precomp_mgr.ctx->fout->put('S');

  recompress_deflate(precomp_mgr, precomp_hdr_flags, true, temp_files_tag() + "_recomp_swf", "SWF");
}

#endif //PRECOMP_SWF_HANDLER_H