#ifndef PRECOMP_ZLIB_HANDLER_H
#define PRECOMP_ZLIB_HANDLER_H
#include "precomp_dll.h"

#include <span>

bool zlib_header_check(const std::span<unsigned char> checkbuf_span);

class ZlibFormatHandler : public PrecompFormatHandler {
public:
	bool quick_check(std::span<unsigned char> buffer) override {
		return zlib_header_check(buffer);
	}

	std::unique_ptr<precompression_result> attempt_precompression(Precomp& precomp_instance, std::span<unsigned char> buffer, long long input_stream_pos) override;

	void recompress(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) override;

	constexpr std::vector<SupportedFormats> get_header_bytes() override { return { D_RAW }; }

	static ZlibFormatHandler* create() {
		return new ZlibFormatHandler();
	}
};

#endif //PRECOMP_ZLIB_HANDLER_H