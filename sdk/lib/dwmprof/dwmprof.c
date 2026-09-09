/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */
#include <windows.h>
#include <string.h>
#include <reactos/dwmprof.h>

typedef struct _DPT_CLIENT
{
    HWND Server;
    ULONG Session;
    LONG Status;
    DPT_SNAPSHOT *Output;
    BOOL Received;
} DPT_CLIENT;

static LRESULT CALLBACK DwmTraceReply(HWND Window, UINT Message,
                                     WPARAM WParam, LPARAM LParam)
{
    DPT_CLIENT *Client = (DPT_CLIENT *)GetWindowLongPtrW(Window, GWLP_USERDATA);
    const COPYDATASTRUCT *Data = (const COPYDATASTRUCT *)LParam;
    if (Message == WM_NCCREATE)
        SetWindowLongPtrW(Window, GWLP_USERDATA,
            (LONG_PTR)((CREATESTRUCTW *)LParam)->lpCreateParams);
    if (Message == WM_COPYDATA && Client && (HWND)WParam == Client->Server &&
        Data && Data->dwData == DPT_REPLY && Data->lpData)
    {
        if (Data->cbData == sizeof(LONG))
            memcpy(&Client->Status, Data->lpData, sizeof(LONG));
        else if (Data->cbData == sizeof(DPT_SNAPSHOT))
        {
            const DPT_SNAPSHOT *Snapshot = Data->lpData;
            if (Snapshot->Size != sizeof(*Snapshot) || Snapshot->Version != DPT_VERSION ||
                (Client->Session && Snapshot->Session != Client->Session))
                return FALSE;
            memcpy(Client->Output, Snapshot, sizeof(*Snapshot));
            Client->Status = S_OK;
        }
        else
            return FALSE;
        Client->Received = TRUE;
        return TRUE;
    }
    return DefWindowProcW(Window, Message, WParam, LParam);
}

/* Public private-ABI export: usable from a diagnostic process without GL. */
HRESULT WINAPI DwmPresentationTraceControl(const DPT_REQUEST *Request,
                                           DPT_SNAPSHOT *Output, ULONG Bytes)
{
    static const WCHAR ClassName[] = L"ReactOS.Dwm.TraceReply.v1";
    WNDCLASSW Class = {0};
    DPT_CLIENT Client = {0};
    HWND Reply;
    COPYDATASTRUCT Data;
    DWORD_PTR Result;
    BOOL Sent;
    if (!Request || Request->Size != sizeof(*Request) || Request->Version != DPT_VERSION ||
        (!Request->Session && Request->Operation != DPT_QUERY) || Request->Operation < DPT_START || Request->Operation > DPT_QUERY ||
        !Output || Bytes < sizeof(*Output))
        return E_INVALIDARG;
    Client.Server = FindWindowExW(HWND_MESSAGE, NULL, L"ReactOS.Dwm.Notification", NULL);
    if (!Client.Server)
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    Class.lpfnWndProc = DwmTraceReply;
    Class.hInstance = GetModuleHandleW(L"dwmprof.dll");
    Class.lpszClassName = ClassName;
    if (!RegisterClassW(&Class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return HRESULT_FROM_WIN32(GetLastError());
    Client.Output = Output;
    Client.Session = Request->Session;
    Client.Status = E_FAIL;
    Reply = CreateWindowExW(0, ClassName, L"", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, NULL, Class.hInstance, &Client);
    if (!Reply)
    {
        HRESULT Error = HRESULT_FROM_WIN32(GetLastError());
        UnregisterClassW(ClassName, Class.hInstance);
        return Error;
    }
    Data.dwData = DPT_COPYDATA;
    Data.cbData = sizeof(*Request);
    Data.lpData = (PVOID)Request;
    /* Pump synchronous replies. Use the actual timeout: an idle message-only
     * window or a new reply window can trip the last-input hung heuristic. */
    Sent = SendMessageTimeoutW(Client.Server, WM_COPYDATA, (WPARAM)Reply,
        (LPARAM)&Data, SMTO_NORMAL, 5000, &Result) != 0;
    DestroyWindow(Reply);
    UnregisterClassW(ClassName, Class.hInstance);
    return Sent && Result && Client.Received ? Client.Status : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
}

ULONG WINAPI DwmProfileGetSnapshotSize(void)
{
    return sizeof(DPT_SNAPSHOT);
}

HRESULT WINAPI DwmProfileStartCapture(ULONG *Session)
{
    static volatile LONG Sequence;
    DPT_REQUEST Request = {sizeof(Request), DPT_VERSION, DPT_START, 0};
    DPT_SNAPSHOT *Snapshot;
    HRESULT Status;
    if (!Session)
        return E_INVALIDARG;
    *Session = 0;
    Snapshot = HeapAlloc(GetProcessHeap(), 0, sizeof(*Snapshot));
    if (!Snapshot)
        return E_OUTOFMEMORY;
    Request.Session = (GetTickCount() ^ GetCurrentProcessId() ^
                       InterlockedIncrement(&Sequence)) | 1;
    Status = DwmPresentationTraceControl(&Request, Snapshot, sizeof(*Snapshot));
    if (SUCCEEDED(Status))
        *Session = Request.Session;
    HeapFree(GetProcessHeap(), 0, Snapshot);
    return Status;
}

HRESULT WINAPI DwmProfileStopCapture(ULONG Session, DPT_SNAPSHOT *Output, ULONG Bytes)
{
    DPT_REQUEST Request = {sizeof(Request), DPT_VERSION, DPT_STOP, Session};
    return DwmPresentationTraceControl(&Request, Output, Bytes);
}

HRESULT WINAPI DwmProfileReadLastCapture(DPT_SNAPSHOT *Output, ULONG Bytes)
{
    DPT_REQUEST Request = {sizeof(Request), DPT_VERSION, DPT_QUERY, 0};
    return DwmPresentationTraceControl(&Request, Output, Bytes);
}

HRESULT WINAPI DwmProfileCapture(ULONG Milliseconds, DPT_SNAPSHOT *Output, ULONG Bytes)
{
    ULONG Session;
    HRESULT Status;
    if (!Output || Bytes < sizeof(*Output) || Milliseconds < 100 || Milliseconds > 600000)
        return E_INVALIDARG;
    Status = DwmProfileStartCapture(&Session);
    if (FAILED(Status))
        return Status;
    Sleep(Milliseconds);
    return DwmProfileStopCapture(Session, Output, Bytes);
}
