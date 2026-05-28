/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:         User-mode part of the netio WSK listen/accept kmtest
 */

#include <kmt_test.h>
#include <winsock2.h>

#include "Netio.h"

#define NETIO_CONNECT_TIMEOUT_MS 5000
#define ROS_USER_SHARED_DATA_TAG 0x08eac705

static BOOL
IsReactOSSystem(void)
{
    const UCHAR *UserSharedData;

    UserSharedData = (const UCHAR *)SharedUserData;
    return *(const ULONG *)(UserSharedData + 0x1000 - sizeof(ULONG)) == ROS_USER_SHARED_DATA_TAG;
}

static SOCKET
ConnectToNetioListener(
    _In_ USHORT Port)
{
    DWORD StartTick;
    SOCKET Socket;
    struct sockaddr_in Address;
    int Error;

    ZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_port = htons(Port);
    Address.sin_addr.s_addr = inet_addr("127.0.0.1");

    StartTick = GetTickCount();
    for (;;)
    {
        Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ok(Socket != INVALID_SOCKET, "socket failed with %d\n", WSAGetLastError());
        if (Socket == INVALID_SOCKET)
            return INVALID_SOCKET;

        if (connect(Socket, (const struct sockaddr *)&Address, sizeof(Address)) == 0)
            return Socket;

        Error = WSAGetLastError();
        closesocket(Socket);

        if (GetTickCount() - StartTick >= NETIO_CONNECT_TIMEOUT_MS)
        {
            ok(FALSE, "connect(%u) failed with %d\n", Port, Error);
            return INVALID_SOCKET;
        }

        Sleep(50);
    }
}

static void
RunNetioScenario(
    _In_ DWORD StartControlCode,
    _In_ DWORD FinishControlCode,
    _In_ USHORT Port)
{
    DWORD Error;
    SOCKET Socket;

    Error = KmtSendToDriver(StartControlCode);
    ok_eq_ulong(Error, ERROR_SUCCESS);
    if (Error)
        return;

    Socket = ConnectToNetioListener(Port);
    Error = KmtSendToDriver(FinishControlCode);
    ok_eq_ulong(Error, ERROR_SUCCESS);

    if (Socket != INVALID_SOCKET)
        closesocket(Socket);
}

static void
RunNetioNoConnectScenario(
    _In_ DWORD StartControlCode,
    _In_ DWORD FinishControlCode)
{
    DWORD Error;

    Error = KmtSendToDriver(StartControlCode);
    ok_eq_ulong(Error, ERROR_SUCCESS);
    if (Error)
        return;

    Error = KmtSendToDriver(FinishControlCode);
    ok_eq_ulong(Error, ERROR_SUCCESS);
}

START_TEST(Netio)
{
    WORD WinsockVersion;
    WSADATA WsaData;
    DWORD Error;

    if (!IsReactOSSystem() && GetNTVersion() < _WIN32_WINNT_VISTA)
    {
        skip(FALSE, "kmtest:Netio requires Vista+ on Windows.\n");
        return;
    }

    WinsockVersion = MAKEWORD(2, 2);
    Error = WSAStartup(WinsockVersion, &WsaData);
    ok_eq_ulong(Error, ERROR_SUCCESS);
    if (Error)
        return;

    Error = KmtLoadAndOpenDriver(L"Netio", TRUE);
    ok_eq_ulong(Error, ERROR_SUCCESS);
    if (Error)
        goto cleanup_winsock;

    RunNetioScenario(IOCTL_NETIO_START_EVENT_TEST,
                     IOCTL_NETIO_FINISH_EVENT_TEST,
                     NETIO_TEST_EVENT_PORT);
    RunNetioScenario(IOCTL_NETIO_START_PRECEDENCE_TEST,
                     IOCTL_NETIO_FINISH_PRECEDENCE_TEST,
                     NETIO_TEST_PRECEDENCE_PORT);
    RunNetioScenario(IOCTL_NETIO_START_DISABLE_TEST,
                     IOCTL_NETIO_FINISH_DISABLE_TEST,
                     NETIO_TEST_DISABLE_PORT);
    RunNetioNoConnectScenario(IOCTL_NETIO_START_CLOSE_ACCEPT_TEST,
                              IOCTL_NETIO_FINISH_CLOSE_ACCEPT_TEST);

    KmtCloseDriver();
    KmtUnloadDriver();

cleanup_winsock:
    WSACleanup();
}
