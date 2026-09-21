/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/pool/host/t_ntpool.c
 * PURPOSE:     NT pool allocator host-native regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/pool/ex/expool.h>

#define ARENA_BYTES (128 * 1024 * 1024)
#define TAG_TEST 0x74507845

_Thread_local ULONG PoolHostProcessor;
ULONG KeNumberProcessors = 1;
int MiSystem;
static EPROCESS SystemProcess, TestProcess;
PEPROCESS PsInitialSystemProcess = &SystemProcess;
static PUCHAR Arena, Protection;
static ULONG Failures, Checks;
extern POOL_KM_LAYOUT MiPoolLayout;

#define CHECK(e) do { Checks++; if (!(e)) { Failures++; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); } } while (0)

ULONG64 KeQueryInterruptTime(VOID) { return 1; }
KIRQL KeGetCurrentIrql(VOID) { return 0; }
_Noreturn VOID KeBugCheckEx(ULONG Code, ULONG_PTR A, ULONG_PTR B, ULONG_PTR C, ULONG_PTR D)
{ fprintf(stderr, "bugcheck %x %zx %zx %zx %zx\n", Code, A, B, C, D); abort(); }
_Noreturn VOID ExRaiseStatus(NTSTATUS Status) { fprintf(stderr, "raise %x\n", Status); abort(); }
BOOLEAN MmUseSpecialPool(SIZE_T Size, ULONG Tag) { return FALSE; }
PVOID MmAllocateSpecialPool(SIZE_T Size, ULONG Tag, POOL_TYPE Type, ULONG Mode) { return NULL; }
BOOLEAN MmIsSpecialPoolAddress(PVOID Block) { return FALSE; }
VOID MmFreeSpecialPool(PVOID Block) { abort(); }
PEPROCESS PsGetCurrentProcess(VOID) { return &TestProcess; }
VOID ObReferenceObject(PEPROCESS Process) { Process->References++; }
VOID ObDereferenceObject(PEPROCESS Process) { Process->References--; }
VOID MiWakeBalanceSetManager(VOID) { }

NTSTATUS PsChargeProcessPoolQuota(PEPROCESS Process, ULONG Type, SIZE_T Size)
{
    CHECK(Type < 2);
    if (Type >= 2) return STATUS_INVALID_PARAMETER;
    Process->Quota[Type] += Size;
    return STATUS_SUCCESS;
}

VOID PsReturnPoolQuota(PEPROCESS Process, ULONG Type, SIZE_T Size)
{
    CHECK(Type < 2);
    if (Type >= 2) return;
    CHECK(Process->Quota[Type] >= Size);
    Process->Quota[Type] -= Size;
}

NTSTATUS MiSystemCommitPinned(int *System, ULONG64 Base, ULONG Pages, ULONG Protect)
{
    SIZE_T Offset = (SIZE_T)(Base - (ULONG64)(ULONG_PTR)Arena);
    CHECK(Offset < ARENA_BYTES && Pages <= (ARENA_BYTES - Offset) / PAGE_SIZE);
    if (Offset >= ARENA_BYTES || Pages > (ARENA_BYTES - Offset) / PAGE_SIZE)
        return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < Pages; i++)
    {
        CHECK(Protection[(Offset >> PAGE_SHIFT) + i] == 0);
        Protection[(Offset >> PAGE_SHIFT) + i] = (UCHAR)Protect;
    }
    return STATUS_SUCCESS;
}

VOID MiSystemDecommitPinned(int *System, ULONG64 Base, ULONG Pages)
{
    SIZE_T First = ((ULONG_PTR)Base - (ULONG_PTR)Arena) >> PAGE_SHIFT;
    for (ULONG i = 0; i < Pages; i++)
    {
        CHECK(Protection[First + i] != 0);
        Protection[First + i] = 0;
    }
}

static VOID CheckAllocation(PVOID Block, SIZE_T Size, BOOLEAN Execute, BOOLEAN Zero)
{
    CHECK(Block != NULL);
    if (Block == NULL) return;
    CHECK(((ULONG_PTR)Block & (Size >= PAGE_SIZE ? PAGE_SIZE - 1 : 15)) == 0);
    for (SIZE_T i = 0; i < Size; i += PAGE_SIZE)
        CHECK(Protection[((PUCHAR)Block + i - Arena) >> PAGE_SHIFT] ==
              (Execute ? MI_PROT_EXECUTE_READWRITE : MI_PROT_READWRITE));
    if (Zero)
        for (SIZE_T i = 0; i < Size; i++) CHECK(((PUCHAR)Block)[i] == 0);
    memset(Block, 0xAB, Size);
    ExFreePoolWithTag(Block, TAG_TEST);
}

int main(void)
{
    const SIZE_T Sizes[] = { 32, 160, PAGE_SIZE, PAGE_SIZE * 17, POOL_UNIT_SIZE * 2 };
    POOL_TAG_USAGE Usage;
    CHECK(posix_memalign((void **)&Arena, POOL_UNIT_SIZE, ARENA_BYTES) == 0);
    Protection = calloc(ARENA_BYTES / PAGE_SIZE, 1);
    CHECK(Arena != NULL && Protection != NULL);
    memset(Arena, 0, ARENA_BYTES);
    memset(Protection, MI_PROT_READWRITE, (4 * 1024 * 1024) / PAGE_SIZE);
    MiPoolLayout.NonPagedResidentBase = Arena;
    MiPoolLayout.NonPagedResidentBytes = 4 * 1024 * 1024;
    MiPoolLayout.NonPagedExpansionBase = Arena + MiPoolLayout.NonPagedResidentBytes;
    MiPoolLayout.NonPagedExpansionBytes = 28 * 1024 * 1024;
    MiPoolLayout.PagedBase = Arena + 64 * 1024 * 1024;
    MiPoolLayout.PagedBytes = 64 * 1024 * 1024;
    MiPoolLayout.ExecutableBase = Arena + 32 * 1024 * 1024;
    MiPoolLayout.ExecutableBytes = 32 * 1024 * 1024;
    InitializePool(NonPagedPool, 0);
    InitializePool(PagedPool, 0);
    for (ULONG j = 0; j < 20; j++)
    {
        SIZE_T Size = Sizes[j % RTL_NUMBER_OF(Sizes)];
        CheckAllocation(ExAllocatePoolWithTag(NonPagedPoolExecute, Size, TAG_TEST), Size, TRUE, FALSE);
        CheckAllocation(ExAllocatePoolWithTag(NonPagedPoolNx, Size, TAG_TEST), Size, FALSE, FALSE);
        CheckAllocation(ExAllocatePool2(POOL_FLAG_NON_PAGED_EXECUTE, Size, TAG_TEST), Size, TRUE, TRUE);
        CheckAllocation(ExAllocatePool2(POOL_FLAG_NON_PAGED, Size, TAG_TEST), Size, FALSE, TRUE);
        CheckAllocation(ExAllocatePool2(POOL_FLAG_NON_PAGED | POOL_FLAG_USE_QUOTA, Size, TAG_TEST), Size, FALSE, TRUE);
        CheckAllocation(ExAllocatePool2(POOL_FLAG_NON_PAGED_EXECUTE | POOL_FLAG_USE_QUOTA, Size, TAG_TEST), Size, TRUE, TRUE);
        CheckAllocation(ExAllocatePoolWithQuotaTag(NonPagedPool, Size, TAG_TEST), Size, TRUE, FALSE);
        CheckAllocation(ExAllocatePool2(POOL_FLAG_PAGED | POOL_FLAG_USE_QUOTA, Size, TAG_TEST), Size, FALSE, TRUE);
        CheckAllocation(ExAllocatePool3(POOL_FLAG_NON_PAGED_EXECUTE, Size, TAG_TEST, NULL, 0), Size, TRUE, TRUE);
        CHECK(TestProcess.References == 0);
        CHECK(TestProcess.Quota[0] == 0 && TestProcess.Quota[1] == 0);
    }
    CHECK(PoolTagQuery(&ExpPoolState.Tracker, TAG_TEST, &Usage));
    CHECK(Usage.Allocations[0] == 160 && Usage.Allocations[1] == 20);
    CHECK(Usage.Allocations[0] == Usage.Frees[0] && Usage.Allocations[1] == Usage.Frees[1]);
    CHECK(Usage.Bytes[0] == 0 && Usage.Bytes[1] == 0);
    free(Protection);
    free(Arena);
    printf("NT pool: %u checks, %u failures\n", Checks, Failures);
    return Failures != 0;
}
