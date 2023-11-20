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

class FormatResults {
public:
  unsigned int detected_amount = 0;
  unsigned int precompressed_amount = 0;
};

class ResultStatistics: public CResultStatistics {
  std::map<std::string, FormatResults> format_results;
public:
  ResultStatistics();

  void increase_detected_count(std::string tag) { format_results[tag].detected_amount++; }
  void increase_precompressed_count(std::string tag) { format_results[tag].precompressed_amount++; }

  unsigned int get_total_detected_count() {
    unsigned int total = 0;
    for (const auto& [key, value] : format_results) {
      total += value.detected_amount;
    }
    return total;
  }

  unsigned int get_total_precompressed_count() {
    unsigned int total = 0;
    for (const auto& [key, value] : format_results) {
      total += value.precompressed_amount;
    }
    return total;
  }

  void print_results() {
    print_to_console("Recompressed streams: " + std::to_string(get_total_precompressed_count()) + "/" + std::to_string(get_total_detected_count()) + "\n");
    for (const auto& [key, value] : format_results) {
      print_to_console(key + " streams: " + std::to_string(value.precompressed_amount) + "/" + std::to_string(value.detected_amount) + "\n");
    }
  }
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


// This class serves to provide a PrecompFormatHandler with some tools to facilitate their functioning and communication with the Precomp, without giving it
// access to its internals
class Tools {
public:
  std::function<void()> progress_callback;
  std::function<std::string(const std::string& name, bool append_tag)> get_tempfile_name;
  std::function<void(std::string)> increase_detected_count;
  std::function<void(std::string)> increase_precompressed_count;
  std::function<void(SupportedFormats, long long, unsigned int)> add_ignore_offset;

  Tools(
    std::function<void()>&& _progress_callback,
    std::function<std::string(const std::string& name, bool append_tag)>&& _get_tempfile_name,
    std::function<void(std::string)>&& _increase_detected_count,
    std::function<void(std::string)>&& _increase_precompressed_count,
    std::function<void(SupportedFormats, long long, unsigned int)>&& _add_ignore_offset
  ) :
    progress_callback(std::move(_progress_callback)),
    get_tempfile_name(std::move(_get_tempfile_name)),
    increase_detected_count(std::move(_increase_detected_count)),
    increase_precompressed_count(std::move(_increase_precompressed_count)),
    add_ignore_offset(std::move(_add_ignore_offset)) {}
};


class precompression_result {
protected:
  virtual void dump_header_to_outfile(OStreamLike& outfile) const;
  void dump_penaltybytes_to_outfile(OStreamLike& outfile) const;
  void dump_stream_sizes_to_outfile(OStreamLike& outfile) const;
  virtual void dump_precompressed_data_to_outfile(OStreamLike& outfile) const;

  Tools* tools;
public:
  explicit precompression_result(SupportedFormats format, Tools* _tools) : tools(_tools), success(false), format(format) {}
  virtual ~precompression_result() = default;

  virtual void increase_detected_count() = 0;
  virtual void increase_precompressed_count() = 0;

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


class PrecompFormatHandler;
extern std::map<SupportedFormats, std::function<PrecompFormatHandler*(Tools*)>> registeredHandlerFactoryFunctions;

class PrecompFormatHandler {    
protected:
    std::vector<SupportedFormats> header_bytes;
    Tools* precomp_tools;

public:
    std::optional<unsigned int> depth_limit;
    bool recursion_allowed;

    PrecompFormatHandler(std::vector<SupportedFormats> _header_bytes, Tools* _precomp_tools, std::optional<unsigned int> _depth_limit = std::nullopt, bool _recursion_allowed = false)
        : header_bytes(_header_bytes), precomp_tools(_precomp_tools), depth_limit(_depth_limit), recursion_allowed(_recursion_allowed) {}
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
    virtual std::unique_ptr<precompression_result>
    attempt_precompression(IStreamLike &input, OStreamLike &output, std::span<unsigned char> buffer, long long input_stream_pos, const Switches &precomp_switches, unsigned int recursion_depth) = 0;

    virtual std::unique_ptr<PrecompFormatHeaderData> read_format_header(IStreamLike &input, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) = 0;
    // recompress method is guaranteed to get the PrecompFormatHeaderData gotten from read_format_header(), so you can, and probably should, downcast to a derived class
    // with your extra format header data, provided you are using and returned such an instance from read_format_header()
    virtual void recompress(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format) = 0;
    // Any data that must be written before the actual stream's data, where recursion can occur, must be written here as this is executed before recompress()
    // Such data should be things like Zip/ZLib or any other compression/container headers.
    virtual void write_pre_recursion_data(OStreamLike &output, PrecompFormatHeaderData& precomp_hdr_data) {}

    // Each format handler is associated with at least one header byte which is outputted to the PCF file when writting the precompressed data
    // If there is more than one supported header byte for the handler, keep in mind that the handler will still be identified by the first one on the vector
    const std::vector<SupportedFormats>& get_header_bytes() { return header_bytes; }

    // Subclasses should register themselves here, as the available PrecompFormatHandlers will be queried and the instances created when we create Precomp instances.
    // If you fail to register the PrecompFormatHandler here then it won't be available and any attempt to set it up for precompression, or of recompressing any file that uses your
    // format handler, will fail.
    static bool registerFormatHandler(SupportedFormats format_tag, std::function<PrecompFormatHandler*(Tools*)> factory_func) {
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
  Tools* precomp_tools;

public:
  uint32_t avail_in = 0;
  std::byte* next_in = nullptr;
  uint32_t avail_out = 0;
  std::byte* next_out = nullptr;
  uint64_t original_stream_size = 0;

  PrecompFormatPrecompressor(Tools* _precomp_tools) : precomp_tools(_precomp_tools) {}
  virtual ~PrecompFormatPrecompressor() = default;

  virtual PrecompProcessorReturnCode process(bool input_eof) = 0;

  // Precompressors are allowed to not drop any data at the start of the entire stream or block, not sure how you would make
  // that work, but didn't want to impose it as a restriction
  virtual void dump_extra_stream_header_data(OStreamLike& output) {}
  virtual void dump_extra_block_header_data(OStreamLike& output) {}

  virtual void increase_detected_count() = 0;
  virtual void increase_precompressed_count() = 0;

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

/*
ProcessorAdapter allows you to adapt an operation that has an IStreamLike input, and an OStreamLike output so that it has the "Processor" interface of having
an next_in, avail_in, next_out, avail_out, ZLib/BZip2/etc style deal which we also use for our PrecompFormatPrecompressor and PrecompFormatRecompressor.
This of course allows you to develop these Precomp processor in a much simpler way by just making such a function and then inheriting from this.
 */
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
      : mtx(_mtx), avail_out(_avail_out), next_out(_next_out), data_full_cv(_data_full_cv) {}

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

  std::function<bool(IStreamLike&, OStreamLike&)> process_func;
  std::thread execution_thread;
  bool started = false;
  bool success = false;
  bool finished = false;
  std::mutex mtx;

  ProcessorIStreamLike input_stream;
  ProcessorOStreamLike output_stream;
  std::condition_variable data_flush_needed_cv;

public:
  bool force_input_eof = false;

  ProcessorAdapter(uint32_t* avail_in, std::byte** next_in, uint32_t* avail_out, std::byte** next_out, std::function<bool(IStreamLike&, OStreamLike&)> process_func_)
    : process_func(std::move(process_func_)), input_stream(&mtx, &data_flush_needed_cv, avail_in, next_in), output_stream(&mtx, &data_flush_needed_cv, avail_out, next_out) {
  }
  virtual ~ProcessorAdapter() {
    input_stream.force_eof = true;
    output_stream.force_eof = true;
    input_stream.data_available_cv.notify_all();
    output_stream.data_freed_cv.notify_all();
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
          success = process_func(input_stream, output_stream);
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
      if (force_input_eof) { input_stream.force_eof = true; }
      if (output_stream.sleeping) {
        output_stream.data_freed_cv.notify_one();
      }
      else {
        input_stream.data_available_cv.notify_one();
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

  virtual std::unique_ptr<PrecompFormatPrecompressor> make_precompressor(Tools& precomp_tools, Switches& precomp_switches, const std::span<unsigned char>& buffer) = 0;

  virtual std::unique_ptr<PrecompFormatHeaderData> read_format_header(IStreamLike& input, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) = 0;
  // make_recompressor method is guaranteed to get the PrecompFormatHeaderData gotten from read_format_header(), so you can, and probably should, downcast to a derived class
  // with your extra format header data, provided you are using and returned such an instance from read_format_header()
  virtual std::unique_ptr<PrecompFormatRecompressor> make_recompressor(PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format, const Tools& tools) = 0;
  // Any data that must be written before the actual stream's data, where recursion can occur, must be written here as this is executed before recompress()
  // Such data should be things like Zip/ZLib or any other compression/container headers.
  virtual void write_pre_recursion_data(OStreamLike &output, PrecompFormatHeaderData &precomp_hdr_data) {}

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
  Tools format_handler_tools;

  long long input_file_pos;
  std::string input_file_name;
  std::string output_file_name;

  std::unique_ptr<IStreamLike> fin = std::make_unique<WrappedIStream>(new std::ifstream(), true);  
  void set_input_stream(std::istream* istream, bool take_ownership = true);
  void set_input_stream(FILE* fhandle, bool take_ownership = true);
  std::unique_ptr<ObservableOStream> fout = std::unique_ptr<ObservableOStream>(new ObservableWrappedOStream(new std::ofstream(), true));
  void set_output_stream(std::ostream* ostream, bool take_ownership = true);
  void set_output_stream(FILE* fhandle, bool take_ownership = true);

  void set_progress_callback(std::function<void(float)> callback);
  void call_progress_callback() const;

  std::string get_tempfile_name(const std::string& name, bool prepend_random_tag = true) const;

  // When precompressing only the requested (or default if nothing was specified) format handlers will be initialized, but on recompression we always enable them all
  // as they might be needed to handle the already precompressed PCF file
  void init_format_handlers(bool is_recompressing = false);
  const std::vector<std::unique_ptr<PrecompFormatHandler>>& get_format_handlers() const;
  const std::vector<std::unique_ptr<PrecompFormatHandler2>>& get_format_handlers2() const;
  bool is_format_handler_active(SupportedFormats format_id, unsigned int recursion_depth) const;

  // Ignore offsets can be set for any Format handler, so that if for example, we failed to precompress a deflate stream inside a ZIP file, we don't attempt to precompress
  // it again, which is destined to fail, by using the intense mode (ZLIB) format handler.
  std::vector<std::array<std::queue<long long>, 256>> ignore_offsets { {} };  // 256 is at least for now the maximum possible amount of format handlers, will most likely be enough for a while
  
};

// All this stuff was moved from precomp.h, most likely doesn't make sense as part of the API, TODO: delete/modularize/whatever stuff that shouldn't be here

// helpers for try_decompression functions
int32_t fin_fget32(IStreamLike& input);
long long fin_fget_vlint(IStreamLike& input);
void fout_fput32_little_endian(OStreamLike& output, unsigned int v);
void fout_fput32(OStreamLike& output, unsigned int v);
void fout_fput_vlint(OStreamLike& output, unsigned long long v);

std::tuple<long long, std::vector<std::tuple<uint32_t, char>>> compare_files_penalty(const std::function<void()>& progress_callback, IStreamLike& original, IStreamLike& candidate, long long original_size);

template <class> inline constexpr bool is_smart_pointer_v = false;
template <class _Ty> inline constexpr bool is_smart_pointer_v<std::unique_ptr<_Ty>> = true;
template <class _Ty> inline constexpr bool is_smart_pointer_v<std::shared_ptr<_Ty>> = true;
template <class _Ty> inline constexpr bool is_smart_pointer_v<std::unique_ptr<_Ty const>> = true;
template <class _Ty> inline constexpr bool is_smart_pointer_v<std::shared_ptr<_Ty const>> = true;
// No volatile ones, don't expect we will run into those.

// Moves any remaining data to the front of the buffer, reads from IStreamLike to fill the buffer, so the processor is ready to process() for another iteration
// Returns a tuple [how much was missing on the buffer, how much it was actually read]
template <typename T, typename R>
[[nodiscard]] std::tuple<std::streamsize, std::streamsize> fill_processor_input_buffer(T& processor, std::vector<R>& in_buf, IStreamLike& input) {
  static_assert(!std::is_pointer_v<T>, "No processor pointers, dereference it yourself!");
  static_assert(!is_smart_pointer_v<T>, "No processor pointers, dereference it yourself!");
  // There might be some data remaining on the in_buf from a previous iteration that wasn't consumed yet, we ensure we don't lose it
  if (processor.avail_in > 0) {
    // The remaining data at the end is moved to the start of the array and we will complete it up so that a full CHUNK of data is available for the iteration
    std::memmove(in_buf.data(), processor.next_in, processor.avail_in);
  }
  const auto in_buf_ptr = reinterpret_cast<char*>(in_buf.data() + processor.avail_in);
  const std::streamsize read_amt = in_buf.size() - processor.avail_in;
  std::streamsize gcount = 0;
  if (read_amt > 0) {
    input.read(in_buf_ptr, read_amt);
    gcount = input.gcount();
  }

  processor.avail_in += gcount;
  processor.next_in = in_buf.data();
  return { read_amt, gcount };
}

/*
Runs the whole processing with a processor, reading from an IStreamLike so you can then output chunks of a given size.
Returns the last PrecompProcessorReturnCode so that you can know if it errored out midway or reached the end of the stream,
and the size of input read.
The actual output of chunks is left to you by running a callback, so you can customize it however you need.
The output_callback assumes you are done with all avail_out data after it runs and loses it, so plan accordingly.
The callback receives the actual size of the data available, a boolean indicating if its the last block of the stream,
and has to return a success boolean, if return is false then the whole process is terminated with an error.
*/
template <typename T, typename R>
[[nodiscard]] std::tuple<PrecompProcessorReturnCode, uint64_t> process_from_input_and_output_chunks(
  T& processor, std::vector<R>& in_buf, IStreamLike& input, std::vector<R>& out_buf, uint32_t output_chunk_size, std::function<bool(std::vector<R>&, uint32_t, bool)> output_callback
) {
  static_assert(!std::is_pointer_v<T>, "No processor pointers, dereference it yourself!");
  static_assert(!is_smart_pointer_v<T>, "No processor pointers, dereference it yourself!");

  in_buf.resize(CHUNK);
  processor.next_in = in_buf.data();
  processor.avail_in = 0;
  out_buf.resize(output_chunk_size);
  processor.next_out = out_buf.data();
  processor.avail_out = output_chunk_size;

  bool reached_eof = false;
  auto ret = PP_OK;
  uint64_t input_stream_size = 0;

  while (true) {
    const auto [read_amt, gcount] = fill_processor_input_buffer(processor, in_buf, input);
    if (input.bad()) break;

    const auto avail_in_before_process = processor.avail_in;
    ret = processor.process(reached_eof);
    if (ret != PP_OK && ret != PP_STREAM_END) break;
    input_stream_size += (avail_in_before_process - processor.avail_in);

    const uint32_t decompressed_chunk_size = output_chunk_size - processor.avail_out;
    const bool is_last_block = ret == PP_STREAM_END;
    if (is_last_block || decompressed_chunk_size == output_chunk_size) {
      const bool output_success = output_callback(out_buf, decompressed_chunk_size, is_last_block);
      if (!output_success) return { PP_ERROR, input_stream_size };

      // All outputted data handled by output_callback, reset processor's output so we can get another chunk
      processor.next_out = out_buf.data();
      processor.avail_out = output_chunk_size;
    }

    if (ret == PP_STREAM_END) break;  // maybe we should also check fin for eof? we could support partial/broken BZip2 streams

    if (read_amt > 0 && gcount == 0 && decompressed_chunk_size < output_chunk_size) {
      // No more data will ever come, and we already flushed all the output data processed, notify processor so it can finish
      reached_eof = true;
    }
    // TODO: What if I feed the processor a full CHUNK and it outputs nothing? Should we quit? Should processors be able to advertise a chunk size they need?
  }
  return { ret, input_stream_size };
}
// Convenience overload of process_from_input_and_output_chunks which allocates its own in_buf and out_buf
template <typename T>
[[nodiscard]] std::tuple<PrecompProcessorReturnCode, uint64_t> process_from_input_and_output_chunks(
  T& processor, IStreamLike& input, std::function<bool(std::vector<std::byte>&, uint32_t, bool)> output_callback, uint32_t output_chunk_size = CHUNK
) {
  static_assert(!std::is_pointer_v<T>, "No processor pointers, dereference it yourself!");
  static_assert(!is_smart_pointer_v<T>, "No processor pointers, dereference it yourself!");
  std::vector<std::byte> out_buf{};  
  std::vector<std::byte> in_buf{};

  return process_from_input_and_output_chunks(processor, in_buf, input, out_buf, output_chunk_size, std::move(output_callback));
}

#endif // PRECOMP_DLL_H
