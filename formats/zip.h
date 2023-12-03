#ifndef PRECOMP_ZIP_HANDLER_H
#define PRECOMP_ZIP_HANDLER_H
#include "precomp_dll.h"

#include <span>

#include "zlib.h"

class ZipFormatHandler : public ZlibFormatHandler {
public:
	explicit ZipFormatHandler(std::vector<SupportedFormats> _header_bytes, std::optional<unsigned int> _depth_limit = std::nullopt)
		: ZlibFormatHandler(_header_bytes, _depth_limit) {}

	bool quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) override;

	std::unique_ptr<PrecompFormatPrecompressor> make_precompressor(Tools& precomp_tools, Switches& precomp_switches, const std::span<unsigned char>& buffer) override;

	void write_pre_recursion_data(OStreamLike& output, PrecompFormatHeaderData& precomp_hdr_data) override;

	static ZipFormatHandler* create() {
		return new ZipFormatHandler({ D_ZIP });
	}
};

#endif //PRECOMP_ZIP_HANDLER_H