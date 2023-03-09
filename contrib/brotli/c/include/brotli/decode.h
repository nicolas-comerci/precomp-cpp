/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/**
 * @file
 * API for Brotli decompression.
 */

#ifndef BROTLI_DEC_DECODE_H_
#define BROTLI_DEC_DECODE_H_

#include <brotli/port.h>
#include <brotli/types.h>

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/**
 * Opaque structure that holds decoder state.
 *
 * Allocated and initialized with ::BrotliDecoderCreateInstance.
 * Cleaned up and deallocated with ::BrotliDecoderDestroyInstance.
 */
typedef struct BrotliDecoderStateStruct BrotliDecoderState;

/**
 * Result type for ::BrotliDecoderDecompress and
 * ::BrotliDecoderDecompressStream functions.
 */
typedef enum {
  /** Decoding error, e.g. corrupted input or memory allocation problem. */
  BROTLI_DECODER_RESULT_ERROR = 0,
  /** Decoding successfully completed. */
  BROTLI_DECODER_RESULT_SUCCESS = 1,
  /** Partially done; should be called again with more input. */
  BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT = 2,
  /** Partially done; should be called again with more output. */
  BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT = 3
} BrotliDecoderResult;

// We just return nullptr so anything that attempts to create a decoder fails immediately
BROTLI_DEC_API BrotliDecoderState* BrotliDecoderCreateInstance(
    brotli_alloc_func alloc_func, brotli_free_func free_func, void* opaque);

// As we don't actually ever allow creation of instances, this function is just empty
BROTLI_DEC_API void BrotliDecoderDestroyInstance(BrotliDecoderState* state);

// Brunsli decode attempts to use this when decompressing the metadata Brotli stream, we have actually hijacked encoding so there is only uncompressed data there.
// We just copy the data and return a successful return code
BROTLI_DEC_API BrotliDecoderResult BrotliDecoderDecompress(
    size_t encoded_size,
    const uint8_t encoded_buffer[BROTLI_ARRAY_PARAM(encoded_size)],
    size_t* decoded_size,
    uint8_t decoded_buffer[BROTLI_ARRAY_PARAM(*decoded_size)]);

// Idem but when attempting to verify a Brotli stream, we just return success code
BROTLI_DEC_API BrotliDecoderResult BrotliDecoderDecompressStream(
  BrotliDecoderState* state, size_t* available_in, const uint8_t** next_in,
  size_t* available_out, uint8_t** next_out, size_t* total_out);

// Brunsli also attempts to use this when verifying a Brotli stream, we just return a nullptr.
BROTLI_DEC_API const uint8_t* BrotliDecoderTakeOutput(BrotliDecoderState* state, size_t* size);

#if defined(__cplusplus) || defined(c_plusplus)
} /* extern "C" */
#endif

#endif  /* BROTLI_DEC_DECODE_H_ */
