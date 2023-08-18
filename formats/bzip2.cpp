#include "bzip2.h"

#include "contrib/bzip2/bzlib.h"

#include <cstddef>
#include <memory>
#include <cstring>

class bzip2_precompression_result : public precompression_result {
public:
    int compression_level;

    explicit bzip2_precompression_result(int compression_level) : precompression_result(D_BZIP2), compression_level(compression_level) {}

    void dump_to_outfile(OStreamLike& outfile) const override {
        dump_header_to_outfile(outfile);
        outfile.put(compression_level);
        dump_penaltybytes_to_outfile(outfile);
        dump_stream_sizes_to_outfile(outfile);
        dump_precompressed_data_to_outfile(outfile);
    }
};

bool BZip2FormatHandler::quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) {
  auto checkbuf = buffer.data();
  // BZhx = header, x = compression level/blocksize (1-9)
  return (*checkbuf == 'B') && (*(checkbuf + 1) == 'Z') && (*(checkbuf + 2) == 'h');
}

constexpr auto DEF_COMPARE_CHUNK = 512;
unsigned char input_bytes1[DEF_COMPARE_CHUNK];

long long compare_file_mem_penalty(IStreamLike& file1, const unsigned char* input_bytes2, long long pos1, long long bytecount, long long& total_same_byte_count, long long& total_same_byte_count_penalty, long long& rek_same_byte_count, long long& rek_same_byte_count_penalty, long long& rek_penalty_bytes_len, long long& local_penalty_bytes_len, bool& use_penalty_bytes, std::vector<char>& penalty_bytes) {
  int same_byte_count = 0;
  std::streamsize size1;
  int i;

  unsigned long long old_pos = file1.tellg();
  file1.seekg(pos1, std::ios_base::beg);

  file1.read(reinterpret_cast<char*>(input_bytes1), bytecount);
  size1 = file1.gcount();

  for (i = 0; i < size1; i++) {
    if (input_bytes1[i] == input_bytes2[i]) {
      same_byte_count++;
      total_same_byte_count_penalty++;
    }
    else {
      total_same_byte_count_penalty -= 5; // 4 bytes = position, 1 byte = new byte

      // stop, if local_penalty_bytes_len gets too big
      if ((local_penalty_bytes_len + 5) >= MAX_PENALTY_BYTES) {
        break;
      }

      local_penalty_bytes_len += 5;
      penalty_bytes.resize(local_penalty_bytes_len);
      // position
      penalty_bytes[local_penalty_bytes_len - 5] = (total_same_byte_count >> 24) % 256;
      penalty_bytes[local_penalty_bytes_len - 4] = (total_same_byte_count >> 16) % 256;
      penalty_bytes[local_penalty_bytes_len - 3] = (total_same_byte_count >> 8) % 256;
      penalty_bytes[local_penalty_bytes_len - 2] = total_same_byte_count % 256;
      // new byte
      penalty_bytes[local_penalty_bytes_len - 1] = input_bytes1[i];
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

std::tuple<long long, long long, std::vector<char>> def_compare_bzip2(Precomp& precomp_mgr, IStreamLike& decompressed_stream, IStreamLike& original_bzip2, long long original_input_pos, int level) {
  int ret, flush;
  unsigned have;
  bz_stream strm;
  long long identical_bytes_compare;

  long long comp_pos = 0;
  long long decompressed_bytes_used = 0;
  std::vector<char> penalty_bytes;

  /* allocate deflate state */
  strm.bzalloc = nullptr;
  strm.bzfree = nullptr;
  strm.opaque = nullptr;
  ret = BZ2_bzCompressInit(&strm, level, 0, 0);
  if (ret != BZ_OK)
    return { ret, decompressed_bytes_used, std::move(penalty_bytes) };

  long long total_same_byte_count = 0;
  long long total_same_byte_count_penalty = 0;
  long long rek_same_byte_count = 0;
  long long rek_same_byte_count_penalty = -1;
  long long rek_penalty_bytes_len = 0;
  long long local_penalty_bytes_len = 0;
  bool use_penalty_bytes = false;

  /* compress until end of file */
  std::vector<unsigned char> in_buf{};
  in_buf.resize(DEF_COMPARE_CHUNK);
  do {
    precomp_mgr.call_progress_callback();

    decompressed_stream.read(reinterpret_cast<char*>(in_buf.data()), DEF_COMPARE_CHUNK);
    strm.avail_in = decompressed_stream.gcount();
    if (decompressed_stream.bad()) {
      (void)BZ2_bzCompressEnd(&strm);
      return { BZ_PARAM_ERROR, decompressed_bytes_used, std::move(penalty_bytes) };
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
          identical_bytes_compare = compare_file_mem_penalty(original_bzip2, precomp_mgr.ctx->tmp_out, original_input_pos + comp_pos, have, total_same_byte_count, total_same_byte_count_penalty, rek_same_byte_count, rek_same_byte_count_penalty, rek_penalty_bytes_len, local_penalty_bytes_len, use_penalty_bytes, penalty_bytes);
        }
        else {
          identical_bytes_compare = compare_file_mem_penalty(original_bzip2, precomp_mgr.ctx->tmp_out, comp_pos, have, total_same_byte_count, total_same_byte_count_penalty, rek_same_byte_count, rek_same_byte_count_penalty, rek_penalty_bytes_len, local_penalty_bytes_len, use_penalty_bytes, penalty_bytes);
        }
      }

      if (have > 0) {
        if (static_cast<unsigned int>(identical_bytes_compare) < (have >> 1)) {
          (void)BZ2_bzCompressEnd(&strm);

          if ((rek_penalty_bytes_len > 0) && (use_penalty_bytes)) penalty_bytes.resize(rek_penalty_bytes_len);
          return { rek_same_byte_count, decompressed_bytes_used, std::move(penalty_bytes) };
        }
      }

      comp_pos += have;

    } while (strm.avail_out == 0);

  } while (flush != BZ_FINISH);

  (void)BZ2_bzCompressEnd(&strm);
  if ((rek_penalty_bytes_len > 0) && (use_penalty_bytes)) penalty_bytes.resize(rek_penalty_bytes_len);
  return { rek_same_byte_count, decompressed_bytes_used, std::move(penalty_bytes) };
}

std::tuple<long long, long long, std::vector<char>> file_recompress_bzip2(Precomp& precomp_mgr, IStreamLike& bzip2_stream, long long original_input_pos, int level, long long& decompressed_bytes_total, IStreamLike& decompressed_stream) {
  decompressed_stream.seekg(0, std::ios_base::end);
  decompressed_bytes_total = decompressed_stream.tellg();

  decompressed_stream.seekg(0, std::ios_base::beg);
  auto [retval, decompressed_bytes_used, penalty_bytes] = def_compare_bzip2(precomp_mgr, decompressed_stream, bzip2_stream, original_input_pos, level);
  return { retval < 0 ? -1 : retval, decompressed_bytes_used, std::move(penalty_bytes) };
}

[[nodiscard]] std::tuple<long long, long long, std::vector<char>> try_recompress_bzip2(Precomp& precomp_mgr, IStreamLike& origfile, long long original_input_pos, int level, long long& compressed_stream_size, IStreamLike& tmpfile) {
  precomp_mgr.call_progress_callback();

  long long decomp_bytes_total;
  std::vector<char> best_penalty_bytes;
  long long best_identical_bytes;
  long long best_identical_bytes_decomp;
  auto [identical_bytes, identical_bytes_decomp, penalty_bytes] = file_recompress_bzip2(precomp_mgr, origfile, original_input_pos, level, decomp_bytes_total, tmpfile);
  if (identical_bytes > -1) { // successfully recompressed?
    if (identical_bytes > precomp_mgr.switches.min_ident_size) {
      print_to_log(PRECOMP_DEBUG_LOG, "Identical recompressed bytes: %lli of %lli\n", identical_bytes, compressed_stream_size);
      print_to_log(PRECOMP_DEBUG_LOG, "Identical decompressed bytes: %lli of %lli\n", identical_bytes_decomp, decomp_bytes_total);
    }

    // Partial matches sometimes need all the decompressed bytes, but there are much less
    // identical recompressed bytes - in these cases, all the decompressed bytes have to
    // be stored together with the remaining recompressed bytes, so the result won't compress
    // better than the original stream. What's important here is the ratio between recompressed ratio
    // and decompressed ratio that shouldn't get too high.
    // Example: A stream has 5 of 1000 identical recompressed bytes, but needs 1000 of 1000 decompressed bytes,
    // so the ratio is (1000/1000)/(5/1000) = 200 which is too high. With 5 of 1000 decompressed bytes or
    // 1000 of 1000 identical recompressed bytes, ratio would've been 1 and we'd accept it.

    float partial_ratio = (static_cast<float>(identical_bytes_decomp) / decomp_bytes_total) / (static_cast<float>(identical_bytes) / compressed_stream_size);
    if (partial_ratio < 3.0f) {
      best_identical_bytes_decomp = identical_bytes_decomp;
      best_identical_bytes = identical_bytes;
      if (penalty_bytes.size() > 0) {
        best_penalty_bytes.resize(penalty_bytes.size());
        memcpy(best_penalty_bytes.data(), penalty_bytes.data(), penalty_bytes.size());
      }
    }
    else {
      print_to_log(PRECOMP_DEBUG_LOG, "Not enough identical recompressed bytes\n");
    }
  }

  return { best_identical_bytes, best_identical_bytes_decomp, best_penalty_bytes };
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

  decompressed_stream.reopen();

  bzip2_stream.seekg(&bzip2_stream == precomp_mgr.ctx->fin.get() ? original_input_pos : 0, std::ios_base::beg);

  r = inf_bzip2(precomp_mgr, bzip2_stream, decompressed_stream, compressed_stream_size, decompressed_stream_size);
  if (r == BZ_OK) return decompressed_stream_size;

  return r;
}

std::unique_ptr<precompression_result> BZip2FormatHandler::attempt_precompression(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span, const long long original_input_pos) {
  const int compression_level = *(checkbuf_span.data() + 3) - '0';
  std::unique_ptr<bzip2_precompression_result> result = std::make_unique<bzip2_precompression_result>(compression_level);
  if ((compression_level < 1) || (compression_level > 9)) return result;

  std::unique_ptr<PrecompTmpFile> tmpfile = std::make_unique<PrecompTmpFile>();
  tmpfile->open(precomp_mgr.get_tempfile_name("original_bzip2"), std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);

  // try to decompress at current position
  long long compressed_stream_size = -1;
  auto retval = try_to_decompress_bzip2(precomp_mgr, *precomp_mgr.ctx->fin, original_input_pos, compressed_stream_size, *tmpfile);

  if (retval > 0) { // seems to be a zLib-Stream

    precomp_mgr.statistics.decompressed_streams_count++;
    precomp_mgr.statistics.decompressed_bzip2_count++;

    print_to_log(PRECOMP_DEBUG_LOG, "Possible bZip2-Stream found at position %lli, compression level = %i\n", original_input_pos, compression_level);
    print_to_log(PRECOMP_DEBUG_LOG, "Compressed size: %lli\n", compressed_stream_size);

    if (PRECOMP_VERBOSITY_LEVEL == PRECOMP_DEBUG_LOG) {
      tmpfile->reopen();
      tmpfile->seekg(0, std::ios_base::end);
      print_to_log(PRECOMP_DEBUG_LOG, "Can be decompressed to %lli bytes\n", tmpfile->tellg());
    }
  }

  tmpfile->reopen();
  if (!tmpfile->is_open()) {
    throw PrecompError(ERR_TEMP_FILE_DISAPPEARED);
  }
  auto [best_identical_bytes, best_identical_bytes_decomp, best_penalty_bytes] = try_recompress_bzip2(precomp_mgr, *precomp_mgr.ctx->fin, original_input_pos, compression_level, compressed_stream_size, *tmpfile);

  if ((best_identical_bytes > precomp_mgr.switches.min_ident_size) && (best_identical_bytes < best_identical_bytes_decomp)) {
    precomp_mgr.statistics.recompressed_streams_count++;
    precomp_mgr.statistics.recompressed_bzip2_count++;

    print_to_log(PRECOMP_DEBUG_LOG, "Best match: %lli bytes, decompressed to %lli bytes\n", best_identical_bytes, best_identical_bytes_decomp);

    precomp_mgr.ctx->non_zlib_was_used = true;
    result->success = true;

    // check recursion
    tmpfile->close();
    tmpfile->open(tmpfile->file_path, std::ios_base::in | std::ios_base::binary);
    tmpfile->close();

    // write compressed data header (bZip2)

    std::byte header_byte{ 0b1 };
    if (best_penalty_bytes.size() != 0) {
      header_byte |= std::byte{ 0b10 };
    }
    result->flags = header_byte;

    // store penalty bytes, if any
    result->penalty_bytes = std::move(best_penalty_bytes);

    result->original_size = best_identical_bytes;
    result->precompressed_size = best_identical_bytes_decomp;

    // write decompressed data
    tmpfile->reopen();
    result->precompressed_stream = std::move(tmpfile);
  }
  else {
    print_to_log(PRECOMP_DEBUG_LOG, "No matches\n");
  }

  return result;
}

void get_next_penalty_byte_and_pos(long long& penalty_bytes_index, std::optional<char>& next_penalty_byte, std::optional<uint32_t>& next_penalty_byte_pos, std::vector<unsigned char>& penalty_bytes) {
  if (penalty_bytes.size() == penalty_bytes_index) {
    next_penalty_byte = std::nullopt;
    next_penalty_byte_pos = std::nullopt;
    return;
  }
  uint32_t pb_pos = static_cast<unsigned char>(penalty_bytes[penalty_bytes_index]) << 24;
  pb_pos += static_cast<unsigned char>(penalty_bytes[penalty_bytes_index + 1]) << 16;
  pb_pos += static_cast<unsigned char>(penalty_bytes[penalty_bytes_index + 2]) << 8;
  pb_pos += static_cast<unsigned char>(penalty_bytes[penalty_bytes_index + 3]);
  next_penalty_byte_pos = pb_pos;

  next_penalty_byte = penalty_bytes[penalty_bytes_index + 4];
  penalty_bytes_index += 5;
}

class BZip2FormatHeaderData : public PrecompFormatHeaderData {
public:
  int level;
};

int def_part_bzip2(RecursionContext& precomp_ctx, IStreamLike& source, OStreamLike& dest, BZip2FormatHeaderData& bzip2_precomp_hdr_data) {
  int flush;
  bz_stream strm;

  /* allocate deflate state */
  strm.bzalloc = nullptr;
  strm.bzfree = nullptr;
  strm.opaque = nullptr;
  int ret = BZ2_bzCompressInit(&strm, bzip2_precomp_hdr_data.level, 0, 0);
  if (ret != BZ_OK)
    return ret;

  long long pos_in = 0;
  long long pos_out = 0;

  std::optional<char> next_penalty_byte = std::nullopt;
  std::optional<uint32_t> next_penalty_byte_pos = std::nullopt;
  long long penalty_bytes_index = 0;
  get_next_penalty_byte_and_pos(penalty_bytes_index, next_penalty_byte, next_penalty_byte_pos, bzip2_precomp_hdr_data.penalty_bytes);

  /* compress until end of file */
  std::vector<unsigned char> in_buf{};
  in_buf.resize(CHUNK);
  do {
    if ((bzip2_precomp_hdr_data.precompressed_size - pos_in) > CHUNK) {
      precomp_ctx.precomp.call_progress_callback();

      source.read(reinterpret_cast<char*>(in_buf.data()), CHUNK);
      strm.avail_in = source.gcount();
      pos_in += CHUNK;
      flush = BZ_RUN;
    }
    else {
      source.read(reinterpret_cast<char*>(in_buf.data()), bzip2_precomp_hdr_data.precompressed_size - pos_in);
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
      strm.next_out = reinterpret_cast<char*>(precomp_ctx.tmp_out);

      ret = BZ2_bzCompress(&strm, flush);

      unsigned int have = CHUNK - strm.avail_out;

      if ((pos_out + static_cast<signed int>(have)) > bzip2_precomp_hdr_data.original_size) {
        have = bzip2_precomp_hdr_data.original_size - pos_out;
      }

      while (next_penalty_byte_pos.has_value() && next_penalty_byte_pos.value() < pos_out + static_cast<signed int>(have)) {
        uint32_t next_penalty_byte_pos_in_buffer = next_penalty_byte_pos.value() - pos_out;
        precomp_ctx.tmp_out[next_penalty_byte_pos_in_buffer] = next_penalty_byte.value();
        get_next_penalty_byte_and_pos(penalty_bytes_index, next_penalty_byte, next_penalty_byte_pos, bzip2_precomp_hdr_data.penalty_bytes);
      }

      pos_out += have;

      dest.write(reinterpret_cast<char*>(precomp_ctx.tmp_out), have);
      if (dest.bad()) {
        (void)BZ2_bzCompressEnd(&strm);
        return BZ_DATA_ERROR;
      }
    } while (strm.avail_out == 0);

  } while (flush != BZ_FINISH);

  (void)BZ2_bzCompressEnd(&strm);
  return BZ_OK;
}

std::unique_ptr<PrecompFormatHeaderData> BZip2FormatHandler::read_format_header(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) {
  auto fmt_hdr = std::make_unique<BZip2FormatHeaderData>();
 
  fmt_hdr->level = context.fin->get();

  bool penalty_bytes_stored = (precomp_hdr_flags & std::byte{ 0b10 }) == std::byte{ 0b10 };
  bool recursion_used = (precomp_hdr_flags & std::byte{ 0b10000000 }) == std::byte{ 0b10000000 };

  // read penalty bytes
  if (penalty_bytes_stored) {
    auto penalty_bytes_len = fin_fget_vlint(*context.fin);
    fmt_hdr->penalty_bytes.resize(penalty_bytes_len);
    context.fin->read(reinterpret_cast<char*>(fmt_hdr->penalty_bytes.data()), penalty_bytes_len);
  }

  fmt_hdr->original_size = fin_fget_vlint(*context.fin);
  fmt_hdr->precompressed_size = fin_fget_vlint(*context.fin);

  if (recursion_used) {
    fmt_hdr->recursion_data_size = fin_fget_vlint(*context.fin);
  }

  return fmt_hdr;
}

void BZip2FormatHandler::recompress(RecursionContext& context, PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format) {
  print_to_log(PRECOMP_DEBUG_LOG, "Decompressed data - bZip2\n");
  auto& bzip2_precomp_hdr_data = static_cast<BZip2FormatHeaderData&>(precomp_hdr_data);
  print_to_log(PRECOMP_DEBUG_LOG, "Compression level: %i\n", bzip2_precomp_hdr_data.level);

  if (bzip2_precomp_hdr_data.recursion_data_size > 0) {
    print_to_log(PRECOMP_DEBUG_LOG, "Recursion data length: %lli\n", bzip2_precomp_hdr_data.recursion_data_size);
  }
  else {
    print_to_log(PRECOMP_DEBUG_LOG, "Recompressed length: %lli - decompressed length: %lli\n", bzip2_precomp_hdr_data.original_size, bzip2_precomp_hdr_data.precompressed_size);
  }

  int retval;
  if (bzip2_precomp_hdr_data.recursion_data_size) {
    auto r = recursion_decompress(context, bzip2_precomp_hdr_data.recursion_data_size, context.precomp.get_tempfile_name("recomp_bzip2"));
    retval = def_part_bzip2(context, *r, *context.fout, bzip2_precomp_hdr_data);
    r->get_recursion_return_code();
  }
  else {
    retval = def_part_bzip2(context, *context.fin, *context.fout, bzip2_precomp_hdr_data);
  }

  if (retval != BZ_OK) {
    print_to_log(PRECOMP_DEBUG_LOG, "BZIP2 retval = %lli\n", retval);
    throw PrecompError(ERR_DURING_RECOMPRESSION);
  }
}
