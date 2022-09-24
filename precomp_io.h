#ifndef PRECOMP_IO_H
#define PRECOMP_IO_H
#include "precomp_utils.h"

// compression-on-the-fly
enum { OTF_NONE = 0, OTF_BZIP2 = 1, OTF_XZ_MT = 2 }; // uncompressed, bzip2, lzma2 multithreaded

template <typename T>
class StreamWrapper_Base
{
public:
  std::unique_ptr<T> stream = std::unique_ptr<T>(new T());

  bool good() const
  {
    return stream->good();
  }

  bool eof() const
  {
    return stream->eof();
  }

  bool bad() const
  {
    return stream->bad();
  }
};

template <typename T>
class IStreamWrapper_Base : public StreamWrapper_Base<T>
{
  static_assert(std::is_base_of_v<std::istream, T>, "IStreamWrapper must get an std::istream derivative as template parameter");
public:
  void rdbuf(std::streambuf* new_sb)
  {
    std::istream& istream = *StreamWrapper_Base<T>::stream;
    istream.rdbuf(new_sb);
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

  ~IStreamWrapper_Base()
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

  T& read(char* buf, std::streamsize size)
  {
    StreamWrapper_Base<T>::stream->read(buf, size);
    return *StreamWrapper_Base<T>::stream;
  }

  std::ifstream::traits_type::int_type get()
  {
    return StreamWrapper_Base<T>::stream->get();
  }

  std::streamsize gcount()
  {
    return StreamWrapper_Base<T>::stream->gcount();
  }

  std::istream::traits_type::pos_type tellg()
  {
    return StreamWrapper_Base<T>::stream->tellg();
  }
};

template <typename T>
class IStreamWrapper : public IStreamWrapper_Base<T> {};

class IfStreamWrapper : public IStreamWrapper_Base<std::ifstream>
{
public:
  void open(std::string filename, std::ios_base::openmode mode)
  {
    stream->open(filename, mode);
  }

  bool is_open()
  {
    return stream->is_open();
  }

  void close()
  {
    stream->close();
  }

  size_t own_fread(void* ptr, size_t size, size_t count);
};

template <typename T>
class OStreamWrapper_Base : public StreamWrapper_Base<T>
{
  static_assert(std::is_base_of_v<std::ostream, T>, "OStreamWrapper must get an std::ostream derivative as template parameter");
public:
  void rdbuf(std::streambuf* streambuffer)
  {
    std::ostream& stream_ref = *StreamWrapper_Base<T>::stream;
    stream_ref.rdbuf(streambuffer);
  }

  int compression_otf_method = OTF_NONE;
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
      uint64_t max_memory = g_precomp.switches.compression_otf_max_memory * 1024 * 1024LL;
      int threads = g_precomp.switches.compression_otf_thread_count;

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

  T& write(char* buf, std::streamsize size)
  {
    StreamWrapper_Base<T>::stream->write(buf, size);
    return *StreamWrapper_Base<T>::stream;
  }

  T& put(char chr)
  {
    StreamWrapper_Base<T>::stream->put(chr);
    return *StreamWrapper_Base<T>::stream;
  }

  T& flush()
  {
    StreamWrapper_Base<T>::stream->flush();
    return *StreamWrapper_Base<T>::stream;
  }

  T& seekp(std::ostream::off_type offset, std::ostream::seekdir way)
  {
    StreamWrapper_Base<T>::stream->seekp(offset, way);
    return *StreamWrapper_Base<T>::stream;
  }

  std::ostream::traits_type::pos_type tellp()
  {
    return StreamWrapper_Base<T>::stream->tellp();
  }
};

template <typename T>
class OStreamWrapper : public OStreamWrapper_Base<T> {};

class OfStreamWrapper : public OStreamWrapper_Base<std::ofstream>
{
public:
  ~OfStreamWrapper()
  {
    if (this->compression_otf_method > OTF_NONE) {

      // uncompressed data of length 0 ends compress-on-the-fly data
      char final_buf[9];
      for (int i = 0; i < 9; i++) {
        final_buf[i] = 0;
      }
      this->own_fwrite(final_buf, 1, 9, true, true);
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

  void open(std::string filename, std::ios_base::openmode mode)
  {
    stream->open(filename, mode);
  }

  bool is_open()
  {
    return stream->is_open();
  }

  void close()
  {
    stream->close();
  }

  void own_fwrite(const void* ptr, size_t size, size_t count, bool final_byte = false, bool update_lzma_progress = false);
  void lzma_progress_update();
};
#endif // PRECOMP_IO_H
