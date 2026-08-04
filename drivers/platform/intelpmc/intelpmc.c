/*
 * PROJECT:     ReactOS Intel Power Management Controller Driver
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Alder Lake-N PMC ownership, S0ix diagnostics, and SMP suspend policy
 */

#include <ntddk.h>
#include <intrin.h>
#include <reactos/drivers/acpi/acpisystem.h>
#include <initguid.h>
#include <reactos/drivers/intelpmc.h>

#define NDEBUG
#include <debug.h>

#define INTELPMC_TAG 'cmPI'
#define INTELPMC_ACPI_TABLE_LIMIT 65536
#define INTELPMC_MODEL_ALDER_LAKE_N 0xBE
#define INTELPMC_BASE_DEFAULT 0xFE000000ULL
#define INTELPMC_LENGTH 0x2000
#define INTELPMC_SLP_S0_OFFSET 0x193C
#define INTELPMC_PM_CFG_OFFSET 0x1818
#define INTELPMC_PM_READ_DISABLE (1UL << 22)
#define INTELPMC_LTR_IGNORE_OFFSET 0x1B0C
#define INTELPMC_LTR_IGNORE_GBE (1UL << 3)
#define INTELPMC_PPFEAR_OFFSET 0x1D90
#define INTELPMC_LPM_STATUS_OFFSET 0x1C3C
#define INTELPMC_LPM_LIVE_STATUS_OFFSET 0x1C5C
#define INTELPMC_LPM_ENABLE_OFFSET 0x1C78
#define INTELPMC_LPM_PRIORITY_OFFSET 0x1C7C
#define INTELPMC_LPM_RESIDENCY_OFFSET 0x1C80
#define INTELPMC_MSR_PKG_CST_CONFIG 0xE2
#define INTELPMC_C1_AUTO_DEMOTE (1ULL << 26)

#define INTELPMC_POWER_NONE 0
#define INTELPMC_POWER_ROLLBACK_ON_FAILURE 1
#define INTELPMC_POWER_RESUME 2

#include <pshpack1.h>
typedef struct _INTELPMC_ACPI_TABLE_HEADER
{
    CHAR Signature[4];
    ULONG Length;
    UCHAR Revision;
    UCHAR Checksum;
    CHAR OemId[6];
    CHAR OemTableId[8];
    ULONG OemRevision;
    ULONG AslCompilerId;
    ULONG AslCompilerRevision;
} INTELPMC_ACPI_TABLE_HEADER, *PINTELPMC_ACPI_TABLE_HEADER;

typedef struct _INTELPMC_ACPI_GENERIC_ADDRESS
{
    UCHAR SpaceId;
    UCHAR BitWidth;
    UCHAR BitOffset;
    UCHAR AccessWidth;
    ULONGLONG Address;
} INTELPMC_ACPI_GENERIC_ADDRESS, *PINTELPMC_ACPI_GENERIC_ADDRESS;

typedef struct _INTELPMC_LPIT_HEADER
{
    ULONG Type;
    ULONG Length;
    USHORT UniqueId;
    USHORT Reserved;
    ULONG Flags;
} INTELPMC_LPIT_HEADER, *PINTELPMC_LPIT_HEADER;

typedef struct _INTELPMC_LPIT_NATIVE
{
    INTELPMC_LPIT_HEADER Header;
    INTELPMC_ACPI_GENERIC_ADDRESS EntryTrigger;
    ULONG Residency;
    ULONG Latency;
    INTELPMC_ACPI_GENERIC_ADDRESS ResidencyCounter;
    ULONGLONG CounterFrequency;
} INTELPMC_LPIT_NATIVE, *PINTELPMC_LPIT_NATIVE;
#include <poppack.h>

typedef struct _INTELPMC_DEVICE_EXTENSION
{
    PDEVICE_OBJECT PhysicalDevice;
    PDEVICE_OBJECT LowerDevice;
    IO_REMOVE_LOCK RemoveLock;
    UNICODE_STRING InterfaceName;
    KSPIN_LOCK RegisterLock;
    PUCHAR RegisterBase;
    PHYSICAL_ADDRESS PhysicalBase;
    ULONG ProcessorModel;
    ULONG ActiveProcessors;
    ULONG SavedLtrIgnore;
    ULONG SleepResidency;
    ULONGLONG SavedCState[MAXIMUM_PROCESSORS];
    UCHAR SavedCStateValid[MAXIMUM_PROCESSORS];
    volatile LONG SleepPrepared;
    BOOLEAN Started;
    BOOLEAN BaseFromLpit;
} INTELPMC_DEVICE_EXTENSION, *PINTELPMC_DEVICE_EXTENSION;

typedef struct _INTELPMC_POWER_CONTEXT
{
    PINTELPMC_DEVICE_EXTENSION DeviceExtension;
    ULONG Action;
} INTELPMC_POWER_CONTEXT, *PINTELPMC_POWER_CONTEXT;

static
NTSTATUS
NTAPI
IntelPmcCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
IntelPmcForwardSynchronously(
    _In_ PINTELPMC_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, IntelPmcCompletion, &Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    return Status;
}

static
NTSTATUS
IntelPmcSendIoctlSynchronously(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputLength) PVOID InputBuffer,
    _In_ ULONG InputLength,
    _Out_writes_bytes_opt_(OutputLength) PVOID OutputBuffer,
    _In_ ULONG OutputLength,
    _Out_ PULONG_PTR Information)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IoControlCode, DeviceObject, InputBuffer, InputLength, OutputBuffer, OutputLength, FALSE, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    *Information = IoStatus.Information;
    return Status;
}

static
NTSTATUS
IntelPmcReadAcpiTable(
    _In_reads_(4) const CHAR Signature[4],
    _Outptr_result_bytebuffer_(*TableLength) PUCHAR *TableBuffer,
    _Out_ PULONG TableLength)
{
    ACPI_GET_SYSTEM_TABLE_INPUT Input;
    PFILE_OBJECT FileObject = NULL;
    PDEVICE_OBJECT DeviceObject = NULL;
    UNICODE_STRING InterfaceName;
    PWSTR InterfaceList = NULL;
    PUCHAR Buffer = NULL;
    ULONG BufferLength = 256;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    *TableBuffer = NULL;
    *TableLength = 0;
    Status = IoGetDeviceInterfaces(&GUID_ACPI_SYSTEM_INTERFACE, NULL, 0, &InterfaceList);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!InterfaceList || InterfaceList[0] == UNICODE_NULL)
    {
        Status = STATUS_NOT_FOUND;
        goto Cleanup;
    }
    RtlInitUnicodeString(&InterfaceName, InterfaceList);
    Status = IoGetDeviceObjectPointer(&InterfaceName, FILE_READ_DATA | SYNCHRONIZE, &FileObject, &DeviceObject);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    RtlCopyMemory(Input.Signature, Signature, sizeof(Input.Signature));
    Input.Instance = 1;
    for (;;)
    {
        Buffer = ExAllocatePoolWithTag(PagedPool, BufferLength, INTELPMC_TAG);
        if (!Buffer)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }
        Status = IntelPmcSendIoctlSynchronously(DeviceObject, IOCTL_ACPI_GET_SYSTEM_TABLE, &Input, sizeof(Input), Buffer, BufferLength, &Information);
        if (Status != STATUS_BUFFER_TOO_SMALL)
            break;
        ExFreePoolWithTag(Buffer, INTELPMC_TAG);
        Buffer = NULL;
        if (Information <= BufferLength || Information > INTELPMC_ACPI_TABLE_LIMIT)
        {
            Status = STATUS_ACPI_INVALID_DATA;
            break;
        }
        BufferLength = (ULONG)Information;
    }
    if (NT_SUCCESS(Status))
    {
        if (Information < sizeof(INTELPMC_ACPI_TABLE_HEADER) || Information > BufferLength)
            Status = STATUS_ACPI_INVALID_DATA;
        else
        {
            *TableBuffer = Buffer;
            *TableLength = (ULONG)Information;
            Buffer = NULL;
        }
    }

Cleanup:
    if (Buffer)
        ExFreePoolWithTag(Buffer, INTELPMC_TAG);
    if (FileObject)
        ObDereferenceObject(FileObject);
    if (InterfaceList)
        ExFreePool(InterfaceList);
    return Status;
}

static
NTSTATUS
IntelPmcFindLpitBase(
    _Out_ PPHYSICAL_ADDRESS PhysicalBase)
{
    PINTELPMC_ACPI_TABLE_HEADER Table;
    PINTELPMC_LPIT_HEADER Header;
    PINTELPMC_LPIT_NATIVE Native;
    PUCHAR Buffer;
    PUCHAR Cursor;
    PUCHAR End;
    ULONG TableLength;
    NTSTATUS Status;

    Status = IntelPmcReadAcpiTable("LPIT", &Buffer, &TableLength);
    if (!NT_SUCCESS(Status))
        return Status;
    Table = (PINTELPMC_ACPI_TABLE_HEADER)Buffer;
    if (RtlCompareMemory(Table->Signature, "LPIT", 4) != 4 || Table->Length < sizeof(*Table) || Table->Length > TableLength)
    {
        Status = STATUS_ACPI_INVALID_DATA;
        goto Cleanup;
    }
    Cursor = Buffer + sizeof(*Table);
    End = Buffer + Table->Length;
    Status = STATUS_NOT_FOUND;
    while ((ULONG_PTR)(End - Cursor) >= sizeof(*Header))
    {
        Header = (PINTELPMC_LPIT_HEADER)Cursor;
        if (Header->Length < sizeof(*Header) || Header->Length > (ULONG)(End - Cursor))
        {
            Status = STATUS_ACPI_INVALID_DATA;
            break;
        }
        if (Header->Type == 0 && Header->Flags == 0 && Header->Length >= sizeof(*Native))
        {
            Native = (PINTELPMC_LPIT_NATIVE)Cursor;
            if (Native->ResidencyCounter.SpaceId == 0 && Native->ResidencyCounter.BitWidth >= 32 && Native->ResidencyCounter.Address >= INTELPMC_SLP_S0_OFFSET)
            {
                PhysicalBase->QuadPart = Native->ResidencyCounter.Address - INTELPMC_SLP_S0_OFFSET;
                Status = STATUS_SUCCESS;
                break;
            }
        }
        Cursor += Header->Length;
    }

Cleanup:
    ExFreePoolWithTag(Buffer, INTELPMC_TAG);
    return Status;
}

static
NTSTATUS
IntelPmcGetProcessorModel(
    _Out_ PULONG ProcessorModel)
{
#if defined(_M_IX86) || defined(_M_AMD64)
    int Registers[4];
    ULONG Family;
    ULONG Model;

    __cpuid(Registers, 0);
    if (Registers[1] != 0x756E6547 || Registers[3] != 0x49656E69 || Registers[2] != 0x6C65746E)
        return STATUS_NOT_SUPPORTED;
    __cpuid(Registers, 1);
    Family = ((ULONG)Registers[0] >> 8) & 0xF;
    Model = ((ULONG)Registers[0] >> 4) & 0xF;
    if (Family == 0xF)
        Family += ((ULONG)Registers[0] >> 20) & 0xFF;
    if (Family == 0x6 || Family == 0xF)
        Model |= ((ULONG)Registers[0] >> 12) & 0xF0;
    if (Family != 6 || Model != INTELPMC_MODEL_ALDER_LAKE_N)
        return STATUS_NOT_SUPPORTED;
    *ProcessorModel = Model;
    return STATUS_SUCCESS;
#else
    UNREFERENCED_PARAMETER(ProcessorModel);
    return STATUS_NOT_SUPPORTED;
#endif
}

static
ULONG
IntelPmcReadUlong(
    _In_ PINTELPMC_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(DeviceExtension->RegisterBase + Offset));
}

static
VOID
IntelPmcWriteUlong(
    _In_ PINTELPMC_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(DeviceExtension->RegisterBase + Offset), Value);
}

static
ULONG_PTR
NTAPI
IntelPmcDisableC1AutoDemote(
    _In_ ULONG_PTR Context)
{
#if defined(_M_IX86) || defined(_M_AMD64)
    PINTELPMC_DEVICE_EXTENSION DeviceExtension = (PINTELPMC_DEVICE_EXTENSION)Context;
    ULONG Processor = KeGetCurrentProcessorNumber();
    ULONGLONG Value;

    if (Processor < MAXIMUM_PROCESSORS)
    {
        Value = __readmsr(INTELPMC_MSR_PKG_CST_CONFIG);
        DeviceExtension->SavedCState[Processor] = Value;
        DeviceExtension->SavedCStateValid[Processor] = TRUE;
        __writemsr(INTELPMC_MSR_PKG_CST_CONFIG, Value & ~INTELPMC_C1_AUTO_DEMOTE);
    }
#else
    UNREFERENCED_PARAMETER(Context);
#endif
    return 0;
}

static
ULONG_PTR
NTAPI
IntelPmcRestoreC1AutoDemote(
    _In_ ULONG_PTR Context)
{
#if defined(_M_IX86) || defined(_M_AMD64)
    PINTELPMC_DEVICE_EXTENSION DeviceExtension = (PINTELPMC_DEVICE_EXTENSION)Context;
    ULONG Processor = KeGetCurrentProcessorNumber();

    if (Processor < MAXIMUM_PROCESSORS && DeviceExtension->SavedCStateValid[Processor])
        __writemsr(INTELPMC_MSR_PKG_CST_CONFIG, DeviceExtension->SavedCState[Processor]);
#else
    UNREFERENCED_PARAMETER(Context);
#endif
    return 0;
}

static
VOID
IntelPmcPrepareSleep(
    _Inout_ PINTELPMC_DEVICE_EXTENSION DeviceExtension)
{
    KIRQL OldIrql;

    if (!DeviceExtension->Started || !DeviceExtension->RegisterBase || InterlockedCompareExchange(&DeviceExtension->SleepPrepared, 1, 0) != 0)
        return;
    RtlZeroMemory(DeviceExtension->SavedCStateValid, sizeof(DeviceExtension->SavedCStateValid));
    KeIpiGenericCall(IntelPmcDisableC1AutoDemote, (ULONG_PTR)DeviceExtension);
    KeAcquireSpinLock(&DeviceExtension->RegisterLock, &OldIrql);
    DeviceExtension->SavedLtrIgnore = IntelPmcReadUlong(DeviceExtension, INTELPMC_LTR_IGNORE_OFFSET);
    DeviceExtension->SleepResidency = IntelPmcReadUlong(DeviceExtension, INTELPMC_SLP_S0_OFFSET);
    IntelPmcWriteUlong(DeviceExtension, INTELPMC_LTR_IGNORE_OFFSET, DeviceExtension->SavedLtrIgnore | INTELPMC_LTR_IGNORE_GBE);
    KeReleaseSpinLock(&DeviceExtension->RegisterLock, OldIrql);
    DPRINT1("INTELPMC: prepared S0ix on %lu processors, SLP_S0=%lu\n", DeviceExtension->ActiveProcessors, DeviceExtension->SleepResidency);
}

static
VOID
IntelPmcRestoreSleep(
    _Inout_ PINTELPMC_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Residency;
    KIRQL OldIrql;

    if (InterlockedCompareExchange(&DeviceExtension->SleepPrepared, 0, 1) != 1)
        return;
    if (DeviceExtension->RegisterBase)
    {
        KeAcquireSpinLock(&DeviceExtension->RegisterLock, &OldIrql);
        IntelPmcWriteUlong(DeviceExtension, INTELPMC_LTR_IGNORE_OFFSET, DeviceExtension->SavedLtrIgnore);
        Residency = IntelPmcReadUlong(DeviceExtension, INTELPMC_SLP_S0_OFFSET);
        KeReleaseSpinLock(&DeviceExtension->RegisterLock, OldIrql);
    }
    else
    {
        Residency = DeviceExtension->SleepResidency;
    }
    KeIpiGenericCall(IntelPmcRestoreC1AutoDemote, (ULONG_PTR)DeviceExtension);
    DPRINT1("INTELPMC: resumed SMP policy, SLP_S0 delta=%lu ticks\n", Residency - DeviceExtension->SleepResidency);
}

static
NTSTATUS
IntelPmcQueryInformation(
    _In_ PINTELPMC_DEVICE_EXTENSION DeviceExtension,
    _Out_ PINTELPMC_INFORMATION Information)
{
    KIRQL OldIrql;
    ULONG Index;

    RtlZeroMemory(Information, sizeof(*Information));
    if (!DeviceExtension->Started || !DeviceExtension->RegisterBase)
        return STATUS_DEVICE_NOT_READY;
    Information->Version = INTELPMC_INTERFACE_VERSION;
    Information->ProcessorModel = DeviceExtension->ProcessorModel;
    Information->ActiveProcessors = DeviceExtension->ActiveProcessors;
    Information->PhysicalBase = DeviceExtension->PhysicalBase.QuadPart;
    Information->Flags = DeviceExtension->BaseFromLpit ? INTELPMC_FLAG_LPIT_BASE : INTELPMC_FLAG_DEFAULT_BASE;
    if (InterlockedCompareExchange(&DeviceExtension->SleepPrepared, 0, 0))
        Information->Flags |= INTELPMC_FLAG_SLEEP_PREPARED;
    KeAcquireSpinLock(&DeviceExtension->RegisterLock, &OldIrql);
    Information->SlpS0Residency = IntelPmcReadUlong(DeviceExtension, INTELPMC_SLP_S0_OFFSET);
    Information->PmConfiguration = IntelPmcReadUlong(DeviceExtension, INTELPMC_PM_CFG_OFFSET);
    Information->LtrIgnore = IntelPmcReadUlong(DeviceExtension, INTELPMC_LTR_IGNORE_OFFSET);
    Information->LpmEnable = IntelPmcReadUlong(DeviceExtension, INTELPMC_LPM_ENABLE_OFFSET);
    Information->LpmPriority = IntelPmcReadUlong(DeviceExtension, INTELPMC_LPM_PRIORITY_OFFSET);
    for (Index = 0; Index < INTELPMC_LPM_MODE_COUNT; Index++)
        Information->LpmResidency[Index] = IntelPmcReadUlong(DeviceExtension, INTELPMC_LPM_RESIDENCY_OFFSET + Index * sizeof(ULONG));
    for (Index = 0; Index < INTELPMC_LPM_MAP_COUNT; Index++)
    {
        Information->LpmStatus[Index] = IntelPmcReadUlong(DeviceExtension, INTELPMC_LPM_STATUS_OFFSET + Index * sizeof(ULONG));
        Information->LpmLiveStatus[Index] = IntelPmcReadUlong(DeviceExtension, INTELPMC_LPM_LIVE_STATUS_OFFSET + Index * sizeof(ULONG));
    }
    for (Index = 0; Index < INTELPMC_PPFEAR_COUNT; Index++)
        Information->PowerGatingStatus[Index] = READ_REGISTER_UCHAR(DeviceExtension->RegisterBase + INTELPMC_PPFEAR_OFFSET + Index);
    KeReleaseSpinLock(&DeviceExtension->RegisterLock, OldIrql);
    if (Information->PmConfiguration & INTELPMC_PM_READ_DISABLE)
        Information->Flags |= INTELPMC_FLAG_READ_DISABLED;
    return STATUS_SUCCESS;
}

static
NTSTATUS
IntelPmcStartHardware(
    _Inout_ PINTELPMC_DEVICE_EXTENSION DeviceExtension)
{
    NTSTATUS Status;

    Status = IntelPmcGetProcessorModel(&DeviceExtension->ProcessorModel);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = IntelPmcFindLpitBase(&DeviceExtension->PhysicalBase);
    DeviceExtension->BaseFromLpit = NT_SUCCESS(Status);
    if (!DeviceExtension->BaseFromLpit)
        DeviceExtension->PhysicalBase.QuadPart = INTELPMC_BASE_DEFAULT;
    if ((DeviceExtension->PhysicalBase.QuadPart & (PAGE_SIZE - 1)) != 0)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    DeviceExtension->RegisterBase = MmMapIoSpace(DeviceExtension->PhysicalBase, INTELPMC_LENGTH, MmNonCached);
    if (!DeviceExtension->RegisterBase)
        return STATUS_INSUFFICIENT_RESOURCES;
    DeviceExtension->ActiveProcessors = KeQueryActiveProcessorCount(NULL);
    DeviceExtension->Started = TRUE;
    Status = IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, TRUE);
    if (!NT_SUCCESS(Status))
    {
        DeviceExtension->Started = FALSE;
        MmUnmapIoSpace(DeviceExtension->RegisterBase, INTELPMC_LENGTH);
        DeviceExtension->RegisterBase = NULL;
        return Status;
    }
    DPRINT1("INTELPMC: P0 Alder Lake-N model=0x%lx base=%I64x source=%s processors=%lu SMP\n", DeviceExtension->ProcessorModel, DeviceExtension->PhysicalBase.QuadPart, DeviceExtension->BaseFromLpit ? "LPIT" : "default", DeviceExtension->ActiveProcessors);
    return STATUS_SUCCESS;
}

static
VOID
IntelPmcStopHardware(
    _Inout_ PINTELPMC_DEVICE_EXTENSION DeviceExtension)
{
    DeviceExtension->Started = FALSE;
    IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, FALSE);
    IntelPmcRestoreSleep(DeviceExtension);
    if (DeviceExtension->RegisterBase)
    {
        MmUnmapIoSpace(DeviceExtension->RegisterBase, INTELPMC_LENGTH);
        DeviceExtension->RegisterBase = NULL;
    }
}

static
NTSTATUS
NTAPI
IntelPmcCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PINTELPMC_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (NT_SUCCESS(Status))
        IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
IntelPmcDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PINTELPMC_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;
    ULONG_PTR Information = 0;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        goto Complete;
    if (Stack->Parameters.DeviceIoControl.IoControlCode != IOCTL_INTELPMC_QUERY_INFORMATION)
        Status = STATUS_INVALID_DEVICE_REQUEST;
    else if (Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(INTELPMC_INFORMATION))
        Status = STATUS_BUFFER_TOO_SMALL;
    else
    {
        Status = IntelPmcQueryInformation(DeviceExtension, (PINTELPMC_INFORMATION)Irp->AssociatedIrp.SystemBuffer);
        if (NT_SUCCESS(Status))
            Information = sizeof(INTELPMC_INFORMATION);
    }
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);

Complete:
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
IntelPmcPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PINTELPMC_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IntelPmcForwardSynchronously(DeviceExtension, Irp);
            if (NT_SUCCESS(Status))
                Status = IntelPmcStartHardware(DeviceExtension);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
            IntelPmcStopHardware(DeviceExtension);
            break;

        case IRP_MN_SURPRISE_REMOVAL:
            DeviceExtension->Started = FALSE;
            IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, FALSE);
            IntelPmcRestoreSleep(DeviceExtension);
            break;

        case IRP_MN_REMOVE_DEVICE:
            Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
            if (!NT_SUCCESS(Status))
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }
            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            IntelPmcStopHardware(DeviceExtension);
            Status = IntelPmcForwardSynchronously(DeviceExtension, Irp);
            IoDetachDevice(DeviceExtension->LowerDevice);
            RtlFreeUnicodeString(&DeviceExtension->InterfaceName);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            IoDeleteDevice(DeviceObject);
            return Status;

        default:
            break;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
IntelPmcPowerCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PINTELPMC_POWER_CONTEXT PowerContext = Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (PowerContext->Action == INTELPMC_POWER_RESUME || (PowerContext->Action == INTELPMC_POWER_ROLLBACK_ON_FAILURE && !NT_SUCCESS(Irp->IoStatus.Status)))
        IntelPmcRestoreSleep(PowerContext->DeviceExtension);
    IoReleaseRemoveLock(&PowerContext->DeviceExtension->RemoveLock, Irp);
    ExFreePoolWithTag(PowerContext, INTELPMC_TAG);
    if (Irp->PendingReturned)
        IoMarkIrpPending(Irp);
    return STATUS_CONTINUE_COMPLETION;
}

static
NTSTATUS
NTAPI
IntelPmcPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PINTELPMC_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PINTELPMC_POWER_CONTEXT PowerContext;
    ULONG Action = INTELPMC_POWER_NONE;
    NTSTATUS Status;

    if (Stack->MinorFunction == IRP_MN_SET_POWER && Stack->Parameters.Power.Type == SystemPowerState)
    {
        if (Stack->Parameters.Power.State.SystemState == PowerSystemWorking)
            Action = INTELPMC_POWER_RESUME;
        else
            Action = INTELPMC_POWER_ROLLBACK_ON_FAILURE;
    }
    PowerContext = ExAllocatePoolWithTag(NonPagedPool, sizeof(*PowerContext), INTELPMC_TAG);
    if (!PowerContext)
    {
        if (Action == INTELPMC_POWER_RESUME)
            IntelPmcRestoreSleep(DeviceExtension);
        PoStartNextPowerIrp(Irp);
        IoSkipCurrentIrpStackLocation(Irp);
        return PoCallDriver(DeviceExtension->LowerDevice, Irp);
    }
    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(PowerContext, INTELPMC_TAG);
        PoStartNextPowerIrp(Irp);
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    if (Action == INTELPMC_POWER_ROLLBACK_ON_FAILURE)
        IntelPmcPrepareSleep(DeviceExtension);
    PowerContext->DeviceExtension = DeviceExtension;
    PowerContext->Action = Action;
    PoStartNextPowerIrp(Irp);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, IntelPmcPowerCompletion, PowerContext, TRUE, TRUE, TRUE);
    return PoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
IntelPmcUnsupported(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_INVALID_DEVICE_REQUEST;
}

static
NTSTATUS
NTAPI
IntelPmcAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PINTELPMC_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(*DeviceExtension), NULL, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;
    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    DeviceExtension->PhysicalDevice = PhysicalDeviceObject;
    IoInitializeRemoveLock(&DeviceExtension->RemoveLock, INTELPMC_TAG, 0, 0);
    KeInitializeSpinLock(&DeviceExtension->RegisterLock);
    Status = IoRegisterDeviceInterface(PhysicalDeviceObject, &GUID_DEVINTERFACE_REACTOS_INTEL_PMC, NULL, &DeviceExtension->InterfaceName);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(DeviceObject);
        return Status;
    }
    Status = IoAttachDeviceToDeviceStackSafe(DeviceObject, PhysicalDeviceObject, &DeviceExtension->LowerDevice);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeUnicodeString(&DeviceExtension->InterfaceName);
        IoDeleteDevice(DeviceObject);
        return Status;
    }
    DeviceObject->Flags |= DO_POWER_PAGABLE | DO_BUFFERED_IO;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
IntelPmcUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);
    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++)
        DriverObject->MajorFunction[Index] = IntelPmcUnsupported;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = IntelPmcCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = IntelPmcCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = IntelPmcDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = IntelPmcPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = IntelPmcPower;
    DriverObject->DriverExtension->AddDevice = IntelPmcAddDevice;
    DriverObject->DriverUnload = IntelPmcUnload;
    return STATUS_SUCCESS;
}
