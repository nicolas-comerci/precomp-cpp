#include "swf.h"

#include "zlib.h"

bool swf_header_check(const std::span<unsigned char> checkbuf_span) {
  auto checkbuf = checkbuf_span.data();
  // CWS = Compressed SWF file
  auto cws_hdr = (*checkbuf == 'C') && (*(checkbuf + 1) == 'W') && (*(checkbuf + 2) == 'S');
  return cws_hdr && zlib_header_check(std::span(checkbuf + 8, checkbuf_span.size() - 8));
}

std::unique_ptr<deflate_precompression_result> try_decompression_swf(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span, long long original_input_pos) {
 std::unique_ptr<deflate_precompression_result> result = std::make_unique<deflate_precompression_result>(D_SWF);
  //int windowbits = (*(checkbuf_span.data() + 8) >> 4) + 8;

  long long deflate_stream_pos = original_input_pos + 10; // skip CWS and zLib header

  result = try_decompression_deflate_type(precomp_mgr, precomp_mgr.statistics.decompressed_swf_count, precomp_mgr.statistics.recompressed_swf_count,
    D_SWF, checkbuf_span.data() + 3, 7, deflate_stream_pos, true,
    "in SWF", precomp_mgr.get_tempfile_name("original_swf"));

  result->input_pos_extra_add += 10;
  return result;
}

void recompress_swf(RecursionContext& context, std::byte precomp_hdr_flags) {
  context.fout->put('C');
  context.fout->put('W');
  context.fout->put('S');

  recompress_deflate(context, precomp_hdr_flags, true, context.precomp.get_tempfile_name("recomp_swf"), "SWF");
}
