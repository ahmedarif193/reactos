/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Feature-disabled graphics IOMMU export boundary
 */

#include "dxgkrnl_private.h"

/*
 * ReactOS exposes the kernel IOMMU interface shape, but no platform provider
 * currently owns a DMA-remapping domain for a graphics adapter. Mapping must
 * therefore fail explicitly; an identity mapping would defeat isolation and
 * falsely advertise successful DMA translation.
 */
NTSTATUS
CDECL
SysMmMapIommuContiguousRange(
    _In_opt_ PVOID Adapter,
    _In_ ULONGLONG DeviceAddress,
    _In_ LARGE_INTEGER PhysicalAddress,
    _In_ ULONGLONG NumberOfBytes,
    _In_ BOOLEAN Writable)
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(DeviceAddress);
    UNREFERENCED_PARAMETER(PhysicalAddress);
    UNREFERENCED_PARAMETER(NumberOfBytes);
    UNREFERENCED_PARAMETER(Writable);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
CDECL
SysMmMapIommuRange(
    _In_opt_ PVOID Adapter,
    _In_ ULONGLONG DeviceAddress,
    _In_opt_ PMDL Mdl,
    _In_ BOOLEAN Writable)
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(DeviceAddress);
    UNREFERENCED_PARAMETER(Mdl);
    UNREFERENCED_PARAMETER(Writable);
    return STATUS_NOT_SUPPORTED;
}

/* The failed map boundary above cannot leave a ReactOS-owned mapping. */
VOID
CDECL
SysMmUnmapIommuContiguousRange(
    _In_opt_ PVOID Adapter,
    _In_ ULONGLONG DeviceAddress,
    _In_ LARGE_INTEGER PhysicalAddress,
    _In_ ULONGLONG NumberOfBytes,
    _In_ BOOLEAN Writable)
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(DeviceAddress);
    UNREFERENCED_PARAMETER(PhysicalAddress);
    UNREFERENCED_PARAMETER(NumberOfBytes);
    UNREFERENCED_PARAMETER(Writable);
}

VOID
CDECL
SysMmUnmapIommuRange(
    _In_opt_ PVOID Adapter,
    _In_ ULONGLONG DeviceAddress,
    _In_opt_ PMDL Mdl,
    _In_ BOOLEAN Writable)
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(DeviceAddress);
    UNREFERENCED_PARAMETER(Mdl);
    UNREFERENCED_PARAMETER(Writable);
}
