/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Modern kernel compatibility tests
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define TEST_TRIAGE_BLOCK_COUNT 3

typedef struct _TEST_TRIAGE_BUFFER
{
    KTRIAGE_DUMP_DATA_ARRAY Array;
    KADDRESS_RANGE AdditionalBlocks[TEST_TRIAGE_BLOCK_COUNT - 1];
} TEST_TRIAGE_BUFFER, *PTEST_TRIAGE_BUFFER;

C_ASSERT(sizeof(TEST_TRIAGE_BUFFER) ==
         FIELD_OFFSET(KTRIAGE_DUMP_DATA_ARRAY, Blocks) +
         TEST_TRIAGE_BLOCK_COUNT * sizeof(KADDRESS_RANGE));

static
VOID
TestTriageDumpData(VOID)
{
    TEST_TRIAGE_BUFFER Buffer;
    UCHAR Data[128];
    PKTRIAGE_DUMP_DATA_ARRAY Array = &Buffer.Array;
    PKADDRESS_RANGE Blocks = (PKADDRESS_RANGE)Array->Blocks;
    NTSTATUS Status;

    Status = KeInitializeTriageDumpDataArray(NULL, sizeof(Buffer));
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = KeInitializeTriageDumpDataArray(Array,
                                             FIELD_OFFSET(KTRIAGE_DUMP_DATA_ARRAY,
                                                          Blocks));
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);

    RtlFillMemory(&Buffer, sizeof(Buffer), 0xA5);
    Status = KeInitializeTriageDumpDataArray(Array, sizeof(Buffer));
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    ok_eq_pointer(Array->List.Flink, &Array->List);
    ok_eq_pointer(Array->List.Blink, &Array->List);
    ok_eq_ulong(Array->NumBlocksUsed, 0);
    ok_eq_ulong(Array->NumBlocksTotal, TEST_TRIAGE_BLOCK_COUNT);
    ok_eq_ulong(Array->DataSize, 0);
    ok_eq_ulong(Array->MaxDataSize, KE_MAX_TRIAGE_DUMP_DATA_MEMORY_SIZE);
    ok_eq_ulong(Array->ComponentNameBufferLength, 0);
    ok_eq_pointer(Array->ComponentName, NULL);

    Status = KeAddTriageDumpDataBlock(Array, &Data[16], 16);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Array->NumBlocksUsed, 1);
    ok_eq_ulong(Array->DataSize, 16);
    ok_eq_pointer(Blocks[0].Address, &Data[16]);
    ok_eq_ulongptr(Blocks[0].Size, 16);

    /* Native treats ranges as half-open and does not merge adjacency. */
    Status = KeAddTriageDumpDataBlock(Array, &Data[0], 16);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Array->NumBlocksUsed, 2);
    ok_eq_ulong(Array->DataSize, 32);
    ok_eq_pointer(Blocks[0].Address, &Data[16]);
    ok_eq_ulongptr(Blocks[0].Size, 16);
    ok_eq_pointer(Blocks[1].Address, &Data[0]);
    ok_eq_ulongptr(Blocks[1].Size, 16);

    Status = KeAddTriageDumpDataBlock(Array, &Data[8], 8);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Array->NumBlocksUsed, 2);
    ok_eq_ulong(Array->DataSize, 32);

    Status = KeAddTriageDumpDataBlock(Array, &Data[64], 8);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = KeAddTriageDumpDataBlock(Array, &Data[96], 8);
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok_eq_ulong(Array->NumBlocksUsed, TEST_TRIAGE_BLOCK_COUNT);
    ok_eq_ulong(Array->DataSize, 40);

    Status = KeAddTriageDumpDataBlock(Array, &Data[112], 8);
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok_eq_ulong(Array->NumBlocksUsed, TEST_TRIAGE_BLOCK_COUNT);
    ok_eq_ulong(Array->DataSize, 40);

    Status = KeAddTriageDumpDataBlock(Array,
                                      (PVOID)(ULONG_PTR)-8,
                                      16);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
}

START_TEST(KeModern)
{
    TestTriageDumpData();
}
