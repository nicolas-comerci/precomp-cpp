#include "gzip.h"

bool gzip_header_check(Precomp& precomp_mgr, const unsigned char* checkbuf) {
  if ((*checkbuf == 31) && (*(checkbuf + 1) == 139)) {
    // check zLib header in GZip header
    int compression_method = (*(checkbuf + 2) & 15);
    // reserved FLG bits must be zero
    if ((compression_method == 8) && ((*(checkbuf + 3) & 224) == 0)) return true;
  }
  return false;
}

deflate_precompression_result try_decompression_gzip(Precomp& precomp_mgr) {
  deflate_precompression_result result = deflate_precompression_result(D_GZIP);
  //((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 8] == 2) || (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 8] == 4)) { //XFL = 2 or 4
          //  TODO: Can be 0 also, check if other values are used, too.
          //
          //  TODO: compressed data is followed by CRC-32 and uncompressed
          //    size. Uncompressed size can be used to check if it is really
          //    a GZ stream.

  bool fhcrc = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] & 2) == 2;
  bool fextra = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] & 4) == 4;
  bool fname = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] & 8) == 8;
  bool fcomment = (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] & 16) == 16;

  int header_length = 10;

  bool dont_compress = false;

  if (fhcrc || fextra || fname || fcomment) {
    int act_checkbuf_pos = 10;

    if (fextra) {
      int xlen = precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_checkbuf_pos] + (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_checkbuf_pos + 1] << 8);
      if ((act_checkbuf_pos + xlen) > CHECKBUF_SIZE) {
        dont_compress = true;
      }
      else {
        act_checkbuf_pos += 2;
        header_length += 2;
        act_checkbuf_pos += xlen;
        header_length += xlen;
      }
    }
    if ((fname) && (!dont_compress)) {
      do {
        act_checkbuf_pos++;
        dont_compress = (act_checkbuf_pos == CHECKBUF_SIZE);
        header_length++;
      } while ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_checkbuf_pos - 1] != 0) && (!dont_compress));
    }
    if ((fcomment) && (!dont_compress)) {
      do {
        act_checkbuf_pos++;
        dont_compress = (act_checkbuf_pos == CHECKBUF_SIZE);
        header_length++;
      } while ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + act_checkbuf_pos - 1] != 0) && (!dont_compress));
    }
    if ((fhcrc) && (!dont_compress)) {
      act_checkbuf_pos += 2;
      dont_compress = (act_checkbuf_pos > CHECKBUF_SIZE);
      header_length += 2;
    }
  }
  if (dont_compress) return result;

  precomp_mgr.ctx->input_file_pos += header_length; // skip GZip header

  result = try_decompression_deflate_type(precomp_mgr, precomp_mgr.statistics.decompressed_gzip_count, precomp_mgr.statistics.recompressed_gzip_count,
    D_GZIP, precomp_mgr.ctx->in_buf + precomp_mgr.ctx->cb + 2, header_length - 2, false,
    "in GZIP", temp_files_tag() + "_precomp_gzip");

  precomp_mgr.ctx->cb += header_length;
  return result;
}

void recompress_gzip(Precomp& precomp_mgr, std::byte precomp_hdr_flags) {
  precomp_mgr.ctx->fout->put(31);
  precomp_mgr.ctx->fout->put(139);
  recompress_deflate(precomp_mgr, precomp_hdr_flags, false, temp_files_tag() + "_recomp_gzip", "GZIP");
}
