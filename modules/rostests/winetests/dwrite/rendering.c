/* ReactOS DirectWrite monitor preferences tests. LGPL-2.1-or-later. */
#define COBJMACROS
#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winreg.h"
#include "initguid.h"
#include "dwrite.h"
#include "wine/test.h"

START_TEST(rendering)
{
    IDWriteFactory *factory = NULL;
    IDWriteRenderingParams *params = NULL;
    MONITORINFOEXW info = { sizeof(info) };
    POINT point = {0, 0};
    HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTOPRIMARY);
    HKEY key;
    WCHAR path[128];
    const WCHAR *device;
    BYTE saved[1024];
    DWORD size = sizeof(saved), type, disposition, gamma = 1800;
    LONG status;
    BOOL present;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, &IID_IDWriteFactory, (IUnknown **)&factory);
    ok(hr == S_OK, "factory: %#lx\n", hr);
    if (!factory) return;
    hr = IDWriteFactory_CreateMonitorRenderingParams(factory, monitor, &params);
    ok(hr == S_OK && params, "monitor rendering parameters: %#lx\n", hr);
    if (params) IDWriteRenderingParams_Release(params);
    if (!GetMonitorInfoW(monitor, (MONITORINFO *)&info)) goto done;
    device = info.szDevice;
    if (!wcsncmp(device, L"\\\\.\\", 4)) device += 4;
    wcscpy(path, L"Software\\Microsoft\\Avalon.Graphics\\");
    wcscat(path, device);
    status = RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &key, &disposition);
    if (status) { skip("cannot open monitor settings: %ld\n", status); goto done; }
    status = RegQueryValueExW(key, L"GammaLevel", NULL, &type, saved, &size);
    present = status == ERROR_SUCCESS;
    if (status && status != ERROR_FILE_NOT_FOUND)
    {
        skip("cannot preserve GammaLevel: %ld\n", status);
        RegCloseKey(key);
        goto done;
    }
    status = RegSetValueExW(key, L"GammaLevel", 0, REG_DWORD, (BYTE *)&gamma, sizeof(gamma));
    if (!status)
    {
        /* Use a new factory: native DirectWrite may cache settings per factory. */
        IDWriteFactory_Release(factory);
        factory = NULL;
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, &IID_IDWriteFactory, (IUnknown **)&factory);
        ok(hr == S_OK, "new factory: %#lx\n", hr);
        if (factory)
        {
            params = NULL;
            hr = IDWriteFactory_CreateMonitorRenderingParams(factory, monitor, &params);
            ok(hr == S_OK && params, "configured monitor: %#lx\n", hr);
            if (params)
            {
                float actual = IDWriteRenderingParams_GetGamma(params);
                ok(actual > 1.799f && actual < 1.801f, "monitor gamma %f, expected 1.8\n", actual);
                IDWriteRenderingParams_Release(params);
            }
        }
        if (present) status = RegSetValueExW(key, L"GammaLevel", 0, type, saved, size);
        else status = RegDeleteValueW(key, L"GammaLevel");
        ok(!status, "failed to restore GammaLevel: %ld\n", status);
    }
    else skip("cannot set monitor gamma: %ld\n", status);
    RegCloseKey(key);
    if (disposition == REG_CREATED_NEW_KEY) RegDeleteKeyW(HKEY_CURRENT_USER, path);
done:
    if (factory) IDWriteFactory_Release(factory);
}
