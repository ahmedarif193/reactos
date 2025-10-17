/*
 * PROJECT:         ReactOS api tests
 * LICENSE:         GPLv2+ - See COPYING in the top level directory
 * PURPOSE:         Basic activation tests for wbemdisp classes
 */

#include "com_apitest.h"
#include <libloaderapi.h>
#include <wbemdisp.h>

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
