#ifndef LIBPRECOMP_H
#define LIBPRECOMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "precomp_xz_params.h"

#ifdef __cplusplus
#define ExternC extern "C"
#else
#define ExternC
#endif


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

// This is provided as a courtesy for C users, as checking the size of a file in a compliant and portable way is unnecessarily hard, but pretty much trivial for us on C++17
ExternC LIBPRECOMP uintmax_t fileSize64(const char* filename, int* error_code);

typedef enum
{
  PRECOMP_NORMAL_LOG,
  PRECOMP_DEBUG_LOG
} PrecompLoggingLevels;

extern PrecompLoggingLevels PRECOMP_VERBOSITY_LEVEL; // (default: PRECOMP_NORMAL_LOG)

// You DON'T and SHOULDN'T delete the given char*, it comes from a C++ std::string and will free itself after your callback finishes running
// With that said, if for any reason you want to keep the message around instead of immediately printing/dumping it somewhere, make a copy or your program will go BOOM!
ExternC LIBPRECOMP void PrecompSetLoggingCallback(void(*callback)(PrecompLoggingLevels, char*));

#define P_NONE 0
#define P_COMPRESS 1
#define P_DECOMPRESS 2
#define P_CONVERT 3

ExternC LIBPRECOMP void PrecompGetCopyrightMsg(char* msg);

// Do NOT instantiate any of these structs directly (get them using PrecompGet/CreateX functions instead)
// if you do instantiate and attempt to use them, terrible things will happen to you and you will deserve every last bit of it.

typedef struct {
  bool DEBUG_MODE;               //debug mode (default: off)

  int compression_method;        //compression method to use (default: none)
  unsigned long long compression_otf_max_memory;    // max. memory for LZMA compression method (default: 2 GiB)
  unsigned int compression_otf_thread_count;  // max. thread count for LZMA compression method (default: auto-detect)

  bool intense_mode;             //intense mode (default: off)
  int intense_mode_depth_limit;
  bool fast_mode;                //fast mode (default: off)
  bool brute_mode;               //brute mode (default: off)
  int brute_mode_depth_limit;
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

  size_t preflate_meta_block_size;
  bool preflate_verify;

  //byte positions to ignore (default: none)
  long long* ignore_list_ptr;
  int ignore_list_count;
} CSwitches;

typedef struct {
  uintmax_t fin_length;
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
  bool header_already_read;

  int conversion_to_method;

  // recursion
  int recursion_depth;
  int max_recursion_depth;
  int max_recursion_depth_used;
  bool max_recursion_depth_reached;
} CPrecomp;

ExternC LIBPRECOMP CPrecomp* PrecompCreate();
ExternC LIBPRECOMP void PrecompSetProgressCallback(CPrecomp* precomp_mgr, void(*callback)(float));
ExternC LIBPRECOMP CSwitches* PrecompGetSwitches(CPrecomp* precomp_mgr);
ExternC LIBPRECOMP CRecursionContext* PrecompGetRecursionContext(CPrecomp* precomp_mgr);
ExternC LIBPRECOMP CResultStatistics* PrecompGetResultStatistics(CPrecomp* precomp_mgr);
ExternC LIBPRECOMP lzma_init_mt_extra_parameters* PrecompGetXzParameters(CPrecomp* precomp_mgr);

ExternC LIBPRECOMP typedef void* CPrecompIStream;
ExternC LIBPRECOMP void PrecompSetInputStream(CPrecomp* precomp_mgr, CPrecompIStream istream, const char* input_file_name);
ExternC LIBPRECOMP void PrecompSetInputFile(CPrecomp* precomp_mgr, FILE* fhandle, const char* input_file_name);
ExternC LIBPRECOMP typedef void* CPrecompOStream;
ExternC LIBPRECOMP void PrecompSetOutStream(CPrecomp* precomp_mgr, CPrecompOStream ostream, const char* output_file_name);
ExternC LIBPRECOMP void PrecompSetOutputFile(CPrecomp* precomp_mgr, FILE* fhandle, const char* output_file_name);

ExternC LIBPRECOMP int PrecompPrecompress(CPrecomp* precomp_mgr);
ExternC LIBPRECOMP int PrecompRecompress(CPrecomp* precomp_mgr);
ExternC LIBPRECOMP int PrecompConvert(CPrecomp* precomp_mgr);
ExternC LIBPRECOMP const char* PrecompReadHeader(CPrecomp* precomp_mgr, bool seek_to_beg);
ExternC LIBPRECOMP void PrecompConvertHeader(CPrecomp* precomp_mgr);

#endif