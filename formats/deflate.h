#ifndef PRECOMP_DEFLATE_HANDLER_H
#define PRECOMP_DEFLATE_HANDLER_H
#include "precomp_dll.h"

#include <span>

struct recompress_deflate_result {
  long long compressed_stream_size;
  long long uncompressed_stream_size;
  std::vector<unsigned char> recon_data;
  bool accepted;
  std::vector<unsigned char> uncompressed_stream_mem;
  bool zlib_perfect;
  char zlib_comp_level;
  char zlib_mem_level;
  char zlib_window_bits;
};

class deflate_precompression_result : public precompression_result {
protected:
  void dump_recon_data_to_outfile(Precomp& precomp_mgr);
public:
  recompress_deflate_result rdres;
  std::vector<unsigned char> zlib_header;
  bool inc_last_hdr_byte = false;
  bool recursion_used = false;
  long long recursion_filesize;

  explicit deflate_precompression_result(SupportedFormats format);

  void dump_header_to_outfile(Precomp& precomp_mgr) const override;
  void dump_precompressed_data_to_outfile(Precomp& precomp_mgr) override;
  void dump_to_outfile(Precomp& precomp_mgr) override;
};

void fin_fget_recon_data(IStreamLike& input, recompress_deflate_result&);

recompress_deflate_result try_recompression_deflate(Precomp& precomp_mgr, IStreamLike& file, long long file_deflate_stream_pos, PrecompTmpFile& tmpfile);

void debug_deflate_detected(RecursionContext& context, const recompress_deflate_result& rdres, const char* type, long long deflate_stream_pos);

void debug_sums(RecursionContext& context, const recompress_deflate_result& rdres);

void debug_pos(Precomp& precomp_mgr);

deflate_precompression_result try_decompression_deflate_type(Precomp& precomp_mgr, unsigned& dcounter, unsigned& rcounter, SupportedFormats type,
  const unsigned char* hdr, const unsigned int hdr_length, long long deflate_stream_pos, const bool inc_last, const char* debugname, std::string tmp_filename);

bool check_inflate_result(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span, unsigned char* out_buf, int windowbits, const long long deflate_stream_pos, bool use_brute_parameters = false);

bool check_raw_deflate_stream_start(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span, const long long original_input_pos);

deflate_precompression_result try_decompression_raw_deflate(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span, const long long original_input_pos);

bool try_reconstructing_deflate_skip(RecursionContext& context, IStreamLike& fin, OStreamLike& fout, const recompress_deflate_result& rdres, const size_t read_part, const size_t skip_part);

void fin_fget_deflate_hdr(IStreamLike& input, OStreamLike& output, recompress_deflate_result& rdres, const std::byte flags,
  unsigned char* hdr_data, unsigned& hdr_length,
  const bool inc_last_hdr_byte);

void fin_fget_deflate_rec(RecursionContext& context, recompress_deflate_result& rdres, const std::byte flags, unsigned char* hdr, unsigned& hdr_length, const bool inc_last);

void debug_deflate_reconstruct(const recompress_deflate_result& rdres, const char* type, const unsigned hdr_length, const uint64_t rec_length);

void recompress_deflate(RecursionContext& context, std::byte precomp_hdr_flags, bool incl_last_hdr_byte, std::string filename, std::string type);

void recompress_raw_deflate(RecursionContext& context, std::byte precomp_hdr_flags);

#endif //PRECOMP_DEFLATE_HANDLER_H