/*
 * PROJECT:     LiberNT NT Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC user-mode kernel transitions
 *
 * The kernel enters these routines with r1 at a 32-byte frame header followed
 * by the saved CONTEXT (see KUSER_EXCEPTION_STACK), r2 = the ntdll TOC and
 * r13 = the TEB.
 */

#include <asm/ksppc.h>
#include <kxppc.h>

#define USER_FRAME_HEADER 32

/* r3 = &ExceptionRecord, r4 = &Context. RTL recognizes this code range and
 * restores the saved CONTEXT instead of unwinding through a return address. */
NESTED_ENTRY(KiUserExceptionDispatcher)
    bl      ..KiUserExceptionDispatcherWorker
    .globl  ..KiPpcUserExceptionDispatcherEnd
..KiPpcUserExceptionDispatcherEnd:
    .long   0
1:
    b       1b
NESTED_END(KiUserExceptionDispatcher)

/* r3 = NormalContext, r4 = SystemArgument1, r5 = SystemArgument2,
 * r6 = NormalRoutine (a function descriptor); the saved CONTEXT follows the
 * frame header. The routine also receives the CONTEXT as a fourth argument,
 * which LdrInitializeThunk uses. */
NESTED_ENTRY(KiUserApcDispatcher)
    addi    31, 1, USER_FRAME_HEADER
    lwz     0, 0(6)
    lwz     2, 4(6)
    mtctr   0
    mr      6, 31
    bctrl
    mr      3, 31
    li      4, 1
    bl      ..NtContinue
    /* A failed continuation must not return. */
    .long   0
1:
    b       1b
NESTED_END(KiUserApcDispatcher)

/* Reached through the initial user APC: r6 = the CONTEXT to continue with. */
NESTED_ENTRY(LdrInitializeThunk)
    mr      3, 6
    b       ..LdrpInit
NESTED_END(LdrInitializeThunk)
