/*
 * PROJECT:     ReactOS Desktop Window Manager
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     User DWM policy module
 */

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <reactos/dwmcore.h>

struct IWICImagingFactory;
struct IWICBitmap;

typedef HRESULT (CDECL *PFN_CREATE_COMPRESSED_SOURCE_BITMAP)(
    struct IWICImagingFactory *, const BYTE *, UINT,
    double, double, struct IWICBitmap **);

HRESULT CDECL CreateCompressedSourceBitmap(
    struct IWICImagingFactory *ImagingFactory,
    const BYTE *CompressedBytes,
    UINT ByteCount,
    double DpiX,
    double DpiY,
    struct IWICBitmap **Bitmap);

/* Keep the native private dwmcore dependency in the import graph. */
static PFN_CREATE_COMPRESSED_SOURCE_BITMAP volatile g_BitmapFactory =
    CreateCompressedSourceBitmap;

typedef HRESULT (CDECL *PFN_CREATE_CHANNEL)(IDwmChannelProvider *,
                                            IDwmChannelPrivate **);
typedef HRESULT (CDECL *PFN_CREATE_CURSOR_CONTROLLER)(ULONGLONG,
                                                       IDwmCursorController **);

typedef struct _DWM_APPLICATION_HOST_VTBL
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IUnknown *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IUnknown *);
    ULONG (STDMETHODCALLTYPE *Release)(IUnknown *);
    IDwmSettingsManager *(STDMETHODCALLTYPE *GetSettingsManager)(IUnknown *);
} DWM_APPLICATION_HOST_VTBL;

typedef struct _UDWM_CHANNEL_PROVIDER
{
    IUnknown IUnknown_iface;
    LONG References;
} UDWM_CHANNEL_PROVIDER;

static const GUID IID_IDwmApplicationHost =
    {0x3ae5dff1, 0x7681, 0x484a, {0x95, 0x6a, 0x6f, 0xd0, 0x6c, 0x8e, 0x67, 0x1e}};

static IUnknown *g_ApplicationHost;
static IDwmChannelPrivate *g_Channel;
static IDwmCursorController *g_CursorController;
static UINT g_ComposedEventId;

static HRESULT STDMETHODCALLTYPE
UdwmChannelProvider_QueryInterface(IUnknown *Interface, REFIID InterfaceId,
                                   void **Object)
{
    if (Object == NULL)
        return E_POINTER;
    *Object = NULL;
    if (!IsEqualIID(InterfaceId, &IID_IUnknown))
        return E_NOINTERFACE;
    *Object = Interface;
    IUnknown_AddRef(Interface);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE
UdwmChannelProvider_AddRef(IUnknown *Interface)
{
    UDWM_CHANNEL_PROVIDER *Provider =
        CONTAINING_RECORD(Interface, UDWM_CHANNEL_PROVIDER, IUnknown_iface);
    return (ULONG)InterlockedIncrement(&Provider->References);
}

static ULONG STDMETHODCALLTYPE
UdwmChannelProvider_Release(IUnknown *Interface)
{
    UDWM_CHANNEL_PROVIDER *Provider =
        CONTAINING_RECORD(Interface, UDWM_CHANNEL_PROVIDER, IUnknown_iface);
    LONG References = InterlockedDecrement(&Provider->References);

    /* The provider is process-lifetime storage. */
    if (References < 1)
    {
        Provider->References = 1;
        References = 1;
    }
    return (ULONG)References;
}

static IUnknownVtbl g_UdwmChannelProviderVtbl =
{
    UdwmChannelProvider_QueryInterface,
    UdwmChannelProvider_AddRef,
    UdwmChannelProvider_Release
};

static UDWM_CHANNEL_PROVIDER g_UdwmChannelProvider =
{
    {&g_UdwmChannelProviderVtbl},
    1
};

BOOL WINAPI
DllMain(HINSTANCE Instance, DWORD Reason, LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Reserved);

    if (Reason == DLL_PROCESS_DETACH)
    {
        g_ApplicationHost = NULL;
        g_Channel = NULL;
        g_CursorController = NULL;
        g_ComposedEventId = 0;
    }
    return TRUE;
}

HRESULT CDECL
DwmClientStartup(struct IUnknown *ApplicationHost)
{
    IUnknown *HostInterface = NULL;
    IDwmSettingsManager *Settings;
    DWM_APPLICATION_HOST_VTBL *HostVtable;
    PFN_CREATE_CHANNEL CreateChannel;
    PFN_CREATE_CURSOR_CONTROLLER CreateCursorController;
    HMODULE DwmCore;
    DWORD DefaultRemoteAppCreation = 0;
    IDwmChannelPrivate *Channel = NULL;
    IDwmCursorController *CursorController = NULL;
    HRESULT Result;

    if (g_BitmapFactory == NULL)
        return E_UNEXPECTED;
    if (ApplicationHost == NULL)
        return E_INVALIDARG;
    if (g_ApplicationHost != NULL)
        return E_UNEXPECTED;

    Result = MilCompositionEngine_GetComposedEventId(&g_ComposedEventId);
    if (FAILED(Result))
        return Result;

    Result = IUnknown_QueryInterface(ApplicationHost, &IID_IDwmApplicationHost,
                                     (void **)&HostInterface);
    if (FAILED(Result))
        return Result;

    HostVtable = (DWM_APPLICATION_HOST_VTBL *)HostInterface->lpVtbl;
    Settings = HostVtable->GetSettingsManager(HostInterface);
    if (Settings == NULL)
    {
        IUnknown_Release(HostInterface);
        return E_NOINTERFACE;
    }

    /* This is the first preference queried by native Win11 uDWM startup. */
    (void)Settings->lpVtbl->GetPreferenceDword(
        Settings, L"DefaultRemoteAppCreation", &DefaultRemoteAppCreation);

    DwmCore = GetModuleHandleW(L"dwmcore.dll");
    if (DwmCore == NULL)
    {
        IUnknown_Release(HostInterface);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    CreateChannel = (PFN_CREATE_CHANNEL)GetProcAddress(
        DwmCore, "MilCompositionEngine_CreateChannel");
    CreateCursorController = (PFN_CREATE_CURSOR_CONTROLLER)GetProcAddress(
        DwmCore, "MilCompositionEngine_CreateCursorController");
    if (CreateChannel == NULL || CreateCursorController == NULL)
    {
        IUnknown_Release(HostInterface);
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }

    Result = CreateChannel(
        (IDwmChannelProvider *)&g_UdwmChannelProvider.IUnknown_iface,
        &Channel);
    if (FAILED(Result))
    {
        IUnknown_Release(HostInterface);
        return Result;
    }

    Result = CreateCursorController(GetCurrentProcessId(), &CursorController);
    if (FAILED(Result))
    {
        Channel->lpVtbl->Release(Channel);
        IUnknown_Release(HostInterface);
        return Result;
    }

    Result = Channel->lpVtbl->Commit(Channel);
    if (FAILED(Result))
    {
        IUnknown_Release((IUnknown *)CursorController);
        Channel->lpVtbl->Release(Channel);
        IUnknown_Release(HostInterface);
        return Result;
    }

    g_ApplicationHost = HostInterface;
    g_Channel = Channel;
    g_CursorController = CursorController;
    return S_OK;
}
