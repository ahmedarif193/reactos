/*
 * PROJECT:         ReactOS Operating System
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Process component energy accounting
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>

#define NDEBUG
#include <debug.h>

C_ASSERT(sizeof(PROCESS_ENERGY_VALUES) == 432);

/* PUBLIC FUNCTIONS **********************************************************/

VOID
NTAPI
PsUpdateComponentPower(
    _In_opt_ PEPROCESS Process,
    _In_ ULONG Component,
    _In_ ULONGLONG Value)
{
    if ((Process == NULL) || (Process == PsInitialSystemProcess))
        Process = PsInitialSystemProcess;

    ASSERT((Component >= 1) && (Component <= 3));
    if ((Process == NULL) || (Value == 0) || (Component < 1) || (Component > 3))
        return;

    if (Component == 1)
    {
        InterlockedExchangeAdd64(
            (volatile LONG64 *)&Process->EnergyValues.DiskEnergy,
            (LONG64)Value);
    }
    else if (Component == 2)
    {
        InterlockedExchangeAdd64(
            (volatile LONG64 *)&Process->EnergyValues.NetworkTailEnergy,
            (LONG64)(Value >> 32));
        InterlockedExchangeAdd64(
            (volatile LONG64 *)&Process->EnergyValues.NetworkTxRxBytes,
            (LONG64)(ULONG)Value);
    }
    else
    {
        InterlockedExchangeAdd64(
            (volatile LONG64 *)&Process->EnergyValues.MbbTailEnergy,
            (LONG64)(Value >> 32));
        InterlockedExchangeAdd64(
            (volatile LONG64 *)&Process->EnergyValues.MbbTxRxBytes,
            (LONG64)(ULONG)Value);
    }
}
