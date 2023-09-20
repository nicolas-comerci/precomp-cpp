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

#ifndef MEMSTREAM_H
#define MEMSTREAM_H

#include <stdint.h>
#include <vector>
#include "stream.h"

template<class T>
class MemStreamBase : public SeekableInputStream {
public:
  MemStreamBase(T content) : _data(content), _pos(0) {};

  bool eof() const override {
    return _pos == _data->size();
  }
  size_t read(unsigned char* buffer, const size_t size) override {
    size_t toCopy = std::min(size, _data->size() - _pos);
    memcpy(buffer, _data->data() + _pos, toCopy);
    _pos += toCopy;
    return toCopy;
  }

  uint64_t tell() const override {
    return _pos;
  }
  uint64_t seek(const uint64_t newPos) override {
    size_t oldPos = _pos;
    _pos = std::min(newPos, static_cast<uint64_t>(_data->size()));
    return oldPos;
  }

protected:
  size_t _pos;
  T _data;
  
};

template<class T>
using BorrowedMemStreamReadOnly = MemStreamBase<const std::vector<T>*>;

template<class T>
class BorrowedMemStream : public MemStreamBase<std::vector<T>*>, public OutputStream {
public:
  BorrowedMemStream(std::vector<T>* content): MemStreamBase<std::vector<T>*>(content) {}
  size_t write(const unsigned char* buffer, const size_t size) override {
    size_t remaining = _data->size() - _pos;
    if (size > remaining) {
      _data->resize(_pos + size);
    }
    memcpy(_data->data() + _pos, buffer, size);
    _pos += size;
    return size;
  }

protected:
  using MemStreamBase<std::vector<T>*>::_data;
  using MemStreamBase<std::vector<T>*>::_pos;
};

class MemStream : public BorrowedMemStream<uint8_t> {
private:
  std::vector<uint8_t> _owned_data;

public:
  MemStream();
  MemStream(const std::vector<uint8_t>& content, const size_t off, const size_t sz);
  MemStream(const std::vector<uint8_t>& content) : MemStream(content, 0, content.size()) {};

  void replaceData(const std::vector<uint8_t>& content) {
    _owned_data = content;
    _data = &_owned_data;
  }
  const std::vector<uint8_t>& data() const {
    return _owned_data;
  }
  std::vector<uint8_t> extractData() {
    auto old_data = std::move(_owned_data);
    _data = &_owned_data;
    return old_data;
  }
};

#endif /* MEMSTREAM_H */
