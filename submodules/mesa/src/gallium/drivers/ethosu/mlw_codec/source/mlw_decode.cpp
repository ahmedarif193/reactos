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

#include "../include/mlw_decode.h"
#include "ml_encoder_internal.hpp"
#include "ml_bit_buffer.hpp"

#include <algorithm>
#include <cassert>
#include <vector>

#if NDEBUG || 1
   // Release build get bits macro
    #define bitbuf_get(bb_, name_, len_) bb_.get(len_)
#else
    #include <cstdio>

    // Debug build get bits macro
    inline int bitbuf_get(bitbuf_t &bb, const char *name, int len)
    {
        assert(len <= 32);
        int tmp = bb.get(len);
        printf("%6d  %s:%d = %d\n", bb.pos() - len, name, len, tmp);
        fflush(stdout);
        return tmp;
    }
#endif

// Extract and decode weights from the given bitstream
//
//   outbuf       - output decoded weights
//   bb           - input bitstream buffer (wherever it is positioned)
//   endpos       - bitstream end position (in bytes)
//   single_slice - process slices one-at-a-time
//   slice_len    - slice length in bits
//
// Returns - the number of weights extracted from the bitstream
static int ml_decode_internal(raw_buffer_t<int16_t> &outbuf, bitbuf_t &bb, palette_t &palette, int endpos, bool single_slice, int slice_len)
{
    int start_offset = outbuf.used();
    int w_cnt;
    int w_grc_div;
    int w_grc_trunc;
    int w_uncompressed;
    int z_grc_div;
    int z_prev_grc_div = -1;
    bool new_palette;
    int i, j;
    endpos = std::min(endpos, bb.byte_length());

    // Loop over all slices
    do {
        // Decode slice header
        int bits_avail = bb.read_avail(endpos);
        if ( bits_avail >= 3 )
        {
            z_grc_div = bitbuf_get(bb, "ZDIV", 3);
        }
        else // Insufficient bits left for a terminator (EOS may be optional)
        {
            z_grc_div = ZDIV_EOS;
            int ones = bb.get(bits_avail);
            assert( ones == (1 << bits_avail) - 1 );
        }

        while ( z_grc_div == ZDIV_EOS )
        {
            // End of stream
            // Byte align
            bb.read_align(8);
            if ( bb.byte_pos() >= endpos || single_slice )
            {
                goto labelExit;
            }
            z_grc_div = bitbuf_get(bb, "ZDIV", 3);
        }
        if ( bb.read_avail(endpos) <= 0 )
        {
            assert(false);
            break;  // Unexpectedly reached end of the input stream
        }
        assert(z_grc_div < 4 || z_grc_div == ZDIV_DISABLE);
        bool use_zero_runs = z_grc_div != ZDIV_DISABLE;  // alternating grc
        w_cnt = bitbuf_get(bb, "SLICELEN", slice_len) + (slice_len == ETHOSU_SLICELEN_BITS ? 1 : 0);
        w_grc_div = bitbuf_get(bb, "WDIV", 3);
        w_grc_trunc = bitbuf_get(bb, "WTRUNC", 1);
        new_palette = bitbuf_get(bb, "NEWPAL", 1);
        if ( !new_palette )
        {
            // At the moment it is not supported to change between alternating
            // and non-alternating without redefining the palette (this is because
            // the zero is not included in the palette in case of alternating)
            bool prev_use_zero_run = z_prev_grc_div != ZDIV_DISABLE;
            (void)(prev_use_zero_run);
            assert((z_prev_grc_div == -1) || (use_zero_runs == prev_use_zero_run));
        }
        z_prev_grc_div = z_grc_div;
        if ( new_palette )
        {
            palette.direct_offset = bitbuf_get(bb, "DIROFS", 5);
            palette.palsize = bitbuf_get(bb, "PALSIZE", 5);
            if ( palette.palsize > 0 )
            {
                palette.palsize++;
            }
            palette.palbits = bitbuf_get(bb, "PALBITS", 3) + 2;
            for ( i = 0; i < palette.palsize; i++ )
            {
                palette.inv_lut[i] = int16_t(bitbuf_get(bb, "PALETTE", palette.palbits));
            }
        }

        if ( w_grc_div == WDIV_UNCOMPRESSED )
        {
            // Uncompressed mode
            w_uncompressed = 1;
            int uncompressed_bits;
            if ( palette.palsize > 0 )
            {
                // Uncompressed bits is given by palette size.
                uncompressed_bits = 0;
                while ( (1 << uncompressed_bits) < palette.palsize )
                {
                    uncompressed_bits++;
                }
            }
            else
            {
                // No palette. PALBITS is used to specify uncompressed bits.
                uncompressed_bits = palette.palbits;
            }
            // In uncompressed mode there's only a remainder part (no unary)
            // This is achieved by setting w_grc_div to index bit width
            w_grc_div = uncompressed_bits;
        }
        else
        {
            w_uncompressed = 0;
            assert(w_grc_div < 6);
        }

        // Decode the slice
        int z_cnt = w_cnt + (( slice_len != ETHOSU_SLICELEN_BITS || new_palette ) ? 1 : 0);
        std::vector<int> w_value(w_cnt);
        std::vector<int> z_value(z_cnt);
        int w_pos = 0, z_pos = 0;
        int w_prev_pos = 0, z_prev_pos = 0;
        int w_unary0 = 0, w_unary1 = 0, w_unary1_len = 0, w_q[12] = {0}, wq = 0;
        int z_unary = 0, z_q[12] = {0}, zq = 0;
        int w_nsymbols = 0;
        int w_prev_enable = 0, w_prev_nsymbols = 0, w_prev_q[12] = {0};
        int z_nsymbols = 0;
        int z_prev_enable = 0, z_prev_nsymbols = 0, z_prev_q[12] = {0};
        int total_zcnt = 0;
        int z_unary_len = z_grc_div < 3 ? 12 : 8;

        // Loop over all chunks in the slice
        do
        {
            // Flow control to possibly throttle either the weights or zero-runs
            int balance = use_zero_runs ? w_pos - z_pos : 0;
            int w_enable = (balance < 8 || !use_zero_runs) && w_pos < w_cnt;
            int z_enable = (balance >= 0 && use_zero_runs) && z_pos < z_cnt;
            if ( w_enable )
            {
                w_unary0 = w_uncompressed ? 0 : bitbuf_get(bb, "WUNARY0", 12);
            }
            if ( z_enable )
            {
                z_unary = bitbuf_get(bb, "ZUNARY", z_unary_len);
                z_nsymbols = 0;
                for ( i = 0; i < z_unary_len; i++ )
                {
                    if ( z_unary & (1 << i) )
                    {
                        zq++;
                    }
                    else
                    {
                        z_q[z_nsymbols++] = zq;
                        zq = 0;
                    }
                }
                z_pos += z_nsymbols;
            }

            if ( w_enable )
            {
                w_unary1_len = 0;
                int max_symbols = w_uncompressed && w_grc_div > 5 ? 8 : 12;
                if ( w_unary0 != 0 )
                {
                    for ( i = 0; i < max_symbols; i++ )
                    {
                        if ( w_unary0 & (1 << i) )
                        {
                            w_unary1_len++;
                        }
                    }
                }
                w_unary1 = (w_unary1_len > 0) ? bitbuf_get(bb, "WUNARY1", w_unary1_len) : 0;
                w_nsymbols = 0;

                for ( i = 0; i < max_symbols && (w_nsymbols < (w_cnt - w_pos)); i++ )
                {
                    int code = 0;
                    if ( w_unary0 & (1 << i) )
                    {
                        code = 1 + ( w_unary1 & 1 );
                        w_unary1 = w_unary1 >> 1;
                    }
                    wq += code;
                    if ( code < 2 || w_grc_trunc )
                    {
                        w_q[w_nsymbols++] = wq;
                        wq = 0;
                    }
                }
                w_pos += w_nsymbols;
            }

            // Remainders corresponding to the quotients in the previous chunk
            if ( w_prev_enable )
            {
                for ( i = 0; i < w_prev_nsymbols && w_prev_pos < w_cnt; i++, w_prev_pos++ )
                {
                    int remain = bitbuf_get(bb, "WREMAIN", w_grc_div);
                    w_value[w_prev_pos] = (w_prev_q[i] << w_grc_div) + remain;
                }
            }
            if ( z_prev_enable )
            {
                for ( i = 0; i < z_prev_nsymbols && z_prev_pos < z_cnt; i++, z_prev_pos++ )
                {
                    int remain = 0;
                    if ( z_grc_div != 0 )
                    {
                        remain = bitbuf_get(bb, "ZREMAIN", z_grc_div);
                    }
                    z_value[z_prev_pos] = (z_prev_q[i] << z_grc_div) + remain;
                    total_zcnt += z_value[z_prev_pos];
                }
            }
            w_prev_enable = w_enable;
            w_prev_nsymbols = w_nsymbols;
            std::copy(std::begin(w_q), std::end(w_q), std::begin(w_prev_q));
            z_prev_enable = z_enable;
            z_prev_nsymbols = z_nsymbols;
            std::copy(std::begin(z_q), std::end(z_q), std::begin(z_prev_q));
        } while ( w_prev_enable || z_prev_enable );

        // Interleave non-zero and zeros into the outbuf buffer
        // Increase the outbuffer to fit the new slice
        int16_t *p = outbuf.reserve(w_cnt + total_zcnt);
        assert(p);

        // Insert initial zeros
        if ( ( slice_len != ETHOSU_SLICELEN_BITS || new_palette ) && use_zero_runs )
        {
            for ( j = 0; j < z_value[0]; j++ )
            {
                *p++ = 0;
            }
        }

        // Loop over all weights and insert zeros in-between
        for ( i = 0; i < w_cnt; i++ )
        {
            int val;
            assert(w_value[i] < 512);  // HW supports 9bit
            if ( w_value[i] < palette.palsize )
            {
                val = palette.inv_lut[w_value[i]];
            }
            else
            {
                val = w_value[i] - palette.palsize + palette.direct_offset;
            }
            int sign = val & 1;
            int mag = val >> 1;
            *p++ = sign ? int16_t(-mag) : int16_t(mag);
            if ( use_zero_runs )
            {
                for ( j = 0; j < z_value[i + (new_palette ? 1 : 0)]; j++ )
                {
                    *p++ = 0;
                }
            }
        }

        outbuf.use(w_cnt + total_zcnt);
    } while (!single_slice);
labelExit:
    return outbuf.used() - start_offset;
}


constexpr int INITIAL_BLOCKS = 4;


#if defined __cplusplus
extern "C"
{
#endif

// Decode a stream
//
//   result     - Resulting data from decode (must be freeed after use)
//   buffer     - Incoming bitstream buffer
//   size_bytes - Size of the bitstream buffer (in bytes)
void ml_decode_ethosu_stream(ml_decode_result_t *result, const uint8_t *buffer, int size_bytes)
{
    assert(result && buffer && size_bytes);
    result->decoded_data = nullptr;
    result->section_sizes = nullptr;

    bitbuf_t bb(buffer, size_bytes);
    raw_buffer_t<int16_t> output(4096, MLW_ENCODE_ALLOC_STREAM0, nullptr);
    palette_t palette;
    ml_decode_internal(output, bb, palette, size_bytes, false, ETHOSU_SLICELEN_BITS);

    // Populate the results set
    result->decoded_length = output.used();
    result->decoded_data = output.detach();
}

ML_ENCODER_DLL_EXPORT void mld_free(ml_decode_result_t *result)
{
    if ( result )
    {
        if ( result->decoded_data )
        {
            result->decoded_data = static_cast<int16_t*>(realloc( result->decoded_data, 0));
        }
        if ( result->section_sizes )
        {
            free( result->section_sizes );
            result->section_sizes = nullptr;
        }
    }
}


#if defined __cplusplus
}  // extern "C"
#endif

