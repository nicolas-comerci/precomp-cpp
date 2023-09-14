#ifndef PRECOMP_DEFLATE_HANDLER_H
#define PRECOMP_DEFLATE_HANDLER_H
#include "precomp_dll.h"

#include <span>

struct recompress_deflate_result {
  long long compressed_stream_size = -1;
  long long uncompressed_stream_size = -1;
  std::vector<unsigned char> recon_data;
  bool accepted = false;
  std::vector<unsigned char> uncompressed_stream_mem;
  bool zlib_perfect = false;
  char zlib_comp_level = 0;
  char zlib_mem_level = 0;
  char zlib_window_bits = 0;
};

class deflate_precompression_result : public precompression_result {
protected:
  void dump_recon_data_to_outfile(OStreamLike& outfile) const;
public:
  recompress_deflate_result rdres;
  std::vector<unsigned char> zlib_header;
  bool inc_last_hdr_byte = false;

  explicit deflate_precompression_result(SupportedFormats format);

  void dump_header_to_outfile(OStreamLike& outfile) const override;
  void dump_to_outfile(OStreamLike& outfile) const override;
};

struct DeflateHistogramFalsePositiveDetector {
	unsigned char tmp_out[CHUNK];
	int histogram[256];
	// We use the address of a IStreamLike as ID to check if we are on the same input as the last time this data was updated, using uintptr_t to make it clear
	// that this thing should never be dereferenced here, it might not exist anymore, nor you should need to access it
	uintptr_t prev_input_id;
	unsigned char prev_first_byte;
	long long prev_deflate_stream_pos;
	int prev_maximum, prev_used;
	int prev_i;
};

void fin_fget_recon_data(IStreamLike& input, recompress_deflate_result&);

recompress_deflate_result try_recompression_deflate(Precomp& precomp_mgr, IStreamLike& file, long long file_deflate_stream_pos, OStreamLike& tmpfile);

void debug_deflate_detected(RecursionContext& context, const recompress_deflate_result& rdres, const char* type, long long deflate_stream_pos);

void debug_sums(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, const recompress_deflate_result& rdres);

std::unique_ptr<deflate_precompression_result> try_decompression_deflate_type(Precomp& precomp_mgr, unsigned& dcounter, unsigned& rcounter, SupportedFormats type,
  const unsigned char* hdr, const unsigned int hdr_length, long long deflate_stream_pos, const bool inc_last, const char* debugname, std::string tmp_filename);

bool check_inflate_result(DeflateHistogramFalsePositiveDetector& falsePositiveDetector, uintptr_t current_input_id, const std::span<unsigned char> checkbuf_span, int windowbits, const long long deflate_stream_pos, bool use_brute_parameters = false);

bool try_reconstructing_deflate_skip(IStreamLike& fin, OStreamLike& fout, const recompress_deflate_result& rdres, const size_t read_part, const size_t skip_part, const std::function<void()>& progress_callback);

void fin_fget_deflate_hdr(IStreamLike& input, recompress_deflate_result& rdres, const std::byte flags, unsigned char* hdr_data, unsigned& hdr_length, const bool inc_last_hdr_byte);

void fin_fget_deflate_rec(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, recompress_deflate_result& rdres, const std::byte flags, unsigned char* hdr, unsigned& hdr_length, const bool inc_last);

void debug_deflate_reconstruct(const recompress_deflate_result& rdres, const char* type, const unsigned hdr_length, const uint64_t rec_length);

class DeflateFormatHeaderData: public PrecompFormatHeaderData {
public:
  recompress_deflate_result rdres;  // not the cleanest for this to be here, but for now it simplifies the refactoring for simplified recursion/penalty_bytes support in Precomp
  std::vector<unsigned char> stream_hdr;
};

void recompress_deflate(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, DeflateFormatHeaderData& precomp_hdr_data, std::string filename, std::string type, const PrecompFormatHandler::Tools& tools);

std::unique_ptr<PrecompFormatHeaderData> read_deflate_format_header(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, std::byte precomp_hdr_flags, bool inc_last_hdr_byte);

class DeflateFormatHandler : public PrecompFormatHandler {
	DeflateHistogramFalsePositiveDetector falsePositiveDetector {};
public:
	explicit DeflateFormatHandler(std::vector<SupportedFormats> _header_bytes, std::optional<unsigned int> _depth_limit = std::nullopt)
		: PrecompFormatHandler(_header_bytes, _depth_limit, true) {}

	bool quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) override;

	std::unique_ptr<precompression_result> attempt_precompression(Precomp& precomp_instance, std::span<unsigned char> buffer, long long input_stream_pos) override;

  std::unique_ptr<PrecompFormatHeaderData> read_format_header(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) override {
    return read_deflate_format_header(*context.fin, *context.fout, precomp_hdr_flags, false);
  }

  void recompress(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format, const Tools& tools) override;
  void write_pre_recursion_data(RecursionContext& context, PrecompFormatHeaderData& precomp_hdr_data) override;

  static DeflateFormatHandler* create() {
    return new DeflateFormatHandler({ D_BRUTE });
  }
};

#endif //PRECOMP_DEFLATE_HANDLER_H