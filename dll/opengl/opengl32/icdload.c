/*
 * COPYRIGHT:            See COPYING in the top level directory
 * PROJECT:              ReactOS kernel
 * FILE:                 lib/opengl32/icdload.c
 * PURPOSE:              OpenGL32 lib, ICD dll loader
 */

#include "opengl32.h"

#include <d3dkmthk.h>
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

/*
 * D3DKMT function pointers resolved dynamically from gdi32.dll.
 * Windows opengl32 does NOT statically import these -- it uses
 * GetProcAddress so the DLL can load on XPDM-only systems where
 * gdi32 does not export the D3DKMT entry points.
 */
typedef NTSTATUS (APIENTRY *PFN_D3DKMTOpenAdapterFromHdc)(D3DKMT_OPENADAPTERFROMHDC*);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTQueryAdapterInfo)(CONST D3DKMT_QUERYADAPTERINFO*);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTCloseAdapter)(CONST D3DKMT_CLOSEADAPTER*);

static PFN_D3DKMTOpenAdapterFromHdc pfnOpenAdapterFromHdc;
static PFN_D3DKMTQueryAdapterInfo   pfnQueryAdapterInfo;
static PFN_D3DKMTCloseAdapter       pfnCloseAdapter;
static BOOL D3dKmtResolved;

static CRITICAL_SECTION icdload_cs = {NULL, -1, 0, 0, 0, 0};
static struct ICD_Data* ICD_Data_List = NULL;
static const WCHAR OpenGLDrivers_Key[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\OpenGLDrivers";
static const WCHAR CustomDrivers_Key[] = L"SOFTWARE\\ReactOS\\OpenGL";
static Drv_Opengl_Info CustomDrvInfo;
static CUSTOM_DRIVER_STATE CustomDriverState = OGL_CD_NOT_QUERIED;
static LONG WddmPresentUnsupportedReported;

/* GDI entry points (win32k) */
extern INT APIENTRY GdiDescribePixelFormat(HDC hdc, INT ipfd, UINT cjpfd, PPIXELFORMATDESCRIPTOR ppfd);
extern BOOL APIENTRY GdiSetPixelFormat(HDC hdc, INT ipfd);
extern BOOL APIENTRY GdiSwapBuffers(HDC hdc);

/* Resolve D3DKMT functions from gdi32.dll once. Returns TRUE if all three are present. */
static BOOL IntResolveD3dKmt(void)
{
    HMODULE hGdi32;

    if (D3dKmtResolved)
        return (pfnOpenAdapterFromHdc != NULL);

    D3dKmtResolved = TRUE;

    hGdi32 = GetModuleHandleW(L"gdi32.dll");
    if (!hGdi32)
        return FALSE;

    pfnOpenAdapterFromHdc = (PFN_D3DKMTOpenAdapterFromHdc)GetProcAddress(hGdi32, "D3DKMTOpenAdapterFromHdc");
    pfnQueryAdapterInfo   = (PFN_D3DKMTQueryAdapterInfo)GetProcAddress(hGdi32, "D3DKMTQueryAdapterInfo");
    pfnCloseAdapter       = (PFN_D3DKMTCloseAdapter)GetProcAddress(hGdi32, "D3DKMTCloseAdapter");

    if (!pfnOpenAdapterFromHdc || !pfnQueryAdapterInfo || !pfnCloseAdapter)
    {
        TRACE("gdi32.dll does not export D3DKMT functions -- WDDM path unavailable\n");
        pfnOpenAdapterFromHdc = NULL;
        pfnQueryAdapterInfo   = NULL;
        pfnCloseAdapter       = NULL;
        return FALSE;
    }

    TRACE("D3DKMT functions resolved from gdi32.dll\n");
    return TRUE;
}

static BOOL
IntGetWddmIcdInfo(
    HDC hdc,
    pDrv_Opengl_Info pDrvInfo,
    LPWSTR DllName,
    DWORD DllNameCount)
{
    D3DKMT_OPENADAPTERFROMHDC OpenAdapter;
    D3DKMT_QUERYADAPTERINFO QueryInfo;
    D3DKMT_OPENGLINFO OpenGlInfo;
    D3DKMT_CLOSEADAPTER CloseAdapter;
    NTSTATUS Status;
    BOOL Result = FALSE;

    if (!hdc || !pDrvInfo || !DllName || !DllNameCount)
        return FALSE;

    if (!IntResolveD3dKmt())
        return FALSE;

    memset(&OpenAdapter, 0, sizeof(OpenAdapter));
    OpenAdapter.hDc = hdc;

    Status = pfnOpenAdapterFromHdc(&OpenAdapter);
    if (Status != 0)
        return FALSE;

    memset(&OpenGlInfo, 0, sizeof(OpenGlInfo));
    memset(&QueryInfo, 0, sizeof(QueryInfo));
    QueryInfo.hAdapter = OpenAdapter.hAdapter;
    QueryInfo.Type = KMTQAITYPE_UMOPENGLINFO;
    QueryInfo.pPrivateDriverData = &OpenGlInfo;
    QueryInfo.PrivateDriverDataSize = sizeof(OpenGlInfo);

    Status = pfnQueryAdapterInfo(&QueryInfo);
    if (Status == 0 && OpenGlInfo.UmdOpenGlIcdFileName[0] != L'\0')
    {
        memset(pDrvInfo, 0, sizeof(*pDrvInfo));
        pDrvInfo->Version = OpenGlInfo.Version;
        pDrvInfo->DriverVersion = OpenGlInfo.Version;

        lstrcpynW(DllName, OpenGlInfo.UmdOpenGlIcdFileName, DllNameCount);
        lstrcpynW(pDrvInfo->DriverName, DllName, ARRAYSIZE(pDrvInfo->DriverName));
        TRACE("WDDM ICD query returned %S (Version=%lu Flags=%lu)\n",
              DllName, OpenGlInfo.Version, OpenGlInfo.Flags);
        Result = TRUE;
    }

    memset(&CloseAdapter, 0, sizeof(CloseAdapter));
    CloseAdapter.hAdapter = OpenAdapter.hAdapter;
    pfnCloseAdapter(&CloseAdapter);
    return Result;
}

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

static BOOL APIENTRY wglPresentBuffers(HDC hdc, LPOPENGL32_PRESENTBUFFERSCB pData)
{
    if (!hdc || !pData)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    /*
     * WDDM ICDs call this callback from DrvSwapBuffers after completing the
     * GL work.  The callback contract available here carries only adapter,
     * private driver data, and the update rectangle; it does not carry a
     * D3DKMT allocation/device/context tuple that ReactOS can present itself.
     *
     * Windows opengl32 accepts this callback as part of the WDDM ICD contract.
     * Do the same for now so WDDM ICD swaps do not fail solely because this
     * glue point exists.  Calling back into NtGdiSwapBuffers from here is not
     * valid: the ICD reaches this path with win32k user locking already active.
     */
    if (InterlockedCompareExchange(&WddmPresentUnsupportedReported, 1, 0) == 0)
    {
        TRACE("WDDM OpenGL PresentBuffers accepted "
              "(nVersion=%u syncType=%u luid=%08lx:%08lx rect=%ld,%ld-%ld,%ld)\n",
              pData->nVersion,
              pData->syncType,
              pData->luidAdapter.HighPart,
              pData->luidAdapter.LowPart,
              pData->updateRect.left,
              pData->updateRect.top,
              pData->updateRect.right,
              pData->updateRect.bottom);
    }

    return TRUE;
}

static VOID APIENTRY wglGetAdapterLuid(HDC hdc, LUID *pLuid)
{
    D3DKMT_OPENADAPTERFROMHDC OpenAdapter;
    D3DKMT_CLOSEADAPTER CloseAdapter;
    NTSTATUS Status;

    if (pLuid == NULL)
        return;

    RtlZeroMemory(pLuid, sizeof(*pLuid));

    if (hdc == NULL)
        return;

    if (!IntResolveD3dKmt())
        return;

    RtlZeroMemory(&OpenAdapter, sizeof(OpenAdapter));
    OpenAdapter.hDc = hdc;

    Status = pfnOpenAdapterFromHdc(&OpenAdapter);
    if (Status != 0)
        return;

    *pLuid = OpenAdapter.AdapterLuid;

    RtlZeroMemory(&CloseAdapter, sizeof(CloseAdapter));
    CloseAdapter.hAdapter = OpenAdapter.hAdapter;
    pfnCloseAdapter(&CloseAdapter);
}

/* Retrieves the ICD data (driver version + relevant DLL entry points) for a device context */
struct ICD_Data* IntGetIcdData(HDC hdc)
{
    int ret;
    DWORD dwInput, dwValueType, Version, DriverVersion, Flags;
    Drv_Opengl_Info DrvInfo;
    pDrv_Opengl_Info pDrvInfo;
    struct ICD_Data* data;
    HKEY OglKey = NULL;
    HKEY DrvKey, CustomKey;
    WCHAR DllName[MAX_PATH + 1];
    BOOL DirectDllName = FALSE;
    BOOL WddmIcd = FALSE;
    BOOL (WINAPI *DrvValidateVersion)(DWORD);

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
        /*
         * WDDM ICDs are discovered through D3DKMTQueryAdapterInfo, while the
         * old XPDM path still uses OPENGL_GETINFO. Prefer the WDDM contract
         * first and fall back to the legacy escape only if it is missing.
         */
        if (IntGetWddmIcdInfo(hdc, &DrvInfo, DllName, ARRAYSIZE(DllName)))
        {
            WddmIcd = TRUE;
            DirectDllName = TRUE;
            pDrvInfo = &DrvInfo;
        }
        else
        {
            /* First, see if the driver supports this */
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

            pDrvInfo = &DrvInfo;
        }
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

    if (!DirectDllName)
    {
        /* It was still not loaded, look for it in the registry */
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
    else
    {
        Version = pDrvInfo->Version;
        DriverVersion = pDrvInfo->DriverVersion;
        Flags = 0;
        TRACE("Using WDDM ICD path %S directly.\n", DllName);
    }

    /* So far so good, allocate data */
    data = HeapAlloc(GetProcessHeap(), 0, sizeof(*data));
    if(!data)
    {
        ERR("Unable to allocate ICD data!\n");
        goto end;
    }

    memset(data, 0, sizeof(*data));
    data->IsWddmIcd = WddmIcd;

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
#undef DRV_LOAD

    /* Optional entry points -- not all ICDs export these */
    data->DrvPresentBuffers = (void*)GetProcAddress(data->hModule, "DrvPresentBuffers");
    data->DrvSwapMultipleBuffers = (void*)GetProcAddress(data->hModule, "DrvSwapMultipleBuffers");
    data->DrvSetCallbackProcs = (void*)GetProcAddress(data->hModule, "DrvSetCallbackProcs");

    if (data->IsWddmIcd && !data->DrvPresentBuffers)
    {
        WARN("%S lacks DrvPresentBuffers for the WDDM path\n", DllName);
    }

    /* Pass the callbacks to the ICD */
    if(data->DrvSetCallbackProcs)
    {
        if (data->IsWddmIcd)
        {
            PROC callbacks[] = {
                (PROC)wglSetCurrentValue,
                (PROC)wglGetCurrentValue,
                (PROC)wglGetDHGLRC,
                (PROC)NULL,
                (PROC)wglPresentBuffers,
                (PROC)wglGetAdapterLuid};
            data->DrvSetCallbackProcs(ARRAYSIZE(callbacks), callbacks);
        }
        else
        {
            PROC callbacks[] = {
                (PROC)wglSetCurrentValue,
                (PROC)wglGetCurrentValue,
                (PROC)wglGetDHGLRC};
            data->DrvSetCallbackProcs(ARRAYSIZE(callbacks), callbacks);
        }
    }

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
