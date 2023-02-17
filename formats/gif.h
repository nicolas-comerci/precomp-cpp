#ifndef PRECOMP_GIF_HANDLER_H
#define PRECOMP_GIF_HANDLER_H
#include "precomp_dll.h"

#include "contrib/giflib/precomp_gif.h"

int DGifGetLineByte(GifFileType* GifFile, GifPixelType* Line, int LineLen, GifCodeStruct* g)
{
  GifPixelType* LineBuf = new GifPixelType[LineLen];
  memcpy(LineBuf, Line, LineLen);
  int result = DGifGetLine(GifFile, LineBuf, g, LineLen);
  memcpy(Line, LineBuf, LineLen);
  delete[] LineBuf;

  return result;
}

bool newgif_may_write;
OStreamLike* frecompress_gif = NULL;
IStreamLike* freadfunc = NULL;

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
  unsigned char** ScreenBuff = new unsigned char* [myGifFile->SHeight];
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
  if (ScreenBuff != NULL) {
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

bool decompress_gif(Precomp& precomp_mgr, IStreamLike& srcfile, OStreamLike& dstfile, long long src_pos, int& gif_length, long long& decomp_length, unsigned char& block_size, GifCodeStruct* g) {
  int i, j;
  GifFileType* myGifFile;
  int Row, Col, Width, Height, ExtCode;
  GifByteType* Extension;
  GifRecordType RecordType;

  long long srcfile_pos;
  long long last_pos = -1;

  freadfunc = &srcfile;
  myGifFile = DGifOpen(NULL, readFunc);
  if (myGifFile == NULL) {
    return false;
  }

  unsigned char** ScreenBuff = NULL;

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

      if (ScreenBuff == NULL) {
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
      while (Extension != NULL) {
        if (DGifGetExtensionNext(myGifFile, &Extension) == GIF_ERROR) {
          return precompress_gif_error(ScreenBuff, myGifFile);
        }
      }
      break;
    case TERMINATE_RECORD_TYPE:
      break;
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

  myGifFile = DGifOpenPCF(NULL, readFunc);
  if (myGifFile == NULL) {
    return false;
  }

  newGifFile = EGifOpen(NULL, writeFunc);

  newGifFile->BlockSize = block_size;

  if (newGifFile == NULL) {
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
      while (Extension != NULL) {
        if (DGifGetExtensionNext(myGifFile, &Extension) == GIF_ERROR) {
          return recompress_gif_error(ScreenBuff, myGifFile, newGifFile);
        }
      }
      break;
    case TERMINATE_RECORD_TYPE:
      break;
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

bool gif_header_check(unsigned char* checkbuf)
{
  return
    *checkbuf == 'G' && *(checkbuf + 1) == 'I' && *(checkbuf + 2) == 'F' &&
    *(checkbuf + 3) == '8' && (*(checkbuf + 4) == '7' || *(checkbuf + 4) == '9') && *(checkbuf + 5) == 'a';
}

class gif_precompression_result: public precompression_result {
  void dump_gif_diff_to_outfile(Precomp& precomp_mgr) {
    // store diff bytes
    fout_fput_vlint(*precomp_mgr.ctx->fout, gif_diff.size());
    if (gif_diff.size() > 0)
      print_to_log(PRECOMP_DEBUG_LOG, "Diff bytes were used: %i bytes\n", gif_diff.size());
    for (int dbc = 0; dbc < gif_diff.size(); dbc++) {
      precomp_mgr.ctx->fout->put(gif_diff[dbc]);
    }
  }
public:

  std::vector<unsigned char> gif_diff;

  gif_precompression_result(): precompression_result(D_GIF) {}

  void dump_to_outfile(Precomp& precomp_mgr) override {
    dump_header_to_outfile(precomp_mgr);
    dump_gif_diff_to_outfile(precomp_mgr);
    dump_penaltybytes_to_outfile(precomp_mgr);
    dump_stream_sizes_to_outfile(precomp_mgr);
    dump_precompressed_data_to_outfile(precomp_mgr);
  }
};

gif_precompression_result precompress_gif(Precomp& precomp_mgr) {
  gif_precompression_result result = gif_precompression_result();
  std::unique_ptr<PrecompTmpFile> tmpfile { new PrecompTmpFile() };
  tmpfile->open(temp_files_tag() + "_decomp_gif", std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
  tmpfile->close();
  unsigned char version[5];

  for (int i = 0; i < 5; i++) {
    version[i] = precomp_mgr.ctx->in_buf[precomp_mgr.ctx->cb + i];
  }

  unsigned char block_size = 255;
  int gif_length = -1;
  long long decomp_length = -1;

  GifCodeStruct gCode;
  GifCodeInit(&gCode);
  GifDiffStruct gDiff;
  GifDiffInit(&gDiff);

  bool recompress_success_needed = true;

  print_to_log(PRECOMP_DEBUG_LOG, "Possible GIF found at position %lli\n", precomp_mgr.ctx->input_file_pos);

  precomp_mgr.ctx->fin->seekg(precomp_mgr.ctx->input_file_pos, std::ios_base::beg);
  
  // read GIF file
  {
    WrappedFStream ftempout;
    ftempout.open(tmpfile->file_path, std::ios_base::out | std::ios_base::binary);

    if (!decompress_gif(precomp_mgr, *precomp_mgr.ctx->fin, ftempout, precomp_mgr.ctx->input_file_pos, gif_length, decomp_length, block_size, &gCode)) {
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
    auto [best_identical_bytes, penalty_bytes] = compare_files_penalty(precomp_mgr, *precomp_mgr.ctx, *precomp_mgr.ctx->fin, frecomp2, precomp_mgr.ctx->input_file_pos, 0);
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
        unsigned char add_bits = 0;
        if (!penalty_bytes.empty()) add_bits += 2;
        if (block_size == 254) add_bits += 4;
        if (recompress_success_needed) add_bits += 128;
        result.flags = 1 + add_bits;

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

#endif // PRECOMP_GIF_HANDLER_H