#ifndef LIBPRECOMP_H
#define LIBPRECOMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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

#if PRECOMPDLL
#define LIBPRECOMP EXPORT
#elif !defined(PRECOMPSTATIC)
#define LIBPRECOMP IMPORT
#else
#define LIBPRECOMP
#endif

// This is provided as a courtesy for C users, as checking the size of a file in a compliant and portable way is unnecessarily hard, but pretty much trivial for us on C++17
ExternC LIBPRECOMP uintmax_t fileSize64(const char* filename, int* error_code);
// Similarly, because libprecomp allows writing output to stdout, we provide this function so you can continue printing to the console/terminal while outputting to stdout
ExternC LIBPRECOMP void print_to_terminal(const char* fmt, ...);

typedef enum
{
  STDIN_HANDLE =  0,
  STDOUT_HANDLE = 1,
  STDERR_HANDLE = 2,
} StdHandles;
// If you are going to feed Precomp, or output from Precomp using a STD handle, you need to ensure it is set to work in binary mode.
// On Unix systems this is just how it is, but on Windows it needs to be set explicitly.
// You can just use this function to ensure your program works in both, if on unix it will just be a no-op.
// NOTE: Precomp will autodetect if given a FILE*/(i|o)stream that happens to be stdin or stdout, and set binary mode on its own,
// so you might get away with not calling this at any point, but if you use stderr or a generic input/output stream that autodetection will NOT happen.
ExternC LIBPRECOMP void set_std_handle_binary_mode(StdHandles handle);

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
#define P_PRECOMPRESS 1
#define P_RECOMPRESS 2

ExternC LIBPRECOMP void PrecompGetCopyrightMsg(char* msg);

// Do NOT instantiate any of these structs directly (get them using PrecompGet/CreateX functions instead)
// if you do instantiate and attempt to use them, terrible things will happen to you and you will deserve every last bit of it.

typedef struct {
  bool DEBUG_MODE;               //debug mode (default: off)

  bool verify_precompressed;     // (default: on)
  uintmax_t uncompressed_block_length;  // (default: 100mb)
  bool intense_mode;             //intense mode (default: off)
  int intense_mode_depth_limit;
  bool brute_mode;               //brute mode (default: off)
  int brute_mode_depth_limit;
  bool pdf_bmp_mode;             //wrap BMP header around PDF images (default: off)
  bool prog_only;                //recompress progressive JPGs only (default: off)
  bool use_mjpeg;                //insert huffman table for MJPEG recompression (default: on)
  bool use_brunsli;              //use brunsli for JPG compression (default: on)
  bool use_packjpg_fallback;     //use packJPG for JPG compression (fallback when brunsli fails) (default: on)
  unsigned int min_ident_size;   //minimal identical bytes (default: 4)

  // This string will be freed when Precomp is destroyed so don't double free it afterwards
  char* working_dir;

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

  int max_recursion_depth;
} CSwitches;

typedef struct {
  uintmax_t fin_length;
} CRecursionContext;

typedef struct {
  unsigned int precompressed_streams_count;
  unsigned int decompressed_streams_count;
  // recursion
  int max_recursion_depth_used;
  bool max_recursion_depth_reached;

  bool header_already_read;
} CResultStatistics;

typedef struct Precomp Precomp;

void packjpg_mp3_dll_msg();

ExternC LIBPRECOMP Precomp* PrecompCreate();
ExternC LIBPRECOMP void PrecompDestroy(Precomp* precomp_mgr);
ExternC LIBPRECOMP void PrecompSetProgressCallback(Precomp* precomp_mgr, void(*callback)(float));
ExternC LIBPRECOMP CSwitches* PrecompGetSwitches(Precomp* precomp_mgr);
// This COPIES the list into the Switches structure, so you are free to well, free the ignore_pos_list memory after setting it
ExternC LIBPRECOMP void PrecompSwitchesSetIgnoreList(CSwitches* precomp_switches, const long long* ignore_pos_list, size_t ignore_post_list_count);
ExternC LIBPRECOMP CRecursionContext* PrecompGetRecursionContext(Precomp* precomp_mgr);
ExternC LIBPRECOMP CResultStatistics* PrecompGetResultStatistics(Precomp* precomp_mgr);

// This is not really a realistic interface to access the results, but works for Precomp CLI for now
ExternC LIBPRECOMP void PrecompPrintResults(Precomp* precomp_mgr);

// IMPORTANT!! Input streams for precompression HAVE to be seekable, else it WILL fail.
// For recompression no seeking is done so in those cases its okay to have input streams that can't seek.
ExternC LIBPRECOMP typedef void* PrecompIStream;
ExternC LIBPRECOMP void PrecompSetInputStream(Precomp* precomp_mgr, PrecompIStream istream, const char* input_file_name);
ExternC LIBPRECOMP void PrecompSetInputFile(Precomp* precomp_mgr, FILE* fhandle, const char* input_file_name);
// This allows you to customize exactly how you would like data to be fed to Precomp.
// You can use an instance ptr of anything you may want (Socket, Handle, something custom from your application) and functions you define about how to operate
// with that instance to do all of the different operations a C++ IStream might do, which is what Precomp uses (sort of).
ExternC LIBPRECOMP void PrecompSetGenericInputStream(
  Precomp* precomp_mgr, const char* input_file_name, void* backing_structure,
  // The read function uses your backing structure and somehow reads up to the long long param amount of bytes into the char* buffer
  size_t (*read_func)(void*, char*, long long),
  // The get function reads a single character using your backing structure, should return EOF if no more data is to be received anymore
  int (*get_func)(void*),
  // The seekg function semantics are the same as fseek, only difference being that it uses your backing structure instead of a FILE
  // As previously stated, for recompression no seeking is performed and thus you can just give a null pointer here if you are recompressing.
  int (*seekg_func)(void*, long long, int),
  // Similarily, tellg function is analogue to ftell
  long long (*tellg_func)(void*),

  // eof function returns true if EOF has been reached and no more data can be read from the backing structure, false otherwise
  bool (*eof_func)(void*),
  // bad function returns true if some unrecoverable error happened and the stream will no longer be usable, signals to Precomp that it should fail
  bool (*bad_func)(void*),
  // clear function clears the eof and bad flags, the backing structure should be set to attempt to continue as if everything is fine.
  // This is a lot of the time meaningless as any further operation is doomed to immediately hit EOF or an unrecoverable error again.
  // You might as well feed it an empty function if you don't want to bother.
  void (*clear_func)(void*)
);
#ifdef DEBUG
// Uses the given FILE and runs all read/get/seek/tell/etc operations in parallel between that and the already set Input stream so you can detect if your
// GenericInputStream behaves incorrectly at any point.
// Of course the FILE should have identical data and length as what you will stream through the GenericInputStream (so just dump that test data),
// and you need to call PrecompSetGenericInputStream before this.
ExternC LIBPRECOMP void PrecompSetDebugCompareInputFile(Precomp* precomp_mgr, FILE* fhandle);
#endif

ExternC LIBPRECOMP typedef void* PrecompOStream;
ExternC LIBPRECOMP void PrecompSetOutStream(Precomp* precomp_mgr, PrecompOStream ostream, const char* output_file_name);
ExternC LIBPRECOMP void PrecompSetOutputFile(Precomp* precomp_mgr, FILE* fhandle, const char* output_file_name);
// Same thing as PrecompSetGenericInputStream but for the output
ExternC LIBPRECOMP void PrecompSetGenericOutputStream(
  Precomp* precomp_mgr, const char* output_file_name, void* backing_structure,
  // The write function gives a buffer and a byte count to your backing structure, which should read that count of bytes from the buffer and write/dump them somewhere
  size_t(*write_func)(void*, char const*, long long),
  // The put function puts a single character into your backing structure
  int (*put_func)(void*, int),
  // Idem seekg on the PrecompSetGenericInputStream, however seeking should not be required for either precompression nor recompression... maybe I should remove this param
  int (*seekp_func)(void*, long long, int),
  // Idem PrecompSetGenericInputStream tellg
  long long (*tellp_func)(void*),

  // These 3 parameters are identical to those on PrecompSetGenericInputStream
  bool (*eof_func)(void*),
  bool (*bad_func)(void*),
  void (*clear_func)(void*)
);

ExternC LIBPRECOMP int PrecompPrecompress(Precomp* precomp_mgr);
ExternC LIBPRECOMP int PrecompRecompress(Precomp* precomp_mgr);
ExternC LIBPRECOMP int PrecompReadHeader(Precomp* precomp_mgr, bool seek_to_beg);
// Mostly useful to run after a successful PrecompReadHeader, to know the original filename of the precompressed file
ExternC LIBPRECOMP const char* PrecompGetOutputFilename(Precomp* precomp_mgr);
#endif