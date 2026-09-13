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

#if !defined ML_ENCODER_INTERNAL_HPP
#define ML_ENCODER_INTERNAL_HPP

#pragma once

#include "../include/mlw_encode.h"
#include "ml_bit_buffer.hpp"

#include <cstdint>
#include <vector>

#if __GNUC__
    #define ML_ENCODER_DLL_EXPORT __attribute__((visibility("default")))
#elif _WIN32
    #if TARGET_WIN32_DLL
        #define ML_ENCODER_DLL_EXPORT __declspec(dllexport)
    #else
        #define ML_ENCODER_DLL_EXPORT
    #endif
#else
    #error "undefined export semantics"
#endif

#if !defined ENABLE_DEBUG_PACKET
    #define ENABLE_DEBUG_PACKET (0)
#endif

#if !defined ENABLE_DEBUG_BITSTREAM
    #define ENABLE_DEBUG_BITSTREAM (0)
#endif

#if ENABLE_DEBUG_PACKET
    #include <cstdio>
    #define PACKET_LOG(...) printf(__VA_ARGS__)
#else
    #define PACKET_LOG(...)
#endif

#if ENABLE_DEBUG_BITSTREAM
    #include <cstdio>
    #define BITSTREAM_LOG(...) printf(__VA_ARGS__)
#else
    #define BITSTREAM_LOG(...)
#endif

constexpr int ETHOSU_SLICELEN_BITS = 15;
constexpr int ZDIV_DISABLE = 6;  // not alternating mode
constexpr int ZDIV_EOS = 7;      // indicates end of stream
constexpr int WDIV_UNCOMPRESSED = 7;  // indicates uncompressed weights

struct palette_t
{
    int16_t lut[32] = {0};
    int16_t inv_lut[512] = {0};
    int freq[512] = {0};
    int palsize;         // number of palette entries
    int palbits;         // bit width of palette entries
    int direct_offset;   // added to the decoded weight index before direct conversion to sign/mag
    bool use_zero_runs;  // zeros are coded separately
    bool only_palette;   // no values outside the palette
    bool only_zeros;     // special case that the section is all zeros
};

struct slice_params_t
{
    uint8_t w_grc_trunc;
    bool    w_uncompressed;
    uint8_t z_grc_div;
    uint8_t w_grc_div;
};

struct mle_slice_debug_t
{
    slice_params_t params;
    palette_t palette;
};

struct mle_context_t
{
    palette_t palette;
    uint64_t weights_used[512 / 64] = {0};
    int  distinct_weights = 0;
    int  syntax = 0;
    int  zero_count = 0;
    int  slicelen_bits = ETHOSU_SLICELEN_BITS;
    bool palette_valid = false;
    bool single_slice_sections = false;
    bool allow_empty_slices = false;
    bool eos_required = false;
    bool enable_slice_debug = false;
    bool disable_lut = false;
    int8_t fixed_wgrc = -1;
    int8_t fixed_zgrc = -1;
    std::vector<mle_slice_debug_t> slice_debug;
    void* (*realloc_func)(void*, size_t, int); // Custom output allocator function
};

inline int div_round_up(int num, int div)
{
    return (num + div - 1) / div;
}

int ml_encode_fwd(mle_context_t *ctx, bitbuf_t &bits, const int16_t *weights, int encode_count, unsigned mlw_encode_flags);
int ml_encode_section(mle_context_t *ctx, const int16_t *inbuf, int size, palette_t *p, bitbuf_t *bitbuf);
palette_t *ml_encode_palette(mle_context_t *ctx, const int16_t *weights, int encode_count, int analyse_count, unsigned mlw_encode_flags);
void ml_encode_eos(mle_context_t *ctx, bitbuf_t &bits, unsigned mlw_encode_flags);
int ml_encode_internal(mle_context_t *ctx, bitbuf_t &bits, const int16_t *weights, int encode_count, int analyse_count, unsigned mlw_encode_flags);

#endif // ML_ENCODER_INTERNAL_HPP



