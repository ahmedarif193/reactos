/* ReactOS API tests. LGPL-2.1-or-later. */
#include <apitest.h>
#include <shlobj.h>

START_TEST(ITaskbarList)
{
    ITaskbarList4 *taskbar = NULL;
    HRESULT hr = CoInitialize(NULL);
    ok(SUCCEEDED(hr), "CoInitialize: %#lx\n", hr);
    if (FAILED(hr)) return;
    hr = CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER,
                          IID_ITaskbarList4, (void **)&taskbar);
    ok(hr == S_OK, "ITaskbarList4 activation: %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        const IID *iids[] = { &IID_IUnknown, &IID_ITaskbarList, &IID_ITaskbarList2,
                             &IID_ITaskbarList3, &IID_ITaskbarList4 };
        IUnknown *identity = NULL;
        taskbar->QueryInterface(IID_IUnknown, (void **)&identity);
        for (unsigned i = 0; i < _countof(iids); ++i)
        {
            IUnknown *iface = NULL, *unknown = NULL;
            hr = taskbar->QueryInterface(*iids[i], (void **)&iface);
            ok(hr == S_OK, "interface %u: %#lx\n", i, hr);
            if (!iface) continue;
            hr = iface->QueryInterface(IID_IUnknown, (void **)&unknown);
            ok(hr == S_OK && unknown == identity, "inconsistent COM identity\n");
            if (unknown) unknown->Release();
            iface->Release();
        }
        if (identity) identity->Release();
        IUnknown *unknown = (IUnknown *)0xdeadbeef;
        hr = taskbar->QueryInterface(IID_IClassFactory, (void **)&unknown);
        ok(hr == E_NOINTERFACE && !unknown, "unsupported IID: %#lx, %p\n", hr, unknown);
        taskbar->Release();
    }
    CoUninitialize();
}
