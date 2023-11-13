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

#include <algorithm>
#include <memory.h>
#include <cstring>
#include "bitstream.h"

BitInputStream::BitInputStream(InputStream& is)
  : _input(is)
  , _bufPos(0)
  , _bufSize(0)
  , _eof(false)
  , _bits(0)
  , _bitsRemaining(0)
  , _totalBitPos(0)
{}

void BitInputStream::_fillBytes(const unsigned nBytes) {
  // free space in bit buffer
  if (_bufPos >= _bufSize) {
    if (!_eof) {
      const unsigned remaining = _bufSize - _bufPos;
      // This might be inefficient when there is overlap. Original code from Dirk heavily complicated BitInputStream to prevent it,
      // but I removed it while refactoring BitInputStream so that it does not reach EOF prematurely when feeding Preflate metablock by
      // metablock as Precomp does.
      // BitInputStream is complex enough, DO NOT complicate this again, either refactor this using an abstracted circular array buffer
      // or some such, that avoids the problem without impact on BitInputStream's code, or leave it alone.
      std::memmove(_buffer, _buffer + _bufPos, remaining);
      _bufPos = 0;
      if (remaining >= nBytes) {
        _bufSize = remaining;
      }
      else {
        const auto to_read = nBytes - remaining;
        _bufSize = remaining + _input.read(_buffer + remaining, to_read);
        _eof = _bufSize != (remaining + to_read);
      }
    }
  }
}
void BitInputStream::_fill(const unsigned n) {
  // If we have enough bits we don't do anything
  if (_bitsRemaining >= n) return;

  const auto neededBits = n - _bitsRemaining;
  unsigned neededBytes = neededBits / 8;
  neededBytes = neededBytes * 8 == neededBits ? neededBytes : neededBytes + 1;

  // free space in bit buffer
  if (_bufPos >= _bufSize) {
    if (!_eof) {
      _fillBytes(neededBytes);
    }
  }
  while (_bitsRemaining <= BITS - 8 && _bufPos < _bufSize && neededBytes > 0) {
    _bits |= ((size_t)_buffer[_bufPos++]) << _bitsRemaining;
    _bitsRemaining += 8;
    neededBytes -= 1;

    if (_bufPos >= _bufSize && neededBytes > 0) {
      if (!_eof) {
        _fillBytes(neededBytes);
      }
    }
  }
}
size_t BitInputStream::copyBytesTo(OutputStream& output, const size_t len) {
  // If _bitsRemaining is not multiple of 8 (that is, we don't have a whole amount of bytes remaining) just quit
  if (_bitsRemaining & 7) return 0;

  size_t l = 0;
  uint8_t a;
  while (l < len) {
    l++;
    unsigned gottenAmount;
    a = get(8, gottenAmount);
    // Because we already checked that we have whole bytes, the only real possibility here for !=8 is 0 which means
    // we are not able to get any more bytes and thus just return the amount that was copied so far
    if (gottenAmount != 8) return l;
    const auto just_written = output.write(&a, 1);
    if (just_written != 1) {
      return l - 1;
    }
  }
  return l;
}
uint64_t BitInputStream::getVLI() {
  uint64_t result = 0, o = 0;
  unsigned s = 0, c;
  unsigned bitsRemaining = ((_bitsRemaining - 1) & 7) + 1;
  unsigned limit = 1 << (bitsRemaining - 1);
  while ((c = get(bitsRemaining)) >= limit) {
    result += ((uint64_t)(c & (limit - 1))) << s;
    s += (bitsRemaining - 1);
    o = (o + 1) << (bitsRemaining - 1);
    bitsRemaining = 8;
    limit = 128;
  }
  return result + o + (((uint64_t)c) << s);
}

BitOutputStream::BitOutputStream(OutputStream& output)
  : _output(output)
  , _bufPos(0)
  , _bits(0)
  , _bitPos(0) {}

void BitOutputStream::_flush() {
  while (_bitPos >= 8) {
    _buffer[_bufPos++] = _bits & 0xff;
    _bits >>= 8;
    _bitPos -= 8;
  }
  if (_bufPos >= BUF_SIZE) {
    _output.write(_buffer, BUF_SIZE);
    memcpy(_buffer, _buffer + BUF_SIZE, _bufPos - BUF_SIZE);
    _bufPos -= BUF_SIZE;
  }
}
void BitOutputStream::flush() {
  _flush();

  if (_bitPos > 0) {
    _buffer[_bufPos++] = _bits & 0xff;
    _bits   = 0;
    _bitPos = 0;
  }

  _output.write(_buffer, _bufPos);
  _bufPos = 0;
}
void BitOutputStream::putBytes(const uint8_t* data, const size_t size) {
  flush();
  _output.write(data, size);
}
void BitOutputStream::putVLI(const uint64_t size_) {
  uint64_t size = size_;
  unsigned bitsRemaining = 8 - (_bitPos & 7);
  unsigned limit = 1 << (bitsRemaining - 1);
  while (size >= limit) {
    put(size | limit, bitsRemaining);
    size = (size >> (bitsRemaining - 1)) - 1;
    bitsRemaining = 8;
    limit = 128;
  }
  put(size, bitsRemaining);
 
}
