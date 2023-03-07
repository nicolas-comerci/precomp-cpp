#ifndef PRECOMP_IO_H
#define PRECOMP_IO_H

#include <memory>
#include <fstream>
#include <functional>
#include <map>
#include <span>

#include "precomp_xz_params.h"

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
  virtual ~StreamLikeCommon() = default;

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
  WrappedStream(T* stream, bool take_stream_ownership): wrapped_stream(stream), owns_wrapped_stream(take_stream_ownership) { }

public:
  ~WrappedStream() override {
    if (!owns_wrapped_stream) return;
    delete wrapped_stream;
  }

  // No copies allowed, would cause problems with the pointer ownership
  WrappedStream(WrappedStream& o) = delete;
  WrappedStream& operator=(WrappedStream const&) = delete;

  bool is_owns_wrapped_stream() { return owns_wrapped_stream; }

  // Careful! I am not writing safeguards for this, if you attempt to use this after release you will get a nullptr dereference, and you get to keep the pieces!
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
  WrappedOStream(std::ostream* stream, bool take_ownership);

  ~WrappedOStream() override;

  bool eof() override;
  bool good() override;
  bool bad() override;
  void clear() override;

  WrappedOStream& write(const char* buf, std::streamsize count) override;
  WrappedOStream& put(char chr) override;
  void flush() override;
  std::ostream::pos_type tellp() override;
  WrappedOStream& seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) override;
};

void fseek64(FILE* file_ptr, unsigned long long pos, std::ios_base::seekdir dir);

class FILEOStream : public OStreamLike {
  FILE* file_ptr;
  bool take_ownership;
public:
  FILEOStream(FILE* file, bool take_ownership);
  ~FILEOStream() override;

  bool eof() override;
  bool good() override;
  bool bad() override;
  void clear() override;

  FILEOStream& write(const char* buf, std::streamsize count) override;
  FILEOStream& put(char chr) override;
  void flush() override;
  std::ostream::pos_type tellp() override;
  FILEOStream& seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) override;
};

class FILEIStream: public IStreamLike {
  FILE* file_ptr;
  bool take_ownership;
  size_t _gcount;
public:
  FILEIStream(FILE* file, bool take_ownership);
  ~FILEIStream() override;

  bool eof() override;
  bool good() override;
  bool bad() override;
  void clear() override;

  FILEIStream& read(char* buff, std::streamsize count) override;
  std::istream::int_type get() override;
  std::streamsize gcount() override;
  FILEIStream& seekg(std::istream::off_type offset, std::ios_base::seekdir dir) override;
  std::istream::pos_type tellg() override;
};

class WrappedIStream : public WrappedStream<std::istream>, public IStreamLike {
public:
  WrappedIStream(std::istream* stream, bool take_ownership);

  bool eof() override;
  bool good() override;
  bool bad() override;
  void clear() override;

  WrappedIStream& read(char* buff, std::streamsize count) override;
  std::istream::int_type get() override;
  std::streamsize gcount() override;
  WrappedIStream& seekg(std::istream::off_type offset, std::ios_base::seekdir dir) override;
  std::istream::pos_type tellg() override;
};

// This allows us to treat subsections of an IStreamLike as if they were a separate stream, and reaching the end of the subsection makes the View behave like it reached eof.
// The whole point of this is to behave exactly as if we had copied the data to a temporary file and were working with it.
class IStreamLikeView : public IStreamLike {
public:
  IStreamLike* istream;
  long long starting_stream_pos;
  long long current_stream_pos;
  long long final_allowed_stream_pos;
  bool _eof = false;

  explicit IStreamLikeView(IStreamLike* istream_, long long final_allowed_stream_pos_);

  IStreamLikeView& read(char* buff, std::streamsize count) override;
  std::istream::int_type get() override;
  std::streamsize gcount() override;
  std::istream::pos_type tellg() override;
  bool eof() override;
  bool good() override;
  bool bad() override;
  void clear() override;

  IStreamLikeView& seekg(std::istream::off_type offset, std::ios_base::seekdir dir) override;
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

  void register_observer(observable_methods method, std::function<void()> callback);
protected:
  std::map<observable_methods, std::function<void()>> method_observers = { {write_method, {}} };
  void notify_observer(observable_methods method);
};

class ObservableOStream: public OStreamLike, public ObservableStreamBase {
protected:
  virtual void internal_write(const char* buf, std::streamsize count) = 0;
  virtual void internal_put(char chr) = 0;
  virtual void internal_flush() = 0;
  virtual std::ostream::pos_type internal_tellp() = 0;
  virtual void internal_seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) = 0;
public:
  ObservableOStream& write(const char* buf, std::streamsize count) override;
  ObservableOStream& put(char chr) override;
  void flush() override;
  std::ostream::pos_type tellp() override;
  ObservableOStream& seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) override;
};

class ObservableOStreamWrapper : public ObservableOStream {
  OStreamLike* ostream;
  bool owns_ostream;

  void internal_write(const char* buf, std::streamsize count) override  { ostream->write(buf, count); }
  void internal_put(char chr) override { ostream->put(chr); }
  void internal_flush() override { ostream->flush(); }
  std::ostream::pos_type internal_tellp() override { return ostream->tellp(); }
  void internal_seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) override { ostream->seekp(offset, dir); }

public:
  ObservableOStreamWrapper(OStreamLike* ostream_, bool take_ownership) : ostream(ostream_), owns_ostream(take_ownership) {}
  ~ObservableOStreamWrapper() { if (owns_ostream) delete ostream; }

  bool eof() override { return ostream->eof(); }
  bool good() override { return ostream->good(); }
  bool bad() override { return ostream->bad(); }
  void clear() override { ostream->clear(); }
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

size_t ostream_printf(OStreamLike& out, const std::string& str);
size_t ostream_printf(std::ostream& out, const std::string& str);

template <typename T>
class WrappedIOStream: public WrappedOStream, public WrappedIStream {
protected:
  std::unique_ptr<T> wrapped_iostream;

public:
  explicit WrappedIOStream(T* stream):
    WrappedOStream(stream, false), WrappedIStream(stream, false), wrapped_iostream(stream) { }

  WrappedIOStream(): WrappedIOStream(new T()) { }

  explicit WrappedIOStream(std::streambuf* sbuff): WrappedIOStream(new T(sbuff)) { }

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

  WrappedFStream();
  ~WrappedFStream() override = default;

  virtual void open(std::string file_path, std::ios_base::openmode mode);
  virtual bool is_open();
  virtual void close();

  // Some extra members that are not wrapped from the fstream
  void reopen();
  void resize(long long size);
};

class PrecompTmpFile: public WrappedFStream {
public:
  PrecompTmpFile();
  ~PrecompTmpFile() override;
};

class memiostream: public WrappedIOStream<std::iostream>
{
  class membuf : public std::streambuf
  {
  public:
    bool owns_ptr = false;
    char* data_ptr = nullptr;
    std::vector<char> memvector;
    std::vector<unsigned char> memvector_unsigned;

    membuf() = delete;
    explicit membuf(char* begin, char* end, bool owns_ptr_);
    explicit membuf(std::vector<char>&& memvector_);
    explicit membuf(std::vector<unsigned char>&& memvector_);
    virtual ~membuf();

    std::streambuf::pos_type seekoff(std::streambuf::off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which) override;
    std::streambuf::pos_type seekpos(std::streambuf::pos_type sp, std::ios_base::openmode which) override;
  };

  std::unique_ptr<membuf> m_buf;
  explicit memiostream(membuf* buf);

public:
  static std::unique_ptr<memiostream> make(std::vector<char>&& memvector);
  static std::unique_ptr<memiostream> make(std::vector<unsigned char>&& memvector);
  static std::unique_ptr<memiostream> make(unsigned char* begin, unsigned char* end, bool take_mem_ownership = false);
};

void fast_copy(IStreamLike& file1, OStreamLike& file2, long long bytecount);

bool read_with_memstream_buffer(IStreamLike& orig_input, std::unique_ptr<memiostream>& memstream_buf, char* target_buf, int minimum_gcount, long long& cur_pos);
// This makes a temporary stream for use as input, reusing checkbuf or copying from original_input to mem if the stream is small enough, or to a temp file if larger
// copy_to_temp is so you can use a custom way of copying to the temporary stream, in case you need to skip some data or something like that, if not provided, fast_copy will be used
std::unique_ptr<IStreamLike> make_temporary_stream(
  long long stream_pos, long long stream_size, std::span<unsigned char> checkbuf,
  IStreamLike& original_input, long long original_input_pos, std::string temp_filename,
  std::function<void(IStreamLike&, OStreamLike&)> copy_to_temp, long long max_memory_size
);

class CompressedOStreamBuffer;
constexpr auto CHUNK = 262144; // 256 KB buffersize

// compression-on-the-fly
enum { OTF_NONE = 0, OTF_BZIP2 = 1, OTF_XZ_MT = 2 }; // uncompressed, bzip2, lzma2 multithreaded

int lzma_max_memory_default();

class CompressedIStreamBuffer: public std::streambuf {
public:
  std::unique_ptr<std::istream> wrapped_istream;
  std::unique_ptr<char[]> otf_in;
  std::unique_ptr<char[]> otf_dec;

  explicit CompressedIStreamBuffer(std::unique_ptr<std::istream>&& istream);

  std::streambuf::pos_type seekoff(std::streambuf::off_type offset, std::ios_base::seekdir dir, std::ios_base::openmode mode) override;
};

// forward declare of bz_stream, but because it's a C typedef struct on implementation we will make a class that public inherits from it
class precomp_bz_stream;
class Bz2IStreamBuffer: public CompressedIStreamBuffer
{
public:
  std::unique_ptr<precomp_bz_stream> otf_bz2_stream_d;

  explicit Bz2IStreamBuffer(std::unique_ptr<std::istream>&& istream);

  static std::unique_ptr<std::istream> from_istream(std::unique_ptr<std::istream>&& istream);

  void init();

  ~Bz2IStreamBuffer() override;

  int underflow() override;
};

class precomp_lzma_stream;
class XzIStreamBuffer: public CompressedIStreamBuffer
{
public:
  std::unique_ptr<precomp_lzma_stream> otf_xz_stream_d;

  explicit XzIStreamBuffer(std::unique_ptr<std::istream>&& istream);

  static std::unique_ptr<std::istream> from_istream(std::unique_ptr<std::istream>&& istream);

  void init();

  ~XzIStreamBuffer() override;

  int underflow() override;
};

WrappedIStream wrap_istream_otf_compression(std::unique_ptr<std::istream>&& istream, int otf_compression_method, bool take_ownership);

class CompressedOStreamBuffer : public std::streambuf {
public:
  std::unique_ptr<std::ostream> wrapped_ostream;
  bool is_stream_eof = false;
  std::unique_ptr<char[]> otf_in;
  std::unique_ptr<char[]> otf_out;

  explicit CompressedOStreamBuffer(std::unique_ptr<std::ostream>&& wrapped_ostream);

  std::streambuf::pos_type seekoff(std::streambuf::off_type offset, std::ios_base::seekdir dir, std::ios_base::openmode mode) override;

  virtual int sync(bool final_byte) = 0;
  int sync() override;

  void set_stream_eof();

  int overflow(int c) override;
};

class Bz2OStreamBuffer : public CompressedOStreamBuffer
{
public:
  std::unique_ptr<precomp_bz_stream, std::function<void(precomp_bz_stream*)>> otf_bz2_stream_c;

  explicit Bz2OStreamBuffer(std::unique_ptr<std::ostream>&& wrapped_ostream);

  static std::unique_ptr<std::ostream> from_ostream(std::unique_ptr<std::ostream>&& ostream);

  void init();

  int sync(bool final_byte) override;
};

class XzOStreamBuffer : public CompressedOStreamBuffer
{
public:
  uint64_t compression_otf_max_memory;
  unsigned int compression_otf_thread_count;
  std::unique_ptr<precomp_lzma_stream, std::function<void(precomp_lzma_stream*)>> otf_xz_stream_c;
  std::unique_ptr<lzma_init_mt_extra_parameters> otf_xz_extra_params;

  XzOStreamBuffer(
    std::unique_ptr<std::ostream>&& wrapped_ostream,
    uint64_t compression_otf_max_memory,
    unsigned int compression_otf_thread_count,
    std::unique_ptr<lzma_init_mt_extra_parameters>&& otf_xz_extra_params
  );

  static std::unique_ptr<std::ostream> from_ostream(
    std::unique_ptr<std::ostream>&& ostream,
    std::unique_ptr<lzma_init_mt_extra_parameters>&& otf_xz_extra_params,
    uint64_t compression_otf_max_memory,
    unsigned int compression_otf_thread_count
  );

  void init(std::unique_ptr<lzma_init_mt_extra_parameters>&& otf_xz_extra_params);

  int sync(bool final_byte) override;
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
