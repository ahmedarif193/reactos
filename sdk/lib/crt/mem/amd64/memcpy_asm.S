/* 
 * PROJECT:           ReactOS C Runtime Library
 * PURPOSE:           Optimized memcpy implementation for amd64 (GAS/Clang/GCC, COFF-safe)
 * NOTES:
 *   - Windows x64 ABI safe: uses only caller-saved GPRs (RAX, RCX, RDX, R8–R11)
 *     and XMM0–XMM3 (no XMM6–XMM15).
 *   - Destination is aligned to 16 bytes for aligned/NT stores.
 *   - Uses non-temporal stores for very large copies (>= 256 KiB).
 *   - Micro-opts: 128B unroll, aligned-load fast path, light prefetch.
 *   - Win64 SEH unwind is provided via .seh_* directives.
 *   - ERMS dispatch: uses rep movsb when supported, else SSE2/NT path.
 */

.intel_syntax noprefix

# Internal dispatch pointer (initialized once at first call)
.section .data
.p2align 3
__reactos_memcpy_impl:
    .quad 0

.text
.p2align 4, 0x90

# Public entry: memcpy wrapper with ERMS dispatch and proper SEH
.globl memcpy
.def memcpy; .scl 2; .type 32; .endef
    .seh_proc memcpy
memcpy:
    # Preserve RBX across CPUID per Win64 ABI
    push    rbx
    .seh_pushreg rbx
    .seh_endprologue
    .cfi_startproc
    # RCX=dst, RDX=src, R8=count
    mov     rax, rcx                # return value = dst

    # Fast path: if impl is set, tailcall it
    mov     r9, qword ptr [rip+__reactos_memcpy_impl]
    test    r9, r9
    jnz     .Lcall_impl

    # Detect ERMS once (CPUID EAX=7, ECX=0, check EBX bit 9)
    # Preserve RCX and RDX across CPUID (RBX preserved via stack)
    mov     r10, rcx                # save dst
    mov     r11, rdx                # save src

    mov     eax, 7
    xor     ecx, ecx
    cpuid

    lea     rax, [rip+memcpy_sse2nt_impl]
    lea     rdx, [rip+memcpy_erms_impl]
    bt      ebx, 9                  # ERMS feature
    mov     r9, rax                 # default: SSE2/NT
    jnc     .Lchoose_done
    mov     r9, rdx                 # ERMS available
.Lchoose_done:
    # Store chosen impl atomically if not already set
    xor     eax, eax                # expected = 0
    lock cmpxchg qword ptr [rip+__reactos_memcpy_impl], r9
    # Load final impl (in case of race)
    mov     r9, qword ptr [rip+__reactos_memcpy_impl]
    # Restore RCX, RDX for tailcall and fix RAX return on first-call path
    mov     rcx, r10
    mov     rdx, r11
    mov     rax, rcx
.Lcall_impl:
    pop     rbx
    jmp     r9                      # tailcall chosen implementation
    .seh_endproc
    .cfi_endproc

# SSE2 + NT implementation (body below)
    .seh_proc memcpy_sse2nt_impl
memcpy_sse2nt_impl:
    .seh_endprologue
    .cfi_startproc
    # RCX=dst, RDX=src, R8=count
    mov     rax, rcx                # return value = dst

    cmp     r8, 32
    jb      .Lsmall_copy

    # Align dst to 16
    mov     r9, rcx
    and     r9, 15
    jz      .Lafter_align

    mov     r10, 16
    sub     r10, r9                 # bytes to align [1..15]
    sub     r8,  r10                # reduce remaining count

.Lalign_loop:
    mov     r11b, [rdx]
    mov     [rcx], r11b
    inc     rdx
    inc     rcx
    dec     r10
    jnz     .Lalign_loop

.Lafter_align:
    # Decide regular vs NT path
    cmp     r8, 262144              # 256 KiB
    jae     .Lnt_large

    # -------- Regular SSE2 path (aligned dst) --------
    # Choose aligned-loads when src is 16B aligned
    mov     r10, rdx
    and     r10, 15
    jz      .Lreg_aligned_loads

    # --- Unaligned-load main loop: 128B per iteration (two 64B batches) ---
    mov     r9, r8
    shr     r9, 7                   # r9 = number of 128B blocks
    jz      .Lreg_64tail
.p2align 4, 0x90
.Lreg128_unaligned:
    prefetchnta [rdx+256]
    prefetchnta [rdx+320]

    movdqu  xmm0, [rdx]
    movdqu  xmm1, [rdx+16]
    movdqu  xmm2, [rdx+32]
    movdqu  xmm3, [rdx+48]
    movdqa  [rcx],     xmm0
    movdqa  [rcx+16],  xmm1
    movdqa  [rcx+32],  xmm2
    movdqa  [rcx+48],  xmm3

    movdqu  xmm0, [rdx+64]
    movdqu  xmm1, [rdx+80]
    movdqu  xmm2, [rdx+96]
    movdqu  xmm3, [rdx+112]
    movdqa  [rcx+64],  xmm0
    movdqa  [rcx+80],  xmm1
    movdqa  [rcx+96],  xmm2
    movdqa  [rcx+112], xmm3

    add     rdx, 128
    add     rcx, 128
    dec     r9
    jnz     .Lreg128_unaligned

.Lreg_64tail:
    mov     r9, r8
    and     r9, 127                 # <128B remainder
    shr     r9, 6                   # # of 64B blocks
    jz      .Lreg_16tail

.p2align 4, 0x90
.Lreg64_unaligned:
    movdqu  xmm0, [rdx]
    movdqu  xmm1, [rdx+16]
    movdqu  xmm2, [rdx+32]
    movdqu  xmm3, [rdx+48]
    movdqa  [rcx],     xmm0
    movdqa  [rcx+16],  xmm1
    movdqa  [rcx+32],  xmm2
    movdqa  [rcx+48],  xmm3
    add     rdx, 64
    add     rcx, 64
    dec     r9
    jnz     .Lreg64_unaligned
    jmp     .Lreg_16tail

    # --- Aligned-load fast path ---
.p2align 4, 0x90
.Lreg_aligned_loads:
    mov     r9, r8
    shr     r9, 7                   # 128B blocks
    jz      .Lreg_64tail_al

.p2align 4, 0x90
.Lreg128_aligned:
    prefetchnta [rdx+256]
    prefetchnta [rdx+320]

    movdqa  xmm0, [rdx]
    movdqa  xmm1, [rdx+16]
    movdqa  xmm2, [rdx+32]
    movdqa  xmm3, [rdx+48]
    movdqa  [rcx],     xmm0
    movdqa  [rcx+16],  xmm1
    movdqa  [rcx+32],  xmm2
    movdqa  [rcx+48],  xmm3

    movdqa  xmm0, [rdx+64]
    movdqa  xmm1, [rdx+80]
    movdqa  xmm2, [rdx+96]
    movdqa  xmm3, [rdx+112]
    movdqa  [rcx+64],  xmm0
    movdqa  [rcx+80],  xmm1
    movdqa  [rcx+96],  xmm2
    movdqa  [rcx+112], xmm3

    add     rdx, 128
    add     rcx, 128
    dec     r9
    jnz     .Lreg128_aligned

.Lreg_64tail_al:
    mov     r9, r8
    and     r9, 127
    shr     r9, 6
    jz      .Lreg_16tail

.p2align 4, 0x90
.Lreg64_aligned:
    movdqa  xmm0, [rdx]
    movdqa  xmm1, [rdx+16]
    movdqa  xmm2, [rdx+32]
    movdqa  xmm3, [rdx+48]
    movdqa  [rcx],     xmm0
    movdqa  [rcx+16],  xmm1
    movdqa  [rcx+32],  xmm2
    movdqa  [rcx+48],  xmm3
    add     rdx, 64
    add     rcx, 64
    dec     r9
    jnz     .Lreg64_aligned

    # Common tails
.Lreg_16tail:
    mov     r9, r8
    and     r9, 63                  # <64
    shr     r9, 4                   # 16B chunks
    jz      .Lreg_bytetail
.p2align 4, 0x90
.Lreg_16body:
    movdqu  xmm0, [rdx]
    movdqa  [rcx], xmm0
    add     rdx, 16
    add     rcx, 16
    dec     r9
    jnz     .Lreg_16body

.Lreg_bytetail:
    and     r8, 15
    jz      .Ldone
.p2align 4, 0x90
.Lreg_byteloop:
    mov     r9b, [rdx]
    mov     [rcx], r9b
    inc     rdx
    inc     rcx
    dec     r8
    jnz     .Lreg_byteloop
    jmp     .Ldone

    # -------- Non-temporal large copy (>= 256 KiB) --------
.p2align 4, 0x90
.Lnt_large:
    # 64B-align destination for NT stores
    mov     r10, rcx
    and     r10, 63                 # misalignment [0..63]
    jz      .Lnt_after_align64
    mov     r11, 64
    sub     r11, r10                # bytes to reach 64B alignment
    cmp     r11, r8
    cmova   r11, r8
    mov     r10, r11                # total preface

    # 16B chunks to reach 64B boundary
    mov     r9, r11
    shr     r9, 4
    jz      .Lnt_align64_tail
.p2align 4, 0x90
.Lnt_align64_16:
    movdqu  xmm0, [rdx]
    movdqa  [rcx], xmm0
    add     rdx, 16
    add     rcx, 16
    dec     r9
    jnz     .Lnt_align64_16

.Lnt_align64_tail:
    and     r11, 15
    jz      .Lnt_align64_done
.p2align 4, 0x90
.Lnt_align64_tail_loop:
    mov     r9b, [rdx]
    mov     [rcx], r9b
    inc     rdx
    inc     rcx
    dec     r11
    jnz     .Lnt_align64_tail_loop

.Lnt_align64_done:
    sub     r8, r10

.Lnt_after_align64:
    # 128B-unrolled NT main loop
    mov     r9, r8
    shr     r9, 7                   # 128B blocks
    jz      .Lnt_64tail

.p2align 4, 0x90
.Lnt128:
    prefetchnta [rdx+256]
    prefetchnta [rdx+320]

    movdqu  xmm0, [rdx]
    movdqu  xmm1, [rdx+16]
    movdqu  xmm2, [rdx+32]
    movdqu  xmm3, [rdx+48]
    movntdq [rcx],     xmm0
    movntdq [rcx+16],  xmm1
    movntdq [rcx+32],  xmm2
    movntdq [rcx+48],  xmm3

    movdqu  xmm0, [rdx+64]
    movdqu  xmm1, [rdx+80]
    movdqu  xmm2, [rdx+96]
    movdqu  xmm3, [rdx+112]
    movntdq [rcx+64],  xmm0
    movntdq [rcx+80],  xmm1
    movntdq [rcx+96],  xmm2
    movntdq [rcx+112], xmm3

    add     rdx, 128
    add     rcx, 128
    dec     r9
    jnz     .Lnt128

.Lnt_64tail:
    mov     r9, r8
    and     r9, 127
    shr     r9, 6                   # 64B blocks
    jz      .Lnt_16tail
.p2align 4, 0x90
.Lnt64:
    movdqu  xmm0, [rdx]
    movdqu  xmm1, [rdx+16]
    movdqu  xmm2, [rdx+32]
    movdqu  xmm3, [rdx+48]
    movntdq [rcx],     xmm0
    movntdq [rcx+16],  xmm1
    movntdq [rcx+32],  xmm2
    movntdq [rcx+48],  xmm3
    add     rdx, 64
    add     rcx, 64
    dec     r9
    jnz     .Lnt64

.Lnt_16tail:
    mov     r9, r8
    and     r9, 63
    shr     r9, 4                   # 16B chunks
    jz      .Lnt_bytetail
.p2align 4, 0x90
.Lnt_16body:
    movdqu  xmm0, [rdx]
    movntdq [rcx], xmm0
    add     rdx, 16
    add     rcx, 16
    dec     r9
    jnz     .Lnt_16body

.Lnt_bytetail:
    and     r8, 15
    jz      .Lnt_finish
.p2align 4, 0x90
.Lnt_byteloop:
    mov     r9b, [rdx]
    mov     [rcx], r9b
    inc     rdx
    inc     rcx
    dec     r8
    jnz     .Lnt_byteloop

.Lnt_finish:
    sfence                          # ensure NT stores reach memory
    jmp     .Ldone

    # -------- Tiny copies (< 32 bytes) --------
.p2align 4, 0x90
.Lsmall_copy:
    mov     r9, r8
    cmp     r9, 16
    jb      .Lsmall_8

    mov     r10, [rdx]
    mov     r11, [rdx+8]
    mov     [rcx],   r10
    mov     [rcx+8], r11
    add     rcx, 16
    add     rdx, 16
    sub     r9, 16

.Lsmall_8:
    cmp     r9, 8
    jb      .Lsmall_4
    mov     r10, [rdx]
    mov     [rcx], r10
    add     rcx, 8
    add     rdx, 8
    sub     r9, 8

.Lsmall_4:
    cmp     r9, 4
    jb      .Lsmall_2
    mov     r10d, [rdx]
    mov     [rcx], r10d
    add     rcx, 4
    add     rdx, 4
    sub     r9, 4

.Lsmall_2:
    cmp     r9, 2
    jb      .Lsmall_1
    mov     r10w, [rdx]
    mov     [rcx], r10w
    add     rcx, 2
    add     rdx, 2
    sub     r9, 2

.Lsmall_1:
    test    r9, r9
    jz      .Ldone
    mov     r10b, [rdx]
    mov     [rcx], r10b

.Ldone:
    ret
    .seh_endproc
    .cfi_endproc

# ERMS implementation: rep movsb (Win64 ABI-safe with saved RSI/RDI)
    .seh_proc memcpy_erms_impl
memcpy_erms_impl:
    # Prologue: save non-volatiles used (RSI, RDI)
    push    rdi
    .seh_pushreg rdi
    push    rsi
    .seh_pushreg rsi
    .seh_endprologue
    .cfi_startproc

    # rax already holds return value (dst)
    mov     rdi, rcx                # RDI = dst
    mov     rsi, rdx                # RSI = src
    mov     rcx, r8                 # RCX = count
    test    rcx, rcx
    jz      .Lerms_done
    cld
    rep movsb

.Lerms_done:
    # Epilogue: restore RSI/RDI
    pop     rsi
    pop     rdi
    ret
    .seh_endproc
    .cfi_endproc
