#include "bzip2.h"

#include "contrib/bzip2/bzlib.h"

#include <cstddef>
#include <memory>

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

int inf_bzip2(Precomp& precomp_mgr, IStreamLike& source, OStreamLike& dest, long long& compressed_stream_size, long long& decompressed_stream_size, std::span<unsigned char> tmp_out) {
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
      strm.next_out = reinterpret_cast<char*>(tmp_out.data());

      ret = BZ2_bzDecompress(&strm);
      if ((ret != BZ_OK) && (ret != BZ_STREAM_END)) {
        (void)BZ2_bzDecompressEnd(&strm);
        return ret;
      }

      compressed_stream_size += (avail_in_before - strm.avail_in);
      avail_in_before = strm.avail_in;

      have = CHUNK - strm.avail_out;
      dest.write(reinterpret_cast<char*>(tmp_out.data()), have);
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

long long try_to_decompress_bzip2(Precomp& precomp_mgr, IStreamLike& bzip2_stream, long long original_input_pos, long long& compressed_stream_size, PrecompTmpFile& decompressed_stream, std::span<unsigned char> tmp_out) {
  long long r, decompressed_stream_size;

  precomp_mgr.call_progress_callback();

  decompressed_stream.reopen();

  bzip2_stream.seekg(&bzip2_stream == precomp_mgr.ctx->fin.get() ? original_input_pos : 0, std::ios_base::beg);

  r = inf_bzip2(precomp_mgr, bzip2_stream, decompressed_stream, compressed_stream_size, decompressed_stream_size, tmp_out);
  if (r == BZ_OK) return decompressed_stream_size;

  return r;
}

class BZip2FormatHeaderData : public PrecompFormatHeaderData {
public:
  int level;
};

int def_part_bzip2(IStreamLike& source, OStreamLike& dest, int level, unsigned long long decompressed_size, unsigned long long compressed_size, std::span<unsigned char> tmp_out, const std::function<void()>& progress_callback) {
  int flush;
  bz_stream strm;

  /* allocate deflate state */
  strm.bzalloc = nullptr;
  strm.bzfree = nullptr;
  strm.opaque = nullptr;
  int ret = BZ2_bzCompressInit(&strm, level, 0, 0);
  if (ret != BZ_OK)
    return ret;

  long long pos_in = 0;
  long long pos_out = 0;

  /* compress until end of file */
  std::vector<unsigned char> in_buf{};
  in_buf.resize(CHUNK);
  do {
    if ((decompressed_size - pos_in) > CHUNK) {
      progress_callback();

      source.read(reinterpret_cast<char*>(in_buf.data()), CHUNK);
      strm.avail_in = source.gcount();
      pos_in += CHUNK;
      flush = BZ_RUN;
    }
    else {
      source.read(reinterpret_cast<char*>(in_buf.data()), decompressed_size - pos_in);
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
      strm.next_out = reinterpret_cast<char*>(tmp_out.data());

      ret = BZ2_bzCompress(&strm, flush);

      unsigned int have = CHUNK - strm.avail_out;

      if ((pos_out + static_cast<signed int>(have)) > compressed_size) {
        have = compressed_size - pos_out;
      }

      pos_out += have;

      dest.write(reinterpret_cast<char*>(tmp_out.data()), have);
      if (dest.bad()) {
        (void)BZ2_bzCompressEnd(&strm);
        return BZ_DATA_ERROR;
      }
    } while (strm.avail_out == 0);

  } while (flush != BZ_FINISH);

  (void)BZ2_bzCompressEnd(&strm);
  return BZ_OK;
}

std::unique_ptr<precompression_result> BZip2FormatHandler::attempt_precompression(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span, const long long original_input_pos) {
  const int compression_level = *(checkbuf_span.data() + 3) - '0';
  std::unique_ptr<bzip2_precompression_result> result = std::make_unique<bzip2_precompression_result>(compression_level);
  if ((compression_level < 1) || (compression_level > 9)) return result;

  std::unique_ptr<PrecompTmpFile> tmpfile = std::make_unique<PrecompTmpFile>();
  tmpfile->open(precomp_mgr.get_tempfile_name("original_bzip2"), std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);

  // try to decompress at current position
  long long compressed_stream_size = -1;
  auto decompressed_stream_size = try_to_decompress_bzip2(precomp_mgr, *precomp_mgr.ctx->fin, original_input_pos, compressed_stream_size, *tmpfile, tmp_out);
  if (decompressed_stream_size <= 0) return result;

  precomp_mgr.statistics.decompressed_streams_count++;
  precomp_mgr.statistics.decompressed_bzip2_count++;

  print_to_log(PRECOMP_DEBUG_LOG, "Possible bZip2-Stream found at position %lli, compression level = %i\n", original_input_pos, compression_level);
  print_to_log(PRECOMP_DEBUG_LOG, "Compressed size: %lli\n", compressed_stream_size);

  if (PRECOMP_VERBOSITY_LEVEL == PRECOMP_DEBUG_LOG) {
    tmpfile->reopen();
    tmpfile->seekg(0, std::ios_base::end);
    print_to_log(PRECOMP_DEBUG_LOG, "Can be decompressed to %lli bytes\n", tmpfile->tellg());
  }

  tmpfile->reopen();
  if (!tmpfile->is_open()) {
    throw PrecompError(ERR_TEMP_FILE_DISAPPEARED);
  }

  std::string tempfile2 = tmpfile->file_path + "_rec_";
  PrecompTmpFile frecomp;
  frecomp.open(tempfile2, std::ios_base::out | std::ios_base::binary);
  int retval = def_part_bzip2(*tmpfile, frecomp, result->compression_level, decompressed_stream_size, compressed_stream_size, tmp_out, [&precomp_mgr]() { precomp_mgr.call_progress_callback(); });
  if (retval != BZ_OK) {
    print_to_log(PRECOMP_DEBUG_LOG, "BZIP2 retval = %lli\n", retval);
    return result;
  }
  frecomp.close();

  frecomp.open(tempfile2, std::ios_base::in | std::ios_base::binary);
  precomp_mgr.ctx->fin->seekg(original_input_pos, std::ios_base::beg);
  const auto [identical_bytes, penalty_bytes] = compare_files_penalty(precomp_mgr, *precomp_mgr.ctx->fin, frecomp, compressed_stream_size);

  if (
    identical_bytes < precomp_mgr.switches.min_ident_size ||  // reject: too small to matter
    identical_bytes != compressed_stream_size  // reject: doesn't recover the whole original BZip2 stream
  ) {
    print_to_log(PRECOMP_DEBUG_LOG, "No matches\n");
    return result;
  }

  precomp_mgr.statistics.recompressed_streams_count++;
  precomp_mgr.statistics.recompressed_bzip2_count++;

  print_to_log(PRECOMP_DEBUG_LOG, "Best match: %lli bytes, decompressed to %lli bytes\n", identical_bytes, decompressed_stream_size);

  precomp_mgr.ctx->non_zlib_was_used = true;
  result->success = true;

  // check recursion
  tmpfile->close();
  tmpfile->open(tmpfile->file_path, std::ios_base::in | std::ios_base::binary);
  tmpfile->close();

  // write compressed data header (bZip2)
  std::byte header_byte{ 0b1 };
  if (!penalty_bytes.empty()) {
    header_byte |= std::byte{ 0b10 };
  }
  result->flags = header_byte;

  // store penalty bytes, if any
  for (const auto& [pos, patch_byte] : penalty_bytes) {
    result->penalty_bytes.push_back((pos >> 24) % 256);
    result->penalty_bytes.push_back((pos >> 16) % 256);
    result->penalty_bytes.push_back((pos >> 8) % 256);
    result->penalty_bytes.push_back(pos % 256);
    result->penalty_bytes.push_back(patch_byte);
  }
  //result->penalty_bytes = std::move(penalty_bytes);

  result->original_size = compressed_stream_size;
  result->precompressed_size = decompressed_stream_size;

  // write decompressed data
  tmpfile->reopen();
  result->precompressed_stream = std::move(tmpfile);

  return result;
}

std::unique_ptr<PrecompFormatHeaderData> BZip2FormatHandler::read_format_header(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) {
  auto fmt_hdr = std::make_unique<BZip2FormatHeaderData>();
 
  fmt_hdr->level = context.fin->get();

  bool penalty_bytes_stored = (precomp_hdr_flags & std::byte{ 0b10 }) == std::byte{ 0b10 };
  bool recursion_used = (precomp_hdr_flags & std::byte{ 0b10000000 }) == std::byte{ 0b10000000 };

  // read penalty bytes
  if (penalty_bytes_stored) {
    auto penalty_bytes_len = fin_fget_vlint(*context.fin);
    while (penalty_bytes_len > 0) {
      unsigned char pb_data[5];
      context.fin->read(reinterpret_cast<char*>(pb_data), 5);
      penalty_bytes_len -= 5;

      uint32_t next_pb_pos = pb_data[0] << 24;
      next_pb_pos += pb_data[1] << 16;
      next_pb_pos += pb_data[2] << 8;
      next_pb_pos += pb_data[3];

      fmt_hdr->penalty_bytes.emplace(next_pb_pos, pb_data[4]);
    }
  }

  fmt_hdr->original_size = fin_fget_vlint(*context.fin);
  fmt_hdr->precompressed_size = fin_fget_vlint(*context.fin);

  if (recursion_used) {
    fmt_hdr->recursion_data_size = fin_fget_vlint(*context.fin);
  }

  return fmt_hdr;
}

void BZip2FormatHandler::recompress(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format, const Tools& tools) {
  print_to_log(PRECOMP_DEBUG_LOG, "Decompressed data - bZip2\n");
  auto& bzip2_precomp_hdr_data = static_cast<BZip2FormatHeaderData&>(precomp_hdr_data);
  print_to_log(PRECOMP_DEBUG_LOG, "Compression level: %i\n", bzip2_precomp_hdr_data.level);

  if (bzip2_precomp_hdr_data.recursion_data_size > 0) {
    print_to_log(PRECOMP_DEBUG_LOG, "Recursion data length: %lli\n", bzip2_precomp_hdr_data.recursion_data_size);
  }
  else {
    print_to_log(PRECOMP_DEBUG_LOG, "Recompressed length: %lli - decompressed length: %lli\n", bzip2_precomp_hdr_data.original_size, bzip2_precomp_hdr_data.precompressed_size);
  }

  int retval = def_part_bzip2(precompressed_input, recompressed_stream, bzip2_precomp_hdr_data.level, bzip2_precomp_hdr_data.precompressed_size, bzip2_precomp_hdr_data.original_size, tmp_out, tools.progress_callback);
  if (retval != BZ_OK) {
    print_to_log(PRECOMP_DEBUG_LOG, "BZIP2 retval = %lli\n", retval);
    throw PrecompError(ERR_DURING_RECOMPRESSION);
  }
}
