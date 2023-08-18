#include "zlib.h"

bool zlib_header_check(const std::span<unsigned char> checkbuf_span) {
  auto checkbuf = checkbuf_span.data();
  bool looks_like_zlib_header = ((((*checkbuf << 8) + *(checkbuf + 1)) % 31) == 0) && ((*(checkbuf + 1) & 32) == 0);  // FDICT must not be set
  if (!looks_like_zlib_header) return false;
  int compression_method = (*checkbuf & 15);
  return compression_method == 8;
}

std::unique_ptr<precompression_result> ZlibFormatHandler::attempt_precompression(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span, const long long original_input_pos) {
  auto checkbuf = checkbuf_span.data();
  std::unique_ptr<deflate_precompression_result> result = std::make_unique<deflate_precompression_result>(D_RAW);
  int windowbits = (*checkbuf >> 4) + 8;

  const auto deflate_stream_pos = original_input_pos + 2; // skip zLib header
  if (check_inflate_result(this->falsePositiveDetector, reinterpret_cast<uintptr_t>(precomp_mgr.ctx->fin.get()), std::span(checkbuf_span.data() + 2, checkbuf_span.size() - 2), -windowbits, deflate_stream_pos)) {

    result = try_decompression_deflate_type(precomp_mgr, precomp_mgr.statistics.decompressed_zlib_count, precomp_mgr.statistics.recompressed_zlib_count,
      D_RAW, checkbuf, 2, deflate_stream_pos, true, "(intense mode)", precomp_mgr.get_tempfile_name("original_zlib"));

    result->original_size_extra += 2;
  }
  return result;
}

std::unique_ptr<PrecompFormatHeaderData> ZlibFormatHandler::read_format_header(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) {
  return read_deflate_format_header(context, precomp_hdr_flags, true);
}

void ZlibFormatHandler::recompress(RecursionContext& context, PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format) {
  recompress_deflate(context, static_cast<DeflateFormatHeaderData&>(precomp_hdr_data), context.precomp.get_tempfile_name("recomp_zlib"), "raw zLib");
}
