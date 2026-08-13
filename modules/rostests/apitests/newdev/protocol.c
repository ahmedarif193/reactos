/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Tests for the private newdev device-install protocol
 */

#include <apitest.h>

#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <dll/newdevp.h>

typedef BOOL (WINAPI *PCLIENT_SIDE_INSTALL_W)(HWND, HINSTANCE, LPWSTR, INT);

typedef struct _INSTALL_CONTEXT
{
    PCLIENT_SIDE_INSTALL_W Install;
    WCHAR PipeName[128];
    BOOL Result;
} INSTALL_CONTEXT, *PINSTALL_CONTEXT;

typedef enum _BATCH_TEST_CASE
{
    BatchEmpty,
    BatchTooLarge,
    BatchTruncated
} BATCH_TEST_CASE;

static DWORD WINAPI
InstallThread(
    IN PVOID Parameter)
{
    PINSTALL_CONTEXT Context = Parameter;

    Context->Result = Context->Install(NULL, NULL, Context->PipeName, SW_HIDE);
    return 0;
}

static BOOL
WriteBytewise(
    IN HANDLE Pipe,
    IN const VOID *Buffer,
    IN DWORD Size)
{
    const BYTE *Cursor = Buffer;
    DWORD BytesWritten;

    while (Size-- != 0)
    {
        if (!WriteFile(Pipe, Cursor++, 1, &BytesWritten, NULL) || BytesWritten != 1)
            return FALSE;
    }

    return TRUE;
}

static VOID
TestBatchProtocol(
    IN PCLIENT_SIDE_INSTALL_W Install,
    IN BATCH_TEST_CASE TestCase)
{
    INSTALL_CONTEXT Context;
    WCHAR EventName[128];
    HANDLE Pipe = INVALID_HANDLE_VALUE;
    HANDLE Event = NULL;
    HANDLE Thread = NULL;
    DWORD Value;
    DWORD DeviceCount;
    DWORD WaitResult;
    BOOL ShowWizard = FALSE;
    BOOL Connected = FALSE;
    BOOL PayloadWritten = FALSE;

    wsprintfW(Context.PipeName, L"\\\\.\\pipe\\newdev_apitest_%lu_%lu_%u", GetCurrentProcessId(), GetTickCount(), TestCase);
    wsprintfW(EventName, L"Local\\newdev_apitest_%lu_%lu_%u", GetCurrentProcessId(), GetTickCount(), TestCase);
    Context.Install = Install;
    Context.Result = TRUE;

    Event = CreateEventW(NULL, TRUE, FALSE, EventName);
    ok(Event != NULL, "CreateEventW failed: %lu\n", GetLastError());
    if (!Event)
        goto cleanup;

    Pipe = CreateNamedPipeW(Context.PipeName, PIPE_ACCESS_OUTBOUND, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 1024, 1024, 0, NULL);
    ok(Pipe != INVALID_HANDLE_VALUE, "CreateNamedPipeW failed: %lu\n", GetLastError());
    if (Pipe == INVALID_HANDLE_VALUE)
        goto cleanup;

    Thread = CreateThread(NULL, 0, InstallThread, &Context, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread)
        goto cleanup;

    Connected = ConnectNamedPipe(Pipe, NULL);
    if (!Connected)
        Connected = GetLastError() == ERROR_PIPE_CONNECTED;
    ok(Connected, "ConnectNamedPipe failed: %lu\n", GetLastError());
    if (!Connected)
        goto cleanup;

    Value = (lstrlenW(EventName) + 1) * sizeof(WCHAR);
    PayloadWritten = WriteBytewise(Pipe, &Value, sizeof(Value));
    PayloadWritten = PayloadWritten && WriteBytewise(Pipe, EventName, Value);
    PayloadWritten = PayloadWritten && WriteBytewise(Pipe, &ShowWizard, sizeof(ShowWizard));
    Value = NEWDEV_INSTALL_BATCH_MARKER;
    PayloadWritten = PayloadWritten && WriteBytewise(Pipe, &Value, sizeof(Value));

    if (TestCase != BatchTruncated)
    {
        DeviceCount = TestCase == BatchEmpty ? 0 : NEWDEV_INSTALL_BATCH_MAX_DEVICES + 1;
        PayloadWritten = PayloadWritten && WriteBytewise(Pipe, &DeviceCount, sizeof(DeviceCount));
    }

    ok(PayloadWritten, "Failed to write the test payload: %lu\n", GetLastError());
    if (TestCase == BatchTruncated)
    {
        CloseHandle(Pipe);
        Pipe = INVALID_HANDLE_VALUE;
    }

    WaitResult = WaitForSingleObject(Thread, 10000);
    ok(WaitResult == WAIT_OBJECT_0, "Install thread wait returned %lu\n", WaitResult);
    if (WaitResult != WAIT_OBJECT_0)
        goto cleanup;

    if (TestCase == BatchEmpty)
    {
        ok(Context.Result, "An empty batch failed: %lu\n", GetLastError());
        ok(WaitForSingleObject(Event, 0) == WAIT_OBJECT_0, "The completion event was not signalled\n");
    }
    else
    {
        ok(!Context.Result, "Malformed batch unexpectedly succeeded\n");
        ok(WaitForSingleObject(Event, 0) == WAIT_TIMEOUT, "Malformed batch signalled the completion event\n");
    }

cleanup:
    if (Pipe != INVALID_HANDLE_VALUE)
        CloseHandle(Pipe);
    if (Thread)
    {
        WaitForSingleObject(Thread, 1000);
        CloseHandle(Thread);
    }
    if (Event)
        CloseHandle(Event);
}

START_TEST(protocol)
{
    HMODULE Module;
    PCLIENT_SIDE_INSTALL_W Install;

    Module = LoadLibraryW(L"newdev.dll");
    ok(Module != NULL, "LoadLibraryW(newdev.dll) failed: %lu\n", GetLastError());
    if (!Module)
        return;

    ok(GetProcAddress(Module, "ClientSideInstallBatchW") == NULL, "ClientSideInstallBatchW must not be exported\n");
    Install = (PCLIENT_SIDE_INSTALL_W)GetProcAddress(Module, "ClientSideInstallW");

    if (!is_reactos())
    {
        win_skip("The private batch protocol is ReactOS-only\n");
        FreeLibrary(Module);
        return;
    }

    ok(Install != NULL, "ClientSideInstallW is not exported\n");
    if (Install)
    {
        TestBatchProtocol(Install, BatchEmpty);
        TestBatchProtocol(Install, BatchTooLarge);
        TestBatchProtocol(Install, BatchTruncated);
    }

    FreeLibrary(Module);
}
