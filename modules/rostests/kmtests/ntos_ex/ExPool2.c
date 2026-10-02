/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite ExAllocatePool2 flag validation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define TEST_TAG 'tP2E'
#define TEST_SIZE 256

typedef struct _POOL2_CASE
{
    ULONG64 Flags;
    BOOLEAN Allocated;
    NTSTATUS Raised;
    BOOLEAN CrashesWindows;
} POOL2_CASE;

static const POOL2_CASE Cases[] =
{
    { 0, FALSE, STATUS_INVALID_PARAMETER },
    { POOL_FLAG_USE_QUOTA, FALSE, STATUS_INVALID_PARAMETER },
    { POOL_FLAG_UNINITIALIZED, FALSE, STATUS_INVALID_PARAMETER },
    { POOL_FLAG_CACHE_ALIGNED, FALSE, STATUS_INVALID_PARAMETER },
    { POOL_FLAG_NON_PAGED, TRUE, STATUS_SUCCESS },
    { POOL_FLAG_NON_PAGED_EXECUTE, TRUE, STATUS_SUCCESS },
    { POOL_FLAG_PAGED, TRUE, STATUS_SUCCESS },
    { POOL_FLAG_NON_PAGED | POOL_FLAG_PAGED, FALSE, STATUS_INVALID_PARAMETER },
    { POOL_FLAG_NON_PAGED | POOL_FLAG_NON_PAGED_EXECUTE, FALSE, STATUS_INVALID_PARAMETER },
    { POOL_FLAG_PAGED | POOL_FLAG_NON_PAGED_EXECUTE, FALSE, STATUS_INVALID_PARAMETER },
    { POOL_FLAG_NON_PAGED | POOL_FLAG_PAGED | POOL_FLAG_NON_PAGED_EXECUTE, FALSE, STATUS_INVALID_PARAMETER },
    { POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED, TRUE, STATUS_SUCCESS },
    { POOL_FLAG_PAGED | POOL_FLAG_UNINITIALIZED, TRUE, STATUS_SUCCESS },
    { POOL_FLAG_NON_PAGED | POOL_FLAG_CACHE_ALIGNED, TRUE, STATUS_SUCCESS },
    { POOL_FLAG_PAGED | POOL_FLAG_CACHE_ALIGNED, TRUE, STATUS_SUCCESS },
    { POOL_FLAG_NON_PAGED | POOL_FLAG_USE_QUOTA, TRUE, STATUS_SUCCESS },
    { POOL_FLAG_PAGED | POOL_FLAG_USE_QUOTA, TRUE, STATUS_SUCCESS },
    { POOL_FLAG_NON_PAGED | POOL_FLAG_RESERVED1, FALSE, STATUS_INVALID_PARAMETER },
    { POOL_FLAG_NON_PAGED | POOL_FLAG_RESERVED2, TRUE, STATUS_SUCCESS },
    { POOL_FLAG_NON_PAGED | POOL_FLAG_SPECIAL_POOL, TRUE, STATUS_SUCCESS },
    { POOL_FLAG_NON_PAGED | 0x200000000ULL, TRUE, STATUS_SUCCESS },
    { POOL_FLAG_NON_PAGED | 0x8000000000000000ULL, TRUE, STATUS_SUCCESS },
    { POOL_FLAG_SPECIAL_POOL, FALSE, STATUS_INVALID_PARAMETER },
    { POOL_FLAG_NON_PAGED | POOL_FLAG_RESERVED4, FALSE, STATUS_INVALID_PARAMETER },
    { POOL_FLAG_NON_PAGED | 0x1000, FALSE, STATUS_INVALID_PARAMETER },
    { POOL_FLAG_NON_PAGED | 0x80000000, FALSE, STATUS_INVALID_PARAMETER },
    { POOL_FLAG_NON_PAGED | POOL_FLAG_RESERVED3, FALSE, STATUS_INVALID_PARAMETER, TRUE },
};

static
BOOLEAN
IsZeroed(
    _In_ PUCHAR Buffer,
    _In_ SIZE_T Size)
{
    SIZE_T Index;

    for (Index = 0; Index < Size; Index++)
    {
        if (Buffer[Index] != 0)
            return FALSE;
    }
    return TRUE;
}

static
NTSTATUS
AllocateRaising(
    _In_ ULONG64 Flags,
    _In_ SIZE_T Size,
    _Out_ PVOID *Block)
{
    NTSTATUS Status = STATUS_SUCCESS;

    *Block = NULL;
    _SEH2_TRY
    {
        *Block = ExAllocatePool2(Flags | POOL_FLAG_RAISE_ON_FAILURE, Size, TEST_TAG);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    return Status;
}

START_TEST(ExPool2)
{
    const POOL2_CASE *Case;
    NTSTATUS Status;
    PVOID Block;
    ULONG Index, Round;
    BOOLEAN IsReactOS;

    IsReactOS = *(volatile ULONG *)(KI_USER_SHARED_DATA + PAGE_SIZE - sizeof(ULONG)) == 0x8eac705;
    Block = ExAllocatePool2(POOL_FLAG_NON_PAGED, 0, TEST_TAG);
    ok(Block != NULL, "zero bytes: allocated=%u\n", Block != NULL);
    if (Block != NULL)
        ExFreePoolWithTag(Block, TEST_TAG);

    Status = AllocateRaising(POOL_FLAG_NON_PAGED, 0, &Block);
    ok(Status == STATUS_SUCCESS, "zero bytes raising: status=%lx allocated=%u\n", Status, Block != NULL);
    if (Block != NULL)
        ExFreePoolWithTag(Block, TEST_TAG);

    Block = ExAllocatePool2(POOL_FLAG_NON_PAGED, MAXSIZE_T / 2, TEST_TAG);
    ok(Block == NULL, "huge: allocated=%u\n", Block != NULL);
    if (Block != NULL)
        ExFreePoolWithTag(Block, TEST_TAG);

    Status = AllocateRaising(POOL_FLAG_NON_PAGED, MAXSIZE_T / 2, &Block);
    ok(Status == STATUS_INSUFFICIENT_RESOURCES, "huge raising: status=%lx allocated=%u\n", Status, Block != NULL);
    if (Block != NULL)
        ExFreePoolWithTag(Block, TEST_TAG);

    for (Round = 0; Round < 2000; Round++)
    {
        static PVOID Blocks[32];
        SIZE_T Size = 1 + (Round * 37) % 5000;
        ULONG Slot = Round % RTL_NUMBER_OF(Blocks);
        ULONG64 Flags = (Round & 1) ? POOL_FLAG_PAGED : POOL_FLAG_NON_PAGED;

        if (Blocks[Slot] != NULL)
            ExFreePoolWithTag(Blocks[Slot], TEST_TAG);
        Blocks[Slot] = ExAllocatePool2(Flags, Size, TEST_TAG);
        if (Blocks[Slot] == NULL || !IsZeroed(Blocks[Slot], Size))
        {
            ok(FALSE, "round %lu: size %Iu allocated=%u\n", Round, Size, Blocks[Slot] != NULL);
            break;
        }

        RtlFillMemory(Blocks[Slot], Size, 0x5A);
        if (Round == 1999)
        {
            for (Slot = 0; Slot < RTL_NUMBER_OF(Blocks); Slot++)
            {
                if (Blocks[Slot] != NULL)
                    ExFreePoolWithTag(Blocks[Slot], TEST_TAG);
                Blocks[Slot] = NULL;
            }
        }
    }
    ok(Round == 2000, "torture stopped at round %lu\n", Round);

    for (Index = 0; Index < RTL_NUMBER_OF(Cases); Index++)
    {
        Case = &Cases[Index];
        if (Case->CrashesWindows && !IsReactOS)
            continue;

        Block = ExAllocatePool2(Case->Flags, TEST_SIZE, TEST_TAG);
        ok((Block != NULL) == Case->Allocated, "flags %I64x: allocated=%u\n", Case->Flags, Block != NULL);
        if (Block != NULL)
        {
            if (!(Case->Flags & POOL_FLAG_UNINITIALIZED))
                ok(IsZeroed(Block, TEST_SIZE), "flags %I64x: block is not zeroed\n", Case->Flags);
            if (Case->Flags & POOL_FLAG_CACHE_ALIGNED)
                ok(((ULONG_PTR)Block & (SYSTEM_CACHE_ALIGNMENT_SIZE - 1)) == 0, "flags %I64x: block %p is not cache aligned\n", Case->Flags, Block);
            RtlFillMemory(Block, TEST_SIZE, 0xA5);
            ExFreePoolWithTag(Block, TEST_TAG);
        }

        Status = AllocateRaising(Case->Flags, TEST_SIZE, &Block);
        ok(Status == Case->Raised, "flags %I64x raising: status=%lx allocated=%u\n", Case->Flags, Status, Block != NULL);
        ok((Block != NULL) == Case->Allocated, "flags %I64x raising: allocated=%u\n", Case->Flags, Block != NULL);
        if (Block != NULL)
            ExFreePoolWithTag(Block, TEST_TAG);
    }
}
