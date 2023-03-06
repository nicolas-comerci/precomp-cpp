#include "precomp_io.h"

#include <array>

#include "precomp_utils.h"

#include <filesystem>
#include <memory>

#include "contrib/bzip2/bzlib.h"
#include "contrib/liblzma/precomp_xz.h"

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

CompressedIStreamBuffer::CompressedIStreamBuffer(std::unique_ptr<std::istream>&& istream) : wrapped_istream(std::move(istream)) { }
std::streambuf::pos_type CompressedIStreamBuffer::seekoff(std::streambuf::off_type offset, std::ios_base::seekdir dir, std::ios_base::openmode mode) {
  return wrapped_istream->rdbuf()->pubseekoff(offset, dir, mode);
}

class precomp_bz_stream: public bz_stream {};

Bz2IStreamBuffer::Bz2IStreamBuffer(std::unique_ptr<std::istream>&& istream) : CompressedIStreamBuffer(std::move(istream)) {
  init();
}
std::unique_ptr<std::istream> Bz2IStreamBuffer::from_istream(std::unique_ptr<std::istream>&& istream) {
  auto new_fin = std::unique_ptr<std::istream>(new std::ifstream());
  auto bz2_streambuf = new Bz2IStreamBuffer(std::move(istream));
  new_fin->rdbuf(bz2_streambuf);
  return new_fin;
}
void Bz2IStreamBuffer::init() {
  otf_in = std::make_unique<char[]>(CHUNK);
  otf_dec = std::make_unique<char[]>(CHUNK * 10);
  otf_bz2_stream_d = std::make_unique<precomp_bz_stream>();
  otf_bz2_stream_d->bzalloc = nullptr;
  otf_bz2_stream_d->bzfree = nullptr;
  otf_bz2_stream_d->opaque = nullptr;
  otf_bz2_stream_d->avail_in = 0;
  otf_bz2_stream_d->next_in = nullptr;
  if (BZ2_bzDecompressInit(otf_bz2_stream_d.get(), 0, 0) != BZ_OK) {
    throw std::runtime_error(make_cstyle_format_string("ERROR: bZip2 init failed\n"));
  }

  setg(otf_dec.get(), otf_dec.get(), otf_dec.get());
}
Bz2IStreamBuffer::~Bz2IStreamBuffer() {
  (void)BZ2_bzDecompressEnd(otf_bz2_stream_d.get());
}
int Bz2IStreamBuffer::underflow() {
  if (gptr() < egptr())
    return *gptr();

  int ret;

  this->otf_bz2_stream_d->avail_out = CHUNK * 10;
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

class precomp_lzma_stream: public lzma_stream {};

XzIStreamBuffer::XzIStreamBuffer(std::unique_ptr<std::istream>&& istream) : CompressedIStreamBuffer(std::move(istream)) {
  init();
}
std::unique_ptr<std::istream> XzIStreamBuffer::from_istream(std::unique_ptr<std::istream>&& istream) {
  auto new_fin = std::unique_ptr<std::istream>(new std::ifstream());
  auto xz_streambuf = new XzIStreamBuffer(std::move(istream));
  new_fin->rdbuf(xz_streambuf);
  return new_fin;
}
void XzIStreamBuffer::init() {
  otf_in = std::make_unique<char[]>(CHUNK);
  otf_dec = std::make_unique<char[]>(CHUNK * 10);
  otf_xz_stream_d = std::make_unique<precomp_lzma_stream>();
  if (!init_decoder(otf_xz_stream_d.get())) {
    throw std::runtime_error("ERROR: liblzma init failed\n");
  }

  setg(otf_dec.get(), otf_dec.get(), otf_dec.get());
}
XzIStreamBuffer::~XzIStreamBuffer() {
  lzma_end(otf_xz_stream_d.get());
}
int XzIStreamBuffer::underflow() {
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

  size_t amt_read = CHUNK * 10 - otf_xz_stream_d->avail_out;
  setg(otf_dec.get(), otf_dec.get(), otf_dec.get() + amt_read);
  if (amt_read == 0 && stream_eof) return EOF;
  return static_cast<unsigned char>(*gptr());
}

CompressedOStreamBuffer::CompressedOStreamBuffer(std::unique_ptr<std::ostream>&& wrapped_ostream) : wrapped_ostream(std::move(wrapped_ostream)) {
  otf_in = std::make_unique<char[]>(CHUNK);
  otf_out = std::make_unique<char[]>(CHUNK);
}
std::streambuf::pos_type CompressedOStreamBuffer::seekoff(std::streambuf::off_type offset, std::ios_base::seekdir dir, std::ios_base::openmode mode) {
  return wrapped_ostream->rdbuf()->pubseekoff(offset, dir, mode);
}
int CompressedOStreamBuffer::sync() { return sync(false); }
void CompressedOStreamBuffer::set_stream_eof() {
  if (is_stream_eof) return;
  // uncompressed data of length 0 ends compress-on-the-fly data
  for (int i = 0; i < 9; i++) {
    overflow(0);
  }
  sync(true);
  is_stream_eof = true;
}
int CompressedOStreamBuffer::overflow(int c) {
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

Bz2OStreamBuffer::Bz2OStreamBuffer(std::unique_ptr<std::ostream>&& wrapped_ostream) : CompressedOStreamBuffer(std::move(wrapped_ostream)) {
  init();
}
void Bz2OStreamBuffer::init() {
  otf_bz2_stream_c = std::unique_ptr<precomp_bz_stream, std::function<void(precomp_bz_stream*)>>(
    new precomp_bz_stream(),
    [](precomp_bz_stream* ptr) {
      BZ2_bzCompressEnd(ptr);
    }
  );
  otf_bz2_stream_c->bzalloc = nullptr;
  otf_bz2_stream_c->bzfree = nullptr;
  otf_bz2_stream_c->opaque = nullptr;
  if (BZ2_bzCompressInit(otf_bz2_stream_c.get(), 9, 0, 0) != BZ_OK) {
    throw std::runtime_error(make_cstyle_format_string("ERROR: bZip2 init failed\n"));
  }

  setp(otf_in.get(), otf_in.get() + CHUNK - 1);
}
int Bz2OStreamBuffer::sync(bool final_byte) {
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

template <typename T>
class Precomp_OStream : public T
{
  static_assert(std::is_base_of_v<std::ostream, T>, "OStreamWrapper must get an std::ostream derivative as template parameter");
public:
  std::unique_ptr<CompressedOStreamBuffer> otf_compression_streambuf;
  void rdbuf(std::streambuf * streambuffer)
  {
    std::ostream& stream_ref = *this;
    stream_ref.rdbuf(streambuffer);
  }

  ~Precomp_OStream() override {
    if (otf_compression_streambuf != nullptr) otf_compression_streambuf->set_stream_eof();
  }
};

XzOStreamBuffer::XzOStreamBuffer(
  std::unique_ptr<std::ostream>&& wrapped_ostream,
  uint64_t compression_otf_max_memory,
  unsigned int compression_otf_thread_count,
  std::unique_ptr<lzma_init_mt_extra_parameters>&& otf_xz_extra_params
) :
  CompressedOStreamBuffer(std::move(wrapped_ostream)), compression_otf_max_memory(compression_otf_max_memory), compression_otf_thread_count(compression_otf_thread_count)
{
  init(std::move(otf_xz_extra_params));
}
void XzOStreamBuffer::init(std::unique_ptr<lzma_init_mt_extra_parameters>&& otf_xz_extra_params) {
  this->otf_xz_extra_params = std::move(otf_xz_extra_params);
  otf_xz_stream_c = std::unique_ptr<precomp_lzma_stream, std::function<void(precomp_lzma_stream*)>>(
    new precomp_lzma_stream(),
    [](precomp_lzma_stream* ptr) {
      lzma_end(ptr);
    }
  );
  uint64_t memory_usage = 0;
  uint64_t block_size = 0;
  uint64_t max_memory = this->compression_otf_max_memory * 1024 * 1024LL;
  auto threads = this->compression_otf_thread_count;

  if (max_memory == 0) {
    max_memory = lzma_max_memory_default() * 1024 * 1024LL;
  }
  if (threads == 0) {
    threads = auto_detected_thread_count();
  }

  if (!init_encoder_mt(otf_xz_stream_c.get(), threads, max_memory, memory_usage, block_size, *this->otf_xz_extra_params)) {
    throw std::runtime_error(make_cstyle_format_string("ERROR: xz Multi-Threaded init failed\n"));
  }

  setp(otf_in.get(), otf_in.get() + CHUNK - 1);
}
int XzOStreamBuffer::sync(bool final_byte) {
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

// Return maximal memory to use per default for LZMA in MiB
// Use only 1 GiB in the 32-bit windows variant
// because of the 2 or 3 GiB limit on these systems
int lzma_max_memory_default() {
  int max_memory = 2048;
#ifndef __unix
#ifndef BIT64
  max_memory = 1024;
#endif
#endif
  return max_memory;
}

WrappedIStream wrap_istream_otf_compression(std::unique_ptr<std::istream>&& istream, int otf_compression_method, bool take_ownership) {
  switch (otf_compression_method) {
  case OTF_NONE: {
    return { istream.release(), take_ownership };
    }
    case OTF_BZIP2: {
      return { Bz2IStreamBuffer::from_istream(std::move(istream)).release(), take_ownership };
    }
    case OTF_XZ_MT: {
      return { XzIStreamBuffer::from_istream(std::move(istream)).release(), take_ownership };
    }
  }
  throw std::runtime_error(make_cstyle_format_string("Unknown compression method!"));
}

std::unique_ptr<std::ostream> XzOStreamBuffer::from_ostream(
  std::unique_ptr<std::ostream>&& ostream,
  std::unique_ptr<lzma_init_mt_extra_parameters>&& otf_xz_extra_params,
  uint64_t compression_otf_max_memory,
  unsigned int compression_otf_thread_count
) {
  auto new_fout = new Precomp_OStream<std::ofstream>();
  auto xz_streambuf = new XzOStreamBuffer(std::move(ostream), compression_otf_max_memory, compression_otf_thread_count, std::move(otf_xz_extra_params));
  new_fout->otf_compression_streambuf = std::unique_ptr<XzOStreamBuffer>(xz_streambuf);
  new_fout->rdbuf(xz_streambuf);
  return std::unique_ptr<std::ostream>(new_fout);
}

std::unique_ptr<std::ostream> Bz2OStreamBuffer::from_ostream(std::unique_ptr<std::ostream>&& ostream) {
  auto new_fout = new Precomp_OStream<std::ofstream>();
  auto bz2_streambuf = new Bz2OStreamBuffer(std::move(ostream));
  new_fout->otf_compression_streambuf = std::unique_ptr<Bz2OStreamBuffer>(bz2_streambuf);
  new_fout->rdbuf(bz2_streambuf);
  return std::unique_ptr<std::ostream>(new_fout);
}

WrappedOStream wrap_ostream_otf_compression(
  std::unique_ptr<std::ostream>&& ostream,
  int otf_compression_method,
  std::unique_ptr<lzma_init_mt_extra_parameters>&& otf_xz_extra_params,
  uint64_t compression_otf_max_memory,
  unsigned int compression_otf_thread_count,
  bool take_ownership
) {
  switch (otf_compression_method) {
  case OTF_NONE: {
    return { ostream.release(), take_ownership };
  }
  case OTF_BZIP2: {
    return { Bz2OStreamBuffer::from_ostream(std::move(ostream)).release(), take_ownership };
  }
  case OTF_XZ_MT: {
    return { XzOStreamBuffer::from_ostream(std::move(ostream), std::move(otf_xz_extra_params), compression_otf_max_memory, compression_otf_thread_count).release(), take_ownership };
  }
  }
  throw std::runtime_error(make_cstyle_format_string("Unknown compression method!"));
}
