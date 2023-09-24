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
  auto fmt_hdr = std::make_unique<DeflateFormatHeaderData>();
  fmt_hdr->read_data(*context.fin, precomp_hdr_flags, true);
  return fmt_hdr;
}

void ZlibFormatHandler::write_pre_recursion_data(RecursionContext& context, PrecompFormatHeaderData& precomp_hdr_data) {
  auto precomp_deflate_hdr_data = static_cast<DeflateFormatHeaderData&>(precomp_hdr_data);
  // Write zlib_header
  context.fout->write(reinterpret_cast<char*>(precomp_deflate_hdr_data.stream_hdr.data()), precomp_deflate_hdr_data.stream_hdr.size());
}

void ZlibFormatHandler::recompress(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format, const Tools& tools) {
  recompress_deflate(precompressed_input, recompressed_stream, static_cast<DeflateFormatHeaderData&>(precomp_hdr_data), tools.get_tempfile_name("recomp_zlib", true), "raw zLib", tools);
}
