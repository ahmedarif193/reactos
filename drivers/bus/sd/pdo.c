/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     PDO PnP dispatch for child SD/eMMC/SDIO devices
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "sdbus.h"

#include <initguid.h>
#include <wdmguid.h>

#define NDEBUG
#include <debug.h>

DEFINE_GUID(GUID_SD_HOST_CONTROLLER_LOCAL,
    0x79626149, 0x04A0, 0x4353, 0xBE, 0x16, 0x4B, 0x34, 0x1B, 0x11, 0x07, 0xA9);

DEFINE_GUID(GUID_SDBUS_INTERFACE_STANDARD_LOCAL,
    0x6BB24D81, 0xE924, 0x4825, 0xAF, 0x49, 0x3A, 0xCD, 0x33, 0xC1, 0xD8, 0x20);

static __inline BOOLEAN
SdBusIsSdioFunctionPdo(
    _In_ PPDO_EXTENSION PdoExtension)
{
    return (PdoExtension->FunctionNumber != 0);
}

static PCWSTR
SdBusEmmcPartitionSuffix(
    _In_ UCHAR PartitionId)
{
    switch (PartitionId)
    {
        case EMMC_PARTITION_BOOT0: return L"EMMC_BOOT0";
        case EMMC_PARTITION_BOOT1: return L"EMMC_BOOT1";
        case EMMC_PARTITION_RPMB:  return L"EMMC_RPMB";
        case EMMC_PARTITION_GPP1:  return L"EMMC_GPP1";
        case EMMC_PARTITION_GPP2:  return L"EMMC_GPP2";
        case EMMC_PARTITION_GPP3:  return L"EMMC_GPP3";
        case EMMC_PARTITION_GPP4:  return L"EMMC_GPP4";
        default:                   return L"EMMC_UNKNOWN";
    }
}

static NTSTATUS
SdBusFormatEmmcPartitionHardwareIds(
    _In_ PPDO_EXTENSION PdoExtension,
    _Out_writes_(BufSize) PWCHAR Buffer,
    _In_ SIZE_T BufSize,
    _Out_ PULONG OutLengthWchars)
{
    PWCHAR Cursor = Buffer;
    SIZE_T Remaining = BufSize;
    NTSTATUS Status;
    SIZE_T Written;
    PCWSTR ClassSuffix = SdBusEmmcPartitionSuffix(PdoExtension->EmmcPartitionId);

    Status = RtlStringCchPrintfW(
        Cursor, Remaining,
        L"SD\\VID_%02X&OID_%04X&CLASS_%s",
        (ULONG)PdoExtension->Cid.ManufacturerId,
        (ULONG)PdoExtension->Cid.OemId,
        ClassSuffix);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Written = wcslen(Cursor) + 1;
    Cursor += Written;
    Remaining -= Written;

    Status = RtlStringCchPrintfW(
        Cursor, Remaining,
        L"SD\\CLASS_%s",
        ClassSuffix);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Written = wcslen(Cursor) + 1;
    Cursor += Written;
    Remaining -= Written;

    Status = RtlStringCchPrintfW(
        Cursor, Remaining,
        L"SD\\GenericEmmcPartition");
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Written = wcslen(Cursor) + 1;
    Cursor += Written;
    Remaining -= Written;

    if (Remaining < 1)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    Cursor += 1;

    return STATUS_SUCCESS;
}

static NTSTATUS
SdBusFormatEmmcPartitionCompatibleIds(
    _In_ PPDO_EXTENSION PdoExtension,
    _Out_writes_(BufSize) PWCHAR Buffer,
    _In_ SIZE_T BufSize,
    _Out_ PULONG OutLengthWchars)
{
    PWCHAR Cursor = Buffer;
    SIZE_T Remaining = BufSize;
    NTSTATUS Status;
    SIZE_T Written;
    PCWSTR ClassSuffix = SdBusEmmcPartitionSuffix(PdoExtension->EmmcPartitionId);

    Status = RtlStringCchPrintfW(
        Cursor, Remaining,
        L"SD\\CLASS_%s",
        ClassSuffix);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Written = wcslen(Cursor) + 1;
    Cursor += Written;
    Remaining -= Written;

    Status = RtlStringCchPrintfW(
        Cursor, Remaining,
        L"SD\\GenericEmmcPartition");
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Written = wcslen(Cursor) + 1;
    Cursor += Written;
    Remaining -= Written;

    if (Remaining < 1)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    Cursor += 1;

    return STATUS_SUCCESS;
}

static NTSTATUS
SdBusFormatSdioHardwareIds(
    _In_ PPDO_EXTENSION PdoExtension,
    _Out_writes_(BufSize) PWCHAR Buffer,
    _In_ SIZE_T BufSize,
    _Out_ PULONG OutLengthWchars)
{
    PWCHAR Cursor = Buffer;
    SIZE_T Remaining = BufSize;
    NTSTATUS Status;
    SIZE_T Written;

    Status = RtlStringCchPrintfW(
        Cursor, Remaining,
        L"SDIO\\VEN_%04X&DEV_%04X&FUNC_%u",
        PdoExtension->SdioVendorId,
        PdoExtension->SdioDeviceId,
        PdoExtension->FunctionNumber);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Written = wcslen(Cursor) + 1;
    Cursor += Written;
    Remaining -= Written;

    Status = RtlStringCchPrintfW(
        Cursor, Remaining,
        L"SDIO\\VEN_%04X&CLASS_%02X&FUNC_%u",
        PdoExtension->SdioVendorId,
        PdoExtension->SdioClass,
        PdoExtension->FunctionNumber);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Written = wcslen(Cursor) + 1;
    Cursor += Written;
    Remaining -= Written;

    Status = RtlStringCchPrintfW(
        Cursor, Remaining,
        L"SDIO\\CLASS_%02X&FUNC_%u",
        PdoExtension->SdioClass,
        PdoExtension->FunctionNumber);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Written = wcslen(Cursor) + 1;
    Cursor += Written;
    Remaining -= Written;

    Status = RtlStringCchPrintfW(
        Cursor, Remaining,
        L"SDIO\\GenericSDIO");
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Written = wcslen(Cursor) + 1;
    Cursor += Written;
    Remaining -= Written;

    if (Remaining < 1)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    Cursor += 1;

    return STATUS_SUCCESS;
}

static NTSTATUS
SdBusFormatSdioCompatibleIds(
    _In_ PPDO_EXTENSION PdoExtension,
    _Out_writes_(BufSize) PWCHAR Buffer,
    _In_ SIZE_T BufSize,
    _Out_ PULONG OutLengthWchars)
{
    PWCHAR Cursor = Buffer;
    SIZE_T Remaining = BufSize;
    NTSTATUS Status;
    SIZE_T Written;

    Status = RtlStringCchPrintfW(
        Cursor, Remaining,
        L"SDIO\\CLASS_%02X",
        PdoExtension->SdioClass);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Written = wcslen(Cursor) + 1;
    Cursor += Written;
    Remaining -= Written;

    Status = RtlStringCchPrintfW(
        Cursor, Remaining,
        L"SDIO\\GenericSDIO");
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Written = wcslen(Cursor) + 1;
    Cursor += Written;
    Remaining -= Written;

    if (Remaining < 1)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    Cursor += 1;

    return STATUS_SUCCESS;
}

/**
 * @brief Handle IRP_MN_QUERY_ID for a child SD card PDO.
 *
 * Returns device ID, hardware IDs, compatible IDs, or instance ID strings
 * derived from the card's CID register and card type. Hardware IDs encode
 * manufacturer, OEM, and product information; compatible IDs provide a
 * generic class fallback (e.g. SD\\CLASS_MEMORY).
 *
 * @param[in]     PdoExtension  Pointer to the PDO device extension.
 * @param[in,out] Irp           Pointer to the IRP_MN_QUERY_ID IRP.
 *
 * @return STATUS_SUCCESS on success, STATUS_INSUFFICIENT_RESOURCES, or
 *         STATUS_NOT_SUPPORTED for unknown ID types.
 */
static NTSTATUS
SdBusPdoQueryId(
    _In_ PPDO_EXTENSION PdoExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    BUS_QUERY_ID_TYPE IdType;
    PWCHAR Buffer;
    WCHAR TempBuffer[512];
    ULONG Length;
    NTSTATUS Status;
    BOOLEAN IsSdioFunction;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    IdType = IoStack->Parameters.QueryId.IdType;
    IsSdioFunction = SdBusIsSdioFunctionPdo(PdoExtension);

    RtlZeroMemory(TempBuffer, sizeof(TempBuffer));

    switch (IdType)
    {
        case BusQueryDeviceID:
            /*
             * Hardware ID format:
             */
            if (PdoExtension->IsEmmcPartition)
            {
                Status = RtlStringCchPrintfW(
                    TempBuffer,
                    RTL_NUMBER_OF(TempBuffer),
                    L"SD\\VID_%02X&OID_%04X&CLASS_%s",
                    (ULONG)PdoExtension->Cid.ManufacturerId,
                    (ULONG)PdoExtension->Cid.OemId,
                    SdBusEmmcPartitionSuffix(PdoExtension->EmmcPartitionId));
            }
            else if (IsSdioFunction)
            {
                Status = RtlStringCchPrintfW(
                    TempBuffer,
                    RTL_NUMBER_OF(TempBuffer),
                    L"SDIO\\VEN_%04X&DEV_%04X&FUNC_%u",
                    PdoExtension->SdioVendorId,
                    PdoExtension->SdioDeviceId,
                    PdoExtension->FunctionNumber);
            }
            else if (PdoExtension->CardType == SdCardTypeSdio)
            {
                Status = RtlStringCchPrintfW(
                    TempBuffer,
                    RTL_NUMBER_OF(TempBuffer),
                    L"SD\\VID_%04X&PID_%04X",
                    (ULONG)PdoExtension->Cid.ManufacturerId,
                    PdoExtension->Cid.ProductSerialNumber & 0xFFFF);
            }
            else
            {
                Status = RtlStringCchPrintfW(
                    TempBuffer,
                    RTL_NUMBER_OF(TempBuffer),
                    L"SD\\VID_%02X&OID_%04X&PID_%c%c%c%c%c",
                    (ULONG)PdoExtension->Cid.ManufacturerId,
                    (ULONG)PdoExtension->Cid.OemId,
                    PdoExtension->Cid.ProductName[0] ? PdoExtension->Cid.ProductName[0] : L'_',
                    PdoExtension->Cid.ProductName[1] ? PdoExtension->Cid.ProductName[1] : L'_',
                    PdoExtension->Cid.ProductName[2] ? PdoExtension->Cid.ProductName[2] : L'_',
                    PdoExtension->Cid.ProductName[3] ? PdoExtension->Cid.ProductName[3] : L'_',
                    PdoExtension->Cid.ProductName[4] ? PdoExtension->Cid.ProductName[4] : L'_');
            }

            if (!NT_SUCCESS(Status))
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }
            break;

        case BusQueryHardwareIDs:
            /*
             * Multi-string of hardware IDs.
             * Primary: same as DeviceID
             * Secondary: generic class ID
             */
            if (PdoExtension->IsEmmcPartition)
            {
                Status = SdBusFormatEmmcPartitionHardwareIds(PdoExtension,
                                                             TempBuffer,
                                                             RTL_NUMBER_OF(TempBuffer),
                                                             &Length);
                if (!NT_SUCCESS(Status))
                {
                    Irp->IoStatus.Status = Status;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return Status;
                }

                Buffer = (PWCHAR)ExAllocatePoolWithTag(PagedPool,
                                                        Length * sizeof(WCHAR),
                                                        TAG_SDBUS);
                if (Buffer == NULL)
                {
                    Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }

                RtlCopyMemory(Buffer, TempBuffer, Length * sizeof(WCHAR));
                Irp->IoStatus.Information = (ULONG_PTR)Buffer;
                Irp->IoStatus.Status = STATUS_SUCCESS;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_SUCCESS;
            }

            if (IsSdioFunction)
            {
                Status = SdBusFormatSdioHardwareIds(PdoExtension,
                                                   TempBuffer,
                                                   RTL_NUMBER_OF(TempBuffer),
                                                   &Length);
                if (!NT_SUCCESS(Status))
                {
                    Irp->IoStatus.Status = Status;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return Status;
                }

                Buffer = (PWCHAR)ExAllocatePoolWithTag(PagedPool,
                                                        Length * sizeof(WCHAR),
                                                        TAG_SDBUS);
                if (Buffer == NULL)
                {
                    Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }

                RtlCopyMemory(Buffer, TempBuffer, Length * sizeof(WCHAR));
                Irp->IoStatus.Information = (ULONG_PTR)Buffer;
                Irp->IoStatus.Status = STATUS_SUCCESS;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_SUCCESS;
            }

            if (PdoExtension->CardType == SdCardTypeSdio)
            {
                Status = RtlStringCchPrintfW(
                    TempBuffer,
                    RTL_NUMBER_OF(TempBuffer) - 2,
                    L"SD\\VID_%04X&PID_%04X",
                    (ULONG)PdoExtension->Cid.ManufacturerId,
                    PdoExtension->Cid.ProductSerialNumber & 0xFFFF);
            }
            else
            {
                Status = RtlStringCchPrintfW(
                    TempBuffer,
                    RTL_NUMBER_OF(TempBuffer) - 2,
                    L"SD\\VID_%02X&OID_%04X&PID_%c%c%c%c%c",
                    (ULONG)PdoExtension->Cid.ManufacturerId,
                    (ULONG)PdoExtension->Cid.OemId,
                    PdoExtension->Cid.ProductName[0] ? PdoExtension->Cid.ProductName[0] : L'_',
                    PdoExtension->Cid.ProductName[1] ? PdoExtension->Cid.ProductName[1] : L'_',
                    PdoExtension->Cid.ProductName[2] ? PdoExtension->Cid.ProductName[2] : L'_',
                    PdoExtension->Cid.ProductName[3] ? PdoExtension->Cid.ProductName[3] : L'_',
                    PdoExtension->Cid.ProductName[4] ? PdoExtension->Cid.ProductName[4] : L'_');
            }

            if (!NT_SUCCESS(Status))
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }

            /* Append a second null to form a multi-string */
            Length = (ULONG)wcslen(TempBuffer);
            TempBuffer[Length + 1] = UNICODE_NULL;
            /* The multi-string needs space for the double null */
            Length += 2;

            Buffer = (PWCHAR)ExAllocatePoolWithTag(PagedPool,
                                                    Length * sizeof(WCHAR),
                                                    TAG_SDBUS);
            if (Buffer == NULL)
            {
                Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            RtlCopyMemory(Buffer, TempBuffer, Length * sizeof(WCHAR));
            Irp->IoStatus.Information = (ULONG_PTR)Buffer;
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;

        case BusQueryInstanceID:
        {
            /*
             * Include parent PCI location (bus/address) so identical cards on
             * different controllers don't collapse to one device instance.
             */
            ULONG ParentBusNumber = MAXULONG;
            ULONG ParentAddress = MAXULONG;
            ULONG ResultLength = 0;
            NTSTATUS BusStatus;
            NTSTATUS AddressStatus;

            BusStatus = IoGetDeviceProperty(
                PdoExtension->FdoExtension->PhysicalDevice,
                DevicePropertyBusNumber,
                sizeof(ParentBusNumber),
                &ParentBusNumber,
                &ResultLength);
            if (!NT_SUCCESS(BusStatus))
            {
                ParentBusNumber = MAXULONG;
            }

            AddressStatus = IoGetDeviceProperty(
                PdoExtension->FdoExtension->PhysicalDevice,
                DevicePropertyAddress,
                sizeof(ParentAddress),
                &ParentAddress,
                &ResultLength);
            if (!NT_SUCCESS(AddressStatus))
            {
                ParentAddress = MAXULONG;
            }

            if (PdoExtension->IsEmmcPartition)
            {
                if (ParentBusNumber != MAXULONG && ParentAddress != MAXULONG)
                {
                    Status = RtlStringCchPrintfW(
                        TempBuffer,
                        RTL_NUMBER_OF(TempBuffer),
                        L"%08lX_%08lX_%08lX_EMMC_PART_%u",
                        ParentBusNumber,
                        ParentAddress,
                        PdoExtension->Cid.ProductSerialNumber,
                        PdoExtension->EmmcPartitionId);
                }
                else
                {
                    Status = RtlStringCchPrintfW(
                        TempBuffer,
                        RTL_NUMBER_OF(TempBuffer),
                        L"%08lX_%04lX_EMMC_PART_%u",
                        PdoExtension->Cid.ProductSerialNumber,
                        PdoExtension->RelativeAddress & 0xFFFF,
                        PdoExtension->EmmcPartitionId);
                }
            }
            else if (IsSdioFunction)
            {
                if (ParentBusNumber != MAXULONG && ParentAddress != MAXULONG)
                {
                    Status = RtlStringCchPrintfW(
                        TempBuffer,
                        RTL_NUMBER_OF(TempBuffer),
                        L"%08lX_%08lX_%04X_%04X_FUNC_%u",
                        ParentBusNumber,
                        ParentAddress,
                        PdoExtension->SdioVendorId,
                        PdoExtension->SdioDeviceId,
                        PdoExtension->FunctionNumber);
                }
                else
                {
                    Status = RtlStringCchPrintfW(
                        TempBuffer,
                        RTL_NUMBER_OF(TempBuffer),
                        L"%04X_%04X_FUNC_%u_RCA_%04lX",
                        PdoExtension->SdioVendorId,
                        PdoExtension->SdioDeviceId,
                        PdoExtension->FunctionNumber,
                        PdoExtension->RelativeAddress & 0xFFFF);
                }
            }
            else if (ParentBusNumber != MAXULONG && ParentAddress != MAXULONG)
            {
                Status = RtlStringCchPrintfW(
                    TempBuffer,
                    RTL_NUMBER_OF(TempBuffer),
                    L"%08lX_%08lX_%08lX",
                    ParentBusNumber,
                    ParentAddress,
                    PdoExtension->Cid.ProductSerialNumber);
            }
            else
            {
                DPRINT1("SdBusPdoQueryId: missing parent location (bus 0x%08lx, addr 0x%08lx), using fallback instance ID\n",
                        ParentBusNumber, ParentAddress);

                Status = RtlStringCchPrintfW(
                    TempBuffer,
                    RTL_NUMBER_OF(TempBuffer),
                    L"%08lX_%04lX",
                    PdoExtension->Cid.ProductSerialNumber,
                    PdoExtension->RelativeAddress & 0xFFFF);
            }

            if (!NT_SUCCESS(Status))
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }
            break;
        }

        case BusQueryCompatibleIDs:
        {
            PCWSTR CompatId;

            if (PdoExtension->IsEmmcPartition)
            {
                Status = SdBusFormatEmmcPartitionCompatibleIds(PdoExtension,
                                                               TempBuffer,
                                                               RTL_NUMBER_OF(TempBuffer),
                                                               &Length);
                if (!NT_SUCCESS(Status))
                {
                    Irp->IoStatus.Status = Status;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return Status;
                }

                Buffer = (PWCHAR)ExAllocatePoolWithTag(PagedPool,
                                                        Length * sizeof(WCHAR),
                                                        TAG_SDBUS);
                if (Buffer == NULL)
                {
                    Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }

                RtlCopyMemory(Buffer, TempBuffer, Length * sizeof(WCHAR));
                Irp->IoStatus.Information = (ULONG_PTR)Buffer;
                Irp->IoStatus.Status = STATUS_SUCCESS;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_SUCCESS;
            }

            if (IsSdioFunction)
            {
                Status = SdBusFormatSdioCompatibleIds(PdoExtension,
                                                     TempBuffer,
                                                     RTL_NUMBER_OF(TempBuffer),
                                                     &Length);
                if (!NT_SUCCESS(Status))
                {
                    Irp->IoStatus.Status = Status;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return Status;
                }

                Buffer = (PWCHAR)ExAllocatePoolWithTag(PagedPool,
                                                        Length * sizeof(WCHAR),
                                                        TAG_SDBUS);
                if (Buffer == NULL)
                {
                    Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }

                RtlCopyMemory(Buffer, TempBuffer, Length * sizeof(WCHAR));
                Irp->IoStatus.Information = (ULONG_PTR)Buffer;
                Irp->IoStatus.Status = STATUS_SUCCESS;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_SUCCESS;
            }

            switch (PdoExtension->CardType)
            {
                case SdCardTypeSdV1:
                case SdCardTypeSdV2:
                case SdCardTypeSdhc:
                case SdCardTypeSdxc:
                    CompatId = L"SD\\CLASS_MEMORY";
                    break;
                case SdCardTypeSdio:
                    CompatId = L"SD\\CLASS_SDIO";
                    break;
                case SdCardTypeCombo:
                    CompatId = L"SD\\CLASS_COMBO";
                    break;
                case SdCardTypeMmc:
                    CompatId = L"SD\\CLASS_MMC";
                    break;
                case SdCardTypeEmmc:
                    CompatId = L"SD\\CLASS_EMMC";
                    break;
                default:
                    CompatId = L"SD\\CLASS_UNKNOWN";
                    break;
            }

            Length = (ULONG)wcslen(CompatId) + 2; /* double null */

            Buffer = (PWCHAR)ExAllocatePoolWithTag(PagedPool,
                                                    Length * sizeof(WCHAR),
                                                    TAG_SDBUS);
            if (Buffer == NULL)
            {
                Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            RtlZeroMemory(Buffer, Length * sizeof(WCHAR));
            RtlCopyMemory(Buffer, CompatId, wcslen(CompatId) * sizeof(WCHAR));

            Irp->IoStatus.Information = (ULONG_PTR)Buffer;
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;
        }

        default:
            Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_NOT_SUPPORTED;
    }

    /* Allocate and copy the single string result (DeviceID / InstanceID) */
    Length = (ULONG)wcslen(TempBuffer) + 1;

    Buffer = (PWCHAR)ExAllocatePoolWithTag(PagedPool,
                                            Length * sizeof(WCHAR),
                                            TAG_SDBUS);
    if (Buffer == NULL)
    {
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(Buffer, TempBuffer, Length * sizeof(WCHAR));

    Irp->IoStatus.Information = (ULONG_PTR)Buffer;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/**
 * @brief Handle IRP_MN_QUERY_CAPABILITIES for a child SD card PDO.
 *
 * Fills in the DEVICE_CAPABILITIES structure with removability flags,
 * power state mappings, and other properties appropriate for the card type
 * (eMMC is non-removable; SD cards are removable and ejectable).
 *
 * @param[in]     PdoExtension  Pointer to the PDO device extension.
 * @param[in,out] Irp           Pointer to the IRP_MN_QUERY_CAPABILITIES IRP.
 *
 * @return STATUS_SUCCESS on success, or STATUS_INVALID_PARAMETER.
 */
static NTSTATUS
SdBusPdoQueryCapabilities(
    _In_ PPDO_EXTENSION PdoExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PDEVICE_CAPABILITIES Caps;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    Caps = IoStack->Parameters.DeviceCapabilities.Capabilities;

    if (Caps == NULL || Caps->Size < sizeof(DEVICE_CAPABILITIES))
    {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER;
    }

    Caps->Version = 1;

    /* eMMC is soldered on, not removable; SD cards are removable */
    if (PdoExtension->CardType == SdCardTypeEmmc)
    {
        Caps->Removable = FALSE;
        Caps->EjectSupported = FALSE;
    }
    else
    {
        Caps->Removable = TRUE;
        Caps->EjectSupported = TRUE;
    }

    Caps->SurpriseRemovalOK = TRUE;
    Caps->UniqueID = TRUE;
    Caps->Address = 0; /* Slot 0 */
    Caps->UINumber = 0;

    Caps->LockSupported = FALSE;
    Caps->DockDevice = FALSE;
    Caps->SilentInstall = TRUE;
    Caps->RawDeviceOK = FALSE;
    Caps->HardwareDisabled = FALSE;

    /* Power state mappings */
    Caps->DeviceState[PowerSystemWorking] = PowerDeviceD0;
    Caps->DeviceState[PowerSystemSleeping1] = PowerDeviceD3;
    Caps->DeviceState[PowerSystemSleeping2] = PowerDeviceD3;
    Caps->DeviceState[PowerSystemSleeping3] = PowerDeviceD3;
    Caps->DeviceState[PowerSystemHibernate] = PowerDeviceD3;
    Caps->DeviceState[PowerSystemShutdown] = PowerDeviceD3;
    Caps->SystemWake = PowerSystemUnspecified;
    Caps->DeviceWake = PowerDeviceUnspecified;

    Caps->D1Latency = 0;
    Caps->D2Latency = 0;
    Caps->D3Latency = 0;

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/**
 * @brief Handle IRP_MN_QUERY_DEVICE_TEXT for a child SD card PDO.
 *
 * Returns a human-readable description string based on the card type
 * (e.g. "SDHC Memory Card", "eMMC Storage Device").
 *
 * @param[in]     PdoExtension  Pointer to the PDO device extension.
 * @param[in,out] Irp           Pointer to the IRP_MN_QUERY_DEVICE_TEXT IRP.
 *
 * @return STATUS_SUCCESS, STATUS_NOT_SUPPORTED, or STATUS_INSUFFICIENT_RESOURCES.
 */
static NTSTATUS
SdBusPdoQueryDeviceText(
    _In_ PPDO_EXTENSION PdoExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PWCHAR Buffer;
    PCWSTR Description;
    ULONG Length;

    IoStack = IoGetCurrentIrpStackLocation(Irp);

    if (IoStack->Parameters.QueryDeviceText.DeviceTextType != DeviceTextDescription)
    {
        Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_NOT_SUPPORTED;
    }

    if (PdoExtension->IsEmmcPartition)
    {
        switch (PdoExtension->EmmcPartitionId)
        {
            case EMMC_PARTITION_BOOT0: Description = L"eMMC Boot Partition 0"; break;
            case EMMC_PARTITION_BOOT1: Description = L"eMMC Boot Partition 1"; break;
            case EMMC_PARTITION_RPMB:  Description = L"eMMC RPMB Partition";   break;
            case EMMC_PARTITION_GPP1:  Description = L"eMMC General Purpose Partition 1"; break;
            case EMMC_PARTITION_GPP2:  Description = L"eMMC General Purpose Partition 2"; break;
            case EMMC_PARTITION_GPP3:  Description = L"eMMC General Purpose Partition 3"; break;
            case EMMC_PARTITION_GPP4:  Description = L"eMMC General Purpose Partition 4"; break;
            default:                   Description = L"eMMC Partition";        break;
        }
    }
    else if (SdBusIsSdioFunctionPdo(PdoExtension))
    {
        Description = L"SDIO Function";
    }
    else switch (PdoExtension->CardType)
    {
        case SdCardTypeSdV1:
            Description = L"SD Memory Card (v1.x)";
            break;
        case SdCardTypeSdV2:
            Description = L"SD Memory Card (v2.0)";
            break;
        case SdCardTypeSdhc:
            Description = L"SDHC Memory Card";
            break;
        case SdCardTypeSdxc:
            Description = L"SDXC Memory Card";
            break;
        case SdCardTypeSdio:
            Description = L"SDIO Device";
            break;
        case SdCardTypeCombo:
            Description = L"SD Combo Card";
            break;
        case SdCardTypeMmc:
            Description = L"MMC Memory Card";
            break;
        case SdCardTypeEmmc:
            Description = L"eMMC Storage Device";
            break;
        default:
            Description = L"SD/MMC Device";
            break;
    }

    Length = (ULONG)(wcslen(Description) + 1) * sizeof(WCHAR);
    Buffer = (PWCHAR)ExAllocatePoolWithTag(PagedPool, Length, TAG_SDBUS);
    if (Buffer == NULL)
    {
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(Buffer, Description, Length);

    Irp->IoStatus.Information = (ULONG_PTR)Buffer;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/**
 * @brief Handle IRP_MN_QUERY_BUS_INFORMATION for a child SD card PDO.
 *
 * Returns a PNP_BUS_INFORMATION structure identifying the SD bus type GUID,
 * PNPBus legacy bus type, and bus number 0.
 *
 * @param[in]     PdoExtension  Pointer to the PDO device extension (unused).
 * @param[in,out] Irp           Pointer to the IRP_MN_QUERY_BUS_INFORMATION IRP.
 *
 * @return STATUS_SUCCESS on success, or STATUS_INSUFFICIENT_RESOURCES.
 */
static NTSTATUS
SdBusPdoQueryBusInformation(
    _In_ PPDO_EXTENSION PdoExtension,
    _Inout_ PIRP Irp)
{
    PPNP_BUS_INFORMATION BusInfo;

    UNREFERENCED_PARAMETER(PdoExtension);

    BusInfo = (PPNP_BUS_INFORMATION)ExAllocatePoolWithTag(
        PagedPool,
        sizeof(PNP_BUS_INFORMATION),
        TAG_SDBUS);

    if (BusInfo == NULL)
    {
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(&BusInfo->BusTypeGuid,
                   &GUID_SD_HOST_CONTROLLER_LOCAL,
                   sizeof(GUID));
    BusInfo->LegacyBusType = PNPBus;
    BusInfo->BusNumber = 0;

    Irp->IoStatus.Information = (ULONG_PTR)BusInfo;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/**
 * @brief Handle IRP_MN_QUERY_INTERFACE for a child SD card PDO.
 *
 * Checks whether the caller requests GUID_SDBUS_INTERFACE_STANDARD and,
 * if so, fills in the SDBUS_INTERFACE_STANDARD structure via
 * SdBusOpenInterfaceImpl. Requests for other GUIDs are rejected.
 *
 * @param[in]     PdoExtension  Pointer to the PDO device extension.
 * @param[in,out] Irp           Pointer to the IRP_MN_QUERY_INTERFACE IRP.
 *
 * @return STATUS_SUCCESS, STATUS_BUFFER_TOO_SMALL, or STATUS_NOT_SUPPORTED.
 */
static NTSTATUS
SdBusPdoQueryInterface(
    _In_ PPDO_EXTENSION PdoExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;

    IoStack = IoGetCurrentIrpStackLocation(Irp);

    /* Check if the caller is requesting GUID_SDBUS_INTERFACE_STANDARD */
    if (RtlCompareMemory(IoStack->Parameters.QueryInterface.InterfaceType,
                         &GUID_SDBUS_INTERFACE_STANDARD_LOCAL,
                         sizeof(GUID)) == sizeof(GUID))
    {
        if (IoStack->Parameters.QueryInterface.Size < sizeof(SDBUS_INTERFACE_STANDARD))
        {
            Irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_BUFFER_TOO_SMALL;
        }

        Status = SdBusOpenInterfaceImpl(
            PdoExtension,
            (PSDBUS_INTERFACE_STANDARD)IoStack->Parameters.QueryInterface.Interface,
            IoStack->Parameters.QueryInterface.Size,
            IoStack->Parameters.QueryInterface.Version);

        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    /* Not our GUID, leave the IRP untouched */
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_NOT_SUPPORTED;
}

/**
 * @brief Handle IRP_MN_QUERY_DEVICE_RELATIONS for TargetDeviceRelation.
 *
 * Allocates a DEVICE_RELATIONS structure containing a single reference
 * to the PDO itself, as required by the TargetDeviceRelation query.
 *
 * @param[in]     PdoExtension  Pointer to the PDO device extension.
 * @param[in,out] Irp           Pointer to the query-relations IRP.
 *
 * @return STATUS_SUCCESS on success, or STATUS_INSUFFICIENT_RESOURCES.
 */
static NTSTATUS
SdBusPdoQueryTargetRelation(
    _In_ PPDO_EXTENSION PdoExtension,
    _Inout_ PIRP Irp)
{
    PDEVICE_RELATIONS Relations;

    Relations = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(
        PagedPool,
        sizeof(DEVICE_RELATIONS),
        TAG_SDBUS);

    if (Relations == NULL)
    {
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Relations->Count = 1;
    Relations->Objects[0] = PdoExtension->Common.Self;
    ObReferenceObject(PdoExtension->Common.Self);

    Irp->IoStatus.Information = (ULONG_PTR)Relations;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/**
 * @brief Handle IRP_MN_START_DEVICE for a child SD card PDO.
 *
 * Marks the PDO as started. The PDO has no hardware resources of its own;
 * all hardware access goes through the parent FDO.
 *
 * @param[in]     PdoExtension  Pointer to the PDO device extension.
 * @param[in,out] Irp           Pointer to the IRP_MN_START_DEVICE IRP.
 *
 * @return STATUS_SUCCESS always.
 */
static NTSTATUS
SdBusPdoStartDevice(
    _In_ PPDO_EXTENSION PdoExtension,
    _Inout_ PIRP Irp)
{
    PdoExtension->Started = TRUE;
    PdoExtension->Common.DeviceState = SdBusDeviceStateStarted;

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/**
 * @brief Handle IRP_MN_STOP_DEVICE for a child SD card PDO.
 *
 * Marks the PDO as stopped.
 *
 * @param[in]     PdoExtension  Pointer to the PDO device extension.
 * @param[in,out] Irp           Pointer to the IRP_MN_STOP_DEVICE IRP.
 *
 * @return STATUS_SUCCESS always.
 */
static NTSTATUS
SdBusPdoStopDevice(
    _In_ PPDO_EXTENSION PdoExtension,
    _Inout_ PIRP Irp)
{
    PdoExtension->Started = FALSE;
    PdoExtension->Common.DeviceState = SdBusDeviceStateStopped;

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/**
 * @brief Handle IRP_MN_REMOVE_DEVICE for a child SD card PDO.
 *
 * Marks the PDO as removed, completes the IRP, and if the PDO has been
 * reported as missing to the PnP manager, removes it from the parent FDO's
 * child list and deletes the device object.
 *
 * @param[in]     PdoExtension  Pointer to the PDO device extension.
 * @param[in,out] Irp           Pointer to the IRP_MN_REMOVE_DEVICE IRP.
 *
 * @return STATUS_SUCCESS always.
 */
static NTSTATUS
SdBusPdoRemoveDevice(
    _In_ PPDO_EXTENSION PdoExtension,
    _Inout_ PIRP Irp)
{
    PFDO_EXTENSION FdoExtension = PdoExtension->FdoExtension;
    KIRQL OldIrql;

    PdoExtension->Common.DeviceState = SdBusDeviceStateRemoved;
    PdoExtension->Present = FALSE;
    PdoExtension->Started = FALSE;

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    /* If the PDO has been reported missing, remove it from the list and delete */
    if (PdoExtension->ReportedMissing)
    {
        /* Remove from the FDO's child list */
        KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
        RemoveEntryList(&PdoExtension->ListEntry);
        if (FdoExtension->ChildPdoCount > 0)
        {
            FdoExtension->ChildPdoCount--;
        }
        KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

        IoDeleteDevice(PdoExtension->Common.Self);
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Handle IRP_MN_SURPRISE_REMOVAL for a child SD card PDO.
 *
 * Marks the PDO state as surprise-removed and clears the present/started flags.
 *
 * @param[in]     PdoExtension  Pointer to the PDO device extension.
 * @param[in,out] Irp           Pointer to the IRP_MN_SURPRISE_REMOVAL IRP.
 *
 * @return STATUS_SUCCESS always.
 */
static NTSTATUS
SdBusPdoSurpriseRemoval(
    _In_ PPDO_EXTENSION PdoExtension,
    _Inout_ PIRP Irp)
{
    PdoExtension->Common.DeviceState = SdBusDeviceStateSurpriseRemoved;
    PdoExtension->Present = FALSE;
    PdoExtension->Started = FALSE;

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/**
 * @brief Handle IRP_MN_QUERY_RESOURCE_REQUIREMENTS for a child SD card PDO.
 *
 * Returns an empty resource list since the PDO has no hardware resources
 * of its own (all hardware is owned by the parent FDO).
 *
 * @param[in]     PdoExtension  Pointer to the PDO device extension (unused).
 * @param[in,out] Irp           Pointer to the query-resource-requirements IRP.
 *
 * @return STATUS_SUCCESS always.
 */
static NTSTATUS
SdBusPdoQueryResourceRequirements(
    _In_ PPDO_EXTENSION PdoExtension,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(PdoExtension);

    /* PDO has no hardware resources of its own */
    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS
SdBusPdoQueryPnpDeviceState(
    _In_ PPDO_EXTENSION PdoExtension,
    _Inout_ PIRP Irp)
{
    PNP_DEVICE_STATE State;

    State = (PNP_DEVICE_STATE)Irp->IoStatus.Information;

    if (!PdoExtension->Present || PdoExtension->EjectPending ||
        PdoExtension->Common.DeviceState == SdBusDeviceStateRemoved ||
        PdoExtension->Common.DeviceState == SdBusDeviceStateSurpriseRemoved)
    {
        State |= PNP_DEVICE_REMOVED;
    }


    Irp->IoStatus.Information = (ULONG_PTR)State;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS
SdBusPdoDeviceUsageNotification(
    _In_ PPDO_EXTENSION PdoExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    DEVICE_USAGE_NOTIFICATION_TYPE Type;
    BOOLEAN InPath;
    PULONG Counter;
    PDEVICE_OBJECT DeviceObject;
    BOOLEAN AdjustPagableFlag;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    Type = IoStack->Parameters.UsageNotification.Type;
    InPath = IoStack->Parameters.UsageNotification.InPath;
    DeviceObject = PdoExtension->Common.Self;
    AdjustPagableFlag = FALSE;

    switch (Type)
    {
        case DeviceUsageTypePaging:
            Counter = &PdoExtension->PagingPathCount;
            AdjustPagableFlag = TRUE;
            break;

        case DeviceUsageTypeHibernation:
            Counter = &PdoExtension->HibernationPathCount;
            break;

        case DeviceUsageTypeDumpFile:
            Counter = &PdoExtension->DumpPathCount;
            break;

        default:
            Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_NOT_SUPPORTED;
    }

    if (InPath)
    {
        ULONG Previous = (*Counter)++;

        if (AdjustPagableFlag && Previous == 0)
        {
            DeviceObject->Flags &= ~DO_POWER_PAGABLE;
        }
    }
    else
    {
        if (*Counter > 0)
        {
            (*Counter)--;
        }

        if (AdjustPagableFlag && *Counter == 0)
        {
            DeviceObject->Flags |= DO_POWER_PAGABLE;
        }
    }

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS
SdBusPdoEject(
    _In_ PPDO_EXTENSION PdoExtension,
    _Inout_ PIRP Irp)
{
    PFDO_EXTENSION FdoExtension = PdoExtension->FdoExtension;
    KIRQL OldIrql;

    KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
    PdoExtension->EjectPending = TRUE;
    PdoExtension->Present = FALSE;
    PdoExtension->ReportedMissing = TRUE;
    KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

    IoInvalidateDeviceRelations(FdoExtension->PhysicalDevice, BusRelations);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/**
 * @brief Top-level PnP dispatch for a child SD card PDO.
 *
 * Acquires the remove lock, dispatches to minor-function-specific handlers
 * (start, stop, remove, query ID, capabilities, device text, bus info,
 * interface, device relations), and completes unhandled IRPs in-place
 * since PDOs have no lower driver.
 *
 * @param[in]     DeviceObject  Pointer to the PDO device object.
 * @param[in,out] Irp           Pointer to the PnP IRP.
 *
 * @return NTSTATUS from the specific PnP handler.
 */
NTSTATUS
SdBusPdoPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PPDO_EXTENSION PdoExtension;
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;

    PdoExtension = (PPDO_EXTENSION)DeviceObject->DeviceExtension;
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    Status = IoAcquireRemoveLock(&PdoExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    switch (IoStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            DPRINT1("SdBusPdoPnp: IRP_MN_START_DEVICE\n");
            Status = SdBusPdoStartDevice(PdoExtension, Irp);
            break;

        case IRP_MN_STOP_DEVICE:
            DPRINT1("SdBusPdoPnp: IRP_MN_STOP_DEVICE\n");
            Status = SdBusPdoStopDevice(PdoExtension, Irp);
            break;

        case IRP_MN_REMOVE_DEVICE:
            DPRINT1("SdBusPdoPnp: IRP_MN_REMOVE_DEVICE\n");
            /* Use the waiting variant to drain all outstanding I/O */
            IoReleaseRemoveLockAndWait(&PdoExtension->RemoveLock, Irp);
            return SdBusPdoRemoveDevice(PdoExtension, Irp);

        case IRP_MN_SURPRISE_REMOVAL:
            DPRINT1("SdBusPdoPnp: IRP_MN_SURPRISE_REMOVAL\n");
            Status = SdBusPdoSurpriseRemoval(PdoExtension, Irp);
            break;

        case IRP_MN_QUERY_ID:
            DPRINT1("SdBusPdoPnp: IRP_MN_QUERY_ID (type %lu)\n",
                   IoStack->Parameters.QueryId.IdType);
            Status = SdBusPdoQueryId(PdoExtension, Irp);
            break;

        case IRP_MN_QUERY_CAPABILITIES:
            DPRINT1("SdBusPdoPnp: IRP_MN_QUERY_CAPABILITIES\n");
            Status = SdBusPdoQueryCapabilities(PdoExtension, Irp);
            break;

        case IRP_MN_QUERY_DEVICE_TEXT:
            DPRINT1("SdBusPdoPnp: IRP_MN_QUERY_DEVICE_TEXT\n");
            Status = SdBusPdoQueryDeviceText(PdoExtension, Irp);
            break;

        case IRP_MN_QUERY_BUS_INFORMATION:
            DPRINT1("SdBusPdoPnp: IRP_MN_QUERY_BUS_INFORMATION\n");
            Status = SdBusPdoQueryBusInformation(PdoExtension, Irp);
            break;

        case IRP_MN_QUERY_INTERFACE:
            DPRINT1("SdBusPdoPnp: IRP_MN_QUERY_INTERFACE\n");
            Status = SdBusPdoQueryInterface(PdoExtension, Irp);
            break;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (IoStack->Parameters.QueryDeviceRelations.Type == TargetDeviceRelation)
            {
                DPRINT1("SdBusPdoPnp: IRP_MN_QUERY_DEVICE_RELATIONS (TargetDevice)\n");
                Status = SdBusPdoQueryTargetRelation(PdoExtension, Irp);
            }
            else
            {
                Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                Status = STATUS_NOT_SUPPORTED;
            }
            break;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            Status = SdBusPdoQueryResourceRequirements(PdoExtension, Irp);
            break;

        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
            DPRINT1("SdBusPdoPnp: IRP_MN_FILTER_RESOURCE_REQUIREMENTS\n");
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_RESOURCES:
            /* No hardware resources */
            Irp->IoStatus.Information = 0;
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            DPRINT1("SdBusPdoPnp: IRP_MN_QUERY_PNP_DEVICE_STATE\n");
            Status = SdBusPdoQueryPnpDeviceState(PdoExtension, Irp);
            break;

        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
            DPRINT1("SdBusPdoPnp: IRP_MN_DEVICE_USAGE_NOTIFICATION (type %u, inpath %u)\n",
                    IoStack->Parameters.UsageNotification.Type,
                    IoStack->Parameters.UsageNotification.InPath);
            Status = SdBusPdoDeviceUsageNotification(PdoExtension, Irp);
            break;

        case IRP_MN_EJECT:
            DPRINT1("SdBusPdoPnp: IRP_MN_EJECT\n");
            Status = SdBusPdoEject(PdoExtension, Irp);
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            Status = STATUS_SUCCESS;
            break;

        default:
            Status = Irp->IoStatus.Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            break;
    }

    IoReleaseRemoveLock(&PdoExtension->RemoveLock, Irp);
    return Status;
}

/**
 * @brief Handle IRP_MJ_INTERNAL_DEVICE_CONTROL for a child SD card PDO.
 *
 *
 * @param[in]     DeviceObject  Pointer to the PDO device object.
 * @param[in,out] Irp           Pointer to the internal device control IRP.
 *
 */
NTSTATUS
SdBusPdoInternalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PPDO_EXTENSION PdoExtension;
    PFDO_EXTENSION FdoExtension;
    PIO_STACK_LOCATION IoStack;
    PSDBUS_REQUEST_PACKET RequestPacket;
    NTSTATUS Status;

    PdoExtension = (PPDO_EXTENSION)DeviceObject->DeviceExtension;
    FdoExtension = PdoExtension->FdoExtension;

    /* Acquire remove lock to prevent device removal during request processing */
    Status = IoAcquireRemoveLock(&PdoExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    IoStack = IoGetCurrentIrpStackLocation(Irp);

    /* The IOCTL code for SD requests is IOCTL_SD_SUBMIT_REQUEST,
     * but the Windows SD bus driver uses the Argument1 field of
     * Parameters.Others to pass the SDBUS_REQUEST_PACKET pointer. */
    RequestPacket = (PSDBUS_REQUEST_PACKET)IoStack->Parameters.Others.Argument1;
    if (RequestPacket == NULL)
    {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        IoReleaseRemoveLock(&PdoExtension->RemoveLock, Irp);
        return STATUS_INVALID_PARAMETER;
    }

    if (RequestPacket->RequestFunction == SDRF_GET_PROPERTY)
    {
        Status = SdBusHandleRequest(PdoExtension, RequestPacket);
        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = RequestPacket->Information;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        IoReleaseRemoveLock(&PdoExtension->RemoveLock, Irp);
        return Status;
    }

    if (!FdoExtension->WorkerStarted ||
        InterlockedCompareExchange(&FdoExtension->WorkerShutdown, 0, 0) != 0)
    {
        Status = SdBusHandleRequest(PdoExtension, RequestPacket);
        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = RequestPacket->Information;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        IoReleaseRemoveLock(&PdoExtension->RemoveLock, Irp);
        return Status;
    }

    IoMarkIrpPending(Irp);
    InterlockedIncrement(&FdoExtension->OutstandingRequestCount);
    Status = IoCsqInsertIrpEx(&FdoExtension->RequestCsq, Irp, NULL, NULL);
    if (!NT_SUCCESS(Status))
    {
        InterlockedDecrement(&FdoExtension->OutstandingRequestCount);

        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        IoReleaseRemoveLock(&PdoExtension->RemoveLock, Irp);
        return STATUS_PENDING;
    }
    return STATUS_PENDING;
}
