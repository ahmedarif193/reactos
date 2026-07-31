/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     eMMC CMD6 SWITCH helpers, partition switching, SANITIZE hook,
 *              per-partition child PDO enumeration.
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "sdbus.h"

#define NDEBUG
#include <debug.h>

#define EMMC_CMD6_ARG_ACCESS_SHIFT   24
#define EMMC_CMD6_ARG_INDEX_SHIFT    16
#define EMMC_CMD6_ARG_VALUE_SHIFT    8
#define EMMC_CMD6_ARG_CMDSET_MASK    0x07U

#define EMMC_SANITIZE_TIMEOUT_MS     240000UL

#define EMMC_BOOT_SIZE_UNIT_BYTES    (128ULL * 1024ULL)
#define EMMC_RPMB_SIZE_UNIT_BYTES    (128ULL * 1024ULL)
#define EMMC_GP_SIZE_UNIT_BYTES      (512ULL * 1024ULL)
#define EMMC_EXT_CSD_PARTITION_SETTING_COMPLETED 155
#define EMMC_EXT_CSD_HC_WP_GRP_SIZE  221
#define EMMC_EXT_CSD_HC_ERASE_GRP_SIZE 224
#define EMMC_PARTITION_SETTING_COMPLETED 0x01
#define EMMC_PARTITION_SUPPORT_ENABLED 0x01

typedef struct _SDBUS_EMMC_PARTITION_INFO
{
    UCHAR PartitionId;
    ULONGLONG SizeBytes;
} SDBUS_EMMC_PARTITION_INFO, *PSDBUS_EMMC_PARTITION_INFO;

static __inline ULONG
SdBusEmmcBuildSwitchArgument(
    _In_ UCHAR Access,
    _In_ UCHAR Index,
    _In_ UCHAR Value,
    _In_ UCHAR CmdSet)
{
    ULONG Arg;

    Arg  = ((ULONG)(Access & 0x3U)) << EMMC_CMD6_ARG_ACCESS_SHIFT;
    Arg |= ((ULONG)Index)           << EMMC_CMD6_ARG_INDEX_SHIFT;
    Arg |= ((ULONG)Value)           << EMMC_CMD6_ARG_VALUE_SHIFT;
    Arg |= ((ULONG)(CmdSet & EMMC_CMD6_ARG_CMDSET_MASK));
    return Arg;
}

NTSTATUS
SdBusEmmcWaitReadyByRca(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG Rca,
    _In_ ULONG TimeoutMs)
{
    ULONG Status;
    ULONG Retry;
    NTSTATUS CmdStatus;
    NTSTATUS LastStatus = STATUS_IO_TIMEOUT;
    LARGE_INTEGER Delay;

    Delay.QuadPart = -10000LL;

    for (Retry = 0; Retry < TimeoutMs; Retry++)
    {
        Status = 0;
        CmdStatus = SdBusSendCommand(FdoExtension,
                                     SDCMD_SEND_STATUS,
                                     Rca << 16,
                                     SDHCI_CMD_RESP_48 |
                                         SDHCI_CMD_CRC_CHECK |
                                         SDHCI_CMD_INDEX_CHECK,
                                     &Status);
        if (!NT_SUCCESS(CmdStatus))
        {
            LastStatus = CmdStatus;
            if (CmdStatus == STATUS_SD_CMD_CRC_ERROR ||
                CmdStatus == STATUS_SD_CMD_TIMEOUT ||
                CmdStatus == STATUS_IO_TIMEOUT)
            {
                goto DelayAndRetry;
            }
            return CmdStatus;
        }

        CmdStatus = SdBusR1Status(Status);
        if (!NT_SUCCESS(CmdStatus))
        {
            return CmdStatus;
        }

        if (Status & EMMC_STATUS_SWITCH_ERROR)
        {
            DPRINT1("SdBusEmmcWaitReadyByRca: SWITCH_ERROR set in status 0x%08lx\n",
                    Status);
            return STATUS_INVALID_PARAMETER;
        }

        if (Status & EMMC_STATUS_READY_FOR_DATA)
        {
            ULONG State = (Status & EMMC_STATUS_CURRENT_STATE_MASK) >>
                          EMMC_STATUS_CURRENT_STATE_SHIFT;
            if (State == EMMC_STATE_TRAN)
            {
                return STATUS_SUCCESS;
            }
        }

DelayAndRetry:
        if (KeGetCurrentIrql() <= APC_LEVEL)
        {
            KeDelayExecutionThread(KernelMode, FALSE, &Delay);
        }
        else
        {
            KeStallExecutionProcessor(1000);
        }
    }

    DPRINT1("SdBusEmmcWaitReadyByRca: timed out after %lu ms (RCA=0x%04lX)\n",
            TimeoutMs, Rca);
    return LastStatus;
}

NTSTATUS
SdBusEmmcSwitchByRca(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG Rca,
    _In_ UCHAR Access,
    _In_ UCHAR Index,
    _In_ UCHAR Value,
    _In_ UCHAR CmdSet,
    _In_ ULONG TimeoutMs)
{
    ULONG Argument;
    ULONG Response;
    NTSTATUS Status;

    if (FdoExtension == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Rca == 0)
    {
        DPRINT1("SdBusEmmcSwitchByRca: invalid RCA 0\n");
        return STATUS_INVALID_PARAMETER;
    }

    if (Access > EMMC_SWITCH_ACCESS_WRITE_BYTE)
    {
        DPRINT1("SdBusEmmcSwitchByRca: invalid access mode %u\n", Access);
        return STATUS_INVALID_PARAMETER;
    }

    Argument = SdBusEmmcBuildSwitchArgument(Access, Index, Value, CmdSet);

    DPRINT("SdBusEmmcSwitchByRca: RCA=0x%04lX Access=%u Index=%u Value=0x%02X CmdSet=%u Arg=0x%08lx\n",
           Rca, Access, Index, Value, CmdSet, Argument);

    Status = SdBusSendCommand(FdoExtension,
                              SDCMD_SWITCH_FUNC,
                              Argument,
                              SDHCI_CMD_RESP_48_BUSY |
                                  SDHCI_CMD_CRC_CHECK |
                                  SDHCI_CMD_INDEX_CHECK,
                              &Response);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusEmmcSwitchByRca: CMD6 failed (0x%08lx)\n", Status);
        return Status;
    }

    Status = SdBusR1Status(Response);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (TimeoutMs == 0)
    {
        return STATUS_SUCCESS;
    }

    Status = SdBusEmmcWaitReadyByRca(FdoExtension, Rca, TimeoutMs);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusEmmcSwitchByRca: post-switch CMD13 poll failed (0x%08lx)\n",
                Status);
    }
    return Status;
}

NTSTATUS
SdBusEmmcSwitch(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR Access,
    _In_ UCHAR Index,
    _In_ UCHAR Value,
    _In_ UCHAR CmdSet)
{
    PPDO_EXTENSION TargetPdo = NULL;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    ULONG Rca = 0;

    if (FdoExtension == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
    for (Entry = FdoExtension->ChildPdoList.Flink;
         Entry != &FdoExtension->ChildPdoList;
         Entry = Entry->Flink)
    {
        PPDO_EXTENSION Candidate = CONTAINING_RECORD(Entry, PDO_EXTENSION, ListEntry);
        if (!Candidate->Present || Candidate->IsEmmcPartition)
        {
            continue;
        }
        if (Candidate->CardType == SdCardTypeEmmc ||
            Candidate->CardType == SdCardTypeMmc)
        {
            TargetPdo = Candidate;
            Rca = Candidate->RelativeAddress;
            break;
        }
    }
    KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

    if (TargetPdo == NULL)
    {
        DPRINT1("SdBusEmmcSwitch: no eMMC card PDO found on FDO %p\n", FdoExtension);
        return STATUS_DEVICE_NOT_READY;
    }

    return SdBusEmmcSwitchByRca(FdoExtension,
                                Rca,
                                Access,
                                Index,
                                Value,
                                CmdSet,
                                SD_DATA_TIMEOUT_MS);
}

NTSTATUS
SdBusEmmcSelectPartition(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PPDO_EXTENSION PdoExtension,
    _In_ UCHAR PartitionId)
{
    UCHAR OldConfig;
    UCHAR NewConfig;
    NTSTATUS Status;

    if (FdoExtension == NULL || PdoExtension == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (PartitionId > EMMC_PARTITION_GPP4)
    {
        DPRINT1("SdBusEmmcSelectPartition: invalid partition id %u\n", PartitionId);
        return STATUS_INVALID_PARAMETER;
    }

    if (PdoExtension->CardType != SdCardTypeEmmc &&
        PdoExtension->CardType != SdCardTypeMmc)
    {
        DPRINT1("SdBusEmmcSelectPartition: PDO is not an eMMC (type=%d)\n",
                PdoExtension->CardType);
        return STATUS_NOT_SUPPORTED;
    }

    OldConfig = FdoExtension->EmmcPartitionConfigValid ?
        FdoExtension->EmmcPartitionConfig :
        PdoExtension->ExtCsd[EMMC_EXT_CSD_PARTITION_CONFIG];
    NewConfig = (UCHAR)((OldConfig & ~EMMC_PARTITION_ACCESS_MASK) |
                        (PartitionId & EMMC_PARTITION_ACCESS_MASK));
    if (NewConfig == OldConfig)
    {
        return STATUS_SUCCESS;
    }

    Status = SdBusEmmcSwitchByRca(FdoExtension,
                                  PdoExtension->RelativeAddress,
                                  EMMC_SWITCH_ACCESS_WRITE_BYTE,
                                  (UCHAR)EMMC_EXT_CSD_PARTITION_CONFIG,
                                  NewConfig,
                                  0,
                                  SD_DATA_TIMEOUT_MS);
    if (NT_SUCCESS(Status))
    {
        PLIST_ENTRY Entry;
        KIRQL OldIrql;

        FdoExtension->EmmcPartitionConfig = NewConfig;
        FdoExtension->EmmcPartitionConfigValid = TRUE;
        PdoExtension->ExtCsd[EMMC_EXT_CSD_PARTITION_CONFIG] = NewConfig;

        KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
        for (Entry = FdoExtension->ChildPdoList.Flink;
             Entry != &FdoExtension->ChildPdoList;
             Entry = Entry->Flink)
        {
            PPDO_EXTENSION Child;

            Child = CONTAINING_RECORD(Entry, PDO_EXTENSION, ListEntry);
            if ((Child->CardType == SdCardTypeEmmc ||
                 Child->CardType == SdCardTypeMmc) &&
                Child->RelativeAddress == PdoExtension->RelativeAddress)
            {
                Child->ExtCsd[EMMC_EXT_CSD_PARTITION_CONFIG] = NewConfig;
            }
        }
        KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);
    }

    return Status;
}

NTSTATUS
SdBusEmmcSanitize(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PPDO_EXTENSION PdoExtension)
{
    NTSTATUS Status;
    ULONG Rca;

    if (FdoExtension == NULL || PdoExtension == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (PdoExtension->CardType != SdCardTypeEmmc &&
        PdoExtension->CardType != SdCardTypeMmc)
    {
        DPRINT1("SdBusEmmcSanitize: PDO is not an eMMC (type=%d)\n",
                PdoExtension->CardType);
        return STATUS_NOT_SUPPORTED;
    }

    Rca = PdoExtension->RelativeAddress;

    DPRINT1("SdBusEmmcSanitize: issuing SANITIZE_START (RCA=0x%04lX)\n", Rca);

    Status = SdBusEmmcSwitchByRca(FdoExtension,
                                  Rca,
                                  EMMC_SWITCH_ACCESS_WRITE_BYTE,
                                  (UCHAR)EMMC_EXT_CSD_SANITIZE_START,
                                  1,
                                  0,
                                  EMMC_SANITIZE_TIMEOUT_MS);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusEmmcSanitize: CMD6 SANITIZE failed (0x%08lx)\n", Status);
    }
    else
    {
        DPRINT1("SdBusEmmcSanitize: SANITIZE completed successfully\n");
    }
    return Status;
}

NTSTATUS
SdBusEmmcEnableBkops(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PPDO_EXTENSION PdoExtension)
{
    if (FdoExtension == NULL || PdoExtension == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    return SdBusEmmcSwitch(FdoExtension,
                           EMMC_SWITCH_ACCESS_WRITE_BYTE,
                           (UCHAR)EMMC_EXT_CSD_BKOPS_EN,
                           1,
                           0);
}

NTSTATUS
SdBusEmmcEnableCache(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PPDO_EXTENSION PdoExtension)
{
    if (FdoExtension == NULL || PdoExtension == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    return SdBusEmmcSwitch(FdoExtension,
                           EMMC_SWITCH_ACCESS_WRITE_BYTE,
                           (UCHAR)EMMC_EXT_CSD_CACHE_CTRL,
                           1,
                           0);
}

static NTSTATUS
SdBusCreateEmmcPartitionPdo(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PPDO_EXTENSION MainPdo,
    _In_ UCHAR PartitionId,
    _In_ ULONGLONG PartitionSize,
    _Out_ PDEVICE_OBJECT *CreatedPdo)
{
    PDEVICE_OBJECT Pdo;
    PPDO_EXTENSION PdoExtension;
    NTSTATUS Status;

    *CreatedPdo = NULL;

    Status = IoCreateDevice(FdoExtension->Common.Self->DriverObject,
                            sizeof(PDO_EXTENSION),
                            NULL,
                            FILE_DEVICE_MASS_STORAGE,
                            FILE_AUTOGENERATED_DEVICE_NAME,
                            FALSE,
                            &Pdo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusCreateEmmcPartitionPdo: IoCreateDevice failed (0x%08lx)\n",
                Status);
        return Status;
    }

    PdoExtension = (PPDO_EXTENSION)Pdo->DeviceExtension;
    RtlZeroMemory(PdoExtension, sizeof(PDO_EXTENSION));

    PdoExtension->Common.Self = Pdo;
    PdoExtension->Common.IsFdo = FALSE;
    PdoExtension->Common.DeviceState = SdBusDeviceStateStopped;
    PdoExtension->Common.DevicePowerState = PowerDeviceD0;

    PdoExtension->FdoExtension = FdoExtension;
    PdoExtension->Present = TRUE;
    PdoExtension->ReportedMissing = FALSE;
    PdoExtension->Started = FALSE;
    PdoExtension->CardType = MainPdo->CardType;
    PdoExtension->Cid = MainPdo->Cid;
    PdoExtension->Csd = MainPdo->Csd;
    RtlCopyMemory(PdoExtension->ExtCsd, MainPdo->ExtCsd, sizeof(PdoExtension->ExtCsd));
    PdoExtension->RelativeAddress = MainPdo->RelativeAddress;
    PdoExtension->HighCapacity = MainPdo->HighCapacity;
    PdoExtension->WriteProtected = MainPdo->WriteProtected;
    PdoExtension->InsertionGeneration = MainPdo->InsertionGeneration;

    PdoExtension->IsEmmcPartition = TRUE;
    PdoExtension->EmmcPartitionId = PartitionId;
    PdoExtension->EmmcPartitionSizeBytes = PartitionSize;
    PdoExtension->BytesPerSector = SD_DEFAULT_BLOCK_SIZE;
    PdoExtension->TotalSectors = PartitionSize / SD_DEFAULT_BLOCK_SIZE;

    InitializeListHead(&PdoExtension->ListEntry);
    IoInitializeRemoveLock(&PdoExtension->RemoveLock, TAG_SDBUS, 0, 0);

    Pdo->Flags |= DO_DIRECT_IO;
    if (FdoExtension->Common.PowerPagable)
    {
        Pdo->Flags |= DO_POWER_PAGABLE;
    }
    SdBusInitializeDeviceUsage(&PdoExtension->Common,
                               FdoExtension->Common.PowerPagable);
    *CreatedPdo = Pdo;
    return STATUS_SUCCESS;
}

static __inline ULONG
SdBusEmmcRead24Le(
    _In_reads_(3) const UCHAR *Bytes)
{
    return ((ULONG)Bytes[0]) |
           (((ULONG)Bytes[1]) << 8) |
           (((ULONG)Bytes[2]) << 16);
}

NTSTATUS
SdBusEmmcEnumeratePartitions(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PPDO_EXTENSION MainPdo)
{
    const UCHAR *ExtCsd;
    SDBUS_EMMC_PARTITION_INFO PartitionInfo[7];
    PDEVICE_OBJECT PartitionPdos[7];
    ULONG PartitionCount = 0;
    ULONG CreatedCount = 0;
    ULONG BootSizeMult;
    ULONG RpmbSizeMult;
    ULONG GpSizeMult;
    ULONG HcEraseGroupSize;
    ULONG HcWriteProtectGroupSize;
    ULONGLONG PartSizeBytes;
    UCHAR PartitioningSupport;
    UCHAR Partition;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    NTSTATUS Status;

    if (FdoExtension == NULL || MainPdo == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (MainPdo->CardType != SdCardTypeEmmc &&
        MainPdo->CardType != SdCardTypeMmc)
    {
        return STATUS_SUCCESS;
    }

    ExtCsd = MainPdo->ExtCsd;
    RtlZeroMemory(PartitionInfo, sizeof(PartitionInfo));
    RtlZeroMemory(PartitionPdos, sizeof(PartitionPdos));
    PartitioningSupport = ExtCsd[EMMC_EXT_CSD_PARTITIONING_SUPPORT];
    BootSizeMult = ExtCsd[EMMC_EXT_CSD_BOOT_SIZE_MULT_OFFSET];
    RpmbSizeMult = ExtCsd[EMMC_EXT_CSD_RPMB_SIZE_MULT_OFFSET];
    HcEraseGroupSize = ExtCsd[EMMC_EXT_CSD_HC_ERASE_GRP_SIZE];
    HcWriteProtectGroupSize = ExtCsd[EMMC_EXT_CSD_HC_WP_GRP_SIZE];

    DPRINT1("SdBusEmmcEnumeratePartitions: PARTITIONING_SUPPORT=0x%02X BOOT=%lu RPMB=%lu\n",
            PartitioningSupport, BootSizeMult, RpmbSizeMult);

    if (BootSizeMult > 0)
    {
        PartSizeBytes = (ULONGLONG)BootSizeMult * EMMC_BOOT_SIZE_UNIT_BYTES;
        PartitionInfo[PartitionCount].PartitionId = EMMC_PARTITION_BOOT0;
        PartitionInfo[PartitionCount++].SizeBytes = PartSizeBytes;
        PartitionInfo[PartitionCount].PartitionId = EMMC_PARTITION_BOOT1;
        PartitionInfo[PartitionCount++].SizeBytes = PartSizeBytes;
    }

    if (RpmbSizeMult > 0)
    {
        PartSizeBytes = (ULONGLONG)RpmbSizeMult * EMMC_RPMB_SIZE_UNIT_BYTES;
        PartitionInfo[PartitionCount].PartitionId = EMMC_PARTITION_RPMB;
        PartitionInfo[PartitionCount++].SizeBytes = PartSizeBytes;
    }

    if ((PartitioningSupport & EMMC_PARTITION_SUPPORT_ENABLED) &&
        (ExtCsd[EMMC_EXT_CSD_PARTITION_SETTING_COMPLETED] &
         EMMC_PARTITION_SETTING_COMPLETED) != 0)
    {
        for (Partition = 0; Partition < 4; Partition++)
        {
            const UCHAR *Slot =
                &ExtCsd[EMMC_EXT_CSD_GP_SIZE_MULT_OFFSET + (Partition * 3)];
            GpSizeMult = SdBusEmmcRead24Le(Slot);
            if (GpSizeMult == 0)
            {
                continue;
            }
            if (HcEraseGroupSize == 0 || HcWriteProtectGroupSize == 0)
            {
                DPRINT1("SdBusEmmcEnumeratePartitions: invalid GP group "
                        "multipliers (erase=%lu wp=%lu)\n",
                        HcEraseGroupSize, HcWriteProtectGroupSize);
                return STATUS_DEVICE_DATA_ERROR;
            }

            PartSizeBytes = (ULONGLONG)GpSizeMult *
                            HcEraseGroupSize *
                            HcWriteProtectGroupSize *
                            EMMC_GP_SIZE_UNIT_BYTES;
            PartitionInfo[PartitionCount].PartitionId =
                (UCHAR)(EMMC_PARTITION_GPP1 + Partition);
            PartitionInfo[PartitionCount++].SizeBytes = PartSizeBytes;
        }
    }

    KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
    {
        ULONG ExistingCount = 0;
        UCHAR SeenPartitions = 0;

        Status = STATUS_SUCCESS;
        for (Entry = FdoExtension->ChildPdoList.Flink;
             Entry != &FdoExtension->ChildPdoList;
             Entry = Entry->Flink)
        {
            PPDO_EXTENSION Existing;
            ULONG InfoIndex;
            UCHAR PartitionBit;

            Existing = CONTAINING_RECORD(Entry, PDO_EXTENSION, ListEntry);
            if (!Existing->IsEmmcPartition ||
                Existing->InsertionGeneration != MainPdo->InsertionGeneration)
            {
                continue;
            }

            for (InfoIndex = 0; InfoIndex < PartitionCount; InfoIndex++)
            {
                if (PartitionInfo[InfoIndex].PartitionId ==
                    Existing->EmmcPartitionId)
                {
                    break;
                }
            }
            PartitionBit = (UCHAR)(1U << Existing->EmmcPartitionId);
            if (!Existing->Present || InfoIndex == PartitionCount ||
                (SeenPartitions & PartitionBit) != 0 ||
                Existing->EmmcPartitionSizeBytes !=
                    PartitionInfo[InfoIndex].SizeBytes)
            {
                Status = STATUS_DEVICE_DATA_ERROR;
                break;
            }

            Existing->CardType = MainPdo->CardType;
            Existing->Cid = MainPdo->Cid;
            Existing->Csd = MainPdo->Csd;
            RtlCopyMemory(Existing->ExtCsd, MainPdo->ExtCsd,
                          sizeof(Existing->ExtCsd));
            Existing->RelativeAddress = MainPdo->RelativeAddress;
            Existing->HighCapacity = MainPdo->HighCapacity;
            Existing->WriteProtected = MainPdo->WriteProtected;
            Existing->TotalSectors =
                Existing->EmmcPartitionSizeBytes / SD_DEFAULT_BLOCK_SIZE;
            SeenPartitions |= PartitionBit;
            ExistingCount++;
        }

        if (NT_SUCCESS(Status) && ExistingCount != 0 &&
            ExistingCount != PartitionCount)
        {
            Status = STATUS_DEVICE_DATA_ERROR;
        }

        if (!NT_SUCCESS(Status) || ExistingCount != 0)
        {
            KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);
            return Status;
        }
    }
    KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

    for (Partition = 0; Partition < PartitionCount; Partition++)
    {
        Status = SdBusCreateEmmcPartitionPdo(
            FdoExtension,
            MainPdo,
            PartitionInfo[Partition].PartitionId,
            PartitionInfo[Partition].SizeBytes,
            &PartitionPdos[Partition]);
        if (!NT_SUCCESS(Status))
        {
            while (CreatedCount != 0)
            {
                CreatedCount--;
                IoDeleteDevice(PartitionPdos[CreatedCount]);
            }
            return Status;
        }
        CreatedCount++;
    }

    KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
    for (Partition = 0; Partition < PartitionCount; Partition++)
    {
        PPDO_EXTENSION PartitionExtension;

        PartitionExtension = (PPDO_EXTENSION)
            PartitionPdos[Partition]->DeviceExtension;
        PartitionPdos[Partition]->Flags &= ~DO_DEVICE_INITIALIZING;
        InsertTailList(&FdoExtension->ChildPdoList,
                       &PartitionExtension->ListEntry);
        FdoExtension->ChildPdoCount++;
    }
    KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

    for (Partition = 0; Partition < PartitionCount; Partition++)
    {
        DPRINT1("SdBusCreateEmmcPartitionPdo: partition=%u size=%I64u bytes "
                "PDO=%p\n", PartitionInfo[Partition].PartitionId,
                PartitionInfo[Partition].SizeBytes, PartitionPdos[Partition]);
    }

    return STATUS_SUCCESS;
}
