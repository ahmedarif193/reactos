/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Process component energy accounting parity tests
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>

C_ASSERT(sizeof(PROCESS_ENERGY_VALUES) == 432);

START_TEST(PsEnergy)
{
    PROCESS_ENERGY_VALUES Before;
    PROCESS_ENERGY_VALUES After;
    ULONG ReturnLength;
    NTSTATUS Status;

    RtlFillMemory(&Before, sizeof(Before), 0xA5);
    ReturnLength = 0xA5A5A5A5;
    Status = ZwQueryInformationProcess(NtCurrentProcess(),
                                       ProcessEnergyValues,
                                       &Before,
                                       sizeof(Before),
                                       &ReturnLength);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(ReturnLength, sizeof(Before));
    if (!NT_SUCCESS(Status))
        return;

    PsUpdateComponentPower(PsGetCurrentProcess(), 1, 0x101);
    PsUpdateComponentPower(PsGetCurrentProcess(),
                           2,
                           0x0000010200000103ULL);
    PsUpdateComponentPower(PsGetCurrentProcess(),
                           3,
                           0x0000010400000105ULL);

    RtlFillMemory(&After, sizeof(After), 0xA5);
    ReturnLength = 0xA5A5A5A5;
    Status = ZwQueryInformationProcess(NtCurrentProcess(),
                                       ProcessEnergyValues,
                                       &After,
                                       sizeof(After),
                                       &ReturnLength);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(ReturnLength, sizeof(After));
    if (!NT_SUCCESS(Status))
        return;

    ok_eq_ulonglong(After.DiskEnergy - Before.DiskEnergy, 0x101);
    ok_eq_ulonglong(After.NetworkTailEnergy - Before.NetworkTailEnergy, 0x102);
    ok_eq_ulonglong(After.NetworkTxRxBytes - Before.NetworkTxRxBytes, 0x103);
    ok_eq_ulonglong(After.MbbTailEnergy - Before.MbbTailEnergy, 0x104);
    ok_eq_ulonglong(After.MbbTxRxBytes - Before.MbbTxRxBytes, 0x105);
}
