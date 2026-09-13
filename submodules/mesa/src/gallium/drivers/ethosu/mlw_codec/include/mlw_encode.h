//
// SPDX-FileCopyrightText: Copyright 2022-2024 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the License); you may
// not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an AS IS BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#if !defined MLW_ENCODE_H
#define MLW_ENCODE_H

#include <stdint.h>
#include <stddef.h>

#if defined _MSC_VER
    #define MLW_CODEC_PACKED
    #define MLW_CODEC_USE_PACK_PRAGMA (1)
#else // __GNUC__ and clang
    #define MLW_CODEC_PACKED __attribute__((packed))
#endif

// Encoder input parameters EthosU
typedef struct ml_ethosu_encode_params_t
{
    int32_t source_buffering_hint;     // Recommend a buffering size
    uint16_t encoder_flags;            // Control flags to pass to the encoder
    void* (*realloc_func)(void*, size_t, int purpose); // Custom output allocator function
} ml_ethosu_encode_params_t;

// Resulting encoded section information
typedef struct ml_encode_section_t
{
    int32_t offset;                    // Byte offset of encoded section
    int32_t size;                      // Byte size of encoded section
    int32_t zeroes;                    // Number of zeroes encoded in a section
    int8_t  group_start;               // Start of group
} ml_encode_section_t;

// Result of the encode process
typedef struct ml_encode_result_t
{
    uint8_t *encoded_data;              // Encoded weight data
    int32_t  encoded_length;            // Encoded weight length (in bytes)
    int32_t  source_length;             // Source elements read
    ml_encode_section_t *section_info;  // Array of sections in stream
    int32_t  section_count;             // Number of section in stream
} ml_encode_result_t;

#define MLW_SOURCE_QUERY_WEIGHTS 0
#define MLW_SOURCE_QUERY_SHIFTS  1

// State of the source iterator
typedef struct ml_source_state_t
{
    uint8_t new_dim_mask;               // Dimension start mask
    uint8_t end_dim_mask;               // Dimension end mask
    bool    eos;                        // End-of-stream flag
} ml_source_state_t;

// Stream input callback (encoder will collect input through this function, of the size recommended by the buffering hint)
typedef int32_t (*ml_weight_source_fn)(int32_t query, ml_source_state_t *state, int16_t *buffer, int32_t size, void *user_arg);

// Internal state context
typedef struct mle_context_t mle_context_t;

#define MLW_ENCODE_FLAG_NONE (0)       // Default encoding flag
#define MLW_ENCODE_NO_BITSTREAM (1)    // Do not write any bitstream data (only return the length)
#define MLW_ENCODE_INSERT_PALETTE (2)  // Insert a new palette header with this encode
#define MLW_ENCODE_RESET_PALETTE (4)   // Clear and recalculate the palette header
#define MLW_ENCODE_PARTIAL_DATA (8)    // Frequency analysis and palette will be constructed from incomplete data
#define MLW_ENCODE_NO_PADDING (16)     // Disable trailing padding
#define MLW_ENCODE_NO_PALETTE_LUT (32) // Disable palette LUT generation
#define MLW_ENCODE_NO_ZERO_RUNS (64)   // Disable zero run generation
#define MLW_ENCODE_DPIC_FORCE_PARAMS (128) // Force debug parameters
#define MLW_ENCODE_NEW_PALETTE (MLW_ENCODE_INSERT_PALETTE|MLW_ENCODE_RESET_PALETTE)

#define MLW_ENCODE_SYNTAX_ETHOSU (0)   // EthosU bitstream encode syntax
#define MLW_ENCODE_SYNTAX_ETHOSU_FWD (2) // EthosU FWD bitstream encode syntax

#define MLW_ENCODE_ALLOC_GENERAL  (0)   // General allocations used by the encoder
#define MLW_ENCODE_ALLOC_METADATA (1)   // Allocation for codec's metadata output
#define MLW_ENCODE_ALLOC_STREAM0  (2)   // Stream 0 allocation for this codec
#define MLW_ENCODE_ALLOC_STREAM1  (3)   // Stream 1 allocation for this codec

#if defined __cplusplus
extern "C"
{
#endif
    // Baseline encode
    mle_context_t *mle_create_context(int syntax);
    int      mle_context_query_zeroes(mle_context_t *ctx);
    int      mle_context_query_weights_used(mle_context_t *ctx, uint64_t weights_used[512 / 64]);
    void     mle_context_set_allocator(mle_context_t *ctx, void* (*realloc_func)(void*, size_t, int purpose));
    void     mle_destroy_context(mle_context_t *ctx);
    int      mle_encode(mle_context_t *ctx, ml_encode_result_t *result, const int16_t *inbuf, int inbuf_size, unsigned mlw_encode_flags);
    void     mle_free(ml_encode_result_t *result);

    int32_t  ml_encode_ethosu_stream(ml_encode_result_t *result, const ml_ethosu_encode_params_t *ep, ml_weight_source_fn src, void *user_arg, mle_context_t **ctx_out);

#if defined __cplusplus
}  // extern "C"
#endif


#endif // MLW_ENCODE_H
