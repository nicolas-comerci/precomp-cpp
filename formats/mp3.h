#ifndef PRECOMP_MP3_HANDLER_H
#define PRECOMP_MP3_HANDLER_H
#include "precomp_dll.h"

#include <span>

const char* packmp3_version_info();

class Mp3FormatHandler : public PrecompFormatHandler {
public:
	explicit Mp3FormatHandler(std::vector<SupportedFormats> _header_bytes, Tools* _precomp_tools, std::optional<unsigned int> _depth_limit = std::nullopt)
		: PrecompFormatHandler(_header_bytes, _precomp_tools, _depth_limit) {}

	bool quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) override;

	std::unique_ptr<precompression_result> attempt_precompression(Precomp& precomp_instance, std::span<unsigned char> buffer, long long input_stream_pos) override;

	std::unique_ptr<PrecompFormatHeaderData> read_format_header(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) override;

	void recompress(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format) override;

	static Mp3FormatHandler* create(Tools* tools) {
		return new Mp3FormatHandler({ D_MP3 }, tools);
	}
};

#endif // PRECOMP_MP3_HANDLER_H
