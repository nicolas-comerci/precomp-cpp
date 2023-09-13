#include "bzip2.h"

#include "contrib/bzip2/bzlib.h"

#include <cstddef>
#include <memory>

bool BZip2FormatHandler::quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) {
  auto checkbuf = buffer.data();
  // BZhx = header, x = compression level/blocksize (1-9)
  const bool has_BZh_magic = (*checkbuf == 'B') && (*(checkbuf + 1) == 'Z') && (*(checkbuf + 2) == 'h');
  const int compression_level = *(checkbuf + 3) - '0';
  const bool valid_compression_level = (compression_level >= 1) && (compression_level <= 9);
  return has_BZh_magic && valid_compression_level;
}

class Bzip2Decompressor: public PrecompFormatPrecompressor {
  bz_stream strm;
  int compression_level;

public:
  bool stream_failed = false;

  Bzip2Decompressor(const std::span<unsigned char>& buffer, const std::function<void()>& _progress_callback):
    PrecompFormatPrecompressor(buffer, _progress_callback), compression_level(*(buffer.data() + 3) - '0')
  {
    strm.bzalloc = nullptr;
    strm.bzfree = nullptr;
    strm.opaque = nullptr;
    strm.avail_in = 0;
    strm.next_in = nullptr;
    auto ret = BZ2_bzDecompressInit(&strm, 0, 0);
    if (ret != BZ_OK)
      throw PrecompError(ERR_GENERIC_OR_UNKNOWN);
  }

  ~Bzip2Decompressor() override {
    BZ2_bzDecompressEnd(&strm);
  }

  PrecompProcessorReturnCode process() override {
    if (stream_failed) return PrecompProcessorReturnCode::PP_ERROR;
    progress_callback();

    strm.avail_in = avail_in;
    if (strm.avail_in == 0) return PrecompProcessorReturnCode::PP_ERROR;
    strm.next_in = reinterpret_cast<char*>(in_buf.data());

    strm.avail_out = CHUNK;
    strm.next_out = reinterpret_cast<char*>(out_buf.data());

    int ret = BZ2_bzDecompress(&strm);
    if ((ret != BZ_OK) && (ret != BZ_STREAM_END)) {
      stream_failed = true;
      return PP_ERROR;
    }

    avail_in = strm.avail_in;
    avail_out = CHUNK - strm.avail_out;
    return ret == BZ_OK ? PrecompProcessorReturnCode::PP_OK : PrecompProcessorReturnCode::PP_STREAM_END;
  }

  void dump_extra_header_data(OStreamLike& output) override {
    output.put(compression_level);
  }
};

class BZip2FormatHeaderData : public PrecompFormatHeaderData {
public:
  int level;
};

class BZip2Compressor : public PrecompFormatRecompressor {
  bz_stream strm;

  int _compress_block(IStreamLike& input, unsigned long long count, OStreamLike& output, int flush = BZ_RUN) {
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

public:
  BZip2Compressor(const PrecompFormatHeaderData& precomp_hdr_data, const std::function<void()>& _progress_callback):
    PrecompFormatRecompressor(precomp_hdr_data, _progress_callback) {
    const auto& bzip2_hdr_data = static_cast<const BZip2FormatHeaderData&>(precomp_hdr_data);
    /* allocate deflate state */
    strm.bzalloc = nullptr;
    strm.bzfree = nullptr;
    strm.opaque = nullptr;
    int ret = BZ2_bzCompressInit(&strm, bzip2_hdr_data.level, 0, 0);
    if (ret != BZ_OK)
      throw PrecompError(ERR_DURING_RECOMPRESSION);
  }
  ~BZip2Compressor() {
    BZ2_bzCompressEnd(&strm);
  }

  PrecompProcessorReturnCode recompress(IStreamLike& input, unsigned long long count, OStreamLike& output) override {
    progress_callback();

    auto remaining_in = count;
    while (remaining_in > 0) {
      auto in_chunk_size = remaining_in > CHUNK ? CHUNK : remaining_in;
      remaining_in -= in_chunk_size;
      auto ret = _compress_block(input, count, output, BZ_RUN);
      if (ret != BZ_OK) return PP_ERROR;
    }
    return PP_OK;
  }

  PrecompProcessorReturnCode recompress_final_block(IStreamLike& input, unsigned long long count, OStreamLike& output) override {
    return _compress_block(input, count, output, BZ_FINISH) == BZ_OK ? PP_OK : PP_ERROR;
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

std::unique_ptr<PrecompFormatPrecompressor> BZip2FormatHandler::make_precompressor(Precomp& precomp_mgr, const std::span<unsigned char>& buffer) {
  return std::make_unique<Bzip2Decompressor>(buffer, [&precomp_mgr]() { precomp_mgr.call_progress_callback(); });
}

/*
std::unique_ptr<precompression_result> attempt_precompression(Precomp& precomp_mgr, std::unique_ptr<PrecompTmpFile>&& precompressed, const std::span<unsigned char> checkbuf_span, const long long original_input_pos) {
  const int compression_level = *(checkbuf_span.data() + 3) - '0';
  std::unique_ptr<bzip2_precompression_result> result = std::make_unique<bzip2_precompression_result>(compression_level);

  // try to decompress at current position
  precomp_mgr.call_progress_callback();

  precompressed->reopen();
  precomp_mgr.ctx->fin->seekg(original_input_pos, std::ios_base::beg);

  long long compressed_stream_size = 0;
  long long decompressed_stream_size = 0;
  auto decompressor = Bzip2Decompressor([&precomp_mgr]() { precomp_mgr.call_progress_callback(); });
  while (true) {
    precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(decompressor.in_buf.data()), CHUNK);
    decompressor.avail_in = precomp_mgr.ctx->fin->gcount();
    const auto avail_in_before = decompressor.avail_in;
    if (precomp_mgr.ctx->fin->bad()) return result;

    auto ret = decompressor.process();
    if (ret != PP_OK && ret != PP_STREAM_END) return result;

    // This should mostly be for when the stream ends, we might have read extra data beyond the end of it, which will not have been consumed by the process
    // and shouldn't be counted towards the stream's size
    compressed_stream_size += (avail_in_before - decompressor.avail_in);

    const auto decompressed_chunk_size = decompressor.avail_out;
    decompressed_stream_size += decompressed_chunk_size;
    precompressed->write(reinterpret_cast<char*>(decompressor.out_buf.data()), decompressed_chunk_size);
    if (precompressed->bad()) return result;
    if (ret == PP_STREAM_END) break;  // maybe we should also check fin for eof? we could support partial/broken BZip2 streams
  }

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
*/

std::unique_ptr<PrecompFormatHeaderData> BZip2FormatHandler::read_format_header(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) {
  auto fmt_hdr = std::make_unique<BZip2FormatHeaderData>();
  fmt_hdr->level = context.fin->get();
  return fmt_hdr;
}

std::unique_ptr<PrecompFormatRecompressor> BZip2FormatHandler::make_recompressor(PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format, const Tools& tools) {
  print_to_log(PRECOMP_DEBUG_LOG, "Decompressed data - bZip2\n");
  print_to_log(PRECOMP_DEBUG_LOG, "Compression level: %i\n", static_cast<BZip2FormatHeaderData&>(precomp_hdr_data).level);

  return std::make_unique<BZip2Compressor>(precomp_hdr_data, tools.progress_callback);
}
