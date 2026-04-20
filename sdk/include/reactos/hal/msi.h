/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GPL-2.0-or-later
 * PURPOSE:         Kernel-private MSI / MSI-X vector routing interface used by
 *                  ntoskrnl and bus drivers to reach the HAL's APIC MSI
 *                  allocator regardless of NTDDI_VERSION.
 *
 * NOTE:            ReactOS-specific. Not present in Windows. Public counterparts
 *                  HalGetInterruptTargetInformation / HalGetMessageRoutingInfo
 *                  are Win7 DDK APIs and remain gated in xdk/halfuncs.h.
 */

#pragma once

#include <ntdef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HAL_INTERRUPT_TARGET_INFORMATION_VERSION
typedef struct _HAL_INTERRUPT_TARGET_INFORMATION {
    ULONG Version;
    KAFFINITY TargetProcessors;
    ULONG ProcessorNumber;
    ULONG DestinationId;
} HAL_INTERRUPT_TARGET_INFORMATION, *PHAL_INTERRUPT_TARGET_INFORMATION;

#define HAL_INTERRUPT_TARGET_INFORMATION_VERSION 1
#endif

#ifndef HAL_MESSAGE_ROUTING_INFO_VERSION
typedef struct _HAL_MESSAGE_ROUTING_INFO {
    ULONG Version;
    ULONG Flags;
    KIRQL DesiredIrql;
    ULONG MessageCount;
    ULONG Vector;
    KIRQL Irql;
    KAFFINITY TargetProcessors;
    ULONG DestinationId;
    PHYSICAL_ADDRESS MessageAddress;
    USHORT MessageData;
    USHORT Reserved;
} HAL_MESSAGE_ROUTING_INFO, *PHAL_MESSAGE_ROUTING_INFO;

#define HAL_MESSAGE_ROUTING_INFO_VERSION 1
#define HAL_MSI_ROUTING_ALLOCATE_VECTOR  0x00000001
#endif

NTHALAPI
NTSTATUS
NTAPI
HalpGetInterruptTargetInformation(
  _Inout_ PHAL_INTERRUPT_TARGET_INFORMATION TargetInformation);

NTHALAPI
NTSTATUS
NTAPI
HalpGetMessageRoutingInfo(
  _Inout_ PHAL_MESSAGE_ROUTING_INFO RoutingInfo);

#ifdef __cplusplus
}
#endif
