/* ReactOS API tests. LGPL-2.1-or-later. */
#define COBJMACROS
#include <apitest.h>
#include <shlobj.h>
#include <knownfolders.h>

static void test_association_queries(IApplicationAssociationRegistration *registration)
{
    WCHAR extension[80], app[80], classes_path[160], choice_path[256], caps_path[160];
    HKEY classes = NULL, choice = NULL, caps = NULL, apps = NULL, associations = NULL;
    const WCHAR progid[] = L"ReactOS.AssociationTest.ProgId";
    const WCHAR override[] = L"ReactOS.AssociationTest.Override";
    WCHAR *value;
    BOOL is_default;
    HRESULT hr;
    LONG ret;
    wsprintfW(extension, L".ReactOS.AssociationTest.%lu", GetCurrentProcessId());
    wsprintfW(app, L"ReactOS.AssociationTest.%lu", GetCurrentProcessId());
    wsprintfW(classes_path, L"Software\\Classes\\%s", extension);
    wsprintfW(choice_path, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\%s\\UserChoice", extension);
    wsprintfW(caps_path, L"Software\\%s\\Capabilities", app);
    ret = RegCreateKeyW(HKEY_CURRENT_USER, classes_path, &classes);
    if (!ret) ret = RegSetValueExW(classes, NULL, 0, REG_SZ, (BYTE *)progid, sizeof(progid));
    if (ret) { skip("cannot create test association: %ld\n", ret); goto done; }
    value = NULL;
    hr = IApplicationAssociationRegistration_QueryCurrentDefault(registration, extension, AT_FILEEXTENSION, AL_EFFECTIVE, &value);
    ok(hr == S_OK && value && !lstrcmpW(value, progid), "effective user class: %#lx\n", hr);
    CoTaskMemFree(value);
    value = NULL;
    hr = IApplicationAssociationRegistration_QueryCurrentDefault(registration, extension, AT_FILEEXTENSION, AL_USER, &value);
    ok(hr == S_OK && value && !lstrcmpW(value, progid), "explicit user class: %#lx\n", hr);
    CoTaskMemFree(value);
    value = NULL;
    hr = IApplicationAssociationRegistration_QueryCurrentDefault(registration, extension, AT_FILEEXTENSION, AL_MACHINE, &value);
    ok(hr == HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION) && !value, "machine query used user association: %#lx\n", hr);
    CoTaskMemFree(value);
    ret = RegCreateKeyW(HKEY_CURRENT_USER, choice_path, &choice);
    if (!ret) ret = RegSetValueExW(choice, L"ProgId", 0, REG_SZ, (BYTE *)override, sizeof(override));
    if (ret) { skip("cannot create user choice: %ld\n", ret); goto done; }
    value = NULL;
    hr = IApplicationAssociationRegistration_QueryCurrentDefault(registration, extension, AT_FILEEXTENSION, AL_EFFECTIVE, &value);
    ok(hr == S_OK && value && !lstrcmpW(value, override), "user choice precedence: %#lx\n", hr);
    CoTaskMemFree(value);
    ret = RegCreateKeyW(HKEY_CURRENT_USER, caps_path, &caps);
    if (!ret) ret = RegCreateKeyW(caps, L"FileAssociations", &associations);
    if (!ret) ret = RegSetValueExW(associations, extension, 0, REG_SZ, (BYTE *)override, sizeof(override));
    if (!ret) ret = RegCreateKeyW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", &apps);
    if (!ret) ret = RegSetValueExW(apps, app, 0, REG_SZ, (BYTE *)caps_path, (lstrlenW(caps_path) + 1) * sizeof(WCHAR));
    if (ret) { skip("cannot register test application: %ld\n", ret); goto done; }
    hr = IApplicationAssociationRegistration_QueryAppIsDefault(registration, extension, AT_FILEEXTENSION, AL_EFFECTIVE, app, &is_default);
    ok(hr == S_OK && is_default, "QueryAppIsDefault: %#lx, %d\n", hr, is_default);
    hr = IApplicationAssociationRegistration_QueryAppIsDefaultAll(registration, AL_EFFECTIVE, app, &is_default);
    ok(hr == S_OK && is_default, "QueryAppIsDefaultAll: %#lx, %d\n", hr, is_default);
    hr = IApplicationAssociationRegistration_QueryAppIsDefault(registration, extension, AT_FILEEXTENSION, AL_USER, app, &is_default);
    ok(hr == S_OK && is_default, "QueryAppIsDefault user: %#lx, %d\n", hr, is_default);
    hr = IApplicationAssociationRegistration_QueryAppIsDefaultAll(registration, AL_USER, app, &is_default);
    ok(hr == S_OK && is_default, "QueryAppIsDefaultAll user: %#lx, %d\n", hr, is_default);
    RegSetValueExW(choice, L"ProgId", 0, REG_SZ, (BYTE *)progid, sizeof(progid));
    hr = IApplicationAssociationRegistration_QueryAppIsDefaultAll(registration, AL_EFFECTIVE, app, &is_default);
    ok(hr == S_OK && !is_default, "changed default: %#lx, %d\n", hr, is_default);
done:
    if (apps) { RegDeleteValueW(apps, app); RegCloseKey(apps); }
    if (associations) RegCloseKey(associations);
    if (caps) { RegDeleteKeyW(caps, L"FileAssociations"); RegCloseKey(caps); }
    RegDeleteKeyW(HKEY_CURRENT_USER, caps_path);
    wsprintfW(caps_path, L"Software\\%s", app);
    RegDeleteKeyW(HKEY_CURRENT_USER, caps_path);
    if (choice) RegCloseKey(choice);
    RegDeleteKeyW(HKEY_CURRENT_USER, choice_path);
    *wcsrchr(choice_path, L'\\') = 0;
    RegDeleteKeyW(HKEY_CURRENT_USER, choice_path);
    if (classes) RegCloseKey(classes);
    RegDeleteKeyW(HKEY_CURRENT_USER, classes_path);
}

START_TEST(ModernShell)
{
    IKnownFolderManager *manager = NULL;
    IApplicationAssociationRegistration *registration = NULL;
    IKnownFolder *folder = NULL;
    KNOWNFOLDERID id, *ids;
    WCHAR *path;
    WCHAR windows[MAX_PATH];
    UINT count, i;
    int csidl;
    void *unknown;
    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) { skip("CoInitialize: %#lx\n", hr); return; }
    hr = CoCreateInstance(&CLSID_KnownFolderManager, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IKnownFolderManager, (void **)&manager);
    ok(hr == S_OK, "KnownFolderManager activation: %#lx\n", hr);
    if (manager)
    {
        hr = IKnownFolderManager_FolderIdFromCsidl(manager, CSIDL_WINDOWS, &id);
        ok(hr == S_OK && IsEqualGUID(&id, &FOLDERID_Windows), "Windows id: %#lx\n", hr);
        hr = IKnownFolderManager_FolderIdFromCsidl(manager, -1, &id);
        ok(hr == E_INVALIDARG, "negative CSIDL: %#lx\n", hr);
        hr = IKnownFolderManager_FolderIdToCsidl(manager, &FOLDERID_Windows, &csidl);
        ok(hr == S_OK && csidl == CSIDL_WINDOWS, "Windows CSIDL: %#lx, %d\n", hr, csidl);
        hr = IKnownFolderManager_GetFolderIds(manager, &ids, &count);
        ok(hr == S_OK && count, "folder enumeration: %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            for (i = 0; i < count; ++i) if (IsEqualGUID(&ids[i], &FOLDERID_Windows)) break;
            ok(i < count, "Windows missing from enumeration\n");
            CoTaskMemFree(ids);
        }
        hr = IKnownFolderManager_GetFolderByName(manager, L"windows", &folder);
        ok(hr == S_OK, "canonical name: %#lx\n", hr);
        if (folder)
        {
            hr = IKnownFolder_GetPath(folder, KF_FLAG_DONT_VERIFY, &path);
            GetWindowsDirectoryW(windows, ARRAY_SIZE(windows));
            ok(hr == S_OK, "folder path: %#lx\n", hr);
            if (SUCCEEDED(hr))
            {
                ok(!lstrcmpiW(path, windows), "unexpected path %ls\n", path);
                CoTaskMemFree(path);
            }
            IKnownFolder_Release(folder);
        }
        IKnownFolderManager_Release(manager);
    }
    unknown = (void *)0xdeadbeef;
    hr = CoCreateInstance(&CLSID_KnownFolderManager, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IClassFactory, &unknown);
    ok(hr == E_NOINTERFACE && !unknown, "unsupported constructor IID: %#lx, %p\n", hr, unknown);
    hr = CoCreateInstance(&CLSID_ApplicationAssociationRegistration, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IApplicationAssociationRegistration, (void **)&registration);
    ok(hr == S_OK, "association registration activation: %#lx\n", hr);
    if (registration)
    {
        path = (void *)0xdeadbeef;
        hr = IApplicationAssociationRegistration_QueryCurrentDefault(registration,
                L"", AT_FILEEXTENSION, AL_EFFECTIVE, &path);
        ok(hr == E_INVALIDARG && !path, "empty extension: %#lx, %p\n", hr, path);
        path = (void *)0xdeadbeef;
        hr = IApplicationAssociationRegistration_QueryCurrentDefault(registration,
                L".ReactOS.Nonexistent.Association.9D351", AT_FILEEXTENSION, AL_EFFECTIVE, &path);
        ok(hr == HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION) && !path,
           "absent association: %#lx, %p\n", hr, path);
        test_association_queries(registration);
        IApplicationAssociationRegistration_Release(registration);
    }
    CoUninitialize();
}
