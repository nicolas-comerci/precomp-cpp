#include "zlib.h"

bool zlib_header_check(const std::span<unsigned char> checkbuf_span) {
  auto checkbuf = checkbuf_span.data();
  bool looks_like_zlib_header = ((((*checkbuf << 8) + *(checkbuf + 1)) % 31) == 0) && ((*(checkbuf + 1) & 32) == 0);  // FDICT must not be set
  if (!looks_like_zlib_header) return false;
  int compression_method = (*checkbuf & 15);
  return compression_method == 8;
}

deflate_precompression_result try_decompression_zlib(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span, const long long original_input_pos) {
  auto checkbuf = checkbuf_span.data();
  deflate_precompression_result result = deflate_precompression_result(D_RAW);
  int windowbits = (*checkbuf >> 4) + 8;

  const auto deflate_stream_pos = original_input_pos + 2; // skip zLib header
  if (check_inflate_result(precomp_mgr, std::span(checkbuf_span.data() + 2, checkbuf_span.size() - 2), -windowbits, deflate_stream_pos)) {

    result = try_decompression_deflate_type(precomp_mgr, precomp_mgr.statistics.decompressed_zlib_count, precomp_mgr.statistics.recompressed_zlib_count,
      D_RAW, checkbuf, 2, deflate_stream_pos, true, "(intense mode)", temp_files_tag() + "_original_zlib");

    result.input_pos_extra_add += 2;
  }
  return result;
}

void recompress_zlib(RecursionContext& context, std::byte precomp_hdr_flags) {
  recompress_deflate(context, precomp_hdr_flags, true, temp_files_tag() + "_recomp_zlib", "raw zLib");
}
