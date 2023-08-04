#ifndef PRECOMP_MP3_HANDLER_H
#define PRECOMP_MP3_HANDLER_H
#include "precomp_dll.h"

#include <span>

const char* packmp3_version_info();

class Mp3FormatHandler : public PrecompFormatHandler {
public:
	bool quick_check(std::span<unsigned char> buffer) override;

	std::unique_ptr<precompression_result> attempt_precompression(Precomp& precomp_instance, std::span<unsigned char> buffer, long long input_stream_pos) override;

	void recompress(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) override;

	constexpr std::vector<SupportedFormats> get_header_bytes() override { return { D_MP3 }; }

	static Mp3FormatHandler* create() {
		return new Mp3FormatHandler();
	}
};

#endif // PRECOMP_MP3_HANDLER_H
