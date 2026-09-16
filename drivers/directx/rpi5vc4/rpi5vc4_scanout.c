/*
 * PROJECT:     ReactOS Raspberry Pi 5 WDDM display-only miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Standard ACPI display-output discovery and scanout geometry.
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "rpi5vc4.h"

#define RPI5VC4_ACPI_METHOD(a, b, c, d) \
    ((ULONG)(UCHAR)(a) | ((ULONG)(UCHAR)(b) << 8) | \
     ((ULONG)(UCHAR)(c) << 16) | ((ULONG)(UCHAR)(d) << 24))

#define RPI5VC4_ACPI_DOD_STANDARD_SCHEME 0x80000000UL
#define RPI5VC4_ACPI_DOD_TYPE_MASK       0x00000f00UL
#define RPI5VC4_ACPI_DOD_TYPE_INTERNAL   0x00000400UL

static BOOLEAN
Rpi5Vc4ValidateEdid(
    _In_reads_bytes_(128) const UCHAR *Edid)
{
    static const UCHAR Header[8] =
        {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
    UCHAR Sum = 0;
    ULONG Index;

    if (RtlCompareMemory(Edid, Header, sizeof(Header)) != sizeof(Header))
        return FALSE;

    for (Index = 0; Index < 128; ++Index)
        Sum = (UCHAR)(Sum + Edid[Index]);

    return Sum == 0;
}

static BOOLEAN
Rpi5Vc4GetAcpiArgumentRange(
    _In_reads_bytes_(OutputSize) PACPI_EVAL_OUTPUT_BUFFER Output,
    _In_ ULONG OutputSize,
    _Out_ PACPI_METHOD_ARGUMENT *First,
    _Out_ PUCHAR *End,
    _Out_ PULONG Count)
{
    PACPI_METHOD_ARGUMENT Argument;
    PUCHAR OutputEnd;

    if (Output == NULL ||
        OutputSize < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) ||
        Output->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE ||
        Output->Length < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) ||
        Output->Length > OutputSize)
    {
        return FALSE;
    }

    Argument = &Output->Argument[0];
    OutputEnd = (PUCHAR)Output + Output->Length;
    if (Output->Count == 1 &&
        (SIZE_T)(OutputEnd - (PUCHAR)Argument) >=
            FIELD_OFFSET(ACPI_METHOD_ARGUMENT, Data) &&
        Argument->Type == ACPI_METHOD_ARGUMENT_PACKAGE)
    {
        if (Argument->DataLength > (SIZE_T)(OutputEnd - Argument->Data))
            return FALSE;

        *First = (PACPI_METHOD_ARGUMENT)Argument->Data;
        *End = Argument->Data + Argument->DataLength;
        /* Packages do not carry an element count in the wire format. */
        *Count = MAXULONG;
        return TRUE;
    }

    *First = Argument;
    *End = OutputEnd;
    *Count = Output->Count;
    return TRUE;
}

static BOOLEAN
Rpi5Vc4FindInternalOutput(
    _In_reads_bytes_(OutputSize) PACPI_EVAL_OUTPUT_BUFFER Output,
    _In_ ULONG OutputSize,
    _Out_ PULONG AcpiUid)
{
    PACPI_METHOD_ARGUMENT Argument;
    PUCHAR End;
    ULONG Remaining;

    if (!Rpi5Vc4GetAcpiArgumentRange(Output,
                                     OutputSize,
                                     &Argument,
                                     &End,
                                     &Remaining))
    {
        return FALSE;
    }

    while ((PUCHAR)Argument < End && Remaining != 0)
    {
        ULONG Length;

        if ((SIZE_T)(End - (PUCHAR)Argument) <
            FIELD_OFFSET(ACPI_METHOD_ARGUMENT, Data))
        {
            return FALSE;
        }

        Length = ACPI_METHOD_ARGUMENT_LENGTH(Argument->DataLength);
        if (Length > (SIZE_T)(End - (PUCHAR)Argument))
            return FALSE;

        if (Argument->Type == ACPI_METHOD_ARGUMENT_INTEGER &&
            Argument->DataLength >= sizeof(ULONG) &&
            (Argument->Argument & RPI5VC4_ACPI_DOD_STANDARD_SCHEME) != 0 &&
            (Argument->Argument & RPI5VC4_ACPI_DOD_TYPE_MASK) ==
                RPI5VC4_ACPI_DOD_TYPE_INTERNAL)
        {
            *AcpiUid = Argument->Argument;
            return TRUE;
        }

        Argument = (PACPI_METHOD_ARGUMENT)((PUCHAR)Argument + Length);
        if (Remaining != MAXULONG)
            --Remaining;
    }

    return FALSE;
}

static BOOLEAN
Rpi5Vc4CopyAcpiEdid(
    _In_reads_bytes_(OutputSize) PACPI_EVAL_OUTPUT_BUFFER Output,
    _In_ ULONG OutputSize,
    _Out_writes_bytes_(128) UCHAR *Edid)
{
    PACPI_METHOD_ARGUMENT Argument;
    PUCHAR End;
    ULONG Count;

    if (!Rpi5Vc4GetAcpiArgumentRange(Output,
                                     OutputSize,
                                     &Argument,
                                     &End,
                                     &Count) ||
        Count == 0 ||
        (SIZE_T)(End - (PUCHAR)Argument) <
            FIELD_OFFSET(ACPI_METHOD_ARGUMENT, Data) ||
        Argument->Type != ACPI_METHOD_ARGUMENT_BUFFER ||
        Argument->DataLength < 128 ||
        Argument->DataLength > (SIZE_T)(End - Argument->Data))
    {
        return FALSE;
    }

    RtlCopyMemory(Edid, Argument->Data, 128);
    return Rpi5Vc4ValidateEdid(Edid);
}

static NTSTATUS
Rpi5Vc4EvaluateNoArgMethod(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG DeviceUid,
    _In_ ULONG Signature,
    _In_ ULONG Method,
    _Out_writes_bytes_(OutputSize) PACPI_EVAL_OUTPUT_BUFFER Output,
    _In_ ULONG OutputSize)
{
    ACPI_EVAL_INPUT_BUFFER_COMPLEX Input;

    RtlZeroMemory(&Input, sizeof(Input));
    RtlZeroMemory(Output, OutputSize);
    Input.Signature = Signature;
    Input.MethodNameAsUlong = Method;
    Input.Size = 0;
    Input.ArgumentCount = 0;

    return DeviceExtension->DxgkInterface.DxgkCbEvalAcpiMethod(
               DeviceExtension->DxgkInterface.DeviceHandle,
               DeviceUid,
               &Input,
               sizeof(Input),
               Output,
               OutputSize);
}

static NTSTATUS
Rpi5Vc4EvaluateDdc(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG AcpiUid,
    _Out_writes_bytes_(OutputSize) PACPI_EVAL_OUTPUT_BUFFER Output,
    _In_ ULONG OutputSize)
{
    ULONG InputStorage[
        (FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument) +
         ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ULONG)) + sizeof(ULONG) - 1) /
        sizeof(ULONG)];
    PACPI_EVAL_INPUT_BUFFER_COMPLEX Input = (PVOID)InputStorage;
    PACPI_METHOD_ARGUMENT Argument;
    const ULONG InputSize = sizeof(InputStorage);

    RtlZeroMemory(InputStorage, sizeof(InputStorage));
    RtlZeroMemory(Output, OutputSize);
    Input->Signature = DXGK_ACPI_USE_ACPI_UID;
    Input->MethodNameAsUlong = RPI5VC4_ACPI_METHOD('_', 'D', 'D', 'C');
    Input->Size = InputSize -
        FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument);
    Input->ArgumentCount = 1;
    Argument = Input->Argument;
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Argument, 1);

    return DeviceExtension->DxgkInterface.DxgkCbEvalAcpiMethod(
               DeviceExtension->DxgkInterface.DeviceHandle,
               AcpiUid,
               Input,
               InputSize,
               Output,
               OutputSize);
}

VOID
Rpi5Vc4DiscoverDisplayOutput(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    UCHAR OutputStorage[512];
    PACPI_EVAL_OUTPUT_BUFFER Output = (PVOID)OutputStorage;
    ULONG AcpiUid;
    NTSTATUS Status;

    DeviceExtension->ScanoutBackend = Rpi5Vc4ScanoutHvs;
    DeviceExtension->DisplayChildCount = RPI5VC4_MAX_CHILD_COUNT;
    DeviceExtension->BootDisplayAcpiUid = 0;
    DeviceExtension->PathRotation = D3DKMDT_VPPR_IDENTITY;
    DeviceExtension->EdidValid = FALSE;

    if (DeviceExtension->DxgkInterface.DxgkCbEvalAcpiMethod == NULL)
        return;

    Status = Rpi5Vc4EvaluateNoArgMethod(
                 DeviceExtension,
                 DISPLAY_ADAPTER_HW_ID,
                 ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE,
                 RPI5VC4_ACPI_METHOD('_', 'D', 'O', 'D'),
                 Output,
                 sizeof(OutputStorage));
    if (!NT_SUCCESS(Status) ||
        !Rpi5Vc4FindInternalOutput(Output,
                                  sizeof(OutputStorage),
                                  &AcpiUid))
    {
        return;
    }

    DeviceExtension->ScanoutBackend = Rpi5Vc4ScanoutFixedFirmware;
    DeviceExtension->DisplayChildCount = 1;
    DeviceExtension->BootDisplayAcpiUid = AcpiUid;

    Status = Rpi5Vc4EvaluateDdc(DeviceExtension,
                                AcpiUid,
                                Output,
                                sizeof(OutputStorage));
    if (NT_SUCCESS(Status) &&
        Rpi5Vc4CopyAcpiEdid(Output,
                            sizeof(OutputStorage),
                            DeviceExtension->Edid))
    {
        DeviceExtension->EdidValid = TRUE;
    }
}

static BOOLEAN
Rpi5Vc4ParsePreferredTiming(
    _In_reads_bytes_(128) const UCHAR *Edid,
    _Out_ PULONG ActiveWidth,
    _Out_ PULONG ActiveHeight,
    _Out_ PULONG TotalWidth,
    _Out_ PULONG TotalHeight,
    _Out_ PSIZE_T PixelRate)
{
    const UCHAR *Timing = Edid + 54;
    ULONG PixelClock10Khz;
    ULONG HBlank;
    ULONG VBlank;

    PixelClock10Khz = Timing[0] | ((ULONG)Timing[1] << 8);
    if (PixelClock10Khz == 0)
        return FALSE;

    *ActiveWidth = Timing[2] | ((ULONG)(Timing[4] & 0xf0) << 4);
    HBlank = Timing[3] | ((ULONG)(Timing[4] & 0x0f) << 8);
    *ActiveHeight = Timing[5] | ((ULONG)(Timing[7] & 0xf0) << 4);
    VBlank = Timing[6] | ((ULONG)(Timing[7] & 0x0f) << 8);
    *TotalWidth = *ActiveWidth + HBlank;
    *TotalHeight = *ActiveHeight + VBlank;
    *PixelRate = (SIZE_T)PixelClock10Khz * 10000;

    return *ActiveWidth != 0 && *ActiveHeight != 0 &&
           *TotalWidth > *ActiveWidth && *TotalHeight > *ActiveHeight;
}

NTSTATUS
Rpi5Vc4ConfigureFirmwareScanout(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ const DXGK_DISPLAY_INFORMATION *DisplayInfo)
{
    ULONG ActiveWidth = DisplayInfo->Width;
    ULONG ActiveHeight = DisplayInfo->Height;
    ULONG TotalWidth = ActiveWidth;
    ULONG TotalHeight = ActiveHeight;
    SIZE_T PixelRate = (SIZE_T)ActiveWidth * ActiveHeight * 60;

    DeviceExtension->ScanoutWidth = DisplayInfo->Width;
    DeviceExtension->ScanoutHeight = DisplayInfo->Height;
    DeviceExtension->ScanoutPitch = DisplayInfo->Pitch;

    if (DeviceExtension->EdidValid)
    {
        ULONG EdidWidth;
        ULONG EdidHeight;
        ULONG EdidTotalWidth;
        ULONG EdidTotalHeight;
        SIZE_T EdidPixelRate;

        if (Rpi5Vc4ParsePreferredTiming(DeviceExtension->Edid,
                                        &EdidWidth,
                                        &EdidHeight,
                                        &EdidTotalWidth,
                                        &EdidTotalHeight,
                                        &EdidPixelRate))
        {
            ActiveWidth = EdidWidth;
            ActiveHeight = EdidHeight;
            TotalWidth = EdidTotalWidth;
            TotalHeight = EdidTotalHeight;
            PixelRate = EdidPixelRate;
        }
    }

    if (Rpi5Vc4IsFixedFirmwareScanout(DeviceExtension) &&
        (ActiveWidth != DisplayInfo->Width ||
         ActiveHeight != DisplayInfo->Height ||
         DisplayInfo->Pitch < DisplayInfo->Width * sizeof(ULONG)))
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    DeviceExtension->TargetHTotal = TotalWidth;
    DeviceExtension->TargetVTotal = TotalHeight;
    DeviceExtension->TargetPixelRate = PixelRate;
    DeviceExtension->TargetVSyncFreq.Numerator = (UINT)PixelRate;
    DeviceExtension->TargetVSyncFreq.Denominator = TotalWidth * TotalHeight;
    DeviceExtension->TargetHSyncFreq.Numerator = (UINT)PixelRate;
    DeviceExtension->TargetHSyncFreq.Denominator = TotalWidth;

    if (Rpi5Vc4IsFixedFirmwareScanout(DeviceExtension) &&
        ActiveHeight > ActiveWidth)
    {
        DeviceExtension->PathRotation = D3DKMDT_VPPR_ROTATE90;
        DeviceExtension->ScreenWidth = ActiveHeight;
        DeviceExtension->ScreenHeight = ActiveWidth;
        DeviceExtension->BytesPerScanLine = ActiveHeight * sizeof(ULONG);
        DeviceExtension->PixelsPerScanLine = ActiveHeight;
    }
    else
    {
        DeviceExtension->PathRotation = D3DKMDT_VPPR_IDENTITY;
        DeviceExtension->ScreenWidth = ActiveWidth;
        DeviceExtension->ScreenHeight = ActiveHeight;
        DeviceExtension->BytesPerScanLine = DisplayInfo->Pitch;
        DeviceExtension->PixelsPerScanLine = DisplayInfo->Pitch / sizeof(ULONG);
    }

    return STATUS_SUCCESS;
}

BOOLEAN
Rpi5Vc4IsFixedFirmwareScanout(
    _In_ const RPI5VC4_DEVICE_EXTENSION *DeviceExtension)
{
    return DeviceExtension->ScanoutBackend == Rpi5Vc4ScanoutFixedFirmware;
}
