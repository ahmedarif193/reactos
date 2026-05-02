/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/arch/common/include/hal.h
 * PURPOSE:         Minimal HAL header for common ACPI helpers on non-x86
 */

#pragma once

#include <ntifs.h>
#include <arc/arc.h>
#include <ioaccess.h>
#include <halfuncs.h>
#include <ndk/iofuncs.h>
#include <reactos/hal/acpi_pci.h>
#include <reactos/hal/acpi_cstate.h>
#include <reactos/drivers/acpi/acpi.h>
#include <bugcodes.h>
#include <halacpi.h>
#include <halirq.h>

#ifndef TAG_HAL
#define TAG_HAL    ' laH'
#endif

#ifndef MACHINE_TYPE_ISA
#define MACHINE_TYPE_ISA        0x0000
#define MACHINE_TYPE_EISA       0x0001
#define MACHINE_TYPE_MCA        0x0002
#endif

extern ULONG HalpBusType;
extern KAFFINITY HalpDefaultInterruptAffinity;
extern PWCHAR HalHardwareIdString;

VOID
NTAPI
HalpInitDma(VOID);

VOID
NTAPI
HalpReportResourceUsage(
    _In_ PUNICODE_STRING HalName,
    _In_ INTERFACE_TYPE InterfaceType);

VOID
NTAPI
HalpRegisterPciDebuggingDeviceInfo(VOID);

NTSTATUS
NTAPI
HalpOpenRegistryKey(
    _Out_ PHANDLE KeyHandle,
    _In_opt_ HANDLE RootKey,
    _In_ PUNICODE_STRING KeyName,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ BOOLEAN Create);

BOOLEAN
NTAPI
HalpTranslateBusAddress(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ PHYSICAL_ADDRESS BusAddress,
    _Inout_ PULONG AddressSpace,
    _Out_ PPHYSICAL_ADDRESS TranslatedAddress);

NTSTATUS
NTAPI
HalpAssignSlotResources(
    _In_ PUNICODE_STRING RegistryPath,
    _In_opt_ PUNICODE_STRING DriverClassName,
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ INTERFACE_TYPE BusType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Inout_ PCM_RESOURCE_LIST *AllocatedResources);

BOOLEAN
NTAPI
HalpFindBusAddressTranslation(
    _In_ PHYSICAL_ADDRESS BusAddress,
    _Inout_ PULONG AddressSpace,
    _Out_ PPHYSICAL_ADDRESS TranslatedAddress,
    _Inout_ PULONG_PTR Context,
    _In_ BOOLEAN NextBus);

ULONG64
NTAPI
HalpAllocPhysicalMemory(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ ULONG64 MaxAddress,
    _In_ PFN_NUMBER PageCount,
    _In_ BOOLEAN Aligned);

PVOID
NTAPI
HalpMapPhysicalMemory64(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ PFN_COUNT PageCount);

VOID
NTAPI
HalpUnmapVirtualAddress(
    _In_ PVOID VirtualAddress,
    _In_ PFN_COUNT NumberPages);

NTSTATUS
NTAPI
HaliInitPnpDriver(
    VOID);

ULONG
NTAPI
HalpGetRootInterruptVector(
    _In_ ULONG BusInterruptLevel,
    _In_ ULONG BusInterruptVector,
    _Out_ PKIRQL OutIrql,
    _Out_ PKAFFINITY OutAffinity);

NTSTATUS
NTAPI
HalpQueryAcpiResourceRequirements(
    _Out_ PIO_RESOURCE_REQUIREMENTS_LIST *Requirements);
