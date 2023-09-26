#include "deflate.h"

#include "contrib/preflate/preflate.h"
#include "contrib/zlib/zlib.h"

#include <cstddef>
#include <memory>
#include <sstream>

std::byte make_deflate_pcf_hdr_flags(const recompress_deflate_result& rdres) {
  return std::byte{ 0b1 } | std::byte{ 0b10 };
}

void deflate_precompression_result::dump_recon_data_to_outfile(OStreamLike& outfile) const {
  fout_fput_vlint(outfile, rdres.recon_data.size());
  outfile.write(reinterpret_cast<const char*>(rdres.recon_data.data()), rdres.recon_data.size());
}
deflate_precompression_result::deflate_precompression_result(SupportedFormats format) : precompression_result(format) {}
void deflate_precompression_result::dump_header_to_outfile(OStreamLike& outfile) const {
  // write compressed data header
  outfile.put(static_cast<char>(make_deflate_pcf_hdr_flags(rdres) | flags | (recursion_used ? std::byte{ 0b10000000 } : std::byte{ 0b0 })));
  outfile.put(format);
  fout_fput_vlint(outfile, zlib_header.size());
  if (!inc_last_hdr_byte) {
    outfile.write(reinterpret_cast<char*>(const_cast<unsigned char*>(zlib_header.data())), zlib_header.size());
  }
  else {
    outfile.write(reinterpret_cast<char*>(const_cast<unsigned char*>(zlib_header.data())), zlib_header.size() - 1);
    outfile.put(zlib_header[zlib_header.size() - 1] + 1);
  }
}
void deflate_precompression_result::dump_to_outfile(OStreamLike& outfile) const {
  dump_header_to_outfile(outfile);
  dump_penaltybytes_to_outfile(outfile);
  dump_recon_data_to_outfile(outfile);
  dump_stream_sizes_to_outfile(outfile);
  dump_precompressed_data_to_outfile(outfile);
}

void fin_fget_recon_data(IStreamLike& input, recompress_deflate_result& rdres) {
  size_t sz = fin_fget_vlint(input);
  rdres.recon_data.resize(sz);
  input.read(reinterpret_cast<char*>(rdres.recon_data.data()), rdres.recon_data.size());

  rdres.compressed_stream_size = fin_fget_vlint(input);
  rdres.uncompressed_stream_size = fin_fget_vlint(input);
}

class OwnIStream : public InputStream {
public:
  explicit OwnIStream(IStreamLike* f) : _f(f), _eof(false) {}

  [[nodiscard]] bool eof() const override {
    return _eof;
  }
  size_t read(unsigned char* buffer, const size_t size) override {
    _f->read(reinterpret_cast<char*>(buffer), size);
    size_t res = _f->gcount();
    _eof |= res < size;
    return res;
  }
private:
  IStreamLike* _f;
  bool _eof;
};
class OwnOStream : public OutputStream {
public:
  explicit OwnOStream(OStreamLike* f) : _f(f) {}

  size_t write(const unsigned char* buffer, const size_t size) override {
    _f->write(reinterpret_cast<char*>(const_cast<unsigned char*>(buffer)), size);
    return _f->bad() ? 0 : size;
  }
private:
  OStreamLike* _f;
};
class UncompressedOutStream : public OutputStream {
  uint64_t _written = 0;
  bool _in_memory = true;

public:
  OStreamLike& ftempout;
  Precomp* precomp_mgr;
  std::vector<unsigned char> decomp_io_buf;

  UncompressedOutStream(OStreamLike& tmpfile, Precomp* precomp_mgr) : ftempout(tmpfile), precomp_mgr(precomp_mgr) {}
  ~UncompressedOutStream() override = default;

  size_t write(const unsigned char* buffer, const size_t size) override {
    precomp_mgr->call_progress_callback();
    if (_in_memory) {
      auto decomp_io_buf_ptr = decomp_io_buf.data();
      if (_written + size >= MAX_IO_BUFFER_SIZE) {
        _in_memory = false;
        auto memstream = memiostream::make(decomp_io_buf_ptr, decomp_io_buf_ptr + _written);
        fast_copy(*memstream, ftempout, _written);
        decomp_io_buf.clear();
      }
      else {
        decomp_io_buf.resize(decomp_io_buf.size() + size);
        memcpy(decomp_io_buf.data() + _written, buffer, size);
        _written += size;
        return size;
      }
    }
    _written += size;
    ftempout.write(reinterpret_cast<char*>(const_cast<unsigned char*>(buffer)), size);
    return ftempout.bad() ? 0 : size;
  }

  [[nodiscard]] uint64_t written() const {
    return _written;
  }

  [[nodiscard]] bool in_memory() const {
    return _in_memory;
  }

};

class PreflateProcessorBase : public ProcessorAdapter {
protected:
  class ProcessorInputStream : public InputStream {
  private:
    IStreamLike* istream;

  public:
    explicit ProcessorInputStream(IStreamLike* istream_) : istream(istream_) {}

    [[nodiscard]] bool eof() const override {
      return istream->eof();
    }
    size_t read(unsigned char* buffer, const size_t size) override {
      istream->read(reinterpret_cast<char*>(buffer), size);
      return istream->gcount();
    }
  };

  class ProcessorOutStream : public OutputStream {
    OStreamLike* ostream;

  public:
    ProcessorOutStream(OStreamLike* ostream_) : ostream(ostream_) {}

    size_t write(const unsigned char* buffer, const size_t size) override {
      ostream->write(reinterpret_cast<const char*>(buffer), size);
      return size;
    }
  };

  ProcessorInputStream preflate_input;
  ProcessorOutStream preflate_output;
public:
  PreflateProcessorBase(uint32_t* avail_in, std::byte** next_in, uint32_t* avail_out, std::byte** next_out)
    : ProcessorAdapter(avail_in, next_in, avail_out, next_out), preflate_input(input_stream.get()), preflate_output(output_stream.get()) {

  }
};

class DeflatePrecompressor : public PrecompFormatPrecompressor, public PreflateProcessorBase {
public:
  recompress_deflate_result result{};
  uint64_t compressed_stream_size = 0;

  DeflatePrecompressor(const std::span<unsigned char>& buffer, const std::function<void()>& _progress_callback)
    : PrecompFormatPrecompressor(buffer, _progress_callback), PreflateProcessorBase(&avail_in, &next_in, &avail_out, &next_out) {
    process_func = [this]() -> bool {
      result.accepted = preflate_decode(
        preflate_output,
        result.recon_data,
        compressed_stream_size,
        preflate_input,
        []() {},
        1 << 21
      );
      return result.accepted;
      };
  }

  PrecompProcessorReturnCode process() override {
    return PreflateProcessorBase::process();
  }
};

class DeflateRecompressor : public PrecompFormatRecompressor, public PreflateProcessorBase {
public:
  DeflateRecompressor(const DeflateFormatHeaderData& precomp_hdr_data, const std::function<void()>& _progress_callback)
    : PrecompFormatRecompressor(precomp_hdr_data, _progress_callback), PreflateProcessorBase(&avail_in, &next_in, &avail_out, &next_out) {
    process_func = [this, &recon_data = precomp_hdr_data.rdres.recon_data, uncompressed_stream_size = precomp_hdr_data.rdres.uncompressed_stream_size]() -> bool {
      return preflate_reencode(preflate_output, recon_data, preflate_input, uncompressed_stream_size, progress_callback);
      };
  }

  PrecompProcessorReturnCode process() override {
    return PreflateProcessorBase::process();
  }
};

template <class T>
PrecompProcessorReturnCode preflate_processor_full_process(T& processor, IStreamLike& istream, OStreamLike& ostream, long long& input_stream_size, long long& processed_stream_size) {
  PrecompProcessorReturnCode ret = PP_OK;
  std::vector<std::byte> out_buf{};
  out_buf.resize(CHUNK);
  processor.next_out = out_buf.data();
  processor.avail_out = CHUNK;
  std::vector<std::byte> in_buf{};
  in_buf.resize(CHUNK);
  processor.next_in = in_buf.data();
  processor.avail_in = 0;
  while (true) {
    // There might be some data remaining on the in_buf from a previous iteration that wasn't consumed yet, we ensure we don't lose it
    if (processor.avail_in > 0) {
      // The remaining data at the end is moved to the start of the array and we will complete it up so that a full CHUNK of data is available for the iteration
      std::memmove(in_buf.data(), processor.next_in, processor.avail_in);
    }
    const auto in_buf_ptr = reinterpret_cast<char*>(in_buf.data() + processor.avail_in);
    const auto read_amt = CHUNK - processor.avail_in;
    std::streamsize gcount = 0;
    if (read_amt > 0) {
      istream.read(in_buf_ptr, read_amt);
      gcount = istream.gcount();
    }

    processor.avail_in += gcount;
    processor.next_in = in_buf.data();
    if (istream.bad()) break;

    const auto avail_in_before_process = processor.avail_in;
    ret = processor.process();
    if (ret != PP_OK && ret != PP_STREAM_END) break;

    // This should mostly be for when the stream ends, we might have read extra data beyond the end of it, which will not have been consumed by the process
    // and shouldn't be counted towards the stream's size
    input_stream_size += (avail_in_before_process - processor.avail_in);

    const auto decompressed_chunk_size = CHUNK - processor.avail_out;
    processed_stream_size += decompressed_chunk_size;
    ostream.write(reinterpret_cast<char*>(out_buf.data()), decompressed_chunk_size);

    if (ostream.bad()) break;
    if (ret == PP_STREAM_END) break;  // maybe we should also check fin for eof? we could support partial/broken BZip2 streams

    if (read_amt > 0 && gcount == 0 && decompressed_chunk_size == 0) {
      // No more data will ever come and we already flushed all the output data processed, notify processor so it can finish
      processor.force_input_eof = true;
    }
    // TODO: What if I feed the processor a full CHUNK and it outputs nothing? Should we quit? Should processors be able to advertise a chunk size they need?

    processor.next_out = out_buf.data();
    processor.avail_out = CHUNK;
  }
  return ret;
}

recompress_deflate_result try_recompression_deflate(Precomp& precomp_mgr, IStreamLike& file, long long file_deflate_stream_pos, OStreamLike& tmpfile) {
  file.seekg(file_deflate_stream_pos, std::ios_base::beg);

  std::vector<unsigned char> fake_checkbuf{};
  auto precompressor = std::make_unique<DeflatePrecompressor>(fake_checkbuf, [&precomp_mgr]() { precomp_mgr.call_progress_callback(); });

  long long compressed_stream_size = 0;
  long long decompressed_stream_size = 0;
  bool partial_stream = false;

  auto ret = preflate_processor_full_process(*precompressor, file, tmpfile, compressed_stream_size, decompressed_stream_size);

  if (ret == PP_ERROR) {
    // something here I guess?
  }
  else if (ret == PP_OK) {
    partial_stream = true;
  }

  recompress_deflate_result result = std::move(precompressor->result);
  result.compressed_stream_size = precompressor->compressed_stream_size;
  result.uncompressed_stream_size = decompressed_stream_size;

  return result;
}

void debug_deflate_detected(RecursionContext& context, const recompress_deflate_result& rdres, const char* type, long long deflate_stream_pos) {
  if (PRECOMP_VERBOSITY_LEVEL < PRECOMP_DEBUG_LOG) return;
  std::stringstream ss;
  ss << "Possible zLib-Stream within " << type << " found at position " << deflate_stream_pos << std::endl;
  ss << "Compressed size: " << rdres.compressed_stream_size << std::endl;
  ss << "Can be decompressed to " << rdres.uncompressed_stream_size << " bytes" << std::endl;

  if (rdres.accepted) {
    ss << "Non-ZLIB reconstruction data size: " << rdres.recon_data.size() << " bytes" << std::endl;
  }

  print_to_log(PRECOMP_DEBUG_LOG, ss.str());
}

static uint64_t sum_compressed = 0, sum_uncompressed = 0, sum_recon = 0, sum_expansion = 0;
void debug_sums(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, const recompress_deflate_result& rdres) {
  if (PRECOMP_VERBOSITY_LEVEL < PRECOMP_DEBUG_LOG) return;
  sum_compressed += rdres.compressed_stream_size;
  sum_uncompressed += rdres.uncompressed_stream_size;
  sum_expansion += rdres.uncompressed_stream_size - rdres.compressed_stream_size;
  sum_recon += rdres.recon_data.size();
  print_to_log(PRECOMP_DEBUG_LOG, "deflate sums: c %I64d, u %I64d, x %I64d, r %I64d, i %I64d, o %I64d\n",
    sum_compressed, sum_uncompressed, sum_expansion, sum_recon, (uint64_t)precompressed_input.tellg(), (uint64_t)recompressed_stream.tellp());
}

std::unique_ptr<deflate_precompression_result> try_decompression_deflate_type(Precomp& precomp_mgr, unsigned& dcounter, unsigned& rcounter, SupportedFormats type,
  const unsigned char* hdr, const unsigned int hdr_length, long long deflate_stream_pos, const bool inc_last, const char* debugname, std::string tmp_filename) {
  std::unique_ptr<PrecompTmpFile> tmpfile = std::make_unique<PrecompTmpFile>();
  tmpfile->open(tmp_filename, std::ios_base::in | std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
  std::unique_ptr<deflate_precompression_result> result = std::make_unique<deflate_precompression_result>(type);

  // try to decompress at current position
  recompress_deflate_result rdres = try_recompression_deflate(precomp_mgr, *precomp_mgr.ctx->fin, deflate_stream_pos, *tmpfile);
  tmpfile->close();

  if (rdres.uncompressed_stream_size > 0) { // seems to be a zLib-Stream
    precomp_mgr.statistics.decompressed_streams_count++;
    dcounter++;

    debug_deflate_detected(*precomp_mgr.ctx, rdres, debugname, deflate_stream_pos);

    if (rdres.accepted) {
      result->success = rdres.accepted;
      result->original_size = rdres.compressed_stream_size;
      result->precompressed_size = rdres.uncompressed_stream_size;
      precomp_mgr.statistics.recompressed_streams_count++;
      rcounter++;

      precomp_mgr.ctx->non_zlib_was_used = true;

      debug_sums(*precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, rdres);

#if 0
      // Do we really want to allow uncompressed streams that are smaller than the compressed
      // ones? (It makes sense if the uncompressed stream contains a JPEG, or something similar.
      if (rdres.uncompressed_stream_size <= rdres.compressed_stream_size && !r.success) {
        precomp_mgr.statistics.recompressed_streams_count--;
        compressed_data_found = false;
        return result;
      }
#endif

      result->inc_last_hdr_byte = inc_last;
      result->zlib_header = std::vector(hdr, hdr + hdr_length);
      if (!rdres.uncompressed_stream_mem.empty()) {
          //rdres.uncompressed_stream_mem.resize(rdres.uncompressed_stream_size);
          auto memstream = memiostream::make(std::move(rdres.uncompressed_stream_mem));
          result->precompressed_stream = std::move(memstream);
      }
      else {
          tmpfile->open(tmpfile->file_path, std::ios_base::in | std::ios_base::binary);
          result->precompressed_stream = std::move(tmpfile);
      }

      result->rdres = std::move(rdres);
    }
    else {
      if (type == D_SWF && precomp_mgr.is_format_handler_active(D_RAW)) precomp_mgr.ctx->ignore_offsets[D_RAW].emplace(deflate_stream_pos - 2);
      if (type != D_BRUTE && precomp_mgr.is_format_handler_active(D_BRUTE)) precomp_mgr.ctx->ignore_offsets[D_BRUTE].emplace(deflate_stream_pos);
      print_to_log(PRECOMP_DEBUG_LOG, "No matches\n");
    }
  }
  return result;
}

bool check_inflate_result(
    DeflateHistogramFalsePositiveDetector& falsePositiveDetector, uintptr_t current_input_id, const std::span<unsigned char> checkbuf_span,
    int windowbits, const long long deflate_stream_pos, bool use_brute_parameters
) {
  // first check BTYPE bits, skip 11 ("reserved (error)")
  const int btype = (*checkbuf_span.data() & 0x07) >> 1;
  if (btype == 3) return false;
  // skip BTYPE = 00 ("uncompressed") only in brute mode, because these can be useful for recursion
  // and often occur in combination with static/dynamic BTYPE blocks
  if (use_brute_parameters) {
    if (btype == 0) return false;

    // use a histogram to see if the first 64 bytes are too redundant for a deflate stream,
    // if a byte is present 8 or more times, it's most likely not a deflate stream
    // and could slow down the process (e.g. repeated patterns of "0xEBE1F1" or "0xEBEBEBFF"
    // did this before)
    int maximum = 0, used = 0;
    auto data_ptr = checkbuf_span.data();
    int i, j;
    if (current_input_id != falsePositiveDetector.prev_input_id || falsePositiveDetector.prev_deflate_stream_pos + 1 != deflate_stream_pos) {
      // if we are not at the next pos from the last run, we need to remake the whole histogram from scratch
      memset(&falsePositiveDetector.histogram[0], 0, sizeof(falsePositiveDetector.histogram));
      i = 0;
      j = 0;
    }
    else {
      // if we are at the next pos from the last run, we just remove the data from the last run's first byte and pick up the histogram from there
      i = falsePositiveDetector.prev_i == 4 ? falsePositiveDetector.prev_i - 1 : falsePositiveDetector.prev_i;
      j = 63;
      data_ptr += 64 * i; // adjust the data_ptr to point to the byte we determined we need to continue computing the histogram from
      const bool prev_first_byte_repeated = falsePositiveDetector.histogram[falsePositiveDetector.prev_first_byte] > 1;
      falsePositiveDetector.histogram[falsePositiveDetector.prev_first_byte] -= 1;  // remove counting of the last byte
      maximum = *std::max_element(falsePositiveDetector.histogram, falsePositiveDetector.histogram + 256);
      used = falsePositiveDetector.prev_used;
      // if the first byte was repeated the used count would have been increased anyways, so we don't subtract, but if not repeated, that's one less for the used chars count
      if (!prev_first_byte_repeated) used -= 1;
    }

    for (; i < 4; i++, data_ptr += 64) {
      for (; j < 64; j++) {
        int* freq = &falsePositiveDetector.histogram[*(data_ptr + j)];
        used += ((*freq) == 0);
        maximum += (++(*freq)) > maximum;
      }
      if (maximum >= ((12 + i) << i) || used * (7 - (i + (i / 2))) < (i + 1) * 64)
        break;
      if (i != 3) j = 0;
    }

    // set vars to be able to pick up the histogram for the next position
    falsePositiveDetector.prev_input_id = current_input_id;
    falsePositiveDetector.prev_deflate_stream_pos = deflate_stream_pos;
    falsePositiveDetector.prev_first_byte = *checkbuf_span.data();
    falsePositiveDetector.prev_maximum = maximum;
    falsePositiveDetector.prev_used = used;
    falsePositiveDetector.prev_i = i;
    
    if (i < 3 || j < 63) {  // if we did break before the end then we found enough duplication to consider this a false positive stream
      return false;
    }
  }

  int ret;
  unsigned have = 0;
  z_stream strm;

  /* allocate inflate state */
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  strm.avail_in = 0;
  strm.next_in = Z_NULL;
  ret = inflateInit2(&strm, windowbits);
  if (ret != Z_OK)
    return false;

  strm.avail_in = 2048;
  strm.next_in = checkbuf_span.data();

  /* run inflate() on input until output buffer not full */
  do {
    strm.avail_out = CHUNK;
    strm.next_out = falsePositiveDetector.tmp_out;

    ret = inflate(&strm, Z_NO_FLUSH);
    switch (ret) {
    case Z_NEED_DICT:
      ret = Z_DATA_ERROR;
    case Z_DATA_ERROR:
    case Z_MEM_ERROR:
      (void)inflateEnd(&strm);
      return false;
    }

    have += CHUNK - strm.avail_out;
  } while (strm.avail_out == 0);


  /* clean up and return */
  (void)inflateEnd(&strm);
  switch (ret) {
  case Z_OK:
    return true;
  case Z_STREAM_END:
    // Skip short streams - most likely false positives
    unsigned less_than_skip = 32;
    if (use_brute_parameters) less_than_skip = 1024;
    return (have >= less_than_skip);
  }

  return false;
}

bool DeflateFormatHandler::quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) {
  return check_inflate_result(this->falsePositiveDetector, current_input_id, buffer, -15, original_input_pos, true);
}

std::unique_ptr<PrecompFormatHeaderData> DeflateFormatHandler::read_format_header(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) {
  auto fmt_hdr = std::make_unique<DeflateFormatHeaderData>();
  fmt_hdr->read_data(*context.fin, precomp_hdr_flags, false);
  return fmt_hdr;
}

std::unique_ptr<precompression_result> DeflateFormatHandler::attempt_precompression(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span, const long long original_input_pos) {
  return try_decompression_deflate_type(precomp_mgr,
    precomp_mgr.statistics.decompressed_brute_count, precomp_mgr.statistics.recompressed_brute_count,
    D_BRUTE, checkbuf_span.data(), 0, original_input_pos, false,
    "(brute mode)", precomp_mgr.get_tempfile_name("decomp_brute"));
}

bool try_reconstructing_deflate(IStreamLike& fin, OStreamLike& fout, const DeflateFormatHeaderData& precomp_hdr_data, const std::function<void()>& progress_callback) {
  long long precompressed_stream_size = 0;
  long long recompressed_stream_size = 0;
  auto recompressor = std::make_unique<DeflateRecompressor>(precomp_hdr_data, progress_callback);
  
  auto input_pos_before = fin.tellg();
  // Limit the input stream to the streams known size
  auto original_data_view = IStreamLikeView(&fin, input_pos_before + precomp_hdr_data.rdres.uncompressed_stream_size);
  auto retval = preflate_processor_full_process(*recompressor, original_data_view, fout, precompressed_stream_size, recompressed_stream_size);

  return retval == PP_STREAM_END;
}

size_t fread_skip(unsigned char* ptr, size_t size, size_t count, IStreamLike& stream, unsigned int frs_offset, unsigned int frs_line_len, unsigned int frs_skip_len) {
  size_t bytes_read = 0;
  unsigned int read_tmp;
  unsigned char frs_skipbuf[4];

  do {
    if ((count - bytes_read) >= (frs_line_len - frs_offset)) {
      if ((frs_line_len - frs_offset) > 0) {
        stream.read(reinterpret_cast<char*>(ptr + bytes_read), size * (frs_line_len - frs_offset));
        read_tmp = stream.gcount();
        if (read_tmp == 0) return bytes_read;
        bytes_read += read_tmp;
      }
      // skip padding bytes
      stream.read(reinterpret_cast<char*>(frs_skipbuf), size * frs_skip_len);
      read_tmp = stream.gcount();
      if (read_tmp == 0) return bytes_read;
      frs_offset = 0;
    }
    else {
      stream.read(reinterpret_cast<char*>(ptr + bytes_read), size * (count - bytes_read));
      read_tmp = stream.gcount();
      if (read_tmp == 0) return bytes_read;
      bytes_read += read_tmp;
      frs_offset += read_tmp;
    }
  } while (bytes_read < count);

  return bytes_read;
}

bool try_reconstructing_deflate_skip(IStreamLike& fin, OStreamLike& fout, const recompress_deflate_result& rdres, const size_t read_part, const size_t skip_part, const std::function<void()>& progress_callback) {
  std::vector<unsigned char> unpacked_output;
  unpacked_output.resize(rdres.uncompressed_stream_size);
  unsigned int frs_offset = 0;
  unsigned int frs_skip_len = skip_part;
  unsigned int frs_line_len = read_part;
  if ((int64_t)fread_skip(unpacked_output.data(), 1, rdres.uncompressed_stream_size, fin, frs_offset, frs_line_len, frs_skip_len) != rdres.uncompressed_stream_size) {
    return false;
  }
  OwnOStream os(&fout);
  return preflate_reencode(os, rdres.recon_data, unpacked_output, progress_callback);
}

void fin_fget_deflate_hdr(IStreamLike& input, recompress_deflate_result& rdres, const std::byte flags,
  unsigned char* hdr_data, unsigned& hdr_length,
  const bool inc_last_hdr_byte) {
  hdr_length = fin_fget_vlint(input);
  if (!inc_last_hdr_byte) {
    input.read(reinterpret_cast<char*>(hdr_data), hdr_length);
  }
  else {
    input.read(reinterpret_cast<char*>(hdr_data), hdr_length - 1);
    hdr_data[hdr_length - 1] = input.get() - 1;
  }
}

void fin_fget_deflate_rec(IStreamLike& precompressed_input, recompress_deflate_result& rdres, const std::byte flags, unsigned char* hdr, unsigned& hdr_length, const bool inc_last) {
  fin_fget_deflate_hdr(precompressed_input, rdres, flags, hdr, hdr_length, inc_last);
  fin_fget_recon_data(precompressed_input, rdres);
}

void debug_deflate_reconstruct(const recompress_deflate_result& rdres, const char* type, const unsigned hdr_length, const uint64_t rec_length) {
  if (PRECOMP_VERBOSITY_LEVEL < PRECOMP_DEBUG_LOG) return;
  std::stringstream ss;
  ss << "Decompressed data - " << type << std::endl;
  ss << "Header length: " << hdr_length << std::endl;
  ss << "Reconstruction data size: " << rdres.recon_data.size() << std::endl;
  if (rec_length > 0) {
    ss << "Recursion data length: " << rec_length << std::endl;
  }
  else {
    ss << "Recompressed length: " << rdres.compressed_stream_size << " - decompressed length: " << rdres.uncompressed_stream_size << std::endl;
  }
  print_to_log(PRECOMP_DEBUG_LOG, ss.str());
}

void DeflateFormatHeaderData::read_data(IStreamLike& precompressed_input, std::byte precomp_hdr_flags, bool inc_last_hdr_byte) {
  unsigned hdr_length;
  stream_hdr.resize(CHUNK);

  fin_fget_deflate_rec(precompressed_input, rdres, precomp_hdr_flags, stream_hdr.data(), hdr_length, inc_last_hdr_byte);
  stream_hdr.resize(hdr_length);
  if ((precomp_hdr_flags & std::byte{ 0b10000000 }) == std::byte{ 0b10000000 }) {
    recursion_data_size = fin_fget_vlint(precompressed_input);
  }
}

void recompress_deflate(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, DeflateFormatHeaderData& precomp_hdr_data, std::string filename, std::string type, const PrecompFormatHandler::Tools& tools) {
  bool ok;

  // write decompressed data
  ok = try_reconstructing_deflate(precompressed_input, recompressed_stream, precomp_hdr_data, tools.progress_callback);

  if (!ok) {
    throw PrecompError(ERR_DURING_RECOMPRESSION);
  }
}

void DeflateFormatHandler::recompress(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format, const Tools& tools) {
  auto& deflate_hdr_data = static_cast<DeflateFormatHeaderData&>(precomp_hdr_data);
  auto tmpfile_name = tools.get_tempfile_name("recomp_deflate", true);
  recompress_deflate(precompressed_input, recompressed_stream, deflate_hdr_data, tmpfile_name, "brute mode", tools);
}

