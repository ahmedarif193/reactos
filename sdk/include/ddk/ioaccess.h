/*
 * ioaccess.h
 *
 * Windows Device Driver Kit
 *
 * This file is part of the w32api package.
 *
 * THIS SOFTWARE IS NOT COPYRIGHTED
 *
 * This source code is offered for use in the public domain. You may
 * use, modify or distribute it freely.
 *
 * This code is distributed in the hope that it will be useful but
 * WITHOUT ANY WARRANTY. ALL WARRANTIES, EXPRESS OR IMPLIED ARE HEREBY
 * DISCLAIMED. This includes but is not limited to warranties of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */
#ifndef __IOACCESS_H
#define __IOACCESS_H

#ifdef __cplusplus
extern "C" {
#endif

#define H2I(p) PtrToUshort(p)

#ifndef NO_PORT_MACROS

#if defined(_X86_) || defined(_M_AMD64)
#define READ_REGISTER_UCHAR(r) (*(volatile UCHAR *)(r))
#define READ_REGISTER_USHORT(r) (*(volatile USHORT *)(r))
#define READ_REGISTER_ULONG(r) (*(volatile ULONG *)(r))
#define WRITE_REGISTER_UCHAR(r, v) (*(volatile UCHAR *)(r) = (v))
#define WRITE_REGISTER_USHORT(r, v) (*(volatile USHORT *)(r) = (v))
#define WRITE_REGISTER_ULONG(r, v) (*(volatile ULONG *)(r) = (v))
#define READ_PORT_UCHAR(p) (UCHAR)(__inbyte (H2I(p)))
#define READ_PORT_USHORT(p) (USHORT)(__inword (H2I(p)))
#define READ_PORT_ULONG(p) (ULONG)(__indword (H2I(p)))
#define WRITE_PORT_UCHAR(p, v) __outbyte (H2I(p), (v))
#define WRITE_PORT_USHORT(p, v) __outword (H2I(p), (v))
#define WRITE_PORT_ULONG(p, v) __outdword (H2I(p), (v))

#define MEMORY_BARRIER()

#elif defined(_PPC_) || defined(_MIPS_) || defined(_ARM_)

#define READ_REGISTER_UCHAR(r)      (*(volatile UCHAR * const)(r))
#define READ_REGISTER_USHORT(r)     (*(volatile USHORT * const)(r))
#define READ_REGISTER_ULONG(r)      (*(volatile ULONG * const)(r))
#define WRITE_REGISTER_UCHAR(r, v)  (*(volatile UCHAR * const)(r) = (v))
#define WRITE_REGISTER_USHORT(r, v) (*(volatile USHORT * const)(r) = (v))
#define WRITE_REGISTER_ULONG(r, v)  (*(volatile ULONG * const)(r) = (v))
#define READ_PORT_UCHAR(r)          READ_REGISTER_UCHAR(r)
#define READ_PORT_USHORT(r)         READ_REGISTER_USHORT(r)
#define READ_PORT_ULONG(r)          READ_REGISTER_ULONG(r)
#define WRITE_PORT_UCHAR(p, v)      WRITE_REGISTER_UCHAR(p, (UCHAR) (v))
#define WRITE_PORT_USHORT(p, v)     WRITE_REGISTER_USHORT(p, (USHORT) (v))
#define WRITE_PORT_ULONG(p, v)      WRITE_REGISTER_ULONG(p, (ULONG) (v))

#elif defined(_ARM64_) || defined(_M_ARM64) || defined(__aarch64__) || defined(__arm64__)

/*
 * ARM64 Architecture MMIO Access
 *
 * ARM64 uses a weakly-ordered memory model where the CPU can reorder loads and stores.
 * For MMIO registers, READ/WRITE_REGISTER_* functions are implemented in the kernel
 * (ntoskrnl/arch/arm64/ex/ioport.c) with proper memory barriers to enforce ordering.
 *
 * Full function declarations are in xdk/iofuncs.h. For code that doesn't include the
 * full DDK headers (like cportlib), we provide minimal forward declarations here.
 *
 * MEMORY_BARRIER() is defined as a full system data memory barrier (DMB SY) which
 * ensures all prior memory operations complete before subsequent operations proceed.
 */

/*
 * ARM64 MMIO accessor macros with memory barriers
 *
 * ARM64 uses a weakly-ordered memory model where the CPU can reorder loads and stores.
 * For MMIO (device memory), we use DSB barriers which are appropriate for device access:
 * - DSB LD: Data Synchronization Barrier for loads (reads)
 * - DSB ST: Data Synchronization Barrier for stores (writes)
 *
 * The barrier placement:
 * - READ: Barrier AFTER the read to ensure the read completes before subsequent operations
 * - WRITE: Barrier AFTER the write to ensure the write completes before subsequent operations
 *
 * Note: DMB is for Normal memory ordering, DSB is for Device memory (MMIO) synchronization.
 * We use DSB here because MMIO registers are mapped as Device memory on ARM64.
 */

/* Helper macros for ARM64 barriers */
#define __ARM64_DMB_SY() __asm__ __volatile__("dmb sy" ::: "memory")
#define __ARM64_DSB_LD() __asm__ __volatile__("dsb ld" ::: "memory")
#define __ARM64_DSB_ST() __asm__ __volatile__("dsb st" ::: "memory")

/* Single register reads - DSB LD barrier after to ensure read completion */
#define READ_REGISTER_UCHAR(r) \
    ({ UCHAR __v = *(volatile UCHAR const *)(r); __ARM64_DSB_LD(); __v; })

#define READ_REGISTER_USHORT(r) \
    ({ USHORT __v = *(volatile USHORT const *)(r); __ARM64_DSB_LD(); __v; })

#define READ_REGISTER_ULONG(r) \
    ({ ULONG __v = *(volatile ULONG const *)(r); __ARM64_DSB_LD(); __v; })

/* Single register writes - DSB ST barrier after to ensure write completion */
#define WRITE_REGISTER_UCHAR(r, v) \
    do { *(volatile UCHAR *)(r) = (v); __ARM64_DSB_ST(); } while (0)

#define WRITE_REGISTER_USHORT(r, v) \
    do { *(volatile USHORT *)(r) = (v); __ARM64_DSB_ST(); } while (0)

#define WRITE_REGISTER_ULONG(r, v) \
    do { *(volatile ULONG *)(r) = (v); __ARM64_DSB_ST(); } while (0)

/* Buffer reads - single DSB LD barrier after all reads for efficiency */
#define READ_REGISTER_BUFFER_UCHAR(r, b, c) \
    do { \
        volatile const UCHAR *__src = (volatile const UCHAR *)(r); \
        PUCHAR __dst = (b); \
        ULONG __cnt = (c); \
        while (__cnt--) *__dst++ = *__src; \
        __ARM64_DSB_LD(); \
    } while (0)

#define READ_REGISTER_BUFFER_USHORT(r, b, c) \
    do { \
        volatile const USHORT *__src = (volatile const USHORT *)(r); \
        PUSHORT __dst = (b); \
        ULONG __cnt = (c); \
        while (__cnt--) *__dst++ = *__src; \
        __ARM64_DSB_LD(); \
    } while (0)

#define READ_REGISTER_BUFFER_ULONG(r, b, c) \
    do { \
        volatile const ULONG *__src = (volatile const ULONG *)(r); \
        PULONG __dst = (b); \
        ULONG __cnt = (c); \
        while (__cnt--) *__dst++ = *__src; \
        __ARM64_DSB_LD(); \
    } while (0)

/* Buffer writes - single DSB ST barrier after all writes */
#define WRITE_REGISTER_BUFFER_UCHAR(r, b, c) \
    do { \
        volatile UCHAR *__dst = (volatile UCHAR *)(r); \
        PUCHAR __src = (b); \
        ULONG __cnt = (c); \
        while (__cnt--) *__dst = *__src++; \
        __ARM64_DSB_ST(); \
    } while (0)

#define WRITE_REGISTER_BUFFER_USHORT(r, b, c) \
    do { \
        volatile USHORT *__dst = (volatile USHORT *)(r); \
        PUSHORT __src = (b); \
        ULONG __cnt = (c); \
        while (__cnt--) *__dst = *__src++; \
        __ARM64_DSB_ST(); \
    } while (0)

#define WRITE_REGISTER_BUFFER_ULONG(r, b, c) \
    do { \
        volatile ULONG *__dst = (volatile ULONG *)(r); \
        PULONG __src = (b); \
        ULONG __cnt = (c); \
        while (__cnt--) *__dst = *__src++; \
        __ARM64_DSB_ST(); \
    } while (0)

/* Port I/O macros - on ARM64, port I/O is mapped to register I/O */
#define READ_PORT_UCHAR(r)          READ_REGISTER_UCHAR((PUCHAR)(r))
#define READ_PORT_USHORT(r)         READ_REGISTER_USHORT((PUSHORT)(r))
#define READ_PORT_ULONG(r)          READ_REGISTER_ULONG((PULONG)(r))
#define WRITE_PORT_UCHAR(p, v)      WRITE_REGISTER_UCHAR((PUCHAR)(p), (UCHAR)(v))
#define WRITE_PORT_USHORT(p, v)     WRITE_REGISTER_USHORT((PUSHORT)(p), (USHORT)(v))
#define WRITE_PORT_ULONG(p, v)      WRITE_REGISTER_ULONG((PULONG)(p), (ULONG)(v))

/*
 * MEMORY_BARRIER for ARM64: Full system data memory barrier
 *
 * Uses DMB SY (Data Memory Barrier, full SYstem) which:
 * - Ensures all memory accesses before the barrier are observed before those after
 * - Applies to all memory types (normal and device memory)
 * - Visible across all cores in the system
 *
 * This matches the Windows kernel's approach for driver-level memory barriers.
 */
#if defined(__GNUC__) || defined(__clang__)
#define MEMORY_BARRIER() __asm__ __volatile__("dmb sy" ::: "memory")
#else
/* For other compilers, use intrinsic if available */
#define MEMORY_BARRIER() __dmb(0xF)  /* 0xF = SY (full system) */
#endif

#else

#error Unsupported architecture

#endif

#endif /* NO_PORT_MACROS */

#ifdef __cplusplus
}
#endif

#endif /* __IOACCESS_H */
