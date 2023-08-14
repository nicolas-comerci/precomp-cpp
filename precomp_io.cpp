#include "precomp_io.h"
#include "precomp_utils.h"

#include <array>
#include <filesystem>
#include <memory>

std::string get_sha1_hash(boost::uuids::detail::sha1& s) {
    unsigned int hash[5];
    s.get_digest(hash);
    
    // Back to string
    char buf[41] = { 0 };

    for (int i = 0; i < 5; i++)
    {
        std::sprintf(buf + (i << 3), "%08x", hash[i]);
    }

    return std::string(buf);
}

std::string calculate_sha1(IStreamLike& file1, unsigned int pos1) {
    std::vector<unsigned char> input_bytes1;
    input_bytes1.resize(CHUNK);
    long long size1;
    boost::uuids::detail::sha1 s;

    file1.seekg(pos1, std::ios_base::beg);

    do {
        file1.read(reinterpret_cast<char*>(input_bytes1.data()), CHUNK);
        size1 = file1.gcount();
        s.process_bytes(input_bytes1.data(), size1);
    } while (size1 == CHUNK);

    return get_sha1_hash(s);
}

Sha1Ostream& Sha1Ostream::write(const char* buf, std::streamsize count) {
    s.process_bytes(reinterpret_cast<const unsigned char*>(buf), count);
    dataLength += count;
    return *this;
}

Sha1Ostream& Sha1Ostream::put(char chr) {
    char buf[1];
    buf[0] = chr;
    write(buf, 1);
    return *this;
}

std::ostream::pos_type Sha1Ostream::tellp() {
    return dataLength;
}

Sha1Ostream& Sha1Ostream::seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) {
    throw std::runtime_error("Can't seek on Sha1Ostream");
}

std::string Sha1Ostream::get_digest() {
    return get_sha1_hash(s);
}

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

GenericIStreamLike::GenericIStreamLike(
  void* backing_structure_,
  std::function<size_t(void*, char*, long long)> read_func_,
  std::function<int(void*)> get_func_,
  std::function<int(void*, long long, int)> seekg_func_,
  std::function<long long(void*)> tellg_func_,

  std::function<bool(void*)> eof_func_,
  std::function<bool(void*)> bad_func_,
  std::function<void(void*)> clear_func_
) : backing_structure(backing_structure_), read_func(std::move(read_func_)), get_func(std::move(get_func_)), seekg_func(std::move(seekg_func_)),
tellg_func(std::move(tellg_func_)), eof_func(std::move(eof_func_)), bad_func(std::move(bad_func_)), clear_func(std::move(clear_func_)) {}
GenericIStreamLike& GenericIStreamLike::read(char* buff, std::streamsize count) {
  _gcount = read_func(backing_structure, buff, count);
  return *this;
}
std::istream::int_type GenericIStreamLike::get() {
  auto chr = get_func(backing_structure);
  _gcount = chr != EOF ? 1 : 0;
  return chr;
}
std::streamsize GenericIStreamLike::gcount() { return _gcount; }
GenericIStreamLike& GenericIStreamLike::seekg(std::istream::off_type offset, std::ios_base::seekdir dir) {
  auto result = seekg_func(backing_structure, offset, dir);
  if (result != 0) _bad = true;
  return *this;
}
std::istream::pos_type GenericIStreamLike::tellg() { return tellg_func(backing_structure); }
bool GenericIStreamLike::eof() { return eof_func(backing_structure); }
bool GenericIStreamLike::bad() {
  if (bad_func(backing_structure)) _bad = true;
  return _bad;
}
bool GenericIStreamLike::good() { return !eof() && !bad(); }
void GenericIStreamLike::clear() {
  _bad = false;
  clear_func(backing_structure);
}

GenericOStreamLike::GenericOStreamLike(
  void* backing_structure_,
  std::function<size_t(void*, char const*, long long)> write_func_,
  std::function<int(void*, int)> put_func_,
  std::function<long long(void*)> tellp_func_,
  std::function<int(void*, long long, int)> seekp_func_,

  std::function<bool(void*)> eof_func_,
  std::function<bool(void*)> bad_func_,
  std::function<void(void*)> clear_func_
) : backing_structure(backing_structure_), write_func(std::move(write_func_)), put_func(std::move(put_func_)),
tellp_func(std::move(tellp_func_)), seekp_func(std::move(seekp_func_)),
eof_func(std::move(eof_func_)), bad_func(std::move(bad_func_)), clear_func(std::move(clear_func_)) {}
GenericOStreamLike& GenericOStreamLike::write(const char* buf, std::streamsize count) {
  write_func(backing_structure, buf, count);
  return *this;
}
GenericOStreamLike& GenericOStreamLike::put(char chr) {
  put_func(backing_structure, chr);
  return *this;
}
void GenericOStreamLike::flush() { flush_func(backing_structure); }
std::ostream::pos_type GenericOStreamLike::tellp() { return tellp_func(backing_structure); }
GenericOStreamLike& GenericOStreamLike::seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) {
  auto result = seekp_func(backing_structure, offset, dir);
  if (result != 0) _bad = true;
  return *this;
}
bool GenericOStreamLike::eof() { return eof_func(backing_structure); }
bool GenericOStreamLike::bad() {
  if (bad_func(backing_structure)) _bad = true;
  return _bad;
}
bool GenericOStreamLike::good() { return !eof() && !bad(); }
void GenericOStreamLike::clear() {
  _bad = false;
  clear_func(backing_structure);
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
  auto chr = std::fgetc(file_ptr);
  if (chr == EOF) {
    _gcount = 0;
    chr = std::istream::traits_type::eof();
  }
  else {
    _gcount = 1;
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
  if (_eof) {
    _gcount = 0;
    return *this;
  }

  std::streamsize effective_count = count;
  if (current_stream_pos + count > final_allowed_stream_pos) {
    effective_count = final_allowed_stream_pos - current_stream_pos;
    _eof = true;
  }

  istream->read(buff, effective_count);
  _gcount = istream->gcount();
  if (_gcount < effective_count) _eof = true;
  current_stream_pos += _gcount;

  return *this;
}
std::istream::int_type IStreamLikeView::get() {
  unsigned char chr[1];
  read(reinterpret_cast<char*>(&chr[0]), 1);
  return _gcount == 1 ? chr[0] : EOF;
}
std::streamsize IStreamLikeView::gcount() { return _gcount; }
std::istream::pos_type IStreamLikeView::tellg() { return _eof ? -1 : current_stream_pos - starting_stream_pos; }
bool IStreamLikeView::eof() { return _eof; }
bool IStreamLikeView::good() { return istream->good() && !eof(); }
bool IStreamLikeView::bad() { return istream->bad(); }
void IStreamLikeView::clear() {
  _eof = false;
  istream->clear();
}
IStreamLikeView& IStreamLikeView::seekg(std::istream::off_type offset, std::ios_base::seekdir dir) {
  if (bad()) {
    throw std::runtime_error(make_cstyle_format_string("Input stream went bad"));
  }
  clear();
  if (dir == std::ios_base::beg) {
    current_stream_pos = starting_stream_pos + offset;
  }
  else if (dir == std::ios_base::end) {
    current_stream_pos = final_allowed_stream_pos - offset;
  }
  else {
    current_stream_pos += offset;
  }
  istream->seekg(current_stream_pos, std::ios_base::beg);
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
  auto new_gptr = gptr();
  auto new_pptr = pptr();
  if (dir == std::ios_base::cur) {
    if (which & std::ios_base::in) new_gptr += off;
    if (which & std::ios_base::out) new_pptr += off;
  }
  else if (dir == std::ios_base::end) {
    if (which & std::ios_base::in) new_gptr = egptr() + off;
    if (which & std::ios_base::out) new_pptr = epptr() + off;
  }
  else if (dir == std::ios_base::beg) {
    if (which & std::ios_base::in) new_gptr = eback() + off;
    if (which & std::ios_base::out) new_pptr = pbase() + off;
  }

  if (new_gptr < eback() || new_gptr > egptr() || new_pptr < pbase() || new_pptr > epptr()) return pos_type(off_type(-1));
  if (which & std::ios_base::in) {
    setg(eback(), new_gptr, egptr());
    auto newoff = gptr() - eback();
    return newoff;
  }
  else {
    setp(pbase(), epptr());
    pbump(new_pptr - pbase());
    auto newoff = pptr() - pbase();
    return newoff;
  }
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
    if (!checkbuf.empty() && stream_size < checkbuf.size()) {  // The whole file did fit the checkbuf, we can copy it from there
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

void PasstroughStream::unlock_everything() {
    // Any locked thread that wakes up after EOF should not lock again on read/write, and should hopefully check eof() and realize it can't continue and fail gracefully
    write_eof = true;
    read_eof = true;
    data_needed_cv.notify_all();
    data_available_cv.notify_all();
}

PasstroughStream::PasstroughStream(std::function<void(OStreamLike&)> func, unsigned int _buffer_size) : buffer_size(_buffer_size), owner_thread_id(std::this_thread::get_id()) {
    buffer.reserve(buffer_size);

    thread = std::thread([this, func] {
        try {
            func(*this);
        }
        catch (std::exception& e) {
            thread_error = e;
        }
        unlock_everything();
    });
}

PasstroughStream::~PasstroughStream() {
    unlock_everything();
    if (thread.joinable()) thread.join();
}

void PasstroughStream::wait_thread_completed() {
    unlock_everything();
    if (thread.joinable()) thread.join();
    if (thread_error.has_value()) throw thread_error.value();
}

PasstroughStream& PasstroughStream::read(char* buff, std::streamsize count) {
    if (read_eof) {
        if (std::this_thread::get_id() != owner_thread_id) throw std::runtime_error("PasstroughStream somehow exhausted by non owning thread");
        _gcount = 0;
        return *this;
    }
    //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: TAKING LOCK FOR READ!\n\n", static_cast<void*>(this));
    std::unique_lock lock(mtx);
    //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: LOCK TAKEN FOR READ!\n\n", static_cast<void*>(this));
    std::streamsize already_read_count = 0;

    while (already_read_count < count) {
        std::streamsize remaining_count = count - already_read_count;
        std::streamsize iteration_read_count = data_available() >= remaining_count ? remaining_count : data_available();
        if (iteration_read_count == 0) {
            // If we don't have any data in the buffer we need to wait for more
            // unless we already reached the write_eof, in which case we know no more data will ever arrive
            // or if read_eof is true, which given that we were already in the middle of reading it probably means the stream was forcefully truncated (most likely to destroy it)
            if (read_eof || write_eof) { break; }

            data_needed_cv.notify_one();
            //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: WAITING FOR DATA AVAILABLE FOR READ!\n\n", static_cast<void*>(this));
            data_available_cv.wait(lock);
            //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: WOKE UP FROM WAITING FOR DATA AVAILABLE FOR READ!\n\n", static_cast<void*>(this));

            // now that we are back from getting more data, recalculate the iteration_read_count
            iteration_read_count = data_available() >= remaining_count ? remaining_count : data_available();
            if (iteration_read_count == 0) { continue; }
        }

        memcpy(buff + already_read_count, buffer_current_pos(), iteration_read_count);
        already_read_count += iteration_read_count;
        buffer_already_read_count += iteration_read_count;
    }

    // if we didn't read enough to satisfy the count (or we did but write_eof has been reached and there is no more data available) we set read_eof
    if (already_read_count < count || (write_eof && !data_available())) {
        read_eof = true;
    }
    _gcount = already_read_count;
    //data_needed_cv.notify_one();
    //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: EXITING READ, RELEASING LOCK!\n\n", static_cast<void*>(this));
    return *this;
}

std::istream::int_type PasstroughStream::get() {
    unsigned char chr[1];
    read(reinterpret_cast<char*>(&chr[0]), 1);
    return _gcount == 1 ? chr[0] : EOF;
}

std::streamsize PasstroughStream::gcount() { return _gcount; }
PasstroughStream& PasstroughStream::seekg(std::istream::off_type offset, std::ios_base::seekdir dir) {
    throw std::runtime_error("CANT SEEK ON A PasstroughStream!");
}
std::istream::pos_type PasstroughStream::tellg() { return accumulated_already_read_count + buffer_already_read_count; }
bool PasstroughStream::eof() { return write_eof && read_eof; }
bool PasstroughStream::good() { return !bad(); }
bool PasstroughStream::bad() { return thread_error.has_value(); }
void PasstroughStream::clear() { throw std::runtime_error("CANT CLEAR ON A PasstroughStream!"); }

PasstroughStream& PasstroughStream::write(const char* buf, std::streamsize count) {
    if (write_eof) {
        if (std::this_thread::get_id() != owner_thread_id) throw std::runtime_error("Some non owning thread attempted to write to an EOF'd PasstroughStream");
        return *this;
    }
    //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: TAKING LOCK FOR WRITE!\n\n", static_cast<void*>(this));
    std::unique_lock lock(mtx);
    //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: LOCK TAKEN FOR WRITE!\n\n", static_cast<void*>(this));
    std::streamsize data_already_written = 0;

    while (data_already_written < count) {
        std::streamsize remaining_data_to_write = count - data_already_written;
        //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: WAITING FOR MORE DATA NEEDED FOR WRITE!\n\n", static_cast<void*>(this));
        if (data_available()) {
            data_available_cv.notify_one();
            data_needed_cv.wait(lock);
            // Check if data is actually needed or we were woken up to be terminated
            if (write_eof || read_eof) {
                if (std::this_thread::get_id() != owner_thread_id) throw std::runtime_error("Some non owning thread attempted to write to an EOF'd PasstroughStream");
                write_eof = true;
                return *this;
            }
        }
        //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: WOKE UP FROM WAITING FOR MORE DATA NEEDED FOR WRITE!\n\n", static_cast<void*>(this));

        // at this point, all data available was consumed and we are ready to replenish the buffer with new data
        accumulated_already_read_count += buffer_already_read_count;
        buffer_already_read_count = 0;

        std::streamsize iteration_data_to_write = remaining_data_to_write >= buffer_size ? buffer_size : remaining_data_to_write;
        if (buffer.size() != iteration_data_to_write) buffer.resize(iteration_data_to_write);
        memcpy(buffer.data(), buf + data_already_written, iteration_data_to_write);
        data_already_written += iteration_data_to_write;
    }

    //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: EXITING WRITE, RELEASING LOCK!\n\n", static_cast<void*>(this));
    if (data_available()) { data_available_cv.notify_one(); }
    return *this;
}

PasstroughStream& PasstroughStream::put(char chr) {
    write(&chr, 1);
    return *this;
}

void PasstroughStream::flush() { throw std::runtime_error("CANT FLUSH ON A RecursionPassthroughStream!"); }
std::ostream::pos_type PasstroughStream::tellp() { return accumulated_already_read_count + buffer.size(); }
OStreamLike& PasstroughStream::seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) {
    throw std::runtime_error("CANT SEEK ON A RecursionPassthroughStream!");
}

#ifdef DEBUG
void DebugComparatorIStreamLike::compare_status() {
  long long known_good_pos = known_good->tellg();
  long long known_good_gcount = known_good->gcount();
  long long known_good_good = known_good->good();
  long long known_good_bad = known_good->bad();
  long long known_good_eof = known_good->eof();
  long long test_stream_pos = test_stream->tellg();
  long long test_stream_gcount = test_stream->gcount();
  long long test_stream_good = test_stream->good();
  long long test_stream_bad = test_stream->bad();
  long long test_stream_eof = test_stream->eof();

  if (
    known_good_pos != test_stream_pos ||
    known_good_gcount != test_stream_gcount ||
    known_good_good != test_stream_good ||
    known_good_bad != test_stream_bad ||
    known_good_eof != test_stream_eof
    ) {
    throw std::runtime_error("De-sync in flag/pos/gcount status between known_good and test_stream!");
  }
}

IStreamLike& DebugComparatorIStreamLike::read(char* buff, std::streamsize count) {
  compare_status();  // Ensure identical status before starting read
  known_good->read(buff, count);
  std::vector<char> test_buff;
  test_buff.resize(count);
  test_stream->read(test_buff.data(), count);
  compare_status();  // Ensure status still synced after read

  // Compare buffers and ensure the exact same data was read
  for (std::streamsize i = 0; i < known_good->gcount(); i++) {
    auto buff_chr = buff[i];
    auto test_chr = test_buff[i];
    if (buff_chr != test_chr) {
      throw std::runtime_error("Different data read between known_good and test_stream!");
    }
  }
  return *this;
}

std::istream::int_type DebugComparatorIStreamLike::get() {
  compare_status();  // Ensure identical status before starting get
  auto buff_chr = test_stream->get();
  auto test_chr = known_good->get();
  compare_status();  // Ensure status still synced after get
  if (buff_chr != test_chr) {
    throw std::runtime_error("Different data read between known_good and test_stream!");
  }
  return buff_chr;
}

std::streamsize DebugComparatorIStreamLike::gcount() {
  compare_status();
  return known_good->gcount();
}

IStreamLike& DebugComparatorIStreamLike::seekg(std::istream::off_type offset, std::ios_base::seekdir dir) {
  compare_status();
  test_stream->seekg(offset, dir);
  known_good->seekg(offset, dir);
  compare_status();
  return *this;
}

std::istream::pos_type DebugComparatorIStreamLike::tellg() {
  compare_status();
  return known_good->tellg();
}

bool DebugComparatorIStreamLike::eof() {
  compare_status();
  return known_good->eof();
}
bool DebugComparatorIStreamLike::good() {
  compare_status();
  return known_good->good();
}
bool DebugComparatorIStreamLike::bad() {
  compare_status();
  return known_good->bad();
}
void DebugComparatorIStreamLike::clear() {
  compare_status();
  known_good->clear();
  test_stream->clear();
  compare_status();
}
#endif
