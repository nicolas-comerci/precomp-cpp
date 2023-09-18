/* Copyright 2018 Dirk Steinke

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License. */

#ifndef PREFLATE_REENCODER_H
#define PREFLATE_REENCODER_H

#include <vector>
#include "preflate_statistical_codec.h"
#include "support/stream.h"
#include "support/task_pool.h"

class PreflateReencoderTask {
public:
  class Handler {
  public:
    virtual ~Handler() {}
    virtual bool beginDecoding(const PreflateMetaDecoder::metaBlockInfo& mb, PreflatePredictionDecoder&, PreflateParameters&) = 0;
    virtual bool endDecoding(const PreflateMetaDecoder::metaBlockInfo& mb, PreflatePredictionDecoder&,
                             std::vector<PreflateTokenBlock>&& tokenData,
                             std::vector<uint8_t>&& uncompressedData, 
                             const size_t uncompressedOffset,
                             const size_t paddingBitCount,
                             const size_t paddingValue) = 0;
    virtual void markProgress() = 0;
  };

  PreflateReencoderTask(Handler& handler,
                        PreflateMetaDecoder::metaBlockInfo&& mb,
                        std::vector<uint8_t>&& uncompressedData,
                        const size_t uncompressedOffset);

  bool decodeAndRepredict();
  bool reencode();

private:
  Handler& handler;
  PreflateMetaDecoder::metaBlockInfo mb;
  std::vector<uint8_t> uncompressedData;
  size_t uncompressedOffset;
  std::vector<PreflateTokenBlock> tokenData;
  PreflatePredictionDecoder pcodec;
  size_t paddingBitCount;
  size_t paddingBits;
};

bool preflate_reencode(std::vector<unsigned char>& deflate_raw,
                       const std::vector<unsigned char>& preflate_diff,
                       const std::vector<unsigned char>& unpacked_input);

bool preflate_reencode(OutputStream& os,
                       const std::vector<unsigned char>& preflate_diff,
                       InputStream& unpacked_input,
                       const uint64_t unpacked_size,
                       std::function<void(void)> block_callback);

bool preflate_reencode(OutputStream& os,
                       const std::vector<unsigned char>& preflate_diff,
                       const std::vector<unsigned char>& unpacked_input,
                       std::function<void(void)> block_callback);

#endif /* PREFLATE_REENCODER_H */
