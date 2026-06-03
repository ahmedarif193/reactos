/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Tests for GetAdaptersInfo
 */

#include <apitest.h>
#include <winsock2.h>
#include <iphlpapi.h>

#define NTOS_MODE_USER
#include <ndk/rtlfuncs.h>

static DWORD
GetAdaptersInfoWithAlloc(
    _Outptr_result_maybenull_ PIP_ADAPTER_INFO *Adapters)
{
    PIP_ADAPTER_INFO Buffer;
    ULONG Size = 0;
    DWORD Ret;

    *Adapters = NULL;

    Ret = GetAdaptersInfo(NULL, &Size);
    if (Ret != ERROR_BUFFER_OVERFLOW)
        return Ret;

    Buffer = HeapAlloc(GetProcessHeap(), 0, Size);
    if (Buffer == NULL)
        return ERROR_OUTOFMEMORY;

    Ret = GetAdaptersInfo(Buffer, &Size);
    if (Ret != ERROR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        return Ret;
    }

    *Adapters = Buffer;
    return ERROR_SUCCESS;
}

static DWORD
GetIpAddrTableWithAlloc(
    _Outptr_result_maybenull_ PMIB_IPADDRTABLE *Table)
{
    PMIB_IPADDRTABLE Buffer;
    ULONG Size = 0;
    DWORD Ret;

    *Table = NULL;

    Ret = GetIpAddrTable(NULL, &Size, FALSE);
    if (Ret != ERROR_INSUFFICIENT_BUFFER)
        return Ret;

    Buffer = HeapAlloc(GetProcessHeap(), 0, Size);
    if (Buffer == NULL)
        return ERROR_OUTOFMEMORY;

    Ret = GetIpAddrTable(Buffer, &Size, FALSE);
    if (Ret != ERROR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        return Ret;
    }

    *Table = Buffer;
    return ERROR_SUCCESS;
}

static BOOL
ParseIpAddressString(
    _In_z_ PCSTR String,
    _Out_ IPAddr *Address)
{
    struct in_addr Addr;
    const CHAR *Terminator;
    NTSTATUS Status;

    Status = RtlIpv4StringToAddressA(String, TRUE, &Terminator, &Addr);
    if (!NT_SUCCESS(Status) || *Terminator != ANSI_NULL)
        return FALSE;

    *Address = Addr.s_addr;
    return TRUE;
}

static DWORD
CountIpAddrTableRowsForAdapter(
    _In_ PMIB_IPADDRTABLE Table,
    _In_ DWORD IfIndex)
{
    DWORD Count = 0;
    DWORD i;

    for (i = 0; i < Table->dwNumEntries; i++)
    {
        if (Table->table[i].dwIndex == IfIndex)
            Count++;
    }

    return Count;
}

static BOOL
FindIpAddrTableRow(
    _In_ PMIB_IPADDRTABLE Table,
    _In_ DWORD IfIndex,
    _In_ IPAddr Address,
    _In_ IPMask Mask)
{
    DWORD i;

    for (i = 0; i < Table->dwNumEntries; i++)
    {
        if (Table->table[i].dwIndex == IfIndex &&
            Table->table[i].dwAddr == Address &&
            Table->table[i].dwMask == Mask)
        {
            return TRUE;
        }
    }

    return FALSE;
}

START_TEST(GetAdaptersInfo)
{
    PIP_ADAPTER_INFO Adapters, Adapter;
    PMIB_IPADDRTABLE IpAddrTable;
    DWORD Ret;

    Ret = GetAdaptersInfo(NULL, NULL);
    ok(Ret == ERROR_INVALID_PARAMETER,
       "GetAdaptersInfo(NULL, NULL) returned %lu\n", Ret);

    Ret = GetAdaptersInfoWithAlloc(&Adapters);
    if (Ret == ERROR_NO_DATA)
    {
        skip("No adapters returned by GetAdaptersInfo\n");
        return;
    }
    ok(Ret == ERROR_SUCCESS, "GetAdaptersInfo failed with %lu\n", Ret);
    if (Ret != ERROR_SUCCESS)
        return;

    Ret = GetIpAddrTableWithAlloc(&IpAddrTable);
    ok(Ret == ERROR_SUCCESS, "GetIpAddrTable failed with %lu\n", Ret);
    if (Ret != ERROR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, Adapters);
        return;
    }

    for (Adapter = Adapters; Adapter != NULL; Adapter = Adapter->Next)
    {
        PIP_ADDR_STRING Address;
        DWORD AddressCount = 0;
        DWORD RowCount;

        RowCount = CountIpAddrTableRowsForAdapter(IpAddrTable, Adapter->Index);

        for (Address = &Adapter->IpAddressList; Address != NULL; Address = Address->Next)
        {
            IPAddr IpAddress;
            IPMask IpMask;

            AddressCount++;
            ok(AddressCount < 256, "Adapter %lu has a cyclic IpAddressList\n", Adapter->Index);
            if (AddressCount >= 256)
                break;

            ok(ParseIpAddressString(Address->IpAddress.String, &IpAddress),
               "Adapter %lu has invalid IP address '%s'\n",
               Adapter->Index, Address->IpAddress.String);
            ok(ParseIpAddressString(Address->IpMask.String, &IpMask),
               "Adapter %lu has invalid IP mask '%s'\n",
               Adapter->Index, Address->IpMask.String);

            if (RowCount != 0)
            {
                ok(FindIpAddrTableRow(IpAddrTable, Adapter->Index, IpAddress, IpMask),
                   "Missing GetIpAddrTable row for adapter %lu address %s mask %s\n",
                   Adapter->Index, Address->IpAddress.String, Address->IpMask.String);
            }
        }

        if (RowCount != 0)
        {
            ok(AddressCount == RowCount,
               "Adapter %lu returned %lu GetAdaptersInfo addresses, expected %lu from GetIpAddrTable\n",
               Adapter->Index, AddressCount, RowCount);
        }
        else
        {
            ok(AddressCount == 1,
               "Adapter %lu has no GetIpAddrTable rows but returned %lu GetAdaptersInfo addresses\n",
               Adapter->Index, AddressCount);
        }
    }

    HeapFree(GetProcessHeap(), 0, IpAddrTable);
    HeapFree(GetProcessHeap(), 0, Adapters);
}
