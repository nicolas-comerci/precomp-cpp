#ifndef PRECOMP_DLL_H
#define PRECOMP_DLL_H

#ifndef STDTHREAD_IMPORTED
#define STDTHREAD_IMPORTED
#include <memory>
#include <thread>
#endif

#include "libprecomp.h"
#include "precomp_io.h"
#include "precomp_utils.h"

#include <cstdio>
#include <array>
#include <vector>
#include <set>
#include <string>
#include <fstream>
#include <memory>
#include <optional>
#include <mutex>

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
    Switches();
    
    std::set<long long> ignore_set;
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

  std::set<long long> intense_ignore_offsets;
  std::set<long long> brute_ignore_offsets;

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
  unsigned char tmp_out[CHUNK];

  // Uncompressed data info
  long long uncompressed_pos;
  std::optional<long long> uncompressed_length = std::nullopt;
  long long uncompressed_bytes_total = 0;
};

class Precomp: public CPrecomp {
  std::function<void(float)> progress_callback;
  void set_input_stdin();
  void set_output_stdout();
  void register_output_observer_callbacks();

public:
  explicit Precomp();

  Switches switches;
  ResultStatistics statistics;
  std::unique_ptr<lzma_init_mt_extra_parameters> otf_xz_extra_params = std::make_unique<lzma_init_mt_extra_parameters>();
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
  // Input stream OTF decompression method has to be set AFTER the Precomp header has been read, as the compressed stream starts just after it
  void enable_input_stream_otf_decompression();
  void enable_output_stream_otf_compression(int otf_compression_method);

  void set_progress_callback(std::function<void(float)> callback);
  void call_progress_callback();

  int conversion_from_method;
  
};

class precompression_result
{
protected:
  virtual void dump_header_to_outfile(Precomp& precomp_mgr) const;
  void dump_penaltybytes_to_outfile(Precomp& precomp_mgr) const;
  void dump_stream_sizes_to_outfile(Precomp& precomp_mgr) const;
  virtual void dump_precompressed_data_to_outfile(Precomp& precomp_mgr);
public:
  explicit precompression_result(SupportedFormats format) : success(false), format(format) {}

  bool success;
  char format;
  std::byte flags{ 0 };
  std::vector<char> penalty_bytes;
  long long original_size = -1;
  long long input_pos_extra_add = 0;  // this is so we can skip headers and stuff like that as well as the precompressed stream, after the module is done
  long long precompressed_size = -1;
  std::unique_ptr<IStreamLike> precompressed_stream;

  virtual void dump_to_outfile(Precomp& precomp_mgr);
  virtual long long input_pos_add_offset() { return input_pos_extra_add + original_size - 1; }
};

// All this stuff was moved from precomp.h, most likely doesn't make sense as part of the API, TODO: delete/modularize/whatever stuff that shouldn't be here

bool intense_mode_is_active(Precomp& precomp_mgr);
bool brute_mode_is_active(Precomp& precomp_mgr);
unsigned long long compare_files(Precomp& precomp_mgr, IStreamLike& file1, IStreamLike& file2, unsigned int pos1, unsigned int pos2);
std::tuple<long long, std::vector<char>> compare_files_penalty(Precomp& precomp_mgr, RecursionContext& context, IStreamLike& file1, IStreamLike& file2, long long pos1, long long pos2);

// helpers for try_decompression functions
int32_t fin_fget32(IStreamLike& input);
long long fin_fget_vlint(IStreamLike& input);
void fout_fput32_little_endian(OStreamLike& output, unsigned int v);
void fout_fput32(OStreamLike& output, unsigned int v);
void fout_fput_vlint(OStreamLike& output, unsigned long long v);

struct recursion_result {
  bool success;
  std::string file_name;
  long long file_length;
  std::unique_ptr<std::ifstream> frecurse = std::make_unique<std::ifstream>();
};
recursion_result recursion_compress(Precomp& precomp_mgr, long long compressed_bytes, long long decompressed_bytes, PrecompTmpFile& tmpfile, bool deflate_type = false, std::vector<unsigned char> in_memory = std::vector<unsigned char>());

class RecursionPasstroughStream : public IStreamLike, public OStreamLike {
  // boost::circular_buffer would be a good idea here
  std::vector<unsigned char> buffer{};
  unsigned int buffer_already_read_count = 0;
  unsigned int accumulated_already_read_count = 0;

  std::thread thread;
  std::mutex mtx;
  std::condition_variable data_needed_cv;  // this is used to signal that reads had to stop because all buffered data was already used
  std::condition_variable data_available_cv;  // this is used to signal that writes had to stop because the buffer is full

  bool write_eof = false;
  bool read_eof = false;

  long long _gcount = 0;

  unsigned int data_available() const { return buffer.size() - buffer_already_read_count; }
  const unsigned char* buffer_current_pos() const { return buffer.data() + buffer_already_read_count; }

public:
  std::unique_ptr<RecursionContext> ctx;

  RecursionPasstroughStream(std::unique_ptr<RecursionContext>&& ctx_);
  ~RecursionPasstroughStream();

  RecursionPasstroughStream& read(char* buff, std::streamsize count) override;

  std::istream::int_type get() override;

  std::streamsize gcount() override;
  RecursionPasstroughStream& seekg(std::istream::off_type offset, std::ios_base::seekdir dir) override;
  std::istream::pos_type tellg() override;
  bool eof() override;
  bool good() override;
  bool bad() override;
  void clear() override;

  RecursionPasstroughStream& write(const char* buf, std::streamsize count) override;

  RecursionPasstroughStream& put(char chr);

  void flush() override;
  std::ostream::pos_type tellp() override;
  OStreamLike& seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) override;
};
std::unique_ptr<RecursionPasstroughStream> recursion_decompress(RecursionContext& context, long long recursion_data_length, std::string tmpfile);
#endif // PRECOMP_DLL_H
