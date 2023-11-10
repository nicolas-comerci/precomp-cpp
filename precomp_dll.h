#ifndef PRECOMP_DLL_H
#define PRECOMP_DLL_H

#ifndef STDTHREAD_IMPORTED
#define STDTHREAD_IMPORTED
#include <thread>
#endif

#include "libprecomp.h"
#include "precomp_io.h"
#include "precomp_utils.h"

#include <cstdio>
#include <array>
#include <queue>
#include <vector>
#include <set>
#include <string>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>

static constexpr int MAX_PENALTY_BYTES = 16384;

enum SupportedFormats {
  D_PDF = 0,
  D_ZIP = 1,
  D_GZIP = 2,
  D_PNG = 3,
  D_MULTIPNG = 4,
  D_GIF = 5,
  D_JPG = 6,
  D_SWF = 7,
  D_BASE64 = 8,
  D_BZIP2 = 9,
  D_MP3 = 10,
  D_RAW = 255,
  D_BRUTE = 254,
};

void print_to_log(PrecompLoggingLevels log_level, std::string format);

template< typename... Args >
void print_to_log(PrecompLoggingLevels log_level, const char* format, Args... args) {
  print_to_log(log_level, make_cstyle_format_string(format, args...));
}

class EXPORT Switches: public CSwitches {
  public:
    std::queue<long long> ignore_pos_queue;

    Switches();
    ~Switches();
};

class ResultStatistics: public CResultStatistics {
public:
  ResultStatistics();
};

//input buffer
constexpr auto IN_BUF_SIZE = 65536;
// DIV3CHUNK is a bit smaller/larger than CHUNK, so that DIV3CHUNK mod 3 = 0
constexpr auto DIV3CHUNK = 262143;
// CHECKBUF is a subsection of the IN_BUF, from the current position onwards, format support modules should only read this section of the IN_BUF
constexpr auto CHECKBUF_SIZE = 4096;
constexpr auto COMP_CHUNK = 512;
constexpr auto MAX_IO_BUFFER_SIZE = 64 * 1024 * 1024;

class Precomp;
class RecursionContext: public CRecursionContext {
public:
  Precomp& precomp;

  explicit RecursionContext(float min_percent, float max_percent, Precomp& precomp_);

  // Ignore offsets can be set for any Format handler, so that if for example, we failed to precompress a deflate stream inside a ZIP file, we don't attempt to precompress
  // it again, which is destined to fail, by using the intense mode (ZLIB) format handler.
  std::array<std::queue<long long>, 256> ignore_offsets;  // 256 is at least for now the maximum possible amount of format handlers, will most likely be enough for a while

  std::unique_ptr<IStreamLike> fin = std::make_unique<WrappedIStream>(new std::ifstream(), true);
  void set_input_stream(std::istream* istream, bool take_ownership = true);
  void set_input_stream(FILE* fhandle, bool take_ownership = true);
  std::unique_ptr<ObservableOStream> fout = std::unique_ptr<ObservableOStream>(new ObservableWrappedOStream(new std::ofstream(), true));
  void set_output_stream(std::ostream* ostream, bool take_ownership = true);
  void set_output_stream(FILE* fhandle, bool take_ownership = true);

  float global_min_percent;
  float global_max_percent;
  int comp_decomp_state = P_NONE;

  long long input_file_pos;
  unsigned char in_buf[IN_BUF_SIZE];

  // Uncompressed data info
  long long uncompressed_pos;
  std::optional<long long> uncompressed_length = std::nullopt;
  long long uncompressed_bytes_total = 0;
};

class precompression_result
{
protected:
    virtual void dump_header_to_outfile(OStreamLike& outfile) const;
    void dump_penaltybytes_to_outfile(OStreamLike& outfile) const;
    void dump_stream_sizes_to_outfile(OStreamLike& outfile) const;
    virtual void dump_precompressed_data_to_outfile(OStreamLike& outfile) const;
public:
    explicit precompression_result(SupportedFormats format) : success(false), format(format) {}
    virtual ~precompression_result() = default;

    bool success;
    char format;
    std::byte flags{ 0 };
    std::vector<std::tuple<uint32_t, char>> penalty_bytes;
    long long original_size = -1;
    // The original_size field only refers to the actual data subject to precompression/recompression which usually
    // does not include the size of headers for example, this field allows us to track the complete size of the data
    // to be reconstructed during recompression
    long long original_size_extra = 0;
    long long precompressed_size = -1;
    std::unique_ptr<IStreamLike> precompressed_stream;
    bool recursion_used = false;
    long long recursion_filesize = 0;

    virtual void dump_to_outfile(OStreamLike& outfile) const;
    virtual long long complete_original_size() const { return original_size_extra + original_size; }
};

// When Precomp detects and precompresses a stream, there is a header that is written before the precompressed data with information about the precompressed stream
// such as the length, penalty bytes and if recursion was used.
// Format handlers may opt to write longer headers with additional data, in which case they must handle it correctly, respecting the API provided.
class PrecompFormatHeaderData {
public:
  virtual ~PrecompFormatHeaderData() = default;

  std::byte option_flags { 0b0 };
  SupportedFormats format;
  std::queue<std::tuple<uint32_t, unsigned char>> penalty_bytes;
  unsigned long long original_size = 0;
  unsigned long long precompressed_size = 0;
  unsigned long long recursion_data_size = 0;
};

class PrecompFormatHandler;
extern std::map<SupportedFormats, std::function<PrecompFormatHandler*()>> registeredHandlerFactoryFunctions;

class PrecompFormatHandler {
protected:
    std::vector<SupportedFormats> header_bytes;
public:
    // This class serves to provide a PrecompFormatHandler with some tools to facilitate their functioning and communication with the Precomp, without giving it
    // access to its internals
    class Tools {
    public:
      std::function<void()> progress_callback;
      std::function<std::string(const std::string& name, bool append_tag)> get_tempfile_name;

      Tools(std::function<void()>&& _progress_callback, std::function<std::string(const std::string& name, bool append_tag)>&& _get_tempfile_name):
        progress_callback(std::move(_progress_callback)), get_tempfile_name(std::move(_get_tempfile_name)) {}
    };

    std::optional<unsigned int> depth_limit;
    bool recursion_allowed;

    PrecompFormatHandler(std::vector<SupportedFormats> _header_bytes, std::optional<unsigned int> _depth_limit = std::nullopt, bool _recursion_allowed = false)
        : header_bytes(_header_bytes), depth_limit(_depth_limit), recursion_allowed(_recursion_allowed) {}
    virtual ~PrecompFormatHandler() = default;

    // The quick check should attempt to detect applicable format data by inspecting File Signatures/Magic Bytes or via any other easy/quick check that could
    // be done by using the small buffered chunk provided.
    // If quick detection is impossible, just return true so Precomp will move on and attempt precompression.
    // The current_input_id and original_input_pos parameters are provided so that format handlers can apply certain optimizations by confirming that they
    // might have already seen part of the data on the buffer_chunk, like insane/brute deflate handlers that use an histogram to detect false positives.
    virtual bool quick_check(const std::span<unsigned char> buffer_chunk, uintptr_t current_input_id, const long long original_input_pos) = 0;

    // The main precompression entrypoint, you are given full access to Precomp instance which in turn gives you access to the current context and input/output streams.
    // You should however if possible not output anything to the output stream directly or otherwise mess with the Precomp instance or current context unless strictly necessary,
    // ideally the format handler should just read from the context's input stream, precompress the data, and return a precompression_result, without touching much else.
    virtual std::unique_ptr<precompression_result> attempt_precompression(Precomp& precomp_instance, std::span<unsigned char> buffer, long long input_stream_pos) = 0;

    virtual std::unique_ptr<PrecompFormatHeaderData> read_format_header(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) = 0;
    // recompress method is guaranteed to get the PrecompFormatHeaderData gotten from read_format_header(), so you can, and probably should, downcast to a derived class
    // with your extra format header data, provided you are using and returned such an instance from read_format_header()
    virtual void recompress(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format, const Tools& tools) = 0;
    // Any data that must be written before the actual stream's data, where recursion can occur, must be written here as this is executed before recompress()
    // Such data should be things like Zip/ZLib or any other compression/container headers.
    virtual void write_pre_recursion_data(RecursionContext& context, PrecompFormatHeaderData& precomp_hdr_data) {}

    // Each format handler is associated with at least one header byte which is outputted to the PCF file when writting the precompressed data
    // If there is more than one supported header byte for the handler, keep in mind that the handler will still be identified by the first one on the vector
    const std::vector<SupportedFormats>& get_header_bytes() { return header_bytes; }

    // Subclasses should register themselves here, as the available PrecompFormatHandlers will be queried and the instances created when we create Precomp instances.
    // If you fail to register the PrecompFormatHandler here then it won't be available and any attempt to set it up for precompression, or of recompressing any file that uses your
    // format handler, will fail.
    static bool registerFormatHandler(SupportedFormats format_tag, std::function<PrecompFormatHandler*()> factory_func) {
        registeredHandlerFactoryFunctions[format_tag] = factory_func;
        return true;
    }
};

#define REGISTER_PRECOMP_FORMAT_HANDLER(format_tag, factory_func) \
    bool format_tag ## _entry = PrecompFormatHandler::registerFormatHandler(format_tag, (factory_func))

enum PrecompProcessorReturnCode {
  PP_OK = 0,
  PP_STREAM_END = 1,
  PP_ERROR = -1,
};

class PrecompFormatPrecompressor {
protected:
  std::function<void()> progress_callback;

public:
  uint32_t avail_in = 0;
  std::byte* next_in = nullptr;
  uint32_t avail_out = 0;
  std::byte* next_out = nullptr;
  uint64_t original_stream_size = 0;

  PrecompFormatPrecompressor(const std::span<unsigned char>& buffer, const std::function<void()>& _progress_callback) : progress_callback(_progress_callback) {}
  virtual ~PrecompFormatPrecompressor() = default;

  virtual PrecompProcessorReturnCode process(bool input_eof) = 0;

  virtual void dump_extra_stream_header_data(OStreamLike& output) {}
  virtual void dump_extra_block_header_data(OStreamLike& output) {}
};

class PrecompFormatRecompressor {
protected:
  std::function<void()> progress_callback;

public:
  uint32_t avail_in = 0;
  std::byte* next_in = nullptr;
  uint32_t avail_out = 0;
  std::byte* next_out = nullptr;

  PrecompFormatRecompressor(const PrecompFormatHeaderData& precomp_hdr_data, const std::function<void()>& _progress_callback) : progress_callback(_progress_callback) {}
  virtual ~PrecompFormatRecompressor() = default;

  virtual PrecompProcessorReturnCode process(bool input_eof) = 0;

  virtual void read_extra_block_header_data(IStreamLike& input) {}
};

class ProcessorAdapter {
protected:
  class ProcessorIStreamLike : public IStreamLike {
  private:
    std::mutex* mtx;
    std::condition_variable* data_needed_cv;
    uint32_t* avail_in;
    std::byte** next_in;
    std::streamsize _gcount = 0;
    bool _eof = false;
    std::istream::pos_type _tellg = 0;

  public:
    std::condition_variable data_available_cv;
    bool sleeping = false;
    bool force_eof = false;

    explicit ProcessorIStreamLike(std::mutex* _mtx, std::condition_variable* _data_needed_cv, uint32_t* _avail_in, std::byte** _next_in)
      : _eof(false), mtx(_mtx), data_needed_cv(_data_needed_cv), avail_in(_avail_in), next_in(_next_in) {}

    ProcessorIStreamLike& read(char* buff, std::streamsize count) override {
      if (force_eof) {
        _eof = true;
        _gcount = 0;
        return *this;
      }
      std::unique_lock lock(*mtx);
      auto remainingSize = count;
      auto currBufferPos = buff;
      while (remainingSize > 0) {
        const auto iterationSize = std::min<uint64_t>(*avail_in, remainingSize);
        if (iterationSize > 0) {
          std::copy_n(reinterpret_cast<unsigned char*>(*next_in), iterationSize, currBufferPos);
          *next_in += iterationSize;
          *avail_in -= iterationSize;
          remainingSize -= iterationSize;
          currBufferPos += iterationSize;
        }

        if (force_eof) {
          _eof = true;
          _gcount = count - remainingSize;
          _tellg += _gcount;
          return *this;
        }
        // If we couldn't read all the data from our input buffer we need to block the thread until the consumer gives us more memory in
        if (remainingSize > 0) {
          data_needed_cv->notify_one();
          sleeping = true;
          data_available_cv.wait(lock);
          sleeping = false;
        }
      }

      _gcount = count;
      _tellg += _gcount;
      return *this;
    }

    ProcessorIStreamLike& seekg(std::istream::off_type offset, std::ios_base::seekdir dir) override {
      throw std::runtime_error("SEEK NOT ALLOWED ON ProcessorIStreamLike");
    }

    std::streamsize gcount() override { return _gcount; }
    std::istream::pos_type tellg() override { return _tellg; }
    bool eof() override { return _eof; };
    bool good() override { return !eof(); };
    bool bad() override { return false; }
    void clear() override {};
  };

  class ProcessorOStreamLike : public OStreamLike {
    std::mutex* mtx;
    uint32_t* avail_out;
    std::byte** next_out;
    std::condition_variable* data_full_cv;
    std::istream::pos_type _tellp = 0;
    bool _eof = false;

  public:
    std::condition_variable data_freed_cv;
    bool sleeping = false;
    bool force_eof = false;

    explicit ProcessorOStreamLike(std::mutex* _mtx, std::condition_variable* _data_full_cv, uint32_t* _avail_out, std::byte** _next_out)
      : mtx(_mtx), data_full_cv(_data_full_cv), avail_out(_avail_out), next_out(_next_out) {}

    ProcessorOStreamLike& write(const char* buf, std::streamsize count) override {
      if (force_eof) {
        _eof = true;
        return *this;
      }
      std::unique_lock lock(*mtx);
      auto remainingSize = count;
      auto currBufferPos = buf;
      while (remainingSize > 0) {
        const auto iterationSize = std::min<uint64_t>(*avail_out, remainingSize);
        if (iterationSize > 0) {
          std::copy_n(currBufferPos, iterationSize, reinterpret_cast<unsigned char*>(*next_out));
          *avail_out -= iterationSize;
          *next_out += iterationSize;
          remainingSize -= iterationSize;
          currBufferPos += iterationSize;
        }

        if (force_eof) {
          _eof = true;
          _tellp += count - remainingSize;
          return *this;
        }
        // If we couldn't write all the data to our output buffer we need to block the thread until the consumer takes that data and gives us more memory
        if (remainingSize > 0) {
          data_full_cv->notify_one();
          sleeping = true;
          data_freed_cv.wait(lock);
          sleeping = false;
        }
      }

      _tellp += count;
      return *this;
    }

    std::ostream::pos_type tellp() override { return _tellp; }
    ProcessorOStreamLike& seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) {
      throw std::runtime_error("SEEK NOT ALLOWED ON ProcessorOStreamLike");
    }

    bool eof() override { return _eof; };
    bool good() override { return !eof(); };
    bool bad() override { return false; }
    void clear() override {};
  };

  std::function<bool()> process_func;
  std::thread execution_thread;
  bool started = false;
  bool success = false;
  bool finished = false;
  std::mutex mtx;

  std::unique_ptr<ProcessorIStreamLike> input_stream;
  std::unique_ptr<ProcessorOStreamLike> output_stream;
  std::condition_variable data_flush_needed_cv;
public:
  bool force_input_eof = false;

  ProcessorAdapter(
    uint32_t* avail_in, std::byte** next_in,
    uint32_t* avail_out, std::byte** next_out
  ) {
    input_stream = std::make_unique<ProcessorIStreamLike>(&mtx, &data_flush_needed_cv, avail_in, next_in);
    output_stream = std::make_unique<ProcessorOStreamLike>(&mtx, &data_flush_needed_cv, avail_out, next_out);
  }
  ProcessorAdapter(uint32_t* avail_in, std::byte** next_in, uint32_t* avail_out, std::byte** next_out, std::function<bool()> process_func_)
    : ProcessorAdapter(avail_in, next_in, avail_out, next_out) {
    process_func = process_func_;
  }
  virtual ~ProcessorAdapter() {
    input_stream->force_eof = true;
    output_stream->force_eof = true;
    input_stream->data_available_cv.notify_all();
    output_stream->data_freed_cv.notify_all();
    if (execution_thread.joinable()) execution_thread.join();
  }

  PrecompProcessorReturnCode process(bool input_eof) {
    if (input_eof) force_input_eof = true;
    if (finished) {
      return success ? PP_STREAM_END : PP_ERROR;
    }
    std::unique_lock lock(mtx);
    if (!started) {
      execution_thread = std::thread([&]() {
        try {
          success = process_func();
        }
        catch (...) {
          success = false;
        }
        finished = true;
        data_flush_needed_cv.notify_one();
        });
      started = true;
    }
    else {  // The thread was already started on a previous run of process() and got locked because it ran out of space on the in or out buffers
      if (force_input_eof) { input_stream->force_eof = true; }
      if (output_stream->sleeping) {
        output_stream->data_freed_cv.notify_one();
      }
      else {
        input_stream->data_available_cv.notify_one();
      }
    }

    data_flush_needed_cv.wait(lock);
    if (finished) {
      return success ? PP_STREAM_END : PP_ERROR;
    }
    return PP_OK;
  }
};

class PrecompFormatHandler2;
extern std::map<SupportedFormats, std::function<PrecompFormatHandler2* ()>> registeredHandlerFactoryFunctions2;

class PrecompFormatHandler2 {
protected:
  std::vector<SupportedFormats> header_bytes;
public:
  // This class serves to provide a PrecompFormatHandler with some tools to facilitate their functioning and communication with the Precomp, without giving it
  // access to its internals
  class Tools {
  public:
    std::function<void()> progress_callback;
    std::function<std::string(const std::string& name, bool append_tag)> get_tempfile_name;

    Tools(std::function<void()>&& _progress_callback, std::function<std::string(const std::string& name, bool append_tag)>&& _get_tempfile_name) :
      progress_callback(std::move(_progress_callback)), get_tempfile_name(std::move(_get_tempfile_name)) {}
  };

  std::optional<unsigned int> depth_limit;
  bool recursion_allowed;

  PrecompFormatHandler2(std::vector<SupportedFormats> _header_bytes, std::optional<unsigned int> _depth_limit = std::nullopt, bool _recursion_allowed = false)
    : header_bytes(_header_bytes), depth_limit(_depth_limit), recursion_allowed(_recursion_allowed) {}
  virtual ~PrecompFormatHandler2() = default;

  // The quick check should attempt to detect applicable format data by inspecting File Signatures/Magic Bytes or via any other easy/quick check that could
  // be done by using the small buffered chunk provided.
  // If quick detection is impossible, just return true so Precomp will move on and attempt precompression.
  // The current_input_id and original_input_pos parameters are provided so that format handlers can apply certain optimizations by confirming that they
  // might have already seen part of the data on the buffer_chunk, like insane/brute deflate handlers that use an histogram to detect false positives.
  virtual bool quick_check(const std::span<unsigned char> buffer_chunk, uintptr_t current_input_id, const long long original_input_pos) = 0;

  virtual std::unique_ptr<PrecompFormatPrecompressor> make_precompressor(Precomp& precomp_mgr, const std::span<unsigned char>& buffer) = 0;

  virtual std::unique_ptr<PrecompFormatHeaderData> read_format_header(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) = 0;
  // make_recompressor method is guaranteed to get the PrecompFormatHeaderData gotten from read_format_header(), so you can, and probably should, downcast to a derived class
  // with your extra format header data, provided you are using and returned such an instance from read_format_header()
  virtual std::unique_ptr<PrecompFormatRecompressor> make_recompressor(PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format, const PrecompFormatHandler2::Tools& tools) = 0;
  // Any data that must be written before the actual stream's data, where recursion can occur, must be written here as this is executed before recompress()
  // Such data should be things like Zip/ZLib or any other compression/container headers.
  virtual void write_pre_recursion_data(RecursionContext& context, PrecompFormatHeaderData& precomp_hdr_data) {}

  // Each format handler is associated with at least one header byte which is outputted to the PCF file when writting the precompressed data
  // If there is more than one supported header byte for the handler, keep in mind that the handler will still be identified by the first one on the vector
  const std::vector<SupportedFormats>& get_header_bytes() { return header_bytes; }

  // Subclasses should register themselves here, as the available PrecompFormatHandlers will be queried and the instances created when we create Precomp instances.
  // If you fail to register the PrecompFormatHandler here then it won't be available and any attempt to set it up for precompression, or of recompressing any file that uses your
  // format handler, will fail.
  static bool registerFormatHandler(SupportedFormats format_tag, std::function<PrecompFormatHandler2* ()> factory_func) {
    registeredHandlerFactoryFunctions2[format_tag] = factory_func;
    return true;
  }
};

#define REGISTER_PRECOMP_FORMAT_HANDLER2(format_tag, factory_func) \
    bool format_tag ## _entry = PrecompFormatHandler2::registerFormatHandler(format_tag, (factory_func))

class Precomp {
  std::function<void(float)> progress_callback;
  void set_input_stdin();
  void set_output_stdout();
  void register_output_observer_callbacks();
  std::vector<std::unique_ptr<PrecompFormatHandler>> format_handlers {};
  std::vector<std::unique_ptr<PrecompFormatHandler2>> format_handlers2 {};

public:
  explicit Precomp();

  Switches switches;
  ResultStatistics statistics;
  std::unique_ptr<RecursionContext> ctx = std::make_unique<RecursionContext>(0, 100, *this);
  std::vector<std::unique_ptr<RecursionContext>> recursion_contexts_stack;

  std::string input_file_name;
  std::string output_file_name;
  // Useful so we can easily get (for example) info on the original input/output streams at any time
  std::unique_ptr<RecursionContext>& get_original_context();
  void set_input_stream(std::istream* istream, bool take_ownership = true);
  void set_input_stream(FILE* fhandle, bool take_ownership = true);
  void set_output_stream(std::ostream* ostream, bool take_ownership = true);
  void set_output_stream(FILE* fhandle, bool take_ownership = true);

  void set_progress_callback(std::function<void(float)> callback);
  void call_progress_callback();

  std::string get_tempfile_name(const std::string& name, bool prepend_random_tag = true) const;

  // When precompressing only the requested (or default if nothing was specified) format handlers will be initialized, but on recompression we always enable them all
  // as they might be needed to handle the already precompressed PCF file
  void init_format_handlers(bool is_recompressing = false);
  const std::vector<std::unique_ptr<PrecompFormatHandler>>& get_format_handlers() const;
  const std::vector<std::unique_ptr<PrecompFormatHandler2>>& get_format_handlers2() const;
  bool is_format_handler_active(SupportedFormats format_id) const;

  int recursion_depth = 0;
  
};

// All this stuff was moved from precomp.h, most likely doesn't make sense as part of the API, TODO: delete/modularize/whatever stuff that shouldn't be here

// helpers for try_decompression functions
int32_t fin_fget32(IStreamLike& input);
long long fin_fget_vlint(IStreamLike& input);
void fout_fput32_little_endian(OStreamLike& output, unsigned int v);
void fout_fput32(OStreamLike& output, unsigned int v);
void fout_fput_vlint(OStreamLike& output, unsigned long long v);

std::tuple<long long, std::vector<std::tuple<uint32_t, char>>> compare_files_penalty(Precomp& precomp_mgr, IStreamLike& original, IStreamLike& candidate, long long original_size);

#endif // PRECOMP_DLL_H
