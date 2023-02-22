#ifndef PRECOMP_DEFLATE_HANDLER_H
#define PRECOMP_DEFLATE_HANDLER_H
#include "precomp_dll.h"

class deflate_precompression_result : public precompression_result {
protected:
  void dump_recon_data_to_outfile(Precomp& precomp_mgr) {
    if (!rdres.zlib_perfect) {
      fout_fput_vlint(*precomp_mgr.ctx->fout, rdres.recon_data.size());
      precomp_mgr.ctx->fout->write(reinterpret_cast<char*>(rdres.recon_data.data()), rdres.recon_data.size());
    }
  }
public:
  recompress_deflate_result rdres;
  std::vector<unsigned char> zlib_header;
  bool inc_last_hdr_byte = false;
  bool recursion_used = false;
  long long recursion_filesize;

  deflate_precompression_result(SupportedFormats format) : precompression_result(format) {}

  void dump_header_to_outfile(Precomp& precomp_mgr) const override {
    // write compressed data header
    precomp_mgr.ctx->fout->put(make_deflate_pcf_hdr_flags(rdres) + flags);
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

  void dump_precompressed_data_to_outfile(Precomp& precomp_mgr) override {
    if (recursion_used) fout_fput_vlint(*precomp_mgr.ctx->fout, recursion_filesize);
    auto out_size = recursion_used ? recursion_filesize : precompressed_size;
    fast_copy(precomp_mgr, *precompressed_stream, *precomp_mgr.ctx->fout, out_size);
  }

  void dump_to_outfile(Precomp& precomp_mgr) override {
    dump_header_to_outfile(precomp_mgr);
    dump_penaltybytes_to_outfile(precomp_mgr);
    dump_recon_data_to_outfile(precomp_mgr);
    dump_stream_sizes_to_outfile(precomp_mgr);
    dump_precompressed_data_to_outfile(precomp_mgr);
  }
};

class OwnIStream : public InputStream {
public:
  OwnIStream(IStreamLike* f) : _f(f), _eof(false) {}

  virtual bool eof() const {
    return _eof;
  }
  virtual size_t read(unsigned char* buffer, const size_t size) {
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
  OwnOStream(OStreamLike* f) : _f(f) {}

  virtual size_t write(const unsigned char* buffer, const size_t size) {
    _f->write(reinterpret_cast<char*>(const_cast<unsigned char*>(buffer)), size);
    return _f->bad() ? 0 : size;
  }
private:
  OStreamLike* _f;
};
class UncompressedOutStream : public OutputStream {
public:
  OStreamLike& ftempout;
  Precomp* precomp_mgr;

  UncompressedOutStream(bool& in_memory, OStreamLike& tmpfile, Precomp* precomp_mgr)
    : ftempout(tmpfile), precomp_mgr(precomp_mgr), _written(0), _in_memory(in_memory) {}
  ~UncompressedOutStream() {}

  virtual size_t write(const unsigned char* buffer, const size_t size) {
    precomp_mgr->call_progress_callback();
    if (_in_memory) {
      auto decomp_io_buf_ptr = precomp_mgr->ctx->decomp_io_buf.data();
      if (_written + size >= MAX_IO_BUFFER_SIZE) {
        _in_memory = false;
        memiostream memstream = memiostream::make(decomp_io_buf_ptr, decomp_io_buf_ptr + _written);
        fast_copy(*precomp_mgr, memstream, ftempout, _written);
      }
      else {
        memcpy(decomp_io_buf_ptr + _written, buffer, size);
        _written += size;
        return size;
      }
    }
    _written += size;
    ftempout.write(reinterpret_cast<char*>(const_cast<unsigned char*>(buffer)), size);
    return ftempout.bad() ? 0 : size;
  }

  uint64_t written() const {
    return _written;
  }

private:
  uint64_t _written;
  bool& _in_memory;
};

recompress_deflate_result try_recompression_deflate(Precomp& precomp_mgr, IStreamLike& file, PrecompTmpFile& tmpfile) {
  file.seekg(&file == precomp_mgr.ctx->fin.get() ? precomp_mgr.ctx->input_file_pos : 0, std::ios_base::beg);

  recompress_deflate_result result;
  memset(&result, 0, sizeof(result));

  OwnIStream is(&file);

  {
    result.uncompressed_in_memory = true;
    UncompressedOutStream uos(result.uncompressed_in_memory, tmpfile, &precomp_mgr);
    uint64_t compressed_stream_size = 0;
    result.accepted = preflate_decode(uos, result.recon_data,
      compressed_stream_size, is, [&precomp_mgr]() { precomp_mgr.call_progress_callback(); },
      0,
      precomp_mgr.switches.preflate_meta_block_size); // you can set a minimum deflate stream size here
    result.compressed_stream_size = compressed_stream_size;
    result.uncompressed_stream_size = uos.written();

    if (precomp_mgr.switches.preflate_verify && result.accepted) {
      file.seekg(&file == precomp_mgr.ctx->fin.get() ? precomp_mgr.ctx->input_file_pos : 0, std::ios_base::beg);
      OwnIStream is2(&file);
      std::vector<uint8_t> orgdata(result.compressed_stream_size);
      is2.read(orgdata.data(), orgdata.size());

      MemStream reencoded_deflate;
      auto decomp_io_buf_ptr = precomp_mgr.ctx->decomp_io_buf.data();
      MemStream uncompressed_mem(result.uncompressed_in_memory ? std::vector<uint8_t>(decomp_io_buf_ptr, decomp_io_buf_ptr + result.uncompressed_stream_size) : std::vector<uint8_t>());
      OwnIStream uncompressed_file(result.uncompressed_in_memory ? nullptr : &tmpfile);
      if (!preflate_reencode(reencoded_deflate, result.recon_data,
        result.uncompressed_in_memory ? (InputStream&)uncompressed_mem : (InputStream&)uncompressed_file,
        result.uncompressed_stream_size,
        [] {})
        || orgdata != reencoded_deflate.data()) {
        result.accepted = false;
        static size_t counter = 0;
        char namebuf[50];
        while (true) {
          snprintf(namebuf, 49, "preflate_error_%04d.raw", counter++);
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
  return std::move(result);
}

deflate_precompression_result try_decompression_deflate_type_internal(Precomp& precomp_mgr, unsigned& dcounter, unsigned& rcounter, SupportedFormats type,
  const unsigned char* hdr, const int hdr_length, const bool inc_last, const char* debugname, std::string tmp_filename) {
  std::unique_ptr<PrecompTmpFile> tmpfile = std::unique_ptr<PrecompTmpFile>(new PrecompTmpFile());
  tmpfile->open(tmp_filename, std::ios_base::in | std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
  deflate_precompression_result result = deflate_precompression_result(type);
  init_decompression_variables(*precomp_mgr.ctx);

  // try to decompress at current position
  recompress_deflate_result rdres = try_recompression_deflate(precomp_mgr, *precomp_mgr.ctx->fin, *tmpfile);

  if (rdres.uncompressed_stream_size > 0) { // seems to be a zLib-Stream
    precomp_mgr.statistics.decompressed_streams_count++;
    dcounter++;

    debug_deflate_detected(*precomp_mgr.ctx, rdres, debugname);

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
      recursion_result r = recursion_write_file_and_compress(precomp_mgr, rdres, *tmpfile);

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

      result.flags = r.success ? 128 : 0;
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
        if (rdres.uncompressed_stream_size) {
          auto decomp_io_buf_ptr = precomp_mgr.ctx->decomp_io_buf.data();
          auto memstream = memiostream::make_copy(decomp_io_buf_ptr, decomp_io_buf_ptr + rdres.uncompressed_stream_size);
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
      if (type == D_SWF && intense_mode_is_active(precomp_mgr)) precomp_mgr.ctx->intense_ignore_offsets->insert(precomp_mgr.ctx->input_file_pos - 2);
      if (type != D_BRUTE && brute_mode_is_active(precomp_mgr)) precomp_mgr.ctx->brute_ignore_offsets->insert(precomp_mgr.ctx->input_file_pos);
      print_to_log(PRECOMP_DEBUG_LOG, "No matches\n");
    }
  }
  return result;
}

deflate_precompression_result try_decompression_deflate_type(Precomp& precomp_mgr, unsigned& dcounter, unsigned& rcounter, SupportedFormats type,
  const unsigned char* hdr, const int hdr_length, const bool inc_last, const char* debugname, PrecompTmpFile& tmpfile) {
  tmpfile.close();

  auto result = try_decompression_deflate_type_internal(precomp_mgr, dcounter, rcounter, type, hdr, hdr_length, inc_last, debugname, tmpfile.file_path);
  if (result.success) {
    precomp_mgr.ctx->compressed_data_found = true;

    end_uncompressed_data(precomp_mgr);

    result.dump_to_outfile(precomp_mgr);

    // set input file pointer after recompressed data
    precomp_mgr.ctx->input_file_pos += result.input_pos_add_offset();
    precomp_mgr.ctx->cb += result.input_pos_add_offset();
  }
  return result;
}

bool check_inflate_result(Precomp& precomp_mgr, unsigned char* in_buf, unsigned char* out_buf, int cb_pos, int windowbits, bool use_brute_parameters = false) {
  // first check BTYPE bits, skip 11 ("reserved (error)")
  int btype = (in_buf[cb_pos] & 0x07) >> 1;
  if (btype == 3) return false;
  // skip BTYPE = 00 ("uncompressed") only in brute mode, because these can be useful for recursion
  // and often occur in combination with static/dynamic BTYPE blocks
  if (use_brute_parameters) {
    if (btype == 0) return false;

    // use a histogram to see if the first 64 bytes are too redundant for a deflate stream,
    // if a byte is present 8 or more times, it's most likely not a deflate stream
    // and could slow down the process (e.g. repeated patterns of "0xEBE1F1" or "0xEBEBEBFF"
    // did this before)
    int histogram[256];
    memset(&histogram[0], 0, sizeof(histogram));
    int maximum = 0, used = 0, offset = cb_pos;
    for (int i = 0; i < 4; i++, offset += 64) {
      for (int j = 0; j < 64; j++) {
        int* freq = &histogram[in_buf[offset + j]];
        used += ((*freq) == 0);
        maximum += (++(*freq)) > maximum;
      }
      if (maximum >= ((12 + i) << i) || used * (7 - (i + (i / 2))) < (i + 1) * 64)
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
  strm.next_in = in_buf + cb_pos;

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

bool check_raw_deflate_stream_start(Precomp& precomp_mgr) {
  return check_inflate_result(precomp_mgr, precomp_mgr.ctx->in_buf, precomp_mgr.out, precomp_mgr.ctx->cb, -15, true);
}

deflate_precompression_result try_decompression_raw_deflate(Precomp& precomp_mgr) {
  return try_decompression_deflate_type_internal(precomp_mgr,
    precomp_mgr.statistics.decompressed_brute_count, precomp_mgr.statistics.recompressed_brute_count,
    D_BRUTE, precomp_mgr.ctx->in_buf + precomp_mgr.ctx->cb, 0, false,
    "(brute mode)", temp_files_tag() + "_decomp_brute");
}

bool try_reconstructing_deflate(Precomp& precomp_mgr, IStreamLike& fin, OStreamLike& fout, const recompress_deflate_result& rdres) {
  OwnOStream os(&fout);
  OwnIStream is(&fin);
  bool result = preflate_reencode(os, rdres.recon_data, is, rdres.uncompressed_stream_size, [&precomp_mgr]() { precomp_mgr.call_progress_callback(); });
  return result;
}

void fin_fget_deflate_rec(Precomp& precomp_mgr, recompress_deflate_result& rdres, const unsigned char flags, unsigned char* hdr, unsigned& hdr_length, const bool inc_last) {
  fin_fget_deflate_hdr(*precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, rdres, flags, hdr, hdr_length, inc_last);
  fin_fget_recon_data(*precomp_mgr.ctx->fin, rdres);

  debug_sums(precomp_mgr, rdres);
}

void debug_deflate_reconstruct(const recompress_deflate_result& rdres, const char* type,
  const unsigned hdr_length, const uint64_t rec_length) {
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

void recompress_deflate(Precomp& precomp_mgr, unsigned char precomp_hdr_flags, bool incl_last_hdr_byte, std::string filename, std::string type) {
  recompress_deflate_result rdres;
  unsigned hdr_length;
  int64_t recursion_data_length = 0;
  bool ok;
  fin_fget_deflate_rec(precomp_mgr, rdres, precomp_hdr_flags, precomp_mgr.in, hdr_length, incl_last_hdr_byte);

  // write decompressed data
  if (precomp_hdr_flags & 128) {
    recursion_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);
    PrecompTmpFile tmpfile;
    tmpfile.open(filename, std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
    recursion_result r = recursion_decompress(precomp_mgr, recursion_data_length, tmpfile);
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

void recompress_raw_deflate(Precomp& precomp_mgr, unsigned char precomp_hdr_flags) {
  recompress_deflate(precomp_mgr, precomp_hdr_flags, false, temp_files_tag() + "_recomp_deflate", "brute mode");
}

#endif //PRECOMP_DEFLATE_HANDLER_H