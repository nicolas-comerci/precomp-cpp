#ifndef PRECOMP_BZip2_HANDLER_H
#define PRECOMP_BZip2_HANDLER_H
#include "precomp_dll.h"

#include <span>

class BZip2FormatHandler : public PrecompFormatHandler2 {
	std::array<unsigned char, CHUNK> tmp_out{};
public:
	explicit BZip2FormatHandler(std::vector<SupportedFormats> _header_bytes, std::optional<unsigned int> _depth_limit = std::nullopt)
		: PrecompFormatHandler2(_header_bytes, _depth_limit, true) {}

	bool quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) override;

	std::unique_ptr<PrecompFormatPrecompressor> make_precompressor(Tools& precomp_tools, Switches& precomp_switches, const std::span<unsigned char>& buffer) override;

	std::unique_ptr<PrecompFormatHeaderData> read_format_header(IStreamLike& input, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) override;

	std::unique_ptr<PrecompFormatRecompressor> make_recompressor(PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format, const Tools& tools) override;

	static BZip2FormatHandler* create() {
		return new BZip2FormatHandler({ D_BZIP2 });
	}
};

#endif //PRECOMP_BZip2_HANDLER_H