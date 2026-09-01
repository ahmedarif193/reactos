/*
 * PROJECT:     ReactOS API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Non-CFG loader control image
 */

#include <windows.h>

#ifndef CFG_CALL_TARGET_VALID
typedef struct _CFG_CALL_TARGET_INFO_LOCAL
{
    ULONG_PTR Offset;
    ULONG_PTR Flags;
} CFG_CALL_TARGET_INFO_LOCAL;
#else
typedef CFG_CALL_TARGET_INFO CFG_CALL_TARGET_INFO_LOCAL;
#endif

typedef BOOL (WINAPI *PFN_SET_PROCESS_VALID_CALL_TARGETS)(HANDLE, PVOID, SIZE_T, ULONG, CFG_CALL_TARGET_INFO_LOCAL *);

__declspec(dllexport) __declspec(noinline)
DWORD WINAPI
CfgPlainTarget(VOID)
{
    return 0x47f;
}

__declspec(dllexport)
BOOL WINAPI
CfgPlainSetProcessValidCallTargets(HANDLE Process, PVOID Address, SIZE_T Size, ULONG Count, CFG_CALL_TARGET_INFO_LOCAL *Targets)
{
    PFN_SET_PROCESS_VALID_CALL_TARGETS Function;
    HMODULE Module;

    Module = LoadLibraryW(L"api-ms-win-core-memory-l1-1-2.dll");
    if (Module == NULL)
        Module = GetModuleHandleW(L"kernel32.dll");
    Function = Module != NULL ? (PFN_SET_PROCESS_VALID_CALL_TARGETS)GetProcAddress(Module, "SetProcessValidCallTargets") : NULL;
    return Function != NULL && Function(Process, Address, Size, Count, Targets);
}

BOOL WINAPI
DllMain(HINSTANCE Instance, DWORD Reason, LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Reason);
    UNREFERENCED_PARAMETER(Reserved);
    return TRUE;
}
