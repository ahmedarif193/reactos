/*
 * PROJECT:           ReactOS C Runtime Library
 * LICENSE:           BSD - See COPYING.ARM in the top level directory
 * FILE:              sdk/lib/crt/mem/arm64/memset_asm.S
 * PURPOSE:           Optimized memset implementation for ARM64
 * NOTES:
 *   - Bootloader-safe: no NEON/FP, uses only integer registers
 *   - ARM64 AAPCS ABI: x0 = dst, w1 = value, x2 = count
 *   - Aligns to 8 bytes, then writes 8-byte chunks + 4/2/1 tails
 *   - Returns original dst pointer in x0
 */

/* DWARF debug info directives - only for ELF/Mach-O, not COFF (MinGW) */
#ifndef _WIN32
.file "sdk/lib/crt/mem/arm64/memset_asm.S"
#endif

.text
.align 4

.globl memset

memset:
#ifndef _WIN32
.loc 1 22
#endif
    // x0 = dst (preserve for return)
    // w1 = value (byte to fill)
    // x2 = count

    mov     x3, x0              // Save dst for return value
    cbz     x2, .Ldone          // if (count == 0) return

    // Replicate byte across 64 bits
    and     w1, w1, #0xFF       // Mask to byte
    orr     x1, x1, x1, lsl #8  // Replicate to 16 bits
    orr     x1, x1, x1, lsl #16 // Replicate to 32 bits
    orr     x1, x1, x1, lsl #32 // Replicate to 64 bits
    // Now x1 contains the byte pattern replicated 8 times

    // If count < 16, use small path
    cmp     x2, #16
    b.lo    .Lsmall

    // -------------------------
    // Align destination to 8 bytes
    // -------------------------
    ands    x4, x0, #7          // x4 = misalignment [0..7]
    b.eq    .Lqwords             // Already aligned

    mov     x5, #8
    sub     x5, x5, x4          // x5 = bytes to align [1..7]
    cmp     x5, x2
    csel    x5, x5, x2, ls      // x5 = min(x5, count)
    sub     x2, x2, x5          // Reduce remaining count

.Lalign_loop:
    strb    w1, [x0], #1        // *dst++ = byte
    subs    x5, x5, #1
    b.ne    .Lalign_loop

    cbz     x2, .Ldone          // If nothing left, done

    // -------------------------
    // 8-byte bulk stores
    // -------------------------
.Lqwords:
    lsr     x4, x2, #3          // x4 = count / 8 (number of qwords)
    cbz     x4, .Ltail          // If < 8 bytes left, handle tail

.Lqword_loop:
    str     x1, [x0], #8        // *dst++ = qword
    subs    x4, x4, #1
    b.ne    .Lqword_loop

    and     x2, x2, #7          // remaining = count % 8

    // -------------------------
    // Handle tail: 4/2/1 bytes
    // -------------------------
.Ltail:
    tbz     x2, #2, .Ltail_2    // Test bit 2 (if 4 bytes remain)
    str     w1, [x0], #4        // *dst++ = dword

.Ltail_2:
    tbz     x2, #1, .Ltail_1    // Test bit 1 (if 2 bytes remain)
    strh    w1, [x0], #2        // *dst++ = word

.Ltail_1:
    tbz     x2, #0, .Ldone      // Test bit 0 (if 1 byte remains)
    strb    w1, [x0]            // *dst = byte
    b       .Ldone

    // -------------------------
    // Small sizes (< 16 bytes)
    // -------------------------
.Lsmall:
    tbz     x2, #3, .Lsm_4      // Test bit 3 (if 8 bytes)
    str     x1, [x0], #8

.Lsm_4:
    tbz     x2, #2, .Lsm_2      // Test bit 2 (if 4 bytes)
    str     w1, [x0], #4

.Lsm_2:
    tbz     x2, #1, .Lsm_1      // Test bit 1 (if 2 bytes)
    strh    w1, [x0], #2

.Lsm_1:
    tbz     x2, #0, .Ldone      // Test bit 0 (if 1 byte)
    strb    w1, [x0]

.Ldone:
    mov     x0, x3              // Restore original dst for return
    ret
