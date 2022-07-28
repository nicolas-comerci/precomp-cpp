#ifndef STDTHREAD_IMPORTED
#define STDTHREAD_IMPORTED
#include <thread>
#endif
#include <cstdio>
#include <vector>
#include <string>
#include <fstream>
#include <memory>

// Quick and dirty wrapper to move away from using naked FILE* and simplify later refactor to use iostreams,
// the idea being that we will be able to arbitrarily make most/any? io operations be from/to a file, memory or stdin/stdout
class FileWrapper {
protected:
  bool is_tmp_file = false;

public:
  std::shared_ptr<std::FILE> file_ptr = nullptr;
  std::string file_path;
  std::string file_mode;

  friend bool operator==(const FileWrapper& lhs, const std::FILE* rhs) {
    return rhs == lhs.file_ptr.get();
  }
  friend bool operator==(const std::FILE* lhs, const FileWrapper& rhs) {
    return lhs == rhs.file_ptr.get();
  }
  friend bool operator!=(const FileWrapper& lhs, const std::FILE* rhs) {
    return rhs != lhs.file_ptr.get();
  }
  friend bool operator!=(const std::FILE* lhs, const FileWrapper& rhs) {
    return lhs != rhs.file_ptr.get();
  }

  ~FileWrapper() {
    file_ptr = nullptr;
    if (is_tmp_file) std::remove(file_path.c_str());
  }
 
  void open(std::string file_path, std::string mode) {
    this->file_path = file_path;
    this->file_mode = mode;
    // shared_ptr to FILE that closes itself when last instance is destroyed
    file_ptr = std::shared_ptr<std::FILE>(
      std::fopen(file_path.c_str(), mode.c_str()),
      [file_path](FILE* raw_file_ptr) {
        if (raw_file_ptr != nullptr) std::fclose(raw_file_ptr);
      }
    );
  }

  void reopen(std::string mode = "") {
    if (mode.empty()) mode = "a+";
    if (is_open()) close();
    open(file_path, file_mode);
    if (mode.compare("a+") == 0) fseek(file_ptr.get(), 0, SEEK_SET);
  }

  bool is_open() {
    return file_ptr != nullptr;
  }

  int close() {
    int result = is_open() ? 0 : std::char_traits<char>::eof();
    if (file_ptr != nullptr) result = std::fclose(file_ptr.get());
    file_ptr = nullptr;
    return result;
  }

  int flush() {
    return is_open() ? std::fflush(file_ptr.get()) : std::char_traits<char>::eof();
  }

  void seek_64(unsigned long long pos) const {
#ifndef __unix
    fpos_t fpt_pos = pos;
    fsetpos(file_ptr.get(), &fpt_pos);
#else
    fseeko(file_ptr.get(), pos, SEEK_SET);
#endif
  }

  void seekg(long long offset, int origin) const {
    if (origin == SEEK_SET) {
      seek_64(offset);
    }
    else {
      std::fseek(file_ptr.get(), offset, origin);
    }
  }

  void seekp(long long offset, int origin) const {
    seekg(offset, origin);
  }

  unsigned long long tell_64() const {
#ifndef __unix
    fpos_t fpt_pos;
    fgetpos(file_ptr.get(), &fpt_pos);
    return fpt_pos;
#else
    return ftello(file_ptr.get());
#endif
  }

  long long tellg() const {
    return tell_64();
  }

  long long tellp() const {
    return tellg();
  }

  size_t read(void* s, std::streamsize n) const {
    return file_ptr != nullptr ? fread(s, 1, n, file_ptr.get()) : 0;
  }

  int get() const {
    return file_ptr != nullptr ? fgetc(file_ptr.get()) : 0;
  }

  size_t write(const void* s, std::streamsize n) const {
    return file_ptr != nullptr ? fwrite(s, 1, n, file_ptr.get()) : 0;
  }

  int put(int chr) const {
    return file_ptr != nullptr ? std::fputc(chr, file_ptr.get()) : 0;
  }

  int printf(std::string str) {
    for (char character : str) {
      int result = put(character);
      if (result == 0) return 0;
    }
    return str.length();
  }

  bool fail() const {
    return file_ptr != nullptr ? ferror(file_ptr.get()) : true;
  }

  bool eof() const {
    return file_ptr != nullptr ? feof(file_ptr.get()) : false;
  }
};

class PrecompTmpFile: public FileWrapper {
public:
  PrecompTmpFile() {
    is_tmp_file = true;
  }
};

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

#ifndef DLL
#define DLL __declspec(dllexport)
#endif

DLL void get_copyright_msg(char* msg);
DLL bool precompress_file(char* in_file, char* out_file, char* msg, Switches switches);
DLL bool recompress_file(char* in_file, char* out_file, char* msg, Switches switches);
