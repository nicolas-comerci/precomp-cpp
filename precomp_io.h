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

class Bz2StreamBuffer : public std::streambuf
{
public:
  std::istream* wrapped_istream;
  bool owns_wrapped_istream = false;
  std::unique_ptr<bz_stream> otf_bz2_stream_d;
  std::unique_ptr<char[]> otf_in;
  std::unique_ptr<char[]> otf_dec;

  Bz2StreamBuffer(std::istream& wrapped_istream) : wrapped_istream(&wrapped_istream) {
    init();
  }

  Bz2StreamBuffer(std::unique_ptr<std::istream>&& wrapped_istream) {
    this->wrapped_istream = wrapped_istream.release();
    owns_wrapped_istream = true;
    init();
  }

  static std::unique_ptr<std::istream> from_istream(std::unique_ptr<std::istream>&& istream) {
    auto new_fin = std::unique_ptr<std::istream>(new std::ifstream());
    auto bz2_streambuf = new Bz2StreamBuffer(std::move(istream));
    new_fin->rdbuf(bz2_streambuf);
    return new_fin;
  }

  void init() {
    otf_in = std::make_unique<char[]>(CHUNK);
    otf_dec = std::make_unique<char[]>(CHUNK * 10);
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

    setg(otf_dec.get(), otf_dec.get(), otf_dec.get());
  }

  ~Bz2StreamBuffer() override {
    (void)BZ2_bzDecompressEnd(otf_bz2_stream_d.get());
    if (owns_wrapped_istream) delete wrapped_istream;
  }

  int underflow() override {
    if (gptr() < egptr())
      return *gptr();

    int ret;
    print_work_sign(true);

    this->otf_bz2_stream_d->avail_out = CHUNK*10;
    this->otf_bz2_stream_d->next_out = otf_dec.get();

    bool stream_eof = false;
    do {

      if (this->otf_bz2_stream_d->avail_in == 0) {
        wrapped_istream->read(otf_in.get(), CHUNK);
        this->otf_bz2_stream_d->avail_in = wrapped_istream->gcount();
        this->otf_bz2_stream_d->next_in = otf_in.get();
        if (this->otf_bz2_stream_d->avail_in == 0) break;
      }

      ret = BZ2_bzDecompress(otf_bz2_stream_d.get());
      if ((ret != BZ_OK) && (ret != BZ_STREAM_END)) {
        (void)BZ2_bzDecompressEnd(otf_bz2_stream_d.get());
        print_to_console("ERROR: bZip2 stream corrupted - return value %i\n", ret);
        exit(1);
      }

      if (ret == BZ_STREAM_END) stream_eof = true;

    } while (this->otf_bz2_stream_d->avail_out > 0 && ret != BZ_STREAM_END);

    std::streamsize amt_read = CHUNK * 10 - this->otf_bz2_stream_d->avail_out;
    setg(otf_dec.get(), otf_dec.get(), otf_dec.get() + amt_read);
    if (amt_read == 0 && stream_eof) return EOF;
    return static_cast<unsigned char>(*gptr());
  }
};

class XzStreamBuffer: public std::streambuf
{
public:
  std::istream* wrapped_istream;
  bool owns_wrapped_istream = false;
  std::unique_ptr<lzma_stream> otf_xz_stream_d;
  std::unique_ptr<char[]> otf_in;
  std::unique_ptr<char[]> otf_dec;

  XzStreamBuffer(std::istream& wrapped_istream): wrapped_istream(&wrapped_istream) {
    init();
  }

  XzStreamBuffer(std::unique_ptr<std::istream>&& wrapped_istream) {
    this->wrapped_istream = wrapped_istream.release();
    owns_wrapped_istream = true;
    init();
  }

  static std::unique_ptr<std::istream> from_istream(std::unique_ptr<std::istream>&& istream) {
    auto new_fin = std::unique_ptr<std::istream>(new std::ifstream());
    auto xz_streambuf = new XzStreamBuffer(std::move(istream));
    new_fin->rdbuf(xz_streambuf);
    return new_fin;
  }

  void init() {
    otf_in = std::make_unique<char[]>(CHUNK);
    otf_dec = std::make_unique<char[]>(CHUNK * 10);
    otf_xz_stream_d = std::unique_ptr<lzma_stream>(new lzma_stream());
    if (!init_decoder(otf_xz_stream_d.get())) {
      print_to_console("ERROR: liblzma init failed\n");
      exit(1);
    }

    setg(otf_dec.get(), otf_dec.get(), otf_dec.get());
  }

  ~XzStreamBuffer() override {
    lzma_end(otf_xz_stream_d.get());
    if (owns_wrapped_istream) delete wrapped_istream;
  }

  int underflow() override {
    if (gptr() < egptr())
      return *gptr();

    otf_xz_stream_d->avail_out = CHUNK * 10;
    otf_xz_stream_d->next_out = (uint8_t*)otf_dec.get();

    bool stream_eof = false;
    do {
      print_work_sign(true);
      if ((otf_xz_stream_d->avail_in == 0) && !wrapped_istream->eof()) {
        otf_xz_stream_d->next_in = (uint8_t*)otf_in.get();
        wrapped_istream->read(otf_in.get(), CHUNK);
        otf_xz_stream_d->avail_in = wrapped_istream->gcount();

        if (wrapped_istream->bad()) {
          print_to_console("ERROR: Could not read input file\n");
          exit(1);
        }
      }

      lzma_ret ret = lzma_code(otf_xz_stream_d.get(), LZMA_RUN);

      if (ret == LZMA_STREAM_END) {
        stream_eof = true;
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

    std::streamsize amt_read = CHUNK * 10 - otf_xz_stream_d->avail_out;
    setg(otf_dec.get(), otf_dec.get(), otf_dec.get() + amt_read);
    if (amt_read == 0 && stream_eof) return EOF;
    return static_cast<unsigned char>(*gptr());
  }
};

std::unique_ptr<std::istream> wrap_istream_otf_compression(std::unique_ptr<std::istream>&& istream, int otf_compression_method);

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
