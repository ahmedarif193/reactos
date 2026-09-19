/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <msctf.h>
#include <wine/test.h>

START_TEST(activeprofile)
{
    static const CLSID service = {0x86bb4830, 0x7d9d, 0x49c4, {0x90,0x73,0x9f,0x3a,0xe9,0x70,0x21,0xa3}};
    ITfInputProcessorProfiles *profiles = NULL;
    ITfInputProcessorProfileMgr *manager = NULL;
    ITfCategoryMgr *categories = NULL;
    ITfThreadMgr *thread = NULL;
    TF_INPUTPROCESSORPROFILE profile;
    LANGID language;
    HRESULT hr;
    HKL layout;

    hr = CoInitialize(NULL);
    ok(SUCCEEDED(hr), "CoInitialize: %lx\n", hr);
    if (FAILED(hr)) return;
    hr = CoCreateInstance(&CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                         &IID_ITfInputProcessorProfiles, (void **)&profiles);
    ok(hr == S_OK, "Create profiles: %lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = ITfInputProcessorProfiles_QueryInterface(profiles, &IID_ITfInputProcessorProfileMgr, (void **)&manager);
    if (hr == E_NOINTERFACE)
    {
        win_skip("Profile manager unavailable\n");
        goto done;
    }
    ok(hr == S_OK, "Query profile manager: %lx\n", hr);
    if (FAILED(hr)) goto done;

    hr = ITfInputProcessorProfileMgr_GetActiveProfile(manager, &GUID_TFCAT_TIP_KEYBOARD, NULL);
    ok(hr == E_INVALIDARG, "NULL output: %lx\n", hr);
    hr = ITfInputProcessorProfileMgr_GetActiveProfile(manager, &GUID_NULL, &profile);
    ok(hr == E_INVALIDARG, "Invalid category: %lx\n", hr);
    layout = GetKeyboardLayout(0);
    memset(&profile, 0xcc, sizeof(profile));
    hr = ITfInputProcessorProfileMgr_GetActiveProfile(manager, &GUID_TFCAT_TIP_KEYBOARD, &profile);
    ok(hr == S_OK, "GetActiveProfile: %lx\n", hr);
    if (hr == S_OK)
    {
        ok(profile.dwProfileType == TF_PROFILETYPE_KEYBOARDLAYOUT, "Type %lu\n", profile.dwProfileType);
        ok(profile.hkl == layout, "Layout %p, expected %p\n", profile.hkl, layout);
        ok(profile.langid == LOWORD((ULONG_PTR)layout), "Language %x\n", profile.langid);
        ok(IsEqualGUID(&profile.clsid, &GUID_NULL), "Keyboard profile has a service CLSID\n");
        ok(IsEqualGUID(&profile.guidProfile, &GUID_NULL), "Keyboard profile has a profile GUID\n");
        ok(IsEqualGUID(&profile.catid, &GUID_TFCAT_TIP_KEYBOARD), "Wrong category\n");
        ok(profile.dwFlags & TF_IPP_FLAG_ACTIVE, "Active flag missing\n");
        ok(profile.hklSubstitute == NULL, "Unexpected substitute %p\n", profile.hklSubstitute);
    }

    hr = CoCreateInstance(&CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, &IID_ITfThreadMgr, (void **)&thread);
    ok(hr == S_OK, "Create thread manager: %lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = CoCreateInstance(&CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER, &IID_ITfCategoryMgr, (void **)&categories);
    ok(hr == S_OK, "Create category manager: %lx\n", hr);
    if (FAILED(hr)) goto done;
    ITfInputProcessorProfiles_GetCurrentLanguage(profiles, &language);
    hr = ITfInputProcessorProfiles_Register(profiles, &service);
    if (FAILED(hr))
    {
        skip("Cannot register test input service: %lx\n", hr);
        goto done;
    }
    hr = ITfCategoryMgr_RegisterCategory(categories, &service, &GUID_TFCAT_TIP_KEYBOARD, &service);
    ok(hr == S_OK, "Register category: %lx\n", hr);
    hr = ITfInputProcessorProfiles_AddLanguageProfile(profiles, &service, language, &service,
                                                     L"Profile test", 12, NULL, 0, 0);
    ok(hr == S_OK, "Add profile: %lx\n", hr);
    hr = ITfInputProcessorProfiles_EnableLanguageProfile(profiles, &service, language, &service, TRUE);
    ok(hr == S_OK, "Enable profile: %lx\n", hr);
    hr = ITfInputProcessorProfiles_ActivateLanguageProfile(profiles, &service, language, &service);
    ok(hr == S_OK, "Select profile: %lx\n", hr);
    hr = ITfInputProcessorProfileMgr_GetActiveProfile(manager, &GUID_TFCAT_TIP_KEYBOARD, &profile);
    ok(hr == S_OK, "Get selected service: %lx\n", hr);
    if (hr == S_OK)
    {
        ok(profile.dwProfileType == TF_PROFILETYPE_INPUTPROCESSOR, "Service type %lu\n", profile.dwProfileType);
        ok(IsEqualGUID(&profile.clsid, &service), "Wrong service CLSID\n");
        ok(IsEqualGUID(&profile.guidProfile, &service), "Wrong service profile\n");
        ok(profile.langid == language, "Service language %x, expected %x\n", profile.langid, language);
        ok(profile.hkl == NULL, "Service has keyboard handle %p\n", profile.hkl);
    }
    ITfInputProcessorProfiles_Unregister(profiles, &service);
    ITfCategoryMgr_UnregisterCategory(categories, &service, &GUID_TFCAT_TIP_KEYBOARD, &service);
done:
    if (categories) ITfCategoryMgr_Release(categories);
    if (thread) ITfThreadMgr_Release(thread);
    if (manager) ITfInputProcessorProfileMgr_Release(manager);
    if (profiles) ITfInputProcessorProfiles_Release(profiles);
    CoUninitialize();
}
