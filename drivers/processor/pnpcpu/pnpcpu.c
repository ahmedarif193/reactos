/*
 * PROJECT:     ReactOS ACPI Processor Module Driver
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ACPI0007 topology and processor power capability discovery
 */

#include "pnpcpu.h"

#define NDEBUG
#include <debug.h>

typedef struct _PNPCPU_WORK_CONTEXT
{
    WORK_QUEUE_ITEM WorkItem;
    PPNPCPU_DEVICE_EXTENSION DeviceExtension;
} PNPCPU_WORK_CONTEXT, *PPNPCPU_WORK_CONTEXT;

static PPNPCPU_DEVICE_EXTENSION PnpcpuProcessors[MAXIMUM_PROCESSORS];

#if defined(_M_IX86) || defined(_M_AMD64)
#define PNPCPU_INTEL_PERF_CTL_MSR 0x199

#if defined(__clang__) || defined(__GNUC__)
__attribute__((target("sse3")))
#endif
static
VOID
PnpcpuMonitorMwait(
    _In_ volatile LONG *Address,
    _In_ ULONG Hint)
{
    _mm_monitor((PVOID)Address, 0, 0);
    _mm_mwait(Hint, 1);
}
#endif

static
NTSTATUS
NTAPI
PnpcpuCompletion(
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
PnpcpuForwardSynchronously(
    _In_ PPNPCPU_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, PnpcpuCompletion, &Event, TRUE, TRUE, TRUE);
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
PnpcpuSendAcpiRequest(
    _In_ PPNPCPU_DEVICE_EXTENSION DeviceExtension,
    _In_ PACPI_EVAL_INPUT_BUFFER InputBuffer,
    _Out_writes_bytes_(OutputLength) PACPI_EVAL_OUTPUT_BUFFER OutputBuffer,
    _In_ ULONG OutputLength,
    _Out_ PULONG_PTR Information)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    *Information = 0;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_ACPI_EVAL_METHOD, DeviceExtension->LowerDevice, InputBuffer, sizeof(*InputBuffer), OutputBuffer, OutputLength, FALSE, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
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
PnpcpuEvaluateMethod(
    _In_ PPNPCPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG MethodName,
    _Outptr_result_bytebuffer_(*OutputLength) PACPI_EVAL_OUTPUT_BUFFER *OutputBuffer,
    _Out_ PULONG OutputLength)
{
    ACPI_EVAL_INPUT_BUFFER InputBuffer;
    PACPI_EVAL_OUTPUT_BUFFER Buffer;
    ULONG BufferLength = PAGE_SIZE;
    ULONG_PTR Information;
    NTSTATUS Status;

    *OutputBuffer = NULL;
    *OutputLength = 0;
    RtlZeroMemory(&InputBuffer, sizeof(InputBuffer));
    InputBuffer.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    InputBuffer.MethodNameAsUlong = MethodName;

    for (;;)
    {
        Buffer = ExAllocatePoolWithTag(PagedPool, BufferLength, PNPCPU_TAG);
        if (!Buffer)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(Buffer, BufferLength);

        Status = PnpcpuSendAcpiRequest(DeviceExtension, &InputBuffer, Buffer, BufferLength, &Information);
        if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
            break;

        ExFreePoolWithTag(Buffer, PNPCPU_TAG);
        if (Information <= BufferLength || Information > PNPCPU_MAX_ACPI_OUTPUT)
            return STATUS_ACPI_INVALID_DATA;
        BufferLength = (ULONG)Information;
    }

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, PNPCPU_TAG);
        return Status;
    }
    if (Buffer->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE || Buffer->Length < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) || Buffer->Length > BufferLength)
    {
        ExFreePoolWithTag(Buffer, PNPCPU_TAG);
        return STATUS_ACPI_INVALID_DATA;
    }

    *OutputBuffer = Buffer;
    *OutputLength = BufferLength;
    return STATUS_SUCCESS;
}

static
BOOLEAN
PnpcpuFirstArgumentValid(
    _In_ PACPI_EVAL_OUTPUT_BUFFER OutputBuffer,
    _In_ ULONG OutputLength)
{
    ULONG HeaderLength = FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument);
    ULONG ArgumentLength;

    if (OutputBuffer->Count == 0 || OutputBuffer->Length < HeaderLength + ACPI_METHOD_ARGUMENT_LENGTH(0) || OutputBuffer->Length > OutputLength)
        return FALSE;
    ArgumentLength = ACPI_METHOD_ARGUMENT_LENGTH_FROM_ARGUMENT(&OutputBuffer->Argument[0]);
    return ArgumentLength <= OutputBuffer->Length - HeaderLength;
}

static
BOOLEAN
PnpcpuParseUnsignedAscii(
    _In_reads_bytes_(Length) const UCHAR *String,
    _In_ ULONG Length,
    _Out_ PULONG Value)
{
    ULONG Result = 0;
    ULONG Index = 0;
    ULONG Base = 10;
    BOOLEAN FoundDigit = FALSE;

    if (Length >= 2 && String[0] == '0' && (String[1] == 'x' || String[1] == 'X'))
    {
        Base = 16;
        Index = 2;
    }
    for (; Index < Length && String[Index] != ANSI_NULL; Index++)
    {
        ULONG Digit;

        if (String[Index] >= '0' && String[Index] <= '9')
            Digit = String[Index] - '0';
        else if (Base == 16 && String[Index] >= 'a' && String[Index] <= 'f')
            Digit = String[Index] - 'a' + 10;
        else if (Base == 16 && String[Index] >= 'A' && String[Index] <= 'F')
            Digit = String[Index] - 'A' + 10;
        else
            return FALSE;
        if (Result > (MAXULONG - Digit) / Base)
            return FALSE;
        Result = Result * Base + Digit;
        FoundDigit = TRUE;
    }
    if (!FoundDigit)
        return FALSE;
    *Value = Result;
    return TRUE;
}

static
PACPI_METHOD_ARGUMENT
PnpcpuGetArgument(
    _In_ PACPI_EVAL_OUTPUT_BUFFER OutputBuffer,
    _In_ ULONG OutputLength,
    _In_ ULONG Index)
{
    PACPI_METHOD_ARGUMENT Argument = &OutputBuffer->Argument[0];
    PUCHAR End;
    ULONG ArgumentLength;
    ULONG Current;

    if (OutputBuffer->Length > OutputLength || OutputBuffer->Length < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) || Index >= OutputBuffer->Count)
        return NULL;
    End = (PUCHAR)OutputBuffer + OutputBuffer->Length;
    for (Current = 0; Current <= Index; Current++)
    {
        if ((PUCHAR)Argument > End || (ULONG)(End - (PUCHAR)Argument) < ACPI_METHOD_ARGUMENT_LENGTH(0))
            return NULL;
        ArgumentLength = ACPI_METHOD_ARGUMENT_LENGTH_FROM_ARGUMENT(Argument);
        if (ArgumentLength > (ULONG)(End - (PUCHAR)Argument))
            return NULL;
        if (Current == Index)
            return Argument;
        Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    }
    return NULL;
}

static
PACPI_METHOD_ARGUMENT
PnpcpuGetPackageArgument(
    _In_ PACPI_METHOD_ARGUMENT Package,
    _In_ ULONG Index)
{
    PACPI_METHOD_ARGUMENT Argument;
    PUCHAR End;
    ULONG ArgumentLength;
    ULONG Current = 0;

    if (!Package || Package->Type != ACPI_METHOD_ARGUMENT_PACKAGE)
        return NULL;
    if (Package->DataLength < ACPI_METHOD_ARGUMENT_LENGTH(0))
        return NULL;
    Argument = (PACPI_METHOD_ARGUMENT)Package->Data;
    End = Package->Data + Package->DataLength;
    while ((PUCHAR)Argument <= End && (ULONG)(End - (PUCHAR)Argument) >= ACPI_METHOD_ARGUMENT_LENGTH(0))
    {
        ArgumentLength = ACPI_METHOD_ARGUMENT_LENGTH_FROM_ARGUMENT(Argument);
        if (ArgumentLength > (ULONG)(End - (PUCHAR)Argument))
            return NULL;
        if (Current++ == Index)
            return Argument;
        Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    }
    return NULL;
}

static
BOOLEAN
PnpcpuArgumentInteger(
    _In_opt_ PACPI_METHOD_ARGUMENT Argument,
    _Out_ PULONG Value)
{
    if (!Argument || Argument->Type != ACPI_METHOD_ARGUMENT_INTEGER)
        return FALSE;
    *Value = Argument->Argument;
    return TRUE;
}

static
BOOLEAN
PnpcpuParseRegister(
    _In_opt_ PACPI_METHOD_ARGUMENT Argument,
    _Out_ PPNPCPU_REGISTER Register)
{
    USHORT DescriptorLength;

    RtlZeroMemory(Register, sizeof(*Register));
    if (!Argument || Argument->Type != ACPI_METHOD_ARGUMENT_BUFFER || Argument->DataLength < 15 || Argument->Data[0] != 0x82)
        return FALSE;
    RtlCopyMemory(&DescriptorLength, Argument->Data + 1, sizeof(DescriptorLength));
    if (DescriptorLength < 12 || (ULONG)DescriptorLength + 3 > Argument->DataLength)
        return FALSE;
    Register->SpaceId = Argument->Data[3];
    Register->BitWidth = Argument->Data[4];
    Register->BitOffset = Argument->Data[5];
    Register->AccessSize = Argument->Data[6];
    RtlCopyMemory(&Register->Address, Argument->Data + 7, sizeof(Register->Address));
    if (Register->BitWidth == 0)
    {
        if (Register->SpaceId != PNPCPU_SPACE_FIXED_HARDWARE || Register->Address != 0)
            return FALSE;
    }
    else if (Register->BitWidth > 64 || Register->BitOffset >= 64 || Register->BitWidth + Register->BitOffset > 64)
        return FALSE;
    Register->Valid = TRUE;
    return TRUE;
}

static
ULONG
PnpcpuRegisterAccessBytes(
    _In_ PPNPCPU_REGISTER Register)
{
    ULONG Bytes;

    if (Register->AccessSize >= 1 && Register->AccessSize <= 4)
        Bytes = 1u << (Register->AccessSize - 1);
    else
        Bytes = (Register->BitWidth + Register->BitOffset + 7) / 8;
    if (Bytes <= 1)
        return 1;
    if (Bytes <= 2)
        return 2;
    if (Bytes <= 4)
        return 4;
    return 8;
}

static
BOOLEAN
PnpcpuPrepareRegister(
    _Inout_ PPNPCPU_REGISTER Register)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONGLONG PageOffset;
    ULONG Bytes;

    if (!Register->Valid)
        return FALSE;
    Bytes = PnpcpuRegisterAccessBytes(Register);
    if (Register->SpaceId == PNPCPU_SPACE_SYSTEM_MEMORY)
    {
        PageOffset = Register->Address & (PAGE_SIZE - 1);
        Register->MappingLength = (SIZE_T)((PageOffset + Bytes + PAGE_SIZE - 1) & ~(ULONGLONG)(PAGE_SIZE - 1));
        PhysicalAddress.QuadPart = Register->Address - PageOffset;
        Register->MappingBase = MmMapIoSpace(PhysicalAddress, Register->MappingLength, MmNonCached);
        if (!Register->MappingBase)
        {
            Register->Valid = FALSE;
            return FALSE;
        }
        Register->MappedAddress = (PUCHAR)Register->MappingBase + (SIZE_T)PageOffset;
    }
    else if (Register->SpaceId == PNPCPU_SPACE_SYSTEM_IO)
    {
        if (Register->Address > MAXULONG_PTR || Bytes > sizeof(ULONG))
        {
            Register->Valid = FALSE;
            return FALSE;
        }
    }
    else if (Register->SpaceId != PNPCPU_SPACE_FIXED_HARDWARE)
    {
        Register->Valid = FALSE;
        return FALSE;
    }
    return TRUE;
}

static
BOOLEAN
PnpcpuPrepareCppcRegister(
    _Inout_ PPNPCPU_REGISTER Register)
{
    if (!Register->Valid || Register->BitWidth == 0 || (Register->SpaceId == PNPCPU_SPACE_FIXED_HARDWARE && Register->Address == 0))
    {
        RtlZeroMemory(Register, sizeof(*Register));
        return FALSE;
    }
    return PnpcpuPrepareRegister(Register);
}

static
VOID
PnpcpuReleaseRegister(
    _Inout_ PPNPCPU_REGISTER Register)
{
    if (Register->MappingBase)
        MmUnmapIoSpace(Register->MappingBase, Register->MappingLength);
    RtlZeroMemory(Register, sizeof(*Register));
}

static
ULONGLONG
PnpcpuRegisterMask(
    _In_ PPNPCPU_REGISTER Register)
{
    if (Register->BitWidth == 64)
        return MAXULONGLONG;
    return (1ULL << Register->BitWidth) - 1;
}

static
NTSTATUS
PnpcpuReadRegister(
    _In_ PPNPCPU_REGISTER Register,
    _Out_ PULONGLONG Value)
{
    ULONGLONG RawValue;
    ULONG Bytes;

    if (!Register->Valid)
        return STATUS_NOT_SUPPORTED;
    Bytes = PnpcpuRegisterAccessBytes(Register);
    if (Register->SpaceId == PNPCPU_SPACE_SYSTEM_MEMORY && Register->MappedAddress)
    {
        KeMemoryBarrier();
        if (Bytes == 1)
            RawValue = *(volatile UCHAR *)Register->MappedAddress;
        else if (Bytes == 2)
            RawValue = *(volatile USHORT *)Register->MappedAddress;
        else if (Bytes == 4)
            RawValue = *(volatile ULONG *)Register->MappedAddress;
        else
            RawValue = *(volatile ULONGLONG *)Register->MappedAddress;
        KeMemoryBarrier();
    }
    else if (Register->SpaceId == PNPCPU_SPACE_SYSTEM_IO)
    {
        if (Bytes == 1)
            RawValue = READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)Register->Address);
        else if (Bytes == 2)
            RawValue = READ_PORT_USHORT((PUSHORT)(ULONG_PTR)Register->Address);
        else
            RawValue = READ_PORT_ULONG((PULONG)(ULONG_PTR)Register->Address);
    }
#if defined(_M_IX86) || defined(_M_AMD64)
    else if (Register->SpaceId == PNPCPU_SPACE_FIXED_HARDWARE && Register->Address <= MAXULONG)
    {
        RawValue = __readmsr((ULONG)Register->Address);
    }
#endif
    else
    {
        return STATUS_NOT_SUPPORTED;
    }
    *Value = (RawValue >> Register->BitOffset) & PnpcpuRegisterMask(Register);
    return STATUS_SUCCESS;
}

static
NTSTATUS
PnpcpuWriteRegister(
    _In_ PPNPCPU_REGISTER Register,
    _In_ ULONGLONG Value)
{
    ULONGLONG Mask;
    ULONGLONG RawValue = 0;
    ULONG Bytes;
    NTSTATUS Status;

    if (!Register->Valid)
        return STATUS_NOT_SUPPORTED;
    Mask = PnpcpuRegisterMask(Register);
    if (Register->BitWidth != 64 || Register->BitOffset != 0)
    {
        Status = PnpcpuReadRegister(Register, &RawValue);
        if (!NT_SUCCESS(Status))
            return Status;
        RawValue &= ~(Mask << Register->BitOffset);
        RawValue |= (Value & Mask) << Register->BitOffset;
    }
    else
    {
        RawValue = Value;
    }
    Bytes = PnpcpuRegisterAccessBytes(Register);
    if (Register->SpaceId == PNPCPU_SPACE_SYSTEM_MEMORY && Register->MappedAddress)
    {
        KeMemoryBarrier();
        if (Bytes == 1)
            *(volatile UCHAR *)Register->MappedAddress = (UCHAR)RawValue;
        else if (Bytes == 2)
            *(volatile USHORT *)Register->MappedAddress = (USHORT)RawValue;
        else if (Bytes == 4)
            *(volatile ULONG *)Register->MappedAddress = (ULONG)RawValue;
        else
            *(volatile ULONGLONG *)Register->MappedAddress = RawValue;
        KeMemoryBarrier();
        return STATUS_SUCCESS;
    }
    if (Register->SpaceId == PNPCPU_SPACE_SYSTEM_IO)
    {
        if (Bytes == 1)
            WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)Register->Address, (UCHAR)RawValue);
        else if (Bytes == 2)
            WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)Register->Address, (USHORT)RawValue);
        else
            WRITE_PORT_ULONG((PULONG)(ULONG_PTR)Register->Address, (ULONG)RawValue);
        return STATUS_SUCCESS;
    }
#if defined(_M_IX86) || defined(_M_AMD64)
    if (Register->SpaceId == PNPCPU_SPACE_FIXED_HARDWARE && Register->Address <= MAXULONG)
    {
        __writemsr((ULONG)Register->Address, RawValue);
        return STATUS_SUCCESS;
    }
#endif
    return STATUS_NOT_SUPPORTED;
}

static
BOOLEAN
PnpcpuQueryInteger(
    _In_ PPNPCPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG MethodName,
    _Out_ PULONG Value)
{
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer;
    ULONG OutputLength;
    NTSTATUS Status;
    BOOLEAN Valid = FALSE;

    Status = PnpcpuEvaluateMethod(DeviceExtension, MethodName, &OutputBuffer, &OutputLength);
    if (!NT_SUCCESS(Status))
        return FALSE;
    if (PnpcpuFirstArgumentValid(OutputBuffer, OutputLength) && OutputBuffer->Argument[0].Type == ACPI_METHOD_ARGUMENT_INTEGER)
    {
        *Value = OutputBuffer->Argument[0].Argument;
        Valid = TRUE;
    }
    else if (PnpcpuFirstArgumentValid(OutputBuffer, OutputLength) && OutputBuffer->Argument[0].Type == ACPI_METHOD_ARGUMENT_STRING)
    {
        Valid = PnpcpuParseUnsignedAscii(OutputBuffer->Argument[0].Data, OutputBuffer->Argument[0].DataLength, Value);
    }
    ExFreePoolWithTag(OutputBuffer, PNPCPU_TAG);
    return Valid;
}

static
VOID
PnpcpuQueryMat(
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer;
    PACPI_METHOD_ARGUMENT Argument;
    ULONG OutputLength;
    ULONG Flags;
    ULONG MatUid;
    PUCHAR Data;
    NTSTATUS Status;

    DeviceExtension->ApicIdValid = FALSE;
    Status = PnpcpuEvaluateMethod(DeviceExtension, PNPCPU_METHOD('_', 'M', 'A', 'T'), &OutputBuffer, &OutputLength);
    if (!NT_SUCCESS(Status))
        return;
    if (!PnpcpuFirstArgumentValid(OutputBuffer, OutputLength))
        goto Exit;

    Argument = &OutputBuffer->Argument[0];
    if (Argument->Type != ACPI_METHOD_ARGUMENT_BUFFER || Argument->DataLength < 2)
        goto Exit;
    Data = Argument->Data;
    if (Data[0] == 0 && Data[1] >= 8 && Argument->DataLength >= 8)
    {
        RtlCopyMemory(&Flags, Data + 4, sizeof(Flags));
        if (Flags & 3)
        {
            DeviceExtension->ApicId = Data[3];
            DeviceExtension->ApicIdValid = TRUE;
            if (!DeviceExtension->UidValid)
            {
                DeviceExtension->Uid = Data[2];
                DeviceExtension->UidValid = TRUE;
            }
        }
    }
    else if (Data[0] == 9 && Data[1] >= 16 && Argument->DataLength >= 16)
    {
        RtlCopyMemory(&Flags, Data + 8, sizeof(Flags));
        if (Flags & 3)
        {
            RtlCopyMemory(&DeviceExtension->ApicId, Data + 4, sizeof(DeviceExtension->ApicId));
            RtlCopyMemory(&MatUid, Data + 12, sizeof(MatUid));
            DeviceExtension->ApicIdValid = TRUE;
            if (!DeviceExtension->UidValid)
            {
                DeviceExtension->Uid = MatUid;
                DeviceExtension->UidValid = TRUE;
            }
        }
    }

Exit:
    ExFreePoolWithTag(OutputBuffer, PNPCPU_TAG);
}

static
ULONG
PnpcpuQueryCurrentApicId(VOID)
{
#if defined(_M_IX86) || defined(_M_AMD64)
    int Registers[4];
    ULONG MaximumLeaf;

    __cpuid(Registers, 0);
    MaximumLeaf = (ULONG)Registers[0];
    if (MaximumLeaf >= 0xB)
    {
        __cpuidex(Registers, 0xB, 0);
        if (Registers[1] != 0)
            return (ULONG)Registers[3];
    }
    __cpuid(Registers, 1);
    return (ULONG)Registers[1] >> 24;
#else
    return MAXULONG;
#endif
}

#if defined(_M_IX86) || defined(_M_AMD64)
static
VOID
PnpcpuQueryProcessorFeatures(
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    int Registers[4];
    ULONG MaximumLeaf;
    KAFFINITY PreviousAffinity = 0;

    DeviceExtension->MonitorMwaitSupported = FALSE;
    DeviceExtension->IntelEstSupported = FALSE;
    DeviceExtension->MwaitSubstates = 0;
    if (DeviceExtension->ProcessorNumberValid)
        PreviousAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1 << DeviceExtension->ProcessorNumber);
    __cpuid(Registers, 0);
    MaximumLeaf = (ULONG)Registers[0];
    if (MaximumLeaf >= 1)
    {
        BOOLEAN Intel;

        Intel = (ULONG)Registers[1] == 0x756E6547 && (ULONG)Registers[3] == 0x49656E69 && (ULONG)Registers[2] == 0x6C65746E;
        __cpuid(Registers, 1);
        DeviceExtension->IntelEstSupported = Intel && (Registers[2] & (1 << 7)) != 0;
        if ((Registers[2] & (1 << 3)) != 0 && MaximumLeaf >= 5)
        {
            __cpuid(Registers, 5);
            DeviceExtension->MonitorMwaitSupported = (Registers[2] & 3) == 3;
            DeviceExtension->MwaitSubstates = (ULONG)Registers[3];
        }
    }
    if (DeviceExtension->ProcessorNumberValid)
        KeRevertToUserAffinityThreadEx(PreviousAffinity);
}

static
BOOLEAN
PnpcpuMwaitHintSupported(
    _In_ PPNPCPU_DEVICE_EXTENSION DeviceExtension,
    _In_ PPNPCPU_REGISTER Register)
{
    ULONG CState;

    if (!DeviceExtension->MonitorMwaitSupported || Register->BitOffset != 2 || Register->Address > MAXULONG)
        return FALSE;
    CState = ((((ULONG)Register->Address >> 4) & 0xF) + 1) & 0xF;
    if (CState >= 8)
        return FALSE;
    return ((DeviceExtension->MwaitSubstates >> (CState * 4)) & 0xF) != 0;
}
#endif

static
VOID
PnpcpuFindProcessorNumber(
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    ULONG ActiveProcessors = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    ULONG Index;

    DeviceExtension->ProcessorNumberValid = FALSE;
    if (ActiveProcessors > MAXIMUM_PROCESSORS)
        ActiveProcessors = MAXIMUM_PROCESSORS;
#if defined(_M_IX86) || defined(_M_AMD64)
    if (DeviceExtension->ApicIdValid)
    {
        for (Index = 0; Index < ActiveProcessors; Index++)
        {
            KAFFINITY PreviousAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1 << Index);
            ULONG ApicId = PnpcpuQueryCurrentApicId();

            KeRevertToUserAffinityThreadEx(PreviousAffinity);
            if (ApicId == DeviceExtension->ApicId)
            {
                DeviceExtension->ProcessorNumber = Index;
                DeviceExtension->ProcessorNumberValid = TRUE;
                return;
            }
        }
    }
#endif
    if (DeviceExtension->UidValid && DeviceExtension->Uid < ActiveProcessors)
    {
        DeviceExtension->ProcessorNumber = DeviceExtension->Uid;
        DeviceExtension->ProcessorNumberValid = TRUE;
    }
}

static
VOID
PnpcpuReleasePowerConfiguration(
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(DeviceExtension->IdleStates); Index++)
        PnpcpuReleaseRegister(&DeviceExtension->IdleStates[Index].Register);
    PnpcpuReleaseRegister(&DeviceExtension->PerfControl);
    PnpcpuReleaseRegister(&DeviceExtension->PerfStatus);
    PnpcpuReleaseRegister(&DeviceExtension->CppcDesired);
    PnpcpuReleaseRegister(&DeviceExtension->CppcMinimum);
    PnpcpuReleaseRegister(&DeviceExtension->CppcMaximum);
    PnpcpuReleaseRegister(&DeviceExtension->CppcEnable);
    PnpcpuReleaseRegister(&DeviceExtension->CppcAutonomous);
    RtlZeroMemory(DeviceExtension->IdleStates, sizeof(DeviceExtension->IdleStates));
    RtlZeroMemory(DeviceExtension->PerfStates, sizeof(DeviceExtension->PerfStates));
    DeviceExtension->IdleStateCount = 0;
    DeviceExtension->PerfStateCount = 0;
    DeviceExtension->PerfMode = PNPCPU_PERF_NONE;
    DeviceExtension->CppcHighest = 0;
    DeviceExtension->CppcNominal = 0;
    DeviceExtension->CppcLowestNonlinear = 0;
    DeviceExtension->CppcLowest = 0;
    DeviceExtension->CppcRestoreMask = 0;
}

static
BOOLEAN
PnpcpuParseCst(
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer;
    PACPI_METHOD_ARGUMENT Package;
    PACPI_METHOD_ARGUMENT Argument;
    ULONG OutputLength;
    ULONG DeclaredCount;
    ULONG Type;
    ULONG Latency;
    ULONG Power;
    ULONG Index;
    NTSTATUS Status;

    Status = PnpcpuEvaluateMethod(DeviceExtension, PNPCPU_METHOD('_', 'C', 'S', 'T'), &OutputBuffer, &OutputLength);
    if (!NT_SUCCESS(Status))
        goto Fallback;
    if (!PnpcpuArgumentInteger(PnpcpuGetArgument(OutputBuffer, OutputLength, 0), &DeclaredCount))
        goto Exit;
    DeclaredCount = min(DeclaredCount, OutputBuffer->Count > 0 ? OutputBuffer->Count - 1 : 0);
    for (Index = 0; Index < DeclaredCount && DeviceExtension->IdleStateCount < PNPCPU_MAX_IDLE_STATES; Index++)
    {
        PPNPCPU_IDLE_STATE IdleState = &DeviceExtension->IdleStates[DeviceExtension->IdleStateCount];

        Package = PnpcpuGetArgument(OutputBuffer, OutputLength, Index + 1);
        Argument = PnpcpuGetPackageArgument(Package, 0);
        if (!PnpcpuArgumentInteger(PnpcpuGetPackageArgument(Package, 1), &Type) || !PnpcpuArgumentInteger(PnpcpuGetPackageArgument(Package, 2), &Latency) || !PnpcpuArgumentInteger(PnpcpuGetPackageArgument(Package, 3), &Power))
            continue;
        if (Type == 0)
            continue;
        RtlZeroMemory(IdleState, sizeof(*IdleState));
        IdleState->Type = Type;
        IdleState->Latency = Latency;
        IdleState->Power = Power;
        if (Type > 1)
        {
            if (!PnpcpuParseRegister(Argument, &IdleState->Register))
                continue;
            if (IdleState->Register.SpaceId == PNPCPU_SPACE_FIXED_HARDWARE)
            {
#if defined(_M_IX86) || defined(_M_AMD64)
                if (!PnpcpuMwaitHintSupported(DeviceExtension, &IdleState->Register))
                    continue;
#else
                continue;
#endif
            }
            else if (Type >= 3)
            {
                continue;
            }
            else if (!PnpcpuPrepareRegister(&IdleState->Register))
            {
                continue;
            }
        }
        DeviceExtension->IdleStateCount++;
    }
Exit:
    ExFreePoolWithTag(OutputBuffer, PNPCPU_TAG);
Fallback:
    if (DeviceExtension->IdleStateCount == 0)
    {
        DeviceExtension->IdleStates[0].Type = 1;
        DeviceExtension->IdleStates[0].Latency = 1;
        DeviceExtension->IdleStateCount = 1;
    }
    return DeviceExtension->IdleStateCount != 0;
}

static
BOOLEAN
PnpcpuParseCppc(
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer;
    ULONG OutputLength;
    ULONG NumEntries;
    NTSTATUS Status;
    BOOLEAN Valid = FALSE;

    Status = PnpcpuEvaluateMethod(DeviceExtension, PNPCPU_METHOD('_', 'C', 'P', 'C'), &OutputBuffer, &OutputLength);
    if (!NT_SUCCESS(Status))
        return FALSE;
    if (!PnpcpuArgumentInteger(PnpcpuGetArgument(OutputBuffer, OutputLength, 0), &NumEntries) || NumEntries > OutputBuffer->Count || NumEntries < 17)
        goto Exit;
    if (!PnpcpuArgumentInteger(PnpcpuGetArgument(OutputBuffer, OutputLength, 2), &DeviceExtension->CppcHighest) || !PnpcpuArgumentInteger(PnpcpuGetArgument(OutputBuffer, OutputLength, 3), &DeviceExtension->CppcNominal) || !PnpcpuArgumentInteger(PnpcpuGetArgument(OutputBuffer, OutputLength, 4), &DeviceExtension->CppcLowestNonlinear) || !PnpcpuArgumentInteger(PnpcpuGetArgument(OutputBuffer, OutputLength, 5), &DeviceExtension->CppcLowest))
        goto Exit;
    if (DeviceExtension->CppcHighest == 0 || DeviceExtension->CppcLowest > DeviceExtension->CppcHighest)
        goto Exit;
    if (!PnpcpuParseRegister(PnpcpuGetArgument(OutputBuffer, OutputLength, 7), &DeviceExtension->CppcDesired) || !PnpcpuPrepareCppcRegister(&DeviceExtension->CppcDesired))
        goto Exit;
    if (PnpcpuParseRegister(PnpcpuGetArgument(OutputBuffer, OutputLength, 8), &DeviceExtension->CppcMinimum))
        PnpcpuPrepareCppcRegister(&DeviceExtension->CppcMinimum);
    if (PnpcpuParseRegister(PnpcpuGetArgument(OutputBuffer, OutputLength, 9), &DeviceExtension->CppcMaximum))
        PnpcpuPrepareCppcRegister(&DeviceExtension->CppcMaximum);
    if (NumEntries > 16 && PnpcpuParseRegister(PnpcpuGetArgument(OutputBuffer, OutputLength, 16), &DeviceExtension->CppcEnable))
        PnpcpuPrepareCppcRegister(&DeviceExtension->CppcEnable);
    if (NumEntries > 17 && PnpcpuParseRegister(PnpcpuGetArgument(OutputBuffer, OutputLength, 17), &DeviceExtension->CppcAutonomous))
        PnpcpuPrepareCppcRegister(&DeviceExtension->CppcAutonomous);
    DeviceExtension->PerfMode = PNPCPU_PERF_CPPC;
    DeviceExtension->PerfStateCount = 2;
    DeviceExtension->PerfStates[0].Percentage = 100;
    DeviceExtension->PerfStates[0].Frequency = DeviceExtension->CppcHighest;
    DeviceExtension->PerfStates[1].Percentage = (UCHAR)max(1, (DeviceExtension->CppcLowest * 100) / DeviceExtension->CppcHighest);
    DeviceExtension->PerfStates[1].Frequency = DeviceExtension->CppcLowest;
    Valid = TRUE;
Exit:
    ExFreePoolWithTag(OutputBuffer, PNPCPU_TAG);
    if (!Valid)
    {
        PnpcpuReleaseRegister(&DeviceExtension->CppcDesired);
        PnpcpuReleaseRegister(&DeviceExtension->CppcMinimum);
        PnpcpuReleaseRegister(&DeviceExtension->CppcMaximum);
        PnpcpuReleaseRegister(&DeviceExtension->CppcEnable);
        PnpcpuReleaseRegister(&DeviceExtension->CppcAutonomous);
    }
    return Valid;
}

static
BOOLEAN
PnpcpuParsePss(
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    PACPI_EVAL_OUTPUT_BUFFER PctOutput;
    PACPI_EVAL_OUTPUT_BUFFER PssOutput;
    PACPI_METHOD_ARGUMENT Package;
    ULONG PctLength;
    ULONG PssLength;
    ULONG Ppc = 0;
    ULONG MaximumFrequency = 0;
    ULONG Index;
    NTSTATUS Status;
    BOOLEAN Valid = FALSE;

    Status = PnpcpuEvaluateMethod(DeviceExtension, PNPCPU_METHOD('_', 'P', 'C', 'T'), &PctOutput, &PctLength);
    if (!NT_SUCCESS(Status))
        return FALSE;
    if (!PnpcpuParseRegister(PnpcpuGetArgument(PctOutput, PctLength, 0), &DeviceExtension->PerfControl) || !PnpcpuParseRegister(PnpcpuGetArgument(PctOutput, PctLength, 1), &DeviceExtension->PerfStatus))
        goto PctExit;
#if defined(_M_IX86) || defined(_M_AMD64)
    if (DeviceExtension->PerfControl.SpaceId == PNPCPU_SPACE_FIXED_HARDWARE || DeviceExtension->PerfStatus.SpaceId == PNPCPU_SPACE_FIXED_HARDWARE)
    {
        if (!DeviceExtension->IntelEstSupported || DeviceExtension->PerfControl.SpaceId != PNPCPU_SPACE_FIXED_HARDWARE || DeviceExtension->PerfStatus.SpaceId != PNPCPU_SPACE_FIXED_HARDWARE)
            goto PctExit;
        DeviceExtension->PerfControl.Address = PNPCPU_INTEL_PERF_CTL_MSR;
        DeviceExtension->PerfControl.BitWidth = 16;
        DeviceExtension->PerfControl.BitOffset = 0;
        DeviceExtension->PerfControl.AccessSize = 2;
        DeviceExtension->PerfStatus = DeviceExtension->PerfControl;
    }
#else
    if (DeviceExtension->PerfControl.SpaceId == PNPCPU_SPACE_FIXED_HARDWARE || DeviceExtension->PerfStatus.SpaceId == PNPCPU_SPACE_FIXED_HARDWARE)
        goto PctExit;
#endif
    if (!PnpcpuPrepareRegister(&DeviceExtension->PerfControl) || !PnpcpuPrepareRegister(&DeviceExtension->PerfStatus))
        goto PctExit;
    Status = PnpcpuEvaluateMethod(DeviceExtension, PNPCPU_METHOD('_', 'P', 'S', 'S'), &PssOutput, &PssLength);
    if (!NT_SUCCESS(Status))
        goto PctExit;
    PnpcpuQueryInteger(DeviceExtension, PNPCPU_METHOD('_', 'P', 'P', 'C'), &Ppc);
    if (Ppc >= PssOutput->Count)
        Ppc = 0;
    for (Index = Ppc; Index < PssOutput->Count && DeviceExtension->PerfStateCount < PNPCPU_MAX_PERF_STATES; Index++)
    {
        PPNPCPU_PERF_STATE PerfState = &DeviceExtension->PerfStates[DeviceExtension->PerfStateCount];

        Package = PnpcpuGetArgument(PssOutput, PssLength, Index);
        if (!PnpcpuArgumentInteger(PnpcpuGetPackageArgument(Package, 0), &PerfState->Frequency) || !PnpcpuArgumentInteger(PnpcpuGetPackageArgument(Package, 1), &PerfState->Power) || !PnpcpuArgumentInteger(PnpcpuGetPackageArgument(Package, 2), &PerfState->TransitionLatency) || !PnpcpuArgumentInteger(PnpcpuGetPackageArgument(Package, 3), &PerfState->BusMasterLatency) || !PnpcpuArgumentInteger(PnpcpuGetPackageArgument(Package, 4), &PerfState->Control) || !PnpcpuArgumentInteger(PnpcpuGetPackageArgument(Package, 5), &PerfState->Status) || PerfState->Frequency == 0)
            continue;
        if (MaximumFrequency == 0)
            MaximumFrequency = PerfState->Frequency;
        PerfState->Percentage = (UCHAR)max(1, min(100, (PerfState->Frequency * 100) / MaximumFrequency));
        DeviceExtension->PerfStateCount++;
    }
    if (DeviceExtension->PerfStateCount != 0)
    {
        DeviceExtension->PerfMode = PNPCPU_PERF_PSS;
        Valid = TRUE;
    }
    ExFreePoolWithTag(PssOutput, PNPCPU_TAG);
PctExit:
    ExFreePoolWithTag(PctOutput, PNPCPU_TAG);
    if (!Valid)
    {
        PnpcpuReleaseRegister(&DeviceExtension->PerfControl);
        PnpcpuReleaseRegister(&DeviceExtension->PerfStatus);
        DeviceExtension->PerfStateCount = 0;
    }
    return Valid;
}

static
NTSTATUS
FASTCALL
PnpcpuIdleHandler(
    _In_ ULONG_PTR Context,
    _Inout_ PPROCESSOR_IDLE_TIMES IdleTimes)
{
    ULONG ProcessorNumber = KeGetCurrentProcessorNumberEx(NULL);
    PPNPCPU_DEVICE_EXTENSION DeviceExtension;
    PPNPCPU_IDLE_STATE IdleState;
    ULONG Bytes;

    UNREFERENCED_PARAMETER(IdleTimes);
    if (ProcessorNumber >= MAXIMUM_PROCESSORS || Context >= PNPCPU_MAX_IDLE_STATES)
        return STATUS_INVALID_PARAMETER;
    DeviceExtension = InterlockedCompareExchangePointer((PVOID volatile *)&PnpcpuProcessors[ProcessorNumber], NULL, NULL);
    if (!DeviceExtension || DeviceExtension->Removing || Context >= DeviceExtension->IdleStateCount)
        return STATUS_DEVICE_NOT_READY;
    IdleState = &DeviceExtension->IdleStates[Context];
    if (IdleState->Type == 1)
    {
        HalProcessorIdle();
        return STATUS_SUCCESS;
    }
#if defined(_M_IX86) || defined(_M_AMD64)
    if (IdleState->Register.SpaceId == PNPCPU_SPACE_FIXED_HARDWARE && DeviceExtension->MonitorMwaitSupported)
    {
        PnpcpuMonitorMwait(&DeviceExtension->IdleMonitor, (ULONG)IdleState->Register.Address);
        _enable();
        return STATUS_SUCCESS;
    }
#endif
    Bytes = PnpcpuRegisterAccessBytes(&IdleState->Register);
    if (IdleState->Register.SpaceId == PNPCPU_SPACE_SYSTEM_IO)
    {
        if (Bytes == 1)
            (VOID)READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)IdleState->Register.Address);
        else if (Bytes == 2)
            (VOID)READ_PORT_USHORT((PUSHORT)(ULONG_PTR)IdleState->Register.Address);
        else
            (VOID)READ_PORT_ULONG((PULONG)(ULONG_PTR)IdleState->Register.Address);
        _enable();
        return STATUS_SUCCESS;
    }
    if (IdleState->Register.SpaceId == PNPCPU_SPACE_SYSTEM_MEMORY && IdleState->Register.MappedAddress)
    {
        if (Bytes == 1)
            (VOID)*(volatile UCHAR *)IdleState->Register.MappedAddress;
        else if (Bytes == 2)
            (VOID)*(volatile USHORT *)IdleState->Register.MappedAddress;
        else if (Bytes == 4)
            (VOID)*(volatile ULONG *)IdleState->Register.MappedAddress;
        else
            (VOID)*(volatile ULONGLONG *)IdleState->Register.MappedAddress;
        _enable();
        return STATUS_SUCCESS;
    }
    HalProcessorIdle();
    return STATUS_SUCCESS;
}

static
NTSTATUS
FASTCALL
PnpcpuSetPerfLevel(
    _In_ UCHAR Throttle)
{
    ULONG ProcessorNumber = KeGetCurrentProcessorNumberEx(NULL);
    PPNPCPU_DEVICE_EXTENSION DeviceExtension;
    LONG ThermalLimit;
    ULONG Index;
    ULONG Selected = 0;
    ULONG SelectedPercentage = 0;
    ULONG LowestPercentage = MAXULONG;
    ULONGLONG Value;
    BOOLEAN Found = FALSE;
    NTSTATUS Status;

    if (ProcessorNumber >= MAXIMUM_PROCESSORS)
        return STATUS_INVALID_PARAMETER;
    DeviceExtension = InterlockedCompareExchangePointer((PVOID volatile *)&PnpcpuProcessors[ProcessorNumber], NULL, NULL);
    if (!DeviceExtension || DeviceExtension->Removing)
        return STATUS_DEVICE_NOT_READY;
    ThermalLimit = InterlockedCompareExchange(&DeviceExtension->ThermalLimit, 0, 0);
    if (ThermalLimit < 0)
        ThermalLimit = 0;
    if (ThermalLimit > 100)
        ThermalLimit = 100;
    Throttle = (UCHAR)min((ULONG)Throttle, (ULONG)ThermalLimit);
    if (DeviceExtension->PerfMode == PNPCPU_PERF_CPPC)
    {
        Value = ((ULONGLONG)DeviceExtension->CppcHighest * Throttle) / 100;
        Value = max(Value, (ULONGLONG)DeviceExtension->CppcLowest);
        Value = min(Value, (ULONGLONG)DeviceExtension->CppcHighest);
        return PnpcpuWriteRegister(&DeviceExtension->CppcDesired, Value);
    }
    if (DeviceExtension->PerfMode != PNPCPU_PERF_PSS || DeviceExtension->PerfStateCount == 0)
        return STATUS_NOT_SUPPORTED;
    for (Index = 0; Index < DeviceExtension->PerfStateCount; Index++)
    {
        ULONG Percentage = DeviceExtension->PerfStates[Index].Percentage;

        if (Percentage < LowestPercentage)
        {
            LowestPercentage = Percentage;
            Selected = Index;
        }
        if (Percentage <= Throttle && (!Found || Percentage > SelectedPercentage))
        {
            Found = TRUE;
            SelectedPercentage = Percentage;
            Selected = Index;
        }
    }
    Status = PnpcpuWriteRegister(&DeviceExtension->PerfControl, DeviceExtension->PerfStates[Selected].Control);
    if (NT_SUCCESS(Status) && NT_SUCCESS(PnpcpuReadRegister(&DeviceExtension->PerfStatus, &Value)) && Value != DeviceExtension->PerfStates[Selected].Status)
        DPRINT1("PNPCPU: CPU %lu P-state status 0x%I64x expected 0x%lx\n", ProcessorNumber, Value, DeviceExtension->PerfStates[Selected].Status);
    return Status;
}

static
VOID
NTAPI
PnpcpuThermalInterfaceReference(
    _In_ PVOID Context)
{
    PPNPCPU_DEVICE_EXTENSION DeviceExtension = Context;

    ObReferenceObject(DeviceExtension->Self);
}

static
VOID
NTAPI
PnpcpuThermalInterfaceDereference(
    _In_ PVOID Context)
{
    PPNPCPU_DEVICE_EXTENSION DeviceExtension = Context;

    ObDereferenceObject(DeviceExtension->Self);
}

static
VOID
PnpcpuPassiveCooling(
    _Inout_opt_ PVOID Context,
    _In_ ULONG Percentage)
{
    PPNPCPU_DEVICE_EXTENSION DeviceExtension = Context;
    KAFFINITY PreviousAffinity;
    NTSTATUS Status;

    if (!DeviceExtension)
        return;
    if (Percentage > 100)
        Percentage = 100;
    InterlockedExchange(&DeviceExtension->ThermalLimit, (LONG)Percentage);
    if (!DeviceExtension->Started || DeviceExtension->Removing || !DeviceExtension->PowerRegistered || !DeviceExtension->ProcessorNumberValid || DeviceExtension->ProcessorNumber >= sizeof(KAFFINITY) * 8)
        return;
    PreviousAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1 << DeviceExtension->ProcessorNumber);
    Status = PnpcpuSetPerfLevel((UCHAR)Percentage);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
    if (!NT_SUCCESS(Status))
        DPRINT1("PNPCPU: CPU %lu thermal limit %lu%% failed, status 0x%08lx\n", DeviceExtension->ProcessorNumber, Percentage, Status);
}

static
NTSTATUS
PnpcpuQueryThermalInterface(
    _In_ PPNPCPU_DEVICE_EXTENSION DeviceExtension,
    _In_ PIO_STACK_LOCATION Stack)
{
    PTHERMAL_COOLING_INTERFACE Interface;

    if (!Stack->Parameters.QueryInterface.InterfaceType || RtlCompareMemory(Stack->Parameters.QueryInterface.InterfaceType, &GUID_THERMAL_COOLING_INTERFACE, sizeof(GUID)) != sizeof(GUID))
        return STATUS_NOT_SUPPORTED;
    if (!DeviceExtension->Started || DeviceExtension->Removing || !DeviceExtension->PowerRegistered || DeviceExtension->PerfMode == PNPCPU_PERF_NONE)
        return STATUS_DEVICE_NOT_READY;
    if (!Stack->Parameters.QueryInterface.Interface || Stack->Parameters.QueryInterface.Size < sizeof(*Interface))
        return STATUS_BUFFER_TOO_SMALL;
    if (Stack->Parameters.QueryInterface.Version != THERMAL_COOLING_INTERFACE_VERSION)
        return STATUS_NOT_SUPPORTED;
    Interface = (PTHERMAL_COOLING_INTERFACE)Stack->Parameters.QueryInterface.Interface;
    RtlZeroMemory(Interface, sizeof(*Interface));
    Interface->Size = sizeof(*Interface);
    Interface->Version = THERMAL_COOLING_INTERFACE_VERSION;
    Interface->Context = DeviceExtension;
    Interface->InterfaceReference = PnpcpuThermalInterfaceReference;
    Interface->InterfaceDereference = PnpcpuThermalInterfaceDereference;
    Interface->Flags = ThermalDeviceFlagPassiveCooling;
    Interface->PassiveCooling = PnpcpuPassiveCooling;
    Interface->InterfaceReference(Interface->Context);
    return STATUS_SUCCESS;
}

static
NTSTATUS
PnpcpuCaptureCppcRegister(
    _In_ PPNPCPU_REGISTER Register,
    _Out_ PULONGLONG OriginalValue,
    _In_ ULONG RestoreFlag,
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    NTSTATUS Status;

    if (!Register->Valid)
        return STATUS_SUCCESS;
    Status = PnpcpuReadRegister(Register, OriginalValue);
    if (NT_SUCCESS(Status))
        DeviceExtension->CppcRestoreMask |= RestoreFlag;
    return Status;
}

static
VOID
PnpcpuRestoreCppcConfiguration(
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->CppcRestoreMask & PNPCPU_CPPC_RESTORE_DESIRED)
        PnpcpuWriteRegister(&DeviceExtension->CppcDesired, DeviceExtension->CppcOriginalDesired);
    if (DeviceExtension->CppcRestoreMask & PNPCPU_CPPC_RESTORE_MINIMUM)
        PnpcpuWriteRegister(&DeviceExtension->CppcMinimum, DeviceExtension->CppcOriginalMinimum);
    if (DeviceExtension->CppcRestoreMask & PNPCPU_CPPC_RESTORE_MAXIMUM)
        PnpcpuWriteRegister(&DeviceExtension->CppcMaximum, DeviceExtension->CppcOriginalMaximum);
    if (DeviceExtension->CppcRestoreMask & PNPCPU_CPPC_RESTORE_ENABLE)
        PnpcpuWriteRegister(&DeviceExtension->CppcEnable, DeviceExtension->CppcOriginalEnable);
    if (DeviceExtension->CppcRestoreMask & PNPCPU_CPPC_RESTORE_AUTONOMOUS)
        PnpcpuWriteRegister(&DeviceExtension->CppcAutonomous, DeviceExtension->CppcOriginalAutonomous);
    DeviceExtension->CppcRestoreMask = 0;
}

static
NTSTATUS
PnpcpuEnableCppcConfiguration(
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    NTSTATUS Status = STATUS_SUCCESS;
    NTSTATUS WriteStatus;

    if (DeviceExtension->PerfMode != PNPCPU_PERF_CPPC)
        return STATUS_SUCCESS;
    DeviceExtension->CppcRestoreMask = 0;
    WriteStatus = PnpcpuCaptureCppcRegister(&DeviceExtension->CppcDesired, &DeviceExtension->CppcOriginalDesired, PNPCPU_CPPC_RESTORE_DESIRED, DeviceExtension);
    if (!NT_SUCCESS(WriteStatus))
        Status = WriteStatus;
    WriteStatus = PnpcpuCaptureCppcRegister(&DeviceExtension->CppcMinimum, &DeviceExtension->CppcOriginalMinimum, PNPCPU_CPPC_RESTORE_MINIMUM, DeviceExtension);
    if (!NT_SUCCESS(WriteStatus))
        Status = WriteStatus;
    WriteStatus = PnpcpuCaptureCppcRegister(&DeviceExtension->CppcMaximum, &DeviceExtension->CppcOriginalMaximum, PNPCPU_CPPC_RESTORE_MAXIMUM, DeviceExtension);
    if (!NT_SUCCESS(WriteStatus))
        Status = WriteStatus;
    WriteStatus = PnpcpuCaptureCppcRegister(&DeviceExtension->CppcEnable, &DeviceExtension->CppcOriginalEnable, PNPCPU_CPPC_RESTORE_ENABLE, DeviceExtension);
    if (!NT_SUCCESS(WriteStatus))
        Status = WriteStatus;
    WriteStatus = PnpcpuCaptureCppcRegister(&DeviceExtension->CppcAutonomous, &DeviceExtension->CppcOriginalAutonomous, PNPCPU_CPPC_RESTORE_AUTONOMOUS, DeviceExtension);
    if (!NT_SUCCESS(WriteStatus))
        Status = WriteStatus;
    if (!NT_SUCCESS(Status))
    {
        PnpcpuRestoreCppcConfiguration(DeviceExtension);
        return Status;
    }
    if (DeviceExtension->CppcEnable.Valid)
    {
        WriteStatus = PnpcpuWriteRegister(&DeviceExtension->CppcEnable, 1);
        if (!NT_SUCCESS(WriteStatus))
            Status = WriteStatus;
    }
    if (DeviceExtension->CppcMinimum.Valid)
    {
        WriteStatus = PnpcpuWriteRegister(&DeviceExtension->CppcMinimum, DeviceExtension->CppcLowest);
        if (!NT_SUCCESS(WriteStatus))
            Status = WriteStatus;
    }
    if (DeviceExtension->CppcMaximum.Valid)
    {
        WriteStatus = PnpcpuWriteRegister(&DeviceExtension->CppcMaximum, DeviceExtension->CppcHighest);
        if (!NT_SUCCESS(WriteStatus))
            Status = WriteStatus;
    }
    if (DeviceExtension->CppcAutonomous.Valid)
    {
        WriteStatus = PnpcpuWriteRegister(&DeviceExtension->CppcAutonomous, 0);
        if (!NT_SUCCESS(WriteStatus))
            Status = WriteStatus;
    }
    WriteStatus = PnpcpuWriteRegister(&DeviceExtension->CppcDesired, DeviceExtension->CppcNominal ? DeviceExtension->CppcNominal : DeviceExtension->CppcHighest);
    if (!NT_SUCCESS(WriteStatus))
        Status = WriteStatus;
    if (!NT_SUCCESS(Status))
        PnpcpuRestoreCppcConfiguration(DeviceExtension);
    return Status;
}

static
NTSTATUS
PnpcpuRegisterPowerConfiguration(
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    UCHAR HandlerStorage[FIELD_OFFSET(PROCESSOR_STATE_HANDLER2, PerfLevel) + PNPCPU_MAX_PERF_STATES * sizeof(PROCESSOR_PERF_LEVEL)];
    PPROCESSOR_STATE_HANDLER2 Handler = (PPROCESSOR_STATE_HANDLER2)HandlerStorage;
    KAFFINITY PreviousAffinity;
    PVOID PreviousDevice;
    ULONG Index;
    NTSTATUS Status;

    if (!DeviceExtension->ProcessorNumberValid)
        return STATUS_NOT_FOUND;
    PreviousDevice = InterlockedCompareExchangePointer((PVOID volatile *)&PnpcpuProcessors[DeviceExtension->ProcessorNumber], DeviceExtension, NULL);
    if (PreviousDevice && PreviousDevice != DeviceExtension)
        return STATUS_OBJECT_NAME_COLLISION;
    RtlZeroMemory(HandlerStorage, sizeof(HandlerStorage));
    Handler->NumIdleHandlers = DeviceExtension->IdleStateCount;
    for (Index = 0; Index < DeviceExtension->IdleStateCount; Index++)
    {
        Handler->IdleHandler[Index].HardwareLatency = DeviceExtension->IdleStates[Index].Latency;
        Handler->IdleHandler[Index].Handler = PnpcpuIdleHandler;
    }
    Handler->HardwareLatency = 0;
    if (DeviceExtension->PerfMode != PNPCPU_PERF_NONE)
    {
        Handler->SetPerfLevel = PnpcpuSetPerfLevel;
        Handler->NumPerfStates = (UCHAR)DeviceExtension->PerfStateCount;
        for (Index = 0; Index < DeviceExtension->PerfStateCount; Index++)
            Handler->PerfLevel[Index].PercentFrequency = DeviceExtension->PerfStates[Index].Percentage;
    }
    PreviousAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1 << DeviceExtension->ProcessorNumber);
    Status = PnpcpuEnableCppcConfiguration(DeviceExtension);
    if (NT_SUCCESS(Status))
        Status = ZwPowerInformation(ProcessorStateHandler2, Handler, FIELD_OFFSET(PROCESSOR_STATE_HANDLER2, PerfLevel) + Handler->NumPerfStates * sizeof(Handler->PerfLevel[0]), NULL, 0);
    else
        DPRINT1("PNPCPU: CPU %lu CPPC initialization failed, status 0x%08lx\n", DeviceExtension->ProcessorNumber, Status);
    if (!NT_SUCCESS(Status))
        PnpcpuRestoreCppcConfiguration(DeviceExtension);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
    if (!NT_SUCCESS(Status))
    {
        InterlockedCompareExchangePointer((PVOID volatile *)&PnpcpuProcessors[DeviceExtension->ProcessorNumber], NULL, DeviceExtension);
        return Status;
    }
    DeviceExtension->PowerRegistered = TRUE;
    DPRINT1("PNPCPU: P0 CPU %lu uid=%lu apic=%lu idle=%lu perf=%s/%lu registered SMP\n", DeviceExtension->ProcessorNumber, DeviceExtension->Uid, DeviceExtension->ApicId, DeviceExtension->IdleStateCount, DeviceExtension->PerfMode == PNPCPU_PERF_CPPC ? "CPPC" : (DeviceExtension->PerfMode == PNPCPU_PERF_PSS ? "PSS" : "none"), DeviceExtension->PerfStateCount);
    return STATUS_SUCCESS;
}

static
VOID
PnpcpuUnregisterPowerConfiguration(
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    UCHAR HandlerStorage[FIELD_OFFSET(PROCESSOR_STATE_HANDLER2, PerfLevel)];
    PPROCESSOR_STATE_HANDLER2 Handler = (PPROCESSOR_STATE_HANDLER2)HandlerStorage;
    KAFFINITY PreviousAffinity;
    NTSTATUS Status;

    if (!DeviceExtension->PowerRegistered || !DeviceExtension->ProcessorNumberValid)
        return;
    RtlZeroMemory(HandlerStorage, sizeof(HandlerStorage));
    PreviousAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1 << DeviceExtension->ProcessorNumber);
    Status = ZwPowerInformation(ProcessorStateHandler2, Handler, sizeof(HandlerStorage), NULL, 0);
    PnpcpuRestoreCppcConfiguration(DeviceExtension);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
    if (!NT_SUCCESS(Status))
        DPRINT1("PNPCPU: CPU %lu power unregister failed, status 0x%08lx\n", DeviceExtension->ProcessorNumber, Status);
    InterlockedCompareExchangePointer((PVOID volatile *)&PnpcpuProcessors[DeviceExtension->ProcessorNumber], NULL, DeviceExtension);
    DeviceExtension->PowerRegistered = FALSE;
}

static
VOID
PnpcpuRefreshPowerConfiguration(
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
#if defined(_M_IX86) || defined(_M_AMD64)
    PnpcpuQueryProcessorFeatures(DeviceExtension);
#endif
    PnpcpuUnregisterPowerConfiguration(DeviceExtension);
    PnpcpuReleasePowerConfiguration(DeviceExtension);
    PnpcpuParseCst(DeviceExtension);
    if (!PnpcpuParseCppc(DeviceExtension))
        PnpcpuParsePss(DeviceExtension);
    if (DeviceExtension->ProcessorNumberValid)
    {
        NTSTATUS Status = PnpcpuRegisterPowerConfiguration(DeviceExtension);

        if (!NT_SUCCESS(Status))
            DPRINT1("PNPCPU: CPU mapping %lu power registration failed, status 0x%08lx\n", DeviceExtension->ProcessorNumber, Status);
    }
    else
    {
        DPRINT1("PNPCPU: inactive firmware processor uid=%s%lu apic=%s%lu left unregistered\n", DeviceExtension->UidValid ? "" : "?", DeviceExtension->Uid, DeviceExtension->ApicIdValid ? "" : "?", DeviceExtension->ApicId);
    }
}

static
BOOLEAN
PnpcpuProbeMethod(
    _In_ PPNPCPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG MethodName,
    _Out_ PULONG Count)
{
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer;
    ULONG OutputLength;
    NTSTATUS Status;

    *Count = 0;
    Status = PnpcpuEvaluateMethod(DeviceExtension, MethodName, &OutputBuffer, &OutputLength);
    if (!NT_SUCCESS(Status))
        return FALSE;
    *Count = OutputBuffer->Count;
    ExFreePoolWithTag(OutputBuffer, PNPCPU_TAG);
    return TRUE;
}

static
VOID
PnpcpuWriteRegistryDword(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR Name,
    _In_ ULONG Value)
{
    UNICODE_STRING ValueName;

    RtlInitUnicodeString(&ValueName, Name);
    ZwSetValueKey(KeyHandle, &ValueName, 0, REG_DWORD, &Value, sizeof(Value));
}

static
VOID
PnpcpuPublishProperties(
    _In_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    HANDLE KeyHandle;
    NTSTATUS Status;

    Status = IoOpenDeviceRegistryKey(DeviceExtension->Pdo, PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &KeyHandle);
    if (!NT_SUCCESS(Status))
        return;
    if (DeviceExtension->UidValid)
        PnpcpuWriteRegistryDword(KeyHandle, L"AcpiUid", DeviceExtension->Uid);
    if (DeviceExtension->ApicIdValid)
        PnpcpuWriteRegistryDword(KeyHandle, L"ApicId", DeviceExtension->ApicId);
    if (DeviceExtension->ProximityValid)
        PnpcpuWriteRegistryDword(KeyHandle, L"ProximityDomain", DeviceExtension->ProximityDomain);
    if (DeviceExtension->ProcessorNumberValid)
        PnpcpuWriteRegistryDword(KeyHandle, L"ProcessorNumber", DeviceExtension->ProcessorNumber);
    PnpcpuWriteRegistryDword(KeyHandle, L"PowerCapabilities", DeviceExtension->CapabilityMask);
    PnpcpuWriteRegistryDword(KeyHandle, L"IdleStateCount", DeviceExtension->IdleStateCount);
    PnpcpuWriteRegistryDword(KeyHandle, L"PerformanceMode", DeviceExtension->PerfMode);
    PnpcpuWriteRegistryDword(KeyHandle, L"PerformanceStateCount", DeviceExtension->PerfStateCount);
    ZwClose(KeyHandle);
}

static
VOID
PnpcpuRefreshCapabilities(
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    static const ULONG Methods[8] =
    {
        PNPCPU_METHOD('_', 'C', 'P', 'C'),
        PNPCPU_METHOD('_', 'C', 'S', 'T'),
        PNPCPU_METHOD('_', 'P', 'S', 'S'),
        PNPCPU_METHOD('_', 'P', 'C', 'T'),
        PNPCPU_METHOD('_', 'P', 'S', 'D'),
        PNPCPU_METHOD('_', 'P', 'P', 'C'),
        PNPCPU_METHOD('_', 'T', 'S', 'S'),
        PNPCPU_METHOD('_', 'T', 'S', 'D')
    };
    ULONG CapabilityMask = 0;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Methods); Index++)
    {
        if (PnpcpuProbeMethod(DeviceExtension, Methods[Index], &DeviceExtension->CapabilityCounts[Index]))
            CapabilityMask |= 1u << Index;
    }
    DeviceExtension->CapabilityMask = CapabilityMask;
    PnpcpuPublishProperties(DeviceExtension);

    DPRINT1("PNPCPU: uid=%s%lu apic=%s%lu pxm=%s%lu active=%lu caps=0x%02lx CPC=%lu CST=%lu PSS=%lu PCT=%lu PSD=%lu PPC=%lu TSS=%lu TSD=%lu\n",
            DeviceExtension->UidValid ? "" : "?", DeviceExtension->Uid,
            DeviceExtension->ApicIdValid ? "" : "?", DeviceExtension->ApicId,
            DeviceExtension->ProximityValid ? "" : "?", DeviceExtension->ProximityDomain,
            KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS), DeviceExtension->CapabilityMask,
            DeviceExtension->CapabilityCounts[0], DeviceExtension->CapabilityCounts[1],
            DeviceExtension->CapabilityCounts[2], DeviceExtension->CapabilityCounts[3],
            DeviceExtension->CapabilityCounts[4], DeviceExtension->CapabilityCounts[5],
            DeviceExtension->CapabilityCounts[6], DeviceExtension->CapabilityCounts[7]);
}

static
VOID
PnpcpuWorker(
    _In_ PVOID Context)
{
    PPNPCPU_WORK_CONTEXT WorkContext = Context;
    PPNPCPU_DEVICE_EXTENSION DeviceExtension = WorkContext->DeviceExtension;

    ExAcquireFastMutex(&DeviceExtension->ConfigurationLock);
    if (DeviceExtension->Started && !DeviceExtension->Removing)
    {
        PnpcpuRefreshCapabilities(DeviceExtension);
        PnpcpuRefreshPowerConfiguration(DeviceExtension);
        PnpcpuPublishProperties(DeviceExtension);
    }
    ExReleaseFastMutex(&DeviceExtension->ConfigurationLock);
    if (InterlockedDecrement(&DeviceExtension->WorkCount) == 0)
        KeSetEvent(&DeviceExtension->WorkIdleEvent, IO_NO_INCREMENT, FALSE);
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, DeviceExtension);
    ExFreePoolWithTag(WorkContext, PNPCPU_TAG);
}

static
VOID
NTAPI
PnpcpuNotification(
    _In_ PVOID Context,
    _In_ ULONG NotifyCode)
{
    PPNPCPU_DEVICE_EXTENSION DeviceExtension = Context;
    PPNPCPU_WORK_CONTEXT WorkContext;

    if (NotifyCode != 0x80 && NotifyCode != 0x81 && NotifyCode != 0x82)
        return;
    if (!DeviceExtension->Started || DeviceExtension->Removing)
        return;
    if (!NT_SUCCESS(IoAcquireRemoveLock(&DeviceExtension->RemoveLock, DeviceExtension)))
        return;
    WorkContext = ExAllocatePoolWithTag(NonPagedPool, sizeof(*WorkContext), PNPCPU_TAG);
    if (!WorkContext)
    {
        IoReleaseRemoveLock(&DeviceExtension->RemoveLock, DeviceExtension);
        return;
    }
    WorkContext->DeviceExtension = DeviceExtension;
    ExInitializeWorkItem(&WorkContext->WorkItem, PnpcpuWorker, WorkContext);
    if (InterlockedIncrement(&DeviceExtension->WorkCount) == 1)
        KeClearEvent(&DeviceExtension->WorkIdleEvent);
    DPRINT1("PNPCPU: ACPI notification 0x%02lx; refreshing processor capabilities\n", NotifyCode);
    ExQueueWorkItem(&WorkContext->WorkItem, DelayedWorkQueue);
}

static
NTSTATUS
PnpcpuQueryAcpiInterface(
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    RtlZeroMemory(&DeviceExtension->AcpiInterface, sizeof(DeviceExtension->AcpiInterface));
    DeviceExtension->AcpiInterface.Size = sizeof(DeviceExtension->AcpiInterface);
    DeviceExtension->AcpiInterface.Version = 1;
    Irp = IoAllocateIrp(DeviceExtension->LowerDevice->StackSize, FALSE);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_PNP;
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType = &GUID_ACPI_INTERFACE_STANDARD;
    Stack->Parameters.QueryInterface.Size = sizeof(DeviceExtension->AcpiInterface);
    Stack->Parameters.QueryInterface.Version = 1;
    Stack->Parameters.QueryInterface.Interface = (PINTERFACE)&DeviceExtension->AcpiInterface;
    Stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;
    IoSetCompletionRoutine(Irp, PnpcpuCompletion, &Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    IoFreeIrp(Irp);
    if (NT_SUCCESS(Status))
        DeviceExtension->InterfaceAcquired = TRUE;
    return Status;
}

static
VOID
PnpcpuReleaseAcpiInterface(
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->NotificationsRegistered)
    {
        DeviceExtension->AcpiInterface.UnregisterForDeviceNotifications(DeviceExtension->AcpiInterface.Context, PnpcpuNotification);
        DeviceExtension->NotificationsRegistered = FALSE;
    }
    if (DeviceExtension->InterfaceAcquired)
    {
        DeviceExtension->AcpiInterface.InterfaceDereference(DeviceExtension->AcpiInterface.Context);
        DeviceExtension->InterfaceAcquired = FALSE;
    }
}

static
NTSTATUS
PnpcpuStartDevice(
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    NTSTATUS Status;

    ExAcquireFastMutex(&DeviceExtension->ConfigurationLock);
    DeviceExtension->UidValid = PnpcpuQueryInteger(DeviceExtension, PNPCPU_METHOD('_', 'U', 'I', 'D'), &DeviceExtension->Uid);
    DeviceExtension->ProximityValid = PnpcpuQueryInteger(DeviceExtension, PNPCPU_METHOD('_', 'P', 'X', 'M'), &DeviceExtension->ProximityDomain);
    PnpcpuQueryMat(DeviceExtension);
    PnpcpuFindProcessorNumber(DeviceExtension);
    PnpcpuRefreshCapabilities(DeviceExtension);
    DeviceExtension->Started = TRUE;
    PnpcpuRefreshPowerConfiguration(DeviceExtension);
    PnpcpuPublishProperties(DeviceExtension);
    ExReleaseFastMutex(&DeviceExtension->ConfigurationLock);

    Status = PnpcpuQueryAcpiInterface(DeviceExtension);
    if (NT_SUCCESS(Status))
    {
        Status = DeviceExtension->AcpiInterface.RegisterForDeviceNotifications(DeviceExtension->AcpiInterface.Context, PnpcpuNotification, DeviceExtension);
        if (NT_SUCCESS(Status))
            DeviceExtension->NotificationsRegistered = TRUE;
        else
            DPRINT1("PNPCPU: notification registration failed, status 0x%08lx\n", Status);
    }
    else
    {
        DPRINT1("PNPCPU: ACPI interface query failed, status 0x%08lx\n", Status);
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
PnpcpuPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PPNPCPU_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = PnpcpuForwardSynchronously(DeviceExtension, Irp);
            if (NT_SUCCESS(Status))
                Status = PnpcpuStartDevice(DeviceExtension);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_QUERY_INTERFACE:
            Status = PnpcpuQueryThermalInterface(DeviceExtension, Stack);
            if (NT_SUCCESS(Status))
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }
            break;

        case IRP_MN_STOP_DEVICE:
            DeviceExtension->Started = FALSE;
            PnpcpuReleaseAcpiInterface(DeviceExtension);
            ExAcquireFastMutex(&DeviceExtension->ConfigurationLock);
            PnpcpuUnregisterPowerConfiguration(DeviceExtension);
            PnpcpuReleasePowerConfiguration(DeviceExtension);
            ExReleaseFastMutex(&DeviceExtension->ConfigurationLock);
            break;

        case IRP_MN_SURPRISE_REMOVAL:
            DeviceExtension->Started = FALSE;
            DeviceExtension->Removing = TRUE;
            PnpcpuReleaseAcpiInterface(DeviceExtension);
            ExAcquireFastMutex(&DeviceExtension->ConfigurationLock);
            PnpcpuUnregisterPowerConfiguration(DeviceExtension);
            ExReleaseFastMutex(&DeviceExtension->ConfigurationLock);
            break;

        case IRP_MN_REMOVE_DEVICE:
            Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
            if (!NT_SUCCESS(Status))
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }
            DeviceExtension->Started = FALSE;
            DeviceExtension->Removing = TRUE;
            PnpcpuReleaseAcpiInterface(DeviceExtension);
            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            PnpcpuUnregisterPowerConfiguration(DeviceExtension);
            PnpcpuReleasePowerConfiguration(DeviceExtension);
            Status = PnpcpuForwardSynchronously(DeviceExtension, Irp);
            IoDetachDevice(DeviceExtension->LowerDevice);
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
PnpcpuPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PPNPCPU_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
PnpcpuAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PPNPCPU_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(*DeviceExtension), NULL, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    DeviceExtension->Self = DeviceObject;
    DeviceExtension->Pdo = PhysicalDeviceObject;
    DeviceExtension->ThermalLimit = 100;
    IoInitializeRemoveLock(&DeviceExtension->RemoveLock, PNPCPU_TAG, 0, 0);
    ExInitializeFastMutex(&DeviceExtension->ConfigurationLock);
    KeInitializeEvent(&DeviceExtension->WorkIdleEvent, NotificationEvent, TRUE);
    Status = IoAttachDeviceToDeviceStackSafe(DeviceObject, PhysicalDeviceObject, &DeviceExtension->LowerDevice);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(DeviceObject);
        return Status;
    }

    DeviceObject->Flags |= DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
PnpcpuUnload(
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
    UNREFERENCED_PARAMETER(RegistryPath);
    DriverObject->MajorFunction[IRP_MJ_PNP] = PnpcpuPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = PnpcpuPower;
    DriverObject->DriverExtension->AddDevice = PnpcpuAddDevice;
    DriverObject->DriverUnload = PnpcpuUnload;
    return STATUS_SUCCESS;
}
