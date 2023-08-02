#ifndef PRECOMP_ZIP_HANDLER_H
#define PRECOMP_ZIP_HANDLER_H
#include "precomp_dll.h"

#include <span>

class ZipFormatHandler : public PrecompFormatHandler {
public:
	bool quick_check(std::span<unsigned char> buffer) override;

	std::unique_ptr<precompression_result> attempt_precompression(Precomp& precomp_instance, std::span<unsigned char> buffer, long long input_stream_pos) override;

	void recompress(RecursionContext& context, std::byte precomp_hdr_flags) override;

	static ZipFormatHandler* create() {
		return new ZipFormatHandler();
	}
};

#endif //PRECOMP_ZIP_HANDLER_H