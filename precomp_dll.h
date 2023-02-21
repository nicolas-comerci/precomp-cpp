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
#include <vector>
#include <set>
#include <string>
#include <fstream>
#include <memory>

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
  using CSwitches::ignore_list_ptr;
  using CSwitches::ignore_list_count;
  public:
    Switches();
    
    std::set<long long> ignore_set();
};

class ResultStatistics: public CResultStatistics {
public:
  ResultStatistics();
};

constexpr auto IN_BUF_SIZE = 65536; //input buffer
constexpr auto DIV3CHUNK = 262143; // DIV3CHUNK is a bit smaller/larger than CHUNK, so that DIV3CHUNK mod 3 = 0
constexpr auto CHECKBUF_SIZE = 4096;
constexpr auto COPY_BUF_SIZE = 512;
constexpr auto FAST_COPY_WORK_SIGN_DIST = 64; // update work sign after (FAST_COPY_WORK_SIGN_DIST * COPY_BUF_SIZE) bytes
constexpr auto COMP_CHUNK = 512;
constexpr auto PENALTY_BYTES_TOLERANCE = 160;
constexpr auto IDENTICAL_COMPRESSED_BYTES_TOLERANCE = 32;
constexpr auto MAX_IO_BUFFER_SIZE = 64 * 1024 * 1024;

class Precomp;
class RecursionContext: public CRecursionContext {
public:
  RecursionContext(Precomp& instance);

  Precomp& precomp_owner; // The precomp instance that owns this context

  std::set<long long>* intense_ignore_offsets = new std::set<long long>();
  std::set<long long>* brute_ignore_offsets = new std::set<long long>();

  std::array<unsigned char, MAX_IO_BUFFER_SIZE> decomp_io_buf;

  std::unique_ptr<IStreamLike> fin = std::unique_ptr<WrappedIStream>(new WrappedIStream(new std::ifstream(), true));
  void set_input_stream(std::istream* istream, bool take_ownership = true);
  void set_input_stream(FILE* fhandle, bool take_ownership = true);
  std::unique_ptr<ObservableOStream> fout = std::unique_ptr<ObservableOStream>(new ObservableWrappedOStream(new std::ofstream(), true));
  void set_output_stream(std::ostream* ostream, bool take_ownership = true);
  void set_output_stream(FILE* fhandle, bool take_ownership = true);

  float global_min_percent = 0;
  float global_max_percent = 100;
  int comp_decomp_state = P_NONE;

  long long input_file_pos;
  unsigned char in_buf[IN_BUF_SIZE];
  int cb; // "checkbuf"
  long long saved_input_file_pos;
  long long saved_cb;

  bool compressed_data_found;

  // Uncompressed data info
  long long uncompressed_pos;
  bool uncompressed_data_in_work;
  long long uncompressed_length = -1;
  long long uncompressed_bytes_total = 0;
  long long uncompressed_bytes_written = 0;

  long long retval;

  // penalty bytes
#define MAX_PENALTY_BYTES 16384
  std::array<char, MAX_PENALTY_BYTES> penalty_bytes;
  int penalty_bytes_len = 0;
  std::array<char, MAX_PENALTY_BYTES> best_penalty_bytes;
  int best_penalty_bytes_len = 0;
  std::array<char, MAX_PENALTY_BYTES> local_penalty_bytes;

  long long identical_bytes = -1;
  long long identical_bytes_decomp = -1;
  long long best_identical_bytes = -1;
  long long best_identical_bytes_decomp = -1;
};

class Precomp: public CPrecomp {
  std::function<void(float)> progress_callback;
  void set_input_stdin();
  void set_output_stdout();
  void register_output_observer_callbacks();

public:
  Precomp();

  Switches switches;
  ResultStatistics statistics;
  std::unique_ptr<lzma_init_mt_extra_parameters> otf_xz_extra_params = std::unique_ptr<lzma_init_mt_extra_parameters>(new lzma_init_mt_extra_parameters());
  std::unique_ptr<RecursionContext> ctx = std::unique_ptr<RecursionContext>(new RecursionContext(*this));
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

  unsigned char in[CHUNK];
  unsigned char out[CHUNK];
  unsigned char copybuf[COPY_BUF_SIZE];

  int conversion_from_method;
  
};

class precompression_result
{
protected:
  virtual void dump_header_to_outfile(Precomp& precomp_mgr) const;
  void dump_penaltybytes_to_outfile(Precomp& precomp_mgr) const;
  void dump_stream_sizes_to_outfile(Precomp& precomp_mgr);
  virtual void dump_precompressed_data_to_outfile(Precomp& precomp_mgr);
public:
  precompression_result(SupportedFormats format) : success(false), format(format) {}

  bool success;
  char format;
  char flags = 0;
  std::vector<char> penalty_bytes;
  long long original_size = -1;
  long long precompressed_size = -1;
  std::unique_ptr<IStreamLike> precompressed_stream;

  virtual void dump_to_outfile(Precomp& precomp_mgr);
  virtual long long input_pos_add_offset() { return original_size - 1; }
};

void packjpg_mp3_dll_msg();

// All this stuff was moved from precomp.h, most likely doesn't make sense as part of the API, TODO: delete/modularize/whatever stuff that shouldn't be here

int def(std::istream& source, OStreamLike& dest, int level, int windowbits, int memlevel);
long long def_compare(std::istream& compfile, int level, int windowbits, int memlevel, long long& decompressed_bytes_used, long long decompressed_bytes_total, bool in_memory);
int def_part(std::istream& source, OStreamLike& dest, int level, int windowbits, int memlevel, long long stream_size_in, long long stream_size_out);
int def_part_skip(std::istream& source, OStreamLike& dest, int level, int windowbits, int memlevel, long long stream_size_in, long long stream_size_out, int bmp_width);
void zerr(int ret);
bool intense_mode_is_active(Precomp& precomp_mgr);
bool brute_mode_is_active(Precomp& precomp_mgr);
int inf_bzip2(Precomp& precomp_mgr, IStreamLike& source, OStreamLike& dest, long long& compressed_stream_size, long long& decompressed_stream_size);
int def_bzip2(Precomp& precomp_mgr, std::istream& source, std::ostream& dest, int level);
long long file_recompress(std::istream& origfile, int compression_level, int windowbits, int memlevel, long long& decompressed_bytes_used, long long decomp_bytes_total, bool in_memory);
void write_decompressed_data(Precomp& precomp_mgr, OStreamLike& ostream, long long byte_count, const char* decompressed_file_name);
void write_decompressed_data_io_buf(Precomp& precomp_mgr, long long byte_count, bool in_memory, const char* decompressed_file_name);
unsigned long long compare_files(Precomp& precomp_mgr, IStreamLike& file1, IStreamLike& file2, unsigned int pos1, unsigned int pos2);
long long compare_file_mem_penalty(RecursionContext& context, IStreamLike& file1, unsigned char* input_bytes2, long long pos1, long long bytecount, long long& total_same_byte_count, long long& total_same_byte_count_penalty, long long& rek_same_byte_count, long long& rek_same_byte_count_penalty, long long& rek_penalty_bytes_len, long long& local_penalty_bytes_len, bool& use_penalty_bytes);
std::tuple<long long, std::vector<char>> compare_files_penalty(Precomp& precomp_mgr, RecursionContext& context, IStreamLike& file1, IStreamLike& file2, long long pos1, long long pos2);
void start_uncompressed_data(RecursionContext& context);
void end_uncompressed_data(Precomp& precomp_mgr);
void try_decompression_pdf(Precomp& precomp_mgr, int windowbits, int pdf_header_length, int img_width, int img_height, int img_bpc, PrecompTmpFile& tmpfile);
void try_decompression_zip(Precomp& precomp_mgr, int zip_header_length, PrecompTmpFile& tmpfile);
void try_decompression_gzip(Precomp& precomp_mgr, int gzip_header_length, PrecompTmpFile& tmpfile);
void try_decompression_swf(Precomp& precomp_mgr, int windowbits, PrecompTmpFile& tmpfile);
void try_decompression_bzip2(Precomp& precomp_mgr, int compression_level, PrecompTmpFile& tmpfile);
void try_decompression_base64(Precomp& precomp_mgr, int gzip_header_length, PrecompTmpFile& tmpfile);

// helpers for try_decompression functions

void init_decompression_variables(RecursionContext& context);

void sort_comp_mem_levels();
int compress_file(Precomp& precomp_mgr, float min_percent = 0, float max_percent = 100);
int decompress_file(Precomp& precomp_mgr);
int convert_file(Precomp& precomp_mgr);
long long try_to_decompress(std::istream& file, int windowbits, long long& compressed_stream_size, bool& in_memory);
long long try_to_decompress_bzip2(Precomp& precomp_mgr, IStreamLike& file, int compression_level, long long& compressed_stream_size, PrecompTmpFile& tmpfile);
void try_recompress(std::istream& origfile, int comp_level, int mem_level, int windowbits, long long& compressed_stream_size, long long decomp_bytes_total, bool in_memory);
void write_header(Precomp& precomp_mgr);
void read_header(Precomp& precomp_mgr);
void convert_header(Precomp& precomp_mgr);
std::fstream& tryOpen(const char* filename, std::ios_base::openmode mode);
void print64(long long i64);

class zLibMTF {
  struct MTFItem {
    int Next, Previous;
  };
  alignas(16) MTFItem List[81];
  int Root, Index;
public:
  zLibMTF() : Root(0), Index(0) {
    for (int i = 0; i < 81; i++) {
      List[i].Next = i + 1;
      List[i].Previous = i - 1;
    }
    List[80].Next = -1;
  }
  inline int First() {
    return Index = Root;
  }
  inline int Next() {
    return (Index >= 0) ? Index = List[Index].Next : Index;
  }
  inline void Update() {
    if (Index == Root) return;

    List[List[Index].Previous].Next = List[Index].Next;
    if (List[Index].Next >= 0)
      List[List[Index].Next].Previous = List[Index].Previous;
    List[Root].Previous = Index;
    List[Index].Next = Root;
    List[Root = Index].Previous = -1;
  }
};

struct recompress_deflate_result {
  long long compressed_stream_size;
  long long uncompressed_stream_size;
  std::vector<unsigned char> recon_data;
  bool accepted;
  bool uncompressed_in_memory;
  bool zlib_perfect;
  char zlib_comp_level;
  char zlib_mem_level;
  char zlib_window_bits;
};

void debug_deflate_detected(RecursionContext& context, const recompress_deflate_result& rdres, const char* type);
void debug_sums(Precomp& precomp_mgr, const recompress_deflate_result& rdres);
void debug_pos(Precomp& precomp_mgr);

int32_t fin_fget32_little_endian(std::istream& input);
int32_t fin_fget32(IStreamLike& input);
long long fin_fget_vlint(IStreamLike& input);
void fin_fget_deflate_hdr(IStreamLike& input, OStreamLike& output, recompress_deflate_result&, const unsigned char flags, unsigned char* hdr_data, unsigned& hdr_length, const bool inc_last);
void fin_fget_recon_data(IStreamLike& input, recompress_deflate_result&);
void fin_fget_uncompressed(const recompress_deflate_result&);
void fout_fput32_little_endian(OStreamLike& output, int v);
void fout_fput32(OStreamLike& output, int v);
void fout_fput32(OStreamLike& output, unsigned int v);
void fout_fput_vlint(OStreamLike& output, unsigned long long v);
char make_deflate_pcf_hdr_flags(const recompress_deflate_result& rdres);
void fout_fput_deflate_hdr(OStreamLike& output, const unsigned char type, const unsigned char flags, const recompress_deflate_result&, const unsigned char* hdr_data, const unsigned hdr_length, const bool inc_last);
void fout_fput_recon_data(OStreamLike& output, const recompress_deflate_result&);
void fout_fput_uncompressed(Precomp& precomp_mgr, const recompress_deflate_result&, PrecompTmpFile& tmpfile);

void fast_copy(Precomp& precomp_mgr, IStreamLike& file1, OStreamLike& file2, long long bytecount, bool update_progress = false);

unsigned char base64_char_decode(unsigned char c);
void base64_reencode(Precomp& precomp_mgr, IStreamLike& file_in, OStreamLike& file_out, int line_count, unsigned int* base64_line_len, long long max_in_count = 0x7FFFFFFFFFFFFFFF, long long max_byte_count = 0x7FFFFFFFFFFFFFFF);

struct recursion_result {
  bool success;
  std::string file_name;
  long long file_length;
  std::shared_ptr<std::ifstream> frecurse = std::shared_ptr<std::ifstream>(new std::ifstream());
};
recursion_result recursion_compress(Precomp& precomp_mgr, long long compressed_bytes, long long decompressed_bytes, PrecompTmpFile& tmpfile, bool deflate_type = false, bool in_memory = true);
recursion_result recursion_decompress(Precomp& precomp_mgr, long long recursion_data_length, PrecompTmpFile& tmpfile);
recursion_result recursion_write_file_and_compress(Precomp& precomp_mgr, const recompress_deflate_result&, PrecompTmpFile& tmpfile);
void fout_fput_deflate_rec(Precomp& precomp_mgr, const unsigned char type, const recompress_deflate_result&, const unsigned char* hdr, const unsigned hdr_length, const bool inc_last, const recursion_result& recres, PrecompTmpFile& tmpfile);
#endif // PRECOMP_DLL_H
