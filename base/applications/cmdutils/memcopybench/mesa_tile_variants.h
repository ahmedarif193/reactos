/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 * Optional before/after measurement of the actual external Mesa tile helper.
 */
#if defined(HAVE_ARM64_COPY_VARIANTS) && defined(MEMCOPYBENCH_MESA_BEFORE) && defined(MEMCOPYBENCH_MESA_AFTER)
#include <stdint.h>
#define DETECT_ARCH_AARCH64 1
#define DETECT_ARCH_ARM 0
#define v3d_load_utile MemBenchLoadBefore
#define v3d_store_utile MemBenchStoreBefore
#include MEMCOPYBENCH_MESA_BEFORE
#undef v3d_load_utile
#undef v3d_store_utile
#define v3d_load_utile MemBenchLoadAfter
#define v3d_store_utile MemBenchStoreAfter
#include MEMCOPYBENCH_MESA_AFTER
#undef v3d_load_utile
#undef v3d_store_utile
#define HAVE_MESA_TILE_VARIANTS 1
#define MESA_TILE_COPY(Name, Load, Width) \
static __attribute__((noinline)) void * __cdecl \
Name(void *Destination, const void *Source, size_t Bytes) \
{ \
    void *Result = Destination; \
    while (Bytes >= 64) \
    { \
        Load(Destination, Width, (void *)Source, Width); \
        Destination = (unsigned char *)Destination + 64; \
        Source = (const unsigned char *)Source + 64; \
        Bytes -= 64; \
    } \
    CopyGpr64(Destination, Source, Bytes); \
    return Result; \
}
MESA_TILE_COPY(CopyMesaBefore8, MemBenchLoadBefore, 8)
MESA_TILE_COPY(CopyMesaAfter8, MemBenchLoadAfter, 8)
MESA_TILE_COPY(CopyMesaBefore16, MemBenchLoadBefore, 16)
MESA_TILE_COPY(CopyMesaAfter16, MemBenchLoadAfter, 16)
#undef MESA_TILE_COPY
#endif
