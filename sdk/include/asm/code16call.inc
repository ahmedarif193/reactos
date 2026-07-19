/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Clang call/ret compatibility for pure .code16 sources
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/*
 * Opt-in include for real-mode sources that are 16-bit from top to bottom.
 * Include it after <asm.inc>.
 *
 * Under LLVM's integrated assembler, direct call/ret pick the wide
 * 66-prefixed encodings, blowing past the .org size checks of boot-sector
 * blobs: remap them onto asm.inc's CALL16/RET16, which force the 16-bit
 * forms and pass indirect operands (call word ptr [...]) through verbatim.
 *
 * MASM-style far calls through memory (call dword ptr ss:[bp-x] /
 * call dword ptr [x]) cannot be encoded by the integrated assembler at
 * all, so raw CALL FAR m16:16 (FF /3) opcode macros are provided for the
 * sites that need them.
 */

#ifdef __clang__

#define call CALL16
#define ret RET16

.macro call_far32 addr
    .byte 0xFF, 0x1E    /* call far [disp16], DS base */
    .word \addr
.endm

.macro call_far32_ss_bp disp
    .byte 0xFF, 0x5E    /* call far [bp+disp8], SS base */
    .byte \disp
.endm

#else /* GNU as encodes the MASM-style far-through-memory forms directly */

.macro call_far32 addr
    call dword ptr [\addr]
.endm

.macro call_far32_ss_bp disp
    call dword ptr [bp+(\disp)]
.endm

#endif /* __clang__ */
