/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Implementation of Brotli compressor. */

#include <brotli/encode.h>

#include <string.h>  /* memcpy, memset */

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

BROTLI_BOOL BrotliEncoderCompress(
    int quality, int lgwin, BrotliEncoderMode mode, size_t input_size,
    const uint8_t* input_buffer, size_t* encoded_size,
    uint8_t* encoded_buffer) {
  memcpy(encoded_buffer, input_buffer, input_size);
  *encoded_size = input_size;
  return BROTLI_TRUE;
}

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
