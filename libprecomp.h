#ifndef LIBPRECOMP_H
#define LIBPRECOMP_H

enum PrecompLoggingLevels
{
  PRECOMP_NORMAL_LOG,
  PRECOMP_DEBUG_LOG
};

extern PrecompLoggingLevels PRECOMP_VERBOSITY_LEVEL; // (default: PRECOMP_NORMAL_LOG)

// You DON'T and SHOULDN'T delete the given char*, it comes from a C++ std::string and will free itself after your callback finishes running
// With that said, if for any reason you want to keep the message around instead of immediately printing/dumping it somewhere, make a copy or your program will go BOOM!
void PrecompSetLoggingCallback(void(*callback)(PrecompLoggingLevels, char*));

#define P_NONE 0
#define P_COMPRESS 1
#define P_DECOMPRESS 2
#define P_CONVERT 3

// Do NOT instantiate any of these structs directly (get them using PrecompGet/CreateX functions instead)
// if you do instantiate and attempt to use them, terrible things will happen to you and you will deserve every last bit of it.

typedef struct {
  bool DEBUG_MODE;               //debug mode (default: off)

  int compression_method;        //compression method to use (default: none)
  unsigned long long compression_otf_max_memory;    // max. memory for LZMA compression method (default: 2 GiB)
  unsigned int compression_otf_thread_count;  // max. thread count for LZMA compression method (default: auto-detect)

  bool intense_mode;             //intense mode (default: off)
  int intense_mode_depth_limit = -1;
  bool fast_mode;                //fast mode (default: off)
  bool brute_mode;               //brute mode (default: off)
  int brute_mode_depth_limit = -1;
  bool pdf_bmp_mode;             //wrap BMP header around PDF images (default: off)
  bool prog_only;                //recompress progressive JPGs only (default: off)
  bool use_mjpeg;                //insert huffman table for MJPEG recompression (default: on)
  bool use_brunsli;              //use brunsli for JPG compression (default: on)
  bool use_brotli;               //use brotli for JPG metadata when brunsli is used (default: off)
  bool use_packjpg_fallback;     //use packJPG for JPG compression (fallback when brunsli fails) (default: on)
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

  // preflate config
  size_t preflate_meta_block_size = 1 << 21; // 2 MB blocks by default
  bool preflate_verify = false;

  //byte positions to ignore (default: none)
  long long* ignore_list_ptr;
  int ignore_list_count;
} CSwitches;

typedef struct {
  long long fin_length;
  int compression_otf_method ;

  bool anything_was_used;
  bool non_zlib_was_used;
} CRecursionContext;

typedef struct {
  unsigned int recompressed_streams_count;
  unsigned int recompressed_pdf_count;
  unsigned int recompressed_pdf_count_8_bit;
  unsigned int recompressed_pdf_count_24_bit;
  unsigned int recompressed_zip_count;
  unsigned int recompressed_gzip_count;
  unsigned int recompressed_png_count;
  unsigned int recompressed_png_multi_count;
  unsigned int recompressed_gif_count;
  unsigned int recompressed_jpg_count;
  unsigned int recompressed_jpg_prog_count;
  unsigned int recompressed_mp3_count;
  unsigned int recompressed_swf_count;
  unsigned int recompressed_base64_count;
  unsigned int recompressed_bzip2_count;
  unsigned int recompressed_zlib_count;    // intense mode
  unsigned int recompressed_brute_count;   // brute mode

  unsigned int decompressed_streams_count;
  unsigned int decompressed_pdf_count;
  unsigned int decompressed_pdf_count_8_bit;
  unsigned int decompressed_pdf_count_24_bit;
  unsigned int decompressed_zip_count;
  unsigned int decompressed_gzip_count;
  unsigned int decompressed_png_count;
  unsigned int decompressed_png_multi_count;
  unsigned int decompressed_gif_count;
  unsigned int decompressed_jpg_count;
  unsigned int decompressed_jpg_prog_count;
  unsigned int decompressed_mp3_count;
  unsigned int decompressed_swf_count;
  unsigned int decompressed_base64_count;
  unsigned int decompressed_bzip2_count;
  unsigned int decompressed_zlib_count;    // intense mode
  unsigned int decompressed_brute_count;   // brute mode
} CResultStatistics;

typedef struct {
  long long start_time;

  int conversion_to_method;

  // recursion
  int recursion_depth = 0;
  int max_recursion_depth = 10;
  int max_recursion_depth_used = 0;
  bool max_recursion_depth_reached = false;
} CPrecomp;

CPrecomp* PrecompCreate();
void PrecompSetProgressCallback(CPrecomp* precomp_mgr, void(*callback)(float));
CSwitches* PrecompGetSwitches(CPrecomp* precomp_mgr);
CRecursionContext* PrecompGetRecursionContext(CPrecomp* precomp_mgr);
CResultStatistics* PrecompGetResultStatistics(CPrecomp* precomp_mgr);
struct lzma_init_mt_extra_parameters;
lzma_init_mt_extra_parameters* PrecompGetXzParameters(CPrecomp* precomp_mgr);

typedef void* CPrecompIStream;
void PrecompSetInputStream(CPrecomp* precomp_mgr, CPrecompIStream istream, const char* input_file_name);
typedef void* CPrecompOStream;
void PrecompSetOutStream(CPrecomp* precomp_mgr, CPrecompOStream ostream, const char* output_file_name);

int PrecompPrecompress(CPrecomp* precomp_mgr);
int PrecompRecompress(CPrecomp* precomp_mgr);
int PrecompConvert(CPrecomp* precomp_mgr);
const char* PrecompReadHeader(CPrecomp* precomp_mgr, bool seek_to_beg);
void PrecompConvertHeader(CPrecomp* precomp_mgr);

#endif