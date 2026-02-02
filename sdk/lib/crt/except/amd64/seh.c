/*
 * PROJECT:     ReactOS CRT
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     SEH support helpers for AMD64
 * COPYRIGHT:   Copyright 2025 ReactOS contributors
 *               Copyright 2025 Ahmed ARIF (arif.ing@outlook.com)
 */

#include <excpt.h>
#include <stdint.h>
#include <windef.h>
#include <winnt.h>

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)
#define SEH_RETURN_ADDRESS() _ReturnAddress()
#else
#define SEH_RETURN_ADDRESS() __builtin_return_address(0)
#endif

#if defined(_MSC_VER)
#define SEH_TLS __declspec(thread)
#else
/*
 * Avoid compiler TLS on non-MSVC toolchains to keep
 * kernel-mode consumers linkable without emu-TLS runtime.
 */
#define SEH_TLS
#endif

NTSYSAPI
VOID
NTAPI
RtlUnwind(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_opt_ PVOID ReturnValue);

#if defined(_MSC_VER)
/* MSVC still uses compiler-provided TLS. */
SEH_TLS LONG __seh_abnormal_termination_flag;
static __inline LONG *
SehGetAbnormalFlagPointer(VOID)
{
    return &__seh_abnormal_termination_flag;
}
#else
#if defined(_NTSYSTEM_) || defined(_LIBCNT_)
/*
 * Kernel mode with GCC/Clang: emu-TLS (__emutls_get_address) is not available
 * because TlsGetValue/TlsSetValue don't exist in the NT kernel.
 * Use a simple static variable instead. This is safe because:
 * - PSEH2 on amd64 passes the abnormal termination flag via RCX, so the
 *   _abnormal_termination() macro never reads this global.
 * - __C_specific_handler only uses it as a scratch pad with save/restore.
 */
static LONG __seh_abnormal_termination_flag_fallback;
static __inline LONG *
SehGetAbnormalFlagPointer(VOID)
{
    return &__seh_abnormal_termination_flag_fallback;
}
#else
typedef struct __emutls_control
{
    uintptr_t size;
    uintptr_t align;
    union
    {
        uintptr_t index;
        void *address;
    } object;
    void *value;
} __emutls_control;

extern void *__cdecl __emutls_get_address(void *Control);

static __emutls_control __seh_abnormal_termination_flag_control =
{
    sizeof(LONG),
    __alignof__(LONG),
    { 0 },
    NULL
};

static __inline LONG *
SehGetAbnormalFlagPointer(VOID)
{
    return (LONG *)__emutls_get_address(&__seh_abnormal_termination_flag_control);
}
#endif /* _NTSYSTEM_ || _LIBCNT_ */
#endif /* _MSC_VER */

LONG *
__cdecl
__seh_get_abnormal_termination_flag_pointer(VOID)
{
    return SehGetAbnormalFlagPointer();
}

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
    return (*SehGetAbnormalFlagPointer() != 0);
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
