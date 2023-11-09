#ifndef PRECOMP_ZLIB_HANDLER_H
#define PRECOMP_ZLIB_HANDLER_H
#include "precomp_dll.h"
#include "deflate.h"

#include <span>

bool zlib_header_check(const std::span<unsigned char> checkbuf_span);

class ZlibFormatHandler2 : public DeflateFormatHandler {
	DeflateHistogramFalsePositiveDetector falsePositiveDetector{};
public:
	explicit ZlibFormatHandler2(std::vector<SupportedFormats> _header_bytes, std::optional<unsigned int> _depth_limit = std::nullopt)
		: DeflateFormatHandler(_header_bytes, _depth_limit) {}

	bool inc_last_hdr_byte() override { return true; }

	bool quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) override {
		return zlib_header_check(buffer);
	}

	std::unique_ptr<PrecompFormatPrecompressor> make_precompressor(Precomp& precomp_mgr, const std::span<unsigned char>& buffer) override;

	std::unique_ptr<PrecompFormatRecompressor> make_recompressor(PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format, const Tools& tools) override;

	static ZlibFormatHandler2* create() {
		return new ZlibFormatHandler2({ D_RAW });
	}
};

#endif //PRECOMP_ZLIB_HANDLER_H