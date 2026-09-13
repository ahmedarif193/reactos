/*
 * PROJECT:         ReactOS api tests
 * LICENSE:         GPL - See COPYING in the top level directory
 * PURPOSE:         Test for WSARecv
 * PROGRAMMERS:     Peter Hater
 */

#include "ws2_32.h"

#define RECV_BUF   4
#define WSARecv_TIMEOUT 2000

static int count = 0;

void
CALLBACK completion(
    DWORD dwError,
    DWORD cbTransferred,
    LPWSAOVERLAPPED lpOverlapped,
    DWORD dwFlags)
{
    //trace("completion called dwFlags %ld cbTransferred %ld lpOverlapped %p dwFlags %ld\n", dwError, cbTransferred, lpOverlapped, dwFlags);
    count++;
    ok(count == 1, "completion sould be called only once count = %d\n", count);
    ok(dwError == 0, "dwError = %ld\n", dwError);
    ok(cbTransferred == RECV_BUF, "cbTransferred %ld != %d\n", cbTransferred, RECV_BUF);
    ok(lpOverlapped != NULL, "lpOverlapped %p\n", lpOverlapped);
    if (lpOverlapped)
    {
        ok(lpOverlapped->hEvent != INVALID_HANDLE_VALUE, "lpOverlapped->hEvent %p\n", lpOverlapped->hEvent);
        if (lpOverlapped->hEvent != INVALID_HANDLE_VALUE)
            WSASetEvent(lpOverlapped->hEvent);
    }
}

void Test_WSARecv()
{
    const char szDummyBytes[RECV_BUF] = { 0xFF, 0x00, 0xFF, 0x00 };

    char szBuf[RECV_BUF];
    char szRecvBuf[RECV_BUF];
    int iResult, err;
    SOCKET sck;
    WSADATA wdata;
    WSABUF buffers;
    DWORD dwRecv, dwSent, dwFlags;
    WSAOVERLAPPED overlapped;
    char szGetRequest[] = "GET / HTTP/1.0\r\n\r\n";
    struct fd_set readable;
    BOOL ret;

    /* Start up Winsock */
    iResult = WSAStartup(MAKEWORD(2, 2), &wdata);
    ok(iResult == 0, "WSAStartup failed, iResult == %d\n", iResult);

    /* If we call recv without a socket, it should return with an error and do nothing. */
    memcpy(szBuf, szDummyBytes, RECV_BUF);
    buffers.buf = szBuf;
    buffers.len = sizeof(szBuf);
    dwFlags = 0;
    dwRecv = 0;
    iResult = WSARecv(0, &buffers, 1, &dwRecv, &dwFlags, NULL, NULL);
    ok(iResult == SOCKET_ERROR, "iRseult = %d\n", iResult);
    ok(!memcmp(szBuf, szDummyBytes, RECV_BUF), "not equal\n");

    /* Create the socket */
    sck = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if(sck == INVALID_SOCKET)
    {
        WSACleanup();
        skip("CreateSocket failed. Aborting test.\n");
        return;
    }

    /* Now we can pass at least a socket, but we have no connection yet. Should return with an error and do nothing. */
    memcpy(szBuf, szDummyBytes, RECV_BUF);
    buffers.buf = szBuf;
    buffers.len = sizeof(szBuf);
    dwFlags = 0;
    dwRecv = 0;
    iResult = WSARecv(sck, &buffers, 1, &dwRecv, &dwFlags, NULL, NULL);
    ok(iResult == SOCKET_ERROR, "iResult = %d\n", iResult);
    ok(!memcmp(szBuf, szDummyBytes, RECV_BUF), "not equal\n");

    /* Connect to "www.reactos.org" */
    if (!ConnectToReactOSWebsite(sck))
    {
        WSACleanup();
        skip("ConnectToReactOSWebsite failed. Aborting test.\n");
        return;
    }

    /* prepare overlapped */
    memset(&overlapped, 0, sizeof(overlapped));
    overlapped.hEvent = WSACreateEvent();

    /* Send the GET request */
    buffers.buf = szGetRequest;
    buffers.len = lstrlenA(szGetRequest);
    dwSent = 0;
    WSASetLastError(0xdeadbeef);
    iResult = WSASend(sck, &buffers, 1, &dwSent, 0, &overlapped, NULL);
    err = WSAGetLastError();
    ok(iResult == 0 || (iResult == SOCKET_ERROR && err == WSA_IO_PENDING), "iResult = %d, %d\n", iResult, err);
    if (err == WSA_IO_PENDING)
    {
        iResult = WSAWaitForMultipleEvents(1, &overlapped.hEvent, TRUE, WSARecv_TIMEOUT, TRUE);
        ok(iResult == WSA_WAIT_EVENT_0, "WSAWaitForMultipleEvents failed %d\n", iResult);
        ret = WSAGetOverlappedResult(sck, &overlapped, &dwSent, TRUE, &dwFlags);
        ok(ret, "WSAGetOverlappedResult failed %d\n", WSAGetLastError());
    }
    ok(dwSent == strlen(szGetRequest), "dwSent %ld != %Iu\n", dwSent, strlen(szGetRequest));
#if 0 /* break windows too */
    /* Shutdown the SEND connection */
    iResult = shutdown(sck, SD_SEND);
    ok(iResult != SOCKET_ERROR, "iResult = %d\n", iResult);
#endif
    /* Wait until we're ready to read */
    FD_ZERO(&readable);
    FD_SET(sck, &readable);

    iResult = select(0, &readable, NULL, NULL, NULL);
    ok(iResult != SOCKET_ERROR, "iResult = %d\n", iResult);

    /* Receive the data. */
    buffers.buf = szBuf;
    buffers.len = sizeof(szBuf);
    dwRecv = sizeof(szBuf);
    iResult = WSARecv(sck, &buffers, 1, &dwRecv, &dwFlags, NULL, NULL);
    ok(iResult != SOCKET_ERROR, "iResult = %d\n", iResult);
    ok(dwRecv == sizeof(szBuf), "dwRecv %ld != %Iu\n", dwRecv, sizeof(szBuf));
    /* MSG_PEEK is invalid for overlapped (MSDN), but passes??? */
    buffers.buf = szRecvBuf;
    buffers.len = sizeof(szRecvBuf);
    dwFlags = MSG_PEEK;
    dwRecv = sizeof(szRecvBuf);
    ok(overlapped.hEvent != NULL, "WSACreateEvent failed %d\n", WSAGetLastError());
    WSASetLastError(0xdeadbeef);
    iResult = WSARecv(sck, &buffers, 1, &dwRecv, &dwFlags, &overlapped, NULL);
    err = WSAGetLastError();
    ok(iResult == 0 || (iResult == SOCKET_ERROR && err == WSA_IO_PENDING), "iResult = %d, %d\n", iResult, err);
    if (err == WSA_IO_PENDING)
    {
        iResult = WSAWaitForMultipleEvents(1, &overlapped.hEvent, TRUE, WSARecv_TIMEOUT, TRUE);
        ok(iResult == WSA_WAIT_EVENT_0, "WSAWaitForMultipleEvents failed %d\n", iResult);
        ret = WSAGetOverlappedResult(sck, &overlapped, &dwRecv, TRUE, &dwFlags);
        ok(ret, "WSAGetOverlappedResult failed %d\n", WSAGetLastError());
    }
    ok(dwRecv == sizeof(szRecvBuf), "dwRecv %ld != %Iu\n", dwRecv, sizeof(szRecvBuf));
    /* normal overlapped, no completion */
    buffers.buf = szBuf;
    buffers.len = sizeof(szBuf);
    dwFlags = 0;
    dwRecv = sizeof(szBuf);
    WSAResetEvent(overlapped.hEvent);
    WSASetLastError(0xdeadbeef);
    iResult = WSARecv(sck, &buffers, 1, &dwRecv, &dwFlags, &overlapped, NULL);
    err = WSAGetLastError();
    ok(iResult == 0 || (iResult == SOCKET_ERROR && err == WSA_IO_PENDING), "iResult = %d, %d\n", iResult, err);
    if (err == WSA_IO_PENDING)
    {
        iResult = WSAWaitForMultipleEvents(1, &overlapped.hEvent, TRUE, WSARecv_TIMEOUT, TRUE);
        ok(iResult == WSA_WAIT_EVENT_0, "WSAWaitForMultipleEvents failed %d\n", iResult);
        ret = WSAGetOverlappedResult(sck, &overlapped, &dwRecv, TRUE, &dwFlags);
        ok(ret, "WSAGetOverlappedResult failed %d\n", WSAGetLastError());
    }
    ok(dwRecv == sizeof(szBuf), "dwRecv %ld != %Iu\n", dwRecv, sizeof(szBuf));
    ok(memcmp(szRecvBuf, szBuf, sizeof(szBuf)) == 0, "MSG_PEEK shouldn't have moved the pointer\n");
    /* overlapped with completion */
    dwFlags = 0;
    dwRecv = sizeof(szBuf);
    WSAResetEvent(overlapped.hEvent);
    WSASetLastError(0xdeadbeef);
    iResult = WSARecv(sck, &buffers, 1, &dwRecv, &dwFlags, &overlapped, &completion);
    err = WSAGetLastError();
    ok(iResult == 0 || (iResult == SOCKET_ERROR && err == WSA_IO_PENDING), "iResult = %d, %d\n", iResult, err);
    if (err == WSA_IO_PENDING)
    {
        iResult = WSAWaitForMultipleEvents(1, &overlapped.hEvent, TRUE, WSARecv_TIMEOUT, TRUE);
        ok(iResult == WSA_WAIT_EVENT_0, "WSAWaitForMultipleEvents failed %d\n", iResult);
        ret = WSAGetOverlappedResult(sck, &overlapped, &dwRecv, TRUE, &dwFlags);
        ok(ret, "WSAGetOverlappedResult failed %d\n", WSAGetLastError());
    }
    ret = WSACloseEvent(overlapped.hEvent);
    ok(ret, "WSACloseEvent failed %d\n", WSAGetLastError());
    ok(dwRecv == sizeof(szBuf), "dwRecv %ld != %Iu\n", dwRecv, sizeof(szBuf));
    /* no overlapped with completion */
    dwFlags = 0;
    dwRecv = sizeof(szBuf);
    WSASetLastError(0xdeadbeef);
    /* call doesn't fail, but completion is not called */
    iResult = WSARecv(sck, &buffers, 1, &dwRecv, &dwFlags, NULL, &completion);
    err = WSAGetLastError();
    ok(iResult == 0 || (iResult == SOCKET_ERROR && err == WSA_IO_PENDING), "iResult = %d, %d\n", iResult, err);
    ok(err == 0, "WSARecv failed %d\n", err);
    ok(dwRecv == sizeof(szBuf), "dwRecv %ld != %Iu and 0\n", dwRecv, sizeof(szBuf));

    closesocket(sck);
    WSACleanup();
    return;
}

START_TEST(WSARecv)
{
    Test_WSARecv();
}

START_TEST(WSABufferArray)
{
    WSADATA Data;
    SOCKET Receiver = INVALID_SOCKET, Sender = INVALID_SOCKET;
    struct sockaddr_in ReceiveAddress, SendAddress, FromAddress;
    WSABUF ReceiveBuffers[2], SendBuffer;
    WSAOVERLAPPED Overlapped;
    struct
    {
        BYTE Before;
        CHAR First[3];
        BYTE Between;
        CHAR Second[3];
        BYTE After;
    } Storage;
    DWORD Received, Sent, Flags, Wait;
    INT Result, AddressLength, FromLength, Pass;
    BOOL Completed;

    Result = WSAStartup(MAKEWORD(2, 2), &Data);
    ok(Result == 0, "WSAStartup returned %d\n", Result);
    if (Result) return;

    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.hEvent = WSACreateEvent();
    ok(Overlapped.hEvent != WSA_INVALID_EVENT, "WSACreateEvent failed: %d\n", WSAGetLastError());
    if (Overlapped.hEvent == WSA_INVALID_EVENT) goto Cleanup;

    Receiver = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    Sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(Receiver != INVALID_SOCKET && Sender != INVALID_SOCKET, "socket failed: %d\n", WSAGetLastError());
    if (Receiver == INVALID_SOCKET || Sender == INVALID_SOCKET) goto Cleanup;

    ZeroMemory(&ReceiveAddress, sizeof(ReceiveAddress));
    ReceiveAddress.sin_family = AF_INET;
    ReceiveAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    SendAddress = ReceiveAddress;
    Result = bind(Receiver, (const struct sockaddr *)&ReceiveAddress, sizeof(ReceiveAddress));
    ok(Result == 0, "receiver bind failed: %d\n", WSAGetLastError());
    if (Result) goto Cleanup;
    Result = bind(Sender, (const struct sockaddr *)&SendAddress, sizeof(SendAddress));
    ok(Result == 0, "sender bind failed: %d\n", WSAGetLastError());
    if (Result) goto Cleanup;
    AddressLength = sizeof(ReceiveAddress);
    Result = getsockname(Receiver, (struct sockaddr *)&ReceiveAddress, &AddressLength);
    ok(Result == 0, "receiver getsockname failed: %d\n", WSAGetLastError());
    if (Result) goto Cleanup;
    AddressLength = sizeof(SendAddress);
    Result = getsockname(Sender, (struct sockaddr *)&SendAddress, &AddressLength);
    ok(Result == 0, "sender getsockname failed: %d\n", WSAGetLastError());
    if (Result) goto Cleanup;

    ReceiveBuffers[0].buf = Storage.First;
    ReceiveBuffers[0].len = sizeof(Storage.First);
    ReceiveBuffers[1].buf = Storage.Second;
    ReceiveBuffers[1].len = sizeof(Storage.Second);
    SendBuffer.buf = "abcdef";
    SendBuffer.len = 6;

    /* Exercise both datagram-specific requests and connected send/receive. */
    for (Pass = 0; Pass < 2; ++Pass)
    {
        if (Pass)
        {
            Result = connect(Sender, (const struct sockaddr *)&ReceiveAddress, sizeof(ReceiveAddress));
            ok(Result == 0, "sender connect failed: %d\n", WSAGetLastError());
            if (Result) goto Cleanup;
            Result = connect(Receiver, (const struct sockaddr *)&SendAddress, sizeof(SendAddress));
            ok(Result == 0, "receiver connect failed: %d\n", WSAGetLastError());
            if (Result) goto Cleanup;
        }

        memset(&Storage, 0x55, sizeof(Storage));
        ZeroMemory(&FromAddress, sizeof(FromAddress));
        FromLength = sizeof(FromAddress);
        WSAResetEvent(Overlapped.hEvent);
        Overlapped.Internal = Overlapped.InternalHigh = 0;
        Received = Sent = Flags = 0;
        if (Pass)
            Result = WSARecv(Receiver, ReceiveBuffers, 2, &Received, &Flags, &Overlapped, NULL);
        else
            Result = WSARecvFrom(Receiver, ReceiveBuffers, 2, &Received, &Flags, (struct sockaddr *)&FromAddress, &FromLength, &Overlapped, NULL);
        ok(Result == SOCKET_ERROR && WSAGetLastError() == WSA_IO_PENDING, "pass %d: empty receive returned %d, error %d\n", Pass, Result, WSAGetLastError());
        if (Result != SOCKET_ERROR || WSAGetLastError() != WSA_IO_PENDING) goto Cleanup;

        if (Pass)
            Result = WSASend(Sender, &SendBuffer, 1, &Sent, 0, NULL, NULL);
        else
            Result = WSASendTo(Sender, &SendBuffer, 1, &Sent, 0, (const struct sockaddr *)&ReceiveAddress, sizeof(ReceiveAddress), NULL, NULL);
        ok(Result == 0 && Sent == 6, "pass %d: send returned %d, bytes %lu, error %d\n", Pass, Result, Sent, WSAGetLastError());
        if (Result) goto Cleanup;

        Wait = WaitForSingleObject(Overlapped.hEvent, 5000);
        ok(Wait == WAIT_OBJECT_0, "pass %d: receive wait returned %lu\n", Pass, Wait);
        if (Wait != WAIT_OBJECT_0) goto Cleanup;
        Completed = WSAGetOverlappedResult(Receiver, &Overlapped, &Received, FALSE, &Flags);
        ok(Completed && Received == 6, "pass %d: completion %d, bytes %lu, error %d\n", Pass, Completed, Received, WSAGetLastError());
        ok(!memcmp(Storage.First, "abc", 3) && !memcmp(Storage.Second, "def", 3), "pass %d: scatter buffers contain incorrect data\n", Pass);
        ok(Storage.Before == 0x55 && Storage.Between == 0x55 && Storage.After == 0x55, "pass %d: buffer guards changed\n", Pass);
        if (!Pass)
        {
            ok(FromLength == sizeof(FromAddress), "source address length %d\n", FromLength);
            ok(FromAddress.sin_family == AF_INET && FromAddress.sin_port == SendAddress.sin_port && FromAddress.sin_addr.s_addr == SendAddress.sin_addr.s_addr, "incorrect datagram source address\n");
        }
    }

Cleanup:
    if (Receiver != INVALID_SOCKET) closesocket(Receiver);
    if (Sender != INVALID_SOCKET) closesocket(Sender);
    if (Overlapped.hEvent != WSA_INVALID_EVENT) WSACloseEvent(Overlapped.hEvent);
    WSACleanup();
}
