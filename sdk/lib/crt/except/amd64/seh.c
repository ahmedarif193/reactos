/*
 * PROJECT:     ReactOS CRT
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     SEH support helpers for AMD64
 * COPYRIGHT:   Copyright 2025 ReactOS contributors
 *               Copyright 2025 Ahmed ARIF (arif.ing@outlook.com)
 */

#include <excpt.h>
#include <windef.h>
#include <winnt.h>

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)
#define SEH_RETURN_ADDRESS() _ReturnAddress()
#else
#define SEH_RETURN_ADDRESS() __builtin_return_address(0)
#endif

#define SEH_TLS __declspec(thread)

NTSYSAPI
VOID
NTAPI
RtlUnwind(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_opt_ PVOID ReturnValue);

/*
 * TLS flag that tracks whether the currently executing __finally handler
 * is running due to an unwind (abnormal termination).
 */
SEH_TLS LONG __seh_abnormal_termination_flag;

VOID
__cdecl
_global_unwind2(
    _In_opt_ PVOID TargetFrame)
{
    RtlUnwind(TargetFrame,
              SEH_RETURN_ADDRESS(),
              NULL,
              NULL);
}

VOID
__cdecl
_local_unwind2(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp)
{
    PVOID EffectiveTargetIp = (TargetIp != NULL) ? TargetIp : SEH_RETURN_ADDRESS();

    RtlUnwind(TargetFrame,
              EffectiveTargetIp,
              NULL,
              NULL);
}

#undef _abnormal_termination

#if defined(__clang__) && !defined(_MSC_VER)
#pragma redefine_extname K32SehAbnormalTermination _abnormal_termination
#define SEH_ABNORMAL_TERMINATION_NAME K32SehAbnormalTermination
#else
#if defined(_MSC_VER)
#pragma function(_abnormal_termination)
#endif
#define SEH_ABNORMAL_TERMINATION_NAME _abnormal_termination
#endif

INT
__cdecl
SEH_ABNORMAL_TERMINATION_NAME(VOID)
{
    return (__seh_abnormal_termination_flag != 0);
}

#undef SEH_ABNORMAL_TERMINATION_NAME

EXCEPTION_DISPOSITION
__cdecl
_except_handler2(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_ PVOID EstablisherFrame,
    _Inout_ PCONTEXT ContextRecord,
    _Inout_ PDISPATCHER_CONTEXT DispatcherContext)
{
    return __C_specific_handler(ExceptionRecord,
                                EstablisherFrame,
                                ContextRecord,
                                DispatcherContext);
}

EXCEPTION_DISPOSITION
__cdecl
_except_handler3(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_ PVOID EstablisherFrame,
    _Inout_ PCONTEXT ContextRecord,
    _Inout_ PDISPATCHER_CONTEXT DispatcherContext)
{
    return __C_specific_handler(ExceptionRecord,
                                EstablisherFrame,
                                ContextRecord,
                                DispatcherContext);
}
