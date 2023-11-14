#include "gif.h"

#include "contrib/giflib/precomp_gif.h"

#include <cstddef>
#include <cstring>

class gif_precompression_result : public precompression_result {
    void dump_gif_diff_to_outfile(OStreamLike& outfile) const {
        // store diff bytes
        fout_fput_vlint(outfile, gif_diff.size());
        if (!gif_diff.empty())
            print_to_log(PRECOMP_DEBUG_LOG, "Diff bytes were used: %i bytes\n", gif_diff.size());
        for (unsigned char dbc : gif_diff) {
            outfile.put(dbc);
        }
    }
public:
    std::vector<unsigned char> gif_diff;

    explicit gif_precompression_result(Tools* _tools) : precompression_result(D_GIF, _tools) {}

    void dump_to_outfile(OStreamLike& outfile) const override {
        dump_header_to_outfile(outfile);
        dump_gif_diff_to_outfile(outfile);
        dump_penaltybytes_to_outfile(outfile);
        dump_stream_sizes_to_outfile(outfile);
        dump_precompressed_data_to_outfile(outfile);
    }

    void increase_detected_count() override { tools->increase_detected_count("GIF"); }
    void increase_precompressed_count() override { tools->increase_precompressed_count("GIF"); }
};

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

bool GifFormatHandler::quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) {
  auto checkbuf = buffer.data();
  return
    *checkbuf == 'G' && *(checkbuf + 1) == 'I' && *(checkbuf + 2) == 'F' &&
    *(checkbuf + 3) == '8' && (*(checkbuf + 4) == '7' || *(checkbuf + 4) == '9') && *(checkbuf + 5) == 'a';
}

bool decompress_gif(IStreamLike& srcfile, OStreamLike& dstfile, long long src_pos, long long& gif_length, long long& decomp_length, unsigned char& block_size, GifCodeStruct* g) {
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
          last_pos = src_pos + 2;
          // change GIF8xa to PGF8xa
          dstfile.put('P');
          dstfile.put('G');
        }
        srcfile.seekg(last_pos, std::ios_base::beg);
        fast_copy(srcfile, dstfile, srcfile_pos - last_pos);
        srcfile.seekg(srcfile_pos, std::ios_base::beg);
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
    fast_copy(srcfile, dstfile, srcfile_pos - last_pos);
    srcfile.seekg(srcfile_pos, std::ios_base::beg);
  }

  gif_length = srcfile_pos - src_pos;
  decomp_length = dstfile.tellp();

  return precompress_gif_ok(ScreenBuff, myGifFile);
}

bool recompress_gif(IStreamLike& srcfile, OStreamLike& dstfile, unsigned char block_size, GifCodeStruct* g, GifDiffStruct* gd) {
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
          last_pos = init_src_pos + 2;
          // change PGF8xa to GIF8xa
          dstfile.put('G');
          dstfile.put('I');
        }
        srcfile.seekg(last_pos, std::ios_base::beg);
        fast_copy(srcfile, dstfile, src_pos - last_pos);
        srcfile.seekg(src_pos, std::ios_base::beg);
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
    fast_copy(srcfile, dstfile, src_pos - last_pos);
    srcfile.seekg(src_pos, std::ios_base::beg);
  }
  return recompress_gif_ok(ScreenBuff, myGifFile, newGifFile);
}

class GifFormatHeaderData: public PrecompFormatHeaderData {
public:
  GifDiffStruct gDiff;
  unsigned char block_size;
  bool recompress_success_needed;
};

std::unique_ptr<PrecompFormatHeaderData> GifFormatHandler::read_format_header(IStreamLike &input, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) {
  auto fmt_hdr = std::make_unique<GifFormatHeaderData>();

  bool penalty_bytes_stored = (precomp_hdr_flags & std::byte{ 0b10 }) == std::byte{ 0b10 };
  fmt_hdr->block_size = 255;
  if ((precomp_hdr_flags & std::byte{ 0b100 }) == std::byte{ 0b100 }) fmt_hdr->block_size = 254;
  fmt_hdr->recompress_success_needed = ((precomp_hdr_flags & std::byte{ 0b10000000 }) == std::byte{ 0b10000000 });

  // read diff bytes
  fmt_hdr->gDiff.GIFDiffIndex = fin_fget_vlint(input);
  fmt_hdr->gDiff.GIFDiff = (unsigned char*)malloc(fmt_hdr->gDiff.GIFDiffIndex * sizeof(unsigned char));
  input.read(reinterpret_cast<char*>(fmt_hdr->gDiff.GIFDiff), fmt_hdr->gDiff.GIFDiffIndex);
  print_to_log(PRECOMP_DEBUG_LOG, "Diff bytes were used: %i bytes\n", fmt_hdr->gDiff.GIFDiffIndex);
  fmt_hdr->gDiff.GIFDiffSize = fmt_hdr->gDiff.GIFDiffIndex;
  fmt_hdr->gDiff.GIFDiffIndex = 0;
  fmt_hdr->gDiff.GIFCodeCount = 0;

  // read penalty bytes
  if (penalty_bytes_stored) {
    auto penalty_bytes_len = fin_fget_vlint(input);
    while (penalty_bytes_len > 0) {
      unsigned char pb_data[5];
      input.read(reinterpret_cast<char*>(pb_data), 5);
      penalty_bytes_len -= 5;

      uint32_t next_pb_pos = pb_data[0] << 24;
      next_pb_pos += pb_data[1] << 16;
      next_pb_pos += pb_data[2] << 8;
      next_pb_pos += pb_data[3];

      fmt_hdr->penalty_bytes.emplace(next_pb_pos, pb_data[4]);
    }
  }

  fmt_hdr->original_size = fin_fget_vlint(input);
  fmt_hdr->precompressed_size = fin_fget_vlint(input);

  return fmt_hdr;
}

void GifFormatHandler::recompress(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format) {
  auto& gif_precomp_hdr_format = static_cast<GifFormatHeaderData&>(precomp_hdr_data);
  print_to_log(PRECOMP_DEBUG_LOG, "Recompressed length: %lli - decompressed length: %lli\n", gif_precomp_hdr_format.original_size, gif_precomp_hdr_format.precompressed_size);

  std::string tmp_tag = temp_files_tag();
  std::string tempfile = precomp_tools->get_tempfile_name(tmp_tag + "_precompressed_gif", false);

  dump_to_file(precompressed_input, tempfile, gif_precomp_hdr_format.precompressed_size);
  bool recompress_success;

  {
    // recompress data
    WrappedFStream ftempout;
    ftempout.open(tempfile, std::ios_base::in | std::ios_base::binary);
    recompress_success = recompress_gif(ftempout, recompressed_stream, gif_precomp_hdr_format.block_size, nullptr, &gif_precomp_hdr_format.gDiff);
    ftempout.close();
  }

  // TODO: wtf is this? how can recompression fail? do we just allow bad data?
  if (gif_precomp_hdr_format.recompress_success_needed) {
    if (!recompress_success) {
      GifDiffFree(&gif_precomp_hdr_format.gDiff);
      throw PrecompError(ERR_DURING_RECOMPRESSION);
    }
  }

  remove(tempfile.c_str());
  GifDiffFree(&gif_precomp_hdr_format.gDiff);
}

std::unique_ptr<precompression_result>
GifFormatHandler::attempt_precompression(IStreamLike &input, OStreamLike &output,
                                         std::span<unsigned char> checkbuf_span,
                                         long long original_input_pos, const Switches &precomp_switches) {
  std::unique_ptr<gif_precompression_result> result = std::make_unique<gif_precompression_result>(precomp_tools);
  std::unique_ptr<PrecompTmpFile> tmpfile = std::make_unique<PrecompTmpFile>();
  tmpfile->open(precomp_tools->get_tempfile_name("decomp_gif", true), std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);
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

  input.seekg(original_input_pos, std::ios_base::beg);

  // read GIF file
  {
    tmpfile->open(tmpfile->file_path, std::ios_base::in | std::ios_base::out | std::ios_base::binary);
    if (!decompress_gif(input, *tmpfile, original_input_pos, gif_length, decomp_length, block_size, &gCode)) {
      tmpfile->close();
      GifDiffFree(&gDiff);
      GifCodeFree(&gCode);
      return result;
    }

    print_to_log(PRECOMP_DEBUG_LOG, "Can be decompressed to %lli bytes\n", decomp_length);
  }

  std::string tempfile2 = tmpfile->file_path + "_rec_";
  tmpfile->reopen();
  PrecompTmpFile frecomp;
  frecomp.open(tempfile2, std::ios_base::out | std::ios_base::binary);
  if (recompress_gif(*tmpfile, frecomp, block_size, &gCode, &gDiff)) {

    frecomp.close();
    tmpfile->close();

    WrappedFStream frecomp2;
    frecomp2.open(tempfile2, std::ios_base::in | std::ios_base::binary);
    input.seekg(original_input_pos, std::ios_base::beg);
    auto [identical_bytes, penalty_bytes] = compare_files_penalty(precomp_tools->progress_callback, input, frecomp2, gif_length);
    frecomp2.close();
    result->original_size = identical_bytes;
    result->precompressed_size = decomp_length;

    if (result->original_size < gif_length) {
      print_to_log(PRECOMP_DEBUG_LOG, "Recompression failed\n");
    }
    else {
      print_to_log(PRECOMP_DEBUG_LOG, "Recompression successful\n");
      recompress_success_needed = true;

      if (result->original_size > precomp_switches.min_ident_size) {
        result->success = true;

        // write compressed data header (GIF)
        std::byte add_bits {0};
        if (!penalty_bytes.empty()) add_bits |= std::byte{0b10};
        if (block_size == 254) add_bits |= std::byte{0b100};
        add_bits |= std::byte{0b10000000};
        result->flags = std::byte{0b1} | add_bits;

        result->gif_diff = std::vector(gDiff.GIFDiff, gDiff.GIFDiff + gDiff.GIFDiffIndex);
        result->penalty_bytes = std::move(penalty_bytes);

        tmpfile->reopen();
        result->precompressed_stream = std::move(tmpfile);
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
