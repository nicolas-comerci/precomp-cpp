/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/**
 * @file
 * API for Brotli compression.
 */

#ifndef BROTLI_ENC_ENCODE_H_
#define BROTLI_ENC_ENCODE_H_

#include <brotli/port.h>
#include <brotli/types.h>

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/** Minimal value for ::BROTLI_PARAM_LGWIN parameter. */
#define BROTLI_MIN_WINDOW_BITS 10
/**
 * Maximal value for ::BROTLI_PARAM_LGWIN parameter.
 *
 * @note equal to @c BROTLI_MAX_DISTANCE_BITS constant.
 */
#define BROTLI_MAX_WINDOW_BITS 24
/**
 * Maximal value for ::BROTLI_PARAM_LGWIN parameter
 * in "Large Window Brotli" (32-bit).
 */
#define BROTLI_LARGE_MAX_WINDOW_BITS 30
/** Minimal value for ::BROTLI_PARAM_LGBLOCK parameter. */
#define BROTLI_MIN_INPUT_BLOCK_BITS 16
/** Maximal value for ::BROTLI_PARAM_LGBLOCK parameter. */
#define BROTLI_MAX_INPUT_BLOCK_BITS 24
/** Minimal value for ::BROTLI_PARAM_QUALITY parameter. */
#define BROTLI_MIN_QUALITY 0
/** Maximal value for ::BROTLI_PARAM_QUALITY parameter. */
#define BROTLI_MAX_QUALITY 11

/** Options for ::BROTLI_PARAM_MODE parameter. */
typedef enum BrotliEncoderMode {
  /**
   * Default compression mode.
   *
   * In this mode compressor does not know anything in advance about the
   * properties of the input.
   */
  BROTLI_MODE_GENERIC = 0,
  /** Compression mode for UTF-8 formatted text input. */
  BROTLI_MODE_TEXT = 1,
  /** Compression mode used in WOFF 2.0. */
  BROTLI_MODE_FONT = 2
} BrotliEncoderMode;

/** Default value for ::BROTLI_PARAM_MODE parameter. */
#define BROTLI_DEFAULT_MODE BROTLI_MODE_GENERIC

// Fake BrotliEncoderCompress function so Brunsli believes its compressing with Brotli, but actually, data will just be copied as-is
BROTLI_ENC_API BROTLI_BOOL BrotliEncoderCompress(
    int quality, int lgwin, BrotliEncoderMode mode, size_t input_size,
    const uint8_t input_buffer[BROTLI_ARRAY_PARAM(input_size)],
    size_t* encoded_size,
    uint8_t encoded_buffer[BROTLI_ARRAY_PARAM(*encoded_size)]);

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_ENCODE_H_ */
