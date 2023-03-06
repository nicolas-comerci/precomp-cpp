#include "gzip.h"

bool gzip_header_check(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span) {
  auto checkbuf = checkbuf_span.data();
  if ((*checkbuf == 31) && (*(checkbuf + 1) == 139)) {
    // check zLib header in GZip header
    int compression_method = (*(checkbuf + 2) & 15);
    // reserved FLG bits must be zero
    if ((compression_method == 8) && ((*(checkbuf + 3) & 224) == 0)) return true;
  }
  return false;
}

deflate_precompression_result try_decompression_gzip(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span, long long input_stream_pos) {
  auto checkbuf = checkbuf_span.data();
  deflate_precompression_result result = deflate_precompression_result(D_GZIP);
  //((*(checkbuf + 8) == 2) || (*(checkbuf + 8) == 4)) { //XFL = 2 or 4
          //  TODO: Can be 0 also, check if other values are used, too.
          //
          //  TODO: compressed data is followed by CRC-32 and uncompressed
          //    size. Uncompressed size can be used to check if it is really
          //    a GZ stream.

  bool fhcrc = (*(checkbuf + 3) & 2) == 2;
  bool fextra = (*(checkbuf + 3) & 4) == 4;
  bool fname = (*(checkbuf + 3) & 8) == 8;
  bool fcomment = (*(checkbuf + 3) & 16) == 16;

  int header_length = 10;

  bool dont_compress = false;

  if (fhcrc || fextra || fname || fcomment) {
    int act_checkbuf_pos = 10;

    if (fextra) {
      int xlen = *(checkbuf + act_checkbuf_pos) + (*(checkbuf + act_checkbuf_pos + 1) << 8);
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
      } while ((*(checkbuf + act_checkbuf_pos - 1) != 0) && (!dont_compress));
    }
    if ((fcomment) && (!dont_compress)) {
      do {
        act_checkbuf_pos++;
        dont_compress = (act_checkbuf_pos == CHECKBUF_SIZE);
        header_length++;
      } while ((*(checkbuf + act_checkbuf_pos - 1) != 0) && (!dont_compress));
    }
    if ((fhcrc) && (!dont_compress)) {
      act_checkbuf_pos += 2;
      dont_compress = (act_checkbuf_pos > CHECKBUF_SIZE);
      header_length += 2;
    }
  }
  if (dont_compress) return result;

  result = try_decompression_deflate_type(precomp_mgr, precomp_mgr.statistics.decompressed_gzip_count, precomp_mgr.statistics.recompressed_gzip_count,
    D_GZIP, checkbuf + 2, header_length - 2, input_stream_pos + header_length, false,
    "in GZIP", temp_files_tag() + "_precomp_gzip");

  result.input_pos_extra_add += header_length;  // Add the Gzip header length to the deflate stream size for the proper original Gzip stream size
  return result;
}

void recompress_gzip(RecursionContext& context, std::byte precomp_hdr_flags) {
  context.fout->put(31);
  context.fout->put(139);
  recompress_deflate(context, precomp_hdr_flags, false, temp_files_tag() + "_recomp_gzip", "GZIP");
}
