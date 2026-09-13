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

#ifndef __MLW_DECODE_H__
#define __MLW_DECODE_H__

#pragma once

#include <stdint.h>

// Result of the decode process
typedef struct ml_decode_result_t
{
    int16_t *decoded_data;              // decoded weight elements
    int32_t  decoded_length;            // decoded weight length (in elements)
    int32_t  section_count;             // number of sections in stream
    int32_t *section_sizes;             // section sizes in stream
} ml_decode_result_t;


#if defined __cplusplus
extern "C"
{
#endif

    void ml_decode_ethosu_stream(ml_decode_result_t *result, const uint8_t *buffer, int size_bytes);

    void mld_free(ml_decode_result_t *result);

#if defined __cplusplus
}  // extern "C"
#endif


#endif
