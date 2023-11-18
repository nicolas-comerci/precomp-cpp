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
    unsigned int compressed_size = (*(checkbuf + 21) << 24) + (*(checkbuf + 20) << 16) + (*(checkbuf + 19) << 8) + *(checkbuf + 18);
    unsigned int uncompressed_size = (*(checkbuf + 25) << 24) + (*(checkbuf + 24) << 16) + (*(checkbuf + 23) << 8) + *(checkbuf + 22);
    unsigned int filename_length = (*(checkbuf + 27) << 8) + *(checkbuf + 26);
    unsigned int extra_field_length = (*(checkbuf + 29) << 8) + *(checkbuf + 28);
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

class zip_precompression_result : public deflate_precompression_result {
public:
  explicit zip_precompression_result(Tools* tools) : deflate_precompression_result(D_ZIP, tools) {}

  void increase_detected_count() override { tools->increase_detected_count("ZIP"); }
  void increase_precompressed_count() override { tools->increase_precompressed_count("ZIP"); }
};

std::unique_ptr<precompression_result>
ZipFormatHandler::attempt_precompression(IStreamLike &input, OStreamLike &output, std::span<unsigned char> checkbuf_span, long long input_stream_pos, const Switches &precomp_switches, unsigned int recursion_depth) {
  std::unique_ptr<deflate_precompression_result> result = std::make_unique<zip_precompression_result>(precomp_tools);
  unsigned char* checkbuf = checkbuf_span.data();
  const unsigned int filename_length = (*(checkbuf + 27) << 8) + *(checkbuf + 26);
  const unsigned int extra_field_length = (*(checkbuf + 29) << 8) + *(checkbuf + 28);
  const auto header_length = 30 + filename_length + extra_field_length;

  const auto deflate_stream_pos = input_stream_pos + header_length;  // skip ZIP header, get in position for deflate stream

  try_decompression_deflate_type(result, *precomp_tools, input, output,
    D_ZIP, checkbuf + 4, header_length - 4, deflate_stream_pos, false, "in ZIP", precomp_tools->get_tempfile_name("decomp_zip", true), recursion_depth);

  result->original_size_extra += header_length;  // the deflate result only count the original deflate stream size, need to add the ZIP header size for full ZIP stream size
  return result;
}

std::unique_ptr<PrecompFormatHeaderData> ZipFormatHandler::read_format_header(IStreamLike &input, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) {
  auto fmt_hdr = std::make_unique<DeflateFormatHeaderData>();
  fmt_hdr->read_data(input, precomp_hdr_flags, false);
  return fmt_hdr;
}

void ZipFormatHandler::write_pre_recursion_data(OStreamLike &output, PrecompFormatHeaderData& precomp_hdr_data) {
  auto precomp_deflate_hdr_data = static_cast<DeflateFormatHeaderData&>(precomp_hdr_data);
  // ZIP header
  output.put('P');
  output.put('K');
  output.put(3);
  output.put(4);
  // Write zlib_header
  output.write(reinterpret_cast<char*>(precomp_deflate_hdr_data.stream_hdr.data()), precomp_deflate_hdr_data.stream_hdr.size());
}

void ZipFormatHandler::recompress(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format) {
  recompress_deflate(precompressed_input, recompressed_stream, static_cast<DeflateFormatHeaderData&>(precomp_hdr_data), precomp_tools->get_tempfile_name("recomp_zip", true), "ZIP", *precomp_tools);
}
