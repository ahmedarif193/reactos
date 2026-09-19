/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#pragma once

/* Architecture-owned synthetic user exception frame. The dispatcher entry
 * receives &ExceptionRecord in a0 and &Context in a1, with SP at this record. */
typedef struct DECLSPEC_ALIGN(16) _KUSER_EXCEPTION_STACK
{
    CONTEXT Context;
    EXCEPTION_RECORD ExceptionRecord;
} KUSER_EXCEPTION_STACK, *PKUSER_EXCEPTION_STACK;

C_ASSERT(FIELD_OFFSET(KUSER_EXCEPTION_STACK, Context) == 0);
C_ASSERT((FIELD_OFFSET(KUSER_EXCEPTION_STACK, ExceptionRecord) & 15) == 0);
C_ASSERT((sizeof(KUSER_EXCEPTION_STACK) & 15) == 0);
