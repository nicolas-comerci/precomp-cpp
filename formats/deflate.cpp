#include "deflate.h"

#include "contrib/preflate/preflate.h"
#include "contrib/zlib/zlib.h"

#include <cstddef>
#include <memory>
#include <sstream>

std::byte make_deflate_pcf_hdr_flags(const recompress_deflate_result& rdres) {
  return std::byte{ 0b1 } | (rdres.zlib_perfect ? static_cast<std::byte>(rdres.zlib_comp_level) << 2 : std::byte{ 0b10 });
}

void deflate_precompression_result::dump_recon_data_to_outfile(Precomp& precomp_mgr) {
  if (!rdres.zlib_perfect) {
    fout_fput_vlint(*precomp_mgr.ctx->fout, rdres.recon_data.size());
    precomp_mgr.ctx->fout->write(reinterpret_cast<char*>(rdres.recon_data.data()), rdres.recon_data.size());
  }
}
deflate_precompression_result::deflate_precompression_result(SupportedFormats format) : precompression_result(format) {}
void deflate_precompression_result::dump_header_to_outfile(Precomp& precomp_mgr) const {
  // write compressed data header
  precomp_mgr.ctx->fout->put(static_cast<char>(make_deflate_pcf_hdr_flags(rdres) | flags));
  precomp_mgr.ctx->fout->put(format);
  if (rdres.zlib_perfect) {
    precomp_mgr.ctx->fout->put(((rdres.zlib_window_bits - 8) << 4) + rdres.zlib_mem_level);
  }
  fout_fput_vlint(*precomp_mgr.ctx->fout, zlib_header.size());
  if (!inc_last_hdr_byte) {
    precomp_mgr.ctx->fout->write(reinterpret_cast<char*>(const_cast<unsigned char*>(zlib_header.data())), zlib_header.size());
  }
  else {
    precomp_mgr.ctx->fout->write(reinterpret_cast<char*>(const_cast<unsigned char*>(zlib_header.data())), zlib_header.size() - 1);
    precomp_mgr.ctx->fout->put(zlib_header[zlib_header.size() - 1] + 1);
  }
}
void deflate_precompression_result::dump_precompressed_data_to_outfile(Precomp& precomp_mgr) {
  if (recursion_used) fout_fput_vlint(*precomp_mgr.ctx->fout, recursion_filesize);
  auto out_size = recursion_used ? recursion_filesize : precompressed_size;
  fast_copy(*precompressed_stream, *precomp_mgr.ctx->fout, out_size);
}
void deflate_precompression_result::dump_to_outfile(Precomp& precomp_mgr) {
  dump_header_to_outfile(precomp_mgr);
  dump_penaltybytes_to_outfile(precomp_mgr);
  dump_recon_data_to_outfile(precomp_mgr);
  dump_stream_sizes_to_outfile(precomp_mgr);
  dump_precompressed_data_to_outfile(precomp_mgr);
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

recompress_deflate_result try_recompression_deflate(Precomp& precomp_mgr, IStreamLike& file, long long file_deflate_stream_pos, PrecompTmpFile& tmpfile) {
  file.seekg(file_deflate_stream_pos, std::ios_base::beg);

  recompress_deflate_result result;

  OwnIStream is(&file);

  {
    UncompressedOutStream uos(tmpfile, &precomp_mgr);
    uint64_t compressed_stream_size = 0;
    result.accepted = preflate_decode(uos, result.recon_data,
      compressed_stream_size, is, [&precomp_mgr]() { precomp_mgr.call_progress_callback(); },
      0,
      precomp_mgr.switches.preflate_meta_block_size); // you can set a minimum deflate stream size here
    result.compressed_stream_size = compressed_stream_size;
    result.uncompressed_stream_size = uos.written();
    if (uos.in_memory()) {
      result.uncompressed_stream_mem = uos.decomp_io_buf;
    }

    if (precomp_mgr.switches.preflate_verify && result.accepted) {
      file.seekg(file_deflate_stream_pos, std::ios_base::beg);
      OwnIStream is2(&file);
      std::vector<uint8_t> orgdata(result.compressed_stream_size);
      is2.read(orgdata.data(), orgdata.size());

      MemStream reencoded_deflate;
      auto decomp_io_buf_ptr = result.uncompressed_stream_mem.data();
      MemStream uncompressed_mem(uos.in_memory() ? std::vector(decomp_io_buf_ptr, decomp_io_buf_ptr + result.uncompressed_stream_size) : std::vector<uint8_t>());
      OwnIStream uncompressed_file(uos.in_memory() ? nullptr : &tmpfile);
      if (!preflate_reencode(reencoded_deflate, result.recon_data,
        uos.in_memory() ? (InputStream&)uncompressed_mem : (InputStream&)uncompressed_file,
        result.uncompressed_stream_size,
        [] {})
        || orgdata != reencoded_deflate.data()) {
        result.accepted = false;
        static size_t counter = 0;
        char namebuf[50];
        while (true) {
          snprintf(namebuf, 49, "preflate_error_%04zu.raw", counter++);
          std::fstream f;
          f.open(namebuf, std::ios_base::in | std::ios_base::binary);
          if (f.is_open()) {
            continue;
          }
          f.open(namebuf, std::ios_base::out | std::ios_base::binary);
          f.write(reinterpret_cast<char*>(orgdata.data()), orgdata.size());
          break;
        }
      }
    }
  }
  // If you think "hey I'll remove this std::move here so we can have copy elision", thread lightly... I tried it and it got me invalid precompressed streams...
  // You can try to get to the bottom of it if you want, for now I decided its not worth my time.
  return std::move(result);
}

void debug_deflate_detected(RecursionContext& context, const recompress_deflate_result& rdres, const char* type, long long deflate_stream_pos) {
  if (PRECOMP_VERBOSITY_LEVEL < PRECOMP_DEBUG_LOG) return;
  std::stringstream ss;
  ss << "Possible zLib-Stream within " << type << " found at position " << deflate_stream_pos << std::endl;
  ss << "Compressed size: " << rdres.compressed_stream_size << std::endl;
  ss << "Can be decompressed to " << rdres.uncompressed_stream_size << " bytes" << std::endl;

  if (rdres.accepted) {
    if (rdres.zlib_perfect) {
      ss << "Detect ZLIB parameters: comp level " << rdres.zlib_comp_level << ", mem level " << rdres.zlib_mem_level << ", " << rdres.zlib_window_bits << "window bits" << std::endl;
    }
    else {
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

deflate_precompression_result try_decompression_deflate_type(Precomp& precomp_mgr, unsigned& dcounter, unsigned& rcounter, SupportedFormats type,
  const unsigned char* hdr, const unsigned int hdr_length, long long deflate_stream_pos, const bool inc_last, const char* debugname, std::string tmp_filename) {
  std::unique_ptr<PrecompTmpFile> tmpfile = std::make_unique<PrecompTmpFile>();
  tmpfile->open(tmp_filename, std::ios_base::in | std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
  deflate_precompression_result result = deflate_precompression_result(type);

  // try to decompress at current position
  recompress_deflate_result rdres = try_recompression_deflate(precomp_mgr, *precomp_mgr.ctx->fin, deflate_stream_pos, *tmpfile);

  if (rdres.uncompressed_stream_size > 0) { // seems to be a zLib-Stream
    precomp_mgr.statistics.decompressed_streams_count++;
    dcounter++;

    debug_deflate_detected(*precomp_mgr.ctx, rdres, debugname, deflate_stream_pos);

    if (rdres.accepted) {
      result.success = rdres.accepted;
      result.original_size = rdres.compressed_stream_size;
      result.precompressed_size = rdres.uncompressed_stream_size;
      precomp_mgr.statistics.recompressed_streams_count++;
      rcounter++;

      precomp_mgr.ctx->non_zlib_was_used = true;

      debug_sums(precomp_mgr, rdres);

      // end uncompressed data

      debug_pos(precomp_mgr);

      // check recursion
      tmpfile->reopen();
      recursion_result r = recursion_compress(precomp_mgr, rdres.compressed_stream_size, rdres.uncompressed_stream_size, *tmpfile, true, rdres.uncompressed_stream_mem);

#if 0
      // Do we really want to allow uncompressed streams that are smaller than the compressed
      // ones? (It makes sense if the uncompressed stream contains a JPEG, or something similar.
      if (rdres.uncompressed_stream_size <= rdres.compressed_stream_size && !r.success) {
        precomp_mgr.statistics.recompressed_streams_count--;
        compressed_data_found = false;
        return result;
      }
#endif

      debug_pos(precomp_mgr);

      result.flags = r.success ? std::byte{ 0b10000000 } : std::byte{ 0 };
      result.inc_last_hdr_byte = inc_last;
      result.zlib_header = std::vector(hdr, hdr + hdr_length);
      if (r.success) {
        auto rec_tmpfile = new PrecompTmpFile();
        rec_tmpfile->open(r.file_name, std::ios_base::in | std::ios_base::binary);
        result.precompressed_stream = std::unique_ptr<IStreamLike>(rec_tmpfile);
        result.recursion_filesize = r.file_length;
        result.recursion_used = true;
      }
      else {
        if (!rdres.uncompressed_stream_mem.empty()) {
          auto memstream = memiostream::make(rdres.uncompressed_stream_mem.data(), rdres.uncompressed_stream_mem.data() + rdres.uncompressed_stream_size);
          result.precompressed_stream = std::move(memstream);
        }
        else {
          tmpfile->reopen();
          result.precompressed_stream = std::move(tmpfile);
        }
      }

      result.rdres = std::move(rdres);

      debug_pos(precomp_mgr);
    }
    else {
      if (type == D_SWF && intense_mode_is_active(precomp_mgr)) precomp_mgr.ctx->intense_ignore_offsets.insert(deflate_stream_pos - 2);
      if (type != D_BRUTE && brute_mode_is_active(precomp_mgr)) precomp_mgr.ctx->brute_ignore_offsets.insert(deflate_stream_pos);
      print_to_log(PRECOMP_DEBUG_LOG, "No matches\n");
    }
  }
  return result;
}

int histogram[256];
RecursionContext* prev_context;
unsigned char prev_first_byte;
long long prev_deflate_stream_pos;
int prev_maximum, prev_used;
int prev_i;

bool check_inflate_result(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span, unsigned char* out_buf, int windowbits, const long long deflate_stream_pos, bool use_brute_parameters) {
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
    if (precomp_mgr.ctx.get() != prev_context || prev_deflate_stream_pos + 1 != deflate_stream_pos) {
      // if we are not at the next pos from the last run, we need to remake the whole histogram from scratch
      memset(&histogram[0], 0, sizeof(histogram));
      i = 0;
      j = 0;
    }
    else {
      // if we are at the next pos from the last run, we just remove the data from the last run's first byte and pick up the histogram from there
      i = prev_i == 4 ? prev_i - 1 : prev_i;
      j = 63;
      data_ptr += 64 * i; // adjust the data_ptr to point to the byte we determined we need to continue computing the histogram from
      const bool prev_first_byte_repeated = histogram[prev_first_byte] > 1;
      histogram[prev_first_byte] -= 1;  // remove counting of the last byte
      maximum = *std::max_element(histogram, histogram + 256);
      used = prev_used;
      // if the first byte was repeated the used count would have been increased anyways, so we don't subtract, but if not repeated, that's one less for the use chars count
      if (!prev_first_byte_repeated) used -= 1;
    }

    for (; i < 4; i++, data_ptr += 64) {
      for (; j < 64; j++) {
        int* freq = &histogram[*(data_ptr + j)];
        used += ((*freq) == 0);
        maximum += (++(*freq)) > maximum;
      }
      if (maximum >= ((12 + i) << i) || used * (7 - (i + (i / 2))) < (i + 1) * 64)
        break;
      if (i != 3) j = 0;
    }

    // set vars to be able to pick up the histogram for the next position
    prev_context = precomp_mgr.ctx.get();
    prev_deflate_stream_pos = deflate_stream_pos;
    prev_first_byte = *checkbuf_span.data();
    prev_maximum = maximum;
    prev_used = used;
    prev_i = i;
    
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

  precomp_mgr.call_progress_callback();

  strm.avail_in = 2048;
  strm.next_in = checkbuf_span.data();

  /* run inflate() on input until output buffer not full */
  do {
    strm.avail_out = CHUNK;
    strm.next_out = out_buf;

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

bool check_raw_deflate_stream_start(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span, const long long original_input_pos) {
  return check_inflate_result(precomp_mgr, checkbuf_span, precomp_mgr.out, -15, original_input_pos, true);
}

deflate_precompression_result try_decompression_raw_deflate(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span, const long long original_input_pos) {
  return try_decompression_deflate_type(precomp_mgr,
    precomp_mgr.statistics.decompressed_brute_count, precomp_mgr.statistics.recompressed_brute_count,
    D_BRUTE, checkbuf_span.data(), 0, original_input_pos, false,
    "(brute mode)", temp_files_tag() + "_decomp_brute");
}

bool try_reconstructing_deflate(Precomp& precomp_mgr, IStreamLike& fin, OStreamLike& fout, const recompress_deflate_result& rdres) {
  OwnOStream os(&fout);
  OwnIStream is(&fin);
  bool result = preflate_reencode(os, rdres.recon_data, is, rdres.uncompressed_stream_size, [&precomp_mgr]() { precomp_mgr.call_progress_callback(); });
  return result;
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

bool try_reconstructing_deflate_skip(Precomp& precomp_mgr, IStreamLike& fin, OStreamLike& fout, const recompress_deflate_result& rdres, const size_t read_part, const size_t skip_part) {
  std::vector<unsigned char> unpacked_output;
  unpacked_output.resize(rdres.uncompressed_stream_size);
  unsigned int frs_offset = 0;
  unsigned int frs_skip_len = skip_part;
  unsigned int frs_line_len = read_part;
  if ((int64_t)fread_skip(unpacked_output.data(), 1, rdres.uncompressed_stream_size, fin, frs_offset, frs_line_len, frs_skip_len) != rdres.uncompressed_stream_size) {
    return false;
  }
  OwnOStream os(&fout);
  return preflate_reencode(os, rdres.recon_data, unpacked_output, [&precomp_mgr]() { precomp_mgr.call_progress_callback(); });
}

void fin_fget_deflate_hdr(IStreamLike& input, OStreamLike& output, recompress_deflate_result& rdres, const std::byte flags,
  unsigned char* hdr_data, unsigned& hdr_length,
  const bool inc_last_hdr_byte) {
  rdres.zlib_perfect = (flags & std::byte{ 0b10 }) == std::byte{ 0 };
  if (rdres.zlib_perfect) {
    const auto zlib_params = static_cast<std::byte>(input.get());
    rdres.zlib_comp_level = static_cast<char>((flags & std::byte{ 0b00111100 }) >> 2);
    rdres.zlib_mem_level = static_cast<char>(zlib_params & std::byte{ 0b00001111 });
    rdres.zlib_window_bits = static_cast<char>(static_cast<int>(zlib_params & std::byte{ 0b01110000 }) + 8);
  }
  hdr_length = fin_fget_vlint(input);
  if (!inc_last_hdr_byte) {
    input.read(reinterpret_cast<char*>(hdr_data), hdr_length);
  }
  else {
    input.read(reinterpret_cast<char*>(hdr_data), hdr_length - 1);
    hdr_data[hdr_length - 1] = input.get() - 1;
  }
  output.write(reinterpret_cast<char*>(hdr_data), hdr_length);
}

void fin_fget_deflate_rec(Precomp& precomp_mgr, recompress_deflate_result& rdres, const std::byte flags, unsigned char* hdr, unsigned& hdr_length, const bool inc_last) {
  fin_fget_deflate_hdr(*precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, rdres, flags, hdr, hdr_length, inc_last);
  fin_fget_recon_data(*precomp_mgr.ctx->fin, rdres);

  debug_sums(precomp_mgr, rdres);
}

void debug_deflate_reconstruct(const recompress_deflate_result& rdres, const char* type, const unsigned hdr_length, const uint64_t rec_length) {
  if (PRECOMP_VERBOSITY_LEVEL < PRECOMP_DEBUG_LOG) return;
  std::stringstream ss;
  ss << "Decompressed data - " << type << std::endl;
  ss << "Header length: " << hdr_length << std::endl;
  if (rdres.zlib_perfect) {
    ss << "ZLIB Parameters: compression level " << rdres.zlib_comp_level
      << " memory level " << rdres.zlib_mem_level
      << " window bits " << rdres.zlib_window_bits << std::endl;
  }
  else {
    ss << "Reconstruction data size: " << rdres.recon_data.size() << std::endl;
  }
  if (rec_length > 0) {
    ss << "Recursion data length: " << rec_length << std::endl;
  }
  else {
    ss << "Recompressed length: " << rdres.compressed_stream_size << " - decompressed length: " << rdres.uncompressed_stream_size << std::endl;
  }
  print_to_log(PRECOMP_DEBUG_LOG, ss.str());
}

void recompress_deflate(Precomp& precomp_mgr, std::byte precomp_hdr_flags, bool incl_last_hdr_byte, std::string filename, std::string type) {
  recompress_deflate_result rdres;
  unsigned hdr_length;
  int64_t recursion_data_length = 0;
  bool ok;
  fin_fget_deflate_rec(precomp_mgr, rdres, precomp_hdr_flags, precomp_mgr.in, hdr_length, incl_last_hdr_byte);

  // write decompressed data
  if ((precomp_hdr_flags & std::byte{ 0b10000000 }) == std::byte{ 0b10000000 }) {
    recursion_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);
    recursion_result r = recursion_decompress(precomp_mgr, recursion_data_length, filename);
    debug_pos(precomp_mgr);
    auto wrapped_istream_frecurse = WrappedIStream(r.frecurse.get(), false);
    ok = try_reconstructing_deflate(precomp_mgr, wrapped_istream_frecurse, *precomp_mgr.ctx->fout, rdres);
    debug_pos(precomp_mgr);
    r.frecurse->close();
    remove(r.file_name.c_str());
  }
  else {
    debug_pos(precomp_mgr);
    ok = try_reconstructing_deflate(precomp_mgr, *precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, rdres);
    debug_pos(precomp_mgr);
  }

  debug_deflate_reconstruct(rdres, type.c_str(), hdr_length, recursion_data_length);

  if (!ok) {
    throw PrecompError(ERR_DURING_RECOMPRESSION);
  }
}

void recompress_raw_deflate(Precomp& precomp_mgr, std::byte precomp_hdr_flags) {
  recompress_deflate(precomp_mgr, precomp_hdr_flags, false, temp_files_tag() + "_recomp_deflate", "brute mode");
}
