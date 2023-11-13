#ifndef PRECOMP_ZLIB_HANDLER_H
#define PRECOMP_ZLIB_HANDLER_H
#include "precomp_dll.h"
#include "deflate.h"

#include <span>

class DeflateWithHeaderPrecompressor : public PrecompFormatPrecompressor {
protected:
  std::unique_ptr<DeflatePrecompressor> deflate_precompressor;
  std::vector<unsigned char> pre_deflate_header;
  unsigned int hdr_bytes_skipped = 0;
public:
  DeflateWithHeaderPrecompressor(std::vector<unsigned char>&& _pre_deflate_header, const std::function<void()>& _progress_callback, Tools* _precomp_tools);

  PrecompProcessorReturnCode process(bool input_eof) override;
  void dump_extra_stream_header_data(OStreamLike& output) override;
  void dump_extra_block_header_data(OStreamLike& output) override;
};

bool zlib_header_check(const std::span<unsigned char> checkbuf_span);

class ZlibFormatHandler : public DeflateFormatHandler {
	DeflateHistogramFalsePositiveDetector falsePositiveDetector{};
public:
	explicit ZlibFormatHandler(std::vector<SupportedFormats> _header_bytes, std::optional<unsigned int> _depth_limit = std::nullopt)
		: DeflateFormatHandler(_header_bytes, _depth_limit) {}

	bool inc_last_hdr_byte() override { return true; }

	bool quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) override;

	std::unique_ptr<PrecompFormatPrecompressor> make_precompressor(Precomp& precomp_mgr, const std::span<unsigned char>& buffer) override;

	void write_pre_recursion_data(RecursionContext& context, PrecompFormatHeaderData& precomp_hdr_data) override;

	static ZlibFormatHandler* create() {
		return new ZlibFormatHandler({ D_RAW });
	}
};

#endif //PRECOMP_ZLIB_HANDLER_H