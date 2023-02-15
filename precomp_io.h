#ifndef PRECOMP_IO_H
#define PRECOMP_IO_H
#include "precomp_utils.h"

#include "contrib/bzip2/bzlib.h"
#include "contrib/liblzma/precomp_xz.h"

#include <memory>
#include <fstream>
#include <functional>
#include <map>

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

// Interface for the things in common that something that behaves like a std::istream or std::ostream has to have
class StreamLikeCommon {
public:
  virtual ~StreamLikeCommon() {}

  virtual bool eof() = 0;
  virtual bool good() = 0;
  virtual bool bad() = 0;
  virtual void clear() = 0;
};

class IStreamLike: public StreamLikeCommon {
public:
  virtual IStreamLike& read(char* buff, std::streamsize count) = 0;
  virtual std::istream::int_type get() = 0;
  virtual std::streamsize gcount() = 0;
  virtual IStreamLike& seekg(std::istream::off_type offset, std::ios_base::seekdir dir) = 0;
  virtual std::istream::pos_type tellg() = 0;
};

class OStreamLike: public StreamLikeCommon {
public:
  virtual OStreamLike& write(const char* buf, std::streamsize count) = 0;
  virtual OStreamLike& put(char chr) = 0;
  virtual void flush() = 0;
  virtual std::ostream::pos_type tellp() = 0;
  virtual OStreamLike& seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) = 0;
};

template <typename T>
class WrappedStream: public StreamLikeCommon {
protected:
  T* wrapped_stream;
  bool owns_wrapped_stream;

  // Protected constructor to make the class "abstract" (done this way because there is no method we can make pure virtual as destructor needs to handle ptr ownership)
  WrappedStream(T* stream, bool owns_wrapped_stream) {
    wrapped_stream = stream;
    this->owns_wrapped_stream = owns_wrapped_stream;
  }

public:
  ~WrappedStream() override {
    if (!owns_wrapped_stream) return;
    delete wrapped_stream;
  }

  // No copies allowed, would cause problems with the pointer ownership
  WrappedStream(WrappedStream& o) = delete;
  WrappedStream& operator=(WrappedStream const&) = delete;

  bool is_owns_wrapped_stream() { return owns_wrapped_stream; }

  // Careful! I am not writting safeguards for this, if you attempt to use this after release you will get a nullptr dereference and you get to keep the pieces!
  T* release() {
    owns_wrapped_stream = false;
    auto ptr_copy = wrapped_stream;
    wrapped_stream = nullptr;
    return ptr_copy;
  }

  virtual std::streambuf* rdbuf(std::streambuf* sbuff) { return wrapped_stream->rdbuf(sbuff); }

  bool eof() override { return wrapped_stream->eof(); }
  bool good() override { return wrapped_stream->good(); }
  bool bad() override { return wrapped_stream->bad(); }
  void clear() override { wrapped_stream->clear(); }
};

class WrappedOStream: public WrappedStream<std::ostream>, public OStreamLike {
public:
  WrappedOStream(std::ostream* stream, bool take_ownership): WrappedStream(stream, take_ownership) { }

  ~WrappedOStream() override {}

  bool eof() override { return WrappedStream::eof(); }
  bool good() override { return WrappedStream::good(); }
  bool bad() override { return WrappedStream::bad(); }
  void clear() override { WrappedStream::clear(); }

  WrappedOStream& write(const char* buf, std::streamsize count) override {
    wrapped_stream->write(buf, count);
    return *this;
  }

  WrappedOStream& put(char chr) override {
    wrapped_stream->put(chr);
    return *this;
  }

  void flush() override { wrapped_stream->flush(); }

  std::ostream::pos_type tellp() override { return wrapped_stream->tellp(); }

  WrappedOStream& seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) override {
    wrapped_stream->seekp(offset, dir);
    return *this;
  }
};

void fseek64(FILE* file_ptr, unsigned long long pos, std::ios_base::seekdir dir);

class FILEOStream : public OStreamLike {
  FILE* file_ptr;
  bool take_ownership;
public:
  FILEOStream(FILE* file, bool take_ownership) : file_ptr(file), take_ownership(take_ownership) { }
  ~FILEOStream() override {
    if (!take_ownership) return;
    std::fclose(file_ptr);
  }

  bool eof() override { return std::feof(file_ptr); }
  bool good() override { return !std::feof(file_ptr) && !std::ferror(file_ptr); }
  bool bad() override { return std::ferror(file_ptr); }
  void clear() override {
    std::clearerr(file_ptr);
  }

  FILEOStream& write(const char* buf, std::streamsize count) override {
    std::fwrite(buf, sizeof(const char), count, file_ptr);
    return *this;
  }

  FILEOStream& put(char chr) override
  {
    std::fputc(chr, file_ptr);
    return *this;
  }

  void flush() override { std::fflush(file_ptr); }
  std::ostream::pos_type tellp() override { return std::ftell(file_ptr); }
  FILEOStream& seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) override
  {
    fseek64(file_ptr, offset, dir);
    return *this;
  }
};

class FILEIStream: public IStreamLike {
  FILE* file_ptr;
  bool take_ownership;
  size_t _gcount;
public:
  FILEIStream(FILE* file, bool take_ownership): file_ptr(file), take_ownership(take_ownership), _gcount(0) { }
  ~FILEIStream() override {
    if (!take_ownership) return;
    std::fclose(file_ptr);
  }

  bool eof() override { return std::feof(file_ptr); }
  bool good() override { return !std::feof(file_ptr) && !std::ferror(file_ptr); }
  bool bad() override { return std::ferror(file_ptr); }
  void clear() override {
    std::clearerr(file_ptr);
  }

  FILEIStream& read(char* buff, std::streamsize count) override {
    _gcount = std::fread(buff, sizeof(char), count, file_ptr);
    return *this;
  }

  std::istream::int_type get() override {
    _gcount = 1;
    auto chr = std::fgetc(file_ptr);
    if (chr == EOF) {
      chr = std::istream::traits_type::eof();
    }
    return chr;
  }
  std::streamsize gcount() override { return _gcount; }

  FILEIStream& seekg(std::istream::off_type offset, std::ios_base::seekdir dir) override {
    fseek64(file_ptr, offset, dir);
    return *this;
  }

  std::istream::pos_type tellg() override { return std::ftell(file_ptr); }
};

class WrappedIStream : public WrappedStream<std::istream>, public IStreamLike {
public:
  WrappedIStream(std::istream* stream, bool take_ownership): WrappedStream(stream, take_ownership) { }

  bool eof() override { return WrappedStream::eof(); }
  bool good() override { return WrappedStream::good(); }
  bool bad() override { return WrappedStream::bad(); }
  void clear() override { WrappedStream::clear(); }

  WrappedIStream& read(char* buff, std::streamsize count) override {
    wrapped_stream->read(buff, count);
    return *this;
  }

  std::istream::int_type get() override { return wrapped_stream->get(); }

  std::streamsize gcount() override { return wrapped_stream->gcount(); }

  WrappedIStream& seekg(std::istream::off_type offset, std::ios_base::seekdir dir) override {
    // istreams don't allow seeking once eof/failbit is set, which happens if we read a file to the end.
    // This behaves more like std::fseek by just clearing the eof and failbit to allow the seek operation to happen.
    if (bad()) {
      throw std::runtime_error(make_cstyle_format_string("Input stream went bad"));
    }
    clear();
    wrapped_stream->seekg(offset, dir);
    return *this;
  }

  std::istream::pos_type tellg() override { return wrapped_stream->tellg(); }
};

// With this we can get notified whenever we write to the ostream, useful for registering callbacks to update progress without littering our code with calls for it
class ObservableStreamBase {
public:
  enum observable_methods {
    write_method,
    put_method,
    flush_method,
    tellp_method,
    seekp_method
  };

  void register_observer(observable_methods method, std::function<void()> callback) {
    method_observers[method] = callback;
  }
protected:
  std::map<observable_methods, std::function<void()>> method_observers = { {write_method, {}} };

  void notify_observer(observable_methods method) {
    auto observer_callback = method_observers[method];
    if (observer_callback) observer_callback();
  }
};

class ObservableOStream: public OStreamLike, public ObservableStreamBase {
protected:
  virtual void internal_write(const char* buf, std::streamsize count) = 0;
  virtual void internal_put(char chr) = 0;
  virtual void internal_flush() = 0;
  virtual std::ostream::pos_type internal_tellp() = 0;
  virtual void internal_seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) = 0;
public:
  ObservableOStream& write(const char* buf, std::streamsize count) override {
    internal_write(buf, count);
    notify_observer(write_method);
    return *this;
  }
  ObservableOStream& put(char chr) override {
    internal_put(chr);
    notify_observer(put_method);
    return *this;
  }
  void flush() override {
    internal_flush();
    notify_observer(flush_method);
  }
  std::ostream::pos_type tellp() override {
    notify_observer(tellp_method);
    return internal_tellp();    
  }
  ObservableOStream& seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) override {
    internal_seekp(offset, dir);
    notify_observer(seekp_method);
    return *this;
  }
};

template <typename T>
class ObservableOStreamIMPL: public T, public ObservableOStream {
protected:
  void internal_write(const char* buf, std::streamsize count) override { T::write(buf, count); }
  void internal_put(char chr) override { T::put(chr); }
  void internal_flush() override { T::flush(); }
  std::ostream::pos_type internal_tellp() override { return T::tellp(); }
  void internal_seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) override { T::seekp(offset, dir); }

public:
  ObservableOStreamIMPL(FILE* stream, bool take_ownership) : T(stream, take_ownership) { }
  ObservableOStreamIMPL(std::ostream* stream, bool take_ownership): T(stream, take_ownership) { }

  bool eof() override { return T::eof(); }
  bool good() override { return T::good(); }
  bool bad() override { return T::bad(); }
  void clear() override { T::clear(); }
};

using ObservableWrappedOStream = ObservableOStreamIMPL<WrappedOStream>;
using ObservableFILEOStream = ObservableOStreamIMPL<FILEOStream>;

class ObservableIStream : public WrappedIStream {
public:
  ObservableIStream(std::istream* stream, bool take_ownership): WrappedIStream(stream, take_ownership) { }
};

int ostream_printf(OStreamLike& out, std::string str);
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

  bool eof() override { return wrapped_iostream->eof(); }
  bool good() override { return wrapped_iostream->good(); }
  bool bad() override { return wrapped_iostream->bad(); }
  void clear() override { wrapped_iostream->clear(); }
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

class CompressedIStreamBuffer: public std::streambuf
{
public:
  std::unique_ptr<std::istream> wrapped_istream;
  std::unique_ptr<char[]> otf_in;
  std::unique_ptr<char[]> otf_dec;

  CompressedIStreamBuffer(std::istream& istream): wrapped_istream(&istream) { }
  CompressedIStreamBuffer(std::unique_ptr<std::istream>&& istream): wrapped_istream(std::move(istream)) { }

  pos_type seekoff(off_type offset, std::ios_base::seekdir dir, std::ios_base::openmode mode) override {
    return wrapped_istream->rdbuf()->pubseekoff(offset, dir, mode);
  }
};

class Bz2IStreamBuffer: public CompressedIStreamBuffer
{
public:
  std::unique_ptr<bz_stream> otf_bz2_stream_d;

  Bz2IStreamBuffer(std::istream& istream): CompressedIStreamBuffer(istream) {
    init();
  }

  Bz2IStreamBuffer(std::unique_ptr<std::istream>&& istream): CompressedIStreamBuffer(std::move(istream)){
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

class XzIStreamBuffer: public CompressedIStreamBuffer
{
public:
  std::unique_ptr<lzma_stream> otf_xz_stream_d;

  XzIStreamBuffer(std::unique_ptr<std::istream>&& istream): CompressedIStreamBuffer(std::move(istream)) {
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

WrappedIStream wrap_istream_otf_compression(std::unique_ptr<std::istream>&& istream, int otf_compression_method, bool take_ownership);

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

  pos_type seekoff(off_type offset, std::ios_base::seekdir dir, std::ios_base::openmode mode) override {
    return wrapped_ostream->rdbuf()->pubseekoff(offset, dir, mode);
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

    setp(otf_in.get(), otf_in.get() + CHUNK - 1);
  }

  int sync(bool final_byte) override {
    lzma_action action = final_byte ? LZMA_FINISH : LZMA_RUN;
    lzma_ret ret;

    otf_xz_stream_c->avail_in = pptr() - pbase();
    otf_xz_stream_c->next_in = (uint8_t*)otf_in.get();
    do {
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
  unsigned int compression_otf_thread_count,
  bool take_ownership
);
#endif // PRECOMP_IO_H
