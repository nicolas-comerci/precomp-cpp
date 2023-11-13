#include "swf.h"

#include "formats/zlib.h"

bool SwfFormatHandler::quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) {
  auto checkbuf = buffer.data();
  // CWS = Compressed SWF file
  auto cws_hdr = (*checkbuf == 'C') && (*(checkbuf + 1) == 'W') && (*(checkbuf + 2) == 'S');
  // First 8 Bytes are SWF header stuff, bytes 9-10 are Zlib header
  return cws_hdr && zlib_header_check(std::span(checkbuf + 8, buffer.size() - 8));
}

class SwfPrecompressor: public DeflateWithHeaderPrecompressor {
  std::array<unsigned char, 3> cws{ 'C', 'W', 'S' };
  unsigned int cws_bytes_skipped = 0;
public:
  SwfPrecompressor(std::vector<unsigned char>&& _pre_deflate_header, const std::function<void()>& _progress_callback, Tools* _precomp_tools) :
    DeflateWithHeaderPrecompressor(std::move(_pre_deflate_header), _progress_callback, _precomp_tools) {}

  PrecompProcessorReturnCode process(bool input_eof) {
    while (cws_bytes_skipped < cws.size() && avail_in > 0) {
      // TODO: maybe check that the bytes are the same?
      avail_in -= 1;
      next_in += 1;
      cws_bytes_skipped++;
    }
    const auto retval = DeflateWithHeaderPrecompressor::process(input_eof);
    original_stream_size = deflate_precompressor->original_stream_size + hdr_bytes_skipped + cws_bytes_skipped;
    return retval;
  }

  void increase_detected_count() override { precomp_tools->increase_detected_count("SWF"); }
  void increase_precompressed_count() override { precomp_tools->increase_precompressed_count("SWF"); }
};

std::unique_ptr<PrecompFormatPrecompressor> SwfFormatHandler::make_precompressor(Precomp& precomp_mgr, const std::span<unsigned char>& buffer) {
  return std::make_unique<SwfPrecompressor>(
    std::vector(buffer.data() + 3, buffer.data() + 10),
    [&precomp_mgr]() { precomp_mgr.call_progress_callback(); },
    &precomp_mgr.format_handler_tools);
}

void SwfFormatHandler::write_pre_recursion_data(RecursionContext& context, PrecompFormatHeaderData& precomp_hdr_data) {
  context.fout->put('C');
  context.fout->put('W');
  context.fout->put('S');
  ZlibFormatHandler::write_pre_recursion_data(context, precomp_hdr_data);
  
}
