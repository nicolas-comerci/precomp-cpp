#include "zip.h"

bool zip_header_check(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span) {
  auto checkbuf = checkbuf_span.data();
  if (
    (*checkbuf == 'P') && (*(checkbuf + 1) == 'K') &&
    (*(checkbuf + 2) == 3) && (*(checkbuf + 3) == 4)
    ) {
    print_to_log(PRECOMP_DEBUG_LOG, "ZIP header detected\n");
    print_to_log(PRECOMP_DEBUG_LOG, "ZIP header detected at position %lli\n", precomp_mgr.ctx->input_file_pos);
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

deflate_precompression_result try_decompression_zip(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span) {
  unsigned char* checkbuf = checkbuf_span.data();
  unsigned int filename_length = (*(checkbuf + 27) << 8) + *(checkbuf + 26);
  unsigned int extra_field_length = (*(checkbuf + 29) << 8) + *(checkbuf + 28);
  auto header_length = 30 + filename_length + extra_field_length;

  precomp_mgr.ctx->input_file_pos += header_length;  // skip ZIP header, get in position for deflate stream

  auto result = try_decompression_deflate_type(precomp_mgr, precomp_mgr.statistics.decompressed_zip_count, precomp_mgr.statistics.recompressed_zip_count,
    D_ZIP, checkbuf + 4, header_length - 4, false,
    "in ZIP", temp_files_tag() + "_decomp_zip");

  precomp_mgr.ctx->input_file_pos -= header_length;  // restore input file pos
  result.input_pos_extra_add += header_length;  // the deflate result only count the original deflate stream size, need to add the ZIP header size for full ZIP stream size
  return result;
}

void recompress_zip(Precomp& precomp_mgr, std::byte precomp_hdr_flags) {
  precomp_mgr.ctx->fout->put('P');
  precomp_mgr.ctx->fout->put('K');
  precomp_mgr.ctx->fout->put(3);
  precomp_mgr.ctx->fout->put(4);
  recompress_deflate(precomp_mgr, precomp_hdr_flags, false, temp_files_tag() + "_recomp_zip", "ZIP");
}
