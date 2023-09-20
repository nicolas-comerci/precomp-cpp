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
#include <string.h>
#include "memstream.h"

MemStream::MemStream(): BorrowedMemStream(nullptr) {
  _pos = 0;
  _data = &_owned_data;
}
MemStream::MemStream(const std::vector<uint8_t>& content, const size_t off, const size_t sz)
  : BorrowedMemStream(nullptr), _owned_data(std::max(std::min(content.size(), off + sz), off) - off) {
  _pos = 0;
  memcpy(_owned_data.data(), content.data() + off, _owned_data.size());
  _data = &_owned_data;
}
