//
// SPDX-FileCopyrightText: Copyright 2020-2024 Arm Limited and/or its affiliates <open-source-office@arm.com>
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

#include "../include/mlw_encode.h"

#include "ml_encoder_internal.hpp"
#include "ml_raw_buffer.hpp"
#include "ml_bit_buffer.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <tuple>
#include <vector>
#include <limits>

constexpr int ZERO_FREQ_THRESHOLD = 5;
constexpr int MIN_ZERO_RUN_LENGTH = 2;

// Create palette from the given frequencies
// Freq index 0-511 correspond to weights -256..255
// partial_data - don't make decisions about data that will be encoded
//                that wasn't included in the frequency analysis.
static void create_palette(palette_t *p, bool partial_data, bool disable_lut)
{
    uint64_t freq64[512] = {0};
    int i, all_cnt, all_max_val;

    // Pair the frequency with the value so that
    // the array can be sorted on frequency while keeping
    // track of the corresponding palette value
    all_cnt = 0;
    all_max_val = 0;
    for ( i = -255; i < 256; i++ )
    {
        if ( i == 0 && p->use_zero_runs ) continue;
        int sign = i < 0;
        int mag = abs(i);
        int palval = (mag << 1) | sign;

        // Store palette value in 16 LSB bits, which will not affect the sorting
        freq64[palval] = ((static_cast<uint64_t>(p->freq[i + 256])) << 16) | palval;
        all_cnt += p->freq[i + 256];

        if ( p->freq[i + 256] > 0 )
        {
            all_max_val = std::max(all_max_val, palval);
        }
    }

    // Cannot use direct offset with partial data.
    p->only_zeros = !partial_data && (all_cnt == 0);
    p->direct_offset = 0;
    if ( !partial_data && (all_cnt != 0) )
    {
        // Find the first non-used weight value around zero (0, -1, +1, -2, +2 etc)
        for ( i = 0; i < 31; i++ )
        {
            if ( (freq64[i] >> 16) != 0 )
            {
                break;
            }
        }
        p->direct_offset = i;
    }

    // Sort in descending frequency order
    std::sort(std::begin(freq64), std::end(freq64), [](uint64_t a, uint64_t b) { return b < a; });

    // Check if all weights fit into the palette (and the palette is not empty)
    p->only_palette = !disable_lut && !partial_data && (freq64[0] >> 16) > 0 && (freq64[32] >> 16) == 0;

    int max_palette_size;
    if ( p->only_palette )
    {
        max_palette_size = 32;
    }
    else
    {
        // For direct-lut we must make sure that the encoded weight
        // index is not > 511. We do that by limiting the palette size
        // such that the greatest value can be reached after subtracting
        // the palette size.
        max_palette_size = std::min(32, 511 - all_max_val);
        if ( max_palette_size == 1 )
        {
            max_palette_size = 0;  // because palette of size 1 is not supported
        }
    }

    // Setup the 32 entry palette
    int max_lut_val = 0, val, cnt, lut_cnt = 0;
    for ( i = 0; i < max_palette_size; i++ )
    {
        cnt = static_cast<int>(freq64[i] >> 16);
        val = freq64[i] & 0xffff;
        // If partial data, all palette entries must be filled (even if they're wrong)
        if ( cnt == 0 && !partial_data ) break;
        p->lut[i] = int16_t(val);
        max_lut_val = std::max(max_lut_val, val);
        lut_cnt += cnt;
    }

    // When all weights are the same nonzero value a palette size of 1 is possible; but not supported.
    // Make the palette 2 entries long and zero the second entry (it's never indexed).
    if ( i == 1 )
    {
        p->lut[i++] = 0;
    }

    // Heuristic for when to use the palette. If more than half of the
    // weights are in the palette then we use it. This ensures we don't
    // use palette for e.g. rectangular distributions.
    int palbits_val;
    if ( !disable_lut && (lut_cnt >= all_cnt / 2) )
    {
        p->palsize = i;
        palbits_val = max_lut_val;
    }
    else
    {
        // No palette
        p->palsize = 0;
        // If no palette, then palbits is used to specify the
        // number of bits required for uncompressed mode, i.e.
        // the number of bits for the greatest weight value
        palbits_val = all_max_val;
    }

    // the palette entry bit width
    // minimum 2-bits (because PALBITS is in range 2..9)
    int palbits = 2;
    while ( (1 << palbits) <= palbits_val )
    {
        palbits++;
    }
    assert(palbits <= 9);
    p->palbits = palbits;
}

static void create_inverse_palette(palette_t *p)
{
    int i;
    int val = p->palsize - p->direct_offset;
    for ( i = 0; i < 256; i++ )
    {
        p->inv_lut[256 + i] = int16_t(val);
        p->inv_lut[256 - i] = int16_t(val + 1);
        val += 2;
    }
    p->inv_lut[0] = 0;

    for ( i = 0; i < p->palsize; i++ )
    {
        val = p->lut[i];
        int sign = val & 1;
        int mag = val >> 1;
        int weight = sign ? -mag : mag;
        assert( ((weight + 256) >= 0) && ((weight + 256) < 512) );
        p->inv_lut[weight + 256] = int16_t(i);
    }
}

// If palette_size is 512, then palette is not used (in that case the palette is setup
// with the standard alternating unsigned to signed mapping)
static void update_palette(mle_context_t *ctx, palette_t *p, const int16_t *weights, int weights_count, bool partial_data, bool disable_lut, bool disable_zruns)
{
    int(&freq)[512] = p->freq;

    int total_zeroes = 0;
    int zeroes_in_run = 0;
    int zeroes_in_all_runs = 0;

    // Calculate frequencies of the given weight stream
    for ( int i = 0; i < weights_count; i++ )
    {
        unsigned weight = weights[i] + 256;
        freq[weight]++;

        uint64_t &value = ctx->weights_used[weight / 64];
        uint64_t mask = (1ull << (weight % 64));
        if ((value & mask) == 0)
        {
            ctx->distinct_weights++;
            value |= mask;
        }

        if ( weights[i] == 0 )
        {
            total_zeroes++;
            zeroes_in_run++;
        }
        else
        {
            if ( zeroes_in_run >= MIN_ZERO_RUN_LENGTH )
            {
                zeroes_in_all_runs += zeroes_in_run;
            }
            zeroes_in_run = 0;
        }
    }

    // Detect trailing zero runs in compression
    if ( zeroes_in_run >= MIN_ZERO_RUN_LENGTH )
    {
        zeroes_in_all_runs += zeroes_in_run;
    }

    int common_val = 0;
    int common_freq = 0;
    for ( int i = 0; i < 512; i++ )
    {
        // Most common non-zero frequency (because we already have that)
        if ( (i != 256) && freq[i] > common_freq )
        {
            common_val = i - 256;
            common_freq = freq[i];
        }
    }

    // Decide if zero-runs (alternating mode) should be used:
    // * zero runs must make up at least half of the zeroes
    // * zero should be the most common symbol
    // * zero should be sufficiently more common than the second most common symbol
    bool use_zero_runs = zeroes_in_all_runs >= (total_zeroes / 2);
    use_zero_runs &= total_zeroes > (ZERO_FREQ_THRESHOLD * common_freq);
    p->use_zero_runs = use_zero_runs && !disable_zruns;
    // Create the palette
    create_palette(p, partial_data, disable_lut);
}

#define NWCFG 13
#define NZCFG 4  // restrict search to ZDIV=0..3
#define MAX_ZWCFG ((NWCFG > NZCFG) ? NWCFG : NZCFG)

// (trunc<<4) | div, 0x20 means uncompressed
static constexpr char w_grc_params[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x20};
static constexpr char z_grc_params[] = {0x00, 0x01, 0x02, 0x03};

struct grc_param_t
{
    int cfg = 0;
    int end_pos = 0;
};

template <typename TYPE>
static int search_grc_params(const TYPE *inval_buf, int n_inval, int zrun_mode, int uncompressed_bits,
                             std::vector<grc_param_t> &result, bool single_slice)
{
    assert(uncompressed_bits < 32);
    int n_cfg = zrun_mode ? NZCFG : NWCFG;
    const char *grc_params = zrun_mode ? z_grc_params : w_grc_params;

    // Greedy forward-only GRC search (with optimisation for avoiding
    // unusable GRC parameters).
    const int cmd_cost = 40;

    int bit_count[MAX_ZWCFG] = {0};
    int reset_pos[MAX_ZWCFG] = {0};
    bool coded[MAX_ZWCFG] = {false};
    bool any_uncodable[MAX_ZWCFG] = {false};
    int active_bitcount = 0;
    int active_cfg = -1;
    int add_uncompressed_bits = (uncompressed_bits > 0) ? uncompressed_bits : 100;

    for ( int i = 0; i < n_inval; i++ )
    {
        int value = inval_buf[i];

        int best_bitcount = 0x7FFFFFFF;
        int best_cfg = 0;

        // Loop over GRC parameters, calculate bits to code value, and then update the search state
        for ( int j = 0; j < n_cfg; j++ )
        {
            int div = grc_params[j] & 15;
            int trunc = grc_params[j] >> 4;
            int q = value >> div;
            int bits = trunc ? std::min(q + 1, 2) + div : q + 1 + div;

            bool can_code = !(!zrun_mode && ((trunc && q > 2) || q > 31));
            if ( trunc == 2 )
            {
                bits = add_uncompressed_bits;
                can_code = true;
            }

            if ( can_code )
            {
                if ( !coded[j] )
                {
                    bit_count[j] = active_bitcount;  // Reset non-coded to current best
                }
                bit_count[j] = bit_count[j] + bits;

                if ( bit_count[j] < best_bitcount )
                {
                    best_bitcount = bit_count[j];
                    best_cfg = j;
                }
            }
            else
            {
                reset_pos[j] = i + 1;
                bit_count[j] += cmd_cost;  // Would have to change away if used
            }

            coded[j] = can_code;
            any_uncodable[j] |= !can_code;
        }

        // In single-slice mode we can check the bit counts afterwards; otherwise we record
        // slice start points by tracking the minimum of the accumulting bit counts for
        // different grc parameters.
        if ( !single_slice )
        {
            bool must_code = (active_cfg == -1) || !coded[active_cfg];
            if ( must_code || ((best_cfg != active_cfg) && (best_bitcount + cmd_cost) < active_bitcount) )
            {
                // Commit non-initial changes
                if ( active_cfg != -1 )
                {
                    // Range elision (was the other config better all along?)
                    if ( (bit_count[best_cfg] < bit_count[active_cfg]) && (reset_pos[best_cfg] <= reset_pos[active_cfg]) )
                    {
                        // If the current BEST config started  before the ACTIVE config in time, then switch to using the BEST config.
                        active_cfg = best_cfg;  // Duplicated on both paths for clarity
                    }
                    else
                    {
                        // Otherwise use the ACTIVE config for this slice before switching to the BEST config.
                        grc_param_t param;
                        param.cfg = active_cfg;
                        param.end_pos = i;
                        assert((active_cfg != 12) || (uncompressed_bits != 0));
                        result.push_back(param);
                    }
                }
                active_cfg = best_cfg;
            }
        }
        else if ( active_cfg == -1 )
        {
            active_cfg = best_cfg;
        }

        active_bitcount = bit_count[active_cfg];
    }

    // terminate the run
    if ( result.empty() || (result.back().cfg != active_cfg) )
    {
        // If single slice then select the best minimum-bits configuration
        if (single_slice)
        {
            assert( result.empty() );
            active_cfg = -1;
            int max_bit_count = std::numeric_limits<int>::max();
            for (int i=0; i < n_cfg; i++)
            {
                if ( !any_uncodable[i] && (bit_count[i] <= max_bit_count) )
                {
                    if ( (active_cfg != 12) || (uncompressed_bits != 0) )
                    {
                        active_cfg = i;
                        max_bit_count = bit_count[i];
                    }
                }
            }
            assert(active_cfg != -1); // There isn't a usable grc parameter (fatal)
        }

        grc_param_t param;
        param.cfg = active_cfg;
        assert((active_cfg != 12) || (uncompressed_bits != 0));
        result.push_back(param);
    }
    result.back().end_pos = n_inval;

    return active_bitcount;
}

#if !ENABLE_DEBUG_BITSTREAM
    // Release build putbits macro
    #define bitbuf_put(bb_, name_, len_, data_) bb_->put(len_, data_)
    #define bitbuf_align(bb_, name_, len_, data_) bb_->align(len_, data_)
#else
    // Debug build putbits macro
    inline void bitbuf_put(bitbuf_t *bb, const char *name, int len, int data)
    {
        assert(len <= 32);
        int pre_pos = bb->pos();
        bb->put(len, data);
        BITSTREAM_LOG("%6d  %s:%d = %d\n", pre_pos, name, bb->pos()-pre_pos, data);
    }

    // Debug build putbits macro
    inline void bitbuf_align(bitbuf_t *bb, const char *name, int len, int data)
    {
        assert(len <= 32);
        int pre_pos = bb->pos();
        bb->align(len, data);
        BITSTREAM_LOG("%6d  %s:%d = %d\n", pre_pos, name, bb->pos()-pre_pos, data);
    }
#endif

static slice_params_t encode_slice_header(mle_context_t *ctx, int slicelen, bool new_palette, int uncompressed_bits, int w_cfg, int z_cfg, bitbuf_t *bb)
{
    assert( (ctx->slicelen_bits == 15) || (ctx->slicelen_bits == 17) ); // Currently known formats
    assert( slicelen < (1 << ctx->slicelen_bits) );
    assert( w_cfg >= 0 && w_cfg < (sizeof(w_grc_params)/sizeof(w_grc_params[0])));

    // GRC parameters for this slice
    int w_grc_trunc = (w_grc_params[w_cfg] >> 4) == 1;
    int w_uncompressed = (w_grc_params[w_cfg] >> 4) == 2;
    // Callers can signal a truly empty slice with a negative z_cfg index
    assert( ((z_cfg < 0) && (slicelen == 0)) || (ctx->allow_empty_slices && slicelen >= 0) || (slicelen >= 1) );
    int z_grc_div = (z_cfg < 0) ? ZDIV_DISABLE : z_grc_params[z_cfg] & 15;
    int w_grc_div = w_uncompressed ? uncompressed_bits : (w_grc_params[w_cfg] & 15);

    int zdiv = ctx->palette.use_zero_runs ? z_grc_div : ZDIV_DISABLE;
    int wdiv = !w_uncompressed ? w_grc_div : WDIV_UNCOMPRESSED;

    if ( ENABLE_DEBUG_BITSTREAM )
    {
        BITSTREAM_LOG("slice: bitoffset %d slicelen %d zdiv %d wdiv %d wtrunc %d newpal %d palbits %d palsize %d\n", bb->pos(),
                       slicelen, zdiv, wdiv, w_grc_trunc, new_palette, ctx->palette.palbits, ctx->palette.palsize);
    }

    // Write slice header
    bitbuf_put(bb, "ZDIV", 3, zdiv);
    bitbuf_put(bb, "SLICELEN", ctx->slicelen_bits, ctx->allow_empty_slices ? slicelen : slicelen - 1);
    bitbuf_put(bb, "WDIV", 3, wdiv);
    bitbuf_put(bb, "WTRUNC", 1, w_grc_trunc);
    bitbuf_put(bb, "NEWPAL", 1, new_palette);
    if ( new_palette )
    {
        bitbuf_put(bb, "DIROFS", 5, ctx->palette.direct_offset);
        bitbuf_put(bb, "PALSIZE", 5, std::max(0, ctx->palette.palsize - 1));
        bitbuf_put(bb, "PALBITS", 3, ctx->palette.palbits - 2);
        for (int i = 0; i < ctx->palette.palsize; i++ )
        {
            bitbuf_put(bb, "PALETTE", ctx->palette.palbits, ctx->palette.lut[i]);
        }
    }

    slice_params_t header;
    header.w_grc_trunc = w_grc_trunc;
    header.w_uncompressed = w_uncompressed;
    header.z_grc_div = z_grc_div;
    header.w_grc_div = w_grc_div;
    return header;
}


static void encode_slice(mle_context_t *ctx, const int16_t *w_values, int weight_count, const int32_t *z_values, int zero_count, bool new_palette,
                         int uncompressed_bits, int w_cfg, int z_cfg, bitbuf_t *bb)
{
    int w_cnt = weight_count;
    int z_cnt = (z_values && zero_count) ? w_cnt + (new_palette ? 1 : 0) : 0;

    slice_params_t hdr = encode_slice_header(ctx, w_cnt, new_palette, uncompressed_bits, w_cfg, z_cfg, bb);

    assert(z_cfg >= 0 && "slice was signalled as truly empty");

    // Record slice parameters for HW testbench debugging
    if ( ctx->enable_slice_debug )
    {
        ctx->slice_debug.push_back( mle_slice_debug_t { hdr, ctx->palette } );
    }

    int  w_grc_div = hdr.w_grc_div;
    bool w_uncompressed = hdr.w_uncompressed;
    int  z_grc_div = hdr.z_grc_div;
    int  w_grc_trunc = hdr.w_grc_trunc;

    int j;
    int z_unary_len = z_grc_div < 3 ? 12 : 8;
    int w_pos = 0, z_pos = 0;
    int w_unary0 = 0, w_unary1 = 0, w_unary1_len = 0, w_q = -1, w_r = 0;
    int z_unary = 0, z_q = -1, z_r = 0;

    int w_remain_data[2][12] = {{0}};
    int *w_remain = w_remain_data[0];
    int *w_prev_remain = w_remain_data[1];
    int w_nsymbols = 0;
    int w_prev_enable = 0, w_prev_nsymbols = 0;

    int z_remain_data[2][12] = {{0}};
    int *z_remain = z_remain_data[0];
    int *z_prev_remain = z_remain_data[1];
    int z_nsymbols = 0;
    int z_prev_enable = 0, z_prev_nsymbols = 0;
    bool use_zero_runs = ctx->palette.use_zero_runs;

    do
    {
        int balance = use_zero_runs ? w_pos - z_pos : 0;
        int w_enable = balance < 8 && w_pos < w_cnt;
        int z_enable = balance >= 0 && use_zero_runs && z_pos < z_cnt;
        if ( w_enable )
        {
            // Encode chunk (weights)
            j = 0;
            w_nsymbols = 0;
            w_unary0 = 0;
            w_unary1 = 0;
            w_unary1_len = 0;
            int max_symbols = (w_uncompressed && w_grc_div > 5) ? 8 : 12;
            while ( j < max_symbols )
            {
                if ( w_q < 0 )
                {
                    if ( w_pos < w_cnt )
                    {
                        int value = w_values[w_pos];
                        assert( value >= 0 && value < 512 );
                        w_q = value >> w_grc_div;
                        w_r = value & ((1 << w_grc_div) - 1);
                        assert(w_q <= 31 && (!w_grc_trunc || w_q <= 2));
                    }
                    else
                    {
                        w_q = 0;
                        w_r = -1;  // don't send remainder
                    }
                }
                while ( w_q >= 0 && j < max_symbols )
                {
                    w_unary0 |= w_q > 0 ? (1 << j) : 0;
                    if ( w_q > 0 )
                    {
                        w_unary1 |= w_q > 1 ? (1 << w_unary1_len) : 0;
                        w_unary1_len++;
                    }
                    j++;
                    w_q -= 2;
                    if ( w_grc_trunc ) w_q--;
                }
                if ( w_q < 0 && w_r >= 0 )
                {
                    w_remain[w_nsymbols] = w_r;
                    w_nsymbols++;
                    w_pos++;
                }
            }
        }

        if ( z_enable )
        {
            // Encode chunk (zrun)
            j = 0;
            z_nsymbols = 0;
            z_unary = 0;
            while ( j < z_unary_len )
            {
                if ( z_q < 0 )
                {
                    if ( z_pos < z_cnt )
                    {
                        int value = z_values[z_pos];
                        z_q = value >> z_grc_div;
                        z_r = value & ((1 << z_grc_div) - 1);
                        assert( z_q >= 0 );  // There are no negative length z-runs
                    }
                    else
                    {
                        z_q = 0;
                        z_r = -1;
                    }
                }
                while ( z_q >= 0 && j < z_unary_len )
                {
                    z_unary |= z_q > 0 ? (1 << j) : 0;
                    j++;
                    z_q--;
                }
                if ( z_q < 0 && z_r >= 0 )
                {
                    assert( z_nsymbols < 12 );
                    z_remain[z_nsymbols] = z_r;
                    z_nsymbols++;
                    z_pos++;
                }
            }
        }

        // Write chunk to bitstream
        if ( w_enable && !w_uncompressed )
        {
            bitbuf_put(bb, "WUNARY0", 12, w_unary0);  // 12 bits
        }
        if ( z_enable )
        {
            bitbuf_put(bb, "ZUNARY", z_unary_len, z_unary);  // 12 or 8 bits
        }
        if ( w_enable && !w_uncompressed && (w_unary1_len > 0) )
        {
            bitbuf_put(bb, "WUNARY1", w_unary1_len, w_unary1);  // max 12 bits
        }
        if ( w_prev_enable )
        {
            for (int i = 0; i < w_prev_nsymbols; i++ )
            {
                bitbuf_put(bb, "WREMAIN", w_grc_div, w_prev_remain[i]);
            }
        }
        if ( z_prev_enable && (z_grc_div > 0) )
        {
            for (int i = 0; i < z_prev_nsymbols; i++ )
            {
                bitbuf_put(bb, "ZREMAIN", z_grc_div, z_prev_remain[i]);
            }
        }
        w_prev_enable = w_enable;
        w_prev_nsymbols = w_nsymbols;
        std::swap(w_prev_remain, w_remain);
        z_prev_enable = z_enable;
        z_prev_nsymbols = z_nsymbols;
        std::swap(z_prev_remain, z_remain);
    } while ( w_prev_enable || z_prev_enable );
}


int ml_encode_section(mle_context_t *ctx, const int16_t *inbuf, int size, palette_t *p, bitbuf_t *bitbuf)
{
    bool new_palette = (p != nullptr);

    // Reuse previous if not specified
    if ( p == nullptr )
    {
        p = &ctx->palette;
    }

    // Uncompressed mode can only be used if either all weights
    // are in the palette OR if the palette is not used.
    int  uncompressed_bits = 0;
    if ( p->only_palette )
    {
        // Uncompressed bits derived from palette size
        while ( (1 << uncompressed_bits) < p->palsize )
        {
            uncompressed_bits++;
        }
    }
    else if ( p->palsize == 0 )
    {
        // Uncompressed bits is palbits (which is the bitdepth of the greatest weight)
        uncompressed_bits = p->palbits;
    }

    // If there are no weights at all, emit an empty slice header, then exit.
    if ( size == 0 )
    {
        // Signal a truly empty slice using -ve zgrc to ensure ZDIV_DISABLE is written to the stream.
        if ( ctx->allow_empty_slices )
        {
            encode_slice_header(ctx, 0, new_palette, uncompressed_bits, 0, -1, bitbuf);
        }
        return 0;
    }

    std::vector<int16_t> weight_values;
    weight_values.reserve(size);

    // If zruns was enabled, expect total to be < weight_values/2
    std::vector<int32_t> zrun_values;
    if ( p->use_zero_runs )
    {
        zrun_values.reserve( size / 4);
    }

    // Get weights (or weight indicies) AND zero-runs from the input weight stream.
    int i = 0;
    bool allow_empty_slices = !p->only_zeros || ctx->allow_empty_slices;
    int total_zcnt = 0;
    const int max_slice_len = (1 << ctx->slicelen_bits) - 1;
    const int max_zero_run_length = ctx->allow_empty_slices ? max_slice_len - 1 : INT32_MAX;
    while ( 1 )
    {
        if ( p->use_zero_runs )
        {
            int zcnt = 0;
            // Count zero run
            // Special case: if all weights in the section are zero, we must
            // still ensure we have one coded weight so the the slice length
            // doesn't become 0. Therefore we skip the first zero run and code
            // the zero explicitly as a weight value instead
            if ( allow_empty_slices || i > 0 )
            {
                while ( i < size && inbuf[i] == 0 && zcnt < max_zero_run_length )
                {
                    zcnt++;
                    i++;
                }
            }
            total_zcnt += zcnt;
            zrun_values.push_back(zcnt);
        }
        if ( i == size ) break;
        int16_t value = p->inv_lut[inbuf[i] + 256];
        weight_values.push_back(value);
        i++;
    }

    // Search for good GRC parameters for the weight stream
    std::vector<grc_param_t> w_slice_cfg;
    int n_weights = int(weight_values.size());
    if ( n_weights )
    {
        // Use a fixed grc config index if provided (partial-data mode sets this)
        if ( ctx->fixed_wgrc >= 0 )
        {
            w_slice_cfg.push_back(grc_param_t{ ctx->fixed_wgrc,  n_weights });
        }
        else
        {
            search_grc_params(weight_values.data(), n_weights, 0, uncompressed_bits, w_slice_cfg, ctx->single_slice_sections);
        }
    }
    int n_w_slice = int(w_slice_cfg.size());

    // Search for good GRC parameters for the zrun stream
    std::vector<grc_param_t> z_slice_cfg;
    if ( p->use_zero_runs )
    {
        // Use a fixed grc config index if provided (partial-data mode sets this)
        if ( ctx->fixed_zgrc >= 0 )
        {
            z_slice_cfg.push_back(grc_param_t{ ctx->fixed_zgrc,  n_weights + 1 });
        }
        else
        {
            search_grc_params(zrun_values.data(), n_weights + 1, 1, 0, z_slice_cfg, ctx->single_slice_sections);
        }
    }
    int n_z_slice = int(z_slice_cfg.size());

    int loops = 0;

    // Encode bitstream slice
    int pos = 0, i_w_slice = 0, i_z_slice = 0;
    bool only_zero_runs_pass = !zrun_values.empty();
    while ( (pos < n_weights) || new_palette || only_zero_runs_pass )
    {
        int w_len = 0;
        int z_len = 0;

        if ( i_w_slice < n_w_slice )
        {
            w_len = w_slice_cfg[i_w_slice].end_pos - pos;
            w_len = std::min(w_len, max_slice_len);
        }

        if ( i_z_slice < n_z_slice )
        {
            z_len = z_slice_cfg[i_z_slice].end_pos - pos;
            z_len = std::min(z_len, max_slice_len);
        }

        // The first slice (when new_palette is 1) encodes zero runs both at the
        // beginning and end (i.e. number of zero runs are len+1).
        // The following slices only encode zero runs at the end (there cannot be
        // any zeros in the beginning since they are encoded by the previous slice)
        const int32_t *zrun_buf = p->use_zero_runs ? zrun_values.data() + pos + !(new_palette || ctx->allow_empty_slices) : nullptr;
        const int16_t *w_buf = w_len ? weight_values.data() + pos : nullptr;
        int            w_cfg = w_len ? w_slice_cfg[i_w_slice].cfg : 0;
        int            z_cfg = p->use_zero_runs ? z_slice_cfg[i_z_slice].cfg : 0;

        encode_slice(ctx, w_buf, w_len, zrun_buf, z_len, new_palette, uncompressed_bits, w_cfg, z_cfg, bitbuf);
        new_palette = 0;

        if ( z_len <= 0 && w_len > 0 )
            pos += w_len;
        else if ( w_len <= 0 && z_len > 0 )
            pos += z_len;
        else
            pos += std::min(z_len, w_len);

        if ( i_w_slice < n_w_slice && w_slice_cfg[i_w_slice].end_pos <= pos )
        {
            i_w_slice++;
        }
        if ( i_z_slice < n_z_slice && z_slice_cfg[i_z_slice].end_pos <= pos )
        {
            i_z_slice++;
        }
        loops++;
        only_zero_runs_pass = false;
    }
    // Single-slice sections can only generate one slice (a single loop)
    assert( !ctx->single_slice_sections || (ctx->single_slice_sections && loops == 1) );
    if ( ctx->single_slice_sections && (loops != 1) )
    {
        return -1;
    }
    return total_zcnt;
}


palette_t *ml_encode_palette(mle_context_t *ctx, const int16_t *weights, int encode_count, int analyse_count, unsigned mlw_encode_flags)
{
    palette_t *palette = nullptr;
    if ( !ctx->palette_valid || (mlw_encode_flags & MLW_ENCODE_INSERT_PALETTE) )
    {
        if (mlw_encode_flags & MLW_ENCODE_RESET_PALETTE)
        {
            memset( ctx->palette.freq, 0, sizeof(ctx->palette.freq) );
        }

        bool partial_data = (mlw_encode_flags & MLW_ENCODE_PARTIAL_DATA) != 0;
        bool disable_lut = (mlw_encode_flags & MLW_ENCODE_NO_PALETTE_LUT) != 0;
        bool disable_zruns = (mlw_encode_flags & MLW_ENCODE_NO_ZERO_RUNS) != 0;

        assert( analyse_count >= encode_count && "Must analyse at least as much as is encoded");
        update_palette(ctx, &ctx->palette, weights, analyse_count, partial_data, disable_lut, disable_zruns);
        ctx->palette_valid = true;
        if ( !(mlw_encode_flags & MLW_ENCODE_DPIC_FORCE_PARAMS) )
        {
            ctx->fixed_wgrc = (partial_data) ? 5 : -1;
            ctx->fixed_zgrc = (partial_data) ? 3 : -1;
        }

        create_inverse_palette(&ctx->palette);
        palette = &ctx->palette;
    }
    return palette;
}

void ml_encode_eos(mle_context_t *ctx, bitbuf_t &bits, unsigned mlw_encode_flags)
{
    // Add end of stream marker and align to 128bit
    bitbuf_t *bb = &bits;
    if ( ctx->eos_required )
    {
        bitbuf_put(bb, "ZDIV", 3, ZDIV_EOS);
    }
    bitbuf_align(bb, "BYTEALIGN", 8, 0xff);

    if ( !(mlw_encode_flags & MLW_ENCODE_NO_PADDING) )
    {
        bb->align( 128, 0xFF );
    }
    bb->flush();
}

int ml_encode_internal(mle_context_t *ctx, bitbuf_t &bits, const int16_t *weights, int encode_count, int analyse_count, unsigned mlw_encode_flags)
{
    palette_t *palette = ml_encode_palette(ctx, weights, encode_count, analyse_count, mlw_encode_flags);

    int zresult = ml_encode_section(ctx, weights, encode_count, palette, &bits);
    if ( zresult < 0 )
    {
        return -1;
    }
    ctx->zero_count += zresult;
    return 0;
}

extern "C"
{

ML_ENCODER_DLL_EXPORT mle_context_t *mle_create_context(int32_t syntax)
{
    mle_context_t *ctx = new mle_context_t;
    ctx->zero_count = 0;
    ctx->syntax = syntax;
    ctx->realloc_func = nullptr;
    if (syntax == MLW_ENCODE_SYNTAX_ETHOSU)
    {
        ctx->slicelen_bits = ETHOSU_SLICELEN_BITS;
        ctx->allow_empty_slices = false;
        ctx->single_slice_sections = false;
        ctx->eos_required = true;
    }
    else if (syntax == MLW_ENCODE_SYNTAX_ETHOSU_FWD)
    {
        ctx->slicelen_bits = 0;
    }
    else
    {
        assert(false && "bad syntax");
        delete ctx;
        return nullptr;
    }
    return ctx;
}

ML_ENCODER_DLL_EXPORT int mle_context_query_zeroes(mle_context_t *ctx)
{
    assert( ctx );
    return ctx->zero_count;
}

ML_ENCODER_DLL_EXPORT int mle_context_query_weights_used(mle_context_t *ctx, uint64_t weights_used[512 / 64])
{
    assert( ctx );
    std::copy(std::begin(ctx->weights_used), std::end(ctx->weights_used), weights_used);
    return ctx->distinct_weights;
}

ML_ENCODER_DLL_EXPORT void mle_context_set_allocator(mle_context_t *ctx, void* (*realloc_func)(void*, size_t, int))
{
    assert( ctx );
    ctx->realloc_func = realloc_func;
}

ML_ENCODER_DLL_EXPORT void mle_destroy_context(mle_context_t *ctx)
{
    assert(ctx);
    delete ctx;
}

ML_ENCODER_DLL_EXPORT int mle_encode(mle_context_t *ctx, ml_encode_result_t *result, const int16_t *inbuf, int inbuf_size, unsigned mlw_encode_flags)
{
    assert( ctx && result );
    raw_buffer_t<uint8_t> output(4096, MLW_ENCODE_ALLOC_STREAM0, ctx->realloc_func);
    bitbuf_t bits(output, 4096, mlw_encode_flags & MLW_ENCODE_NO_BITSTREAM);
    int written = 0;

    if ( ctx->syntax == MLW_ENCODE_SYNTAX_ETHOSU_FWD )
    {
        written = ml_encode_fwd(ctx, bits, inbuf, inbuf_size, mlw_encode_flags);
    }
    else
    {
        int start = bits.byte_pos();
        if ( ml_encode_internal(ctx, bits, inbuf, inbuf_size, inbuf_size, mlw_encode_flags) < 0 )
        {
            return -1;
        }
        ml_encode_eos(ctx, bits, mlw_encode_flags);
        written = bits.byte_pos() - start;
    }

    if ( written >= 0 )
    {
        result->encoded_data = output.detach();
        result->source_length = inbuf_size;
        result->encoded_length = written;
        result->section_info = nullptr;
        result->section_count = 0;
    }
    return written;
}

ML_ENCODER_DLL_EXPORT void mle_free(ml_encode_result_t *result)
{
    if ( result )
    {
        if ( result->encoded_data )
        {
            free( result->encoded_data );
            result->encoded_data = nullptr;
        }
        if ( result->section_info )
        {
            free( result->section_info );
            result->section_info = nullptr;
        }
    }
}

}
