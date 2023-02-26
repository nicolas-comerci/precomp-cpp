#include "gif.h"

#include "contrib/giflib/precomp_gif.h"

#include <cstddef>

gif_precompression_result::gif_precompression_result() : precompression_result(D_GIF) {}
void gif_precompression_result::dump_gif_diff_to_outfile(Precomp& precomp_mgr) {
  // store diff bytes
  fout_fput_vlint(*precomp_mgr.ctx->fout, gif_diff.size());
  if (!gif_diff.empty())
    print_to_log(PRECOMP_DEBUG_LOG, "Diff bytes were used: %i bytes\n", gif_diff.size());
  for (unsigned char dbc : gif_diff) {
    precomp_mgr.ctx->fout->put(dbc);
  }
}
void gif_precompression_result::dump_to_outfile(Precomp& precomp_mgr) {
  dump_header_to_outfile(precomp_mgr);
  dump_gif_diff_to_outfile(precomp_mgr);
  dump_penaltybytes_to_outfile(precomp_mgr);
  dump_stream_sizes_to_outfile(precomp_mgr);
  dump_precompressed_data_to_outfile(precomp_mgr);
}

int DGifGetLineByte(GifFileType* GifFile, GifPixelType* Line, int LineLen, GifCodeStruct* g)
{
  auto LineBuf = new GifPixelType[LineLen];
  memcpy(LineBuf, Line, LineLen);
  int result = DGifGetLine(GifFile, LineBuf, g, LineLen);
  memcpy(Line, LineBuf, LineLen);
  delete[] LineBuf;

  return result;
}

bool newgif_may_write;
OStreamLike* frecompress_gif = nullptr;
IStreamLike* freadfunc = nullptr;

int readFunc(GifFileType* GifFile, GifByteType* buf, int count)
{
  freadfunc->read(reinterpret_cast<char*>(buf), count);
  return freadfunc->gcount();
}

int writeFunc(GifFileType* GifFile, const GifByteType* buf, int count)
{
  if (newgif_may_write) {
    frecompress_gif->write(reinterpret_cast<char*>(const_cast<GifByteType*>(buf)), count);
    return frecompress_gif->bad() ? 0 : count;
  }
  else {
    return count;
  }
}

unsigned char** alloc_gif_screenbuf(GifFileType* myGifFile) {
  if (myGifFile->SHeight <= 0 || myGifFile->SWidth <= 0) {
    return nullptr;
  }
  auto ScreenBuff = new unsigned char* [myGifFile->SHeight];
  for (int i = 0; i < myGifFile->SHeight; i++) {
    ScreenBuff[i] = new unsigned char[myGifFile->SWidth];
  }

  for (int i = 0; i < myGifFile->SWidth; i++)  /* Set its color to BackGround. */
    ScreenBuff[0][i] = myGifFile->SBackGroundColor;
  for (int i = 1; i < myGifFile->SHeight; i++) {
    memcpy(ScreenBuff[i], ScreenBuff[0], myGifFile->SWidth);
  }
  return ScreenBuff;
}

void free_gif_screenbuf(unsigned char** ScreenBuff, GifFileType* myGifFile) {
  if (ScreenBuff != nullptr) {
    for (int i = 0; i < myGifFile->SHeight; i++) {
      delete[] ScreenBuff[i];
    }
    delete[] ScreenBuff;
  }
}

bool precompress_gif_result(unsigned char** ScreenBuff, GifFileType* myGifFile, bool result) {
  free_gif_screenbuf(ScreenBuff, myGifFile);
  DGifCloseFile(myGifFile);
  return result;
}
bool precompress_gif_error(unsigned char** ScreenBuff, GifFileType* myGifFile) {
  return precompress_gif_result(ScreenBuff, myGifFile, false);
}
bool precompress_gif_ok(unsigned char** ScreenBuff, GifFileType* myGifFile) {
  return precompress_gif_result(ScreenBuff, myGifFile, true);
}

bool recompress_gif_result(unsigned char** ScreenBuff, GifFileType* myGifFile, GifFileType* newGifFile, bool result) {
  free_gif_screenbuf(ScreenBuff, myGifFile);
  DGifCloseFile(myGifFile);
  EGifCloseFile(newGifFile);
  return result;
}
bool recompress_gif_error(unsigned char** ScreenBuff, GifFileType* myGifFile, GifFileType* newGifFile) {
  return recompress_gif_result(ScreenBuff, myGifFile, newGifFile, false);
}
bool recompress_gif_ok(unsigned char** ScreenBuff, GifFileType* myGifFile, GifFileType* newGifFile) {
  return recompress_gif_result(ScreenBuff, myGifFile, newGifFile, true);
}

bool gif_header_check(const std::span<unsigned char> checkbuf_span) {
  auto checkbuf = checkbuf_span.data();
  return
    *checkbuf == 'G' && *(checkbuf + 1) == 'I' && *(checkbuf + 2) == 'F' &&
    *(checkbuf + 3) == '8' && (*(checkbuf + 4) == '7' || *(checkbuf + 4) == '9') && *(checkbuf + 5) == 'a';
}

bool decompress_gif(Precomp& precomp_mgr, IStreamLike& srcfile, OStreamLike& dstfile, long long src_pos, long long& gif_length, long long& decomp_length, unsigned char& block_size, GifCodeStruct* g) {
  int i, j;
  GifFileType* myGifFile;
  int Row, Col, Width, Height, ExtCode;
  GifByteType* Extension;
  GifRecordType RecordType;

  long long srcfile_pos;
  long long last_pos = -1;

  freadfunc = &srcfile;
  myGifFile = DGifOpen(nullptr, readFunc);
  if (myGifFile == nullptr) {
    return false;
  }

  unsigned char** ScreenBuff = nullptr;

  do {

    if (DGifGetRecordType(myGifFile, &RecordType) == GIF_ERROR) {
      DGifCloseFile(myGifFile);
      return false;
    }

    switch (RecordType) {
    case IMAGE_DESC_RECORD_TYPE:
      if (DGifGetImageDesc(myGifFile) == GIF_ERROR) {
        return precompress_gif_error(ScreenBuff, myGifFile);
      }

      if (ScreenBuff == nullptr) {
        ScreenBuff = alloc_gif_screenbuf(myGifFile);
        if (!ScreenBuff) {
          DGifCloseFile(myGifFile);
          return false;
        }
      }

      srcfile_pos = srcfile.tellg();
      if (last_pos != srcfile_pos) {
        if (last_pos == -1) {
          srcfile.seekg(src_pos, std::ios_base::beg);
          fast_copy(precomp_mgr, srcfile, dstfile, srcfile_pos - src_pos);
          srcfile.seekg(srcfile_pos, std::ios_base::beg);

          long long dstfile_pos = dstfile.tellp();
          dstfile.seekp(0, std::ios_base::beg);
          // change GIF8xa to PGF8xa
          dstfile.put('P');
          dstfile.put('G');
          dstfile.seekp(dstfile_pos, std::ios_base::beg);
        }
        else {
          srcfile.seekg(last_pos, std::ios_base::beg);
          fast_copy(precomp_mgr, srcfile, dstfile, srcfile_pos - last_pos);
          srcfile.seekg(srcfile_pos, std::ios_base::beg);
        }
      }

      unsigned char c;
      c = srcfile.get();
      if (c == 254) {
        block_size = 254;
      }
      srcfile.seekg(srcfile_pos, std::ios_base::beg);

      Row = myGifFile->Image.Top; /* Image Position relative to Screen. */
      Col = myGifFile->Image.Left;
      Width = myGifFile->Image.Width;
      Height = myGifFile->Image.Height;

      if (((Col + Width) > myGifFile->SWidth) ||
        ((Row + Height) > myGifFile->SHeight)) {
        return precompress_gif_error(ScreenBuff, myGifFile);
      }

      if (myGifFile->Image.Interlace) {
        /* Need to perform 4 passes on the images: */
        for (i = 0; i < 4; i++) {
          for (j = Row + InterlacedOffset[i]; j < (Row + Height); j += InterlacedJumps[i]) {
            if (DGifGetLineByte(myGifFile, &ScreenBuff[j][Col], Width, g) == GIF_ERROR) {
              // TODO: If this fails, write as much rows to dstfile
              //       as possible to support second decompression.
              return precompress_gif_error(ScreenBuff, myGifFile);
            }
          }
        }
        // write to dstfile
        for (i = Row; i < (Row + Height); i++) {
          dstfile.write(reinterpret_cast<char*>(&ScreenBuff[i][Col]), Width);
        }
      }
      else {
        for (i = Row; i < (Row + Height); i++) {
          if (DGifGetLineByte(myGifFile, &ScreenBuff[i][Col], Width, g) == GIF_ERROR) {
            return precompress_gif_error(ScreenBuff, myGifFile);
          }
          // write to dstfile
          dstfile.write(reinterpret_cast<char*>(&ScreenBuff[i][Col]), Width);
        }
      }

      last_pos = srcfile.tellg();

      break;
    case EXTENSION_RECORD_TYPE:
      /* Skip any extension blocks in file: */

      if (DGifGetExtension(myGifFile, &ExtCode, &Extension) == GIF_ERROR) {
        return precompress_gif_error(ScreenBuff, myGifFile);
      }
      while (Extension != nullptr) {
        if (DGifGetExtensionNext(myGifFile, &Extension) == GIF_ERROR) {
          return precompress_gif_error(ScreenBuff, myGifFile);
        }
      }
      break;
    case TERMINATE_RECORD_TYPE:
    default:                    /* Should be traps by DGifGetRecordType. */
      break;
    }
  } while (RecordType != TERMINATE_RECORD_TYPE);

  srcfile_pos = srcfile.tellg();
  if (last_pos != srcfile_pos) {
    srcfile.seekg(last_pos, std::ios_base::beg);
    fast_copy(precomp_mgr, srcfile, dstfile, srcfile_pos - last_pos);
    srcfile.seekg(srcfile_pos, std::ios_base::beg);
  }

  gif_length = srcfile_pos - src_pos;
  decomp_length = dstfile.tellp();

  return precompress_gif_ok(ScreenBuff, myGifFile);
}

bool recompress_gif(Precomp& precomp_mgr, IStreamLike& srcfile, OStreamLike& dstfile, unsigned char block_size, GifCodeStruct* g, GifDiffStruct* gd) {
  int i, j;
  long long last_pos = -1;
  int Row, Col, Width, Height, ExtCode;
  long long src_pos, init_src_pos;

  GifFileType* myGifFile;
  GifFileType* newGifFile;
  GifRecordType RecordType;
  GifByteType* Extension;

  freadfunc = &srcfile;
  frecompress_gif = &dstfile;
  newgif_may_write = false;

  init_src_pos = srcfile.tellg();

  myGifFile = DGifOpenPCF(nullptr, readFunc);
  if (myGifFile == nullptr) {
    return false;
  }

  newGifFile = EGifOpen(nullptr, writeFunc);

  newGifFile->BlockSize = block_size;

  if (newGifFile == nullptr) {
    return false;
  }
  unsigned char** ScreenBuff = alloc_gif_screenbuf(myGifFile);
  if (!ScreenBuff) {
    DGifCloseFile(myGifFile);
    EGifCloseFile(newGifFile);
    return false;
  }

  EGifPutScreenDesc(newGifFile, myGifFile->SWidth, myGifFile->SHeight, myGifFile->SColorResolution, myGifFile->SBackGroundColor, myGifFile->SPixelAspectRatio, myGifFile->SColorMap);

  do {
    if (DGifGetRecordType(myGifFile, &RecordType) == GIF_ERROR) {
      return recompress_gif_error(ScreenBuff, myGifFile, newGifFile);
    }

    switch (RecordType) {
    case IMAGE_DESC_RECORD_TYPE:
      if (DGifGetImageDesc(myGifFile) == GIF_ERROR) {
        return recompress_gif_error(ScreenBuff, myGifFile, newGifFile);
      }

      src_pos = srcfile.tellg();
      if (last_pos != src_pos) {
        if (last_pos == -1) {
          srcfile.seekg(init_src_pos, std::ios_base::beg);
          fast_copy(precomp_mgr, srcfile, dstfile, src_pos - init_src_pos);
          srcfile.seekg(src_pos, std::ios_base::beg);

          long long dstfile_pos = dstfile.tellp();
          dstfile.seekp(0, std::ios_base::beg);
          // change PGF8xa to GIF8xa
          dstfile.put('G');
          dstfile.put('I');
          dstfile.seekp(dstfile_pos, std::ios_base::beg);
        }
        else {
          srcfile.seekg(last_pos, std::ios_base::beg);
          fast_copy(precomp_mgr, srcfile, dstfile, src_pos - last_pos);
          srcfile.seekg(src_pos, std::ios_base::beg);
        }
      }

      Row = myGifFile->Image.Top; /* Image Position relative to Screen. */
      Col = myGifFile->Image.Left;
      Width = myGifFile->Image.Width;
      Height = myGifFile->Image.Height;

      for (i = Row; i < (Row + Height); i++) {
        srcfile.read(reinterpret_cast<char*>(&ScreenBuff[i][Col]), Width);
      }

      // this does send a clear code, so we pass g and gd
      if (EGifPutImageDesc(newGifFile, g, gd, Row, Col, Width, Height, myGifFile->Image.Interlace, myGifFile->Image.ColorMap) == GIF_ERROR) {
        return recompress_gif_error(ScreenBuff, myGifFile, newGifFile);
      }

      newgif_may_write = true;

      if (myGifFile->Image.Interlace) {
        for (i = 0; i < 4; i++) {
          for (j = Row + InterlacedOffset[i]; j < (Row + Height); j += InterlacedJumps[i]) {
            EGifPutLine(newGifFile, &ScreenBuff[j][Col], g, gd, Width);
          }
        }
      }
      else {
        for (i = Row; i < (Row + Height); i++) {
          EGifPutLine(newGifFile, &ScreenBuff[i][Col], g, gd, Width);
        }
      }

      newgif_may_write = false;

      last_pos = srcfile.tellg();

      break;
    case EXTENSION_RECORD_TYPE:
      /* Skip any extension blocks in file: */

      if (DGifGetExtension(myGifFile, &ExtCode, &Extension) == GIF_ERROR) {
        return recompress_gif_error(ScreenBuff, myGifFile, newGifFile);
      }
      while (Extension != nullptr) {
        if (DGifGetExtensionNext(myGifFile, &Extension) == GIF_ERROR) {
          return recompress_gif_error(ScreenBuff, myGifFile, newGifFile);
        }
      }
      break;
    case TERMINATE_RECORD_TYPE:
    default:                    /* Should be traps by DGifGetRecordType. */
      break;
    }
  } while (RecordType != TERMINATE_RECORD_TYPE);

  src_pos = srcfile.tellg();
  if (last_pos != src_pos) {
    srcfile.seekg(last_pos, std::ios_base::beg);
    fast_copy(precomp_mgr, srcfile, dstfile, src_pos - last_pos);
    srcfile.seekg(src_pos, std::ios_base::beg);
  }
  return recompress_gif_ok(ScreenBuff, myGifFile, newGifFile);
}

void try_recompression_gif(Precomp& precomp_mgr, std::byte header1, std::string& tempfile, std::string& tempfile2)
{
  unsigned char block_size = 255;

  const bool penalty_bytes_stored = ((header1 & std::byte{ 0b10 }) == std::byte{ 0b10 });
  if ((header1 & std::byte{ 0b100 }) == std::byte{ 0b100 }) block_size = 254;
  const bool recompress_success_needed = ((header1 & std::byte{ 0b10000000 }) == std::byte{ 0b10000000 });

  GifDiffStruct gDiff;

  // read diff bytes
  gDiff.GIFDiffIndex = fin_fget_vlint(*precomp_mgr.ctx->fin);
  gDiff.GIFDiff = (unsigned char*)malloc(gDiff.GIFDiffIndex * sizeof(unsigned char));
  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(gDiff.GIFDiff), gDiff.GIFDiffIndex);
  print_to_log(PRECOMP_DEBUG_LOG, "Diff bytes were used: %i bytes\n", gDiff.GIFDiffIndex);
  gDiff.GIFDiffSize = gDiff.GIFDiffIndex;
  gDiff.GIFDiffIndex = 0;
  gDiff.GIFCodeCount = 0;

  // read penalty bytes
  if (penalty_bytes_stored) {
    precomp_mgr.ctx->penalty_bytes_len = fin_fget_vlint(*precomp_mgr.ctx->fin);
    precomp_mgr.ctx->fin->read(precomp_mgr.ctx->penalty_bytes.data(), precomp_mgr.ctx->penalty_bytes_len);
  }

  const long long recompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);
  const long long decompressed_data_length = fin_fget_vlint(*precomp_mgr.ctx->fin);

  print_to_log(PRECOMP_DEBUG_LOG, "Recompressed length: %lli - decompressed length: %lli\n", recompressed_data_length, decompressed_data_length);

  tempfile += "gif";
  tempfile2 += "gif";
  remove(tempfile.c_str());
  {
    WrappedFStream ftempout;
    ftempout.open(tempfile, std::ios_base::out | std::ios_base::binary);
    fast_copy(precomp_mgr, *precomp_mgr.ctx->fin, ftempout, decompressed_data_length);
    ftempout.close();
  }

  bool recompress_success = false;

  {
    remove(tempfile2.c_str());
    WrappedFStream frecomp;
    frecomp.open(tempfile2, std::ios_base::out | std::ios_base::binary);

    // recompress data
    WrappedFStream ftempout;
    ftempout.open(tempfile, std::ios_base::in | std::ios_base::binary);
    recompress_success = recompress_gif(precomp_mgr, ftempout, frecomp, block_size, nullptr, &gDiff);
    frecomp.close();
    ftempout.close();
  }

  if (recompress_success_needed) {
    if (!recompress_success) {
      GifDiffFree(&gDiff);
      throw PrecompError(ERR_DURING_RECOMPRESSION);
    }
  }

  const long long old_fout_pos = precomp_mgr.ctx->fout->tellp();

  {
    PrecompTmpFile frecomp;
    frecomp.open(tempfile2, std::ios_base::in | std::ios_base::binary);
    fast_copy(precomp_mgr, frecomp, *precomp_mgr.ctx->fout, recompressed_data_length);
  }

  remove(tempfile2.c_str());
  remove(tempfile.c_str());

  if (penalty_bytes_stored) {
    precomp_mgr.ctx->fout->flush();

    const long long fsave_fout_pos = precomp_mgr.ctx->fout->tellp();

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

  GifDiffFree(&gDiff);
}

gif_precompression_result precompress_gif(Precomp& precomp_mgr, const std::span<unsigned char> checkbuf_span, long long original_input_pos) {
  gif_precompression_result result = gif_precompression_result();
  std::unique_ptr<PrecompTmpFile> tmpfile{ new PrecompTmpFile() };
  tmpfile->open(temp_files_tag() + "_decomp_gif", std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
  tmpfile->close();
  unsigned char version[5];

  for (int i = 0; i < 5; i++) {
    version[i] = *(checkbuf_span.data() + i);
  }

  unsigned char block_size = 255;
  long long gif_length = -1;
  long long decomp_length = -1;

  GifCodeStruct gCode;
  GifCodeInit(&gCode);
  GifDiffStruct gDiff;
  GifDiffInit(&gDiff);

  bool recompress_success_needed = true;

  print_to_log(PRECOMP_DEBUG_LOG, "Possible GIF found at position %lli\n", original_input_pos);

  precomp_mgr.ctx->fin->seekg(original_input_pos, std::ios_base::beg);

  // read GIF file
  {
    WrappedFStream ftempout;
    ftempout.open(tmpfile->file_path, std::ios_base::out | std::ios_base::binary);

    if (!decompress_gif(precomp_mgr, *precomp_mgr.ctx->fin, ftempout, original_input_pos, gif_length, decomp_length, block_size, &gCode)) {
      ftempout.close();
      remove(tmpfile->file_path.c_str());
      GifDiffFree(&gDiff);
      GifCodeFree(&gCode);
      return result;
    }

    print_to_log(PRECOMP_DEBUG_LOG, "Can be decompressed to %lli bytes\n", decomp_length);
  }

  precomp_mgr.statistics.decompressed_streams_count++;
  precomp_mgr.statistics.decompressed_gif_count++;

  std::string tempfile2 = tmpfile->file_path + "_rec_";
  WrappedFStream ftempout;
  ftempout.open(tmpfile->file_path, std::ios_base::in | std::ios_base::binary);
  PrecompTmpFile frecomp;
  frecomp.open(tempfile2, std::ios_base::out | std::ios_base::binary);
  if (recompress_gif(precomp_mgr, ftempout, frecomp, block_size, &gCode, &gDiff)) {

    frecomp.close();
    ftempout.close();

    WrappedFStream frecomp2;
    frecomp2.open(tempfile2, std::ios_base::in | std::ios_base::binary);
    auto [best_identical_bytes, penalty_bytes] = compare_files_penalty(precomp_mgr, *precomp_mgr.ctx, *precomp_mgr.ctx->fin, frecomp2, original_input_pos, 0);
    frecomp2.close();
    result.original_size = best_identical_bytes;
    result.precompressed_size = decomp_length;

    if (result.original_size < gif_length) {
      print_to_log(PRECOMP_DEBUG_LOG, "Recompression failed\n");
    }
    else {
      print_to_log(PRECOMP_DEBUG_LOG, "Recompression successful\n");
      recompress_success_needed = true;

      if (result.original_size > precomp_mgr.switches.min_ident_size) {
        precomp_mgr.statistics.recompressed_streams_count++;
        precomp_mgr.statistics.recompressed_gif_count++;
        precomp_mgr.ctx->non_zlib_was_used = true;

        result.success = true;

        // write compressed data header (GIF)
        std::byte add_bits {0};
        if (!penalty_bytes.empty()) add_bits |= std::byte{0b10};
        if (block_size == 254) add_bits |= std::byte{0b100};
        add_bits |= std::byte{0b10000000};
        result.flags = std::byte{0b1} | add_bits;

        result.gif_diff = std::vector(gDiff.GIFDiff, gDiff.GIFDiff + gDiff.GIFDiffIndex);

        result.penalty_bytes = std::move(penalty_bytes);

        tmpfile->reopen();
        result.precompressed_stream = std::move(tmpfile);
      }
    }
  }
  else {
    print_to_log(PRECOMP_DEBUG_LOG, "No matches\n");
  }

  GifDiffFree(&gDiff);
  GifCodeFree(&gCode);
  return result;
}
