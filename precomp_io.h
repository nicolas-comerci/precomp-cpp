#ifndef PRECOMP_IO_H
#define PRECOMP_IO_H
#include "precomp_utils.h"

#include "contrib/bzip2/bzlib.h"
#include "contrib/liblzma/precomp_xz.h"

#include <memory>
#include <fstream>

constexpr auto CHUNK = 262144; // 256 KB buffersize

// compression-on-the-fly
enum { OTF_NONE = 0, OTF_BZIP2 = 1, OTF_XZ_MT = 2 }; // uncompressed, bzip2, lzma2 multithreaded

int lzma_max_memory_default();

template <typename T>
class Precomp_IStream_Base : public T
{
  static_assert(std::is_base_of_v<std::istream, T>, "Precomp_IStream must get an std::istream derivative as template parameter");
public:
  void rdbuf(std::streambuf* streambuffer)
  {
    std::istream& stream_ref = *this;
    stream_ref.rdbuf(streambuffer);
  }

  int compression_otf_method = OTF_NONE;
  bool decompress_otf_end = false;
  std::unique_ptr<bz_stream> otf_bz2_stream_d;
  std::unique_ptr<lzma_stream> otf_xz_stream_d;
  std::unique_ptr<unsigned char[]> otf_in;

  void init_otf_in_if_needed()
  {
    if (otf_in != nullptr) return;
    otf_in = std::make_unique<unsigned char[]>(CHUNK);
    if (this->compression_otf_method == OTF_BZIP2) {
      otf_bz2_stream_d = std::unique_ptr<bz_stream>(new bz_stream());
      otf_bz2_stream_d->bzalloc = NULL;
      otf_bz2_stream_d->bzfree = NULL;
      otf_bz2_stream_d->opaque = NULL;
      otf_bz2_stream_d->avail_in = 0;
      otf_bz2_stream_d->next_in = NULL;
      if (BZ2_bzDecompressInit(otf_bz2_stream_d.get(), 0, 0) != BZ_OK) {
        print_to_console("ERROR: bZip2 init failed\n");
        exit(1);
      }
    }
    else if (this->compression_otf_method == OTF_XZ_MT) {
      otf_xz_stream_d = std::unique_ptr<lzma_stream>(new lzma_stream());
      if (!init_decoder(otf_xz_stream_d.get())) {
        print_to_console("ERROR: liblzma init failed\n");
        exit(1);
      }
    }
  }

  ~Precomp_IStream_Base()
  {
    if (otf_bz2_stream_d != nullptr)
    {
      (void)BZ2_bzDecompressEnd(otf_bz2_stream_d.get());
    }
    if (otf_xz_stream_d != nullptr)
    {
      (void)lzma_end(otf_xz_stream_d.get());
    }
  }
};

template <typename T>
class Precomp_IStream : public Precomp_IStream_Base<T> {};

class Precomp_IfStream : public Precomp_IStream_Base<std::ifstream>
{
  size_t own_fread(void* ptr, size_t size, size_t count);
public:
  std::streamsize last_gcount = 0;

  std::streamsize gcount() { return last_gcount; }

  int get() {
    if (compression_otf_method == OTF_NONE) {
      int chr = this->std::ifstream::get();
      last_gcount = this->std::ifstream::gcount();
      return chr;
    }
    else {
      unsigned char temp_buf[1];
      read(reinterpret_cast<char*>(temp_buf), 1);
      return temp_buf[0];
    }
  }

  std::ifstream& read(char* buf, std::streamsize size)
  {
    last_gcount = this->own_fread(buf, 1, size);
    return *this;
  }
};

template <typename T>
class Precomp_OStream : public T
{
  static_assert(std::is_base_of_v<std::ostream, T>, "OStreamWrapper must get an std::ostream derivative as template parameter");
public:
  void rdbuf(std::streambuf* streambuffer)
  {
    std::ostream& stream_ref = *this;
    stream_ref.rdbuf(streambuffer);
  }

  int compression_otf_method = OTF_NONE;
  uint64_t compression_otf_max_memory;
  unsigned int compression_otf_thread_count;
  std::unique_ptr<bz_stream> otf_bz2_stream_c;
  std::unique_ptr<lzma_stream> otf_xz_stream_c;
  std::unique_ptr<lzma_init_mt_extra_parameters> otf_xz_extra_params;
  std::unique_ptr<unsigned char[]> otf_out;

  void init_otf_in_if_needed()
  {
    if (otf_out != nullptr) return;
    otf_out = std::make_unique<unsigned char[]>(CHUNK);
    if (this->compression_otf_method == OTF_BZIP2) {
      otf_bz2_stream_c = std::unique_ptr<bz_stream>(new bz_stream());
      otf_bz2_stream_c->bzalloc = NULL;
      otf_bz2_stream_c->bzfree = NULL;
      otf_bz2_stream_c->opaque = NULL;
      if (BZ2_bzCompressInit(otf_bz2_stream_c.get(), 9, 0, 0) != BZ_OK) {
        print_to_console("ERROR: bZip2 init failed\n");
        exit(1);
      }
    }
    else if (this->compression_otf_method == OTF_XZ_MT) {
      otf_xz_stream_c = std::unique_ptr<lzma_stream>(new lzma_stream());
      uint64_t memory_usage = 0;
      uint64_t block_size = 0;
      uint64_t max_memory = this->compression_otf_max_memory * 1024 * 1024LL;
      int threads = this->compression_otf_thread_count;

      if (max_memory == 0) {
        max_memory = lzma_max_memory_default() * 1024 * 1024LL;
      }
      if (threads == 0) {
        threads = auto_detected_thread_count();
      }

      if (!init_encoder_mt(otf_xz_stream_c.get(), threads, max_memory, memory_usage, block_size, *otf_xz_extra_params)) {
        print_to_console("ERROR: xz Multi-Threaded init failed\n");
        exit(1);
      }

      std::string plural = "";
      if (threads > 1) {
        plural = "s";
      }
      print_to_console(
        "Compressing with LZMA, " + std::to_string(threads) + plural + ", memory usage: " + std::to_string(memory_usage / (1024 * 1024)) + " MiB, block size: " + std::to_string(block_size / (1024 * 1024)) + " MiB\n\n"
      );
    }
  }
};

template <typename T>
class OStreamWrapper : public Precomp_OStream<T> {};

class Precomp_OfStream : public Precomp_OStream<std::ofstream>
{
  Precomp_OfStream& own_fwrite(const void* ptr, std::streamsize size, std::streamsize count, bool final_byte = false);
public:
  ~Precomp_OfStream()
  {
    if (this->compression_otf_method > OTF_NONE) {

      // uncompressed data of length 0 ends compress-on-the-fly data
      char final_buf[9];
      for (int i = 0; i < 9; i++) {
        final_buf[i] = 0;
      }
      this->own_fwrite(final_buf, 1, 9, true);
    }
    if (otf_bz2_stream_c != nullptr)
    {
      (void)BZ2_bzCompressEnd(otf_bz2_stream_c.get());
    }
    if (otf_xz_stream_c != nullptr)
    {
      (void)lzma_end(otf_xz_stream_c.get());
    }
  }

  Precomp_OfStream& write(char* buf, std::streamsize count) {
    return this->own_fwrite(buf, 1, count);
  }

  Precomp_OfStream& put(char c) {
    if (this->compression_otf_method == OTF_NONE) { // uncompressed
      this->std::ofstream::put(c);
    }
    else {
      unsigned char temp_buf[1];
      temp_buf[0] = c;
      this->own_fwrite(temp_buf, 1, 1);
    }
    return *this;
  }

  void lzma_progress_update();
};
#endif // PRECOMP_IO_H
