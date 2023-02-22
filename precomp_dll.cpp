/* Copyright 2006-2021 Christian Schneider

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License. */

// version information
#define V_MAJOR 0
#define V_MINOR 4
#define V_MINOR2 8
//#define V_STATE "ALPHA"
#define V_STATE "DEVELOPMENT"
//#define V_MSG "USE FOR TESTING ONLY"
#define V_MSG "USE AT YOUR OWN RISK!"
#ifdef __unix
  #define V_OS "Unix"
#else
  #define V_OS "Windows"
#endif
#ifdef BIT64
  #define V_BIT "64-bit"
#else
  #define V_BIT "32-bit"
#endif

#define NOMINMAX

#include <stdio.h>
#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <fstream>
#include <sstream>
#include <string>
#include <array>
#include <signal.h>
#include <random>
#include <fcntl.h>
#include <filesystem>
#include <set>
#ifdef _MSC_VER
#include <io.h>
#define ftruncate _chsize_s
#endif

#ifndef __unix
#include <conio.h>
#include <windows.h>
#include <io.h>
#else
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#endif

#include "contrib/zlib/zlib.h"
#include "contrib/preflate/preflate.h"

#include "precomp_dll.h"

#include "formats/deflate.h"
#include "formats/zlib.h"
#include "formats/zip.h"
#include "formats/gzip.h"
#include "formats/mp3.h"
#include "formats/pdf.h"
#include "formats/jpeg.h"
#include "formats/gif.h"
#include "formats/png.h"
#include "formats/swf.h"

// This I shamelessly lifted from https://web.archive.org/web/20090907131154/http://www.cs.toronto.edu:80/~ramona/cosmin/TA/prog/sysconf/
// (credit to this StackOverflow answer for pointing me to it https://stackoverflow.com/a/1613677)
// It allows us to portably (at least for Windows/Linux/Mac) set a std stream as binary
#define STDIN  0
#define STDOUT 1
#define STDERR 2
#ifndef __unix
# define SET_BINARY_MODE(handle) setmode(handle, O_BINARY)
#else
# define SET_BINARY_MODE(handle) ((void)0)
#endif

PrecompLoggingLevels PRECOMP_VERBOSITY_LEVEL = PRECOMP_NORMAL_LOG;

std::function<void(PrecompLoggingLevels, char*)> logging_callback;

void PrecompSetLoggingCallback(void(*callback)(PrecompLoggingLevels, char*)) {
  logging_callback = callback;
}

void print_to_log(PrecompLoggingLevels log_level, std::string format) {
  if (PRECOMP_VERBOSITY_LEVEL < log_level || !logging_callback) return;
  logging_callback(log_level, format.data());
}

void precompression_result::dump_header_to_outfile(Precomp& precomp_mgr) const {
  // write compressed data header
  precomp_mgr.ctx->fout->put(flags);
  precomp_mgr.ctx->fout->put(format);
}

void precompression_result::dump_penaltybytes_to_outfile(Precomp& precomp_mgr) const {
  if (penalty_bytes.empty()) return;
  fout_fput_vlint(*precomp_mgr.ctx->fout, penalty_bytes.size());

  for (auto& chr: penalty_bytes) {
    precomp_mgr.ctx->fout->put(chr);
  }
}

void precompression_result::dump_stream_sizes_to_outfile(Precomp& precomp_mgr) {
  fout_fput_vlint(*precomp_mgr.ctx->fout, original_size);
  fout_fput_vlint(*precomp_mgr.ctx->fout, precompressed_size);
}

void precompression_result::dump_precompressed_data_to_outfile(Precomp& precomp_mgr) {
  fast_copy(precomp_mgr, *precompressed_stream, *precomp_mgr.ctx->fout, precompressed_size);
}

void precompression_result::dump_to_outfile(Precomp& precomp_mgr) {
  dump_header_to_outfile(precomp_mgr);
  dump_penaltybytes_to_outfile(precomp_mgr);
  dump_stream_sizes_to_outfile(precomp_mgr);
  dump_precompressed_data_to_outfile(precomp_mgr);
}


zLibMTF MTF;

int min_ident_size_intense_brute_mode = 64;

unsigned char zlib_header[2];
unsigned int* idat_lengths = NULL;
unsigned int* idat_crcs = NULL;

ResultStatistics::ResultStatistics(): CResultStatistics() {
  recompressed_streams_count = 0;
  recompressed_pdf_count = 0;
  recompressed_pdf_count_8_bit = 0;
  recompressed_pdf_count_24_bit = 0;
  recompressed_zip_count = 0;
  recompressed_gzip_count = 0;
  recompressed_png_count = 0;
  recompressed_png_multi_count = 0;
  recompressed_gif_count = 0;
  recompressed_jpg_count = 0;
  recompressed_jpg_prog_count = 0;
  recompressed_mp3_count = 0;
  recompressed_swf_count = 0;
  recompressed_base64_count = 0;
  recompressed_bzip2_count = 0;
  recompressed_zlib_count = 0;    // intense mode
  recompressed_brute_count = 0;   // brute mode

  decompressed_streams_count = 0;
  decompressed_pdf_count = 0;
  decompressed_pdf_count_8_bit = 0;
  decompressed_pdf_count_24_bit = 0;
  decompressed_zip_count = 0;
  decompressed_gzip_count = 0;
  decompressed_png_count = 0;
  decompressed_png_multi_count = 0;
  decompressed_gif_count = 0;
  decompressed_jpg_count = 0;
  decompressed_jpg_prog_count = 0;
  decompressed_mp3_count = 0;
  decompressed_swf_count = 0;
  decompressed_base64_count = 0;
  decompressed_bzip2_count = 0;
  decompressed_zlib_count = 0;    // intense mode
  decompressed_brute_count = 0;   // brute mode
}

CSwitches* CreateCSwitches() {
  // We create out C++ class but return a pointer to the inherited C struct, that way C users can interact with the data but we still are able
  // to use our C++ amenities like default values via constructor and member functions
  return new Switches();
}

CPrecomp* PrecompCreate() { return new Precomp(); }
void PrecompSetProgressCallback(CPrecomp* precomp_mgr, void(*callback)(float)) {
  reinterpret_cast<Precomp*>(precomp_mgr)->set_progress_callback(callback);
}
CSwitches* PrecompGetSwitches(CPrecomp * precomp_mgr) { return &reinterpret_cast<Precomp*>(precomp_mgr)->switches; }
CRecursionContext* PrecompGetRecursionContext(CPrecomp* precomp_mgr) { return reinterpret_cast<Precomp*>(precomp_mgr)->ctx.get(); }
CResultStatistics* PrecompGetResultStatistics(CPrecomp* precomp_mgr) { return &reinterpret_cast<Precomp*>(precomp_mgr)->statistics; }
lzma_init_mt_extra_parameters* PrecompGetXzParameters(CPrecomp* precomp_mgr) { return reinterpret_cast<Precomp*>(precomp_mgr)->otf_xz_extra_params.get(); }

int PrecompPrecompress(CPrecomp* precomp_mgr) {
  precomp_mgr->start_time = get_time_ms();
  return compress_file(*reinterpret_cast<Precomp*>(precomp_mgr), 0, 100);
}

int PrecompRecompress(CPrecomp* precomp_mgr) {
  precomp_mgr->start_time = get_time_ms();
  auto internal_precomp_ptr = reinterpret_cast<Precomp*>(precomp_mgr);
  if (!precomp_mgr->header_already_read) read_header(*internal_precomp_ptr);
  return decompress_file(*internal_precomp_ptr);
}

int PrecompConvert(CPrecomp* precomp_mgr) {
  precomp_mgr->start_time = get_time_ms();
  return convert_file(*reinterpret_cast<Precomp*>(precomp_mgr));
}

const char* PrecompReadHeader(CPrecomp* precomp_mgr, bool seek_to_beg) {
  auto internal_precomp_ptr = reinterpret_cast<Precomp*>(precomp_mgr);
  if (seek_to_beg) internal_precomp_ptr->ctx->fin->seekg(0, std::ios_base::beg);
  read_header(*internal_precomp_ptr);
  return internal_precomp_ptr->output_file_name.c_str();
}

void PrecompConvertHeader(CPrecomp* precomp_mgr) {
  convert_header(*reinterpret_cast<Precomp*>(precomp_mgr));
}

void PrecompSetInputStream(CPrecomp* precomp_mgr, CPrecompIStream istream, const char* input_file_name) {
  auto internal_precomp_ptr = reinterpret_cast<Precomp*>(precomp_mgr);
  internal_precomp_ptr->input_file_name = input_file_name;
  internal_precomp_ptr->set_input_stream(static_cast<std::istream*>(istream));
}
void PrecompSetInputFile(CPrecomp* precomp_mgr, FILE* fhandle, const char* input_file_name) {
  auto internal_precomp_ptr = reinterpret_cast<Precomp*>(precomp_mgr);
  internal_precomp_ptr->input_file_name = input_file_name;
  internal_precomp_ptr->set_input_stream(fhandle);
}

void PrecompSetOutStream(CPrecomp* precomp_mgr, CPrecompOStream ostream, const char* output_file_name) {
  auto internal_precomp_ptr = reinterpret_cast<Precomp*>(precomp_mgr);
  internal_precomp_ptr->output_file_name = output_file_name;
  internal_precomp_ptr->set_output_stream(static_cast<std::ostream*>(ostream));
}
void PrecompSetOutputFile(CPrecomp* precomp_mgr, FILE* fhandle, const char* output_file_name) {
  auto internal_precomp_ptr = reinterpret_cast<Precomp*>(precomp_mgr);
  internal_precomp_ptr->output_file_name = output_file_name;
  internal_precomp_ptr->set_output_stream(fhandle);
}

//Switches constructor
Switches::Switches() {
  compression_method = OTF_XZ_MT;
  compression_otf_max_memory = 2048;
  compression_otf_thread_count = std::thread::hardware_concurrency();
  if (compression_otf_thread_count == 0) {
    compression_otf_thread_count = 2;
  }

  intense_mode = false;
  intense_mode_depth_limit = -1;
  fast_mode = false;
  brute_mode = false;
  brute_mode_depth_limit = -1;
  pdf_bmp_mode = false;
  prog_only = false;
  use_mjpeg = true;
  use_brunsli = true;
  use_brotli = false;
  use_packjpg_fallback = true;
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

  ignore_list_ptr = nullptr;
  ignore_list_count = 0;

  // preflate config
  preflate_meta_block_size = 1 << 21; // 2 MB blocks by default
  preflate_verify = false;
}

std::set<long long> Switches::ignore_set() {
  return std::set(ignore_list_ptr, ignore_list_ptr + ignore_list_count);
}

RecursionContext::RecursionContext(Precomp& instance): precomp_owner(instance) {
  compression_otf_method = OTF_XZ_MT;
}


void RecursionContext::set_input_stream(std::istream* istream, bool take_ownership) {
  this->fin = std::unique_ptr<WrappedIStream>(new WrappedIStream(istream, take_ownership));
}

void RecursionContext::set_input_stream(FILE* fhandle, bool take_ownership) {
  this->fin = std::unique_ptr<FILEIStream>(new FILEIStream(fhandle, take_ownership));
}

void RecursionContext::set_output_stream(std::ostream* ostream, bool take_ownership) {
  this->fout = std::unique_ptr<ObservableOStream>(new ObservableWrappedOStream(ostream, take_ownership));
}

void RecursionContext::set_output_stream(FILE* fhandle, bool take_ownership) {
  this->fout = std::unique_ptr<ObservableOStream>(new ObservableFILEOStream(fhandle, take_ownership));
}

std::unique_ptr<RecursionContext>&  Precomp::get_original_context() {
  if (recursion_contexts_stack.empty()) return ctx;
  return recursion_contexts_stack[0];
}

Precomp::Precomp(): CPrecomp() {
  header_already_read = false;
  recursion_depth = 0;
  max_recursion_depth = 10;
  max_recursion_depth_used = 0;
  max_recursion_depth_reached = false;
}

void Precomp::set_input_stdin() {
  // Read binary to stdin
  SET_BINARY_MODE(STDIN);
  auto new_fin = std::unique_ptr<WrappedIStream>(new WrappedIStream(new std::ifstream(), true));
  new_fin->rdbuf(std::cin.rdbuf());
  this->get_original_context()->fin = std::move(new_fin);
}

void Precomp::set_input_stream(std::istream* istream, bool take_ownership) {
  if (istream == &std::cin) { set_input_stdin(); }
  else {
    this->get_original_context()->set_input_stream(istream, take_ownership);
  }
}
void Precomp::set_input_stream(FILE* fhandle, bool take_ownership) {
  if (fhandle == stdin) { set_input_stdin(); }
  else {
    this->get_original_context()->set_input_stream(fhandle, take_ownership);
  }
}

void Precomp::set_output_stdout() {
  // Read binary to stdin
  // Write binary to stdout
  SET_BINARY_MODE(STDOUT);
  auto new_fout = std::unique_ptr<ObservableWrappedOStream>(new ObservableWrappedOStream(new std::ofstream(), true));
  new_fout->rdbuf(std::cout.rdbuf());
  this->get_original_context()->fout = std::move(new_fout);
}

void Precomp::register_output_observer_callbacks() {
  // Set write observer to update progress when writing to output file, based on how much of the input file we have read
  this->get_original_context()->fout->register_observer(ObservableOStream::observable_methods::write_method, [this]()
  {
    this->call_progress_callback();
  });
}

void Precomp::set_output_stream(std::ostream* ostream, bool take_ownership) {
  auto& orig_context = this->get_original_context();
  if (ostream == &std::cout) { set_output_stdout(); }
  else {
    orig_context->set_output_stream(ostream, take_ownership);
  }
  register_output_observer_callbacks();
}

void Precomp::set_output_stream(FILE* fhandle, bool take_ownership) {
  auto& orig_context = this->get_original_context();
  if (fhandle == stdout) { set_output_stdout(); }
  else {
    orig_context->set_output_stream(fhandle, take_ownership);
  }
  register_output_observer_callbacks();
}

void Precomp::enable_input_stream_otf_decompression() {
  const auto& orig_context = this->get_original_context();
  if (&orig_context != &ctx || orig_context->compression_otf_method == OTF_NONE) return;
  auto wrapped_istream = dynamic_cast<WrappedIStream*>(orig_context->fin.get());
  if (!wrapped_istream) throw std::runtime_error("Compression only supported when using C++'s std::istream or std::ostream, sorry if you are using the C API");
  const bool take_ownership = wrapped_istream->is_owns_wrapped_stream();
  set_input_stream(
    wrap_istream_otf_compression(
      std::unique_ptr<std::istream>(wrapped_istream->release()), orig_context->compression_otf_method, false
    ).release(),
    take_ownership
  );
}

void Precomp::enable_output_stream_otf_compression(int otf_compression_method) {
  const auto& orig_context = this->get_original_context();
  if (&orig_context != &ctx || otf_compression_method == OTF_NONE) return;
  auto wrapped_ostream = dynamic_cast<WrappedOStream*>(orig_context->fout.get());
  if (!wrapped_ostream) throw std::runtime_error("Compression only supported when using C++'s std::istream or std::ostream, sorry if you are using the C API");
  const bool take_ownership = wrapped_ostream->is_owns_wrapped_stream();
  set_output_stream(
    wrap_ostream_otf_compression(
      std::unique_ptr<std::ostream>(wrapped_ostream->release()),
      otf_compression_method,
      std::move(this->otf_xz_extra_params),
      this->switches.compression_otf_max_memory,
      this->switches.compression_otf_thread_count,
      false
    ).release(),
    take_ownership
  );
}

void Precomp::set_progress_callback(std::function<void(float)> callback) {
  progress_callback = callback;
}
void Precomp::call_progress_callback() {
  if (!this->progress_callback) return;
  const auto percent = (this->get_original_context()->fin->tellg() / static_cast<float>(this->get_original_context()->fin_length)) * 100;
  this->progress_callback(percent);
}

// get copyright message
// msg = Buffer for error messages (256 bytes buffer size are enough)
LIBPRECOMP void PrecompGetCopyrightMsg(char* msg) {
  if (V_MINOR2 == 0) {
    sprintf(msg, "Precomp DLL v%i.%i (c) 2006-2021 by Christian Schneider",V_MAJOR,V_MINOR);
  } else {
    sprintf(msg, "Precomp DLL v%i.%i.%i (c) 2006-2021 by Christian Schneider",V_MAJOR,V_MINOR,V_MINOR2);
  }
}

// Brute mode detects a bit less than intense mode to avoid false positives
// and slowdowns, so both can be active. Also, both of them can have a level
// limit, so two helper functions make things easier to handle.

bool intense_mode_is_active(Precomp& precomp_mgr) {
  if (!precomp_mgr.switches.intense_mode) return false;
  if ((precomp_mgr.switches.intense_mode_depth_limit == -1) || (precomp_mgr.recursion_depth <= precomp_mgr.switches.intense_mode_depth_limit)) return true;

  return false;
}

bool brute_mode_is_active(Precomp& precomp_mgr) {
  if (!precomp_mgr.switches.brute_mode) return false;
  if ((precomp_mgr.switches.brute_mode_depth_limit == -1) || (precomp_mgr.recursion_depth <= precomp_mgr.switches.brute_mode_depth_limit)) return true;

  return false;
}

void copy_penalty_bytes(RecursionContext& context, long long& rek_penalty_bytes_len, bool& use_penalty_bytes) {
  if ((rek_penalty_bytes_len > 0) && (use_penalty_bytes)) {
    std::copy(context.local_penalty_bytes.data(), context.local_penalty_bytes.data() + rek_penalty_bytes_len, context.penalty_bytes.begin());
    context.penalty_bytes_len = rek_penalty_bytes_len;
  } else {
    context.penalty_bytes_len = 0;
  }
}

#define DEF_COMPARE_CHUNK 512
long long def_compare_bzip2(Precomp& precomp_mgr, IStreamLike& source, IStreamLike& compfile, int level, long long& decompressed_bytes_used) {
  int ret, flush;
  unsigned have;
  bz_stream strm;
  long long identical_bytes_compare = 0;

  long long comp_pos = 0;
  decompressed_bytes_used = 0;

  /* allocate deflate state */
  strm.bzalloc = NULL;
  strm.bzfree = NULL;
  strm.opaque = NULL;
  ret = BZ2_bzCompressInit(&strm, level, 0, 0);
  if (ret != BZ_OK)
    return ret;

  long long total_same_byte_count = 0;
  long long total_same_byte_count_penalty = 0;
  long long rek_same_byte_count = 0;
  long long rek_same_byte_count_penalty = -1;
  long long rek_penalty_bytes_len = 0;
  long long local_penalty_bytes_len = 0;
  bool use_penalty_bytes = false;

  /* compress until end of file */
  do {
    precomp_mgr.call_progress_callback();

    source.read(reinterpret_cast<char*>(precomp_mgr.in), DEF_COMPARE_CHUNK);
    strm.avail_in = source.gcount();
    if (source.bad()) {
      (void)BZ2_bzCompressEnd(&strm);
      return BZ_PARAM_ERROR;
    }
    flush = source.eof() ? BZ_FINISH : BZ_RUN;
    strm.next_in = (char*)precomp_mgr.in;
    decompressed_bytes_used += strm.avail_in;

    do {
      strm.avail_out = DEF_COMPARE_CHUNK;
      strm.next_out = (char*)precomp_mgr.out;

      ret = BZ2_bzCompress(&strm, flush);

      have = DEF_COMPARE_CHUNK - strm.avail_out;

      if (have > 0) {
        if (&compfile == precomp_mgr.ctx->fin.get()) {
          identical_bytes_compare = compare_file_mem_penalty(*precomp_mgr.ctx, compfile, precomp_mgr.out, precomp_mgr.ctx->input_file_pos + comp_pos, have, total_same_byte_count, total_same_byte_count_penalty, rek_same_byte_count, rek_same_byte_count_penalty, rek_penalty_bytes_len, local_penalty_bytes_len, use_penalty_bytes);
        } else {
          identical_bytes_compare = compare_file_mem_penalty(*precomp_mgr.ctx, compfile, precomp_mgr.out, comp_pos, have, total_same_byte_count, total_same_byte_count_penalty, rek_same_byte_count, rek_same_byte_count_penalty, rek_penalty_bytes_len, local_penalty_bytes_len, use_penalty_bytes);
        }
      }

      if (have > 0) {
        if ((unsigned int)identical_bytes_compare < (have >> 1)) {
          (void)BZ2_bzCompressEnd(&strm);
          copy_penalty_bytes(*precomp_mgr.ctx, rek_penalty_bytes_len, use_penalty_bytes);
          return rek_same_byte_count;
        }
      }

      comp_pos += have;

    } while (strm.avail_out == 0);

  } while (flush != BZ_FINISH);

  (void)BZ2_bzCompressEnd(&strm);
  copy_penalty_bytes(*precomp_mgr.ctx, rek_penalty_bytes_len, use_penalty_bytes);
  return rek_same_byte_count;
}

int def_part_bzip2(Precomp& precomp_mgr, IStreamLike& source, OStreamLike& dest, int level, long long stream_size_in, long long stream_size_out) {
  int ret, flush;
  unsigned have;
  bz_stream strm;

  /* allocate deflate state */
  strm.bzalloc = NULL;
  strm.bzfree = NULL;
  strm.opaque = NULL;
  ret = BZ2_bzCompressInit(&strm, level, 0, 0);
  if (ret != BZ_OK)
    return ret;

  long long pos_in = 0;
  long long pos_out = 0;

  /* compress until end of file */
  do {
    if ((stream_size_in - pos_in) > CHUNK) {
      precomp_mgr.call_progress_callback();

      source.read(reinterpret_cast<char*>(precomp_mgr.in), CHUNK);
      strm.avail_in = source.gcount();
      pos_in += CHUNK;
      flush = BZ_RUN;
    } else {
      source.read(reinterpret_cast<char*>(precomp_mgr.in), stream_size_in - pos_in);
      strm.avail_in = source.gcount();
      flush = BZ_FINISH;
    }
    if (source.bad()) {
      (void)BZ2_bzCompressEnd(&strm);
      return BZ_PARAM_ERROR;
    }
    strm.next_in = (char*)precomp_mgr.in;

    do {
      strm.avail_out = CHUNK;
      strm.next_out = (char*)precomp_mgr.out;

      ret = BZ2_bzCompress(&strm, flush);

      have = CHUNK - strm.avail_out;

      if ((pos_out + (signed)have) > stream_size_out) {
        have = stream_size_out - pos_out;
      }
      pos_out += have;

      dest.write(reinterpret_cast<char*>(precomp_mgr.out), have);
      if (dest.bad()) {
        (void)BZ2_bzCompressEnd(&strm);
        return BZ_DATA_ERROR;
      }
    } while (strm.avail_out == 0);

  } while (flush != BZ_FINISH);

  (void)BZ2_bzCompressEnd(&strm);
  return BZ_OK;
}

int inf_bzip2(Precomp& precomp_mgr, IStreamLike& source, OStreamLike& dest, long long& compressed_stream_size, long long& decompressed_stream_size) {
  int ret;
  unsigned have;
  bz_stream strm;

  strm.bzalloc = NULL;
  strm.bzfree = NULL;
  strm.opaque = NULL;
  strm.avail_in = 0;
  strm.next_in = NULL;
  ret = BZ2_bzDecompressInit(&strm, 0, 0);
  if (ret != BZ_OK)
    return ret;

  compressed_stream_size = 0;
  decompressed_stream_size = 0;
  int avail_in_before;
  
  do {
    precomp_mgr.call_progress_callback();

    source.read(reinterpret_cast<char*>(precomp_mgr.in), CHUNK);
    strm.avail_in = source.gcount();
    avail_in_before = strm.avail_in;

    if (source.bad()) {
      (void)BZ2_bzDecompressEnd(&strm);
      return BZ_PARAM_ERROR;
    }
    if (strm.avail_in == 0)
      break;
    strm.next_in = (char*)precomp_mgr.in;

    do {
      strm.avail_out = CHUNK;
      strm.next_out = (char*)precomp_mgr.out;

      ret = BZ2_bzDecompress(&strm);
      if ((ret != BZ_OK) && (ret != BZ_STREAM_END)) {
        (void)BZ2_bzDecompressEnd(&strm);
        return ret;
      }

      compressed_stream_size += (avail_in_before - strm.avail_in);
      avail_in_before = strm.avail_in;
      
      have = CHUNK - strm.avail_out;
      dest.write(reinterpret_cast<char*>(precomp_mgr.out), have);
      if (dest.bad()) {
        (void)BZ2_bzDecompressEnd(&strm);
        return BZ_DATA_ERROR;
      }
      decompressed_stream_size += have;

    } while (strm.avail_out == 0);

    /* done when inflate() says it's done */
  } while (ret != BZ_STREAM_END);

  /* clean up and return */

  (void)BZ2_bzDecompressEnd(&strm);
  return ret == BZ_STREAM_END ? BZ_OK : BZ_DATA_ERROR;

}


int def_bzip2(Precomp& precomp_mgr, std::istream& source, std::ostream& dest, int level) {
  int ret, flush;
  unsigned have;
  bz_stream strm;

  /* allocate deflate state */
  strm.bzalloc = NULL;
  strm.bzfree = NULL;
  strm.opaque = NULL;
  ret = BZ2_bzCompressInit(&strm, level, 0, 0);
  if (ret != BZ_OK)
    return ret;

  /* compress until end of file */
  do {
    precomp_mgr.call_progress_callback();

    source.read(reinterpret_cast<char*>(precomp_mgr.in), CHUNK);
    strm.avail_in = source.gcount();
    if (source.bad()) {
      (void)BZ2_bzCompressEnd(&strm);
      return BZ_PARAM_ERROR;
    }
    flush = source.eof() ? BZ_FINISH : BZ_RUN;
    strm.next_in = (char*)precomp_mgr.in;

    do {
      strm.avail_out = CHUNK;
      strm.next_out = (char*)precomp_mgr.out;

      ret = BZ2_bzCompress(&strm, flush);

      have = CHUNK - strm.avail_out;

      dest.write(reinterpret_cast<char*>(precomp_mgr.out), have);
      if (dest.bad()) {
        (void)BZ2_bzCompressEnd(&strm);
        return BZ_DATA_ERROR;
      }
    } while (strm.avail_out == 0);

  } while (flush != BZ_FINISH);

  (void)BZ2_bzCompressEnd(&strm);
  return BZ_OK;
}

long long file_recompress_bzip2(Precomp& precomp_mgr, IStreamLike& origfile, int level, long long& decompressed_bytes_used, long long& decompressed_bytes_total, PrecompTmpFile& tmpfile) {
  long long retval;

  tmpfile.seekg(0, std::ios_base::end);
  decompressed_bytes_total = tmpfile.tellg();
  if (!tmpfile.is_open()) {
    throw PrecompError(ERR_TEMP_FILE_DISAPPEARED);
  }

  tmpfile.seekg(0, std::ios_base::beg);
  tmpfile.close();
  WrappedFStream tmpfile2;
  tmpfile2.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
  retval = def_compare_bzip2(precomp_mgr, tmpfile2, origfile, level, decompressed_bytes_used);
  tmpfile2.close();
  return retval < 0 ? -1 : retval;
}

void write_decompressed_data(Precomp& precomp_mgr, OStreamLike& ostream, long long byte_count, const char* decompressed_file_name) {
  WrappedFStream ftempout;
  ftempout.open(decompressed_file_name, std::ios_base::in | std::ios_base::binary);
  if (!ftempout.is_open()) throw PrecompError(ERR_TEMP_FILE_DISAPPEARED);

  ftempout.seekg(0, std::ios_base::beg);

  fast_copy(precomp_mgr, ftempout, ostream, byte_count);
  ftempout.close();
}

void write_decompressed_data_io_buf(Precomp& precomp_mgr, long long byte_count, bool in_memory, const char* decompressed_file_name) {
    if (in_memory) {
      auto decomp_io_buf_ptr = precomp_mgr.ctx->decomp_io_buf.data();
      memiostream memstream = memiostream::make(decomp_io_buf_ptr, decomp_io_buf_ptr + byte_count);
      fast_copy(precomp_mgr, memstream, *precomp_mgr.ctx->fout, byte_count);
    } else {
      write_decompressed_data(precomp_mgr, *precomp_mgr.ctx->fout, byte_count, decompressed_file_name);
    }
}

unsigned long long compare_files(Precomp& precomp_mgr, IStreamLike& file1, IStreamLike& file2, unsigned int pos1, unsigned int pos2) {
  unsigned char input_bytes1[COMP_CHUNK];
  unsigned char input_bytes2[COMP_CHUNK];
  long long same_byte_count = 0;
  int size1, size2, minsize;
  int i;
  bool endNow = false;

  file1.seekg(pos1, std::ios_base::beg);
  file2.seekg(pos2, std::ios_base::beg);

  do {
    precomp_mgr.call_progress_callback();

    file1.read(reinterpret_cast<char*>(input_bytes1), COMP_CHUNK);
    size1 = file1.gcount();
    file2.read(reinterpret_cast<char*>(input_bytes2), COMP_CHUNK);
    size1 = file2.gcount();

    minsize = std::min(size1, size2);
    for (i = 0; i < minsize; i++) {
      if (input_bytes1[i] != input_bytes2[i]) {
        endNow = true;
        break;
      }
      same_byte_count++;
    }
  } while ((minsize == COMP_CHUNK) && (!endNow));

  return same_byte_count;
}

unsigned char input_bytes1[DEF_COMPARE_CHUNK];

long long compare_file_mem_penalty(RecursionContext& context, IStreamLike& file1, unsigned char* input_bytes2, long long pos1, long long bytecount, long long& total_same_byte_count, long long& total_same_byte_count_penalty, long long& rek_same_byte_count, long long& rek_same_byte_count_penalty, long long& rek_penalty_bytes_len, long long& local_penalty_bytes_len, bool& use_penalty_bytes) {
  int same_byte_count = 0;
  int size1;
  int i;

  unsigned long long old_pos = file1.tellg();
  file1.seekg(pos1, std::ios_base::beg);

  file1.read(reinterpret_cast<char*>(input_bytes1), bytecount);
  size1 = file1.gcount();

  for (i = 0; i < size1; i++) {
    if (input_bytes1[i] == input_bytes2[i]) {
      same_byte_count++;
      total_same_byte_count_penalty++;
    } else {
      total_same_byte_count_penalty -= 5; // 4 bytes = position, 1 byte = new byte

      // stop, if local_penalty_bytes_len gets too big
      if ((local_penalty_bytes_len + 5) >= MAX_PENALTY_BYTES) {
        break;
      }

      local_penalty_bytes_len += 5;
      // position
      context.local_penalty_bytes[local_penalty_bytes_len-5] = (total_same_byte_count >> 24) % 256;
      context.local_penalty_bytes[local_penalty_bytes_len-4] = (total_same_byte_count >> 16) % 256;
      context.local_penalty_bytes[local_penalty_bytes_len-3] = (total_same_byte_count >> 8) % 256;
      context.local_penalty_bytes[local_penalty_bytes_len-2] = total_same_byte_count % 256;
      // new byte
      context.local_penalty_bytes[local_penalty_bytes_len-1] = input_bytes1[i];
    }
    total_same_byte_count++;

    if (total_same_byte_count_penalty > rek_same_byte_count_penalty) {
      use_penalty_bytes = true;
      rek_penalty_bytes_len = local_penalty_bytes_len;

      rek_same_byte_count = total_same_byte_count;
      rek_same_byte_count_penalty = total_same_byte_count_penalty;
    }
  }

  file1.seekg(old_pos, std::ios_base::beg);

  return same_byte_count;
}

void start_uncompressed_data(RecursionContext& context) {
  context.uncompressed_length = 0;
  context.uncompressed_pos = context.input_file_pos;

  // uncompressed data
  context.fout->put(0);

  context.uncompressed_data_in_work = true;
}

void end_uncompressed_data(Precomp& precomp_mgr) {

  if (!precomp_mgr.ctx->uncompressed_data_in_work) return;

  fout_fput_vlint(*precomp_mgr.ctx->fout, precomp_mgr.ctx->uncompressed_length);

  // fast copy of uncompressed data
  precomp_mgr.ctx->fin->seekg(precomp_mgr.ctx->uncompressed_pos, std::ios_base::beg);
  fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, precomp_mgr.ctx->uncompressed_length, true);

  precomp_mgr.ctx->uncompressed_length = -1;

  precomp_mgr.ctx->uncompressed_data_in_work = false;
}

int best_windowbits = -1;

void init_decompression_variables(RecursionContext& context) {
  context.identical_bytes = -1;
  context.best_identical_bytes = -1;
  context.best_penalty_bytes_len = 0;
  context.best_identical_bytes_decomp = -1;
  context.identical_bytes_decomp = -1;
}

void debug_deflate_detected(RecursionContext& context, const recompress_deflate_result& rdres, const char* type) {
  if (PRECOMP_VERBOSITY_LEVEL < PRECOMP_DEBUG_LOG) return;
  std::stringstream ss;
  ss << "Possible zLib-Stream " << type << " found at position " << context.saved_input_file_pos << std::endl;
  ss << "Compressed size: " << rdres.compressed_stream_size << std::endl;
  ss << "Can be decompressed to " << rdres.uncompressed_stream_size << " bytes" << std::endl;

  if (rdres.accepted) {
    if (rdres.zlib_perfect) {
      ss << "Detect ZLIB parameters: comp level " << rdres.zlib_comp_level << ", mem level " << rdres.zlib_mem_level << ", " << rdres.zlib_window_bits << "window bits" << std::endl;
    } else {
      ss << "Non-ZLIB reconstruction data size: " << rdres.recon_data.size() << " bytes" << std::endl;
    }
  }

  print_to_log(PRECOMP_DEBUG_LOG, ss.str());
}

static uint64_t sum_compressed = 0, sum_uncompressed = 0, sum_recon = 0, sum_expansion = 0;
void debug_sums(Precomp& precomp_mgr, const recompress_deflate_result& rdres) {
  if (PRECOMP_VERBOSITY_LEVEL < PRECOMP_DEBUG_LOG) return;
  sum_compressed += rdres.compressed_stream_size;
  sum_uncompressed += rdres.uncompressed_stream_size;
  sum_expansion += rdres.uncompressed_stream_size - rdres.compressed_stream_size;
  sum_recon += rdres.recon_data.size();
  print_to_log(PRECOMP_DEBUG_LOG, "deflate sums: c %I64d, u %I64d, x %I64d, r %I64d, i %I64d, o %I64d\n",
         sum_compressed, sum_uncompressed, sum_expansion, sum_recon, (uint64_t)precomp_mgr.ctx->fin->tellg(), (uint64_t)precomp_mgr.ctx->fout->tellp());
}
void debug_pos(Precomp& precomp_mgr) {
  print_to_log(PRECOMP_DEBUG_LOG, "deflate pos: i %I64d, o %I64d\n", (uint64_t)precomp_mgr.ctx->fin->tellg(), (uint64_t)precomp_mgr.ctx->fout->tellp());
}

int compress_file_impl(Precomp& precomp_mgr, float min_percent, float max_percent) {

  precomp_mgr.ctx->comp_decomp_state = P_COMPRESS;
  if (precomp_mgr.recursion_depth == 0) write_header(precomp_mgr);
  precomp_mgr.enable_output_stream_otf_compression(precomp_mgr.ctx->compression_otf_method);

  precomp_mgr.ctx->global_min_percent = min_percent;
  precomp_mgr.ctx->global_max_percent = max_percent;

  precomp_mgr.ctx->uncompressed_length = -1;
  precomp_mgr.ctx->uncompressed_bytes_total = 0;
  precomp_mgr.ctx->uncompressed_bytes_written = 0;

  precomp_mgr.ctx->fin->seekg(0, std::ios_base::beg);
  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.ctx->in_buf), IN_BUF_SIZE);
  long long in_buf_pos = 0;
  precomp_mgr.ctx->cb = -1;

  precomp_mgr.ctx->anything_was_used = false;
  precomp_mgr.ctx->non_zlib_was_used = false;

  std::string tempfile_base = temp_files_tag() + "_decomp_";
  std::string tempfile;
  auto ignore_set = precomp_mgr.switches.ignore_set();

  for (precomp_mgr.ctx->input_file_pos = 0; precomp_mgr.ctx->input_file_pos < precomp_mgr.ctx->fin_length; precomp_mgr.ctx->input_file_pos++) {
  tempfile = tempfile_base;

  precomp_mgr.ctx->compressed_data_found = false;

  bool ignore_this_pos = false;

  if ((in_buf_pos + IN_BUF_SIZE) <= (precomp_mgr.ctx->input_file_pos + CHECKBUF_SIZE)) {
    precomp_mgr.ctx->fin->seekg(precomp_mgr.ctx->input_file_pos, std::ios_base::beg);
    precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.ctx->in_buf), IN_BUF_SIZE);
    in_buf_pos = precomp_mgr.ctx->input_file_pos;
    precomp_mgr.ctx->cb = 0;
  } else {
    precomp_mgr.ctx->cb++;
  }

  ignore_this_pos = ignore_set.find(precomp_mgr.ctx->input_file_pos) != ignore_set.end();

  if (!ignore_this_pos) {

    // ZIP header?
    if (precomp_mgr.switches.use_zip && zip_header_check(precomp_mgr, &precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb])) {
      precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
      precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

      auto result = try_decompression_zip(precomp_mgr);
      precomp_mgr.ctx->compressed_data_found = result.success;

      if (result.success) {
        end_uncompressed_data(precomp_mgr);

        result.dump_to_outfile(precomp_mgr);

        // start new uncompressed data

        // set input file pointer after recompressed data
        precomp_mgr.ctx->input_file_pos += result.input_pos_add_offset();
        precomp_mgr.ctx->cb += result.input_pos_add_offset();
      }
      else {
        precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
        precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
      }
    }

    if ((!precomp_mgr.ctx->compressed_data_found) && (precomp_mgr.switches.use_gzip)) { // no ZIP header -> GZip header?
      if (gzip_header_check(precomp_mgr, &precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb])) {
        precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
        precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

        auto result = try_decompression_gzip(precomp_mgr);
        precomp_mgr.ctx->compressed_data_found = result.success;

        if (result.success) {
          end_uncompressed_data(precomp_mgr);

          result.dump_to_outfile(precomp_mgr);

          // start new uncompressed data

          // set input file pointer after recompressed data
          precomp_mgr.ctx->input_file_pos += result.input_pos_add_offset();
          precomp_mgr.ctx->cb += result.input_pos_add_offset();
        }
        else {
          precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
          precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
        }
      }
    }

    if ((!precomp_mgr.ctx->compressed_data_found) && (precomp_mgr.switches.use_pdf)) { // no Gzip header -> PDF FlateDecode?
      if (pdf_header_check(precomp_mgr.ctx->in_buf + precomp_mgr.ctx->cb)) {
        precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
        precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

        auto result = precompress_pdf(precomp_mgr);
        precomp_mgr.ctx->compressed_data_found = result.success;

        if (result.success) {
          end_uncompressed_data(precomp_mgr);

          result.dump_to_outfile(precomp_mgr);

          // start new uncompressed data

          // set input file pointer after recompressed data
          precomp_mgr.ctx->input_file_pos += result.input_pos_add_offset();
          precomp_mgr.ctx->cb += result.input_pos_add_offset();
        }
        else {
          precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
          precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
        }
      }
    }

    if ((!precomp_mgr.ctx->compressed_data_found) && (precomp_mgr.switches.use_png)) { // no PDF header -> PNG IDAT?
      if (png_header_check(precomp_mgr.ctx->in_buf + precomp_mgr.ctx->cb)) {
        auto result = precompress_png(precomp_mgr);
        precomp_mgr.ctx->compressed_data_found = result.success;

        if (result.success) {
          end_uncompressed_data(precomp_mgr);

          result.dump_to_outfile(precomp_mgr);

          // start new uncompressed data

          // set input file pointer after recompressed data
          precomp_mgr.ctx->input_file_pos += result.input_pos_add_offset();
          precomp_mgr.ctx->cb += result.input_pos_add_offset();
        }
        else {
          precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
          precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
        }
      }

    }

    if ((!precomp_mgr.ctx->compressed_data_found) && (precomp_mgr.switches.use_gif)) { // no PNG header -> GIF header?
      if (gif_header_check(&precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb])) {
        precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
        precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

        auto result = precompress_gif(precomp_mgr);
        precomp_mgr.ctx->compressed_data_found = result.success;
        if (result.success) {
          end_uncompressed_data(precomp_mgr);

          result.dump_to_outfile(precomp_mgr);

          // start new uncompressed data

          // set input file pointer after recompressed data
          precomp_mgr.ctx->input_file_pos += result.input_pos_add_offset();
          precomp_mgr.ctx->cb += result.input_pos_add_offset();
        }
        else {
          precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
          precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
        }
      }
    }

    if ((!precomp_mgr.ctx->compressed_data_found) && (precomp_mgr.switches.use_jpg)) { // no GIF header -> JPG header?
      if (jpeg_header_check(&precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb])) {
        precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
        precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

        auto result = precompress_jpeg(precomp_mgr);
        precomp_mgr.ctx->compressed_data_found = result.success;
        if (result.success) {
          end_uncompressed_data(precomp_mgr);

          result.dump_to_outfile(precomp_mgr);

          // start new uncompressed data

          // set input file pointer after recompressed data
          precomp_mgr.ctx->input_file_pos += result.input_pos_add_offset();
          precomp_mgr.ctx->cb += result.input_pos_add_offset();
        }
        else {
          precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
          precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
        }
      }
    }

    if ((!precomp_mgr.ctx->compressed_data_found) && (precomp_mgr.switches.use_mp3)) { // no JPG header -> MP3 header?
      if (mp3_header_check(&precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb])) { // frame start
        precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
        precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

        auto result = precompress_mp3(precomp_mgr);
        precomp_mgr.ctx->compressed_data_found = result.success;
        if (result.success) {
          // end uncompressed data
          end_uncompressed_data(precomp_mgr);

          result.dump_to_outfile(precomp_mgr);

          // start new uncompressed data

          // set input file pointer after recompressed data
          precomp_mgr.ctx->input_file_pos += result.input_pos_add_offset();
          precomp_mgr.ctx->cb += result.input_pos_add_offset();
        }
        else {
          precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
          precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
        }
      }
    }

    if ((!precomp_mgr.ctx->compressed_data_found) && (precomp_mgr.switches.use_swf)) { // no MP3 header -> SWF header?
      // CWS = Compressed SWF file
      if (swf_header_check(&precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb])) {
        precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
        precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

        auto result = try_decompression_swf(precomp_mgr);
        precomp_mgr.ctx->compressed_data_found = result.success;
        if (result.success) {
          end_uncompressed_data(precomp_mgr);

          result.dump_to_outfile(precomp_mgr);

          // set input file pointer after recompressed data
          precomp_mgr.ctx->input_file_pos += result.input_pos_add_offset();
          precomp_mgr.ctx->cb += result.input_pos_add_offset();
        }
        else {
          precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
          precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
        }
      }
    }

    if ((!precomp_mgr.ctx->compressed_data_found) && (precomp_mgr.switches.use_base64)) { // no SWF header -> Base64?
    if ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 1] == 'o') && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 2] == 'n') && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] == 't') && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 4] == 'e')) {
      unsigned char cte_detect[33];
      for (int i = 0; i < 33; i++) {
        cte_detect[i] = tolower(precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + i]);
      }
      if (memcmp(cte_detect, "content-transfer-encoding: base64", 33) == 0) {
        // search for double CRLF, all between is "header"
        int base64_header_length = 33;
        bool found_double_crlf = false;
        do {
          if ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + base64_header_length] == 13) && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + base64_header_length + 1] == 10)) {
            if ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + base64_header_length + 2] == 13) && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + base64_header_length + 3] == 10)) {
              found_double_crlf = true;
              base64_header_length += 4;
              // skip additional CRLFs
              while ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + base64_header_length] == 13) && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + base64_header_length + 1] == 10)) {
                base64_header_length += 2;
              }
              break;
            }
          }
          base64_header_length++;
        } while (base64_header_length < (CHECKBUF_SIZE - 2));

        if (found_double_crlf) {

          precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
          precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

          precomp_mgr.ctx->input_file_pos += base64_header_length; // skip "header"

          tempfile += "base64";
          PrecompTmpFile tmp_base64;
          tmp_base64.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
          try_decompression_base64(precomp_mgr, base64_header_length, tmp_base64);

          precomp_mgr.ctx->cb += base64_header_length;

          if (!precomp_mgr.ctx->compressed_data_found) {
            precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
            precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
          }
        }
      }
    }
    }

    if ((!precomp_mgr.ctx->compressed_data_found) && (precomp_mgr.switches.use_bzip2)) { // no Base64 header -> bZip2?
      // BZhx = header, x = compression level/blocksize (1-9)
      if ((precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb] == 'B') && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 1] == 'Z') && (precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 2] == 'h')) {
        int compression_level = precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] - '0';
        if ((compression_level >= 1) && (compression_level <= 9)) {
          precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
          precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

          tempfile += "bzip2";
          PrecompTmpFile tmp_bzip2;
          tmp_bzip2.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
          try_decompression_bzip2(precomp_mgr, compression_level, tmp_bzip2);

          if (!precomp_mgr.ctx->compressed_data_found) {
            precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
            precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
          }
        }
      }
    }


   // nothing so far -> if intense mode is active, look for raw zLib header
   if (intense_mode_is_active(precomp_mgr)) {
    if (!precomp_mgr.ctx->compressed_data_found) {
      bool ignore_this_position = false;
      if (precomp_mgr.ctx->intense_ignore_offsets->size() > 0) {
        auto first = precomp_mgr.ctx->intense_ignore_offsets->begin();
        while (*first < precomp_mgr.ctx->input_file_pos) {
          precomp_mgr.ctx->intense_ignore_offsets->erase(first);
          if (precomp_mgr.ctx->intense_ignore_offsets->size() == 0) break;
          first = precomp_mgr.ctx->intense_ignore_offsets->begin();
        }

        if (precomp_mgr.ctx->intense_ignore_offsets->size() > 0) {
          if (*first == precomp_mgr.ctx->input_file_pos) {
            ignore_this_position = true;
            precomp_mgr.ctx->intense_ignore_offsets->erase(first);
          }
        }
      }

      if (!ignore_this_position) {
        if (zlib_header_check(&precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb])) {
          precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
          precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

          auto result = try_decompression_zlib(precomp_mgr);
          if (result.success) {
            precomp_mgr.ctx->compressed_data_found = true;

            end_uncompressed_data(precomp_mgr);

            result.dump_to_outfile(precomp_mgr);

            // set input file pointer after recompressed data
            precomp_mgr.ctx->input_file_pos += result.input_pos_add_offset();
            precomp_mgr.ctx->cb += result.input_pos_add_offset();
          }
          else {
            precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
            precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
          }
        }
      }
    }
   }

   // nothing so far -> if brute mode is active, brute force for zLib streams
    if (brute_mode_is_active(precomp_mgr)) {
    if (!precomp_mgr.ctx->compressed_data_found) {
      bool ignore_this_position = false;
      if (precomp_mgr.ctx->brute_ignore_offsets->size() > 0) {
        auto first = precomp_mgr.ctx->brute_ignore_offsets->begin();
        while (*first < precomp_mgr.ctx->input_file_pos) {
          precomp_mgr.ctx->brute_ignore_offsets->erase(first);
          if (precomp_mgr.ctx->brute_ignore_offsets->size() == 0) break;
          first = precomp_mgr.ctx->brute_ignore_offsets->begin();
        }

        if (precomp_mgr.ctx->brute_ignore_offsets->size() > 0) {
          if (*first == precomp_mgr.ctx->input_file_pos) {
            ignore_this_position = true;
            precomp_mgr.ctx->brute_ignore_offsets->erase(first);
          }
        }
      }

      if (!ignore_this_position) {
        precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
        precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

        if (check_raw_deflate_stream_start(precomp_mgr)) {
          auto result = try_decompression_raw_deflate(precomp_mgr);
          if (result.success) {
            precomp_mgr.ctx->compressed_data_found = true;

            end_uncompressed_data(precomp_mgr);

            result.dump_to_outfile(precomp_mgr);

            // set input file pointer after recompressed data
            precomp_mgr.ctx->input_file_pos += result.input_pos_add_offset();
            precomp_mgr.ctx->cb += result.input_pos_add_offset();
          }
        }

        if (!precomp_mgr.ctx->compressed_data_found) {
          precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
          precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
        }
      }
    }
   }

  }

    if (!precomp_mgr.ctx->compressed_data_found) {
      if (precomp_mgr.ctx->uncompressed_length == -1) {
        start_uncompressed_data(*precomp_mgr.ctx);
      }
      precomp_mgr.ctx->uncompressed_length++;
      precomp_mgr.ctx->uncompressed_bytes_total++;
    }

  }

  end_uncompressed_data(precomp_mgr);

  precomp_mgr.ctx->fout = nullptr; // To close the outfile TODO: maybe we should just make sure the whole last context gets destroyed if at recursion_depth == 0?

  return (precomp_mgr.ctx->anything_was_used || precomp_mgr.ctx->non_zlib_was_used) ? RETURN_SUCCESS : RETURN_NOTHING_DECOMPRESSED;
}

int wrap_with_exception_catch(std::function<int()> func)
{
  // Didn't want to wrap those big functions on a try catch, and also didn't want to leak out our exceptions to the outside, because we want to mantain a C API
  try
  {
    return func();
  }
  catch (const PrecompError& exc)
  {
    return exc.error_code;
  }
  catch (const std::exception& exc)
  {
    return ERR_GENERIC_OR_UNKNOWN;
  }
}

int compress_file(Precomp& precomp_mgr, float min_percent, float max_percent)
{
  return wrap_with_exception_catch([&]() { return compress_file_impl(precomp_mgr, min_percent, max_percent); });
}

int decompress_file_impl(Precomp& precomp_mgr) {

  long long fin_pos;

  precomp_mgr.ctx->comp_decomp_state = P_DECOMPRESS;
  precomp_mgr.enable_input_stream_otf_decompression();

  std::string tmp_tag = temp_files_tag();
  std::string tempfile_base = tmp_tag + "_recomp_";
  std::string tempfile2_base = tmp_tag + "_recomp2_";
  std::string tempfile;
  std::string tempfile2;

  fin_pos = precomp_mgr.ctx->fin->tellg();

while (precomp_mgr.ctx->fin->good()) {
  tempfile = tempfile_base;
  tempfile2 = tempfile2_base;

  unsigned char header1 = precomp_mgr.ctx->fin->get();
  if (!precomp_mgr.ctx->fin->good()) break;
  if (header1 == 0) { // uncompressed data
    long long uncompressed_data_length;
    uncompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);

    if (uncompressed_data_length == 0) break; // end of PCF file, used by bZip2 compress-on-the-fly

    print_to_log(PRECOMP_DEBUG_LOG, "Uncompressed data, length=%lli\n");
    fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, uncompressed_data_length);

  } else { // decompressed data, recompress

    unsigned char headertype = precomp_mgr.ctx->fin->get();

    switch (headertype) {
    case D_PDF: { // PDF recompression
      recompress_pdf(precomp_mgr, header1);
      break;
    }     
    case D_ZIP: { // ZIP recompression
      recompress_zip(precomp_mgr, header1);
      break;
    }
    case D_GZIP: { // GZip recompression
      recompress_gzip(precomp_mgr, header1);
      break;
    }
    case D_PNG: { // PNG recompression
      recompress_png(precomp_mgr, header1);
      break;
    }
    case D_MULTIPNG: { // PNG multi recompression
      recompress_multipng(precomp_mgr, header1);
      break;
    }
    case D_GIF: { // GIF recompression
      print_to_log(PRECOMP_DEBUG_LOG, "Decompressed data - GIF\n");
      try_recompression_gif(precomp_mgr, header1, tempfile, tempfile2);
      break;
    }
    case D_JPG: { // JPG recompression
      recompress_jpg(precomp_mgr, header1);
      break;
    }
    case D_SWF: { // SWF recompression
      recompress_swf(precomp_mgr, header1);
      break;
    }
    case D_BASE64: { // Base64 recompression
      tempfile += "base64";

      print_to_log(PRECOMP_DEBUG_LOG, "Decompressed data - Base64\n");

      int line_case = (header1 >> 2) & 3;
      bool recursion_used = ((header1 & 128) == 128);

      // restore Base64 "header"
      int base64_header_length = fin_fget_vlint(*precomp_mgr.ctx->fin);

      print_to_log(PRECOMP_DEBUG_LOG, "Base64 header length: %i\n", base64_header_length);
      precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), base64_header_length);
      precomp_mgr.ctx->fout->put(*(precomp_mgr.in)+1); // first char was decreased
      precomp_mgr.ctx->fout->write(reinterpret_cast<char*>(precomp_mgr.in + 1), base64_header_length - 1);

      // read line length list
      int line_count = fin_fget_vlint(*precomp_mgr.ctx->fin);

      unsigned int* base64_line_len = new unsigned int[line_count];

      if (line_case == 2) {
        for (int i = 0; i < line_count; i++) {
          base64_line_len[i] = precomp_mgr.ctx->fin->get();
        }
      } else {
        base64_line_len[0] = precomp_mgr.ctx->fin->get();
        for (int i = 1; i < line_count; i++) {
          base64_line_len[i] = base64_line_len[0];
        }
        if (line_case == 1) base64_line_len[line_count - 1] = precomp_mgr.ctx->fin->get();
      }

      long long recompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);
      long long decompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);

      long long recursion_data_length = 0;
      if (recursion_used) {
        recursion_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);
      }

      if (recursion_used) {
        print_to_log(PRECOMP_DEBUG_LOG, "Recursion data length: %lli\n", recursion_data_length);
      } else {
        print_to_log(PRECOMP_DEBUG_LOG, "Encoded length: %lli - decoded length: %lli\n", recompressed_data_length, decompressed_data_length);
      }

      // re-encode Base64

      if (recursion_used) {
        PrecompTmpFile tmp_base64;
        tmp_base64.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
        recursion_result r = recursion_decompress(precomp_mgr, recursion_data_length, tmp_base64);
        auto wrapped_istream_frecurse = WrappedIStream(r.frecurse.get(), false);
        base64_reencode(precomp_mgr, wrapped_istream_frecurse, *precomp_mgr.ctx->fout, line_count, base64_line_len, r.file_length, decompressed_data_length);
        r.frecurse->close();
        remove(r.file_name.c_str());
      } else {
        base64_reencode(precomp_mgr, *precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, line_count, base64_line_len, recompressed_data_length, decompressed_data_length);
      }

      delete[] base64_line_len;
      break;
    }
    case D_BZIP2: { // bZip2 recompression
      tempfile += "bzip2";

      print_to_log(PRECOMP_DEBUG_LOG, "Decompressed data - bZip2\n");

      unsigned char header2 = precomp_mgr.ctx->fin->get();

      bool penalty_bytes_stored = ((header1 & 2) == 2);
      bool recursion_used = ((header1 & 128) == 128);
      int level = header2;

      print_to_log(PRECOMP_DEBUG_LOG, "Compression level: %i\n", level);

      // read penalty bytes
      if (penalty_bytes_stored) {
        precomp_mgr.ctx->penalty_bytes_len = fin_fget_vlint(*precomp_mgr.ctx->fin);
        precomp_mgr.ctx->fin->read(precomp_mgr.ctx->penalty_bytes.data(), precomp_mgr.ctx->penalty_bytes_len);
      }

      long long recompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);
      long long decompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);

      long long recursion_data_length = 0;
      if (recursion_used) {
        recursion_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);
      }

      if (recursion_used) {
        print_to_log(PRECOMP_DEBUG_LOG, "Recursion data length: %lli\n", recursion_data_length);
      } else {
        print_to_log(PRECOMP_DEBUG_LOG, "Recompressed length: %lli - decompressed length: %lli\n", recompressed_data_length, decompressed_data_length);
      }

      long long old_fout_pos = precomp_mgr.ctx->fout->tellp();

      if (recursion_used) {
        PrecompTmpFile tmp_bzip2;
        tmp_bzip2.open(tempfile, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
        recursion_result r = recursion_decompress(precomp_mgr, recursion_data_length, tmp_bzip2);
        auto wrapped_istream_frecurse = WrappedIStream(r.frecurse.get(), false);
        precomp_mgr.ctx->retval = def_part_bzip2(precomp_mgr, wrapped_istream_frecurse, *precomp_mgr.ctx->fout, level, decompressed_data_length, recompressed_data_length);
        r.frecurse->close();
        remove(r.file_name.c_str());
      } else {
        precomp_mgr.ctx->retval = def_part_bzip2(precomp_mgr, *precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, level, decompressed_data_length, recompressed_data_length);
      }

      if (precomp_mgr.ctx->retval != BZ_OK) {
        print_to_log(PRECOMP_DEBUG_LOG, "BZIP2 retval = %lli\n", precomp_mgr.ctx->retval);
        throw PrecompError(ERR_DURING_RECOMPRESSION);
      }

      if (penalty_bytes_stored) {
        precomp_mgr.ctx->fout->flush();

        long long fsave_fout_pos = precomp_mgr.ctx->fout->tellp();
        int pb_pos = 0;
        for (int pbc = 0; pbc < precomp_mgr.ctx->penalty_bytes_len; pbc += 5) {
          pb_pos = ((unsigned char)precomp_mgr.ctx->penalty_bytes[pbc]) << 24;
          pb_pos += ((unsigned char)precomp_mgr.ctx->penalty_bytes[pbc + 1]) << 16;
          pb_pos += ((unsigned char)precomp_mgr.ctx->penalty_bytes[pbc + 2]) << 8;
          pb_pos += (unsigned char)precomp_mgr.ctx->penalty_bytes[pbc + 3];

          precomp_mgr.ctx->fout->seekp(old_fout_pos + pb_pos, std::ios_base::beg);
          precomp_mgr.ctx->fout->write(precomp_mgr.ctx->penalty_bytes.data() + pbc + 4, 1);
        }

        precomp_mgr.ctx->fout->seekp(fsave_fout_pos, std::ios_base::beg);
      }
      break;
    }
    case D_MP3: { // MP3 recompression
      recompress_mp3(precomp_mgr);
      break;
    }
    case D_BRUTE: { // brute mode recompression
      recompress_raw_deflate(precomp_mgr, header1);
      break;
    }
    case D_RAW: { // raw zLib recompression
      recompress_zlib(precomp_mgr, header1);
      break;
    }
    default:
      throw std::runtime_error(make_cstyle_format_string("ERROR: Unsupported stream type %i\n", headertype));
    }

  }

  fin_pos = precomp_mgr.ctx->fin->tellg();
  if (precomp_mgr.ctx->compression_otf_method != OTF_NONE) {
    if (precomp_mgr.ctx->fin->eof()) break;
    if (fin_pos >= precomp_mgr.ctx->fin_length) fin_pos = precomp_mgr.ctx->fin_length - 1;
  }
}

  return RETURN_SUCCESS;
}

int decompress_file(Precomp& precomp_mgr)
{
  return wrap_with_exception_catch([&]() { return decompress_file_impl(precomp_mgr); });
}

int convert_file_impl(Precomp& precomp_mgr) {
  int bytes_read;
  unsigned char convbuf[COPY_BUF_SIZE];
  int conv_bytes = -1;

  precomp_mgr.ctx->comp_decomp_state = P_CONVERT;
  precomp_mgr.enable_input_stream_otf_decompression();
  precomp_mgr.enable_output_stream_otf_compression(precomp_mgr.conversion_to_method);

  for (;;) {
    precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.copybuf), COPY_BUF_SIZE);
    bytes_read = precomp_mgr.ctx->fin->gcount();
    // truncate by 9 bytes (Precomp on-the-fly delimiter) if converting from compressed data
    if ((precomp_mgr.conversion_from_method > OTF_NONE) && (bytes_read < COPY_BUF_SIZE)) {
      bytes_read -= 9;
      if (bytes_read < 0) {
        conv_bytes += bytes_read;
        bytes_read = 0;
      }
    }
    if (conv_bytes > -1) precomp_mgr.ctx->fout->write(reinterpret_cast<char*>(convbuf), conv_bytes);
    for (int i = 0; i < bytes_read; i++) {
      convbuf[i] = precomp_mgr.copybuf[i];
    }
    conv_bytes = bytes_read;
    if (bytes_read < COPY_BUF_SIZE) {
      break;
    }

    precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->fin->tellg();
    precomp_mgr.call_progress_callback();
  }
  precomp_mgr.ctx->fout->write(reinterpret_cast<char*>(convbuf), conv_bytes);

  return RETURN_SUCCESS;
}

int convert_file(Precomp& precomp_mgr)
{
  return wrap_with_exception_catch([&]() { return convert_file_impl(precomp_mgr); });
}

long long try_to_decompress_bzip2(Precomp& precomp_mgr, IStreamLike& file, int compression_level, long long& compressed_stream_size, PrecompTmpFile& tmpfile) {
  long long r, decompressed_stream_size;

  precomp_mgr.call_progress_callback();

  remove(tmpfile.file_path.c_str());
  WrappedFStream ftempout;
  ftempout.open(tmpfile.file_path, std::ios_base::out | std::ios_base::binary);

  file.seekg(&file == precomp_mgr.ctx->fin.get() ? precomp_mgr.ctx->input_file_pos : 0, std::ios_base::beg);

  r = inf_bzip2(precomp_mgr, file, ftempout, compressed_stream_size, decompressed_stream_size);
  ftempout.close();
  if (r == BZ_OK) return decompressed_stream_size;

  return r;
}

void try_recompress_bzip2(Precomp& precomp_mgr, IStreamLike& origfile, int level, long long& compressed_stream_size, PrecompTmpFile& tmpfile) {
            precomp_mgr.call_progress_callback();

            long long decomp_bytes_total;
            precomp_mgr.ctx->identical_bytes = file_recompress_bzip2(precomp_mgr, origfile, level, precomp_mgr.ctx->identical_bytes_decomp, decomp_bytes_total, tmpfile);
            if (precomp_mgr.ctx->identical_bytes > -1) { // successfully recompressed?
              if ((precomp_mgr.ctx->identical_bytes > precomp_mgr.ctx->best_identical_bytes)  || ((precomp_mgr.ctx->identical_bytes == precomp_mgr.ctx->best_identical_bytes) && (precomp_mgr.ctx->penalty_bytes_len < precomp_mgr.ctx->best_penalty_bytes_len))) {
                if (precomp_mgr.ctx->identical_bytes > precomp_mgr.switches.min_ident_size) {
                  print_to_log(PRECOMP_DEBUG_LOG, "Identical recompressed bytes: %lli of %lli\n", precomp_mgr.ctx->identical_bytes, compressed_stream_size);
                  print_to_log(PRECOMP_DEBUG_LOG, "Identical decompressed bytes: %lli of %lli\n", precomp_mgr.ctx->identical_bytes_decomp, decomp_bytes_total);
                }

				// Partial matches sometimes need all the decompressed bytes, but there are much less 
				// identical recompressed bytes - in these cases, all the decompressed bytes have to
				// be stored together with the remaining recompressed bytes, so the result won't compress
				// better than the original stream. What's important here is the ratio between recompressed ratio
				// and decompressed ratio that shouldn't get too high.
				// Example: A stream has 5 of 1000 identical recompressed bytes, but needs 1000 of 1000 decompressed bytes,
				// so the ratio is (1000/1000)/(5/1000) = 200 which is too high. With 5 of 1000 decompressed bytes or
				// 1000 of 1000 identical recompressed bytes, ratio would've been 1 and we'd accept it.

				float partial_ratio = ((float)precomp_mgr.ctx->identical_bytes_decomp / decomp_bytes_total) / ((float)precomp_mgr.ctx->identical_bytes / compressed_stream_size);
				if (partial_ratio < 3.0f) {
                    precomp_mgr.ctx->best_identical_bytes_decomp = precomp_mgr.ctx->identical_bytes_decomp;
                    precomp_mgr.ctx->best_identical_bytes = precomp_mgr.ctx->identical_bytes;
					if (precomp_mgr.ctx->penalty_bytes_len > 0) {
						memcpy(precomp_mgr.ctx->best_penalty_bytes.data(), precomp_mgr.ctx->penalty_bytes.data(), precomp_mgr.ctx->penalty_bytes_len);
            precomp_mgr.ctx->best_penalty_bytes_len = precomp_mgr.ctx->penalty_bytes_len;
					}
					else {
            precomp_mgr.ctx->best_penalty_bytes_len = 0;
					}
				} else {
					print_to_log(PRECOMP_DEBUG_LOG, "Not enough identical recompressed bytes\n");
				}
    }
  }
}


void write_header(Precomp& precomp_mgr) {
  // write the PCF file header, beware that this needs to be done before wrapping the output file with a CompressedOStreamBuffer
  char* input_file_name_without_path = new char[precomp_mgr.input_file_name.length() + 1];

  ostream_printf(*precomp_mgr.ctx->fout, "PCF");

  // version number
  precomp_mgr.ctx->fout->put(V_MAJOR);
  precomp_mgr.ctx->fout->put(V_MINOR);
  precomp_mgr.ctx->fout->put(V_MINOR2);

  // compression-on-the-fly method used
  precomp_mgr.ctx->fout->put(precomp_mgr.ctx->compression_otf_method);

  // write input file name without path
  const char* last_backslash = strrchr(precomp_mgr.input_file_name.c_str(), PATH_DELIM);
  if (last_backslash != NULL) {
    strcpy(input_file_name_without_path, last_backslash + 1);
  } else {
    strcpy(input_file_name_without_path, precomp_mgr.input_file_name.c_str());
  }

  ostream_printf(*precomp_mgr.ctx->fout, input_file_name_without_path);
  precomp_mgr.ctx->fout->put(0);

  delete[] input_file_name_without_path;
}

void read_header(Precomp& precomp_mgr) {
  if (precomp_mgr.header_already_read) throw std::runtime_error("Attempted to read the input stream header twice");
  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 3);
  if ((precomp_mgr.in[0] == 'P') && (precomp_mgr.in[1] == 'C') && (precomp_mgr.in[2] == 'F')) {
  } else {
    throw PrecompError(ERR_NO_PCF_HEADER);
  }

  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 3);
  if ((precomp_mgr.in[0] == V_MAJOR) && (precomp_mgr.in[1] == V_MINOR) && (precomp_mgr.in[2] == V_MINOR2)) {
  } else {
    throw PrecompError(
      ERR_PCF_HEADER_INCOMPATIBLE_VERSION,
      make_cstyle_format_string("PCF version info: %i.%i.%i\n", precomp_mgr.in[0], precomp_mgr.in[1], precomp_mgr.in[2])
    );
  }

  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 1);
  precomp_mgr.ctx->compression_otf_method = precomp_mgr.in[0];

  std::string header_filename;
  char c;
  do {
    c = precomp_mgr.ctx->fin->get();
    if (c != 0) header_filename += c;
  } while (c != 0);

  if (precomp_mgr.output_file_name.empty()) {
    precomp_mgr.output_file_name = header_filename;
  }
  precomp_mgr.header_already_read = true;
}

void convert_header(Precomp& precomp_mgr) {
  precomp_mgr.ctx->fin->seekg(0, std::ios_base::beg);

  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 3);
  if ((precomp_mgr.in[0] == 'P') && (precomp_mgr.in[1] == 'C') && (precomp_mgr.in[2] == 'F')) {
  } else {
    throw std::runtime_error(make_cstyle_format_string("Input stream has no valid PCF header\n"));
  }
  precomp_mgr.ctx->fout->write(reinterpret_cast<char*>(precomp_mgr.in), 3);

  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 3);
  if ((precomp_mgr.in[0] == V_MAJOR) && (precomp_mgr.in[1] == V_MINOR) && (precomp_mgr.in[2] == V_MINOR2)) {
  } else {
    throw std::runtime_error(make_cstyle_format_string(
      "Input stream was made with a different Precomp version\n"
      "PCF version info: %i.%i.%i\n",
      precomp_mgr.in[0], precomp_mgr.in[1], precomp_mgr.in[2]
    ));
  }
  precomp_mgr.ctx->fout->write(reinterpret_cast<char*>(precomp_mgr.in), 3);

  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), 1);
  precomp_mgr.conversion_from_method = precomp_mgr.in[0];
  if (precomp_mgr.conversion_from_method == precomp_mgr.conversion_to_method) {
    throw std::runtime_error(make_cstyle_format_string("Input file doesn't need to be converted\n"));
  }
  precomp_mgr.in[0] = precomp_mgr.conversion_to_method;
  precomp_mgr.ctx->fout->write(reinterpret_cast<char*>(precomp_mgr.in), 1);

  std::string header_filename = "";
  char c;
  do {
    c = precomp_mgr.ctx->fin->get();
    if (c != 0) header_filename += c;
  } while (c != 0);
  ostream_printf(*precomp_mgr.ctx->fout, header_filename);
  precomp_mgr.ctx->fout->put(0);
}

void fast_copy(Precomp& precomp_mgr, IStreamLike& file1, OStreamLike& file2, long long bytecount, bool update_progress) {
  if (bytecount == 0) return;

  long long i;
  int remaining_bytes = (bytecount % COPY_BUF_SIZE);
  long long maxi = (bytecount / COPY_BUF_SIZE);

  for (i = 1; i <= maxi; i++) {
    file1.read(reinterpret_cast<char*>(precomp_mgr.copybuf), COPY_BUF_SIZE);
    file2.write(reinterpret_cast<char*>(precomp_mgr.copybuf), COPY_BUF_SIZE);

    if (((i - 1) % FAST_COPY_WORK_SIGN_DIST) == 0) {
      precomp_mgr.call_progress_callback();
    }
  }
  if (remaining_bytes != 0) {
    file1.read(reinterpret_cast<char*>(precomp_mgr.copybuf), remaining_bytes);
    file2.write(reinterpret_cast<char*>(precomp_mgr.copybuf), remaining_bytes);
  }

  if ((update_progress) && (PRECOMP_VERBOSITY_LEVEL < PRECOMP_DEBUG_LOG)) precomp_mgr.ctx->uncompressed_bytes_written += bytecount;
}

std::tuple<long long, std::vector<char>> compare_files_penalty(Precomp& precomp_mgr, RecursionContext& context, IStreamLike& file1, IStreamLike& file2, long long pos1, long long pos2) {
  unsigned char input_bytes1[COMP_CHUNK];
  unsigned char input_bytes2[COMP_CHUNK];
  long long same_byte_count = 0;
  long long same_byte_count_penalty = 0;
  long long rek_same_byte_count = 0;
  long long rek_same_byte_count_penalty = -1;
  long long size1, size2, minsize;
  long long i;
  bool endNow = false;

  bool use_penalty_bytes = false;

  std::vector<char> penalty_bytes;

  long long compare_end;
  if (&file1 == context.fin.get()) {
    file2.seekg(0, std::ios_base::end);
    compare_end = file2.tellg();
  } else {
    file1.seekg(0, std::ios_base::end);
    file2.seekg(0, std::ios_base::end);
    compare_end = std::min(file1.tellg(), file2.tellg());
  }

  file1.seekg(pos1, std::ios_base::beg);
  file2.seekg(pos2, std::ios_base::beg);

  do {
    precomp_mgr.call_progress_callback();

    file1.read(reinterpret_cast<char*>(input_bytes1), COMP_CHUNK);
    size1 = file1.gcount();
    file2.read(reinterpret_cast<char*>(input_bytes2), COMP_CHUNK);
    size2 = file2.gcount();

    minsize = std::min(size1, size2);
    for (i = 0; i < minsize; i++) {
      if (input_bytes1[i] != input_bytes2[i]) {

        same_byte_count_penalty -= 5; // 4 bytes = position, 1 byte = new byte

        // if same_byte_count_penalty is too low, stop
        if ((long long)(same_byte_count_penalty + (compare_end - same_byte_count)) < 0) {
          endNow = true;
          break;
        }
        // stop, if penalty_bytes len gets too big
        if ((penalty_bytes.size() + 5) >= MAX_PENALTY_BYTES) {
          endNow = true;
          break;
        }

        // position
        penalty_bytes.push_back((same_byte_count >> 24) % 256);
        penalty_bytes.push_back((same_byte_count >> 16) % 256);
        penalty_bytes.push_back((same_byte_count >> 8) % 256);
        penalty_bytes.push_back(same_byte_count % 256);
        // new byte
        penalty_bytes.push_back(input_bytes1[i]);
      } else {
        same_byte_count_penalty++;
      }

      same_byte_count++;

      if (same_byte_count_penalty > rek_same_byte_count_penalty) {
        use_penalty_bytes = true;
        rek_same_byte_count = same_byte_count;
        rek_same_byte_count_penalty = same_byte_count_penalty;
      }

    }
  } while ((minsize == COMP_CHUNK) && (!endNow));

  return { rek_same_byte_count, use_penalty_bytes ? penalty_bytes: std::vector<char>()};
}

// JPG routines
void packjpg_mp3_dll_msg() {
  print_to_log(PRECOMP_NORMAL_LOG, "Using packJPG for JPG recompression, packMP3 for MP3 recompression.\n");
  print_to_log(PRECOMP_NORMAL_LOG, "%s\n", pjglib_version_info());
  print_to_log(PRECOMP_NORMAL_LOG, "%s\n", pmplib_version_info());
  print_to_log(PRECOMP_NORMAL_LOG, "More about packJPG and packMP3 here: http://www.matthiasstirner.com\n\n");

}

void try_decompression_bzip2(Precomp& precomp_mgr, int compression_level, PrecompTmpFile& tmpfile) {
  init_decompression_variables(*precomp_mgr.ctx);

  // try to decompress at current position
  long long compressed_stream_size = -1;
  precomp_mgr.ctx->retval = try_to_decompress_bzip2(precomp_mgr, *precomp_mgr.ctx->fin, compression_level, compressed_stream_size, tmpfile);

  if (precomp_mgr.ctx->retval > 0) { // seems to be a zLib-Stream

    precomp_mgr.statistics.decompressed_streams_count++;
    precomp_mgr.statistics.decompressed_bzip2_count++;

    print_to_log(PRECOMP_DEBUG_LOG, "Possible bZip2-Stream found at position %lli, compression level = %i\n", precomp_mgr.ctx->saved_input_file_pos, compression_level);
    print_to_log(PRECOMP_DEBUG_LOG, "Compressed size: %lli\n", compressed_stream_size);

    WrappedFStream ftempout;
    ftempout.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
    ftempout.seekg(0, std::ios_base::end);
    print_to_log(PRECOMP_DEBUG_LOG, "Can be decompressed to %lli bytes\n", ftempout.tellg());
    ftempout.close();
  }

  tmpfile.reopen();
  try_recompress_bzip2(precomp_mgr, *precomp_mgr.ctx->fin, compression_level, compressed_stream_size, tmpfile);

  if ((precomp_mgr.ctx->best_identical_bytes > precomp_mgr.switches.min_ident_size) && (precomp_mgr.ctx->best_identical_bytes < precomp_mgr.ctx->best_identical_bytes_decomp)) {
    precomp_mgr.statistics.recompressed_streams_count++;
    precomp_mgr.statistics.recompressed_bzip2_count++;

    print_to_log(PRECOMP_DEBUG_LOG, "Best match: %lli bytes, decompressed to %lli bytes\n", precomp_mgr.ctx->best_identical_bytes, precomp_mgr.ctx->best_identical_bytes_decomp);

    precomp_mgr.ctx->non_zlib_was_used = true;

    // end uncompressed data

    precomp_mgr.ctx->compressed_data_found = true;
    end_uncompressed_data(precomp_mgr);

    // check recursion
    tmpfile.reopen();
    recursion_result r = recursion_compress(precomp_mgr, precomp_mgr.ctx->best_identical_bytes, precomp_mgr.ctx->best_identical_bytes_decomp, tmpfile);

    // write compressed data header (bZip2)

    int header_byte = 1;
    if (precomp_mgr.ctx->best_penalty_bytes_len != 0) {
      header_byte += 2;
    }
    if (r.success) {
      header_byte += 128;
    }
    precomp_mgr.ctx->fout->put(header_byte);
    precomp_mgr.ctx->fout->put(D_BZIP2); // Base64
    precomp_mgr.ctx->fout->put(compression_level);

    // store penalty bytes, if any
    if (precomp_mgr.ctx->best_penalty_bytes_len != 0) {
      print_to_log(PRECOMP_DEBUG_LOG, "Penalty bytes were used: %i bytes\n", precomp_mgr.ctx->best_penalty_bytes_len);
      fout_fput_vlint(*precomp_mgr.ctx->fout, precomp_mgr.ctx->best_penalty_bytes_len);
      for (int pbc = 0; pbc < precomp_mgr.ctx->best_penalty_bytes_len; pbc++) {
        precomp_mgr.ctx->fout->put(precomp_mgr.ctx->best_penalty_bytes[pbc]);
      }
    }

    fout_fput_vlint(*precomp_mgr.ctx->fout, precomp_mgr.ctx->best_identical_bytes);
    fout_fput_vlint(*precomp_mgr.ctx->fout, precomp_mgr.ctx->best_identical_bytes_decomp);

    if (r.success) {
      fout_fput_vlint(*precomp_mgr.ctx->fout, r.file_length);
    }

    // write decompressed data
    if (r.success) {
      write_decompressed_data(precomp_mgr, *precomp_mgr.ctx->fout, r.file_length, r.file_name.c_str());
      remove(r.file_name.c_str());
    } else {
      write_decompressed_data(precomp_mgr, *precomp_mgr.ctx->fout, precomp_mgr.ctx->best_identical_bytes_decomp, tmpfile.file_path.c_str());
    }

    // start new uncompressed data

    // set input file pointer after recompressed data
    precomp_mgr.ctx->input_file_pos += precomp_mgr.ctx->best_identical_bytes - 1;
    precomp_mgr.ctx->cb += precomp_mgr.ctx->best_identical_bytes - 1;

  } else {
    print_to_log(PRECOMP_DEBUG_LOG, "No matches\n");
  }
}

// Base64 alphabet
static const char b64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

unsigned char base64_char_decode(unsigned char c) {
  if ((c >= 'A') && (c <= 'Z')) {
    return (c - 'A');
  }
  if ((c >= 'a') && (c <= 'z')) {
    return (c - 'a' + 26);
  }
  if ((c >= '0') && (c <= '9')) {
    return (c - '0' + 52);
  }
  if (c == '+') return 62;
  if (c == '/') return 63;

  if (c == '=') return 64; // padding
  return 65; // invalid
}

void base64_reencode(Precomp& precomp_mgr, IStreamLike& file_in, OStreamLike& file_out, int line_count, unsigned int* base64_line_len, long long max_in_count, long long max_byte_count) {
          int line_nr = 0;
          unsigned int act_line_len = 0;
          int avail_in;
          unsigned char a,b,c;
          int i;
          long long act_byte_count = 0;

          long long remaining_bytes = max_in_count;

          do {
            if (remaining_bytes > DIV3CHUNK) {
              file_in.read(reinterpret_cast<char*>(precomp_mgr.in), DIV3CHUNK);
              avail_in = file_in.gcount();
            } else {
              file_in.read(reinterpret_cast<char*>(precomp_mgr.in), remaining_bytes);
              avail_in = file_in.gcount();
            }
            remaining_bytes -= avail_in;

            // make sure avail_in mod 3 = 0, pad with 0 bytes
            while ((avail_in % 3) != 0) {
              precomp_mgr.in[avail_in] = 0;
              avail_in++;
            }

            for (i = 0; i < (avail_in/3); i++) {
              a = precomp_mgr.in[i * 3];
              b = precomp_mgr.in[i * 3 + 1];
              c = precomp_mgr.in[i * 3 + 2];
              if (act_byte_count < max_byte_count) file_out.put(b64[a >> 2]);
              act_byte_count++;
              act_line_len++;
              if (act_line_len == base64_line_len[line_nr]) { // end of line, write CRLF
                if (act_byte_count < max_byte_count) file_out.put(13);
                act_byte_count++;
                if (act_byte_count < max_byte_count) file_out.put(10);
                act_byte_count++;
                act_line_len = 0;
                line_nr++;
                if (line_nr == line_count) break;
              }
              if (act_byte_count < max_byte_count) file_out.put(b64[((a & 0x03) << 4) | (b >> 4)]);
              act_byte_count++;
              act_line_len++;
              if (act_line_len == base64_line_len[line_nr]) { // end of line, write CRLF
                if (act_byte_count < max_byte_count) file_out.put(13);
                act_byte_count++;
                if (act_byte_count < max_byte_count) file_out.put(10);
                act_byte_count++;
                act_line_len = 0;
                line_nr++;
                if (line_nr == line_count) break;
              }
              if (act_byte_count < max_byte_count) file_out.put(b64[((b & 0x0F) << 2) | (c >> 6)]);
              act_byte_count++;
              act_line_len++;
              if (act_line_len == base64_line_len[line_nr]) { // end of line, write CRLF
                if (act_byte_count < max_byte_count) file_out.put(13);
                act_byte_count++;
                if (act_byte_count < max_byte_count) file_out.put(10);
                act_byte_count++;
                act_line_len = 0;
                line_nr++;
                if (line_nr == line_count) break;
              }
              if (act_byte_count < max_byte_count) file_out.put(b64[c & 63]);
              act_byte_count++;
              act_line_len++;
              if (act_line_len == base64_line_len[line_nr]) { // end of line, write CRLF
                if (act_byte_count < max_byte_count) file_out.put(13);
                act_byte_count++;
                if (act_byte_count < max_byte_count) file_out.put(10);
                act_byte_count++;
                act_line_len = 0;
                line_nr++;
                if (line_nr == line_count) break;
              }
            }
            if (line_nr == line_count) break;
          } while ((remaining_bytes > 0) && (avail_in > 0));

}

void try_decompression_base64(Precomp& precomp_mgr, int base64_header_length, PrecompTmpFile& tmpfile) {
  init_decompression_variables(*precomp_mgr.ctx);
  tmpfile.close();

  // try to decode at current position
  remove(tmpfile.file_path.c_str());
  precomp_mgr.ctx->fin->seekg(precomp_mgr.ctx->input_file_pos, std::ios_base::beg);

  unsigned char base64_data[CHUNK >> 2];
  unsigned int* base64_line_len = new unsigned int[65536];

  int avail_in = 0;
  int i, j, k;
  unsigned char a, b, c, d;
  int cr_count = 0;
  bool decoding_failed = false;
  bool stream_finished = false;
  k = 0;

  unsigned int line_nr = 0;
  int line_count = 0;
  unsigned int act_line_len = 0;

  {
    std::fstream ftempout;
    ftempout.open(tmpfile.file_path, std::ios_base::out | std::ios_base::binary);
    do {
      precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.in), CHUNK);
      avail_in = precomp_mgr.ctx->fin->gcount();
      for (i = 0; i < (avail_in >> 2); i++) {
        // are these valid base64 chars?
        for (j = (i << 2); j < ((i << 2) + 4); j++) {
          c = base64_char_decode(precomp_mgr.in[j]);
          if (c < 64) {
            base64_data[k] = c;
            k++;
            cr_count = 0;
            act_line_len++;
            continue;
          }
          if ((precomp_mgr.in[j] == 13) || (precomp_mgr.in[j] == 10)) {
            if (precomp_mgr.in[j] == 13) {
              cr_count++;
              if (cr_count == 2) { // double CRLF -> base64 end
                stream_finished = true;
                break;
              }
              base64_line_len[line_nr] = act_line_len;
              line_nr++;
              if (line_nr == 65534) stream_finished = true;
              act_line_len = 0;
            }
            continue;
          }
          else {
            cr_count = 0;
          }
          stream_finished = true;
          base64_line_len[line_nr] = act_line_len;
          line_nr++;
          act_line_len = 0;
          // "=" -> Padding
          if (precomp_mgr.in[j] == '=') {
            while ((k % 4) != 0) {
              base64_data[k] = 0;
              k++;
            }
            break;
          }
          // "-" -> base64 end
          if (precomp_mgr.in[j] == '-') break;
          // invalid char found -> decoding failed
          decoding_failed = true;
          break;
        }
        if (decoding_failed) break;

        for (j = 0; j < (k >> 2); j++) {
          a = base64_data[(j << 2)];
          b = base64_data[(j << 2) + 1];
          c = base64_data[(j << 2) + 2];
          d = base64_data[(j << 2) + 3];
          ftempout.put((a << 2) | (b >> 4));
          ftempout.put(((b << 4) & 0xFF) | (c >> 2));
          ftempout.put(((c << 6) & 0xFF) | d);
        }
        if (stream_finished) break;
        for (j = 0; j < (k % 4); j++) {
          base64_data[j] = base64_data[((k >> 2) << 2) + j];
        }
        k = k % 4;
      }
    } while ((avail_in == CHUNK) && (!decoding_failed) && (!stream_finished));

    line_count = line_nr;
    // if one of the lines is longer than 255 characters -> decoding failed
    for (i = 0; i < line_count; i++) {
      if (base64_line_len[i] > 255) {
        decoding_failed = true;
        break;
      }
    }
    ftempout.close();
  }

  if (!decoding_failed) {
    int line_case = -1;
    // check line case
    if (line_count == 1) {
      line_case = 0; // one length for all lines
    }
    else {
      for (i = 1; i < (line_count - 1); i++) {
        if (base64_line_len[i] != base64_line_len[0]) {
          line_case = 2; // save complete line length list
          break;
        }
      }
      if (line_case == -1) {
        // check last line
        if (base64_line_len[line_count - 1] == base64_line_len[0]) {
          line_case = 0; // one length for all lines
        }
        else {
          line_case = 1; // first length for all lines, second length for last line
        }
      }
    }

    precomp_mgr.statistics.decompressed_streams_count++;
    precomp_mgr.statistics.decompressed_base64_count++;

    {
      WrappedFStream ftempout;
      ftempout.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
      ftempout.seekg(0, std::ios_base::end);
      precomp_mgr.ctx->identical_bytes = ftempout.tellg();
    }

    print_to_log(PRECOMP_DEBUG_LOG, "Possible Base64-Stream (line_case %i, line_count %i) found at position %lli\n", line_case, line_count, precomp_mgr.ctx->saved_input_file_pos);
    print_to_log(PRECOMP_DEBUG_LOG, "Can be decoded to %lli bytes\n", precomp_mgr.ctx->identical_bytes);

    // try to re-encode Base64 data
    {
      WrappedFStream ftempout;
      ftempout.open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
      if (!ftempout.is_open()) {
        throw PrecompError(ERR_TEMP_FILE_DISAPPEARED);
      }

      std::string frecomp_filename = tmpfile.file_path + "_rec";
      remove(frecomp_filename.c_str());
      PrecompTmpFile frecomp;
      frecomp.open(frecomp_filename, std::ios_base::in | std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
      base64_reencode(precomp_mgr, ftempout, frecomp, line_count, base64_line_len);

      ftempout.close();
      precomp_mgr.ctx->identical_bytes_decomp = compare_files(precomp_mgr, *precomp_mgr.ctx->fin, frecomp, precomp_mgr.ctx->input_file_pos, 0);
    }

    if (precomp_mgr.ctx->identical_bytes_decomp > precomp_mgr.switches.min_ident_size) {
      precomp_mgr.statistics.recompressed_streams_count++;
      precomp_mgr.statistics.recompressed_base64_count++;
      print_to_log(PRECOMP_DEBUG_LOG, "Match: encoded to %lli bytes\n");

      // end uncompressed data

      precomp_mgr.ctx->compressed_data_found = true;
      end_uncompressed_data(precomp_mgr);

      // check recursion
      tmpfile.reopen();
      recursion_result r = recursion_compress(precomp_mgr, precomp_mgr.ctx->identical_bytes_decomp, precomp_mgr.ctx->identical_bytes, tmpfile);

      // write compressed data header (Base64)
      int header_byte = 1 + (line_case << 2);
      if (r.success) {
        header_byte += 128;
      }
      precomp_mgr.ctx->fout->put(header_byte);
      precomp_mgr.ctx->fout->put(D_BASE64); // Base64

      fout_fput_vlint(*precomp_mgr.ctx->fout, base64_header_length);

      // write "header", but change first char to prevent re-detection
      precomp_mgr.ctx->fout->put(precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb] - 1);
      precomp_mgr.ctx->fout->write(reinterpret_cast<char*>(precomp_mgr.ctx->in_buf + precomp_mgr.ctx->cb + 1), base64_header_length - 1);

      fout_fput_vlint(*precomp_mgr.ctx->fout, line_count);
      if (line_case == 2) {
        for (i = 0; i < line_count; i++) {
          precomp_mgr.ctx->fout->put(base64_line_len[i]);
        }
      }
      else {
        precomp_mgr.ctx->fout->put(base64_line_len[0]);
        if (line_case == 1) precomp_mgr.ctx->fout->put(base64_line_len[line_count - 1]);
      }

      delete[] base64_line_len;

      fout_fput_vlint(*precomp_mgr.ctx->fout, precomp_mgr.ctx->identical_bytes);
      fout_fput_vlint(*precomp_mgr.ctx->fout, precomp_mgr.ctx->identical_bytes_decomp);

      if (r.success) {
        fout_fput_vlint(*precomp_mgr.ctx->fout, r.file_length);
      }

      // write decompressed data
      if (r.success) {
        write_decompressed_data(precomp_mgr, *precomp_mgr.ctx->fout, r.file_length, r.file_name.c_str());
        remove(r.file_name.c_str());
      } else {
        write_decompressed_data(precomp_mgr, *precomp_mgr.ctx->fout, precomp_mgr.ctx->identical_bytes, tmpfile.file_path.c_str());
      }

      // start new uncompressed data

      // set input file pointer after recompressed data
      precomp_mgr.ctx->input_file_pos += precomp_mgr.ctx->identical_bytes_decomp - 1;
      precomp_mgr.ctx->cb += precomp_mgr.ctx->identical_bytes_decomp - 1;
    }
    else {
      print_to_log(PRECOMP_DEBUG_LOG, "No match\n");
    }
  }
}

std::fstream& tryOpen(const char* filename, std::ios_base::openmode mode) {
  std::fstream fptr;
  fptr.open(filename,mode);

  if (fptr.is_open()) return fptr;

  long long timeoutstart = get_time_ms();
  while ((!fptr.is_open()) && ((get_time_ms() - timeoutstart) <= 15000)) {
    fptr.open(filename,mode);
  }
  if (!fptr.is_open()) {
    throw std::runtime_error(make_cstyle_format_string("ERROR: Access denied for %s\n", filename));
  }
  print_to_log(PRECOMP_DEBUG_LOG, "Access problem for %s\n", filename);
  print_to_log(PRECOMP_DEBUG_LOG, "Time for getting access: %li ms\n", (long)(get_time_ms() - timeoutstart));
  return fptr;
}

uintmax_t fileSize64(const char* filename, int* error_code) {
  std::error_code ec;
  auto size = std::filesystem::file_size(filename, ec);
  *error_code = ec.value();
  return size;
}
void print_to_terminal(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  va_list args_copy;
  va_copy(args_copy, args);
  int length = std::vsnprintf(nullptr, 0, fmt, args);
  va_end(args);
  assert(length >= 0);

  char* buf = new char[length + 1];
  std::vsnprintf(buf, length + 1, fmt, args_copy);
  va_end(args_copy);

  std::string str(buf);
  delete[] buf;
  print_to_console(str);
}

void recursion_push(Precomp& precomp_mgr) {
  precomp_mgr.recursion_contexts_stack.push_back(std::move(precomp_mgr.ctx));
  precomp_mgr.ctx = std::unique_ptr<RecursionContext>(new RecursionContext(precomp_mgr));
}

void recursion_pop(Precomp& precomp_mgr) {
  precomp_mgr.ctx = std::move(precomp_mgr.recursion_contexts_stack.back());
  precomp_mgr.recursion_contexts_stack.pop_back();
}

recursion_result recursion_compress(Precomp& precomp_mgr, long long compressed_bytes, long long decompressed_bytes, PrecompTmpFile& tmpfile, bool deflate_type, bool in_memory) {
  recursion_result tmp_r;
  tmp_r.success = false;

  float recursion_min_percent = ((precomp_mgr.ctx->input_file_pos + precomp_mgr.ctx->uncompressed_bytes_written) / ((float)precomp_mgr.ctx->fin_length + precomp_mgr.ctx->uncompressed_bytes_total)) * (precomp_mgr.ctx->global_max_percent - precomp_mgr.ctx->global_min_percent) + precomp_mgr.ctx->global_min_percent;
  float recursion_max_percent = ((precomp_mgr.ctx->input_file_pos + precomp_mgr.ctx->uncompressed_bytes_written +(compressed_bytes - 1)) / ((float)precomp_mgr.ctx->fin_length + precomp_mgr.ctx->uncompressed_bytes_total)) * (precomp_mgr.ctx->global_max_percent - precomp_mgr.ctx->global_min_percent) + precomp_mgr.ctx->global_min_percent;

  bool rescue_anything_was_used = false;
  bool rescue_non_zlib_was_used = false;

  if ((precomp_mgr.recursion_depth + 1) > precomp_mgr.max_recursion_depth) {
    precomp_mgr.max_recursion_depth_reached = true;
    return tmp_r;
  }

  if (deflate_type && in_memory) {
    auto decomp_io_buf_ptr = precomp_mgr.ctx->decomp_io_buf.data();
    memiostream memstream = memiostream::make(decomp_io_buf_ptr, decomp_io_buf_ptr + decompressed_bytes);
    fast_copy(precomp_mgr, memstream, tmpfile, decompressed_bytes);
  }
  tmpfile.close();

  recursion_push(precomp_mgr);

  if (!deflate_type) {
    // shorten tempfile1 to decompressed_bytes
    std::filesystem::resize_file(tmpfile.file_path, decompressed_bytes);
  }

  precomp_mgr.ctx->fin_length = std::filesystem::file_size(tmpfile.file_path.c_str());
  auto fin = new std::ifstream();
  fin->open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
  if (!fin->is_open()) {
    throw std::runtime_error(make_cstyle_format_string("ERROR: Recursion input file \"%s\" doesn't exist\n", tmpfile.file_path.c_str()));
  }
  precomp_mgr.ctx->set_input_stream(fin);

  tmp_r.file_name = tmpfile.file_path;
  tmp_r.file_name += '_';
  auto fout = new std::ofstream();
  fout->open(tmp_r.file_name.c_str(), std::ios_base::out | std::ios_base::binary);
  precomp_mgr.ctx->set_output_stream(fout, true);

  precomp_mgr.ctx->intense_ignore_offsets = new std::set<long long>();
  precomp_mgr.ctx->brute_ignore_offsets = new std::set<long long>();

  // disable compression-on-the-fly in recursion - we don't want compressed compressed streams
  precomp_mgr.ctx->compression_otf_method = OTF_NONE;

  precomp_mgr.recursion_depth++;
  print_to_log(PRECOMP_DEBUG_LOG, "Recursion start - new recursion depth %i\n", precomp_mgr.recursion_depth);
  const auto ret_code = compress_file(precomp_mgr, recursion_min_percent, recursion_max_percent);
  if (ret_code != RETURN_SUCCESS && ret_code != RETURN_NOTHING_DECOMPRESSED) throw PrecompError(ret_code);
  tmp_r.success = ret_code == RETURN_SUCCESS;

  delete precomp_mgr.ctx->intense_ignore_offsets;
  delete precomp_mgr.ctx->brute_ignore_offsets;
  // TODO CHECK: Delete ctx?

  if (precomp_mgr.ctx->anything_was_used)
    rescue_anything_was_used = true;

  if (precomp_mgr.ctx->non_zlib_was_used)
    rescue_non_zlib_was_used = true;

  precomp_mgr.recursion_depth--;
  recursion_pop(precomp_mgr);

  if (rescue_anything_was_used)
    precomp_mgr.ctx->anything_was_used = true;

  if (rescue_non_zlib_was_used)
    precomp_mgr.ctx->non_zlib_was_used = true;

  if (tmp_r.success) {
    print_to_log(PRECOMP_DEBUG_LOG, "Recursion streams found\n");
  } else {
    print_to_log(PRECOMP_DEBUG_LOG, "No recursion streams found\n");
  }
  print_to_log(PRECOMP_DEBUG_LOG, "Recursion end - back to recursion depth %i\n", precomp_mgr.recursion_depth);

  if (!tmp_r.success) {
    remove(tmp_r.file_name.c_str());
    tmp_r.file_name = "";
  } else {
    if ((precomp_mgr.recursion_depth + 1) > precomp_mgr.max_recursion_depth_used)
      precomp_mgr.max_recursion_depth_used = (precomp_mgr.recursion_depth + 1);
    // get recursion file size
    tmp_r.file_length = std::filesystem::file_size(tmp_r.file_name.c_str());
  }

  return tmp_r;
}
recursion_result recursion_write_file_and_compress(Precomp& precomp_mgr, const recompress_deflate_result& rdres, PrecompTmpFile& tmpfile) {
  recursion_result r = recursion_compress(precomp_mgr, rdres.compressed_stream_size, rdres.uncompressed_stream_size, tmpfile, true, rdres.uncompressed_in_memory);
  return r;
}

recursion_result recursion_decompress(Precomp& precomp_mgr, long long recursion_data_length, PrecompTmpFile& tmpfile) {
  recursion_result tmp_r;

  fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, tmpfile, recursion_data_length);
  tmpfile.close();

  recursion_push(precomp_mgr);

  precomp_mgr.ctx->fin_length = std::filesystem::file_size(tmpfile.file_path.c_str());
  auto fin = new std::ifstream();
  fin->open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
  if (!fin->is_open()) {
    throw std::runtime_error(make_cstyle_format_string("ERROR: Recursion input file \"%s\" doesn't exist\n", tmpfile.file_path.c_str()));
  }
  precomp_mgr.ctx->set_input_stream(fin);

  tmp_r.file_name = tmpfile.file_path;
  tmp_r.file_name += '_';
  auto fout = new std::ofstream();
  fout->open(tmp_r.file_name.c_str(), std::ios_base::out | std::ios_base::binary);
  precomp_mgr.ctx->set_output_stream(fout, true);

  // disable compression-on-the-fly in recursion - we don't want compressed compressed streams
  precomp_mgr.ctx->compression_otf_method = OTF_NONE;

  precomp_mgr.recursion_depth++;
  print_to_log(PRECOMP_DEBUG_LOG, "Recursion start - new recursion depth %i\n", precomp_mgr.recursion_depth);
  const auto ret_code = decompress_file(precomp_mgr);
  if (ret_code != RETURN_SUCCESS) throw PrecompError(ret_code);

  // TODO CHECK: Delete ctx?

  precomp_mgr.recursion_depth--;
  recursion_pop(precomp_mgr);

  print_to_log(PRECOMP_DEBUG_LOG, "Recursion end - back to recursion depth %i\n", precomp_mgr.recursion_depth);

  // get recursion file size
  tmp_r.file_length = std::filesystem::file_size(tmp_r.file_name.c_str());

  tmp_r.frecurse->open(tmp_r.file_name, std::ios_base::in | std::ios_base::binary);

  return tmp_r;
}

void fout_fput32_little_endian(OStreamLike& output, int v) {
  output.put(v % 256);
  output.put((v >> 8) % 256);
  output.put((v >> 16) % 256);
  output.put((v >> 24) % 256);
}

void fout_fput32(OStreamLike& output, int v) {
  output.put((v >> 24) % 256);
  output.put((v >> 16) % 256);
  output.put((v >> 8) % 256);
  output.put(v % 256);
}

void fout_fput32(OStreamLike& output, unsigned int v) {
  output.put((v >> 24) % 256);
  output.put((v >> 16) % 256);
  output.put((v >> 8) % 256);
  output.put(v % 256);
}

void fout_fput_vlint(OStreamLike& output, unsigned long long v) {
  while (v >= 128) {
    output.put((v & 127) + 128);
    v = (v >> 7) - 1;
  }
  output.put(v);
}
char make_deflate_pcf_hdr_flags(const recompress_deflate_result& rdres) {
  return 1 + (rdres.zlib_perfect ? rdres.zlib_comp_level << 2 : 2);
}
void fout_fput_deflate_hdr(OStreamLike& output, const unsigned char type, const unsigned char flags,
                           const recompress_deflate_result& rdres,
                           const unsigned char* hdr, const unsigned hdr_length,
                           const bool inc_last_hdr_byte) {
  output.put(make_deflate_pcf_hdr_flags(rdres) + flags);
  output.put(type); // PDF/PNG/...
  if (rdres.zlib_perfect) {
    output.put(((rdres.zlib_window_bits - 8) << 4) + rdres.zlib_mem_level);
  }
  fout_fput_vlint(output, hdr_length);
  if (!inc_last_hdr_byte) {
    output.write(reinterpret_cast<char*>(const_cast<unsigned char*>(hdr)), hdr_length);
  } else {
    output.write(reinterpret_cast<char*>(const_cast<unsigned char*>(hdr)), hdr_length - 1);
    output.put(hdr[hdr_length - 1] + 1);
  }
}
void fin_fget_deflate_hdr(IStreamLike& input, OStreamLike& output, recompress_deflate_result& rdres, const unsigned char flags,
                          unsigned char* hdr_data, unsigned& hdr_length, 
                          const bool inc_last_hdr_byte) {
  rdres.zlib_perfect = (flags & 2) == 0;
  if (rdres.zlib_perfect) {
    unsigned char zlib_params = input.get();
    rdres.zlib_comp_level  = (flags & 0x3c) >> 2;
    rdres.zlib_mem_level   = zlib_params & 0x0f;
    rdres.zlib_window_bits = ((zlib_params >> 4) & 0x7) + 8;
  }
  hdr_length = fin_fget_vlint(input);
  if (!inc_last_hdr_byte) {
    input.read(reinterpret_cast<char*>(hdr_data), hdr_length);
  } else {
    input.read(reinterpret_cast<char*>(hdr_data), hdr_length - 1);
    hdr_data[hdr_length - 1] = input.get() - 1;
  }
  output.write(reinterpret_cast<char*>(hdr_data), hdr_length);
}
void fout_fput_recon_data(OStreamLike& output, const recompress_deflate_result& rdres) {
  if (!rdres.zlib_perfect) {
    fout_fput_vlint(output, rdres.recon_data.size());
    output.write(reinterpret_cast<char*>(const_cast<unsigned char*>(rdres.recon_data.data())), rdres.recon_data.size());
  }

  fout_fput_vlint(output, rdres.compressed_stream_size);
  fout_fput_vlint(output, rdres.uncompressed_stream_size);
}
void fin_fget_recon_data(IStreamLike& input, recompress_deflate_result& rdres) {
  if (!rdres.zlib_perfect) {
    size_t sz = fin_fget_vlint(input);
    rdres.recon_data.resize(sz);
    input.read(reinterpret_cast<char*>(rdres.recon_data.data()), rdres.recon_data.size());
  }

  rdres.compressed_stream_size = fin_fget_vlint(input);
  rdres.uncompressed_stream_size = fin_fget_vlint(input);
}
void fout_fput_uncompressed(Precomp& precomp_mgr, const recompress_deflate_result& rdres, PrecompTmpFile& tmpfile) {
    write_decompressed_data_io_buf(precomp_mgr, rdres.uncompressed_stream_size, rdres.uncompressed_in_memory, tmpfile.file_path.c_str());
}
void fin_fget_uncompressed(const recompress_deflate_result&) {
}
void fout_fput_deflate_rec(Precomp& precomp_mgr, const unsigned char type,
                           const recompress_deflate_result& rdres,
                           const unsigned char* hdr, const unsigned hdr_length, const bool inc_last,
                           const recursion_result& recres, PrecompTmpFile& tmpfile) {
  fout_fput_deflate_hdr(*precomp_mgr.ctx->fout, type, recres.success ? 128 : 0, rdres, hdr, hdr_length, inc_last);
  fout_fput_recon_data(*precomp_mgr.ctx->fout, rdres);
  
  // write decompressed data
  if (recres.success) {
    fout_fput_vlint(*precomp_mgr.ctx->fout, recres.file_length);
    write_decompressed_data(precomp_mgr, *precomp_mgr.ctx->fout, recres.file_length, recres.file_name.c_str());
    remove(recres.file_name.c_str());
  } else {
    fout_fput_uncompressed(precomp_mgr, rdres, tmpfile);
  }
}

int32_t fin_fget32_little_endian(std::istream& input) {
  int32_t result = 0;
  result += ((long)input.get() << 0);
  result += ((long)input.get() << 8);
  result += ((long)input.get() << 16);
  result += ((long)input.get() << 24);
  return result;
}
int32_t fin_fget32(IStreamLike& input) {
  int32_t result = 0;
  result += ((long)input.get() << 24);
  result += ((long)input.get() << 16);
  result += ((long)input.get() << 8);
  result += (long)input.get();
  return result;
}
long long fin_fget_vlint(IStreamLike& input) {
  unsigned char c;
  long long v = 0, o = 0, s = 0;
  while ((c = input.get()) >= 128) {
    v += (((long long)(c & 127)) << s);
    s += 7;
    o = (o + 1) << 7;
  }
  return v + o + (((long long)c) << s);
}
