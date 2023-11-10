#include "zlib.h"

bool zlib_header_check(const std::span<unsigned char> checkbuf_span) {
  auto checkbuf = checkbuf_span.data();
  bool looks_like_zlib_header = ((((*checkbuf << 8) + *(checkbuf + 1)) % 31) == 0) && ((*(checkbuf + 1) & 32) == 0);  // FDICT must not be set
  if (!looks_like_zlib_header) return false;
  int compression_method = (*checkbuf & 15);
  return compression_method == 8;
}

bool ZlibFormatHandler2::quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) {
  const bool looks_like_zlib_header = zlib_header_check(buffer);
  if (!looks_like_zlib_header) return false;
  const auto checkbuf = buffer.data();
  const auto checkbuf_skip_zlib_hdr = std::span(checkbuf + 2, checkbuf - 2);
  const int windowbits = (*checkbuf >> 4) + 8;
  return check_inflate_result(this->falsePositiveDetector, current_input_id, checkbuf_skip_zlib_hdr, -windowbits, original_input_pos, false);
}

class ZlibPrecompressor: public PrecompFormatPrecompressor {
  std::unique_ptr<DeflatePrecompressor> deflate_precompressor;
  std::vector<unsigned char> zlib_header;
  unsigned int hdr_bytes_skipped = 0;
public:
  ZlibPrecompressor(const std::span<unsigned char>& buffer, const std::function<void()>& _progress_callback):
    PrecompFormatPrecompressor(buffer, _progress_callback),
    deflate_precompressor(std::make_unique<DeflatePrecompressor>(buffer, _progress_callback)), zlib_header(std::vector(buffer.data(), buffer.data() + 2)) {}

  PrecompProcessorReturnCode process(bool input_eof) override {
    while (hdr_bytes_skipped < zlib_header.size() && avail_in > 0) {
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

    return retval;
  }

  void dump_extra_stream_header_data(OStreamLike& output) override {
    fout_fput_vlint(output, zlib_header.size());
    output.write(reinterpret_cast<char*>(const_cast<unsigned char*>(zlib_header.data())), zlib_header.size() - 1);
    output.put(zlib_header[zlib_header.size() - 1] + 1);
  }
  void dump_extra_block_header_data(OStreamLike& output) override { return deflate_precompressor->dump_extra_block_header_data(output); }
};

std::unique_ptr<PrecompFormatPrecompressor> ZlibFormatHandler2::make_precompressor(Precomp& precomp_mgr, const std::span<unsigned char>& buffer) {
  return std::make_unique<ZlibPrecompressor>(buffer, [&precomp_mgr]() { precomp_mgr.call_progress_callback(); });
}

class ZlibRecompressor : public PrecompFormatRecompressor {
  std::unique_ptr<DeflateRecompressor> deflate_recompressor;
  std::vector<unsigned char> zlib_header;
public:
  ZlibRecompressor(const DeflateFormatHeaderData& precomp_hdr_data, const std::function<void()>& _progress_callback):
    PrecompFormatRecompressor(precomp_hdr_data, _progress_callback),
    deflate_recompressor(std::make_unique<DeflateRecompressor>(precomp_hdr_data, _progress_callback)), zlib_header(precomp_hdr_data.stream_hdr) {}

  PrecompProcessorReturnCode process(bool input_eof) override {
    // forward pointers and counts
    deflate_recompressor->avail_in = avail_in;
    deflate_recompressor->next_in = next_in;
    deflate_recompressor->avail_out = avail_out;
    deflate_recompressor->next_out = next_out;

    const auto retval = deflate_recompressor->process(input_eof);

    // recover pointers and counts
    avail_in = deflate_recompressor->avail_in;
    next_in = deflate_recompressor->next_in;
    avail_out = deflate_recompressor->avail_out;
    next_out = deflate_recompressor->next_out;

    return retval;
  }

  void read_extra_block_header_data(IStreamLike& input) override { deflate_recompressor->read_extra_block_header_data(input); }
};

std::unique_ptr<PrecompFormatRecompressor> ZlibFormatHandler2::make_recompressor(PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format, const Tools& tools) {
  return std::make_unique<ZlibRecompressor>(static_cast<DeflateFormatHeaderData&>(precomp_hdr_data), tools.progress_callback);
}

void ZlibFormatHandler2::write_pre_recursion_data(RecursionContext& context, PrecompFormatHeaderData& precomp_hdr_data) {
  const auto hdr_data = static_cast<DeflateFormatHeaderData&>(precomp_hdr_data);
  context.fout->write(reinterpret_cast<const char*>(hdr_data.stream_hdr.data()), hdr_data.stream_hdr.size());
}