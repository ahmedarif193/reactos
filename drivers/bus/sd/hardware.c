/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Hardware-specific SDHCI hook dispatcher
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "hardware.h"

#include <acpiioct.h>

#define NDEBUG
#include <debug.h>

NTSTATUS
SdBusHardwareMapResources(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PHYSICAL_ADDRESS HostPhysicalAddress,
    _In_ PCM_PARTIAL_RESOURCE_LIST PartialList)
{
    UNREFERENCED_PARAMETER(FdoExtension);
    UNREFERENCED_PARAMETER(HostPhysicalAddress);
    UNREFERENCED_PARAMETER(PartialList);

    return STATUS_SUCCESS;
}

VOID
SdBusHardwareRelease(
    _In_ PFDO_EXTENSION FdoExtension)
{
    PSDBUS_HARDWARE_EXTENSION HardwareExtension;

    HardwareExtension = FdoExtension->HardwareExtension;
    if (HardwareExtension == NULL)
    {
        return;
    }

    if (HardwareExtension->Ops != NULL &&
        HardwareExtension->Ops->Release != NULL)
    {
        HardwareExtension->Ops->Release(FdoExtension);
        return;
    }

    ExFreePoolWithTag(HardwareExtension, TAG_SDBUS);
    FdoExtension->HardwareExtension = NULL;
}

VOID
SdBusHardwareInitializeController(
    _In_ PFDO_EXTENSION FdoExtension)
{
    PSDBUS_HARDWARE_EXTENSION HardwareExtension;

    HardwareExtension = FdoExtension->HardwareExtension;
    if (HardwareExtension == NULL ||
        HardwareExtension->Ops == NULL ||
        HardwareExtension->Ops->InitializeController == NULL)
    {
        return;
    }

    HardwareExtension->Ops->InitializeController(FdoExtension);
}

VOID
SdBusHardwareSelectPins(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ BOOLEAN MmcTiming)
{
    PSDBUS_HARDWARE_EXTENSION HardwareExtension;

    HardwareExtension = FdoExtension->HardwareExtension;
    if (HardwareExtension == NULL ||
        HardwareExtension->Ops == NULL ||
        HardwareExtension->Ops->SelectPins == NULL)
    {
        return;
    }

    HardwareExtension->Ops->SelectPins(FdoExtension, MmcTiming);
}

BOOLEAN
SdBusHardwareCanVoltageSwitch(
    _In_ PFDO_EXTENSION FdoExtension)
{
    PSDBUS_HARDWARE_EXTENSION HardwareExtension;

    HardwareExtension = FdoExtension->HardwareExtension;
    if (HardwareExtension == NULL ||
        HardwareExtension->Ops == NULL ||
        HardwareExtension->Ops->CanVoltageSwitch == NULL)
    {
        return FALSE;
    }

    return HardwareExtension->Ops->CanVoltageSwitch(FdoExtension);
}

NTSTATUS
SdBusHardwareVoltageSwitch(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ BOOLEAN To18)
{
    PSDBUS_HARDWARE_EXTENSION HardwareExtension;

    HardwareExtension = FdoExtension->HardwareExtension;
    if (HardwareExtension == NULL ||
        HardwareExtension->Ops == NULL ||
        HardwareExtension->Ops->VoltageSwitch == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }

    return HardwareExtension->Ops->VoltageSwitch(FdoExtension, To18);
}

NTSTATUS
SdBusHardwareQueryUhsModes(
    _In_ PFDO_EXTENSION FdoExtension,
    _Out_ PULONG Modes)
{
    PSDBUS_HARDWARE_EXTENSION HardwareExtension;

    *Modes = 0;

    HardwareExtension = FdoExtension->HardwareExtension;
    if (HardwareExtension == NULL ||
        HardwareExtension->Ops == NULL ||
        HardwareExtension->Ops->QueryUhsModes == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }

    return HardwareExtension->Ops->QueryUhsModes(FdoExtension, Modes);
}

VOID
SdBusHardwareGetSlotCapabilities(
    _In_ PFDO_EXTENSION FdoExtension,
    _Inout_ PULONG Capabilities,
    _Inout_ PULONG Capabilities2)
{
    PSDBUS_HARDWARE_EXTENSION HardwareExtension;

    HardwareExtension = FdoExtension->HardwareExtension;
    if (HardwareExtension == NULL ||
        HardwareExtension->Ops == NULL ||
        HardwareExtension->Ops->GetSlotCapabilities == NULL)
    {
        return;
    }

    HardwareExtension->Ops->GetSlotCapabilities(FdoExtension, Capabilities, Capabilities2);
}

#define SDBUS_DSM_FUNC0_V18_CAPABLE  0x08
#define SDBUS_BCM2847_CAPABILITIES   0x0120FA81UL

static const UCHAR SdBusBrcmDsmUuid[16] =
{
    0xA5, 0x3E, 0xC1, 0xF6, 0xCD, 0x65, 0x1F, 0x46,
    0xAB, 0x7A, 0x29, 0xF7, 0xE8, 0xD5, 0xBD, 0x61
};

static const UCHAR SdBusDsdUuid[16] =
{
    0x14, 0xD8, 0xFF, 0xDA, 0xBA, 0x6E, 0x8C, 0x4D,
    0x8A, 0x91, 0xBC, 0x9B, 0xBF, 0x4A, 0xA3, 0x01
};

static NTSTATUS
SdBusAcpiEvaluate(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_reads_bytes_(InputSize) PVOID Input,
    _In_ ULONG InputSize,
    _Out_writes_bytes_(OutputSize) PACPI_EVAL_OUTPUT_BUFFER Output,
    _In_ ULONG OutputSize)
{
    PDEVICE_OBJECT AcpiDevice;
    PIRP Irp;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;

    AcpiDevice = FdoExtension->PhysicalDevice;
    if (AcpiDevice == NULL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(IOCTL_ACPI_EVAL_METHOD,
                                        AcpiDevice,
                                        Input,
                                        InputSize,
                                        Output,
                                        OutputSize,
                                        FALSE,
                                        &Event,
                                        &IoStatus);
    if (Irp == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = IoCallDriver(AcpiDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Output->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE)
    {
        return STATUS_ACPI_INVALID_DATA;
    }

    return STATUS_SUCCESS;
}

static BOOLEAN
SdBusAcpiQueryDsdInteger(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PCSTR Name,
    _Out_ PULONG Value)
{
    ULONG OutputStorage[128];
    ACPI_EVAL_INPUT_BUFFER Input;
    PACPI_EVAL_OUTPUT_BUFFER Output;
    PACPI_METHOD_ARGUMENT Argument;
    PACPI_METHOD_ARGUMENT Properties;
    PUCHAR Cursor;
    PUCHAR End;
    NTSTATUS Status;

    *Value = 0;

    RtlZeroMemory(&Input, sizeof(Input));
    RtlZeroMemory(OutputStorage, sizeof(OutputStorage));
    Input.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    Input.MethodNameAsUlong = 'DSD_';

    Output = (PACPI_EVAL_OUTPUT_BUFFER)OutputStorage;
    Status = SdBusAcpiEvaluate(FdoExtension,
                               &Input,
                               sizeof(Input),
                               Output,
                               sizeof(OutputStorage));
    if (!NT_SUCCESS(Status) || Output->Count < 2)
    {
        return FALSE;
    }

    Argument = Output->Argument;
    if (Argument->Type != ACPI_METHOD_ARGUMENT_BUFFER ||
        Argument->DataLength != sizeof(SdBusDsdUuid) ||
        RtlCompareMemory(Argument->Data,
                         SdBusDsdUuid,
                         sizeof(SdBusDsdUuid)) != sizeof(SdBusDsdUuid))
    {
        return FALSE;
    }

    Properties = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    if (Properties->Type != ACPI_METHOD_ARGUMENT_PACKAGE)
    {
        return FALSE;
    }

    Cursor = Properties->Data;
    End = Properties->Data + Properties->DataLength;
    while (Cursor + sizeof(ACPI_METHOD_ARGUMENT) <= End)
    {
        PACPI_METHOD_ARGUMENT Pair = (PACPI_METHOD_ARGUMENT)Cursor;

        if (Pair->Type == ACPI_METHOD_ARGUMENT_PACKAGE &&
            Pair->DataLength >= 2 * sizeof(ACPI_METHOD_ARGUMENT) &&
            Pair->Data + Pair->DataLength <= End)
        {
            PACPI_METHOD_ARGUMENT NameArg = (PACPI_METHOD_ARGUMENT)Pair->Data;

            if (NameArg->Type == ACPI_METHOD_ARGUMENT_STRING &&
                NameArg->DataLength != 0 &&
                NameArg->Data[NameArg->DataLength - 1] == '\0' &&
                strcmp((PCSTR)NameArg->Data, Name) == 0)
            {
                PACPI_METHOD_ARGUMENT ValueArg = ACPI_METHOD_NEXT_ARGUMENT(NameArg);

                if ((PUCHAR)ValueArg + sizeof(ACPI_METHOD_ARGUMENT) <=
                        Pair->Data + Pair->DataLength &&
                    ValueArg->Type == ACPI_METHOD_ARGUMENT_INTEGER)
                {
                    *Value = ValueArg->Argument;
                    return TRUE;
                }
            }
        }

        Cursor = (PUCHAR)ACPI_METHOD_NEXT_ARGUMENT(Pair);
    }

    return FALSE;
}

static NTSTATUS
SdBusBrcmDsmEvaluate(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG FunctionIndex,
    _Out_opt_ PULONG ResultInteger)
{
    ULONG InputStorage[32];
    ULONG OutputStorage[64];
    PACPI_EVAL_INPUT_BUFFER_COMPLEX Input;
    PACPI_EVAL_OUTPUT_BUFFER Output;
    PACPI_METHOD_ARGUMENT Argument;
    NTSTATUS Status;

    if (ResultInteger != NULL)
    {
        *ResultInteger = 0;
    }

    RtlZeroMemory(InputStorage, sizeof(InputStorage));
    RtlZeroMemory(OutputStorage, sizeof(OutputStorage));

    Input = (PACPI_EVAL_INPUT_BUFFER_COMPLEX)InputStorage;
    Output = (PACPI_EVAL_OUTPUT_BUFFER)OutputStorage;

    Input->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;
    Input->MethodNameAsUlong = 'MSD_';
    Input->ArgumentCount = 4;

    Argument = Input->Argument;
    ACPI_METHOD_SET_ARGUMENT_BUFFER(Argument, SdBusBrcmDsmUuid, sizeof(SdBusBrcmDsmUuid));

    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Argument, 0);

    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Argument, FunctionIndex);

    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    Argument->Type = ACPI_METHOD_ARGUMENT_PACKAGE;
    Argument->DataLength = 0;
    Argument->Argument = 0;

    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    Input->Size = (ULONG)((PUCHAR)Argument - (PUCHAR)Input);

    Status = SdBusAcpiEvaluate(FdoExtension,
                               Input,
                               Input->Size,
                               Output,
                               sizeof(OutputStorage));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusBrcmDsmEvaluate: _DSM function %lu failed (0x%08lx)\n",
                FunctionIndex, Status);
        return Status;
    }

    if (ResultInteger != NULL && Output->Count > 0)
    {
        if (Output->Argument[0].Type == ACPI_METHOD_ARGUMENT_INTEGER)
        {
            *ResultInteger = Output->Argument[0].Argument;
        }
        else if (Output->Argument[0].Type == ACPI_METHOD_ARGUMENT_BUFFER &&
                 Output->Argument[0].DataLength > 0)
        {
            *ResultInteger = Output->Argument[0].Data[0];
        }
    }

    DPRINT1("SdBusBrcmDsmEvaluate: _DSM function %lu succeeded (0x%08lx)\n",
            FunctionIndex, Status);
    return STATUS_SUCCESS;
}

static NTSTATUS
SdBusBrcmDsmProbe(
    _In_ PFDO_EXTENSION FdoExtension,
    _Out_opt_ PULONG SupportedModes)
{
    ULONG Supported = 0;
    NTSTATUS Status;

    Status = SdBusBrcmDsmEvaluate(FdoExtension, 0, &Supported);
    DPRINT1("SdBusBrcmDsmProbe: query status 0x%08lx, function mask 0x%08lx\n",
            Status, Supported);
    if (SupportedModes != NULL)
    {
        *SupportedModes = Supported;
    }
    return Status;
}

static NTSTATUS
SdBusBrcmDsmQueryModes(
    _In_ PFDO_EXTENSION FdoExtension,
    _Out_ PULONG Modes)
{
    ULONG Bitmap = 0;
    NTSTATUS Status;

    Status = SdBusBrcmDsmEvaluate(FdoExtension, 8, &Bitmap);
    if (!NT_SUCCESS(Status))
    {
        *Modes = 0;
        DPRINT1("SdBusBrcmDsmQueryModes: _DSM func8 failed (0x%08lx)\n", Status);
        return Status;
    }

    *Modes = Bitmap & 0xFF;
    DPRINT1("SdBusBrcmDsmQueryModes: firmware UHS mode bitmap 0x%02lx\n", *Modes);
    return STATUS_SUCCESS;
}

static NTSTATUS
SdBusBrcmDsmSetVoltage(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ BOOLEAN To18)
{
    ULONG FunctionIndex;
    NTSTATUS Status;

    FunctionIndex = To18 ? 3 : 4;
    Status = SdBusBrcmDsmEvaluate(FdoExtension, FunctionIndex, NULL);
    DPRINT1("SdBusBrcmDsmSetVoltage: %s status 0x%08lx\n",
            To18 ? "1.8V" : "3.3V", Status);
    return Status;
}

static BOOLEAN
SdBusBrcmstbCanVoltageSwitch(
    _In_ PFDO_EXTENSION FdoExtension)
{
    UNREFERENCED_PARAMETER(FdoExtension);
    return TRUE;
}

static NTSTATUS
SdBusBrcmstbVoltageSwitch(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ BOOLEAN To18)
{
    return SdBusBrcmDsmSetVoltage(FdoExtension, To18);
}

static NTSTATUS
SdBusBrcmstbQueryUhsModes(
    _In_ PFDO_EXTENSION FdoExtension,
    _Out_ PULONG Modes)
{
    return SdBusBrcmDsmQueryModes(FdoExtension, Modes);
}

static const SDBUS_HARDWARE_OPS SdBusBrcmstbOps =
{
    NULL,
    NULL,
    NULL,
    SdBusBrcmstbCanVoltageSwitch,
    SdBusBrcmstbVoltageSwitch,
    SdBusBrcmstbQueryUhsModes,
    NULL
};

static VOID
SdBusBcm2847GetSlotCapabilities(
    _In_ PFDO_EXTENSION FdoExtension,
    _Inout_ PULONG Capabilities,
    _Inout_ PULONG Capabilities2)
{
    ULONG DsdCaps = 0;
    ULONG DsdCapsMask = 0;
    BOOLEAN HasCaps;
    BOOLEAN HasCapsMask;

    HasCapsMask = SdBusAcpiQueryDsdInteger(FdoExtension, "sdhci-caps-mask", &DsdCapsMask);
    HasCaps = SdBusAcpiQueryDsdInteger(FdoExtension, "sdhci-caps", &DsdCaps);

    if (HasCapsMask)
    {
        *Capabilities &= ~DsdCapsMask;
    }
    if (HasCaps)
    {
        *Capabilities |= DsdCaps;
    }
    if (HasCaps || HasCapsMask)
    {
        DPRINT1("SDHCI: firmware _DSD sdhci-caps 0x%08lx mask 0x%08lx -> capabilities 0x%08lx\n",
                DsdCaps, DsdCapsMask, *Capabilities);
        return;
    }

    /*
     * The BCM2835 Arasan block returns zero from its standard capability
     * registers.  Raspberry Pi firmware publishes the usable value as the
     * BCM2847 _DSD "sdhci-caps" property; fall back to that value if the
     * firmware did not expose the property.
     */
    if (*Capabilities == 0)
    {
        *Capabilities = SDBUS_BCM2847_CAPABILITIES;
        *Capabilities2 = 0;
        DPRINT1("SDHCI: BCM2847 zero capabilities overridden with firmware value 0x%08lx\n",
                *Capabilities);
    }
}

static const SDBUS_HARDWARE_OPS SdBusBcm2847Ops =
{
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    SdBusBcm2847GetSlotCapabilities
};

static BOOLEAN
SdBusStringContainsId(
    _In_ PCWSTR String,
    _In_ PCWSTR Id)
{
    SIZE_T StringLen;
    SIZE_T IdLen;
    SIZE_T i;
    SIZE_T j;

    StringLen = wcslen(String);
    IdLen = wcslen(Id);

    if (IdLen == 0 || IdLen > StringLen)
    {
        return FALSE;
    }

    for (i = 0; i + IdLen <= StringLen; i++)
    {
        for (j = 0; j < IdLen; j++)
        {
            WCHAR a = String[i + j];
            WCHAR b = Id[j];

            if (a >= L'a' && a <= L'z')
            {
                a = a - L'a' + L'A';
            }
            if (b >= L'a' && b <= L'z')
            {
                b = b - L'a' + L'A';
            }
            if (a != b)
            {
                break;
            }
        }

        if (j == IdLen)
        {
            return TRUE;
        }
    }

    return FALSE;
}

static BOOLEAN
SdBusMultiSzContainsAnyId(
    _In_ PCWSTR MultiSz,
    _In_ ULONG ByteLength,
    _In_reads_(IdCount) const PCWSTR *Ids,
    _In_ ULONG IdCount)
{
    ULONG Chars;
    ULONG Index;
    ULONG IdIndex;

    Chars = ByteLength / sizeof(WCHAR);
    Index = 0;

    while (Index < Chars && MultiSz[Index] != L'\0')
    {
        PCWSTR Current = &MultiSz[Index];
        SIZE_T CurrentLen = wcslen(Current);

        for (IdIndex = 0; IdIndex < IdCount; IdIndex++)
        {
            if (SdBusStringContainsId(Current, Ids[IdIndex]))
            {
                return TRUE;
            }
        }

        Index += (ULONG)(CurrentLen + 1);
    }

    return FALSE;
}

static BOOLEAN
SdBusDevicePropertyContainsAnyId(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ DEVICE_REGISTRY_PROPERTY Property,
    _In_reads_(IdCount) const PCWSTR *Ids,
    _In_ ULONG IdCount)
{
    NTSTATUS Status;
    ULONG Length = 0;
    PWCHAR Buffer;
    BOOLEAN Match = FALSE;

    (VOID)IoGetDeviceProperty(Pdo, Property, 0, NULL, &Length);
    if (Length == 0)
    {
        return FALSE;
    }

    Buffer = ExAllocatePoolWithTag(PagedPool, Length + sizeof(WCHAR), TAG_SDBUS);
    if (Buffer == NULL)
    {
        return FALSE;
    }

    RtlZeroMemory(Buffer, Length + sizeof(WCHAR));

    Status = IoGetDeviceProperty(Pdo, Property, Length, Buffer, &Length);
    if (NT_SUCCESS(Status))
    {
        Match = SdBusMultiSzContainsAnyId(Buffer, Length, Ids, IdCount);
    }

    ExFreePoolWithTag(Buffer, TAG_SDBUS);
    return Match;
}

static BOOLEAN
SdBusDeviceMatchesAnyId(
    _In_ PDEVICE_OBJECT Pdo,
    _In_reads_(IdCount) const PCWSTR *Ids,
    _In_ ULONG IdCount)
{
    return SdBusDevicePropertyContainsAnyId(Pdo, DevicePropertyHardwareID, Ids, IdCount) ||
           SdBusDevicePropertyContainsAnyId(Pdo, DevicePropertyCompatibleIDs, Ids, IdCount);
}

static NTSTATUS
SdBusHardwareAttachOps(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ const SDBUS_HARDWARE_OPS *Ops)
{
    PSDBUS_HARDWARE_EXTENSION HardwareExtension;

    HardwareExtension = ExAllocatePoolWithTag(NonPagedPool,
                                              sizeof(SDBUS_HARDWARE_EXTENSION),
                                              TAG_SDBUS);
    if (HardwareExtension == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(HardwareExtension, sizeof(*HardwareExtension));
    HardwareExtension->Ops = Ops;
    FdoExtension->HardwareExtension = HardwareExtension;
    return STATUS_SUCCESS;
}

NTSTATUS
SdBusHardwareAttach(
    _In_ PFDO_EXTENSION FdoExtension)
{
    static const PCWSTR Bcm2835SdHostIds[] = { L"BCM2855" };
    static const PCWSTR BrcmstbIds[] = { L"BRCM5D12", L"80860F16" };
    static const PCWSTR Bcm2847Ids[] = { L"BCM2847" };
    PDEVICE_OBJECT Pdo;
    ULONG SupportedModes = 0;
    NTSTATUS Status;

    if (FdoExtension->HardwareExtension != NULL)
    {
        return STATUS_SUCCESS;
    }

    Pdo = FdoExtension->PhysicalDevice;
    if (Pdo == NULL)
    {
        return STATUS_SUCCESS;
    }

    if (SdBusDeviceMatchesAnyId(Pdo, Bcm2835SdHostIds, RTL_NUMBER_OF(Bcm2835SdHostIds)))
    {
        FdoExtension->HostType = SdBusHostBcm2835;
        return STATUS_SUCCESS;
    }

    if (SdBusDeviceMatchesAnyId(Pdo, Bcm2847Ids, RTL_NUMBER_OF(Bcm2847Ids)))
    {
        Status = SdBusHardwareAttachOps(FdoExtension, &SdBusBcm2847Ops);
        if (NT_SUCCESS(Status))
        {
            DPRINT1("SdBusHardwareAttach: BCM2847 hardware ops attached\n");
        }
        return STATUS_SUCCESS;
    }

    if (!SdBusDeviceMatchesAnyId(Pdo, BrcmstbIds, RTL_NUMBER_OF(BrcmstbIds)))
    {
        DPRINT1("SdBusHardwareAttach: not a brcmstb SD host; UHS voltage switch disabled\n");
        return STATUS_SUCCESS;
    }

    Status = SdBusBrcmDsmProbe(FdoExtension, &SupportedModes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusHardwareAttach: brcmstb _DSM probe failed (0x%08lx); staying at 3.3V\n",
                Status);
        return STATUS_SUCCESS;
    }

    DPRINT1("SdBusHardwareAttach: brcmstb _DSM func0 bitmap 0x%08lx\n", SupportedModes);
    if (!(SupportedModes & SDBUS_DSM_FUNC0_V18_CAPABLE))
    {
        DPRINT1("SdBusHardwareAttach: firmware does not advertise 1.8V signalling "
                "(func0 bit3 clear); staying at 3.3V HS\n");
        return STATUS_SUCCESS;
    }

    Status = SdBusHardwareAttachOps(FdoExtension, &SdBusBrcmstbOps);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusHardwareAttach: hardware extension allocation failed; staying at 3.3V\n");
        return STATUS_SUCCESS;
    }

    DPRINT1("SdBusHardwareAttach: brcmstb UHS hardware ops attached; voltage switch enabled\n");
    return STATUS_SUCCESS;
}
