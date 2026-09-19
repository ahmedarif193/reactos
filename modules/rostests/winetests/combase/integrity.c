#define COBJMACROS
#define WIDL_using_Windows_System_Profile
#include <windows.h>
#include <objbase.h>
#include <roapi.h>
#include <winstring.h>
#include <initguid.h>
#include <windows.system.profile.h>
#include <wine/test.h>

START_TEST(integrity)
{
    IWindowsIntegrityPolicyStatics *policy;
    HRESULT hr;
    HSTRING name;
    boolean enabled, trial, can_disable, supported;
    ULONG count;
    IID *iids;
    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(SUCCEEDED(hr), "initialize: %lx\n", hr);
    if (FAILED(hr)) return;
    WindowsCreateString(RuntimeClass_Windows_System_Profile_WindowsIntegrityPolicy,
                        wcslen(RuntimeClass_Windows_System_Profile_WindowsIntegrityPolicy), &name);
    hr = RoGetActivationFactory(name, &IID_IWindowsIntegrityPolicyStatics, (void **)&policy);
    ok(hr == S_OK, "activate: %lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IWindowsIntegrityPolicyStatics_get_IsEnabled(policy, &enabled);
        ok(hr == S_OK, "enabled: %lx\n", hr);
        hr = IWindowsIntegrityPolicyStatics_get_IsEnabledForTrial(policy, &trial);
        ok(hr == S_OK && (!trial || enabled), "trial: %lx/%u\n", hr, trial);
        hr = IWindowsIntegrityPolicyStatics_get_CanDisable(policy, &can_disable);
        ok(hr == S_OK, "can disable: %lx\n", hr);
        hr = IWindowsIntegrityPolicyStatics_get_IsDisableSupported(policy, &supported);
        ok(hr == S_OK, "disable support: %lx\n", hr);
        hr = IWindowsIntegrityPolicyStatics_GetIids(policy, &count, &iids);
        ok(hr == S_OK && count != 0, "IIDs: %lx/%lu\n", hr, count);
        if (SUCCEEDED(hr)) CoTaskMemFree(iids);
        IWindowsIntegrityPolicyStatics_Release(policy);
    }
    WindowsDeleteString(name);
    RoUninitialize();
}
