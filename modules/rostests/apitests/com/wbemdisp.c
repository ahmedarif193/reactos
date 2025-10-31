/*
 * PROJECT:         ReactOS api tests
 * LICENSE:         GPLv2+ - See COPYING in the top level directory
 * PURPOSE:         Basic activation tests for wbemdisp classes
 */

#include "com_apitest.h"
#include <libloaderapi.h>
#include <initguid.h>
#include <wbemdisp.h>

DEFINE_GUID(CLSID_WbemRefresher, 0xc71566f2, 0x561e, 0x11d1, 0xad,0x87, 0x00,0xc0,0x4f,0xd8,0xfd,0xff);
DEFINE_GUID(CLSID_SWbemRefresher, 0xd269bf5c, 0xd9c1, 0x11d3, 0xb3,0x8f, 0x00,0x10,0x5a,0x1f,0x47,0x3a);

static void test_wbemrefresher_activation(void)
{
    HRESULT hr;
    IUnknown *unk = NULL;

    hr = CoCreateInstance(&CLSID_WbemRefresher, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&unk);
    ok(hr == S_OK, "CLSID_WbemRefresher activation failed hr=%lx\n", hr);
    if (SUCCEEDED(hr) && unk)
        IUnknown_Release(unk);

    hr = CoCreateInstance(&CLSID_SWbemRefresher, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&unk);
    ok(hr == S_OK, "CLSID_SWbemRefresher activation failed hr=%lx\n", hr);
    if (SUCCEEDED(hr) && unk)
        IUnknown_Release(unk);
}

void func_wbemdisp(void)
{
    HRESULT (STDAPICALLTYPE *pDllRegisterServer)(void);
    HMODULE hModule = NULL;
    HRESULT hr;
    BOOL need_uninit = FALSE;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr))
    {
        need_uninit = TRUE;
    }
    else if (hr != RPC_E_CHANGED_MODE)
    {
        ok(FALSE, "CoInitializeEx failed hr=%lx\n", hr);
        return;
    }

    hModule = LoadLibraryW(L"wbemdisp.dll");
    ok(hModule != NULL, "Failed to load wbemdisp.dll, error %lu\n", GetLastError());
    if (hModule)
    {
        pDllRegisterServer = (void *)GetProcAddress(hModule, "DllRegisterServer");
        ok(pDllRegisterServer != NULL, "DllRegisterServer not exported\n");
        if (pDllRegisterServer)
        {
            hr = pDllRegisterServer();
            ok(hr == S_OK, "DllRegisterServer failed hr=%lx\n", hr);
        }
    }

    test_wbemrefresher_activation();

    if (need_uninit)
        CoUninitialize();

    if (hModule)
        FreeLibrary(hModule);
}
