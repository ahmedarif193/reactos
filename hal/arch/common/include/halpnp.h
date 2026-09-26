/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/arch/common/include/halpnp.h
 * PURPOSE:         ACPI HAL bus driver and its architecture hooks
 */

#pragma once

#define HAL_PLATFORM_DEVICE_MAX_MEMORY 4

/* A fixed device the firmware tables do not describe, reported as a bus child */
typedef struct _HAL_PLATFORM_DEVICE
{
    PCSTR Name;                 /* Diagnostics only                       */
    PCWSTR DeviceId;            /* PnP device/hardware ID                 */
    PCWSTR CompatibleId;        /* Optional second hardware ID, or NULL   */

    ULONG MemoryCount;
    struct
    {
        ULONGLONG Base;
        ULONG Length;
    } Memory[HAL_PLATFORM_DEVICE_MAX_MEMORY];

    ULONG Gsi;                  /* 0 = no interrupt resource              */
    BOOLEAN EdgeTriggered;      /* FALSE = level-sensitive                */
} HAL_PLATFORM_DEVICE, *PHAL_PLATFORM_DEVICE;

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

/* Platform devices to report, by index; NULL ends the list */
const HAL_PLATFORM_DEVICE *
NTAPI
HalpGetPlatformDevice(
    _In_ ULONG Index);

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
