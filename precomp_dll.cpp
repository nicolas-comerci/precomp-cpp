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

std::map<SupportedFormats, std::function<PrecompFormatHandler* (Tools*)>> registeredHandlerFactoryFunctions {};
REGISTER_PRECOMP_FORMAT_HANDLER(D_PDF, PdfFormatHandler::create);
// This also adds support for D_MULTIPNG, handlers that define more than one SupportedFormats are somewhat wonky for now
REGISTER_PRECOMP_FORMAT_HANDLER(D_PNG, PngFormatHandler::create);
REGISTER_PRECOMP_FORMAT_HANDLER(D_GIF, GifFormatHandler::create);
REGISTER_PRECOMP_FORMAT_HANDLER(D_JPG, JpegFormatHandler::create);
REGISTER_PRECOMP_FORMAT_HANDLER(D_MP3, Mp3FormatHandler::create);
REGISTER_PRECOMP_FORMAT_HANDLER(D_BASE64, Base64FormatHandler::create);
std::map<SupportedFormats, std::function<PrecompFormatHandler2* ()>> registeredHandlerFactoryFunctions2 = std::map<SupportedFormats, std::function<PrecompFormatHandler2* ()>>{};
REGISTER_PRECOMP_FORMAT_HANDLER2(D_ZIP, ZipFormatHandler::create);
REGISTER_PRECOMP_FORMAT_HANDLER2(D_SWF, SwfFormatHandler::create);
REGISTER_PRECOMP_FORMAT_HANDLER2(D_GZIP, GZipFormatHandler::create);
REGISTER_PRECOMP_FORMAT_HANDLER2(D_RAW, ZlibFormatHandler::create);
REGISTER_PRECOMP_FORMAT_HANDLER2(D_BZIP2, BZip2FormatHandler::create);
REGISTER_PRECOMP_FORMAT_HANDLER2(D_BRUTE, DeflateFormatHandler::create);

void precompression_result::dump_header_to_outfile(OStreamLike& outfile) const {
  // write compressed data header
  outfile.put(static_cast<char>(flags | (recursion_used ? std::byte{ 0b10000000 } : std::byte{ 0b0 })));
  outfile.put(format);
}

void precompression_result::dump_penaltybytes_to_outfile(OStreamLike& outfile) const {
  if (penalty_bytes.empty()) return;
  auto pb_total_bytes = penalty_bytes.size() * 5; // 4bytes=uint32 pos, 1bytes=patch byte
  print_to_log(PRECOMP_DEBUG_LOG, "Penalty bytes were used: %i bytes\n", pb_total_bytes);
  fout_fput_vlint(outfile, pb_total_bytes);

  for (const auto& [pos, patch_byte] : penalty_bytes) {
    outfile.put((pos >> 24) % 256);
    outfile.put((pos >> 16) % 256);
    outfile.put((pos >> 8) % 256);
    outfile.put(pos % 256);
    outfile.put(patch_byte);
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
  precomp_mgr->fin = std::make_unique<GenericIStreamLike>(
    backing_structure, read_func, get_func, seekg_func, tellg_func, eof_func, bad_func, clear_func);
}

#ifdef DEBUG
void PrecompSetDebugCompareInputFile(Precomp* precomp_mgr, FILE* fhandle) {
  auto known_good = std::make_unique<FILEIStream>(fhandle, true);
  precomp_mgr->fin = std::make_unique<DebugComparatorIStreamLike>(std::move(known_good), std::move(precomp_mgr->fin));
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
  precomp_mgr->fout = std::unique_ptr<ObservableOStream>(new ObservableOStreamWrapper(gen_ostream.release(), true));
  
}

const char* PrecompGetOutputFilename(Precomp* precomp_mgr) {
  return precomp_mgr->output_file_name.c_str();
}

//Switches constructor
Switches::Switches(): CSwitches() {
  verify_precompressed = true;
  uncompressed_block_length = 100 * 1024 * 1024;
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

Precomp::Precomp():
  format_handler_tools(
    [this]() { this->call_progress_callback(); },
    [this](std::string name, bool append_tag) { return this->get_tempfile_name(name, append_tag); },
    [this](std::string name) { return this->statistics.increase_detected_count(name); },
    [this](std::string name) { return this->statistics.increase_precompressed_count(name); },
    [this](std::string name) { return this->statistics.increase_partially_precompressed_count(name); },
    [this](SupportedFormats format, long long pos, unsigned int recursion_depth) {
      if (this->is_format_handler_active(format, recursion_depth)) this->ignore_offsets[recursion_depth][format].emplace(pos);
    }
  )
{}

void Precomp::set_input_stdin() {
  // Read binary from stdin
  set_std_handle_binary_mode(StdHandles::STDIN_HANDLE);
  auto new_fin = std::make_unique<WrappedIStream>(new std::ifstream(), true);
  new_fin->rdbuf(std::cin.rdbuf());
  this->fin = std::move(new_fin);
}

void Precomp::set_input_stream(std::istream* istream, bool take_ownership) {
  if (istream == &std::cin) { set_input_stdin(); }
  else {
    this->fin = std::make_unique<WrappedIStream>(istream, take_ownership);
  }
}
void Precomp::set_input_stream(FILE* fhandle, bool take_ownership) {
  if (fhandle == stdin) { set_input_stdin(); }
  else {
    this->fin = std::make_unique<FILEIStream>(fhandle, take_ownership);
  }
}

void Precomp::set_output_stdout() {
  // Read binary to stdin
  // Write binary to stdout
  set_std_handle_binary_mode(StdHandles::STDOUT_HANDLE);
  auto new_fout = std::make_unique<ObservableWrappedOStream>(new std::ofstream(), true);
  new_fout->rdbuf(std::cout.rdbuf());
  this->fout = std::move(new_fout);
}

void Precomp::register_output_observer_callbacks() {
  // Set write observer to update progress when writing to output file, based on how much of the input file we have read
  this->fout->register_observer(ObservableOStream::observable_methods::write_method, [this]()
  {
    this->call_progress_callback();
  });
}

void Precomp::set_output_stream(std::ostream* ostream, bool take_ownership) {
  if (ostream == &std::cout) { set_output_stdout(); }
  else {
    this->fout = std::unique_ptr<ObservableOStream>(new ObservableWrappedOStream(ostream, take_ownership));
  }
  register_output_observer_callbacks();
}

void Precomp::set_output_stream(FILE* fhandle, bool take_ownership) {
  if (fhandle == stdout) { set_output_stdout(); }
  else {
    this->fout = std::unique_ptr<ObservableOStream>(new ObservableFILEOStream(fhandle, take_ownership));
  }
  register_output_observer_callbacks();
}

void Precomp::set_progress_callback(std::function<void(float)> callback) {
  progress_callback = std::move(callback);
}
void Precomp::call_progress_callback() const {
  if (!this->progress_callback) return;
  this->progress_callback((static_cast<float>(this->input_file_pos) / this->switches.fin_length) * 100);
}

std::string Precomp::get_tempfile_name(const std::string& name, bool prepend_random_tag) const {
  std::filesystem::path dir = switches.working_dir != nullptr ? switches.working_dir : std::filesystem::path();
  std::filesystem::path filename = prepend_random_tag ? temp_files_tag() + "_" + name : name;
  return (dir / filename).string();
}

void Precomp::init_format_handlers(bool is_recompressing) {
    if (is_recompressing || switches.use_zip) {
        format_handlers2.push_back(std::unique_ptr<PrecompFormatHandler2>(registeredHandlerFactoryFunctions2[D_ZIP]()));
    }
    if (is_recompressing || switches.use_gzip) {
        format_handlers2.push_back(std::unique_ptr<PrecompFormatHandler2>(registeredHandlerFactoryFunctions2[D_GZIP]()));
    }
    if (is_recompressing || switches.use_pdf) {
        format_handlers.push_back(std::unique_ptr<PrecompFormatHandler>(registeredHandlerFactoryFunctions[D_PDF](&format_handler_tools)));
    }
    if (is_recompressing || switches.use_png) {
        format_handlers.push_back(std::unique_ptr<PrecompFormatHandler>(registeredHandlerFactoryFunctions[D_PNG](&format_handler_tools)));
    }
    if (is_recompressing || switches.use_gif) {
        format_handlers.push_back(std::unique_ptr<PrecompFormatHandler>(registeredHandlerFactoryFunctions[D_GIF](&format_handler_tools)));
    }
    if (is_recompressing || switches.use_jpg) {
        format_handlers.push_back(std::unique_ptr<PrecompFormatHandler>(registeredHandlerFactoryFunctions[D_JPG](&format_handler_tools)));
    }
    if (is_recompressing || switches.use_mp3) {
        format_handlers.push_back(std::unique_ptr<PrecompFormatHandler>(registeredHandlerFactoryFunctions[D_MP3](&format_handler_tools)));
    }
    if (is_recompressing || switches.use_swf) {
        format_handlers2.push_back(std::unique_ptr<PrecompFormatHandler2>(registeredHandlerFactoryFunctions2[D_SWF]()));
    }
    if (is_recompressing || switches.use_base64) {
        format_handlers.push_back(std::unique_ptr<PrecompFormatHandler>(registeredHandlerFactoryFunctions[D_BASE64](&format_handler_tools)));
    }
    if (is_recompressing || switches.use_bzip2) {
        format_handlers2.push_back(std::unique_ptr<PrecompFormatHandler2>(registeredHandlerFactoryFunctions2[D_BZIP2]()));
    }
    if (is_recompressing || switches.intense_mode) {
        format_handlers2.push_back(std::unique_ptr<PrecompFormatHandler2>(registeredHandlerFactoryFunctions2[D_RAW]()));
        if (switches.intense_mode_depth_limit >= 0) {
            format_handlers2.back()->depth_limit = switches.intense_mode_depth_limit;
        }
    }
    // Brute mode detects a bit less than intense mode to avoid false positives and slowdowns, so both can be active.
    if (is_recompressing || switches.brute_mode) {
        format_handlers2.push_back(std::unique_ptr<PrecompFormatHandler2>(registeredHandlerFactoryFunctions2[D_BRUTE]()));
        if (switches.brute_mode_depth_limit >= 0) {
            format_handlers2.back()->depth_limit = switches.brute_mode_depth_limit;
        }
    }
}

const std::vector<std::unique_ptr<PrecompFormatHandler>>& Precomp::get_format_handlers() const {
    return format_handlers;
}

const std::vector<std::unique_ptr<PrecompFormatHandler2>>& Precomp::get_format_handlers2() const {
  return format_handlers2;
}

bool Precomp::is_format_handler_active(SupportedFormats format_id, unsigned int recursion_depth) const {
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

void end_uncompressed_data(IBufferedIStream& input, OStreamLike& output, const long long& uncompressed_pos, std::optional<long long>& uncompressed_length) {
  if (!uncompressed_length.has_value()) return;

  fout_fput_vlint(output, *uncompressed_length);

  // fast copy of uncompressed data
  input.seekg(uncompressed_pos, std::ios_base::beg);
  fast_copy(input, output, *uncompressed_length);

  // Tell input it can discard the uncompressed data from it's buffer
  input.set_new_buffer_start_pos(uncompressed_pos + *uncompressed_length);

  uncompressed_length = std::nullopt;
}

void write_header(Precomp& precomp_mgr) {
  // write the PCF file header, beware that this needs to be done before wrapping the output file with a CompressedOStreamBuffer
  char* input_file_name_without_path = new char[precomp_mgr.input_file_name.length() + 1];

  ostream_printf(*precomp_mgr.fout, "PCF");

  // version number
  precomp_mgr.fout->put(V_MAJOR);
  precomp_mgr.fout->put(V_MINOR);
  precomp_mgr.fout->put(V_MINOR2);

  // compression-on-the-fly method used, 0, as OTF compression no longer supported
  precomp_mgr.fout->put(0);

  // write input file name without path
  const char* last_backslash = strrchr(precomp_mgr.input_file_name.c_str(), PATH_DELIM);
  if (last_backslash != nullptr) {
    strcpy(input_file_name_without_path, last_backslash + 1);
  }
  else {
    strcpy(input_file_name_without_path, precomp_mgr.input_file_name.c_str());
  }

  ostream_printf(*precomp_mgr.fout, input_file_name_without_path);
  precomp_mgr.fout->put(0);

  delete[] input_file_name_without_path;
}

bool verify_precompressed_result(Precomp& precomp_mgr, IStreamLike& input, const std::unique_ptr<precompression_result>& result, long long& input_file_pos);
struct recursion_result {
  bool success;
  std::string file_name;
  long long file_length = 0;
  std::unique_ptr<std::ifstream> frecurse = std::make_unique<std::ifstream>();
};
recursion_result recursion_compress(Precomp& precomp_mgr, long long decompressed_bytes, IStreamLike& tmpfile, std::string out_filename, unsigned int recursion_depth);
int decompress_file(
  IStreamLike& input,
  OStreamLike& output,
  Tools& precomp_tools,
  const std::vector<std::unique_ptr<PrecompFormatHandler>>& format_handlers,
  const std::vector<std::unique_ptr<PrecompFormatHandler2>>& format_handlers2
);

class PrecompPcfRecompressor : public ProcessorAdapter {
public:
  uint32_t avail_in = 0;
  std::byte* next_in = nullptr;
  uint32_t avail_out = 0;
  std::byte* next_out = nullptr;

  explicit PrecompPcfRecompressor(
    Tools& precomp_tools,
    const std::vector<std::unique_ptr<PrecompFormatHandler>>& format_handlers,
    const std::vector<std::unique_ptr<PrecompFormatHandler2>>& format_handlers2) :
    ProcessorAdapter(
      &avail_in, &next_in, &avail_out, &next_out,
      [&](IStreamLike& input, OStreamLike& output) -> bool
      {
        const auto result = decompress_file(input, output, precomp_tools, format_handlers, format_handlers2);
        return result == RETURN_SUCCESS;
      }
    ) {}
};

int compress_file(Precomp& precomp_mgr, IBufferedIStream& input, OStreamLike& output, unsigned int recursion_depth);

class PrecompPcfPrecompressor : public ProcessorAdapter {
public:
  uint32_t avail_in = 0;
  std::byte* next_in = nullptr;
  uint32_t avail_out = 0;
  std::byte* next_out = nullptr;

  explicit PrecompPcfPrecompressor(
    Precomp& precomp_mgr,
    unsigned int recursion_depth
  ) :
    ProcessorAdapter(
      &avail_in, &next_in, &avail_out, &next_out,
      [recursion_depth, &precomp_mgr](IStreamLike& input, OStreamLike& output) -> bool
      {
        auto buffered_istream = BufferedIStream(&input, precomp_mgr.get_tempfile_name("recursion_input_buffer"));
        precomp_mgr.ignore_offsets.resize(precomp_mgr.ignore_offsets.size() + 1);
        const auto result = compress_file(precomp_mgr, buffered_istream, output, recursion_depth);
        precomp_mgr.ignore_offsets.pop_back();
        // We use this to Precompress recursively on the fly, we don't want to fail if nothing was recompressed
        // because that data already got outputted anyways most likely, and we at most waste 2 bytes (recursion header + end marker)
        return result >= RETURN_SUCCESS;
      }
    )
  {}
};

bool compare_verified_chunk(Precomp& precomp_mgr, IStreamLike& input, uint32_t output_data_size, uint64_t& already_verified_bytes, long long input_file_pos,
  std::vector<char>& original_block_data, std::vector<std::byte>& verification_vec_out) {
  // Check how much was actually outputted from the recompressor and reset the output buffer for next iteration
  const auto recompressed_block_amount = output_data_size;

  // Check hash of recompressed data by reseeking on input to get the same data and calculate the hash
  const auto saved_pos = input.tellg();
  input.seekg(input_file_pos + already_verified_bytes, std::ios_base::beg);
  original_block_data.resize(recompressed_block_amount);
  input.read(original_block_data.data(), recompressed_block_amount);
  auto original_sha1_block_ostream = Sha1Ostream();
  original_sha1_block_ostream.write(original_block_data.data(), recompressed_block_amount);
  const auto original_data_sha1 = original_sha1_block_ostream.get_digest();

  auto verify_sha1_block_ostream = Sha1Ostream();
  verify_sha1_block_ostream.write(reinterpret_cast<char*>(verification_vec_out.data()), recompressed_block_amount);
  const auto verified_data_sha1 = verify_sha1_block_ostream.get_digest();

  // restore input to its previous pos to not interfere with the Processor
  input.seekg(saved_pos, std::ios_base::beg);
  if (original_data_sha1 != verified_data_sha1) {
    /*  TODO: PENALTY BYTES STUFF
    // if hashes mismatch, attempt to fix the mismatch with penalty bytes
    const auto original_block_data_ptr = reinterpret_cast<unsigned char*>(original_block_data.data());
    auto original_block_view = memiostream(original_block_data_ptr, original_block_data_ptr + recompressed_block_amount, false);
    const auto recompressed_block_data_ptr = reinterpret_cast<unsigned char*>(verification_vec_out.data());
    auto recompressed_block_view = memiostream(recompressed_block_data_ptr, recompressed_block_data_ptr + recompressed_block_amount, false);
    auto [identical_bytes, penalty_bytes] = compare_files_penalty(
      precomp_mgr.format_handler_tools.progress_callback, original_block_view, recompressed_block_view, recompressed_block_amount
    );

    if (identical_bytes == recompressed_block_amount) {
      print_to_console("\n PENALTY BYTES HUBIERAN HECHO PATRIA \n");
    }
    else {
      print_to_console("\n NI SIQUIERA PENALTY BYTES ARREGLAN ACA \n");
    }
    */
    return false;
  }
  already_verified_bytes += recompressed_block_amount;
  return true;  
}

int compress_file_impl(Precomp& precomp_mgr, IBufferedIStream& input, OStreamLike& output, unsigned int recursion_depth) {
  if (recursion_depth == 0) {
      write_header(precomp_mgr);
      precomp_mgr.init_format_handlers();
  }

  const auto& format_handlers = precomp_mgr.get_format_handlers();
  const auto& format_handlers2 = precomp_mgr.get_format_handlers2();

  long long uncompressed_pos = 0;
  std::optional<long long> uncompressed_length = std::nullopt;

  std::vector<std::byte> precompressor_in_buf{};
  std::vector<std::byte> precompressor_out_buf{};

  unsigned char in_buf[IN_BUF_SIZE];

  std::optional<uintmax_t> input_length;

  input.seekg(0, std::ios_base::beg);
  input.read(reinterpret_cast<char*>(in_buf), IN_BUF_SIZE);
  const auto initial_read = input.gcount();
  if (initial_read < IN_BUF_SIZE) {
    input_length = initial_read;
  }
  long long in_buf_pos = 0;
  // This buffer will be fed to the format handlers so they can confirm if the current position is the beggining of a stream they support
  std::span<unsigned char> checkbuf;

  bool anything_was_used = false;

  for (long long input_file_pos = 0; !input_length.has_value() || input_file_pos < *input_length; input_file_pos++) {
    if (recursion_depth == 0) precomp_mgr.input_file_pos = input_file_pos;
    bool compressed_data_found = false;
    bool is_partial_stream = false;

    bool ignore_this_pos = false;

    if ((in_buf_pos + IN_BUF_SIZE) <= (input_file_pos + CHECKBUF_SIZE)) {
      input.seekg(input_file_pos, std::ios_base::beg);
      input.read(reinterpret_cast<char*>(in_buf), IN_BUF_SIZE);
      const auto just_read = input.gcount();
      if (just_read < IN_BUF_SIZE) {
        input_length = input_file_pos + just_read;
      }
      in_buf_pos = input_file_pos;
    }
    auto cb_pos = input_file_pos - in_buf_pos;
    checkbuf = std::span(&in_buf[cb_pos], IN_BUF_SIZE - cb_pos);

    ignore_this_pos = !precomp_mgr.switches.ignore_pos_queue.empty() && input_file_pos == precomp_mgr.switches.ignore_pos_queue.front();

    if (ignore_this_pos) { precomp_mgr.switches.ignore_pos_queue.pop(); }
    else {
      for (const auto& formatHandler : format_handlers2) {
        // Recursion depth check
        if (formatHandler->depth_limit && recursion_depth > formatHandler->depth_limit) continue;

        // Position blacklist check
        const SupportedFormats& formatTag = formatHandler->get_header_bytes()[0];
        std::queue<long long>& ignoreList = precomp_mgr.ignore_offsets[recursion_depth][formatTag];
        if (!ignoreList.empty()) {
          bool ignore_this_position = false;
          auto& first = ignoreList.front();
          while (first < input_file_pos) {
            ignoreList.pop();
            if (ignoreList.empty()) break;
            first = ignoreList.front();
          }

          if (!ignoreList.empty()) {
            if (first == input_file_pos) {
              ignore_this_position = true;
              ignoreList.pop();
            }
          }
          if (ignore_this_position) continue;
        }

        input.seekg(input_file_pos, std::ios_base::beg);

        bool quick_check_result = false;
        try {
          quick_check_result = formatHandler->quick_check(checkbuf, reinterpret_cast<uintptr_t>(&input), input_file_pos);
        }
        catch (...) {}  // TODO: print/record/report handler failed
        if (!quick_check_result) continue;

        std::unique_ptr<PrecompFormatPrecompressor> precompressor{};

        uint64_t initial_output_pos = output.tellp();
        uint64_t already_verified_bytes = 0;
        // they are not really outputted, its more like "with the precompressed data that is already outputted you can recompress this amount of bytes"
        const auto output_chunk_size = 1 * 1024 * 1024; // TODO: make this configurable

        // For recursion we will take the raw outputted data (before chunking or adding headers) and feed it to a PrecompPcfPrecompressor.
        // It will handle the chunking at that new recursion level, we only need to leave a header with a recursion flag.
        // Verification runs always on the stream without recursion, as far as we are concerned precompressing again should be a process that cannot
        // fail, at worst it should just not find anything inside. Any failure is critical and the whole process should fail.
        // Similarly the correctness of recompressing this recursively precompressed data is derived from the verification of each "leaf" stream,
        // and no further verification should be required.
        const bool use_recursion = formatHandler->recursion_allowed && precomp_mgr.switches.max_recursion_depth > recursion_depth;
        if (formatHandler->recursion_allowed && !use_recursion) {
          precomp_mgr.statistics.max_recursion_depth_reached = true;
        }
        auto recurse_precompressor = PrecompPcfPrecompressor(precomp_mgr, recursion_depth + 1);
        std::vector<std::byte> recursion_out{};
        recursion_out.resize(CHUNK);

        // Precomp format header
        std::byte header_byte{ 0b1 };
        if (/*!penalty_bytes.empty()*/false) {
          header_byte |= std::byte{ 0b10 };
        }
        //header_byte |= result->recursion_used ? std::byte{ 0b10000000 } : std::byte{ 0b0 };

        // memory data, header data size, chunk data pos, chunk size
        using PendingChunk = std::tuple<MemVecIOStream, std::streampos, std::streampos, std::streampos>;
        static auto make_chunk = [](
          PrecompFormatPrecompressor& precompressor, uint32_t current_chunk_size, const std::vector<std::byte>& out_buf,
          bool is_first_block, std::byte header_byte, SupportedFormats formatTag, bool is_last_block
          )-> PendingChunk {
            MemVecIOStream block_mem;
            if (is_first_block) {
              // Output Precomp format header
              block_mem.put(static_cast<char>(header_byte));
              block_mem.put(formatTag);
              precompressor.dump_extra_stream_header_data(block_mem);
            }

            // Block chunk header
            block_mem.put(static_cast<char>(is_last_block ? std::byte{ 0b10000001 } : std::byte{ 0b00000000 }));
            precompressor.dump_extra_block_header_data(block_mem);
            precompressor.block_dumped(block_mem);
            const auto header_data_size = block_mem.tellp();

            fout_fput_vlint(block_mem, current_chunk_size);
            const auto chunk_data_pos = block_mem.tellp();
            block_mem.write(reinterpret_cast<const char*>(out_buf.data()), current_chunk_size);
            const auto chunk_size = block_mem.tellp();

            block_mem.seekg(0, std::ios_base::beg); // leave the memory stream ready for reading
            return { block_mem, header_data_size, chunk_data_pos, chunk_size };
          };

        try {
          precompressor = formatHandler->make_precompressor(precomp_mgr.format_handler_tools, precomp_mgr.switches, checkbuf);
          precomp_mgr.call_progress_callback();

          bool verification_is_first_block = true;
          bool output_is_first_block = true;

          // For verification, we will run recompression and calculate SHA1 of recompressed data as we write each block.
          // We can use that SHA1 at the end to compare against the input read data's SHA1 hash.
          auto verify_recompressor = PrecompPcfRecompressor(precomp_mgr.format_handler_tools, precomp_mgr.get_format_handlers(), precomp_mgr.get_format_handlers2());
          std::vector<std::byte> verification_vec_out{};
          verification_vec_out.resize(CHUNK);
          std::vector<char> original_block_data{};

          std::queue<PendingChunk> pending_chunks;
          std::function output_chunk_callback = [&](std::vector<std::byte>& out_buf, uint32_t current_chunk_size, bool is_last_block) {
            bool new_verified_data = true;
            const auto prev_already_verified_bytes = already_verified_bytes;
            if (precomp_mgr.switches.verify_precompressed) {
              // Output the chunk and verify it's data against the original input
              auto new_chunk = make_chunk(*precompressor, current_chunk_size, out_buf, verification_is_first_block, header_byte, formatTag, is_last_block);
              auto& [block_mem, _, _2, chunk_size] = new_chunk;

              // Recompress using PrecompPcfRecompressor reading from verification_vec, dump output to verify_sha1_ostream
              const PrecompProcessorReturnCode verification_ret = process_and_exhaust_output(
                verify_recompressor,
                const_cast<std::byte*>(block_mem.vector().data()),  // puajj const_cast, find better way
                chunk_size,
                verification_vec_out,
                is_last_block,
                [&](uint32_t output_data_size) -> bool {
                  return compare_verified_chunk(precomp_mgr, input, output_data_size, already_verified_bytes, input_file_pos, original_block_data, verification_vec_out);
                }
              );
              if (verification_ret == PP_ERROR || verification_ret == PP_STREAM_END && !is_last_block) {
                // something went wrong here
                return false;
              }

              pending_chunks.push(std::move(new_chunk));
              new_verified_data = already_verified_bytes > prev_already_verified_bytes;
            }
            else {
              pending_chunks.emplace(make_chunk(*precompressor, current_chunk_size, out_buf, verification_is_first_block, header_byte, formatTag, is_last_block));
            }
            verification_is_first_block = false;

            // We only want to actually output data when we know its going to allow us to recompress some data, if no new data was recompressed
            // during verification we hold the data chunks until we get some more verified data, at which point we dump it to the real output
            if (!new_verified_data) return true;

            if (output_is_first_block) {
              const auto saved_pos = input.tellg();
              end_uncompressed_data(input, output, uncompressed_pos, uncompressed_length);
              input.seekg(saved_pos, std::ios_base::beg);
            }
            // If we should recursively precompress then do it, either just dump the precompressed data
            if (use_recursion) {
              while (!pending_chunks.empty()) {
                auto [block_mem, header_data_size, chunk_data_pos, chunk_size] = std::move(pending_chunks.front());
                pending_chunks.pop();

                MemVecIOStream recursive_to_output;
                const bool finalize_recursion = is_last_block && pending_chunks.empty();  // finalize only if we just popped the actual last block
                PrecompProcessorReturnCode recursive_precompression_ret = process_and_exhaust_output(
                  recurse_precompressor,
                  const_cast<std::byte*>(block_mem.vector().data() + chunk_data_pos),  // bleehrg! find better way to do this,
                  chunk_size - chunk_data_pos,
                  recursion_out,
                  finalize_recursion,
                  [&recursive_to_output, &recursion_out](uint32_t output_data_size) -> bool {
                    recursive_to_output.write(reinterpret_cast<char*>(recursion_out.data()), output_data_size);
                    return true;
                  }
                );
                if (recursive_precompression_ret == PP_ERROR || (finalize_recursion && recursive_precompression_ret != PP_STREAM_END)) {
                  throw std::runtime_error("Error during precompression recursion");
                }

                // Probably need to find a better way of doing this, but we need to flag the stream as recusive
                if (output_is_first_block) {
                  output.put(block_mem.get() | 0b10000000);
                  fast_copy(block_mem, output, static_cast<long long>(header_data_size) - 1);
                  output_is_first_block = false;
                }
                else {
                  fast_copy(block_mem, output, header_data_size);
                }

                const uint64_t recursive_block_data_size = recursive_to_output.tellp();
                fout_fput_vlint(output, recursive_block_data_size);
                recursive_to_output.seekg(0, std::ios_base::beg);
                fast_copy(recursive_to_output, output, recursive_block_data_size);
                if (output.bad()) return false;
              }
            }
            else {
              while (!pending_chunks.empty()) {
                auto [block_mem, _, _2, chunk_size] = std::move(pending_chunks.front());
                pending_chunks.pop();

                fast_copy(block_mem, output, chunk_size);
                if (output.bad()) return false;
              }
            }
            output_is_first_block = false;

            if (precomp_mgr.switches.verify_precompressed) {
              input.set_new_buffer_start_pos(input_file_pos + already_verified_bytes);
            }
            return true;
          };

          const auto [ret, _] = process_from_input_and_output_chunks(
            *precompressor,
            precompressor_in_buf, input,
            precompressor_out_buf, output_chunk_size, std::move(output_chunk_callback)
          );

          const uint64_t precompressed_stream_size = static_cast<uint64_t>(output.tellp()) - initial_output_pos;
          if (
            (ret == PP_ERROR && !precomp_mgr.switches.verify_precompressed) ||
            precompressed_stream_size <= 0 ||
            output_is_first_block ||
            already_verified_bytes == 0 && precomp_mgr.switches.verify_precompressed
          ) {
            continue;
          }

          precompressor->increase_detected_count();
          if (ret != PP_STREAM_END) is_partial_stream = true;

          print_to_log(PRECOMP_DEBUG_LOG, "Compressed size: %lli\n", precompressor->original_stream_size);
          print_to_log(PRECOMP_DEBUG_LOG, "Can be decompressed to %lli bytes\n", precompressed_stream_size);
        }
        catch (...) { continue; }  // TODO: print/record/report handler failed

        if (is_partial_stream) {
          precompressor->increase_partially_precompressed_count();
        }
        else {
          precompressor->increase_precompressed_count();
        }
        anything_was_used = true;

        if (recursion_depth > precomp_mgr.statistics.max_recursion_depth_used) {
          precomp_mgr.statistics.max_recursion_depth_used = recursion_depth;
        }

        if (is_partial_stream) {
          // If the stream is partial and we are using recursion then we need to truncate the recursive stream
          if (use_recursion) {
            MemVecIOStream recursive_to_output;
            PrecompProcessorReturnCode recursive_precompression_ret = process_and_exhaust_output(
              recurse_precompressor,
              nullptr,
              0,
              recursion_out,
              true,
              [&recursive_to_output, &recursion_out](uint32_t output_data_size) -> bool {
                recursive_to_output.write(reinterpret_cast<char*>(recursion_out.data()), output_data_size);
                return true;
              }
            );
            if (recursive_precompression_ret != PP_STREAM_END) {
              throw std::runtime_error("Error during precompression recursion");
            }
            auto [block_mem, _, _2, chunk_size] = make_chunk(
              *precompressor, recursive_to_output.tellp(), recursive_to_output.vector(), false,
              header_byte | std::byte{ 0b10000000 }, formatTag, false  // for partial stream there is no real last block, if finishes when it gets the partial stream finish marker
            );
            fast_copy(block_mem, output, chunk_size);
          }

          // partial stream finish marker
          output.put(static_cast<char>(std::byte { 0b10000000 }));
        }

        // set input file pointer after recompressed data (minus 1 because iteration will sum 1)
        if (precomp_mgr.switches.verify_precompressed) {
          input_file_pos += already_verified_bytes - 1;
        }
        else {
          input_file_pos += precompressor->original_stream_size - 1;
        }
        compressed_data_found = true;
        break;
      }
      for (const auto& formatHandler : format_handlers) {
        if (compressed_data_found) break;
        // Recursion depth check
        if (formatHandler->depth_limit && recursion_depth > formatHandler->depth_limit) continue;

        // Position blacklist check
        const SupportedFormats& formatTag = formatHandler->get_header_bytes()[0];
        std::queue<long long>& ignoreList = precomp_mgr.ignore_offsets[recursion_depth][formatTag];
        if (!ignoreList.empty()) {
          bool ignore_this_position = false;
          auto& first = ignoreList.front();
          while (first < input_file_pos) {
            ignoreList.pop();
            if (ignoreList.empty()) break;
            first = ignoreList.front();
          }

          if (!ignoreList.empty()) {
            if (first == input_file_pos) {
              ignore_this_position = true;
              ignoreList.pop();
            }
          }
          if (ignore_this_position) continue;
        }

        input.seekg(input_file_pos, std::ios_base::beg);

        bool quick_check_result = false;
        try {
          quick_check_result = formatHandler->quick_check(checkbuf, reinterpret_cast<uintptr_t>(&input), input_file_pos);
        }
        catch (...) {}  // TODO: print/record/report handler failed
        if (!quick_check_result) continue;

        std::unique_ptr<precompression_result> result {};
        try {
          result = formatHandler->attempt_precompression(input, output, checkbuf, input_file_pos, precomp_mgr.switches, recursion_depth);
        }
        catch (...) {}  // TODO: print/record/report handler failed

        if (!result) continue;
        result->increase_detected_count();
        if (!result->success) continue;

        // If verification is enabled, we attempt to recompress the stream right now, and reject it if anything fails or data doesn't match
        // Note that this is done before recursion for 2 reasons:
        //  1) why bother recursing a stream we might reject
        //  2) verification would be much more complicated as we would need to prevent recursing on recompression which would lead to verifying some streams MANY times
        if (precomp_mgr.switches.verify_precompressed) {
          bool verification_success = false;
          try {
            verification_success = verify_precompressed_result(precomp_mgr, input, result, input_file_pos);
          }
          catch (...) {}  // TODO: print/record/report handler failed
          if (!verification_success) continue;
          // ensure that the precompressed stream is ready to read from the start, as if verification never happened
          result->precompressed_stream->seekg(0, std::ios_base::beg);
        }
        result->increase_precompressed_count();
        anything_was_used = true;

        // We got successful stream and if required it was verified, even if we need to recurse and recursion fails/doesn't find anything, we already know we are
        // going to write this stream, which means we can as well write any pending uncompressed data now
        // (might allow any pipe/code using the library waiting on data from Precomp to be able to work with it while we do recursive processing)
        end_uncompressed_data(input, output, uncompressed_pos, uncompressed_length);

        // If the format allows for it, recurse inside the most likely newly decompressed data
        if (formatHandler->recursion_allowed) {
            auto recurse_tempfile_name = precomp_mgr.get_tempfile_name("recurse");
            recursion_result r{};
            try {
              r = recursion_compress(precomp_mgr, result->precompressed_size, *result->precompressed_stream, recurse_tempfile_name, recursion_depth);
            }
            catch (...) {}  // TODO: print/record/report handler failed
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
        
        result->dump_to_outfile(output);

        // start new uncompressed data

        // set input file pointer after recompressed data
        input_file_pos += result->complete_original_size() - 1;
        compressed_data_found = result->success;
        break;
      }
    }

    if (!compressed_data_found) {
      if (!uncompressed_length.has_value()) {
        uncompressed_length = 0;
        uncompressed_pos = input_file_pos;

        // uncompressed data
        output.put(0);
      }
      (*uncompressed_length)++;
      // If there is a maximum uncompressed_block_length we dump the current uncompressed data as a single block, this makes it so anything waiting on data from Precomp
      // can get some data to possibly process earlier
      if (precomp_mgr.switches.uncompressed_block_length != 0 && uncompressed_length >= precomp_mgr.switches.uncompressed_block_length) {
        end_uncompressed_data(input, output, uncompressed_pos, uncompressed_length);
      }
    }
    else {
      // We found data and precompressed it, and have our current input_file_pos after that data, we have no need for any data prior to this
      // as it was all already precompressed or outputted as uncompressed data
      input.set_new_buffer_start_pos(input_file_pos + 1);  // +1 as we want the pos for the next iteration, not the current one
    }
  }

  // Dump any trailing uncompressed data in case nothing was detected at the end of the stream
  end_uncompressed_data(input, output, uncompressed_pos, uncompressed_length);

  // Stream end marker
  output.put(0xFF);

  // TODO: maybe we should just make sure the whole last context gets destroyed if at recursion_depth == 0?
  if (recursion_depth == 0) precomp_mgr.fout = nullptr; // To close the outfile

  return anything_was_used ? RETURN_SUCCESS : RETURN_NOTHING_DECOMPRESSED;
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

int compress_file(Precomp& precomp_mgr, IBufferedIStream& input, OStreamLike& output, unsigned int recursion_depth)
{
  return wrap_with_exception_catch([&]() { return compress_file_impl(precomp_mgr, input, output, recursion_depth); });
}

class RecursionPasstroughStream: public PrecompPcfRecompressor, public IStreamLike {
  std::vector<std::byte> in_buf;
  std::vector<std::byte> out_buf;
  std::unique_ptr<IStreamLike> input;

  PrecompProcessorReturnCode processor_return = PP_OK;
  std::vector<std::byte> output_pending_data;

  uint64_t pos = 0;
  uint64_t _gcount = 0;

public:
  explicit RecursionPasstroughStream(
    Tools& precomp_tools,
    std::unique_ptr<IStreamLike>&& _input,
    const std::vector<std::unique_ptr<PrecompFormatHandler>>& format_handlers,
    const std::vector<std::unique_ptr<PrecompFormatHandler2>>& format_handlers2)
    : PrecompPcfRecompressor(precomp_tools, format_handlers, format_handlers2), input(std::move(_input))
  {
    in_buf.resize(CHUNK);
    out_buf.resize(CHUNK);
    next_in = in_buf.data();
    avail_in = 0;
    next_out = out_buf.data();
    avail_out = CHUNK;
  }

  RecursionPasstroughStream& read(char* buff, std::streamsize count) override {
    auto remainingCount = count;
    auto currentBuffPos = buff;
    bool reached_eof = false;

    while (remainingCount > 0) {
      // there is some remaining output data on the buffer, get it from there
      if (!output_pending_data.empty()) {
        const auto to_read = std::min<std::streamsize>(output_pending_data.size(), remainingCount);
        std::copy_n(reinterpret_cast<char*>(output_pending_data.data()), to_read, currentBuffPos);
        currentBuffPos += to_read;
        remainingCount -= to_read;
        // TODO: This incurs reallocation, find a better way?
        output_pending_data.erase(output_pending_data.begin(), output_pending_data.begin() + to_read);
      }
      // no more output data available, need to process to get more
      else {
        // Processor already finished or error'd out, can't process anymore 
        if (processor_return != PP_OK) break;
        const auto [read_amt, gcount] = fill_processor_input_buffer(*this, in_buf, *input);
        if (input->bad()) break;

        //const auto avail_in_before_process = avail_in;
        processor_return = this->process(reached_eof);
        if (processor_return != PP_OK && processor_return != PP_STREAM_END) break;

        const uint32_t decompressed_chunk_size = CHUNK - avail_out;
        const bool is_last_block = processor_return == PP_STREAM_END;
        if (is_last_block || decompressed_chunk_size == CHUNK) {
          output_pending_data.insert(output_pending_data.end(), out_buf.data(), out_buf.data() + decompressed_chunk_size);

          // All outputted data handled by output_callback, reset processor's output so we can get another chunk
          next_out = out_buf.data();
          avail_out = CHUNK;
        }

        //if (processor_return == PP_STREAM_END) break;  // maybe we should also check fin for eof? we could support partial/broken BZip2 streams

        if (read_amt > 0 && gcount == 0 && decompressed_chunk_size < CHUNK) {
          // No more data will ever come, and we already flushed all the output data processed, notify processor so it can finish
          reached_eof = true;
        }
      }
    }
    _gcount = count - remainingCount;
    pos += _gcount;
    return *this;
  }

  std::istream::int_type get() override {
    char buf[1];
    read(buf, 1);
    return _gcount == 1 ? buf[0] : EOF;
  }

  std::streamsize gcount() override {
    return _gcount;
  }
  std::istream::pos_type tellg() override { return pos; }

  IStreamLike& seekg(std::istream::off_type offset, std::ios_base::seekdir dir) override {
    throw std::runtime_error("CAN'T SEEK ON RecursionPasstroughStream");
  }

  bool eof() override { return processor_return != PP_OK; }
  bool bad() override { return processor_return == PP_ERROR; }
  bool good() override { return !bad(); }
  void clear() override { processor_return = PP_OK; } // doubt this is ever useful, maybe should just throw an exception
};

std::unique_ptr<RecursionPasstroughStream> recursion_decompress(IStreamLike& input,
  OStreamLike& output,
  Tools& precomp_tools,
  const std::vector<std::unique_ptr<PrecompFormatHandler>>& format_handlers,
  const std::vector<std::unique_ptr<PrecompFormatHandler2>>& format_handlers2,
  long long recursion_data_length
);

class PenaltyBytesPatchedOStream : public OStreamLike {
  OStreamLike* ostream = nullptr;
  uint64_t next_pos = 0;
  std::queue<std::tuple<uint32_t, unsigned char>>* penalty_bytes = nullptr;
public:
  PenaltyBytesPatchedOStream(OStreamLike* _ostream, std::queue<std::tuple<uint32_t, unsigned char>>* _penalty_bytes) : ostream(_ostream), penalty_bytes(_penalty_bytes) {}

  PenaltyBytesPatchedOStream& write(const char* buf, std::streamsize count) override {
    auto chunk_last_post = next_pos + count - 1;
    // If no penalty_bytes at all, or left, or any that apply to the data about to be written, just use the original ostream, unpatched
    if (!penalty_bytes || penalty_bytes->empty() || std::get<0>(penalty_bytes->front()) > chunk_last_post) {
      ostream->write(buf, count);
    }
    // Copy input buffer and patch applicable penalty bytes before writing
    // TODO: why not just write to the output buffer directly and patch there?
    else {
      std::vector<char> buf_cpy{};
      buf_cpy.resize(count);
      std::copy_n(buf, count, buf_cpy.data());
      while (!penalty_bytes->empty()) {
        const auto& next_pb_tuple = penalty_bytes->front();
        const auto& next_pb_pos = std::get<0>(next_pb_tuple);
        if (next_pb_pos > chunk_last_post) break;
        buf_cpy[next_pb_pos - next_pos] = static_cast<char>(std::get<1>(next_pb_tuple));
        penalty_bytes->pop();
      }
      ostream->write(buf_cpy.data(), count);
    }
    next_pos += count;
    return *this;
  }

  PenaltyBytesPatchedOStream& put(char chr) override {
    char buf[1];
    buf[0] = chr;
    return write(buf, 1);
  }

  void flush() override { ostream->flush(); }
  std::ostream::pos_type tellp() override { return ostream->tellp(); }
  PenaltyBytesPatchedOStream& seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) override {
    ostream->seekp(offset, dir);
    return *this;
  }

  bool eof() override { return ostream->eof(); }
  bool bad() override { return ostream->bad(); }
  bool good() override { return ostream->good(); }
  void clear() override { ostream->clear(); }
};

int decompress_file_impl(
  IStreamLike& input,
  OStreamLike& output,
  Tools& precomp_tools,
  const std::vector<std::unique_ptr<PrecompFormatHandler>>& format_handlers,
  const std::vector<std::unique_ptr<PrecompFormatHandler2>>& format_handlers2
) {  
  std::array<unsigned char, CHUNK> in_buf{};
  std::array<unsigned char, CHUNK> out_buf{};

  long long fin_pos = input.tellg();

  while (input.good()) {
    const auto header1 = static_cast<std::byte>(input.get());
    if (!input.good()) break;

    bool handlerFound = false;
    if (header1 == std::byte{ 0 }) { // uncompressed data
      handlerFound = true;
      const long long uncompressed_data_length = fin_fget_vlint(input);
  
      if (uncompressed_data_length == 0) break; // end of PCF file, used by bZip2 compress-on-the-fly
  
      print_to_log(PRECOMP_DEBUG_LOG, "Uncompressed data, length=%lli\n");
      fast_copy(input, output, uncompressed_data_length);
  
    }
    else if (header1 == std::byte{ 0xFF }) { // stream finish marker
      break;
    }
    else { // decompressed data, recompress
      const unsigned char headertype = input.get();
  
      for (const auto& formatHandler : format_handlers2) {
        for (const auto& formatHandlerHeaderByte : formatHandler->get_header_bytes()) {
          if (headertype == formatHandlerHeaderByte) {
            auto format_hdr_data = formatHandler->read_format_header(input, header1, formatHandlerHeaderByte);
            formatHandler->write_pre_recursion_data(output, *format_hdr_data);

            const bool recursion_used = (header1 & std::byte{ 0b10000000 }) == std::byte{ 0b10000000 };

            OStreamLike* patched_output = &output;
            // If there are penalty_bytes we get a patched ostream which will patch the needed bytes transparently while writing to the ostream
            std::unique_ptr<PenaltyBytesPatchedOStream> patched_ostream{};
            if (!format_hdr_data->penalty_bytes.empty()) {
              patched_ostream = std::make_unique<PenaltyBytesPatchedOStream>(&output, &format_hdr_data->penalty_bytes);
              patched_output = patched_ostream.get();
            }

            auto recurse_recompressor = PrecompPcfRecompressor(precomp_tools, format_handlers, format_handlers2);
            MemVecIOStream recurse_tmp;
            std::vector<std::byte> recursion_out_buf;
            recursion_out_buf.resize(CHUNK);
            auto recompressor = formatHandler->make_recompressor(*format_hdr_data, formatHandlerHeaderByte, precomp_tools);

            bool is_finalize_stream = false;
            while (true) {
              const auto data_hdr = static_cast<std::byte>(input.get());
              const bool last_block = (data_hdr & std::byte{ 0b10000000 }) == std::byte{ 0b10000000 };
              const bool finish_stream = (data_hdr & std::byte{ 0b00000001 }) == std::byte{ 0b00000001 };
              if (last_block && !finish_stream) {
                // end stream finish marker, its a Partial Stream. We already outputted all the data that could be recompressed from the Partial Stream
                // so we are done here.
                break;
              }

              recompressor->read_extra_block_header_data(input);
              const auto data_block_size = fin_fget_vlint(input);
              
              PrecompProcessorReturnCode retval;
              if (finish_stream && !last_block) {
                throw PrecompError(ERR_DURING_RECOMPRESSION);  // trying to finish stream when there are more blocks coming, no-go, something's wrong here, bail
              }

              recompressor->avail_in = 0;
              auto remaining_block_bytes = data_block_size;

              while (true) {
                if (recompressor->avail_in == 0 && remaining_block_bytes > 0) {
                  const auto amt_to_read = std::min<long long>(remaining_block_bytes, CHUNK);
                  input.read(reinterpret_cast<char*>(in_buf.data()), amt_to_read);
                  if (input.gcount() != amt_to_read) throw PrecompError(ERR_DURING_RECOMPRESSION);
                  remaining_block_bytes -= amt_to_read;
                  is_finalize_stream = finish_stream && remaining_block_bytes == 0;

                  if (!recursion_used) {
                    recompressor->avail_in = amt_to_read;
                    recompressor->next_in = reinterpret_cast<std::byte*>(in_buf.data());
                  }
                  else {
                    recurse_tmp.seekp(0, std::ios_base::beg);
                    const PrecompProcessorReturnCode recursion_ret = process_and_exhaust_output(
                      recurse_recompressor, reinterpret_cast<std::byte*>(in_buf.data()), amt_to_read, recursion_out_buf,
                      false, // recompressing PCF does not need last block indicator because it has its own end of stream marker
                      [&recurse_tmp, &recursion_out_buf] (uint32_t data_recompressed) {
                        recurse_tmp.write(reinterpret_cast<const char*>(recursion_out_buf.data()), data_recompressed);
                        return true;
                      }
                    );
                    if (recursion_ret == PP_ERROR || (recursion_ret != PP_STREAM_END && is_finalize_stream)) {
                      throw PrecompError(ERR_DURING_RECOMPRESSION);
                    }
                    recompressor->avail_in = recurse_tmp.tellp();
                    recompressor->next_in = const_cast<std::byte*>(recurse_tmp.vector().data());
                  }
                }

                recompressor->avail_out = CHUNK;
                recompressor->next_out = reinterpret_cast<std::byte*>(out_buf.data());

                retval = recompressor->process(is_finalize_stream);
                if (retval == PP_ERROR || (retval == PP_STREAM_END && !last_block)) {
                  // Error or premature end!
                  throw PrecompError(ERR_DURING_RECOMPRESSION);
                }

                // If we read all the block and exhausted all data that could be inputted or outputted, go to the next block
                if (remaining_block_bytes == 0 && recompressor->avail_in == 0 && recompressor->avail_out == CHUNK) {
                  break;
                }

                if (recompressor->avail_out != CHUNK) {
                  patched_output->write(reinterpret_cast<char*>(out_buf.data()), CHUNK - recompressor->avail_out);
                }

                if (retval == PP_STREAM_END) break;
              }
              
              if (last_block) break;
            }

            handlerFound = true;
            break;
          }
        }
        if (handlerFound) break;
      }
      for (const auto& formatHandler : format_handlers) {
        if (handlerFound) break;
        for (const auto& formatHandlerHeaderByte: formatHandler->get_header_bytes()) {
          if (headertype == formatHandlerHeaderByte) {
            auto format_hdr_data = formatHandler->read_format_header(input, header1, formatHandlerHeaderByte);
            formatHandler->write_pre_recursion_data(output, *format_hdr_data);

            OStreamLike* patched_output = &output;
            // If there are penalty_bytes we get a patched ostream which will patch the needed bytes transparently while writing to the ostream
            std::unique_ptr<PenaltyBytesPatchedOStream> patched_ostream{};
            if (!format_hdr_data->penalty_bytes.empty()) {
              patched_ostream = std::make_unique<PenaltyBytesPatchedOStream>(&output, &format_hdr_data->penalty_bytes);
              patched_output = patched_ostream.get();
            }

            if (format_hdr_data->recursion_used) {
              auto recurse_passthrough_input = recursion_decompress(input, *patched_ostream, precomp_tools, format_handlers, format_handlers2, format_hdr_data->recursion_data_size);
              formatHandler->recompress(*recurse_passthrough_input, *patched_output, *format_hdr_data, formatHandlerHeaderByte);
              //recurse_passthrough_input->get_recursion_return_code();
            }
            else {
              formatHandler->recompress(input, *patched_output, *format_hdr_data, formatHandlerHeaderByte);
            }
            handlerFound = true;
            break;
          }
        }
        if (handlerFound) break;
      }
    }
    if (!handlerFound) throw PrecompError(ERR_DURING_RECOMPRESSION);
  
    fin_pos = input.tellg();
  }

  return RETURN_SUCCESS;
}

int decompress_file(
  IStreamLike& input,
  OStreamLike& output,
  Tools& precomp_tools,
  const std::vector<std::unique_ptr<PrecompFormatHandler>>& format_handlers,
  const std::vector<std::unique_ptr<PrecompFormatHandler2>>& format_handlers2
)
{
  return wrap_with_exception_catch([&]() { return decompress_file_impl(input, output, precomp_tools, format_handlers, format_handlers2); });
}

void read_header(Precomp& precomp_mgr) {
  if (precomp_mgr.statistics.header_already_read) throw std::runtime_error("Attempted to read the input stream header twice");
  unsigned char hdr[3];
  precomp_mgr.fin->read(reinterpret_cast<char*>(hdr), 3);
  if ((hdr[0] == 'P') && (hdr[1] == 'C') && (hdr[2] == 'F')) {
  } else {
    throw PrecompError(ERR_NO_PCF_HEADER);
  }

  precomp_mgr.fin->read(reinterpret_cast<char*>(hdr), 3);
  if ((hdr[0] == V_MAJOR) && (hdr[1] == V_MINOR) && (hdr[2] == V_MINOR2)) {
  } else {
    throw PrecompError(
      ERR_PCF_HEADER_INCOMPATIBLE_VERSION,
      make_cstyle_format_string("PCF version info: %i.%i.%i\n", hdr[0], hdr[1], hdr[2])
    );
  }

  precomp_mgr.fin->read(reinterpret_cast<char*>(hdr), 1);
  if (hdr[0] != 0) throw PrecompError(
    ERR_PCF_HEADER_INCOMPATIBLE_VERSION,
    "OTF compression no longer supported, use original Precomp and use the -nn conversion option to get an uncompressed Precomp stream that should work here"
  );

  std::string header_filename;
  char c;
  do {
    c = precomp_mgr.fin->get();
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

bool verify_precompressed_result(Precomp& precomp_mgr, IStreamLike& input, const std::unique_ptr<precompression_result>& result, long long& input_file_pos) {
    // Dump precompressed data including format headers
    // TODO: We should be able to use a PassthroughStream here to avoid having to dump a temporary file, I tried it and it failed, didn't want to spend more
    // time on it right now, but might be worth it to do this optimization later.
    std::unique_ptr<PrecompTmpFile> verify_tmp_precompressed = std::make_unique<PrecompTmpFile>();
    auto verify_precompressed_filename = precomp_mgr.get_tempfile_name("verify_precompressed");
    verify_tmp_precompressed->open(verify_precompressed_filename, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
    result->dump_to_outfile(*verify_tmp_precompressed);
    verify_tmp_precompressed->reopen();

    auto recompressor = PrecompPcfRecompressor(precomp_mgr.format_handler_tools, precomp_mgr.get_format_handlers(), precomp_mgr.get_format_handlers2());

    auto sha1_ostream = Sha1Ostream();

    std::function output_chunk_callback = [&](std::vector<std::byte>& out_buf, uint32_t current_chunk_size, bool) {
      sha1_ostream.write(reinterpret_cast<char*>(out_buf.data()), current_chunk_size);
      return true;
    };

    const auto [processor_result, _] = process_from_input_and_output_chunks(recompressor, *verify_tmp_precompressed, std::move(output_chunk_callback));
    if (processor_result != PP_STREAM_END) return false;

    // Okay at this point we supposedly recompressed the input data successfully, verify data exactly matches original
    const auto recompressed_size = sha1_ostream.tellp();
    const auto recompressed_data_sha1 = sha1_ostream.get_digest();

    // Validate that the recompressed_size is what we expect
    if (recompressed_size != result->complete_original_size()) return false;

    input.seekg(input_file_pos, std::ios_base::beg);
    auto original_data_view = IStreamLikeView(&input, recompressed_size + input_file_pos);
    const auto original_data_sha1 = calculate_sha1(original_data_view, 0);

    if (original_data_sha1 != recompressed_data_sha1) return false;
    return true;
}

recursion_result recursion_compress(Precomp& precomp_mgr, long long decompressed_bytes, IStreamLike& tmpfile, std::string out_filename, unsigned int recursion_depth) {
  recursion_result tmp_r;
  tmp_r.success = false;

  const auto new_recursion_depth = recursion_depth + 1;
  if (new_recursion_depth > precomp_mgr.switches.max_recursion_depth) {
    precomp_mgr.statistics.max_recursion_depth_reached = true;
    return tmp_r;
  }

  auto recursion_input = std::make_unique<IStreamLikeView>(&tmpfile, decompressed_bytes);
  // The recursion input here is always backed by a PrecompTmpFile or a memory stream and is thus seekable, so we just use a FakeBufferedIStream
  auto recursion_input_fake_buffered = FakeBufferedIStream(recursion_input.get());

  tmp_r.file_name = out_filename;
  auto fout = new std::ofstream();
  fout->open(tmp_r.file_name.c_str(), std::ios_base::out | std::ios_base::binary);
  auto recursion_output = std::unique_ptr<ObservableOStream>(new ObservableWrappedOStream(fout, true));

  precomp_mgr.ignore_offsets.resize(precomp_mgr.ignore_offsets.size() + 1);
  print_to_log(PRECOMP_DEBUG_LOG, "Recursion start - new recursion depth %i\n", new_recursion_depth);
  const auto ret_code = compress_file(precomp_mgr, recursion_input_fake_buffered, *recursion_output, new_recursion_depth);
  if (ret_code != RETURN_SUCCESS && ret_code != RETURN_NOTHING_DECOMPRESSED) throw PrecompError(ret_code);
  tmp_r.success = ret_code == RETURN_SUCCESS;

  // TODO CHECK: Delete ctx?

  precomp_mgr.ignore_offsets.pop_back();
  fout->close();

  if (tmp_r.success) {
    print_to_log(PRECOMP_DEBUG_LOG, "Recursion streams found\n");
  } else {
    print_to_log(PRECOMP_DEBUG_LOG, "No recursion streams found\n");
  }
  print_to_log(PRECOMP_DEBUG_LOG, "Recursion end - back to recursion depth %i\n", recursion_depth);

  if (!tmp_r.success) {
    remove(tmp_r.file_name.c_str());
    tmp_r.file_name = "";
  } else {
    if (new_recursion_depth > precomp_mgr.statistics.max_recursion_depth_used) {
      precomp_mgr.statistics.max_recursion_depth_used = new_recursion_depth;
    }
    // get recursion file size
    tmp_r.file_length = std::filesystem::file_size(tmp_r.file_name.c_str());
  }

  return tmp_r;
}

std::unique_ptr<RecursionPasstroughStream> recursion_decompress(IStreamLike& input,
  OStreamLike& output,
  Tools& precomp_tools,
  const std::vector<std::unique_ptr<PrecompFormatHandler>>& format_handlers,
  const std::vector<std::unique_ptr<PrecompFormatHandler2>>& format_handlers2,
  long long recursion_data_length
) {
  const auto original_pos = input.tellg();

  long long recursion_end_pos = original_pos + recursion_data_length;
  // We create a view of the recursion data from the input stream, this way the recursive call to decompress can work as if it were working with a copy of the data
  // on a temporary file, processing it from pos 0 to EOF, while actually only reading from original_pos to recursion_end_pos
  auto fin_view = std::make_unique<IStreamLikeView>(&input, recursion_end_pos);
  std::unique_ptr<RecursionPasstroughStream> passthrough = std::make_unique<RecursionPasstroughStream>(precomp_tools, std::move(fin_view), format_handlers, format_handlers2);
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
    if (input.bad()) break;
    v += (((long long)(c & 127)) << s);
    s += 7;
    o = (o + 1) << 7;
    if (input.eof()) break;
  }
  return v + o + (((long long)c) << s);
}

std::tuple<long long, std::vector<std::tuple<uint32_t, char>>> compare_files_penalty(const std::function<void()>& progress_callback, IStreamLike& original, IStreamLike& candidate, long long original_size) {
  unsigned char input_bytes1[COMP_CHUNK];
  unsigned char input_bytes2[COMP_CHUNK];
  uint32_t pos = 0;
  bool endNow = false;

  std::vector<std::tuple<uint32_t, char>> penalty_bytes;

  do {
    progress_callback();

    original.read(reinterpret_cast<char*>(input_bytes1), COMP_CHUNK);
    long long original_chunk_size = original.gcount();
    candidate.read(reinterpret_cast<char*>(input_bytes2), COMP_CHUNK);
    long long candidate_chunk_size = candidate.gcount();

    uint32_t i;
    for (i = 0; i < original_chunk_size && pos < original_size; i++, pos++) {
      // if we ran out of candidate, it might still be possible to recover the original file if we can fill the whole end with penalty bytes,
      // and of course if we find a mismatching byte we must patch it with a penalty byte
      if (i + 1 > candidate_chunk_size || input_bytes1[i] != input_bytes2[i]) {  // should not be out of bounds on input_bytes2 because of short-circuit evaluation
        // if penalty_bytes_size gets too large, stop
        // penalty_bytes will be larger than 1/6th of stream? bail, why 1/6th? beats me, but this is roughly equivalent to what Precomp v0.4.8 was doing,
        // only much, much simpler
        if (static_cast<double>(penalty_bytes.size() + 1) >= static_cast<double>(original_size) * (1.0 / 6)) {
          endNow = true;
          break;
        }

        // stop, if penalty_bytes len gets too big
        if ((penalty_bytes.size() + 1) * 5 >= MAX_PENALTY_BYTES) {  // 4 bytes = position uint32, 1 byte = patch byte
          endNow = true;
          break;
        }

        // add the penalty byte
        penalty_bytes.emplace_back(pos, input_bytes1[i]);
        if (endNow) break;
      }
    }
    if (pos == original_size) endNow = true;
  } while (!endNow);

  return { pos, penalty_bytes };
}

// C API STUFF
Precomp* PrecompCreate() { return new Precomp(); }
void PrecompSetProgressCallback(Precomp* precomp_mgr, void(*callback)(float)) { precomp_mgr->set_progress_callback(callback); }
void PrecompDestroy(Precomp* precomp_mgr) { delete precomp_mgr; }
CSwitches* PrecompGetSwitches(Precomp* precomp_mgr) { return &precomp_mgr->switches; }
void PrecompSwitchesSetIgnoreList(CSwitches* precomp_switches, const long long* ignore_pos_list, size_t ignore_post_list_count) {
  auto& ignore_pos_queue = reinterpret_cast<Switches*>(precomp_switches)->ignore_pos_queue;
  for (auto ignore_pos : std::span<const long long>(ignore_pos_list, ignore_pos_list + ignore_post_list_count)) {
    ignore_pos_queue.emplace(ignore_pos);
  }
}
CResultStatistics* PrecompGetResultStatistics(Precomp* precomp_mgr) {
  // update counts in a CResultStatistics accessible way before returning it
  precomp_mgr->statistics.decompressed_streams_count = precomp_mgr->statistics.get_total_detected_count();
  precomp_mgr->statistics.precompressed_streams_count = precomp_mgr->statistics.get_total_precompressed_count();
  return &precomp_mgr->statistics;
}

void PrecompPrintResults(Precomp* precomp_mgr) { precomp_mgr->statistics.print_results(); }

int PrecompPrecompress(Precomp* precomp_mgr) {
  std::unique_ptr<IStreamLike> fin = std::move(precomp_mgr->fin);
  auto buffered_istream = std::make_unique<BufferedIStream>(fin.get(), precomp_mgr->get_tempfile_name("input_buffer"));
  return compress_file(*precomp_mgr, *buffered_istream, *precomp_mgr->fout, 0);
}

int PrecompRecompress(Precomp* precomp_mgr) {
  if (!precomp_mgr->statistics.header_already_read) read_header(*precomp_mgr);
  precomp_mgr->init_format_handlers(true);
  return decompress_file(*precomp_mgr->fin, *precomp_mgr->fout, precomp_mgr->format_handler_tools, precomp_mgr->get_format_handlers(), precomp_mgr->get_format_handlers2());
}

int PrecompReadHeader(Precomp* precomp_mgr, bool seek_to_beg) {
  if (seek_to_beg) precomp_mgr->fin->seekg(0, std::ios_base::beg);
  try {
    read_header(*precomp_mgr);
  }
  catch (const PrecompError& err) {
    return err.error_code;
  }
  return 0;
}
