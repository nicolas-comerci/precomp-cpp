#include "precomp_io.h"

void force_seekg(std::istream& stream, long long offset, std::ios_base::seekdir origin) {
  if (stream.bad()) {
    print_to_console("Input stream went bad");
    exit(1);
  }
  stream.clear();
  stream.seekg(offset, origin);
}

int ostream_printf(std::ostream& out, std::string str) {
  for (char character : str) {
    out.put(character);
    if (out.bad()) return 0;
  }
  return str.length();
}

template <typename T>
class Precomp_OStream : public T
{
  static_assert(std::is_base_of_v<std::ostream, T>, "OStreamWrapper must get an std::ostream derivative as template parameter");
public:
  std::unique_ptr<CompressedOStreamBuffer> otf_compression_streambuf;
  void rdbuf(std::streambuf * streambuffer)
  {
    std::ostream& stream_ref = *this;
    stream_ref.rdbuf(streambuffer);
  }

  ~Precomp_OStream() {
    if (otf_compression_streambuf != nullptr) otf_compression_streambuf->set_stream_eof();
  }
};

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

void XzOStreamBuffer::lzma_progress_update() {
  //float percent = ((g_precomp.ctx->input_file_pos + g_precomp.ctx->uncompressed_bytes_written) / ((float)g_precomp.ctx->fin_length + g_precomp.ctx->uncompressed_bytes_total)) * (g_precomp.ctx->global_max_percent - g_precomp.ctx->global_min_percent) + g_precomp.ctx->global_min_percent;
  float percent = -1;

  uint64_t progress_in = 0, progress_out = 0;

  lzma_get_progress(this->otf_xz_stream_c.get(), &progress_in, &progress_out);

  int lzma_mib_total = this->otf_xz_stream_c->total_in / (1024 * 1024);
  int lzma_mib_written = progress_in / (1024 * 1024);
  show_progress(percent, true, true, lzma_mib_total, lzma_mib_written);
}

std::unique_ptr<std::istream> wrap_istream_otf_compression(std::unique_ptr<std::istream>&& istream, int otf_compression_method) {
  switch (otf_compression_method) {
    case OTF_NONE: {
      return std::move(istream);
    }
    case OTF_BZIP2: {
      return Bz2IStreamBuffer::from_istream(std::move(istream));
    }
    case OTF_XZ_MT: {
      return XzIStreamBuffer::from_istream(std::move(istream));
    }
  }
  print_to_console("Unknown compression method!");
  exit(1);
}

std::unique_ptr<std::ostream> XzOStreamBuffer::from_ostream(
  std::unique_ptr<std::ostream>&& ostream,
  std::unique_ptr<lzma_init_mt_extra_parameters>&& otf_xz_extra_params,
  uint64_t compression_otf_max_memory,
  unsigned int compression_otf_thread_count
) {
  auto new_fout = new Precomp_OStream<std::ofstream>();
  auto xz_streambuf = new XzOStreamBuffer(std::move(ostream), compression_otf_max_memory, compression_otf_thread_count, std::move(otf_xz_extra_params));
  new_fout->otf_compression_streambuf = std::unique_ptr<XzOStreamBuffer>(xz_streambuf);
  new_fout->rdbuf(xz_streambuf);
  return std::unique_ptr<std::ostream>(new_fout);
}

std::unique_ptr<std::ostream> Bz2OStreamBuffer::from_ostream(std::unique_ptr<std::ostream>&& ostream) {
  auto new_fout = new Precomp_OStream<std::ofstream>();
  auto bz2_streambuf = new Bz2OStreamBuffer(std::move(ostream));
  new_fout->otf_compression_streambuf = std::unique_ptr<Bz2OStreamBuffer>(bz2_streambuf);
  new_fout->rdbuf(bz2_streambuf);
  return std::unique_ptr<std::ostream>(new_fout);
}

std::unique_ptr<std::ostream> wrap_ostream_otf_compression(
  std::unique_ptr<std::ostream>&& ostream,
  int otf_compression_method,
  std::unique_ptr<lzma_init_mt_extra_parameters>&& otf_xz_extra_params,
  uint64_t compression_otf_max_memory,
  unsigned int compression_otf_thread_count
) {
  switch (otf_compression_method) {
  case OTF_NONE: {
    return std::move(ostream);
  }
  case OTF_BZIP2: {
    return Bz2OStreamBuffer::from_ostream(std::move(ostream));
  }
  case OTF_XZ_MT: {
    return XzOStreamBuffer::from_ostream(std::move(ostream), std::move(otf_xz_extra_params), compression_otf_max_memory, compression_otf_thread_count);
  }
  }
  print_to_console("Unknown compression method!");
  exit(1);
}
