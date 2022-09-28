#ifndef PRECOMP_IO_H
#define PRECOMP_IO_H
#include "precomp_utils.h"

#include "contrib/bzip2/bzlib.h"
#include "contrib/liblzma/precomp_xz.h"

#include <memory>
#include <fstream>
#include <functional>

class CompressedOStreamBuffer;
constexpr auto CHUNK = 262144; // 256 KB buffersize

// compression-on-the-fly
enum { OTF_NONE = 0, OTF_BZIP2 = 1, OTF_XZ_MT = 2 }; // uncompressed, bzip2, lzma2 multithreaded

int lzma_max_memory_default();

class Bz2IStreamBuffer : public std::streambuf
{
public:
  std::istream* wrapped_istream;
  bool owns_wrapped_istream = false;
  std::unique_ptr<bz_stream> otf_bz2_stream_d;
  std::unique_ptr<char[]> otf_in;
  std::unique_ptr<char[]> otf_dec;

  Bz2IStreamBuffer(std::istream& wrapped_istream) : wrapped_istream(&wrapped_istream) {
    init();
  }

  Bz2IStreamBuffer(std::unique_ptr<std::istream>&& wrapped_istream) {
    this->wrapped_istream = wrapped_istream.release();
    owns_wrapped_istream = true;
    init();
  }

  static std::unique_ptr<std::istream> from_istream(std::unique_ptr<std::istream>&& istream) {
    auto new_fin = std::unique_ptr<std::istream>(new std::ifstream());
    auto bz2_streambuf = new Bz2IStreamBuffer(std::move(istream));
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

  ~Bz2IStreamBuffer() override {
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

class XzIStreamBuffer: public std::streambuf
{
public:
  std::istream* wrapped_istream;
  bool owns_wrapped_istream = false;
  std::unique_ptr<lzma_stream> otf_xz_stream_d;
  std::unique_ptr<char[]> otf_in;
  std::unique_ptr<char[]> otf_dec;

  XzIStreamBuffer(std::istream& wrapped_istream): wrapped_istream(&wrapped_istream) {
    init();
  }

  XzIStreamBuffer(std::unique_ptr<std::istream>&& wrapped_istream) {
    this->wrapped_istream = wrapped_istream.release();
    owns_wrapped_istream = true;
    init();
  }

  static std::unique_ptr<std::istream> from_istream(std::unique_ptr<std::istream>&& istream) {
    auto new_fin = std::unique_ptr<std::istream>(new std::ifstream());
    auto xz_streambuf = new XzIStreamBuffer(std::move(istream));
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

  ~XzIStreamBuffer() override {
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
  std::unique_ptr<CompressedOStreamBuffer> otf_compression_streambuf;
  void rdbuf(std::streambuf* streambuffer)
  {
    std::ostream& stream_ref = *this;
    stream_ref.rdbuf(streambuffer);
  }

  ~Precomp_OStream() {
    if (otf_compression_streambuf != nullptr) otf_compression_streambuf->set_stream_eof();
  }
};

class CompressedOStreamBuffer : public std::streambuf
{
public:
  std::unique_ptr<std::ostream> wrapped_ostream;
  bool is_stream_eof = false;
  std::unique_ptr<char[]> otf_in;
  std::unique_ptr<char[]> otf_out;

  CompressedOStreamBuffer(std::unique_ptr<std::ostream>&& wrapped_ostream): wrapped_ostream(std::move(wrapped_ostream))
  {
    otf_in = std::make_unique<char[]>(CHUNK);
    otf_out = std::make_unique<char[]>(CHUNK);
  }

  virtual int sync(bool final_byte) = 0;
  int sync() override { return sync(false); }

  void set_stream_eof() {
    if (is_stream_eof) return;
    // uncompressed data of length 0 ends compress-on-the-fly data
    for (int i = 0; i < 9; i++) {
      overflow(0);
    }
    sync(true);
    is_stream_eof = true;
  }

  int overflow(int c) override {
    if (c == EOF) {
      set_stream_eof();
      return c;
    }

    if (pptr() == epptr()) {
      sync();
    }
    *pptr() = c;
    pbump(1);

    return c;
  }
};

class Bz2OStreamBuffer : public CompressedOStreamBuffer
{
public:
  std::unique_ptr<bz_stream, std::function<void(bz_stream*)>> otf_bz2_stream_c;

  Bz2OStreamBuffer(std::unique_ptr<std::ostream>&& wrapped_ostream) :
    CompressedOStreamBuffer(std::move(wrapped_ostream))
  {
    init();
  }

  static std::unique_ptr<std::ostream> from_ostream(std::unique_ptr<std::ostream>&& ostream);

  void init() {
    otf_bz2_stream_c = std::unique_ptr<bz_stream, std::function<void(bz_stream*)>>(
      new bz_stream(),
      [](bz_stream* ptr) {
        BZ2_bzCompressEnd(ptr);
      }
    );
    otf_bz2_stream_c->bzalloc = NULL;
    otf_bz2_stream_c->bzfree = NULL;
    otf_bz2_stream_c->opaque = NULL;
    if (BZ2_bzCompressInit(otf_bz2_stream_c.get(), 9, 0, 0) != BZ_OK) {
      print_to_console("ERROR: bZip2 init failed\n");
      exit(1);
    }

    setp(otf_in.get(), otf_in.get() + CHUNK - 1);
  }

  int sync(bool final_byte) override {
    int flush, ret;
    print_work_sign(true);

    flush = final_byte ? BZ_FINISH : BZ_RUN;

    otf_bz2_stream_c->avail_in = pptr() - pbase();
    otf_bz2_stream_c->next_in = otf_in.get();
    do {
      otf_bz2_stream_c->avail_out = CHUNK;
      otf_bz2_stream_c->next_out = otf_out.get();
      ret = BZ2_bzCompress(otf_bz2_stream_c.get(), flush);
      unsigned have = CHUNK - otf_bz2_stream_c->avail_out;
      this->wrapped_ostream->write(otf_out.get(), have);
      if (this->wrapped_ostream->bad()) {
        error(ERR_DISK_FULL);
      }
    } while (otf_bz2_stream_c->avail_out == 0);
    if (ret < 0) {
      print_to_console("ERROR: bZip2 compression failed - return value %i\n", ret);
      exit(1);
    }

    setp(otf_in.get(), otf_in.get() + CHUNK - 1);
    return 0;
  }
};

class XzOStreamBuffer : public CompressedOStreamBuffer
{
public:
  uint64_t compression_otf_max_memory;
  unsigned int compression_otf_thread_count;
  std::unique_ptr<lzma_stream, std::function<void(lzma_stream*)>> otf_xz_stream_c;
  std::unique_ptr<lzma_init_mt_extra_parameters> otf_xz_extra_params;

  XzOStreamBuffer(
    std::unique_ptr<std::ostream>&& wrapped_ostream,
    uint64_t compression_otf_max_memory,
    unsigned int compression_otf_thread_count,
    std::unique_ptr<lzma_init_mt_extra_parameters>&& otf_xz_extra_params
  ) :
    CompressedOStreamBuffer(std::move(wrapped_ostream)), compression_otf_max_memory(compression_otf_max_memory), compression_otf_thread_count(compression_otf_thread_count)
  {
    init(std::move(otf_xz_extra_params));
  }

  static std::unique_ptr<std::ostream> from_ostream(
    std::unique_ptr<std::ostream>&& ostream,
    std::unique_ptr<lzma_init_mt_extra_parameters>&& otf_xz_extra_params,
    uint64_t compression_otf_max_memory,
    unsigned int compression_otf_thread_count
  );

  void init(std::unique_ptr<lzma_init_mt_extra_parameters>&& otf_xz_extra_params) {
    this->otf_xz_extra_params = std::move(otf_xz_extra_params);
    otf_xz_stream_c = std::unique_ptr<lzma_stream, std::function<void(lzma_stream*)>>(
      new lzma_stream(),
      [](lzma_stream* ptr) {
        lzma_end(ptr);
      }
    );
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

    if (!init_encoder_mt(otf_xz_stream_c.get(), threads, max_memory, memory_usage, block_size, *this->otf_xz_extra_params)) {
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

    setp(otf_in.get(), otf_in.get() + CHUNK - 1);
  }

  void lzma_progress_update();

  int sync(bool final_byte) override {
    lzma_action action = final_byte ? LZMA_FINISH : LZMA_RUN;
    lzma_ret ret;

    otf_xz_stream_c->avail_in = pptr() - pbase();
    otf_xz_stream_c->next_in = (uint8_t*)otf_in.get();
    do {
      print_work_sign(true);
      otf_xz_stream_c->avail_out = CHUNK;
      otf_xz_stream_c->next_out = (uint8_t*)otf_out.get();
      ret = lzma_code(otf_xz_stream_c.get(), action);
      unsigned have = CHUNK - otf_xz_stream_c->avail_out;
      this->wrapped_ostream->write(otf_out.get(), have);
      if (this->wrapped_ostream->bad()) {
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
      }
      if (!DEBUG_MODE) lzma_progress_update();
    } while (otf_xz_stream_c->avail_in > 0 || otf_xz_stream_c->avail_out != CHUNK || final_byte && ret != LZMA_STREAM_END);

    setp(otf_in.get(), otf_in.get() + CHUNK - 1);
    return 0;
  }
};

std::unique_ptr<std::ostream> wrap_ostream_otf_compression(
  std::unique_ptr<std::ostream>&& ostream,
  int otf_compression_method,
  std::unique_ptr<lzma_init_mt_extra_parameters>&& otf_xz_extra_params,
  uint64_t compression_otf_max_memory,
  unsigned int compression_otf_thread_count
);
#endif // PRECOMP_IO_H
