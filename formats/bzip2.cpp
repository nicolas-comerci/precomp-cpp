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
        //dump_penaltybytes_to_outfile(outfile);
        //dump_stream_sizes_to_outfile(outfile);
        //dump_precompressed_data_to_outfile(outfile);
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

class BZip2Compressor {
public:
  bz_stream strm;
  std::array<unsigned char, CHUNK> in_buf{};
  std::array<unsigned char, CHUNK> out_buf{};
  const std::function<void()>& progress_callback;

  BZip2Compressor(const int level, const std::function<void()>& _progress_callback): progress_callback(_progress_callback) {
    /* allocate deflate state */
    strm.bzalloc = nullptr;
    strm.bzfree = nullptr;
    strm.opaque = nullptr;
    int ret = BZ2_bzCompressInit(&strm, level, 0, 0);
    if (ret != BZ_OK)
      throw PrecompError(ERR_DURING_RECOMPRESSION);
  }
  ~BZip2Compressor() {
    BZ2_bzCompressEnd(&strm);
  }

  int compress(IStreamLike& input, unsigned long long count, OStreamLike& output) {
    progress_callback();

    auto remaining_in = count;
    while (remaining_in > 0) {
      auto in_chunk_size = remaining_in > CHUNK ? CHUNK : remaining_in;
      remaining_in -= in_chunk_size;
      int flush = BZ_RUN;
      input.read(reinterpret_cast<char*>(in_buf.data()), in_chunk_size);
      strm.avail_in = input.gcount();      
      if (input.bad()) {
        (void)BZ2_bzCompressEnd(&strm);
        return BZ_PARAM_ERROR;
      }
      strm.next_in = reinterpret_cast<char*>(in_buf.data());

      // Compress all that available data for that input chunk
      do {
        strm.avail_out = CHUNK;
        strm.next_out = reinterpret_cast<char*>(out_buf.data());

        BZ2_bzCompress(&strm, flush);

        unsigned int compressed_chunk_size = CHUNK - strm.avail_out;
        if (compressed_chunk_size > 0) {
          output.write(reinterpret_cast<char*>(out_buf.data()), compressed_chunk_size);
          if (output.bad()) {
            (void)BZ2_bzCompressEnd(&strm);
            return BZ_DATA_ERROR;
          }
        }
      } while (strm.avail_out == 0);
    }
    return BZ_OK;
  }

  int compress_final_block(IStreamLike& input, unsigned long long count, OStreamLike& output) {
    int flush = BZ_FINISH;
    input.read(reinterpret_cast<char*>(in_buf.data()), count);
    strm.avail_in = input.gcount();
    if (input.bad()) {
      (void)BZ2_bzCompressEnd(&strm);
      return BZ_PARAM_ERROR;
    }
    strm.next_in = reinterpret_cast<char*>(in_buf.data());

    // Compress all that available data for that input chunk
    do {
      strm.avail_out = CHUNK;
      strm.next_out = reinterpret_cast<char*>(out_buf.data());

      BZ2_bzCompress(&strm, flush);

      unsigned int compressed_chunk_size = CHUNK - strm.avail_out;
      if (compressed_chunk_size > 0) {
        output.write(reinterpret_cast<char*>(out_buf.data()), compressed_chunk_size);
        if (output.bad()) {
          (void)BZ2_bzCompressEnd(&strm);
          return BZ_DATA_ERROR;
        }
      }
    } while (strm.avail_out == 0);
    return BZ_OK;
  }
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
    if ((decompressed_size - pos_in) > CHUNK) {  // We have at least one more whole chunk left
      progress_callback();

      source.read(reinterpret_cast<char*>(in_buf.data()), CHUNK);
      strm.avail_in = source.gcount();
      pos_in += CHUNK;
      flush = BZ_RUN;
    }
    else {  // We are on the last chunk (or maybe even already ran out of data), finish the stream
      source.read(reinterpret_cast<char*>(in_buf.data()), decompressed_size - pos_in);
      strm.avail_in = source.gcount();
      flush = BZ_FINISH;
    }
    if (source.bad()) {
      (void)BZ2_bzCompressEnd(&strm);
      return BZ_PARAM_ERROR;
    }
    strm.next_in = reinterpret_cast<char*>(in_buf.data());

    // Compress all that available data for that input chunk
    do {
      strm.avail_out = CHUNK;
      strm.next_out = reinterpret_cast<char*>(tmp_out.data());

      ret = BZ2_bzCompress(&strm, flush);

      unsigned int compressed_chunk_size = CHUNK - strm.avail_out;

      if ((pos_out + static_cast<signed int>(compressed_chunk_size)) > compressed_size) {
        compressed_chunk_size = compressed_size - pos_out;
      }

      pos_out += compressed_chunk_size;

      dest.write(reinterpret_cast<char*>(tmp_out.data()), compressed_chunk_size);
      if (dest.bad()) {
        (void)BZ2_bzCompressEnd(&strm);
        return BZ_DATA_ERROR;
      }
    } while (strm.avail_out == 0);

  } while (flush != BZ_FINISH);

  (void)BZ2_bzCompressEnd(&strm);
  return BZ_OK;
}

std::unique_ptr<precompression_result> BZip2FormatHandler::attempt_precompression(Precomp& precomp_mgr, std::unique_ptr<PrecompTmpFile>&& precompressed, const std::span<unsigned char> checkbuf_span, const long long original_input_pos) {
  const int compression_level = *(checkbuf_span.data() + 3) - '0';
  std::unique_ptr<bzip2_precompression_result> result = std::make_unique<bzip2_precompression_result>(compression_level);
  if ((compression_level < 1) || (compression_level > 9)) return result;

  // try to decompress at current position
  long long compressed_stream_size = -1;
  const auto decompressed_stream_size = try_to_decompress_bzip2(precomp_mgr, *precomp_mgr.ctx->fin, original_input_pos, compressed_stream_size, *precompressed, tmp_out);
  if (decompressed_stream_size <= 0) return result;

  precomp_mgr.statistics.decompressed_streams_count++;
  precomp_mgr.statistics.decompressed_bzip2_count++;

  print_to_log(PRECOMP_DEBUG_LOG, "Possible bZip2-Stream found at position %lli, compression level = %i\n", original_input_pos, compression_level);
  print_to_log(PRECOMP_DEBUG_LOG, "Compressed size: %lli\n", compressed_stream_size);

  if (PRECOMP_VERBOSITY_LEVEL == PRECOMP_DEBUG_LOG) {
    precompressed->reopen();
    precompressed->seekg(0, std::ios_base::end);
    print_to_log(PRECOMP_DEBUG_LOG, "Can be decompressed to %lli bytes\n", precompressed->tellg());
  }

  precompressed->reopen();
  if (!precompressed->is_open()) {
    throw PrecompError(ERR_TEMP_FILE_DISAPPEARED);
  }

  /*
  std::string tempfile2 = precompressed->file_path + "_rec_";
  PrecompTmpFile frecomp;
  frecomp.open(tempfile2, std::ios_base::out | std::ios_base::binary);
  int retval = def_part_bzip2(*precompressed, frecomp, result->compression_level, decompressed_stream_size, compressed_stream_size, tmp_out, [&precomp_mgr]() { precomp_mgr.call_progress_callback(); });
  if (retval != BZ_OK) {
    print_to_log(PRECOMP_DEBUG_LOG, "BZIP2 retval = %lli\n", retval);
    return result;
  }
  frecomp.close();

  frecomp.open(tempfile2, std::ios_base::in | std::ios_base::binary);
  precomp_mgr.ctx->fin->seekg(original_input_pos, std::ios_base::beg);
  auto [identical_bytes, penalty_bytes] = compare_files_penalty(precomp_mgr, *precomp_mgr.ctx->fin, frecomp, compressed_stream_size);

  if (
    identical_bytes < precomp_mgr.switches.min_ident_size ||  // reject: too small to matter
    identical_bytes != compressed_stream_size  // reject: doesn't recover the whole original BZip2 stream
  ) {
    print_to_log(PRECOMP_DEBUG_LOG, "No matches\n");
    return result;
  }
  
  print_to_log(PRECOMP_DEBUG_LOG, "Best match: %lli bytes, decompressed to %lli bytes\n", identical_bytes, decompressed_stream_size);
  */

  std::vector<std::tuple<uint32_t, char>> penalty_bytes{};
  precomp_mgr.statistics.recompressed_streams_count++;
  precomp_mgr.statistics.recompressed_bzip2_count++;

  precomp_mgr.ctx->non_zlib_was_used = true;
  result->success = true;

  // check recursion
  precompressed->close();
  precompressed->open(precompressed->file_path, std::ios_base::in | std::ios_base::binary);
  precompressed->close();

  // write compressed data header (bZip2)
  std::byte header_byte{ 0b1 };
  if (!penalty_bytes.empty()) {
    header_byte |= std::byte{ 0b10 };
  }
  result->flags = header_byte;

  // store penalty bytes, if any
  result->penalty_bytes = std::move(penalty_bytes);

  result->original_size = compressed_stream_size;
  result->precompressed_size = decompressed_stream_size;

  // write decompressed data
  precompressed->reopen();
  result->precompressed_stream = std::move(precompressed);

  return result;
}

std::unique_ptr<PrecompFormatHeaderData> BZip2FormatHandler::read_format_header(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) {
  auto fmt_hdr = std::make_unique<BZip2FormatHeaderData>();
  fmt_hdr->level = context.fin->get();
  return fmt_hdr;
}

void BZip2FormatHandler::recompress(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format, const Tools& tools) {
  print_to_log(PRECOMP_DEBUG_LOG, "Decompressed data - bZip2\n");
  auto& bzip2_precomp_hdr_data = static_cast<BZip2FormatHeaderData&>(precomp_hdr_data);
  print_to_log(PRECOMP_DEBUG_LOG, "Compression level: %i\n", bzip2_precomp_hdr_data.level);

  auto compressor = BZip2Compressor(bzip2_precomp_hdr_data.level, tools.progress_callback);
  while (true) {
    std::byte data_hdr = static_cast<std::byte>(precompressed_input.get());
    auto data_block_size = fin_fget_vlint(precompressed_input);

    bool last_block = (data_hdr & std::byte{ 0b1000000 }) == std::byte{ 0b1000000 };
    bool finish_bzip_stream = (data_hdr & std::byte{ 0b0000001 }) == std::byte{ 0b0000001 };
    int retval;
    if (last_block && finish_bzip_stream) {
      retval = compressor.compress_final_block(precompressed_input, data_block_size, recompressed_stream);
    }
    else if (finish_bzip_stream && !last_block) {
      throw PrecompError(ERR_DURING_RECOMPRESSION);  // trying to finish stream when there are more blocks coming, no-go, something's wrong here, bail
    }
    else {
      retval = compressor.compress(precompressed_input, data_block_size, recompressed_stream);
    }
    if (retval != BZ_OK) {
      print_to_log(PRECOMP_DEBUG_LOG, "BZIP2 retval = %lli\n", retval);
      throw PrecompError(ERR_DURING_RECOMPRESSION);
    }
    
    if (last_block) break;
  }
}
