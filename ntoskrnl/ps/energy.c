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

static
PPO_PROCESS_ENERGY_CONTEXT
PspGetEnergyContext(
    _In_ PEPROCESS Process)
{
    PPO_PROCESS_ENERGY_CONTEXT Context = ReadPointerAcquire((volatile PVOID *)&Process->EnergyContext);
    PPO_PROCESS_ENERGY_CONTEXT NewContext;

    if (Context != NULL)
        return Context;

    NewContext = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*NewContext), TAG_PS_ENERGY);
    if (NewContext == NULL)
        return NULL;

    Context = InterlockedCompareExchangePointer((PVOID *)&Process->EnergyContext, NewContext, NULL);
    if (Context != NULL)
    {
        ExFreePoolWithTag(NewContext, TAG_PS_ENERGY);
        return Context;
    }
    return NewContext;
}

/* PUBLIC FUNCTIONS **********************************************************/

VOID
NTAPI
PsUpdateComponentPower(
    _In_opt_ PEPROCESS Process,
    _In_ ULONG Component,
    _In_ ULONGLONG Value)
{
    PPO_PROCESS_ENERGY_CONTEXT Context;

    if ((Process == NULL) || (Process == PsInitialSystemProcess))
        Process = PsInitialSystemProcess;

    ASSERT((Component >= 1) && (Component <= 3));
    if ((Process == NULL) || (Value == 0) || (Component < 1) || (Component > 3))
        return;

    Context = PspGetEnergyContext(Process);
    if (Context == NULL)
        return;

    if (Component == 1)
    {
        InterlockedExchangeAdd64(
            (volatile LONG64 *)&Context->Values.DiskEnergy,
            (LONG64)Value);
    }
    else if (Component == 2)
    {
        InterlockedExchangeAdd64(
            (volatile LONG64 *)&Context->Values.NetworkTailEnergy,
            (LONG64)(Value >> 32));
        InterlockedExchangeAdd64(
            (volatile LONG64 *)&Context->Values.NetworkTxRxBytes,
            (LONG64)(ULONG)Value);
    }
    else
    {
        InterlockedExchangeAdd64(
            (volatile LONG64 *)&Context->Values.MbbTailEnergy,
            (LONG64)(Value >> 32));
        InterlockedExchangeAdd64(
            (volatile LONG64 *)&Context->Values.MbbTxRxBytes,
            (LONG64)(ULONG)Value);
    }
}
