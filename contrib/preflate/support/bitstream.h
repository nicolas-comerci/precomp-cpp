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

#ifndef BITSTREAM_H
#define BITSTREAM_H

#include <algorithm>
#include "bit_helper.h"
#include "stream.h"

// Little endian, as we use it for the Huffman decoder
class BitInputStream {
public:
  BitInputStream(InputStream&);

  bool eof() {
    const auto noBitsRemaining = _bufPos == _bufSize && !_bitsRemaining;
    // If we don't have any remaining bits but we don't have eof flag set yet, we need to attempt a peek to see if we are actually at eof
    if (noBitsRemaining && !_eof) {
      peek(1);
    }
    return _eof && noBitsRemaining;
  }

  size_t bitPos() const {
    return _totalBitPos;
  }

  size_t peek(const unsigned n, unsigned& peekedAmount) {
    _fill(n);
    peekedAmount = std::min(n, _bitsRemaining);
    return _bits & ((1 << peekedAmount) - 1);
  }
  size_t peek(const unsigned n) {
    unsigned bitsToPeek;
    return peek(n, bitsToPeek);
  }
  void skip(const unsigned n) {
    const auto bitsToSkip = std::min(n, _bitsRemaining);
    _bitsRemaining -= bitsToSkip;
    _bits >>= bitsToSkip;
    _totalBitPos += bitsToSkip;
  }
  size_t get(const unsigned n, unsigned& gottenAmount) {
    size_t v = peek(n, gottenAmount);
    skip(gottenAmount);
    return v;
  }
  size_t get(const unsigned n) {
    unsigned gottenAmount;
    return get(n, gottenAmount);
  }
  size_t getReverse(const unsigned n) {
    return bitReverse(get(n), n);
  }
  size_t copyBytesTo(OutputStream& output, const size_t len);
  uint64_t getVLI();

private:
  void _fillBytes(const unsigned nBytes);
  void _fill(const unsigned n);

  enum { BUF_SIZE = 1024, BITS = sizeof(size_t)*8 };

  InputStream& _input;
  unsigned char _buffer[BUF_SIZE];
  unsigned _bufPos, _bufSize;
  bool _eof;
  size_t _bits;
  unsigned _bitsRemaining;
  size_t _totalBitPos;
};

class BitOutputStream {
public:
  BitOutputStream(OutputStream&);

  void put(const size_t value, const unsigned n) {
    if (_bitPos + n >= BITS) {
      _flush();
    }
    _bits   |= (value & ((1 << n) - 1)) << _bitPos;
    _bitPos += n;
  }
  void putReverse(const size_t value, const unsigned n) {
    put(bitReverse(value, n), n);
  }
  void fillByte() {
    _bitPos = (_bitPos + 7) & ~7;
  }
  void flush();
  unsigned bitPos() const {
    return _bitPos;
  }
  void putBytes(const uint8_t* data, const size_t size);
  void putVLI(const uint64_t size);

private:
  void _flush();

  enum {
    BUF_SIZE = 1024, BUF_EXTRA = 64, BITS = sizeof(size_t) * 8
  };

  OutputStream& _output;
  unsigned char _buffer[BUF_SIZE + BUF_EXTRA];
  unsigned _bufPos;
  size_t _bits;
  unsigned _bitPos;
};

#endif /* BITSTREAM_H */
