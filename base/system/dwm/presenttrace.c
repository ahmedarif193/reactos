/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 * On-demand capture serviced by the host notification thread, independently
 * of the compositor. WM_COPYDATA copies wire data, never process pointers.
 */
#include <windows.h>
#include <string.h>
#include "presenttrace.h"

DPT_BANK g_DwmPresentTrace;
static PVOID volatile IcdControl;
static DPT_SNAPSHOT Capture, LastCompleted;
static volatile LONG DispatchBusy;
static ULONGLONG CpuKernelStart, CpuUserStart;
static BOOL CpuStartValid;

void DwmPresentTraceSetIcd(PFNWGLCONTROLPRESENTATIONTRACEROS Control)
{
    InterlockedExchangePointer(&IcdControl, (PVOID)Control);
}

static BOOL DwmTraceCpu(ULONGLONG *Kernel, ULONGLONG *User)
{
    FILETIME Created, Exited, K, U;
    if (!GetProcessTimes(GetCurrentProcess(), &Created, &Exited, &K, &U))
        return FALSE;
    *Kernel = ((ULONGLONG)K.dwHighDateTime << 32) | K.dwLowDateTime;
    *User = ((ULONGLONG)U.dwHighDateTime << 32) | U.dwLowDateTime;
    return TRUE;
}

static LONG DwmTraceKernel(const DPT_REQUEST *Request, DPT_DOMAIN *Domain)
{
    HDC Dc = GetDC(NULL);
    INT Result;
    if (!Dc)
        return E_FAIL;
    Result = ExtEscape(Dc, CDD_ESCAPE_PRESENT_CAPTURE, sizeof(*Request),
                       (LPCSTR)Request, sizeof(*Domain), (LPSTR)Domain);
    ReleaseDC(NULL, Dc);
    return Result > 0 ? S_OK : E_FAIL;
}

static LONG DwmTraceLocal(const DPT_REQUEST *Request)
{
    LONG Result;
    PFNWGLCONTROLPRESENTATIONTRACEROS Icd;
    ULONGLONG K, U;
    if (Request->Operation == DPT_QUERY)
        return (LastCompleted.Size && (!Request->Session || LastCompleted.Session == Request->Session)) ?
               S_OK : HRESULT_FROM_WIN32(ERROR_NOT_READY);
    if (Request->Operation == DPT_STOP && Capture.Session == Request->Session && Capture.Domain[0].Stop)
        return S_OK;

    Result = DptControl(&g_DwmPresentTrace, Request, &Capture.Domain[0],
                        sizeof(DPT_DOMAIN), GetCurrentProcessId());
    if (Result < 0)
        return Result;
    if (Request->Operation == DPT_START)
    {
        Capture.Size = sizeof(Capture);
        Capture.Version = DPT_VERSION;
        Capture.Session = Request->Session;
        Capture.Available = 1;
        Capture.CpuKernel100ns = Capture.CpuUser100ns = 0;
        ZeroMemory(&Capture.Domain[1], 2 * sizeof(DPT_DOMAIN));
        CpuStartValid = DwmTraceCpu(&CpuKernelStart, &CpuUserStart);
    }
    else if (CpuStartValid && DwmTraceCpu(&K, &U))
    {
        Capture.CpuKernel100ns = K - CpuKernelStart;
        Capture.CpuUser100ns = U - CpuUserStart;
    }
    Capture.Status[0] = S_OK;
    Icd = (PFNWGLCONTROLPRESENTATIONTRACEROS)
        InterlockedCompareExchangePointer(&IcdControl, NULL, NULL);
    if (Request->Operation == DPT_START || (Capture.Available & 2))
    {
        Capture.Status[1] = E_NOTIMPL;
        if (Icd)
            Capture.Status[1] = Icd(Request, &Capture.Domain[1], sizeof(DPT_DOMAIN)) ?
                               S_OK : HRESULT_FROM_WIN32(GetLastError());
        if (Capture.Status[1] >= 0)
            Capture.Available |= 2;
    }
    if (Request->Operation == DPT_START || (Capture.Available & 4))
    {
        Capture.Status[2] = DwmTraceKernel(Request, &Capture.Domain[2]);
        if (Capture.Status[2] >= 0)
            Capture.Available |= 4;
    }
    if (Request->Operation == DPT_STOP)
        LastCompleted = Capture;
    return S_OK;
}

/* Only the DWM host calls this export, on its notification thread. */
LRESULT WINAPI DwmPresentationTraceDispatch(HWND Window, HWND Reply,
                                            const COPYDATASTRUCT *Data)
{
    DPT_REQUEST Request;
    COPYDATASTRUCT Response;
    DWORD_PTR Result;
    LONG Status;
    BOOL Sent;
    BOOL Acquired;
    if (!Data || Data->dwData != DPT_COPYDATA ||
        Data->cbData != sizeof(Request) || !Data->lpData || !IsWindow(Reply))
        return FALSE;
    memcpy(&Request, Data->lpData, sizeof(Request));
    if (Request.Size != sizeof(Request) || Request.Version != DPT_VERSION ||
        (!Request.Session && Request.Operation != DPT_QUERY) || Request.Operation < DPT_START || Request.Operation > DPT_QUERY)
        return FALSE;
    Acquired = InterlockedCompareExchange(&DispatchBusy, 1, 0) == 0;
    Status = Acquired ? DwmTraceLocal(&Request) : HRESULT_FROM_WIN32(ERROR_BUSY);
    Response.dwData = DPT_REPLY;
    Response.cbData = Status < 0 ? sizeof(Status) : sizeof(Capture);
    Response.lpData = Status < 0 ? (PVOID)&Status :
        Request.Operation == DPT_QUERY ? (PVOID)&LastCompleted : (PVOID)&Capture;
    Sent = SendMessageTimeoutW(Reply, WM_COPYDATA, (WPARAM)Window,
        (LPARAM)&Response, SMTO_BLOCK, 2000, &Result) && Result;
    /* A failed START reply must not leave orphan instrumentation enabled. */
    if (!Sent && Status >= 0 && Request.Operation == DPT_START)
    {
        Request.Operation = DPT_STOP;
        DwmTraceLocal(&Request);
    }
    if (Acquired)
        InterlockedExchange(&DispatchBusy, 0);
    return Sent;
}
