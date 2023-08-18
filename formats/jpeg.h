#ifndef PRECOMP_JPEG_HANDLER_H
#define PRECOMP_JPEG_HANDLER_H
#include "precomp_dll.h"

#include <span>

const char* packjpg_version_info();

class JpegFormatHandler : public PrecompFormatHandler {
public:
	explicit JpegFormatHandler(std::vector<SupportedFormats> _header_bytes, std::optional<unsigned int> _depth_limit = std::nullopt)
		: PrecompFormatHandler(_header_bytes, _depth_limit) {}

	bool quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) override;

	std::unique_ptr<precompression_result> attempt_precompression(Precomp& precomp_instance, std::span<unsigned char> buffer, long long input_stream_pos) override;

	std::unique_ptr<PrecompFormatHeaderData> read_format_header(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) override;

	void recompress(RecursionContext& context, PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format) override;

	static JpegFormatHandler* create() {
		return new JpegFormatHandler({ D_JPG });
	}
};

#endif //PRECOMP_JPEG_HANDLER_H