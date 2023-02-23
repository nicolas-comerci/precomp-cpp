#ifndef PRECOMP_BZip2_HANDLER_H
#define PRECOMP_BZip2_HANDLER_H
#include "precomp_dll.h"

#include "contrib/bzip2/bzlib.h"

bool bzip2_header_check(unsigned char* checkbuf) {
  // BZhx = header, x = compression level/blocksize (1-9)
  return (*checkbuf == 'B') && (*(checkbuf + 1) == 'Z') && (*(checkbuf + 2) == 'h');
}

class bzip2_precompression_result : public precompression_result {
public:
  bool recursion_used = false;
  long long recursion_filesize;
  int compression_level;

  bzip2_precompression_result(int compression_level) : precompression_result(D_BZIP2), compression_level(compression_level) {}

  void dump_precompressed_data_to_outfile(Precomp& precomp_mgr) override {
    if (recursion_used) fout_fput_vlint(*precomp_mgr.ctx->fout, recursion_filesize);
    auto out_size = recursion_used ? recursion_filesize : precompressed_size;
    fast_copy(precomp_mgr, *precompressed_stream, *precomp_mgr.ctx->fout, out_size);
  }

  void dump_to_outfile(Precomp& precomp_mgr) {
    dump_header_to_outfile(precomp_mgr);
    precomp_mgr.ctx->fout->put(compression_level);
    dump_penaltybytes_to_outfile(precomp_mgr);
    dump_stream_sizes_to_outfile(precomp_mgr);
    dump_precompressed_data_to_outfile(precomp_mgr);
  }
};

void copy_penalty_bytes(RecursionContext& context, long long& rek_penalty_bytes_len, bool& use_penalty_bytes) {
  if ((rek_penalty_bytes_len > 0) && (use_penalty_bytes)) {
    std::copy(context.local_penalty_bytes.data(), context.local_penalty_bytes.data() + rek_penalty_bytes_len, context.penalty_bytes.begin());
    context.penalty_bytes_len = rek_penalty_bytes_len;
  }
  else {
    context.penalty_bytes_len = 0;
  }
}

constexpr auto DEF_COMPARE_CHUNK = 512;
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
        }
        else {
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

void try_recompress_bzip2(Precomp& precomp_mgr, IStreamLike& origfile, int level, long long& compressed_stream_size, PrecompTmpFile& tmpfile) {
  precomp_mgr.call_progress_callback();

  long long decomp_bytes_total;
  precomp_mgr.ctx->identical_bytes = file_recompress_bzip2(precomp_mgr, origfile, level, precomp_mgr.ctx->identical_bytes_decomp, decomp_bytes_total, tmpfile);
  if (precomp_mgr.ctx->identical_bytes > -1) { // successfully recompressed?
    if ((precomp_mgr.ctx->identical_bytes > precomp_mgr.ctx->best_identical_bytes) || ((precomp_mgr.ctx->identical_bytes == precomp_mgr.ctx->best_identical_bytes) && (precomp_mgr.ctx->penalty_bytes_len < precomp_mgr.ctx->best_penalty_bytes_len))) {
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
      }
      else {
        print_to_log(PRECOMP_DEBUG_LOG, "Not enough identical recompressed bytes\n");
      }
    }
  }
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

bzip2_precompression_result try_decompression_bzip2(Precomp& precomp_mgr) {
  int compression_level = precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + 3] - '0';
  bzip2_precompression_result result = bzip2_precompression_result(compression_level);

  if ((compression_level < 1) || (compression_level > 9)) return result;  

  std::unique_ptr<PrecompTmpFile> tmpfile = std::unique_ptr<PrecompTmpFile>(new PrecompTmpFile());
  tmpfile->open(temp_files_tag() + "_original_bzip2", std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);

  init_decompression_variables(*precomp_mgr.ctx);

  // try to decompress at current position
  long long compressed_stream_size = -1;
  precomp_mgr.ctx->retval = try_to_decompress_bzip2(precomp_mgr, *precomp_mgr.ctx->fin, compression_level, compressed_stream_size, *tmpfile);

  if (precomp_mgr.ctx->retval > 0) { // seems to be a zLib-Stream

    precomp_mgr.statistics.decompressed_streams_count++;
    precomp_mgr.statistics.decompressed_bzip2_count++;

    print_to_log(PRECOMP_DEBUG_LOG, "Possible bZip2-Stream found at position %lli, compression level = %i\n", precomp_mgr.ctx->saved_input_file_pos, compression_level);
    print_to_log(PRECOMP_DEBUG_LOG, "Compressed size: %lli\n", compressed_stream_size);

    WrappedFStream ftempout;
    ftempout.open(tmpfile->file_path, std::ios_base::in | std::ios_base::binary);
    ftempout.seekg(0, std::ios_base::end);
    print_to_log(PRECOMP_DEBUG_LOG, "Can be decompressed to %lli bytes\n", ftempout.tellg());
    ftempout.close();
  }

  tmpfile->reopen();
  try_recompress_bzip2(precomp_mgr, *precomp_mgr.ctx->fin, compression_level, compressed_stream_size, *tmpfile);

  if ((precomp_mgr.ctx->best_identical_bytes > precomp_mgr.switches.min_ident_size) && (precomp_mgr.ctx->best_identical_bytes < precomp_mgr.ctx->best_identical_bytes_decomp)) {
    precomp_mgr.statistics.recompressed_streams_count++;
    precomp_mgr.statistics.recompressed_bzip2_count++;

    print_to_log(PRECOMP_DEBUG_LOG, "Best match: %lli bytes, decompressed to %lli bytes\n", precomp_mgr.ctx->best_identical_bytes, precomp_mgr.ctx->best_identical_bytes_decomp);

    precomp_mgr.ctx->non_zlib_was_used = true;
    result.success = true;

    // check recursion
    tmpfile->reopen();
    recursion_result r = recursion_compress(precomp_mgr, precomp_mgr.ctx->best_identical_bytes, precomp_mgr.ctx->best_identical_bytes_decomp, *tmpfile);

    // write compressed data header (bZip2)

    int header_byte = 1;
    if (precomp_mgr.ctx->best_penalty_bytes_len != 0) {
      header_byte += 2;
    }
    if (r.success) {
      header_byte += 128;
    }
    result.flags = header_byte;

    // store penalty bytes, if any
    result.penalty_bytes = std::vector(precomp_mgr.ctx->best_penalty_bytes.data(), precomp_mgr.ctx->best_penalty_bytes.data() + precomp_mgr.ctx->best_penalty_bytes_len);

    result.original_size = precomp_mgr.ctx->best_identical_bytes;
    result.precompressed_size = precomp_mgr.ctx->best_identical_bytes_decomp;

    // write decompressed data
    if (r.success) {
      auto rec_tmpfile = new PrecompTmpFile();
      rec_tmpfile->open(r.file_name, std::ios_base::in | std::ios_base::binary);
      result.precompressed_stream = std::unique_ptr<IStreamLike>(rec_tmpfile);
      result.recursion_filesize = r.file_length;
      result.recursion_used = true;
    }
    else {
      tmpfile->reopen();
      result.precompressed_stream = std::move(tmpfile);
    }
  }
  else {
    print_to_log(PRECOMP_DEBUG_LOG, "No matches\n");
  }

  return result;
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
    }
    else {
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

void recompress_bzip2(Precomp& precomp_mgr, unsigned char precomp_hdr_flags) {
  print_to_log(PRECOMP_DEBUG_LOG, "Decompressed data - bZip2\n");

  unsigned char header2 = precomp_mgr.ctx->fin->get();

  bool penalty_bytes_stored = ((precomp_hdr_flags & 2) == 2);
  bool recursion_used = ((precomp_hdr_flags & 128) == 128);
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
  }
  else {
    print_to_log(PRECOMP_DEBUG_LOG, "Recompressed length: %lli - decompressed length: %lli\n", recompressed_data_length, decompressed_data_length);
  }

  long long old_fout_pos = precomp_mgr.ctx->fout->tellp();

  if (recursion_used) {
    PrecompTmpFile tmp_bzip2;
    tmp_bzip2.open(temp_files_tag() + "_recomp_bzip2", std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
    recursion_result r = recursion_decompress(precomp_mgr, recursion_data_length, tmp_bzip2);
    auto wrapped_istream_frecurse = WrappedIStream(r.frecurse.get(), false);
    precomp_mgr.ctx->retval = def_part_bzip2(precomp_mgr, wrapped_istream_frecurse, *precomp_mgr.ctx->fout, level, decompressed_data_length, recompressed_data_length);
    r.frecurse->close();
    remove(r.file_name.c_str());
  }
  else {
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
}

#endif //PRECOMP_BZip2_HANDLER_H