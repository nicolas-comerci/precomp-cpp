#ifndef PRECOMP_DLL_H
#define PRECOMP_DLL_H

#ifndef STDTHREAD_IMPORTED
#define STDTHREAD_IMPORTED
#include <thread>
#endif

#include "precomp_io.h"
#include "precomp_utils.h"

#include <cstdio>
#include <array>
#include <vector>
#include <set>
#include <string>
#include <fstream>
#include <memory>

#ifdef _MSC_VER
#define EXPORT __declspec(dllexport)
#define IMPORT __declspec(dllimport)
#else
#define EXPORT __attribute__((visibility("default")))
#define IMPORT
#endif

#ifdef PRECOMPDLL
#define LIBPRECOMP EXPORT
#else
#define LIBPRECOMP IMPORT
#endif

// Switches class
class EXPORT Switches {
  public:
    Switches();

    int compression_method;        //compression method to use (default: none)
    uint64_t compression_otf_max_memory;    // max. memory for LZMA compression method (default: 2 GiB)
    unsigned int compression_otf_thread_count;  // max. thread count for LZMA compression method (default: auto-detect)

    //byte positions to ignore (default: none)
    std::vector<long long> ignore_list;

    bool intense_mode;             //intense mode (default: off)
    bool fast_mode;                //fast mode (default: off)
    bool brute_mode;               //brute mode (default: off)
    bool pdf_bmp_mode;             //wrap BMP header around PDF images
                                   //  (default: off)
    bool prog_only;                //recompress progressive JPGs only
                                   //  (default: off)
    bool use_mjpeg;                //insert huffman table for MJPEG recompression
                                   //  (default: on)
    bool use_brunsli;              //use brunsli for JPG compression
                                   //  (default: on)
    bool use_brotli;               //use brotli for JPG metadata when brunsli is used
                                   //  (default: off)
    bool use_packjpg_fallback;     //use packJPG for JPG compression (fallback when brunsli fails)
                                   //  (default: on)
    bool DEBUG_MODE;               //debug mode (default: off)

    unsigned int min_ident_size;   //minimal identical bytes (default: 4)

    //(p)recompression types to use (default: all)
    bool use_pdf;
    bool use_zip;
    bool use_gzip;
    bool use_png;
    bool use_gif;
    bool use_jpg;
    bool use_mp3;
    bool use_swf;
    bool use_base64;
    bool use_bzip2;

    bool level_switch_used;            //level switch used? (default: no)
    bool use_zlib_level[81];      //compression levels to use (default: all)
    int otf_xz_filter_used_count = 0;

    int intense_mode_depth_limit = -1;
    int brute_mode_depth_limit = -1;

    // preflate config
    size_t preflate_meta_block_size = 1 << 21; // 2 MB blocks by default
    bool preflate_verify = false;
};

// Some variables I think are obsolete, not deleting them yet while other refactoring is in progress
class ObsoleteData {
public:
  // I think these stopped being used when preflate was integrated into precomp
  std::array<int, 81> comp_mem_level_count;
  std::array<bool, 81> zlib_level_was_used;
};

class ResultStatistics {
public:
  unsigned int recompressed_streams_count = 0;
  unsigned int recompressed_pdf_count = 0;
  unsigned int recompressed_pdf_count_8_bit = 0;
  unsigned int recompressed_pdf_count_24_bit = 0;
  unsigned int recompressed_zip_count = 0;
  unsigned int recompressed_gzip_count = 0;
  unsigned int recompressed_png_count = 0;
  unsigned int recompressed_png_multi_count = 0;
  unsigned int recompressed_gif_count = 0;
  unsigned int recompressed_jpg_count = 0;
  unsigned int recompressed_jpg_prog_count = 0;
  unsigned int recompressed_mp3_count = 0;
  unsigned int recompressed_swf_count = 0;
  unsigned int recompressed_base64_count = 0;
  unsigned int recompressed_bzip2_count = 0;
  unsigned int recompressed_zlib_count = 0;    // intense mode
  unsigned int recompressed_brute_count = 0;   // brute mode

  unsigned int decompressed_streams_count = 0;
  unsigned int decompressed_pdf_count = 0;
  unsigned int decompressed_pdf_count_8_bit = 0;
  unsigned int decompressed_pdf_count_24_bit = 0;
  unsigned int decompressed_zip_count = 0;
  unsigned int decompressed_gzip_count = 0;
  unsigned int decompressed_png_count = 0;
  unsigned int decompressed_png_multi_count = 0;
  unsigned int decompressed_gif_count = 0;
  unsigned int decompressed_jpg_count = 0;
  unsigned int decompressed_jpg_prog_count = 0;
  unsigned int decompressed_mp3_count = 0;
  unsigned int decompressed_swf_count = 0;
  unsigned int decompressed_base64_count = 0;
  unsigned int decompressed_bzip2_count = 0;
  unsigned int decompressed_zlib_count = 0;    // intense mode
  unsigned int decompressed_brute_count = 0;   // brute mode
};

#define P_NONE 0
#define P_COMPRESS 1
#define P_DECOMPRESS 2
#define P_CONVERT 3

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
class RecursionContext {
public:
  RecursionContext(Precomp& instance): precomp_owner(instance) {}

  Precomp& precomp_owner; // The precomp instance that owns this context

  long long fin_length;
  std::string input_file_name;
  std::string output_file_name;

  std::set<long long>* intense_ignore_offsets = new std::set<long long>();
  std::set<long long>* brute_ignore_offsets = new std::set<long long>();
  int compression_otf_method = OTF_XZ_MT;
  bool is_show_lzma_progress() { return compression_otf_method == OTF_XZ_MT; }
  std::array<unsigned char, MAX_IO_BUFFER_SIZE> decomp_io_buf;

  std::unique_ptr<WrappedIStream> fin = std::unique_ptr<WrappedIStream>(new WrappedIStream(new std::ifstream(), true));
  void set_input_stream(std::istream* istream, bool take_ownership = true);
  std::unique_ptr<ObservableOStream> fout = std::unique_ptr<ObservableOStream>(new ObservableOStream(new std::ofstream(), true));
  void set_output_stream(std::ostream* ostream, bool take_ownership = true);

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

  bool anything_was_used;
  bool non_zlib_was_used;

  // Mp3 stuff
  long long suppress_mp3_type_until[16];
  long long suppress_mp3_big_value_pairs_sum;
  long long suppress_mp3_non_zero_padbits_sum;
  long long suppress_mp3_inconsistent_emphasis_sum;
  long long suppress_mp3_inconsistent_original_bit;
  long long mp3_parsing_cache_second_frame;
  long long mp3_parsing_cache_n;
  long long mp3_parsing_cache_mp3_length;

};

class Precomp {
public:
  ObsoleteData obsolete;
  Switches switches;
  ResultStatistics statistics;
  std::unique_ptr<lzma_init_mt_extra_parameters> otf_xz_extra_params = std::unique_ptr<lzma_init_mt_extra_parameters>(new lzma_init_mt_extra_parameters());
  std::unique_ptr<RecursionContext> ctx = std::unique_ptr<RecursionContext>(new RecursionContext(*this));
  std::vector<std::unique_ptr<RecursionContext>> recursion_contexts_stack;

  // Useful so we can easily get (for example) info on the original input/output streams at any time
  std::unique_ptr<RecursionContext>& get_original_context();
  void set_input_stream(std::istream* istream, bool take_ownership = true);
  void set_output_stream(std::ostream* ostream, bool take_ownership = true);
  // Input stream OTF decompression method has to be set AFTER the Precomp header has been read, as the compressed stream starts just after it
  void enable_input_stream_otf_decompression();
  void enable_output_stream_otf_compression(int otf_compression_method);

  unsigned char in[CHUNK];
  unsigned char out[CHUNK];
  unsigned char copybuf[COPY_BUF_SIZE];

  long long start_time;
  int conversion_from_method;
  int conversion_to_method;

  // recursion
  int recursion_depth = 0;
  int max_recursion_depth = 10;
  int max_recursion_depth_used = 0;
  bool max_recursion_depth_reached = false;
};

LIBPRECOMP void get_copyright_msg(char* msg);
LIBPRECOMP bool precompress_file(char* in_file, char* out_file, char* msg, Switches switches);
LIBPRECOMP bool recompress_file(char* in_file, char* out_file, char* msg, Switches switches);

void packjpg_mp3_dll_msg();

// All this stuff was moved from precomp.h, most likely doesn't make sense as part of the API, TODO: delete/modularize/whatever stuff that shouldn't be here

int def(std::istream& source, WrappedOStream& dest, int level, int windowbits, int memlevel);
long long def_compare(std::istream& compfile, int level, int windowbits, int memlevel, long long& decompressed_bytes_used, long long decompressed_bytes_total, bool in_memory);
int def_part(std::istream& source, WrappedOStream& dest, int level, int windowbits, int memlevel, long long stream_size_in, long long stream_size_out);
int def_part_skip(std::istream& source, WrappedOStream& dest, int level, int windowbits, int memlevel, long long stream_size_in, long long stream_size_out, int bmp_width);
void zerr(int ret);
bool intense_mode_is_active(Precomp& precomp_mgr);
bool brute_mode_is_active(Precomp& precomp_mgr);
int inf_bzip2(Precomp& precomp_mgr, WrappedIStream& source, WrappedOStream& dest, long long& compressed_stream_size, long long& decompressed_stream_size);
int def_bzip2(Precomp& precomp_mgr, std::istream& source, std::ostream& dest, int level);
long long file_recompress(std::istream& origfile, int compression_level, int windowbits, int memlevel, long long& decompressed_bytes_used, long long decomp_bytes_total, bool in_memory);
void write_decompressed_data(Precomp& precomp_mgr, WrappedOStream& ostream, long long byte_count, const char* decompressed_file_name);
void write_decompressed_data_io_buf(Precomp& precomp_mgr, long long byte_count, bool in_memory, const char* decompressed_file_name);
unsigned long long compare_files(WrappedIStream& file1, WrappedIStream& file2, unsigned int pos1, unsigned int pos2);
long long compare_file_mem_penalty(RecursionContext& context, WrappedIStream& file1, unsigned char* input_bytes2, long long pos1, long long bytecount, long long& total_same_byte_count, long long& total_same_byte_count_penalty, long long& rek_same_byte_count, long long& rek_same_byte_count_penalty, long long& rek_penalty_bytes_len, long long& local_penalty_bytes_len, bool& use_penalty_bytes);
long long compare_files_penalty(RecursionContext& context, WrappedIStream& file1, WrappedIStream& file2, long long pos1, long long pos2);
void start_uncompressed_data(RecursionContext& context);
void end_uncompressed_data(Precomp& precomp_mgr);
void try_decompression_pdf(Precomp& precomp_mgr, int windowbits, int pdf_header_length, int img_width, int img_height, int img_bpc, PrecompTmpFile& tmpfile);
void try_decompression_zip(Precomp& precomp_mgr, int zip_header_length, PrecompTmpFile& tmpfile);
void try_decompression_gzip(Precomp& precomp_mgr, int gzip_header_length, PrecompTmpFile& tmpfile);
void try_decompression_png(Precomp& precomp_mgr, int windowbits, PrecompTmpFile& tmpfile);
void try_decompression_gif(Precomp& precomp_mgr, unsigned char version[5], PrecompTmpFile& tmpfile);
void try_decompression_jpg(Precomp& precomp_mgr, long long jpg_length, bool progressive_jpg, PrecompTmpFile& tmpfile);
void try_decompression_mp3(Precomp& precomp_mgr, long long mp3_length, PrecompTmpFile& tmpfile);
void try_decompression_zlib(Precomp& precomp_mgr, int windowbits, PrecompTmpFile& tmpfile);
void try_decompression_brute(Precomp& precomp_mgr, PrecompTmpFile& tmpfile);
void try_decompression_swf(Precomp& precomp_mgr, int windowbits, PrecompTmpFile& tmpfile);
void try_decompression_bzip2(Precomp& precomp_mgr, int compression_level, PrecompTmpFile& tmpfile);
void try_decompression_base64(Precomp& precomp_mgr, int gzip_header_length, PrecompTmpFile& tmpfile);
void try_decompression_png_multi(Precomp& precomp_mgr, WrappedIStream& fpng, int windowbits, PrecompTmpFile& tmpfile);

// helpers for try_decompression functions

void init_decompression_variables(RecursionContext& context);

bool is_valid_mp3_frame(unsigned char* frame_data, unsigned char header2, unsigned char header3, int protection);
inline unsigned short mp3_calc_layer3_crc(unsigned char header2, unsigned char header3, unsigned char* sideinfo, int sidesize);
void sort_comp_mem_levels();
void show_used_levels(Precomp& precomp_mgr);
int compress_file(Precomp& precomp_mgr, float min_percent = 0, float max_percent = 100);
int decompress_file(Precomp& precomp_mgr);
int convert_file(Precomp& precomp_mgr);
long long try_to_decompress(std::istream& file, int windowbits, long long& compressed_stream_size, bool& in_memory);
long long try_to_decompress_bzip2(Precomp& precomp_mgr, WrappedIStream& file, int compression_level, long long& compressed_stream_size, PrecompTmpFile& tmpfile);
void try_recompress(std::istream& origfile, int comp_level, int mem_level, int windowbits, long long& compressed_stream_size, long long decomp_bytes_total, bool in_memory);
void write_header(RecursionContext& context);
void read_header(Precomp& precomp_mgr);
void convert_header(Precomp& precomp_mgr);
std::fstream& tryOpen(const char* filename, std::ios_base::openmode mode);
long long fileSize64(const char* filename);
void print64(long long i64);
std::string temp_files_tag();

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
struct recompress_deflate_result;

int32_t fin_fget32_little_endian(std::istream& input);
int32_t fin_fget32(WrappedIStream& input);
long long fin_fget_vlint(WrappedIStream& input);
void fin_fget_deflate_hdr(WrappedIStream& input, WrappedOStream& output, recompress_deflate_result&, const unsigned char flags, unsigned char* hdr_data, unsigned& hdr_length, const bool inc_last);
void fin_fget_recon_data(WrappedIStream& input, recompress_deflate_result&);
bool fin_fget_deflate_rec(Precomp& precomp_mgr, recompress_deflate_result&, const unsigned char flags, unsigned char* hdr, unsigned& hdr_length, const bool inc_last, int64_t& rec_length, PrecompTmpFile& tmpfile);
void fin_fget_uncompressed(const recompress_deflate_result&);
void fout_fput32_little_endian(WrappedOStream& output, int v);
void fout_fput32(WrappedOStream& output, int v);
void fout_fput32(WrappedOStream& output, unsigned int v);
void fout_fput_vlint(WrappedOStream& output, unsigned long long v);
void fout_fput_deflate_hdr(WrappedOStream& output, const unsigned char type, const unsigned char flags, const recompress_deflate_result&, const unsigned char* hdr_data, const unsigned hdr_length, const bool inc_last);
void fout_fput_recon_data(WrappedOStream& output, const recompress_deflate_result&);
void fout_fput_uncompressed(Precomp& precomp_mgr, const recompress_deflate_result&, PrecompTmpFile& tmpfile);

void fast_copy(Precomp& precomp_mgr, WrappedIStream& file1, WrappedOStream& file2, long long bytecount, bool update_progress = false);

unsigned char base64_char_decode(unsigned char c);
void base64_reencode(Precomp& precomp_mgr, WrappedIStream& file_in, WrappedOStream& file_out, int line_count, unsigned int* base64_line_len, long long max_in_count = 0x7FFFFFFFFFFFFFFF, long long max_byte_count = 0x7FFFFFFFFFFFFFFF);

void try_recompression_gif(Precomp& precomp_mgr, unsigned char& header1, std::string& tempfile, std::string& tempfile2);

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
