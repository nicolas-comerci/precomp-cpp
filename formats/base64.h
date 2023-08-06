#ifndef PRECOMP_B64_HANDLER_H
#define PRECOMP_B64_HANDLER_H
#include "precomp_dll.h"

#include <span>

unsigned long long compare_files(Precomp& precomp_mgr, IStreamLike& file1, IStreamLike& file2, unsigned int pos1, unsigned int pos2);

class Base64FormatHandler : public PrecompFormatHandler {
public:
	explicit Base64FormatHandler(std::vector<SupportedFormats> _header_bytes, std::optional<unsigned int> _depth_limit = std::nullopt)
		: PrecompFormatHandler(_header_bytes, _depth_limit) {}

	bool quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) override;

	std::unique_ptr<precompression_result> attempt_precompression(Precomp& precomp_instance, std::span<unsigned char> buffer, long long input_stream_pos) override;

	void recompress(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) override;

	static Base64FormatHandler* create() {
		return new Base64FormatHandler({ D_BASE64 });
	}
};

#endif //PRECOMP_B64_HANDLER_H