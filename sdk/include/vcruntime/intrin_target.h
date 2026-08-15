/*
 * PROJECT:     ReactOS vcruntime library
 * PURPOSE:     Select compiler intrinsic declarations by code-generation target
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#if defined(__clang__) && !defined(_MSC_VER)
#define _VCRT_CLANG_INTRINSICS 1
#else
#define _VCRT_CLANG_INTRINSICS 0
#endif

#if defined(_M_ARM64) || defined(_M_ARM64EC) || defined(__aarch64__) || defined(__arm64ec__)
#define _VCRT_ARM64_CODEGEN 1
#else
#define _VCRT_ARM64_CODEGEN 0
#endif

#define _VCRT_ARM64_INTRINSICS _VCRT_ARM64_CODEGEN

#if defined(_M_IX86)
#define _VCRT_I386_INTRINSICS 1
#else
#define _VCRT_I386_INTRINSICS 0
#endif

#if defined(_M_AMD64) && !_VCRT_ARM64_CODEGEN
#define _VCRT_AMD64_CODEGEN 1
#else
#define _VCRT_AMD64_CODEGEN 0
#endif

#define _VCRT_AMD64_INTRINSICS _VCRT_AMD64_CODEGEN

#if _VCRT_I386_INTRINSICS || _VCRT_AMD64_INTRINSICS
#define _VCRT_X86_INTRINSICS 1
#else
#define _VCRT_X86_INTRINSICS 0
#endif

#if defined(_M_ARM) || _VCRT_ARM64_CODEGEN || _VCRT_AMD64_INTRINSICS
#define _VCRT_64BIT_INTERLOCKED_INTRINSICS 1
#else
#define _VCRT_64BIT_INTERLOCKED_INTRINSICS 0
#endif

#if defined(__x86_64__) && !defined(__arm64ec__)
#define _VCRT_GNU_AMD64_CODEGEN 1
#else
#define _VCRT_GNU_AMD64_CODEGEN 0
#endif

#if defined(__i386__) || _VCRT_GNU_AMD64_CODEGEN
#define _VCRT_GNU_X86_CODEGEN 1
#else
#define _VCRT_GNU_X86_CODEGEN 0
#endif

#if _VCRT_CLANG_INTRINSICS && _VCRT_GNU_X86_CODEGEN
#define _VCRT_USE_CLANG_X86_INTRINSICS 1
#else
#define _VCRT_USE_CLANG_X86_INTRINSICS 0
#endif
