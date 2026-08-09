/*
 * PROJECT:     ReactOS Storport NVMe miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Driver-private declarations
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#pragma once

#include <ntddk.h>
#include <storport.h>

#define NDEBUG
#include <debug.h>

#include "nvme.h"

/* Storport's SCSI header subset lacks these. */
#ifndef SCSIOP_UNMAP
#define SCSIOP_UNMAP                        0x42
#endif
#ifndef CDB_FORCE_MEDIA_ACCESS
#define CDB_FORCE_MEDIA_ACCESS              0x08
#endif
#ifndef MODE_DSP_FUA_SUPPORTED
#define MODE_DSP_FUA_SUPPORTED              0x10
#endif
#ifndef VPD_BLOCK_LIMITS
#define VPD_BLOCK_LIMITS                    0xB0
#endif
#ifndef VPD_BLOCK_DEVICE_CHARACTERISTICS
#define VPD_BLOCK_DEVICE_CHARACTERISTICS    0xB1
#endif
#ifndef VPD_LOGICAL_BLOCK_PROVISIONING
#define VPD_LOGICAL_BLOCK_PROVISIONING      0xB2
#endif
#ifndef LOG_PAGE_CODE_SUPPORTED_LOG_PAGES
#define LOG_PAGE_CODE_SUPPORTED_LOG_PAGES   0x00
#endif
#ifndef LOG_PAGE_CODE_TEMPERATURE
#define LOG_PAGE_CODE_TEMPERATURE           0x0D
#endif
#ifndef LOG_PAGE_CODE_INFORMATIONAL_EXCEPTIONS
#define LOG_PAGE_CODE_INFORMATIONAL_EXCEPTIONS 0x2F
#endif

#define NVME_ADMIN_QUEUE_ENTRIES    32
#define NVME_IO_QUEUE_ENTRIES       256
#define NVME_MAX_IO_QUEUES          8
#define NVME_MAX_NAMESPACES         8
#define NVME_MAX_TRANSFER           (1024 * 1024)
#define NVME_MAX_PRP_ENTRIES        (NVME_MAX_TRANSFER / PAGE_SIZE)
#define NVME_MAX_DSM_RANGES         128
#define NVME_MIN_BLOCK_SHIFT        9
#define NVME_MAX_BLOCK_SHIFT        16
#define NVME_APST_MAX_LATENCY_US    100000
#define NVME_APST_MIN_ITPT_MS       100
#define NVME_APST_MAX_ITPT_MS       60000

/* Admin slot owners; the completion drain dispatches on these. */
#define NVME_SLOT_FREE          0
#define NVME_SLOT_SYNC          1
#define NVME_SLOT_ORPHAN        2
#define NVME_SLOT_AER           3
#define NVME_SLOT_SMART         4
#define NVME_SLOT_SRB_LOG       5
#define NVME_SLOT_SRB_FEATURE   6

typedef struct _NVME_QUEUE
{
    STOR_DPC Dpc;
    PNVME_COMMAND Sq;
    PNVME_COMPLETION Cq;
    ULONGLONG SqPhysical;
    ULONGLONG CqPhysical;
    ULONG QueueId;
    ULONG Vector;
    ULONG Entries;
    ULONG SqTail;
    ULONG SqHead;
    ULONG CqHead;
    ULONG Phase;
    ULONG SqDoorbell;
    ULONG CqDoorbell;
    BOOLEAN Created;
    PSCSI_REQUEST_BLOCK Outstanding[NVME_IO_QUEUE_ENTRIES];
} NVME_QUEUE, *PNVME_QUEUE;

typedef struct _NVME_ADMIN_SLOT
{
    UCHAR Type;
    BOOLEAN Done;
    USHORT Status;
    ULONG Dw0;
    PSCSI_REQUEST_BLOCK Srb;
} NVME_ADMIN_SLOT, *PNVME_ADMIN_SLOT;

typedef struct _NVME_NAMESPACE
{
    ULONG Nsid;
    ULONGLONG Blocks;
    ULONG BlockSize;
    ULONG BlockShift;
    BOOLEAN Ready;
    UCHAR Eui64[8];
    UCHAR Nguid[16];
} NVME_NAMESPACE, *PNVME_NAMESPACE;

typedef struct _NVME_DEVICE_EXTENSION
{
    PUCHAR Bar0;
    ULONG DoorbellStride;
    ULONG TimeoutMilliseconds;
    ULONG SystemIoBusNumber;
    ULONG SlotNumber;
    NVME_QUEUE AdminQueue;
    NVME_ADMIN_SLOT AdminSlots[NVME_ADMIN_QUEUE_ENTRIES];
    NVME_QUEUE IoQueues[NVME_MAX_IO_QUEUES];
    ULONG IoQueueCount;
    PVOID UncachedBase;
    ULONG UncachedSize;
    ULONGLONG UncachedPhysical;
    PVOID IdentifyBuffer;
    ULONGLONG IdentifyPhysical;
    PNVME_SMART_LOG SmartLog;
    ULONGLONG SmartLogPhysical;
    NVME_NAMESPACE Namespaces[NVME_MAX_NAMESPACES];
    ULONG NamespaceCount;
    ULONG MaximumTransferLength;
    ULONG MaxQueueEntries;
    USHORT Oncs;
    ULONG Oaes;
    BOOLEAN VolatileWriteCache;
    BOOLEAN WriteCacheEnabled;
    BOOLEAN MessageInterrupts;
    ULONG MessageCount;
    BOOLEAN InterruptsLive;
    BOOLEAN ControllerStarted;
    BOOLEAN AdminReady;
    /* Power and thermal capability, from Identify. */
    UCHAR Npss;
    UCHAR Apsta;
    USHORT Hctma;
    USHORT Mntmt;
    USHORT Mxtmt;
    USHORT Wctemp;
    USHORT Cctemp;
    NVME_POWER_STATE_DESC Psd[32];
    UCHAR DeepestNonOpState;
    BOOLEAN ApstEnabled;
    /* Health state, refreshed from the SMART log on async events. */
    USHORT CompositeTemperature;
    UCHAR CriticalWarning;
    UCHAR AvailableSpare;
    UCHAR PercentageUsed;
    BOOLEAN AerOutstanding;
    BOOLEAN SmartInFlight;
    UCHAR Serial[21];
    UCHAR Model[41];
    UCHAR Firmware[9];
} NVME_DEVICE_EXTENSION, *PNVME_DEVICE_EXTENSION;

typedef struct _NVME_SRB_EXTENSION
{
    /* DMA-visible per-request area: the SRB extension is physically
     * contiguous and page-aligned, so PRP lists built here never cross a
     * page boundary. */
    union
    {
        ULONGLONG PrpList[NVME_MAX_PRP_ENTRIES];
        NVME_DSM_RANGE DsmRanges[NVME_MAX_DSM_RANGES];
        UCHAR LogPage[512];
    } Dma;
    ULONG QueueId;
    ULONG Slot;
    UCHAR ScsiLogPage;
    UCHAR FeatureValue;
    BOOLEAN FeatureIsWce;
} NVME_SRB_EXTENSION, *PNVME_SRB_EXTENSION;

typedef struct _NVME_LOCK
{
    BOOLEAN Taken;
    BOOLEAN MessageLock;
    ULONG MessageId;
    ULONG OldIrql;
    STOR_LOCK_HANDLE LockHandle;
} NVME_LOCK, *PNVME_LOCK;

/*
 * Completions are collected under the queue lock but the SRBs complete
 * after it drops: Storport releases per-request state on RequestComplete,
 * which must not happen at device IRQL.
 */
typedef struct _NVME_COMPLETED_SRB
{
    PSCSI_REQUEST_BLOCK Srb;
    USHORT Status;
    UCHAR SlotType;
} NVME_COMPLETED_SRB, *PNVME_COMPLETED_SRB;

#define NVME_COMPLETION_BATCH   32

/* core.c */
ULONG NvmeReadRegister(_In_ PNVME_DEVICE_EXTENSION Device, _In_ ULONG Offset);
VOID NvmeWriteRegister(_In_ PNVME_DEVICE_EXTENSION Device, _In_ ULONG Offset, _In_ ULONG Value);
VOID NvmeWriteRegister64(_In_ PNVME_DEVICE_EXTENSION Device, _In_ ULONG Offset, _In_ ULONGLONG Value);
BOOLEAN NvmeWaitReady(_In_ PNVME_DEVICE_EXTENSION Device, _In_ ULONG DesiredReady);
VOID NvmeAcquireLock(_In_ PNVME_DEVICE_EXTENSION Device, _In_ ULONG MessageId, _Out_ PNVME_LOCK Lock);
VOID NvmeReleaseLock(_In_ PNVME_DEVICE_EXTENSION Device, _Inout_ PNVME_LOCK Lock);
ULONG NvmeAdminSubmitLocked(_In_ PNVME_DEVICE_EXTENSION Device, _Inout_ PNVME_COMMAND Command, _In_ UCHAR SlotType, _In_opt_ PSCSI_REQUEST_BLOCK Srb);
BOOLEAN NvmeAdminCommandSync(_In_ PNVME_DEVICE_EXTENSION Device, _Inout_ PNVME_COMMAND Command, _Out_opt_ PULONG Result);
VOID NvmeDrainAdminQueue(_In_ PNVME_DEVICE_EXTENSION Device);
BOOLEAN NvmeStartController(_In_ PNVME_DEVICE_EXTENSION Device, _In_ BOOLEAN FirstStart);
VOID NvmeDisableController(_In_ PNVME_DEVICE_EXTENSION Device);
VOID NvmeShutdownController(_In_ PNVME_DEVICE_EXTENSION Device);
BOOLEAN NvmeResetController(_In_ PNVME_DEVICE_EXTENSION Device);

/* pci.c */
VOID NvmeMaskDeviceInterrupts(_In_ PNVME_DEVICE_EXTENSION Device, _In_ BOOLEAN Mask);

/* queue.c */
ULONG NvmeQueueVector(_In_ PNVME_DEVICE_EXTENSION Device, _In_ ULONG QueueId);
BOOLEAN NvmeCreateIoQueues(_In_ PNVME_DEVICE_EXTENSION Device);
PNVME_QUEUE NvmeSelectQueue(_In_ PNVME_DEVICE_EXTENSION Device);
BOOLEAN NvmeSubmitIoCommand(_In_ PNVME_DEVICE_EXTENSION Device, _In_ PNVME_QUEUE Queue, _In_ PSCSI_REQUEST_BLOCK Srb, _Inout_ PNVME_COMMAND Command);
VOID NvmeDrainIoQueue(_In_ PNVME_DEVICE_EXTENSION Device, _In_ PNVME_QUEUE Queue);
VOID NvmeRetireQueue(_In_ PNVME_DEVICE_EXTENSION Device, _In_ PNVME_QUEUE Queue, _In_ UCHAR SrbStatus);
VOID NvmeAdminDpc(_In_ PSTOR_DPC Dpc, _In_ PVOID HwDeviceExtension, _In_opt_ PVOID SystemArgument1, _In_opt_ PVOID SystemArgument2);
VOID NvmeIoQueueDpc(_In_ PSTOR_DPC Dpc, _In_ PVOID HwDeviceExtension, _In_opt_ PVOID SystemArgument1, _In_opt_ PVOID SystemArgument2);
BOOLEAN NTAPI NvmeHwInterrupt(_In_ PVOID DeviceExtension);
BOOLEAN NvmeHwMSInterrupt(_In_ PVOID DeviceExtension, _In_ ULONG MessageId);

/* scsi.c */
BOOLEAN NTAPI NvmeHwStartIo(_In_ PVOID DeviceExtension, _In_ PSCSI_REQUEST_BLOCK Srb);
VOID NvmeCompleteSrb(_In_ PNVME_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb, _In_ UCHAR SrbStatus);
VOID NvmeSetSenseAndComplete(_In_ PNVME_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb, _In_ UCHAR SenseKey, _In_ UCHAR AdditionalSenseCode);
VOID NvmeCompleteLogSenseSrb(_In_ PNVME_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb, _In_ USHORT NvmeStatus);
VOID NvmeCompleteFeatureSrb(_In_ PNVME_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb, _In_ USHORT NvmeStatus);

/* power.c */
VOID NvmeParsePowerCapabilities(_In_ PNVME_DEVICE_EXTENSION Device, _In_ PNVME_IDENTIFY_CONTROLLER Identify);
VOID NvmeConfigurePowerAndEvents(_In_ PNVME_DEVICE_EXTENSION Device);
VOID NvmeArmAerLocked(_In_ PNVME_DEVICE_EXTENSION Device);
VOID NvmeKickSmartLocked(_In_ PNVME_DEVICE_EXTENSION Device);
VOID NvmeHandleAerLocked(_In_ PNVME_DEVICE_EXTENSION Device, _In_ ULONG Dw0, _In_ USHORT Status);
VOID NvmeHandleSmartLocked(_In_ PNVME_DEVICE_EXTENSION Device, _In_ USHORT Status);
BOOLEAN NvmeSubmitPowerStateSrb(_In_ PNVME_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb, _In_ BOOLEAN Operational);
