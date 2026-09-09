/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 * Userspace comparison routines for disjoint normal RAM only. No replacement
 * for the MMU-off-safe system memcpy, and no overlapping-copy contract.
 */
#if defined(__aarch64__) && (defined(__clang__) || defined(__GNUC__))
#define HAVE_ARM64_COPY_VARIANTS 1
static __attribute__((noinline)) void * __cdecl
CopyGpr64(void *Destination, const void *Source, size_t Bytes)
{
    void *Result = Destination;
    size_t Blocks = Bytes / 64;
    if (Blocks)
        __asm__ __volatile__(
            "1:\n"
            "ldp x9, x10, [%[s]]\n"
            "ldp x11, x12, [%[s], #16]\n"
            "ldp x13, x14, [%[s], #32]\n"
            "ldp x15, x16, [%[s], #48]\n"
            "stp x9, x10, [%[d]]\n"
            "stp x11, x12, [%[d], #16]\n"
            "stp x13, x14, [%[d], #32]\n"
            "stp x15, x16, [%[d], #48]\n"
            "add %[s], %[s], #64\n"
            "add %[d], %[d], #64\n"
            "subs %[n], %[n], #1\n"
            "b.ne 1b\n"
            : [d] "+r"(Destination), [s] "+r"(Source), [n] "+r"(Blocks)
            : : "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "cc", "memory");
    for (Bytes %= 64; Bytes; --Bytes)
    {
        *(volatile unsigned char *)Destination = *(const volatile unsigned char *)Source;
        Destination = (unsigned char *)Destination + 1;
        Source = (const unsigned char *)Source + 1;
    }
    return Result;
}

static __attribute__((noinline)) void * __cdecl
CopyNeon64(void *Destination, const void *Source, size_t Bytes)
{
    void *Result = Destination;
    size_t Blocks = Bytes / 64;
    if (Blocks)
        __asm__ __volatile__(
            "1:\n"
            "ldp q0, q1, [%[s]]\n"
            "ldp q2, q3, [%[s], #32]\n"
            "stp q0, q1, [%[d]]\n"
            "stp q2, q3, [%[d], #32]\n"
            "add %[s], %[s], #64\n"
            "add %[d], %[d], #64\n"
            "subs %[n], %[n], #1\n"
            "b.ne 1b\n"
            : [d] "+r"(Destination), [s] "+r"(Source), [n] "+r"(Blocks)
            : : "v0", "v1", "v2", "v3", "cc", "memory");
    for (Bytes %= 64; Bytes; --Bytes)
    {
        *(volatile unsigned char *)Destination = *(const volatile unsigned char *)Source;
        Destination = (unsigned char *)Destination + 1;
        Source = (const unsigned char *)Source + 1;
    }
    return Result;
}

/* Compare scheduling while keeping the same baseline ARMv8-A operations.
 * Every load is inside the requested span, including the pipeline epilogue. */
static __attribute__((noinline)) void * __cdecl
CopyNeonPipelined(void *Destination, const void *Source, size_t Bytes)
{
    void *Result = Destination;
    size_t Blocks = Bytes / 64;
    if (Blocks)
        __asm__ __volatile__(
            "ldp q0, q1, [%[s]]\n"
            "ldp q2, q3, [%[s], #32]\n"
            "subs %[n], %[n], #1\n"
            "b.eq 2f\n"
            "1:\n"
            "stp q0, q1, [%[d]]\n"
            "ldp q0, q1, [%[s], #64]\n"
            "stp q2, q3, [%[d], #32]\n"
            "ldp q2, q3, [%[s], #96]\n"
            "add %[s], %[s], #64\n"
            "add %[d], %[d], #64\n"
            "subs %[n], %[n], #1\n"
            "b.ne 1b\n"
            "2:\n"
            "stp q0, q1, [%[d]]\n"
            "stp q2, q3, [%[d], #32]\n"
            "add %[s], %[s], #64\n"
            "add %[d], %[d], #64\n"
            : [d] "+r"(Destination), [s] "+r"(Source), [n] "+r"(Blocks)
            : : "v0", "v1", "v2", "v3", "cc", "memory");
    for (Bytes %= 64; Bytes; --Bytes)
    {
        *(volatile unsigned char *)Destination = *(const volatile unsigned char *)Source;
        Destination = (unsigned char *)Destination + 1;
        Source = (const unsigned char *)Source + 1;
    }
    return Result;
}

static __attribute__((noinline)) void * __cdecl
CopyNeonInterleaved(void *Destination, const void *Source, size_t Bytes)
{
    void *Result = Destination;
    size_t Blocks = Bytes / 64;
    if (Blocks)
        __asm__ __volatile__(
            "1:\n"
            "ldp q0, q1, [%[s]]\n"
            "stp q0, q1, [%[d]]\n"
            "ldp q2, q3, [%[s], #32]\n"
            "stp q2, q3, [%[d], #32]\n"
            "add %[s], %[s], #64\n"
            "add %[d], %[d], #64\n"
            "subs %[n], %[n], #1\n"
            "b.ne 1b\n"
            : [d] "+r"(Destination), [s] "+r"(Source), [n] "+r"(Blocks)
            : : "v0", "v1", "v2", "v3", "cc", "memory");
    for (Bytes %= 64; Bytes; --Bytes)
    {
        *(volatile unsigned char *)Destination = *(const volatile unsigned char *)Source;
        Destination = (unsigned char *)Destination + 1;
        Source = (const unsigned char *)Source + 1;
    }
    return Result;
}

/* Wider multi-register loads: compare transaction shape on normal NC/WC RAM. */
static __attribute__((noinline)) void * __cdecl
CopyNeonLd1_64(void *Destination, const void *Source, size_t Bytes)
{
    void *Result = Destination;
    size_t Blocks = Bytes / 64;
    if (Blocks)
        __asm__ __volatile__(
            "1:\n"
            "ld1 {v0.16b, v1.16b, v2.16b, v3.16b}, [%[s]], #64\n"
            "st1 {v0.16b, v1.16b, v2.16b, v3.16b}, [%[d]], #64\n"
            "subs %[n], %[n], #1\n"
            "b.ne 1b\n"
            : [d] "+r"(Destination), [s] "+r"(Source), [n] "+r"(Blocks)
            : : "v0", "v1", "v2", "v3", "cc", "memory");
    for (Bytes %= 64; Bytes; --Bytes)
    {
        *(volatile unsigned char *)Destination = *(const volatile unsigned char *)Source;
        Destination = (unsigned char *)Destination + 1;
        Source = (const unsigned char *)Source + 1;
    }
    return Result;
}

/* Wider multi-register loads: compare transaction shape on normal NC/WC RAM. */
static __attribute__((noinline)) void * __cdecl
CopyNeonLd1_2d(void *Destination, const void *Source, size_t Bytes)
{
    void *Result = Destination;
    size_t Blocks = Bytes / 64;
    if (Blocks)
        __asm__ __volatile__(
            "1:\n"
            "ld1 {v0.2d, v1.2d, v2.2d, v3.2d}, [%[s]], #64\n"
            "st1 {v0.2d, v1.2d, v2.2d, v3.2d}, [%[d]], #64\n"
            "subs %[n], %[n], #1\n"
            "b.ne 1b\n"
            : [d] "+r"(Destination), [s] "+r"(Source), [n] "+r"(Blocks)
            : : "v0", "v1", "v2", "v3", "cc", "memory");
    for (Bytes %= 64; Bytes; --Bytes)
    {
        *(volatile unsigned char *)Destination = *(const volatile unsigned char *)Source;
        Destination = (unsigned char *)Destination + 1;
        Source = (const unsigned char *)Source + 1;
    }
    return Result;
}

static __attribute__((noinline)) void * __cdecl
CopyNeonPrefetch256(void *Destination, const void *Source, size_t Bytes)
{
    void *Result = Destination;
    size_t Blocks = Bytes / 64;
    if (Blocks)
        __asm__ __volatile__(
            "1:\n"
            "cmp %[n], #5\n"
            "b.lo 2f\n"
            "prfm pldl1keep, [%[s], #256]\n"
            "2:\n"
            "ldp q0, q1, [%[s]]\n"
            "ldp q2, q3, [%[s], #32]\n"
            "stp q0, q1, [%[d]]\n"
            "stp q2, q3, [%[d], #32]\n"
            "add %[s], %[s], #64\n"
            "add %[d], %[d], #64\n"
            "subs %[n], %[n], #1\n"
            "b.ne 1b\n"
            : [d] "+r"(Destination), [s] "+r"(Source), [n] "+r"(Blocks)
            : : "v0", "v1", "v2", "v3", "cc", "memory");
    for (Bytes %= 64; Bytes; --Bytes)
    {
        *(volatile unsigned char *)Destination = *(const volatile unsigned char *)Source;
        Destination = (unsigned char *)Destination + 1;
        Source = (const unsigned char *)Source + 1;
    }
    return Result;
}

static __attribute__((noinline)) void * __cdecl
CopyNeonPrefetch512(void *Destination, const void *Source, size_t Bytes)
{
    void *Result = Destination;
    size_t Blocks = Bytes / 64;
    if (Blocks)
        __asm__ __volatile__(
            "1:\n"
            "cmp %[n], #9\n"
            "b.lo 2f\n"
            "prfm pldl1keep, [%[s], #512]\n"
            "2:\n"
            "ldp q0, q1, [%[s]]\n"
            "ldp q2, q3, [%[s], #32]\n"
            "stp q0, q1, [%[d]]\n"
            "stp q2, q3, [%[d], #32]\n"
            "add %[s], %[s], #64\n"
            "add %[d], %[d], #64\n"
            "subs %[n], %[n], #1\n"
            "b.ne 1b\n"
            : [d] "+r"(Destination), [s] "+r"(Source), [n] "+r"(Blocks)
            : : "v0", "v1", "v2", "v3", "cc", "memory");
    for (Bytes %= 64; Bytes; --Bytes)
    {
        *(volatile unsigned char *)Destination = *(const volatile unsigned char *)Source;
        Destination = (unsigned char *)Destination + 1;
        Source = (const unsigned char *)Source + 1;
    }
    return Result;
}

static __attribute__((noinline)) void * __cdecl
CopyNeonNonTemporal(void *Destination, const void *Source, size_t Bytes)
{
    void *Result = Destination;
    size_t Blocks = Bytes / 64;
    if (Blocks)
        __asm__ __volatile__(
            "1:\n"
            "ldp q0, q1, [%[s]]\n"
            "ldp q2, q3, [%[s], #32]\n"
            "stnp q0, q1, [%[d]]\n"
            "stnp q2, q3, [%[d], #32]\n"
            "add %[s], %[s], #64\n"
            "add %[d], %[d], #64\n"
            "subs %[n], %[n], #1\n"
            "b.ne 1b\n"
            : [d] "+r"(Destination), [s] "+r"(Source), [n] "+r"(Blocks)
            : : "v0", "v1", "v2", "v3", "cc", "memory");
    for (Bytes %= 64; Bytes; --Bytes)
    {
        *(volatile unsigned char *)Destination = *(const volatile unsigned char *)Source;
        Destination = (unsigned char *)Destination + 1;
        Source = (const unsigned char *)Source + 1;
    }
    return Result;
}

static __attribute__((noinline)) void * __cdecl
CopyNeonPrefetch256Nt(void *Destination, const void *Source, size_t Bytes)
{
    void *Result = Destination;
    size_t Blocks = Bytes / 64;
    if (Blocks)
        __asm__ __volatile__(
            "1:\n"
            "cmp %[n], #5\n"
            "b.lo 2f\n"
            "prfm pldl1keep, [%[s], #256]\n"
            "2:\n"
            "ldp q0, q1, [%[s]]\n"
            "ldp q2, q3, [%[s], #32]\n"
            "stnp q0, q1, [%[d]]\n"
            "stnp q2, q3, [%[d], #32]\n"
            "add %[s], %[s], #64\n"
            "add %[d], %[d], #64\n"
            "subs %[n], %[n], #1\n"
            "b.ne 1b\n"
            : [d] "+r"(Destination), [s] "+r"(Source), [n] "+r"(Blocks)
            : : "v0", "v1", "v2", "v3", "cc", "memory");
    for (Bytes %= 64; Bytes; --Bytes)
    {
        *(volatile unsigned char *)Destination = *(const volatile unsigned char *)Source;
        Destination = (unsigned char *)Destination + 1;
        Source = (const unsigned char *)Source + 1;
    }
    return Result;
}

#endif
