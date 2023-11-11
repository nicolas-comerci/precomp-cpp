#include "zlib.h"

bool zlib_header_check(const std::span<unsigned char> checkbuf_span) {
  auto checkbuf = checkbuf_span.data();
  bool looks_like_zlib_header = ((((*checkbuf << 8) + *(checkbuf + 1)) % 31) == 0) && ((*(checkbuf + 1) & 32) == 0);  // FDICT must not be set
  if (!looks_like_zlib_header) return false;
  int compression_method = (*checkbuf & 15);
  return compression_method == 8;
}

bool ZlibFormatHandler::quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) {
  const bool looks_like_zlib_header = zlib_header_check(buffer);
  if (!looks_like_zlib_header) return false;
  const auto checkbuf = buffer.data();
  const auto checkbuf_skip_zlib_hdr = std::span(checkbuf + 2, buffer.size() - 2);
  const int windowbits = (*checkbuf >> 4) + 8;
  return check_inflate_result(this->falsePositiveDetector, current_input_id, checkbuf_skip_zlib_hdr, -windowbits, original_input_pos, false);
}

DeflateWithHeaderPrecompressor::DeflateWithHeaderPrecompressor(std::vector<unsigned char>&& _pre_deflate_header, const std::function<void()>& _progress_callback) :
  PrecompFormatPrecompressor(_progress_callback),
  deflate_precompressor(std::make_unique<DeflatePrecompressor>(_progress_callback)), pre_deflate_header(std::move(_pre_deflate_header)) {}

PrecompProcessorReturnCode DeflateWithHeaderPrecompressor::process(bool input_eof) {
  while (hdr_bytes_skipped < pre_deflate_header.size() && avail_in > 0) {
    // TODO: maybe check that the bytes are the same?
    avail_in -= 1;
    next_in += 1;
    hdr_bytes_skipped++;
  }

  // forward pointers and counts
  deflate_precompressor->avail_in = avail_in;
  deflate_precompressor->next_in = next_in;
  deflate_precompressor->avail_out = avail_out;
  deflate_precompressor->next_out = next_out;

  const auto retval = deflate_precompressor->process(input_eof);

  // recover pointers and counts
  avail_in = deflate_precompressor->avail_in;
  next_in = deflate_precompressor->next_in;
  avail_out = deflate_precompressor->avail_out;
  next_out = deflate_precompressor->next_out;

  original_stream_size = deflate_precompressor->original_stream_size + hdr_bytes_skipped;

  return retval;
}

void DeflateWithHeaderPrecompressor::dump_extra_stream_header_data(OStreamLike& output) {
  fout_fput_vlint(output, pre_deflate_header.size());
  output.write(reinterpret_cast<char*>(pre_deflate_header.data()), pre_deflate_header.size() - 1);
  output.put(pre_deflate_header[pre_deflate_header.size() - 1] + 1);
}
void DeflateWithHeaderPrecompressor::dump_extra_block_header_data(OStreamLike& output) { return deflate_precompressor->dump_extra_block_header_data(output); }

std::unique_ptr<PrecompFormatPrecompressor> ZlibFormatHandler::make_precompressor(Precomp& precomp_mgr, const std::span<unsigned char>& buffer) {
  return std::make_unique<DeflateWithHeaderPrecompressor>(std::vector(buffer.data(), buffer.data() + 2), [&precomp_mgr]() { precomp_mgr.call_progress_callback(); });
}

void ZlibFormatHandler::write_pre_recursion_data(RecursionContext& context, PrecompFormatHeaderData& precomp_hdr_data) {
  const auto hdr_data = static_cast<DeflateFormatHeaderData&>(precomp_hdr_data);
  context.fout->write(reinterpret_cast<const char*>(hdr_data.stream_hdr.data()), hdr_data.stream_hdr.size());
}