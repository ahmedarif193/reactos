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
#include <cstring>
#include <tuple>
#include <vector>
#include <limits>

constexpr static int LUT_MAX = 16;
constexpr static int INV_MAX = 512;

struct fwd_header_t
{
    bool raw_mode_flag = false;
    bool small_lut_flag = false;
    int8_t zero_adjust = 0;
    int16_t lut[LUT_MAX] = {};
    int8_t  inv_index[INV_MAX] = {};
    fwd_header_t()
    {
        std::fill_n(inv_index, INV_MAX, -1);
    }
};


static inline int32_t fold(int32_t value)
{
    // Fold into positive value (sign in lsb)
    return (abs(value) << 1) | (uint32_t(value) >> 31);
}


void fwd_emit_header(bitbuf_t &bits, const fwd_header_t &hdr)
{
    bits.put( 1, hdr.raw_mode_flag ? 1 : 0 );
    bits.put( 1, hdr.small_lut_flag ? 1 : 0 );
    bits.fill( 102, 0 );
    bits.put_masked( 8, hdr.zero_adjust );
    for (int i = 0; i < LUT_MAX; i++)
    {
        bits.put( 9, fold(hdr.lut[i]) );
    }
}


bool fwd_analyse(fwd_header_t &hdr, const int16_t *weights, int count, bitbuf_t &bits)
{
    int range_min = 1000;
    int range_max = -1000;
    int8_t  *inv = hdr.inv_index;
    int16_t *lut = hdr.lut;
    int lut_used = 0;
    bool use_lut = true;

    // Must check all the zero-point-correct values for full range
    for (int i = 0; i < count; i++)
    {
        int value = weights[i];
        range_min = std::min( range_min, value );
        range_max = std::max( range_max, value );

        // Update the LUT only while it's still viable (predicts well).
        if ( use_lut )
        {
            // Map the signed value to the LUT via +ve indexed table
            int idx = fold(value);
            assert( idx < INV_MAX );

            // Check if value has already been indexed before adding a
            // new lut entry.
            if ( inv[idx] < 0 )
            {
                if ( lut_used < LUT_MAX )
                {
                    inv[idx] = lut_used;
                    lut[lut_used] = value;
                    lut_used++;
                }
                else
                {
                    use_lut = false; // LUT was full and is now unusable
                }
            }
            // While lut2 is valid, encode the entries. When we're
            // done the bitstream will be ready.
            if (lut_used <= 4)
            {
                bits.put(2, inv[idx]);
            }
        }
    }

    hdr.raw_mode_flag = !use_lut;
    hdr.small_lut_flag = (lut_used <= 4);
    hdr.zero_adjust = 0;

    // If raw mode, calculate the zero point
    if ( hdr.raw_mode_flag )
    {
        int full_range = (range_max - range_min);
        if (full_range >= 256)
        {
            return false;   // Can't encode this stream
        }
        else if ( range_min < -128 )
        {
            hdr.zero_adjust = -128 - range_min; // Raw values need offsetting +ve by this amount
        }
        else if ( range_max > 127 )
        {
            hdr.zero_adjust = 127 - range_max; // Raw values need offsetting -ve by this amount
        }
    }

    return (range_min >= -256) && (range_max < 256);
}

// Encode zero-corrected weight values in the optimal fast-weight format.
int ml_encode_fwd(mle_context_t *ctx, bitbuf_t &bits, const int16_t *weights, int count, unsigned mlw_encode_flags)
{
    fwd_header_t header;
    int pos = bits.pos();
    bits.fill(256, 0); // Reserve space for header

    // Encode lut2 weights directly to the main stream while analysing
    if ( !fwd_analyse(header, weights, count, bits) )
    {
        return -1; // Encoding error
    }

    // Check for forced no palette
    if ( mlw_encode_flags & MLW_ENCODE_NO_PALETTE_LUT )
    {
        header.raw_mode_flag = 1;
        header.small_lut_flag = 0;
    }

    // Use a substream of the main stream for the header
    bitbuf_t hdr_bits(bits, pos);
    fwd_emit_header(hdr_bits, header);
    bits.sync(hdr_bits);

    // LUT2
    if ( header.small_lut_flag )
    {
        assert( !header.raw_mode_flag );
    }
    // RAW
    else if ( header.raw_mode_flag )
    {
        bits.reposition(pos + 256);
        for (int i=0; i < count; i++)
        {
            int value = (weights[i] + header.zero_adjust) & 0xFF;
            bits.put(8, value);
        }
    }
    // LUT4
    else
    {
        bits.reposition(pos + 256);
        for (int i=0; i < count; i++)
        {
            int idx = fold(weights[i]);
            bits.put(4, header.inv_index[idx]);
        }
    }

    bits.align(256, 0);
    bits.flush();

    int written = bits.pos() / 8;
    return written;
}
