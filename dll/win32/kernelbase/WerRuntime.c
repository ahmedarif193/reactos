/*
 * PROJECT:     ReactOS KernelBase
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Error Reporting runtime exception module registrations
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <windows.h>
#include <winternl.h>

#define WER_MAX_REGISTERED_RUNTIME_EXCEPTION_MODULES 16

typedef struct _WER_RUNTIME_MODULE
{
    LIST_ENTRY Entry;
    UNICODE_STRING DllName;
    PVOID Context;
} WER_RUNTIME_MODULE, *PWER_RUNTIME_MODULE;

static SRWLOCK WerRuntimeLock = SRWLOCK_INIT;
static LIST_ENTRY WerRuntimeModules = {&WerRuntimeModules, &WerRuntimeModules};
static ULONG WerRuntimeModuleCount;

static PWER_RUNTIME_MODULE
FindRuntimeModule(
    _In_ PCUNICODE_STRING DllName,
    _In_opt_ PVOID Context)
{
    PLIST_ENTRY Entry;
    PWER_RUNTIME_MODULE Module;

    for (Entry = WerRuntimeModules.Flink; Entry != &WerRuntimeModules; Entry = Entry->Flink)
    {
        Module = CONTAINING_RECORD(Entry, WER_RUNTIME_MODULE, Entry);
        if (Module->Context == Context && RtlEqualUnicodeString(&Module->DllName, DllName, TRUE)) return Module;
    }

    return NULL;
}

HRESULT
WINAPI
WerRegisterRuntimeExceptionModule(
    _In_ PCWSTR DllName,
    _In_opt_ PVOID Context)
{
    PWER_RUNTIME_MODULE Module;
    UNICODE_STRING Name;
    SIZE_T AllocationSize;

    if (!DllName) return E_INVALIDARG;
    RtlInitUnicodeString(&Name, DllName);
    AllocationSize = sizeof(*Module) + Name.Length + sizeof(WCHAR);
    Module = RtlAllocateHeap(NtCurrentTeb()->Peb->ProcessHeap, 0, AllocationSize);
    if (!Module) return E_OUTOFMEMORY;

    Module->DllName.Buffer = (PWSTR)(Module + 1);
    Module->DllName.Length = Name.Length;
    Module->DllName.MaximumLength = Name.Length + sizeof(WCHAR);
    RtlCopyMemory(Module->DllName.Buffer, Name.Buffer, Name.Length);
    Module->DllName.Buffer[Name.Length / sizeof(WCHAR)] = UNICODE_NULL;
    Module->Context = Context;

    RtlAcquireSRWLockExclusive(&WerRuntimeLock);
    if (FindRuntimeModule(&Name, Context))
    {
        RtlReleaseSRWLockExclusive(&WerRuntimeLock);
        RtlFreeHeap(NtCurrentTeb()->Peb->ProcessHeap, 0, Module);
        return S_OK;
    }
    if (WerRuntimeModuleCount == WER_MAX_REGISTERED_RUNTIME_EXCEPTION_MODULES)
    {
        RtlReleaseSRWLockExclusive(&WerRuntimeLock);
        RtlFreeHeap(NtCurrentTeb()->Peb->ProcessHeap, 0, Module);
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }

    InsertTailList(&WerRuntimeModules, &Module->Entry);
    WerRuntimeModuleCount++;
    RtlReleaseSRWLockExclusive(&WerRuntimeLock);
    return S_OK;
}

HRESULT
WINAPI
WerUnregisterRuntimeExceptionModule(
    _In_ PCWSTR DllName,
    _In_opt_ PVOID Context)
{
    PWER_RUNTIME_MODULE Module;
    UNICODE_STRING Name;

    if (!DllName) return E_INVALIDARG;
    RtlInitUnicodeString(&Name, DllName);

    RtlAcquireSRWLockExclusive(&WerRuntimeLock);
    Module = FindRuntimeModule(&Name, Context);
    if (!Module)
    {
        RtlReleaseSRWLockExclusive(&WerRuntimeLock);
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }

    RemoveEntryList(&Module->Entry);
    WerRuntimeModuleCount--;
    RtlReleaseSRWLockExclusive(&WerRuntimeLock);
    RtlFreeHeap(NtCurrentTeb()->Peb->ProcessHeap, 0, Module);
    return S_OK;
}

VOID
WerCleanupRuntimeExceptionModules(VOID)
{
    PWER_RUNTIME_MODULE Module;

    RtlAcquireSRWLockExclusive(&WerRuntimeLock);
    while (!IsListEmpty(&WerRuntimeModules))
    {
        Module = CONTAINING_RECORD(RemoveHeadList(&WerRuntimeModules), WER_RUNTIME_MODULE, Entry);
        RtlFreeHeap(NtCurrentTeb()->Peb->ProcessHeap, 0, Module);
    }
    WerRuntimeModuleCount = 0;
    RtlReleaseSRWLockExclusive(&WerRuntimeLock);
}
