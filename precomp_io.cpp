#include "precomp_io.h"

// Return maximal memory to use per default for LZMA in MiB
// Use only 1 GiB in the 32-bit windows variant
// because of the 2 or 3 GiB limit on these systems
int lzma_max_memory_default() {
  int max_memory = 2048;
#ifndef __unix
#ifndef BIT64
  max_memory = 1024;
#endif
#endif
  return max_memory;
}

void Precomp_OfStream::lzma_progress_update() {
  if (compression_otf_method != OTF_XZ_MT) return;
  //float percent = ((g_precomp.ctx->input_file_pos + g_precomp.ctx->uncompressed_bytes_written) / ((float)g_precomp.ctx->fin_length + g_precomp.ctx->uncompressed_bytes_total)) * (g_precomp.ctx->global_max_percent - g_precomp.ctx->global_min_percent) + g_precomp.ctx->global_min_percent;
  float percent = -1;

  uint64_t progress_in = 0, progress_out = 0;

  lzma_get_progress(this->otf_xz_stream_c.get(), &progress_in, &progress_out);

  int lzma_mib_total = this->otf_xz_stream_c->total_in / (1024 * 1024);
  int lzma_mib_written = progress_in / (1024 * 1024);
  show_progress(percent, true, true, lzma_mib_total, lzma_mib_written);
}

Precomp_OfStream& Precomp_OfStream::own_fwrite(const void* ptr, std::streamsize size, std::streamsize count, bool final_byte) {
  switch (this->compression_otf_method) {
  case OTF_NONE: {
    this->std::ofstream::write(static_cast<const char*>(ptr), size * count);
    if (this->bad()) {
      error(ERR_DISK_FULL);
    }
    break;
  }
  case OTF_BZIP2: { // bZip2
    init_otf_in_if_needed();
    int flush, ret;
    unsigned have;

    print_work_sign(true);

    flush = final_byte ? BZ_FINISH : BZ_RUN;

    otf_bz2_stream_c->avail_in = size * count;
    otf_bz2_stream_c->next_in = (char*)ptr;
    do {
      otf_bz2_stream_c->avail_out = CHUNK;
      otf_bz2_stream_c->next_out = (char*)otf_out.get();
      ret = BZ2_bzCompress(otf_bz2_stream_c.get(), flush);
      have = CHUNK - otf_bz2_stream_c->avail_out;
      this->std::ofstream::write(reinterpret_cast<char*>(otf_out.get()), have);
      if (this->bad()) {
        error(ERR_DISK_FULL);
      }
    } while (otf_bz2_stream_c->avail_out == 0);
    if (ret < 0) {
      print_to_console("ERROR: bZip2 compression failed - return value %i\n", ret);
      exit(1);
    }
    break;
  }
  case OTF_XZ_MT: {
    init_otf_in_if_needed();
    lzma_action action = final_byte ? LZMA_FINISH : LZMA_RUN;
    lzma_ret ret;
    unsigned have;

    otf_xz_stream_c->avail_in = size * count;
    otf_xz_stream_c->next_in = (uint8_t*)ptr;
    do {
      print_work_sign(true);
      otf_xz_stream_c->avail_out = CHUNK;
      otf_xz_stream_c->next_out = (uint8_t*)otf_out.get();
      ret = lzma_code(otf_xz_stream_c.get(), action);
      have = CHUNK - otf_xz_stream_c->avail_out;
      this->std::ofstream::write(reinterpret_cast<char*>(otf_out.get()), have);
      if (this->bad()) {
        error(ERR_DISK_FULL);
      }
      if (ret != LZMA_OK && ret != LZMA_STREAM_END) {
        const char* msg;
        switch (ret) {
        case LZMA_MEM_ERROR:
          msg = "Memory allocation failed";
          break;

        case LZMA_DATA_ERROR:
          msg = "File size limits exceeded";
          break;

        default:
          msg = "Unknown error, possibly a bug";
          break;
        }

        print_to_console("ERROR: liblzma error: %s (error code %u)\n", msg, ret);
#ifdef COMFORT
        wait_for_key();
#endif // COMFORT
        exit(1);
      } // .avail_out == 0
      if (!DEBUG_MODE) lzma_progress_update();
    } while ((otf_xz_stream_c->avail_in > 0) || (final_byte && (ret != LZMA_STREAM_END)));
    break;
  }
  }
  return *this;
}

std::unique_ptr<std::istream> wrap_istream_otf_compression(std::unique_ptr<std::istream>&& istream, int otf_compression_method) {
  switch (otf_compression_method) {
  case OTF_NONE: {
    return std::move(istream);
  }
  case OTF_BZIP2: {
    return Bz2StreamBuffer::from_istream(std::move(istream));
  }
  case OTF_XZ_MT: {
    return XzStreamBuffer::from_istream(std::move(istream));
  }
  }
  print_to_console("Unknown compression method!");
  exit(1);
}
