#include "zip.h"
#include "formats/deflate.h"

bool ZipFormatHandler::quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) {
  auto checkbuf = buffer.data();
  if (
    (*checkbuf == 'P') && (*(checkbuf + 1) == 'K') &&
    (*(checkbuf + 2) == 3) && (*(checkbuf + 3) == 4)
    ) {
    print_to_log(PRECOMP_DEBUG_LOG, "ZIP header detected\n");
    //print_to_log(PRECOMP_DEBUG_LOG, "ZIP header detected at position %lli\n", input_stream_pos);
    const unsigned int compressed_size = (*(checkbuf + 21) << 24) + (*(checkbuf + 20) << 16) + (*(checkbuf + 19) << 8) + *(checkbuf + 18);
    const unsigned int uncompressed_size = (*(checkbuf + 25) << 24) + (*(checkbuf + 24) << 16) + (*(checkbuf + 23) << 8) + *(checkbuf + 22);
    const unsigned int filename_length = (*(checkbuf + 27) << 8) + *(checkbuf + 26);
    const unsigned int extra_field_length = (*(checkbuf + 29) << 8) + *(checkbuf + 28);
    print_to_log(PRECOMP_DEBUG_LOG, "compressed size: %i\n", compressed_size);
    print_to_log(PRECOMP_DEBUG_LOG, "uncompressed size: %i\n", uncompressed_size);
    print_to_log(PRECOMP_DEBUG_LOG, "file name length: %i\n", filename_length);
    print_to_log(PRECOMP_DEBUG_LOG, "extra field length: %i\n", extra_field_length);

    if ((filename_length + extra_field_length) <= CHECKBUF_SIZE && *(checkbuf + 8) == 8 && *(checkbuf + 9) == 0) { // Compression method 8: Deflate
      return true;
    }
  }
  return false;
}

class ZipPrecompressor : public DeflateWithHeaderPrecompressor {
public:
  ZipPrecompressor(std::vector<unsigned char>&& _pre_deflate_header, Tools* _precomp_tools, const Switches& precomp_switches) :
    DeflateWithHeaderPrecompressor(std::vector<unsigned char>{ 'P', 'K', 3, 4 }, std::move(_pre_deflate_header), _precomp_tools, precomp_switches) {}

  PrecompProcessorReturnCode process(bool input_eof) override {
    const auto retval = DeflateWithHeaderPrecompressor::process(input_eof);
    original_stream_size = deflate_precompressor->original_stream_size + hdr_bytes_skipped + hdr_magic_bytes_skipped;
    return retval;
  }

  void increase_detected_count() override { precomp_tools->increase_detected_count("ZIP"); }
  void increase_precompressed_count() override { precomp_tools->increase_precompressed_count("ZIP"); }
  void increase_partially_precompressed_count() override { precomp_tools->increase_partially_precompressed_count("ZIP"); }
};

std::unique_ptr<PrecompFormatPrecompressor> ZipFormatHandler::make_precompressor(Tools& precomp_tools, Switches& precomp_switches, const std::span<unsigned char>& buffer) {
  unsigned char* checkbuf = buffer.data();
  const unsigned int filename_length = (*(checkbuf + 27) << 8) + *(checkbuf + 26);
  const unsigned int extra_field_length = (*(checkbuf + 29) << 8) + *(checkbuf + 28);
  const auto header_length = 30 + filename_length + extra_field_length;

  return std::make_unique<ZipPrecompressor>(
    std::vector(buffer.data() + 4, buffer.data() + header_length),
    &precomp_tools, precomp_switches);
}

void ZipFormatHandler::write_pre_recursion_data(OStreamLike& output, PrecompFormatHeaderData& precomp_hdr_data) {
  // ZIP header
  output.put('P');
  output.put('K');
  output.put(3);
  output.put(4);
  ZlibFormatHandler::write_pre_recursion_data(output, precomp_hdr_data);
}
