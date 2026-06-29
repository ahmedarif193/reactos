/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS system libraries
 * PURPOSE:           Implementation of _chkstk and _alloca_probe for amd64
 * FILE:              lib/sdk/crt/except/amd64/chkstk_asm.s
 * PROGRAMMER:        Timo Kreuzer (timo.kreuzer@reactos.org)
 *
 * On amd64, __chkstk is called when a function's local variables exceed
 * one page (4096 bytes).  The allocation size is passed in rax.
 * __chkstk must probe each page between the current rsp and (rsp - rax)
 * so the guard page mechanism can extend the committed stack.
 *
 * Register contract (Windows amd64 ABI):
 *   Input:  rax = number of bytes to allocate on the stack
 *   Output: rax = same (unchanged)
 *   Clobbers: r10, r11 (volatile registers)
 *   rsp is NOT modified by __chkstk — the caller subtracts rax from rsp.
 */

/* INCLUDES ******************************************************************/

#include <asm.inc>

#define PAGE_SIZE 4096

/* CODE **********************************************************************/
.code64

/*
 * __chkstk — stack probe for large stack frames
 *
 * Probes each page from (rsp - PAGE_SIZE) down to (rsp - rax), touching
 * one byte per page to trigger the guard page fault handler.
 */
FUNC __chkstk
    .ENDPROLOG
    /* Save rax (allocation size) in r11 */
    mov r11, rax

    /* r10 = current stack pointer (return address is at [rsp]) */
    lea r10, [rsp + 8]

    /* If allocation <= PAGE_SIZE, no probing needed */
    cmp rax, PAGE_SIZE
    jbe .Ldone

.Lprobe_loop:
    sub r10, PAGE_SIZE
    test dword ptr [r10], 0     /* touch the page to trigger guard page */
    sub rax, PAGE_SIZE
    cmp rax, PAGE_SIZE
    ja .Lprobe_loop

.Ldone:
    /* Restore rax to the original allocation size */
    mov rax, r11
    ret
ENDFUNC

FUNC __alloca_probe
    .ENDPROLOG
    /* __alloca_probe has the same contract as __chkstk on amd64 */
    jmp __chkstk
ENDFUNC

END
/* EOF */
