/*
 * PROJECT:     ReactOS ACPI Processor Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ACPI P-state parsing helpers
 * COPYRIGHT:   2025 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "intelppm.h"

#include <intrin.h>

#define NDEBUG
#include <debug.h>

#ifndef NonPagedPoolNx
#define NonPagedPoolNx NonPagedPool
#endif

#define ACPIPROC_PSS_FIELD_COUNT    6

#define ACPI_PCT_GENERIC_REGISTER_DESCRIPTOR   0x82

#ifndef ACPI_ADR_SPACE_SYSTEM_MEMORY
#define ACPI_ADR_SPACE_SYSTEM_MEMORY    0x00
#define ACPI_ADR_SPACE_SYSTEM_IO        0x01
#endif

#define MSR_IA32_PERF_STATUS            0x00000198
#define MSR_IA32_PERF_CTL               0x00000199

/* Intel Hardware P-States (HWP) MSRs */
#define MSR_IA32_PM_ENABLE              0x00000770
#define MSR_IA32_HWP_CAPABILITIES       0x00000771
#define MSR_IA32_HWP_REQUEST            0x00000774

/* Cached HWP detection result: -1 = not checked, 0 = no, 1 = yes */
static volatile LONG AcpiprocHwpActive = -1;

/**
 * @brief Detect whether Intel HWP is enabled on this CPU.
 *
 * Checks CPUID leaf 6 EAX[7] for HWP support, then reads MSR 0x770
 * to see if HWP was enabled (by the HAL at boot time).
 * Result is cached so CPUID/MSR is only hit once.
 */
static
BOOLEAN
AcpiprocIsHwpActive(VOID)
{
    LONG Cached = InterlockedCompareExchange(&AcpiprocHwpActive, -1, -1);
    if (Cached >= 0)
        return (BOOLEAN)Cached;

    {
        INT32 CpuidRegs[4];
        BOOLEAN Active = FALSE;

        __cpuid(CpuidRegs, 0);
        /* Check Intel vendor and max leaf >= 6 */
        if (CpuidRegs[1] == 0x756E6547 &&  /* "Genu" */
            CpuidRegs[3] == 0x49656E69 &&  /* "ineI" */
            CpuidRegs[2] == 0x6C65746E &&  /* "ntel" */
            (ULONG)CpuidRegs[0] >= 6)
        {
            __cpuid(CpuidRegs, 6);
            /* CPUID.06H:EAX[7] = HWP supported */
            if (CpuidRegs[0] & (1 << 7))
            {
                /* Check if HAL already enabled HWP */
                ULONGLONG PmEnable = __readmsr(MSR_IA32_PM_ENABLE);
                Active = (PmEnable & 1) != 0;
            }
        }

        InterlockedExchange(&AcpiprocHwpActive, (LONG)Active);
        DPRINT1("AcpiprocIsHwpActive: %s\n", Active ? "YES" : "NO");
        return Active;
    }
}

/**
 * @brief Translate a legacy PERF_CTL value into an HWP_REQUEST write.
 *
 * The ACPI _PSS control value for fixed-hardware contains the desired
 * P-state ratio in bits 15:8 (same layout as MSR_IA32_PERF_CTL).
 * We map that ratio into HWP_REQUEST min/max/desired fields and scale
 * the EPP based on how far the target is from the highest performance.
 */
static
VOID
AcpiprocWriteHwpRequest(
    _In_ ULONGLONG PerfCtlValue)
{
    ULONGLONG HwpCaps, HwpReq;
    ULONG TargetRatio, Highest, Lowest;
    ULONG Epp;

    /* Extract the target ratio from the PERF_CTL-format value (bits 15:8) */
    TargetRatio = (ULONG)((PerfCtlValue >> 8) & 0xFF);

    /* Read HWP capabilities for the valid range */
    HwpCaps = __readmsr(MSR_IA32_HWP_CAPABILITIES);
    Highest = (ULONG)(HwpCaps & 0xFF);         /* bits 7:0 */
    Lowest  = (ULONG)((HwpCaps >> 24) & 0xFF); /* bits 31:24 */

    /* Clamp the target ratio to the HWP range */
    if (TargetRatio > Highest) TargetRatio = Highest;
    if (TargetRatio < Lowest)  TargetRatio = Lowest;

    /*
     * Scale EPP linearly: highest ratio → EPP=0 (performance),
     * lowest ratio → EPP=0xFF (power save).
     */
    if (Highest > Lowest)
        Epp = ((Highest - TargetRatio) * 255) / (Highest - Lowest);
    else
        Epp = 0;

    /* Build the HWP_REQUEST value */
    HwpReq = __readmsr(MSR_IA32_HWP_REQUEST);
    HwpReq &= ~0x00000000FFFFFFFFULL; /* Clear min/max/desired/EPP */
    HwpReq |= (ULONGLONG)TargetRatio;          /* bits 7:0  = Minimum */
    HwpReq |= (ULONGLONG)Highest << 8;         /* bits 15:8 = Maximum */
    HwpReq |= (ULONGLONG)TargetRatio << 16;    /* bits 23:16 = Desired */
    HwpReq |= (ULONGLONG)Epp << 24;            /* bits 31:24 = EPP */

    __writemsr(MSR_IA32_HWP_REQUEST, HwpReq);

    DPRINT("HWP: target=%u highest=%u lowest=%u EPP=%u\n",
           TargetRatio, Highest, Lowest, Epp);
}

#pragma pack(push, 1)
typedef struct _ACPIPROC_PCT_GENERIC_REGISTER_DESCRIPTOR {
    UCHAR DescriptorType;
    USHORT ResourceLength;
    UCHAR AddressSpaceId;
    UCHAR BitWidth;
    UCHAR BitOffset;
    UCHAR AccessSize;
    ULONGLONG Address;
} ACPIPROC_PCT_GENERIC_REGISTER_DESCRIPTOR, *PACPIPROC_PCT_GENERIC_REGISTER_DESCRIPTOR;
#pragma pack(pop)

typedef struct _ACPIPROC_PPC_WORKITEM_CONTEXT {
    PACPIPROC_DEVICE DeviceExtension;
    PIO_WORKITEM WorkItem;
} ACPIPROC_PPC_WORKITEM_CONTEXT, *PACPIPROC_PPC_WORKITEM_CONTEXT;

typedef struct _ACPIPROC_DOMAIN_QUEUE_ENTRY {
    PACPIPROC_DEVICE DeviceExtension;
    PDEVICE_OBJECT DeviceObject;
} ACPIPROC_DOMAIN_QUEUE_ENTRY, *PACPIPROC_DOMAIN_QUEUE_ENTRY;

typedef struct _ACPIPROC_PDC_PARAMETERS {
    ULONG Revision;
    ULONG Count;
    ULONG Capabilities;
} ACPIPROC_PDC_PARAMETERS, *PACPIPROC_PDC_PARAMETERS;

static
VOID
NTAPI
AcpiprocPpcWorkItemRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context);

static
VOID
AcpiprocQueueDomainPeersForPpc(
    _Inout_ PACPIPROC_DEVICE SourceDevice);

static
NTSTATUS
AcpiprocEvaluateBufferMethod(
    _In_ PACPIPROC_DEVICE DeviceExtension,
    _In_reads_(4) PCSTR MethodName,
    _In_reads_bytes_(BufferLength) PVOID Buffer,
    _In_ ULONG BufferLength);

static
VOID
AcpiprocNegotiatePdc(
    _Inout_ PACPIPROC_DEVICE DeviceExtension);

static
ULONG
AcpiprocGetAccessSizeBytes(
    _In_ PACPIPROC_REGISTER_BLOCK RegisterBlock)
{
    ULONG ByteWidth;

    if (RegisterBlock->AccessSize >= 1 && RegisterBlock->AccessSize <= 4)
    {
        ByteWidth = 1UL << (RegisterBlock->AccessSize - 1);
    }
    else if (RegisterBlock->BitWidth != 0)
    {
        ByteWidth = (RegisterBlock->BitWidth + 7) / 8;
    }
    else
    {
        ByteWidth = 0;
    }

    if (ByteWidth == 0)
        ByteWidth = sizeof(ULONG);

    if (ByteWidth > sizeof(ULONGLONG))
        ByteWidth = sizeof(ULONGLONG);

    return ByteWidth;
}

static
ULONGLONG
AcpiprocBuildMask(
    _In_ UCHAR BitWidth)
{
    if (BitWidth == 0)
        return 0;

    if (BitWidth >= 64)
        return ~0ULL;

    return (1ULL << BitWidth) - 1ULL;
}

static
NTSTATUS
AcpiprocRawReadSystemIo(
    _In_ PACPIPROC_REGISTER_BLOCK RegisterBlock,
    _In_ ULONG AccessSize,
    _Out_ PULONGLONG Value)
{
    ULONGLONG Data = 0;
    ULONG_PTR Port = (ULONG_PTR)RegisterBlock->Address;

    if (Port > 0xFFFF)
        return STATUS_NOT_SUPPORTED;

    switch (AccessSize)
    {
        case 1:
            Data = READ_PORT_UCHAR((PUCHAR)Port);
            break;
        case 2:
            Data = READ_PORT_USHORT((PUSHORT)Port);
            break;
        case 4:
            Data = READ_PORT_ULONG((PULONG)Port);
            break;
        case 8:
        {
            ULONGLONG Low = READ_PORT_ULONG((PULONG)Port);
            ULONGLONG High = READ_PORT_ULONG((PULONG)Port + 1);
            Data = Low | (High << 32);
            break;
        }
        default:
            return STATUS_NOT_SUPPORTED;
    }

    *Value = Data;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AcpiprocRawWriteSystemIo(
    _In_ PACPIPROC_REGISTER_BLOCK RegisterBlock,
    _In_ ULONG AccessSize,
    _In_ ULONGLONG Value)
{
    ULONG_PTR Port = (ULONG_PTR)RegisterBlock->Address;

    if (Port > 0xFFFF)
        return STATUS_NOT_SUPPORTED;

    switch (AccessSize)
    {
        case 1:
            WRITE_PORT_UCHAR((PUCHAR)Port, (UCHAR)Value);
            break;
        case 2:
            WRITE_PORT_USHORT((PUSHORT)Port, (USHORT)Value);
            break;
        case 4:
            WRITE_PORT_ULONG((PULONG)Port, (ULONG)Value);
            break;
        case 8:
            WRITE_PORT_ULONG((PULONG)Port, (ULONG)Value);
            WRITE_PORT_ULONG((PULONG)Port + 1, (ULONG)(Value >> 32));
            break;
        default:
            return STATUS_NOT_SUPPORTED;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
AcpiprocRawReadSystemMemory(
    _In_ PACPIPROC_REGISTER_BLOCK RegisterBlock,
    _In_ ULONG AccessSize,
    _Out_ PULONGLONG Value)
{
    PHYSICAL_ADDRESS Phys;
    PVOID Base;
    ULONGLONG Data = 0;

    Phys.QuadPart = RegisterBlock->Address;

    Base = MmMapIoSpace(Phys, AccessSize, MmNonCached);
    if (!Base)
        return STATUS_INSUFFICIENT_RESOURCES;

    switch (AccessSize)
    {
        case 1:
            Data = READ_REGISTER_UCHAR((volatile PUCHAR)Base);
            break;
        case 2:
            Data = READ_REGISTER_USHORT((volatile PUSHORT)Base);
            break;
        case 4:
            Data = READ_REGISTER_ULONG((volatile PULONG)Base);
            break;
        case 8:
            Data = *(volatile ULONGLONG UNALIGNED *)Base;
            break;
        default:
            MmUnmapIoSpace(Base, AccessSize);
            return STATUS_NOT_SUPPORTED;
    }

    MmUnmapIoSpace(Base, AccessSize);
    *Value = Data;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AcpiprocRawWriteSystemMemory(
    _In_ PACPIPROC_REGISTER_BLOCK RegisterBlock,
    _In_ ULONG AccessSize,
    _In_ ULONGLONG Value)
{
    PHYSICAL_ADDRESS Phys;
    PVOID Base;

    Phys.QuadPart = RegisterBlock->Address;

    Base = MmMapIoSpace(Phys, AccessSize, MmNonCached);
    if (!Base)
        return STATUS_INSUFFICIENT_RESOURCES;

    switch (AccessSize)
    {
        case 1:
            WRITE_REGISTER_UCHAR((volatile PUCHAR)Base, (UCHAR)Value);
            break;
        case 2:
            WRITE_REGISTER_USHORT((volatile PUSHORT)Base, (USHORT)Value);
            break;
        case 4:
            WRITE_REGISTER_ULONG((volatile PULONG)Base, (ULONG)Value);
            break;
        case 8:
            *(volatile ULONGLONG UNALIGNED *)Base = Value;
            break;
        default:
            MmUnmapIoSpace(Base, AccessSize);
            return STATUS_NOT_SUPPORTED;
    }

    MmUnmapIoSpace(Base, AccessSize);
    return STATUS_SUCCESS;
}

static
NTSTATUS
AcpiprocRawReadFixedHardware(
    _In_ ACPIPROC_REGISTER_KIND Kind,
    _Out_ PULONGLONG Value)
{
#if defined(_M_IX86) || defined(_M_AMD64)
    switch (Kind)
    {
        case AcpiprocRegisterKindControl:
            if (AcpiprocIsHwpActive())
                *Value = __readmsr(MSR_IA32_HWP_REQUEST);
            else
                *Value = __readmsr(MSR_IA32_PERF_CTL);
            return STATUS_SUCCESS;

        case AcpiprocRegisterKindStatus:
            *Value = __readmsr(MSR_IA32_PERF_STATUS);
            return STATUS_SUCCESS;

        default:
            return STATUS_NOT_SUPPORTED;
    }
#else
    UNREFERENCED_PARAMETER(Kind);
    UNREFERENCED_PARAMETER(Value);
    return STATUS_NOT_SUPPORTED;
#endif
}

static
NTSTATUS
AcpiprocRawWriteFixedHardware(
    _In_ ACPIPROC_REGISTER_KIND Kind,
    _In_ ULONGLONG Value)
{
#if defined(_M_IX86) || defined(_M_AMD64)
    switch (Kind)
    {
        case AcpiprocRegisterKindControl:
            if (AcpiprocIsHwpActive())
                AcpiprocWriteHwpRequest(Value);
            else
                __writemsr(MSR_IA32_PERF_CTL, Value);
            return STATUS_SUCCESS;

        case AcpiprocRegisterKindStatus:
            return STATUS_NOT_SUPPORTED;

        default:
            return STATUS_NOT_SUPPORTED;
    }
#else
    UNREFERENCED_PARAMETER(Kind);
    UNREFERENCED_PARAMETER(Value);
    return STATUS_NOT_SUPPORTED;
#endif
}

static
NTSTATUS
AcpiprocRawReadRegister(
    _In_ PACPIPROC_REGISTER_BLOCK RegisterBlock,
    _In_ ACPIPROC_REGISTER_KIND Kind,
    _In_ ULONG AccessSize,
    _Out_ PULONGLONG Value)
{
    switch (RegisterBlock->AddressSpaceId)
    {
        case ACPI_ADR_SPACE_SYSTEM_IO:
            return AcpiprocRawReadSystemIo(RegisterBlock, AccessSize, Value);

        case ACPI_ADR_SPACE_SYSTEM_MEMORY:
            return AcpiprocRawReadSystemMemory(RegisterBlock, AccessSize, Value);

        case ACPIPROC_ADDRESS_SPACE_FIXED_HARDWARE:
            return AcpiprocRawReadFixedHardware(Kind, Value);

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

static
NTSTATUS
AcpiprocRawWriteRegister(
    _In_ PACPIPROC_REGISTER_BLOCK RegisterBlock,
    _In_ ACPIPROC_REGISTER_KIND Kind,
    _In_ ULONG AccessSize,
    _In_ ULONGLONG Value)
{
    switch (RegisterBlock->AddressSpaceId)
    {
        case ACPI_ADR_SPACE_SYSTEM_IO:
            return AcpiprocRawWriteSystemIo(RegisterBlock, AccessSize, Value);

        case ACPI_ADR_SPACE_SYSTEM_MEMORY:
            return AcpiprocRawWriteSystemMemory(RegisterBlock, AccessSize, Value);

        case ACPIPROC_ADDRESS_SPACE_FIXED_HARDWARE:
            return AcpiprocRawWriteFixedHardware(Kind, Value);

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

NTSTATUS
AcpiprocReadRegister(
    _In_ PACPIPROC_REGISTER_BLOCK RegisterBlock,
    _In_ ACPIPROC_REGISTER_KIND Kind,
    _Out_ PULONGLONG Value)
{
    NTSTATUS Status;
    ULONGLONG RawData;
    ULONGLONG Mask;
    ULONG AccessSize;
    ULONG AccessBits;

    if (!Value)
        return STATUS_INVALID_PARAMETER;

    AccessSize = AcpiprocGetAccessSizeBytes(RegisterBlock);
    AccessBits = AccessSize * 8;

    if (RegisterBlock->AddressSpaceId != ACPIPROC_ADDRESS_SPACE_FIXED_HARDWARE &&
        RegisterBlock->BitWidth != 0 &&
        ((ULONG)RegisterBlock->BitOffset + RegisterBlock->BitWidth) > AccessBits)
    {
        return STATUS_NOT_SUPPORTED;
    }

    Status = AcpiprocRawReadRegister(RegisterBlock, Kind, AccessSize, &RawData);
    if (!NT_SUCCESS(Status))
        return Status;

    if (RegisterBlock->BitWidth == 0 || RegisterBlock->BitWidth >= AccessBits)
    {
        *Value = RawData;
        return STATUS_SUCCESS;
    }

    Mask = AcpiprocBuildMask(RegisterBlock->BitWidth);
    RawData >>= RegisterBlock->BitOffset;
    RawData &= Mask;
    *Value = RawData;

    return STATUS_SUCCESS;
}

NTSTATUS
AcpiprocWriteRegister(
    _In_ PACPIPROC_REGISTER_BLOCK RegisterBlock,
    _In_ ACPIPROC_REGISTER_KIND Kind,
    _In_ ULONGLONG Value)
{
    NTSTATUS Status;
    ULONGLONG Mask;
    ULONGLONG RawData = Value;
    ULONGLONG AccessMask;
    ULONG AccessSize;
    ULONG AccessBits;

    AccessSize = AcpiprocGetAccessSizeBytes(RegisterBlock);
    AccessBits = AccessSize * 8;

    if (RegisterBlock->AddressSpaceId != ACPIPROC_ADDRESS_SPACE_FIXED_HARDWARE &&
        RegisterBlock->BitWidth != 0 &&
        ((ULONG)RegisterBlock->BitOffset + RegisterBlock->BitWidth) > AccessBits)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (RegisterBlock->BitWidth == 0)
        Mask = AcpiprocBuildMask((UCHAR)AccessBits);
    else
        Mask = AcpiprocBuildMask(RegisterBlock->BitWidth);

    RawData &= Mask;

    AccessMask = AcpiprocBuildMask((UCHAR)AccessBits);

    if ((RegisterBlock->BitOffset != 0) ||
        (Mask != AccessMask && RegisterBlock->AddressSpaceId != ACPIPROC_ADDRESS_SPACE_FIXED_HARDWARE))
    {
        ULONGLONG CurrentValue;

        Status = AcpiprocReadRegister(RegisterBlock, Kind, &CurrentValue);
        if (!NT_SUCCESS(Status))
            return Status;

        CurrentValue &= ~(Mask << RegisterBlock->BitOffset);
        RawData = CurrentValue | (RawData << RegisterBlock->BitOffset);
    }

    return AcpiprocRawWriteRegister(RegisterBlock, Kind, AccessSize, RawData);
}

NTSTATUS
AcpiprocExecuteMethod(
    _In_ PACPIPROC_DEVICE DeviceExtension,
    _In_reads_(4) PCSTR MethodName,
    _Outptr_result_bytebuffer_(*OutputLength) PACPI_EVAL_OUTPUT_BUFFER *OutputBuffer,
    _Out_ PULONG OutputLength)
{
    ACPI_EVAL_INPUT_BUFFER InputBuffer;
    ULONG BufferLength;
    PACPI_EVAL_OUTPUT_BUFFER LocalBuffer;
    NTSTATUS Status;
    ULONG Attempt;
    ULONG Index;

    *OutputBuffer = NULL;
    *OutputLength = 0;

    RtlZeroMemory(&InputBuffer, sizeof(InputBuffer));
    InputBuffer.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    RtlZeroMemory(InputBuffer.MethodName, sizeof(InputBuffer.MethodName));
    for (Index = 0; Index < 4; ++Index)
    {
        CHAR Character = MethodName[Index];
        InputBuffer.MethodName[Index] = Character;
        if (Character == '\0')
            break;
    }

    BufferLength = sizeof(ACPI_EVAL_OUTPUT_BUFFER) + 256;

    for (Attempt = 0; Attempt < ACPIPROC_MAX_EVAL_RETRIES; ++Attempt)
    {
        LocalBuffer = ExAllocatePoolWithTag(PagedPool,
                                            BufferLength,
                                            ACPIPROC_TAG);
        if (!LocalBuffer)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(LocalBuffer, BufferLength);

        Status = AcpiprocSendAcpiIrp(DeviceExtension,
                                     IOCTL_ACPI_EVAL_METHOD,
                                     &InputBuffer,
                                     sizeof(InputBuffer),
                                     LocalBuffer,
                                     BufferLength);
        if (Status == STATUS_BUFFER_OVERFLOW)
        {
            ExFreePoolWithTag(LocalBuffer, ACPIPROC_TAG);
            BufferLength <<= 1;
            continue;
        }

        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(LocalBuffer, ACPIPROC_TAG);
            return Status;
        }

        *OutputBuffer = LocalBuffer;
        *OutputLength = BufferLength;
        return STATUS_SUCCESS;
    }

    return STATUS_BUFFER_OVERFLOW;
}

static
NTSTATUS
AcpiprocEvaluateBufferMethod(
    _In_ PACPIPROC_DEVICE DeviceExtension,
    _In_reads_(4) PCSTR MethodName,
    _In_reads_bytes_(BufferLength) PVOID Buffer,
    _In_ ULONG BufferLength)
{
    PACPI_EVAL_INPUT_BUFFER_COMPLEX InputBuffer;
    ULONG InputLength;
    NTSTATUS Status;
    ULONG Index;

    if (!DeviceExtension || !MethodName || !Buffer || BufferLength == 0)
        return STATUS_INVALID_PARAMETER;

    InputLength = FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument) +
                  ACPI_METHOD_ARGUMENT_LENGTH(BufferLength);

    InputBuffer = ExAllocatePoolWithTag(PagedPool, InputLength, ACPIPROC_TAG);
    if (!InputBuffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(InputBuffer, InputLength);
    InputBuffer->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;
    for (Index = 0; Index < 4; ++Index)
    {
        CHAR Character = MethodName[Index];
        InputBuffer->MethodName[Index] = Character;
        if (Character == '\0')
            break;
    }

    InputBuffer->Size = InputLength;
    InputBuffer->ArgumentCount = 1;
    {
        PACPI_METHOD_ARGUMENT MethodArgument = &InputBuffer->Argument[0];
        ACPI_METHOD_SET_ARGUMENT_BUFFER(MethodArgument, Buffer, (USHORT)BufferLength);
    }

    Status = AcpiprocSendAcpiIrp(DeviceExtension,
                                 IOCTL_ACPI_EVAL_METHOD,
                                 InputBuffer,
                                 InputLength,
                                 NULL,
                                 0);

    ExFreePoolWithTag(InputBuffer, ACPIPROC_TAG);
    return Status;
}

NTSTATUS
AcpiprocCopyPackageIntegers(
    _In_ PACPI_METHOD_ARGUMENT PackageArgument,
    _Out_writes_(Count) PULONG Values,
    _In_ ULONG Count)
{
    PACPI_METHOD_ARGUMENT Field;
    ULONG Remaining;
    ULONG Index = 0;

    if (PackageArgument->Type != ACPI_METHOD_ARGUMENT_PACKAGE)
        return STATUS_ACPI_INVALID_DATA;

    Field = (PACPI_METHOD_ARGUMENT)PackageArgument->Data;
    Remaining = PackageArgument->DataLength;

    while ((Remaining >= ACPI_METHOD_ARGUMENT_LENGTH(0)) &&
           (Index < Count))
    {
        ULONG FieldSize;

        if (Field->Type != ACPI_METHOD_ARGUMENT_INTEGER)
            return STATUS_ACPI_INVALID_DATA;

        Values[Index++] = Field->Argument;

        FieldSize = ACPI_METHOD_ARGUMENT_LENGTH_FROM_ARGUMENT(Field);
        if (FieldSize > Remaining)
            return STATUS_ACPI_INVALID_DATA;

        Remaining -= FieldSize;
        Field = ACPI_METHOD_NEXT_ARGUMENT(Field);
    }

    if (Index != Count)
        return STATUS_ACPI_INVALID_DATA;

    return STATUS_SUCCESS;
}

NTSTATUS
AcpiprocParseGenericRegisterDescriptor(
    _In_reads_bytes_(BufferLength) PUCHAR Buffer,
    _In_ ULONG BufferLength,
    _Out_ PACPIPROC_REGISTER_BLOCK RegisterBlock)
{
    PACPIPROC_PCT_GENERIC_REGISTER_DESCRIPTOR Descriptor;

    if (BufferLength < sizeof(*Descriptor))
        return STATUS_ACPI_INVALID_DATA;

    Descriptor = (PACPIPROC_PCT_GENERIC_REGISTER_DESCRIPTOR)Buffer;
    if (Descriptor->DescriptorType != ACPI_PCT_GENERIC_REGISTER_DESCRIPTOR)
        return STATUS_ACPI_INVALID_DATA;

    if (Descriptor->ResourceLength < (sizeof(*Descriptor) - sizeof(Descriptor->DescriptorType) - sizeof(Descriptor->ResourceLength)))
        return STATUS_ACPI_INVALID_DATA;

    RegisterBlock->AddressSpaceId = Descriptor->AddressSpaceId;
    RegisterBlock->BitWidth = Descriptor->BitWidth;
    RegisterBlock->BitOffset = Descriptor->BitOffset;
    RegisterBlock->AccessSize = Descriptor->AccessSize;
    RegisterBlock->Address = Descriptor->Address;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AcpiprocCapturePct(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer = NULL;
    ULONG OutputLength = 0;
    NTSTATUS Status;
    PACPI_METHOD_ARGUMENT Argument;

    Status = AcpiprocExecuteMethod(DeviceExtension, "_PCT", &OutputBuffer, &OutputLength);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!OutputBuffer || OutputBuffer->Count < 2)
    {
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
        return STATUS_ACPI_INVALID_DATA;
    }

    Argument = OutputBuffer->Argument;
    if (Argument->Type != ACPI_METHOD_ARGUMENT_BUFFER)
    {
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
        return STATUS_ACPI_INVALID_DATA;
    }

    Status = AcpiprocParseGenericRegisterDescriptor(Argument->Data,
                                                    Argument->DataLength,
                                                    &DeviceExtension->Perf.ControlRegister);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
        return Status;
    }
    DeviceExtension->Perf.ControlRegisterValid = TRUE;

    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    if (Argument->Type != ACPI_METHOD_ARGUMENT_BUFFER)
    {
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
        return STATUS_ACPI_INVALID_DATA;
    }

    Status = AcpiprocParseGenericRegisterDescriptor(Argument->Data,
                                                    Argument->DataLength,
                                                    &DeviceExtension->Perf.StatusRegister);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
        return Status;
    }
    DeviceExtension->Perf.StatusRegisterValid = TRUE;

    ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
    return STATUS_SUCCESS;
}

static
NTSTATUS
AcpiprocCapturePss(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer = NULL;
    ULONG OutputLength = 0;
    NTSTATUS Status;
    PACPI_METHOD_ARGUMENT Argument;
    ULONG StateIndex;
    PACPIPROC_PSS_ENTRY States;
    size_t AllocationSize;

    Status = AcpiprocExecuteMethod(DeviceExtension, "_PSS", &OutputBuffer, &OutputLength);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!OutputBuffer || OutputBuffer->Count == 0)
    {
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
        return STATUS_ACPI_INVALID_DATA;
    }

    Status = RtlSizeTMult(OutputBuffer->Count,
                          sizeof(ACPIPROC_PSS_ENTRY),
                          &AllocationSize);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    States = ExAllocatePoolWithTag(NonPagedPool, AllocationSize, ACPIPROC_TAG);
    if (!States)
    {
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(States, AllocationSize);

    Argument = OutputBuffer->Argument;
    for (StateIndex = 0; StateIndex < OutputBuffer->Count; ++StateIndex)
    {
        ULONG Values[ACPIPROC_PSS_FIELD_COUNT];

        if (Argument->Type != ACPI_METHOD_ARGUMENT_PACKAGE)
        {
            Status = STATUS_ACPI_INVALID_DATA;
            break;
        }

        Status = AcpiprocCopyPackageIntegers(Argument,
                                             Values,
                                             ACPIPROC_PSS_FIELD_COUNT);
        if (!NT_SUCCESS(Status))
            break;

        States[StateIndex].Frequency = Values[0];
        States[StateIndex].Power = Values[1];
        States[StateIndex].TransitionLatency = Values[2];
        States[StateIndex].BusMasterLatency = Values[3];
        States[StateIndex].Control = Values[4];
        States[StateIndex].Status = Values[5];

        Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    }

    if (NT_SUCCESS(Status))
    {
        ULONG StateCount = OutputBuffer->Count;
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
        DeviceExtension->Perf.States = States;
        DeviceExtension->Perf.StateCount = StateCount;
        return STATUS_SUCCESS;
    }

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(States, ACPIPROC_TAG);
    }

    ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
    return Status;
}

static
NTSTATUS
AcpiprocRefreshPpcLimit(
    _Inout_ PACPIPROC_DEVICE DeviceExtension,
    _Out_opt_ PBOOLEAN LimitChanged)
{
    NTSTATUS Status;
    ULONG Value = 0;
    ULONG OldLimit = DeviceExtension->Perf.PpcLimit;
    BOOLEAN OldValid = DeviceExtension->Perf.PpcValid;

    DeviceExtension->Perf.PpcValid = FALSE;
    DeviceExtension->Perf.PpcLimit = 0;

    Status = AcpiprocEvaluateIntegerMethod(DeviceExtension, "_PPC", &Value);
    if (!NT_SUCCESS(Status))
    {
        if (LimitChanged)
            *LimitChanged = FALSE;

        return Status;
    }

    DeviceExtension->Perf.PpcLimit = Value;
    DeviceExtension->Perf.PpcValid = TRUE;

    if (LimitChanged)
    {
        *LimitChanged = (!OldValid || OldLimit != Value);
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
AcpiprocCapturePsd(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer = NULL;
    ULONG OutputLength = 0;
    PACPI_METHOD_ARGUMENT DomainPackage;
    ULONG Values[ACPIPROC_PSD_FIELD_COUNT];
    NTSTATUS Status;

    DeviceExtension->Perf.Psd.Valid = FALSE;
    RtlZeroMemory(&DeviceExtension->Perf.Psd, sizeof(DeviceExtension->Perf.Psd));

    Status = AcpiprocExecuteMethod(DeviceExtension, "_PSD", &OutputBuffer, &OutputLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (!OutputBuffer || OutputBuffer->Count == 0)
    {
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
        return STATUS_ACPI_INVALID_DATA;
    }

    DomainPackage = OutputBuffer->Argument;
    if (DomainPackage->Type != ACPI_METHOD_ARGUMENT_PACKAGE)
    {
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
        return STATUS_ACPI_INVALID_DATA;
    }

    Status = AcpiprocCopyPackageIntegers(DomainPackage,
                                         Values,
                                         ACPIPROC_PSD_FIELD_COUNT);
    if (NT_SUCCESS(Status))
    {
        DeviceExtension->Perf.Psd.Valid = TRUE;
        DeviceExtension->Perf.Psd.NumEntries = Values[0];
        DeviceExtension->Perf.Psd.Revision = Values[1];
        DeviceExtension->Perf.Psd.Domain = Values[2];
        DeviceExtension->Perf.Psd.CoordType = Values[3];
        DeviceExtension->Perf.Psd.NumProcessors = Values[4];
    }

    ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
    return Status;
}

static
ULONG
AcpiprocResolveMinimumState(
    _In_ PACPIPROC_DEVICE DeviceExtension)
{
    ULONG Limit;

    if (!DeviceExtension->Perf.StateCount)
        return 0;

    if (!DeviceExtension->Perf.PpcValid)
        return 0;

    Limit = DeviceExtension->Perf.PpcLimit;
    if (Limit >= DeviceExtension->Perf.StateCount)
    {
        Limit = DeviceExtension->Perf.StateCount - 1;
    }

    return Limit;
}

static
VOID
AcpiprocApplyPpcLimit(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    ULONG MinimumIndex;
    NTSTATUS Status;

    if (!DeviceExtension->Perf.ControlRegisterValid ||
        DeviceExtension->Perf.StateCount == 0 ||
        !DeviceExtension->Perf.States ||
        !DeviceExtension->Perf.PpcValid)
    {
        return;
    }

    MinimumIndex = AcpiprocResolveMinimumState(DeviceExtension);

    if (DeviceExtension->Perf.CurrentStateValid &&
        DeviceExtension->Perf.CurrentStateIndex >= MinimumIndex)
    {
        return;
    }

    Status = AcpiprocSetPerfStateIndex(DeviceExtension, MinimumIndex);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("acpiproc: Failed to clamp processor %lu to P-state %lu (status 0x%lx)\n",
                DeviceExtension->ProcessorIndex,
                MinimumIndex,
                Status);
    }
}

static
BOOLEAN
AcpiprocIsFatalStatus(
    _In_ NTSTATUS Status)
{
    switch (Status)
    {
        case STATUS_SUCCESS:
        case STATUS_OBJECT_NAME_NOT_FOUND:
        case STATUS_ACPI_INVALID_DATA:
        case STATUS_NOT_SUPPORTED:
            return FALSE;
        default:
            return TRUE;
    }
}

static
BOOLEAN
AcpiprocIsStatePermitted(
    _In_ PACPIPROC_DEVICE DeviceExtension,
    _In_ ULONG StateIndex)
{
    ULONG MinimumIndex;

    if (DeviceExtension->Perf.StateCount == 0)
        return TRUE;

    if (!DeviceExtension->Perf.PpcValid)
        return TRUE;

    MinimumIndex = AcpiprocResolveMinimumState(DeviceExtension);

    return (StateIndex >= MinimumIndex);
}

static
NTSTATUS
AcpiprocQueuePpcRefresh(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    PACPIPROC_PPC_WORKITEM_CONTEXT Context;
    NTSTATUS Status;

    if (!DeviceExtension->Self)
        return STATUS_INVALID_DEVICE_STATE;

    if (InterlockedCompareExchange(&DeviceExtension->Perf.PpcWorkItemQueued, 1, 0) != 0)
        return STATUS_SUCCESS;

    Context = ExAllocatePoolWithTag(NonPagedPoolNx,
                                    sizeof(*Context),
                                    ACPIPROC_TAG);
    if (!Context)
    {
        InterlockedExchange(&DeviceExtension->Perf.PpcWorkItemQueued, 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Context->WorkItem = IoAllocateWorkItem(DeviceExtension->Self);
    if (!Context->WorkItem)
    {
        ExFreePoolWithTag(Context, ACPIPROC_TAG);
        InterlockedExchange(&DeviceExtension->Perf.PpcWorkItemQueued, 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Context->DeviceExtension = DeviceExtension;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Context);
    if (!NT_SUCCESS(Status))
    {
        IoFreeWorkItem(Context->WorkItem);
        ExFreePoolWithTag(Context, ACPIPROC_TAG);
        InterlockedExchange(&DeviceExtension->Perf.PpcWorkItemQueued, 0);
        return Status;
    }

    IoQueueWorkItem(Context->WorkItem,
                    AcpiprocPpcWorkItemRoutine,
                    DelayedWorkQueue,
                    Context);

    return STATUS_SUCCESS;
}

static
VOID
NTAPI
AcpiprocPpcWorkItemRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PACPIPROC_PPC_WORKITEM_CONTEXT WorkContext = (PACPIPROC_PPC_WORKITEM_CONTEXT)Context;
    PACPIPROC_DEVICE DeviceExtension;
    BOOLEAN LimitChanged = FALSE;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (!WorkContext)
        return;

    DeviceExtension = WorkContext->DeviceExtension;

    Status = AcpiprocRefreshPpcLimit(DeviceExtension, &LimitChanged);
    if (NT_SUCCESS(Status))
    {
        if (LimitChanged)
        {
            AcpiprocApplyPpcLimit(DeviceExtension);
            AcpiprocQueueDomainPeersForPpc(DeviceExtension);
        }
    }
    else if (Status != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        DPRINT1("acpiproc: _PPC refresh failed (status 0x%lx)\n", Status);
    }

    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, WorkContext);
    IoFreeWorkItem(WorkContext->WorkItem);
    ExFreePoolWithTag(WorkContext, ACPIPROC_TAG);

    InterlockedExchange(&DeviceExtension->Perf.PpcWorkItemQueued, 0);
}

static
VOID
AcpiprocQueueDomainPeersForPpc(
    _Inout_ PACPIPROC_DEVICE SourceDevice)
{
    ACPIPROC_DOMAIN_QUEUE_ENTRY StackEntries[4];
    PACPIPROC_DOMAIN_QUEUE_ENTRY Entries = StackEntries;
    ULONG Capacity = RTL_NUMBER_OF(StackEntries);
    ULONG Count = 0;
    BOOLEAN Allocated = FALSE;
    PLIST_ENTRY Link;

    if (!SourceDevice->Perf.Psd.Valid)
        return;

    ExAcquireFastMutex(&AcpiprocDeviceListLock);
    for (Link = AcpiprocDeviceList.Flink;
         Link != &AcpiprocDeviceList;
         Link = Link->Flink)
    {
        PACPIPROC_DEVICE Peer = CONTAINING_RECORD(Link, ACPIPROC_DEVICE, ListEntry);

        if (Peer == SourceDevice)
            continue;

        if (!Peer->Started)
            continue;

        if (!Peer->Perf.Psd.Valid)
            continue;

        if (Peer->Perf.Psd.Domain != SourceDevice->Perf.Psd.Domain)
            continue;

        if (Count == Capacity)
        {
            ULONG NewCapacity = Capacity * 2;
            PACPIPROC_DOMAIN_QUEUE_ENTRY NewEntries;

            NewEntries = ExAllocatePoolWithTag(PagedPool,
                                               NewCapacity * sizeof(*NewEntries),
                                               ACPIPROC_TAG);
            if (!NewEntries)
                break;

            RtlCopyMemory(NewEntries,
                          Entries,
                          Count * sizeof(*NewEntries));

            if (Allocated)
            {
                ExFreePoolWithTag(Entries, ACPIPROC_TAG);
            }

            Entries = NewEntries;
            Capacity = NewCapacity;
            Allocated = TRUE;
        }

        ObReferenceObject(Peer->Self);
        Entries[Count].DeviceExtension = Peer;
        Entries[Count].DeviceObject = Peer->Self;
        ++Count;
    }
    ExReleaseFastMutex(&AcpiprocDeviceListLock);

    for (ULONG Index = 0; Index < Count; ++Index)
    {
        NTSTATUS Status = AcpiprocQueuePpcRefresh(Entries[Index].DeviceExtension);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("acpiproc: Failed to queue domain _PPC refresh (status 0x%lx)\n",
                    Status);
        }

        ObDereferenceObject(Entries[Index].DeviceObject);
    }

    if (Allocated)
    {
        ExFreePoolWithTag(Entries, ACPIPROC_TAG);
    }
}

NTSTATUS
AcpiprocInitializePerfStates(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    NTSTATUS StatusPct;
    NTSTATUS StatusPss;
    NTSTATUS StatusPpc;
    NTSTATUS StatusPsd;
    NTSTATUS FatalStatus = STATUS_SUCCESS;

    AcpiprocCleanupPerfStates(DeviceExtension);
    AcpiprocNegotiatePdc(DeviceExtension);

    StatusPct = AcpiprocCapturePct(DeviceExtension);
    StatusPss = AcpiprocCapturePss(DeviceExtension);
    StatusPpc = AcpiprocRefreshPpcLimit(DeviceExtension, NULL);
    StatusPsd = AcpiprocCapturePsd(DeviceExtension);

    if (!NT_SUCCESS(StatusPct))
    {
        if (AcpiprocIsFatalStatus(StatusPct))
        {
            FatalStatus = StatusPct;
        }
        else
        {
            DPRINT1("acpiproc: _PCT missing or invalid (status 0x%lx)\n", StatusPct);
        }

        DeviceExtension->Perf.ControlRegisterValid = FALSE;
        DeviceExtension->Perf.StatusRegisterValid = FALSE;
    }

    if (!NT_SUCCESS(StatusPss))
    {
        if (AcpiprocIsFatalStatus(StatusPss))
        {
            if (FatalStatus == STATUS_SUCCESS)
                FatalStatus = StatusPss;
        }
        else
        {
            DPRINT1("acpiproc: _PSS missing or invalid (status 0x%lx)\n", StatusPss);
        }

        DeviceExtension->Perf.StateCount = 0;
        if (DeviceExtension->Perf.States)
        {
            ExFreePoolWithTag(DeviceExtension->Perf.States, ACPIPROC_TAG);
            DeviceExtension->Perf.States = NULL;
        }
    }

    if (!NT_SUCCESS(StatusPpc))
    {
        if (AcpiprocIsFatalStatus(StatusPpc))
        {
            if (FatalStatus == STATUS_SUCCESS)
                FatalStatus = StatusPpc;
        }
        else if (StatusPpc != STATUS_OBJECT_NAME_NOT_FOUND)
        {
            DPRINT1("acpiproc: _PPC unavailable (status 0x%lx)\n", StatusPpc);
        }
    }

    if (!NT_SUCCESS(StatusPsd))
    {
        if (AcpiprocIsFatalStatus(StatusPsd))
        {
            if (FatalStatus == STATUS_SUCCESS)
                FatalStatus = StatusPsd;
        }
        else if (StatusPsd != STATUS_OBJECT_NAME_NOT_FOUND)
        {
            DPRINT1("acpiproc: _PSD unavailable (status 0x%lx)\n", StatusPsd);
        }
    }

    if (FatalStatus != STATUS_SUCCESS)
    {
        return FatalStatus;
    }

    if (DeviceExtension->Perf.StateCount != 0)
    {
        DPRINT("acpiproc: %lu performance states available\n",
               DeviceExtension->Perf.StateCount);
    }
    else
    {
        DPRINT1("acpiproc: No usable _PSS entries published by firmware\n");
    }

    if (DeviceExtension->Perf.PpcValid)
    {
        DPRINT("acpiproc: _PPC limit = %lu\n",
               DeviceExtension->Perf.PpcLimit);
    }

    if (DeviceExtension->Perf.Psd.Valid)
    {
        DPRINT("acpiproc: _PSD domain=%lu coord=%lu count=%lu\n",
               DeviceExtension->Perf.Psd.Domain,
               DeviceExtension->Perf.Psd.CoordType,
               DeviceExtension->Perf.Psd.NumProcessors);
    }

    return STATUS_SUCCESS;
}

VOID
AcpiprocCleanupPerfStates(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    if (DeviceExtension->Perf.States)
    {
        ExFreePoolWithTag(DeviceExtension->Perf.States, ACPIPROC_TAG);
        DeviceExtension->Perf.States = NULL;
    }

    DeviceExtension->Perf.StateCount = 0;
    DeviceExtension->Perf.ControlRegisterValid = FALSE;
    DeviceExtension->Perf.StatusRegisterValid = FALSE;
    DeviceExtension->Perf.PpcValid = FALSE;
    DeviceExtension->Perf.PpcLimit = 0;
    DeviceExtension->Perf.CurrentStateValid = FALSE;
    DeviceExtension->Perf.CurrentStateIndex = 0;
    InterlockedExchange(&DeviceExtension->Perf.PpcWorkItemQueued, 0);
    RtlZeroMemory(&DeviceExtension->Perf.ControlRegister, sizeof(DeviceExtension->Perf.ControlRegister));
    RtlZeroMemory(&DeviceExtension->Perf.StatusRegister, sizeof(DeviceExtension->Perf.StatusRegister));
    RtlZeroMemory(&DeviceExtension->Perf.Psd, sizeof(DeviceExtension->Perf.Psd));
}

NTSTATUS
AcpiprocSetPerfStateIndex(
    _Inout_ PACPIPROC_DEVICE DeviceExtension,
    _In_ ULONG StateIndex)
{
    PACPIPROC_PSS_ENTRY State;
    NTSTATUS Status;

    if (!DeviceExtension->Perf.ControlRegisterValid ||
        DeviceExtension->Perf.StateCount == 0 ||
        !DeviceExtension->Perf.States)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (StateIndex >= DeviceExtension->Perf.StateCount)
        return STATUS_INVALID_PARAMETER;

    if (!AcpiprocIsStatePermitted(DeviceExtension, StateIndex))
        return STATUS_INVALID_DEVICE_STATE;

    State = &DeviceExtension->Perf.States[StateIndex];
    Status = AcpiprocWriteRegister(&DeviceExtension->Perf.ControlRegister,
                                   AcpiprocRegisterKindControl,
                                   State->Control);
    if (NT_SUCCESS(Status))
    {
        DeviceExtension->Perf.CurrentStateIndex = StateIndex;
        DeviceExtension->Perf.CurrentStateValid = TRUE;
    }

    return Status;
}

static
VOID
AcpiprocNegotiatePdc(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    ACPIPROC_PDC_PARAMETERS Parameters;
    NTSTATUS Status;

    Parameters.Revision = ACPIPROC_PDC_REVISION_ID;
    Parameters.Count = 1;
    Parameters.Capabilities =
        ACPIPROC_PDC_P_FFH |
        ACPIPROC_PDC_T_FFH |
        ACPIPROC_PDC_C_C1_HALT |
        ACPIPROC_PDC_C_C1_FFH |
        ACPIPROC_PDC_C_C2C3_FFH |
        ACPIPROC_PDC_SMP_C1PT |
        ACPIPROC_PDC_SMP_C2C3 |
        ACPIPROC_PDC_SMP_P_SWCOORD |
        ACPIPROC_PDC_SMP_P_HWCOORD |
        ACPIPROC_PDC_SMP_C_SWCOORD |
        ACPIPROC_PDC_SMP_T_SWCOORD;

    Status = AcpiprocEvaluateBufferMethod(DeviceExtension,
                                          "_PDC",
                                          &Parameters,
                                          sizeof(Parameters));
    if (!NT_SUCCESS(Status) && Status != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        DPRINT1("acpiproc: _PDC negotiation failed (status 0x%lx)\n", Status);
    }
}

VOID
AcpiprocHandlePpcNotification(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    NTSTATUS Status;

    if (!DeviceExtension)
        return;

    Status = AcpiprocQueuePpcRefresh(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("acpiproc: Failed to schedule _PPC refresh (status 0x%lx)\n",
                Status);
    }
}
