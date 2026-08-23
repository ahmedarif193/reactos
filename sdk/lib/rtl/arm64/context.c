/*
 * PROJECT:     ReactOS Run-Time Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 context restoration support
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <rtl.h>

typedef PVOID (CALLBACK *PRTL_CONSOLIDATE_CALLBACK)(
    _In_ PEXCEPTION_RECORD ExceptionRecord);

DECLSPEC_NORETURN
VOID
NTAPI
RtlpRestoreContextInternal(
    _In_ PCONTEXT ContextRecord);

DECLSPEC_NORETURN
VOID
NTAPI
RtlpArm64ConsolidateCallback(
    _In_ PCONTEXT ContextRecord,
    _In_ PRTL_CONSOLIDATE_CALLBACK Callback,
    _In_ PEXCEPTION_RECORD ExceptionRecord);

VOID
NTAPI
RtlRestoreContext(
    _In_ PCONTEXT ContextRecord,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord)
{
    if ((ExceptionRecord != NULL) &&
        (ExceptionRecord->ExceptionCode == STATUS_UNWIND_CONSOLIDATE) &&
        (ExceptionRecord->NumberParameters >= 1))
    {
        PRTL_CONSOLIDATE_CALLBACK Consolidate;

        Consolidate = (PRTL_CONSOLIDATE_CALLBACK)
            ExceptionRecord->ExceptionInformation[0];
        RtlpArm64ConsolidateCallback(ContextRecord,
                                     Consolidate,
                                     ExceptionRecord);
    }

    RtlpRestoreContextInternal(ContextRecord);
}
