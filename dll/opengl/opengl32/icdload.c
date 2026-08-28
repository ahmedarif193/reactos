/*
 * COPYRIGHT:            See COPYING in the top level directory
 * PROJECT:              ReactOS kernel
 * FILE:                 lib/opengl32/icdload.c
 * PURPOSE:              OpenGL32 lib, ICD dll loader
 */

#include "opengl32.h"

#include <d3dkmthk.h>
#include <reactos/dwmframe.h>
#include <winreg.h>

WINE_DEFAULT_DEBUG_CHANNEL(opengl32);

/* based off https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/gallium/frontends/wgl/gldrv.h */
typedef struct
{
    ULONG Version;                    /*!< Driver interface version */
    ULONG DriverVersion;              /*!< Driver version */
    WCHAR DriverName[MAX_PATH + 1];   /*!< Driver name */
} Drv_Opengl_Info, *pDrv_Opengl_Info;

#ifndef OPENGL_GETINFO_DRVNAME
#define OPENGL_GETINFO_DRVNAME 0
#endif

typedef enum
{
    OGL_CD_NOT_QUERIED,
    OGL_CD_NONE,
    OGL_CD_ROSSWI,
    OGL_CD_CUSTOM_ICD
} CUSTOM_DRIVER_STATE;

static CRITICAL_SECTION icdload_cs = {NULL, -1, 0, 0, 0, 0};
static struct ICD_Data* ICD_Data_List = NULL;
static const WCHAR OpenGLDrivers_Key[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\OpenGLDrivers";
static const WCHAR CustomDrivers_Key[] = L"SOFTWARE\\ReactOS\\OpenGL";
static Drv_Opengl_Info CustomDrvInfo;
static CUSTOM_DRIVER_STATE CustomDriverState = OGL_CD_NOT_QUERIED;

static void APIENTRY wglSetCurrentValue(PVOID value)
{
    IntSetCurrentICDPrivate(value);
}

static PVOID APIENTRY wglGetCurrentValue()
{
    return IntGetCurrentICDPrivate();
}

static DHGLRC APIENTRY wglGetDHGLRC(struct wgl_context* context)
{
    return context->dhglrc;
}

typedef HRESULT (WINAPI *PFN_DWM_DX_GET_WINDOW_SHARED_SURFACE)(
    HWND, LUID, HMONITOR, DWORD, UINT *, HANDLE *, ULONGLONG *);
typedef HRESULT (WINAPI *PFN_DWM_DX_UPDATE_WINDOW_SHARED_SURFACE)(
    HWND, ULONGLONG, DWORD, HMONITOR, const RECT *);

static INIT_ONCE DwmDxInitOnce = INIT_ONCE_STATIC_INIT;
static PFN_DWM_DX_GET_WINDOW_SHARED_SURFACE DwmDxGetWindowSharedSurface;
static PFN_DWM_DX_UPDATE_WINDOW_SHARED_SURFACE DwmDxUpdateWindowSharedSurface;
static LONG DwmDxPresentFailureLogged;

typedef struct _WGL_ASYNC_PRESENT
{
    HWND Window;
    HANDLE CompletionEvent;
    ULONGLONG UpdateId;
    DWORD Flags;
    RECT UpdateRect;
} WGL_ASYNC_PRESENT, *PWGL_ASYNC_PRESENT;

static VOID
IntReportDwmDxPresentFailure(const char *Stage, HRESULT Result)
{
    if (InterlockedCompareExchange(&DwmDxPresentFailureLogged, 1, 0) == 0)
    {
        ERR("DWM shared-surface present failed at %s, hr=%#lx, error=%lu\n",
            Stage, (ULONG)Result, GetLastError());
    }
}

static BOOL
IntPublishDwmDxPresent(PWGL_ASYNC_PRESENT Present)
{
    HRESULT Result;
    DWORD WaitResult;

    WaitResult = WaitForSingleObject(Present->CompletionEvent, INFINITE);
    if (WaitResult != WAIT_OBJECT_0)
    {
        IntReportDwmDxPresentFailure("completion_wait",
                                     HRESULT_FROM_WIN32(GetLastError()));
        Result = E_FAIL;
    }
    else
    {
        Result = DwmDxUpdateWindowSharedSurface(Present->Window,
                                                 Present->UpdateId,
                                                 Present->Flags,
                                                 NULL,
                                                 &Present->UpdateRect);
        if (FAILED(Result))
            IntReportDwmDxPresentFailure("publish", Result);
    }

    if (FAILED(Result))
    {
        (void)DwmDxUpdateWindowSharedSurface(Present->Window,
                                             Present->UpdateId,
                                             DWM_DX_UPDATE_CANCEL,
                                             NULL,
                                             NULL);
    }
    CloseHandle(Present->CompletionEvent);
    HeapFree(GetProcessHeap(), 0, Present);
    return SUCCEEDED(Result);
}

static DWORD WINAPI
IntPublishDwmDxPresentWorker(PVOID Parameter)
{
    (void)IntPublishDwmDxPresent((PWGL_ASYNC_PRESENT)Parameter);
    return 0;
}

static NTSTATUS
IntOpenAdapterFromWindowMonitor(
    HDC hdc,
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME *OpenAdapter)
{
    MONITORINFOEXW MonitorInfo;
    HMONITOR Monitor;
    HWND Window;

    Window = WindowFromDC(hdc);
    if (Window == NULL)
        return STATUS_INVALID_HANDLE;

    Monitor = MonitorFromWindow(Window, MONITOR_DEFAULTTONEAREST);
    if (Monitor == NULL)
        return STATUS_NOT_FOUND;

    RtlZeroMemory(&MonitorInfo, sizeof(MonitorInfo));
    MonitorInfo.cbSize = sizeof(MonitorInfo);
    if (!GetMonitorInfoW(Monitor, (MONITORINFO *)&MonitorInfo) ||
        MonitorInfo.szDevice[0] == UNICODE_NULL)
    {
        return STATUS_NOT_FOUND;
    }

    RtlZeroMemory(OpenAdapter, sizeof(*OpenAdapter));
    lstrcpynW(OpenAdapter->DeviceName, MonitorInfo.szDevice,
              ARRAYSIZE(OpenAdapter->DeviceName));
    return D3DKMTOpenAdapterFromGdiDisplayName(OpenAdapter);
}

static VOID
IntCloseAdapter(D3DKMT_HANDLE Adapter)
{
    D3DKMT_CLOSEADAPTER CloseAdapter;

    if (Adapter == 0)
        return;

    RtlZeroMemory(&CloseAdapter, sizeof(CloseAdapter));
    CloseAdapter.hAdapter = Adapter;
    (void)D3DKMTCloseAdapter(&CloseAdapter);
}

static BOOL CALLBACK
IntLoadDwmDxCallbacks(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *Context)
{
    HMODULE Module;

    UNREFERENCED_PARAMETER(InitOnce);
    UNREFERENCED_PARAMETER(Parameter);
    UNREFERENCED_PARAMETER(Context);

    Module = LoadLibraryW(L"dwmapi.dll");
    if (Module == NULL)
        return TRUE;

    DwmDxGetWindowSharedSurface =
        (PFN_DWM_DX_GET_WINDOW_SHARED_SURFACE)
            GetProcAddress(Module, (LPCSTR)(ULONG_PTR)100);
    DwmDxUpdateWindowSharedSurface =
        (PFN_DWM_DX_UPDATE_WINDOW_SHARED_SURFACE)
            GetProcAddress(Module, (LPCSTR)(ULONG_PTR)101);
    return TRUE;
}

static VOID APIENTRY
wglGetAdapterLuid(HDC hdc, LUID *AdapterLuid)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME OpenDisplayAdapter;
    D3DKMT_OPENADAPTERFROMHDC OpenAdapter;
    NTSTATUS Status;

    if (AdapterLuid == NULL)
        return;
    AdapterLuid->LowPart = 0;
    AdapterLuid->HighPart = 0;
    if (hdc == NULL)
        return;

    Status = IntOpenAdapterFromWindowMonitor(hdc, &OpenDisplayAdapter);
    if (NT_SUCCESS(Status) && OpenDisplayAdapter.hAdapter != 0)
    {
        *AdapterLuid = OpenDisplayAdapter.AdapterLuid;
        IntCloseAdapter(OpenDisplayAdapter.hAdapter);
        return;
    }

    RtlZeroMemory(&OpenAdapter, sizeof(OpenAdapter));
    OpenAdapter.hDc = hdc;
    if (NT_SUCCESS(D3DKMTOpenAdapterFromHdc(&OpenAdapter)) &&
        OpenAdapter.hAdapter != 0)
    {
        *AdapterLuid = OpenAdapter.AdapterLuid;
        IntCloseAdapter(OpenAdapter.hAdapter);
    }
}

static BOOL APIENTRY
wglPresentBuffers(HDC hdc, WGL_PRESENTBUFFERS_CB *CallbackData)
{
    struct ICD_Data *IcdData;
    PWGL_ASYNC_PRESENT AsyncPresent = NULL;
    WGL_PRESENTBUFFERS PresentData;
    HWND Window;
    HANDLE SharedSurface = NULL;
    ULONGLONG UpdateId = 0;
    UINT Format = 0;
    HRESULT Result;

    if (hdc == NULL || CallbackData == NULL ||
        (CallbackData->Version != 2 && CallbackData->Version != 3) ||
        CallbackData->SyncType > 1)
    {
        return FALSE;
    }

    Window = WindowFromDC(hdc);
    if (Window == NULL)
        return FALSE;

    (void)InitOnceExecuteOnce(&DwmDxInitOnce, IntLoadDwmDxCallbacks,
                              NULL, NULL);
    if (DwmDxGetWindowSharedSurface == NULL ||
        DwmDxUpdateWindowSharedSurface == NULL)
    {
        IntReportDwmDxPresentFailure("load_callbacks", E_NOINTERFACE);
        return FALSE;
    }

    Result = DwmDxGetWindowSharedSurface(Window,
                                         CallbackData->AdapterLuid,
                                         NULL,
                                         CallbackData->SyncType,
                                         &Format,
                                         &SharedSurface,
                                         &UpdateId);
    if (Result == S_FALSE)
        return TRUE; /* compositor intentionally dropped this immediate frame */
    if (FAILED(Result))
    {
        IntReportDwmDxPresentFailure("get_surface", Result);
        return FALSE;
    }
    if (SharedSurface == NULL || UpdateId == 0)
    {
        IntReportDwmDxPresentFailure("get_surface_contract", E_UNEXPECTED);
        return FALSE;
    }

    IcdData = IntGetIcdData(hdc);
    if (IcdData == NULL || IcdData->DrvPresentBuffers == NULL)
    {
        IntReportDwmDxPresentFailure("icd_callback", E_NOINTERFACE);
        goto Cancel;
    }

    RtlZeroMemory(&PresentData, sizeof(PresentData));
    PresentData.hSurface = SharedSurface;
    PresentData.AdapterLuid = CallbackData->AdapterLuid;
    PresentData.PresentToken = UpdateId;
    PresentData.PrivateData = CallbackData->PrivateData;
    PresentData.Version = CallbackData->Version;
    if (CallbackData->Version >= 3)
    {
        AsyncPresent = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                 sizeof(*AsyncPresent));
        if (AsyncPresent == NULL)
        {
            IntReportDwmDxPresentFailure("completion_alloc", E_OUTOFMEMORY);
            goto Cancel;
        }
        AsyncPresent->CompletionEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (AsyncPresent->CompletionEvent == NULL)
        {
            IntReportDwmDxPresentFailure("completion_event",
                                         HRESULT_FROM_WIN32(GetLastError()));
            HeapFree(GetProcessHeap(), 0, AsyncPresent);
            AsyncPresent = NULL;
            goto Cancel;
        }
        AsyncPresent->Window = Window;
        AsyncPresent->UpdateId = UpdateId;
        AsyncPresent->Flags = CallbackData->SyncType;
        AsyncPresent->UpdateRect = CallbackData->UpdateRect;
        PresentData.CompletionEvent = AsyncPresent->CompletionEvent;
    }
    if (!IcdData->DrvPresentBuffers(hdc, &PresentData))
    {
        IntReportDwmDxPresentFailure("icd_present", E_FAIL);
        goto Cancel;
    }

    if (AsyncPresent != NULL)
    {
        if (!QueueUserWorkItem(IntPublishDwmDxPresentWorker, AsyncPresent,
                               WT_EXECUTELONGFUNCTION))
        {
            return IntPublishDwmDxPresent(AsyncPresent);
        }
        return TRUE;
    }

    Result = DwmDxUpdateWindowSharedSurface(Window,
                                             UpdateId,
                                             CallbackData->SyncType,
                                             NULL,
                                             &CallbackData->UpdateRect);
    if (SUCCEEDED(Result))
        return TRUE;

    IntReportDwmDxPresentFailure("publish", Result);

Cancel:
    if (AsyncPresent != NULL)
    {
        if (AsyncPresent->CompletionEvent != NULL)
            CloseHandle(AsyncPresent->CompletionEvent);
        HeapFree(GetProcessHeap(), 0, AsyncPresent);
    }
    (void)DwmDxUpdateWindowSharedSurface(Window,
                                         UpdateId,
                                         DWM_DX_UPDATE_CANCEL,
                                         NULL,
                                         NULL);
    return FALSE;
}

/*
 * WDDM display miniports publish their OpenGL ICD through the adapter's
 * software key.  Querying it through D3DKMT keeps the choice tied to the HDC
 * instead of applying a process-wide registry override, which is important
 * when a hardware miniport fails and BasicDisplay owns the desktop instead.
 */
static BOOL
IntGetWddmIcdInfo(
    HDC hdc,
    Drv_Opengl_Info *DrvInfo,
    WCHAR DllName[MAX_PATH],
    DWORD *Flags)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME OpenDisplayAdapter;
    D3DKMT_OPENADAPTERFROMHDC OpenAdapter;
    D3DKMT_QUERYADAPTERINFO QueryInfo;
    D3DKMT_OPENGLINFO OpenGlInfo;
    D3DKMT_HANDLE Adapter = 0;
    NTSTATUS Status;

    if (!hdc || !DrvInfo || !DllName || !Flags)
        return FALSE;

    memset(&OpenAdapter, 0, sizeof(OpenAdapter));
    OpenAdapter.hDc = hdc;
    Status = D3DKMTOpenAdapterFromHdc(&OpenAdapter);
    if (NT_SUCCESS(Status) && OpenAdapter.hAdapter != 0)
        Adapter = OpenAdapter.hAdapter;

    memset(&OpenGlInfo, 0, sizeof(OpenGlInfo));
    memset(&QueryInfo, 0, sizeof(QueryInfo));
    QueryInfo.hAdapter = Adapter;
    QueryInfo.Type = KMTQAITYPE_UMOPENGLINFO;
    QueryInfo.pPrivateDriverData = &OpenGlInfo;
    QueryInfo.PrivateDriverDataSize = sizeof(OpenGlInfo);
    Status = Adapter != 0 ?
        D3DKMTQueryAdapterInfo(&QueryInfo) : STATUS_INVALID_HANDLE;

    IntCloseAdapter(Adapter);

    /* A redirected window DC is backed by a DIB, not by the scan-out PDEV.
     * Resolve that window's monitor when the HDC-selected adapter does not
     * publish an ICD, preserving the normal HDC path for direct display DCs. */
    if (!NT_SUCCESS(Status) || !OpenGlInfo.UmdOpenGlIcdFileName[0])
    {
        Status = IntOpenAdapterFromWindowMonitor(hdc, &OpenDisplayAdapter);
        if (!NT_SUCCESS(Status) || OpenDisplayAdapter.hAdapter == 0)
            return FALSE;

        memset(&OpenGlInfo, 0, sizeof(OpenGlInfo));
        QueryInfo.hAdapter = OpenDisplayAdapter.hAdapter;
        Status = D3DKMTQueryAdapterInfo(&QueryInfo);
        IntCloseAdapter(OpenDisplayAdapter.hAdapter);
    }

    OpenGlInfo.UmdOpenGlIcdFileName[ARRAYSIZE(OpenGlInfo.UmdOpenGlIcdFileName) - 1] = UNICODE_NULL;
    if (!NT_SUCCESS(Status) || !OpenGlInfo.UmdOpenGlIcdFileName[0])
        return FALSE;

    lstrcpynW(DllName, OpenGlInfo.UmdOpenGlIcdFileName, MAX_PATH);
    DrvInfo->Version = OpenGlInfo.Version;
    /* WDDM's query returns one ICD interface version.  The legacy loader's
     * validation callback consumes that value through DriverVersion. */
    DrvInfo->DriverVersion = OpenGlInfo.Version;
    lstrcpynW(DrvInfo->DriverName, OpenGlInfo.UmdOpenGlIcdFileName,
              ARRAYSIZE(DrvInfo->DriverName));
    *Flags = OpenGlInfo.Flags;
    return TRUE;
}

/* GDI entry points (win32k) */
extern INT APIENTRY GdiDescribePixelFormat(HDC hdc, INT ipfd, UINT cjpfd, PPIXELFORMATDESCRIPTOR ppfd);
extern BOOL APIENTRY GdiSetPixelFormat(HDC hdc, INT ipfd);
extern BOOL APIENTRY GdiSwapBuffers(HDC hdc);

/* Retrieves the ICD data (driver version + relevant DLL entry points) for a device context */
struct ICD_Data* IntGetIcdData(HDC hdc)
{
    int ret;
    DWORD dwInput, dwValueType, Version, DriverVersion, Flags;
    Drv_Opengl_Info DrvInfo;
    pDrv_Opengl_Info pDrvInfo;
    struct ICD_Data* data;
    HKEY OglKey = NULL;
    HKEY DrvKey = NULL, CustomKey = NULL;
    WCHAR DllName[MAX_PATH];
    WCHAR WddmDllName[MAX_PATH];
    DWORD WddmFlags = 0;
    BOOL WddmIcd = FALSE;
    BOOL (WINAPI *DrvValidateVersion)(DWORD);
    void (WINAPI *DrvSetCallbackProcs)(int nProcs, PROC* pProcs);

    /* The following code is ReactOS specific and allows us to easily load an arbitrary ICD:
     * It checks HKCU\Software\ReactOS\OpenGL for a custom ICD and will always load it
     * no matter what driver the DC is associated with. It can also force using the
     * built-in Software Implementation*/
    if(CustomDriverState == OGL_CD_NOT_QUERIED)
    {
        /* Only do this once so there's not any significant performance penalty */
        CustomDriverState = OGL_CD_NONE;
        memset(&CustomDrvInfo, 0, sizeof(Drv_Opengl_Info));

        ret = RegOpenKeyExW(HKEY_CURRENT_USER, CustomDrivers_Key, 0, KEY_READ, &CustomKey);
        if(ret != ERROR_SUCCESS)
            goto custom_end;

        dwInput = sizeof(CustomDrvInfo.DriverName);
        ret = RegQueryValueExW(CustomKey, L"", 0, &dwValueType, (LPBYTE)CustomDrvInfo.DriverName, &dwInput);
        RegCloseKey(CustomKey);

        if((ret != ERROR_SUCCESS) || (dwValueType != REG_SZ) || !wcslen(CustomDrvInfo.DriverName))
            goto custom_end;

        if(!_wcsicmp(CustomDrvInfo.DriverName, L"ReactOS Software Implementation"))
        {
            /* Always announce the fact that we're forcing ROSSWI */
            ERR("Forcing ReactOS Software Implementation\n");
            CustomDriverState = OGL_CD_ROSSWI;
            return NULL;
        }

        ret = RegOpenKeyExW(HKEY_LOCAL_MACHINE, OpenGLDrivers_Key, 0, KEY_READ, &OglKey);
        if(ret != ERROR_SUCCESS)
            goto custom_end;

        ret = RegOpenKeyExW(OglKey, CustomDrvInfo.DriverName, 0, KEY_READ, &OglKey);
        if(ret != ERROR_SUCCESS)
            goto custom_end;

        dwInput = sizeof(CustomDrvInfo.Version);
        ret = RegQueryValueExW(OglKey, L"Version", 0, &dwValueType, (LPBYTE)&CustomDrvInfo.Version, &dwInput);
        if((ret != ERROR_SUCCESS) || (dwValueType != REG_DWORD))
            goto custom_end;

        dwInput = sizeof(DriverVersion);
        ret = RegQueryValueExW(OglKey, L"DriverVersion", 0, &dwValueType, (LPBYTE)&CustomDrvInfo.DriverVersion, &dwInput);
        CustomDriverState = OGL_CD_CUSTOM_ICD;

        /* Always announce the fact that we're overriding the default driver */
        ERR("Overriding the default OGL ICD with %S\n", CustomDrvInfo.DriverName);

custom_end:
        if(OglKey)
            RegCloseKey(OglKey);
        if(CustomKey)
            RegCloseKey(CustomKey);
    }

    /* If there's a custom ICD or ROSSWI was requested use it, otherwise proceed as usual */
    if(CustomDriverState == OGL_CD_CUSTOM_ICD)
    {
        pDrvInfo = &CustomDrvInfo;
    }
    else if(CustomDriverState == OGL_CD_ROSSWI)
    {
        return NULL;
    }
    else
    {
        memset(&DrvInfo, 0, sizeof(DrvInfo));
        memset(WddmDllName, 0, sizeof(WddmDllName));
        WddmIcd = IntGetWddmIcdInfo(hdc, &DrvInfo, WddmDllName, &WddmFlags);
        if (!WddmIcd)
        {
            /* XPDM ICD discovery through the display driver's escape. */
            dwInput = OPENGL_GETINFO;
            ret = ExtEscape(hdc, QUERYESCSUPPORT, sizeof(DWORD), (LPCSTR)&dwInput, 0, NULL);

            /* Driver doesn't support opengl */
            if(ret <= 0)
                return NULL;

            /* Query for the ICD DLL name and version */
            dwInput = OPENGL_GETINFO_DRVNAME;
            ret = ExtEscape(hdc, OPENGL_GETINFO, sizeof(DWORD), (LPCSTR)&dwInput, sizeof(DrvInfo), (LPSTR)&DrvInfo);

            if(ret <= 0)
            {
                ERR("Driver claims to support OPENGL_GETINFO escape code, but doesn't. ret: %X\n", ret);
                return NULL;
            }
        }

        pDrvInfo = &DrvInfo;
    }

    /* Protect the list while we are loading*/
    EnterCriticalSection(&icdload_cs);

    /* Search for it in the list of already loaded modules */
    data = ICD_Data_List;
    while(data)
    {
        if(!_wcsicmp(data->DriverName, pDrvInfo->DriverName))
        {
            /* Found it */
            TRACE("Found already loaded %p.\n", data);
            LeaveCriticalSection(&icdload_cs);
            return data;
        }
        data = data->next;
    }

    if (WddmIcd)
    {
        lstrcpynW(DllName, WddmDllName, ARRAYSIZE(DllName));
        Version = DriverVersion = pDrvInfo->Version;
        Flags = WddmFlags;
        TRACE("WDDM ICD is %S, Version %lx, Flags %lx.\n",
              DllName, Version, Flags);
    }
    else
    {
        /* It was still not loaded, look for it in the legacy ICD registry. */
        ret = RegOpenKeyExW(HKEY_LOCAL_MACHINE, OpenGLDrivers_Key, 0, KEY_READ, &OglKey);
        if(ret != ERROR_SUCCESS)
        {
            ERR("Failed to open the OpenGLDrivers key.\n");
            goto end;
        }
        ret = RegOpenKeyExW(OglKey, pDrvInfo->DriverName, 0, KEY_READ, &DrvKey);
        if(ret != ERROR_SUCCESS)
        {
            /* Some driver installer just provide the DLL name, like the Matrox G400 */
            TRACE("No driver subkey for %S, trying to get DLL name directly.\n", pDrvInfo->DriverName);
            dwInput = sizeof(DllName);
            ret = RegQueryValueExW(OglKey, pDrvInfo->DriverName, 0, &dwValueType, (LPBYTE)DllName, &dwInput);
            if((ret != ERROR_SUCCESS) || (dwValueType != REG_SZ))
            {
                ERR("Unable to get ICD DLL name!\n");
                RegCloseKey(OglKey);
                goto end;
            }
            Version = DriverVersion = Flags = 0;
            TRACE("DLL name is %S.\n", DllName);
        }
        else
        {
            /* The driver have a subkey for the ICD */
            TRACE("Querying details from registry for %S.\n", pDrvInfo->DriverName);
            dwInput = sizeof(DllName);
            ret = RegQueryValueExW(DrvKey, L"Dll", 0, &dwValueType, (LPBYTE)DllName, &dwInput);
            if((ret != ERROR_SUCCESS) || (dwValueType != REG_SZ))
            {
                ERR("Unable to get ICD DLL name!.\n");
                RegCloseKey(DrvKey);
                RegCloseKey(OglKey);
                goto end;
            }

            dwInput = sizeof(Version);
            ret = RegQueryValueExW(DrvKey, L"Version", 0, &dwValueType, (LPBYTE)&Version, &dwInput);
            if((ret != ERROR_SUCCESS) || (dwValueType != REG_DWORD))
            {
                WARN("No version in driver subkey\n");
            }
            else if(Version != pDrvInfo->Version)
            {
                ERR("Version mismatch between registry (%lu) and display driver (%lu).\n", Version, pDrvInfo->Version);
                RegCloseKey(DrvKey);
                RegCloseKey(OglKey);
                goto end;
            }

            dwInput = sizeof(DriverVersion);
            ret = RegQueryValueExW(DrvKey, L"DriverVersion", 0, &dwValueType, (LPBYTE)&DriverVersion, &dwInput);
            if((ret != ERROR_SUCCESS) || (dwValueType != REG_DWORD))
            {
                WARN("No driver version in driver subkey\n");
            }
            else if(DriverVersion != pDrvInfo->DriverVersion)
            {
                ERR("Driver version mismatch between registry (%lu) and display driver (%lu).\n", DriverVersion, pDrvInfo->DriverVersion);
                RegCloseKey(DrvKey);
                RegCloseKey(OglKey);
                goto end;
            }

            dwInput = sizeof(Flags);
            ret = RegQueryValueExW(DrvKey, L"Flags", 0, &dwValueType, (LPBYTE)&Flags, &dwInput);
            if((ret != ERROR_SUCCESS) || (dwValueType != REG_DWORD))
            {
                WARN("No driver version in driver subkey\n");
                Flags = 0;
            }

            /* We're done */
            RegCloseKey(DrvKey);
            TRACE("DLL name is %S, Version %lx, DriverVersion %lx, Flags %lx.\n", DllName, Version, DriverVersion, Flags);
        }
        /* No need for this anymore */
        RegCloseKey(OglKey);
    }

    /* So far so good, allocate data */
    data = HeapAlloc(GetProcessHeap(), 0, sizeof(*data));
    if(!data)
    {
        ERR("Unable to allocate ICD data!\n");
        goto end;
    }

    /* Load the library */
    data->hModule = LoadLibraryW(DllName);
    if(!data->hModule)
    {
        ERR("Could not load the ICD DLL: %S.\n", DllName);
        HeapFree(GetProcessHeap(), 0, data);
        data = NULL;
        goto end;
    }

    /*
     * Validate version, if needed.
     * Some drivers (at least VBOX), initialize stuff upon this call.
     */
    DrvValidateVersion = (void*)GetProcAddress(data->hModule, "DrvValidateVersion");
    if(DrvValidateVersion)
    {
        if(!DrvValidateVersion(pDrvInfo->DriverVersion))
        {
            ERR("DrvValidateVersion failed!.\n");
            goto fail;
        }
    }

    /* Pass the callbacks */
    DrvSetCallbackProcs = (void*)GetProcAddress(data->hModule, "DrvSetCallbackProcs");
    if(DrvSetCallbackProcs)
    {
        PROC callbacks[] = {
            (PROC)wglSetCurrentValue,
            (PROC)wglGetCurrentValue,
            (PROC)wglGetDHGLRC,
            NULL,
            (PROC)wglPresentBuffers,
            (PROC)wglGetAdapterLuid};
        DrvSetCallbackProcs(ARRAYSIZE(callbacks), callbacks);
    }

    /* Get the DLL exports */
#define DRV_LOAD(x) do                                  \
{                                                       \
    data->x = (void*)GetProcAddress(data->hModule, #x); \
    if(!data->x) {                                      \
        ERR("%S lacks " #x "!\n", DllName);             \
        goto fail;                                      \
    }                                                   \
} while(0)
    DRV_LOAD(DrvCopyContext);
    DRV_LOAD(DrvCreateContext);
    DRV_LOAD(DrvCreateLayerContext);
    DRV_LOAD(DrvDeleteContext);
    DRV_LOAD(DrvDescribeLayerPlane);
    DRV_LOAD(DrvDescribePixelFormat);
    DRV_LOAD(DrvGetLayerPaletteEntries);
    DRV_LOAD(DrvGetProcAddress);
    DRV_LOAD(DrvReleaseContext);
    DRV_LOAD(DrvRealizeLayerPalette);
    DRV_LOAD(DrvSetContext);
    DRV_LOAD(DrvSetLayerPaletteEntries);
    DRV_LOAD(DrvSetPixelFormat);
    DRV_LOAD(DrvShareLists);
    DRV_LOAD(DrvSwapBuffers);
    DRV_LOAD(DrvSwapLayerBuffers);
    data->DrvPresentBuffers =
        (void *)GetProcAddress(data->hModule, "DrvPresentBuffers");
#undef DRV_LOAD

    /* Let's see if GDI should handle this instead of the ICD DLL */
    // FIXME: maybe there is a better way
    if (GdiDescribePixelFormat(hdc, 0, 0, NULL) != 0)
    {
        /* GDI knows what to do with that. Override */
        TRACE("Forwarding WGL calls to win32k!\n");
        data->DrvDescribePixelFormat = GdiDescribePixelFormat;
        data->DrvSetPixelFormat = GdiSetPixelFormat;
        data->DrvSwapBuffers = GdiSwapBuffers;
    }

    /* Copy the DriverName */
    wcscpy(data->DriverName, pDrvInfo->DriverName);

    /* Push the list */
    data->next = ICD_Data_List;
    ICD_Data_List = data;

    TRACE("Returning %p.\n", data);
    TRACE("ICD driver %S (%S) successfully loaded.\n", pDrvInfo->DriverName, DllName);

end:
    /* Unlock and return */
    LeaveCriticalSection(&icdload_cs);
    return data;

fail:
    LeaveCriticalSection(&icdload_cs);
    FreeLibrary(data->hModule);
    HeapFree(GetProcessHeap(), 0, data);
    return NULL;
}

void IntDeleteAllICDs(void)
{
    struct ICD_Data* data;

    EnterCriticalSection(&icdload_cs);

    while (ICD_Data_List != NULL)
    {
        data = ICD_Data_List;
        ICD_Data_List = data->next;

        FreeLibrary(data->hModule);
        HeapFree(GetProcessHeap(), 0, data);
    }
}
