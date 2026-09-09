/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Thread manager activation flags and nesting
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <windows.h>
#include <msctf.h>
#include <wine/test.h>

#define CHECK(expression) ok((expression), "%s failed, error %lu\n", #expression, GetLastError())

START_TEST(activation)
{
    CHECK(SUCCEEDED(CoInitializeEx(0, COINIT_APARTMENTTHREADED)));
    const DWORD cases[] = {0, 1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0xffffffff};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(*cases); i++)
    {
        DWORD flags = cases[i], actual = 0xdeadbeef;
        TfClientId id = 0, id2 = 0;
        ITfThreadMgrEx *tm = 0;
        HRESULT hr = CoCreateInstance(&CLSID_TF_ThreadMgr, 0, CLSCTX_INPROC_SERVER, &IID_ITfThreadMgrEx, (void **)&tm);
        CHECK(hr == S_OK);
        if (!tm)
            continue;
        CHECK(ITfThreadMgrEx_GetActiveFlags(tm, 0) == E_INVALIDARG);
        CHECK(ITfThreadMgrEx_GetActiveFlags(tm, &actual) == S_OK);
        CHECK(actual == 0);
        CHECK(ITfThreadMgrEx_ActivateEx(tm, 0, flags) == E_INVALIDARG);
        hr = ITfThreadMgrEx_ActivateEx(tm, &id, flags);
        if (flags & ~0x7f)
        {
            CHECK(hr == E_INVALIDARG);
            CHECK(ITfThreadMgrEx_GetActiveFlags(tm, &actual) == S_OK);
            CHECK(actual == 0);
        }
        else
        {
            CHECK(hr == S_OK);
            CHECK(id != 0);
            CHECK(ITfThreadMgrEx_GetActiveFlags(tm, &actual) == S_OK);
            CHECK(actual == ((flags & ~0x20) | 0x80000000));
            CHECK(ITfThreadMgrEx_ActivateEx(tm, &id2, 0) == S_FALSE);
            CHECK(id2 == id);
            CHECK(ITfThreadMgrEx_GetActiveFlags(tm, &actual) == S_OK);
            CHECK(actual == ((flags & ~0x21) | 0x80000000));
            CHECK(ITfThreadMgrEx_Deactivate(tm) == S_OK);
            CHECK(ITfThreadMgrEx_GetActiveFlags(tm, &actual) == S_OK);
            CHECK(actual == ((flags & ~0x21) | 0x80000000));
            CHECK(ITfThreadMgrEx_Deactivate(tm) == S_OK);
            CHECK(ITfThreadMgrEx_GetActiveFlags(tm, &actual) == S_OK);
            CHECK(actual == 0);
        }
        ITfThreadMgrEx_Release(tm);
    }
    CoUninitialize();
}
