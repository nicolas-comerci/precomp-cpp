#include "precomp_io.h"

#include <array>

#include "precomp_utils.h"

#include <filesystem>
#include <memory>

size_t ostream_printf(OStreamLike& out, const std::string& str) {
  for (char character : str) {
    out.put(character);
    if (out.bad()) return 0;
  }
  return str.length();
}

size_t ostream_printf(std::ostream& out, const std::string& str) {
  WrappedOStream wrapped_out = WrappedOStream(&out, false);
  return ostream_printf(wrapped_out, str);
}

void fseek64(FILE* file_ptr, unsigned long long pos, std::ios_base::seekdir dir) {
#if defined(_MSC_VER)
  _fseeki64(file_ptr, pos, dir);
#elif !defined(__unix)
  const fpos_t fpt_pos = pos;
  fsetpos(file_ptr, &fpt_pos);
#else
  fseeko(file_ptr, pos, dir);
#endif
}

WrappedOStream::WrappedOStream(std::ostream* stream, bool take_ownership) : WrappedStream(stream, take_ownership) { }
WrappedOStream::~WrappedOStream() = default;
bool WrappedOStream::eof() { return WrappedStream::eof(); }
bool WrappedOStream::good() { return WrappedStream::good(); }
bool WrappedOStream::bad() { return WrappedStream::bad(); }
void WrappedOStream::clear() { WrappedStream::clear(); }
WrappedOStream& WrappedOStream::write(const char* buf, std::streamsize count) {
  wrapped_stream->write(buf, count);
  return *this;
}
WrappedOStream& WrappedOStream::put(char chr) {
  wrapped_stream->put(chr);
  return *this;
}
void WrappedOStream::flush() { wrapped_stream->flush(); }
std::ostream::pos_type WrappedOStream::tellp() { return wrapped_stream->tellp(); }
WrappedOStream& WrappedOStream::seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) {
  wrapped_stream->seekp(offset, dir);
  return *this;
}

FILEOStream::FILEOStream(FILE* file, bool take_ownership) : file_ptr(file), take_ownership(take_ownership) { }
FILEOStream::~FILEOStream() {
  if (!take_ownership) return;
  std::fclose(file_ptr);
}
bool FILEOStream::eof() { return std::feof(file_ptr); }
bool FILEOStream::good() { return !std::feof(file_ptr) && !std::ferror(file_ptr); }
bool FILEOStream::bad() { return std::ferror(file_ptr); }
void FILEOStream::clear() { std::clearerr(file_ptr); }
FILEOStream& FILEOStream::write(const char* buf, std::streamsize count) {
  std::fwrite(buf, sizeof(const char), count, file_ptr);
  return *this;
}
FILEOStream& FILEOStream::put(char chr) {
  std::fputc(chr, file_ptr);
  return *this;
}
void FILEOStream::flush() { std::fflush(file_ptr); }
std::ostream::pos_type FILEOStream::tellp() { return std::ftell(file_ptr); }
FILEOStream& FILEOStream::seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) {
  fseek64(file_ptr, offset, dir);
  return *this;
}

FILEIStream::FILEIStream(FILE* file, bool take_ownership) : file_ptr(file), take_ownership(take_ownership), _gcount(0) { }
FILEIStream::~FILEIStream() {
  if (!take_ownership) return;
  std::fclose(file_ptr);
}
bool FILEIStream::eof() { return std::feof(file_ptr); }
bool FILEIStream::good() { return !std::feof(file_ptr) && !std::ferror(file_ptr); }
bool FILEIStream::bad() { return std::ferror(file_ptr); }
void FILEIStream::clear() { std::clearerr(file_ptr); }
FILEIStream& FILEIStream::read(char* buff, std::streamsize count) {
  _gcount = std::fread(buff, sizeof(char), count, file_ptr);
  return *this;
}
std::istream::int_type FILEIStream::get() {
  _gcount = 1;
  auto chr = std::fgetc(file_ptr);
  if (chr == EOF) {
    chr = std::istream::traits_type::eof();
  }
  return chr;
}
std::streamsize FILEIStream::gcount() { return _gcount; }
FILEIStream& FILEIStream::seekg(std::istream::off_type offset, std::ios_base::seekdir dir) {
  fseek64(file_ptr, offset, dir);
  return *this;
}
std::istream::pos_type FILEIStream::tellg() { return std::ftell(file_ptr); }

WrappedIStream::WrappedIStream(std::istream* stream, bool take_ownership) : WrappedStream(stream, take_ownership) { }
bool WrappedIStream::eof() { return WrappedStream::eof(); }
bool WrappedIStream::good() { return WrappedStream::good(); }
bool WrappedIStream::bad() { return WrappedStream::bad(); }
void WrappedIStream::clear() { WrappedStream::clear(); }
WrappedIStream& WrappedIStream::read(char* buff, std::streamsize count) {
  wrapped_stream->read(buff, count);
  return *this;
}
std::istream::int_type WrappedIStream::get() { return wrapped_stream->get(); }
std::streamsize WrappedIStream::gcount() { return wrapped_stream->gcount(); }
WrappedIStream& WrappedIStream::seekg(std::istream::off_type offset, std::ios_base::seekdir dir) {
  // istreams don't allow seeking once eof/failbit is set, which happens if we read a file to the end.
  // This behaves more like std::fseek by just clearing the eof and failbit to allow the seek operation to happen.
  if (bad()) {
    throw std::runtime_error(make_cstyle_format_string("Input stream went bad"));
  }
  clear();
  wrapped_stream->seekg(offset, dir);
  return *this;
}
std::istream::pos_type WrappedIStream::tellg() { return wrapped_stream->tellg(); }

IStreamLikeView::IStreamLikeView(IStreamLike* istream_, long long final_allowed_stream_pos_) : istream(istream_), final_allowed_stream_pos(final_allowed_stream_pos_) {
  starting_stream_pos = istream->tellg();
  current_stream_pos = starting_stream_pos;
}
IStreamLikeView& IStreamLikeView::read(char* buff, std::streamsize count) {
  if (_eof) return *this;

  std::streamsize effective_count = count;
  if (current_stream_pos + count > final_allowed_stream_pos) {
    effective_count = final_allowed_stream_pos - current_stream_pos;
    _eof = true;
  }

  istream->read(buff, effective_count);
  long long amt_read = istream->gcount();
  if (amt_read < effective_count) _eof = true;
  current_stream_pos += amt_read;

  return *this;
}
std::istream::int_type IStreamLikeView::get() {
  unsigned char chr[1];
  read(reinterpret_cast<char*>(&chr[0]), 1);
  return istream->gcount() == 1 ? chr[0] : EOF;
}
std::streamsize IStreamLikeView::gcount() { return istream->gcount(); }
std::istream::pos_type IStreamLikeView::tellg() { return current_stream_pos - starting_stream_pos; }
bool IStreamLikeView::eof() { return _eof; }
bool IStreamLikeView::good() { return istream->good() && !eof(); }
bool IStreamLikeView::bad() { return istream->bad(); }
void IStreamLikeView::clear() {
  _eof = false;
  istream->clear();
}
IStreamLikeView& IStreamLikeView::seekg(std::istream::off_type offset, std::ios_base::seekdir dir) {
  istream->seekg(offset, dir);
  if (dir == std::ios_base::beg) {
    current_stream_pos = offset;
  }
  else if (dir == std::ios_base::end) {
    current_stream_pos = final_allowed_stream_pos - offset;
  }
  else {
    current_stream_pos += offset;
  }
  return *this;
}

void ObservableStreamBase::register_observer(observable_methods method, std::function<void()> callback) {
  method_observers[method] = callback;
}
void ObservableStreamBase::notify_observer(observable_methods method) {
  auto observer_callback = method_observers[method];
  if (observer_callback) observer_callback();
}

ObservableOStream& ObservableOStream::write(const char* buf, std::streamsize count) {
  internal_write(buf, count);
  notify_observer(write_method);
  return *this;
}
ObservableOStream& ObservableOStream::put(char chr) {
  internal_put(chr);
  notify_observer(put_method);
  return *this;
}
void ObservableOStream::flush() {
  internal_flush();
  notify_observer(flush_method);
}
std::ostream::pos_type ObservableOStream::tellp() {
  notify_observer(tellp_method);
  return internal_tellp();
}
ObservableOStream& ObservableOStream::seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) {
  internal_seekp(offset, dir);
  notify_observer(seekp_method);
  return *this;
}

WrappedFStream::WrappedFStream(): WrappedIOStream() { }
void WrappedFStream::open(std::string file_path, std::ios_base::openmode mode) {
  this->file_path = file_path;
  this->mode = mode;
  wrapped_iostream->open(file_path, mode);
}
bool WrappedFStream::is_open() { return wrapped_iostream->is_open(); }
void WrappedFStream::close() { wrapped_iostream->close(); }

void WrappedFStream::reopen() {
  if (wrapped_iostream->is_open()) wrapped_iostream->close();
  open(file_path, mode);
}
void WrappedFStream::resize(long long size) {
  if (wrapped_iostream->is_open()) wrapped_iostream->close();
  std::filesystem::resize_file(file_path, size);
  reopen();
}

PrecompTmpFile::PrecompTmpFile() : WrappedFStream() {}
PrecompTmpFile::~PrecompTmpFile() {
  WrappedFStream::close();
  std::remove(file_path.c_str());
}

memiostream::membuf::membuf(std::vector<char>&& memvector_): memvector(std::move(memvector_)) {
  this->setg(memvector.data(), memvector.data(), memvector.data() + memvector.size());
  this->setp(memvector.data(), memvector.data() + memvector.size());
}
memiostream::membuf::membuf(std::vector<unsigned char>&& memvector_) : memvector_unsigned(std::move(memvector_)) {
  auto data_ptr = reinterpret_cast<char*>(memvector_unsigned.data());
  this->setg(data_ptr, data_ptr, data_ptr + memvector_unsigned.size());
  this->setp(data_ptr, data_ptr + memvector_unsigned.size());
}
memiostream::membuf::membuf(char* begin, char* end, bool owns_ptr_): data_ptr(begin), owns_ptr(owns_ptr_) {
  this->setg(begin, begin, end);
  this->setp(begin, end);
}
memiostream::membuf::~membuf() {
  if (owns_ptr) delete[] data_ptr;
}
std::streambuf::pos_type memiostream::membuf::seekoff(std::streambuf::off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which) {
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
std::streambuf::pos_type memiostream::membuf::seekpos(std::streambuf::pos_type sp, std::ios_base::openmode which) {
  return seekoff(sp - pos_type(off_type(0)), std::ios_base::beg, which);
}

memiostream::memiostream(membuf* buf): WrappedIOStream(buf), m_buf(std::unique_ptr<membuf>(buf)) {}
std::unique_ptr<memiostream> memiostream::make(std::vector<char>&& memvector) {
  auto membuf_ptr = new membuf(std::move(memvector));
  return std::unique_ptr<memiostream>(new memiostream(membuf_ptr));
}
std::unique_ptr<memiostream> memiostream::make(std::vector<unsigned char>&& memvector) {
  auto membuf_ptr = new membuf(std::move(memvector));
  return std::unique_ptr<memiostream>(new memiostream(membuf_ptr));
}
std::unique_ptr<memiostream> memiostream::make(unsigned char* begin, unsigned char* end, bool take_mem_ownership) {
  auto membuf_ptr = new membuf(reinterpret_cast<char*>(begin), reinterpret_cast<char*>(end), take_mem_ownership);
  return std::unique_ptr<memiostream>(new memiostream(membuf_ptr));
}

void fast_copy(IStreamLike& file1, OStreamLike& file2, long long bytecount) {
  constexpr auto COPY_BUF_SIZE = 512;
  if (bytecount == 0) return;

  long long i;
  auto remaining_bytes = (bytecount % COPY_BUF_SIZE);
  long long maxi = (bytecount / COPY_BUF_SIZE);
  std::array<unsigned char, COPY_BUF_SIZE> copybuf{};

  for (i = 1; i <= maxi; i++) {
    file1.read(reinterpret_cast<char*>(copybuf.data()), COPY_BUF_SIZE);
    file2.write(reinterpret_cast<char*>(copybuf.data()), COPY_BUF_SIZE);
  }
  if (remaining_bytes != 0) {
    file1.read(reinterpret_cast<char*>(copybuf.data()), remaining_bytes);
    file2.write(reinterpret_cast<char*>(copybuf.data()), remaining_bytes);
  }
}

void dump_to_file(IStreamLike& istream, std::string filename, long long bytecount) {
  WrappedFStream ftempout;
  ftempout.open(filename, std::ios_base::out | std::ios_base::binary);
  fast_copy(istream, ftempout, bytecount);
  ftempout.close();
}

bool read_with_memstream_buffer(IStreamLike& orig_input, std::unique_ptr<memiostream>& memstream_buf, char* target_buf, int minimum_gcount, long long& cur_pos) {
  memstream_buf->read(target_buf, minimum_gcount);
  if (memstream_buf->gcount() == minimum_gcount) {
    cur_pos += minimum_gcount;
    return true;
  }

  // if we couldn't read as much as we wanted from the memstream we attempt to read more from the input stream and reattempt
  orig_input.seekg(cur_pos, std::ios_base::beg);
  std::vector<char> new_read_data{};
  new_read_data.resize(CHUNK);
  orig_input.read(new_read_data.data(), CHUNK);
  auto read_amt = orig_input.gcount();
  if (read_amt < minimum_gcount) return false;
  if (read_amt < CHUNK) new_read_data.resize(read_amt);  // shrink to fit data, needed because memiostream relies on the vector's size

  memstream_buf = memiostream::make(std::move(new_read_data));
  memstream_buf->read(target_buf, minimum_gcount);
  if (memstream_buf->gcount() == minimum_gcount) cur_pos += minimum_gcount;
  return memstream_buf->gcount() == minimum_gcount;  // if somehow that fails again we fail and return false, else true for success
}

std::unique_ptr<IStreamLike> make_temporary_stream(
  long long stream_pos, long long stream_size, std::span<unsigned char> checkbuf,
  IStreamLike& original_input, long long original_input_pos, std::string temp_filename,
  std::function<void(IStreamLike&, OStreamLike&)> copy_to_temp, long long max_memory_size
) {
  std::unique_ptr<IStreamLike> temp_png;
  if (stream_size < max_memory_size) {  // File small enough, will use it from memory
    std::vector<char> mem{};
    mem.resize(stream_size);
    auto mem_png = memiostream::make(std::move(mem));
    if (stream_size < checkbuf.size()) {  // The whole file did fit the checkbuf, we can copy it from there
      auto checkbuf_stream = memiostream::make(checkbuf.data(), checkbuf.data() + checkbuf.size());
      checkbuf_stream->seekg(stream_pos - original_input_pos, std::ios_base::beg);
      copy_to_temp(*checkbuf_stream, *mem_png);
    }
    else {  // just copy it from the input file
      copy_to_temp(original_input, *mem_png);
    }
    temp_png = std::move(mem_png);
  }
  else {  // too large, use temporary file
    auto tmp_png = std::make_unique<PrecompTmpFile>();
    tmp_png->open(temp_filename, std::ios_base::in | std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
    copy_to_temp(original_input, *tmp_png);
    temp_png = std::move(tmp_png);
  }
  return temp_png;
}
