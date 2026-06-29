/*
 * PROJECT:     ReactOS DWM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Public dwmapi.dll behavior required by Aero clients
 */

#include "precomp.h"

typedef HRESULT (WINAPI *PFN_DwmEnableBlurBehindWindow)(HWND, const DWM_BLURBEHIND *);
typedef HRESULT (WINAPI *PFN_DwmEnableComposition)(UINT);
typedef HRESULT (WINAPI *PFN_DwmExtendFrameIntoClientArea)(HWND, const MARGINS *);
typedef HRESULT (WINAPI *PFN_DwmFlush)(void);
typedef HRESULT (WINAPI *PFN_DwmGetColorizationColor)(DWORD *, BOOL *);
typedef HRESULT (WINAPI *PFN_DwmGetCompositionTimingInfo)(HWND, DWM_TIMING_INFO *);
typedef HRESULT (WINAPI *PFN_DwmGetWindowAttribute)(HWND, DWORD, PVOID, DWORD);
typedef HRESULT (WINAPI *PFN_DwmIsCompositionEnabled)(BOOL *);
typedef HRESULT (WINAPI *PFN_DwmRegisterThumbnail)(HWND, HWND, PHTHUMBNAIL);
typedef HRESULT (WINAPI *PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
typedef HRESULT (WINAPI *PFN_DwmUnregisterThumbnail)(HTHUMBNAIL);

typedef struct _DWM_PUBLIC_API
{
    PFN_DwmEnableBlurBehindWindow EnableBlurBehindWindow;
    PFN_DwmEnableComposition EnableComposition;
    PFN_DwmExtendFrameIntoClientArea ExtendFrameIntoClientArea;
    PFN_DwmFlush Flush;
    PFN_DwmGetColorizationColor GetColorizationColor;
    PFN_DwmGetCompositionTimingInfo GetCompositionTimingInfo;
    PFN_DwmGetWindowAttribute GetWindowAttribute;
    PFN_DwmIsCompositionEnabled IsCompositionEnabled;
    PFN_DwmRegisterThumbnail RegisterThumbnail;
    PFN_DwmSetWindowAttribute SetWindowAttribute;
    PFN_DwmUnregisterThumbnail UnregisterThumbnail;
} DWM_PUBLIC_API;

static FARPROC
LoadDwmProc(_In_ HMODULE Module, _In_z_ LPCSTR Name)
{
    FARPROC Proc = GetProcAddress(Module, Name);
    ok(Proc != NULL, "%s is not exported by dwmapi.dll\n", Name);
    return Proc;
}

static BOOL
LoadDwmPublicApi(_Out_ DWM_PUBLIC_API *Api)
{
    HMODULE Module;

    ZeroMemory(Api, sizeof(*Api));

    Module = LoadLibraryW(L"dwmapi.dll");
    ok(Module != NULL, "LoadLibraryW(dwmapi.dll) failed, error %lu\n",
       GetLastError());
    if (!Module)
        return FALSE;

    Api->EnableBlurBehindWindow = (PFN_DwmEnableBlurBehindWindow)
        LoadDwmProc(Module, "DwmEnableBlurBehindWindow");
    Api->EnableComposition = (PFN_DwmEnableComposition)
        LoadDwmProc(Module, "DwmEnableComposition");
    Api->ExtendFrameIntoClientArea = (PFN_DwmExtendFrameIntoClientArea)
        LoadDwmProc(Module, "DwmExtendFrameIntoClientArea");
    Api->Flush = (PFN_DwmFlush)
        LoadDwmProc(Module, "DwmFlush");
    Api->GetColorizationColor = (PFN_DwmGetColorizationColor)
        LoadDwmProc(Module, "DwmGetColorizationColor");
    Api->GetCompositionTimingInfo = (PFN_DwmGetCompositionTimingInfo)
        LoadDwmProc(Module, "DwmGetCompositionTimingInfo");
    Api->GetWindowAttribute = (PFN_DwmGetWindowAttribute)
        LoadDwmProc(Module, "DwmGetWindowAttribute");
    Api->IsCompositionEnabled = (PFN_DwmIsCompositionEnabled)
        LoadDwmProc(Module, "DwmIsCompositionEnabled");
    Api->RegisterThumbnail = (PFN_DwmRegisterThumbnail)
        LoadDwmProc(Module, "DwmRegisterThumbnail");
    Api->SetWindowAttribute = (PFN_DwmSetWindowAttribute)
        LoadDwmProc(Module, "DwmSetWindowAttribute");
    Api->UnregisterThumbnail = (PFN_DwmUnregisterThumbnail)
        LoadDwmProc(Module, "DwmUnregisterThumbnail");

    return Api->EnableBlurBehindWindow &&
           Api->EnableComposition &&
           Api->ExtendFrameIntoClientArea &&
           Api->Flush &&
           Api->GetColorizationColor &&
           Api->GetCompositionTimingInfo &&
           Api->GetWindowAttribute &&
           Api->IsCompositionEnabled &&
           Api->RegisterThumbnail &&
           Api->SetWindowAttribute &&
           Api->UnregisterThumbnail;
}

static LRESULT CALLBACK
DwmPublicApiWndProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(hWnd, Message, wParam, lParam);
}

static HWND
CreateDwmTestWindow(_In_z_ LPCWSTR ClassName, _In_z_ LPCWSTR Title)
{
    WNDCLASSW Class;
    HWND Window;

    ZeroMemory(&Class, sizeof(Class));
    Class.lpfnWndProc = DwmPublicApiWndProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    Class.lpszClassName = ClassName;
    ok(RegisterClassW(&Class) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS,
       "RegisterClassW(%S) failed, error %lu\n", ClassName, GetLastError());

    Window = CreateWindowExW(0,
                             ClassName,
                             Title,
                             WS_OVERLAPPEDWINDOW,
                             32,
                             32,
                             320,
                             240,
                             NULL,
                             NULL,
                             GetModuleHandleW(NULL),
                             NULL);
    ok(Window != NULL, "CreateWindowExW(%S) failed, error %lu\n",
       ClassName, GetLastError());
    if (Window)
    {
        ShowWindow(Window, SW_SHOWNORMAL);
        UpdateWindow(Window);
    }

    return Window;
}

static void
DestroyDwmTestWindow(HWND Window, _In_z_ LPCWSTR ClassName)
{
    if (Window)
        DestroyWindow(Window);
    UnregisterClassW(ClassName, GetModuleHandleW(NULL));
}

static BOOL
ExpectCompositionDisabledAllowed(HRESULT Result, BOOL CompositionEnabled)
{
    return Result == S_OK ||
           (!CompositionEnabled && Result == DWM_E_COMPOSITIONDISABLED);
}

START_TEST(DwmPublicApi)
{
    DWM_PUBLIC_API Api;
    HWND Dest, Source;
    BOOL CompositionEnabled = 0x7f;
    DWORD Color = 0;
    BOOL Opaque = 0x7f;
    HRESULT Result;
    DWM_TIMING_INFO Timing;
    RECT Rect;
    BOOL BoolValue;
    DWORD DwordValue;
    INT Policy;
    MARGINS Margins;
    DWM_BLURBEHIND Blur;
    HTHUMBNAIL Thumbnail = NULL;

    if (!LoadDwmPublicApi(&Api))
        return;

    Dest = CreateDwmTestWindow(L"RosDwmPublicApiDest", L"DWM public API dest");
    Source = CreateDwmTestWindow(L"RosDwmPublicApiSource", L"DWM public API source");
    if (!Dest || !Source)
    {
        DestroyDwmTestWindow(Source, L"RosDwmPublicApiSource");
        DestroyDwmTestWindow(Dest, L"RosDwmPublicApiDest");
        return;
    }

    Result = Api.IsCompositionEnabled(NULL);
    ok(Result == E_INVALIDARG,
       "DwmIsCompositionEnabled(NULL) returned 0x%08lx\n", Result);

    Result = Api.IsCompositionEnabled(&CompositionEnabled);
    ok(Result == S_OK, "DwmIsCompositionEnabled failed with 0x%08lx\n", Result);
    ok(CompositionEnabled == FALSE || CompositionEnabled == TRUE,
       "DwmIsCompositionEnabled returned non-BOOL value %d\n",
       CompositionEnabled);
    trace("DwmIsCompositionEnabled=%d\n", CompositionEnabled);

    Result = Api.GetColorizationColor(NULL, &Opaque);
    ok(Result == E_INVALIDARG,
       "DwmGetColorizationColor(NULL, &Opaque) returned 0x%08lx\n", Result);
    Result = Api.GetColorizationColor(&Color, NULL);
    ok(Result == E_INVALIDARG,
       "DwmGetColorizationColor(&Color, NULL) returned 0x%08lx\n", Result);
    Result = Api.GetColorizationColor(&Color, &Opaque);
    ok(Result == S_OK, "DwmGetColorizationColor failed with 0x%08lx\n", Result);
    ok(Opaque == FALSE || Opaque == TRUE,
       "DwmGetColorizationColor returned non-BOOL opaque value %d\n", Opaque);
    trace("Dwm colorization color=0x%08lx opaque=%d\n", Color, Opaque);

    Result = Api.Flush();
    ok(ExpectCompositionDisabledAllowed(Result, CompositionEnabled),
       "DwmFlush returned 0x%08lx with composition=%d\n",
       Result, CompositionEnabled);

    ZeroMemory(&Timing, sizeof(Timing));
    Result = Api.GetCompositionTimingInfo(Dest, &Timing);
    ok(Result == MILERR_MISMATCHED_SIZE || Result == E_INVALIDARG,
       "DwmGetCompositionTimingInfo with cbSize=0 returned 0x%08lx\n", Result);

    ZeroMemory(&Timing, sizeof(Timing));
    Timing.cbSize = sizeof(Timing);
    Result = Api.GetCompositionTimingInfo(Dest, &Timing);
    ok(ExpectCompositionDisabledAllowed(Result, CompositionEnabled),
       "DwmGetCompositionTimingInfo returned 0x%08lx with composition=%d\n",
       Result, CompositionEnabled);
    if (Result == S_OK)
    {
        ok(Timing.cbSize == sizeof(Timing),
           "Timing cbSize is %u, expected %Iu\n",
           Timing.cbSize, sizeof(Timing));
        ok(Timing.rateRefresh.uiDenominator != 0,
           "Timing refresh denominator is zero\n");
        ok(Timing.rateCompose.uiDenominator != 0,
           "Timing compose denominator is zero\n");
    }

    Result = Api.GetWindowAttribute(NULL,
                                    DWMWA_NCRENDERING_ENABLED,
                                    &BoolValue,
                                    sizeof(BoolValue));
    ok(Result == E_INVALIDARG || Result == E_HANDLE,
       "DwmGetWindowAttribute(NULL) returned 0x%08lx\n", Result);

    BoolValue = 0x7f;
    Result = Api.GetWindowAttribute(Dest,
                                    DWMWA_NCRENDERING_ENABLED,
                                    &BoolValue,
                                    sizeof(BoolValue));
    ok(ExpectCompositionDisabledAllowed(Result, CompositionEnabled),
       "DwmGetWindowAttribute(NCRENDERING_ENABLED) returned 0x%08lx\n",
       Result);
    if (Result == S_OK)
        ok(BoolValue == FALSE || BoolValue == TRUE,
           "NCRENDERING_ENABLED returned non-BOOL value %d\n", BoolValue);

    SetRectEmpty(&Rect);
    Result = Api.GetWindowAttribute(Dest,
                                    DWMWA_EXTENDED_FRAME_BOUNDS,
                                    &Rect,
                                    sizeof(Rect));
    ok(ExpectCompositionDisabledAllowed(Result, CompositionEnabled),
       "DwmGetWindowAttribute(EXTENDED_FRAME_BOUNDS) returned 0x%08lx\n",
       Result);
    if (Result == S_OK)
    {
        ok(Rect.right > Rect.left,
           "Extended frame width is not positive: left=%ld right=%ld\n",
           Rect.left, Rect.right);
        ok(Rect.bottom > Rect.top,
           "Extended frame height is not positive: top=%ld bottom=%ld\n",
           Rect.top, Rect.bottom);
    }

    Policy = DWMNCRP_ENABLED;
    Result = Api.SetWindowAttribute(Dest,
                                    DWMWA_NCRENDERING_POLICY,
                                    &Policy,
                                    sizeof(Policy));
    ok(ExpectCompositionDisabledAllowed(Result, CompositionEnabled),
       "DwmSetWindowAttribute(NCRENDERING_POLICY) returned 0x%08lx\n",
       Result);

    DwordValue = 0;
    Result = Api.SetWindowAttribute(Dest,
                                    DWMWA_CLOAK,
                                    &DwordValue,
                                    sizeof(DwordValue));
    ok(Result == S_OK || Result == E_INVALIDARG ||
       (!CompositionEnabled && Result == DWM_E_COMPOSITIONDISABLED),
       "DwmSetWindowAttribute(CLOAK) returned 0x%08lx\n", Result);

    ZeroMemory(&Margins, sizeof(Margins));
    Margins.cxLeftWidth = 1;
    Margins.cxRightWidth = 1;
    Margins.cyTopHeight = 1;
    Margins.cyBottomHeight = 1;
    Result = Api.ExtendFrameIntoClientArea(Dest, &Margins);
    ok(ExpectCompositionDisabledAllowed(Result, CompositionEnabled),
       "DwmExtendFrameIntoClientArea returned 0x%08lx\n", Result);

    ZeroMemory(&Blur, sizeof(Blur));
    Blur.dwFlags = DWM_BB_ENABLE;
    Blur.fEnable = TRUE;
    Result = Api.EnableBlurBehindWindow(Dest, &Blur);
    ok(ExpectCompositionDisabledAllowed(Result, CompositionEnabled),
       "DwmEnableBlurBehindWindow returned 0x%08lx\n", Result);

    Result = Api.RegisterThumbnail(NULL, Source, &Thumbnail);
    ok(Result == E_INVALIDARG,
       "DwmRegisterThumbnail(NULL, Source) returned 0x%08lx\n", Result);
    Result = Api.RegisterThumbnail(Dest, Source, NULL);
    ok(Result == E_INVALIDARG,
       "DwmRegisterThumbnail(Dest, Source, NULL) returned 0x%08lx\n", Result);

    Thumbnail = NULL;
    Result = Api.RegisterThumbnail(Dest, Source, &Thumbnail);
    ok(ExpectCompositionDisabledAllowed(Result, CompositionEnabled),
       "DwmRegisterThumbnail returned 0x%08lx with composition=%d\n",
       Result, CompositionEnabled);
    if (Result == S_OK)
    {
        ok(Thumbnail != NULL, "DwmRegisterThumbnail returned NULL thumbnail\n");
        Result = Api.UnregisterThumbnail(Thumbnail);
        ok(Result == S_OK, "DwmUnregisterThumbnail returned 0x%08lx\n", Result);
    }
    else
    {
        ok(Thumbnail == NULL,
           "DwmRegisterThumbnail failed but returned thumbnail %p\n",
           Thumbnail);
    }

    Result = Api.UnregisterThumbnail(NULL);
    ok(Result == E_INVALIDARG,
       "DwmUnregisterThumbnail(NULL) returned 0x%08lx\n", Result);

    Result = Api.EnableComposition(3);
    ok(Result == E_INVALIDARG,
       "DwmEnableComposition(3) returned 0x%08lx\n", Result);

    DestroyDwmTestWindow(Source, L"RosDwmPublicApiSource");
    DestroyDwmTestWindow(Dest, L"RosDwmPublicApiDest");
}
