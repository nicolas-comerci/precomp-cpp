#ifndef LIBPRECOMP_H
#define LIBPRECOMP_H

// Do NOT instantiate any of these structs directly (get them using PrecompGetX functions instead)
// if you do instantiate them and attempt to use them, terrible things will happen to you and you will deserve every last bit of it.

typedef struct {
  bool DEBUG_MODE;               //debug mode (default: off)

  int compression_method;        //compression method to use (default: none)
  uint64_t compression_otf_max_memory;    // max. memory for LZMA compression method (default: 2 GiB)
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

struct Precomp;

Precomp* PrecompCreate();
void PrecompSetProgressCallback(Precomp* precomp_mgr, void(*callback)(float));
CSwitches* PrecompGetSwitches(Precomp* precomp_mgr);
CRecursionContext* PrecompGetRecursionContext(Precomp* precomp_mgr);

#endif