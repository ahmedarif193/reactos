/* ReactOS WinRT compatibility tests. LGPL-2.1-or-later. */
#define COBJMACROS
#include <stdarg.h>
#include "initguid.h"
#include "windef.h"
#include "winbase.h"
#include "roapi.h"
#include "winstring.h"
#define WIDL_using_Windows_UI_Shell
#include "windows.ui.shell.h"
#include "wine/test.h"

START_TEST(taskbarmanager)
{
    ITaskbarManagerStatics *factory = NULL;
    ITaskbarManager *manager = NULL;
    IUnknown *identity = NULL, *unknown = NULL;
    HSTRING name, class_name;
    boolean supported, allowed;
    ULONG count;
    IID *ids;
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(SUCCEEDED(hr), "RoInitialize: %#lx\n", hr);
    if (FAILED(hr)) return;
    WindowsCreateString(RuntimeClass_Windows_UI_Shell_TaskbarManager,
                        wcslen(RuntimeClass_Windows_UI_Shell_TaskbarManager), &name);
    hr = RoGetActivationFactory(name, &IID_ITaskbarManagerStatics, (void **)&factory);
    ok(hr == S_OK, "TaskbarManager activation: %#lx\n", hr);
    if (factory)
    {
        hr = ITaskbarManagerStatics_GetDefault(factory, &manager);
        ok(hr == S_OK && manager, "GetDefault: %#lx\n", hr);
        if (manager)
        {
            hr = ITaskbarManager_get_IsSupported(manager, &supported);
            ok(hr == S_OK, "IsSupported: %#lx\n", hr);
            hr = ITaskbarManager_get_IsPinningAllowed(manager, &allowed);
            ok(hr == S_OK, "IsPinningAllowed: %#lx\n", hr);
            ok(supported || !allowed, "unsupported service allows pinning\n");
            hr = ITaskbarManager_GetRuntimeClassName(manager, &class_name);
            ok(hr == S_OK, "runtime class name: %#lx\n", hr);
            if (SUCCEEDED(hr))
            {
                ok(!wcscmp(WindowsGetStringRawBuffer(name, NULL), WindowsGetStringRawBuffer(class_name, NULL)),
                   "incorrect runtime class name\n");
                WindowsDeleteString(class_name);
            }
            hr = ITaskbarManager_GetIids(manager, &count, &ids);
            ok(hr == S_OK && count, "GetIids: %#lx\n", hr);
            if (SUCCEEDED(hr)) CoTaskMemFree(ids);
            ITaskbarManager_QueryInterface(manager, &IID_IUnknown, (void **)&identity);
            ITaskbarManager_QueryInterface(manager, &IID_IInspectable, (void **)&unknown);
            ok(identity == unknown, "inconsistent identity\n");
            if (unknown) IUnknown_Release(unknown);
            if (identity) IUnknown_Release(identity);
            ITaskbarManager_Release(manager);
        }
        ITaskbarManagerStatics_Release(factory);
    }
    WindowsDeleteString(name);
    RoUninitialize();
}
