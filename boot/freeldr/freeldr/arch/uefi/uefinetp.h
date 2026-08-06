/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Private UEFI network boot state
 */

#pragma once

#include <SimpleNetwork.h>

#define UEFI_NETWORK_RETRY_DELAY_US 1000000

/*
 * How long to wait for the firmware and the loaded drivers to produce a
 * usable protocol stack. A machine with no NIC, or one whose UNDI never
 * binds, must fall through to booting from local media instead of waiting
 * for a stack that is never going to arrive.
 */
#define UEFI_NETWORK_MAX_ATTEMPTS 30

typedef struct _UEFI_NET_CONTEXT
{
    EFI_HANDLE ControllerHandle;
    EFI_SIMPLE_NETWORK_PROTOCOL *Snp;
    EFI_IPv4_ADDRESS LocalAddress;
    EFI_IPv4_ADDRESS SubnetMask;
    EFI_IPv4_ADDRESS Gateway;
} UEFI_NET_CONTEXT, *PUEFI_NET_CONTEXT;

extern EFI_SYSTEM_TABLE *GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;

EFI_STATUS
UefiNetGetProtocol(
    _In_opt_ EFI_HANDLE PreferredHandle,
    _In_ EFI_GUID *ProtocolGuid,
    _Out_ VOID **Protocol,
    _Out_opt_ EFI_HANDLE *ProtocolHandle);

BOOLEAN
UefiNetPrepare(
    _Out_ PUEFI_NET_CONTEXT Context);

BOOLEAN
UefiNetMediaPresent(
    _In_ PUEFI_NET_CONTEXT Context);

EFI_HANDLE
UefiNetGetHttpDriverImage(VOID);

BOOLEAN
UefiNetForceRebindHttp(
    _Inout_ PUEFI_NET_CONTEXT Context);

VOID
UefiLattePandaPrepareNic(VOID);

VOID
UefiLattePandaFixMac(
    _In_ EFI_SIMPLE_NETWORK_PROTOCOL *Snp);

BOOLEAN
UefiDhcpAcquire(
    _Inout_ PUEFI_NET_CONTEXT Context);
