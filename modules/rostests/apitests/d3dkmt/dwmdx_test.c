/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Windows parity for the DWM D3DKMT window redirection surface
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Covers the surface added for the shared-GPU-redirection path:
 *   dwmapi ordinal 100/101 (DwmpDxGetWindowSharedSurface /
 *   DwmpDxUpdateWindowSharedSurface), the DWM_DX_SURFACE_EXCHANGE ABI, the
 *   win32k exchange behind it, and the adapter-LUID resolution the OpenGL ICD
 *   present callback depends on.
 */

#include "precomp.h"
#include <winuser.h>
#include <winioctl.h>
#include <winreg.h>
#include <wchar.h>
#include <reactos/dwmframe.h>

#ifndef STATUS_INVALID_HANDLE
#define STATUS_INVALID_HANDLE           ((NTSTATUS)0xC0000008L)
#endif
#ifndef STATUS_INFO_LENGTH_MISMATCH
#define STATUS_INFO_LENGTH_MISMATCH     ((NTSTATUS)0xC0000004L)
#endif
#ifndef STATUS_ACCESS_DENIED
#define STATUS_ACCESS_DENIED            ((NTSTATUS)0xC0000022L)
#endif
#ifndef STATUS_DEVICE_NOT_READY
#define STATUS_DEVICE_NOT_READY         ((NTSTATUS)0xC00000A3L)
#endif
#ifndef STATUS_NOT_FOUND
#define STATUS_NOT_FOUND                ((NTSTATUS)0xC0000225L)
#endif

#define HRESULT_NOT_READY                HRESULT_FROM_WIN32(ERROR_NOT_READY)
#define DXGI_FORMAT_B8G8R8A8_UNORM_VALUE 87u

typedef HRESULT (WINAPI *PFN_DWMPDXGETWINDOWSHAREDSURFACE)(
    HWND, LUID, HMONITOR, DWORD, UINT *, HANDLE *, ULONGLONG *);
typedef HRESULT (WINAPI *PFN_DWMPDXUPDATEWINDOWSHAREDSURFACE)(
    HWND, ULONGLONG, DWORD, HMONITOR, const RECT *);
typedef DWORD_PTR (NTAPI *PFN_NTUSERCALLONEPARAM)(DWORD_PTR, DWORD);

static PFN_DWMPDXGETWINDOWSHAREDSURFACE pDwmpDxGetWindowSharedSurface;
static PFN_DWMPDXUPDATEWINDOWSHAREDSURFACE pDwmpDxUpdateWindowSharedSurface;

static const WCHAR DwmDxClassName[] = L"RosDwmDxParityClass";

static HMODULE
LoadDwmApi(void)
{
    HMODULE Module = GetModuleHandleW(L"dwmapi.dll");

    if (Module == NULL)
        Module = LoadLibraryW(L"dwmapi.dll");
    return Module;
}

static BOOL
LoadDwmDxProcs(void)
{
    HMODULE Module;

    if (pDwmpDxGetWindowSharedSurface != NULL &&
        pDwmpDxUpdateWindowSharedSurface != NULL)
    {
        return TRUE;
    }

    Module = LoadDwmApi();
    if (Module == NULL)
        return FALSE;

    pDwmpDxGetWindowSharedSurface = (PFN_DWMPDXGETWINDOWSHAREDSURFACE)
        GetProcAddress(Module, (LPCSTR)(ULONG_PTR)100);
    pDwmpDxUpdateWindowSharedSurface = (PFN_DWMPDXUPDATEWINDOWSHAREDSURFACE)
        GetProcAddress(Module, (LPCSTR)(ULONG_PTR)101);
    return pDwmpDxGetWindowSharedSurface != NULL &&
           pDwmpDxUpdateWindowSharedSurface != NULL;
}

static BOOL
RunningOnReactOS(void)
{
    static int Cached = -1;
    WCHAR Product[64];
    DWORD Size = sizeof(Product);
    DWORD Type = 0;
    HKEY Key;

    if (Cached >= 0)
        return Cached != 0;

    Cached = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      0, KEY_QUERY_VALUE, &Key) == ERROR_SUCCESS)
    {
        if (RegQueryValueExW(Key, L"ProductName", NULL, &Type,
                             (LPBYTE)Product, &Size) == ERROR_SUCCESS &&
            Type == REG_SZ)
        {
            Product[ARRAYSIZE(Product) - 1] = L'\0';
            if (wcsstr(Product, L"ReactOS") != NULL)
                Cached = 1;
        }
        RegCloseKey(Key);
    }
    return Cached != 0;
}

static BOOL
RegisterParityClass(void)
{
    static ATOM ClassAtom;
    WNDCLASSEXW wc;

    if (ClassAtom != 0)
        return TRUE;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = DwmDxClassName;
    ClassAtom = RegisterClassExW(&wc);
    return ClassAtom != 0;
}

static HWND
CreateParityWindowEx(DWORD Style, int Width, int Height)
{
    HWND Window;

    if (!RegisterParityClass())
        return NULL;

    Window = CreateWindowExW(0, DwmDxClassName, L"DWM DX parity", Style,
                             32, 32, Width, Height, NULL, NULL,
                             GetModuleHandleW(NULL), NULL);
    if (Window == NULL)
        return NULL;

    if (Style & WS_VISIBLE)
        UpdateWindow(Window);
    return Window;
}

static HWND
CreateParityWindow(int Width, int Height)
{
    return CreateParityWindowEx(WS_POPUP | WS_VISIBLE, Width, Height);
}

static BOOL
GetDisplay1Luid(LUID *Luid)
{
    PFN_D3DKMTOpenAdapterFromGdiDisplayName pfnOpen;
    PFN_D3DKMTCloseAdapter pfnClose;
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME Open;
    D3DKMT_CLOSEADAPTER Close;

    pfnOpen = (PFN_D3DKMTOpenAdapterFromGdiDisplayName)
        LoadD3DKMTProc("D3DKMTOpenAdapterFromGdiDisplayName");
    pfnClose = (PFN_D3DKMTCloseAdapter)LoadD3DKMTProc("D3DKMTCloseAdapter");
    if (pfnOpen == NULL || pfnClose == NULL)
        return FALSE;

    memset(&Open, 0, sizeof(Open));
    wcscpy(Open.DeviceName, L"\\\\.\\DISPLAY1");
    if (!NT_SUCCESS(pfnOpen(&Open)) || Open.hAdapter == 0)
        return FALSE;

    *Luid = Open.AdapterLuid;
    memset(&Close, 0, sizeof(Close));
    Close.hAdapter = Open.hAdapter;
    pfnClose(&Close);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Export contract                                                     */
/* ------------------------------------------------------------------ */
START_TEST(dwmdxexport)
{
    HMODULE Module;
    FARPROC ByOrdinal100, ByOrdinal101, ByOrdinal102;
    FARPROC ByName100, ByName101, ByName102;

    Module = LoadDwmApi();
    if (Module == NULL)
    {
        skip("dwmapi.dll is not present\n");
        return;
    }

    ByOrdinal100 = GetProcAddress(Module, (LPCSTR)(ULONG_PTR)100);
    ByOrdinal101 = GetProcAddress(Module, (LPCSTR)(ULONG_PTR)101);
    ByOrdinal102 = GetProcAddress(Module, (LPCSTR)(ULONG_PTR)102);

    ok(ByOrdinal100 != NULL,
       "dwmapi ordinal 100 (DwmpDxGetWindowSharedSurface) must be exported\n");
    ok(ByOrdinal101 != NULL,
       "dwmapi ordinal 101 (DwmpDxUpdateWindowSharedSurface) must be exported\n");

    ByName100 = GetProcAddress(Module, "DwmpDxGetWindowSharedSurface");
    ByName101 = GetProcAddress(Module, "DwmpDxUpdateWindowSharedSurface");
    ByName102 = GetProcAddress(Module, "DwmEnableComposition");

    ok(ByName100 == ByOrdinal100,
       "dwmapi ordinal 100 must resolve to DwmpDxGetWindowSharedSurface, ordinal=%p name=%p\n",
       (void *)ByOrdinal100, (void *)ByName100);
    ok(ByName101 == ByOrdinal101,
       "dwmapi ordinal 101 must resolve to DwmpDxUpdateWindowSharedSurface, ordinal=%p name=%p\n",
       (void *)ByOrdinal101, (void *)ByName101);

    ok(ByOrdinal102 != NULL && ByOrdinal102 == ByName102,
       "dwmapi ordinal 102 must be the named DwmEnableComposition (ordinal-base "
       "anchor), ordinal=%p name=%p\n", (void *)ByOrdinal102, (void *)ByName102);
}

/* ------------------------------------------------------------------ */
/* Fixed-layout ABI freeze: one layout for native and WOW64 clients     */
/* ------------------------------------------------------------------ */
C_ASSERT(sizeof(DWM_DX_SHARED_SURFACE_INFO) == 24);
C_ASSERT(FIELD_OFFSET(DWM_DX_SHARED_SURFACE_INFO, Magic) == 0);
C_ASSERT(FIELD_OFFSET(DWM_DX_SHARED_SURFACE_INFO, Version) == 4);
C_ASSERT(FIELD_OFFSET(DWM_DX_SHARED_SURFACE_INFO, Width) == 8);
C_ASSERT(FIELD_OFFSET(DWM_DX_SHARED_SURFACE_INFO, Height) == 12);
C_ASSERT(FIELD_OFFSET(DWM_DX_SHARED_SURFACE_INFO, Pitch) == 16);
C_ASSERT(FIELD_OFFSET(DWM_DX_SHARED_SURFACE_INFO, Format) == 20);

C_ASSERT(sizeof(DWM_DX_SURFACE_EXCHANGE) == 96);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, StructSize) == 0);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, Action) == 4);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, Window) == 8);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, AdapterLuid) == 16);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, GlobalShare) == 24);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, SurfaceId) == 28);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, Generation) == 32);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, Flags) == 36);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, Info) == 40);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, ReadyEvent) == 64);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, UpdateId) == 72);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, UpdateRect) == 80);

C_ASSERT(sizeof(DWM_WIN) == 232);
C_ASSERT(FIELD_OFFSET(DWM_WIN, DxGlobalShare) == 44);
C_ASSERT(FIELD_OFFSET(DWM_WIN, DxGeneration) == 48);
C_ASSERT(FIELD_OFFSET(DWM_WIN, DxAdapterLuid) == 52);
C_ASSERT(FIELD_OFFSET(DWM_WIN, DxUpdateId) == 60);
C_ASSERT(FIELD_OFFSET(DWM_WIN, DxClientX) == 68);
C_ASSERT(FIELD_OFFSET(DWM_WIN, DxClientY) == 72);
C_ASSERT(FIELD_OFFSET(DWM_WIN, DxWidth) == 76);
C_ASSERT(FIELD_OFFSET(DWM_WIN, DxHeight) == 80);
C_ASSERT(FIELD_OFFSET(DWM_WIN, DxPitch) == 84);
C_ASSERT(FIELD_OFFSET(DWM_WIN, DxFormat) == 88);
C_ASSERT(FIELD_OFFSET(DWM_WIN, BaseGlobalShare) == 92);
C_ASSERT(FIELD_OFFSET(DWM_WIN, BaseGeneration) == 96);
C_ASSERT(FIELD_OFFSET(DWM_WIN, BaseUpdateId) == 100);
C_ASSERT(FIELD_OFFSET(DWM_WIN, BaseWidth) == 108);
C_ASSERT(FIELD_OFFSET(DWM_WIN, BaseHeight) == 112);
C_ASSERT(FIELD_OFFSET(DWM_WIN, BasePitch) == 116);
C_ASSERT(FIELD_OFFSET(DWM_WIN, BaseFormat) == 120);
C_ASSERT(FIELD_OFFSET(DWM_WIN, ContentBackdrop) == 156);
C_ASSERT(FIELD_OFFSET(DWM_WIN, BasePreviousUpdateId) == 208);
C_ASSERT(FIELD_OFFSET(DWM_WIN, BaseDirtyRect) == 216);

C_ASSERT(sizeof(DWM_FRAME_HEADER) == 72);
C_ASSERT(DWM_WINARRAY_BASE == 72);
C_ASSERT(DWM_BLURRECTARRAY_BASE == 72 + 256 * 232);
C_ASSERT(DWM_FRAME_BYTES == 72 + 256 * 232 + 4096 * 16);

START_TEST(dwmdxabi)
{
    ok(sizeof(DWM_DX_SURFACE_EXCHANGE) == 96,
       "DWM_DX_SURFACE_EXCHANGE must be 96 bytes on every architecture, got %u\n",
       (unsigned)sizeof(DWM_DX_SURFACE_EXCHANGE));
    ok(sizeof(DWM_DX_SHARED_SURFACE_INFO) == 24,
       "DWM_DX_SHARED_SURFACE_INFO must be 24 bytes, got %u\n",
       (unsigned)sizeof(DWM_DX_SHARED_SURFACE_INFO));
    ok(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, Window) == 8,
       "Window must be a zero-extended 64-bit HWND at offset 8, got %u\n",
       (unsigned)FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, Window));

    ok(DWM_DX_FORMAT_B8G8R8A8_UNORM == DXGI_FORMAT_B8G8R8A8_UNORM_VALUE,
       "the shared-surface format must be DXGI_FORMAT_B8G8R8A8_UNORM (%u), got %u\n",
       DXGI_FORMAT_B8G8R8A8_UNORM_VALUE, DWM_DX_FORMAT_B8G8R8A8_UNORM);
    ok(DWM_DX_SURFACE_INFO_MAGIC == 0x53585744u,
       "the runtime-data magic must be 'DWXS', got 0x%08lX\n",
       (unsigned long)DWM_DX_SURFACE_INFO_MAGIC);
    ok(DWM_DX_SURFACE_INFO_VERSION == 1u,
       "the runtime-data version must be 1, got %lu\n",
       (unsigned long)DWM_DX_SURFACE_INFO_VERSION);
    ok(DWM_DX_SURFACE_INFO_VERSION_GPU == 2u &&
       DWM_DX_SURFACE_PUBLISH == 7u && DWM_DX_SURFACE_UNREGISTER == 8u,
       "native GPU publication must retain version 2 and actions 7/8\n");

    ok(DWM_DX_SURFACE_REGISTER == 1u && DWM_DX_SURFACE_ISSUE == 2u &&
       DWM_DX_SURFACE_UPDATE == 3u && DWM_DX_SURFACE_CONSUMED == 4u,
       "the exchange action codes must stay 1..4, got %lu/%lu/%lu/%lu\n",
       (unsigned long)DWM_DX_SURFACE_REGISTER,
       (unsigned long)DWM_DX_SURFACE_ISSUE,
       (unsigned long)DWM_DX_SURFACE_UPDATE,
       (unsigned long)DWM_DX_SURFACE_CONSUMED);
    ok(DWM_DX_UPDATE_CANCEL == 0x80000000u,
       "the cancel flag must not collide with the 1-bit SyncType field, "
       "got 0x%08lX\n", (unsigned long)DWM_DX_UPDATE_CANCEL);

    ok(DWM_ROUTINE_DXSURFACE == 0xfffe0017u,
       "DWM_ROUTINE_DXSURFACE must stay 0xfffe0017, got 0x%08lX\n",
       (unsigned long)DWM_ROUTINE_DXSURFACE);
    ok(DWM_FRAME_MAGIC == 0x354d5744u,
       "the frame magic must be 'DWM5' for CDD and app surfaces, got 0x%08lX\n",
       (unsigned long)DWM_FRAME_MAGIC);
}

/* ------------------------------------------------------------------ */
/* Argument validation -- reachable without a running compositor        */
/* ------------------------------------------------------------------ */
START_TEST(dwmdxarg)
{
    HWND Window, Empty;
    LUID Luid;
    HANDLE Surface;
    ULONGLONG UpdateId;
    UINT Format;
    RECT Rect, EmptyRect;
    HRESULT hr;

    if (!LoadDwmDxProcs())
    {
        skip("dwmapi ordinals 100/101 are not available\n");
        return;
    }

    memset(&Luid, 0, sizeof(Luid));
    (void)GetDisplay1Luid(&Luid);

    Window = CreateParityWindow(320, 240);
    if (Window == NULL)
    {
        skip("could not create the parity test window\n");
        return;
    }

    Format = 0xDEADBEEF;
    Surface = (HANDLE)(ULONG_PTR)0xDEADBEEF;
    UpdateId = ~(ULONGLONG)0;

    hr = pDwmpDxGetWindowSharedSurface(Window, Luid, NULL, 0,
                                       NULL, &Surface, &UpdateId);
    ok(hr == E_INVALIDARG,
       "GetWindowSharedSurface(pFormat=NULL) must be E_INVALIDARG, got 0x%08lX\n",
       (unsigned long)hr);

    hr = pDwmpDxGetWindowSharedSurface(Window, Luid, NULL, 0,
                                       &Format, NULL, &UpdateId);
    ok(hr == E_INVALIDARG,
       "GetWindowSharedSurface(phSurface=NULL) must be E_INVALIDARG, got 0x%08lX\n",
       (unsigned long)hr);

    hr = pDwmpDxGetWindowSharedSurface(Window, Luid, NULL, 0,
                                       &Format, &Surface, NULL);
    ok(hr == E_INVALIDARG,
       "GetWindowSharedSurface(pUpdateId=NULL) must be E_INVALIDARG, got 0x%08lX\n",
       (unsigned long)hr);

    hr = pDwmpDxGetWindowSharedSurface((HWND)(ULONG_PTR)0xDEADBEEF, Luid, NULL, 0,
                                       &Format, &Surface, &UpdateId);
    ok(hr == E_INVALIDARG,
       "GetWindowSharedSurface(bogus HWND) must be E_INVALIDARG, got 0x%08lX\n",
       (unsigned long)hr);

    hr = pDwmpDxGetWindowSharedSurface(Window, Luid, NULL, 2,
                                       &Format, &Surface, &UpdateId);
    ok(hr == E_INVALIDARG,
       "GetWindowSharedSurface with a reserved flag bit must be E_INVALIDARG, "
       "got 0x%08lX\n", (unsigned long)hr);

    Empty = CreateParityWindowEx(WS_POPUP, 0, 0);
    if (Empty != NULL)
    {
        SetRectEmpty(&EmptyRect);
        if (GetClientRect(Empty, &EmptyRect) && IsRectEmpty(&EmptyRect))
        {
            hr = pDwmpDxGetWindowSharedSurface(Empty, Luid, NULL, 0,
                                               &Format, &Surface, &UpdateId);
            ok(hr == E_INVALIDARG,
               "GetWindowSharedSurface on an empty client rect must be "
               "E_INVALIDARG, got 0x%08lX\n", (unsigned long)hr);
        }
        else
        {
            skip("the window manager did not produce an empty client rect\n");
        }
        DestroyWindow(Empty);
    }

    SetRect(&Rect, 0, 0, 16, 16);

    hr = pDwmpDxUpdateWindowSharedSurface((HWND)(ULONG_PTR)0xDEADBEEF, 1, 0,
                                          NULL, &Rect);
    ok(hr == E_INVALIDARG,
       "UpdateWindowSharedSurface(bogus HWND) must be E_INVALIDARG, got 0x%08lX\n",
       (unsigned long)hr);

    hr = pDwmpDxUpdateWindowSharedSurface(Window, 0, 0, NULL, &Rect);
    ok(hr == E_INVALIDARG,
       "UpdateWindowSharedSurface(UpdateId=0) must be E_INVALIDARG, got 0x%08lX\n",
       (unsigned long)hr);

    hr = pDwmpDxUpdateWindowSharedSurface(Window, 1, 0, NULL, NULL);
    ok(hr == E_INVALIDARG,
       "UpdateWindowSharedSurface(prc=NULL) without the cancel flag must be "
       "E_INVALIDARG, got 0x%08lX\n", (unsigned long)hr);

    hr = pDwmpDxUpdateWindowSharedSurface(Window, 0x1234567890ABCDEFull,
                                          DWM_DX_UPDATE_CANCEL, NULL, NULL);
    ok(FAILED(hr),
       "UpdateWindowSharedSurface must refuse an update id it never issued, "
       "got 0x%08lX\n", (unsigned long)hr);

    DestroyWindow(Window);
}

/* ------------------------------------------------------------------ */
/* End-to-end: issue / publish / cancel and the shared-handle contract  */
/* ------------------------------------------------------------------ */
static BOOL
OpenSharedSurfaceAdapter(HANDLE SharedSurface,
                         D3DKMT_HANDLE *Adapter,
                         LUID *AdapterLuid)
{
    PFND3DKMT_GETSHAREDRESOURCEADAPTERLUID pfnGetLuid;
    PFN_D3DKMTOpenAdapterFromLuid pfnOpen;
    D3DKMT_GETSHAREDRESOURCEADAPTERLUID GetLuid;
    D3DKMT_OPENADAPTERFROMLUID Open;
    NTSTATUS Status;

    pfnGetLuid = (PFND3DKMT_GETSHAREDRESOURCEADAPTERLUID)
        LoadD3DKMTProc("D3DKMTGetSharedResourceAdapterLuid");
    pfnOpen = (PFN_D3DKMTOpenAdapterFromLuid)
        LoadD3DKMTProc("D3DKMTOpenAdapterFromLuid");
    if (pfnGetLuid == NULL || pfnOpen == NULL)
    {
        skip("the shared-resource LUID adapter entry points are not exported\n");
        return FALSE;
    }

    memset(&GetLuid, 0, sizeof(GetLuid));
    GetLuid.hGlobalShare = (D3DKMT_HANDLE)(ULONG_PTR)SharedSurface;
    Status = pfnGetLuid(&GetLuid);
    ok(NT_SUCCESS(Status),
       "GetSharedResourceAdapterLuid on the redirection surface returned "
       "0x%08lX\n", (unsigned long)Status);
    if (!NT_SUCCESS(Status))
        return FALSE;

    memset(&Open, 0, sizeof(Open));
    Open.AdapterLuid = GetLuid.AdapterLuid;
    Status = pfnOpen(&Open);
    ok(NT_SUCCESS(Status) && Open.hAdapter != 0,
       "the redirection surface owner LUID %08lX:%08lX must reopen its exact "
       "adapter, got 0x%08lX handle=%#lx\n",
       (unsigned long)GetLuid.AdapterLuid.HighPart,
       (unsigned long)GetLuid.AdapterLuid.LowPart,
       (unsigned long)Status, (unsigned long)Open.hAdapter);
    if (!NT_SUCCESS(Status) || Open.hAdapter == 0)
        return FALSE;

    *Adapter = Open.hAdapter;
    if (AdapterLuid != NULL)
        *AdapterLuid = GetLuid.AdapterLuid;
    return TRUE;
}

static void
CheckSharedSurfaceRuntimeData(HANDLE SharedSurface, ULONG Width, ULONG Height)
{
    PFN_D3DKMTQueryResourceInfo pfnQuery;
    D3DKMT_QUERYRESOURCEINFO Query;
    DWM_DX_SHARED_SURFACE_INFO Info;
    D3DKMT_HANDLE hAdapter, hDevice;
    NTSTATUS Status;

    pfnQuery = (PFN_D3DKMTQueryResourceInfo)
        LoadD3DKMTProc("D3DKMTQueryResourceInfo");
    if (pfnQuery == NULL)
    {
        skip("D3DKMTQueryResourceInfo is not exported by gdi32.dll\n");
        return;
    }

    if (!OpenSharedSurfaceAdapter(SharedSurface, &hAdapter, NULL))
        return;

    hDevice = CreateTestDevice(hAdapter);
    if (hDevice == 0)
    {
        ok(0, "could not create a D3DKMT device on the redirection surface "
              "owner\n");
        CloseAdapter(hAdapter);
        return;
    }

    memset(&Info, 0, sizeof(Info));
    memset(&Query, 0, sizeof(Query));
    Query.hDevice = hDevice;
    Query.hGlobalShare = (D3DKMT_HANDLE)(ULONG_PTR)SharedSurface;
    Query.pPrivateRuntimeData = &Info;
    Query.PrivateRuntimeDataSize = sizeof(Info);
    Status = pfnQuery(&Query);
    ok(NT_SUCCESS(Status),
       "the handle returned by GetWindowSharedSurface must be a valid D3DKMT "
       "global-share handle, QueryResourceInfo returned 0x%08lX\n",
       (unsigned long)Status);

    if (NT_SUCCESS(Status))
    {
        ok(Query.NumAllocations == 1,
           "a redirection surface must be a single-allocation resource, got %u\n",
           Query.NumAllocations);
        ok(Query.PrivateRuntimeDataSize == sizeof(Info),
           "the runtime data must be one DWM_DX_SHARED_SURFACE_INFO (%u bytes), "
           "got %u\n", (unsigned)sizeof(Info), Query.PrivateRuntimeDataSize);

        if (Query.PrivateRuntimeDataSize == sizeof(Info))
        {
            ok(Info.Magic == DWM_DX_SURFACE_INFO_MAGIC,
               "the runtime-data magic must be 'DWXS', got 0x%08lX\n",
               (unsigned long)Info.Magic);
            ok(Info.Version == DWM_DX_SURFACE_INFO_VERSION,
               "the runtime-data version must be %lu, got %lu\n",
               (unsigned long)DWM_DX_SURFACE_INFO_VERSION,
               (unsigned long)Info.Version);
            ok(Info.Width == Width && Info.Height == Height,
               "the runtime data must describe the client rect %lux%lu, "
               "got %lux%lu\n", (unsigned long)Width, (unsigned long)Height,
               (unsigned long)Info.Width, (unsigned long)Info.Height);
            ok(Info.Pitch >= Info.Width * 4,
               "a linear B8G8R8A8 surface pitch must cover width * 4 (%lu), "
               "got %lu\n", (unsigned long)(Info.Width * 4),
               (unsigned long)Info.Pitch);
            ok(Info.Format == DWM_DX_FORMAT_B8G8R8A8_UNORM,
               "the runtime-data format must be %u, got %lu\n",
               DWM_DX_FORMAT_B8G8R8A8_UNORM, (unsigned long)Info.Format);
        }
    }

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

static void
CheckSharedSurfaceClientOpen(HANDLE SharedSurface, ULONG Width, ULONG Height)
{
    PFN_D3DKMTQueryResourceInfo pfnQuery;
    PFN_D3DKMTOpenResource pfnOpen;
    PFN_D3DKMTDestroyAllocation pfnDestroy;
    D3DKMT_QUERYRESOURCEINFO Query;
    D3DKMT_OPENRESOURCE Open;
    D3DKMT_DESTROYALLOCATION Destroy;
    D3DDDI_OPENALLOCATIONINFO OpenAllocation;
    DWM_DX_SHARED_SURFACE_INFO Info;
    PVOID ResourcePrivate = NULL, TotalPrivate = NULL;
    D3DKMT_HANDLE hAdapter, hDevice;
    NTSTATUS Status;

    pfnQuery = (PFN_D3DKMTQueryResourceInfo)
        LoadD3DKMTProc("D3DKMTQueryResourceInfo");
    pfnOpen = (PFN_D3DKMTOpenResource)LoadD3DKMTProc("D3DKMTOpenResource");
    pfnDestroy = (PFN_D3DKMTDestroyAllocation)
        LoadD3DKMTProc("D3DKMTDestroyAllocation");
    if (pfnQuery == NULL || pfnOpen == NULL || pfnDestroy == NULL)
    {
        skip("the D3DKMT shared-resource entry points are not exported\n");
        return;
    }

    if (!OpenSharedSurfaceAdapter(SharedSurface, &hAdapter, NULL))
        return;
    hDevice = CreateTestDevice(hAdapter);
    if (hDevice == 0)
    {
        ok(0, "could not create a D3DKMT device on the redirection surface "
              "owner\n");
        CloseAdapter(hAdapter);
        return;
    }

    memset(&Query, 0, sizeof(Query));
    Query.hDevice = hDevice;
    Query.hGlobalShare = (D3DKMT_HANDLE)(ULONG_PTR)SharedSurface;
    Status = pfnQuery(&Query);
    ok(NT_SUCCESS(Status), "QueryResourceInfo on a published surface failed "
       "with 0x%08lX\n", (unsigned long)Status);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (Query.ResourcePrivateDriverDataSize != 0)
        ResourcePrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                    Query.ResourcePrivateDriverDataSize);
    if (Query.TotalPrivateDriverDataSize != 0)
        TotalPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                 Query.TotalPrivateDriverDataSize);
    if ((Query.ResourcePrivateDriverDataSize != 0 && ResourcePrivate == NULL) ||
        (Query.TotalPrivateDriverDataSize != 0 && TotalPrivate == NULL))
    {
        skip("out of memory building the OpenResource buffers\n");
        goto Cleanup;
    }

    memset(&Info, 0, sizeof(Info));
    memset(&OpenAllocation, 0, sizeof(OpenAllocation));
    memset(&Open, 0, sizeof(Open));
    Open.hDevice = hDevice;
    Open.hGlobalShare = (D3DKMT_HANDLE)(ULONG_PTR)SharedSurface;
    Open.NumAllocations = 1;
    Open.pOpenAllocationInfo = &OpenAllocation;
    Open.pPrivateRuntimeData = &Info;
    Open.PrivateRuntimeDataSize = sizeof(Info);
    Open.pResourcePrivateDriverData = ResourcePrivate;
    Open.ResourcePrivateDriverDataSize = Query.ResourcePrivateDriverDataSize;
    Open.pTotalPrivateDriverDataBuffer = TotalPrivate;
    Open.TotalPrivateDriverDataBufferSize = Query.TotalPrivateDriverDataSize;
    Status = pfnOpen(&Open);
    ok(NT_SUCCESS(Status),
       "an ICD must be able to open the DWM redirection surface from its own "
       "device, OpenResource returned 0x%08lX\n", (unsigned long)Status);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    ok(Open.hResource != 0 && OpenAllocation.hAllocation != 0,
       "OpenResource must hand back a resource and an allocation handle, "
       "got %#lx / %#lx\n", (unsigned long)Open.hResource,
       (unsigned long)OpenAllocation.hAllocation);
    ok(Info.Magic == DWM_DX_SURFACE_INFO_MAGIC &&
       Info.Version == DWM_DX_SURFACE_INFO_VERSION,
       "OpenResource must return the same runtime data QueryResourceInfo does, "
       "got magic 0x%08lX version %lu\n",
       (unsigned long)Info.Magic, (unsigned long)Info.Version);
    ok(Info.Width == Width && Info.Height == Height &&
       Info.Pitch >= Width * 4 &&
       Info.Format == DWM_DX_FORMAT_B8G8R8A8_UNORM,
       "the opened surface must describe %lux%lu B8G8R8A8, got %lux%lu pitch %lu "
       "format %lu\n", (unsigned long)Width, (unsigned long)Height,
       (unsigned long)Info.Width, (unsigned long)Info.Height,
       (unsigned long)Info.Pitch, (unsigned long)Info.Format);

    memset(&Destroy, 0, sizeof(Destroy));
    Destroy.hDevice = hDevice;
    Destroy.hResource = Open.hResource;
    Status = pfnDestroy(&Destroy);
    ok(NT_SUCCESS(Status),
       "closing the opened redirection surface must succeed, got 0x%08lX\n",
       (unsigned long)Status);

Cleanup:
    if (ResourcePrivate != NULL)
        HeapFree(GetProcessHeap(), 0, ResourcePrivate);
    if (TotalPrivate != NULL)
        HeapFree(GetProcessHeap(), 0, TotalPrivate);
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* Exercise the ReactOS producer independently of the compositor.  Windows
 * receives its surface from DWM; this private runtime-data request is the
 * ReactOS CDD contract and must not be sent to an arbitrary Windows UMD. */
static void
TestCreateRedirectionSurface(void)
{
    PFN_D3DKMTCreateDevice pfnCreateDevice;
    PFN_D3DKMTCreateAllocation pfnCreate;
    PFN_D3DKMTDestroyAllocation pfnDestroy;
    PFN_D3DKMTQueryAdapterInfo pfnQuery;
    D3DKMT_QUERYADAPTERINFO Query;
    D3DKMT_WDDM_1_2_CAPS Caps;
    D3DKMT_CREATEDEVICE Device;
    D3DKMT_CREATEALLOCATION Create;
    D3DDDI_ALLOCATIONINFO Allocation;
    D3DKMT_DESTROYALLOCATION Destroy;
    DWM_DX_SHARED_SURFACE_INFO Info;
    D3DKMT_HANDLE hAdapter;
    UINT Dimensions[3] = {67, 37, 32};
    LUID Luid;
    BOOL Software;
    NTSTATUS CapsStatus = STATUS_NOT_IMPLEMENTED;
    NTSTATUS Status;

    if (!RunningOnReactOS())
        return;

    pfnCreateDevice = (PFN_D3DKMTCreateDevice)LoadD3DKMTProc("D3DKMTCreateDevice");
    pfnCreate = (PFN_D3DKMTCreateAllocation)LoadD3DKMTProc("D3DKMTCreateAllocation");
    pfnDestroy = (PFN_D3DKMTDestroyAllocation)LoadD3DKMTProc("D3DKMTDestroyAllocation");
    pfnQuery = (PFN_D3DKMTQueryAdapterInfo)LoadD3DKMTProc("D3DKMTQueryAdapterInfo");
    ok(pfnCreateDevice != NULL && pfnCreate != NULL && pfnDestroy != NULL,
       "redirection allocation entry points must be exported\n");
    if (pfnCreateDevice == NULL || pfnCreate == NULL || pfnDestroy == NULL)
        return;

    hAdapter = OpenRenderAdapterEx(&Luid, &Software);
    if (hAdapter == 0)
    {
        skip("no render adapter for a redirection allocation\n");
        return;
    }
    if (Software)
    {
        skip("only a software adapter is available for redirection allocation\n");
        CloseAdapter(hAdapter);
        return;
    }

    memset(&Caps, 0, sizeof(Caps));
    memset(&Query, 0, sizeof(Query));
    Query.hAdapter = hAdapter;
    Query.Type = KMTQAITYPE_WDDM_1_2_CAPS;
    Query.pPrivateDriverData = &Caps;
    Query.PrivateDriverDataSize = sizeof(Caps);
    if (pfnQuery != NULL)
        CapsStatus = pfnQuery(&Query);

    memset(&Device, 0, sizeof(Device));
    Device.hAdapter = hAdapter;
    Status = pfnCreateDevice(&Device);
    ok(NT_SUCCESS(Status) && Device.hDevice != 0,
       "creating the hardware redirection device failed 0x%08lX\n",
       (unsigned long)Status);
    if (!NT_SUCCESS(Status) || Device.hDevice == 0)
    {
        CloseAdapter(hAdapter);
        return;
    }

    memset(&Info, 0, sizeof(Info));
    Info.Magic = DWM_DX_SURFACE_INFO_MAGIC;
    Info.Version = DWM_DX_SURFACE_INFO_VERSION;
    Info.Width = Dimensions[0];
    Info.Height = Dimensions[1];
    Info.Pitch = Dimensions[0] * sizeof(ULONG);
    Info.Format = DWM_DX_FORMAT_B8G8R8A8_UNORM;
    memset(&Allocation, 0, sizeof(Allocation));
    Allocation.pPrivateDriverData = Dimensions;
    Allocation.PrivateDriverDataSize = sizeof(Dimensions);
    memset(&Create, 0, sizeof(Create));
    Create.hDevice = Device.hDevice;
    Create.pPrivateRuntimeData = &Info;
    Create.PrivateRuntimeDataSize = sizeof(Info);
    Create.NumAllocations = 1;
    Create.pAllocationInfo = &Allocation;
    Create.Flags.CreateResource = 1;
    Create.Flags.CreateShared = 1;
    Status = pfnCreate(&Create);
    if (Status == STATUS_NOT_SUPPORTED && NT_SUCCESS(CapsStatus) &&
        !Caps.SupportKernelModeCommandBuffer)
    {
        /* GDISURFACE is the GDI hardware-acceleration allocation contract.
         * A native D3D texture with a CPU upload is tested separately. */
        skip("GDISURFACE unavailable: adapter explicitly reports no GDI command-buffer support\n");
        ok(Create.hResource == 0 && Allocation.hAllocation == 0 && Create.hGlobalShare == 0,
           "an unsupported GDI allocation must not publish partial handles\n");
    }
    else
    {
        ok(NT_SUCCESS(Status), "hardware GDI redirection allocation failed "
           "0x%08lX\n", (unsigned long)Status);
    }
    if (NT_SUCCESS(Status))
    {
        ok(Create.hResource != 0 && Allocation.hAllocation != 0 &&
           Create.hGlobalShare != 0,
           "a redirection allocation must publish resource/allocation/share handles\n");
        if (Create.hGlobalShare != 0)
        {
            CheckSharedSurfaceRuntimeData((HANDLE)(ULONG_PTR)Create.hGlobalShare,
                                         Info.Width, Info.Height);
            CheckSharedSurfaceClientOpen((HANDLE)(ULONG_PTR)Create.hGlobalShare,
                                        Info.Width, Info.Height);
        }

        memset(&Destroy, 0, sizeof(Destroy));
        Destroy.hDevice = Device.hDevice;
        Destroy.hResource = Create.hResource;
        Status = pfnDestroy(&Destroy);
        ok(NT_SUCCESS(Status), "destroying the redirection allocation failed "
           "0x%08lX\n", (unsigned long)Status);
    }

    DestroyTestDevice(Device.hDevice);
    CloseAdapter(hAdapter);
}

START_TEST(dwmdxsurface)
{
    HWND Window;
    LUID Luid, WrongLuid;
    HANDLE Surface = NULL, Surface2 = NULL;
    ULONGLONG FirstId = 0, SecondId = 0;
    UINT Format = 0;
    RECT Client, ClientInWindow, Rect, WindowRect;
    POINT ClientOrigin;
    ULONG Width, Height;
    HRESULT hr;

    TestCreateRedirectionSurface();

    if (!LoadDwmDxProcs())
    {
        skip("dwmapi ordinals 100/101 are not available\n");
        return;
    }
    if (!GetDisplay1Luid(&Luid))
    {
        skip("no D3DKMT display adapter\n");
        return;
    }

    Window = CreateParityWindowEx(WS_OVERLAPPEDWINDOW | WS_VISIBLE, 320, 240);
    if (Window == NULL)
    {
        skip("could not create the parity test window\n");
        return;
    }
    GetClientRect(Window, &Client);
    Width = (ULONG)(Client.right - Client.left);
    Height = (ULONG)(Client.bottom - Client.top);
    ClientOrigin.x = 0;
    ClientOrigin.y = 0;
    ClientToScreen(Window, &ClientOrigin);
    GetWindowRect(Window, &WindowRect);
    ClientInWindow = Client;
    OffsetRect(&ClientInWindow,
               ClientOrigin.x - WindowRect.left,
               ClientOrigin.y - WindowRect.top);
    ok(ClientInWindow.left > 0 && ClientInWindow.top > 0,
       "the regression window must have a non-client offset, got (%ld,%ld)\n",
       ClientInWindow.left, ClientInWindow.top);

    hr = pDwmpDxGetWindowSharedSurface(Window, Luid, NULL, 0,
                                       &Format, &Surface, &FirstId);
    if (hr == HRESULT_NOT_READY)
    {
        skip("no compositor attached; the redirection-surface path is inactive\n");
        DestroyWindow(Window);
        return;
    }
    if (FAILED(hr))
    {
        skip("GetWindowSharedSurface failed with 0x%08lX\n", (unsigned long)hr);
        DestroyWindow(Window);
        return;
    }

    ok(hr == S_OK, "the first acquisition must be S_OK, got 0x%08lX\n",
       (unsigned long)hr);
    ok(Surface != NULL, "a successful acquisition must return a surface handle\n");
    ok(FirstId != 0, "a successful acquisition must return a non-zero update id\n");
    /* This ordinal returns a D3DDDIFORMAT. The private resource metadata
     * checked below uses DXGI_FORMAT instead; the two enums differ. */
    ok(Format == D3DDDIFMT_A8R8G8B8,
       "the public surface format must be D3DDDIFMT_A8R8G8B8 (%u), got %u\n",
       D3DDDIFMT_A8R8G8B8, Format);

    CheckSharedSurfaceRuntimeData(Surface, Width, Height);
    CheckSharedSurfaceClientOpen(Surface, Width, Height);

    hr = pDwmpDxGetWindowSharedSurface(Window, Luid, NULL, 0,
                                       &Format, &Surface2, &SecondId);
    ok(hr == S_FALSE,
       "a second acquisition before the outstanding update is retired must drop "
       "the frame with S_FALSE, got 0x%08lX\n", (unsigned long)hr);

    hr = pDwmpDxUpdateWindowSharedSurface(Window, FirstId,
                                          DWM_DX_UPDATE_CANCEL, NULL, NULL);
    ok(hr == S_OK, "cancelling the issued update must succeed, got 0x%08lX\n",
       (unsigned long)hr);

    Surface2 = NULL;
    SecondId = 0;
    hr = pDwmpDxGetWindowSharedSurface(Window, Luid, NULL, 0,
                                       &Format, &Surface2, &SecondId);
    ok(hr == S_OK,
       "acquisition must succeed again once the previous update is cancelled, "
       "got 0x%08lX\n", (unsigned long)hr);
    ok(Surface2 == Surface,
       "an unchanged window must keep the same shared surface, got %p vs %p\n",
       (void *)Surface2, (void *)Surface);
    ok(SecondId > FirstId,
       "update ids must increase monotonically, got %I64u after %I64u\n",
       SecondId, FirstId);

    hr = pDwmpDxUpdateWindowSharedSurface(Window, FirstId, 0, NULL,
                                          &Client);
    ok(FAILED(hr), "publishing a stale update id must fail, got 0x%08lX\n",
       (unsigned long)hr);

    Rect = Client;
    ++Rect.right;
    hr = pDwmpDxUpdateWindowSharedSurface(Window, SecondId, 0, NULL, &Rect);
    ok(FAILED(hr),
       "an update rect wider than the client bounds must be refused, got 0x%08lX\n",
       (unsigned long)hr);

    Rect = Client;
    --Rect.left;
    hr = pDwmpDxUpdateWindowSharedSurface(Window, SecondId, 0, NULL, &Rect);
    ok(FAILED(hr),
       "an update rect outside the client origin must be refused, got 0x%08lX\n",
       (unsigned long)hr);

    SetRectEmpty(&Rect);
    hr = pDwmpDxUpdateWindowSharedSurface(Window, SecondId, 0, NULL, &Rect);
    ok(FAILED(hr), "an empty update rect must be refused, got 0x%08lX\n",
       (unsigned long)hr);

    hr = pDwmpDxUpdateWindowSharedSurface(Window, SecondId, 4, NULL,
                                          &Client);
    ok(FAILED(hr), "a reserved update flag bit must be refused, got 0x%08lX\n",
       (unsigned long)hr);

    /* The shared allocation is client-sized. Adding the non-client origin
     * to a full-client update would run beyond its right/bottom edges. */
    hr = pDwmpDxUpdateWindowSharedSurface(Window, SecondId, 0, NULL, &ClientInWindow);
    ok(FAILED(hr),
       "a window-relative rect must not exceed the client-sized allocation, "
       "got 0x%08lX\n", (unsigned long)hr);

    hr = pDwmpDxUpdateWindowSharedSurface(Window, SecondId, 0, NULL,
                                          &Client);
    ok(hr == S_OK, "publishing the issued update must succeed, got 0x%08lX\n",
       (unsigned long)hr);

    Surface2 = NULL;
    hr = pDwmpDxGetWindowSharedSurface(Window, Luid, NULL, 0,
                                       &Format, &Surface2, &SecondId);
    ok(hr == S_FALSE || hr == S_OK,
       "after a publish the next acquisition must either wait for the compositor "
       "(S_FALSE) or start a new update (S_OK), got 0x%08lX\n",
       (unsigned long)hr);
    if (hr == S_OK)
    {
        trace("the compositor consumed the published update before the retry\n");
        (void)pDwmpDxUpdateWindowSharedSurface(Window, SecondId,
                                               DWM_DX_UPDATE_CANCEL, NULL, NULL);
    }

    WrongLuid = Luid;
    WrongLuid.LowPart ^= 0xFFFFFFFFu;
    Surface2 = NULL;
    hr = pDwmpDxGetWindowSharedSurface(Window, WrongLuid, NULL, 0,
                                       &Format, &Surface2, &SecondId);
    ok(FAILED(hr) || Surface2 != Surface,
       "a different adapter LUID must not hand back the surface registered for "
       "another adapter, got 0x%08lX surface=%p\n",
       (unsigned long)hr, (void *)Surface2);

    DestroyWindow(Window);
}

/* ------------------------------------------------------------------ */
/* The adapter-LUID resolution the OpenGL ICD present path depends on   */
/* ------------------------------------------------------------------ */
START_TEST(dwmdxluid)
{
    PFN_D3DKMTOpenAdapterFromHdc pfnFromHdc;
    PFN_D3DKMTOpenAdapterFromGdiDisplayName pfnFromName;
    PFN_D3DKMTCloseAdapter pfnClose;
    D3DKMT_OPENADAPTERFROMHDC FromHdc;
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME FromName;
    D3DKMT_CLOSEADAPTER Close;
    MONITORINFOEXW MonitorInfo;
    HMONITOR Monitor;
    HWND Window;
    HDC hdc;
    NTSTATUS Status;
    BOOL HaveHdcLuid = FALSE;
    LUID HdcLuid;

    pfnFromHdc = (PFN_D3DKMTOpenAdapterFromHdc)
        LoadD3DKMTProc("D3DKMTOpenAdapterFromHdc");
    pfnFromName = (PFN_D3DKMTOpenAdapterFromGdiDisplayName)
        LoadD3DKMTProc("D3DKMTOpenAdapterFromGdiDisplayName");
    pfnClose = (PFN_D3DKMTCloseAdapter)LoadD3DKMTProc("D3DKMTCloseAdapter");
    if (pfnFromHdc == NULL || pfnFromName == NULL || pfnClose == NULL)
    {
        skip("the D3DKMT adapter entry points are not exported by gdi32.dll\n");
        return;
    }

    Window = CreateParityWindow(320, 240);
    if (Window == NULL)
    {
        skip("could not create the parity test window\n");
        return;
    }

    hdc = GetDC(Window);
    if (hdc == NULL)
    {
        skip("GetDC on the test window failed\n");
        DestroyWindow(Window);
        return;
    }

    ok(WindowFromDC(hdc) == Window,
       "WindowFromDC must resolve a window DC back to its window\n");

    memset(&HdcLuid, 0, sizeof(HdcLuid));
    memset(&FromHdc, 0, sizeof(FromHdc));
    FromHdc.hDc = hdc;
    Status = pfnFromHdc(&FromHdc);
    if (NT_SUCCESS(Status) && FromHdc.hAdapter != 0)
    {
        HdcLuid = FromHdc.AdapterLuid;
        HaveHdcLuid = TRUE;
        memset(&Close, 0, sizeof(Close));
        Close.hAdapter = FromHdc.hAdapter;
        pfnClose(&Close);
    }
    else
    {
        trace("D3DKMTOpenAdapterFromHdc(window DC) returned 0x%08lX; the monitor "
              "fallback is the only path for this DC\n", (unsigned long)Status);
    }

    Monitor = MonitorFromWindow(Window, MONITOR_DEFAULTTONEAREST);
    ok(Monitor != NULL, "MonitorFromWindow must resolve a monitor\n");

    memset(&MonitorInfo, 0, sizeof(MonitorInfo));
    MonitorInfo.cbSize = sizeof(MonitorInfo);
    ok(Monitor != NULL &&
       GetMonitorInfoW(Monitor, (MONITORINFO *)&MonitorInfo) &&
       MonitorInfo.szDevice[0] != L'\0',
       "GetMonitorInfoW must return a GDI device name for the window's monitor\n");

    if (MonitorInfo.szDevice[0] != L'\0')
    {
        memset(&FromName, 0, sizeof(FromName));
        lstrcpynW(FromName.DeviceName, MonitorInfo.szDevice,
                  ARRAYSIZE(FromName.DeviceName));
        Status = pfnFromName(&FromName);
        ok(NT_SUCCESS(Status) && FromName.hAdapter != 0,
           "the window monitor's GDI device name must open a D3DKMT adapter, "
           "got 0x%08lX\n", (unsigned long)Status);

        if (NT_SUCCESS(Status) && FromName.hAdapter != 0)
        {
            ok(FromName.AdapterLuid.LowPart != 0 ||
               FromName.AdapterLuid.HighPart != 0,
               "a resolved adapter must have a non-zero LUID\n");

            if (HaveHdcLuid)
            {
                ok(FromName.AdapterLuid.LowPart == HdcLuid.LowPart &&
                   FromName.AdapterLuid.HighPart == HdcLuid.HighPart,
                   "the monitor fallback must resolve the same adapter as the HDC "
                   "path, got %08lX:%08lX vs %08lX:%08lX\n",
                   (unsigned long)FromName.AdapterLuid.HighPart,
                   (unsigned long)FromName.AdapterLuid.LowPart,
                   (unsigned long)HdcLuid.HighPart,
                   (unsigned long)HdcLuid.LowPart);
            }

            memset(&Close, 0, sizeof(Close));
            Close.hAdapter = FromName.hAdapter;
            pfnClose(&Close);
        }
    }

    ReleaseDC(Window, hdc);
    DestroyWindow(Window);
}

/* ------------------------------------------------------------------ */
/* The raw win32k exchange -- validation dwmapi cannot reach            */
/* ------------------------------------------------------------------ */
static void
TestNativePublicationArguments(PFN_NTUSERCALLONEPARAM pNtUserCallOneParam,
                               HWND Window, const LUID *Luid)
{
    DWM_DX_SURFACE_EXCHANGE Exchange;
    HANDLE ReadyEvent;
    RECT Client;
    NTSTATUS Status;

    ReadyEvent = CreateEventW(NULL, TRUE, TRUE, NULL);
    ok(ReadyEvent != NULL, "could not create the publication completion event\n");
    if (ReadyEvent == NULL)
        return;
    GetClientRect(Window, &Client);
    memset(&Exchange, 0, sizeof(Exchange));
    Exchange.StructSize = sizeof(Exchange);
    Exchange.Action = DWM_DX_SURFACE_PUBLISH;
    Exchange.Window = (ULONGLONG)(ULONG_PTR)Window;
    Exchange.AdapterLuid = *Luid;
    Exchange.GlobalShare = 0xDEADBEEF;
    Exchange.ReadyEvent = (ULONGLONG)(ULONG_PTR)ReadyEvent;
    Exchange.Info.Magic = DWM_DX_SURFACE_INFO_MAGIC;
    Exchange.Info.Version = DWM_DX_SURFACE_INFO_VERSION_GPU;
    Exchange.Info.Width = Client.right;
    Exchange.Info.Height = Client.bottom;
    Exchange.Info.Format = DWM_DX_FORMAT_B8G8R8A8_UNORM;
    Exchange.UpdateRect.left = Client.left;
    Exchange.UpdateRect.top = Client.top;
    Exchange.UpdateRect.right = Client.right;
    Exchange.UpdateRect.bottom = Client.bottom;
    Exchange.Generation = 0xabcdef;
    Exchange.UpdateId = 0x123456;

    Status = (NTSTATUS)pNtUserCallOneParam((DWORD_PTR)&Exchange, DWM_ROUTINE_DXSURFACE);
    ok(Status == STATUS_INVALID_HANDLE,
       "PUBLISH must reject a fabricated GPU share, got 0x%08lX\n", (unsigned long)Status);
    ok(Exchange.Generation == 0xabcdef && Exchange.UpdateId == 0x123456 &&
       WaitForSingleObject(ReadyEvent, 0) == WAIT_OBJECT_0,
       "rejected PUBLISH must preserve output identity and completion event\n");

    Exchange.Flags = DWM_DX_PUBLISH_PREMULTIPLIED;
    Status = (NTSTATUS)pNtUserCallOneParam((DWORD_PTR)&Exchange, DWM_ROUTINE_DXSURFACE);
    ok(Status == STATUS_INVALID_HANDLE,
       "premultiplied publication must validate resource ownership: 0x%08lX\n", (unsigned long)Status);
    Exchange.Flags = 0x80000000u;
    Status = (NTSTATUS)pNtUserCallOneParam((DWORD_PTR)&Exchange, DWM_ROUTINE_DXSURFACE);
    ok(Status == STATUS_INVALID_PARAMETER,
       "unknown native publication flags must be rejected: 0x%08lX\n", (unsigned long)Status);
    Exchange.Flags = DWM_DX_PUBLISH_SCANOUT;
    Status = (NTSTATUS)pNtUserCallOneParam((DWORD_PTR)&Exchange, DWM_ROUTINE_DXSURFACE);
    ok(Status == STATUS_INVALID_PARAMETER,
       "a scanout publication must be retained: 0x%08lX\n", (unsigned long)Status);
    Exchange.Flags = 0;

    Exchange.Info.Pitch = Client.right * sizeof(ULONG);
    Status = (NTSTATUS)pNtUserCallOneParam((DWORD_PTR)&Exchange, DWM_ROUTINE_DXSURFACE);
    ok(Status == STATUS_INVALID_PARAMETER,
       "native GPU publication must reject a supplied linear pitch, got 0x%08lX\n", (unsigned long)Status);
    Exchange.Info.Pitch = 0;
    Exchange.UpdateRect.right++;
    Status = (NTSTATUS)pNtUserCallOneParam((DWORD_PTR)&Exchange, DWM_ROUTINE_DXSURFACE);
    ok(Status == STATUS_INVALID_PARAMETER,
       "PUBLISH damage outside the client must be rejected, got 0x%08lX\n", (unsigned long)Status);
    Exchange.UpdateRect.right--;
    Exchange.UpdateRect.left = 1;
    Status = (NTSTATUS)pNtUserCallOneParam((DWORD_PTR)&Exchange, DWM_ROUTINE_DXSURFACE);
    ok(Status == STATUS_INVALID_PARAMETER,
       "native publication must require a complete client image, got 0x%08lX\n", (unsigned long)Status);
    Exchange.UpdateRect.left = 0;
    Exchange.Window = (ULONGLONG)(ULONG_PTR)GetDesktopWindow();
    Status = (NTSTATUS)pNtUserCallOneParam((DWORD_PTR)&Exchange, DWM_ROUTINE_DXSURFACE);
    ok(Status == STATUS_ACCESS_DENIED,
       "PUBLISH must reject a foreign-process window, got 0x%08lX\n", (unsigned long)Status);

    Exchange.Window = (ULONGLONG)(ULONG_PTR)Window;
    Exchange.Action = DWM_DX_SURFACE_ISSUE;
    Status = (NTSTATUS)pNtUserCallOneParam((DWORD_PTR)&Exchange, DWM_ROUTINE_DXSURFACE);
    ok(Status == STATUS_INVALID_PARAMETER,
       "a rejected PUBLISH must leave the window unregistered, got 0x%08lX\n", (unsigned long)Status);
    CloseHandle(ReadyEvent);
}

START_TEST(dwmdxntuser)
{
    PFN_NTUSERCALLONEPARAM pNtUserCallOneParam;
    DWM_DX_SURFACE_EXCHANGE Exchange;
    HMODULE Win32u;
    HWND Window;
    LUID Luid;
    NTSTATUS Status;

    if (!RunningOnReactOS())
    {
        skip("the DWM exchange rides a ReactOS-private NtUserCallOneParam "
             "routine number\n");
        return;
    }

    Win32u = GetModuleHandleW(L"win32u.dll");
    if (Win32u == NULL)
        Win32u = LoadLibraryW(L"win32u.dll");
    if (Win32u == NULL)
    {
        skip("win32u.dll is not present\n");
        return;
    }

    pNtUserCallOneParam = (PFN_NTUSERCALLONEPARAM)
        GetProcAddress(Win32u, "NtUserCallOneParam");
    if (pNtUserCallOneParam == NULL)
    {
        skip("win32u.dll does not export NtUserCallOneParam\n");
        return;
    }

    Status = (NTSTATUS)pNtUserCallOneParam(0, DWM_ROUTINE_PRESENTED);
    ok(Status == STATUS_ACCESS_DENIED,
       "Only the compositor may complete a flush barrier, got 0x%08lX\n",
       (unsigned long)Status);

    memset(&Luid, 0, sizeof(Luid));
    (void)GetDisplay1Luid(&Luid);

    Window = CreateParityWindow(320, 240);
    if (Window == NULL)
    {
        skip("could not create the parity test window\n");
        return;
    }

    memset(&Exchange, 0, sizeof(Exchange));
    Exchange.StructSize = sizeof(Exchange);
    Exchange.Action = DWM_DX_SURFACE_ISSUE;
    Exchange.Window = (ULONGLONG)(ULONG_PTR)Window;
    Exchange.AdapterLuid = Luid;
    Exchange.GlobalShare = 0xDEADBEEF;
    Status = (NTSTATUS)pNtUserCallOneParam((DWORD_PTR)&Exchange,
                                           DWM_ROUTINE_DXSURFACE);
    if (Status == STATUS_DEVICE_NOT_READY)
    {
        skip("no compositor attached; the exchange refuses every action\n");
        DestroyWindow(Window);
        return;
    }
    ok(Status == STATUS_INVALID_PARAMETER || Status == STATUS_NOT_FOUND,
       "ISSUE naming an unregistered share handle must be refused, got 0x%08lX\n",
       (unsigned long)Status);

    memset(&Exchange, 0, sizeof(Exchange));
    Exchange.StructSize = sizeof(Exchange) - 4;
    Exchange.Action = DWM_DX_SURFACE_REGISTER;
    Exchange.Window = (ULONGLONG)(ULONG_PTR)Window;
    Status = (NTSTATUS)pNtUserCallOneParam((DWORD_PTR)&Exchange,
                                           DWM_ROUTINE_DXSURFACE);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH,
       "a mismatched StructSize must be STATUS_INFO_LENGTH_MISMATCH, got 0x%08lX\n",
       (unsigned long)Status);

    memset(&Exchange, 0, sizeof(Exchange));
    Exchange.StructSize = sizeof(Exchange);
    Exchange.Action = DWM_DX_SURFACE_CONSUMED;
    Exchange.SurfaceId = 0;
    Exchange.UpdateId = 1;
    Status = (NTSTATUS)pNtUserCallOneParam((DWORD_PTR)&Exchange,
                                           DWM_ROUTINE_DXSURFACE);
    ok(Status == STATUS_ACCESS_DENIED,
       "CONSUMED from a process other than the attached compositor must be "
       "STATUS_ACCESS_DENIED, got 0x%08lX\n", (unsigned long)Status);

    memset(&Exchange, 0, sizeof(Exchange));
    Exchange.StructSize = sizeof(Exchange);
    Exchange.Action = DWM_DX_SURFACE_REGISTER;
    Exchange.Window = 0;
    Status = (NTSTATUS)pNtUserCallOneParam((DWORD_PTR)&Exchange,
                                           DWM_ROUTINE_DXSURFACE);
    ok(Status == STATUS_INVALID_HANDLE,
       "a zero window must be STATUS_INVALID_HANDLE, got 0x%08lX\n",
       (unsigned long)Status);

    memset(&Exchange, 0, sizeof(Exchange));
    Exchange.StructSize = sizeof(Exchange);
    Exchange.Action = DWM_DX_SURFACE_REGISTER;
    Exchange.Window = (ULONGLONG)(ULONG_PTR)GetDesktopWindow();
    Status = (NTSTATUS)pNtUserCallOneParam((DWORD_PTR)&Exchange,
                                           DWM_ROUTINE_DXSURFACE);
    ok(Status == STATUS_ACCESS_DENIED,
       "a window owned by another process must be STATUS_ACCESS_DENIED, "
       "got 0x%08lX\n", (unsigned long)Status);

    memset(&Exchange, 0, sizeof(Exchange));
    Exchange.StructSize = sizeof(Exchange);
    Exchange.Action = 0x99;
    Exchange.Window = (ULONGLONG)(ULONG_PTR)Window;
    Status = (NTSTATUS)pNtUserCallOneParam((DWORD_PTR)&Exchange,
                                           DWM_ROUTINE_DXSURFACE);
    ok(Status == STATUS_INVALID_PARAMETER || Status == STATUS_NOT_FOUND,
       "an unknown action must be refused, got 0x%08lX\n", (unsigned long)Status);

    memset(&Exchange, 0, sizeof(Exchange));
    Exchange.StructSize = sizeof(Exchange);
    Exchange.Action = DWM_DX_SURFACE_REGISTER;
    Exchange.Window = (ULONGLONG)(ULONG_PTR)Window;
    Exchange.AdapterLuid = Luid;
    Exchange.GlobalShare = 0;
    Exchange.Info.Magic = DWM_DX_SURFACE_INFO_MAGIC;
    Exchange.Info.Version = DWM_DX_SURFACE_INFO_VERSION;
    Exchange.Info.Width = 320;
    Exchange.Info.Height = 240;
    Exchange.Info.Pitch = 320 * 4;
    Exchange.Info.Format = DWM_DX_FORMAT_B8G8R8A8_UNORM;
    Status = (NTSTATUS)pNtUserCallOneParam((DWORD_PTR)&Exchange,
                                           DWM_ROUTINE_DXSURFACE);
    ok(Status == STATUS_INVALID_PARAMETER,
       "REGISTER with a zero global-share handle must be refused, got 0x%08lX\n",
       (unsigned long)Status);

    memset(&Exchange, 0, sizeof(Exchange));
    Exchange.StructSize = sizeof(Exchange);
    Exchange.Action = DWM_DX_SURFACE_REGISTER;
    Exchange.Window = (ULONGLONG)(ULONG_PTR)Window;
    Exchange.AdapterLuid = Luid;
    Exchange.GlobalShare = 0xDEADBEEF;
    Exchange.Info.Magic = DWM_DX_SURFACE_INFO_MAGIC;
    Exchange.Info.Version = DWM_DX_SURFACE_INFO_VERSION;
    Exchange.Info.Width = 320;
    Exchange.Info.Height = 240;
    Exchange.Info.Pitch = 320 * 2;
    Exchange.Info.Format = DWM_DX_FORMAT_B8G8R8A8_UNORM;
    Status = (NTSTATUS)pNtUserCallOneParam((DWORD_PTR)&Exchange,
                                           DWM_ROUTINE_DXSURFACE);
    ok(Status == STATUS_INVALID_PARAMETER,
       "REGISTER with a pitch that is not width * 4 must be refused, "
       "got 0x%08lX\n", (unsigned long)Status);

    Status = (NTSTATUS)pNtUserCallOneParam((DWORD_PTR)(ULONG_PTR)0xDEADBEEF,
                                           DWM_ROUTINE_DXSURFACE);
    ok(!NT_SUCCESS(Status),
       "an unreadable exchange pointer must be refused, got 0x%08lX\n",
       (unsigned long)Status);

    TestNativePublicationArguments(pNtUserCallOneParam, Window, &Luid);
    DestroyWindow(Window);
}
