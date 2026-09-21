/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntsession.c
 * PURPOSE:     NT session memory management
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifdef MM_HOST_TEST
#include <mmcc/nvs/core/ntsessionshim.h>
#else
#include <nvs/nt/mint.h>
#endif

typedef struct _MI_SESSION
{
    PVOID GlobalVirtualAddress;
    volatile LONG ReferenceCount;
    ULONG Flags;
    ULONG SessionId;
    volatile LONG ProcessCount;
    LCID LocaleId;
    LIST_ENTRY Link;
    LIST_ENTRY Processes;
} MI_SESSION, *PMI_SESSION;

PVOID MmSessionBase;
SIZE_T MmSessionSize;
PVOID MiSessionBasePte;
PVOID MiSessionLastPte;

static LIST_ENTRY MiSessionList = { &MiSessionList, &MiSessionList };
static KGUARDED_MUTEX MiSessionLock;
static volatile LONG MiSessionLockState;
static volatile LONG MiSessionLeaderExists;
static ULONG MiNextSessionId;
static LCID MiDefaultSessionLocale = 0x409;

static
VOID
MiSessionLockAcquire(VOID)
{
    if (InterlockedCompareExchange(&MiSessionLockState, 1, 0) == 0)
    {
        KeInitializeGuardedMutex(&MiSessionLock);
        InterlockedExchange(&MiSessionLockState, 2);
    }
    while (InterlockedCompareExchange(&MiSessionLockState, 2, 2) != 2)
        MI_PAUSE();

    KeAcquireGuardedMutex(&MiSessionLock);
}

static
VOID
MiSessionJoinLocked(
    _Inout_ PMI_SESSION Session,
    _Inout_ PEPROCESS Process)
{
    PMI_PROCESS Native = MI_PROCESS_OF(Process);

    ASSERT(Native != NULL && Native->SessionProcess == NULL && Process->Session == NULL);
    Native->SessionProcess = Process;
    InsertTailList(&Session->Processes, &Native->SessionLink);
    Session->ReferenceCount++;
    Session->ProcessCount++;
    Process->Session = Session;
    PspSetProcessFlag(Process, PSF_PROCESS_IN_SESSION_BIT);
}

static
BOOLEAN
MiSessionLeaveLocked(
    _Inout_ PEPROCESS Process)
{
    PMI_PROCESS Native = MI_PROCESS_OF(Process);
    PMI_SESSION Session = Process->Session;

    ASSERT(Native != NULL && Native->SessionProcess == Process && Session != NULL);
    RemoveEntryList(&Native->SessionLink);
    Native->SessionProcess = NULL;
    Process->Session = NULL;
    InterlockedAnd((PLONG)&Process->Flags, ~PSF_PROCESS_IN_SESSION_BIT);
    ASSERT(Session->ProcessCount > 0 && Session->ReferenceCount > 0);
    Session->ProcessCount--;
    if (--Session->ReferenceCount != 0)
        return FALSE;
    ASSERT(IsListEmpty(&Session->Processes));
    RemoveEntryList(&Session->Link);
    return TRUE;
}

static
VOID
MiSessionDereference(
    _Inout_ PMI_SESSION Session)
{
    BOOLEAN Free = FALSE;

    MiSessionLockAcquire();
    if (InterlockedDecrement(&Session->ReferenceCount) == 0)
    {
        ASSERT(Session->ProcessCount == 0 && IsListEmpty(&Session->Processes));
        RemoveEntryList(&Session->Link);
        Free = TRUE;
    }
    KeReleaseGuardedMutex(&MiSessionLock);

    if (Free)
        ExFreePoolWithTag(Session, 'eSmM');
}

VOID
MiSessionAddProcess(
    _Inout_ PEPROCESS NewProcess)
{
    PEPROCESS Current = PsGetCurrentProcess();
    PMI_SESSION Session;

    MiSessionLockAcquire();
    Session = Current->Session;
    if ((Current->Flags & PSF_PROCESS_IN_SESSION_BIT) && Session != NULL && NewProcess->Session == NULL)
        MiSessionJoinLocked(Session, NewProcess);
    KeReleaseGuardedMutex(&MiSessionLock);
}

VOID
MiSessionRemoveProcess(
    _Inout_ PEPROCESS Process)
{
    PMI_SESSION Session;
    BOOLEAN Free = FALSE;

    MiSessionLockAcquire();
    Session = Process->Session;
    if (Session != NULL)
        Free = MiSessionLeaveLocked(Process);
    if (Process->Vm.Flags.SessionLeader)
    {
        Process->Vm.Flags.SessionLeader = FALSE;
        InterlockedExchange(&MiSessionLeaderExists, 0);
    }
    KeReleaseGuardedMutex(&MiSessionLock);

    if (Free)
        ExFreePoolWithTag(Session, 'eSmM');
}

NTSTATUS
NTAPI
MmSessionCreate(
    _Out_ PULONG SessionId)
{
    PEPROCESS Process = PsGetCurrentProcess();
    PMI_SESSION Session;
    NTSTATUS Status = STATUS_SUCCESS;

    if (SessionId == NULL)
        return STATUS_INVALID_PARAMETER;
    if (MI_PROCESS_OF(Process) == NULL)
        return STATUS_PROCESS_IS_TERMINATING;

    Session = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Session), 'eSmM');
    if (Session == NULL)
        return STATUS_NO_MEMORY;

    RtlZeroMemory(Session, sizeof(*Session));
    Session->GlobalVirtualAddress = Session;
    Session->Flags = 1;
    InitializeListHead(&Session->Processes);

    MiSessionLockAcquire();
    if ((Process->Flags & PSF_PROCESS_IN_SESSION_BIT) || Process->Session != NULL)
        Status = STATUS_ALREADY_COMMITTED;
    else if (!Process->Vm.Flags.SessionLeader && MiSessionLeaderExists != 0)
        Status = STATUS_INVALID_SYSTEM_SERVICE;
    else if (MiNextSessionId == ~(ULONG)0)
        Status = STATUS_INSUFFICIENT_RESOURCES;
    else
    {
        Process->Vm.Flags.SessionLeader = TRUE;
        MiSessionLeaderExists = 1;
        Session->SessionId = MiNextSessionId++;
        Session->LocaleId = MiDefaultSessionLocale;
        InsertTailList(&MiSessionList, &Session->Link);
        MiSessionJoinLocked(Session, Process);
        *SessionId = Session->SessionId;
    }
    KeReleaseGuardedMutex(&MiSessionLock);

    if (!NT_SUCCESS(Status))
        ExFreePoolWithTag(Session, 'eSmM');
    return Status;
}

NTSTATUS
NTAPI
MmSessionDelete(
    _In_ ULONG SessionId)
{
    PEPROCESS Process = PsGetCurrentProcess();
    PMI_SESSION Session;
    BOOLEAN Free = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    MiSessionLockAcquire();
    Session = Process->Session;
    if (!(Process->Flags & PSF_PROCESS_IN_SESSION_BIT) || !Process->Vm.Flags.SessionLeader || Session == NULL)
        Status = STATUS_UNABLE_TO_FREE_VM;
    else if (Session->SessionId != SessionId)
        Status = STATUS_INVALID_PARAMETER;
    else
        Free = MiSessionLeaveLocked(Process);
    KeReleaseGuardedMutex(&MiSessionLock);
    if (Free)
        ExFreePoolWithTag(Session, 'eSmM');
    return Status;
}

ULONG
NTAPI
MmGetSessionId(
    _In_ PEPROCESS Process)
{
    PMI_SESSION Session;
    ULONG Id;

    MiSessionLockAcquire();
    Session = Process->Session;
    Id = Process->Vm.Flags.SessionLeader || Session == NULL ? 0 : Session->SessionId;
    KeReleaseGuardedMutex(&MiSessionLock);
    return Id;
}

ULONG
NTAPI
MmGetSessionIdEx(
    _In_ PEPROCESS Process)
{
    PMI_SESSION Session;
    ULONG Id;

    MiSessionLockAcquire();
    Session = Process->Session;
    Id = Process->Vm.Flags.SessionLeader || Session == NULL ? (ULONG)-1 : Session->SessionId;
    KeReleaseGuardedMutex(&MiSessionLock);
    return Id;
}

ULONG
NTAPI
MmGetSessionLocaleId(VOID)
{
    PEPROCESS Process = PsGetCurrentProcess();
    PMI_SESSION Session;
    LCID Locale;

    MiSessionLockAcquire();
    Session = Process->Session;
    Locale = Process->Vm.Flags.SessionLeader || Session == NULL ? PsDefaultThreadLocaleId : Session->LocaleId;
    KeReleaseGuardedMutex(&MiSessionLock);
    return Locale;
}

VOID
NTAPI
MmSetSessionLocaleId(
    _In_ LCID LocaleId)
{
    PEPROCESS Process = PsGetCurrentProcess();
    PMI_SESSION Session;

    MiSessionLockAcquire();
    Session = Process->Session;
    if (Process->Vm.Flags.SessionLeader || Session == NULL)
    {
        PsDefaultThreadLocaleId = LocaleId;
        PsDefaultSystemLocaleId = LocaleId;
        MiDefaultSessionLocale = LocaleId;
    }
    else
        Session->LocaleId = LocaleId;
    KeReleaseGuardedMutex(&MiSessionLock);
}

BOOLEAN
NTAPI
MmIsSessionAddress(
    _In_ PVOID Address)
{
    return (BOOLEAN)(MmSessionSize != 0 && (ULONG_PTR)Address >= (ULONG_PTR)MmSessionBase &&
                     (ULONG_PTR)Address < (ULONG_PTR)MmSessionBase + MmSessionSize);
}

PVOID
NTAPI
MmGetSessionById(
    _In_ ULONG SessionId)
{
    PMI_SESSION Found = NULL;
    PLIST_ENTRY Entry;

    MiSessionLockAcquire();

    for (Entry = MiSessionList.Flink; Entry != &MiSessionList; Entry = Entry->Flink)
    {
        PMI_SESSION Session = CONTAINING_RECORD(Entry, MI_SESSION, Link);

        if (Session->SessionId == SessionId && Session->ProcessCount != 0)
        {
            InterlockedIncrement(&Session->ReferenceCount);
            Found = Session;
            break;
        }
    }

    KeReleaseGuardedMutex(&MiSessionLock);
    return Found;
}

NTSTATUS
NTAPI
MmAttachSession(
    _Inout_ PVOID SessionEntry,
    _Out_ PKAPC_STATE ApcState)
{
    PMI_SESSION Session = SessionEntry;
    PEPROCESS Process = NULL;
    PLIST_ENTRY Entry;

    RtlZeroMemory(ApcState, sizeof(*ApcState));
    if (Session == NULL)
        return STATUS_INVALID_PARAMETER;
    MiSessionLockAcquire();
    for (Entry = Session->Processes.Flink; Entry != &Session->Processes; Entry = Entry->Flink)
    {
        PMI_PROCESS Native = CONTAINING_RECORD(Entry, MI_PROCESS, SessionLink);
        PEPROCESS Candidate = Native->SessionProcess;

        if (Candidate->Vm.Flags.SessionLeader || Candidate->AddressSpaceInitialized < 2 ||
            !ExAcquireRundownProtection(&Candidate->RundownProtect))
            continue;
        ObReferenceObject(Candidate);
        Process = Candidate;
        break;
    }
    KeReleaseGuardedMutex(&MiSessionLock);
    if (Process == NULL)
        return STATUS_PROCESS_IS_TERMINATING;
    KeStackAttachProcess(&Process->Pcb, ApcState);
    return STATUS_SUCCESS;
}

VOID
NTAPI
MmDetachSession(
    _Inout_ PVOID SessionEntry,
    _Out_ PKAPC_STATE ApcState)
{
    PEPROCESS Process = PsGetCurrentProcess();

    ASSERT(Process->Session == SessionEntry);
    KeUnstackDetachProcess(ApcState);
    ExReleaseRundownProtection(&Process->RundownProtect);
    ObDereferenceObject(Process);
}

VOID
NTAPI
MmQuitNextSession(
    _Inout_ PVOID SessionEntry)
{
    MiSessionDereference(SessionEntry);
}
