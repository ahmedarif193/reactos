/*
 * PROJECT:         ReactOS Operating System
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         I/O manager DMA IOMMU interface dispatch
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>

#define NDEBUG
#include <debug.h>

#ifdef _WIN64
C_ASSERT(DMA_IOMMU_INTERFACE_EX_V1_SIZE == 0x78);
C_ASSERT(DMA_IOMMU_INTERFACE_EX_V2_SIZE == 0xC0);
C_ASSERT(DMA_IOMMU_INTERFACE_EX_V3_SIZE == 0xE8);
#endif

/* GLOBALS *******************************************************************/

static const IOP_IOMMU_PROVIDER *volatile IopIommuProvider;

/* PRIVATE FUNCTIONS *********************************************************/

static
const IOP_IOMMU_PROVIDER *
IopGetIommuProvider(VOID)
{
    return InterlockedCompareExchangePointer((PVOID volatile *)&IopIommuProvider,
                                             NULL,
                                             NULL);
}

static
BOOLEAN
IopIommuProviderIsComplete(
    _In_ const IOP_IOMMU_PROVIDER *Provider)
{
    const PVOID *Routines;
    ULONG Index;

    if (Provider == NULL)
        return FALSE;

    Routines = (const PVOID *)&Provider->Legacy;
    for (Index = 0; Index < sizeof(Provider->Legacy) / sizeof(PVOID); Index++)
    {
        if (Routines[Index] == NULL)
            return FALSE;
    }

    Routines = (const PVOID *)&Provider->Extended;
    for (Index = 0; Index < sizeof(Provider->Extended) / sizeof(PVOID); Index++)
    {
        if (Routines[Index] == NULL)
            return FALSE;
    }

    return TRUE;
}

static
NTSTATUS
NTAPI
IopIommuCreateDomain(
    _In_ BOOLEAN OsManagedPageTable,
    _Out_ PIOMMU_DMA_DOMAIN *DomainOut)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Legacy.CreateDomain(OsManagedPageTable, DomainOut);
    if (DomainOut == NULL)
        return STATUS_INVALID_PARAMETER_2;
    if (OsManagedPageTable)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
IopIommuCreateDomainEx(
    _In_ IOMMU_DMA_DOMAIN_TYPE DomainType,
    _In_ IOMMU_DMA_DOMAIN_CREATION_FLAGS Flags,
    _In_opt_ PIOMMU_DMA_LOGICAL_ALLOCATOR_CONFIG LogicalAllocatorConfig,
    _In_opt_ PIOMMU_DMA_RESERVED_REGION ReservedRegions,
    _Out_ PIOMMU_DMA_DOMAIN *DomainOut)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
    {
        return Provider->Extended.CreateDomainEx(DomainType,
                                                  Flags,
                                                  LogicalAllocatorConfig,
                                                  ReservedRegions,
                                                  DomainOut);
    }
    if (DomainType >= DomainTypeMax)
        return STATUS_INVALID_PARAMETER_1;
    if (Flags.AsUlonglong != 0)
        return STATUS_INVALID_PARAMETER_2;
    if (DomainOut == NULL)
        return STATUS_INVALID_PARAMETER_5;
    if ((DomainType == DomainTypeTranslate) ||
        (DomainType == DomainTypeTranslateS1))
    {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
IopIommuDeleteDomainLegacy(
    _In_ PIOMMU_DMA_DOMAIN Domain)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Legacy.DeleteDomain(Domain);
    return Domain != NULL ? STATUS_NOT_SUPPORTED : STATUS_INVALID_PARAMETER_1;
}

static
NTSTATUS
NTAPI
IopIommuDeleteDomain(
    _In_ PIOMMU_DMA_DOMAIN Domain)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Extended.DeleteDomain(Domain);
    return Domain != NULL ? STATUS_NOT_SUPPORTED : STATUS_INVALID_PARAMETER_1;
}

static
NTSTATUS
NTAPI
IopIommuAttachDevice(
    _In_ PIOMMU_DMA_DOMAIN Domain,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ ULONG InputMappingIdBase,
    _In_ ULONG MappingCount)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
    {
        return Provider->Legacy.AttachDevice(Domain,
                                              PhysicalDeviceObject,
                                              InputMappingIdBase,
                                              MappingCount);
    }
    if (Domain == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if (PhysicalDeviceObject == NULL)
        return STATUS_INVALID_PARAMETER_2;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
IopIommuDetachDevice(
    _In_ PIOMMU_DMA_DOMAIN Domain,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ ULONG InputMappingId)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
    {
        return Provider->Legacy.DetachDevice(Domain,
                                              PhysicalDeviceObject,
                                              InputMappingId);
    }
    if (Domain == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if (PhysicalDeviceObject == NULL)
        return STATUS_INVALID_PARAMETER_2;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
IopIommuAttachDeviceEx(
    _In_ PIOMMU_DMA_DOMAIN Domain,
    _In_ PIOMMU_DMA_DEVICE DmaDevice)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Extended.AttachDeviceEx(Domain, DmaDevice);
    if (Domain == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if (DmaDevice == NULL)
        return STATUS_INVALID_PARAMETER_2;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
IopIommuDetachDeviceEx(
    _In_ PIOMMU_DMA_DEVICE DmaDevice)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Extended.DetachDeviceEx(DmaDevice);
    return DmaDevice != NULL ? STATUS_NOT_SUPPORTED : STATUS_INVALID_PARAMETER_1;
}

static
NTSTATUS
NTAPI
IopIommuFlushDomainLegacy(
    _In_ PIOMMU_DMA_DOMAIN Domain)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Legacy.FlushDomain(Domain);
    return Domain != NULL ? STATUS_NOT_SUPPORTED : STATUS_INVALID_PARAMETER_1;
}

static
NTSTATUS
NTAPI
IopIommuFlushDomain(
    _In_ PIOMMU_DMA_DOMAIN Domain)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Extended.FlushDomain(Domain);
    return Domain != NULL ? STATUS_NOT_SUPPORTED : STATUS_INVALID_PARAMETER_1;
}

static
NTSTATUS
NTAPI
IopIommuFlushDomainByVaListLegacy(
    _In_ PIOMMU_DMA_DOMAIN Domain,
    _In_ BOOLEAN LastLevel,
    _In_ ULONG Number,
    _In_ PVOID VaList)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Legacy.FlushDomainByVaList(Domain, LastLevel, Number, VaList);
    if (Domain == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if ((Number != 0) && (VaList == NULL))
        return STATUS_INVALID_PARAMETER_4;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
IopIommuFlushDomainByVaList(
    _In_ PIOMMU_DMA_DOMAIN Domain,
    _In_ BOOLEAN LastLevel,
    _In_ ULONG Number,
    _In_ PVOID VaList)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Extended.FlushDomainByVaList(Domain, LastLevel, Number, VaList);
    if (Domain == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if ((Number != 0) && (VaList == NULL))
        return STATUS_INVALID_PARAMETER_4;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
IopIommuQueryInputMappingsLegacy(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Inout_ PINPUT_MAPPING_ELEMENT Buffer,
    _In_ ULONG BufferLength,
    _Out_opt_ PULONG ReturnLength)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
    {
        return Provider->Legacy.QueryInputMappings(PhysicalDeviceObject,
                                                    Buffer,
                                                    BufferLength,
                                                    ReturnLength);
    }
    if (ReturnLength != NULL)
        *ReturnLength = 0;
    if (PhysicalDeviceObject == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if ((BufferLength != 0) && (Buffer == NULL))
        return STATUS_INVALID_PARAMETER_2;
    return STATUS_NOT_FOUND;
}

static
NTSTATUS
NTAPI
IopIommuQueryInputMappings(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Inout_ PINPUT_MAPPING_ELEMENT Buffer,
    _In_ ULONG BufferLength,
    _Out_opt_ PULONG ReturnLength)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
    {
        return Provider->Extended.QueryInputMappings(PhysicalDeviceObject,
                                                      Buffer,
                                                      BufferLength,
                                                      ReturnLength);
    }
    if (ReturnLength != NULL)
        *ReturnLength = 0;
    if (PhysicalDeviceObject == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if ((BufferLength != 0) && (Buffer == NULL))
        return STATUS_INVALID_PARAMETER_2;
    return STATUS_NOT_FOUND;
}

static
NTSTATUS
NTAPI
IopIommuMapLogicalRange(
    _In_ PIOMMU_DMA_DOMAIN Domain,
    _In_ ULONG Permissions,
    _In_ PMDL Mdl,
    _In_ ULONGLONG LogicalAddress)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Legacy.MapLogicalRange(Domain, Permissions, Mdl, LogicalAddress);
    if (Domain == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if (Mdl == NULL)
        return STATUS_INVALID_PARAMETER_3;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
IopIommuMapLogicalRangeEx(
    _In_ PIOMMU_DMA_DOMAIN Domain,
    _In_ ULONG Permissions,
    _In_ PIOMMU_MAP_PHYSICAL_ADDRESS PhysicalAddressToMap,
    _In_opt_ PIOMMU_DMA_LOGICAL_ADDRESS ExplicitLogicalAddress,
    _In_opt_ PIOMMU_DMA_LOGICAL_ADDRESS MinLogicalAddress,
    _In_opt_ PIOMMU_DMA_LOGICAL_ADDRESS MaxLogicalAddress,
    _Out_ PIOMMU_DMA_LOGICAL_ADDRESS LogicalAddressOut)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
    {
        return Provider->Extended.MapLogicalRangeEx(Domain,
                                                     Permissions,
                                                     PhysicalAddressToMap,
                                                     ExplicitLogicalAddress,
                                                     MinLogicalAddress,
                                                     MaxLogicalAddress,
                                                     LogicalAddressOut);
    }
    if (Domain == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if (PhysicalAddressToMap == NULL)
        return STATUS_INVALID_PARAMETER_3;
    if (LogicalAddressOut == NULL)
        return STATUS_INVALID_PARAMETER_7;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
IopIommuUnmapLogicalRangeLegacy(
    _In_ PIOMMU_DMA_DOMAIN Domain,
    _In_ ULONGLONG LogicalAddress,
    _In_ ULONGLONG NumberOfPages)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Legacy.UnmapLogicalRange(Domain, LogicalAddress, NumberOfPages);
    return Domain != NULL ? STATUS_NOT_SUPPORTED : STATUS_INVALID_PARAMETER_1;
}

static
NTSTATUS
NTAPI
IopIommuUnmapLogicalRange(
    _In_ PIOMMU_DMA_DOMAIN Domain,
    _In_ ULONGLONG LogicalAddress,
    _In_ ULONGLONG NumberOfPages)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Extended.UnmapLogicalRange(Domain, LogicalAddress, NumberOfPages);
    return Domain != NULL ? STATUS_NOT_SUPPORTED : STATUS_INVALID_PARAMETER_1;
}

static
NTSTATUS
NTAPI
IopIommuMapIdentityRange(
    _In_ PIOMMU_DMA_DOMAIN Domain,
    _In_ ULONG Permissions,
    _In_ PMDL Mdl)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Legacy.MapIdentityRange(Domain, Permissions, Mdl);
    if (Domain == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if (Mdl == NULL)
        return STATUS_INVALID_PARAMETER_3;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
IopIommuMapIdentityRangeEx(
    _In_ PIOMMU_DMA_DOMAIN Domain,
    _In_ ULONG Permissions,
    _In_ PIOMMU_MAP_PHYSICAL_ADDRESS PhysicalAddressToMap)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Extended.MapIdentityRangeEx(Domain, Permissions, PhysicalAddressToMap);
    if (Domain == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if (PhysicalAddressToMap == NULL)
        return STATUS_INVALID_PARAMETER_3;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
IopIommuUnmapIdentityRange(
    _In_ PIOMMU_DMA_DOMAIN Domain,
    _In_ PMDL Mdl)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Legacy.UnmapIdentityRange(Domain, Mdl);
    if (Domain == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if (Mdl == NULL)
        return STATUS_INVALID_PARAMETER_2;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
IopIommuUnmapIdentityRangeEx(
    _In_ PIOMMU_DMA_DOMAIN Domain,
    _In_ PIOMMU_MAP_PHYSICAL_ADDRESS MappedPhysicalAddress)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Extended.UnmapIdentityRangeEx(Domain, MappedPhysicalAddress);
    if (Domain == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if (MappedPhysicalAddress == NULL)
        return STATUS_INVALID_PARAMETER_2;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
IopIommuSetDeviceFaultReporting(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ ULONG InputMappingIdBase,
    _In_ BOOLEAN Enable,
    _In_opt_ PDEVICE_FAULT_CONFIGURATION FaultConfig)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
    {
        return Provider->Legacy.SetDeviceFaultReporting(PhysicalDeviceObject,
                                                         InputMappingIdBase,
                                                         Enable,
                                                         FaultConfig);
    }
    return PhysicalDeviceObject != NULL ? STATUS_NOT_FOUND :
                                          STATUS_INVALID_PARAMETER_1;
}

static
NTSTATUS
NTAPI
IopIommuSetDeviceFaultReportingEx(
    _In_ PIOMMU_DMA_DEVICE DmaDevice,
    _In_ ULONG InputMappingIdBase,
    _In_ BOOLEAN Enable,
    _In_opt_ PDEVICE_FAULT_CONFIGURATION FaultConfig)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
    {
        return Provider->Extended.SetDeviceFaultReportingEx(DmaDevice,
                                                             InputMappingIdBase,
                                                             Enable,
                                                             FaultConfig);
    }
    return DmaDevice != NULL ? STATUS_NOT_SUPPORTED : STATUS_INVALID_PARAMETER_1;
}

static
NTSTATUS
NTAPI
IopIommuConfigureDomainLegacy(
    _In_ PIOMMU_DMA_DOMAIN Domain,
    _In_ PDOMAIN_CONFIGURATION Configuration)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Legacy.ConfigureDomain(Domain, Configuration);
    if (Domain == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if (Configuration == NULL)
        return STATUS_INVALID_PARAMETER_2;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
IopIommuConfigureDomain(
    _In_ PIOMMU_DMA_DOMAIN Domain,
    _In_ PDOMAIN_CONFIGURATION Configuration)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Extended.ConfigureDomain(Domain, Configuration);
    if (Domain == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if (Configuration == NULL)
        return STATUS_INVALID_PARAMETER_2;
    return STATUS_NOT_SUPPORTED;
}

static
VOID
NTAPI
IopIommuQueryAvailableDomainTypes(
    _In_ PIOMMU_DMA_DEVICE DmaDevice,
    _Out_ PULONG AvailableDomains)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
    {
        Provider->Extended.QueryAvailableDomainTypes(DmaDevice, AvailableDomains);
        return;
    }
    if (AvailableDomains != NULL)
        *AvailableDomains = 0;
}

static
NTSTATUS
NTAPI
IopIommuRegisterStateChangeCallback(
    _In_ PIOMMU_INTERFACE_STATE_CHANGE_CALLBACK StateChangeCallback,
    _In_opt_ PVOID Context,
    _In_ PIOMMU_DMA_DEVICE DmaDevice,
    _In_ PIOMMU_INTERFACE_STATE_CHANGE_FIELDS StateFields)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
    {
        return Provider->Extended.RegisterInterfaceStateChangeCallback(StateChangeCallback,
                                                                        Context,
                                                                        DmaDevice,
                                                                        StateFields);
    }
    if (StateChangeCallback == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if (DmaDevice == NULL)
        return STATUS_INVALID_PARAMETER_3;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
IopIommuUnregisterStateChangeCallback(
    _In_ PIOMMU_INTERFACE_STATE_CHANGE_CALLBACK StateChangeCallback,
    _In_ PIOMMU_DMA_DEVICE DmaDevice)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
    {
        return Provider->Extended.UnregisterInterfaceStateChangeCallback(StateChangeCallback,
                                                                          DmaDevice);
    }
    if (StateChangeCallback == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if (DmaDevice == NULL)
        return STATUS_INVALID_PARAMETER_2;
    return STATUS_NOT_FOUND;
}

static
NTSTATUS
NTAPI
IopIommuReserveLogicalAddressRange(
    _In_ PIOMMU_DMA_DOMAIN Domain,
    _In_ SIZE_T Size,
    _In_opt_ PIOMMU_DMA_LOGICAL_ADDRESS ExplicitLogicalAddress,
    _In_opt_ PIOMMU_DMA_LOGICAL_ADDRESS MinLogicalAddress,
    _In_opt_ PIOMMU_DMA_LOGICAL_ADDRESS MaxLogicalAddress,
    _Out_ PIOMMU_DMA_LOGICAL_ADDRESS_TOKEN *LogicalAddressToken)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
    {
        return Provider->Extended.ReserveLogicalAddressRange(Domain,
                                                              Size,
                                                              ExplicitLogicalAddress,
                                                              MinLogicalAddress,
                                                              MaxLogicalAddress,
                                                              LogicalAddressToken);
    }
    if (Domain == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if (LogicalAddressToken == NULL)
        return STATUS_INVALID_PARAMETER_6;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
IopIommuFreeReservedLogicalAddressRange(
    _In_ PIOMMU_DMA_LOGICAL_ADDRESS_TOKEN LogicalAddressToken)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Extended.FreeReservedLogicalAddressRange(LogicalAddressToken);
    return LogicalAddressToken != NULL ? STATUS_NOT_SUPPORTED :
                                         STATUS_INVALID_PARAMETER_1;
}

static
NTSTATUS
NTAPI
IopIommuMapReservedLogicalRange(
    _Inout_ PIOMMU_DMA_LOGICAL_ADDRESS_TOKEN LogicalAddressToken,
    _In_ SIZE_T Offset,
    _In_ ULONG Permissions,
    _In_ PIOMMU_MAP_PHYSICAL_ADDRESS PhysicalAddressToMap,
    _Out_ PIOMMU_DMA_LOGICAL_ADDRESS_TOKEN_MAPPED_SEGMENT MappedSegment)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
    {
        return Provider->Extended.MapReservedLogicalRange(LogicalAddressToken,
                                                           Offset,
                                                           Permissions,
                                                           PhysicalAddressToMap,
                                                           MappedSegment);
    }
    if (LogicalAddressToken == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if (PhysicalAddressToMap == NULL)
        return STATUS_INVALID_PARAMETER_4;
    if (MappedSegment == NULL)
        return STATUS_INVALID_PARAMETER_5;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
IopIommuUnmapReservedLogicalRange(
    _Inout_ PIOMMU_DMA_LOGICAL_ADDRESS_TOKEN_MAPPED_SEGMENT MappedSegment)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Extended.UnmapReservedLogicalRange(MappedSegment);
    return MappedSegment != NULL ? STATUS_NOT_SUPPORTED :
                                   STATUS_INVALID_PARAMETER_1;
}

static
NTSTATUS
NTAPI
IopIommuCreateDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PIOMMU_DEVICE_CREATION_CONFIGURATION DeviceConfig,
    _Out_ PIOMMU_DMA_DEVICE *DmaDeviceOut)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Extended.CreateDevice(DeviceObject, DeviceConfig, DmaDeviceOut);
    if (DeviceObject == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if (DmaDeviceOut == NULL)
        return STATUS_INVALID_PARAMETER_3;
    return STATUS_NOT_FOUND;
}

static
NTSTATUS
NTAPI
IopIommuDeleteDevice(
    _In_ PIOMMU_DMA_DEVICE DmaDevice)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Extended.DeleteDevice(DmaDevice);
    return DmaDevice != NULL ? STATUS_NOT_SUPPORTED : STATUS_INVALID_PARAMETER_1;
}

static
NTSTATUS
NTAPI
IopIommuCreatePasidDevice(
    _In_ PIOMMU_DMA_DEVICE DmaDevice,
    _Out_ PIOMMU_DMA_PASID_DEVICE *PasidDeviceOut,
    _Out_ PULONG AsidOut)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Extended.CreatePasidDevice(DmaDevice, PasidDeviceOut, AsidOut);
    if (DmaDevice == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if (PasidDeviceOut == NULL)
        return STATUS_INVALID_PARAMETER_2;
    if (AsidOut == NULL)
        return STATUS_INVALID_PARAMETER_3;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
IopIommuDeletePasidDevice(
    _In_ PIOMMU_DMA_PASID_DEVICE PasidDevice)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Extended.DeletePasidDevice(PasidDevice);
    return PasidDevice != NULL ? STATUS_NOT_SUPPORTED :
                                 STATUS_INVALID_PARAMETER_1;
}

static
NTSTATUS
NTAPI
IopIommuAttachPasidDevice(
    _In_ PIOMMU_DMA_DOMAIN Domain,
    _In_ PIOMMU_DMA_PASID_DEVICE PasidDevice)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Extended.AttachPasidDevice(Domain, PasidDevice);
    if (Domain == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if (PasidDevice == NULL)
        return STATUS_INVALID_PARAMETER_2;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
IopIommuDetachPasidDevice(
    _In_ PIOMMU_DMA_PASID_DEVICE PasidDevice)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Extended.DetachPasidDevice(PasidDevice);
    return PasidDevice != NULL ? STATUS_NOT_SUPPORTED :
                                 STATUS_INVALID_PARAMETER_1;
}

static
NTSTATUS
NTAPI
IopIommuQueryDeviceInfo(
    _In_ PIOMMU_DMA_DEVICE DmaDevice,
    _In_ ULONG Size,
    _Out_ PULONG BytesWritten,
    _Out_writes_to_(Size, *BytesWritten) PIOMMU_DMA_DEVICE_INFORMATION Buffer)
{
    const IOP_IOMMU_PROVIDER *Provider = IopGetIommuProvider();

    if (Provider != NULL)
        return Provider->Extended.QueryDeviceInfo(DmaDevice, Size, BytesWritten, Buffer);
    if (BytesWritten != NULL)
        *BytesWritten = 0;
    if (DmaDevice == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if (BytesWritten == NULL)
        return STATUS_INVALID_PARAMETER_3;
    if ((Size != 0) && (Buffer == NULL))
        return STATUS_INVALID_PARAMETER_4;
    return STATUS_NOT_SUPPORTED;
}

static const DMA_IOMMU_INTERFACE_V1 IopIommuInterfaceV1 =
{
    IopIommuCreateDomain,
    IopIommuDeleteDomainLegacy,
    IopIommuAttachDevice,
    IopIommuDetachDevice,
    IopIommuFlushDomainLegacy,
    IopIommuFlushDomainByVaListLegacy,
    IopIommuQueryInputMappingsLegacy,
    IopIommuMapLogicalRange,
    IopIommuUnmapLogicalRangeLegacy,
    IopIommuMapIdentityRange,
    IopIommuUnmapIdentityRange,
    IopIommuSetDeviceFaultReporting,
    IopIommuConfigureDomainLegacy
};

static const DMA_IOMMU_INTERFACE_V2 IopIommuInterfaceV2 =
{
    IopIommuCreateDomainEx,
    IopIommuDeleteDomain,
    IopIommuAttachDeviceEx,
    IopIommuDetachDeviceEx,
    IopIommuFlushDomain,
    IopIommuFlushDomainByVaList,
    IopIommuQueryInputMappings,
    IopIommuMapLogicalRangeEx,
    IopIommuUnmapLogicalRange,
    IopIommuMapIdentityRangeEx,
    IopIommuUnmapIdentityRangeEx,
    IopIommuSetDeviceFaultReportingEx,
    IopIommuConfigureDomain,
    IopIommuQueryAvailableDomainTypes,
    IopIommuRegisterStateChangeCallback,
    IopIommuUnregisterStateChangeCallback,
    IopIommuReserveLogicalAddressRange,
    IopIommuFreeReservedLogicalAddressRange,
    IopIommuMapReservedLogicalRange,
    IopIommuUnmapReservedLogicalRange,
    IopIommuCreateDevice,
    IopIommuDeleteDevice
};

static const DMA_IOMMU_INTERFACE_V3 IopIommuInterfaceV3 =
{
    IopIommuCreateDomainEx,
    IopIommuDeleteDomain,
    IopIommuAttachDeviceEx,
    IopIommuDetachDeviceEx,
    IopIommuFlushDomain,
    IopIommuFlushDomainByVaList,
    IopIommuQueryInputMappings,
    IopIommuMapLogicalRangeEx,
    IopIommuUnmapLogicalRange,
    IopIommuMapIdentityRangeEx,
    IopIommuUnmapIdentityRangeEx,
    IopIommuSetDeviceFaultReportingEx,
    IopIommuConfigureDomain,
    IopIommuQueryAvailableDomainTypes,
    IopIommuRegisterStateChangeCallback,
    IopIommuUnregisterStateChangeCallback,
    IopIommuReserveLogicalAddressRange,
    IopIommuFreeReservedLogicalAddressRange,
    IopIommuMapReservedLogicalRange,
    IopIommuUnmapReservedLogicalRange,
    IopIommuCreateDevice,
    IopIommuDeleteDevice,
    IopIommuCreatePasidDevice,
    IopIommuDeletePasidDevice,
    IopIommuAttachPasidDevice,
    IopIommuDetachPasidDevice,
    IopIommuQueryDeviceInfo
};

/* INTERNAL FUNCTIONS ********************************************************/

NTSTATUS
NTAPI
IopRegisterIommuProvider(
    _In_ const IOP_IOMMU_PROVIDER *Provider)
{
    PAGED_CODE();

    if (!IopIommuProviderIsComplete(Provider))
        return STATUS_INVALID_PARAMETER;

    if (InterlockedCompareExchangePointer((PVOID volatile *)&IopIommuProvider,
                                          (PVOID)Provider,
                                          NULL) != NULL)
    {
        return STATUS_ALREADY_REGISTERED;
    }

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
IoGetIommuInterface(
    _In_ ULONG Version,
    _Out_ PDMA_IOMMU_INTERFACE InterfaceOut)
{
    PAGED_CODE();

    if (Version != DMA_IOMMU_INTERFACE_VERSION_1)
        return STATUS_INVALID_PARAMETER_1;
    if (InterfaceOut == NULL)
        return STATUS_INVALID_PARAMETER_2;
    if (IopGetIommuProvider() == NULL)
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(InterfaceOut, sizeof(*InterfaceOut));
    InterfaceOut->Version = Version;
    RtlCopyMemory(&InterfaceOut->CreateDomain,
                  &IopIommuInterfaceV1,
                  sizeof(IopIommuInterfaceV1));
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoGetIommuInterfaceEx(
    _In_ ULONG Version,
    _In_ ULONGLONG Flags,
    _Out_ PDMA_IOMMU_INTERFACE_EX InterfaceOut)
{
    SIZE_T InterfaceSize;
    const void *InterfaceRoutines;

    PAGED_CODE();

    if ((Version < DMA_IOMMU_INTERFACE_EX_VERSION_MIN) ||
        (Version > DMA_IOMMU_INTERFACE_EX_VERSION_MAX))
    {
        return STATUS_INVALID_PARAMETER_1;
    }
    if ((Flags != 0) || (InterfaceOut == NULL))
        return STATUS_INVALID_PARAMETER_3;

    if (Version == DMA_IOMMU_INTERFACE_EX_VERSION_1)
    {
        InterfaceSize = DMA_IOMMU_INTERFACE_EX_V1_SIZE;
        InterfaceRoutines = &IopIommuInterfaceV1;
    }
    else if (Version == DMA_IOMMU_INTERFACE_EX_VERSION_2)
    {
        InterfaceSize = DMA_IOMMU_INTERFACE_EX_V2_SIZE;
        InterfaceRoutines = &IopIommuInterfaceV2;
    }
    else
    {
        InterfaceSize = DMA_IOMMU_INTERFACE_EX_V3_SIZE;
        InterfaceRoutines = &IopIommuInterfaceV3;
    }

    RtlZeroMemory(InterfaceOut, InterfaceSize);
    InterfaceOut->Size = InterfaceSize;
    InterfaceOut->Version = Version;
    RtlCopyMemory(&InterfaceOut->V1,
                  InterfaceRoutines,
                  InterfaceSize - FIELD_OFFSET(DMA_IOMMU_INTERFACE_EX, V1));
    return STATUS_SUCCESS;
}
