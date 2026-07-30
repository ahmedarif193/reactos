/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     End-to-end softgpu UMD linear 2D execution
 */

#include "precomp.h"
#include <d3dumddi.h>

#define UMD2D_WIDTH  64u
#define UMD2D_HEIGHT 64u

typedef HRESULT (WINAPI *PFN_D3DUmdRtCreateDeviceCallbacks)(
    D3DKMT_HANDLE,
    D3DKMT_HANDLE,
    D3DDDI_DEVICECALLBACKS *,
    HANDLE *);
typedef HRESULT (WINAPI *PFN_D3DUmdRtDestroyDeviceCallbacks)(HANDLE);

static BOOL
Umd2DQueryDriverName(
    PFND3DKMT_QUERYADAPTERINFO pQueryAdapterInfo,
    D3DKMT_HANDLE hAdapter,
    WCHAR *Name,
    SIZE_T NameChars)
{
    D3DKMT_QUERYADAPTERINFO Query;
    D3DKMT_UMDFILENAMEINFO Info;
    NTSTATUS Status;

    memset(&Info, 0, sizeof(Info));
    Info.Version = KMTUMDVERSION_DX9;
    memset(&Query, 0, sizeof(Query));
    Query.hAdapter = hAdapter;
    Query.Type = KMTQAITYPE_UMDRIVERNAME;
    Query.pPrivateDriverData = &Info;
    Query.PrivateDriverDataSize = sizeof(Info);

    Status = pQueryAdapterInfo(&Query);
    if (!NT_SUCCESS(Status) ||
        Info.UmdFileName[0] == UNICODE_NULL)
    {
        return FALSE;
    }
    wcsncpy(Name, Info.UmdFileName, NameChars - 1);
    Name[NameChars - 1] = UNICODE_NULL;
    return TRUE;
}

static BOOL
Umd2DIsSoftGpu(
    CONST WCHAR *Name)
{
    CONST WCHAR *Base;
    CONST WCHAR *Cursor;

    Base = Name;
    for (Cursor = Name; Cursor != NULL && *Cursor != UNICODE_NULL;
         ++Cursor)
    {
        if (*Cursor == L'\\' || *Cursor == L'/')
            Base = Cursor + 1;
    }
    return lstrcmpiW(Base, L"softgpuum.dll") == 0;
}

static HRESULT
Umd2DCreateResource(
    CONST D3DDDI_DEVICEFUNCS *Funcs,
    HANDLE hDevice,
    HANDLE hRuntimeResource,
    D3DDDI_RESOURCEFLAGS Flags,
    HANDLE *hResource)
{
    D3DDDI_SURFACEINFO Surface;
    D3DDDIARG_CREATERESOURCE Create;
    HRESULT Result;

    if (Funcs == NULL || Funcs->pfnCreateResource == NULL ||
        hResource == NULL)
    {
        return E_INVALIDARG;
    }

    *hResource = NULL;
    memset(&Surface, 0, sizeof(Surface));
    Surface.Width = UMD2D_WIDTH;
    Surface.Height = UMD2D_HEIGHT;
    Surface.Depth = 1;

    memset(&Create, 0, sizeof(Create));
    Create.Format = D3DDDIFMT_A8R8G8B8;
    Create.Pool = D3DDDIPOOL_VIDEOMEMORY;
    Create.MultisampleType = D3DDDIMULTISAMPLE_NONE;
    Create.pSurfList = &Surface;
    Create.SurfCount = 1;
    Create.MipLevels = 1;
    Create.hResource = hRuntimeResource;
    Create.Flags = Flags;
    Create.Rotation = D3DDDI_ROTATION_IDENTITY;

    Result = Funcs->pfnCreateResource(hDevice, &Create);
    if (SUCCEEDED(Result))
        *hResource = Create.hResource;
    return Result;
}

static HRESULT
Umd2DLockResource(
    CONST D3DDDI_DEVICEFUNCS *Funcs,
    HANDLE hDevice,
    HANDLE hResource,
    BOOL ReadOnly,
    D3DDDIARG_LOCK *Lock)
{
    memset(Lock, 0, sizeof(*Lock));
    Lock->hResource = hResource;
    Lock->Flags.ReadOnly = ReadOnly ? 1 : 0;
    return Funcs->pfnLock(hDevice, Lock);
}

static HRESULT
Umd2DUnlockResource(
    CONST D3DDDI_DEVICEFUNCS *Funcs,
    HANDLE hDevice,
    HANDLE hResource)
{
    D3DDDIARG_UNLOCK Unlock;

    memset(&Unlock, 0, sizeof(Unlock));
    Unlock.hResource = hResource;
    return Funcs->pfnUnlock(hDevice, &Unlock);
}

static BOOL
Umd2DCheckPixels(
    CONST D3DDDIARG_LOCK *Lock,
    UINT Expected)
{
    UINT X;
    UINT Y;

    if (Lock == NULL || Lock->pSurfData == NULL ||
        Lock->Pitch < UMD2D_WIDTH * sizeof(UINT))
    {
        return FALSE;
    }

    for (Y = 0; Y < UMD2D_HEIGHT; ++Y)
    {
        CONST UINT *Row =
            (CONST UINT *)((CONST BYTE *)Lock->pSurfData +
                           (SIZE_T)Y * Lock->Pitch);
        for (X = 0; X < UMD2D_WIDTH; ++X)
        {
            if (Row[X] != Expected)
                return FALSE;
        }
    }
    return TRUE;
}

static void
Umd2DWritePixels(
    CONST D3DDDIARG_LOCK *Lock,
    UINT Value)
{
    UINT X;
    UINT Y;

    for (Y = 0; Y < UMD2D_HEIGHT; ++Y)
    {
        UINT *Row = (UINT *)((BYTE *)Lock->pSurfData +
                             (SIZE_T)Y * Lock->Pitch);
        for (X = 0; X < UMD2D_WIDTH; ++X)
            Row[X] = Value;
    }
}

static NTSTATUS
Umd2DWaitForIdle(
    PFND3DKMT_WAITFORIDLE pWaitForIdle,
    D3DKMT_HANDLE hDevice)
{
    D3DKMT_WAITFORIDLE Wait;

    memset(&Wait, 0, sizeof(Wait));
    Wait.hDevice = hDevice;
    return pWaitForIdle(&Wait);
}

static void
Test_SoftGpu2DEndToEnd(void)
{
    PFND3DKMT_QUERYADAPTERINFO pQueryAdapterInfo;
    PFND3DKMT_WAITFORIDLE pWaitForIdle;
    PFN_D3DUmdRtCreateDeviceCallbacks pCreateCallbacks;
    PFN_D3DUmdRtDestroyDeviceCallbacks pDestroyCallbacks;
    PFND3DDDI_OPENADAPTER pOpenAdapter;
    D3DKMT_HANDLE hAdapter = 0;
    D3DKMT_HANDLE hKmtDevice = 0;
    HMODULE Runtime = NULL;
    HMODULE Umd = NULL;
    WCHAR UmdName[MAX_PATH];
    D3DDDI_DEVICECALLBACKS Callbacks;
    D3DDDI_ADAPTERCALLBACKS AdapterCallbacks;
    D3DDDI_ADAPTERFUNCS AdapterFuncs;
    D3DDDI_DEVICEFUNCS DeviceFuncs;
    D3DDDIARG_OPENADAPTER Open;
    D3DDDIARG_CREATEDEVICE CreateDevice;
    HANDLE hRuntimeDevice = NULL;
    HANDLE hUmdAdapter = NULL;
    HANDLE hUmdDevice = NULL;
    HANDLE hSource = NULL;
    HANDLE hDestination = NULL;
    ULONG_PTR SourceRuntimeCookie = 0x534F5552;
    ULONG_PTR DestinationRuntimeCookie = 0x44455354;
    D3DDDI_RESOURCEFLAGS ResourceFlags;
    D3DDDIARG_LOCK Lock;
    D3DDDIARG_COLORFILL Fill;
    D3DDDIARG_BLT Blt;
    FORMATOP Formats[2];
    D3DDDIARG_GETCAPS Caps;
    UINT FormatCount = 0;
    UINT SourceColor = 0xFF1248A0;
    UINT FillColor = 0xFF70B030;
    BOOL SourceLocked = FALSE;
    BOOL DestinationLocked = FALSE;
    HRESULT Result;
    NTSTATUS Status;
    UINT Index;

    pQueryAdapterInfo = (PFND3DKMT_QUERYADAPTERINFO)
        LoadD3DKMTProc("D3DKMTQueryAdapterInfo");
    pWaitForIdle = (PFND3DKMT_WAITFORIDLE)
        LoadD3DKMTProc("D3DKMTWaitForIdle");
    if (pQueryAdapterInfo == NULL || pWaitForIdle == NULL)
    {
        skip("required D3DKMT query/idle exports are absent\n");
        return;
    }

    hAdapter = OpenAdapterFromDisplay1();
    if (hAdapter == 0)
    {
        skip("No adapter on \\\\.\\DISPLAY1\n");
        return;
    }
    if (!Umd2DQueryDriverName(pQueryAdapterInfo,
                              hAdapter,
                              UmdName,
                              ARRAYSIZE(UmdName)))
    {
        skip("adapter reports no DX9 user-mode driver\n");
        goto Cleanup;
    }
    if (!Umd2DIsSoftGpu(UmdName))
    {
        skip("adapter uses %S, not softgpuum.dll\n", UmdName);
        goto Cleanup;
    }

    hKmtDevice = CreateTestDevice(hAdapter);
    if (hKmtDevice == 0)
    {
        skip("could not create the KMT device\n");
        goto Cleanup;
    }

    Runtime = LoadLibraryW(L"d3dumdrt.dll");
    if (Runtime == NULL)
    {
        skip("d3dumdrt.dll is unavailable\n");
        goto Cleanup;
    }
    pCreateCallbacks = (PFN_D3DUmdRtCreateDeviceCallbacks)
        GetProcAddress(Runtime, "D3DUmdRtCreateDeviceCallbacks");
    pDestroyCallbacks = (PFN_D3DUmdRtDestroyDeviceCallbacks)
        GetProcAddress(Runtime, "D3DUmdRtDestroyDeviceCallbacks");
    if (pCreateCallbacks == NULL || pDestroyCallbacks == NULL)
    {
        skip("d3dumdrt.dll lacks its device callback exports\n");
        goto Cleanup;
    }

    memset(&Callbacks, 0, sizeof(Callbacks));
    Result = pCreateCallbacks(hAdapter,
                              hKmtDevice,
                              &Callbacks,
                              &hRuntimeDevice);
    ok(Result == S_OK,
       "runtime callback device creation failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        goto Cleanup;

    Umd = LoadLibraryW(UmdName);
    ok(Umd != NULL,
       "LoadLibrary(%S) failed, error %lu\n",
       UmdName,
       GetLastError());
    if (Umd == NULL)
        goto Cleanup;
    pOpenAdapter = (PFND3DDDI_OPENADAPTER)
        GetProcAddress(Umd, "OpenAdapter10_2");
    ok(pOpenAdapter != NULL,
       "softgpuum.dll exports no OpenAdapter10_2\n");
    if (pOpenAdapter == NULL)
        goto Cleanup;

    memset(&AdapterCallbacks, 0, sizeof(AdapterCallbacks));
    memset(&AdapterFuncs, 0, sizeof(AdapterFuncs));
    memset(&Open, 0, sizeof(Open));
    Open.hAdapter = (HANDLE)(ULONG_PTR)hAdapter;
    Open.Interface = REACTOS_EXPECTED_UMD_INTERFACE_VERSION;
    Open.Version = REACTOS_EXPECTED_UMD_INTERFACE_VERSION;
    Open.pAdapterCallbacks = &AdapterCallbacks;
    Open.pAdapterFuncs = &AdapterFuncs;
    Result = pOpenAdapter(&Open);
    ok(Result == S_OK,
       "OpenAdapter10_2 failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        goto Cleanup;
    hUmdAdapter = Open.hAdapter;

    if (Open.DriverVersion !=
        REACTOS_EXPECTED_UMD_INTERFACE_VERSION)
    {
        skip("UMD interface 0x%04X does not match image interface "
             "0x%04X\n",
             (unsigned)Open.DriverVersion,
             (unsigned)REACTOS_EXPECTED_UMD_INTERFACE_VERSION);
        goto Cleanup;
    }

    memset(&Caps, 0, sizeof(Caps));
    Caps.Type = D3DDDICAPS_GETFORMATCOUNT;
    Caps.pData = &FormatCount;
    Caps.DataSize = sizeof(FormatCount);
    Result = AdapterFuncs.pfnGetCaps(hUmdAdapter, &Caps);
    ok(Result == S_OK && FormatCount == ARRAYSIZE(Formats),
       "format count returned hr 0x%08lX, count %u\n",
       (long)Result,
       FormatCount);

    memset(Formats, 0, sizeof(Formats));
    Caps.Type = D3DDDICAPS_GETFORMATDATA;
    Caps.pData = Formats;
    Caps.DataSize = sizeof(Formats);
    Result = AdapterFuncs.pfnGetCaps(hUmdAdapter, &Caps);
    ok(Result == S_OK,
       "format enumeration failed 0x%08lX\n",
       (long)Result);
    for (Index = 0; Index < ARRAYSIZE(Formats); ++Index)
    {
        ok((Formats[Index].Operations & FORMATOP_PIXELSIZE) != 0,
           "format %u is not pixel-addressable\n",
           Index);
        ok((Formats[Index].Operations &
            FORMATOP_OFFSCREENPLAIN) != 0,
           "format %u is not an offscreen 2D surface\n",
           Index);
        ok((Formats[Index].Operations &
            FORMATOP_3DACCELERATION) == 0,
           "format %u falsely advertises 3D acceleration\n",
           Index);
    }

    memset(&DeviceFuncs, 0, sizeof(DeviceFuncs));
    memset(&CreateDevice, 0, sizeof(CreateDevice));
    CreateDevice.hDevice = hRuntimeDevice;
    CreateDevice.Interface =
        REACTOS_EXPECTED_UMD_INTERFACE_VERSION;
    CreateDevice.Version =
        REACTOS_EXPECTED_UMD_INTERFACE_VERSION;
    CreateDevice.pCallbacks = &Callbacks;
    CreateDevice.pDeviceFuncs = &DeviceFuncs;
    Result = AdapterFuncs.pfnCreateDevice(
                 hUmdAdapter,
                 &CreateDevice);
    ok(Result == S_OK,
       "UMD device creation failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        goto Cleanup;
    hUmdDevice = CreateDevice.hDevice;

    ok(DeviceFuncs.pfnCreateResource != NULL,
       "no pfnCreateResource\n");
    ok(DeviceFuncs.pfnDestroyResource != NULL,
       "no pfnDestroyResource\n");
    ok(DeviceFuncs.pfnLock != NULL && DeviceFuncs.pfnUnlock != NULL,
       "no lock/unlock pair\n");
    ok(DeviceFuncs.pfnColorFill != NULL,
       "no pfnColorFill\n");
    ok(DeviceFuncs.pfnBlt != NULL,
       "no pfnBlt\n");
    ok(DeviceFuncs.pfnSetDisplayMode != NULL,
       "no pfnSetDisplayMode\n");
    ok(DeviceFuncs.pfnPresent != NULL,
       "no pfnPresent\n");
    if (DeviceFuncs.pfnCreateResource == NULL ||
        DeviceFuncs.pfnDestroyResource == NULL ||
        DeviceFuncs.pfnLock == NULL ||
        DeviceFuncs.pfnUnlock == NULL ||
        DeviceFuncs.pfnColorFill == NULL ||
        DeviceFuncs.pfnBlt == NULL)
    {
        goto Cleanup;
    }

    memset(&ResourceFlags, 0, sizeof(ResourceFlags));
    Result = Umd2DCreateResource(
                 &DeviceFuncs,
                 hUmdDevice,
                 (HANDLE)&SourceRuntimeCookie,
                 ResourceFlags,
                 &hSource);
    ok(Result == S_OK && hSource != NULL,
       "source resource creation failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result) || hSource == NULL)
        goto Cleanup;

    Result = Umd2DCreateResource(
                 &DeviceFuncs,
                 hUmdDevice,
                 (HANDLE)&DestinationRuntimeCookie,
                 ResourceFlags,
                 &hDestination);
    ok(Result == S_OK && hDestination != NULL,
       "destination resource creation failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result) || hDestination == NULL)
        goto Cleanup;

    Result = Umd2DLockResource(&DeviceFuncs,
                               hUmdDevice,
                               hSource,
                               FALSE,
                               &Lock);
    ok(Result == S_OK && Lock.pSurfData != NULL,
       "source lock failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result) || Lock.pSurfData == NULL)
        goto Cleanup;
    SourceLocked = TRUE;
    Umd2DWritePixels(&Lock, SourceColor);
    Result = Umd2DUnlockResource(&DeviceFuncs,
                                 hUmdDevice,
                                 hSource);
    ok(Result == S_OK,
       "source unlock failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        goto Cleanup;
    SourceLocked = FALSE;

    memset(&Fill, 0, sizeof(Fill));
    Fill.hResource = hDestination;
    Fill.DstRect.right = UMD2D_WIDTH;
    Fill.DstRect.bottom = UMD2D_HEIGHT;
    Fill.Color = FillColor;
    Result = DeviceFuncs.pfnColorFill(hUmdDevice, &Fill);
    ok(Result == S_OK,
       "GPU color fill failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        goto Cleanup;
    Status = Umd2DWaitForIdle(pWaitForIdle, hKmtDevice);
    ok(NT_SUCCESS(Status),
       "wait after color fill failed 0x%08lX\n",
       (long)Status);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Result = Umd2DLockResource(&DeviceFuncs,
                               hUmdDevice,
                               hDestination,
                               TRUE,
                               &Lock);
    ok(Result == S_OK,
       "destination readback lock failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        goto Cleanup;
    DestinationLocked = TRUE;
    ok(Umd2DCheckPixels(&Lock, FillColor),
       "GPU color fill did not reach allocation memory\n");
    Result = Umd2DUnlockResource(&DeviceFuncs,
                                 hUmdDevice,
                                 hDestination);
    ok(Result == S_OK,
       "destination readback unlock failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        goto Cleanup;
    DestinationLocked = FALSE;

    memset(&Blt, 0, sizeof(Blt));
    Blt.hSrcResource = hSource;
    Blt.SrcRect.right = UMD2D_WIDTH;
    Blt.SrcRect.bottom = UMD2D_HEIGHT;
    Blt.hDstResource = hDestination;
    Blt.DstRect.right = UMD2D_WIDTH;
    Blt.DstRect.bottom = UMD2D_HEIGHT;
    Result = DeviceFuncs.pfnBlt(hUmdDevice, &Blt);
    ok(Result == S_OK,
       "GPU blit failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        goto Cleanup;
    Status = Umd2DWaitForIdle(pWaitForIdle, hKmtDevice);
    ok(NT_SUCCESS(Status),
       "wait after blit failed 0x%08lX\n",
       (long)Status);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Result = Umd2DLockResource(&DeviceFuncs,
                               hUmdDevice,
                               hDestination,
                               TRUE,
                               &Lock);
    ok(Result == S_OK,
       "blit readback lock failed 0x%08lX\n",
       (long)Result);
    if (FAILED(Result))
        goto Cleanup;
    DestinationLocked = TRUE;
    ok(Umd2DCheckPixels(&Lock, SourceColor),
       "GPU blit did not copy source pixels to destination\n");
    Result = Umd2DUnlockResource(&DeviceFuncs,
                                 hUmdDevice,
                                 hDestination);
    ok(Result == S_OK,
       "blit readback unlock failed 0x%08lX\n",
       (long)Result);
    if (SUCCEEDED(Result))
        DestinationLocked = FALSE;

Cleanup:
    if (hUmdDevice != NULL)
    {
        if (DestinationLocked && hDestination != NULL &&
            DeviceFuncs.pfnUnlock != NULL)
        {
            (VOID)Umd2DUnlockResource(&DeviceFuncs,
                                      hUmdDevice,
                                      hDestination);
            DestinationLocked = FALSE;
        }
        if (SourceLocked && hSource != NULL &&
            DeviceFuncs.pfnUnlock != NULL)
        {
            (VOID)Umd2DUnlockResource(&DeviceFuncs,
                                      hUmdDevice,
                                      hSource);
            SourceLocked = FALSE;
        }
        if (hDestination != NULL &&
            DeviceFuncs.pfnDestroyResource != NULL)
        {
            Result = DeviceFuncs.pfnDestroyResource(
                         hUmdDevice,
                         hDestination);
            ok(Result == S_OK,
               "destination destruction failed 0x%08lX\n",
               (long)Result);
            hDestination = NULL;
        }
        if (hSource != NULL &&
            DeviceFuncs.pfnDestroyResource != NULL)
        {
            Result = DeviceFuncs.pfnDestroyResource(
                         hUmdDevice,
                         hSource);
            ok(Result == S_OK,
               "source destruction failed 0x%08lX\n",
               (long)Result);
            hSource = NULL;
        }
        if (DeviceFuncs.pfnDestroyDevice != NULL)
        {
            Result = DeviceFuncs.pfnDestroyDevice(hUmdDevice);
            ok(Result == S_OK,
               "UMD device destruction failed 0x%08lX\n",
               (long)Result);
        }
        hUmdDevice = NULL;
    }
    if (hUmdAdapter != NULL &&
        AdapterFuncs.pfnCloseAdapter != NULL)
    {
        Result = AdapterFuncs.pfnCloseAdapter(hUmdAdapter);
        ok(Result == S_OK,
           "UMD adapter close failed 0x%08lX\n",
           (long)Result);
        hUmdAdapter = NULL;
    }
    if (hRuntimeDevice != NULL && pDestroyCallbacks != NULL)
    {
        Result = pDestroyCallbacks(hRuntimeDevice);
        ok(Result == S_OK,
           "runtime callback device destruction failed 0x%08lX\n",
           (long)Result);
        hRuntimeDevice = NULL;
    }
    if (Umd != NULL)
        FreeLibrary(Umd);
    if (Runtime != NULL)
        FreeLibrary(Runtime);
    if (hKmtDevice != 0)
        DestroyTestDevice(hKmtDevice);
    if (hAdapter != 0)
        CloseAdapter(hAdapter);
}

START_TEST(umd2d)
{
    Test_SoftGpu2DEndToEnd();
}

/* EOF */
