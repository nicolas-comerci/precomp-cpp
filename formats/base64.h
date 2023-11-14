#ifndef PRECOMP_B64_HANDLER_H
#define PRECOMP_B64_HANDLER_H
#include "precomp_dll.h"

#include <span>

class Base64FormatHandler : public PrecompFormatHandler {
public:
	explicit Base64FormatHandler(std::vector<SupportedFormats> _header_bytes, Tools* _precomp_tools, std::optional<unsigned int> _depth_limit = std::nullopt)
		: PrecompFormatHandler(_header_bytes, _precomp_tools, _depth_limit, true) {}

	bool quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) override;

	std::unique_ptr<precompression_result>
    attempt_precompression(IStreamLike &input, OStreamLike &output, std::span<unsigned char> buffer,
                           long long input_stream_pos, const Switches &precomp_switches) override;

	std::unique_ptr<PrecompFormatHeaderData> read_format_header(IStreamLike &input, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) override;

	void recompress(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format) override;
	void write_pre_recursion_data(OStreamLike &output, PrecompFormatHeaderData& precomp_hdr_data) override;

	static Base64FormatHandler* create(Tools* tools) {
		return new Base64FormatHandler({ D_BASE64 }, tools);
	}
};

#endif //PRECOMP_B64_HANDLER_H