/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Generate Windows-compatible kernel dumps
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <internal/dump.h>
#include <ntdddisk.h>
#include <ntddstor.h>
#define NDEBUG
#include <debug.h>
#include <reactos/drivers/dumpstor.h>
#if defined(_M_AMD64) || defined(_M_ARM64)
#include <mm/ARM3/miarm.h>
#endif

#if defined(_M_AMD64) || defined(_M_ARM64)

#define KDP_LIVE_DUMP_BUGCHECK 0x161
#define KDP_LIVE_DUMP_WRITE_SIZE (1024 * 1024)
#define TAG_LIVE_DUMP 'pDmK'
#define TAG_CRASH_DUMP 'pDcK'
#define KDP_DUMP_EXTENT_BUFFER_SIZE (64 * 1024)
#define KDP_RAW_DUMP_DEFAULT_MBR_TYPE 0x7f
#define KDP_RAW_DUMP_LAYOUT_PARTITIONS 16
#define KDP_RAW_DUMP_COPY_SIZE (1024 * 1024)
#define KDP_RAW_LOG_RESERVE_SIZE (256 * 1024)
#define KDP_RAW_LOG_SIGNATURE 0x474f4c4bUL
#define KDP_RAW_LOG_VERSION 1
#define KDP_RAW_LOG_FLAG_ROLLED 0x00000001
#define KDP_RAW_LOG_FLAG_TRUNCATED 0x00000002

/* CrashControl!CrashDumpEnabled values, matching Windows */
#define KDP_DUMP_DISABLED 0
#define KDP_DUMP_COMPLETE 1
#define KDP_DUMP_KERNEL 2
#define KDP_DUMP_SMALL 3
#define KDP_DUMP_AUTOMATIC 7

static LONG KdpLiveDumpActive;
static LONG KdpCrashDumpActive;
static volatile NTSTATUS KdpCrashDumpInitializationStatus = STATUS_DEVICE_NOT_READY;

typedef struct _KDP_RAW_LOG_HEADER
{
    ULONG Signature;
    USHORT Version;
    USHORT HeaderSize;
    ULONG DataLength;
    ULONG DataCrc32;
    ULONG Flags;
    ULONG BugCheckCode;
    ULONG64 BugCheckParameters[4];
    ULONG RolloverCount;
    ULONG Reserved;
} KDP_RAW_LOG_HEADER, *PKDP_RAW_LOG_HEADER;

C_ASSERT(sizeof(KDP_RAW_LOG_HEADER) == 64);

typedef struct _KDP_CRASH_DUMP_STATE
{
    HANDLE DumpHandle;
    HANDLE IoEventHandle;
    PFILE_OBJECT FileObject;
    PDUMP_HEADER64 Header;
    PRETRIEVAL_POINTERS_BUFFER RetrievalPointers;
    ROS_STORAGE_DUMP_INTERFACE Storage;
    LARGE_INTEGER PartitionOffset;
    LARGE_INTEGER Capacity;
    ULONG ClusterSize;
    /* Cursor into Extents, so a forward walk of the file never rescans. */
    ULONG ExtentIndex;
    ULONG64 ExtentStartVcn;
    /* CrashControl policy: full or bitmap dump. */
    ULONG DumpType;
    BOOLEAN IncludeUserPages;
    RTL_BITMAP PageBitmap;
    PULONG PageBitmapBuffer;
    PSUMMARY_DUMP64 Summary;
    ULONG SummarySize;
    PVOID CrashLogBuffer;
    PVOID CrashLogIoBuffer;
    ULONG CrashLogBufferSize;
    ULONG64 CrashLogOffset;
    BOOLEAN Dedicated;
    BOOLEAN Initialized;
} KDP_CRASH_DUMP_STATE, *PKDP_CRASH_DUMP_STATE;

static KDP_CRASH_DUMP_STATE KdpCrashDumpState;

typedef struct _KDP_DRIVE_LAYOUT_BUFFER
{
    DRIVE_LAYOUT_INFORMATION_EX Layout;
    PARTITION_INFORMATION_EX AdditionalPartitions[KDP_RAW_DUMP_LAYOUT_PARTITIONS - 1];
} KDP_DRIVE_LAYOUT_BUFFER, *PKDP_DRIVE_LAYOUT_BUFFER;

static VOID KdpSetDedicatedCrashDumpActive(_In_ BOOLEAN Active)
{
    ULONG Value = Active ? 1 : 0;
    NTSTATUS Status;

    Status = RtlWriteRegistryValue(RTL_REGISTRY_CONTROL, L"CrashControl", L"DedicatedDumpActive", REG_DWORD, &Value, sizeof(Value));
    if (!NT_SUCCESS(Status))
        DPRINT1("KD: Failed to publish dedicated crash-dump state (0x%08lx)\n", Status);
}

/* Progress is reported in 100 / KDP_DUMP_PROGRESS_STEPS percent increments. */
#define KDP_DUMP_PROGRESS_STEPS 20
#define KDP_DUMP_DATA_PROGRESS_STEPS (KDP_DUMP_PROGRESS_STEPS - 1)

static PCSTR KdpGetCrashDumpFailureReason(_In_ NTSTATUS Status)
{
    switch (Status)
    {
        case STATUS_INSUFFICIENT_RESOURCES:
        case STATUS_NO_MEMORY:
        case STATUS_COMMITMENT_LIMIT:
            return "insufficient memory";

        case STATUS_DISK_FULL:
            return "insufficient storage space";

        case STATUS_DEVICE_NOT_READY:
        case STATUS_INVALID_DEVICE_STATE:
            return "storage target is not ready";

        case STATUS_DEVICE_BUSY:
        case STATUS_SHARING_VIOLATION:
            return "storage target is busy";

        case STATUS_NOT_SUPPORTED:
            return "dump storage is disabled or unsupported";

        case STATUS_NOT_FOUND:
        case STATUS_OBJECT_NAME_NOT_FOUND:
        case STATUS_OBJECT_PATH_NOT_FOUND:
        case STATUS_NO_SUCH_DEVICE:
        case STATUS_DEVICE_DOES_NOT_EXIST:
            return "dump storage was not found";

        case STATUS_DEVICE_NOT_CONNECTED:
        case STATUS_NO_MEDIA_IN_DEVICE:
            return "storage device is disconnected";

        case STATUS_MEDIA_WRITE_PROTECTED:
        case STATUS_ACCESS_DENIED:
            return "storage target is not writable";

        case STATUS_IO_DEVICE_ERROR:
        case STATUS_DEVICE_POWER_FAILURE:
        case STATUS_DRIVER_INTERNAL_ERROR:
            return "storage I/O error";

        case STATUS_DEVICE_DATA_ERROR:
        case STATUS_CRC_ERROR:
        case STATUS_VERIFY_REQUIRED:
        case STATUS_UNRECOGNIZED_MEDIA:
            return "storage data error";

        case STATUS_IO_TIMEOUT:
            return "storage I/O timed out";

        case STATUS_FILE_CORRUPT_ERROR:
        case STATUS_DISK_CORRUPT_ERROR:
            return "dump target layout is corrupt";

        case STATUS_END_OF_FILE:
            return "dump target ended unexpectedly";

        case STATUS_REVISION_MISMATCH:
            return "storage dump interface is incompatible";

        case STATUS_INVALID_PARAMETER:
        case STATUS_INVALID_HANDLE:
            return "dump target is invalid";

        case STATUS_INTERNAL_ERROR:
            return "dump writer consistency check failed";

        case STATUS_CANCELLED:
            return "dump operation was canceled";

        default:
            return "unknown error";
    }
}

static VOID KdpDisplayCrashDumpProgress(_In_ ULONG Percent)
{
    CHAR ProgressText[64];

    Percent = min(Percent, 100);
    RtlStringCbPrintfA(ProgressText, sizeof(ProgressText), "\rDumping memory to storage: %3lu%% complete", Percent);
    DbgPrint("%s", ProgressText);
    InbvDisplayString(ProgressText);
}

static VOID KdpDisplayCrashDumpFailure(_In_ NTSTATUS Status)
{
    CHAR FailureText[128];
    PCSTR Reason = KdpGetCrashDumpFailureReason(Status);

    RtlStringCbPrintfA(FailureText, sizeof(FailureText), "\nMemory dump failed: %s (0x%08lx).\n", Reason, Status);
    DbgPrint("%s", FailureText);
    RtlStringCbPrintfA(FailureText, sizeof(FailureText), "\r\nMemory dump failed: %s (0x%08lx).\r\n", Reason, Status);
    InbvDisplayString(FailureText);
}

static VOID KdpDisplayCrashLogFailure(_In_ NTSTATUS Status)
{
    CHAR FailureText[128];
    PCSTR Reason = (Status == STATUS_NOT_FOUND) ? "no crash log data was available" : KdpGetCrashDumpFailureReason(Status);

    RtlStringCbPrintfA(FailureText, sizeof(FailureText), "\nCrash log failed: %s (0x%08lx); memory dump saved.\n", Reason, Status);
    DbgPrint("%s", FailureText);
    RtlStringCbPrintfA(FailureText, sizeof(FailureText), "\r\nCrash log failed: %s (0x%08lx).\r\nMemory dump was saved.\r\n", Reason, Status);
    InbvDisplayString(FailureText);
}

static NTSTATUS KdpSendDeviceIoControl(_In_ PDEVICE_OBJECT DeviceObject, _In_ ULONG ControlCode, _In_reads_bytes_opt_(InputLength) PVOID InputBuffer, _In_ ULONG InputLength, _Out_writes_bytes_(OutputLength) PVOID OutputBuffer, _In_ ULONG OutputLength)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(ControlCode, DeviceObject, InputBuffer, InputLength, OutputBuffer, OutputLength, FALSE, &Event, &IoStatus);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    return Status;
}

static NTSTATUS KdpTranslateDumpOffset(_In_ ULONG64 FileOffset, _In_ ULONG Length, _Out_ PULONG64 DiskByteOffset, _Out_ PULONG ContiguousLength)
{
    PRETRIEVAL_POINTERS_BUFFER Retrieval;
    ULONG64 Vcn;
    ULONG64 RunStartVcn;
    ULONG64 RunEndVcn;
    ULONG64 OffsetInCluster;
    ULONG64 RemainingInRun;
    ULONG Index;
    LONGLONG Lcn;

    if ((FileOffset >= (ULONG64)KdpCrashDumpState.Capacity.QuadPart) ||
        ((ULONG64)Length > (ULONG64)KdpCrashDumpState.Capacity.QuadPart - FileOffset))
    {
        return STATUS_DISK_FULL;
    }

    Retrieval = KdpCrashDumpState.RetrievalPointers;
    Vcn = FileOffset / KdpCrashDumpState.ClusterSize;
    OffsetInCluster = FileOffset % KdpCrashDumpState.ClusterSize;

    /*
     * The writer walks the dump forward one page at a time, so resume from
     * the extent that satisfied the previous request. Only a rewind (the
     * header rewrite at offset 0) restarts the scan.
     */
    Index = KdpCrashDumpState.ExtentIndex;
    RunStartVcn = KdpCrashDumpState.ExtentStartVcn;
    if (Vcn < RunStartVcn)
    {
        Index = 0;
        RunStartVcn = Retrieval->StartingVcn.QuadPart;
    }

    for (; Index < Retrieval->ExtentCount; Index++)
    {
        RunEndVcn = Retrieval->Extents[Index].NextVcn.QuadPart;
        if ((Vcn >= RunStartVcn) && (Vcn < RunEndVcn))
        {
            Lcn = Retrieval->Extents[Index].Lcn.QuadPart;
            if (Lcn < 0)
                return STATUS_FILE_CORRUPT_ERROR;

            KdpCrashDumpState.ExtentIndex = Index;
            KdpCrashDumpState.ExtentStartVcn = RunStartVcn;

            Lcn += Vcn - RunStartVcn;
            *DiskByteOffset = KdpCrashDumpState.PartitionOffset.QuadPart + (ULONG64)Lcn * KdpCrashDumpState.ClusterSize + OffsetInCluster;
            RemainingInRun = (RunEndVcn - Vcn) * KdpCrashDumpState.ClusterSize - OffsetInCluster;
            *ContiguousLength = (ULONG)min((ULONG64)Length, RemainingInRun);
            return STATUS_SUCCESS;
        }

        RunStartVcn = RunEndVcn;
    }

    return STATUS_END_OF_FILE;
}

static NTSTATUS KdpWriteStoragePhysical(_In_ PHYSICAL_ADDRESS PhysicalAddress, _In_ ULONG Length, _Inout_ PULONG64 FileOffset)
{
    ULONG64 DiskByteOffset;
    ULONG ContiguousLength;
    ULONG TransferLength;
    NTSTATUS Status;

    while (Length != 0)
    {
        Status = KdpTranslateDumpOffset(*FileOffset, Length, &DiskByteOffset, &ContiguousLength);
        if (!NT_SUCCESS(Status))
            return Status;

        TransferLength = min(ContiguousLength, KdpCrashDumpState.Storage.MaximumTransferLength);
        Status = KdpCrashDumpState.Storage.WriteRoutine(KdpCrashDumpState.Storage.Context, DiskByteOffset, PhysicalAddress, TransferLength);
        if (!NT_SUCCESS(Status))
            return Status;

        PhysicalAddress.QuadPart += TransferLength;
        *FileOffset += TransferLength;
        Length -= TransferLength;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS KdpWriteStorageVirtual(_In_reads_bytes_(Length) PVOID Buffer, _In_ ULONG Length, _Inout_ PULONG64 FileOffset)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG PageLength;
    NTSTATUS Status;

    while (Length != 0)
    {
        PageLength = min(Length, PAGE_SIZE - BYTE_OFFSET(Buffer));
        PhysicalAddress = MmGetPhysicalAddress(Buffer);
        Status = KdpWriteStoragePhysical(PhysicalAddress, PageLength, FileOffset);
        if (!NT_SUCCESS(Status))
            return Status;

        Buffer = (PUCHAR)Buffer + PageLength;
        Length -= PageLength;
    }

    return STATUS_SUCCESS;
}

#if defined(_M_AMD64)
static ULONG_PTR NTAPI KdpCaptureLiveDumpContext(_In_ ULONG_PTR Argument)
{
    UNREFERENCED_PARAMETER(Argument);

    RtlCaptureContext(KeGetCurrentPrcb()->CrashDumpContext);
    return 0;
}
#endif

static NTSTATUS KdpWriteDumpFileChunk(_In_ HANDLE FileHandle, _In_ HANDLE EventHandle, _In_reads_bytes_(Length) PVOID Buffer, _In_ ULONG Length, _Inout_ PLARGE_INTEGER FileOffset)
{
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;

    ZwClearEvent(EventHandle);
    Status = ZwWriteFile(FileHandle, EventHandle, NULL, NULL, &IoStatus, Buffer, Length, FileOffset, NULL);
    if (Status == STATUS_PENDING)
    {
        Status = ZwWaitForSingleObject(EventHandle, FALSE, NULL);
        if (NT_SUCCESS(Status))
            Status = IoStatus.Status;
    }

    if (!NT_SUCCESS(Status))
        return Status;

    if (IoStatus.Information != Length)
        return STATUS_DISK_FULL;

    FileOffset->QuadPart += Length;
    return STATUS_SUCCESS;
}

static VOID KdpQueryCrashControlPolicy(_Out_ PULONG DumpType, _Out_ PBOOLEAN IncludeUserPages)
{
    RTL_QUERY_REGISTRY_TABLE QueryTable[3];
    ULONG CrashDumpEnabled = KDP_DUMP_AUTOMATIC;
    ULONG FilterPages = 0;
    ULONG DefaultEnabled = KDP_DUMP_AUTOMATIC;
    ULONG DefaultFilter = 0;

    RtlZeroMemory(QueryTable, sizeof(QueryTable));
    QueryTable[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
    QueryTable[0].Name = L"CrashDumpEnabled";
    QueryTable[0].EntryContext = &CrashDumpEnabled;
    QueryTable[0].DefaultType = REG_DWORD;
    QueryTable[0].DefaultData = &DefaultEnabled;
    QueryTable[0].DefaultLength = sizeof(DefaultEnabled);
    QueryTable[1].Flags = RTL_QUERY_REGISTRY_DIRECT;
    QueryTable[1].Name = L"FilterPages";
    QueryTable[1].EntryContext = &FilterPages;
    QueryTable[1].DefaultType = REG_DWORD;
    QueryTable[1].DefaultData = &DefaultFilter;
    QueryTable[1].DefaultLength = sizeof(DefaultFilter);

    RtlQueryRegistryValues(RTL_REGISTRY_CONTROL, L"CrashControl", QueryTable, NULL, NULL);

    switch (CrashDumpEnabled)
    {
        case KDP_DUMP_DISABLED:
            *DumpType = DUMP_TYPE_INVALID;
            *IncludeUserPages = FALSE;
            break;
        case KDP_DUMP_COMPLETE:
            /* CrashDumpEnabled=1 + FilterPages=1 is the active memory dump */
            *DumpType = FilterPages ? DUMP_TYPE_BITMAP_FULL : DUMP_TYPE_FULL;
            *IncludeUserPages = FilterPages ? TRUE : FALSE;
            break;
        case KDP_DUMP_KERNEL:
        case KDP_DUMP_AUTOMATIC:
        default:
            *DumpType = DUMP_TYPE_BITMAP_KERNEL;
            *IncludeUserPages = FALSE;
            break;
    }
}

static VOID KdpCleanupCrashDumpState(VOID)
{
    if (KdpCrashDumpState.CrashLogIoBuffer != NULL)
        MmFreeContiguousMemory(KdpCrashDumpState.CrashLogIoBuffer);
    if (KdpCrashDumpState.CrashLogBuffer != NULL)
        ExFreePoolWithTag(KdpCrashDumpState.CrashLogBuffer, TAG_CRASH_DUMP);
    if (KdpCrashDumpState.Summary != NULL)
        ExFreePoolWithTag(KdpCrashDumpState.Summary, TAG_CRASH_DUMP);
    if (KdpCrashDumpState.PageBitmapBuffer != NULL)
        ExFreePoolWithTag(KdpCrashDumpState.PageBitmapBuffer, TAG_CRASH_DUMP);
    if (KdpCrashDumpState.Header != NULL)
        ExFreePoolWithTag(KdpCrashDumpState.Header, TAG_CRASH_DUMP);
    if (KdpCrashDumpState.RetrievalPointers != NULL)
        ExFreePoolWithTag(KdpCrashDumpState.RetrievalPointers, TAG_CRASH_DUMP);
    if (KdpCrashDumpState.IoEventHandle != NULL)
        ZwClose(KdpCrashDumpState.IoEventHandle);
    if (KdpCrashDumpState.FileObject != NULL)
        ObDereferenceObject(KdpCrashDumpState.FileObject);
    if (KdpCrashDumpState.DumpHandle != NULL)
        ZwClose(KdpCrashDumpState.DumpHandle);

    RtlZeroMemory(&KdpCrashDumpState, sizeof(KdpCrashDumpState));
}

static NTSTATUS KdpAllocateCrashDumpResources(_In_ PCSTR TargetKind)
{
    PHYSICAL_ADDRESS HighestAddress;
    ULONG HeaderSize;
    NTSTATUS Status;

    KdpQueryCrashControlPolicy(&KdpCrashDumpState.DumpType, &KdpCrashDumpState.IncludeUserPages);
    if (KdpCrashDumpState.DumpType == (ULONG)DUMP_TYPE_INVALID)
    {
        DPRINT1("KD: Crash dumps disabled by CrashControl!CrashDumpEnabled\n");
        return STATUS_NOT_SUPPORTED;
    }

    KdpCrashDumpState.Header = ExAllocatePoolWithTag(NonPagedPool, DUMP_HEADER64_SIZE, TAG_CRASH_DUMP);
    if (KdpCrashDumpState.Header == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = KeInitializeCrashDumpHeader(DUMP_TYPE_FULL, 0, KdpCrashDumpState.Header, DUMP_HEADER64_SIZE, &HeaderSize);
    if (!NT_SUCCESS(Status))
        return Status;
    if (HeaderSize != DUMP_HEADER64_SIZE)
        return STATUS_INTERNAL_ERROR;

    if (KdpCrashDumpState.Dedicated)
    {
        KdpCrashDumpState.CrashLogBufferSize = min(KdPrintBufferSize, KDP_RAW_LOG_RESERVE_SIZE - KdpCrashDumpState.Storage.BytesPerSector);
        if (KdpCrashDumpState.CrashLogBufferSize == 0)
            return STATUS_INVALID_DEVICE_STATE;

        KdpCrashDumpState.CrashLogBuffer = ExAllocatePoolWithTag(NonPagedPool, KdpCrashDumpState.CrashLogBufferSize, TAG_CRASH_DUMP);
        if (KdpCrashDumpState.CrashLogBuffer == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        HighestAddress.QuadPart = MAXULONGLONG;
        KdpCrashDumpState.CrashLogIoBuffer = MmAllocateContiguousMemory(PAGE_SIZE, HighestAddress);
        if (KdpCrashDumpState.CrashLogIoBuffer == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (KdpCrashDumpState.DumpType == DUMP_TYPE_FULL)
    {
        if (KdpCrashDumpState.Capacity.QuadPart < KdpCrashDumpState.Header->RequiredDumpSpace.QuadPart)
            return STATUS_DISK_FULL;
    }
    else
    {
        PPHYSICAL_MEMORY_RUN LastRun;
        ULONG BitmapBits;
        ULONG BitmapBytes;

        if (MmPhysicalMemoryBlock->NumberOfRuns == 0)
            return STATUS_INVALID_DEVICE_STATE;

        LastRun = &MmPhysicalMemoryBlock->Run[MmPhysicalMemoryBlock->NumberOfRuns - 1];
        BitmapBits = (ULONG)(LastRun->BasePage + LastRun->PageCount);
        BitmapBytes = ALIGN_UP_BY(BitmapBits, 32) / 8;

        KdpCrashDumpState.PageBitmapBuffer = ExAllocatePoolWithTag(NonPagedPool, BitmapBytes, TAG_CRASH_DUMP);
        if (KdpCrashDumpState.PageBitmapBuffer == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlInitializeBitMap(&KdpCrashDumpState.PageBitmap, KdpCrashDumpState.PageBitmapBuffer, BitmapBits);

        KdpCrashDumpState.SummarySize = ALIGN_UP_BY(FIELD_OFFSET(SUMMARY_DUMP64, Buffer) + BitmapBytes, PAGE_SIZE);
        KdpCrashDumpState.Summary = ExAllocatePoolWithTag(NonPagedPool, KdpCrashDumpState.SummarySize, TAG_CRASH_DUMP);
        if (KdpCrashDumpState.Summary == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        if (KdpCrashDumpState.Capacity.QuadPart < (LONGLONG)(DUMP_HEADER64_SIZE + KdpCrashDumpState.SummarySize + PAGE_SIZE))
            return STATUS_DISK_FULL;
    }

    KdpCrashDumpState.Initialized = TRUE;
    DPRINT1("KD: Crash dump target initialized (%s, %s%s dump, %I64u bytes available, %I64u required for full)\n",
            TargetKind,
            KdpCrashDumpState.DumpType == DUMP_TYPE_FULL ? "full" : "summary",
            KdpCrashDumpState.IncludeUserPages ? "+user" : "",
            KdpCrashDumpState.Capacity.QuadPart,
            KdpCrashDumpState.Header->RequiredDumpSpace.QuadPart);
    return STATUS_SUCCESS;
}

BOOLEAN NTAPI KdpInitializeCrashDump(_In_ HANDLE DumpFileHandle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    FILE_FS_SIZE_INFORMATION SizeInfo;
    FILE_STANDARD_INFORMATION StandardInfo;
    PARTITION_INFORMATION_EX PartitionInfo;
    STARTING_VCN_INPUT_BUFFER StartingVcn;
    PDEVICE_OBJECT StorageDevice;
    PVPB Vpb;
    PCSTR Operation = "duplicating the dump target handle";
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;
    PAGED_CODE();

    if (KdpCrashDumpState.Initialized)
    {
        KdpCrashDumpInitializationStatus = STATUS_SUCCESS;
        return TRUE;
    }

    KdpCrashDumpInitializationStatus = STATUS_DEVICE_NOT_READY;

    KdpSetDedicatedCrashDumpActive(FALSE);

    Status = ObDuplicateObject(PsGetCurrentProcess(), DumpFileHandle, PsInitialSystemProcess, &KdpCrashDumpState.DumpHandle, 0, OBJ_KERNEL_HANDLE, DUPLICATE_SAME_ACCESS, KernelMode);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Operation = "referencing the dump target";
    Status = ObReferenceObjectByHandle(KdpCrashDumpState.DumpHandle, FILE_WRITE_DATA, IoFileObjectType, KernelMode, (PVOID *)&KdpCrashDumpState.FileObject, NULL);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Operation = "querying the dump target";
    Status = ZwQueryInformationFile(KdpCrashDumpState.DumpHandle, &IoStatus, &StandardInfo, sizeof(StandardInfo), FileStandardInformation);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Operation = "querying the dump target volume";
    Status = ZwQueryVolumeInformationFile(KdpCrashDumpState.DumpHandle, &IoStatus, &SizeInfo, sizeof(SizeInfo), FileFsSizeInformation);
    if (!NT_SUCCESS(Status))
        goto Failure;

    KdpCrashDumpState.ClusterSize = SizeInfo.BytesPerSector * SizeInfo.SectorsPerAllocationUnit;
    Vpb = KdpCrashDumpState.FileObject->Vpb;
    if ((Vpb == NULL) && (KdpCrashDumpState.FileObject->DeviceObject != NULL))
    {
        Vpb = KdpCrashDumpState.FileObject->DeviceObject->Vpb;
    }

    if ((KdpCrashDumpState.ClusterSize == 0) || (Vpb == NULL) || (Vpb->RealDevice == NULL))
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Failure;
    }

    StorageDevice = Vpb->RealDevice;
    RtlZeroMemory(&PartitionInfo, sizeof(PartitionInfo));
    Operation = "querying the partition";
    Status = KdpSendDeviceIoControl(StorageDevice, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0, &PartitionInfo, sizeof(PartitionInfo));
    if (!NT_SUCCESS(Status))
        goto Failure;
    KdpCrashDumpState.PartitionOffset = PartitionInfo.StartingOffset;

    RtlZeroMemory(&KdpCrashDumpState.Storage, sizeof(KdpCrashDumpState.Storage));
    KdpCrashDumpState.Storage.BytesPerSector = SizeInfo.BytesPerSector;
    Operation = "querying the storage dump interface";
    Status = KdpSendDeviceIoControl(StorageDevice, IOCTL_REACTOS_STORAGE_GET_DUMP_INTERFACE, &KdpCrashDumpState.Storage, sizeof(KdpCrashDumpState.Storage), &KdpCrashDumpState.Storage, sizeof(KdpCrashDumpState.Storage));
    if (!NT_SUCCESS(Status))
        goto Failure;

    if ((KdpCrashDumpState.Storage.Version != ROS_STORAGE_DUMP_INTERFACE_VERSION) || (KdpCrashDumpState.Storage.Size < sizeof(KdpCrashDumpState.Storage)) || (KdpCrashDumpState.Storage.Context == NULL) || (KdpCrashDumpState.Storage.BytesPerSector == 0) || (KdpCrashDumpState.Storage.MaximumTransferLength == 0) || (KdpCrashDumpState.Storage.Prepare == NULL) || (KdpCrashDumpState.Storage.WriteRoutine == NULL) || (KdpCrashDumpState.Storage.Flush == NULL) || ((KdpCrashDumpState.ClusterSize % KdpCrashDumpState.Storage.BytesPerSector) != 0))
    {
        Status = STATUS_REVISION_MISMATCH;
        goto Failure;
    }

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Operation = "creating the dump I/O event";
    Status = ZwCreateEvent(&KdpCrashDumpState.IoEventHandle, EVENT_ALL_ACCESS, &ObjectAttributes, SynchronizationEvent, FALSE);
    if (!NT_SUCCESS(Status))
        goto Failure;

    KdpCrashDumpState.RetrievalPointers = ExAllocatePoolWithTag(NonPagedPool, KDP_DUMP_EXTENT_BUFFER_SIZE, TAG_CRASH_DUMP);
    if (KdpCrashDumpState.RetrievalPointers == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Failure;
    }

    StartingVcn.StartingVcn.QuadPart = 0;
    ZwClearEvent(KdpCrashDumpState.IoEventHandle);
    Operation = "querying dump target retrieval pointers";
    Status = ZwFsControlFile(KdpCrashDumpState.DumpHandle, KdpCrashDumpState.IoEventHandle, NULL, NULL, &IoStatus, FSCTL_GET_RETRIEVAL_POINTERS, &StartingVcn, sizeof(StartingVcn), KdpCrashDumpState.RetrievalPointers, KDP_DUMP_EXTENT_BUFFER_SIZE);
    if (Status == STATUS_PENDING)
    {
        Status = ZwWaitForSingleObject(KdpCrashDumpState.IoEventHandle, FALSE, NULL);
        if (NT_SUCCESS(Status))
            Status = IoStatus.Status;
    }
    if (!NT_SUCCESS(Status))
        goto Failure;

    if ((KdpCrashDumpState.RetrievalPointers->ExtentCount == 0) || (KdpCrashDumpState.RetrievalPointers->StartingVcn.QuadPart != 0))
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Failure;
    }

    DPRINT1("KD: Crash dump storage: partition %I64u, cluster %lu, sector %lu, extents %lu\n", KdpCrashDumpState.PartitionOffset.QuadPart, KdpCrashDumpState.ClusterSize, KdpCrashDumpState.Storage.BytesPerSector, KdpCrashDumpState.RetrievalPointers->ExtentCount);
    DPRINT1("KD: Crash dump first extent: VCN %I64d to %I64d, LCN %I64d, disk byte %I64u\n", KdpCrashDumpState.RetrievalPointers->StartingVcn.QuadPart, KdpCrashDumpState.RetrievalPointers->Extents[0].NextVcn.QuadPart, KdpCrashDumpState.RetrievalPointers->Extents[0].Lcn.QuadPart, KdpCrashDumpState.PartitionOffset.QuadPart + KdpCrashDumpState.RetrievalPointers->Extents[0].Lcn.QuadPart * KdpCrashDumpState.ClusterSize);

    KdpCrashDumpState.Capacity = StandardInfo.EndOfFile;
    Operation = "allocating crash dump resources";
    Status = KdpAllocateCrashDumpResources("file-backed fallback");
    if (!NT_SUCCESS(Status))
        goto Failure;
    KdpCrashDumpInitializationStatus = STATUS_SUCCESS;
    return TRUE;

Failure:
    DPRINT1("KD: Crash dump target initialization failed while %s (0x%08lx)\n", Operation, Status);
    KdpCrashDumpInitializationStatus = Status;
    KdpCleanupCrashDumpState();
    return FALSE;
}

static NTSTATUS KdpBuildCrashLogPath(_Out_writes_bytes_(PathSize) PWCHAR Path, _In_ SIZE_T PathSize)
{
    if ((NtSystemRoot.Buffer == NULL) || (NtSystemRoot.Length < (2 * sizeof(WCHAR))) || (NtSystemRoot.Buffer[1] != L':'))
        return STATUS_OBJECT_PATH_SYNTAX_BAD;

    return RtlStringCbPrintfW(Path, PathSize, L"\\??\\%wc:\\KDWATCHDOG.LOG", NtSystemRoot.Buffer[0]);
}

static NTSTATUS KdpCopyRawRangeToFile(_In_ HANDLE RawHandle, _Inout_updates_bytes_(BufferSize) PVOID Buffer, _In_ ULONG BufferSize, _In_ ULONG BytesPerSector, _In_ ULONG64 RawByteOffset, _In_ ULONG64 DataLength, _In_ PCWSTR FilePath, _In_ BOOLEAN VerifyCrc, _In_ ULONG ExpectedCrc)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    FILE_DISPOSITION_INFORMATION Disposition;
    UNICODE_STRING ObjectName;
    IO_STATUS_BLOCK IoStatus;
    HANDLE FileHandle = NULL;
    LARGE_INTEGER FileOffset;
    LARGE_INTEGER RawOffset;
    ULONG ActualCrc = 0;
    ULONG ReadLength;
    ULONG WriteLength;
    ULONG64 Remaining;
    NTSTATUS Status;

    RtlInitUnicodeString(&ObjectName, FilePath);
    InitializeObjectAttributes(&ObjectAttributes, &ObjectName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(&FileHandle, FILE_WRITE_DATA | DELETE | SYNCHRONIZE, &ObjectAttributes, &IoStatus, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OVERWRITE_IF, FILE_NON_DIRECTORY_FILE | FILE_SEQUENTIAL_ONLY | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (!NT_SUCCESS(Status))
        return Status;

    RawOffset.QuadPart = RawByteOffset;
    FileOffset.QuadPart = 0;
    Remaining = DataLength;
    while (Remaining != 0)
    {
        WriteLength = (ULONG)min(Remaining, (ULONG64)BufferSize);
        ReadLength = ALIGN_UP_BY(WriteLength, BytesPerSector);
        Status = ZwReadFile(RawHandle, NULL, NULL, NULL, &IoStatus, Buffer, ReadLength, &RawOffset, NULL);
        if (!NT_SUCCESS(Status))
            goto Exit;
        if (IoStatus.Information != ReadLength)
        {
            Status = STATUS_DEVICE_DATA_ERROR;
            goto Exit;
        }
        if (VerifyCrc)
            ActualCrc = RtlComputeCrc32(ActualCrc, Buffer, WriteLength);
        Status = ZwWriteFile(FileHandle, NULL, NULL, NULL, &IoStatus, Buffer, WriteLength, &FileOffset, NULL);
        if (!NT_SUCCESS(Status))
            goto Exit;
        if (IoStatus.Information != WriteLength)
        {
            Status = STATUS_DEVICE_DATA_ERROR;
            goto Exit;
        }

        RawOffset.QuadPart += ReadLength;
        FileOffset.QuadPart += WriteLength;
        Remaining -= WriteLength;
    }

    if (VerifyCrc && (ActualCrc != ExpectedCrc))
    {
        Status = STATUS_CRC_ERROR;
        goto Exit;
    }

    Status = ZwFlushBuffersFile(FileHandle, &IoStatus);

Exit:
    if (!NT_SUCCESS(Status))
    {
        Disposition.DeleteFile = TRUE;
        ZwSetInformationFile(FileHandle, &IoStatus, &Disposition, sizeof(Disposition), FileDispositionInformation);
    }
    ZwClose(FileHandle);
    return Status;
}

static NTSTATUS KdpReadRawRange(_In_ HANDLE RawHandle, _Out_writes_bytes_(Length) PVOID Buffer, _In_ ULONG Length, _In_ ULONG64 RawByteOffset)
{
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER RawOffset;
    NTSTATUS Status;

    RawOffset.QuadPart = RawByteOffset;
    Status = ZwReadFile(RawHandle, NULL, NULL, NULL, &IoStatus, Buffer, Length, &RawOffset, NULL);
    if (NT_SUCCESS(Status) && (IoStatus.Information != Length))
        Status = STATUS_DEVICE_DATA_ERROR;
    return Status;
}

static NTSTATUS KdpInvalidateRawHeader(_In_ HANDLE RawHandle, _Inout_updates_bytes_(BytesPerSector) PVOID Buffer, _In_ ULONG BytesPerSector, _In_ ULONG64 RawByteOffset)
{
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER RawOffset;
    NTSTATUS Status;

    RtlZeroMemory(Buffer, BytesPerSector);
    RawOffset.QuadPart = RawByteOffset;
    Status = ZwWriteFile(RawHandle, NULL, NULL, NULL, &IoStatus, Buffer, BytesPerSector, &RawOffset, NULL);
    if (NT_SUCCESS(Status) && (IoStatus.Information != BytesPerSector))
        Status = STATUS_DEVICE_DATA_ERROR;
    return Status;
}

static NTSTATUS KdpPublishPreviousCrashArtifacts(_In_ ULONG DiskNumber, _In_ LARGE_INTEGER PartitionOffset, _In_ ULONG BytesPerSector, _In_ ULONG64 Capacity)
{
    static const WCHAR DumpFilePath[] = L"\\SystemRoot\\MEMORY.DMP";
    OBJECT_ATTRIBUTES ObjectAttributes;
    KDP_RAW_LOG_HEADER LogHeader;
    UNICODE_STRING ObjectName;
    IO_STATUS_BLOCK IoStatus;
    PDUMP_HEADER64 DumpHeader;
    HANDLE RawHandle = NULL;
    WCHAR LogFilePath[64];
    WCHAR RawPathBuffer[64];
    ULONG64 DumpCapacity;
    ULONG64 DumpSize = 0;
    ULONG64 LogOffset;
    PVOID Buffer = NULL;
    NTSTATUS Status;
    BOOLEAN DumpFound = FALSE;
    BOOLEAN LogFound = FALSE;

    if ((BytesPerSector == 0) || (BytesPerSector > KDP_RAW_DUMP_COPY_SIZE) || ((KDP_RAW_DUMP_COPY_SIZE % BytesPerSector) != 0) || (Capacity <= KDP_RAW_LOG_RESERVE_SIZE))
        return STATUS_INVALID_PARAMETER;

    DumpCapacity = Capacity - KDP_RAW_LOG_RESERVE_SIZE;
    LogOffset = PartitionOffset.QuadPart + DumpCapacity;
    Status = RtlStringCbPrintfW(RawPathBuffer, sizeof(RawPathBuffer), L"\\Device\\Harddisk%lu\\Partition0", DiskNumber);
    if (!NT_SUCCESS(Status))
        return Status;
    RtlInitUnicodeString(&ObjectName, RawPathBuffer);
    InitializeObjectAttributes(&ObjectAttributes, &ObjectName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenFile(&RawHandle, FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE, &ObjectAttributes, &IoStatus, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_NO_INTERMEDIATE_BUFFERING);
    if (!NT_SUCCESS(Status))
        return Status;

    Buffer = MmAllocateNonCachedMemory(KDP_RAW_DUMP_COPY_SIZE);
    if (Buffer == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Status = KdpReadRawRange(RawHandle, Buffer, DUMP_HEADER64_SIZE, PartitionOffset.QuadPart);
    if (!NT_SUCCESS(Status))
        goto Exit;
    DumpHeader = Buffer;
    if ((DumpHeader->Signature == DUMP_SIGNATURE64) && (DumpHeader->ValidDump == DUMP_VALID_DUMP64))
    {
        DumpSize = DumpHeader->RequiredDumpSpace.QuadPart;
        if ((DumpSize < DUMP_HEADER64_SIZE) || (DumpSize > DumpCapacity) || ((DumpSize % BytesPerSector) != 0))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Exit;
        }
        DumpFound = TRUE;
    }

    Status = KdpReadRawRange(RawHandle, Buffer, BytesPerSector, LogOffset);
    if (!NT_SUCCESS(Status))
        goto Exit;
    RtlCopyMemory(&LogHeader, Buffer, sizeof(LogHeader));
    if (LogHeader.Signature == KDP_RAW_LOG_SIGNATURE)
    {
        if ((LogHeader.Version != KDP_RAW_LOG_VERSION) || (LogHeader.HeaderSize != sizeof(LogHeader)) || (LogHeader.DataLength == 0) || (LogHeader.DataLength > (KDP_RAW_LOG_RESERVE_SIZE - BytesPerSector)))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Exit;
        }
        LogFound = TRUE;
    }

    if (!DumpFound && !LogFound)
    {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    if (LogFound)
    {
        Status = KdpBuildCrashLogPath(LogFilePath, sizeof(LogFilePath));
        if (!NT_SUCCESS(Status))
            goto Exit;
        Status = KdpCopyRawRangeToFile(RawHandle, Buffer, KDP_RAW_DUMP_COPY_SIZE, BytesPerSector, LogOffset + BytesPerSector, LogHeader.DataLength, LogFilePath, TRUE, LogHeader.DataCrc32);
        if (!NT_SUCCESS(Status))
            goto Exit;
    }

    if (DumpFound)
    {
        Status = KdpCopyRawRangeToFile(RawHandle, Buffer, KDP_RAW_DUMP_COPY_SIZE, BytesPerSector, PartitionOffset.QuadPart, DumpSize, DumpFilePath, FALSE, 0);
        if (!NT_SUCCESS(Status))
            goto Exit;
    }

    if (LogFound)
    {
        Status = KdpInvalidateRawHeader(RawHandle, Buffer, BytesPerSector, LogOffset);
        if (!NT_SUCCESS(Status))
            goto Exit;
    }
    if (DumpFound)
    {
        Status = KdpInvalidateRawHeader(RawHandle, Buffer, BytesPerSector, PartitionOffset.QuadPart);
        if (!NT_SUCCESS(Status))
            goto Exit;
    }

    Status = ZwFlushBuffersFile(RawHandle, &IoStatus);
    if (Status == STATUS_INVALID_DEVICE_REQUEST)
        Status = STATUS_SUCCESS;
    if (!NT_SUCCESS(Status))
        goto Exit;

    if (LogFound)
        DPRINT1("KD: Published previous crash boot log to %ws (%lu bytes, bugcheck 0x%08lx)\n", LogFilePath, LogHeader.DataLength, LogHeader.BugCheckCode);
    if (DumpFound)
        DPRINT1("KD: Published previous crash dump to %ws (%I64u bytes)\n", DumpFilePath, DumpSize);

Exit:
    if (Buffer != NULL)
        MmFreeNonCachedMemory(Buffer, KDP_RAW_DUMP_COPY_SIZE);
    if (RawHandle != NULL)
        ZwClose(RawHandle);
    return Status;
}

BOOLEAN NTAPI KdpInitializeDedicatedCrashDump(VOID)
{
    static const WCHAR SystemHivePath[] = L"\\SystemRoot\\System32\\Config\\SYSTEM";
    RTL_QUERY_REGISTRY_TABLE QueryTable[2];
    KDP_DRIVE_LAYOUT_BUFFER LayoutBuffer;
    FILE_FS_SIZE_INFORMATION SizeInfo;
    STORAGE_DEVICE_NUMBER DeviceNumber;
    PARTITION_INFORMATION_EX DumpPartition;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING ObjectName;
    IO_STATUS_BLOCK IoStatus;
    PFILE_OBJECT DiskFileObject = NULL;
    PFILE_OBJECT SystemFileObject = NULL;
    PDEVICE_OBJECT DiskDevice;
    PDEVICE_OBJECT SystemDevice;
    HANDLE DiskHandle = NULL;
    HANDLE SystemHandle = NULL;
    PVPB Vpb;
    WCHAR DiskPathBuffer[64];
    ULONG DefaultPartitionType = KDP_RAW_DUMP_DEFAULT_MBR_TYPE;
    ULONG PartitionType = KDP_RAW_DUMP_DEFAULT_MBR_TYPE;
    ULONG Index;
    PCSTR Operation = "opening the system volume";
    NTSTATUS Status;
    BOOLEAN Found = FALSE;
    PAGED_CODE();

    if (KdpCrashDumpState.Initialized)
    {
        KdpCrashDumpInitializationStatus = STATUS_SUCCESS;
        return TRUE;
    }

    KdpCrashDumpInitializationStatus = STATUS_DEVICE_NOT_READY;

    RtlZeroMemory(QueryTable, sizeof(QueryTable));
    QueryTable[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
    QueryTable[0].Name = L"DedicatedDumpPartitionType";
    QueryTable[0].EntryContext = &PartitionType;
    QueryTable[0].DefaultType = REG_DWORD;
    QueryTable[0].DefaultData = &DefaultPartitionType;
    QueryTable[0].DefaultLength = sizeof(DefaultPartitionType);
    RtlQueryRegistryValues(RTL_REGISTRY_CONTROL, L"CrashControl", QueryTable, NULL, NULL);
    if ((PartitionType == 0) || (PartitionType > MAXUCHAR))
    {
        DPRINT1("KD: Raw crash dump partition disabled or invalid (type 0x%lx)\n", PartitionType);
        KdpCrashDumpInitializationStatus = STATUS_NOT_SUPPORTED;
        return FALSE;
    }

    RtlInitUnicodeString(&ObjectName, SystemHivePath);
    InitializeObjectAttributes(&ObjectAttributes, &ObjectName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenFile(&SystemHandle, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &ObjectAttributes, &IoStatus, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Operation = "referencing the system volume";
    Status = ObReferenceObjectByHandle(SystemHandle, 0, IoFileObjectType, KernelMode, (PVOID *)&SystemFileObject, NULL);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Operation = "querying the system volume";
    Status = ZwQueryVolumeInformationFile(SystemHandle, &IoStatus, &SizeInfo, sizeof(SizeInfo), FileFsSizeInformation);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Vpb = SystemFileObject->Vpb;
    if ((Vpb == NULL) && (SystemFileObject->DeviceObject != NULL))
        Vpb = SystemFileObject->DeviceObject->Vpb;
    if ((Vpb == NULL) || (Vpb->RealDevice == NULL) || (SizeInfo.BytesPerSector == 0))
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Failure;
    }
    SystemDevice = Vpb->RealDevice;

    RtlZeroMemory(&DeviceNumber, sizeof(DeviceNumber));
    Operation = "locating the system disk";
    Status = KdpSendDeviceIoControl(SystemDevice, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &DeviceNumber, sizeof(DeviceNumber));
    if (!NT_SUCCESS(Status))
        goto Failure;

    Status = RtlStringCbPrintfW(DiskPathBuffer, sizeof(DiskPathBuffer), L"\\Device\\Harddisk%lu\\Partition0", DeviceNumber.DeviceNumber);
    if (!NT_SUCCESS(Status))
        goto Failure;
    RtlInitUnicodeString(&ObjectName, DiskPathBuffer);
    InitializeObjectAttributes(&ObjectAttributes, &ObjectName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Operation = "opening the system disk";
    Status = ZwOpenFile(&DiskHandle, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &ObjectAttributes, &IoStatus, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Operation = "referencing the system disk";
    Status = ObReferenceObjectByHandle(DiskHandle, 0, IoFileObjectType, KernelMode, (PVOID *)&DiskFileObject, NULL);
    if (!NT_SUCCESS(Status))
        goto Failure;
    DiskDevice = IoGetRelatedDeviceObject(DiskFileObject);

    RtlZeroMemory(&LayoutBuffer, sizeof(LayoutBuffer));
    Operation = "querying the disk partition layout";
    Status = KdpSendDeviceIoControl(DiskDevice, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, &LayoutBuffer, sizeof(LayoutBuffer));
    if (!NT_SUCCESS(Status))
        goto Failure;
    if (LayoutBuffer.Layout.PartitionStyle != PARTITION_STYLE_MBR)
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Failure;
    }

    RtlZeroMemory(&DumpPartition, sizeof(DumpPartition));
    for (Index = 0; (Index < LayoutBuffer.Layout.PartitionCount) && (Index < KDP_RAW_DUMP_LAYOUT_PARTITIONS); Index++)
    {
        PPARTITION_INFORMATION_EX Entry = &LayoutBuffer.Layout.PartitionEntry[Index];

        if ((Entry->PartitionStyle == PARTITION_STYLE_MBR) && (Entry->Mbr.PartitionType == (UCHAR)PartitionType))
        {
            DumpPartition = *Entry;
            Found = TRUE;
            break;
        }
    }
    if (!Found || (DumpPartition.PartitionLength.QuadPart <= 0))
    {
        Status = STATUS_NOT_FOUND;
        goto Failure;
    }

    Operation = "publishing previous crash artifacts";
    Status = KdpPublishPreviousCrashArtifacts(DeviceNumber.DeviceNumber, DumpPartition.StartingOffset, SizeInfo.BytesPerSector, DumpPartition.PartitionLength.QuadPart);
    if ((Status != STATUS_NOT_FOUND) && !NT_SUCCESS(Status))
        goto Failure;

    RtlZeroMemory(&KdpCrashDumpState.Storage, sizeof(KdpCrashDumpState.Storage));
    KdpCrashDumpState.Storage.BytesPerSector = SizeInfo.BytesPerSector;
    Operation = "querying the storage dump interface";
    Status = KdpSendDeviceIoControl(SystemDevice, IOCTL_REACTOS_STORAGE_GET_DUMP_INTERFACE, &KdpCrashDumpState.Storage, sizeof(KdpCrashDumpState.Storage), &KdpCrashDumpState.Storage, sizeof(KdpCrashDumpState.Storage));
    if (!NT_SUCCESS(Status))
        goto Failure;

    if ((KdpCrashDumpState.Storage.Version != ROS_STORAGE_DUMP_INTERFACE_VERSION) || (KdpCrashDumpState.Storage.Size < sizeof(KdpCrashDumpState.Storage)) || (KdpCrashDumpState.Storage.Context == NULL) || (KdpCrashDumpState.Storage.BytesPerSector == 0) || (KdpCrashDumpState.Storage.BytesPerSector > PAGE_SIZE) || (KdpCrashDumpState.Storage.MaximumTransferLength < KdpCrashDumpState.Storage.BytesPerSector) || ((KdpCrashDumpState.Storage.MaximumTransferLength % KdpCrashDumpState.Storage.BytesPerSector) != 0) || (KdpCrashDumpState.Storage.Prepare == NULL) || (KdpCrashDumpState.Storage.WriteRoutine == NULL) || (KdpCrashDumpState.Storage.Flush == NULL) || ((DumpPartition.StartingOffset.QuadPart % KdpCrashDumpState.Storage.BytesPerSector) != 0) || ((DumpPartition.PartitionLength.QuadPart % KdpCrashDumpState.Storage.BytesPerSector) != 0) || (DumpPartition.PartitionLength.QuadPart <= KDP_RAW_LOG_RESERVE_SIZE))
    {
        Status = STATUS_REVISION_MISMATCH;
        goto Failure;
    }

    KdpCrashDumpState.RetrievalPointers = ExAllocatePoolWithTag(NonPagedPool, sizeof(RETRIEVAL_POINTERS_BUFFER), TAG_CRASH_DUMP);
    if (KdpCrashDumpState.RetrievalPointers == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Failure;
    }
    RtlZeroMemory(KdpCrashDumpState.RetrievalPointers, sizeof(RETRIEVAL_POINTERS_BUFFER));
    KdpCrashDumpState.RetrievalPointers->ExtentCount = 1;
    KdpCrashDumpState.RetrievalPointers->StartingVcn.QuadPart = 0;
    KdpCrashDumpState.RetrievalPointers->Extents[0].NextVcn.QuadPart = DumpPartition.PartitionLength.QuadPart / KdpCrashDumpState.Storage.BytesPerSector;
    KdpCrashDumpState.RetrievalPointers->Extents[0].Lcn.QuadPart = 0;
    KdpCrashDumpState.PartitionOffset = DumpPartition.StartingOffset;
    KdpCrashDumpState.CrashLogOffset = DumpPartition.PartitionLength.QuadPart - KDP_RAW_LOG_RESERVE_SIZE;
    KdpCrashDumpState.Capacity.QuadPart = KdpCrashDumpState.CrashLogOffset;
    KdpCrashDumpState.ClusterSize = KdpCrashDumpState.Storage.BytesPerSector;
    KdpCrashDumpState.Dedicated = TRUE;
    KdpCrashDumpState.DumpHandle = SystemHandle;
    KdpCrashDumpState.FileObject = SystemFileObject;
    SystemHandle = NULL;
    SystemFileObject = NULL;

    Operation = "allocating crash dump resources";
    Status = KdpAllocateCrashDumpResources("private raw partition");
    if (!NT_SUCCESS(Status))
        goto Failure;

    DPRINT1("KD: Raw crash partition: disk %lu, partition %lu, type 0x%02lx, byte %I64u, dump %I64u bytes, log %lu bytes\n", DeviceNumber.DeviceNumber, DumpPartition.PartitionNumber, PartitionType, DumpPartition.StartingOffset.QuadPart, KdpCrashDumpState.Capacity.QuadPart, KDP_RAW_LOG_RESERVE_SIZE);
    KdpSetDedicatedCrashDumpActive(TRUE);
    KdpCrashDumpInitializationStatus = STATUS_SUCCESS;
    if (DiskFileObject != NULL)
        ObDereferenceObject(DiskFileObject);
    if (DiskHandle != NULL)
        ZwClose(DiskHandle);
    return TRUE;

Failure:
    DPRINT1("KD: Raw crash dump target initialization failed while %s (0x%08lx)\n", Operation, Status);
    KdpCrashDumpInitializationStatus = Status;
    if (DiskFileObject != NULL)
        ObDereferenceObject(DiskFileObject);
    if (DiskHandle != NULL)
        ZwClose(DiskHandle);
    if (SystemFileObject != NULL)
        ObDereferenceObject(SystemFileObject);
    if (SystemHandle != NULL)
        ZwClose(SystemHandle);
    KdpCleanupCrashDumpState();
    return FALSE;
}

static NTSTATUS KdpWriteCrashLog(VOID)
{
    PKDP_RAW_LOG_HEADER Header;
    PHYSICAL_ADDRESS PhysicalAddress;
    PUCHAR Source;
    ULONG BufferOffset = 0;
    ULONG CopyLength;
    ULONG LogLength;
    ULONG Remaining;
    ULONG RolloverCount;
    ULONG TransferLimit;
    ULONG TransferLength;
    ULONG64 DiskByteOffset;
    NTSTATUS Status;

    if (!KdpCrashDumpState.Dedicated)
        return STATUS_SUCCESS;
    if ((KdpCrashDumpState.CrashLogBuffer == NULL) || (KdpCrashDumpState.CrashLogIoBuffer == NULL))
        return STATUS_INVALID_DEVICE_STATE;

    LogLength = KdpCopyPrintBuffer(KdpCrashDumpState.CrashLogBuffer, KdpCrashDumpState.CrashLogBufferSize, &RolloverCount);
    if (LogLength == 0)
        return STATUS_NOT_FOUND;

    TransferLimit = min(PAGE_SIZE, KdpCrashDumpState.Storage.MaximumTransferLength);
    TransferLimit = ALIGN_DOWN_BY(TransferLimit, KdpCrashDumpState.Storage.BytesPerSector);
    if (TransferLimit == 0)
        return STATUS_INVALID_DEVICE_STATE;

    Source = KdpCrashDumpState.CrashLogBuffer;
    Remaining = LogLength;
    DiskByteOffset = KdpCrashDumpState.PartitionOffset.QuadPart + KdpCrashDumpState.CrashLogOffset + KdpCrashDumpState.Storage.BytesPerSector;
    while (Remaining != 0)
    {
        CopyLength = min(Remaining, TransferLimit);
        TransferLength = ALIGN_UP_BY(CopyLength, KdpCrashDumpState.Storage.BytesPerSector);
        RtlZeroMemory(KdpCrashDumpState.CrashLogIoBuffer, TransferLength);
        RtlCopyMemory(KdpCrashDumpState.CrashLogIoBuffer, Source + BufferOffset, CopyLength);
        PhysicalAddress = MmGetPhysicalAddress(KdpCrashDumpState.CrashLogIoBuffer);
        Status = KdpCrashDumpState.Storage.WriteRoutine(KdpCrashDumpState.Storage.Context, DiskByteOffset, PhysicalAddress, TransferLength);
        if (!NT_SUCCESS(Status))
            return Status;

        BufferOffset += CopyLength;
        DiskByteOffset += TransferLength;
        Remaining -= CopyLength;
    }

    Status = KdpCrashDumpState.Storage.Flush(KdpCrashDumpState.Storage.Context);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(KdpCrashDumpState.CrashLogIoBuffer, KdpCrashDumpState.Storage.BytesPerSector);
    Header = KdpCrashDumpState.CrashLogIoBuffer;
    Header->Signature = KDP_RAW_LOG_SIGNATURE;
    Header->Version = KDP_RAW_LOG_VERSION;
    Header->HeaderSize = sizeof(*Header);
    Header->DataLength = LogLength;
    Header->DataCrc32 = RtlComputeCrc32(0, KdpCrashDumpState.CrashLogBuffer, LogLength);
    Header->Flags = RolloverCount != 0 ? KDP_RAW_LOG_FLAG_ROLLED : 0;
    if ((KdpCrashDumpState.CrashLogBufferSize < KdPrintBufferSize) && (LogLength == KdpCrashDumpState.CrashLogBufferSize))
        Header->Flags |= KDP_RAW_LOG_FLAG_TRUNCATED;
    Header->BugCheckCode = (ULONG)KiBugCheckData[0];
    Header->BugCheckParameters[0] = KiBugCheckData[1];
    Header->BugCheckParameters[1] = KiBugCheckData[2];
    Header->BugCheckParameters[2] = KiBugCheckData[3];
    Header->BugCheckParameters[3] = KiBugCheckData[4];
    Header->RolloverCount = RolloverCount;

    DiskByteOffset = KdpCrashDumpState.PartitionOffset.QuadPart + KdpCrashDumpState.CrashLogOffset;
    PhysicalAddress = MmGetPhysicalAddress(KdpCrashDumpState.CrashLogIoBuffer);
    Status = KdpCrashDumpState.Storage.WriteRoutine(KdpCrashDumpState.Storage.Context, DiskByteOffset, PhysicalAddress, KdpCrashDumpState.Storage.BytesPerSector);
    if (!NT_SUCCESS(Status))
        return Status;

    return KdpCrashDumpState.Storage.Flush(KdpCrashDumpState.Storage.Context);
}

NTSTATUS NTAPI KdpWriteCrashDump(VOID)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG64 FileOffset;
    ULONG HeaderSize;
    ULONG RunIndex;
    ULONG Length;
    ULONG Step = 1;
    ULONG ProgressIndex;
    ULONG64 ProgressPages[KDP_DUMP_DATA_PROGRESS_STEPS];
    ULONG64 PagesWritten = 0;
    ULONG64 Remaining;
    ULONG64 RequestedPages;
    ULONG64 TotalPages;
    NTSTATUS LogStatus = STATUS_SUCCESS;
    NTSTATUS Status;
    ULONG ValidDump;

    if (!KdpCrashDumpState.Initialized)
    {
        Status = KdpCrashDumpInitializationStatus;
        if (NT_SUCCESS(Status))
            Status = STATUS_DEVICE_NOT_READY;
        KdpDisplayCrashDumpFailure(Status);
        return Status;
    }

    if (InterlockedCompareExchange(&KdpCrashDumpActive, 1, 0) != 0)
    {
        KdpDisplayCrashDumpFailure(STATUS_DEVICE_BUSY);
        return STATUS_DEVICE_BUSY;
    }

    KdpDisplayCrashDumpProgress(0);

    Status = KeInitializeCrashDumpHeader(DUMP_TYPE_FULL, 0, KdpCrashDumpState.Header, DUMP_HEADER64_SIZE, &HeaderSize);
    if (!NT_SUCCESS(Status))
        goto Exit;
    if (HeaderSize != DUMP_HEADER64_SIZE)
    {
        Status = STATUS_INTERNAL_ERROR;
        goto Exit;
    }

    RtlCopyMemory(KdpCrashDumpState.Header->Comment, "ReactOS crash dump", sizeof("ReactOS crash dump"));

    if (KdpCrashDumpState.DumpType != DUMP_TYPE_FULL)
    {
        PSUMMARY_DUMP64 Summary = KdpCrashDumpState.Summary;
        ULONG64 MaxPages;
        ULONG Bit;

        TotalPages = MmBuildDumpPageBitmap(&KdpCrashDumpState.PageBitmap, KdpCrashDumpState.IncludeUserPages);
        RequestedPages = TotalPages;

        /* Drop the highest pages first if the dedicated target cannot take them all. */
        MaxPages = (KdpCrashDumpState.Capacity.QuadPart - DUMP_HEADER64_SIZE - KdpCrashDumpState.SummarySize) >> PAGE_SHIFT;
        if (TotalPages > MaxPages)
        {
            for (Bit = KdpCrashDumpState.PageBitmap.SizeOfBitMap; (Bit-- > 0) && (TotalPages > MaxPages);)
            {
                if (RtlCheckBit(&KdpCrashDumpState.PageBitmap, Bit))
                {
                    RtlClearBit(&KdpCrashDumpState.PageBitmap, Bit);
                    TotalPages--;
                }
            }
            KdpCrashDumpState.Header->Attributes.InsufficientDumpfileSize = TRUE;
        }

        RtlZeroMemory(Summary, KdpCrashDumpState.SummarySize);
        Summary->Signature = DUMP_SUMMARY_SIGNATURE;
        Summary->ValidDump = DUMP_SUMMARY_VALID;
        Summary->HeaderSize = DUMP_HEADER64_SIZE + KdpCrashDumpState.SummarySize;
        Summary->Pages = TotalPages;
        Summary->BitmapSize = KdpCrashDumpState.PageBitmap.SizeOfBitMap;
        RtlCopyMemory(Summary->Buffer, KdpCrashDumpState.PageBitmapBuffer, ALIGN_UP_BY(KdpCrashDumpState.PageBitmap.SizeOfBitMap, 32) / 8);

        KdpCrashDumpState.Header->DumpType = KdpCrashDumpState.DumpType;
        if (KdpCrashDumpState.IncludeUserPages)
            KdpCrashDumpState.Header->Attributes.FilterDumpFile = TRUE;
        KdpCrashDumpState.Header->RequiredDumpSpace.QuadPart = Summary->HeaderSize + (TotalPages << PAGE_SHIFT);
    }
    else
    {
        TotalPages = KdpCrashDumpState.Header->PhysicalMemoryBlock.NumberOfPages;
        RequestedPages = TotalPages;
    }

    DbgPrint("KD: Crash dump selected %I64u pages, writing %I64u pages (%I64u bytes) to a %I64u-byte target.\n", RequestedPages, TotalPages, KdpCrashDumpState.Header->RequiredDumpSpace.QuadPart, KdpCrashDumpState.Capacity.QuadPart);

    /*
     * Keep the header invalid until every selected physical page has reached
     * storage. Readers must never mistake a partial dump for a complete one.
     */
    ValidDump = KdpCrashDumpState.Header->ValidDump;
    KdpCrashDumpState.Header->ValidDump = 0;

    Status = KdpCrashDumpState.Storage.Prepare(KdpCrashDumpState.Storage.Context);
    if (!NT_SUCCESS(Status))
        goto Exit;

    FileOffset = 0;
    Status = KdpWriteStorageVirtual(KdpCrashDumpState.Header, DUMP_HEADER64_SIZE, &FileOffset);
    if (!NT_SUCCESS(Status))
        goto Exit;

    if (KdpCrashDumpState.DumpType != DUMP_TYPE_FULL)
    {
        Status = KdpWriteStorageVirtual(KdpCrashDumpState.Summary, KdpCrashDumpState.SummarySize, &FileOffset);
        if (!NT_SUCCESS(Status))
            goto Exit;
    }

    Status = KdpCrashDumpState.Storage.Flush(KdpCrashDumpState.Storage.Context);
    if (!NT_SUCCESS(Status))
        goto Exit;

    /* Precompute the 5%-through-95% page thresholds before writing pages. */
    for (ProgressIndex = 0; ProgressIndex < RTL_NUMBER_OF(ProgressPages); ProgressIndex++)
        ProgressPages[ProgressIndex] = (TotalPages * (ProgressIndex + 1) + KDP_DUMP_DATA_PROGRESS_STEPS - 1) / KDP_DUMP_DATA_PROGRESS_STEPS;

    if (KdpCrashDumpState.DumpType != DUMP_TYPE_FULL)
    {
        ULONG Bit;

        for (Bit = 0; Bit < KdpCrashDumpState.PageBitmap.SizeOfBitMap; Bit++)
        {
            if (!RtlCheckBit(&KdpCrashDumpState.PageBitmap, Bit))
                continue;

            PhysicalAddress.QuadPart = (ULONG64)Bit << PAGE_SHIFT;
            Status = KdpWriteStoragePhysical(PhysicalAddress, PAGE_SIZE, &FileOffset);
            if (!NT_SUCCESS(Status))
                goto Exit;

            PagesWritten++;
            while ((Step <= KDP_DUMP_DATA_PROGRESS_STEPS) && (PagesWritten >= ProgressPages[Step - 1]))
            {
                ULONG Percent = Step * (100 / KDP_DUMP_PROGRESS_STEPS);

                KdpDisplayCrashDumpProgress(Percent);
                Step++;
            }
        }
    }
    else
    for (RunIndex = 0; RunIndex < KdpCrashDumpState.Header->PhysicalMemoryBlock.NumberOfRuns; RunIndex++)
    {
        PhysicalAddress.QuadPart = KdpCrashDumpState.Header->PhysicalMemoryBlock.Run[RunIndex].BasePage << PAGE_SHIFT;
        Remaining = KdpCrashDumpState.Header->PhysicalMemoryBlock.Run[RunIndex].PageCount << PAGE_SHIFT;

        while (Remaining)
        {
            Length = (ULONG)min(Remaining, PAGE_SIZE);
            Status = KdpWriteStoragePhysical(PhysicalAddress, Length, &FileOffset);
            if (!NT_SUCCESS(Status))
                goto Exit;

            PhysicalAddress.QuadPart += Length;
            Remaining -= Length;
            PagesWritten += Length >> PAGE_SHIFT;
            while ((Step <= KDP_DUMP_DATA_PROGRESS_STEPS) && (PagesWritten >= ProgressPages[Step - 1]))
            {
                ULONG Percent = Step * (100 / KDP_DUMP_PROGRESS_STEPS);

                KdpDisplayCrashDumpProgress(Percent);
                Step++;
            }
        }
    }

    if ((PagesWritten != TotalPages) || (FileOffset != (ULONG64)KdpCrashDumpState.Header->RequiredDumpSpace.QuadPart))
    {
        Status = STATUS_INTERNAL_ERROR;
        goto Exit;
    }

    Status = KdpCrashDumpState.Storage.Flush(KdpCrashDumpState.Storage.Context);
    if (!NT_SUCCESS(Status))
        goto Exit;

    /* Reserve the final 5% for the crash log and valid-header commit. */
    if (Step <= KDP_DUMP_DATA_PROGRESS_STEPS)
        KdpDisplayCrashDumpProgress(95);
    DbgPrint("\nKD: Physical memory data written; saving crash boot log.\n");
    LogStatus = KdpWriteCrashLog();
    if (!NT_SUCCESS(LogStatus)) DbgPrint("KD: Crash boot log write failed with status 0x%08lx.\n", LogStatus);

    KdpCrashDumpState.Header->ValidDump = ValidDump;
    KdpCrashDumpState.Header->WriterStatus = STATUS_SUCCESS;
    FileOffset = 0;
    Status = KdpWriteStorageVirtual(KdpCrashDumpState.Header, DUMP_HEADER64_SIZE, &FileOffset);
    if (!NT_SUCCESS(Status))
        goto Exit;

    Status = KdpCrashDumpState.Storage.Flush(KdpCrashDumpState.Storage.Context);
    if (NT_SUCCESS(Status))
    {
        if (NT_SUCCESS(LogStatus))
        {
            KdpDisplayCrashDumpProgress(100);
            DbgPrint("\nKD: Memory dump and crash boot log saved successfully.\n");
            InbvDisplayString("\r\nCrash dump status: memory dump and boot log saved successfully.\r\n");
        }
        else
        {
            KdpDisplayCrashLogFailure(LogStatus);
        }
    }

Exit:
    if (!NT_SUCCESS(Status))
        KdpDisplayCrashDumpFailure(Status);

    InterlockedExchange(&KdpCrashDumpActive, 0);
    if (NT_SUCCESS(Status) && !NT_SUCCESS(LogStatus))
        return LogStatus;
    return Status;
}

#else

/*
 * Crash dump writing is only implemented for 64-bit targets so far. The callers in
 * Io/Ke are architecture independent, so provide the two entry points here
 * rather than making every call site test for the architecture.
 */
BOOLEAN NTAPI KdpInitializeCrashDump(_In_ HANDLE DumpFileHandle)
{
    UNREFERENCED_PARAMETER(DumpFileHandle);
    return FALSE;
}

BOOLEAN NTAPI KdpInitializeDedicatedCrashDump(VOID)
{
    return FALSE;
}

NTSTATUS NTAPI KdpWriteCrashDump(VOID)
{
    return STATUS_NOT_SUPPORTED;
}

#endif

#if (NTDDI_VERSION >= NTDDI_WINBLUE)
NTSTATUS NTAPI KdpWriteLiveKernelDump(_In_ PSYSDBG_LIVEDUMP_CONTROL Control, _In_ KPROCESSOR_MODE PreviousMode)
{
#if defined(_M_AMD64)
    OBJECT_ATTRIBUTES ObjectAttributes;
    FILE_END_OF_FILE_INFORMATION EndOfFile;
    IO_STATUS_BLOCK IoStatus;
    SYSDBG_LIVEDUMP_CONTROL_FLAGS SupportedFlags;
    PDUMP_HEADER64 Header = NULL;
    PSUMMARY_DUMP64 Summary = NULL;
    PULONG BitmapBuffer = NULL;
    RTL_BITMAP Bitmap;
    PPHYSICAL_MEMORY_RUN LastRun;
    PFILE_OBJECT FileObject = NULL;
    PKEVENT CancelEvent = NULL;
    HANDLE DumpHandle = NULL;
    HANDLE IoEventHandle = NULL;
    LARGE_INTEGER FileOffset;
    PHYSICAL_ADDRESS PhysicalAddress;
    PVOID MappedAddress;
    ULONG HeaderSize;
    ULONG SummarySize;
    ULONG BitmapBits;
    ULONG BitmapBytes;
    ULONG Bit;
    ULONG BatchLimit;
    ULONG Length;
    ULONG64 TotalPages;
    NTSTATUS Status;
    PCSTR Operation = "validating parameters";

    if (!Control)
        return STATUS_INVALID_PARAMETER;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;

    if (Control->Version != SYSDBG_LIVEDUMP_CONTROL_VERSION)
        return STATUS_REVISION_MISMATCH;

    if (!Control->DumpFileHandle)
        return STATUS_INVALID_HANDLE;

    /* Only the user-space page opt-in is honored so far */
    SupportedFlags.AsUlong = 0;
    SupportedFlags.IncludeUserSpaceMemoryPages = 1;
    if ((Control->Flags.AsUlong & ~SupportedFlags.AsUlong) || Control->AddPagesControl.AsUlong)
        return STATUS_NOT_SUPPORTED;

    if (InterlockedCompareExchange(&KdpLiveDumpActive, 1, 0) != 0)
        return STATUS_DEVICE_BUSY;

    Operation = "duplicating the output file handle";
    Status = ObDuplicateObject(PsGetCurrentProcess(), (HANDLE)Control->DumpFileHandle, PsInitialSystemProcess, &DumpHandle, 0, OBJ_KERNEL_HANDLE, DUPLICATE_SAME_ACCESS, KernelMode);
    if (!NT_SUCCESS(Status))
        goto Exit;

    Operation = "referencing the output file";
    Status = ObReferenceObjectByHandle(DumpHandle, FILE_WRITE_DATA, IoFileObjectType, KernelMode, (PVOID *)&FileObject, NULL);
    if (!NT_SUCCESS(Status))
        goto Exit;

    if (Control->CancelEventHandle)
    {
        Operation = "referencing the cancel event";
        Status = ObReferenceObjectByHandle((HANDLE)Control->CancelEventHandle, EVENT_QUERY_STATE, ExEventObjectType, PreviousMode, (PVOID *)&CancelEvent, NULL);
        if (!NT_SUCCESS(Status))
            goto Exit;
    }

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Operation = "creating the write event";
    Status = ZwCreateEvent(&IoEventHandle, EVENT_ALL_ACCESS, &ObjectAttributes, SynchronizationEvent, FALSE);
    if (!NT_SUCCESS(Status))
        goto Exit;

    Header = ExAllocatePoolWithTag(NonPagedPool, DUMP_HEADER64_SIZE, TAG_LIVE_DUMP);
    if (!Header)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    KeIpiGenericCall(KdpCaptureLiveDumpContext, 0);
    Operation = "initializing the dump header";
    Status = KeInitializeCrashDumpHeader(DUMP_TYPE_FULL, 0, Header, DUMP_HEADER64_SIZE, &HeaderSize);
    if (!NT_SUCCESS(Status))
        goto Exit;
    if (HeaderSize != DUMP_HEADER64_SIZE)
    {
        Status = STATUS_INTERNAL_ERROR;
        goto Exit;
    }

    Header->BugCheckCode = Control->BugCheckCode ? Control->BugCheckCode : KDP_LIVE_DUMP_BUGCHECK;
    Header->BugCheckParameter1 = Control->BugCheckParam1;
    Header->BugCheckParameter2 = Control->BugCheckParam2;
    Header->BugCheckParameter3 = Control->BugCheckParam3;
    Header->BugCheckParameter4 = Control->BugCheckParam4;
    Header->Attributes.LiveDumpGeneratedDump = TRUE;
    RtlCopyMemory(Header->Comment, "ReactOS live kernel dump", sizeof("ReactOS live kernel dump"));

    /* Live dumps carry only the in-use pages: kernel-space ones by default,
     * user-space ones as well when the caller asked for them */
    LastRun = &MmPhysicalMemoryBlock->Run[MmPhysicalMemoryBlock->NumberOfRuns - 1];
    BitmapBits = (ULONG)(LastRun->BasePage + LastRun->PageCount);
    BitmapBytes = ALIGN_UP_BY(BitmapBits, 32) / 8;
    Operation = "allocating the dump page bitmap";
    BitmapBuffer = ExAllocatePoolWithTag(NonPagedPool, BitmapBytes, TAG_LIVE_DUMP);
    if (!BitmapBuffer)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    RtlInitializeBitMap(&Bitmap, BitmapBuffer, BitmapBits);

    SummarySize = ALIGN_UP_BY(FIELD_OFFSET(SUMMARY_DUMP64, Buffer) + BitmapBytes, PAGE_SIZE);
    Operation = "allocating the summary dump header";
    Summary = ExAllocatePoolWithTag(NonPagedPool, SummarySize, TAG_LIVE_DUMP);
    if (!Summary)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    TotalPages = MmBuildDumpPageBitmap(&Bitmap, Control->Flags.IncludeUserSpaceMemoryPages ? TRUE : FALSE);

    RtlZeroMemory(Summary, SummarySize);
    Summary->Signature = DUMP_SUMMARY_SIGNATURE;
    Summary->ValidDump = DUMP_SUMMARY_VALID;
    Summary->HeaderSize = DUMP_HEADER64_SIZE + SummarySize;
    Summary->Pages = TotalPages;
    Summary->BitmapSize = BitmapBits;
    RtlCopyMemory(Summary->Buffer, BitmapBuffer, BitmapBytes);

    Header->DumpType = Control->Flags.IncludeUserSpaceMemoryPages ? DUMP_TYPE_BITMAP_FULL : DUMP_TYPE_BITMAP_KERNEL;
    if (Control->Flags.IncludeUserSpaceMemoryPages)
        Header->Attributes.FilterDumpFile = TRUE;
    Header->RequiredDumpSpace.QuadPart = Summary->HeaderSize + (TotalPages << PAGE_SHIFT);

    EndOfFile.EndOfFile = Header->RequiredDumpSpace;
    Operation = "setting the dump file size";
    Status = ZwSetInformationFile(DumpHandle, &IoStatus, &EndOfFile, sizeof(EndOfFile), FileEndOfFileInformation);
    if (!NT_SUCCESS(Status))
        goto Exit;

    FileOffset.QuadPart = 0;
    Operation = "writing the dump header";
    Status = KdpWriteDumpFileChunk(DumpHandle, IoEventHandle, Header, DUMP_HEADER64_SIZE, &FileOffset);
    if (!NT_SUCCESS(Status))
        goto Exit;

    Operation = "writing the summary dump header";
    Status = KdpWriteDumpFileChunk(DumpHandle, IoEventHandle, Summary, SummarySize, &FileOffset);
    if (!NT_SUCCESS(Status))
        goto Exit;

    BatchLimit = KDP_LIVE_DUMP_WRITE_SIZE >> PAGE_SHIFT;
    Bit = 0;
    while (Bit < BitmapBits)
    {
        ULONG BatchPages;

        if (!RtlCheckBit(&Bitmap, Bit))
        {
            Bit++;
            continue;
        }

        /* Batch physically contiguous marked pages into one mapping/write */
        BatchPages = 1;
        while (((Bit + BatchPages) < BitmapBits) &&
               (BatchPages < BatchLimit) &&
               RtlCheckBit(&Bitmap, Bit + BatchPages))
        {
            BatchPages++;
        }

        if (CancelEvent && KeReadStateEvent(CancelEvent))
        {
            Status = STATUS_CANCELLED;
            goto Exit;
        }

        PhysicalAddress.QuadPart = (ULONG64)Bit << PAGE_SHIFT;
        Length = BatchPages << PAGE_SHIFT;
        MappedAddress = MmMapIoSpace(PhysicalAddress, Length, MmCached);
        if (!MappedAddress)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Exit;
        }

        Operation = "writing physical memory";
        Status = KdpWriteDumpFileChunk(DumpHandle, IoEventHandle, MappedAddress, Length, &FileOffset);
        MmUnmapIoSpace(MappedAddress, Length);
        if (!NT_SUCCESS(Status))
            goto Exit;

        Bit += BatchPages;
    }

    Operation = "flushing the dump file";
    Status = ZwFlushBuffersFile(DumpHandle, &IoStatus);

Exit:
    if (!NT_SUCCESS(Status))
        DPRINT1("KD: Live dump failed while %s (0x%08lx)\n", Operation, Status);

    if (Summary)
        ExFreePoolWithTag(Summary, TAG_LIVE_DUMP);
    if (BitmapBuffer)
        ExFreePoolWithTag(BitmapBuffer, TAG_LIVE_DUMP);
    if (Header)
        ExFreePoolWithTag(Header, TAG_LIVE_DUMP);
    if (IoEventHandle)
        ZwClose(IoEventHandle);
    if (CancelEvent)
        ObDereferenceObject(CancelEvent);
    if (DumpHandle)
        ZwClose(DumpHandle);
    if (FileObject)
        ObDereferenceObject(FileObject);

    InterlockedExchange(&KdpLiveDumpActive, 0);
    return Status;
#else
    UNREFERENCED_PARAMETER(Control);
    UNREFERENCED_PARAMETER(PreviousMode);
    return STATUS_NOT_SUPPORTED;
#endif
}
#endif
