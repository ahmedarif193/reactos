/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Intrinsics for the SSE3 instruction set
 * COPYRIGHT:   Copyright 2024 Timo Kreuzer (timo.kreuzer@reactos.org)
 */

#pragma once

#define _INCLUDED_PMM

#include "intrin_target.h"

/* When building with Clang, use Clang's own intrinsics headers instead. */
#if _VCRT_USE_CLANG_X86_INTRINSICS
#include_next <pmmintrin.h>
#elif _VCRT_ARM64_CODEGEN
/* ARM64: no x86 intrinsics available */
#else

#include <emmintrin.h>

#define _MM_DENORMALS_ZERO_MASK 0x0040
#define _MM_DENORMALS_ZERO_ON   0x0040
#define _MM_DENORMALS_ZERO_OFF  0x0000

#endif /* !(__clang__ && !_MSC_VER) */
