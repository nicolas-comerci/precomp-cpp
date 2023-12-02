#include "gzip.h"
#include "formats/deflate.h"

std::optional<uint64_t> calculate_gzip_header_length(const std::span<unsigned char>& checkbuf_span) {
  auto checkbuf = checkbuf_span.data();
  //((*(checkbuf + 8) == 2) || (*(checkbuf + 8) == 4)) { //XFL = 2 or 4
          //  TODO: Can be 0 also, check if other values are used, too.
          //
          //  TODO: compressed data is followed by CRC-32 and uncompressed
          //    size. Uncompressed size can be used to check if it is really
          //    a GZ stream.

  bool fhcrc = (*(checkbuf + 3) & 2) == 2;
  bool fextra = (*(checkbuf + 3) & 4) == 4;
  bool fname = (*(checkbuf + 3) & 8) == 8;
  bool fcomment = (*(checkbuf + 3) & 16) == 16;

  uint64_t header_length = 10;

  bool dont_compress = false;
  if (fhcrc || fextra || fname || fcomment) {
    int act_checkbuf_pos = 10;

    if (fextra) {
      int xlen = *(checkbuf + act_checkbuf_pos) + (*(checkbuf + act_checkbuf_pos + 1) << 8);
      if ((act_checkbuf_pos + xlen) > CHECKBUF_SIZE) {
        dont_compress = true;
      }
      else {
        act_checkbuf_pos += 2;
        header_length += 2;
        act_checkbuf_pos += xlen;
        header_length += xlen;
      }
    }
    if ((fname) && (!dont_compress)) {
      do {
        act_checkbuf_pos++;
        dont_compress = (act_checkbuf_pos == CHECKBUF_SIZE);
        header_length++;
      } while ((*(checkbuf + act_checkbuf_pos - 1) != 0) && (!dont_compress));
    }
    if ((fcomment) && (!dont_compress)) {
      do {
        act_checkbuf_pos++;
        dont_compress = (act_checkbuf_pos == CHECKBUF_SIZE);
        header_length++;
      } while ((*(checkbuf + act_checkbuf_pos - 1) != 0) && (!dont_compress));
    }
    if ((fhcrc) && (!dont_compress)) {
      act_checkbuf_pos += 2;
      dont_compress = (act_checkbuf_pos > CHECKBUF_SIZE);
      header_length += 2;
    }
  }
  return dont_compress ? std::nullopt : std::optional{ header_length };
}

bool GZipFormatHandler::quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) {
  auto checkbuf = buffer.data();
  if ((*checkbuf != 31) || (*(checkbuf + 1) != 139)) return false;

  // check zLib header in GZip header
  const int compression_method = (*(checkbuf + 2) & 15);
  // reserved FLG bits must be zero
  if ((compression_method != 8) || ((*(checkbuf + 3) & 224) != 0)) return false;
  if (!calculate_gzip_header_length(buffer).has_value()) return false;

  return true;
}

class GZipPrecompressor : public DeflateWithHeaderPrecompressor {
public:
  GZipPrecompressor(std::vector<unsigned char>&& _pre_deflate_header, Tools* _precomp_tools, const Switches& precomp_switches) :
    DeflateWithHeaderPrecompressor(std::vector<unsigned char>{ 31, 139 }, std::move(_pre_deflate_header), _precomp_tools, precomp_switches) {}

  PrecompProcessorReturnCode process(bool input_eof) override {
    const auto retval = DeflateWithHeaderPrecompressor::process(input_eof);
    original_stream_size = deflate_precompressor->original_stream_size + hdr_bytes_skipped + hdr_magic_bytes_skipped;
    return retval;
  }

  void increase_detected_count() override { precomp_tools->increase_detected_count("GZip"); }
  void increase_precompressed_count() override { precomp_tools->increase_precompressed_count("GZip"); }
  void increase_partially_precompressed_count() override { precomp_tools->increase_partially_precompressed_count("GZip"); }
};

std::unique_ptr<PrecompFormatPrecompressor> GZipFormatHandler::make_precompressor(Tools& precomp_tools, Switches& precomp_switches, const std::span<unsigned char>& buffer) {
  auto checkbuf = buffer.data();
  uint64_t header_length = calculate_gzip_header_length(buffer).value();

  return std::make_unique<GZipPrecompressor>(
    std::vector(buffer.data() + 2, buffer.data() + header_length),
    &precomp_tools, precomp_switches);
}

void GZipFormatHandler::write_pre_recursion_data(OStreamLike& output, PrecompFormatHeaderData& precomp_hdr_data) {
  // GZIP header
  output.put(31);
  output.put(139);
  ZlibFormatHandler::write_pre_recursion_data(output, precomp_hdr_data);
}
