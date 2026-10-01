/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC user exception frame
 */

#pragma once

/* Frame the kernel builds on the user stack before entering
 * KiUserExceptionDispatcher with r3 = &ExceptionRecord, r4 = &Context and
 * r1 at this record (after a standard 24-byte frame header). */
typedef struct DECLSPEC_ALIGN(16) _KUSER_EXCEPTION_STACK
{
    ULONG FrameHeader[6];
    ULONG Reserved[2];
    CONTEXT Context;
    EXCEPTION_RECORD ExceptionRecord;
} KUSER_EXCEPTION_STACK, *PKUSER_EXCEPTION_STACK;

C_ASSERT((FIELD_OFFSET(KUSER_EXCEPTION_STACK, Context) & 7) == 0);
C_ASSERT((sizeof(KUSER_EXCEPTION_STACK) & 15) == 0);
