#ifndef PRECOMP_IO_H
#define PRECOMP_IO_H
#include "precomp_utils.h"

#include "contrib/bzip2/bzlib.h"
#include "contrib/liblzma/precomp_xz.h"

#include <memory>
#include <fstream>
#include <functional>

#ifndef __unix
#define PATH_DELIM '\\'
#else
#define PATH_DELIM '/'
#endif

/*
 This template and classes derived from it are so we can more easily customize behavior compared to regular istreams/ostreams, because most of their methods are not virtual,
 and dealing with streambufers is a nuisance.
 In this class hierarchy all delegations to the wrapped streams should be virtual, so we can much more easily just override those and be assured that whatever extra logic we
 add is always executed and is not skipped if the ptr/ref is downcasted.
 TODO: Refactor compressed streambufs to instead be derived from here
 TODO: Finish the derived Observables
 */
template <typename T>
class WrappedStream {
protected:
  T* wrapped_stream;
  bool owns_wrapped_stream;

  // Protected constructor to make the class "abstract" (done this way because there is no method we can make pure virtual as destructor needs to handle ptr ownership)
  WrappedStream(T* stream, bool owns_wrapped_stream) {
    wrapped_stream = stream;
    this->owns_wrapped_stream = owns_wrapped_stream;
  }

public:
  virtual ~WrappedStream() {
    if (!owns_wrapped_stream) return;
    delete wrapped_stream;
  }

  // No copies allowed, would cause problems with the pointer ownership
  WrappedStream(WrappedStream& o) = delete;
  WrappedStream& operator=(WrappedStream const&) = delete;

  // Careful! I am not writting safeguards for this, if you attempt to use this after release you will get a nullptr dereference and you get to keep the pieces!
  T* release() {
    owns_wrapped_stream = false;
    auto ptr_copy = wrapped_stream;
    wrapped_stream = nullptr;
    return ptr_copy;
  }

  virtual std::streambuf* rdbuf(std::streambuf* sbuff) { return wrapped_stream->rdbuf(sbuff); }

  virtual bool eof() { return wrapped_stream->eof(); }
  virtual bool good() { return wrapped_stream->good(); }
  virtual bool bad() { return wrapped_stream->bad(); }
  virtual void clear() { wrapped_stream->clear(); }
};

class WrappedOStream : public WrappedStream<std::ostream> {
public:
  WrappedOStream(std::ostream* stream, bool take_ownership): WrappedStream(stream, take_ownership) { }

  ~WrappedOStream() override {}

  virtual WrappedOStream& write(const char* buf, std::streamsize count) {
    wrapped_stream->write(buf, count);
    return *this;
  }

  virtual WrappedOStream& put(char chr) {
    wrapped_stream->put(chr);
    return *this;
  }

  virtual void flush() { wrapped_stream->flush(); }

  virtual std::istream::pos_type tellp() { return wrapped_stream->tellp(); }

  virtual WrappedOStream& seekp(std::istream::off_type offset, std::ios_base::seekdir dir) {
    wrapped_stream->seekp(offset, dir);
    return *this;
  }
};

class WrappedIStream : public WrappedStream<std::istream> {
public:
  WrappedIStream(std::istream* stream, bool take_ownership): WrappedStream(stream, take_ownership) { }

  virtual WrappedIStream& read(char* buff, std::streamsize count) {
    wrapped_stream->read(buff, count);
    return *this;
  }

  virtual std::istream::int_type get() { return wrapped_stream->get(); }

  virtual std::streamsize gcount() { return wrapped_stream->gcount(); }

  virtual WrappedIStream& seekg(std::istream::off_type offset, std::ios_base::seekdir dir) {
    wrapped_stream->seekg(offset, dir);
    return *this;
  }

  virtual std::istream::pos_type tellg() { return wrapped_stream->tellg(); }
};

// With this we can get notified whenever we write to the ostream, useful for registering callbacks to update progress without littering our code with calls for it
class ObservableOStream: public WrappedOStream {
public:
  ObservableOStream(std::ostream* stream, bool take_ownership): WrappedOStream(stream, take_ownership) { }
};

class ObservableIStream : public WrappedIStream {
public:
  ObservableIStream(std::istream* stream, bool take_ownership): WrappedIStream(stream, take_ownership) { }
};

// istreams don't allow seeking once eof/failbit is set, which happens if we read a file to the end.
// This behaves more like std::fseek by just clearing the eof and failbit to allow the seek operation to happen.
void force_seekg(WrappedIStream& stream, long long offset, std::ios_base::seekdir origin);

int ostream_printf(WrappedOStream& out, std::string str);
int ostream_printf(std::ostream& out, std::string str);

template <typename T>
class WrappedIOStream: public WrappedOStream, public WrappedIStream {
protected:
  std::unique_ptr<T> wrapped_iostream;

public:
  WrappedIOStream(T* stream):
    WrappedOStream(stream, false), WrappedIStream(stream, false), wrapped_iostream(stream) { }

  WrappedIOStream(): WrappedIOStream(new T()) { }

  WrappedIOStream(std::streambuf* sbuff): WrappedIOStream(new T(sbuff)) { }

  ~WrappedIOStream() override = default;

  //virtual typename T::pos_type tellg() { return wrapped_iostream->tellg(); }
};

class WrappedFStream: public WrappedIOStream<std::fstream> {
public:
  std::string file_path;
  std::ios_base::openmode mode;

  WrappedFStream(): WrappedIOStream() { }

  WrappedFStream(std::fstream* stream): WrappedIOStream(stream) {}

  ~WrappedFStream() override = default;

  virtual void open(std::string file_path, std::ios_base::openmode mode) {
    this->file_path = file_path;
    this->mode = mode;
    wrapped_iostream->open(file_path, mode);
  }

  virtual bool is_open() { return wrapped_iostream->is_open(); }

  virtual void close() { wrapped_iostream->close(); }

  // Some extra members that are not wrapped from the fstream
  void reopen() {
    if (wrapped_iostream->is_open()) wrapped_iostream->close();
    open(file_path, mode);
  }
  long long filesize();
  void resize(long long size);
};

class PrecompTmpFile: public WrappedFStream {
public:
  PrecompTmpFile() : WrappedFStream() {}

  ~PrecompTmpFile() override {
    WrappedFStream::close();
    std::remove(file_path.c_str());
  }
};

class memiostream: public WrappedIOStream<std::iostream>
{
  class membuf : public std::streambuf
  {
  public:
    membuf() = delete;
    membuf(char* begin, char* end)
    {
      this->setg(begin, begin, end);
      this->setp(begin, end);
    }

    pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which) override
    {
      if (dir == std::ios_base::cur) {
        gbump(off);
        pbump(off);
      }
      else if (dir == std::ios_base::end) {
        setg(eback(), egptr() + off, egptr());
        setg(pbase(), epptr() + off, epptr());
      }
      else if (dir == std::ios_base::beg) {
        setg(eback(), eback() + off, egptr());
        setg(pbase(), pbase() + off, epptr());
      }
      return gptr() - eback();
    }

    pos_type seekpos(pos_type sp, std::ios_base::openmode which) override
    {
      return seekoff(sp - pos_type(off_type(0)), std::ios_base::beg, which);
    }
  };

  std::unique_ptr<membuf> m_buf;
  memiostream(membuf* buf): WrappedIOStream(buf), m_buf(std::unique_ptr<membuf>(buf)) {}

public:
  static memiostream make(unsigned char* begin, unsigned char* end) {
    auto membuf_ptr = new membuf(reinterpret_cast<char*>(begin), reinterpret_cast<char*>(end));
    return {membuf_ptr};
  }
};

class CompressedOStreamBuffer;
constexpr auto CHUNK = 262144; // 256 KB buffersize

// compression-on-the-fly
enum { OTF_NONE = 0, OTF_BZIP2 = 1, OTF_XZ_MT = 2 }; // uncompressed, bzip2, lzma2 multithreaded

int lzma_max_memory_default();

class Bz2IStreamBuffer : public std::streambuf
{
public:
  std::unique_ptr<std::istream> wrapped_istream;
  std::unique_ptr<bz_stream> otf_bz2_stream_d;
  std::unique_ptr<char[]> otf_in;
  std::unique_ptr<char[]> otf_dec;

  Bz2IStreamBuffer(std::istream& wrapped_istream) : wrapped_istream(&wrapped_istream) {
    init();
  }

  Bz2IStreamBuffer(std::unique_ptr<std::istream>&& wrapped_istream): wrapped_istream(std::move(wrapped_istream)){
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
      throw std::runtime_error(make_cstyle_format_string("ERROR: bZip2 init failed\n"));
    }

    setg(otf_dec.get(), otf_dec.get(), otf_dec.get());
  }

  ~Bz2IStreamBuffer() override {
    (void)BZ2_bzDecompressEnd(otf_bz2_stream_d.get());
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
        throw std::runtime_error(make_cstyle_format_string("ERROR: bZip2 stream corrupted - return value %i\n", ret));
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
  std::unique_ptr<std::istream> wrapped_istream;
  std::unique_ptr<lzma_stream> otf_xz_stream_d;
  std::unique_ptr<char[]> otf_in;
  std::unique_ptr<char[]> otf_dec;

  XzIStreamBuffer(std::unique_ptr<std::istream>&& wrapped_istream): wrapped_istream(std::move(wrapped_istream)) {
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
      throw std::runtime_error("ERROR: liblzma init failed\n");
    }

    setg(otf_dec.get(), otf_dec.get(), otf_dec.get());
  }

  ~XzIStreamBuffer() override {
    lzma_end(otf_xz_stream_d.get());
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
          throw std::runtime_error("ERROR: Could not read input file\n");
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

        throw std::runtime_error(make_cstyle_format_string("ERROR: liblzma error: %s (error code %u)\n", msg, ret));
      }
    } while (otf_xz_stream_d->avail_out > 0);

    std::streamsize amt_read = CHUNK * 10 - otf_xz_stream_d->avail_out;
    setg(otf_dec.get(), otf_dec.get(), otf_dec.get() + amt_read);
    if (amt_read == 0 && stream_eof) return EOF;
    return static_cast<unsigned char>(*gptr());
  }
};

WrappedIStream wrap_istream_otf_compression(std::unique_ptr<std::istream>&& istream, int otf_compression_method);

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
      throw std::runtime_error(make_cstyle_format_string("ERROR: bZip2 init failed\n"));
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
        throw PrecompError(ERR_DISK_FULL);
      }
    } while (otf_bz2_stream_c->avail_out == 0);
    if (ret < 0) {
      throw std::runtime_error(make_cstyle_format_string("ERROR: bZip2 compression failed - return value %i\n", ret));
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
      throw std::runtime_error(make_cstyle_format_string("ERROR: xz Multi-Threaded init failed\n"));
    }

    std::string plural = "";
    if (threads > 1) {
      plural = "s";
    }
    print_to_console(
      "Compressing with LZMA, " + std::to_string(threads) + " thread" + plural + ", memory usage: " + std::to_string(memory_usage / (1024 * 1024)) + " MiB, block size: " + std::to_string(block_size / (1024 * 1024)) + " MiB\n\n"
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
        throw PrecompError(ERR_DISK_FULL);
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

        throw std::runtime_error(make_cstyle_format_string("ERROR: liblzma error: %s (error code %u)\n", msg, ret));
      }
      if (!DEBUG_MODE) lzma_progress_update();
    } while (otf_xz_stream_c->avail_in > 0 || otf_xz_stream_c->avail_out != CHUNK || final_byte && ret != LZMA_STREAM_END);

    setp(otf_in.get(), otf_in.get() + CHUNK - 1);
    return 0;
  }
};

WrappedOStream wrap_ostream_otf_compression(
  std::unique_ptr<std::ostream>&& ostream,
  int otf_compression_method,
  std::unique_ptr<lzma_init_mt_extra_parameters>&& otf_xz_extra_params,
  uint64_t compression_otf_max_memory,
  unsigned int compression_otf_thread_count
);
#endif // PRECOMP_IO_H
