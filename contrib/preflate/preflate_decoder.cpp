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

#include <string.h>
#include <functional>
#include "preflate_block_decoder.h"
#include "preflate_decoder.h"
#include "preflate_parameter_estimator.h"
#include "preflate_statistical_model.h"
#include "preflate_token_predictor.h"
#include "preflate_tree_predictor.h"
#include "support/bitstream.h"
#include "support/memstream.h"
#include "support/outputcachestream.h"

class PreflateDecoderHandler : public PreflateDecoderTask::Handler {
public:
  PreflateDecoderHandler(std::function<void(void)> progressCallback_, std::vector<unsigned char>* _reconData)
    : progressCallback(progressCallback_), encoder(_reconData) {}

  bool error() const {
    return encoder.error();
  }

  virtual uint32_t setModel(const PreflateStatisticsCounter& counters, const PreflateParameters& parameters) {
    return encoder.addModel(counters, parameters);
  }
  virtual bool beginEncoding(const uint32_t metaBlockId, PreflatePredictionEncoder& codec, const uint32_t modelId) {
    return encoder.beginMetaBlockWithModel(codec, modelId);
  }
  virtual bool endEncoding(const uint32_t metaBlockId, PreflatePredictionEncoder& codec, const size_t uncompressedSize, const bool lastMetaBlock) {
    return encoder.endMetaBlock(codec, uncompressedSize, lastMetaBlock);
  }
  virtual void markProgress() {
    std::unique_lock<std::mutex> lock(this->_mutex);
    progressCallback();
  }

private:
  PreflateMetaEncoder encoder;
  std::function<void(void)> progressCallback;
  std::mutex _mutex;
};

PreflateDecoderTask::PreflateDecoderTask(PreflateDecoderTask::Handler& handler_,
                                         const uint32_t metaBlockId_,
                                         std::vector<PreflateTokenBlock>&& tokenData_,
                                         std::vector<uint8_t>&& uncompressedData_,
                                         const size_t uncompressedOffset_,
                                         const bool lastMetaBlock_,
                                         const uint32_t paddingBits_)
  : handler(handler_)
  , metaBlockId(metaBlockId_)
  , tokenData(tokenData_)
  , uncompressedData(uncompressedData_)
  , uncompressedOffset(uncompressedOffset_)
  , lastMetaBlock(lastMetaBlock_)
  , paddingBits(paddingBits_) {
}

bool PreflateDecoderTask::analyze() {
  params = estimatePreflateParameters(uncompressedData, uncompressedOffset, tokenData);
  memset(&counter, 0, sizeof(counter));
  tokenPredictor.reset(new PreflateTokenPredictor(params, uncompressedData, uncompressedOffset));
  treePredictor.reset(new PreflateTreePredictor(uncompressedData, uncompressedOffset));
  for (unsigned i = 0, n = tokenData.size(); i < n; ++i) {
    tokenPredictor->analyzeBlock(i, tokenData[i]);
    treePredictor->analyzeBlock(i, tokenData[i]);
    if (tokenPredictor->predictionFailure || treePredictor->predictionFailure) {
      return false;
    }
    tokenPredictor->updateCounters(&counter, i);
    treePredictor->updateCounters(&counter, i);
    handler.markProgress();
  }
  counter.block.incNonZeroPadding(paddingBits != 0);
  return true;
}

bool PreflateDecoderTask::encode(OutputStream& output) {
  PreflatePredictionEncoder pcodec;
  unsigned modelId = handler.setModel(counter, params);
  if (!handler.beginEncoding(metaBlockId, pcodec, modelId)) {
    return false;
  }
  for (unsigned i = 0, n = tokenData.size(); i < n; ++i) {
    tokenPredictor->encodeBlock(&pcodec, i);
    treePredictor->encodeBlock(&pcodec, i);
    if (tokenPredictor->predictionFailure || treePredictor->predictionFailure) {
      return false;
    }
    if (lastMetaBlock) {
      tokenPredictor->encodeEOF(&pcodec, i, i + 1 == tokenData.size());
    }
  }
  if (lastMetaBlock) {
    pcodec.encodeNonZeroPadding(paddingBits != 0);
    if (paddingBits != 0) {
      unsigned bitsToSave = bitLength(paddingBits);
      pcodec.encodeValue(bitsToSave, 3);
      if (bitsToSave > 1) {
        pcodec.encodeValue(paddingBits & ((1 << (bitsToSave - 1)) - 1), bitsToSave - 1);
      }
    }
  }
  bool endEncodingSuccess =  handler.endEncoding(metaBlockId, pcodec, uncompressedData.size() - uncompressedOffset, lastMetaBlock);
  // Only output uncompressed data if everything worked okay, to ensure client code doesn't end up with useless decompressed data
  if (endEncodingSuccess) {
    // If and only if the first metablock is also the last one, the uncompressedData vector's full data should be outputted,
    // in any other case an extra 1 << 15 bytes have been appended at the end and should be omitted
    const auto uncompressedDataSize = lastMetaBlock ? uncompressedData.size() : uncompressedData.size() - (1 << 15);
    output.write(uncompressedData.data(), uncompressedDataSize);
  }

  return endEncodingSuccess;
}

bool preflate_decode(OutputStream& unpacked_output,
                     std::vector<unsigned char>& preflate_diff,
                     uint64_t& deflate_size,
                     InputStream& deflate_raw,
                     std::function<void(void)> block_callback,
                     const size_t metaBlockSize) {
  deflate_size = 0;
  uint64_t deflate_bits = 0;
  size_t prevBitPos = 0;
  BitInputStream decInBits(deflate_raw);
  MemStream decUnc;
  OutputCacheStream decOutCache(decUnc);
  PreflateBlockDecoder bdec(decInBits, decOutCache);
  if (bdec.status() != PreflateBlockDecoder::OK) {
    return false;
  }
  bool last;
  unsigned i = 0;
  std::vector<std::tuple<PreflateTokenBlock, uint32_t, uint64_t>> blocks; // (PreflateTokenBlock, blockDecSize, original byte count that can be restored up to with that block)
  uint64_t sumBlockDecSizes = 0;
  uint64_t lastEndPos = 0;
  uint64_t uncompressedMetaStart = 0;
  size_t MBSize = std::min<size_t>(std::max<size_t>(metaBlockSize, 1u << 18), (1u << 31) - 1);
  size_t MBThreshold = (MBSize * 3) >> 1;
  PreflateDecoderHandler encoder(block_callback, &preflate_diff);
  size_t MBcount = 0;

  std::queue<std::future<std::tuple<std::shared_ptr<PreflateDecoderTask>, uint64_t>>> futureQueue;
  const size_t queueLimit = std::min(2 * globalTaskPool.extraThreadCount(), (1 << 26) / MBThreshold);
  bool fail = false;

  do {
    PreflateTokenBlock newBlock;

    bool ok = bdec.readBlock(newBlock, last);
    if (!ok) {
      fail = true;
      break;
    }

    uint64_t blockDecSize = decOutCache.cacheEndPos() - lastEndPos;
    lastEndPos = decOutCache.cacheEndPos();
    if (blockDecSize >= (1 << 31)) {
      // No mega blocks
      fail = true;
      break;
    }

    deflate_bits += decInBits.bitPos() - prevBitPos;
    prevBitPos = decInBits.bitPos();

    size_t paddingBits = 0;
    if (last) {
      uint8_t remaining_bit_count = (8 - deflate_bits) & 7;
      paddingBits = decInBits.get(remaining_bit_count);

      deflate_bits += decInBits.bitPos() - prevBitPos;
      prevBitPos = decInBits.bitPos();
    }

    blocks.push_back({ newBlock, blockDecSize, deflate_bits  >> 3 });
    ++i;
    block_callback();

    sumBlockDecSizes += blockDecSize;

    // If there is still more data and we still have space to fill the metablock just go to next iteration and continue decompressing the stream
    if (!last && sumBlockDecSizes < MBThreshold) continue;

    // At this point we must finalize a metablock, which means we need to feed both the original compress blocks and the decompressed data to a PreflateDecoderTask,
    // which will figure out the exact/closest ZLIB parameters + reconstruction data needed to recover the original compressed blocks losslessly
    size_t blockCount, blockDecSizeSum, deflateSizeSum;
    if (last) {
      blockCount = blocks.size();
      blockDecSizeSum = sumBlockDecSizes;
      deflateSizeSum = std::get<2>(blocks.back());
    } else {
      blockCount = 0;
      blockDecSizeSum = 0;
      deflateSizeSum = 0;
      for (const auto& block : blocks) {
        blockDecSizeSum += std::get<1>(block);
        deflateSizeSum = std::get<2>(block);
        ++blockCount;
        if (blockDecSizeSum >= MBSize) {
          break;
        }
      }
    }
    std::vector<PreflateTokenBlock> blocksForMeta;
    for (size_t j = 0; j < blockCount; ++j) {
      blocksForMeta.push_back(std::move(std::get<0>(blocks[j])));
    }
    blocks.erase(blocks.begin(), blocks.begin() + blockCount);
    sumBlockDecSizes -= blockDecSizeSum;

    // WTF is this magic number 1 << 15? I assume it's some overhead/metadata size each meta block adds, confirm and properly document
    size_t uncompressedOffset = MBcount == 0 ? 0 : 1 << 15;

    std::vector<uint8_t> uncompressedDataForMeta(
      decOutCache.cacheData(uncompressedMetaStart - uncompressedOffset),
      decOutCache.cacheData(uncompressedMetaStart - uncompressedOffset) + blockDecSizeSum + uncompressedOffset);
    uncompressedMetaStart += blockDecSizeSum;
    
    if (futureQueue.empty() && (queueLimit == 0 || last)) {
      PreflateDecoderTask task(encoder, MBcount,
                                std::move(blocksForMeta),
                                std::move(uncompressedDataForMeta),
                                uncompressedOffset,
                                last, paddingBits);
      if (!task.analyze() || !task.encode(unpacked_output)) {
        fail = true;
        break;
      }
      deflate_size = deflateSizeSum;
    } else {
      if (futureQueue.size() >= queueLimit) {
        auto first = std::move(futureQueue.front());
        futureQueue.pop();
        auto [data, deflate_size_sum] = first.get();
        if (!data || !data->encode(unpacked_output)) {
          fail = true;
          break;
        }
        deflate_size = deflate_size_sum;
      }
      std::shared_ptr<PreflateDecoderTask> ptask;
      ptask.reset(new PreflateDecoderTask(encoder, MBcount,
                                          std::move(blocksForMeta),
                                          std::move(uncompressedDataForMeta),
                                          uncompressedOffset,
                                          last, paddingBits));
      futureQueue.push(globalTaskPool.addTask([ptask,&fail,deflateSizeSum]() -> std::tuple<std::shared_ptr<PreflateDecoderTask>, uint64_t> {
        if (!fail && ptask->analyze()) {
          return { ptask, deflateSizeSum };
        } else {
          return { std::shared_ptr<PreflateDecoderTask>(), 0 };
        }
      }));
    }
    if (!last) {
      decOutCache.flushUpTo(uncompressedMetaStart - (1 << 15));
    }
    MBcount++;
  } while (!fail && !last);
  while (!futureQueue.empty()) {
    auto first = std::move(futureQueue.front());
    futureQueue.pop();
    auto [data, deflate_size_sum] = first.get();
    if (fail || !data || !data->encode(unpacked_output)) {
      fail = true;
    }
    else {
      deflate_size = deflate_size_sum;
    }
  }
  decOutCache.flush();
  if (!fail) deflate_size = (deflate_bits + 7) >> 3; // Complete any missing bits to get a whole size in bytes
  return !fail;
}

bool preflate_decode(std::vector<unsigned char>& unpacked_output,
                     std::vector<unsigned char>& preflate_diff,
                     uint64_t& deflate_size,
                     InputStream& deflate_raw,
                     std::function<void(void)> block_callback,
                     const size_t metaBlockSize) {
  MemStream uncompressedOutput;
  bool result = preflate_decode(uncompressedOutput, preflate_diff, deflate_size, deflate_raw, block_callback, metaBlockSize);
  unpacked_output = uncompressedOutput.extractData();
  return result;
}

bool preflate_decode(std::vector<unsigned char>& unpacked_output,
                     std::vector<unsigned char>& preflate_diff,
                     const std::vector<unsigned char>& deflate_raw,
                     const size_t metaBlockSize) {
  MemStream mem(deflate_raw);
  uint64_t raw_size;
  return preflate_decode(unpacked_output, preflate_diff, raw_size, mem, [] {}, metaBlockSize) 
          && raw_size == deflate_raw.size();
}
