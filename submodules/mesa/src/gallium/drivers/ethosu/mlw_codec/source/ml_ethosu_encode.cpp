//
// SPDX-FileCopyrightText: Copyright 2024 Arm Limited and/or its affiliates <open-source-office@arm.com>
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

#include "ml_bit_buffer.hpp"
#include "ml_raw_buffer.hpp"
#include "ml_encoder_internal.hpp"

#include <cassert>
#include <cstdint>

#if defined __cplusplus
extern "C"
{
#endif


ML_ENCODER_DLL_EXPORT int32_t ml_encode_ethosu_stream(ml_encode_result_t *result, const ml_ethosu_encode_params_t *ep, ml_weight_source_fn src, void *user_arg, mle_context_t **ctx_out)
{
    constexpr int BUFFERING_REQUEST_SIZE  = 8192;    // Initial input buffering
    constexpr int INITIAL_OUTPUT_BUFFER   = 8192;    // Initial size of output buffer (doubles at every overflow)
    constexpr unsigned VALID_FLAGS = MLW_ENCODE_NO_BITSTREAM;

    assert(result && ep);
    if ( !(result && ep && src) )
    {
        return -1;
    }

    mle_context_t *ctx = mle_create_context(MLW_ENCODE_SYNTAX_ETHOSU);
    // Allow forcing parameters for debug validation - it is expected that
    // the caller knows what they're doing here since it accesses the opaque
    // internals via the public interface.

    assert( !(ep->encoder_flags & ~(VALID_FLAGS)) );  // Check acceptable flags
    unsigned ethosu_encode_flags = (ep->encoder_flags & VALID_FLAGS);

    // Input buffering of data from the source function
    assert( ep->source_buffering_hint >= 0 );
    int request_size = std::max(BUFFERING_REQUEST_SIZE, ep->source_buffering_hint & 0x00FFFFFF);
    raw_buffer_t<int16_t> buffer( request_size );

    // The source function will communicate the state to this encoding loop
    ml_source_state_t state = {0};
    state.eos = false;

    // Output bitstream allocation
    raw_buffer_t<uint8_t> output(INITIAL_OUTPUT_BUFFER, MLW_ENCODE_ALLOC_STREAM0, ep->realloc_func);
    bitbuf_t bits(output, 8, ethosu_encode_flags & MLW_ENCODE_NO_BITSTREAM);

    result->source_length = 0;

    // Repeatedly ask for values until the source function signals end-of-stream.
    while ( !state.eos )
    {
        int16_t *buf_write = buffer.reserve(request_size);
        if ( !buf_write )
        {
            // Memory allocation failed
            mle_destroy_context(ctx);
            return -1;
        }
        int received = (*src)(MLW_SOURCE_QUERY_WEIGHTS, &state, buf_write, buffer.capacity() - buffer.used(), user_arg);
        buffer.use(received);

        unsigned encode_flags = ethosu_encode_flags;
        encode_flags |= MLW_ENCODE_NEW_PALETTE;

        int bytes_written = ml_encode_internal(ctx, bits, buffer.begin(), buffer.used(), buffer.used(), encode_flags);
        if ( bytes_written < 0 )
        {
            // Encoder errored
            mle_destroy_context(ctx);
            return -1;
        }
        result->source_length += buffer.used();
        buffer.clear();
    }

    ml_encode_eos(ctx, bits, ethosu_encode_flags);

    // Populate the return result
    assert(bits.byte_pos() == output.used() || (ethosu_encode_flags & MLW_ENCODE_NO_BITSTREAM));
    result->encoded_length = bits.byte_pos();
    result->encoded_data = output.detach();
    result->section_info = nullptr;
    result->section_count = 0;

    if (ctx_out != nullptr)
    {
        assert( *ctx_out == nullptr );
        *ctx_out = ctx;
    }
    else
    {
        mle_destroy_context(ctx);
    }
    return 1;
}


#if defined __cplusplus
}  // extern "C"
#endif
