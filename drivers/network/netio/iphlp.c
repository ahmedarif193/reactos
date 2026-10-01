/*
 * PROJECT:     LiberNT NetIO driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-mode IP Helper entry points
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <netiodef.h>
#include <ws2def.h>
#include <ws2ipdef.h>
#define IPHLPAPI_DLL_LINKAGE
#include <netioapi.h>

#define NETIO_IPHLP_TAG 'hIeN'

C_ASSERT(sizeof(MIB_IPFORWARD_ROW2) == 0x68);
C_ASSERT(FIELD_OFFSET(MIB_IPFORWARD_ROW2, SitePrefixLength) == 0x48);
C_ASSERT(FIELD_OFFSET(MIB_IPFORWARD_ROW2, ValidLifetime) == 0x4C);
C_ASSERT(FIELD_OFFSET(MIB_IPFORWARD_ROW2, Loopback) == 0x5C);
C_ASSERT(sizeof(MIB_UNICASTIPADDRESS_ROW) == 0x50);
C_ASSERT(FIELD_OFFSET(MIB_UNICASTIPADDRESS_ROW, PrefixOrigin) == 0x2C);
C_ASSERT(FIELD_OFFSET(MIB_UNICASTIPADDRESS_ROW, OnLinkPrefixLength) == 0x3C);
C_ASSERT(sizeof(MIB_IPINTERFACE_ROW) == 0xA8);
C_ASSERT(FIELD_OFFSET(MIB_IPINTERFACE_ROW, BaseReachableTime) == 0x3C);
C_ASSERT(FIELD_OFFSET(MIB_IPINTERFACE_ROW, NlMtu) == 0x98);

VOID
NTAPI
InitializeIpForwardEntry(
    _Out_ PMIB_IPFORWARD_ROW2 Row)
{
    RtlZeroMemory(Row, sizeof(*Row));
    Row->SitePrefixLength = 0xFF;
    Row->ValidLifetime = 0xFFFFFFFF;
    Row->PreferredLifetime = 0xFFFFFFFF;
    Row->Metric = 0xFFFFFFFF;
    Row->Protocol = (NL_ROUTE_PROTOCOL)0xFFFFFFFF;
    Row->Loopback = (BOOLEAN)0xFF;
    Row->AutoconfigureAddress = (BOOLEAN)0xFF;
    Row->Publish = (BOOLEAN)0xFF;
    Row->Immortal = (BOOLEAN)0xFF;
}

VOID
NTAPI
InitializeUnicastIpAddressEntry(
    _Out_ PMIB_UNICASTIPADDRESS_ROW Row)
{
    RtlZeroMemory(Row, sizeof(*Row));
    Row->PrefixOrigin = IpPrefixOriginUnchanged;
    Row->SuffixOrigin = IpSuffixOriginUnchanged;
    Row->ValidLifetime = 0xFFFFFFFF;
    Row->PreferredLifetime = 0xFFFFFFFF;
    Row->OnLinkPrefixLength = 0xFF;
}

VOID
NTAPI
InitializeIpInterfaceEntry(
    _Inout_ PMIB_IPINTERFACE_ROW Row)
{
    RtlFillMemory(Row, sizeof(*Row), 0xFF);
    Row->Family = AF_UNSPEC;
    Row->InterfaceLuid.Value = 0;
    Row->InterfaceIndex = 0;
    Row->BaseReachableTime = 0;
    Row->RetransmitTime = 0;
    Row->PathMtuDiscoveryTimeout = 0;
    Row->NlMtu = 0;
}

VOID
NTAPI
FreeMibTable(
    _In_ PVOID Memory)
{
    if (Memory != NULL)
        ExFreePoolWithTag(Memory, NETIO_IPHLP_TAG);
}

NTSTATUS
NTAPI
GetIfTable2(
    _Outptr_ PMIB_IF_TABLE2 *Table)
{
    if (Table == NULL)
        return STATUS_INVALID_PARAMETER;
    *Table = NULL;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
GetIfEntry2(
    _Inout_ PMIB_IF_ROW2 Row)
{
    if (Row == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
GetIpInterfaceTable(
    _In_ ADDRESS_FAMILY Family,
    _Outptr_ PMIB_IPINTERFACE_TABLE *Table)
{
    UNREFERENCED_PARAMETER(Family);

    if (Table == NULL)
        return STATUS_INVALID_PARAMETER;
    *Table = NULL;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
GetIpInterfaceEntry(
    _Inout_ PMIB_IPINTERFACE_ROW Row)
{
    if (Row == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
SetIpInterfaceEntry(
    _Inout_ PMIB_IPINTERFACE_ROW Row)
{
    if (Row == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
GetUnicastIpAddressTable(
    _In_ ADDRESS_FAMILY Family,
    _Outptr_ PMIB_UNICASTIPADDRESS_TABLE *Table)
{
    UNREFERENCED_PARAMETER(Family);

    if (Table == NULL)
        return STATUS_INVALID_PARAMETER;
    *Table = NULL;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
GetUnicastIpAddressEntry(
    _Inout_ PMIB_UNICASTIPADDRESS_ROW Row)
{
    if (Row == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
CreateUnicastIpAddressEntry(
    _In_ CONST MIB_UNICASTIPADDRESS_ROW *Row)
{
    if (Row == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
DeleteUnicastIpAddressEntry(
    _In_ CONST MIB_UNICASTIPADDRESS_ROW *Row)
{
    if (Row == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
GetIpForwardTable2(
    _In_ ADDRESS_FAMILY Family,
    _Outptr_ PMIB_IPFORWARD_TABLE2 *Table)
{
    UNREFERENCED_PARAMETER(Family);

    if (Table == NULL)
        return STATUS_INVALID_PARAMETER;
    *Table = NULL;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
CreateIpForwardEntry2(
    _In_ CONST MIB_IPFORWARD_ROW2 *Row)
{
    if (Row == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
DeleteIpForwardEntry2(
    _In_ CONST MIB_IPFORWARD_ROW2 *Row)
{
    if (Row == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
ConvertInterfaceLuidToGuid(
    _In_ CONST NET_LUID *InterfaceLuid,
    _Out_ GUID *InterfaceGuid)
{
    if ((InterfaceLuid == NULL) || (InterfaceGuid == NULL))
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(InterfaceGuid, sizeof(*InterfaceGuid));
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
NotifyIpInterfaceChange(
    _In_ ADDRESS_FAMILY Family,
    _In_ PIPINTERFACE_CHANGE_CALLBACK Callback,
    _In_opt_ PVOID CallerContext,
    _In_ BOOLEAN InitialNotification,
    _Inout_ HANDLE *NotificationHandle)
{
    UNREFERENCED_PARAMETER(Family);
    UNREFERENCED_PARAMETER(CallerContext);
    UNREFERENCED_PARAMETER(InitialNotification);

    if ((Callback == NULL) || (NotificationHandle == NULL))
        return STATUS_INVALID_PARAMETER;
    *NotificationHandle = NULL;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
NotifyUnicastIpAddressChange(
    _In_ ADDRESS_FAMILY Family,
    _In_ PUNICAST_IPADDRESS_CHANGE_CALLBACK Callback,
    _In_opt_ PVOID CallerContext,
    _In_ BOOLEAN InitialNotification,
    _Inout_ HANDLE *NotificationHandle)
{
    UNREFERENCED_PARAMETER(Family);
    UNREFERENCED_PARAMETER(CallerContext);
    UNREFERENCED_PARAMETER(InitialNotification);

    if ((Callback == NULL) || (NotificationHandle == NULL))
        return STATUS_INVALID_PARAMETER;
    *NotificationHandle = NULL;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
CancelMibChangeNotify2(
    _In_ HANDLE NotificationHandle)
{
    if (NotificationHandle == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_INVALID_HANDLE;
}

NTSTATUS
NTAPI
SetInterfaceDnsSettings(
    _In_ GUID Interface,
    _In_ CONST VOID *Settings)
{
    UNREFERENCED_PARAMETER(Interface);

    if (Settings == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}
