/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Filter Manager I/O, context and port test, user-mode part
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#include "fltmgr_io.h"

typedef HRESULT (WINAPI *PFN_CONNECT)(LPCWSTR, DWORD, LPCVOID, WORD, LPSECURITY_ATTRIBUTES, HANDLE *);
typedef HRESULT (WINAPI *PFN_SEND)(HANDLE, LPVOID, DWORD, LPVOID, DWORD, LPDWORD);
typedef HRESULT (WINAPI *PFN_GET)(HANDLE, PFILTER_MESSAGE_HEADER, DWORD, LPOVERLAPPED);
typedef HRESULT (WINAPI *PFN_REPLY)(HANDLE, PFILTER_REPLY_HEADER, DWORD);

typedef struct _PORT_MESSAGE_BUFFER
{
    FILTER_MESSAGE_HEADER Header;
    FLTIO_MESSAGE Body;
} PORT_MESSAGE_BUFFER;

typedef struct _PORT_REPLY_BUFFER
{
    FILTER_REPLY_HEADER Header;
    FLTIO_MESSAGE Body;
} PORT_REPLY_BUFFER;

static PFN_CONNECT pConnect;
static PFN_SEND pSend;
static PFN_GET pGet;
static PFN_REPLY pReply;

static HANDLE ReplyPort;
static OVERLAPPED ReplyOverlapped;
static PORT_MESSAGE_BUFFER ReplyMessage;
static DWORD ReplyWait, ReplyBytes;
static BOOL ReplyResult;
static HRESULT ReplyStatus;

static
DWORD
WINAPI
ReplyThread(
    LPVOID Parameter)
{
    PORT_REPLY_BUFFER Reply;

    UNREFERENCED_PARAMETER(Parameter);

    ReplyWait = WaitForSingleObject(ReplyOverlapped.hEvent, 10000);
    ReplyResult = GetOverlappedResult(ReplyPort, &ReplyOverlapped, &ReplyBytes, FALSE);

    RtlZeroMemory(&Reply, sizeof(Reply));
    Reply.Header.Status = 0;
    Reply.Header.MessageId = ReplyMessage.Header.MessageId;
    Reply.Body.Command = ReplyMessage.Body.Command;
    Reply.Body.Value = ReplyMessage.Body.Value + 1;
    ReplyStatus = pReply(ReplyPort, &Reply.Header, sizeof(FILTER_REPLY_HEADER) + sizeof(FLTIO_MESSAGE));
    return 0;
}

static
VOID
TestFiles(VOID)
{
    static UCHAR WriteBuffer[4096], ReadBuffer[4096];
    WCHAR Directory[MAX_PATH], Path[MAX_PATH];
    BY_HANDLE_FILE_INFORMATION Information;
    HANDLE File;
    DWORD Bytes, Index;

    ok(GetTempPathW(RTL_NUMBER_OF(Directory), Directory) != 0, "GetTempPathW failed: %lu\n", GetLastError());

    for (Index = 0; Index < sizeof(WriteBuffer); Index++)
    {
        WriteBuffer[Index] = (UCHAR)(Index * 7);
    }

    StringCbPrintfW(Path, sizeof(Path), L"%lsfltmgrio_test.txt", Directory);
    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed: %lu\n", GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        Bytes = 0;
        ok(WriteFile(File, WriteBuffer, sizeof(WriteBuffer), &Bytes, NULL), "WriteFile failed: %lu\n", GetLastError());
        ok_eq_ulong(Bytes, (DWORD)sizeof(WriteBuffer));

        ok(SetFilePointer(File, 0, NULL, FILE_BEGIN) == 0, "SetFilePointer failed: %lu\n", GetLastError());
        Bytes = 0;
        ok(ReadFile(File, ReadBuffer, sizeof(ReadBuffer), &Bytes, NULL), "ReadFile failed: %lu\n", GetLastError());
        ok_eq_ulong(Bytes, (DWORD)sizeof(ReadBuffer));
        ok(!memcmp(ReadBuffer, WriteBuffer, sizeof(ReadBuffer)), "Data read back differs\n");

        ok(SetFilePointer(File, 100, NULL, FILE_BEGIN) == 100, "SetFilePointer failed: %lu\n", GetLastError());
        ok(SetEndOfFile(File), "SetEndOfFile failed: %lu\n", GetLastError());

        RtlZeroMemory(&Information, sizeof(Information));
        ok(GetFileInformationByHandle(File, &Information), "GetFileInformationByHandle failed: %lu\n", GetLastError());
        ok_eq_ulong(Information.nFileSizeLow, 100UL);

        CloseHandle(File);
    }

    ok(DeleteFileW(Path), "DeleteFileW failed: %lu\n", GetLastError());

    StringCbPrintfW(Path, sizeof(Path), L"%lsfltmgrio_deny.txt", Directory);
    SetLastError(0xdeadbeef);
    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    Bytes = GetLastError();
    ok(File == INVALID_HANDLE_VALUE, "Denied file was created\n");
    ok_eq_ulong(Bytes, (DWORD)ERROR_ACCESS_DENIED);
    if (File != INVALID_HANDLE_VALUE)
    {
        CloseHandle(File);
        DeleteFileW(Path);
    }
}

static
VOID
TestPort(VOID)
{
    HMODULE FltLib;
    HANDLE Port, Second, Thread;
    FLTIO_MESSAGE Input, Output;
    PORT_MESSAGE_BUFFER Message;
    OVERLAPPED Overlapped;
    DWORD Bytes, Wait;
    HRESULT Result;

    FltLib = LoadLibraryW(L"fltlib.dll");
    ok(FltLib != NULL, "fltlib.dll not loaded: %lu\n", GetLastError());
    if (FltLib == NULL)
    {
        return;
    }

    pConnect = (PFN_CONNECT)(ULONG_PTR)GetProcAddress(FltLib, "FilterConnectCommunicationPort");
    pSend = (PFN_SEND)(ULONG_PTR)GetProcAddress(FltLib, "FilterSendMessage");
    pGet = (PFN_GET)(ULONG_PTR)GetProcAddress(FltLib, "FilterGetMessage");
    pReply = (PFN_REPLY)(ULONG_PTR)GetProcAddress(FltLib, "FilterReplyMessage");
    ok(pConnect && pSend && pGet && pReply, "fltlib.dll exports missing\n");
    if (!pConnect || !pSend || !pGet || !pReply)
    {
        return;
    }

    Port = INVALID_HANDLE_VALUE;
    Result = pConnect(L"\\FltMgrIoNoSuchPort", 0, NULL, 0, NULL, &Port);
    ok(FAILED(Result), "Connected to a missing port: 0x%lx\n", Result);

    Port = INVALID_HANDLE_VALUE;
    Result = pConnect(FLTIO_PORT_NAME, 0, FLTIO_CONNECT_CONTEXT, sizeof(FLTIO_CONNECT_CONTEXT), NULL, &Port);
    ok_eq_hex(Result, S_OK);
    if (FAILED(Result))
    {
        return;
    }

    Second = INVALID_HANDLE_VALUE;
    Result = pConnect(FLTIO_PORT_NAME, 0, FLTIO_CONNECT_CONTEXT, sizeof(FLTIO_CONNECT_CONTEXT), NULL, &Second);
    ok(FAILED(Result), "Second connection succeeded: 0x%lx\n", Result);

    Input.Command = FLTIO_MSG_PING;
    Input.Value = 41;
    RtlZeroMemory(&Output, sizeof(Output));
    Bytes = 0;
    Result = pSend(Port, &Input, sizeof(Input), &Output, sizeof(Output), &Bytes);
    ok_eq_hex(Result, S_OK);
    ok_eq_ulong(Bytes, (DWORD)sizeof(Output));
    ok_eq_ulong(Output.Command, (ULONG)FLTIO_MSG_PING);
    ok_eq_ulong(Output.Value, 42UL);

    RtlZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    RtlZeroMemory(&Message, sizeof(Message));
    Result = pGet(Port, &Message.Header, sizeof(Message), &Overlapped);
    ok_eq_hex(Result, HRESULT_FROM_WIN32(ERROR_IO_PENDING));

    Input.Command = FLTIO_MSG_SEND_NOREPLY;
    Input.Value = 7;
    Result = pSend(Port, &Input, sizeof(Input), &Output, sizeof(Output), &Bytes);
    ok_eq_hex(Result, S_OK);
    ok_eq_hex(Output.Value, 0UL);

    Wait = WaitForSingleObject(Overlapped.hEvent, 10000);
    ok_eq_ulong(Wait, (DWORD)WAIT_OBJECT_0);
    Bytes = 0;
    ok(GetOverlappedResult(Port, &Overlapped, &Bytes, FALSE), "GetOverlappedResult failed: %lu\n", GetLastError());
    ok_eq_ulong(Bytes, (DWORD)sizeof(Message));
    ok_eq_ulong(Message.Header.ReplyLength, 0UL);
    ok_eq_ulong(Message.Body.Command, (ULONG)FLTIO_MSG_SEND_NOREPLY);
    ok_eq_ulong(Message.Body.Value, 107UL);
    CloseHandle(Overlapped.hEvent);

    ReplyPort = Port;
    RtlZeroMemory(&ReplyOverlapped, sizeof(ReplyOverlapped));
    ReplyOverlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    RtlZeroMemory(&ReplyMessage, sizeof(ReplyMessage));
    Result = pGet(Port, &ReplyMessage.Header, sizeof(ReplyMessage), &ReplyOverlapped);
    ok_eq_hex(Result, HRESULT_FROM_WIN32(ERROR_IO_PENDING));

    Thread = CreateThread(NULL, 0, ReplyThread, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());

    Input.Command = FLTIO_MSG_SEND;
    Input.Value = 9;
    Result = pSend(Port, &Input, sizeof(Input), &Output, sizeof(Output), &Bytes);
    ok_eq_hex(Result, S_OK);
    ok_eq_hex(Output.Value, 0UL);

    if (Thread != NULL)
    {
        Wait = WaitForSingleObject(Thread, 15000);
        ok_eq_ulong(Wait, (DWORD)WAIT_OBJECT_0);
        CloseHandle(Thread);
    }
    ok_eq_ulong(ReplyWait, (DWORD)WAIT_OBJECT_0);
    ok(ReplyResult, "GetOverlappedResult failed in reply thread\n");
    ok_eq_ulong(ReplyBytes, (DWORD)sizeof(ReplyMessage));
    ok_eq_ulong(ReplyMessage.Header.ReplyLength, (ULONG)(sizeof(FILTER_REPLY_HEADER) + sizeof(FLTIO_MESSAGE)));
    ok_eq_ulong(ReplyMessage.Body.Value, 109UL);
    ok_eq_hex(ReplyStatus, S_OK);
    CloseHandle(ReplyOverlapped.hEvent);

    ok(CloseHandle(Port), "CloseHandle failed: %lu\n", GetLastError());
}

START_TEST(FltMgrIo)
{
    DWORD Error;

    Error = KmtLoadAndOpenDriver(L"FltMgrIo", TRUE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error)
    {
        return;
    }

    Error = KmtSendToDriver(IOCTL_FLTIO_REGISTER);
    ok_eq_int(Error, ERROR_SUCCESS);
    TestFiles();
    TestPort();
    Error = KmtSendToDriver(IOCTL_FLTIO_CHECK);
    ok_eq_int(Error, ERROR_SUCCESS);
    Error = KmtSendToDriver(IOCTL_FLTIO_UNREGISTER);
    ok_eq_int(Error, ERROR_SUCCESS);

    KmtCloseDriver();
    KmtUnloadDriver();
}
