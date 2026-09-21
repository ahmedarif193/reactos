/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/t_session.c
 * PURPOSE:     Session memory host-native regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "mmharness.h"
#include "ntsessionshim.h"

_Thread_local PEPROCESS MiSessionTestCurrent;
volatile LONG MiSessionTestAllocations;
volatile LONG MiSessionTestFailAllocate;
LCID PsDefaultThreadLocaleId = 0x409, PsDefaultSystemLocaleId = 0x409;

static void
SessionProcessInitialize(PEPROCESS Process, PMI_PROCESS Native)
{
    RtlZeroMemory(Process, sizeof(*Process));
    RtlZeroMemory(Native, sizeof(*Native));
    Process->Pcb.Owner = Process;
    Process->References = 1;
    Process->AddressSpaceInitialized = 2;
    Process->Vm.VmWorkingSetList = Native;
}

static void
SessionLifecycle(void)
{
    struct _EPROCESS_HOST Leader, Child, Outsider;
    MI_PROCESS Native[3];
    ULONG Id, NextId;
    PVOID Session;
    KAPC_STATE State;
    NTSTATUS Status;

    SessionProcessInitialize(&Leader, &Native[0]);
    SessionProcessInitialize(&Child, &Native[1]);
    SessionProcessInitialize(&Outsider, &Native[2]);
    MiSessionTestCurrent = &Leader;
    CHECK(MmSessionCreate(&Id) == STATUS_SUCCESS);
    Status = MmSessionDelete(Id + 1);
    CHECK(Status == STATUS_INVALID_PARAMETER);
    if (Status != STATUS_INVALID_PARAMETER)
        return;
    CHECK(MmSessionCreate(&NextId) == STATUS_ALREADY_COMMITTED);
    MiSessionAddProcess(&Child);
    CHECK(MmGetSessionId(&Child) == Id && Child.Session == Leader.Session);
    Session = MmGetSessionById(Id);
    CHECK(Session != NULL);
    MiSessionTestCurrent = &Outsider;
    Child.AddressSpaceInitialized = 1;
    CHECK(MmAttachSession(Session, &State) == STATUS_PROCESS_IS_TERMINATING);
    CHECK(MiSessionTestCurrent == &Outsider && Child.References == 1);
    Child.AddressSpaceInitialized = 2;
    CHECK(MmAttachSession(Session, &State) == STATUS_SUCCESS);
    CHECK(MiSessionTestCurrent == &Child);
    CHECK(Child.References == 2 && Child.RundownProtect.State == 2);
    MmSetSessionLocaleId(0x40C);
    CHECK(MmGetSessionLocaleId() == 0x40C);
    MmDetachSession(Session, &State);
    CHECK(MiSessionTestCurrent == &Outsider);
    CHECK(Child.References == 1 && Child.RundownProtect.State == 0);
    MiSessionTestCurrent = &Leader;
    CHECK(MmSessionDelete(Id) == STATUS_SUCCESS);
    CHECK(Leader.Session == NULL && !(Leader.Flags & PSF_PROCESS_IN_SESSION_BIT));
    CHECK(Child.Session == Session && MmGetSessionId(&Child) == Id);
    CHECK(MmSessionDelete(Id) == STATUS_UNABLE_TO_FREE_VM);
    CHECK(MmSessionCreate(&NextId) == STATUS_SUCCESS && NextId != Id);
    CHECK(MmSessionDelete(NextId) == STATUS_SUCCESS);
    MiSessionRemoveProcess(&Child);
    CHECK(Child.Session == NULL && !(Child.Flags & PSF_PROCESS_IN_SESSION_BIT));
    CHECK(MmGetSessionById(Id) == NULL);
    CHECK(MmAttachSession(Session, &State) == STATUS_PROCESS_IS_TERMINATING);
    MmQuitNextSession(Session);
    CHECK(MiSessionTestAllocations == 0);
    MiSessionRemoveProcess(&Leader);
    CHECK(!Leader.Vm.Flags.SessionLeader);

    MiSessionTestCurrent = &Outsider;
    MiSessionTestFailAllocate = TRUE;
    CHECK(MmSessionCreate(&Id) == STATUS_NO_MEMORY);
    CHECK(Outsider.Session == NULL && !Outsider.Vm.Flags.SessionLeader);
    MiSessionTestFailAllocate = FALSE;
    CHECK(MmSessionCreate(&Id) == STATUS_SUCCESS);
    MiSessionRemoveProcess(&Outsider);
    CHECK(Outsider.Session == NULL && MiSessionTestAllocations == 0);
    MiSessionRemoveProcess(&Outsider);
    CHECK(MiSessionTestAllocations == 0);
    MiSessionTestCurrent = NULL;
}

typedef struct _SESSION_THREAD
{
    struct _EPROCESS_HOST Process;
    MI_PROCESS Native;
    PVOID Session;
    PEPROCESS Target;
    volatile LONG *Progress;
    NTSTATUS Status;
    ULONG Id;
} SESSION_THREAD;

static void *
SessionCreateThread(void *Argument)
{
    SESSION_THREAD *Thread = Argument;

    MiSessionTestCurrent = &Thread->Process;
    Thread->Status = MmSessionCreate(&Thread->Id);
    return NULL;
}

static void
SessionCreateRace(void)
{
    SESSION_THREAD Args[4];
    pthread_t Threads[4];
    ULONG i, Winners = 0;

    for (i = 0; i < 4; i++)
    {
        SessionProcessInitialize(&Args[i].Process, &Args[i].Native);
        CHECK(pthread_create(&Threads[i], NULL, SessionCreateThread, &Args[i]) == 0);
    }
    for (i = 0; i < 4; i++)
        CHECK(pthread_join(Threads[i], NULL) == 0);
    for (i = 0; i < 4; i++)
    {
        CHECK(Args[i].Status == STATUS_SUCCESS || Args[i].Status == STATUS_INVALID_SYSTEM_SERVICE);
        if (Args[i].Status == STATUS_SUCCESS)
            Winners++;
        MiSessionRemoveProcess(&Args[i].Process);
    }
    CHECK(Winners == 1 && MiSessionTestAllocations == 0);
}

static void *
SessionAttachThread(void *Argument)
{
    SESSION_THREAD *Thread = Argument;
    KAPC_STATE State;
    ULONG i;

    MiSessionTestCurrent = &Thread->Process;
    for (i = 0; i < 256; i++)
    {
        NTSTATUS Status = MmAttachSession(Thread->Session, &State);

        CHECK(Status == STATUS_SUCCESS || Status == STATUS_PROCESS_IS_TERMINATING);
        if (NT_SUCCESS(Status))
        {
            CHECK(MiSessionTestCurrent == Thread->Target);
            CHECK(MmGetSessionId(MiSessionTestCurrent) == Thread->Id);
            MmDetachSession(Thread->Session, &State);
        }
        CHECK(MiSessionTestCurrent == &Thread->Process);
        InterlockedIncrement(Thread->Progress);
    }
    return NULL;
}

static void
SessionAttachRundown(void)
{
    struct _EPROCESS_HOST Leader, Child;
    MI_PROCESS Native[2];
    SESSION_THREAD Args[4];
    pthread_t Threads[4];
    volatile LONG Progress = 0;
    PVOID Session;
    ULONG Id, i;

    SessionProcessInitialize(&Leader, &Native[0]);
    SessionProcessInitialize(&Child, &Native[1]);
    MiSessionTestCurrent = &Leader;
    CHECK(MmSessionCreate(&Id) == STATUS_SUCCESS);
    MiSessionAddProcess(&Child);
    MiSessionAddProcess(&Child);
    Session = MmGetSessionById(Id);
    CHECK(Session != NULL);
    for (i = 0; i < 4; i++)
    {
        SessionProcessInitialize(&Args[i].Process, &Args[i].Native);
        Args[i].Session = Session;
        Args[i].Target = &Child;
        Args[i].Id = Id;
        Args[i].Progress = &Progress;
        CHECK(pthread_create(&Threads[i], NULL, SessionAttachThread, &Args[i]) == 0);
    }
    while (MI_ATOMIC_READ32(&Progress) < 16)
        MI_PAUSE();
    __atomic_fetch_or(&Child.RundownProtect.State, 1, __ATOMIC_SEQ_CST);
    while (MI_ATOMIC_READ32(&Child.RundownProtect.State) != 1)
        MI_PAUSE();
    MiSessionRemoveProcess(&Child);
    CHECK(MmSessionDelete(Id) == STATUS_SUCCESS);
    for (i = 0; i < 4; i++)
        CHECK(pthread_join(Threads[i], NULL) == 0);
    CHECK(Progress == 1024 && Child.References == 1 && Child.RundownProtect.State == 1);
    CHECK(MmGetSessionById(Id) == NULL);
    MmQuitNextSession(Session);
    MiSessionRemoveProcess(&Leader);
    CHECK(MiSessionTestAllocations == 0);
    MiSessionTestCurrent = NULL;
}

void
TestSession(void)
{
    SessionCreateRace();
    SessionLifecycle();
    SessionAttachRundown();
}
