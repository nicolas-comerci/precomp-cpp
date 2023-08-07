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

#define NOMINMAX  // This is to prevent min/max implementations from windows.h, so we can use std::min and std::max and so on, without problems

#include <cstdio>
#include <iostream>
#include <string>
#include <array>
#include <random>
#include <fcntl.h>
#include <filesystem>
#include <set>

#ifndef __unix
#include <windows.h>
#include <io.h>
#else
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <cstring>
#include <cstdarg>

#endif

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
#ifndef __unix
void set_std_handle_binary_mode(StdHandles handle) { setmode(handle, O_BINARY); }
#else
void set_std_handle_binary_mode(StdHandles handle) {}
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

std::map<SupportedFormats, std::function<PrecompFormatHandler*()>> registeredHandlerFactoryFunctions = std::map<SupportedFormats, std::function<PrecompFormatHandler*()>>{};
REGISTER_PRECOMP_FORMAT_HANDLER(D_ZIP, ZipFormatHandler::create);
REGISTER_PRECOMP_FORMAT_HANDLER(D_GZIP, GZipFormatHandler::create);
REGISTER_PRECOMP_FORMAT_HANDLER(D_PDF, PdfFormatHandler::create);
// This also adds support for D_MULTIPNG, handlers that define more than one SupportedFormats are somewhat wonky for now
REGISTER_PRECOMP_FORMAT_HANDLER(D_PNG, PngFormatHandler::create);
REGISTER_PRECOMP_FORMAT_HANDLER(D_GIF, GifFormatHandler::create);
REGISTER_PRECOMP_FORMAT_HANDLER(D_JPG, JpegFormatHandler::create);
REGISTER_PRECOMP_FORMAT_HANDLER(D_MP3, Mp3FormatHandler::create);
REGISTER_PRECOMP_FORMAT_HANDLER(D_SWF, SwfFormatHandler::create);
REGISTER_PRECOMP_FORMAT_HANDLER(D_BASE64, Base64FormatHandler::create);
REGISTER_PRECOMP_FORMAT_HANDLER(D_BZIP2, BZip2FormatHandler::create);
REGISTER_PRECOMP_FORMAT_HANDLER(D_RAW, ZlibFormatHandler::create);
REGISTER_PRECOMP_FORMAT_HANDLER(D_BRUTE, DeflateFormatHandler::create);

void precompression_result::dump_header_to_outfile(OStreamLike& outfile) const {
  // write compressed data header
  outfile.put(static_cast<char>(flags | (recursion_used ? std::byte{ 0b10000000 } : std::byte{ 0b0 })));
  outfile.put(format);
}

void precompression_result::dump_penaltybytes_to_outfile(OStreamLike& outfile) const {
  if (penalty_bytes.empty()) return;
  print_to_log(PRECOMP_DEBUG_LOG, "Penalty bytes were used: %i bytes\n", penalty_bytes.size());
  fout_fput_vlint(outfile, penalty_bytes.size());

  for (auto& chr: penalty_bytes) {
      outfile.put(chr);
  }
}

void precompression_result::dump_stream_sizes_to_outfile(OStreamLike& outfile) const {
  fout_fput_vlint(outfile, original_size);
  fout_fput_vlint(outfile, precompressed_size);
}

void precompression_result::dump_precompressed_data_to_outfile(OStreamLike& outfile) const {
  if (recursion_used) fout_fput_vlint(outfile, recursion_filesize);
  auto out_size = recursion_used ? recursion_filesize : precompressed_size;
  fast_copy(*precompressed_stream, outfile, out_size);
}

void precompression_result::dump_to_outfile(OStreamLike& outfile) const {
  dump_header_to_outfile(outfile);
  dump_penaltybytes_to_outfile(outfile);
  dump_stream_sizes_to_outfile(outfile);
  dump_precompressed_data_to_outfile(outfile);
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

  header_already_read = false;

  max_recursion_depth_used = 0;
  max_recursion_depth_reached = false;
}

void PrecompSetInputStream(Precomp* precomp_mgr, PrecompIStream istream, const char* input_file_name) {
  precomp_mgr->input_file_name = input_file_name;
  precomp_mgr->set_input_stream(static_cast<std::istream*>(istream));
}
void PrecompSetInputFile(Precomp* precomp_mgr, FILE* fhandle, const char* input_file_name) {
  precomp_mgr->input_file_name = input_file_name;
  precomp_mgr->set_input_stream(fhandle);
}

void PrecompSetOutStream(Precomp* precomp_mgr, PrecompOStream ostream, const char* output_file_name) {
  precomp_mgr->output_file_name = output_file_name;
  precomp_mgr->set_output_stream(static_cast<std::ostream*>(ostream));
}
void PrecompSetOutputFile(Precomp* precomp_mgr, FILE* fhandle, const char* output_file_name) {
  precomp_mgr->output_file_name = output_file_name;
  precomp_mgr->set_output_stream(fhandle);
}

void PrecompSetGenericInputStream(
  Precomp* precomp_mgr, const char* input_file_name, void* backing_structure,
  size_t(*read_func)(void*, char*, long long),
  int (*get_func)(void*),
  int (*seekg_func)(void*, long long, int),
  long long (*tellg_func)(void*),
  bool (*eof_func)(void*),
  bool (*bad_func)(void*),
  void (*clear_func)(void*)
) {
  precomp_mgr->input_file_name = input_file_name;
  precomp_mgr->get_original_context()->fin = std::make_unique<GenericIStreamLike>(
    backing_structure, read_func, get_func, seekg_func, tellg_func, eof_func, bad_func, clear_func);
}

#ifdef DEBUG
void PrecompSetDebugCompareInputFile(Precomp* precomp_mgr, FILE* fhandle) {
  auto known_good = std::make_unique<FILEIStream>(fhandle, true);
  const auto& orig_context = precomp_mgr->get_original_context();
  orig_context->fin = std::make_unique<DebugComparatorIStreamLike>(std::move(known_good), std::move(orig_context->fin));
}
#endif

void PrecompSetGenericOutputStream(
  Precomp* precomp_mgr, const char* output_file_name, void* backing_structure,
  size_t(*write_func)(void*, char const*, long long),
  int (*put_func)(void*, int),
  int (*seekp_func)(void*, long long, int),
  long long (*tellp_func)(void*),
  bool (*eof_func)(void*),
  bool (*bad_func)(void*),
  void (*clear_func)(void*)
) {
  precomp_mgr->output_file_name = output_file_name;
  auto gen_ostream = std::make_unique<GenericOStreamLike>(backing_structure, write_func, put_func, tellp_func, seekp_func, eof_func, bad_func, clear_func);
  precomp_mgr->get_original_context()->fout = std::unique_ptr<ObservableOStream>(new ObservableOStreamWrapper(gen_ostream.release(), true));
  
}

const char* PrecompGetOutputFilename(Precomp* precomp_mgr) {
  return precomp_mgr->output_file_name.c_str();
}

//Switches constructor
Switches::Switches(): CSwitches() {
  verify_precompressed = false;
  intense_mode = false;
  intense_mode_depth_limit = -1;
  brute_mode = false;
  brute_mode_depth_limit = -1;
  pdf_bmp_mode = false;
  prog_only = false;
  use_mjpeg = true;
  use_brunsli = true;
  use_packjpg_fallback = true;
  min_ident_size = 4;

  working_dir = nullptr;

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

  // preflate config
  preflate_meta_block_size = 1 << 21; // 2 MB blocks by default
  preflate_verify = false;

  max_recursion_depth = 10;
}

Switches::~Switches() {
  if (working_dir != nullptr) {
    free(working_dir);
    working_dir = nullptr;
  }
}

RecursionContext::RecursionContext(float min_percent, float max_percent, Precomp& precomp_):
    CRecursionContext(), precomp(precomp_), global_min_percent(min_percent), global_max_percent(max_percent) {}


void RecursionContext::set_input_stream(std::istream* istream, bool take_ownership) {
  this->fin = std::make_unique<WrappedIStream>(istream, take_ownership);
}

void RecursionContext::set_input_stream(FILE* fhandle, bool take_ownership) {
  this->fin = std::make_unique<FILEIStream>(fhandle, take_ownership);
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

Precomp::Precomp() {
  recursion_depth = 0;
}

void Precomp::set_input_stdin() {
  // Read binary from stdin
  set_std_handle_binary_mode(StdHandles::STDIN_HANDLE);
  auto new_fin = std::make_unique<WrappedIStream>(new std::ifstream(), true);
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
  set_std_handle_binary_mode(StdHandles::STDOUT_HANDLE);
  auto new_fout = std::make_unique<ObservableWrappedOStream>(new std::ofstream(), true);
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

void Precomp::set_progress_callback(std::function<void(float)> callback) {
  progress_callback = callback;
}
void Precomp::call_progress_callback() {
  if (!this->progress_callback || !this->ctx) return;
  auto context_progress_range = this->ctx->global_max_percent - this->ctx->global_min_percent;
  auto inner_context_progress_percent = static_cast<float>(this->ctx->input_file_pos) / this->ctx->fin_length;
  this->progress_callback(this->ctx->global_min_percent + (context_progress_range * inner_context_progress_percent));
}

std::string Precomp::get_tempfile_name(const std::string& name, bool prepend_random_tag) const {
  std::filesystem::path dir = switches.working_dir != nullptr ? switches.working_dir : std::filesystem::path();
  std::filesystem::path filename = prepend_random_tag ? temp_files_tag() + "_" + name : name;
  return (dir / filename).string();
}

void Precomp::init_format_handlers(bool is_recompressing) {
    if (is_recompressing || switches.use_zip) {
        format_handlers.push_back(std::unique_ptr<PrecompFormatHandler>(registeredHandlerFactoryFunctions[D_ZIP]()));
    }
    if (is_recompressing || switches.use_gzip) {
        format_handlers.push_back(std::unique_ptr<PrecompFormatHandler>(registeredHandlerFactoryFunctions[D_GZIP]()));
    }
    if (is_recompressing || switches.use_pdf) {
        format_handlers.push_back(std::unique_ptr<PrecompFormatHandler>(registeredHandlerFactoryFunctions[D_PDF]()));
    }
    if (is_recompressing || switches.use_png) {
        format_handlers.push_back(std::unique_ptr<PrecompFormatHandler>(registeredHandlerFactoryFunctions[D_PNG]()));
    }
    if (is_recompressing || switches.use_gif) {
        format_handlers.push_back(std::unique_ptr<PrecompFormatHandler>(registeredHandlerFactoryFunctions[D_GIF]()));
    }
    if (is_recompressing || switches.use_jpg) {
        format_handlers.push_back(std::unique_ptr<PrecompFormatHandler>(registeredHandlerFactoryFunctions[D_JPG]()));
    }
    if (is_recompressing || switches.use_mp3) {
        format_handlers.push_back(std::unique_ptr<PrecompFormatHandler>(registeredHandlerFactoryFunctions[D_MP3]()));
    }
    if (is_recompressing || switches.use_swf) {
        format_handlers.push_back(std::unique_ptr<PrecompFormatHandler>(registeredHandlerFactoryFunctions[D_SWF]()));
    }
    if (is_recompressing || switches.use_base64) {
        format_handlers.push_back(std::unique_ptr<PrecompFormatHandler>(registeredHandlerFactoryFunctions[D_BASE64]()));
    }
    if (is_recompressing || switches.use_bzip2) {
        format_handlers.push_back(std::unique_ptr<PrecompFormatHandler>(registeredHandlerFactoryFunctions[D_BZIP2]()));
    }
    if (is_recompressing || switches.intense_mode) {
        format_handlers.push_back(std::unique_ptr<PrecompFormatHandler>(registeredHandlerFactoryFunctions[D_RAW]()));
        if (switches.intense_mode_depth_limit >= 0) {
            format_handlers.back()->depth_limit = switches.intense_mode_depth_limit;
        }
    }
    // Brute mode detects a bit less than intense mode to avoid false positives and slowdowns, so both can be active.
    if (is_recompressing || switches.brute_mode) {
        format_handlers.push_back(std::unique_ptr<PrecompFormatHandler>(registeredHandlerFactoryFunctions[D_BRUTE]()));
        if (switches.brute_mode_depth_limit >= 0) {
            format_handlers.back()->depth_limit = switches.brute_mode_depth_limit;
        }
    }
}

const std::vector<std::unique_ptr<PrecompFormatHandler>>& Precomp::get_format_handlers() const {
    return format_handlers;
}

bool Precomp::is_format_handler_active(SupportedFormats format_id) const {
    const auto itemIt = std::find_if(
        format_handlers.cbegin(), format_handlers.cend(),
        [&format_id](const std::unique_ptr<PrecompFormatHandler>& handler) { return handler->get_header_bytes()[0] == format_id; }
    );
    if (itemIt == format_handlers.cend()) return false;
    const auto& formatHandler = *itemIt;

    return (formatHandler->depth_limit && recursion_depth > formatHandler->depth_limit) ? false : true;
}

// get copyright message
// msg = Buffer for error messages (256 bytes buffer size are enough)
LIBPRECOMP void PrecompGetCopyrightMsg(char* msg) {
  if (V_MINOR2 == 0) {
    sprintf(msg, "Precomp Neo Library v%i.%i (c) 2006-2021 by Christian Schneider/2022-2023 by Nicolas Comerci",V_MAJOR,V_MINOR);
  } else {
    sprintf(msg, "Precomp Neo Library v%i.%i.%i (c) 2006-2021 by Christian Schneider/2022-2023 by Nicolas Comerci",V_MAJOR,V_MINOR,V_MINOR2);
  }
}

void end_uncompressed_data(Precomp& precomp_mgr) {
  if (!precomp_mgr.ctx->uncompressed_length.has_value()) return;

  fout_fput_vlint(*precomp_mgr.ctx->fout, *precomp_mgr.ctx->uncompressed_length);

  // fast copy of uncompressed data
  precomp_mgr.ctx->fin->seekg(precomp_mgr.ctx->uncompressed_pos, std::ios_base::beg);
  fast_copy(*precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, *precomp_mgr.ctx->uncompressed_length);

  precomp_mgr.ctx->uncompressed_length = std::nullopt;
}

void write_header(Precomp& precomp_mgr) {
  // write the PCF file header, beware that this needs to be done before wrapping the output file with a CompressedOStreamBuffer
  char* input_file_name_without_path = new char[precomp_mgr.input_file_name.length() + 1];

  ostream_printf(*precomp_mgr.ctx->fout, "PCF");

  // version number
  precomp_mgr.ctx->fout->put(V_MAJOR);
  precomp_mgr.ctx->fout->put(V_MINOR);
  precomp_mgr.ctx->fout->put(V_MINOR2);

  // compression-on-the-fly method used, 0, as OTF compression no longer supported
  precomp_mgr.ctx->fout->put(0);

  // write input file name without path
  const char* last_backslash = strrchr(precomp_mgr.input_file_name.c_str(), PATH_DELIM);
  if (last_backslash != nullptr) {
    strcpy(input_file_name_without_path, last_backslash + 1);
  }
  else {
    strcpy(input_file_name_without_path, precomp_mgr.input_file_name.c_str());
  }

  ostream_printf(*precomp_mgr.ctx->fout, input_file_name_without_path);
  precomp_mgr.ctx->fout->put(0);

  delete[] input_file_name_without_path;
}

bool verify_precompressed_result(Precomp& precomp_mgr, const std::unique_ptr<precompression_result>& result, long long& input_file_pos);

int compress_file_impl(Precomp& precomp_mgr) {
  precomp_mgr.ctx->comp_decomp_state = P_PRECOMPRESS;
  if (precomp_mgr.recursion_depth == 0) {
      write_header(precomp_mgr);
      precomp_mgr.init_format_handlers();
  }

  const auto& format_handlers = precomp_mgr.get_format_handlers();
  precomp_mgr.ctx->uncompressed_bytes_total = 0;

  precomp_mgr.ctx->fin->seekg(0, std::ios_base::beg);
  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.ctx->in_buf), IN_BUF_SIZE);
  long long in_buf_pos = 0;
  // This buffer will be fed to the format handlers so they can confirm if the current position is the beggining of a stream they support
  std::span<unsigned char> checkbuf;

  precomp_mgr.ctx->anything_was_used = false;
  precomp_mgr.ctx->non_zlib_was_used = false;

  for (long long input_file_pos = 0; input_file_pos < precomp_mgr.ctx->fin_length; input_file_pos++) {
    precomp_mgr.ctx->input_file_pos = input_file_pos;
    bool compressed_data_found = false;

    bool ignore_this_pos = false;

    if ((in_buf_pos + IN_BUF_SIZE) <= (input_file_pos + CHECKBUF_SIZE)) {
      precomp_mgr.ctx->fin->seekg(input_file_pos, std::ios_base::beg);
      precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.ctx->in_buf), IN_BUF_SIZE);
      in_buf_pos = input_file_pos;
    }
    auto cb_pos = input_file_pos - in_buf_pos;
    checkbuf = std::span(&precomp_mgr.ctx->in_buf[cb_pos], IN_BUF_SIZE - cb_pos);

    ignore_this_pos = precomp_mgr.switches.ignore_set.find(input_file_pos) != precomp_mgr.switches.ignore_set.end();

    if (!ignore_this_pos) {
      for (const auto& formatHandler : format_handlers) {
        // Recursion depth check
        if (formatHandler->depth_limit && precomp_mgr.recursion_depth > formatHandler->depth_limit) continue;

        // Position blacklist check
        bool ignore_this_position = false;
        const SupportedFormats& formatTag = formatHandler->get_header_bytes()[0];
        auto ignoreListIt = precomp_mgr.ctx->ignore_offsets.find(formatTag);
        if (ignoreListIt != precomp_mgr.ctx->ignore_offsets.cend()) {
          auto& ignore_offsets_set = (*ignoreListIt).second;
          if (!ignore_offsets_set.empty()) {
            auto first = ignore_offsets_set.begin();
            while (*first < input_file_pos) {
              ignore_offsets_set.erase(first);
              if (ignore_offsets_set.empty()) break;
              first = ignore_offsets_set.begin();
            }

            if (!ignore_offsets_set.empty()) {
              if (*first == input_file_pos) {
                ignore_this_position = true;
                ignore_offsets_set.erase(first);
              }
            }
          }
          if (ignore_this_position) continue;
        }

        bool quick_check_result = formatHandler->quick_check(checkbuf, reinterpret_cast<uintptr_t>(precomp_mgr.ctx->fin.get()), input_file_pos);
        if (!quick_check_result) continue;

        auto result = formatHandler->attempt_precompression(precomp_mgr, checkbuf, input_file_pos);
        if (!result || !result->success) continue;

        // If verification is enabled, we attempt to recompress the stream right now, and reject it if anything fails or data doesn't match
        // Note that this is done before recursion for 2 reasons:
        //  1) why bother recursing a stream we might reject
        //  2) verification would be much more complicated as we would need to prevent recursing on recompression which would lead to verifying some streams MANY times
        if (precomp_mgr.switches.verify_precompressed) {
            if (verify_precompressed_result(precomp_mgr, result, input_file_pos)) {
                // ensure that the precompressed stream is ready to read from the start, as if verification never happened
                result->precompressed_stream->seekg(0, std::ios_base::beg);
            }
            else {
                continue;
            }
        }

        // If the format allows for it, recurse inside the most likely newly decompressed data
        if (formatHandler->recursion_allowed) {
            auto recurse_tempfile_name = precomp_mgr.get_tempfile_name("recurse");
            recursion_result r = recursion_compress(precomp_mgr, result->original_size, result->precompressed_size, *result->precompressed_stream, recurse_tempfile_name);
            if (r.success) {
                auto rec_tmpfile = new PrecompTmpFile();
                rec_tmpfile->open(r.file_name, std::ios_base::in | std::ios_base::binary);
                result->precompressed_stream = std::unique_ptr<IStreamLike>(rec_tmpfile);
                result->recursion_filesize = r.file_length;
                result->recursion_used = true;
            }
            else {
                // ensure that the precompressed stream is ready to read from the start, as if recursion attempt never happened
                result->precompressed_stream->seekg(0, std::ios_base::beg);
            }
        }

        end_uncompressed_data(precomp_mgr);
        result->dump_to_outfile(*precomp_mgr.ctx->fout);

        // start new uncompressed data

        // set input file pointer after recompressed data
        input_file_pos += result->input_pos_add_offset();
        compressed_data_found = result->success;
        break;
      }
    }

    if (!compressed_data_found) {
      if (!precomp_mgr.ctx->uncompressed_length.has_value()) {
        precomp_mgr.ctx->uncompressed_length = 0;
        precomp_mgr.ctx->uncompressed_pos = input_file_pos;

        // uncompressed data
        precomp_mgr.ctx->fout->put(0);
      }
      (*precomp_mgr.ctx->uncompressed_length)++;
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

int compress_file(Precomp& precomp_mgr)
{
  return wrap_with_exception_catch([&]() { return compress_file_impl(precomp_mgr); });
}

int decompress_file_impl(RecursionContext& precomp_ctx) {
  precomp_ctx.comp_decomp_state = P_RECOMPRESS;
  const auto& format_handlers = precomp_ctx.precomp.get_format_handlers();

  long long fin_pos = precomp_ctx.fin->tellg();

  while (precomp_ctx.fin->good()) {
    std::byte header1 = static_cast<std::byte>(precomp_ctx.fin->get());
    if (!precomp_ctx.fin->good()) break;

    if (header1 == std::byte{ 0 }) { // uncompressed data
      long long uncompressed_data_length;
      uncompressed_data_length = fin_fget_vlint(*precomp_ctx.fin);
  
      if (uncompressed_data_length == 0) break; // end of PCF file, used by bZip2 compress-on-the-fly
  
      print_to_log(PRECOMP_DEBUG_LOG, "Uncompressed data, length=%lli\n");
      fast_copy(*precomp_ctx.fin, *precomp_ctx.fout, uncompressed_data_length);
  
    }
    else { // decompressed data, recompress
      unsigned char headertype = precomp_ctx.fin->get();
  
      bool handlerFound = false;
      for (const auto& formatHandler : format_handlers) {
        for (auto formatHandlerHeaderByte: formatHandler->get_header_bytes()) {
          if (headertype == formatHandlerHeaderByte) {
            formatHandler->recompress(precomp_ctx, header1, formatHandlerHeaderByte);
            handlerFound = true;
            break;
          }
        }
        if (handlerFound) break;
      }
    }
  
    fin_pos = precomp_ctx.fin->tellg();
  }

  return RETURN_SUCCESS;
}

int decompress_file(RecursionContext& precomp_ctx)
{
  return wrap_with_exception_catch([&]() { return decompress_file_impl(precomp_ctx); });
}

void read_header(Precomp& precomp_mgr) {
  if (precomp_mgr.statistics.header_already_read) throw std::runtime_error("Attempted to read the input stream header twice");
  unsigned char hdr[3];
  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(hdr), 3);
  if ((hdr[0] == 'P') && (hdr[1] == 'C') && (hdr[2] == 'F')) {
  } else {
    throw PrecompError(ERR_NO_PCF_HEADER);
  }

  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(hdr), 3);
  if ((hdr[0] == V_MAJOR) && (hdr[1] == V_MINOR) && (hdr[2] == V_MINOR2)) {
  } else {
    throw PrecompError(
      ERR_PCF_HEADER_INCOMPATIBLE_VERSION,
      make_cstyle_format_string("PCF version info: %i.%i.%i\n", hdr[0], hdr[1], hdr[2])
    );
  }

  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(hdr), 1);
  if (hdr[0] != 0) throw PrecompError(
    ERR_PCF_HEADER_INCOMPATIBLE_VERSION,
    "OTF compression no longer supported, use original Precomp and use the -nn conversion option to get an uncompressed Precomp stream that should work here"
  );

  std::string header_filename;
  char c;
  do {
    c = precomp_mgr.ctx->fin->get();
    if (c != 0) header_filename += c;
  } while (c != 0);

  if (precomp_mgr.output_file_name.empty()) {
    precomp_mgr.output_file_name = header_filename;
  }
  precomp_mgr.statistics.header_already_read = true;
}

// JPG routines
void packjpg_mp3_dll_msg() {
  print_to_log(PRECOMP_NORMAL_LOG, "Using packJPG for JPG recompression, packMP3 for MP3 recompression.\n");
  print_to_log(PRECOMP_NORMAL_LOG, "%s\n", packjpg_version_info());
  print_to_log(PRECOMP_NORMAL_LOG, "%s\n", packmp3_version_info());
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

RecursionContext& recursion_push(RecursionContext& precomp_ctx, long long recurse_stream_length) {
  auto context_progress_range = precomp_ctx.global_max_percent - precomp_ctx.global_min_percent;
  auto current_context_progress_percent = static_cast<float>(precomp_ctx.input_file_pos) / precomp_ctx.fin_length;
  auto recursion_end_progress_percent = static_cast<float>(precomp_ctx.input_file_pos + recurse_stream_length) / precomp_ctx.fin_length;

  auto new_minimum = precomp_ctx.global_min_percent + (context_progress_range * current_context_progress_percent);
  auto new_maximum = precomp_ctx.global_min_percent + (context_progress_range * recursion_end_progress_percent);

  Precomp& precomp_mgr = precomp_ctx.precomp;
  precomp_mgr.recursion_contexts_stack.push_back(std::move(precomp_mgr.ctx));
  precomp_mgr.ctx = std::make_unique<RecursionContext>(new_minimum, new_maximum, precomp_mgr);
  return *precomp_mgr.ctx;
}

void recursion_pop(Precomp& precomp_mgr) {
  precomp_mgr.ctx = std::move(precomp_mgr.recursion_contexts_stack.back());
  precomp_mgr.recursion_contexts_stack.pop_back();
}

bool verify_precompressed_result(Precomp& precomp_mgr, const std::unique_ptr<precompression_result>& result, long long& input_file_pos) {
    // Stupid way to have a new RecursionContext for verification, will probably make progress percentages freak out even more than they already do
    recursion_push(*precomp_mgr.ctx, 0);
    auto new_ctx = std::move(precomp_mgr.ctx);
    recursion_pop(precomp_mgr);

    // Dump precompressed data including format headers
    // TODO: We should be able to use a PassthroughStream here to avoid having to dump a temporary file, I tried it and it failed, didn't want to spend more
    // time on it right now, but might be worth it to do this optimization later.
    std::unique_ptr<PrecompTmpFile> verify_tmp_precompressed = std::make_unique<PrecompTmpFile>();
    auto verify_precompressed_filename = precomp_mgr.get_tempfile_name("verify_precompressed");
    verify_tmp_precompressed->open(verify_precompressed_filename, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
    result->dump_to_outfile(*verify_tmp_precompressed);

    // Set it as the input on the context, we will do what ammounts essentially to run Precomp -r on it as it's on its own pretty much
    // a PCf file without the PCF header, if that makes sense
    verify_tmp_precompressed->close();
    auto precompressed_size = std::filesystem::file_size(verify_precompressed_filename.c_str());
    new_ctx->fin_length = precompressed_size;
    verify_tmp_precompressed->reopen();
    new_ctx->fin = std::make_unique<IStreamLikeView>(verify_tmp_precompressed.get(), precompressed_size);

    // Set output to a stream that calculates it's SHA1 sum without actually writting anything anywhere
    auto sha1_ostream = Sha1Ostream();
    new_ctx->fout = std::make_unique<ObservableOStreamWrapper>(&sha1_ostream, false);

    int verify_result = decompress_file(*new_ctx);
    if (verify_result != RETURN_SUCCESS) return false;

    // Okay at this point we supposedly recompressed the input data successfully, verify data exactly matches original
    auto recompressed_size = sha1_ostream.tellp();
    auto recompressed_data_sha1 = sha1_ostream.get_digest();

    precomp_mgr.ctx->fin->seekg(input_file_pos, std::ios_base::beg);
    auto original_data_view = IStreamLikeView(precomp_mgr.ctx->fin.get(), recompressed_size + input_file_pos);
    auto original_data_sha1 = calculate_sha1(original_data_view, 0);

    if (original_data_sha1 != recompressed_data_sha1) return false;
    return true;
}

recursion_result recursion_compress(Precomp& precomp_mgr, long long compressed_bytes, long long decompressed_bytes, IStreamLike& tmpfile, std::string out_filename) {
  recursion_result tmp_r;
  tmp_r.success = false;

  bool rescue_anything_was_used = false;
  bool rescue_non_zlib_was_used = false;

  if ((precomp_mgr.recursion_depth + 1) > precomp_mgr.switches.max_recursion_depth) {
    precomp_mgr.statistics.max_recursion_depth_reached = true;
    return tmp_r;
  }

  recursion_push(*precomp_mgr.ctx, compressed_bytes);

  precomp_mgr.ctx->fin_length = decompressed_bytes;
  precomp_mgr.ctx->fin = std::make_unique<IStreamLikeView>(&tmpfile, decompressed_bytes);
  //precomp_mgr.ctx->set_input_stream(fin);

  tmp_r.file_name = out_filename;
  auto fout = new std::ofstream();
  fout->open(tmp_r.file_name.c_str(), std::ios_base::out | std::ios_base::binary);
  precomp_mgr.ctx->set_output_stream(fout, true);

  precomp_mgr.recursion_depth++;
  print_to_log(PRECOMP_DEBUG_LOG, "Recursion start - new recursion depth %i\n", precomp_mgr.recursion_depth);
  const auto ret_code = compress_file(precomp_mgr);
  if (ret_code != RETURN_SUCCESS && ret_code != RETURN_NOTHING_DECOMPRESSED) throw PrecompError(ret_code);
  tmp_r.success = ret_code == RETURN_SUCCESS;

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
    if ((precomp_mgr.recursion_depth + 1) > precomp_mgr.statistics.max_recursion_depth_used)
      precomp_mgr.statistics.max_recursion_depth_used = (precomp_mgr.recursion_depth + 1);
    // get recursion file size
    tmp_r.file_length = std::filesystem::file_size(tmp_r.file_name.c_str());
  }

  return tmp_r;
}


void RecursionPasstroughStream::unlock_everything() {
  // Any locked thread that wakes up after EOF should not lock again on read/write, and should hopefully check eof() and realize it can't continue and fail gracefully
  write_eof = true;
  read_eof = true;
  data_needed_cv.notify_all();
  data_available_cv.notify_all();
}


RecursionPasstroughStream::RecursionPasstroughStream(std::unique_ptr<RecursionContext>&& ctx_) : ctx(std::move(ctx_)), owner_thread_id(std::this_thread::get_id()) {
  buffer.reserve(CHUNK);

  ctx->fout = std::make_unique<ObservableOStreamWrapper>(this, false);

  thread = std::thread([this] {
    thread_return_code = decompress_file(*ctx);
    unlock_everything();
  });
}

RecursionPasstroughStream::~RecursionPasstroughStream() {
  unlock_everything();
  if (thread.joinable()) thread.join();
}

int RecursionPasstroughStream::get_recursion_return_code(bool throw_on_failure) {
  unlock_everything();
  if (thread.joinable()) thread.join();
  auto ret_code = thread_return_code.value();
  if (throw_on_failure && ret_code != RETURN_SUCCESS) throw PrecompError(ret_code);
  return ret_code;
}

RecursionPasstroughStream& RecursionPasstroughStream::read(char* buff, std::streamsize count) {
  if (read_eof) {
    if (std::this_thread::get_id() != owner_thread_id) throw PrecompError(thread_return_code.value_or(ERR_DURING_RECOMPRESSION));
    _gcount = 0;
    return *this;
  }
  //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: TAKING LOCK FOR READ!\n\n", static_cast<void*>(this));
  std::unique_lock lock(mtx);
  //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: LOCK TAKEN FOR READ!\n\n", static_cast<void*>(this));
  std::streamsize already_read_count = 0;

  while (already_read_count < count) {
    std::streamsize remaining_count = count - already_read_count;
    std::streamsize iteration_read_count = data_available() >= remaining_count ? remaining_count : data_available();
    if (iteration_read_count == 0) {
      // If we don't have any data in the buffer we need to wait for more
      // unless we already reached the write_eof, in which case we know no more data will ever arrive
      // or if read_eof is true, which given that we were already in the middle of reading it probably means the stream was forcefully truncated (most likely to destroy it)
      if (read_eof || write_eof) { break; }

      data_needed_cv.notify_one();
      //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: WAITING FOR DATA AVAILABLE FOR READ!\n\n", static_cast<void*>(this));
      data_available_cv.wait(lock);
      //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: WOKE UP FROM WAITING FOR DATA AVAILABLE FOR READ!\n\n", static_cast<void*>(this));

      // now that we are back from getting more data, recalculate the iteration_read_count
      iteration_read_count = data_available() >= remaining_count ? remaining_count : data_available();
      if (iteration_read_count == 0) { continue; }
    }

    memcpy(buff + already_read_count, buffer_current_pos(), iteration_read_count);
    already_read_count += iteration_read_count;
    buffer_already_read_count += iteration_read_count;
  }

  // if we didn't read enough to satisfy the count (or we did but write_eof has been reached and there is no more data available) we set read_eof
  if (already_read_count < count || (write_eof && !data_available())) {
    read_eof = true;
  }
  _gcount = already_read_count;
  //data_needed_cv.notify_one();
  //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: EXITING READ, RELEASING LOCK!\n\n", static_cast<void*>(this));
  return *this;
}

std::istream::int_type RecursionPasstroughStream::get() {
  unsigned char chr[1];
  read(reinterpret_cast<char*>(&chr[0]), 1);
  return _gcount == 1 ? chr[0] : EOF;
}

std::streamsize RecursionPasstroughStream::gcount() { return _gcount; }
RecursionPasstroughStream& RecursionPasstroughStream::seekg(std::istream::off_type offset, std::ios_base::seekdir dir) {
  throw std::runtime_error("CANT SEEK ON A RecursionPassthroughStream!");
}
std::istream::pos_type RecursionPasstroughStream::tellg() { return accumulated_already_read_count + buffer_already_read_count; }
bool RecursionPasstroughStream::eof() { return write_eof && read_eof; }
bool RecursionPasstroughStream::good() { return !bad(); }
bool RecursionPasstroughStream::bad() { return thread_return_code.has_value() && thread_return_code.value() != RETURN_SUCCESS; }
void RecursionPasstroughStream::clear() { throw std::runtime_error("CANT CLEAR ON A RecursionPassthroughStream!"); }

RecursionPasstroughStream& RecursionPasstroughStream::write(const char* buf, std::streamsize count) {
  if (write_eof) {
    if (std::this_thread::get_id() != owner_thread_id) throw PrecompError(thread_return_code.value_or(ERR_DURING_RECOMPRESSION));
    return *this;
  }
  //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: TAKING LOCK FOR WRITE!\n\n", static_cast<void*>(this));
  std::unique_lock lock(mtx);
  //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: LOCK TAKEN FOR WRITE!\n\n", static_cast<void*>(this));
  std::streamsize data_already_written = 0;

  while (data_already_written < count) {
    std::streamsize remaining_data_to_write = count - data_already_written;
    //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: WAITING FOR MORE DATA NEEDED FOR WRITE!\n\n", static_cast<void*>(this));
    if (data_available()) {
      data_available_cv.notify_one();
      data_needed_cv.wait(lock);
      // Check if data is actually needed or we were woken up to be terminated
      if (write_eof || read_eof) {
        if (std::this_thread::get_id() != owner_thread_id) throw PrecompError(thread_return_code.value_or(ERR_DURING_RECOMPRESSION));
        write_eof = true;
        return *this;
      }
    }
    //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: WOKE UP FROM WAITING FOR MORE DATA NEEDED FOR WRITE!\n\n", static_cast<void*>(this));

    // at this point, all data available was consumed and we are ready to replenish the buffer with new data
    accumulated_already_read_count += buffer_already_read_count;
    buffer_already_read_count = 0;

    std::streamsize iteration_data_to_write = remaining_data_to_write >= CHUNK ? CHUNK : remaining_data_to_write;
    if (buffer.size() != iteration_data_to_write) buffer.resize(iteration_data_to_write);
    memcpy(buffer.data(), buf + data_already_written, iteration_data_to_write);
    data_already_written += iteration_data_to_write;
  }

  //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: EXITING WRITE, RELEASING LOCK!\n\n", static_cast<void*>(this));
  if (data_available()) { data_available_cv.notify_one(); }
  return *this;
}

RecursionPasstroughStream& RecursionPasstroughStream::put(char chr) {
  write(&chr, 1);
  return *this;
}

void RecursionPasstroughStream::flush() { throw std::runtime_error("CANT FLUSH ON A RecursionPassthroughStream!"); }
std::ostream::pos_type RecursionPasstroughStream::tellp() { return accumulated_already_read_count + buffer.size(); }
OStreamLike& RecursionPasstroughStream::seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) {
  throw std::runtime_error("CANT SEEK ON A RecursionPassthroughStream!");
}

std::unique_ptr<RecursionPasstroughStream> recursion_decompress(RecursionContext& context, long long recursion_data_length, std::string tmpfile) {
  auto original_pos = context.fin->tellg();
  auto& precomp_mgr = context.precomp;

  // This is just a way for getting a new context, as we are not going to really use our context stack for decompression.
  // Is pretty dumb, could be made more straightforward but works for now
  recursion_push(context, recursion_data_length);
  auto new_ctx = std::move(precomp_mgr.ctx);
  recursion_pop(precomp_mgr);

  long long recursion_end_pos = original_pos + recursion_data_length;
  new_ctx->fin_length = recursion_data_length;
  // We create a view of the recursion data from the input stream, this way the recursive call to decompress can work as if it were working with a copy of the data
  // on a temporary file, processing it from pos 0 to EOF, while actually only reading from original_pos to recursion_end_pos
  auto fin_view = std::make_unique<IStreamLikeView>(context.fin.get(), recursion_end_pos);
  new_ctx->fin = std::move(fin_view);

  std::unique_ptr<RecursionPasstroughStream> passthrough = std::make_unique<RecursionPasstroughStream>(std::move(new_ctx));

  //print_to_log(PRECOMP_DEBUG_LOG, "Recursion start - new recursion depth %i\n", precomp_mgr.recursion_depth);

  //print_to_log(PRECOMP_DEBUG_LOG, "Recursion end - back to recursion depth %i\n", precomp_mgr.recursion_depth);

  return passthrough;
}

void fout_fput32_little_endian(OStreamLike& output, unsigned int v) {
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
Precomp* PrecompCreate() { return new Precomp(); }
void PrecompSetProgressCallback(Precomp* precomp_mgr, void(*callback)(float)) { precomp_mgr->set_progress_callback(callback); }
void PrecompDestroy(Precomp* precomp_mgr) { delete precomp_mgr; }
CSwitches* PrecompGetSwitches(Precomp* precomp_mgr) { return &precomp_mgr->switches; }
void PrecompSwitchesSetIgnoreList(CSwitches* precomp_switches, const long long* ignore_pos_list, size_t ignore_post_list_count) {
  reinterpret_cast<Switches*>(precomp_switches)->ignore_set = std::set(ignore_pos_list, ignore_pos_list + ignore_post_list_count);
}
CRecursionContext* PrecompGetRecursionContext(Precomp* precomp_mgr) { return precomp_mgr->ctx.get(); }
CResultStatistics* PrecompGetResultStatistics(Precomp* precomp_mgr) { return &precomp_mgr->statistics; }

int PrecompPrecompress(Precomp* precomp_mgr) {
  return compress_file(*precomp_mgr);
}

int PrecompRecompress(Precomp* precomp_mgr) {
  if (!precomp_mgr->statistics.header_already_read) read_header(*precomp_mgr);
  precomp_mgr->init_format_handlers(true);
  return decompress_file(*precomp_mgr->ctx);
}

int PrecompReadHeader(Precomp* precomp_mgr, bool seek_to_beg) {
  if (seek_to_beg) precomp_mgr->ctx->fin->seekg(0, std::ios_base::beg);
  try {
    read_header(*precomp_mgr);
  }
  catch (const PrecompError& err) {
    return err.error_code;
  }
  return 0;
}
