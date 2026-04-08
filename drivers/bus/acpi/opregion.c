#include "precomp.h"
#include <oprghdlr.h>

#define NDEBUG
#include <debug.h>

#define ACPI_OPRG_TAG 'rOcA'

typedef struct _ACPI_OPREGION_REGISTRATION
{
    ULONG Signature;
    PDEVICE_OBJECT DeviceObject;
    ULONG SpaceId;
    ULONG AccessType;
    ULONG Flags;
    PACPI_OP_REGION_HANDLER Handler;
    PVOID Context;
} ACPI_OPREGION_REGISTRATION, *PACPI_OPREGION_REGISTRATION;

static
ACPI_STATUS
NTAPI
AcpiOpRegionNtStatusToAcpiStatus(
    _In_ NTSTATUS Status)
{
    if (NT_SUCCESS(Status))
        return AE_OK;

    switch (Status)
    {
        case STATUS_INSUFFICIENT_RESOURCES:
            return AE_NO_MEMORY;

        case STATUS_OBJECT_NAME_NOT_FOUND:
            return AE_NOT_FOUND;

        case STATUS_INVALID_PARAMETER:
            return AE_BAD_PARAMETER;

        default:
            return AE_ERROR;
    }
}

static
NTSTATUS
NTAPI
AcpiOpRegionAcpiStatusToNtStatus(
    _In_ ACPI_STATUS Status)
{
    if (ACPI_SUCCESS(Status))
    {
        return STATUS_SUCCESS;
    }

    switch (Status)
    {
        case AE_ALREADY_EXISTS:
            return STATUS_OBJECT_NAME_EXISTS;

        case AE_NO_MEMORY:
            return STATUS_INSUFFICIENT_RESOURCES;

        case AE_NOT_FOUND:
        case AE_NOT_EXIST:
            return STATUS_OBJECT_NAME_NOT_FOUND;

        case AE_BAD_PARAMETER:
            return STATUS_INVALID_PARAMETER;

        default:
            return STATUS_UNSUCCESSFUL;
    }
}

static
PACPI_OPREGION_REGISTRATION
NTAPI
AcpiOpRegionValidateRegistration(
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID OperationRegionObject)
{
    PACPI_OPREGION_REGISTRATION Registration;

    Registration = (PACPI_OPREGION_REGISTRATION)OperationRegionObject;
    if (!Registration || Registration->Signature != ACPI_OPRG_TAG)
        return NULL;

    if (DeviceObject && Registration->DeviceObject != DeviceObject)
        return NULL;

    return Registration;
}

static
ACPI_STATUS
ACPI_SYSTEM_XFACE
AcpiOpRegionSetupThunk(
    _In_ ACPI_HANDLE RegionHandle,
    _In_ UINT32 Function,
    _In_ PVOID HandlerContext,
    _Outptr_result_maybenull_ PVOID *RegionContext)
{
    PACPI_OPREGION_REGISTRATION Registration;

    Registration = (PACPI_OPREGION_REGISTRATION)HandlerContext;
    if (!AcpiOpRegionValidateRegistration(NULL, Registration) || !RegionContext)
        return AE_BAD_PARAMETER;

    *RegionContext = (Function == ACPI_REGION_ACTIVATE) ? RegionHandle : NULL;
    return AE_OK;
}

static
ACPI_STATUS
ACPI_SYSTEM_XFACE
AcpiOpRegionHandlerThunk(
    _In_ UINT32 Function,
    _In_ ACPI_PHYSICAL_ADDRESS Address,
    _In_ UINT32 BitWidth,
    _Inout_ UINT64 *Value,
    _In_ PVOID HandlerContext,
    _In_opt_ PVOID RegionContext)
{
    PACPI_OPREGION_REGISTRATION Registration;
    ULONG AccessMode;
    ULONG TransferSize;
    ULONGLONG TransferValue;
    NTSTATUS Status;

    Registration = (PACPI_OPREGION_REGISTRATION)HandlerContext;
    if (!AcpiOpRegionValidateRegistration(NULL, Registration) || !Value)
        return AE_BAD_PARAMETER;

    if (Address > MAXULONG || BitWidth == 0 || BitWidth > sizeof(ULONGLONG) * 8)
        return AE_BAD_PARAMETER;

    TransferSize = (BitWidth + 7) / 8;
    TransferValue = *Value;
    AccessMode = (Function == ACPI_WRITE) ? ACPI_OPREGION_WRITE : ACPI_OPREGION_READ;

    Status = Registration->Handler(AccessMode,
                                   RegionContext,
                                   (ULONG)Address,
                                   TransferSize,
                                   (PULONG)&TransferValue,
                                   (ULONG_PTR)Registration->Context,
                                   NULL,
                                   NULL);
    *Value = TransferValue;
    return AcpiOpRegionNtStatusToAcpiStatus(Status);
}

static
NTSTATUS
NTAPI
AcpiOpRegionGetDeviceData(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PPDO_DEVICE_DATA *DeviceData)
{
    PCOMMON_DEVICE_DATA CommonData;

    if (!DeviceObject || !DeviceObject->DeviceExtension || !DeviceData)
        return STATUS_INVALID_PARAMETER;

    CommonData = (PCOMMON_DEVICE_DATA)DeviceObject->DeviceExtension;
    if (CommonData->IsFDO)
        return STATUS_INVALID_DEVICE_REQUEST;

    *DeviceData = (PPDO_DEVICE_DATA)CommonData;
    if (!(*DeviceData)->AcpiHandle)
        return STATUS_INVALID_DEVICE_STATE;

    return STATUS_SUCCESS;
}

NTSTATUS
RegisterOpRegionHandler(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG AccessType,
    _In_ ULONG RegionSpace,
    _In_ PACPI_OP_REGION_HANDLER Handler,
    _In_ PVOID Context,
    _In_ ULONG Flags,
    _Inout_ PVOID *OperationRegionObject)
{
    PPDO_DEVICE_DATA DeviceData;
    PACPI_OPREGION_REGISTRATION Registration = NULL;
    ACPI_STATUS AcpiStatus;
    NTSTATUS Status;
    UINT8 SpaceId;

    if (!Handler)
        return STATUS_INVALID_PARAMETER;

    Status = AcpiOpRegionGetDeviceData(DeviceObject, &DeviceData);
    if (!NT_SUCCESS(Status))
        return Status;

    switch (RegionSpace)
    {
        case ACPI_OPREGION_REGION_SPACE_MEMORY:
            SpaceId = ACPI_ADR_SPACE_SYSTEM_MEMORY;
            break;

        case ACPI_OPREGION_REGION_SPACE_IO:
            SpaceId = ACPI_ADR_SPACE_SYSTEM_IO;
            break;

        case ACPI_OPREGION_REGION_SPACE_PCI_CONFIG:
            SpaceId = ACPI_ADR_SPACE_PCI_CONFIG;
            break;

        case ACPI_OPREGION_REGION_SPACE_EC:
            SpaceId = ACPI_ADR_SPACE_EC;
            break;

        case ACPI_OPREGION_REGION_SPACE_SMB:
            SpaceId = ACPI_ADR_SPACE_SMBUS;
            break;

        default:
            return STATUS_INVALID_PARAMETER;
    }

    if (AccessType != ACPI_OPREGION_ACCESS_AS_RAW &&
        AccessType != ACPI_OPREGION_ACCESS_AS_COOKED)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Registration = ExAllocatePoolWithTag(NonPagedPool,
                                         sizeof(*Registration),
                                         ACPI_OPRG_TAG);
    if (!Registration)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Registration, sizeof(*Registration));
    Registration->Signature = ACPI_OPRG_TAG;
    Registration->DeviceObject = DeviceObject;
    Registration->SpaceId = SpaceId;
    Registration->AccessType = AccessType;
    Registration->Flags = Flags;
    Registration->Handler = Handler;
    Registration->Context = Context;

    AcpiStatus = AcpiInstallAddressSpaceHandler(DeviceData->AcpiHandle,
                                                SpaceId,
                                                AcpiOpRegionHandlerThunk,
                                                AcpiOpRegionSetupThunk,
                                                Registration);
    Status = AcpiOpRegionAcpiStatusToNtStatus(AcpiStatus);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Registration, ACPI_OPRG_TAG);
        return Status;
    }

    if (OperationRegionObject)
        *OperationRegionObject = Registration;

    return STATUS_SUCCESS;
}

NTSTATUS
DeRegisterOpRegionHandler(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PVOID OperationRegionObject)
{
    PPDO_DEVICE_DATA DeviceData;
    PACPI_OPREGION_REGISTRATION Registration;
    ACPI_STATUS AcpiStatus;
    NTSTATUS Status;

    Status = AcpiOpRegionGetDeviceData(DeviceObject, &DeviceData);
    if (!NT_SUCCESS(Status))
        return Status;

    Registration = AcpiOpRegionValidateRegistration(DeviceObject, OperationRegionObject);
    if (!Registration)
        return STATUS_INVALID_PARAMETER;

    AcpiStatus = AcpiRemoveAddressSpaceHandler(DeviceData->AcpiHandle,
                                               (UINT8)Registration->SpaceId,
                                               AcpiOpRegionHandlerThunk);
    Status = AcpiOpRegionAcpiStatusToNtStatus(AcpiStatus);
    if (NT_SUCCESS(Status))
    {
        Registration->Signature = 0;
        ExFreePoolWithTag(Registration, ACPI_OPRG_TAG);
    }

    return Status;
}
