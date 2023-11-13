#ifndef PRECOMP_PDF_HANDLER_H
#define PRECOMP_PDF_HANDLER_H
#include "precomp_dll.h"

#include <span>

class PdfFormatHandler : public PrecompFormatHandler {
public:
	explicit PdfFormatHandler(std::vector<SupportedFormats> _header_bytes, Tools* _precomp_tools, std::optional<unsigned int> _depth_limit = std::nullopt)
		: PrecompFormatHandler(_header_bytes, _precomp_tools, _depth_limit) {}

	bool quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) override;

	std::unique_ptr<precompression_result> attempt_precompression(Precomp& precomp_instance, std::span<unsigned char> buffer, long long input_stream_pos) override;

	std::unique_ptr<PrecompFormatHeaderData> read_format_header(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) override;

	void recompress(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format) override;

	static PdfFormatHandler* create(Tools* tools) {
		return new PdfFormatHandler({ D_PDF }, tools);
	}
};

#endif //PRECOMP_PDF_HANDLER_H