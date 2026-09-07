/*
 * PROJECT:     ReactOS Desktop Window Manager
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DWM application host and native-compatible startup sequence
 */

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <reactos/dwmcore.h>

typedef struct _DWM_APP_HOST
{
    IUnknown IUnknown_iface;
    LONG References;
    IDwmSettingsManager Settings;
    DWORD PolicyBits;
    DWORD PreferenceBits;
} DWM_APP_HOST;

typedef struct _DWM_APP_HOST_VTBL
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IUnknown *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IUnknown *);
    ULONG (STDMETHODCALLTYPE *Release)(IUnknown *);
    IDwmSettingsManager *(STDMETHODCALLTYPE *GetSettingsManager)(IUnknown *);
} DWM_APP_HOST_VTBL;

typedef HRESULT (CDECL *PFN_MIL_UNINITIALIZE)(HMIL_CONNECTION *);

static const GUID IID_IDwmApplicationHost =
    {0x3ae5dff1, 0x7681, 0x484a, {0x95, 0x6a, 0x6f, 0xd0, 0x6c, 0x8e, 0x67, 0x1e}};
static const GUID IID_IWeakReference =
    {0x00000037, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

static const WCHAR DwmPreferencesKey[] = L"Software\\Microsoft\\Windows\\DWM";
static const WCHAR DwmPoliciesKey[] = L"Software\\Policies\\Microsoft\\Windows\\DWM";
static const WCHAR DwmPersonalizeKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

static HRESULT
DwmUninitializeEngine(HMIL_CONNECTION *Connection)
{
    HMODULE Core = GetModuleHandleW(L"dwmcore.dll");
    PFN_MIL_UNINITIALIZE Uninitialize;

    if (Core == NULL)
        return HRESULT_FROM_WIN32(GetLastError());
    Uninitialize = (PFN_MIL_UNINITIALIZE)GetProcAddress(
        Core, "MilCompositionEngine_Uninitialize");
    if (Uninitialize == NULL)
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    return Uninitialize(Connection);
}

static void
DwmSignalComposition(void)
{
    HMODULE Core = GetModuleHandleW(L"dwmcore.dll");
    void (CDECL *SignalStartNow)(void);

    if (Core == NULL)
        return;
    SignalStartNow = (void (CDECL *)(void))GetProcAddress(
        Core, (LPCSTR)(ULONG_PTR)1001);
    if (SignalStartNow != NULL)
        SignalStartNow();
}

static LRESULT CALLBACK
DwmNotificationWindowProc(HWND Window, UINT Message, WPARAM WParam,
                          LPARAM LParam)
{
    switch (Message)
    {
        case WM_QUERYENDSESSION:
            return TRUE;

        case WM_ENDSESSION:
            if (WParam)
                DestroyWindow(Window);
            return 0;

        case WM_CLOSE:
            DestroyWindow(Window);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
        case WM_SYSCOLORCHANGE:
        case WM_THEMECHANGED:
        case WM_DWMCOMPOSITIONCHANGED:
            DwmSignalComposition();
            return 0;

        default:
            return DefWindowProcW(Window, Message, WParam, LParam);
    }
}

static HWND
DwmCreateNotificationWindow(HINSTANCE Instance)
{
    static const WCHAR ClassName[] = L"ReactOS.Dwm.Notification";
    WNDCLASSEXW Class;

    ZeroMemory(&Class, sizeof(Class));
    Class.cbSize = sizeof(Class);
    Class.lpfnWndProc = DwmNotificationWindowProc;
    Class.hInstance = Instance;
    Class.lpszClassName = ClassName;
    if (!RegisterClassExW(&Class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return NULL;

    return CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, ClassName,
                           L"DWM Notification", WS_POPUP,
                           0, 0, 0, 0, HWND_MESSAGE, NULL, Instance, NULL);
}

static HRESULT
DwmQueryRegistryValue(HKEY Root, const WCHAR *SubKey, const WCHAR *Name,
                      DWORD ExpectedType, void *Data, DWORD *Bytes)
{
    HKEY Key;
    DWORD Type = 0;
    LONG Error;

    if (Name == NULL || Data == NULL || Bytes == NULL)
        return E_INVALIDARG;

    Error = RegOpenKeyExW(Root, SubKey, 0, KEY_QUERY_VALUE, &Key);
    if (Error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(Error);
    Error = RegQueryValueExW(Key, Name, NULL, &Type, Data, Bytes);
    RegCloseKey(Key);
    if (Error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(Error);
    if (Type != ExpectedType)
        return HRESULT_FROM_WIN32(ERROR_DATATYPE_MISMATCH);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
DwmSettings_GetPolicyDword(IDwmSettingsManager *Interface,
                           const WCHAR *Name, DWORD *Value)
{
    DWORD Bytes = sizeof(*Value);
    UNREFERENCED_PARAMETER(Interface);
    return DwmQueryRegistryValue(HKEY_LOCAL_MACHINE, DwmPoliciesKey, Name,
                                 REG_DWORD, Value, &Bytes);
}

static HRESULT STDMETHODCALLTYPE
DwmSettings_GetPreferenceDword(IDwmSettingsManager *Interface,
                               const WCHAR *Name, DWORD *Value)
{
    DWORD Bytes = sizeof(*Value);
    UNREFERENCED_PARAMETER(Interface);
    return DwmQueryRegistryValue(HKEY_CURRENT_USER, DwmPreferencesKey, Name,
                                 REG_DWORD, Value, &Bytes);
}

static HRESULT STDMETHODCALLTYPE
DwmSettings_SetPreferenceDword(IDwmSettingsManager *Interface,
                               const WCHAR *Name, DWORD Value)
{
    HKEY Key;
    DWORD Disposition;
    LONG Error;
    UNREFERENCED_PARAMETER(Interface);

    if (Name == NULL)
        return E_INVALIDARG;
    Error = RegCreateKeyExW(HKEY_CURRENT_USER, DwmPreferencesKey, 0, NULL, 0,
                            KEY_SET_VALUE, NULL, &Key, &Disposition);
    if (Error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(Error);
    Error = RegSetValueExW(Key, Name, 0, REG_DWORD,
                           (const BYTE *)&Value, sizeof(Value));
    RegCloseKey(Key);
    return HRESULT_FROM_WIN32(Error);
}

static HRESULT STDMETHODCALLTYPE
DwmSettings_GetThemesPersonalizeDword(IDwmSettingsManager *Interface,
                                      const WCHAR *Name, DWORD *Value)
{
    DWORD Bytes = sizeof(*Value);
    UNREFERENCED_PARAMETER(Interface);
    return DwmQueryRegistryValue(HKEY_CURRENT_USER, DwmPersonalizeKey, Name,
                                 REG_DWORD, Value, &Bytes);
}

static HRESULT STDMETHODCALLTYPE
DwmSettings_GetPreferenceFloat(IDwmSettingsManager *Interface,
                               const WCHAR *Name, float *Value)
{
    DWORD Bytes = sizeof(*Value);
    UNREFERENCED_PARAMETER(Interface);
    return DwmQueryRegistryValue(HKEY_CURRENT_USER, DwmPreferencesKey, Name,
                                 REG_BINARY, Value, &Bytes);
}

static HRESULT STDMETHODCALLTYPE
DwmSettings_ReadRegistryDwords(IDwmSettingsManager *Interface,
                               UINT Type, void *Settings, UINT Count)
{
    UNREFERENCED_PARAMETER(Interface);
    UNREFERENCED_PARAMETER(Type);
    if (Settings == NULL && Count != 0)
        return E_INVALIDARG;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
DwmSettings_GetPreferenceString(IDwmSettingsManager *Interface,
                                const WCHAR *Name, UINT CharacterCount,
                                WCHAR *Value)
{
    DWORD Bytes;
    UNREFERENCED_PARAMETER(Interface);

    if (Value == NULL || CharacterCount == 0 ||
        CharacterCount > MAXDWORD / sizeof(WCHAR))
        return E_INVALIDARG;
    Value[0] = UNICODE_NULL;
    Bytes = CharacterCount * sizeof(WCHAR);
    return DwmQueryRegistryValue(HKEY_CURRENT_USER, DwmPreferencesKey, Name,
                                 REG_SZ, Value, &Bytes);
}

static BOOL STDMETHODCALLTYPE
DwmSettings_ReadOnlyMode(IDwmSettingsManager *Interface, UINT Type)
{
    UNREFERENCED_PARAMETER(Interface);
    UNREFERENCED_PARAMETER(Type);
    return FALSE;
}

static BOOL STDMETHODCALLTYPE
DwmSettings_CheckPolicy(IDwmSettingsManager *Interface, DWORD Mask)
{
    DWM_APP_HOST *Host = CONTAINING_RECORD(Interface, DWM_APP_HOST, Settings);
    return (Host->PolicyBits & Mask) != 0;
}

static BOOL STDMETHODCALLTYPE
DwmSettings_CheckPreference(IDwmSettingsManager *Interface, DWORD Mask)
{
    DWM_APP_HOST *Host = CONTAINING_RECORD(Interface, DWM_APP_HOST, Settings);
    return (Host->PreferenceBits & Mask) != 0;
}

static const IDwmSettingsManagerVtbl g_DwmSettingsVtbl =
{
    DwmSettings_GetPolicyDword,
    DwmSettings_GetPreferenceDword,
    DwmSettings_SetPreferenceDword,
    DwmSettings_GetThemesPersonalizeDword,
    DwmSettings_GetPreferenceFloat,
    DwmSettings_ReadRegistryDwords,
    DwmSettings_GetPreferenceString,
    DwmSettings_ReadOnlyMode,
    DwmSettings_CheckPolicy,
    DwmSettings_CheckPreference
};

static HRESULT STDMETHODCALLTYPE
DwmAppHost_QueryInterface(IUnknown *Interface, REFIID InterfaceId, PVOID *Object)
{
    if (Object == NULL)
        return E_POINTER;
    *Object = NULL;
    if (!IsEqualIID(InterfaceId, &IID_IUnknown) &&
        !IsEqualIID(InterfaceId, &IID_IDwmApplicationHost) &&
        !IsEqualIID(InterfaceId, &IID_IWeakReference))
        return E_NOINTERFACE;

    *Object = Interface;
    IUnknown_AddRef(Interface);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE
DwmAppHost_AddRef(IUnknown *Interface)
{
    DWM_APP_HOST *Host = CONTAINING_RECORD(Interface, DWM_APP_HOST,
                                           IUnknown_iface);
    return (ULONG)InterlockedIncrement(&Host->References);
}

static ULONG STDMETHODCALLTYPE
DwmAppHost_Release(IUnknown *Interface)
{
    DWM_APP_HOST *Host = CONTAINING_RECORD(Interface, DWM_APP_HOST,
                                           IUnknown_iface);
    LONG References = InterlockedDecrement(&Host->References);

    /* The application host has process lifetime. */
    if (References < 1)
    {
        Host->References = 1;
        References = 1;
    }
    return (ULONG)References;
}

static IDwmSettingsManager *STDMETHODCALLTYPE
DwmAppHost_GetSettingsManager(IUnknown *Interface)
{
    DWM_APP_HOST *Host = CONTAINING_RECORD(Interface, DWM_APP_HOST,
                                           IUnknown_iface);
    return &Host->Settings;
}

static DWM_APP_HOST_VTBL g_DwmAppHostVtbl =
{
    DwmAppHost_QueryInterface,
    DwmAppHost_AddRef,
    DwmAppHost_Release,
    DwmAppHost_GetSettingsManager
};

int WINAPI
wWinMain(HINSTANCE Instance, HINSTANCE PreviousInstance,
         LPWSTR CommandLine, int ShowCommand)
{
    DWM_APP_HOST Host;
    HMIL_CONNECTION *Connection = NULL;
    HWND NotificationWindow;
    HRESULT Result;
    MSG Message;
    BOOL MessageResult;

    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(PreviousInstance);
    UNREFERENCED_PARAMETER(CommandLine);
    UNREFERENCED_PARAMETER(ShowCommand);

    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    ZeroMemory(&Host, sizeof(Host));
    Host.IUnknown_iface.lpVtbl = (IUnknownVtbl *)&g_DwmAppHostVtbl;
    Host.References = 1;
    Host.Settings.lpVtbl = &g_DwmSettingsVtbl;

    NotificationWindow = DwmCreateNotificationWindow(Instance);
    if (NotificationWindow == NULL)
        return (int)HRESULT_FROM_WIN32(GetLastError());

    Result = MilCompositionEngine_Initialize(0x0F, &Connection);
    if (FAILED(Result))
    {
        DestroyWindow(NotificationWindow);
        return (int)Result;
    }

    Result = DwmClientStartup(&Host.IUnknown_iface);
    if (FAILED(Result))
    {
        DwmUninitializeEngine(Connection);
        DestroyWindow(NotificationWindow);
        return (int)Result;
    }

    ZeroMemory(&Message, sizeof(Message));
    while ((MessageResult = GetMessageW(&Message, NULL, 0, 0)) != FALSE &&
           MessageResult != (BOOL)-1)
    {
        TranslateMessage(&Message);
        DispatchMessageW(&Message);
    }
    DwmUninitializeEngine(Connection);
    if (IsWindow(NotificationWindow))
        DestroyWindow(NotificationWindow);
    if (MessageResult == (BOOL)-1)
        return (int)HRESULT_FROM_WIN32(GetLastError());
    return (int)Message.wParam;
}
