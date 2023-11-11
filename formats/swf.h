#ifndef PRECOMP_SWF_HANDLER_H
#define PRECOMP_SWF_HANDLER_H
#include "precomp_dll.h"

#include "zlib.h"

class SwfFormatHandler : public ZlibFormatHandler {
public:
	explicit SwfFormatHandler(std::vector<SupportedFormats> _header_bytes, std::optional<unsigned int> _depth_limit = std::nullopt)
		: ZlibFormatHandler(_header_bytes, _depth_limit) {}

	bool quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) override;

	std::unique_ptr<PrecompFormatPrecompressor> make_precompressor(Precomp& precomp_mgr, const std::span<unsigned char>& buffer) override;

	void write_pre_recursion_data(RecursionContext& context, PrecompFormatHeaderData& precomp_hdr_data) override;

	static SwfFormatHandler* create() {
		return new SwfFormatHandler({ D_SWF });
	}
};

#endif //PRECOMP_SWF_HANDLER_H