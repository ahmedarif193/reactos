/* Unit tests for progressdialog object
 *
 * Copyright 2012 Detlef Riekenberg
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define COBJMACROS

#include <stdarg.h>
#include <shlobj.h>

#include "wine/test.h"

#define IDC_PROGRESS_PAUSE 110

static void pump_messages(DWORD milliseconds)
{
    DWORD end = GetTickCount() + milliseconds;
    MSG msg;

    do
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(1);
    } while ((LONG)(end - GetTickCount()) > 0);
}


static void test_IProgressDialog_QueryInterface(void)
{
    IProgressDialog *dlg;
    IProgressDialog *dlg2;
    IOleWindow *olewindow;
    IUnknown *unk;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_ProgressDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IProgressDialog, (void **)&dlg);
    if (FAILED(hr))
    {
        win_skip("ProgressDialog is not supported, hr %#lx.\n", hr);
        return;
    }

    hr = IProgressDialog_QueryInterface(dlg, &IID_IUnknown, NULL);
    ok(hr == E_POINTER, "Unexpected hr %#lx.\n", hr);

    hr = IProgressDialog_QueryInterface(dlg, &IID_IUnknown, (void**)&unk);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    if (SUCCEEDED(hr)) {
        IUnknown_Release(unk);
    }

    hr = IProgressDialog_QueryInterface(dlg, &IID_IOleWindow, (void**)&olewindow);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    if (SUCCEEDED(hr)) {
        hr = IOleWindow_QueryInterface(olewindow, &IID_IProgressDialog, (void**)&dlg2);
        ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) {
            IProgressDialog_Release(dlg2);
        }
        IOleWindow_Release(olewindow);
    }
    IProgressDialog_Release(dlg);
}

static void test_IProgressDialog_lifecycle(DWORD flags)
{
    IProgressDialog *dlg;
    IOleWindow *olewindow;
    HWND hwnd = (HWND)0xdeadbeef;
    HRESULT hr;
    BOOL cancelled;

    hr = CoCreateInstance(&CLSID_ProgressDialog, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IProgressDialog, (void **)&dlg);
    ok(hr == S_OK, "CoCreateInstance failed, hr %#lx.\n", hr);
    if (FAILED(hr))
        return;

    hr = IProgressDialog_QueryInterface(dlg, &IID_IOleWindow, (void **)&olewindow);
    ok(hr == S_OK, "QueryInterface(IOleWindow) failed, hr %#lx.\n", hr);
    if (FAILED(hr))
    {
        IProgressDialog_Release(dlg);
        return;
    }

    IProgressDialog_SetTitle(dlg, L"progress parity title");
    IProgressDialog_SetLine(dlg, 1, L"first line", FALSE, NULL);
    IProgressDialog_SetLine(dlg, 2, L"second line", FALSE, NULL);
    IProgressDialog_SetLine(dlg, 3, L"third line", FALSE, NULL);
    IProgressDialog_SetProgress64(dlg, 0x100000001ULL, 0x200000002ULL);
    IProgressDialog_StartProgressDialog(dlg, NULL, NULL, flags, NULL);
    pump_messages(500);

    hr = IOleWindow_GetWindow(olewindow, &hwnd);
    ok(hr == S_OK, "GetWindow failed, hr %#lx.\n", hr);
    ok(hwnd && hwnd != (HWND)0xdeadbeef && IsWindow(hwnd), "invalid window %p.\n", hwnd);
    if (hwnd && IsWindow(hwnd))
    {
        ok(!GetDlgItem(hwnd, IDC_PROGRESS_PAUSE), "unexpected non-native pause control.\n");
        if (!(flags & PROGDLG_NOCANCEL))
        {
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            pump_messages(50);
            cancelled = IProgressDialog_HasUserCancelled(dlg);
            ok(cancelled, "cancel command was not observed.\n");
        }
    }

    IProgressDialog_StopProgressDialog(dlg);
    pump_messages(20);
    hwnd = (HWND)0xdeadbeef;
    hr = IOleWindow_GetWindow(olewindow, &hwnd);
    ok(hr == S_OK || hr == E_FAIL, "GetWindow after stop returned %#lx.\n", hr);
    ok(!hwnd || hwnd == (HWND)0xdeadbeef || !IsWindow(hwnd),
       "window %p survived StopProgressDialog.\n", hwnd);

    IOleWindow_Release(olewindow);
    IProgressDialog_Release(dlg);
}


START_TEST(progressdlg)
{
    CoInitialize(NULL);

    test_IProgressDialog_QueryInterface();
    test_IProgressDialog_lifecycle(PROGDLG_NORMAL);
    test_IProgressDialog_lifecycle(PROGDLG_NOCANCEL | PROGDLG_NOMINIMIZE |
                                   PROGDLG_NOPROGRESSBAR);
    test_IProgressDialog_lifecycle(PROGDLG_AUTOTIME);

    CoUninitialize();
}
