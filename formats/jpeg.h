#ifndef PRECOMP_JPEG_HANDLER_H
#define PRECOMP_JPEG_HANDLER_H
#include "precomp_dll.h"

#include <span>

const char* packjpg_version_info();

class JpegFormatHandler : public PrecompFormatHandler {
public:
	bool quick_check(std::span<unsigned char> buffer) override;

	std::unique_ptr<precompression_result> attempt_precompression(Precomp& precomp_instance, std::span<unsigned char> buffer, long long input_stream_pos) override;

	void recompress(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) override;

	constexpr std::vector<SupportedFormats> get_header_bytes() override { return { D_JPG }; }

	static JpegFormatHandler* create() {
		return new JpegFormatHandler();
	}
};

#endif //PRECOMP_JPEG_HANDLER_H