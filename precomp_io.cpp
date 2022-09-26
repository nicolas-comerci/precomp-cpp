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

void OfStreamWrapper::lzma_progress_update() {
  //float percent = ((g_precomp.ctx->input_file_pos + g_precomp.ctx->uncompressed_bytes_written) / ((float)g_precomp.ctx->fin_length + g_precomp.ctx->uncompressed_bytes_total)) * (g_precomp.ctx->global_max_percent - g_precomp.ctx->global_min_percent) + g_precomp.ctx->global_min_percent;
  float percent = -1;

  uint64_t progress_in = 0, progress_out = 0;

  lzma_get_progress(this->otf_xz_stream_c.get(), &progress_in, &progress_out);

  int lzma_mib_total = this->otf_xz_stream_c->total_in / (1024 * 1024);
  int lzma_mib_written = progress_in / (1024 * 1024);
  show_progress(percent, true, true, lzma_mib_total, lzma_mib_written);
}

void OfStreamWrapper::own_fwrite(const void* ptr, size_t size, size_t count, bool final_byte, bool update_lzma_progress) {
  switch (this->compression_otf_method) {
  case OTF_NONE: {
    this->stream->write(static_cast<const char*>(ptr), size * count);
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
      this->write(reinterpret_cast<char*>(otf_out.get()), have);
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
      this->write(reinterpret_cast<char*>(otf_out.get()), have);
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
      if ((!DEBUG_MODE) && (update_lzma_progress)) lzma_progress_update();
    } while ((otf_xz_stream_c->avail_in > 0) || (final_byte && (ret != LZMA_STREAM_END)));
    break;
  }
  }
}

size_t Precomp_IfStream::own_fread(void* ptr, size_t size, size_t count) {
  switch (this->compression_otf_method) {
  case OTF_NONE: {
    this->std::ifstream::read(static_cast<char*>(ptr), size * count);
    return this->std::ifstream::gcount();
  }
  case OTF_BZIP2: {
    init_otf_in_if_needed();
    int ret;
    int bytes_read = 0;

    print_work_sign(true);

    this->otf_bz2_stream_d->avail_out = size * count;
    this->otf_bz2_stream_d->next_out = (char*)ptr;

    do {

      if (this->otf_bz2_stream_d->avail_in == 0) {
        this->std::ifstream::read(reinterpret_cast<char*>(otf_in.get()), CHUNK);
        this->otf_bz2_stream_d->avail_in = this->std::ifstream::gcount();
        this->otf_bz2_stream_d->next_in = (char*)otf_in.get();
        if (this->otf_bz2_stream_d->avail_in == 0) break;
      }

      ret = BZ2_bzDecompress(otf_bz2_stream_d.get());
      if ((ret != BZ_OK) && (ret != BZ_STREAM_END)) {
        (void)BZ2_bzDecompressEnd(otf_bz2_stream_d.get());
        print_to_console("ERROR: bZip2 stream corrupted - return value %i\n", ret);
        exit(1);
      }

      if (ret == BZ_STREAM_END) this->decompress_otf_end = true;

    } while (this->otf_bz2_stream_d->avail_out > 0);

    bytes_read = (size * count - this->otf_bz2_stream_d->avail_out);

    return bytes_read;
  }
  case OTF_XZ_MT: {
    init_otf_in_if_needed();
    lzma_action action = LZMA_RUN;
    lzma_ret ret;

    otf_xz_stream_d->avail_out = size * count;
    otf_xz_stream_d->next_out = (uint8_t*)ptr;

    do {
      print_work_sign(true);
      if ((otf_xz_stream_d->avail_in == 0) && !this->eof()) {
        otf_xz_stream_d->next_in = (uint8_t*)otf_in.get();
        this->std::ifstream::read(reinterpret_cast<char*>(otf_in.get()), CHUNK);
        otf_xz_stream_d->avail_in = this->std::ifstream::gcount();

        if (this->bad()) {
          print_to_console("ERROR: Could not read input file\n");
          exit(1);
        }
      }

      ret = lzma_code(otf_xz_stream_d.get(), action);

      if (ret == LZMA_STREAM_END) {
        this->decompress_otf_end = true;
        break;
      }

      if (ret != LZMA_OK) {
        const char* msg;
        switch (ret) {
        case LZMA_MEM_ERROR:
          msg = "Memory allocation failed";
          break;
        case LZMA_FORMAT_ERROR:
          msg = "Wrong file format";
          break;
        case LZMA_OPTIONS_ERROR:
          msg = "Unsupported compression options";
          break;
        case LZMA_DATA_ERROR:
        case LZMA_BUF_ERROR:
          msg = "Compressed file is corrupt";
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
      }
    } while (otf_xz_stream_d->avail_out > 0);

    return size * count - otf_xz_stream_d->avail_out;
  }
  }

  return 0;
}
