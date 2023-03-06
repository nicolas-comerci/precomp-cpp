#include "bzip2.h"

#include "contrib/bzip2/bzlib.h"

#include <cstddef>
#include <memory>

bzip2_precompression_result::bzip2_precompression_result(int compression_level) : precompression_result(D_BZIP2), compression_level(compression_level) {}
void bzip2_precompression_result::dump_precompressed_data_to_outfile(Precomp& precomp_mgr) {
  if (recursion_used) fout_fput_vlint(*precomp_mgr.ctx->fout, recursion_filesize);
  auto out_size = recursion_used ? recursion_filesize : precompressed_size;
  fast_copy(*precompressed_stream, *precomp_mgr.ctx->fout, out_size);
}
void bzip2_precompression_result::dump_to_outfile(Precomp& precomp_mgr) {
  dump_header_to_outfile(precomp_mgr);
  precomp_mgr.ctx->fout->put(compression_level);
  dump_penaltybytes_to_outfile(precomp_mgr);
  dump_stream_sizes_to_outfile(precomp_mgr);
  dump_precompressed_data_to_outfile(precomp_mgr);
}

bool bzip2_header_check(const std::span<unsigned char> checkbuf_span) {
  auto checkbuf = checkbuf_span.data();
  // BZhx = header, x = compression level/blocksize (1-9)
  return (*checkbuf == 'B') && (*(checkbuf + 1) == 'Z') && (*(checkbuf + 2) == 'h');
}

constexpr auto DEF_COMPARE_CHUNK = 512;
unsigned char input_bytes1[DEF_COMPARE_CHUNK];

std::tuple<long long, std::optional<std::vector<char>>> compare_file_mem_penalty(RecursionContext& context, IStreamLike& file1, const unsigned char* input_bytes2, long long pos1, long long bytecount, long long& total_same_byte_count, long long& rek_same_byte_count, long long& rek_penalty_bytes_len) {
  int same_byte_count = 0;

  unsigned long long old_pos = file1.tellg();
  file1.seekg(pos1, std::ios_base::beg);

  file1.read(reinterpret_cast<char*>(input_bytes1), bytecount);
  std::streamsize size1 = file1.gcount();

  bool use_penalty_bytes = false;
  std::vector<char> penalty_bytes{};
  penalty_bytes.reserve(MAX_PENALTY_BYTES);
  long long total_same_byte_count_penalty = 0;
  long long rek_same_byte_count_penalty = -1;

  for (int i = 0; i < size1; i++) {
    if (input_bytes1[i] == input_bytes2[i]) {
      same_byte_count++;
      total_same_byte_count_penalty++;
    }
    else {
      total_same_byte_count_penalty -= 5; // 4 bytes = position, 1 byte = new byte

      // stop, if local_penalty_bytes_len gets too big
      if ((penalty_bytes.size() + 5) >= MAX_PENALTY_BYTES) {
        break;
      }

      // position
      penalty_bytes.push_back((total_same_byte_count >> 24) % 256);
      penalty_bytes.push_back((total_same_byte_count >> 16) % 256);
      penalty_bytes.push_back((total_same_byte_count >> 8) % 256);
      penalty_bytes.push_back((total_same_byte_count) % 256);
      // new byte
      penalty_bytes.push_back(input_bytes1[i]);
    }
    total_same_byte_count++;

    if (total_same_byte_count_penalty > rek_same_byte_count_penalty) {
      use_penalty_bytes = true;
      rek_penalty_bytes_len = penalty_bytes.size();

      rek_same_byte_count = total_same_byte_count;
      rek_same_byte_count_penalty = total_same_byte_count_penalty;
    }
  }

  file1.seekg(old_pos, std::ios_base::beg);

  return { same_byte_count, use_penalty_bytes ? std::optional(penalty_bytes) : std::nullopt };
}

std::tuple<long long, std::optional<std::vector<char>>> def_compare_bzip2(Precomp& precomp_mgr, IStreamLike& decompressed_stream, IStreamLike& original_bzip2, long long original_input_pos, int level, long long& decompressed_bytes_used) {
  int ret, flush;
  unsigned have;
  bz_stream strm;
  long long identical_bytes_compare = 0;
  std::optional<std::vector<char>> local_penalty_bytes{};


  long long comp_pos = 0;
  decompressed_bytes_used = 0;

  /* allocate deflate state */
  strm.bzalloc = nullptr;
  strm.bzfree = nullptr;
  strm.opaque = nullptr;
  ret = BZ2_bzCompressInit(&strm, level, 0, 0);
  if (ret != BZ_OK)
    return { ret, local_penalty_bytes };

  long long total_same_byte_count = 0;
  long long rek_same_byte_count = 0;
  long long rek_penalty_bytes_len = 0;
  
  /* compress until end of file */
  std::vector<unsigned char> in_buf{};
  in_buf.resize(DEF_COMPARE_CHUNK);
  do {
    precomp_mgr.call_progress_callback();

    decompressed_stream.read(reinterpret_cast<char*>(in_buf.data()), DEF_COMPARE_CHUNK);
    strm.avail_in = decompressed_stream.gcount();
    if (decompressed_stream.bad()) {
      (void)BZ2_bzCompressEnd(&strm);
      return { BZ_PARAM_ERROR, local_penalty_bytes };
    }
    flush = decompressed_stream.eof() ? BZ_FINISH : BZ_RUN;
    strm.next_in = reinterpret_cast<char*>(in_buf.data());
    decompressed_bytes_used += strm.avail_in;

    do {
      strm.avail_out = DEF_COMPARE_CHUNK;
      strm.next_out = reinterpret_cast<char*>(precomp_mgr.ctx->tmp_out);

      ret = BZ2_bzCompress(&strm, flush);

      have = DEF_COMPARE_CHUNK - strm.avail_out;

      if (have > 0) {
        if (&original_bzip2 == precomp_mgr.ctx->fin.get()) {
          std::tie(identical_bytes_compare, local_penalty_bytes) = compare_file_mem_penalty(*precomp_mgr.ctx, original_bzip2, precomp_mgr.ctx->tmp_out, original_input_pos + comp_pos, have, total_same_byte_count, rek_same_byte_count, rek_penalty_bytes_len);
        }
        else {
          std::tie(identical_bytes_compare, local_penalty_bytes) = compare_file_mem_penalty(*precomp_mgr.ctx, original_bzip2, precomp_mgr.ctx->tmp_out, comp_pos, have, total_same_byte_count, rek_same_byte_count, rek_penalty_bytes_len);
        }
      }

      if (have > 0) {
        if ((unsigned int)identical_bytes_compare < (have >> 1)) {
          (void)BZ2_bzCompressEnd(&strm);
          if (local_penalty_bytes.has_value()) local_penalty_bytes.value().resize(rek_penalty_bytes_len);
          return { rek_same_byte_count, local_penalty_bytes };
        }
      }

      comp_pos += have;

    } while (strm.avail_out == 0);

  } while (flush != BZ_FINISH);

  (void)BZ2_bzCompressEnd(&strm);
  if (local_penalty_bytes.has_value()) local_penalty_bytes.value().resize(rek_penalty_bytes_len);
  return { rek_same_byte_count, local_penalty_bytes };
}

std::tuple<long long, std::optional<std::vector<char>>> file_recompress_bzip2(Precomp& precomp_mgr, IStreamLike& bzip2_stream, long long original_input_pos, int level, long long& decompressed_bytes_used, long long& decompressed_bytes_total, IStreamLike& decompressed_stream) {
  long long retval;
  std::optional<std::vector<char>> local_penalty_bytes{};

  decompressed_stream.seekg(0, std::ios_base::end);
  decompressed_bytes_total = decompressed_stream.tellg();

  decompressed_stream.seekg(0, std::ios_base::beg);
  std::tie(retval, local_penalty_bytes) = def_compare_bzip2(precomp_mgr, decompressed_stream, bzip2_stream, original_input_pos, level, decompressed_bytes_used);
  return { retval < 0 ? -1 : retval, local_penalty_bytes };
}

std::tuple<long long, long long, std::optional<std::vector<char>>> try_recompress_bzip2(Precomp& precomp_mgr, IStreamLike& origfile, long long original_input_pos, int level, long long& compressed_stream_size, IStreamLike& tmpfile) {
  precomp_mgr.call_progress_callback();

  long long decomp_bytes_total;
  long long decompressed_size;
  auto [recompressed_size, penalty_bytes] = file_recompress_bzip2(precomp_mgr, origfile, original_input_pos, level, decompressed_size, decomp_bytes_total, tmpfile);
  if (recompressed_size > -1) { // successfully recompressed?
    if ((recompressed_size > -1) || ((recompressed_size == -1) && (penalty_bytes.has_value() && penalty_bytes.value().size() < 0))) {
      if (recompressed_size > precomp_mgr.switches.min_ident_size) {
        print_to_log(PRECOMP_DEBUG_LOG, "Identical recompressed bytes: %lli of %lli\n", recompressed_size, compressed_stream_size);
        print_to_log(PRECOMP_DEBUG_LOG, "Identical decompressed bytes: %lli of %lli\n", decompressed_size, decomp_bytes_total);
      }

      // Partial matches sometimes need all the decompressed bytes, but there are much less 
      // identical recompressed bytes - in these cases, all the decompressed bytes have to
      // be stored together with the remaining recompressed bytes, so the result won't compress
      // better than the original stream. What's important here is the ratio between recompressed ratio
      // and decompressed ratio that shouldn't get too high.
      // Example: A stream has 5 of 1000 identical recompressed bytes, but needs 1000 of 1000 decompressed bytes,
      // so the ratio is (1000/1000)/(5/1000) = 200 which is too high. With 5 of 1000 decompressed bytes or
      // 1000 of 1000 identical recompressed bytes, ratio would've been 1 and we'd accept it.

      float partial_ratio = ((float)decompressed_size / decomp_bytes_total) / ((float)recompressed_size / compressed_stream_size);
      if (partial_ratio >= 3.0f) {
        penalty_bytes = std::nullopt;
        print_to_log(PRECOMP_DEBUG_LOG, "Not enough identical recompressed bytes\n");
      }
    }
  }
  return  { decompressed_size, recompressed_size, penalty_bytes };
}

int inf_bzip2(Precomp& precomp_mgr, IStreamLike& source, OStreamLike& dest, long long& compressed_stream_size, long long& decompressed_stream_size) {
  int ret;
  unsigned have;
  bz_stream strm;

  strm.bzalloc = nullptr;
  strm.bzfree = nullptr;
  strm.opaque = nullptr;
  strm.avail_in = 0;
  strm.next_in = nullptr;
  ret = BZ2_bzDecompressInit(&strm, 0, 0);
  if (ret != BZ_OK)
    return ret;

  compressed_stream_size = 0;
  decompressed_stream_size = 0;
  unsigned int avail_in_before;
  std::vector<unsigned char> in_buf{};
  in_buf.resize(CHUNK);

  do {
    precomp_mgr.call_progress_callback();

    source.read(reinterpret_cast<char*>(in_buf.data()), CHUNK);
    strm.avail_in = source.gcount();
    avail_in_before = strm.avail_in;

    if (source.bad()) {
      (void)BZ2_bzDecompressEnd(&strm);
      return BZ_PARAM_ERROR;
    }
    if (strm.avail_in == 0)
      break;
    strm.next_in = reinterpret_cast<char*>(in_buf.data());

    do {
      strm.avail_out = CHUNK;
      strm.next_out = reinterpret_cast<char*>(precomp_mgr.ctx->tmp_out);

      ret = BZ2_bzDecompress(&strm);
      if ((ret != BZ_OK) && (ret != BZ_STREAM_END)) {
        (void)BZ2_bzDecompressEnd(&strm);
        return ret;
      }

      compressed_stream_size += (avail_in_before - strm.avail_in);
      avail_in_before = strm.avail_in;

      have = CHUNK - strm.avail_out;
      dest.write(reinterpret_cast<char*>(precomp_mgr.ctx->tmp_out), have);
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

long long try_to_decompress_bzip2(Precomp& precomp_mgr, IStreamLike& bzip2_stream, long long original_input_pos, long long& compressed_stream_size, PrecompTmpFile& decompressed_stream) {
  long long r, decompressed_stream_size;

  precomp_mgr.call_progress_callback();

  remove(decompressed_stream.file_path.c_str());
  WrappedFStream ftempout;
  ftempout.open(decompressed_stream.file_path, std::ios_base::out | std::ios_base::binary);

  bzip2_stream.seekg(&bzip2_stream == precomp_mgr.ctx->fin.get() ? original_input_pos : 0, std::ios_base::beg);

  r = inf_bzip2(precomp_mgr, bzip2_stream, ftempout, compressed_stream_size, decompressed_stream_size);
  ftempout.close();
  if (r == BZ_OK) return decompressed_stream_size;

  return r;
}

bzip2_precompression_result try_decompression_bzip2(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span, const long long original_input_pos) {
  const int compression_level = *(checkbuf_span.data() + 3) - '0';
  bzip2_precompression_result result = bzip2_precompression_result(compression_level);
  if ((compression_level < 1) || (compression_level > 9)) return result;

  std::unique_ptr<PrecompTmpFile> tmpfile = std::make_unique<PrecompTmpFile>();
  tmpfile->open(temp_files_tag() + "_original_bzip2", std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);

  // try to decompress at current position
  long long compressed_stream_size = -1;
  long long retval = try_to_decompress_bzip2(precomp_mgr, *precomp_mgr.ctx->fin, original_input_pos, compressed_stream_size, *tmpfile);

  if (retval > 0) { // seems to be a zLib-Stream

    precomp_mgr.statistics.decompressed_streams_count++;
    precomp_mgr.statistics.decompressed_bzip2_count++;

    print_to_log(PRECOMP_DEBUG_LOG, "Possible bZip2-Stream found at position %lli, compression level = %i\n", original_input_pos, compression_level);
    print_to_log(PRECOMP_DEBUG_LOG, "Compressed size: %lli\n", compressed_stream_size);

    WrappedFStream ftempout;
    ftempout.open(tmpfile->file_path, std::ios_base::in | std::ios_base::binary);
    ftempout.seekg(0, std::ios_base::end);
    print_to_log(PRECOMP_DEBUG_LOG, "Can be decompressed to %lli bytes\n", ftempout.tellg());
    ftempout.close();
  }

  tmpfile->reopen();
  if (!tmpfile->is_open()) {
    throw PrecompError(ERR_TEMP_FILE_DISAPPEARED);
  }
  auto [decompressed_size, recompressed_size, best_penalty_bytes] = try_recompress_bzip2(precomp_mgr, *precomp_mgr.ctx->fin, original_input_pos, compression_level, compressed_stream_size, *tmpfile);

  if ((recompressed_size > precomp_mgr.switches.min_ident_size) && (recompressed_size < decompressed_size)) {
    precomp_mgr.statistics.recompressed_streams_count++;
    precomp_mgr.statistics.recompressed_bzip2_count++;

    print_to_log(PRECOMP_DEBUG_LOG, "Best match: %lli bytes, decompressed to %lli bytes\n", recompressed_size, decompressed_size);

    precomp_mgr.ctx->non_zlib_was_used = true;
    result.success = true;

    // check recursion
    tmpfile->reopen();
    const recursion_result r = recursion_compress(precomp_mgr, recompressed_size, decompressed_size, *tmpfile);

    // write compressed data header (bZip2)

    std::byte header_byte{ 0b1 };
    if (best_penalty_bytes.has_value() && !best_penalty_bytes.value().empty()) {
      header_byte |= std::byte{ 0b10 };
    }
    if (r.success) {
      header_byte |= std::byte{ 0b10000000 };
    }
    result.flags = header_byte;

    // store penalty bytes, if any
    if (best_penalty_bytes.has_value()) {
      result.penalty_bytes = best_penalty_bytes.value();
    }

    result.original_size = recompressed_size;
    result.precompressed_size = decompressed_size;

    // write decompressed data
    if (r.success) {
      const auto rec_tmpfile = new PrecompTmpFile();
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
  strm.bzalloc = nullptr;
  strm.bzfree = nullptr;
  strm.opaque = nullptr;
  ret = BZ2_bzCompressInit(&strm, level, 0, 0);
  if (ret != BZ_OK)
    return ret;

  long long pos_in = 0;
  long long pos_out = 0;

  /* compress until end of file */
  std::vector<unsigned char> in_buf{};
  in_buf.resize(CHUNK);
  do {
    if ((stream_size_in - pos_in) > CHUNK) {
      precomp_mgr.call_progress_callback();

      source.read(reinterpret_cast<char*>(in_buf.data()), CHUNK);
      strm.avail_in = source.gcount();
      pos_in += CHUNK;
      flush = BZ_RUN;
    }
    else {
      source.read(reinterpret_cast<char*>(in_buf.data()), stream_size_in - pos_in);
      strm.avail_in = source.gcount();
      flush = BZ_FINISH;
    }
    if (source.bad()) {
      (void)BZ2_bzCompressEnd(&strm);
      return BZ_PARAM_ERROR;
    }
    strm.next_in = reinterpret_cast<char*>(in_buf.data());

    do {
      strm.avail_out = CHUNK;
      strm.next_out = reinterpret_cast<char*>(precomp_mgr.ctx->tmp_out);

      ret = BZ2_bzCompress(&strm, flush);

      have = CHUNK - strm.avail_out;

      if ((pos_out + static_cast<signed>(have)) > stream_size_out) {
        have = stream_size_out - pos_out;
      }
      pos_out += have;

      dest.write(reinterpret_cast<char*>(precomp_mgr.ctx->tmp_out), have);
      if (dest.bad()) {
        (void)BZ2_bzCompressEnd(&strm);
        return BZ_DATA_ERROR;
      }
    } while (strm.avail_out == 0);

  } while (flush != BZ_FINISH);

  (void)BZ2_bzCompressEnd(&strm);
  return BZ_OK;
}

void recompress_bzip2(RecursionContext& context, std::byte precomp_hdr_flags) {
  print_to_log(PRECOMP_DEBUG_LOG, "Decompressed data - bZip2\n");

  unsigned char header2 = context.fin->get();

  bool penalty_bytes_stored = (precomp_hdr_flags & std::byte{ 0b10 }) == std::byte{ 0b10 };
  bool recursion_used = (precomp_hdr_flags & std::byte{ 0b10000000 }) == std::byte{ 0b10000000 };
  int level = header2;

  print_to_log(PRECOMP_DEBUG_LOG, "Compression level: %i\n", level);

  // read penalty bytes
  std::vector<char> penalty_bytes {};
  if (penalty_bytes_stored) {
    auto penalty_bytes_len = fin_fget_vlint(*context.fin);
    penalty_bytes.resize(penalty_bytes_len);
    context.fin->read(penalty_bytes.data(), penalty_bytes_len);
  }

  long long recompressed_data_length = fin_fget_vlint(*context.fin);
  long long decompressed_data_length = fin_fget_vlint(*context.fin);

  long long recursion_data_length = 0;
  if (recursion_used) {
    recursion_data_length = fin_fget_vlint(*context.fin);
  }

  if (recursion_used) {
    print_to_log(PRECOMP_DEBUG_LOG, "Recursion data length: %lli\n", recursion_data_length);
  }
  else {
    print_to_log(PRECOMP_DEBUG_LOG, "Recompressed length: %lli - decompressed length: %lli\n", recompressed_data_length, decompressed_data_length);
  }

  long long old_fout_pos = context.fout->tellp();

  long long retval;
  if (recursion_used) {
    recursion_result r = recursion_decompress(context.precomp, recursion_data_length, temp_files_tag() + "_recomp_bzip2");
    auto wrapped_istream_frecurse = WrappedIStream(r.frecurse.get(), false);
    retval = def_part_bzip2(context.precomp, wrapped_istream_frecurse, *context.fout, level, decompressed_data_length, recompressed_data_length);
    r.frecurse->close();
    remove(r.file_name.c_str());
  }
  else {
    retval = def_part_bzip2(context.precomp, *context.fin, *context.fout, level, decompressed_data_length, recompressed_data_length);
  }

  if (retval != BZ_OK) {
    print_to_log(PRECOMP_DEBUG_LOG, "BZIP2 retval = %lli\n", retval);
    throw PrecompError(ERR_DURING_RECOMPRESSION);
  }

  if (penalty_bytes_stored) {
    context.fout->flush();

    long long fsave_fout_pos = context.fout->tellp();
    int pb_pos = 0;
    for (int pbc = 0; pbc < penalty_bytes.size(); pbc += 5) {
      pb_pos = (unsigned char)penalty_bytes[pbc] << 24;
      pb_pos += (unsigned char)penalty_bytes[pbc + 1] << 16;
      pb_pos += (unsigned char)penalty_bytes[pbc + 2] << 8;
      pb_pos += (unsigned char)penalty_bytes[pbc + 3];

      context.fout->seekp(old_fout_pos + pb_pos, std::ios_base::beg);
      context.fout->write(penalty_bytes.data() + pbc + 4, 1);
    }

    context.fout->seekp(fsave_fout_pos, std::ios_base::beg);
  }
}
