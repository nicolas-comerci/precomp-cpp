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
#include "formats/bzip2.h"
#include "formats/mp3.h"
#include "formats/pdf.h"
#include "formats/jpeg.h"
#include "formats/gif.h"
#include "formats/png.h"
#include "formats/swf.h"
#include "formats/base64.h"

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
  print_to_log(PRECOMP_DEBUG_LOG, "Penalty bytes were used: %i bytes\n", penalty_bytes.size());
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

void end_uncompressed_data(Precomp& precomp_mgr) {

  if (!precomp_mgr.ctx->uncompressed_data_in_work) return;

  fout_fput_vlint(*precomp_mgr.ctx->fout, precomp_mgr.ctx->uncompressed_length);

  // fast copy of uncompressed data
  precomp_mgr.ctx->fin->seekg(precomp_mgr.ctx->uncompressed_pos, std::ios_base::beg);
  fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, precomp_mgr.ctx->uncompressed_length, true);

  precomp_mgr.ctx->uncompressed_length = -1;

  precomp_mgr.ctx->uncompressed_data_in_work = false;
}

void init_decompression_variables(RecursionContext& context) {
  context.identical_bytes = -1;
  context.best_identical_bytes = -1;
  context.best_penalty_bytes_len = 0;
  context.best_identical_bytes_decomp = -1;
  context.identical_bytes_decomp = -1;
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
  }
  else {
    strcpy(input_file_name_without_path, precomp_mgr.input_file_name.c_str());
  }

  ostream_printf(*precomp_mgr.ctx->fout, input_file_name_without_path);
  precomp_mgr.ctx->fout->put(0);

  delete[] input_file_name_without_path;
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

  auto ignore_set = precomp_mgr.switches.ignore_set();

  for (precomp_mgr.ctx->input_file_pos = 0; precomp_mgr.ctx->input_file_pos < precomp_mgr.ctx->fin_length; precomp_mgr.ctx->input_file_pos++) {
  bool compressed_data_found = false;

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
      compressed_data_found = result.success;

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

    if ((!compressed_data_found) && (precomp_mgr.switches.use_gzip)) { // no ZIP header -> GZip header?
      if (gzip_header_check(precomp_mgr, &precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb])) {
        precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
        precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

        auto result = try_decompression_gzip(precomp_mgr);
        compressed_data_found = result.success;

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

    if ((!compressed_data_found) && (precomp_mgr.switches.use_pdf)) { // no Gzip header -> PDF FlateDecode?
      if (pdf_header_check(precomp_mgr.ctx->in_buf + precomp_mgr.ctx->cb)) {
        precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
        precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

        auto result = precompress_pdf(precomp_mgr);
        compressed_data_found = result.success;

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

    if ((!compressed_data_found) && (precomp_mgr.switches.use_png)) { // no PDF header -> PNG IDAT?
      if (png_header_check(precomp_mgr.ctx->in_buf + precomp_mgr.ctx->cb)) {
        auto result = precompress_png(precomp_mgr);
        compressed_data_found = result.success;

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

    if ((!compressed_data_found) && (precomp_mgr.switches.use_gif)) { // no PNG header -> GIF header?
      if (gif_header_check(&precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb])) {
        precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
        precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

        auto result = precompress_gif(precomp_mgr);
        compressed_data_found = result.success;
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

    if ((!compressed_data_found) && (precomp_mgr.switches.use_jpg)) { // no GIF header -> JPG header?
      if (jpeg_header_check(&precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb])) {
        precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
        precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

        auto result = precompress_jpeg(precomp_mgr);
        compressed_data_found = result.success;
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

    if ((!compressed_data_found) && (precomp_mgr.switches.use_mp3)) { // no JPG header -> MP3 header?
      if (mp3_header_check(&precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb])) { // frame start
        precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
        precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

        auto result = precompress_mp3(precomp_mgr);
        compressed_data_found = result.success;
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

    if ((!compressed_data_found) && (precomp_mgr.switches.use_swf)) { // no MP3 header -> SWF header?
      // CWS = Compressed SWF file
      if (swf_header_check(&precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb])) {
        precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
        precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

        auto result = try_decompression_swf(precomp_mgr);
        compressed_data_found = result.success;
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

    if ((!compressed_data_found) && (precomp_mgr.switches.use_base64)) { // no SWF header -> Base64?
      if (base64_header_check(&precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb])) {
        precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
        precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

        auto result = precompress_base64(precomp_mgr);
        compressed_data_found = result.success;
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

    if ((!compressed_data_found) && (precomp_mgr.switches.use_bzip2)) { // no Base64 header -> bZip2?
      if (bzip2_header_check(&precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb])) {
        precomp_mgr.ctx->saved_input_file_pos = precomp_mgr.ctx->input_file_pos;
        precomp_mgr.ctx->saved_cb = precomp_mgr.ctx->cb;

        auto result = try_decompression_bzip2(precomp_mgr);
        compressed_data_found = result.success;
        if (result.success) {
          end_uncompressed_data(precomp_mgr);

          result.dump_to_outfile(precomp_mgr);

          // set input file pointer after recompressed data
          precomp_mgr.ctx->input_file_pos += precomp_mgr.ctx->best_identical_bytes - 1;
          precomp_mgr.ctx->cb += precomp_mgr.ctx->best_identical_bytes - 1;
        }
        else {
          precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
          precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
        }
      }
    }


   // nothing so far -> if intense mode is active, look for raw zLib header
   if (intense_mode_is_active(precomp_mgr)) {
    if (!compressed_data_found) {
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
          compressed_data_found = result.success;
          if (result.success) {
            compressed_data_found = true;

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
    if (!compressed_data_found) {
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
          compressed_data_found = result.success;
          if (result.success) {
            compressed_data_found = true;

            end_uncompressed_data(precomp_mgr);

            result.dump_to_outfile(precomp_mgr);

            // set input file pointer after recompressed data
            precomp_mgr.ctx->input_file_pos += result.input_pos_add_offset();
            precomp_mgr.ctx->cb += result.input_pos_add_offset();
          }
        }

        if (!compressed_data_found) {
          precomp_mgr.ctx->input_file_pos = precomp_mgr.ctx->saved_input_file_pos;
          precomp_mgr.ctx->cb = precomp_mgr.ctx->saved_cb;
        }
      }
    }
   }

  }

    if (!compressed_data_found) {
      if (precomp_mgr.ctx->uncompressed_length == -1) {
          precomp_mgr.ctx->uncompressed_length = 0;
          precomp_mgr.ctx->uncompressed_pos = precomp_mgr.ctx->input_file_pos;

          // uncompressed data
          precomp_mgr.ctx->fout->put(0);

          precomp_mgr.ctx->uncompressed_data_in_work = true;
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
      recompress_base64(precomp_mgr, header1);
      break;
    }
    case D_BZIP2: { // bZip2 recompression
      recompress_bzip2(precomp_mgr, header1);
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
void fin_fget_recon_data(IStreamLike& input, recompress_deflate_result& rdres) {
  if (!rdres.zlib_perfect) {
    size_t sz = fin_fget_vlint(input);
    rdres.recon_data.resize(sz);
    input.read(reinterpret_cast<char*>(rdres.recon_data.data()), rdres.recon_data.size());
  }

  rdres.compressed_stream_size = fin_fget_vlint(input);
  rdres.uncompressed_stream_size = fin_fget_vlint(input);
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

// C API STUFF
CSwitches* CreateCSwitches() {
  // We create our C++ class but return a pointer to the inherited C struct, that way C users can interact with the data but we still are able
  // to use our C++ amenities like default values via constructor and member functions
  return new Switches();
}

CPrecomp* PrecompCreate() { return new Precomp(); }
void PrecompSetProgressCallback(CPrecomp* precomp_mgr, void(*callback)(float)) {
  reinterpret_cast<Precomp*>(precomp_mgr)->set_progress_callback(callback);
}
CSwitches* PrecompGetSwitches(CPrecomp* precomp_mgr) { return &reinterpret_cast<Precomp*>(precomp_mgr)->switches; }
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
