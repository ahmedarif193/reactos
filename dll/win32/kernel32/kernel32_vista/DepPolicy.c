/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Data execution prevention policy and assorted process helpers
 * COPYRIGHT:   Adapted from Wine dlls/kernel32/process.c and dlls/kernelbase/debug.c
 */

#include "k32_vista.h"

NTSYSAPI
VOID
WINAPI
TpCallbackUnloadDllOnCompletion(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _In_ HMODULE Module);

#ifndef PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION
#define PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION 0x00000002
#endif

BOOL
WINAPI
SetProcessDEPPolicy(
    _In_ DWORD dwFlags)
{
#ifdef _WIN64
    UNREFERENCED_PARAMETER(dwFlags);
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
#else
    ULONG ExecuteFlags;
    NTSTATUS Status;

    if (dwFlags & ~(PROCESS_DEP_ENABLE | PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    ExecuteFlags = MEM_EXECUTE_OPTION_PERMANENT;
    if (dwFlags & PROCESS_DEP_ENABLE)
        ExecuteFlags |= MEM_EXECUTE_OPTION_DISABLE;
    else
        ExecuteFlags |= MEM_EXECUTE_OPTION_ENABLE;

    if (dwFlags & PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION)
        ExecuteFlags |= MEM_EXECUTE_OPTION_DISABLE_THUNK_EMULATION;

    Status = NtSetInformationProcess(NtCurrentProcess(),
                                     ProcessExecuteFlags,
                                     &ExecuteFlags,
                                     sizeof(ExecuteFlags));
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    return TRUE;
#endif
}

BOOL
WINAPI
GetProcessDEPPolicy(
    _In_ HANDLE hProcess,
    _Out_ LPDWORD lpFlags,
    _Out_ PBOOL lpPermanent)
{
#ifdef _WIN64
    UNREFERENCED_PARAMETER(hProcess);
    UNREFERENCED_PARAMETER(lpFlags);
    UNREFERENCED_PARAMETER(lpPermanent);
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
#else
    ULONG ExecuteFlags = 0;
    NTSTATUS Status;

    if (lpFlags == NULL || lpPermanent == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Status = NtQueryInformationProcess(hProcess,
                                       ProcessExecuteFlags,
                                       &ExecuteFlags,
                                       sizeof(ExecuteFlags),
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    *lpFlags = 0;
    if (ExecuteFlags & MEM_EXECUTE_OPTION_DISABLE)
        *lpFlags |= PROCESS_DEP_ENABLE;
    if (ExecuteFlags & MEM_EXECUTE_OPTION_DISABLE_THUNK_EMULATION)
        *lpFlags |= PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION;

    *lpPermanent = (ExecuteFlags & MEM_EXECUTE_OPTION_PERMANENT) ? TRUE : FALSE;
    return TRUE;
#endif
}

VOID
WINAPI
FlushProcessWriteBuffers(VOID)
{
    MemoryBarrier();
    (VOID)NtFlushInstructionCache(NtCurrentProcess(), NULL, 0);
    MemoryBarrier();
}

VOID
WINAPI
FreeLibraryWhenCallbackReturns(
    _Inout_ PTP_CALLBACK_INSTANCE pci,
    _In_ HMODULE mod)
{
    TpCallbackUnloadDllOnCompletion(pci, mod);
}
