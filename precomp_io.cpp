#include "precomp_io.h"

#include <filesystem>

void force_seekg(WrappedIStream& stream, long long offset, std::ios_base::seekdir origin) {
  if (stream.bad()) {
    throw std::runtime_error(make_cstyle_format_string("Input stream went bad"));
  }
  stream.clear();
  stream.seekg(offset, origin);
}

int ostream_printf(WrappedOStream& out, std::string str) {
  for (char character : str) {
    out.put(character);
    if (out.bad()) return 0;
  }
  return str.length();
}

int ostream_printf(std::ostream& out, std::string str) {
  WrappedOStream wrapped_out = WrappedOStream(&out, false);
  return ostream_printf(wrapped_out, str);
}

long long WrappedFStream::filesize() {
  if (wrapped_iostream->is_open()) wrapped_iostream->close();
  long long size = std::filesystem::file_size(file_path.c_str());
  reopen();
  return size;
}

void WrappedFStream::resize(long long size) {
  if (wrapped_iostream->is_open()) wrapped_iostream->close();
  std::filesystem::resize_file(file_path, size);
  reopen();
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

WrappedIStream wrap_istream_otf_compression(std::unique_ptr<std::istream>&& istream, int otf_compression_method) {
  switch (otf_compression_method) {
  case OTF_NONE: {
    return { istream.release(), true };
    }
    case OTF_BZIP2: {
      return { Bz2IStreamBuffer::from_istream(std::move(istream)).release(), true };
    }
    case OTF_XZ_MT: {
      return { XzIStreamBuffer::from_istream(std::move(istream)).release(), true };
    }
  }
  throw std::runtime_error(make_cstyle_format_string("Unknown compression method!"));
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

WrappedOStream wrap_ostream_otf_compression(
  std::unique_ptr<std::ostream>&& ostream,
  int otf_compression_method,
  std::unique_ptr<lzma_init_mt_extra_parameters>&& otf_xz_extra_params,
  uint64_t compression_otf_max_memory,
  unsigned int compression_otf_thread_count
) {
  switch (otf_compression_method) {
  case OTF_NONE: {
    return { ostream.release(), true };
  }
  case OTF_BZIP2: {
    return { Bz2OStreamBuffer::from_ostream(std::move(ostream)).release(), true };
  }
  case OTF_XZ_MT: {
    return { XzOStreamBuffer::from_ostream(std::move(ostream), std::move(otf_xz_extra_params), compression_otf_max_memory, compression_otf_thread_count).release(), true };
  }
  }
  throw std::runtime_error(make_cstyle_format_string("Unknown compression method!"));
}
