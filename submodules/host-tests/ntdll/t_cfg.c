/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/ntdll/t_cfg.c
 * PURPOSE:     Dynamic Control Flow Guard registry regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <ntdll.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); abort(); } } while (0)

typedef struct { ULONG_PTR Offset, Flags; } CFG_TARGET_INFO;
NTSTATUS NTAPI RtlSetCfgTargetValidity(PVOID Base, SIZE_T Size, ULONG Count, CFG_TARGET_INFO *Targets);

PEB HostPeb;
static atomic_uint HostLockAcquisitions;
static atomic_uint HostAllocations;
static atomic_bool HostFailAllocation;

VOID RtlAcquireSRWLockExclusive(RTL_SRWLOCK *Lock)
{
    atomic_fetch_add(&HostLockAcquisitions, 1);
    CHECK(pthread_rwlock_wrlock(Lock) == 0);
}

VOID RtlReleaseSRWLockExclusive(RTL_SRWLOCK *Lock)
{
    CHECK(pthread_rwlock_unlock(Lock) == 0);
}

VOID RtlAcquireSRWLockShared(RTL_SRWLOCK *Lock)
{
    atomic_fetch_add(&HostLockAcquisitions, 1);
    CHECK(pthread_rwlock_rdlock(Lock) == 0);
}

VOID RtlReleaseSRWLockShared(RTL_SRWLOCK *Lock)
{
    CHECK(pthread_rwlock_unlock(Lock) == 0);
}

PVOID RtlAllocateHeap(PVOID Heap, ULONG Flags, SIZE_T Size)
{
    PVOID Allocation;
    (void)Heap;
    CHECK(Flags == HEAP_ZERO_MEMORY);
    if (atomic_load(&HostFailAllocation))
        return NULL;
    Allocation = calloc(1, Size);
    if (Allocation != NULL)
        atomic_fetch_add(&HostAllocations, 1);
    return Allocation;
}

BOOLEAN RtlFreeHeap(PVOID Heap, ULONG Flags, PVOID Address)
{
    (void)Heap;
    CHECK(Flags == 0 && Address != NULL);
    CHECK(atomic_fetch_sub(&HostAllocations, 1) != 0);
    free(Address);
    return TRUE;
}

NTSTATUS NtQueryVirtualMemory(PVOID Process, PVOID Address, ULONG Class,
                             MEMORY_BASIC_INFORMATION *Info, SIZE_T Size, SIZE_T *Returned)
{
    (void)Returned;
    CHECK(Process == NtCurrentProcess() && Class == MemoryBasicInformation && Size == sizeof(*Info));
    if ((ULONG_PTR)Address == 0x1000)
        return STATUS_INVALID_ADDRESS;
    Info->State = (ULONG_PTR)Address == 0x2000 ? 0 : MEM_COMMIT;
    Info->Protect = (ULONG_PTR)Address == 0x3000 ? 1 : PAGE_EXECUTE_READ;
    return STATUS_SUCCESS;
}

static void *EmptyWorker(void *Argument)
{
    ULONG Index;
    for (Index = 0; Index < 10000; Index++)
        RtlUnregisterCfgTargetRange(Argument);
    return NULL;
}

static void RunWorkers(void *(*Worker)(void *))
{
    pthread_t Threads[4];
    ULONG Index;
    for (Index = 0; Index < 4; Index++)
        CHECK(pthread_create(&Threads[Index], NULL, Worker, (PVOID)(ULONG_PTR)(0x10000 + Index * 0x10000)) == 0);
    for (Index = 0; Index < 4; Index++)
        CHECK(pthread_join(Threads[Index], NULL) == 0);
}

static void CheckEmptyRegistry(void)
{
    unsigned Before = atomic_load(&HostLockAcquisitions);
    RunWorkers(EmptyWorker);
    CHECK(atomic_load(&HostLockAcquisitions) == Before);
    CHECK(atomic_load(&HostAllocations) == 0);
}

static void *RangeWorker(void *Base)
{
    ULONG Iteration, Index;
    for (Iteration = 0; Iteration < 512; Iteration++)
    {
        CFG_TARGET_INFO Targets[16];
        CHECK(RtlRegisterCfgTargetRange(Base, 256) == STATUS_SUCCESS);
        for (Index = 0; Index < 16; Index++)
        {
            CHECK(!LdrpGuardDynamicTargetIsExecutable((PVOID)((ULONG_PTR)Base + Index * 16)));
            Targets[Index].Offset = Index * 16;
            Targets[Index].Flags = 1;
        }
        CHECK(RtlSetCfgTargetValidity(Base, 256, 16, Targets) == STATUS_SUCCESS);
        for (Index = 0; Index < 16; Index++)
        {
            CHECK(Targets[Index].Flags == 3);
            CHECK(LdrpGuardDynamicTargetIsExecutable((PVOID)((ULONG_PTR)Base + Index * 16)));
            Targets[Index].Flags = 0;
        }
        CHECK(RtlSetCfgTargetValidity(Base, 256, 16, Targets) == STATUS_SUCCESS);
        for (Index = 0; Index < 16; Index++)
            CHECK(!LdrpGuardDynamicTargetIsExecutable((PVOID)((ULONG_PTR)Base + Index * 16)));
        RtlUnregisterCfgTargetRange((PVOID)((ULONG_PTR)Base + 0x1000));
        CHECK(!LdrpGuardDynamicTargetIsExecutable(Base));
        RtlUnregisterCfgTargetRange(Base);
        CHECK(LdrpGuardDynamicTargetIsExecutable(Base));
    }
    return NULL;
}

int main(void)
{
    CFG_TARGET_INFO Target = {0, 1};
    PVOID Base = (PVOID)(ULONG_PTR)0x10000;

    CheckEmptyRegistry();
    CHECK(RtlRegisterCfgTargetRange(NULL, 256) == STATUS_INVALID_PARAMETER);
    CHECK(RtlRegisterCfgTargetRange(Base, 0) == STATUS_INVALID_PARAMETER);
    CHECK(RtlRegisterCfgTargetRange(Base, MAXULONG_PTR) == STATUS_INVALID_PARAMETER);
    atomic_store(&HostFailAllocation, TRUE);
    CHECK(RtlRegisterCfgTargetRange(Base, 256) == STATUS_NO_MEMORY);
    atomic_store(&HostFailAllocation, FALSE);
    CheckEmptyRegistry();
    CHECK(RtlSetCfgTargetValidity(Base, 256, 1, &Target) == STATUS_INVALID_ADDRESS);
    CHECK(!LdrpGuardDynamicTargetIsExecutable((PVOID)(ULONG_PTR)0x1000));
    CHECK(!LdrpGuardDynamicTargetIsExecutable((PVOID)(ULONG_PTR)0x2000));
    CHECK(!LdrpGuardDynamicTargetIsExecutable((PVOID)(ULONG_PTR)0x3000));
    RunWorkers(RangeWorker);
    CheckEmptyRegistry();
    puts("CFG registry: empty cleanup, validity, concurrent publication and teardown passed");
    return 0;
}
