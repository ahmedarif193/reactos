/*
 * PROJECT:     ReactOS Run-Time Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 RTL stubs
 */

#include <rtl.h>

#define NDEBUG
#include <debug.h>

typedef
_Function_class_(GET_RUNTIME_FUNCTION_CALLBACK)
PRUNTIME_FUNCTION
GET_RUNTIME_FUNCTION_CALLBACK(
    _In_ DWORD64 ControlPc,
    _In_opt_ PVOID Context);
typedef GET_RUNTIME_FUNCTION_CALLBACK *PGET_RUNTIME_FUNCTION_CALLBACK;

PLIST_ENTRY
NTAPI
RtlGetFunctionTableListHead(VOID)
{
    return NULL;
}

BOOLEAN
NTAPI
RtlInstallFunctionTableCallback(
    _In_ ULONG_PTR TableIdentifier,
    _In_ ULONG_PTR BaseAddress,
    _In_ ULONG Length,
    _In_ PGET_RUNTIME_FUNCTION_CALLBACK Callback,
    _In_ PVOID Context,
    _In_opt_z_ PCWSTR OutOfProcessCallbackDll)
{
    (VOID)TableIdentifier;
    (VOID)BaseAddress;
    (VOID)Length;
    (VOID)Callback;
    (VOID)Context;
    (VOID)OutOfProcessCallbackDll;
    return FALSE;
}
