#ifndef PRECOMP_IO_H
#define PRECOMP_IO_H

#include "../boost/uuid/detail/sha1.hpp"

#include <memory>
#include <fstream>
#include <functional>
#include <map>
#include <span>
#include <vector>
#include <optional>
#include <thread>
#include <mutex>
#include <condition_variable>

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
  virtual std::istream::int_type get() {
    unsigned char chr[1];
    read(reinterpret_cast<char*>(&chr[0]), 1);
    return gcount() == 1 ? chr[0] : EOF;
  };
  virtual std::streamsize gcount() = 0;
  virtual IStreamLike& seekg(std::istream::off_type offset, std::ios_base::seekdir dir) = 0;
  virtual std::istream::pos_type tellg() = 0;
};

class OStreamLike: public StreamLikeCommon {
public:
  virtual OStreamLike& write(const char* buf, std::streamsize count) = 0;
  virtual OStreamLike& put(char chr) {
    write(&chr, 1);
    return *this;
  };
  virtual void flush() {};
  virtual std::ostream::pos_type tellp() = 0;
  virtual OStreamLike& seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) = 0;
};

// GenericIStreamLike is an IStreamLike that can be whatever we need it to be, as its constructed with and uses arbitrary functions using an arbitrary backing structure.
// It's meant to allow ultimate and unlimited flexibility. Where are we reading from? Memory? A socket? A file? Some other type of handle? Data piped from other process?
// Only who constructed the instance knows! What is the difference from just implementing IStreamLike? That we can expose a function to create these from plain C!
class GenericIStreamLike: public IStreamLike {
  void* backing_structure;
  size_t _gcount = 0;
  bool _bad = false;

  std::function<size_t(void*, char*, long long)> read_func;
  std::function<int(void*)> get_func;
  std::function<int(void*, long long, int)> seekg_func;
  std::function<long long(void*)> tellg_func;

  std::function<bool(void*)> eof_func;
  std::function<bool(void*)> bad_func;
  std::function<void(void*)> clear_func;

public:
  GenericIStreamLike(
    void* backing_structure_,
    std::function<size_t(void*, char*, long long)> read_func_,
    std::function<int(void*)> get_func_,
    std::function<int(void*, long long, int)> seekg_func_,
    std::function<long long(void*)> tellg_func_,

    std::function<bool(void*)> eof_func_,
    std::function<bool(void*)> bad_func_,
    std::function<void(void*)> clear_func_
  );

  GenericIStreamLike& read(char* buff, std::streamsize count) override;
  std::istream::int_type get() override;
  std::streamsize gcount() override;
  GenericIStreamLike& seekg(std::istream::off_type offset, std::ios_base::seekdir dir) override;
  std::istream::pos_type tellg() override;

  bool eof() override;
  bool bad() override;
  bool good() override;
  void clear() override;
};

// Analogue of GenericIStreamLike for OStreamLike
class GenericOStreamLike: public OStreamLike {
  void* backing_structure;
  bool _bad = false;

  std::function<size_t(void*, char const*, long long)> write_func;
  std::function<int(void*, int)> put_func;
  std::function<void(void*)> flush_func;
  std::function<long long(void*)> tellp_func;
  std::function<int(void*, long long, int)> seekp_func;

  std::function<bool(void*)> eof_func;
  std::function<bool(void*)> bad_func;
  std::function<void(void*)> clear_func;

public:
  GenericOStreamLike(
    void* backing_structure_,
    std::function<size_t(void*, char const*, long long)> write_func_,
    std::function<int(void*, int)> put_func_,
    std::function<long long(void*)> tellp_func_,
    std::function<int(void*, long long, int)> seekp_func_,

    std::function<bool(void*)> eof_func_,
    std::function<bool(void*)> bad_func_,
    std::function<void(void*)> clear_func_
  );

  GenericOStreamLike& write(const char* buf, std::streamsize count) override;
  GenericOStreamLike& put(char chr) override;
  void flush() override;
  std::ostream::pos_type tellp() override;
  GenericOStreamLike& seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) override;

  bool eof() override;
  bool bad() override;
  bool good() override;
  void clear() override;
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
  std::streamsize _gcount = 0;

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

class memiostream: public WrappedIOStream<std::iostream> {
  class membuf : public std::streambuf
  {
  public:
    bool owns_ptr = false;
    char* data_ptr = nullptr;
    std::vector<unsigned char> memvector_unsigned;

    membuf() = delete;
    explicit membuf(char* begin, char* end, bool owns_ptr_);
    explicit membuf(std::vector<unsigned char>&& memvector_);
    ~membuf() override;

    std::streambuf::pos_type seekoff(std::streambuf::off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which) override;
    std::streambuf::pos_type seekpos(std::streambuf::pos_type sp, std::ios_base::openmode which) override;
  };

  std::unique_ptr<membuf> m_buf;
  explicit memiostream(membuf* buf);

public:
  explicit memiostream();
  explicit memiostream(unsigned char* begin, unsigned char* end, bool take_mem_ownership = false);
  explicit memiostream(std::vector<unsigned char>&& memvector);
};

class MemVecIOStream: public IStreamLike, public OStreamLike {
  std::vector<std::byte> memvec {};

  uint64_t pos = 0;
  uint64_t _gcount = 0;

public:
  explicit MemVecIOStream() {}

  const std::vector<std::byte>& vector() { return memvec; }

  MemVecIOStream& read(char* buff, std::streamsize count) override {
    if (memvec.empty() || pos > memvec.size()) {
      _gcount = 0;
      return *this;
    }
    const auto to_read = std::min<uint64_t>(count, memvec.size() - pos);
    std::copy_n(reinterpret_cast<char*>(memvec.data() + pos), to_read, buff);
    _gcount = to_read;
    pos += to_read;
    return *this;
  }
  std::istream::int_type get() override {
    char buff[1];
    read(buff, 1);
    return _gcount == 1 ? buff[0] : EOF;
  }

  // Write is more permissive, if trying to write past the end of the vector we just resize it to fit the new data
  MemVecIOStream& write(const char* buf, std::streamsize count) override {
    const auto new_pos = pos + count;
    if (new_pos >= memvec.size()) {
      memvec.resize(new_pos);
    }
    std::copy_n(buf, count, reinterpret_cast<char*>(memvec.data() + pos));
    pos += count;
    return *this;
  }

  std::istream::pos_type tellg() override { return pos; }
  std::istream::pos_type tellp() override { return pos; }

  MemVecIOStream& seekg(std::istream::off_type offset, std::ios_base::seekdir dir) override {
    uint64_t new_pos;
    switch (dir) {
    case std::ios_base::end:
      {
      new_pos = pos - offset;
      new_pos = new_pos >= 0 ? new_pos : 0;
      break;
      }
    case std::ios_base::cur:
      {
      new_pos = pos + offset;
      break;
      }
    case std::ios_base::beg:
      {
      new_pos = offset;
      break;
      }
    }
    pos = new_pos;
    return *this;
  }
  OStreamLike& seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) override {
    seekg(offset, dir);
    return *this;
  }

  void reset() {
    pos = 0;
    _gcount = 0;
    memvec.clear();
  }

  std::streamsize gcount() override { return _gcount; }
  bool eof() override {
    return pos >= memvec.size();
  }
  bool good() override { return true; }
  bool bad() override { return false; }
  void clear() override {}
};

void fast_copy(IStreamLike& file1, OStreamLike& file2, long long bytecount);

void dump_to_file(IStreamLike& istream, std::string filename, long long bytecount);

bool read_with_memstream_buffer(IStreamLike& orig_input, std::unique_ptr<memiostream>& memstream_buf, char* target_buf, int minimum_gcount, long long& cur_pos);
// This makes a temporary stream for use as input, reusing checkbuf or copying from original_input to mem if the stream is small enough, or to a temp file if larger
// copy_to_temp is so you can use a custom way of copying to the temporary stream, in case you need to skip some data or something like that, if not provided, fast_copy will be used
std::unique_ptr<IStreamLike> make_temporary_stream(
  long long stream_pos, long long stream_size, std::span<unsigned char> checkbuf,
  IStreamLike& original_input, long long original_input_pos, std::string temp_filename,
  std::function<void(IStreamLike&, OStreamLike&)> copy_to_temp, long long max_memory_size
);

constexpr auto CHUNK = 262144; // 256 KB buffersize

std::string calculate_sha1(IStreamLike& file1, unsigned int pos1);

// This Ostream will process any written data to compute a SHA1 digest, but otherwise discards any data that goes through it, useful for data verification purposes
class Sha1Ostream : public OStreamLike {
    boost::uuids::detail::sha1 s;
    uint64_t dataLength = 0;
public:
    Sha1Ostream& write(const char* buf, std::streamsize count) override;
    Sha1Ostream& put(char chr) override;
    void flush() override {};
    std::ostream::pos_type tellp() override;
    Sha1Ostream& seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) override;

    bool eof() override { return false; }
    bool good() override { return true; }
    bool bad() override { return false; }
    void clear() override {}

    std::string get_digest();
};

// A type of IStreamLike that assumes it will wrap a non-seekable IStreamLike so it buffers all the read data so that it can seek back even though
// it would normally be impossible.
// It requires the implementation of the set_new_buffer_start_pos, which should be called as soon as you know you are completely done with some data.
class IBufferedIStream : public IStreamLike {
public:
  // Discards any data before the new start pos, also sets the current pos to this new position
  virtual void set_new_buffer_start_pos(uint64_t new_buffer_start_pos) = 0;
};

// In case you have a seekable IStreamLike but want to use it where a IBufferedIStream is required. This just wraps the istream and ignores set_new_buffer_start_pos.
// Seems to defeat the point but this allows code to just assume you get a IBufferedIStream and properly call set_new_buffer_start_pos when needed without having
// to check for anything.
class FakeBufferedIStream : public IBufferedIStream {
  IStreamLike* istream;
public:
  FakeBufferedIStream() = delete;
  explicit FakeBufferedIStream(IStreamLike* istream_) : istream(istream_) {}

  void set_new_buffer_start_pos(uint64_t new_buffer_start_pos) override {}
  FakeBufferedIStream& read(char* buff, std::streamsize count) override {
    istream->read(buff, count);
    return *this;
  }
  std::streamsize gcount() override { return istream->gcount(); }
  std::istream::pos_type tellg() override { return istream->tellg(); }
  FakeBufferedIStream& seekg(std::istream::off_type offset, std::ios_base::seekdir dir) override {
    istream->seekg(offset, dir);
    return *this;
  }
  bool eof() override { return istream->eof(); }
  bool good() override { return istream->good(); }
  bool bad() override { return istream->bad(); }
  void clear() override { istream->clear(); }
};

class BufferedIStream: public IBufferedIStream {
  IStreamLike* istream;
  PrecompTmpFile buffer;
  uint64_t buffer_size = 0;
  uint64_t buffer_start_pos = 0;
  uint64_t current_pos = 0;
  std::streamsize gcount_ = 0;
  std::optional<uint64_t> istream_eof_pos = std::nullopt;

  std::string temporary_file_name;

  void ensure_buffer_is_open() {
    if (buffer.is_open()) return;
    buffer.open(temporary_file_name, std::ios_base::in | std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
  }
public:
  BufferedIStream() = delete;
  explicit BufferedIStream(IStreamLike* istream_, std::string temporary_file_name_): istream(istream_), temporary_file_name(std::move(temporary_file_name_)) {}

  void set_new_buffer_start_pos(uint64_t new_buffer_start_pos) override;

  BufferedIStream& read(char* buff, std::streamsize count) override;
  std::streamsize gcount() override { return gcount_; }

  std::istream::pos_type tellg() override { return current_pos; }
  BufferedIStream& seekg(std::istream::off_type offset, std::ios_base::seekdir dir) override;

  bool eof() override { return istream_eof_pos.has_value() && istream_eof_pos.value() == current_pos; }
  bool good() override { return istream->good(); }
  bool bad() override { return istream->bad(); }
  void clear() override { istream->clear(); }
};

#ifdef DEBUG
/*
 * The purpose of this class is to allow debugging when developing a new type of ISteamLike (most likely by a consumer with a GenericIStreamLike) by comparing
 * it's behavior against a known good IStreamLike.
 * The easiest way would be dumping the data to be read into a file and using an FILEIStream as the known good, and whatever IStreamLike you want as the tested one.
 * If your tested IStreamLike at any point reads different data than the known good, reaches or fails to flag EOF before the known good, or otherwise deviates in
 * behavior, this class will throw an exception making it clear when things start to go wrong.
 */
class DebugComparatorIStreamLike : public IStreamLike {
public:
  std::unique_ptr<IStreamLike> known_good;
  std::unique_ptr<IStreamLike> test_stream;

  explicit DebugComparatorIStreamLike(std::unique_ptr<IStreamLike>&& known_good_, std::unique_ptr<IStreamLike>&& test_stream_):
    known_good(std::move(known_good_)), test_stream(std::move(test_stream_)) {}

  void compare_status();

  IStreamLike& read(char* buff, std::streamsize count) override;

  std::istream::int_type get() override;

  std::streamsize gcount() override;

  IStreamLike& seekg(std::istream::off_type offset, std::ios_base::seekdir dir) override;

  std::istream::pos_type tellg() override;
  bool eof() override;
  bool good() override;
  bool bad() override;
  void clear() override;
};
#endif

#endif // PRECOMP_IO_H
