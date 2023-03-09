/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#include <brotli/decode.h>

#include <string.h>  /* memcpy, memset */

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

BrotliDecoderState* BrotliDecoderCreateInstance(
    brotli_alloc_func alloc_func, brotli_free_func free_func, void* opaque) {
  return 0;
}

/* Deinitializes and frees BrotliDecoderState instance. */
void BrotliDecoderDestroyInstance(BrotliDecoderState* state) { }

BrotliDecoderResult BrotliDecoderDecompress(
    size_t encoded_size, const uint8_t* encoded_buffer, size_t* decoded_size,
    uint8_t* decoded_buffer) {
  memcpy(decoded_buffer, encoded_buffer, encoded_size);
  return BROTLI_DECODER_RESULT_SUCCESS;
}

BrotliDecoderResult BrotliDecoderDecompressStream(
    BrotliDecoderState* s, size_t* available_in, const uint8_t** next_in,
    size_t* available_out, uint8_t** next_out, size_t* total_out) {
  available_in = 0;
  return BROTLI_DECODER_RESULT_SUCCESS;
}

const uint8_t* BrotliDecoderTakeOutput(BrotliDecoderState* s, size_t* size) {
  return 0;
}

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
