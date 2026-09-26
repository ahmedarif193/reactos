/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/arch/common/include/halpnp.h
 * PURPOSE:         ACPI HAL bus driver and its architecture hooks
 */

#pragma once

/* acpi/halpnpdd.c: reference counting for the interfaces the bus exports */
VOID
NTAPI
HalpPnpInterfaceReference(
    _In_opt_ PVOID Context);

VOID
NTAPI
HalpPnpInterfaceDereference(
    _In_opt_ PVOID Context);

/*
 * Architecture hooks
 */

/* Fill an interface the bus exports; STATUS_NOT_SUPPORTED if unknown */
NTSTATUS
NTAPI
HalpQueryBusInterface(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ CONST GUID *InterfaceType,
    _In_ USHORT Version,
    _In_ ULONG InterfaceBufferSize,
    _Out_ PINTERFACE Interface,
    _Out_opt_ PULONG Length);

/*
 * Resources the HAL keeps for itself, reported with the ACPI device.
 * Returns the descriptor count; Descriptors may be NULL to query it.
 */
ULONG
NTAPI
HalpQueryReservedResources(
    _Out_opt_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptors);

/* EOF */
