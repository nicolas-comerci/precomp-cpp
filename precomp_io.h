#ifndef PRECOMP_IO_H
#define PRECOMP_IO_H
#include "precomp_utils.h"

#include "contrib/bzip2/bzlib.h"
#include "contrib/liblzma/precomp_xz.h"
#include "contrib/zpaq/libzpaq.h"

#include <memory>
#include <fstream>
#include <functional>

class PrecompTmpFile : public std::fstream {
public:
  std::string file_path;
  std::ios_base::openmode mode;

  ~PrecompTmpFile() {
    close();
    std::remove(file_path.c_str());
  }

  void open(std::string file_path, std::ios_base::openmode mode) {
    this->file_path = file_path;
    this->mode = mode;
    std::fstream::open(file_path, mode);
  }

  void reopen() {
    if (is_open()) close();
    open(file_path, mode);
  }

  long long filesize();
  void resize(long long size);
};

class memiostream: public std::iostream
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

  std::unique_ptr<membuf> buf;
  memiostream(membuf* buf): std::iostream(buf), buf(std::unique_ptr<membuf>(buf)) {}

public:
  static memiostream make(unsigned char* begin, unsigned char* end) {
    auto membuf_ptr = new membuf(reinterpret_cast<char*>(begin), reinterpret_cast<char*>(end));
    return {membuf_ptr};
  }
};

class CompressedOStreamBuffer;
constexpr auto CHUNK = 262144 * 4 * 10; // 10 MB buffersize

// compression-on-the-fly
enum { OTF_NONE = 0, OTF_BZIP2 = 1, OTF_XZ_MT = 2, OTF_ZPAQ = 3 };

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

class ZpaqIStreamBuffer : public std::streambuf
{
  class ZpaqIStreamBufReader : public libzpaq::Reader
  {
  public:
    std::unique_ptr<char[]> otf_in;
    std::istream* streambuf_wrapped_istream;
    char* curr_read = nullptr;
    char* eof_ptr = nullptr; // If eof has been reached on the istream, this points to the respective position of eof on otf_in

    ZpaqIStreamBufReader(std::istream* wrapped_istream) : streambuf_wrapped_istream(wrapped_istream) {
      otf_in = std::make_unique<char[]>(CHUNK);
    }

    int get() override {
      if (curr_read != nullptr && curr_read == eof_ptr) return EOF;
      if (curr_read == nullptr || curr_read == otf_in.get() + CHUNK) {
        curr_read = otf_in.get();
        streambuf_wrapped_istream->read(otf_in.get(), CHUNK);
        const auto read_count = streambuf_wrapped_istream->gcount();
        if (read_count < CHUNK) eof_ptr = otf_in.get() + read_count;
        if (otf_in.get() == eof_ptr) return EOF;
      }
      const auto chr = static_cast<unsigned char>(*curr_read);
      curr_read++;
      return chr;
    }
  };

  class ZpaqIStreamBufWriter : public libzpaq::Writer
  {
  public:
    std::unique_ptr<char[]>* streambuf_otf_dec;
    char* curr_write = nullptr;

    ZpaqIStreamBufWriter(std::unique_ptr<char[]>* otf_dec) : streambuf_otf_dec(otf_dec) {}

    void put(int c) override {
      if (curr_write == nullptr) reset_write_ptr();
      *curr_write = c;
      curr_write++;
    }

    void reset_write_ptr() { curr_write = streambuf_otf_dec->get(); }
  };
public:
  std::istream* wrapped_istream;
  bool owns_wrapped_istream = false;
  std::unique_ptr<char[]> otf_dec;
  ZpaqIStreamBufReader reader;
  ZpaqIStreamBufWriter writer;
  libzpaq::Decompresser zpaq_decompresser;
  bool decompression_started = false;

  ZpaqIStreamBuffer(std::istream& wrapped_istream) : wrapped_istream(&wrapped_istream), reader(&wrapped_istream), writer(&this->otf_dec) {
    init();
  }

  ZpaqIStreamBuffer(std::unique_ptr<std::istream>&& wrapped_istream): reader(wrapped_istream.get()), writer(&this->otf_dec) {
    this->wrapped_istream = wrapped_istream.release();
    owns_wrapped_istream = true;
    init();
  }

  static std::unique_ptr<std::istream> from_istream(std::unique_ptr<std::istream>&& istream) {
    auto new_fin = std::unique_ptr<std::istream>(new std::ifstream());
    auto zpaq_streambuf = new ZpaqIStreamBuffer(std::move(istream));
    new_fin->rdbuf(zpaq_streambuf);
    return new_fin;
  }

  void init() {
    otf_dec = std::make_unique<char[]>(CHUNK * 10);
    zpaq_decompresser.setInput(&reader);
    zpaq_decompresser.setOutput(&writer);

    setg(otf_dec.get(), otf_dec.get(), otf_dec.get());
  }

  ~ZpaqIStreamBuffer() override {
    if (owns_wrapped_istream) delete wrapped_istream;
  }

  bool findBlock() {
    bool found = true;
    double memory;                         // bytes required to decompress
    found &= zpaq_decompresser.findBlock(&memory);
    found &= zpaq_decompresser.findFilename(); // This finds the segment
    zpaq_decompresser.readComment();
    return found;
  }

  int underflow() override {
    if (gptr() < egptr())
      return *gptr();

    print_work_sign(true);

    if (!decompression_started) {
      findBlock();
      decompression_started = true;
    }
    int amt_read;
    while (true) {
      bool still_more_data = zpaq_decompresser.decompress(CHUNK);
      amt_read = writer.curr_write - otf_dec.get();
      if (still_more_data) break;

      zpaq_decompresser.readSegmentEnd();
      decompression_started = false;
      // If we didn't read anything, check that we are not at the end of a block and the start of another
      // If that is the case, we read again now that we have found the new block
      if (amt_read != 0 || !findBlock()) break;
    }
    
    setg(otf_dec.get(), otf_dec.get(), otf_dec.get() + amt_read);
    if (amt_read == 0) return EOF;
    writer.reset_write_ptr();
    return static_cast<unsigned char>(*gptr());
  }
};

std::unique_ptr<std::istream> wrap_istream_otf_compression(std::unique_ptr<std::istream>&& istream, int otf_compression_method);

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
    setp(otf_in.get(), otf_in.get() + CHUNK);
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
  bool silent;

  XzOStreamBuffer(
    std::unique_ptr<std::ostream>&& wrapped_ostream,
    uint64_t compression_otf_max_memory,
    unsigned int compression_otf_thread_count,
    std::unique_ptr<lzma_init_mt_extra_parameters>&& otf_xz_extra_params,
    bool silent = false
  ) :
    CompressedOStreamBuffer(std::move(wrapped_ostream)), compression_otf_max_memory(compression_otf_max_memory), compression_otf_thread_count(compression_otf_thread_count), silent(silent)
  {
    init(std::move(otf_xz_extra_params));
  }

  static std::unique_ptr<std::ostream> from_ostream(
    std::unique_ptr<std::ostream>&& ostream,
    std::unique_ptr<lzma_init_mt_extra_parameters>&& otf_xz_extra_params,
    uint64_t compression_otf_max_memory,
    unsigned int compression_otf_thread_count,
    bool silent = false
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

    if (!silent)
    print_to_console(
      "Compressing with LZMA, " + std::to_string(threads) + plural + ", memory usage: " + std::to_string(memory_usage / (1024 * 1024)) + " MiB, block size: " + std::to_string(block_size / (1024 * 1024)) + " MiB\n\n"
    );
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
      if (!DEBUG_MODE && !silent) lzma_progress_update();
    } while (otf_xz_stream_c->avail_in > 0 || otf_xz_stream_c->avail_out != CHUNK || final_byte && ret != LZMA_STREAM_END);

    setp(otf_in.get(), otf_in.get() + CHUNK - 1);
    return 0;
  }
};

class ZpaqOStreamBuffer : public CompressedOStreamBuffer
{
  class ZpaqOStreamBufReader : public libzpaq::Reader
  {
  public:
    std::unique_ptr<char[]> buffer;
    char* curr_read = nullptr;
    char* data_end;

    ZpaqOStreamBufReader(std::unique_ptr<char[]>* otf_in) {
      buffer = std::make_unique<char[]>(CHUNK);
      std::copy_n(otf_in->get(), CHUNK, buffer.get());
      data_end = buffer.get() + CHUNK;
    }

    int read(char* buf, int n) override {
      if (curr_read == nullptr) reset_read_ptr();
      if (curr_read == data_end) return 0;
      auto remaining_data_size = data_end - curr_read;
      auto read_size = remaining_data_size;
      if (n < remaining_data_size) {
        read_size = n;
      }
      memcpy(buf, curr_read, read_size);
      curr_read += read_size;
      return read_size;
    }

    int get() override {
      if (curr_read == nullptr) reset_read_ptr();
      if (curr_read == data_end) return EOF;
      auto chr = static_cast<unsigned char>(*curr_read);
      curr_read++;
      return chr;
    }

    void reset_read_ptr() { curr_read = buffer.get(); }
  };

  class ZpaqOStreamBufWriter : public libzpaq::Writer
  {
  public:
    std::unique_ptr<char[]> buffer;
    char* current_buffer_pos = nullptr;

    ZpaqOStreamBufWriter()
    {
      buffer = std::make_unique<char[]>(2*CHUNK);
      current_buffer_pos = buffer.get();
    }

    void write(const char* buf, int n) override {
      std::copy_n(buf, n, current_buffer_pos);
      current_buffer_pos += n;
    }

    void put(int c) override {
      *current_buffer_pos = c;
      current_buffer_pos++;
    }

    long long written_amt() const { return current_buffer_pos - buffer.get(); }
  };
public:

  ZpaqOStreamBuffer(std::unique_ptr<std::ostream>&& wrapped_ostream) : CompressedOStreamBuffer(std::move(wrapped_ostream)) { }

  static std::unique_ptr<std::ostream> from_ostream(std::unique_ptr<std::ostream>&& ostream);

  int sync(bool final_byte) override {
    libzpaq::Compressor compressor;
    ZpaqOStreamBufReader reader = ZpaqOStreamBufReader(&this->otf_in);
    compressor.setInput(&reader);
    if (final_byte) {
      reader.data_end = reader.buffer.get() + (pptr() - pbase());
    }

    ZpaqOStreamBufWriter writer;
    compressor.setOutput(&writer);

    compressor.writeTag();
    compressor.startBlock(2);
    compressor.startSegment();
    compressor.compress(CHUNK);
    compressor.endSegment();
    compressor.endBlock();
    reader.reset_read_ptr();
    this->wrapped_ostream->write(writer.buffer.get(), writer.written_amt());

    setp(otf_in.get(), otf_in.get(), otf_in.get() + CHUNK);
    return 0;
  }
};

std::unique_ptr<std::ostream> wrap_ostream_otf_compression(
  std::unique_ptr<std::ostream>&& ostream,
  int otf_compression_method,
  std::unique_ptr<lzma_init_mt_extra_parameters>&& otf_xz_extra_params,
  uint64_t compression_otf_max_memory,
  unsigned int compression_otf_thread_count,
  bool silent = false
);
#endif // PRECOMP_IO_H
