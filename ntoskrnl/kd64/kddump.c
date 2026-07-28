/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Generate Windows-compatible kernel dumps
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <internal/dump.h>
#include <ntdddisk.h>
#include <ntoskrnl.h>
#include <reactos/drivers/dumpstor.h>

#define NDEBUG
#include <debug.h>

#if defined(_M_AMD64)

#define KDP_LIVE_DUMP_BUGCHECK 0x161
#define KDP_LIVE_DUMP_WRITE_SIZE (1024 * 1024)
#define TAG_LIVE_DUMP 'pDmK'
#define TAG_CRASH_DUMP 'pDcK'
#define KDP_DUMP_EXTENT_BUFFER_SIZE (64 * 1024)

static LONG KdpLiveDumpActive;
static LONG KdpCrashDumpActive;

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
    BOOLEAN Initialized;
} KDP_CRASH_DUMP_STATE, *PKDP_CRASH_DUMP_STATE;

static KDP_CRASH_DUMP_STATE KdpCrashDumpState;

/* Progress is reported in 100 / KDP_DUMP_PROGRESS_STEPS percent increments. */
#define KDP_DUMP_PROGRESS_STEPS 20

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
        Status = KdpCrashDumpState.Storage.Write(KdpCrashDumpState.Storage.Context, DiskByteOffset, PhysicalAddress, TransferLength);
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

static ULONG_PTR NTAPI KdpCaptureLiveDumpContext(_In_ ULONG_PTR Argument)
{
    UNREFERENCED_PARAMETER(Argument);

    RtlCaptureContext(KeGetCurrentPrcb()->CrashDumpContext);
    return 0;
}

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

BOOLEAN NTAPI KdpInitializeCrashDump(_In_ HANDLE PageFileHandle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    FILE_FS_SIZE_INFORMATION SizeInfo;
    FILE_STANDARD_INFORMATION StandardInfo;
    PARTITION_INFORMATION_EX PartitionInfo;
    STARTING_VCN_INPUT_BUFFER StartingVcn;
    PDEVICE_OBJECT StorageDevice;
    PVPB Vpb;
    PCSTR Operation = "duplicating the pagefile handle";
    IO_STATUS_BLOCK IoStatus;
    ULONG HeaderSize;
    NTSTATUS Status;
    PAGED_CODE();

    if (KdpCrashDumpState.Initialized)
        return TRUE;

    Status = ObDuplicateObject(PsGetCurrentProcess(), PageFileHandle, PsInitialSystemProcess, &KdpCrashDumpState.DumpHandle, 0, OBJ_KERNEL_HANDLE, DUPLICATE_SAME_ACCESS, KernelMode);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Operation = "referencing the pagefile";
    Status = ObReferenceObjectByHandle(KdpCrashDumpState.DumpHandle, FILE_WRITE_DATA, IoFileObjectType, KernelMode, (PVOID *)&KdpCrashDumpState.FileObject, NULL);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Operation = "querying the pagefile";
    Status = ZwQueryInformationFile(KdpCrashDumpState.DumpHandle, &IoStatus, &StandardInfo, sizeof(StandardInfo), FileStandardInformation);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Operation = "querying the pagefile volume";
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

    if ((KdpCrashDumpState.Storage.Version != ROS_STORAGE_DUMP_INTERFACE_VERSION) || (KdpCrashDumpState.Storage.Size < sizeof(KdpCrashDumpState.Storage)) || (KdpCrashDumpState.Storage.Context == NULL) || (KdpCrashDumpState.Storage.BytesPerSector == 0) || (KdpCrashDumpState.Storage.MaximumTransferLength == 0) || (KdpCrashDumpState.Storage.Prepare == NULL) || (KdpCrashDumpState.Storage.Write == NULL) || (KdpCrashDumpState.Storage.Flush == NULL) || ((KdpCrashDumpState.ClusterSize % KdpCrashDumpState.Storage.BytesPerSector) != 0))
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
    Operation = "querying pagefile retrieval pointers";
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

    KdpCrashDumpState.Header = ExAllocatePoolWithTag(NonPagedPool, DUMP_HEADER64_SIZE, TAG_CRASH_DUMP);
    if (!KdpCrashDumpState.Header)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Failure;
    }

    Status = KeInitializeCrashDumpHeader(DUMP_TYPE_FULL, 0, KdpCrashDumpState.Header, DUMP_HEADER64_SIZE, &HeaderSize);
    if (!NT_SUCCESS(Status))
        goto Failure;
    if (HeaderSize != DUMP_HEADER64_SIZE)
    {
        Status = STATUS_INTERNAL_ERROR;
        goto Failure;
    }

    KdpCrashDumpState.Capacity = StandardInfo.EndOfFile;
    if (KdpCrashDumpState.Capacity.QuadPart < KdpCrashDumpState.Header->RequiredDumpSpace.QuadPart)
    {
        Status = STATUS_DISK_FULL;
        goto Failure;
    }

    KdpCrashDumpState.Initialized = TRUE;
    DPRINT1("KD: Crash dump target initialized (%I64u bytes available, %I64u required)\n", KdpCrashDumpState.Capacity.QuadPart, KdpCrashDumpState.Header->RequiredDumpSpace.QuadPart);
    return TRUE;

Failure:
    DPRINT1("KD: Crash dump target initialization failed while %s (0x%08lx)\n", Operation, Status);

    if (KdpCrashDumpState.Header)
    {
        ExFreePoolWithTag(KdpCrashDumpState.Header, TAG_CRASH_DUMP);
        KdpCrashDumpState.Header = NULL;
    }
    if (KdpCrashDumpState.RetrievalPointers)
    {
        ExFreePoolWithTag(KdpCrashDumpState.RetrievalPointers, TAG_CRASH_DUMP);
        KdpCrashDumpState.RetrievalPointers = NULL;
    }
    if (KdpCrashDumpState.IoEventHandle)
    {
        ZwClose(KdpCrashDumpState.IoEventHandle);
        KdpCrashDumpState.IoEventHandle = NULL;
    }
    if (KdpCrashDumpState.FileObject)
    {
        ObDereferenceObject(KdpCrashDumpState.FileObject);
        KdpCrashDumpState.FileObject = NULL;
    }
    if (KdpCrashDumpState.DumpHandle)
    {
        ZwClose(KdpCrashDumpState.DumpHandle);
        KdpCrashDumpState.DumpHandle = NULL;
    }
    return FALSE;
}

NTSTATUS NTAPI KdpWriteCrashDump(VOID)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG64 FileOffset;
    ULONG HeaderSize;
    ULONG RunIndex;
    ULONG Length;
    ULONG Step = 1;
    ULONG64 PagesPerStep;
    ULONG64 NextStepPage;
    ULONG64 PagesWritten = 0;
    ULONG64 Remaining;
    NTSTATUS Status;
    ULONG ValidDump;
    CHAR ProgressText[64];

    if (!KdpCrashDumpState.Initialized)
        return STATUS_DEVICE_NOT_READY;

    if (InterlockedCompareExchange(&KdpCrashDumpActive, 1, 0) != 0)
        return STATUS_DEVICE_BUSY;

    Status = KeInitializeCrashDumpHeader(DUMP_TYPE_FULL, 0, KdpCrashDumpState.Header, DUMP_HEADER64_SIZE, &HeaderSize);
    if (!NT_SUCCESS(Status))
        goto Exit;
    if (HeaderSize != DUMP_HEADER64_SIZE)
    {
        Status = STATUS_INTERNAL_ERROR;
        goto Exit;
    }

    RtlCopyMemory(KdpCrashDumpState.Header->Comment, "ReactOS crash dump", sizeof("ReactOS crash dump"));

    /*
     * Keep the pagefile header invalid until every physical page has reached
     * storage. SMSS must never mistake a partial dump for a complete one.
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

    Status = KdpCrashDumpState.Storage.Flush(KdpCrashDumpState.Storage.Context);
    if (!NT_SUCCESS(Status))
        goto Exit;

    DbgPrint("Dumping physical memory to disk:   0");
    InbvDisplayString("\r\nDumping physical memory to disk:   0");

    /*
     * Progress is reported in 5% steps. Precompute the page count per step so
     * the write loop never divides: this runs at HIGH_LEVEL, once per page.
     */
    PagesPerStep = (KdpCrashDumpState.Header->PhysicalMemoryBlock.NumberOfPages + KDP_DUMP_PROGRESS_STEPS - 1) / KDP_DUMP_PROGRESS_STEPS;
    if (PagesPerStep == 0)
        PagesPerStep = 1;
    NextStepPage = PagesPerStep;

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
            if ((PagesWritten >= NextStepPage) && (Step <= KDP_DUMP_PROGRESS_STEPS))
            {
                ULONG Percent = Step * (100 / KDP_DUMP_PROGRESS_STEPS);

                DbgPrint("\b\b\b%3lu", Percent);
                RtlStringCbPrintfA(ProgressText, sizeof(ProgressText), "\rDumping physical memory to disk: %3lu", Percent);
                InbvDisplayString(ProgressText);
                NextStepPage += PagesPerStep;
                Step++;
            }
        }
    }

    Status = KdpCrashDumpState.Storage.Flush(KdpCrashDumpState.Storage.Context);
    if (!NT_SUCCESS(Status))
        goto Exit;

    KdpCrashDumpState.Header->ValidDump = ValidDump;
    KdpCrashDumpState.Header->WriterStatus = STATUS_SUCCESS;
    FileOffset = 0;
    Status = KdpWriteStorageVirtual(KdpCrashDumpState.Header, DUMP_HEADER64_SIZE, &FileOffset);
    if (!NT_SUCCESS(Status))
        goto Exit;

    Status = KdpCrashDumpState.Storage.Flush(KdpCrashDumpState.Storage.Context);
    if (NT_SUCCESS(Status))
    {
        DbgPrint("\nPhysical memory dump complete.\n");
        InbvDisplayString("\r\nPhysical memory dump complete.\r\n");
    }

Exit:
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("\nPhysical memory dump failed with status 0x%08lx.\n", Status);
        InbvDisplayString("\r\nPhysical memory dump failed.\r\n");
    }

    InterlockedExchange(&KdpCrashDumpActive, 0);
    return Status;
}

#endif

#if (NTDDI_VERSION >= NTDDI_WINBLUE)
NTSTATUS NTAPI KdpWriteLiveKernelDump(_In_ PSYSDBG_LIVEDUMP_CONTROL Control, _In_ KPROCESSOR_MODE PreviousMode)
{
#if defined(_M_AMD64)
    OBJECT_ATTRIBUTES ObjectAttributes;
    FILE_END_OF_FILE_INFORMATION EndOfFile;
    IO_STATUS_BLOCK IoStatus;
    PDUMP_HEADER64 Header = NULL;
    PFILE_OBJECT FileObject = NULL;
    PKEVENT CancelEvent = NULL;
    HANDLE DumpHandle = NULL;
    HANDLE IoEventHandle = NULL;
    LARGE_INTEGER FileOffset;
    PHYSICAL_ADDRESS PhysicalAddress;
    PVOID MappedAddress;
    ULONG HeaderSize;
    ULONG RunIndex;
    ULONG Length;
    ULONG64 Remaining;
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

    if (Control->Flags.AsUlong || Control->AddPagesControl.AsUlong)
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

    for (RunIndex = 0; RunIndex < Header->PhysicalMemoryBlock.NumberOfRuns; RunIndex++)
    {
        PhysicalAddress.QuadPart = Header->PhysicalMemoryBlock.Run[RunIndex].BasePage << PAGE_SHIFT;
        Remaining = Header->PhysicalMemoryBlock.Run[RunIndex].PageCount << PAGE_SHIFT;

        while (Remaining)
        {
            if (CancelEvent && KeReadStateEvent(CancelEvent))
            {
                Status = STATUS_CANCELLED;
                goto Exit;
            }

            Length = (ULONG)min(Remaining, KDP_LIVE_DUMP_WRITE_SIZE);
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

            PhysicalAddress.QuadPart += Length;
            Remaining -= Length;
        }
    }

    Operation = "flushing the dump file";
    Status = ZwFlushBuffersFile(DumpHandle, &IoStatus);

Exit:
    if (!NT_SUCCESS(Status))
        DPRINT1("KD: Live dump failed while %s (0x%08lx)\n", Operation, Status);

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
