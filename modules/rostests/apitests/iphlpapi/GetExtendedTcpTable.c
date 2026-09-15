/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Tests for TCP connections enumeration functions
 * COPYRIGHT:   Copyright 2018 Pierre Schweitzer
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <iphlpapi.h>
#include <winsock2.h>

static DWORD GetExtendedTcpTableWithAlloc(PVOID *TcpTable, BOOL Order, DWORD Family, TCP_TABLE_CLASS Class)
{
    DWORD ret;
    DWORD Size = 0;

    *TcpTable = NULL;

    ret = GetExtendedTcpTable(*TcpTable, &Size, Order, Family, Class, 0);
    if (ret == ERROR_INSUFFICIENT_BUFFER)
    {
        *TcpTable = HeapAlloc(GetProcessHeap(), 0, Size);
        if (*TcpTable == NULL)
        {
            return ERROR_OUTOFMEMORY;
        }

        ret = GetExtendedTcpTable(*TcpTable, &Size, Order, Family, Class, 0);
        if (ret != NO_ERROR)
        {
            HeapFree(GetProcessHeap(), 0, *TcpTable);
            *TcpTable = NULL;
        }
    }

    return ret;
}

static void TestEstablishedPorts(void)
{
    SOCKET Listener = INVALID_SOCKET;
    SOCKET Client = INVALID_SOCKET;
    SOCKET Accepted = INVALID_SOCKET;
    SOCKADDR_IN Address;
    SOCKADDR_IN LocalAddress;
    SOCKADDR_IN RemoteAddress;
    PMIB_TCPTABLE_OWNER_PID TcpTable = NULL;
    DWORD Error;
    DWORD Index;
    INT AddressLength;
    BOOL Found = FALSE;

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Listener != INVALID_SOCKET, "socket failed: %d\n", WSAGetLastError());
    if (Listener == INVALID_SOCKET)
        goto Cleanup;

    ZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Address.sin_port = 0;
    if (bind(Listener, (SOCKADDR *)&Address, sizeof(Address)) == SOCKET_ERROR)
    {
        skip("bind failed: %d\n", WSAGetLastError());
        goto Cleanup;
    }
    if (listen(Listener, 1) == SOCKET_ERROR)
    {
        skip("listen failed: %d\n", WSAGetLastError());
        goto Cleanup;
    }

    AddressLength = sizeof(Address);
    if (getsockname(Listener, (SOCKADDR *)&Address, &AddressLength) == SOCKET_ERROR)
    {
        skip("getsockname failed: %d\n", WSAGetLastError());
        goto Cleanup;
    }

    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Client != INVALID_SOCKET, "socket failed: %d\n", WSAGetLastError());
    if (Client == INVALID_SOCKET)
        goto Cleanup;

    if (connect(Client, (SOCKADDR *)&Address, sizeof(Address)) == SOCKET_ERROR)
    {
        skip("connect failed: %d\n", WSAGetLastError());
        goto Cleanup;
    }
    Accepted = accept(Listener, NULL, NULL);
    ok(Accepted != INVALID_SOCKET, "accept failed: %d\n", WSAGetLastError());
    if (Accepted == INVALID_SOCKET)
        goto Cleanup;

    AddressLength = sizeof(LocalAddress);
    ok(getsockname(Client, (SOCKADDR *)&LocalAddress, &AddressLength) != SOCKET_ERROR,
       "getsockname failed: %d\n", WSAGetLastError());
    AddressLength = sizeof(RemoteAddress);
    ok(getpeername(Client, (SOCKADDR *)&RemoteAddress, &AddressLength) != SOCKET_ERROR,
       "getpeername failed: %d\n", WSAGetLastError());

    Error = GetExtendedTcpTableWithAlloc((PVOID *)&TcpTable, FALSE, AF_INET,
                                         TCP_TABLE_OWNER_PID_CONNECTIONS);
    ok(Error == ERROR_SUCCESS, "GetExtendedTcpTable failed: %lu\n", Error);
    if (Error != ERROR_SUCCESS)
        goto Cleanup;

    for (Index = 0; Index < TcpTable->dwNumEntries; Index++)
    {
        MIB_TCPROW_OWNER_PID *Row = &TcpTable->table[Index];

        if (Row->dwState == MIB_TCP_STATE_ESTAB &&
            Row->dwLocalAddr == LocalAddress.sin_addr.s_addr &&
            Row->dwLocalPort == LocalAddress.sin_port &&
            Row->dwRemoteAddr == RemoteAddress.sin_addr.s_addr &&
            Row->dwRemotePort == RemoteAddress.sin_port &&
            Row->dwOwningPid == GetCurrentProcessId())
        {
            Found = TRUE;
            break;
        }
    }
    ok(Found, "established connection ports were not reported in network byte order\n");

Cleanup:
    if (TcpTable != NULL)
        HeapFree(GetProcessHeap(), 0, TcpTable);
    if (Accepted != INVALID_SOCKET)
        closesocket(Accepted);
    if (Client != INVALID_SOCKET)
        closesocket(Client);
    if (Listener != INVALID_SOCKET)
        closesocket(Listener);
}

START_TEST(GetExtendedTcpTable)
{
    WSADATA wsaData;
    SOCKET sock;
    SOCKADDR_IN server;
    PMIB_TCPTABLE TcpTable;
    PMIB_TCPTABLE_OWNER_PID TcpTableOwner;
    PMIB_TCPTABLE_OWNER_MODULE TcpTableOwnerMod;
    DWORD i;
    BOOLEAN Found;
    FILETIME Creation;
    LARGE_INTEGER CreationTime;
    DWORD Pid = GetCurrentProcessId();

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        skip("Failed to init WS2\n");
        return;
    }

    TestEstablishedPorts();

    GetSystemTimeAsFileTime(&Creation);
    CreationTime.LowPart = Creation.dwLowDateTime;
    CreationTime.HighPart = Creation.dwHighDateTime;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET)
    {
        skip("Cannot create socket\n");
        goto quit;
    }

    ZeroMemory(&server, sizeof(SOCKADDR_IN));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(9876);

    if (bind(sock, (SOCKADDR*)&server, sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
    {
        skip("Cannot bind socket\n");
        goto quit2;
    }

    if (listen(sock, SOMAXCONN) == SOCKET_ERROR)
    {
        skip("Cannot listen on socket\n");
        goto quit2;
    }

    if (GetExtendedTcpTableWithAlloc((PVOID *)&TcpTable, TRUE, AF_INET, TCP_TABLE_BASIC_ALL) == ERROR_SUCCESS)
    {
        ok(TcpTable->dwNumEntries > 0, "No TCP connections?!\n");

        Found = FALSE;
        for (i = 0; i < TcpTable->dwNumEntries; ++i)
        {
            if (TcpTable->table[i].dwState == MIB_TCP_STATE_LISTEN &&
                TcpTable->table[i].dwLocalAddr == 0 &&
                TcpTable->table[i].dwLocalPort == htons(9876) &&
                TcpTable->table[i].dwRemoteAddr == 0)
            {
                Found = TRUE;
                break;
            }
        }
        ok(Found, "Our socket wasn't found!\n");

        HeapFree(GetProcessHeap(), 0, TcpTable);
    }
    else
    {
        skip("GetExtendedTcpTableWithAlloc failure\n");
    }

    if (GetExtendedTcpTableWithAlloc((PVOID *)&TcpTable, TRUE, AF_INET, TCP_TABLE_BASIC_CONNECTIONS) == ERROR_SUCCESS)
    {
        Found = FALSE;
        for (i = 0; i < TcpTable->dwNumEntries; ++i)
        {
            if (TcpTable->table[i].dwState == MIB_TCP_STATE_LISTEN &&
                TcpTable->table[i].dwLocalAddr == 0 &&
                TcpTable->table[i].dwLocalPort == htons(9876) &&
                TcpTable->table[i].dwRemoteAddr == 0)
            {
                Found = TRUE;
                break;
            }
        }
        ok(Found == FALSE, "Our socket was found!\n");

        HeapFree(GetProcessHeap(), 0, TcpTable);
    }
    else
    {
        skip("GetExtendedTcpTableWithAlloc failure\n");
    }

    if (GetExtendedTcpTableWithAlloc((PVOID *)&TcpTable, TRUE, AF_INET, TCP_TABLE_BASIC_LISTENER) == ERROR_SUCCESS)
    {
        ok(TcpTable->dwNumEntries > 0, "No TCP connections?!\n");

        Found = FALSE;
        for (i = 0; i < TcpTable->dwNumEntries; ++i)
        {
            if (TcpTable->table[i].dwState == MIB_TCP_STATE_LISTEN &&
                TcpTable->table[i].dwLocalAddr == 0 &&
                TcpTable->table[i].dwLocalPort == htons(9876) &&
                TcpTable->table[i].dwRemoteAddr == 0)
            {
                Found = TRUE;
                break;
            }
        }
        ok(Found, "Our socket wasn't found!\n");

        HeapFree(GetProcessHeap(), 0, TcpTable);
    }
    else
    {
        skip("GetExtendedTcpTableWithAlloc failure\n");
    }

    if (GetExtendedTcpTableWithAlloc((PVOID *)&TcpTableOwner, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL) == ERROR_SUCCESS)
    {
        ok(TcpTableOwner->dwNumEntries > 0, "No TCP connections?!\n");

        Found = FALSE;
        for (i = 0; i < TcpTableOwner->dwNumEntries; ++i)
        {
            if (TcpTableOwner->table[i].dwState == MIB_TCP_STATE_LISTEN &&
                TcpTableOwner->table[i].dwLocalAddr == 0 &&
                TcpTableOwner->table[i].dwLocalPort == htons(9876) &&
                TcpTableOwner->table[i].dwRemoteAddr == 0)
            {
                Found = TRUE;
                break;
            }
        }

        if (!Found)
        {
            skip("Our socket wasn't found!\n");
        }
        else
        {
            ok(TcpTableOwner->table[i].dwOwningPid == Pid, "Invalid owner\n");
        }

        HeapFree(GetProcessHeap(), 0, TcpTableOwner);
    }
    else
    {
        skip("GetExtendedTcpTableWithAlloc failure\n");
    }

    if (GetExtendedTcpTableWithAlloc((PVOID *)&TcpTableOwner, TRUE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS) == ERROR_SUCCESS)
    {
        Found = FALSE;
        for (i = 0; i < TcpTableOwner->dwNumEntries; ++i)
        {
            if (TcpTableOwner->table[i].dwState == MIB_TCP_STATE_LISTEN &&
                TcpTableOwner->table[i].dwLocalAddr == 0 &&
                TcpTableOwner->table[i].dwLocalPort == htons(9876) &&
                TcpTableOwner->table[i].dwRemoteAddr == 0)
            {
                Found = TRUE;
                break;
            }
        }
        ok(Found == FALSE, "Our socket was found!\n");

        HeapFree(GetProcessHeap(), 0, TcpTableOwner);
    }
    else
    {
        skip("GetExtendedTcpTableWithAlloc failure\n");
    }

    if (GetExtendedTcpTableWithAlloc((PVOID *)&TcpTableOwner, TRUE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER) == ERROR_SUCCESS)
    {
        ok(TcpTableOwner->dwNumEntries > 0, "No TCP connections?!\n");

        Found = FALSE;
        for (i = 0; i < TcpTableOwner->dwNumEntries; ++i)
        {
            if (TcpTableOwner->table[i].dwState == MIB_TCP_STATE_LISTEN &&
                TcpTableOwner->table[i].dwLocalAddr == 0 &&
                TcpTableOwner->table[i].dwLocalPort == htons(9876) &&
                TcpTableOwner->table[i].dwRemoteAddr == 0)
            {
                Found = TRUE;
                break;
            }
        }

        if (!Found)
        {
            skip("Our socket wasn't found!\n");
        }
        else
        {
            ok(TcpTableOwner->table[i].dwOwningPid == Pid, "Invalid owner\n");
        }

        HeapFree(GetProcessHeap(), 0, TcpTableOwner);
    }
    else
    {
        skip("GetExtendedTcpTableWithAlloc failure\n");
    }

    if (GetExtendedTcpTableWithAlloc((PVOID *)&TcpTableOwnerMod, TRUE, AF_INET, TCP_TABLE_OWNER_MODULE_ALL) == ERROR_SUCCESS)
    {
        ok(TcpTableOwnerMod->dwNumEntries > 0, "No TCP connections?!\n");

        Found = FALSE;
        for (i = 0; i < TcpTableOwnerMod->dwNumEntries; ++i)
        {
            if (TcpTableOwnerMod->table[i].dwState == MIB_TCP_STATE_LISTEN &&
                TcpTableOwnerMod->table[i].dwLocalAddr == 0 &&
                TcpTableOwnerMod->table[i].dwLocalPort == htons(9876) &&
                TcpTableOwnerMod->table[i].dwRemoteAddr == 0)
            {
                Found = TRUE;
                break;
            }
        }

        if (!Found)
        {
            skip("Our socket wasn't found!\n");
        }
        else
        {
            ok(TcpTableOwnerMod->table[i].dwOwningPid == Pid, "Invalid owner\n");

            // liCreateTimestamp may not be populated on Vista, 7, and 8.1
            if (TcpTableOwnerMod->table[i].liCreateTimestamp.QuadPart != 0)
            {
                ok(TcpTableOwnerMod->table[i].liCreateTimestamp.QuadPart >= CreationTime.QuadPart, "Invalid time\n");
                ok(TcpTableOwnerMod->table[i].liCreateTimestamp.QuadPart <= CreationTime.QuadPart + 60000000000LL, "Invalid time\n");
            }
        }

        HeapFree(GetProcessHeap(), 0, TcpTableOwnerMod);
    }
    else
    {
        skip("GetExtendedTcpTableWithAlloc failure\n");
    }

    if (GetExtendedTcpTableWithAlloc((PVOID *)&TcpTableOwnerMod, TRUE, AF_INET, TCP_TABLE_OWNER_MODULE_CONNECTIONS) == ERROR_SUCCESS)
    {
        Found = FALSE;
        for (i = 0; i < TcpTableOwnerMod->dwNumEntries; ++i)
        {
            if (TcpTableOwnerMod->table[i].dwState == MIB_TCP_STATE_LISTEN &&
                TcpTableOwnerMod->table[i].dwLocalAddr == 0 &&
                TcpTableOwnerMod->table[i].dwLocalPort == htons(9876) &&
                TcpTableOwnerMod->table[i].dwRemoteAddr == 0)
            {
                Found = TRUE;
                break;
            }
        }
        ok(Found == FALSE, "Our socket was found!\n");

        HeapFree(GetProcessHeap(), 0, TcpTableOwnerMod);
    }
    else
    {
        skip("GetExtendedTcpTableWithAlloc failure\n");
    }

    if (GetExtendedTcpTableWithAlloc((PVOID *)&TcpTableOwnerMod, TRUE, AF_INET, TCP_TABLE_OWNER_MODULE_LISTENER) == ERROR_SUCCESS)
    {
        ok(TcpTableOwnerMod->dwNumEntries > 0, "No TCP connections?!\n");

        Found = FALSE;
        for (i = 0; i < TcpTableOwnerMod->dwNumEntries; ++i)
        {
            if (TcpTableOwnerMod->table[i].dwState == MIB_TCP_STATE_LISTEN &&
                TcpTableOwnerMod->table[i].dwLocalAddr == 0 &&
                TcpTableOwnerMod->table[i].dwLocalPort == htons(9876) &&
                TcpTableOwnerMod->table[i].dwRemoteAddr == 0)
            {
                Found = TRUE;
                break;
            }
        }

        if (!Found)
        {
            skip("Our socket wasn't found!\n");
        }
        else
        {
            ok(TcpTableOwnerMod->table[i].dwOwningPid == Pid, "Invalid owner\n");

            // liCreateTimestamp may not be populated on Vista, 7, and 8.1
            if (TcpTableOwnerMod->table[i].liCreateTimestamp.QuadPart != 0)
            {
                ok(TcpTableOwnerMod->table[i].liCreateTimestamp.QuadPart >= CreationTime.QuadPart, "Invalid time\n");
                ok(TcpTableOwnerMod->table[i].liCreateTimestamp.QuadPart <= CreationTime.QuadPart + 60000000000LL, "Invalid time\n");
            }
        }

        HeapFree(GetProcessHeap(), 0, TcpTableOwnerMod);
    }
    else
    {
        skip("GetExtendedTcpTableWithAlloc failure\n");
    }

quit2:
    closesocket(sock);
quit:
    WSACleanup();
}
