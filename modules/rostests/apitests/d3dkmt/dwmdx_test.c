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

    ok(ByName100 == NULL,
       "Windows exports dwmapi ordinal 100 with no name; exporting a name is a "
       "parity deviation (the spec entry needs -noname), got %p\n",
       (void *)ByName100);
    ok(ByName101 == NULL,
       "Windows exports dwmapi ordinal 101 with no name; exporting a name is a "
       "parity deviation (the spec entry needs -noname), got %p\n",
       (void *)ByName101);

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

C_ASSERT(sizeof(DWM_DX_SURFACE_EXCHANGE) == 88);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, StructSize) == 0);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, Action) == 4);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, Window) == 8);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, AdapterLuid) == 16);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, GlobalShare) == 24);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, SurfaceId) == 28);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, Generation) == 32);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, Flags) == 36);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, Info) == 40);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, UpdateId) == 64);
C_ASSERT(FIELD_OFFSET(DWM_DX_SURFACE_EXCHANGE, UpdateRect) == 72);

C_ASSERT(sizeof(DWM_WIN) == 92);
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

C_ASSERT(sizeof(DWM_FRAME_HEADER) == 48);
C_ASSERT(DWM_WINARRAY_BASE == 48);
C_ASSERT(DWM_FRAME_BYTES == 48 + 256 * 92);

START_TEST(dwmdxabi)
{
    ok(sizeof(DWM_DX_SURFACE_EXCHANGE) == 88,
       "DWM_DX_SURFACE_EXCHANGE must be 88 bytes on every architecture, got %u\n",
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
    ok(DWM_FRAME_MAGIC == 0x334d5744u,
       "the frame magic must be bumped to 'DWM3' now that DWM_WIN carries the Dx "
       "fields, got 0x%08lX\n", (unsigned long)DWM_FRAME_MAGIC);
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

    hAdapter = OpenAdapterFromDisplay1();
    if (hAdapter == 0)
    {
        skip("could not open the DISPLAY1 adapter\n");
        return;
    }

    hDevice = CreateTestDevice(hAdapter);
    if (hDevice == 0)
    {
        skip("could not create a D3DKMT device on the display adapter\n");
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
            ok(Info.Pitch == Info.Width * 4,
               "a linear B8G8R8A8 surface must have pitch = width * 4 (%lu), "
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

    hAdapter = OpenAdapterFromDisplay1();
    if (hAdapter == 0)
    {
        skip("could not open the DISPLAY1 adapter\n");
        return;
    }
    hDevice = CreateTestDevice(hAdapter);
    if (hDevice == 0)
    {
        skip("could not create a D3DKMT device on the display adapter\n");
        CloseAdapter(hAdapter);
        return;
    }

    memset(&Query, 0, sizeof(Query));
    Query.hDevice = hDevice;
    Query.hGlobalShare = (D3DKMT_HANDLE)(ULONG_PTR)SharedSurface;
    Status = pfnQuery(&Query);
    if (!NT_SUCCESS(Status))
    {
        skip("QueryResourceInfo on the shared surface failed with 0x%08lX\n",
             (unsigned long)Status);
        goto Cleanup;
    }

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
       Info.Pitch == Width * 4 &&
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

START_TEST(dwmdxsurface)
{
    HWND Window;
    LUID Luid, WrongLuid;
    HANDLE Surface = NULL, Surface2 = NULL;
    ULONGLONG FirstId = 0, SecondId = 0;
    UINT Format = 0;
    RECT Client, Rect;
    ULONG Width, Height;
    HRESULT hr;

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

    Window = CreateParityWindow(320, 240);
    if (Window == NULL)
    {
        skip("could not create the parity test window\n");
        return;
    }
    GetClientRect(Window, &Client);
    Width = (ULONG)(Client.right - Client.left);
    Height = (ULONG)(Client.bottom - Client.top);

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
    ok(Format == DWM_DX_FORMAT_B8G8R8A8_UNORM,
       "the surface format must be DXGI_FORMAT_B8G8R8A8_UNORM (%u), got %u\n",
       DWM_DX_FORMAT_B8G8R8A8_UNORM, Format);

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

    hr = pDwmpDxUpdateWindowSharedSurface(Window, FirstId, 0, NULL, &Client);
    ok(FAILED(hr), "publishing a stale update id must fail, got 0x%08lX\n",
       (unsigned long)hr);

    SetRect(&Rect, 0, 0, (int)Width + 1, (int)Height);
    hr = pDwmpDxUpdateWindowSharedSurface(Window, SecondId, 0, NULL, &Rect);
    ok(FAILED(hr),
       "an update rect wider than the surface must be refused, got 0x%08lX\n",
       (unsigned long)hr);

    SetRect(&Rect, -1, 0, (int)Width, (int)Height);
    hr = pDwmpDxUpdateWindowSharedSurface(Window, SecondId, 0, NULL, &Rect);
    ok(FAILED(hr),
       "a negative update-rect origin must be refused, got 0x%08lX\n",
       (unsigned long)hr);

    SetRectEmpty(&Rect);
    hr = pDwmpDxUpdateWindowSharedSurface(Window, SecondId, 0, NULL, &Rect);
    ok(FAILED(hr), "an empty update rect must be refused, got 0x%08lX\n",
       (unsigned long)hr);

    hr = pDwmpDxUpdateWindowSharedSurface(Window, SecondId, 4, NULL, &Client);
    ok(FAILED(hr), "a reserved update flag bit must be refused, got 0x%08lX\n",
       (unsigned long)hr);

    hr = pDwmpDxUpdateWindowSharedSurface(Window, SecondId, 0, NULL, &Client);
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

    DestroyWindow(Window);
}
