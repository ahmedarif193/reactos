/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/ntsessionshim.h
 * PURPOSE:     NT session memory host-test compatibility definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <nvs/include/miproc.h>

#define NTAPI
#define NonPagedPool 0
#define PSF_PROCESS_IN_SESSION_BIT 0x00010000
#define STATUS_INVALID_SYSTEM_SERVICE ((NTSTATUS)0xC000001C)
#define STATUS_ALREADY_COMMITTED ((NTSTATUS)0xC0000021)
#define STATUS_PROCESS_IS_TERMINATING ((NTSTATUS)0xC000010A)
#define ASSERT MI_ASSERT
#define InterlockedIncrement(p) (MI_ATOMIC_ADD32(p, 1) + 1)
#define InterlockedDecrement(p) (MI_ATOMIC_ADD32(p, -1) - 1)
#define InterlockedCompareExchange(p, n, o) MI_ATOMIC_CAS32(p, n, o)
#define InterlockedExchange(p, n) __atomic_exchange_n(p, n, __ATOMIC_SEQ_CST)
#define InterlockedAnd(p, n) __atomic_fetch_and(p, n, __ATOMIC_SEQ_CST)
#define PspSetProcessFlag(p, f) __atomic_fetch_or(&(p)->Flags, f, __ATOMIC_SEQ_CST)
#define MI_PROCESS_OF(p) ((PMI_PROCESS)(p)->Vm.VmWorkingSetList)

typedef ULONG LCID;
typedef LONG *PLONG;
typedef MI_MUTEX KGUARDED_MUTEX;
typedef struct _SESSION_TEST_RUNDOWN { volatile LONG State; } SESSION_TEST_RUNDOWN;
typedef struct _SESSION_TEST_KPROCESS { PEPROCESS Owner; } SESSION_TEST_KPROCESS;
typedef struct _SESSION_TEST_APC_STATE { PEPROCESS Process; } KAPC_STATE, *PKAPC_STATE;
struct _EPROCESS_HOST
{
    SESSION_TEST_KPROCESS Pcb;
    volatile LONG References;
    ULONG Flags;
    ULONG AddressSpaceInitialized;
    PVOID Session;
    struct
    {
        struct { BOOLEAN SessionLeader; } Flags;
        PVOID VmWorkingSetList;
    } Vm;
    SESSION_TEST_RUNDOWN RundownProtect;
};

extern _Thread_local PEPROCESS MiSessionTestCurrent;
extern volatile LONG MiSessionTestAllocations;
extern volatile LONG MiSessionTestFailAllocate;
extern LCID PsDefaultThreadLocaleId, PsDefaultSystemLocaleId;

static inline PEPROCESS PsGetCurrentProcess(VOID) { return MiSessionTestCurrent; }
static inline VOID KeInitializeGuardedMutex(KGUARDED_MUTEX *Lock) { MI_MUTEX_INIT(Lock); }
static inline VOID KeAcquireGuardedMutex(KGUARDED_MUTEX *Lock) { MI_MUTEX_ACQUIRE(Lock); }
static inline VOID KeReleaseGuardedMutex(KGUARDED_MUTEX *Lock) { MI_MUTEX_RELEASE(Lock); }
static inline PVOID ExAllocatePoolWithTag(ULONG Pool, SIZE_T Size, ULONG Tag)
{
    PVOID Allocation;
    (void)Pool; (void)Tag;
    if (MI_ATOMIC_READ32(&MiSessionTestFailAllocate)) return NULL;
    Allocation = malloc(Size);
    if (Allocation != NULL) InterlockedIncrement(&MiSessionTestAllocations);
    return Allocation;
}
static inline VOID ExFreePoolWithTag(PVOID Allocation, ULONG Tag)
{
    (void)Tag;
    InterlockedDecrement(&MiSessionTestAllocations);
    free(Allocation);
}
static inline BOOLEAN ExAcquireRundownProtection(SESSION_TEST_RUNDOWN *Rundown)
{
    LONG State;
    do
    {
        State = MI_ATOMIC_READ32(&Rundown->State);
        if (State & 1) return FALSE;
    } while (MI_ATOMIC_CAS32(&Rundown->State, State + 2, State) != State);
    return TRUE;
}
static inline VOID ExReleaseRundownProtection(SESSION_TEST_RUNDOWN *Rundown)
{
    MI_ASSERT(MI_ATOMIC_ADD32(&Rundown->State, -2) >= 2);
}
static inline VOID ObReferenceObject(PEPROCESS Process) { InterlockedIncrement(&Process->References); }
static inline VOID ObDereferenceObject(PEPROCESS Process) { MI_ASSERT(InterlockedDecrement(&Process->References) >= 1); }
static inline VOID KeStackAttachProcess(SESSION_TEST_KPROCESS *Process, PKAPC_STATE State)
{
    State->Process = MiSessionTestCurrent;
    MiSessionTestCurrent = Process->Owner;
}
static inline VOID KeUnstackDetachProcess(PKAPC_STATE State) { MiSessionTestCurrent = State->Process; }

NTSTATUS NTAPI MmSessionCreate(PULONG SessionId);
NTSTATUS NTAPI MmSessionDelete(ULONG SessionId);
ULONG NTAPI MmGetSessionId(PEPROCESS Process);
ULONG NTAPI MmGetSessionIdEx(PEPROCESS Process);
ULONG NTAPI MmGetSessionLocaleId(VOID);
VOID NTAPI MmSetSessionLocaleId(LCID LocaleId);
PVOID NTAPI MmGetSessionById(ULONG SessionId);
NTSTATUS NTAPI MmAttachSession(PVOID SessionEntry, PKAPC_STATE State);
VOID NTAPI MmDetachSession(PVOID SessionEntry, PKAPC_STATE State);
VOID NTAPI MmQuitNextSession(PVOID SessionEntry);
VOID MiSessionAddProcess(PEPROCESS Process);
VOID MiSessionRemoveProcess(PEPROCESS Process);
