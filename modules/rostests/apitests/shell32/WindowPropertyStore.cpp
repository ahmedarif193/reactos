#include "shelltest.h"
#include <propsys.h>

static const PROPERTYKEY key = {{0xdbaab278, 0x32c2, 0x4891, {0x88,0x62,0xfa,0x98,0xa7,0x28,0x24,0x01}}, 2};

START_TEST(WindowPropertyStore)
{
    char **argv;
    int argc = winetest_get_mainargs(&argv);
    CoInitialize(NULL);
    if (argc > 3 && !strcmp(argv[2], "child"))
    {
        HWND window = (HWND)UlongToHandle(strtoul(argv[3], NULL, 16));
        CComPtr<IPropertyStore> store;
        HRESULT hr = SHGetPropertyStoreForWindow(window, IID_IPropertyStore, (void **)&store);
        ok(hr == S_OK, "child open: %lx\n", hr);
        if (store)
        {
            PROPVARIANT value;
            PropVariantInit(&value);
            value.vt = VT_UI4;
            value.ulVal = 42;
            hr = store->SetValue(key, value);
            ok(hr == S_OK, "child write: %lx\n", hr);
        }
        CoUninitialize();
        return;
    }
    HWND window = CreateWindowW(L"STATIC", L"property-store", WS_POPUP, 0, 0, 64, 64, NULL, NULL, NULL, NULL);
    ok(window != NULL, "CreateWindow: %lu\n", GetLastError());
    if (window)
    {
        CComPtr<IPropertyStore> first, second;
        HRESULT hr = SHGetPropertyStoreForWindow(window, IID_IPropertyStore, (void **)&first);
        ok(hr == S_OK, "open: %lx\n", hr);
        if (first)
        {
            WCHAR text[] = L"persisted value";
            PROPVARIANT value, result;
            PropVariantInit(&value);
            value.vt = VT_LPWSTR;
            value.pwszVal = text;
            hr = first->SetValue(key, value);
            ok(hr == S_OK, "write: %lx\n", hr);
            text[0] = L'X';
            first.Release();
            hr = SHGetPropertyStoreForWindow(window, IID_IPropertyStore, (void **)&second);
            ok(hr == S_OK, "reopen: %lx\n", hr);
            if (second)
            {
                hr = second->GetValue(key, &result);
                ok(hr == S_OK && result.vt == VT_LPWSTR, "read: %lx/%u\n", hr, result.vt);
                if (result.vt == VT_LPWSTR) ok(!wcscmp(result.pwszVal, L"persisted value"), "value not copied\n");
                PropVariantClear(&result);
                DWORD count = 0;
                hr = second->GetCount(&count);
                ok(hr == S_OK && count == 1, "count: %lx/%lu\n", hr, count);
                PROPERTYKEY found;
                hr = second->GetAt(0, &found);
                ok(hr == S_OK && IsEqualGUID(found.fmtid, key.fmtid) && found.pid == key.pid, "wrong key\n");
                WCHAR executable[MAX_PATH], command[2 * MAX_PATH];
                GetModuleFileNameW(NULL, executable, _countof(executable));
                swprintf(command, _countof(command), L"\"%ls\" WindowPropertyStore child %08lx", executable, HandleToUlong(window));
                STARTUPINFOW startup = {sizeof(startup)};
                PROCESS_INFORMATION process;
                BOOL started = CreateProcessW(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process);
                ok(started, "child launch: %lu\n", GetLastError());
                if (started)
                {
                    DWORD wait = WaitForSingleObject(process.hProcess, 10000);
                    ok(wait == WAIT_OBJECT_0, "child wait: %lu\n", wait);
                    CloseHandle(process.hThread);
                    CloseHandle(process.hProcess);
                    hr = second->GetValue(key, &result);
                    ok(hr == S_OK && result.vt == VT_UI4 && result.ulVal == 42, "cross-process value: %lx/%u\n", hr, result.vt);
                    PropVariantClear(&result);
                }
                PropVariantInit(&value);
                hr = second->SetValue(key, value);
                ok(hr == S_OK, "remove: %lx\n", hr);
                hr = second->GetCount(&count);
                ok(hr == S_OK && !count, "empty count: %lx/%lu\n", hr, count);
                hr = second->GetValue(key, &result);
                ok(hr == S_OK && result.vt == VT_EMPTY, "missing property: %lx/%u\n", hr, result.vt);
                PropVariantClear(&result);
            }
        }
        DestroyWindow(window);
    }
    CoUninitialize();
}
