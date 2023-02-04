#ifndef STDTHREAD_IMPORTED
#define STDTHREAD_IMPORTED
#include <thread>
#endif

#include "precomp_io.h"

#include <cstdio>
#include <array>
#include <vector>
#include <set>
#include <string>
#include <fstream>
#include <memory>

// Switches class
class Switches {
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
};

//Switches constructor
Switches::Switches() {
  compression_method = 2;
  compression_otf_max_memory = 2048;
  compression_otf_thread_count = std::thread::hardware_concurrency();
  if (compression_otf_thread_count == 0) {
    compression_otf_thread_count = 2;
  }

  intense_mode = false;
  fast_mode = false;
  brute_mode = false;
  pdf_bmp_mode = false;
  prog_only = false;
  use_mjpeg = true;
  use_brunsli = true;
  use_brotli = false;
  use_packjpg_fallback = true;
  DEBUG_MODE = false;
  min_ident_size = 4;
  
  use_pdf = true;
  use_zip = true;
  use_gzip = true;
  use_png = true;
  use_gif = true;
  use_jpg = true;
  use_mp3 = true;
  use_swf = true;
  use_base64 = true;
  use_bzip2 = true;
  level_switch_used = false;
  for (int i = 0; i < 81; i++) {
    use_zlib_level[i] = true;
  }
}

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

class RecursionContext {
public:
  long long fin_length;
  std::string input_file_name;
  std::string output_file_name;

  std::set<long long>* intense_ignore_offsets = new std::set<long long>();
  std::set<long long>* brute_ignore_offsets = new std::set<long long>();
  int compression_otf_method = OTF_XZ_MT;
  bool is_show_lzma_progress() { return compression_otf_method == OTF_XZ_MT; }
  unsigned char* decomp_io_buf = NULL;

  std::unique_ptr<std::istream> fin = std::unique_ptr<std::istream>(new std::ifstream());
  std::unique_ptr<std::ostream> fout = std::unique_ptr<std::ostream>(new std::ofstream());

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
  std::unique_ptr<RecursionContext> ctx = std::unique_ptr<RecursionContext>(new RecursionContext());
  std::vector<std::unique_ptr<RecursionContext>> recursion_contexts_stack;
};

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


#ifndef DLL
#define DLL __declspec(dllexport)
#endif

LIBPRECOMP void get_copyright_msg(char* msg);
LIBPRECOMP bool precompress_file(char* in_file, char* out_file, char* msg, Switches switches);
LIBPRECOMP bool recompress_file(char* in_file, char* out_file, char* msg, Switches switches);
