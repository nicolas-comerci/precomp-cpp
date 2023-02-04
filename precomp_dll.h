#ifndef STDTHREAD_IMPORTED
#define STDTHREAD_IMPORTED
#include <thread>
#endif
#include <cstdio>
#include <vector>
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

    bool compression_test_mode;
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
