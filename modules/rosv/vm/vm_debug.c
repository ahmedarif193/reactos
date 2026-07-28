/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Boot checkpoints, exit ring buffer dump, and exit statistics
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 */

#include <rosv/rosv.h>
#include <rosv/vm.h>

/* ---- Checkpoint name table ---------------------------------------------- */

static const char *g_CheckpointNames[] = {
    "DRIVER_LOADED",
    "VM_CREATED",
    "MEMORY_SET",
    "KERNEL_LOADED",
    "INITRD_LOADED",
    "VMCS_CONFIGURED",
    "VM_LAUNCHED",
    "FIRST_EXIT",
    "FIRST_IO",
    "FIRST_PRINTK",
    "SHELL_PROMPT"
};

#define CHECKPOINT_NAME_COUNT (sizeof(g_CheckpointNames) / sizeof(g_CheckpointNames[0]))

static const char *
RosvpGetCheckpointName(
    _In_ ROSV_CHECKPOINT Cp)
{
    if ((ULONG)Cp < CHECKPOINT_NAME_COUNT)
        return g_CheckpointNames[(ULONG)Cp];
    return "UNKNOWN";
}

/* ---- RosvVmSetCheckpoint ------------------------------------------------ */

VOID
RosvVmSetCheckpoint(
    _Inout_ PROSV_VM Vm,
    _In_ ROSV_CHECKPOINT Checkpoint)
{
    LARGE_INTEGER Timestamp;

    if ((ULONG)Checkpoint >= sizeof(Vm->CheckpointTimes) / sizeof(Vm->CheckpointTimes[0]))
    {
        ROSV_ERR("Invalid checkpoint index %u (max %u)",
                 (ULONG)Checkpoint,
                 (ULONG)(sizeof(Vm->CheckpointTimes) / sizeof(Vm->CheckpointTimes[0])) - 1);
        return;
    }

    KeQuerySystemTime(&Timestamp);

    Vm->Vcpu.LastCheckpoint = Checkpoint;
    Vm->CheckpointTimes[(ULONG)Checkpoint] = Timestamp;

    DbgPrint("[ROSV:CP] Checkpoint %s reached (exit_count=%llu, time=%lld)\n",
             RosvpGetCheckpointName(Checkpoint),
             Vm->Vcpu.ExitCount,
             Timestamp.QuadPart);
}

/* ---- RosvVmDumpCheckpoints ---------------------------------------------- */

VOID
RosvVmDumpCheckpoints(
    _In_ PROSV_VM Vm)
{
    ULONG i;
    LARGE_INTEGER BaseTime;

    DbgPrint("[ROSV:CP] === Checkpoint Summary (VM %u) ===\n", Vm->VmId);

    BaseTime = Vm->CheckpointTimes[0];

    for (i = 0; i < CHECKPOINT_NAME_COUNT; i++)
    {
        if (Vm->CheckpointTimes[i].QuadPart != 0)
        {
            LONGLONG DeltaUs = 0;

            if (BaseTime.QuadPart != 0)
            {
                /* System time is in 100ns units, convert to microseconds */
                DeltaUs = (Vm->CheckpointTimes[i].QuadPart - BaseTime.QuadPart) / 10;
            }

            DbgPrint("[ROSV:CP]   %-18s at +%lld us\n",
                     g_CheckpointNames[i], DeltaUs);
        }
    }

    DbgPrint("[ROSV:CP] Last checkpoint: %s, state=%d, total exits=%llu\n",
             RosvpGetCheckpointName(Vm->Vcpu.LastCheckpoint),
             Vm->State,
             Vm->Vcpu.ExitCount);
}
