#include "gzip.h"
#include "formats/deflate.h"

bool GZipFormatHandler::quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) {
  auto checkbuf = buffer.data();
  if ((*checkbuf == 31) && (*(checkbuf + 1) == 139)) {
    // check zLib header in GZip header
    int compression_method = (*(checkbuf + 2) & 15);
    // reserved FLG bits must be zero
    if ((compression_method == 8) && ((*(checkbuf + 3) & 224) == 0)) return true;
  }
  return false;
}

class gzip_precompression_result: public deflate_precompression_result {
public:
  explicit gzip_precompression_result(Tools* tools): deflate_precompression_result(D_GZIP, tools) {}

  void increase_detected_count() override { tools->increase_detected_count("GZip"); }
  void increase_precompressed_count() override { tools->increase_precompressed_count("GZip"); }
};

std::unique_ptr<precompression_result> GZipFormatHandler::attempt_precompression(Precomp& precomp_mgr, std::span<unsigned char> checkbuf_span, long long input_stream_pos) {
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

  int header_length = 10;

  // TODO: Why not move this code to the quick_check?
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
  if (dont_compress) return std::unique_ptr<precompression_result>{};

  std::unique_ptr<deflate_precompression_result> result = std::make_unique<gzip_precompression_result>(&precomp_mgr.format_handler_tools);
  try_decompression_deflate_type(result, precomp_mgr,
    D_GZIP, checkbuf + 2, header_length - 2, input_stream_pos + header_length, false,
    "in GZIP", precomp_mgr.get_tempfile_name("precomp_gzip"));

  result->original_size_extra += header_length;  // Add the Gzip header length to the deflate stream size for the proper original Gzip stream size
  return result;
}

std::unique_ptr<PrecompFormatHeaderData> GZipFormatHandler::read_format_header(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) {
  auto fmt_hdr = std::make_unique<DeflateFormatHeaderData>();
  fmt_hdr->read_data(*context.fin, precomp_hdr_flags, false);
  return fmt_hdr;
}

void GZipFormatHandler::write_pre_recursion_data(RecursionContext& context, PrecompFormatHeaderData& precomp_hdr_data) {
  auto precomp_deflate_hdr_data = static_cast<DeflateFormatHeaderData&>(precomp_hdr_data);
  // GZIP header
  context.fout->put(31);
  context.fout->put(139);
  // Write zlib_header
  context.fout->write(reinterpret_cast<char*>(precomp_deflate_hdr_data.stream_hdr.data()), precomp_deflate_hdr_data.stream_hdr.size());
}

void GZipFormatHandler::recompress(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format) {
  recompress_deflate(precompressed_input, recompressed_stream, static_cast<DeflateFormatHeaderData&>(precomp_hdr_data), precomp_tools->get_tempfile_name("recomp_gzip", true), "GZIP", *precomp_tools);
}
