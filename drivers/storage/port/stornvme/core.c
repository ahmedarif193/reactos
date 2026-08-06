/*
 * PROJECT:     ReactOS Storport NVMe miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Controller lifecycle and admin command machinery
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#include "stornvme.h"

ULONG
NvmeReadRegister(_In_ PNVME_DEVICE_EXTENSION Device, _In_ ULONG Offset)
{
    return StorPortReadRegisterUlong(Device, (PULONG)(Device->Bar0 + Offset));
}

VOID
NvmeWriteRegister(_In_ PNVME_DEVICE_EXTENSION Device, _In_ ULONG Offset, _In_ ULONG Value)
{
    StorPortWriteRegisterUlong(Device, (PULONG)(Device->Bar0 + Offset), Value);
}

VOID
NvmeWriteRegister64(_In_ PNVME_DEVICE_EXTENSION Device, _In_ ULONG Offset, _In_ ULONGLONG Value)
{
    NvmeWriteRegister(Device, Offset, (ULONG)Value);
    NvmeWriteRegister(Device, Offset + 4, (ULONG)(Value >> 32));
}

BOOLEAN
NvmeWaitReady(_In_ PNVME_DEVICE_EXTENSION Device, _In_ ULONG DesiredReady)
{
    ULONG Waited;

    for (Waited = 0; Waited < Device->TimeoutMilliseconds; Waited++)
    {
        ULONG Status = NvmeReadRegister(Device, NVME_REG_CSTS);

        if ((Status & NVME_CSTS_RDY) == DesiredReady)
            return TRUE;
        if (DesiredReady != 0 && (Status & NVME_CSTS_CFS))
            return FALSE;
        StorPortStallExecution(1000);
    }
    return FALSE;
}

/*
 * Until HwInitialize has connected interrupts everything is single-threaded,
 * so the lock helpers degrade to no-ops during early bring-up. Afterwards a
 * queue is serialized against its own interrupt vector: the per-message
 * spinlock is the same lock the kernel holds around the message ISR.
 */
VOID
NvmeAcquireLock(_In_ PNVME_DEVICE_EXTENSION Device, _In_ ULONG MessageId, _Out_ PNVME_LOCK Lock)
{
    RtlZeroMemory(Lock, sizeof(*Lock));
    if (!Device->InterruptsLive)
        return;
    if (Device->MessageInterrupts &&
        StorPortAcquireMSISpinLock(Device, MessageId, &Lock->OldIrql) == STOR_STATUS_SUCCESS)
    {
        Lock->MessageLock = TRUE;
        Lock->MessageId = MessageId;
    }
    else
    {
        StorPortAcquireSpinLock(Device, InterruptLock, NULL, &Lock->LockHandle);
    }
    Lock->Taken = TRUE;
}

VOID
NvmeReleaseLock(_In_ PNVME_DEVICE_EXTENSION Device, _Inout_ PNVME_LOCK Lock)
{
    if (!Lock->Taken)
        return;
    if (Lock->MessageLock)
        StorPortReleaseMSISpinLock(Device, Lock->MessageId, Lock->OldIrql);
    else
        StorPortReleaseSpinLock(Device, &Lock->LockHandle);
    Lock->Taken = FALSE;
}

/* Caller holds the admin domain (message 0 / interrupt lock, or early init). */
ULONG
NvmeAdminSubmitLocked(_In_ PNVME_DEVICE_EXTENSION Device,
                      _Inout_ PNVME_COMMAND Command,
                      _In_ UCHAR SlotType,
                      _In_opt_ PSCSI_REQUEST_BLOCK Srb)
{
    PNVME_QUEUE Queue = &Device->AdminQueue;
    PNVME_ADMIN_SLOT AdminSlot;
    ULONG NextTail;
    ULONG Slot;

    if (!Device->AdminReady)
        return MAXULONG;

    NextTail = (Queue->SqTail + 1) % Queue->Entries;
    if (NextTail == Queue->SqHead)
        return MAXULONG;

    for (Slot = 0; Slot < Queue->Entries; Slot++)
    {
        if (Device->AdminSlots[Slot].Type == NVME_SLOT_FREE)
            break;
    }
    if (Slot >= Queue->Entries)
        return MAXULONG;

    AdminSlot = &Device->AdminSlots[Slot];
    AdminSlot->Type = SlotType;
    AdminSlot->Done = FALSE;
    AdminSlot->Status = 0;
    AdminSlot->Dw0 = 0;
    AdminSlot->Srb = Srb;

    Command->CDW0 = (Command->CDW0 & 0xFFFF) | (Slot << 16);
    Queue->Sq[Queue->SqTail] = *Command;
    KeMemoryBarrier();
    Queue->SqTail = NextTail;
    NvmeWriteRegister(Device, Queue->SqDoorbell, Queue->SqTail);
    return Slot;
}

/*
 * Caller holds the admin domain. Slots that carry an SRB are collected into
 * the caller's batch and completed after the lock drops; internal slots are
 * handled in place.
 */
static
ULONG
NvmeProcessAdminCompletionsLocked(_In_ PNVME_DEVICE_EXTENSION Device,
                                  _Out_ PNVME_COMPLETED_SRB Batch,
                                  _In_ ULONG BatchLimit)
{
    PNVME_QUEUE Queue = &Device->AdminQueue;
    BOOLEAN Handled = FALSE;
    ULONG Collected = 0;

    if (!Device->AdminReady)
        return 0;

    while (Collected < BatchLimit)
    {
        PNVME_COMPLETION Completion = &Queue->Cq[Queue->CqHead];
        PNVME_ADMIN_SLOT AdminSlot;
        USHORT CommandId;
        USHORT Status;
        ULONG Dw0;

        if (NVME_COMPLETION_PHASE(Completion) != Queue->Phase)
            break;
        KeMemoryBarrier();

        Handled = TRUE;
        CommandId = NVME_COMPLETION_COMMAND_ID(Completion);
        Status = NVME_COMPLETION_STATUS(Completion);
        Dw0 = Completion->DW0;
        Queue->SqHead = NVME_COMPLETION_SQ_HEAD(Completion);

        Queue->CqHead = (Queue->CqHead + 1) % Queue->Entries;
        if (Queue->CqHead == 0)
            Queue->Phase ^= 1;

        if (CommandId < NVME_ADMIN_QUEUE_ENTRIES)
        {
            AdminSlot = &Device->AdminSlots[CommandId];
            switch (AdminSlot->Type)
            {
                case NVME_SLOT_SYNC:
                    AdminSlot->Status = Status;
                    AdminSlot->Dw0 = Dw0;
                    AdminSlot->Done = TRUE;
                    break;

                case NVME_SLOT_ORPHAN:
                    AdminSlot->Type = NVME_SLOT_FREE;
                    break;

                case NVME_SLOT_AER:
                    AdminSlot->Type = NVME_SLOT_FREE;
                    NvmeHandleAerLocked(Device, Dw0, Status);
                    break;

                case NVME_SLOT_SMART:
                    AdminSlot->Type = NVME_SLOT_FREE;
                    NvmeHandleSmartLocked(Device, Status);
                    break;

                case NVME_SLOT_SRB_LOG:
                case NVME_SLOT_SRB_FEATURE:
                    if (AdminSlot->Srb != NULL)
                    {
                        Batch[Collected].Srb = AdminSlot->Srb;
                        Batch[Collected].Status = Status;
                        Batch[Collected].SlotType = AdminSlot->Type;
                        Collected++;
                    }
                    AdminSlot->Srb = NULL;
                    AdminSlot->Type = NVME_SLOT_FREE;
                    break;

                default:
                    break;
            }
        }
    }

    if (Handled)
        NvmeWriteRegister(Device, Queue->CqDoorbell, Queue->CqHead);
    return Collected;
}

VOID
NvmeDrainAdminQueue(_In_ PNVME_DEVICE_EXTENSION Device)
{
    NVME_COMPLETED_SRB Batch[NVME_COMPLETION_BATCH];
    NVME_LOCK Lock;
    ULONG Collected;
    ULONG Index;

    do
    {
        NvmeAcquireLock(Device, 0, &Lock);
        Collected = NvmeProcessAdminCompletionsLocked(Device, Batch, NVME_COMPLETION_BATCH);
        NvmeReleaseLock(Device, &Lock);

        for (Index = 0; Index < Collected; Index++)
        {
            if (Batch[Index].SlotType == NVME_SLOT_SRB_LOG)
                NvmeCompleteLogSenseSrb(Device, Batch[Index].Srb, Batch[Index].Status);
            else
                NvmeCompleteFeatureSrb(Device, Batch[Index].Srb, Batch[Index].Status);
        }
    } while (Collected == NVME_COMPLETION_BATCH);
}

/*
 * Synchronous admin command: submit, then poll the shared drain until the
 * slot completes. The interrupt handler may consume the completion first;
 * the Done flag hands the result over either way.
 */
BOOLEAN
NvmeAdminCommandSync(_In_ PNVME_DEVICE_EXTENSION Device,
                     _Inout_ PNVME_COMMAND Command,
                     _Out_opt_ PULONG Result)
{
    NVME_LOCK Lock;
    ULONG Slot;
    ULONG Waited;
    BOOLEAN Done = FALSE;
    USHORT Status = 0;
    ULONG Dw0 = 0;

    NvmeAcquireLock(Device, 0, &Lock);
    Slot = NvmeAdminSubmitLocked(Device, Command, NVME_SLOT_SYNC, NULL);
    NvmeReleaseLock(Device, &Lock);
    if (Slot == MAXULONG)
        return FALSE;

    for (Waited = 0; Waited < Device->TimeoutMilliseconds; Waited++)
    {
        NvmeDrainAdminQueue(Device);
        NvmeAcquireLock(Device, 0, &Lock);
        if (Device->AdminSlots[Slot].Done)
        {
            Done = TRUE;
            Status = Device->AdminSlots[Slot].Status;
            Dw0 = Device->AdminSlots[Slot].Dw0;
            Device->AdminSlots[Slot].Type = NVME_SLOT_FREE;
        }
        NvmeReleaseLock(Device, &Lock);
        if (Done)
            break;
        StorPortStallExecution(1000);
    }

    if (!Done)
    {
        NvmeAcquireLock(Device, 0, &Lock);
        if (Device->AdminSlots[Slot].Done)
        {
            Status = Device->AdminSlots[Slot].Status;
            Dw0 = Device->AdminSlots[Slot].Dw0;
            Device->AdminSlots[Slot].Type = NVME_SLOT_FREE;
            Done = TRUE;
        }
        else
        {
            Device->AdminSlots[Slot].Type = NVME_SLOT_ORPHAN;
        }
        NvmeReleaseLock(Device, &Lock);
        if (!Done)
        {
            DPRINT1("stornvme: admin command %02lx timed out\n", Command->CDW0 & 0xFF);
            return FALSE;
        }
    }

    if (Result)
        *Result = Dw0;
    if (Status != 0)
    {
        DPRINT1("stornvme: admin command %02lx failed, status 0x%04x\n", Command->CDW0 & 0xFF, Status);
        return FALSE;
    }
    return TRUE;
}

static
VOID
NvmeCopyIdentifyString(_Out_ PUCHAR Target, _In_ PUCHAR Source, _In_ ULONG Length)
{
    ULONG Index;

    RtlCopyMemory(Target, Source, Length);
    Target[Length] = 0;
    for (Index = Length; Index > 0 && Target[Index - 1] == ' '; Index--)
        Target[Index - 1] = 0;
}

static
VOID
NvmeResetAdminState(_In_ PNVME_DEVICE_EXTENSION Device)
{
    PNVME_QUEUE Queue = &Device->AdminQueue;

    RtlZeroMemory(Queue->Sq, Queue->Entries * sizeof(NVME_COMMAND));
    RtlZeroMemory(Queue->Cq, Queue->Entries * sizeof(NVME_COMPLETION));
    RtlZeroMemory(Device->AdminSlots, sizeof(Device->AdminSlots));
    Queue->SqTail = 0;
    Queue->SqHead = 0;
    Queue->CqHead = 0;
    Queue->Phase = 1;
    Device->AerOutstanding = FALSE;
    Device->SmartInFlight = FALSE;
}

static
BOOLEAN
NvmeIdentifyController(_In_ PNVME_DEVICE_EXTENSION Device)
{
    PNVME_IDENTIFY_CONTROLLER Identify = (PNVME_IDENTIFY_CONTROLLER)Device->IdentifyBuffer;
    NVME_COMMAND Command;
    ULONG EntrySizeMin;
    ULONG EntrySizeMax;
    ULONG Mdts;
    ULONG Result;

    RtlZeroMemory(Device->IdentifyBuffer, PAGE_SIZE);
    RtlZeroMemory(&Command, sizeof(Command));
    NVME_COMMAND_SET_OPCODE(&Command, NVME_ADMIN_IDENTIFY, 0);
    Command.PRP1 = Device->IdentifyPhysical;
    Command.CDW10 = NVME_CNS_CONTROLLER;
    if (!NvmeAdminCommandSync(Device, &Command, NULL))
        return FALSE;

    EntrySizeMin = Identify->SQES & 0xF;
    EntrySizeMax = Identify->SQES >> 4;
    if (EntrySizeMin > 6 || EntrySizeMax < 6)
    {
        DPRINT1("stornvme: unsupported submission entry size range %lu..%lu\n", EntrySizeMin, EntrySizeMax);
        return FALSE;
    }
    EntrySizeMin = Identify->CQES & 0xF;
    EntrySizeMax = Identify->CQES >> 4;
    if (EntrySizeMin > 4 || EntrySizeMax < 4)
    {
        DPRINT1("stornvme: unsupported completion entry size range %lu..%lu\n", EntrySizeMin, EntrySizeMax);
        return FALSE;
    }

    NvmeCopyIdentifyString(Device->Serial, Identify->SN, 20);
    NvmeCopyIdentifyString(Device->Model, Identify->MN, 40);
    NvmeCopyIdentifyString(Device->Firmware, Identify->FR, 8);
    Device->Oncs = Identify->ONCS;
    Device->Oaes = Identify->OAES;

    Mdts = Identify->MDTS;
    Device->MaximumTransferLength = NVME_MAX_TRANSFER;
    if (Mdts != 0 && Mdts < 32 - PAGE_SHIFT && (PAGE_SIZE << Mdts) < Device->MaximumTransferLength)
        Device->MaximumTransferLength = PAGE_SIZE << Mdts;

    Device->VolatileWriteCache = (Identify->VWC & 1) != 0;
    Device->WriteCacheEnabled = FALSE;
    if (Device->VolatileWriteCache)
    {
        RtlZeroMemory(&Command, sizeof(Command));
        NVME_COMMAND_SET_OPCODE(&Command, NVME_ADMIN_GET_FEATURES, 0);
        Command.CDW10 = NVME_FEATURE_VOLATILE_WC;
        if (NvmeAdminCommandSync(Device, &Command, &Result))
            Device->WriteCacheEnabled = (Result & 1) != 0;
        else
            Device->WriteCacheEnabled = TRUE;
    }

    NvmeParsePowerCapabilities(Device, Identify);

    DPRINT1("stornvme: controller '%s' serial '%s' firmware '%s', %lu namespaces\n",
            Device->Model, Device->Serial, Device->Firmware, Identify->NN);
    return TRUE;
}

static
BOOLEAN
NvmeNegotiateQueueCount(_In_ PNVME_DEVICE_EXTENSION Device)
{
    NVME_COMMAND Command;
    ULONG Want = Device->IoQueueCount;
    ULONG Granted;
    ULONG Result;

    RtlZeroMemory(&Command, sizeof(Command));
    NVME_COMMAND_SET_OPCODE(&Command, NVME_ADMIN_SET_FEATURES, 0);
    Command.CDW10 = NVME_FEATURE_NUMBER_OF_QUEUES;
    Command.CDW11 = ((Want - 1) << 16) | (Want - 1);
    if (!NvmeAdminCommandSync(Device, &Command, &Result))
        return FALSE;

    Granted = min((Result & 0xFFFF), (Result >> 16)) + 1;
    if (Granted < Device->IoQueueCount)
        Device->IoQueueCount = Granted;
    if (Device->IoQueueCount == 0)
        Device->IoQueueCount = 1;
    return TRUE;
}

static
BOOLEAN
NvmeScanNamespace(_In_ PNVME_DEVICE_EXTENSION Device, _In_ ULONG Nsid)
{
    PNVME_IDENTIFY_NAMESPACE Identify = (PNVME_IDENTIFY_NAMESPACE)Device->IdentifyBuffer;
    PNVME_NAMESPACE Namespace;
    NVME_COMMAND Command;
    ULONG FormatIndex;
    UCHAR Lbads;

    if (Device->NamespaceCount >= NVME_MAX_NAMESPACES)
        return FALSE;

    RtlZeroMemory(Device->IdentifyBuffer, PAGE_SIZE);
    RtlZeroMemory(&Command, sizeof(Command));
    NVME_COMMAND_SET_OPCODE(&Command, NVME_ADMIN_IDENTIFY, 0);
    Command.NSID = Nsid;
    Command.PRP1 = Device->IdentifyPhysical;
    Command.CDW10 = NVME_CNS_NAMESPACE;
    if (!NvmeAdminCommandSync(Device, &Command, NULL) || Identify->NSZE == 0)
        return FALSE;

    FormatIndex = Identify->FLBAS & 0xF;
    if (FormatIndex > Identify->NLBAF || (Identify->FLBAS & 0x10) != 0 ||
        Identify->LBAF[FormatIndex].MS != 0 || (Identify->DPS & 0x7) != 0)
    {
        DPRINT1("stornvme: namespace %lu uses unsupported metadata or protection information\n", Nsid);
        return FALSE;
    }
    Lbads = Identify->LBAF[FormatIndex].LBADS;
    if (Lbads < NVME_MIN_BLOCK_SHIFT || Lbads > NVME_MAX_BLOCK_SHIFT)
    {
        DPRINT1("stornvme: namespace %lu has unsupported LBA shift %u\n", Nsid, Lbads);
        return FALSE;
    }

    Namespace = &Device->Namespaces[Device->NamespaceCount];
    Namespace->Nsid = Nsid;
    Namespace->Blocks = Identify->NSZE;
    Namespace->BlockShift = Lbads;
    Namespace->BlockSize = 1UL << Lbads;
    RtlCopyMemory(Namespace->Eui64, Identify->EUI64, sizeof(Namespace->Eui64));
    RtlCopyMemory(Namespace->Nguid, Identify->NGUID, sizeof(Namespace->Nguid));
    Namespace->Ready = TRUE;
    Device->NamespaceCount++;
    DPRINT1("stornvme: namespace %lu: %I64u blocks of %lu bytes\n", Nsid, Namespace->Blocks, Namespace->BlockSize);
    return TRUE;
}

static
VOID
NvmeScanNamespaces(_In_ PNVME_DEVICE_EXTENSION Device, _In_ ULONG NamespaceCount)
{
    NVME_COMMAND Command;
    ULONG NsidList[NVME_MAX_NAMESPACES];
    ULONG Found = 0;
    ULONG Index;

    Device->NamespaceCount = 0;
    RtlZeroMemory(Device->Namespaces, sizeof(Device->Namespaces));
    if (NamespaceCount == 0)
    {
        DPRINT1("stornvme: no active namespaces\n");
        return;
    }

    RtlZeroMemory(Device->IdentifyBuffer, PAGE_SIZE);
    RtlZeroMemory(&Command, sizeof(Command));
    NVME_COMMAND_SET_OPCODE(&Command, NVME_ADMIN_IDENTIFY, 0);
    Command.PRP1 = Device->IdentifyPhysical;
    Command.CDW10 = NVME_CNS_ACTIVE_NS;
    if (NvmeAdminCommandSync(Device, &Command, NULL))
    {
        PULONG List = (PULONG)Device->IdentifyBuffer;

        for (Index = 0; Index < NVME_MAX_NAMESPACES && List[Index] != 0; Index++)
            NsidList[Found++] = List[Index];
    }
    if (Found == 0)
    {
        /* NVMe 1.0 controllers lack the active list; probe sequentially. */
        for (Index = 1; Index <= NamespaceCount && Found < NVME_MAX_NAMESPACES; Index++)
            NsidList[Found++] = Index;
    }

    for (Index = 0; Index < Found; Index++)
        NvmeScanNamespace(Device, NsidList[Index]);
}

BOOLEAN
NvmeStartController(_In_ PNVME_DEVICE_EXTENSION Device, _In_ BOOLEAN FirstStart)
{
    ULONG NamespaceCount = 0;

    Device->ControllerStarted = FALSE;
    Device->AdminReady = FALSE;

    NvmeWriteRegister(Device, NVME_REG_INTMS, 0xFFFFFFFF);
    NvmeWriteRegister(Device, NVME_REG_CC, 0);
    if (!NvmeWaitReady(Device, 0))
    {
        DPRINT1("stornvme: controller did not leave ready state\n");
        return FALSE;
    }

    NvmeResetAdminState(Device);
    NvmeWriteRegister(Device, NVME_REG_AQA,
                      ((Device->AdminQueue.Entries - 1) << 16) | (Device->AdminQueue.Entries - 1));
    NvmeWriteRegister64(Device, NVME_REG_ASQ, Device->AdminQueue.SqPhysical);
    NvmeWriteRegister64(Device, NVME_REG_ACQ, Device->AdminQueue.CqPhysical);
    NvmeWriteRegister(Device, NVME_REG_CC, NVME_CC_CSS_NVM | NVME_CC_IOSQES | NVME_CC_IOCQES | NVME_CC_EN);
    if (!NvmeWaitReady(Device, NVME_CSTS_RDY))
    {
        DPRINT1("stornvme: controller did not become ready\n");
        return FALSE;
    }
    Device->AdminReady = TRUE;

    if (FirstStart)
    {
        if (!NvmeIdentifyController(Device))
            goto Failure;
        NamespaceCount = ((PNVME_IDENTIFY_CONTROLLER)Device->IdentifyBuffer)->NN;
    }

    if (!NvmeNegotiateQueueCount(Device))
        goto Failure;

    NvmeConfigurePowerAndEvents(Device);

    if (FirstStart)
        NvmeScanNamespaces(Device, NamespaceCount);

    Device->ControllerStarted = TRUE;
    return TRUE;

Failure:
    Device->AdminReady = FALSE;
    NvmeWriteRegister(Device, NVME_REG_CC, 0);
    NvmeWaitReady(Device, 0);
    return FALSE;
}

VOID
NvmeDisableController(_In_ PNVME_DEVICE_EXTENSION Device)
{
    Device->ControllerStarted = FALSE;
    Device->AdminReady = FALSE;
    NvmeWriteRegister(Device, NVME_REG_INTMS, 0xFFFFFFFF);
    NvmeWriteRegister(Device, NVME_REG_CC, 0);
    NvmeWaitReady(Device, 0);
}

VOID
NvmeShutdownController(_In_ PNVME_DEVICE_EXTENSION Device)
{
    ULONG Waited;
    ULONG Index;

    Device->ControllerStarted = FALSE;
    Device->AdminReady = FALSE;
    for (Index = 0; Index < Device->IoQueueCount; Index++)
        NvmeRetireQueue(Device, &Device->IoQueues[Index], SRB_STATUS_NO_DEVICE);
    NvmeWriteRegister(Device, NVME_REG_CC,
                      (NvmeReadRegister(Device, NVME_REG_CC) & ~NVME_CC_SHN_MASK) | NVME_CC_SHN_NORMAL);
    for (Waited = 0; Waited < 5000; Waited++)
    {
        if ((NvmeReadRegister(Device, NVME_REG_CSTS) & NVME_CSTS_SHST_MASK) == NVME_CSTS_SHST_DONE)
            break;
        StorPortStallExecution(1000);
    }
}

BOOLEAN
NvmeResetController(_In_ PNVME_DEVICE_EXTENSION Device)
{
    PSCSI_REQUEST_BLOCK AdminSrbs[NVME_ADMIN_QUEUE_ENTRIES];
    NVME_LOCK Lock;
    ULONG AdminSrbCount = 0;
    ULONG Index;

    DPRINT1("stornvme: controller reset\n");
    NvmeDisableController(Device);

    for (Index = 0; Index < Device->IoQueueCount; Index++)
        NvmeRetireQueue(Device, &Device->IoQueues[Index], SRB_STATUS_BUS_RESET);

    NvmeAcquireLock(Device, 0, &Lock);
    for (Index = 0; Index < NVME_ADMIN_QUEUE_ENTRIES; Index++)
    {
        PNVME_ADMIN_SLOT AdminSlot = &Device->AdminSlots[Index];

        if (AdminSlot->Type == NVME_SLOT_SRB_LOG || AdminSlot->Type == NVME_SLOT_SRB_FEATURE)
        {
            if (AdminSlot->Srb != NULL)
                AdminSrbs[AdminSrbCount++] = AdminSlot->Srb;
            AdminSlot->Srb = NULL;
            AdminSlot->Type = NVME_SLOT_FREE;
        }
    }
    NvmeReleaseLock(Device, &Lock);
    for (Index = 0; Index < AdminSrbCount; Index++)
        NvmeCompleteSrb(Device, AdminSrbs[Index], SRB_STATUS_BUS_RESET);

    if (!NvmeStartController(Device, FALSE))
        return FALSE;
    if (!NvmeCreateIoQueues(Device))
        return FALSE;

    NvmeWriteRegister(Device, NVME_REG_INTMC, 0xFFFFFFFF);

    NvmeAcquireLock(Device, 0, &Lock);
    NvmeArmAerLocked(Device);
    NvmeKickSmartLocked(Device);
    NvmeReleaseLock(Device, &Lock);
    return TRUE;
}
