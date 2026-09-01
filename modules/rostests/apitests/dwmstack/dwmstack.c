/*
 * PROJECT:     ReactOS API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Native-paired DWM module ABI and error-contract tests
 */

#define COBJMACROS
#include <apitest.h>
#include <windows.h>
#include <ndk/obfuncs.h>
#include <dwmapi.h>
#include <objbase.h>
#include <wincodec.h>
#include <reactos/dwmcore.h>

#define DWM_E_ENGINE_NOT_INITIALIZED ((HRESULT)0x8000000EL)
#define DWM_E_CHANNEL_UNAVAILABLE    ((HRESULT)0x88980416L)
#define DWM_WINDOW_MANAGER_SID_RID   0x5a
#define DWM_COLORIZATION_VALUE_COUNT 7

typedef HRESULT (CDECL *PFN_DWM_CREATE_CHANNEL)(PVOID, PVOID *);
typedef HRESULT (CDECL *PFN_DWM_CREATE_CURSOR)(ULONGLONG, PVOID *);
typedef HRESULT (CDECL *PFN_DWM_CREATE_BITMAP)(
    IWICImagingFactory *, const BYTE *, UINT, double, double, IWICBitmap **);
typedef HRESULT (CDECL *PFN_DWM_GET_EVENT_ID)(UINT *);
typedef HRESULT (CDECL *PFN_DWM_INITIALIZE)(INT, PVOID *);
typedef HRESULT (CDECL *PFN_DWM_UNINITIALIZE)(PVOID);
typedef HRESULT (WINAPI *PFN_DWM_IS_COMPOSITION_ENABLED)(BOOL *);
typedef HRESULT (WINAPI *PFN_DWM_GET_WINDOW_ATTRIBUTE)(HWND, DWORD, PVOID,
                                                       DWORD);
typedef HRESULT (WINAPI *PFN_DWM_SET_WINDOW_ATTRIBUTE)(HWND, DWORD, LPCVOID,
                                                       DWORD);
typedef HRESULT (WINAPI *PFN_DWM_ENABLE_BLUR_BEHIND_WINDOW)(
    HWND, const DWM_BLURBEHIND *);
typedef HRESULT (WINAPI *PFN_DWM_ENABLE_COMPOSITION)(UINT);
typedef HRESULT (WINAPI *PFN_DWM_MIL_CONTENT)(HWND);
typedef HRESULT (WINAPI *PFN_DWM_GET_GRAPHICS_STREAM_CLIENT)(UINT, UUID *);
typedef HRESULT (WINAPI *PFN_DWM_GET_GRAPHICS_STREAM_TRANSFORM_HINT)(
    UINT, MilMatrix3x2D *);
typedef HRESULT (WINAPI *PFN_DWM_GET_TRANSPORT_ATTRIBUTES)(BOOL *, BOOL *,
                                                           DWORD *);
typedef HRESULT (WINAPI *PFN_DWM_GET_UNMET_TAB_REQUIREMENTS)(
    HWND, enum DWM_TAB_WINDOW_REQUIREMENTS *);
typedef HRESULT (WINAPI *PFN_DWM_MODIFY_PREVIOUS_DX_FRAME_DURATION)(
    HWND, INT, BOOL);
typedef HRESULT (WINAPI *PFN_DWM_SET_DX_FRAME_DURATION)(HWND, INT);
typedef HRESULT (WINAPI *PFN_DWM_SET_PRESENT_PARAMETERS)(
    HWND, DWM_PRESENT_PARAMETERS *);
typedef HRESULT (WINAPI *PFN_DWM_RETURN_ONLY)(void);
typedef HRESULT (WINAPI *PFN_DWMP_IS_COMPOSITION_CAPABLE)(PVOID, BOOL *);
typedef HRESULT (WINAPI *PFN_DWMP_GET_TITLE_BAR_VISUAL)(HWND, ULONGLONG *);
typedef HRESULT (WINAPI *PFN_DWM_QUERY_THUMBNAIL_SOURCE_SIZE)(HTHUMBNAIL,
                                                              SIZE *);
typedef HRESULT (WINAPI *PFN_DWM_RENDER_GESTURE)(enum GESTURE_TYPE, UINT,
                                                  const DWORD *,
                                                  const POINT *);
typedef HRESULT (WINAPI *PFN_DWM_TRANSITION_OWNED_WINDOW)(
    HWND, enum DWMTRANSITION_OWNEDWINDOW_TARGET);
typedef HRESULT (WINAPI *PFN_DLL_CAN_UNLOAD_NOW)(void);
typedef HRESULT (WINAPI *PFN_DLL_GET_CLASS_OBJECT)(REFCLSID, REFIID, void **);
/* These private signatures are clean-room contracts inferred from the native
 * export table, public symbols and observable call behavior.  Keep each name
 * and argument shape explicit: an arity-only typedef hides ABI mistakes. */
typedef HRESULT (WINAPI *PFN_DWMP_GET_GLOBAL_STATE)(PVOID);
typedef HRESULT (WINAPI *PFN_DWMP_ACTIVATE_LIVE_PREVIEW)(
    DWORD, HWND, const DWORD *, INT, const RECT *);
typedef HRESULT (WINAPI *PFN_DWMP_QUERY_THUMBNAIL_TYPE)(ULONGLONG, DWORD *);
typedef HRESULT (WINAPI *PFN_DWMP_REGISTER_THUMBNAIL)(
    HWND, HWND, DWORD, DWORD, ULONGLONG *);
typedef HRESULT (WINAPI *PFN_DWMP_IS_THREAD_DESKTOP_COMPOSITED)(BOOL *);
typedef HRESULT (WINAPI *PFN_DWMP_SET_COLORIZATION_PARAMETERS)(
    const void *, BOOL);
typedef HRESULT (WINAPI *PFN_DWMP_GET_COMPOSITION_TIMING_INFO_EX)(
    HWND, DWM_TIMING_INFO *);
typedef HRESULT (WINAPI *PFN_DWMP_RENDER_FLICK)(HWND, POINT);
typedef HRESULT (WINAPI *PFN_DWMP_ALLOCATE_SECURITY_DESCRIPTOR)(
    PSECURITY_DESCRIPTOR *, ACCESS_MASK);
typedef void (WINAPI *PFN_DWMP_FREE_SECURITY_DESCRIPTOR)(
    PSECURITY_DESCRIPTOR);
typedef HRESULT (WINAPI *PFN_DWMP_BEGIN_TRANSITION_REQUEST)(DWORD);
typedef HRESULT (WINAPI *PFN_DWMP_TRANSITION_WINDOW)(HWND, DWORD);
typedef HRESULT (WINAPI *PFN_DWMP_END_TRANSITION_REQUEST)(DWORD);
typedef HRESULT (WINAPI *PFN_DWMP_TRANSITION_WINDOW_WITH_RECTS)(
    HWND, DWORD, const RECT *, const RECT *, const RECT *, const RECT *,
    const RECT *);
typedef HRESULT (WINAPI *PFN_DWMP_UPDATE_DESKTOP_THUMBNAIL)(
    ULONGLONG, const RECT *, ULONGLONG, ULONGLONG, ULONGLONG, BOOL, DWORD);
typedef HRESULT (WINAPI *PFN_DWMP_TRANSITION_BITMAP)(
    DWORD, HBITMAP, DWORD, const RECT *, const RECT *);
typedef HRESULT (WINAPI *PFN_DWMP_BEGIN_TRANSITION_REQUEST_WITH_GUID)(
    DWORD, const GUID *);
typedef HRESULT (WINAPI *PFN_DWMP_CREATE_SHARED_THUMBNAIL_VISUAL)(
    HWND, HWND, DWORD, const void *, IUnknown *, ULONGLONG *, ULONGLONG *);
typedef HRESULT (WINAPI *PFN_DWMP_BEGIN_TRANSITION_REQUEST_WITH_GUID_EX)(
    DWORD, const GUID *, GUID *);
typedef HRESULT (WINAPI *PFN_DWMP_CREATE_ANIMATION_CLOCK)(
    ULONGLONG, ULONGLONG, DWORD);
typedef HRESULT (WINAPI *PFN_DWMP_BEGIN_ANIMATION_CLOCK)(
    ULONGLONG, ULONGLONG, DWORD);
typedef HRESULT (WINAPI *PFN_DWMP_END_ANIMATION_CLOCK)(ULONGLONG, ULONGLONG);
typedef HRESULT (WINAPI *PFN_DWMP_GET_ANIMATION_CLOCK_TIME)(
    ULONGLONG, ULONGLONG, DWORD, ULONGLONG *);
typedef HRESULT (WINAPI *PFN_DWMP_SET_ANIMATION_CLOCK_TIME)(
    ULONGLONG, ULONGLONG, DWORD, const ULONGLONG *);
typedef HRESULT (WINAPI *PFN_DWMP_GET_ANIMATION_CLOCK_TOKEN)(
    ULONGLONG, ULONGLONG, ULONGLONG *);
typedef HRESULT (WINAPI *PFN_DWMP_REGISTER_SWAPCHAIN_RENDER_TARGET)(
    HANDLE, ULONGLONG, ULONGLONG, DWORD);
typedef HRESULT (WINAPI *PFN_DWMP_UNREGISTER_SWAPCHAIN_RENDER_TARGET)(
    ULONGLONG, DWORD *);
typedef HRESULT (WINAPI *PFN_DWMP_UPDATE_ACCENT_BLUR_RECT)(
    HWND, const RECT *);
typedef HRESULT (WINAPI *PFN_DWMP_SET_IMMERSIVE_ICONIC)(
    HWND, HBITMAP, DWORD, INT);
typedef HRESULT (WINAPI *PFN_DWMP_SET_IMMERSIVE_ICONIC_NOTIFY_WINDOW)(HWND);
typedef HRESULT (WINAPI *PFN_DWMP_QUERY_WINDOW_THUMBNAIL_SOURCE_SIZE)(
    ULONGLONG, DWORD, SIZE *);
typedef HRESULT (WINAPI *PFN_DWMP_CREATE_SHARED_MULTI_WINDOW_VISUAL)(
    HWND, IUnknown *, ULONGLONG *, ULONGLONG *);
typedef HRESULT (WINAPI *PFN_DWMP_UPDATE_SHARED_MULTI_WINDOW_VISUAL)(
    ULONGLONG, const HWND *, UINT, const HWND *, UINT, const RECT *,
    const SIZE *, DWORD);
typedef HRESULT (WINAPI *PFN_DWMP_SET_HOLOGRAPHIC_EXCLUSIVE_VIEW)(BOOL);
typedef HRESULT (WINAPI *PFN_DWMP_SET_CHILD_ROOT_VISUAL)(
    HWND, IUnknown *, IUnknown *, ULONGLONG, ULONGLONG);
typedef HRESULT (WINAPI *PFN_DWMP_GET_HMD_STATUS)(PVOID);
typedef HRESULT (WINAPI *PFN_DWMP_RESET_COLORIZATION_PARAMETERS)(void);
typedef HRESULT (WINAPI *PFN_DWMP_READ_COLORIZATION_PARAMETERS)(PVOID);
typedef HRESULT (WINAPI *PFN_DWMP_CREATE_SESSION_SHUTDOWN_EVENT)(
    DWORD, HANDLE *);
typedef HRESULT (WINAPI *PFN_DWMP_SDR_TO_HDR_BOOST)(ULONGLONG, ULONGLONG);
typedef HRESULT (WINAPI *PFN_DWMP_BEGIN_WINDOW_CAPTURE)(
    ULONGLONG, HANDLE, ULONGLONG *);
typedef HRESULT (WINAPI *PFN_DWMP_BEGIN_DISPLAY_CAPTURE)(
    ULONGLONG, HANDLE, ULONGLONG *);
typedef HRESULT (WINAPI *PFN_DWMP_UPDATE_WINDOW_CAPTURE)(
    ULONGLONG, ULONGLONG);
typedef HRESULT (WINAPI *PFN_DWMP_STOP_WINDOW_CAPTURE)(ULONGLONG);
typedef HRESULT (WINAPI *PFN_DWMP_STOP_DISPLAY_CAPTURE)(ULONGLONG);
typedef HRESULT (WINAPI *PFN_DWMP_GET_ANIMATION_COMMIT_HANDLE)(
    ULONGLONG, ULONGLONG, ULONGLONG *);
typedef HRESULT (WINAPI *PFN_DWMP_GET_TITLEBAR_INFO)(HWND, PVOID);
typedef HRESULT (WINAPI *PFN_DWMP_ADD_SHARED_PROJECTED_SHADOW_CASTER)(
    ULONGLONG, HANDLE, HANDLE);
typedef HRESULT (WINAPI *PFN_DWMP_BEGIN_VIRTUAL_MONITOR_CAPTURE)(
    ULONGLONG, HANDLE, ULONGLONG *);
typedef HRESULT (WINAPI *PFN_DWMP_STOP_VIRTUAL_MONITOR_CAPTURE)(ULONGLONG);
typedef HRESULT (WINAPI *PFN_DWMP_UPDATE_PROXY_WINDOW_FOR_CAPTURE)(
    ULONGLONG, HWND);
typedef HRESULT (WINAPI *PFN_DWMP_UPDATE_WINDOW_CAPTURE_BORDER)(
    ULONGLONG, DWORD);
typedef HRESULT (WINAPI *PFN_DWMP_UPDATE_DISPLAY_CAPTURE_BORDER)(
    ULONGLONG, DWORD);
typedef HRESULT (WINAPI *PFN_DWMP_SET_BLURRED_WALLPAPER_SURFACE)(
    IUnknown *, ULONGLONG);
typedef HRESULT (WINAPI *PFN_DWMP_ACTIVATE_LIVE_PREVIEW_EX)(
    DWORD, const HWND *, UINT, const DWORD *, INT, const RECT *);
typedef HRESULT (WINAPI *PFN_DWMP_ENABLE_WINDOW_NOTIFICATIONS)(BOOL);
typedef HRESULT (WINAPI *PFN_DWMP_ENABLE_MODE_CHANGE_ANIMATION)(BOOL);
typedef HRESULT (WINAPI *PFN_DWMP_BEGIN_FILTERED_DISPLAY_CAPTURE)(
    ULONGLONG, HANDLE, ULONGLONG *);
typedef HRESULT (WINAPI *PFN_DWMP_STOP_FILTERED_DISPLAY_CAPTURE)(ULONGLONG);
typedef HRESULT (WINAPI *PFN_DWMP_ADD_REMOVE_WINDOW_FILTERED_CAPTURE)(
    ULONGLONG, BOOL, HWND);
typedef HRESULT (WINAPI *PFN_DWMP_UPDATE_FILTERED_DISPLAY_CAPTURE_BORDER)(
    ULONGLONG, DWORD);

typedef struct _DWM_PRIVATE_COLORIZATION_TEST
{
    DWORD ColorizationColor;
    DWORD ColorizationAfterglow;
    DWORD ColorizationColorBalance;
    DWORD ColorizationAfterglowBalance;
    DWORD ColorizationBlurBalance;
    BOOL EnableWindowColorization;
    DWORD ColorizationGlassAttribute;
    DWORD Reserved;
} DWM_PRIVATE_COLORIZATION_TEST;

typedef struct _DWM_PRIVATE_REG_VALUE_BACKUP
{
    const WCHAR *Name;
    BOOL Present;
    DWORD Type;
    DWORD Size;
    BYTE Data[256];
} DWM_PRIVATE_REG_VALUE_BACKUP;

typedef struct _DWM_NAMED_EXPORT
{
    WORD Ordinal;
    const char *Name;
} DWM_NAMED_EXPORT;

static const DWM_NAMED_EXPORT DwmApiNamedExports[] =
{
    {100, "DwmpDxGetWindowSharedSurface"},
    {101, "DwmpDxUpdateWindowSharedSurface"},
    {102, "DwmEnableComposition"},
    {111, "DllCanUnloadNow"},
    {115, "DllGetClassObject"},
    {116, "DwmAttachMilContent"},
    {117, "DwmDefWindowProc"},
    {118, "DwmDetachMilContent"},
    {119, "DwmEnableBlurBehindWindow"},
    {120, "DwmEnableMMCSS"},
    {121, "DwmExtendFrameIntoClientArea"},
    {122, "DwmFlush"},
    {123, "DwmGetColorizationColor"},
    {125, "DwmGetCompositionTimingInfo"},
    {126, "DwmGetGraphicsStreamClient"},
    {127, "DwmpGetColorizationParameters"},
    {128, "DwmpDxgiIsThreadDesktopComposited"},
    {129, "DwmGetGraphicsStreamTransformHint"},
    {130, "DwmGetTransportAttributes"},
    {131, "DwmpSetColorizationParameters"},
    {133, "DwmGetUnmetTabRequirements"},
    {134, "DwmGetWindowAttribute"},
    {135, "DwmpRenderFlick"},
    {136, "DwmpAllocateSecurityDescriptor"},
    {137, "DwmpFreeSecurityDescriptor"},
    {143, "DwmpEnableDDASupport"},
    {146, "DwmInvalidateIconicBitmaps"},
    {149, "DwmIsCompositionEnabled"},
    {156, "DwmTetherTextContact"},
    {183, "DwmpUpdateProxyWindowForCapture"},
    {194, "DwmModifyPreviousDxFrameDuration"},
    {195, "DwmQueryThumbnailSourceSize"},
    {196, "DwmRegisterThumbnail"},
    {197, "DwmRenderGesture"},
    {198, "DwmSetDxFrameDuration"},
    {199, "DwmSetIconicLivePreviewBitmap"},
    {200, "DwmSetIconicThumbnail"},
    {201, "DwmSetPresentParameters"},
    {202, "DwmSetWindowAttribute"},
    {203, "DwmShowContact"},
    {204, "DwmTetherContact"},
    {205, "DwmTransitionOwnedWindow"},
    {206, "DwmUnregisterThumbnail"},
    {207, "DwmUpdateThumbnailProperties"},
};

static const char * const DwmApiOrdinalNames[] =
{
    "DwmpDxGetWindowSharedSurface",
    "DwmpDxUpdateWindowSharedSurface",
    "DwmEnableComposition",
    "DwmpRestartComposition",
    "DwmpSetColorizationColor",
    "DwmpStartOrStopFlip3D",
    "DwmpIsCompositionCapable",
    "DwmpGetGlobalState",
    "DwmpEnableRedirection",
    "DwmpOpenGraphicsStream",
    "DwmpCloseGraphicsStream",
    "DllCanUnloadNow",
    "DwmpSetGraphicsStreamTransformHint",
    "DwmpActivateLivePreview",
    "DwmpQueryThumbnailType",
    "DllGetClassObject",
    "DwmAttachMilContent",
    "DwmDefWindowProc",
    "DwmDetachMilContent",
    "DwmEnableBlurBehindWindow",
    "DwmEnableMMCSS",
    "DwmExtendFrameIntoClientArea",
    "DwmFlush",
    "DwmGetColorizationColor",
    "DwmpRegisterThumbnail",
    "DwmGetCompositionTimingInfo",
    "DwmGetGraphicsStreamClient",
    "DwmpGetColorizationParameters",
    "DwmpDxgiIsThreadDesktopComposited",
    "DwmGetGraphicsStreamTransformHint",
    "DwmGetTransportAttributes",
    "DwmpSetColorizationParameters",
    "DwmpGetCompositionTimingInfoEx",
    "DwmGetUnmetTabRequirements",
    "DwmGetWindowAttribute",
    "DwmpRenderFlick",
    "DwmpAllocateSecurityDescriptor",
    "DwmpFreeSecurityDescriptor",
    "DwmpBeginTransitionRequest",
    "DwmpTransitionWindow",
    "DwmpEndTransitionRequest",
    "DwmpTransitionWindowWithRects",
    "DwmpUpdateDesktopThumbnail",
    "DwmpEnableDDASupport",
    "DwmpTransitionBitmap",
    "DwmpBeginTransitionRequestWithGUID",
    "DwmInvalidateIconicBitmaps",
    "DwmpCreateSharedThumbnailVisual",
    "DwmpBeginTransitionRequestWithGUIDEx",
    "DwmIsCompositionEnabled",
    "DwmpCreateAnimationClock",
    "DwmpBeginAnimationClock",
    "DwmpEndAnimationClock",
    "DwmpGetAnimationClockTime",
    "DwmpSetAnimationClockTime",
    "DwmpGetAnimationClockToken",
    "DwmTetherTextContact",
    "DwmpRegisterSwapchainRenderTarget",
    "DwmpUnregisterSwapchainRenderTarget",
    "DwmpUpdateAccentBlurRect",
    "DwmpSetImmersiveIconic",
    "DwmpSetImmersiveIconicNotifyWindow",
    "DwmpQueryWindowThumbnailSourceSize",
    "DwmpCreateSharedMultiWindowVisual",
    "DwmpUpdateSharedMultiWindowVisual",
    "DwmpSetHolographicExclusiveView",
    "DwmpSetChildRootVisual",
    "DwmpGetHmdStatus",
    "DwmpResetColorizationParameters",
    "DwmpReadColorizationParameters",
    "DwmpCreateSessionShutdownEvent",
    "DwmpSDRToHDRBoost",
    "DwmpGetTitleBarVisual",
    "DwmpBeginWindowCapture",
    "DwmpBeginDisplayCapture",
    "DwmpUpdateWindowCapture",
    "DwmpStopWindowCapture",
    "DwmpStopDisplayCapture",
    "DwmpGetAnimationCommitHandle",
    "DwmpGetTitlebarInfo",
    "DwmpAddSharedProjectedShadowCaster",
    "DwmpBeginVirtualMonitorCapture",
    "DwmpStopVirtualMonitorCapture",
    "DwmpUpdateProxyWindowForCapture",
    "DwmpUpdateWindowCaptureBorder",
    "DwmpUpdateDisplayCaptureBorder",
    "DwmpSetBlurredWallpaperSurface",
    "DwmpActivateLivePreviewEx",
    "DwmpEnableWindowNotifications",
    "DwmpEnableModeChangeAnimation",
    "DwmpBeginFilteredDisplayCapture",
    "DwmpStopFilteredDisplayCapture",
    "DwmpAddRemoveWindowToFilteredDisplayCapture",
    "DwmpUpdateFilteredDisplayCaptureBorder",
    "DwmModifyPreviousDxFrameDuration",
    "DwmQueryThumbnailSourceSize",
    "DwmRegisterThumbnail",
    "DwmRenderGesture",
    "DwmSetDxFrameDuration",
    "DwmSetIconicLivePreviewBitmap",
    "DwmSetIconicThumbnail",
    "DwmSetPresentParameters",
    "DwmSetWindowAttribute",
    "DwmShowContact",
    "DwmTetherContact",
    "DwmTransitionOwnedWindow",
    "DwmUnregisterThumbnail",
    "DwmUpdateThumbnailProperties",
};

C_ASSERT(sizeof(DwmApiOrdinalNames) / sizeof(DwmApiOrdinalNames[0]) == 108);

static const WORD DwmApiFormerStubOrdinals[] =
{
    107, 113, 114, 124, 128, 131, 132, 135, 136, 137,
    138, 139, 140, 141, 142, 144, 145, 147, 148,
    150, 151, 152, 153, 154, 155,
    157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167,
    168, 169, 170, 171,
    173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183,
    184, 185, 186, 187, 188, 189, 190, 191, 192, 193
};

C_ASSERT(sizeof(DwmApiFormerStubOrdinals) /
         sizeof(DwmApiFormerStubOrdinals[0]) == 61);

static BOOL DwmApiFormerStubCovered[61];
static const void *SharedCfgCheckFunction;
static const void *SharedCfgDispatchFunction;

typedef struct _DWM_PRIVATE_EXPECTATION
{
    WORD Ordinal;
    const char *CaseName;
    HRESULT Result;
} DWM_PRIVATE_EXPECTATION;

static const DWM_PRIVATE_EXPECTATION DwmApiNativeExpectations[] =
{
    {107, "null-output",         (HRESULT)0x80070057L},
    {107, "state",               (HRESULT)0x80070057L},
    {113, "invalid-type",        (HRESULT)0x80070057L},
    {114, "null-output",         (HRESULT)0x80070057L},
    {114, "invalid-thumbnail",   (HRESULT)0x80070057L},
    {124, "same-window",         (HRESULT)0x80070057L},
    {124, "valid-windows",       S_OK},
    {128, "null-output",         (HRESULT)0x80070057L},
    {128, "thread-desktop",      S_OK},
    {131, "null-parameters",     (HRESULT)0x80070057L},
    {132, "null-output",         (HRESULT)0x80070057L},
    {132, "invalid-size",        (HRESULT)0x80070018L},
    {132, "legacy-size",         S_OK},
    {132, "extended-size",       S_OK},
    {135, "point-11-17",         S_OK},
    {136, "allocate",            S_OK},
    {137, "free",                S_OK},
    {138, "begin",               (HRESULT)0x80070057L},
    {139, "null-window",         (HRESULT)0x80070578L},
    {140, "end",                 S_OK},
    {141, "null-window",         (HRESULT)0x80070578L},
    {142, "invalid-thumbnail",   S_OK},
    {144, "null-bitmap",         (HRESULT)0x80070057L},
    {145, "begin-guid",          (HRESULT)0x80070057L},
    {147, "invalid-destination", (HRESULT)0x80070057L},
    {148, "begin-guid-ex",       (HRESULT)0x80070057L},
    {150, "zero-clock",          (HRESULT)0x80070057L},
    {151, "zero-clock",          (HRESULT)0x80070057L},
    {152, "zero-clock",          (HRESULT)0x80070057L},
    {153, "null-output",         (HRESULT)0x80070057L},
    {153, "zero-clock",          (HRESULT)0x80070057L},
    {154, "zero-clock",          (HRESULT)0x80070057L},
    {155, "null-output",         (HRESULT)0x80070057L},
    {155, "zero-clock",          (HRESULT)0x80070057L},
    {157, "null-swapchain",      (HRESULT)0xd0000008L},
    {158, "zero-target",         (HRESULT)0x80070057L},
    {159, "null-rect",           (HRESULT)0x80070057L},
    {160, "null-window",         (HRESULT)0x80070057L},
    {161, "null-window",         (HRESULT)0x80070057L},
    {162, "null-output",         (HRESULT)0x80070057L},
    {162, "zero-thumbnail",      (HRESULT)0x80070057L},
    {163, "null-window",         (HRESULT)0x80070057L},
    {164, "zero-token",          (HRESULT)0x80070057L},
    {165, "disable",             S_OK},
    {166, "null-window",         (HRESULT)0x80070057L},
    {167, "null-output",         (HRESULT)0x80070057L},
    {167, "status",              S_OK},
    {169, "null-output",         (HRESULT)0x80070057L},
    {169, "controlled-registry", S_OK},
    {131, "do-not-persist",      S_OK},
    {168, "reset",               S_OK},
    {170, "current-session",     (HRESULT)0xd0000035L},
    {171, "zero-target",         (HRESULT)0x80070057L},
    {173, "null-swapchain",      (HRESULT)0x80070006L},
    {174, "null-swapchain",      (HRESULT)0x80070006L},
    {175, "zero-token",          (HRESULT)0x80070005L},
    {176, "zero-token",          (HRESULT)0x80070005L},
    {177, "zero-token",          (HRESULT)0x80070005L},
    {178, "null-output",         (HRESULT)0x80070057L},
    {179, "null-window",         (HRESULT)0x80070057L},
    {180, "one-sided-handles",   (HRESULT)0x80070057L},
    {181, "null-swapchain",      (HRESULT)0x80070006L},
    {182, "zero-token",          (HRESULT)0x80070005L},
    {183, "null-window",         (HRESULT)0x80070005L},
    {184, "zero-token",          (HRESULT)0x80070005L},
    {185, "zero-token",          (HRESULT)0x80070005L},
    {186, "surface-without-handle", (HRESULT)0x80070057L},
    {186, "clear",               S_OK},
    {187, "invalid-type",        (HRESULT)0x80070057L},
    {188, "disable",             (HRESULT)0x80004001L},
    {189, "disable",             S_OK},
    {190, "null-swapchain",      (HRESULT)0x80070006L},
    {191, "zero-token",          (HRESULT)0x80070005L},
    {192, "null-window",         (HRESULT)0x80070005L},
    {193, "zero-token",          (HRESULT)0x80070005L}
};

C_ASSERT(sizeof(DwmApiNativeExpectations) /
         sizeof(DwmApiNativeExpectations[0]) == 75);

static void
RecordDwmApiObservation(WORD Ordinal, const char *CaseName, HRESULT Result,
                        ULONGLONG Output0, ULONGLONG Output1)
{
    ULONG Index, ExpectedIndex;
    BOOL Found = FALSE;
    BOOL ExpectedFound = FALSE;

    for (Index = 0;
         Index < sizeof(DwmApiFormerStubOrdinals) /
                 sizeof(DwmApiFormerStubOrdinals[0]);
         ++Index)
    {
        if (DwmApiFormerStubOrdinals[Index] == Ordinal)
        {
            DwmApiFormerStubCovered[Index] = TRUE;
            Found = TRUE;
            break;
        }
    }
    ok(Found, "ordinal %u is not in the former-stub inventory\n", Ordinal);
    for (ExpectedIndex = 0;
         ExpectedIndex < sizeof(DwmApiNativeExpectations) /
                         sizeof(DwmApiNativeExpectations[0]);
         ++ExpectedIndex)
    {
        if (DwmApiNativeExpectations[ExpectedIndex].Ordinal == Ordinal &&
            !strcmp(DwmApiNativeExpectations[ExpectedIndex].CaseName,
                    CaseName))
        {
            ok_hex(Result, DwmApiNativeExpectations[ExpectedIndex].Result);
            ExpectedFound = TRUE;
            break;
        }
    }
    ok(ExpectedFound, "ordinal %u case %s has no native expectation\n",
       Ordinal, CaseName);
    trace("DWMOBS|%u|%s|hr=%08lx|out0=%I64x|out1=%I64x\n",
          Ordinal, CaseName, Result, Output0, Output1);
}

static FARPROC
GetDwmApiOrdinal(HMODULE DwmApi, WORD Ordinal)
{
    FARPROC Function = GetProcAddress(DwmApi,
                                      (LPCSTR)(ULONG_PTR)Ordinal);
    ok(Function != NULL, "dwmapi ordinal %u (%s) is absent\n", Ordinal,
       DwmApiOrdinalNames[Ordinal - 100]);
    return Function;
}

static void
VerifyDwmApiFormerStubCoverage(void)
{
    ULONG Index;

    for (Index = 0;
         Index < sizeof(DwmApiFormerStubOrdinals) /
                 sizeof(DwmApiFormerStubOrdinals[0]);
         ++Index)
    {
        ok(DwmApiFormerStubCovered[Index],
           "dwmapi ordinal %u (%s) has no detailed behavior session\n",
           DwmApiFormerStubOrdinals[Index],
           DwmApiOrdinalNames[DwmApiFormerStubOrdinals[Index] - 100]);
    }
}

#ifdef _WIN64
C_ASSERT(sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64) == 0x140);
#endif

static BOOL
AddressIsInImage(const void *Address, const void *ImageBase, SIZE_T ImageSize)
{
    ULONG_PTR Value = (ULONG_PTR)Address;
    ULONG_PTR Base = (ULONG_PTR)ImageBase;

    return Value >= Base && Value - Base < ImageSize;
}

static void
TestImageContract(HMODULE Module, const char *ModuleName, BOOL LoaderResolved)
{
    const IMAGE_LOAD_CONFIG_DIRECTORY *LoadConfig;
    const IMAGE_DATA_DIRECTORY *Directory;
    const IMAGE_DOS_HEADER *DosHeader;
    const IMAGE_NT_HEADERS *NtHeaders;
    const void *CheckFunction;
    const void *DispatchFunction;

    ok(Module != NULL, "%s module is NULL\n", ModuleName);
    if (Module == NULL)
        return;

    DosHeader = (const IMAGE_DOS_HEADER *)Module;
    ok(DosHeader->e_magic == IMAGE_DOS_SIGNATURE,
       "%s has invalid DOS signature 0x%x\n", ModuleName,
       DosHeader->e_magic);
    if (DosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        return;

    NtHeaders = (const IMAGE_NT_HEADERS *)((const BYTE *)Module +
                                           DosHeader->e_lfanew);
    ok(NtHeaders->Signature == IMAGE_NT_SIGNATURE,
       "%s has invalid NT signature 0x%lx\n", ModuleName,
       NtHeaders->Signature);
    if (NtHeaders->Signature != IMAGE_NT_SIGNATURE)
        return;

    ok_eq_int(NtHeaders->OptionalHeader.MajorOperatingSystemVersion, 10);
    ok_eq_int(NtHeaders->OptionalHeader.MinorOperatingSystemVersion, 0);
    ok_eq_int(NtHeaders->OptionalHeader.MajorImageVersion, 10);
    ok_eq_int(NtHeaders->OptionalHeader.MinorImageVersion, 0);
    ok_eq_int(NtHeaders->OptionalHeader.MajorSubsystemVersion, 10);
    ok_eq_int(NtHeaders->OptionalHeader.MinorSubsystemVersion, 0);
    ok(NtHeaders->OptionalHeader.AddressOfEntryPoint != 0,
       "%s has no entry point\n", ModuleName);
    ok((NtHeaders->OptionalHeader.DllCharacteristics &
        IMAGE_DLLCHARACTERISTICS_GUARD_CF) != 0,
       "%s lacks IMAGE_DLLCHARACTERISTICS_GUARD_CF\n", ModuleName);

    Directory = &NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
    ok_eq_ulong(Directory->Size, sizeof(*LoadConfig));
    ok(Directory->VirtualAddress != 0,
       "%s has no load-config directory\n", ModuleName);
    if (Directory->VirtualAddress == 0 ||
        Directory->Size < sizeof(*LoadConfig))
        return;

    LoadConfig = (const IMAGE_LOAD_CONFIG_DIRECTORY *)((const BYTE *)Module +
                                                        Directory->VirtualAddress);
    ok_eq_ulong(LoadConfig->Size, sizeof(*LoadConfig));
    ok((LoadConfig->GuardFlags & IMAGE_GUARD_CF_INSTRUMENTED) != 0,
       "%s load config is not CFG-instrumented\n", ModuleName);
    ok((LoadConfig->GuardFlags & IMAGE_GUARD_CF_FUNCTION_TABLE_PRESENT) != 0,
       "%s has no CFG function table flag\n", ModuleName);
    ok(LoadConfig->GuardCFFunctionTable != 0,
       "%s has no CFG function table\n", ModuleName);
    ok(LoadConfig->GuardCFFunctionCount != 0,
       "%s has an empty CFG function table\n", ModuleName);
    ok(AddressIsInImage((const void *)(ULONG_PTR)LoadConfig->GuardCFCheckFunctionPointer,
                        Module, NtHeaders->OptionalHeader.SizeOfImage),
       "%s CFG check slot is outside the image\n", ModuleName);
    ok(AddressIsInImage((const void *)(ULONG_PTR)LoadConfig->GuardCFDispatchFunctionPointer,
                        Module, NtHeaders->OptionalHeader.SizeOfImage),
       "%s CFG dispatch slot is outside the image\n", ModuleName);

    CheckFunction = *(const void * const *)(ULONG_PTR)LoadConfig->GuardCFCheckFunctionPointer;
    DispatchFunction = *(const void * const *)(ULONG_PTR)LoadConfig->GuardCFDispatchFunctionPointer;
    ok(CheckFunction != NULL, "%s CFG check function is NULL\n", ModuleName);
    ok(DispatchFunction != NULL, "%s CFG dispatch function is NULL\n", ModuleName);
    if (LoaderResolved)
    {
        ok(!AddressIsInImage(CheckFunction, Module,
                            NtHeaders->OptionalHeader.SizeOfImage),
           "%s CFG check function was not loader-resolved at %p\n",
           ModuleName, CheckFunction);
        ok(!AddressIsInImage(DispatchFunction, Module,
                            NtHeaders->OptionalHeader.SizeOfImage),
           "%s CFG dispatch function was not loader-resolved at %p\n",
           ModuleName, DispatchFunction);

        if (SharedCfgCheckFunction == NULL)
            SharedCfgCheckFunction = CheckFunction;
        else
            ok(CheckFunction == SharedCfgCheckFunction,
               "%s CFG check function %p differs from shared target %p\n",
               ModuleName, CheckFunction, SharedCfgCheckFunction);

        if (SharedCfgDispatchFunction == NULL)
            SharedCfgDispatchFunction = DispatchFunction;
        else
            ok(DispatchFunction == SharedCfgDispatchFunction,
               "%s CFG dispatch function %p differs from shared target %p\n",
               ModuleName, DispatchFunction, SharedCfgDispatchFunction);
    }
}

static HMODULE
LoadSystemImageWithoutImports(const WCHAR *FileName)
{
    WCHAR Path[MAX_PATH];
    UINT Length;

    Length = GetSystemDirectoryW(Path, ARRAYSIZE(Path));
    if (Length == 0 || Length >= ARRAYSIZE(Path) - 1)
        return NULL;
    Path[Length++] = L'\\';
    if (lstrlenW(FileName) >= ARRAYSIZE(Path) - Length)
        return NULL;
    lstrcpyW(Path + Length, FileName);
    return LoadLibraryExW(Path, NULL, DONT_RESOLVE_DLL_REFERENCES);
}

static const IMAGE_EXPORT_DIRECTORY *
GetExportDirectory(HMODULE Module, const char *ModuleName)
{
    const IMAGE_DOS_HEADER *DosHeader;
    const IMAGE_NT_HEADERS *NtHeaders;
    const IMAGE_DATA_DIRECTORY *Directory;

    DosHeader = (const IMAGE_DOS_HEADER *)Module;
    ok(DosHeader->e_magic == IMAGE_DOS_SIGNATURE,
       "%s has invalid DOS signature 0x%x\n", ModuleName,
       DosHeader->e_magic);
    if (DosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        return NULL;

    NtHeaders = (const IMAGE_NT_HEADERS *)((const BYTE *)Module +
                                           DosHeader->e_lfanew);
    ok(NtHeaders->Signature == IMAGE_NT_SIGNATURE,
       "%s has invalid NT signature 0x%lx\n", ModuleName,
       NtHeaders->Signature);
    if (NtHeaders->Signature != IMAGE_NT_SIGNATURE)
        return NULL;

    Directory = &NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    ok(Directory->VirtualAddress != 0 &&
       Directory->Size >= sizeof(IMAGE_EXPORT_DIRECTORY),
       "%s has no valid export directory\n", ModuleName);
    if (Directory->VirtualAddress == 0 ||
        Directory->Size < sizeof(IMAGE_EXPORT_DIRECTORY))
        return NULL;

    return (const IMAGE_EXPORT_DIRECTORY *)((const BYTE *)Module +
                                             Directory->VirtualAddress);
}

static void
TestOrdinalInventory(HMODULE Module, const char *ModuleName, DWORD Base,
                     DWORD FunctionCount, DWORD NameCount)
{
    const IMAGE_EXPORT_DIRECTORY *Exports;
    DWORD Index;

    Exports = GetExportDirectory(Module, ModuleName);
    if (Exports == NULL)
        return;

    ok_eq_ulong(Exports->Base, Base);
    ok_eq_ulong(Exports->NumberOfFunctions, FunctionCount);
    ok_eq_ulong(Exports->NumberOfNames, NameCount);

    /* A spec-file stub still has an address and therefore passes only this
     * ABI inventory gate.  Per-function behavior is tested separately. */
    for (Index = 0; Index < FunctionCount; ++Index)
    {
        DWORD Ordinal = Base + Index;
        ok(GetProcAddress(Module, (LPCSTR)(ULONG_PTR)Ordinal) != NULL,
           "%s ordinal %lu is absent\n", ModuleName, Ordinal);
    }
}

static void
TestDwmApiOrdinalInventory(HMODULE Module)
{
    const IMAGE_EXPORT_DIRECTORY *Exports;
    ULONG Index;

    Exports = GetExportDirectory(Module, "dwmapi");
    if (Exports == NULL)
        return;

    ok_eq_ulong(Exports->Base, 100);
    ok_eq_ulong(Exports->NumberOfFunctions, ARRAYSIZE(DwmApiOrdinalNames));
    ok_eq_ulong(Exports->NumberOfNames, ARRAYSIZE(DwmApiNamedExports));

    for (Index = 0; Index < ARRAYSIZE(DwmApiOrdinalNames); ++Index)
    {
        WORD Ordinal = (WORD)(100 + Index);

        ok(GetProcAddress(Module, (LPCSTR)(ULONG_PTR)Ordinal) != NULL,
           "dwmapi ordinal %u (%s) is absent\n",
           Ordinal, DwmApiOrdinalNames[Index]);
    }
}

static void
TestBlurBehindValidation(HMODULE DwmApi)
{
    PFN_DWM_ENABLE_BLUR_BEHIND_WINDOW EnableBlurBehindWindow;
    PFN_DWM_IS_COMPOSITION_ENABLED IsCompositionEnabled;
    DWM_BLURBEHIND Blur;
    BOOL CompositionEnabled = FALSE;
    HRGN Region;
    HWND Window;
    HRESULT Result;

    EnableBlurBehindWindow = (PFN_DWM_ENABLE_BLUR_BEHIND_WINDOW)GetProcAddress(
        DwmApi, "DwmEnableBlurBehindWindow");
    IsCompositionEnabled = (PFN_DWM_IS_COMPOSITION_ENABLED)GetProcAddress(
        DwmApi, "DwmIsCompositionEnabled");
    if (EnableBlurBehindWindow == NULL)
        return;

    Result = EnableBlurBehindWindow(NULL, NULL);
    ok_hex(Result, E_INVALIDARG);

    Window = CreateWindowExW(0, L"STATIC", L"dwmstack-blur",
                             WS_OVERLAPPED, 0, 0, 64, 64,
                             NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(Window != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (Window == NULL)
        return;

    Result = EnableBlurBehindWindow(Window, NULL);
    ok_hex(Result, E_INVALIDARG);

    ZeroMemory(&Blur, sizeof(Blur));
    Result = EnableBlurBehindWindow(Window, &Blur);
    ok_hex(Result, E_INVALIDARG);

    Blur.dwFlags = 8;
    Result = EnableBlurBehindWindow(Window, &Blur);
    ok_hex(Result, E_INVALIDARG);

    Region = CreateRectRgn(4, 5, 48, 49);
    ok(Region != NULL, "CreateRectRgn failed: %lu\n", GetLastError());
    if (Region != NULL)
    {
        if (IsCompositionEnabled != NULL)
        {
            Result = IsCompositionEnabled(&CompositionEnabled);
            ok_hex(Result, S_OK);
        }

        ZeroMemory(&Blur, sizeof(Blur));
        Blur.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION |
                       DWM_BB_TRANSITIONONMAXIMIZED;
        Blur.fEnable = TRUE;
        Blur.hRgnBlur = Region;
        Blur.fTransitionOnMaximized = TRUE;
        Result = EnableBlurBehindWindow(Window, &Blur);
        if (CompositionEnabled)
            ok_hex(Result, S_OK);
        else
            ok(Result == S_OK || Result == DWM_E_COMPOSITIONDISABLED,
               "disabled composition returned 0x%08lx\n", Result);

        if (SUCCEEDED(Result))
        {
            ZeroMemory(&Blur, sizeof(Blur));
            Blur.dwFlags = DWM_BB_ENABLE;
            Result = EnableBlurBehindWindow(Window, &Blur);
            ok_hex(Result, S_OK);
        }
        DeleteObject(Region);
    }

    DestroyWindow(Window);
}

static void
TestDwmApiSimpleContracts(HMODULE DwmApi)
{
    static const struct
    {
        WORD Ordinal;
        HRESULT Expected;
    } ReturnOnlyContracts[] =
    {
        {103, S_OK},
        {104, E_NOTIMPL},
        {105, S_OK},
        {108, E_NOTIMPL},
        {109, DWM_E_COMPOSITIONDISABLED},
        {110, DWM_E_COMPOSITIONDISABLED},
        {112, DWM_E_COMPOSITIONDISABLED},
        {143, S_OK},
        {156, S_OK},
    };
    PFN_DWM_ENABLE_COMPOSITION EnableComposition;
    PFN_DWM_MIL_CONTENT AttachMilContent;
    PFN_DWM_MIL_CONTENT DetachMilContent;
    PFN_DWM_GET_GRAPHICS_STREAM_CLIENT GetGraphicsStreamClient;
    PFN_DWM_GET_GRAPHICS_STREAM_TRANSFORM_HINT GetGraphicsStreamTransformHint;
    PFN_DWM_GET_TRANSPORT_ATTRIBUTES GetTransportAttributes;
    PFN_DWM_GET_UNMET_TAB_REQUIREMENTS GetUnmetTabRequirements;
    PFN_DWM_MODIFY_PREVIOUS_DX_FRAME_DURATION ModifyPreviousDxFrameDuration;
    PFN_DWM_SET_DX_FRAME_DURATION SetDxFrameDuration;
    PFN_DWM_SET_PRESENT_PARAMETERS SetPresentParameters;
    PFN_DWMP_IS_COMPOSITION_CAPABLE IsCompositionCapable;
    PFN_DWMP_GET_TITLE_BAR_VISUAL GetTitleBarVisual;
    PFN_DWM_QUERY_THUMBNAIL_SOURCE_SIZE QueryThumbnailSourceSize;
    PFN_DWM_RENDER_GESTURE RenderGesture;
    PFN_DWM_TRANSITION_OWNED_WINDOW TransitionOwnedWindow;
    PFN_DLL_CAN_UNLOAD_NOW DllCanUnload;
    PFN_DLL_GET_CLASS_OBJECT DllGetClass;
    MilMatrix3x2D Transform;
    MilMatrix3x2D ExpectedTransform;
    UUID Client;
    UUID ExpectedClient;
    enum DWM_TAB_WINDOW_REQUIREMENTS Requirements;
    BOOL IsRemoting;
    BOOL IsConnected;
    DWORD Generation;
    ULONGLONG Visual;
    ULONG Index;
    HRESULT Result;

    for (Index = 0; Index < ARRAYSIZE(ReturnOnlyContracts); ++Index)
    {
        PFN_DWM_RETURN_ONLY Function = (PFN_DWM_RETURN_ONLY)GetProcAddress(
            DwmApi,
            (LPCSTR)(ULONG_PTR)ReturnOnlyContracts[Index].Ordinal);

        ok(Function != NULL, "dwmapi ordinal %u is absent\n",
           ReturnOnlyContracts[Index].Ordinal);
        if (Function != NULL)
            ok_hex(Function(), ReturnOnlyContracts[Index].Expected);
    }

    EnableComposition = (PFN_DWM_ENABLE_COMPOSITION)GetProcAddress(
        DwmApi, "DwmEnableComposition");
    AttachMilContent = (PFN_DWM_MIL_CONTENT)GetProcAddress(
        DwmApi, "DwmAttachMilContent");
    DetachMilContent = (PFN_DWM_MIL_CONTENT)GetProcAddress(
        DwmApi, "DwmDetachMilContent");
    GetGraphicsStreamClient = (PFN_DWM_GET_GRAPHICS_STREAM_CLIENT)GetProcAddress(
        DwmApi, "DwmGetGraphicsStreamClient");
    GetGraphicsStreamTransformHint =
        (PFN_DWM_GET_GRAPHICS_STREAM_TRANSFORM_HINT)GetProcAddress(
            DwmApi, "DwmGetGraphicsStreamTransformHint");
    GetTransportAttributes = (PFN_DWM_GET_TRANSPORT_ATTRIBUTES)GetProcAddress(
        DwmApi, "DwmGetTransportAttributes");
    GetUnmetTabRequirements =
        (PFN_DWM_GET_UNMET_TAB_REQUIREMENTS)GetProcAddress(
            DwmApi, "DwmGetUnmetTabRequirements");
    ModifyPreviousDxFrameDuration =
        (PFN_DWM_MODIFY_PREVIOUS_DX_FRAME_DURATION)GetProcAddress(
            DwmApi, "DwmModifyPreviousDxFrameDuration");
    SetDxFrameDuration = (PFN_DWM_SET_DX_FRAME_DURATION)GetProcAddress(
        DwmApi, "DwmSetDxFrameDuration");
    SetPresentParameters = (PFN_DWM_SET_PRESENT_PARAMETERS)GetProcAddress(
        DwmApi, "DwmSetPresentParameters");
    IsCompositionCapable = (PFN_DWMP_IS_COMPOSITION_CAPABLE)GetProcAddress(
        DwmApi, (LPCSTR)(ULONG_PTR)106);
    GetTitleBarVisual = (PFN_DWMP_GET_TITLE_BAR_VISUAL)GetProcAddress(
        DwmApi, (LPCSTR)(ULONG_PTR)172);
    QueryThumbnailSourceSize =
        (PFN_DWM_QUERY_THUMBNAIL_SOURCE_SIZE)GetProcAddress(
            DwmApi, "DwmQueryThumbnailSourceSize");
    RenderGesture = (PFN_DWM_RENDER_GESTURE)GetProcAddress(
        DwmApi, "DwmRenderGesture");
    TransitionOwnedWindow =
        (PFN_DWM_TRANSITION_OWNED_WINDOW)GetProcAddress(
            DwmApi, "DwmTransitionOwnedWindow");
    DllCanUnload = (PFN_DLL_CAN_UNLOAD_NOW)GetProcAddress(
        DwmApi, "DllCanUnloadNow");
    DllGetClass = (PFN_DLL_GET_CLASS_OBJECT)GetProcAddress(
        DwmApi, "DllGetClassObject");

    if (DllCanUnload != NULL)
        ok_hex(DllCanUnload(), S_OK);
    if (DllGetClass != NULL)
    {
        PVOID Object = (PVOID)(ULONG_PTR)0xDEADBEEF;

        Result = DllGetClass(&CLSID_NULL, &IID_IClassFactory, &Object);
        ok_hex(Result, CLASS_E_CLASSNOTAVAILABLE);
        ok(Object == NULL, "DllGetClassObject did not clear output: %p\n",
           Object);
    }

    if (IsCompositionCapable != NULL)
    {
        IsConnected = FALSE;
        Result = IsCompositionCapable(NULL, &IsConnected);
        ok_hex(Result, S_OK);
        ok_eq_int(IsConnected, TRUE);
        Result = IsCompositionCapable(NULL, NULL);
        ok_hex(Result, E_INVALIDARG);
    }

    if (GetTitleBarVisual != NULL)
    {
        Visual = 0;
        Result = GetTitleBarVisual(NULL, &Visual);
        ok_hex(Result, E_NOTIMPL);
        ok(Visual == ~(ULONGLONG)0,
           "DwmpGetTitleBarVisual returned 0x%I64x\n", Visual);
    }

    if (QueryThumbnailSourceSize != NULL)
        ok_hex(QueryThumbnailSourceSize(NULL, NULL), E_INVALIDARG);

    if (RenderGesture != NULL)
    {
        DWORD PointerIds[2] = {1, 2};
        POINT Points[2] = {{1, 2}, {3, 4}};

        ok_hex(RenderGesture(GT_TOUCH_TAP, 0, PointerIds, Points),
               E_INVALIDARG);
        ok_hex(RenderGesture(GT_TOUCH_TAP, 1, NULL, Points), E_INVALIDARG);
        ok_hex(RenderGesture(GT_TOUCH_TAP, 1, PointerIds, NULL), E_INVALIDARG);
        ok_hex(RenderGesture(GT_TOUCH_TAP, 2, PointerIds, Points),
               E_INVALIDARG);
        ok_hex(RenderGesture(GT_TOUCH_PRESSANDTAP, 1, PointerIds, Points),
               E_INVALIDARG);
    }

    if (TransitionOwnedWindow != NULL)
    {
        ok_hex(TransitionOwnedWindow(
                   NULL, DWMTRANSITION_OWNEDWINDOW_REPOSITION),
               E_INVALIDARG);
    }

    if (EnableComposition != NULL)
        ok_hex(EnableComposition(0xDEADBEEF), S_OK);
    if (AttachMilContent != NULL)
        ok_hex(AttachMilContent(NULL), DWM_E_COMPOSITIONDISABLED);
    if (DetachMilContent != NULL)
        ok_hex(DetachMilContent(NULL), DWM_E_COMPOSITIONDISABLED);

    if (GetGraphicsStreamClient != NULL)
    {
        memset(&Client, 0x5a, sizeof(Client));
        ExpectedClient = Client;
        Result = GetGraphicsStreamClient(0xDEADBEEF, &Client);
        ok_hex(Result, DWM_E_COMPOSITIONDISABLED);
        ok(!memcmp(&Client, &ExpectedClient, sizeof(Client)),
           "DwmGetGraphicsStreamClient modified output\n");
    }

    if (GetGraphicsStreamTransformHint != NULL)
    {
        memset(&Transform, 0x5a, sizeof(Transform));
        ExpectedTransform = Transform;
        Result = GetGraphicsStreamTransformHint(0xDEADBEEF, &Transform);
        ok_hex(Result, DWM_E_COMPOSITIONDISABLED);
        ok(!memcmp(&Transform, &ExpectedTransform, sizeof(Transform)),
           "DwmGetGraphicsStreamTransformHint modified output\n");
    }

    if (GetTransportAttributes != NULL)
    {
        IsRemoting = TRUE;
        IsConnected = FALSE;
        Generation = 0xDEADBEEF;
        Result = GetTransportAttributes(&IsRemoting, &IsConnected, &Generation);
        ok_hex(Result, S_OK);
        ok_eq_int(IsRemoting, FALSE);
        ok_eq_int(IsConnected, TRUE);
        ok_eq_ulong(Generation, 1);
    }

    if (GetUnmetTabRequirements != NULL)
    {
        Requirements = (enum DWM_TAB_WINDOW_REQUIREMENTS)0xDEADBEEF;
        Result = GetUnmetTabRequirements(NULL, &Requirements);
        ok_hex(Result, S_OK);
        ok_eq_int(Requirements, DWMTWR_IMPLEMENTED_BY_SYSTEM);
    }

    if (ModifyPreviousDxFrameDuration != NULL)
        ok_hex(ModifyPreviousDxFrameDuration(NULL, 7, TRUE), E_NOTIMPL);
    if (SetDxFrameDuration != NULL)
        ok_hex(SetDxFrameDuration(NULL, 7), E_NOTIMPL);
    if (SetPresentParameters != NULL)
        ok_hex(SetPresentParameters(NULL, NULL), E_NOTIMPL);
}

static void
TestDwmApiExports(HMODULE DwmApi)
{
    PFN_DWM_IS_COMPOSITION_ENABLED IsCompositionEnabled;
    PFN_DWM_GET_WINDOW_ATTRIBUTE GetWindowAttribute;
    PFN_DWM_SET_WINDOW_ATTRIBUTE SetWindowAttribute;
    ULONG Index;
    BOOL Enabled = TRUE;
    HRESULT Result;

    TestDwmApiOrdinalInventory(DwmApi);

    for (Index = 0; Index < ARRAYSIZE(DwmApiNamedExports); ++Index)
    {
        FARPROC ByName = GetProcAddress(DwmApi, DwmApiNamedExports[Index].Name);
        FARPROC ByOrdinal = GetProcAddress(
            DwmApi, (LPCSTR)(ULONG_PTR)DwmApiNamedExports[Index].Ordinal);

        ok(ByName != NULL, "dwmapi export %s is absent\n",
           DwmApiNamedExports[Index].Name);
        ok(ByOrdinal != NULL, "dwmapi ordinal %u is absent\n",
           DwmApiNamedExports[Index].Ordinal);
        ok(ByName == ByOrdinal,
           "dwmapi %s resolves to %p, ordinal %u resolves to %p\n",
           DwmApiNamedExports[Index].Name, ByName,
           DwmApiNamedExports[Index].Ordinal, ByOrdinal);
    }

    IsCompositionEnabled = (PFN_DWM_IS_COMPOSITION_ENABLED)GetProcAddress(
        DwmApi, "DwmIsCompositionEnabled");
    if (IsCompositionEnabled != NULL)
    {
        Result = IsCompositionEnabled(NULL);
        ok_hex(Result, E_INVALIDARG);
        Result = IsCompositionEnabled(&Enabled);
        ok_hex(Result, S_OK);
        trace("DwmIsCompositionEnabled = %d\n", Enabled);
    }

    GetWindowAttribute = (PFN_DWM_GET_WINDOW_ATTRIBUTE)GetProcAddress(
        DwmApi, "DwmGetWindowAttribute");
    SetWindowAttribute = (PFN_DWM_SET_WINDOW_ATTRIBUTE)GetProcAddress(
        DwmApi, "DwmSetWindowAttribute");
    if (GetWindowAttribute != NULL && SetWindowAttribute != NULL)
    {
        HWND Window = CreateWindowExW(0, L"STATIC", L"dwmstack",
                                      WS_OVERLAPPED, 0, 0, 64, 64,
                                      NULL, NULL, GetModuleHandleW(NULL), NULL);
        DWORD Value = 0;

        ok(Window != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
        if (Window != NULL)
        {
            Result = SetWindowAttribute(Window,
                                        DWMWA_USE_IMMERSIVE_DARK_MODE,
                                        NULL, sizeof(Value));
            ok_hex(Result, E_INVALIDARG);
            Result = SetWindowAttribute(Window,
                                        DWMWA_USE_IMMERSIVE_DARK_MODE,
                                        &Value, sizeof(Value) - 1);
            ok_hex(Result, E_INVALIDARG);
            Result = SetWindowAttribute(Window, 0, &Value, sizeof(Value));
            ok_hex(Result, E_INVALIDARG);

            Result = GetWindowAttribute(Window,
                                        DWMWA_USE_IMMERSIVE_DARK_MODE,
                                        NULL, sizeof(Value));
            ok_hex(Result, E_INVALIDARG);
            Result = GetWindowAttribute(Window,
                                        DWMWA_USE_IMMERSIVE_DARK_MODE,
                                        &Value, sizeof(Value) - 1);
            ok_hex(Result, E_INVALIDARG);
            Result = GetWindowAttribute(Window, 0, &Value, sizeof(Value));
            ok_hex(Result, E_INVALIDARG);
            DestroyWindow(Window);
        }
    }

    TestDwmApiSimpleContracts(DwmApi);
    TestBlurBehindValidation(DwmApi);
}

C_ASSERT(sizeof(IDwmChannelPrivateVtbl) == 103 * sizeof(PVOID));

static const char * const DwmChannelSlotNames[] =
{
    "QueryInterface",
    "AddRef",
    "Release",
    "Commit",
    "SynchronizedCommit",
    "PeekNextMessage",
    "SyncFlush",
    "WaitForNextMessage",
    "AddRefResource",
    "CreateResource",
    "CreateSharedResource",
    "DuplicateSharedResource",
    "ReleaseResource",
    "CreateRenderDataBuilder",
    "QueryResourceInterface",
    "RoundTripRequest",
    "AsyncFlush",
    "PartitionRegisterForNotifications",
    "PartitionSetCurrentMmTask",
    "PartitionSwitchRemotingMode",
    "PartitionSetCursor",
    "PartitionSetMagnifier",
    "PartitionSetExcludeFromDDA",
    "PartitionToggleHolographicSuspension",
    "BitmapSource",
    "DoubleResourceUpdate",
    "RectResourceUpdate",
    "SizeResourceUpdate",
    "ColorTransformResourceUpdate",
    "RedirectVisualSetRedirectedVisual",
    "RenderDataUpdate",
    "SyncLegacyVisualCaptureRenderTargetCaptureBits",
    "VisualSetBlurredWallpaperSurface",
    "VisualSetTouchTargetRect",
    "VisualSetOptions",
    "VisualSetContent",
    "VisualSetColorTransform",
    "VisualTopLevelNode",
    "VisualSetPassiveUpdateMode",
    "VisualSetExcludeSubtree",
    "VisualTargetSetRoot",
    "WindowNodeInitialize",
    "WindowNodeSetIsComposeOnce",
    "VisualGroupUpdate",
    "RectangleGeometrySetRectangle",
    "RenderTargetSetRoot",
    "SyncDesktopCaptureBits",
    "SyncMagnifierRenderTargetCaptureBits",
    "MagnifierRenderTargetCreate",
    "MagnifierRenderTargetSetTransform",
    "MagnifierRenderTargetSetColorTransform",
    "MagnifierRenderTargetUpdate",
    "MagnifierRenderTargetSetFilterList",
    "SyncIndirectSwapchainRenderTargetCreate",
    "IndirectSwapchainRenderTargetUpdateTargetBounds",
    "IndirectSwapchainRenderTargetUnregister",
    "BaseAnimationAddBinding",
    "BaseAnimationRemoveBinding",
    "AnimationUpdateBeginTime",
    "AnimationUpdatePrimitives",
    "AnimationSetTrigger",
    "EffectGroupUpdate",
    "CachedVisualImageUpdate",
    "CachedVisualImageFreeze",
    "CachedVisualImageSnapshot",
    "AnimationTriggerTrigger",
    "MeshGeometry2DUpdate",
    "Geometry2DGroupUpdate",
    "AtlasedRectsMeshUpdate",
    "AtlasedRectsMeshSetOpacity",
    "AtlasedRectsGroupUpdate",
    "GaussianBlurEffectUpdate",
    "MatrixTransform3DUpdate",
    "Transform3DGroupUpdate",
    "TransformGroupUpdate",
    "TranslateTransformUpdate",
    "ScaleTransformUpdate",
    "RotateTransformUpdate",
    "MatrixTransformUpdate",
    "CombinedGeometryUpdate",
    "RgnGeometryUpdate",
    "SolidColorLegacyMilBrushUpdate",
    "LinearGradientLegacyMilBrushUpdate",
    "ImageLegacyMilBrushUpdate",
    "HolographicInteropTextureSetRoot",
    "VisualSetResampleMode",
    "MagnifierRenderTargetSetResampleMode",
    "CaptureControllerSetRootVisual",
    "CaptureControllerSetCaptureState",
    "CaptureControllerSetContentSize",
    "CaptureControllerSetTransform",
    "CaptureControllerSetDefaultSDRBoost",
    "CaptureControllerSetReferenceVisual",
    "CaptureControllerSetSuspendOnScreenOff",
    "CursorVisualSetCursorId",
    "CursorVisualSetIsHardwareCursorEnabled",
    "CursorVisualSetIsSynchronized",
    "CursorVisualSetPosition",
    "CaptureControllerSetWindowInfos",
    "CaptureControllerSetContentOffset",
    "GetCommandBatch",
    "ReleaseCommandBatch",
    "IsRemoteTreeEnabled",
};

C_ASSERT(sizeof(DwmChannelSlotNames) / sizeof(DwmChannelSlotNames[0]) == 103);

static void
TestPrivateChannelInventory(IDwmChannelPrivate *Channel)
{
    const PVOID *Slots;
    UINT Index;

    ok(Channel != NULL, "private channel is NULL\n");
    if (Channel == NULL)
        return;
    ok(Channel->lpVtbl != NULL, "private channel vtable is NULL\n");
    if (Channel->lpVtbl == NULL)
        return;

    Slots = (const PVOID *)Channel->lpVtbl;
    for (Index = 0; Index < ARRAYSIZE(DwmChannelSlotNames); ++Index)
    {
        ok(Slots[Index] != NULL,
           "private channel slot %u (%s) is absent\n",
           Index, DwmChannelSlotNames[Index]);
    }
}

typedef struct _TEST_CHANNEL_PROVIDER
{
    IUnknown IUnknown_iface;
    LONG References;
} TEST_CHANNEL_PROVIDER;

static HRESULT STDMETHODCALLTYPE
TestProvider_QueryInterface(IUnknown *Interface, REFIID InterfaceId,
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
TestProvider_AddRef(IUnknown *Interface)
{
    TEST_CHANNEL_PROVIDER *Provider = CONTAINING_RECORD(
        Interface, TEST_CHANNEL_PROVIDER, IUnknown_iface);
    return InterlockedIncrement(&Provider->References);
}

static ULONG STDMETHODCALLTYPE
TestProvider_Release(IUnknown *Interface)
{
    TEST_CHANNEL_PROVIDER *Provider = CONTAINING_RECORD(
        Interface, TEST_CHANNEL_PROVIDER, IUnknown_iface);
    return InterlockedDecrement(&Provider->References);
}

static IUnknownVtbl TestProviderVtable =
{
    TestProvider_QueryInterface,
    TestProvider_AddRef,
    TestProvider_Release
};

static TEST_CHANNEL_PROVIDER TestProvider =
{
    {&TestProviderVtable},
    1
};

static const WCHAR * const DwmColorizationValueNames[] =
{
    L"ColorizationColor",
    L"ColorizationAfterglow",
    L"ColorizationColorBalance",
    L"ColorizationAfterglowBalance",
    L"ColorizationBlurBalance",
    L"EnableWindowColorization",
    L"ColorizationGlassAttribute"
};

C_ASSERT(sizeof(DwmColorizationValueNames) /
         sizeof(DwmColorizationValueNames[0]) ==
         DWM_COLORIZATION_VALUE_COUNT);

static BOOL
BackupDwmColorizationValues(HKEY *Key,
                            DWM_PRIVATE_REG_VALUE_BACKUP *Backup,
                            BOOL *KeyCreated)
{
    static const WCHAR Path[] = L"Software\\Microsoft\\Windows\\DWM";
    DWORD Disposition;
    LONG Error;
    UINT Index;

    *Key = NULL;
    *KeyCreated = FALSE;
    Error = RegCreateKeyExW(HKEY_CURRENT_USER, Path, 0, NULL, 0,
                            KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, Key,
                            &Disposition);
    ok_eq_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS)
        return FALSE;
    *KeyCreated = (Disposition == REG_CREATED_NEW_KEY);

    for (Index = 0; Index < ARRAYSIZE(DwmColorizationValueNames); ++Index)
    {
        Backup[Index].Name = DwmColorizationValueNames[Index];
        Backup[Index].Present = FALSE;
        Backup[Index].Type = 0;
        Backup[Index].Size = sizeof(Backup[Index].Data);
        Error = RegQueryValueExW(*Key, Backup[Index].Name, NULL,
                                 &Backup[Index].Type, Backup[Index].Data,
                                 &Backup[Index].Size);
        if (Error == ERROR_FILE_NOT_FOUND)
            continue;
        ok_eq_long(Error, ERROR_SUCCESS);
        if (Error != ERROR_SUCCESS)
        {
            RegCloseKey(*Key);
            *Key = NULL;
            return FALSE;
        }
        Backup[Index].Present = TRUE;
    }
    return TRUE;
}

static void
RestoreDwmColorizationValues(HKEY Key,
                             const DWM_PRIVATE_REG_VALUE_BACKUP *Backup,
                             BOOL KeyCreated)
{
    static const WCHAR Path[] = L"Software\\Microsoft\\Windows\\DWM";
    LONG Error;
    UINT Index;

    if (Key == NULL)
        return;
    for (Index = 0; Index < ARRAYSIZE(DwmColorizationValueNames); ++Index)
    {
        if (Backup[Index].Present)
        {
            Error = RegSetValueExW(Key, Backup[Index].Name, 0,
                                   Backup[Index].Type, Backup[Index].Data,
                                   Backup[Index].Size);
        }
        else
        {
            Error = RegDeleteValueW(Key, Backup[Index].Name);
            if (Error == ERROR_FILE_NOT_FOUND)
                Error = ERROR_SUCCESS;
        }
        ok_eq_long(Error, ERROR_SUCCESS);
    }
    RegCloseKey(Key);
    if (KeyCreated)
    {
        Error = RegDeleteKeyW(HKEY_CURRENT_USER, Path);
        ok(Error == ERROR_SUCCESS || Error == ERROR_DIR_NOT_EMPTY,
           "RegDeleteKeyW returned %ld\n", Error);
    }
}

static void
TestDwmApiPrivateCoreSessions(HMODULE DwmApi, HWND FirstWindow,
                              HWND SecondWindow)
{
    PFN_DWMP_GET_GLOBAL_STATE GetGlobalState;
    PFN_DWMP_ACTIVATE_LIVE_PREVIEW ActivateLivePreview;
    PFN_DWMP_QUERY_THUMBNAIL_TYPE QueryThumbnailType;
    PFN_DWMP_REGISTER_THUMBNAIL RegisterThumbnail;
    PFN_DWMP_IS_THREAD_DESKTOP_COMPOSITED IsThreadDesktopComposited;
    PFN_DWMP_SET_COLORIZATION_PARAMETERS SetColorizationParameters;
    PFN_DWMP_GET_COMPOSITION_TIMING_INFO_EX GetTimingInfoEx;
    PFN_DWMP_RENDER_FLICK RenderFlick;
    PFN_DWMP_ALLOCATE_SECURITY_DESCRIPTOR AllocateSecurityDescriptor;
    PFN_DWMP_FREE_SECURITY_DESCRIPTOR FreeSecurityDescriptor;
    BYTE GlobalState[16];
    BYTE ExtendedTiming[0x13c];
    DWM_TIMING_INFO Timing;
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    PACL Dacl = NULL;
    PVOID Ace = NULL;
    BOOL DaclPresent = FALSE, DaclDefaulted = FALSE;
    BOOL Composited = 0x5a;
    ULONGLONG Thumbnail = 0xccccccccccccccccULL;
    DWORD Type = 0xcccccccc;
    POINT Point = {11, 17};
    HRESULT Result;
    BOOL Valid;

    GetGlobalState = (PFN_DWMP_GET_GLOBAL_STATE)GetDwmApiOrdinal(DwmApi, 107);
    ActivateLivePreview = (PFN_DWMP_ACTIVATE_LIVE_PREVIEW)GetDwmApiOrdinal(DwmApi, 113);
    QueryThumbnailType = (PFN_DWMP_QUERY_THUMBNAIL_TYPE)GetDwmApiOrdinal(DwmApi, 114);
    RegisterThumbnail = (PFN_DWMP_REGISTER_THUMBNAIL)GetDwmApiOrdinal(DwmApi, 124);
    IsThreadDesktopComposited = (PFN_DWMP_IS_THREAD_DESKTOP_COMPOSITED)GetDwmApiOrdinal(DwmApi, 128);
    SetColorizationParameters = (PFN_DWMP_SET_COLORIZATION_PARAMETERS)GetDwmApiOrdinal(DwmApi, 131);
    GetTimingInfoEx = (PFN_DWMP_GET_COMPOSITION_TIMING_INFO_EX)GetDwmApiOrdinal(DwmApi, 132);
    RenderFlick = (PFN_DWMP_RENDER_FLICK)GetDwmApiOrdinal(DwmApi, 135);
    AllocateSecurityDescriptor = (PFN_DWMP_ALLOCATE_SECURITY_DESCRIPTOR)GetDwmApiOrdinal(DwmApi, 136);
    FreeSecurityDescriptor = (PFN_DWMP_FREE_SECURITY_DESCRIPTOR)GetDwmApiOrdinal(DwmApi, 137);

    if (GetGlobalState != NULL)
    {
        Result = GetGlobalState(NULL);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(107, "null-output", Result, 0, 0);
        memset(GlobalState, 0xcc, sizeof(GlobalState));
        Result = GetGlobalState(GlobalState);
        RecordDwmApiObservation(107, "state", Result,
                                *(ULONGLONG *)&GlobalState[0],
                                *(ULONGLONG *)&GlobalState[8]);
    }
    if (ActivateLivePreview != NULL)
    {
        Result = ActivateLivePreview(0, FirstWindow, NULL, 0, NULL);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(113, "invalid-type", Result, 0, 0);
    }
    if (QueryThumbnailType != NULL)
    {
        Result = QueryThumbnailType(0, NULL);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(114, "null-output", Result, 0, 0);
        Result = QueryThumbnailType(0, &Type);
        RecordDwmApiObservation(114, "invalid-thumbnail", Result, Type, 0);
    }
    if (RegisterThumbnail != NULL)
    {
        Result = RegisterThumbnail(FirstWindow, FirstWindow, 0, 0,
                                   &Thumbnail);
        ok_hex(Result, E_INVALIDARG);
        ok(Thumbnail == 0xccccccccccccccccULL,
           "DwmpRegisterThumbnail modified output on validation failure\n");
        RecordDwmApiObservation(124, "same-window", Result, Thumbnail, 0);
        if (SecondWindow != NULL)
        {
            Thumbnail = 0;
            Result = RegisterThumbnail(FirstWindow, SecondWindow, 0, 0,
                                       &Thumbnail);
            RecordDwmApiObservation(124, "valid-windows", Result,
                                    Thumbnail, 0);
        }
    }
    if (IsThreadDesktopComposited != NULL)
    {
        Result = IsThreadDesktopComposited(NULL);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(128, "null-output", Result, 0, 0);
        Result = IsThreadDesktopComposited(&Composited);
        RecordDwmApiObservation(128, "thread-desktop", Result,
                                Composited, 0);
    }
    if (SetColorizationParameters != NULL)
    {
        Result = SetColorizationParameters(NULL, TRUE);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(131, "null-parameters", Result, 0, 0);
    }
    if (GetTimingInfoEx != NULL)
    {
        Result = GetTimingInfoEx(NULL, NULL);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(132, "null-output", Result, 0, 0);
        ZeroMemory(&Timing, sizeof(Timing));
        Result = GetTimingInfoEx(NULL, &Timing);
        ok_hex(Result, HRESULT_FROM_WIN32(ERROR_BAD_LENGTH));
        RecordDwmApiObservation(132, "invalid-size", Result,
                                Timing.cbSize, 0);
        ZeroMemory(&Timing, sizeof(Timing));
        Timing.cbSize = sizeof(Timing);
        Result = GetTimingInfoEx(NULL, &Timing);
        RecordDwmApiObservation(132, "legacy-size", Result,
                                Timing.cbSize, Timing.rateRefresh.uiNumerator);
        if (SUCCEEDED(Result))
            ok_eq_ulong(Timing.cbSize, sizeof(Timing));

        memset(ExtendedTiming, 0xcc, sizeof(ExtendedTiming));
        *(DWORD *)ExtendedTiming = sizeof(ExtendedTiming);
        Result = GetTimingInfoEx(NULL, (DWM_TIMING_INFO *)ExtendedTiming);
        RecordDwmApiObservation(132, "extended-size", Result,
                                *(DWORD *)ExtendedTiming,
                                *(ULONGLONG *)&ExtendedTiming[0x124]);
        if (SUCCEEDED(Result))
            ok_eq_ulong(*(DWORD *)ExtendedTiming, sizeof(ExtendedTiming));
    }
    if (RenderFlick != NULL)
    {
        Result = RenderFlick(NULL, Point);
        RecordDwmApiObservation(135, "point-11-17", Result,
                                (DWORD)Point.x, (DWORD)Point.y);
    }
    if (AllocateSecurityDescriptor != NULL && FreeSecurityDescriptor != NULL)
    {
        Result = AllocateSecurityDescriptor(&Descriptor, GENERIC_READ);
        RecordDwmApiObservation(136, "allocate", Result,
                                (ULONGLONG)(ULONG_PTR)Descriptor, 0);
        if (SUCCEEDED(Result))
        {
            Valid = IsValidSecurityDescriptor(Descriptor);
            ok(Valid, "native private descriptor is invalid\n");
            ok(GetSecurityDescriptorDacl(Descriptor, &DaclPresent, &Dacl,
                                         &DaclDefaulted),
               "GetSecurityDescriptorDacl failed: %lu\n", GetLastError());
            ok(DaclPresent && Dacl != NULL && !DaclDefaulted,
               "unexpected private DACL state\n");
            if (Dacl != NULL)
            {
                ok_eq_int(Dacl->AceCount, 1);
                ok(GetAce(Dacl, 0, &Ace), "GetAce failed: %lu\n",
                   GetLastError());
                if (Ace != NULL)
                {
                    ACCESS_ALLOWED_ACE *Allowed = Ace;
                    PSID Sid = (PSID)&Allowed->SidStart;
                    SID_IDENTIFIER_AUTHORITY ExpectedAuthority = SECURITY_NT_AUTHORITY;
                    DWORD SessionId = 0;

                    ProcessIdToSessionId(GetCurrentProcessId(), &SessionId);
                    ok_eq_int(Allowed->Header.AceType, ACCESS_ALLOWED_ACE_TYPE);
                    ok_eq_ulong(Allowed->Mask, GENERIC_READ);
                    ok(IsValidSid(Sid), "private ACE SID is invalid\n");
                    ok(!memcmp(GetSidIdentifierAuthority(Sid),
                               &ExpectedAuthority, sizeof(ExpectedAuthority)),
                       "private ACE has wrong SID authority\n");
                    ok_eq_int(*GetSidSubAuthorityCount(Sid), 3);
                    ok_eq_ulong(*GetSidSubAuthority(Sid, 0),
                                DWM_WINDOW_MANAGER_SID_RID);
                    ok_eq_ulong(*GetSidSubAuthority(Sid, 1), 0);
                    ok_eq_ulong(*GetSidSubAuthority(Sid, 2), SessionId);
                }
            }
            FreeSecurityDescriptor(Descriptor);
            RecordDwmApiObservation(137, "free", S_OK, 0, 0);
        }
    }
}

static void
TestDwmApiPrivateTransitionSessions(HMODULE DwmApi, HWND Window)
{
    PFN_DWMP_BEGIN_TRANSITION_REQUEST BeginRequest;
    PFN_DWMP_TRANSITION_WINDOW TransitionWindow;
    PFN_DWMP_END_TRANSITION_REQUEST EndRequest;
    PFN_DWMP_TRANSITION_WINDOW_WITH_RECTS TransitionWithRects;
    PFN_DWMP_UPDATE_DESKTOP_THUMBNAIL UpdateDesktopThumbnail;
    PFN_DWMP_TRANSITION_BITMAP TransitionBitmap;
    PFN_DWMP_BEGIN_TRANSITION_REQUEST_WITH_GUID BeginWithGuid;
    PFN_DWMP_CREATE_SHARED_THUMBNAIL_VISUAL CreateThumbnailVisual;
    PFN_DWMP_BEGIN_TRANSITION_REQUEST_WITH_GUID_EX BeginWithGuidEx;
    RECT Rect = {0, 0, 16, 16};
    GUID Correlation;
    ULONGLONG Visual = 0xccccccccccccccccULL;
    ULONGLONG Thumbnail = 0xccccccccccccccccULL;
    HRESULT Result;

    BeginRequest = (PFN_DWMP_BEGIN_TRANSITION_REQUEST)GetDwmApiOrdinal(DwmApi, 138);
    TransitionWindow = (PFN_DWMP_TRANSITION_WINDOW)GetDwmApiOrdinal(DwmApi, 139);
    EndRequest = (PFN_DWMP_END_TRANSITION_REQUEST)GetDwmApiOrdinal(DwmApi, 140);
    TransitionWithRects = (PFN_DWMP_TRANSITION_WINDOW_WITH_RECTS)GetDwmApiOrdinal(DwmApi, 141);
    UpdateDesktopThumbnail = (PFN_DWMP_UPDATE_DESKTOP_THUMBNAIL)GetDwmApiOrdinal(DwmApi, 142);
    TransitionBitmap = (PFN_DWMP_TRANSITION_BITMAP)GetDwmApiOrdinal(DwmApi, 144);
    BeginWithGuid = (PFN_DWMP_BEGIN_TRANSITION_REQUEST_WITH_GUID)GetDwmApiOrdinal(DwmApi, 145);
    CreateThumbnailVisual = (PFN_DWMP_CREATE_SHARED_THUMBNAIL_VISUAL)GetDwmApiOrdinal(DwmApi, 147);
    BeginWithGuidEx = (PFN_DWMP_BEGIN_TRANSITION_REQUEST_WITH_GUID_EX)GetDwmApiOrdinal(DwmApi, 148);

    if (BeginRequest != NULL)
    {
        Result = BeginRequest(0x1234);
        RecordDwmApiObservation(138, "begin", Result, 0x1234, 0);
    }
    if (TransitionWindow != NULL)
    {
        Result = TransitionWindow(NULL, 0);
        RecordDwmApiObservation(139, "null-window", Result, 0, 0);
    }
    if (EndRequest != NULL)
    {
        Result = EndRequest(0x1234);
        RecordDwmApiObservation(140, "end", Result, 0x1234, 0);
    }
    if (TransitionWithRects != NULL)
    {
        Result = TransitionWithRects(NULL, 0, &Rect, &Rect, &Rect,
                                     &Rect, &Rect);
        RecordDwmApiObservation(141, "null-window", Result, 0, 0);
    }
    if (UpdateDesktopThumbnail != NULL)
    {
        Result = UpdateDesktopThumbnail(0, &Rect, 0, 0, 0, FALSE, 0);
        RecordDwmApiObservation(142, "invalid-thumbnail", Result, 0, 0);
    }
    if (TransitionBitmap != NULL)
    {
        Result = TransitionBitmap(0, NULL, 0, &Rect, &Rect);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(144, "null-bitmap", Result, 0, 0);
    }
    if (BeginWithGuid != NULL)
    {
        Result = BeginWithGuid(0x1234, &GUID_NULL);
        RecordDwmApiObservation(145, "begin-guid", Result, 0x1234, 0);
    }
    if (CreateThumbnailVisual != NULL)
    {
        Result = CreateThumbnailVisual(NULL, Window, 0, &Rect,
                                       &TestProvider.IUnknown_iface,
                                       &Visual, &Thumbnail);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(147, "invalid-destination", Result,
                                Visual, Thumbnail);
    }
    if (BeginWithGuidEx != NULL)
    {
        memset(&Correlation, 0xcc, sizeof(Correlation));
        Result = BeginWithGuidEx(0x1234, &GUID_NULL, &Correlation);
        RecordDwmApiObservation(148, "begin-guid-ex", Result,
                                Correlation.Data1,
                                ((ULONGLONG)Correlation.Data2 << 16) |
                                Correlation.Data3);
    }
}

static void
TestDwmApiPrivateAnimationSessions(HMODULE DwmApi)
{
    PFN_DWMP_CREATE_ANIMATION_CLOCK CreateClock;
    PFN_DWMP_BEGIN_ANIMATION_CLOCK BeginClock;
    PFN_DWMP_END_ANIMATION_CLOCK EndClock;
    PFN_DWMP_GET_ANIMATION_CLOCK_TIME GetClockTime;
    PFN_DWMP_SET_ANIMATION_CLOCK_TIME SetClockTime;
    PFN_DWMP_GET_ANIMATION_CLOCK_TOKEN GetClockToken;
    ULONGLONG Time = 0xccccccccccccccccULL;
    ULONGLONG Token = 0xccccccccccccccccULL;
    HRESULT Result;

    CreateClock = (PFN_DWMP_CREATE_ANIMATION_CLOCK)GetDwmApiOrdinal(DwmApi, 150);
    BeginClock = (PFN_DWMP_BEGIN_ANIMATION_CLOCK)GetDwmApiOrdinal(DwmApi, 151);
    EndClock = (PFN_DWMP_END_ANIMATION_CLOCK)GetDwmApiOrdinal(DwmApi, 152);
    GetClockTime = (PFN_DWMP_GET_ANIMATION_CLOCK_TIME)GetDwmApiOrdinal(DwmApi, 153);
    SetClockTime = (PFN_DWMP_SET_ANIMATION_CLOCK_TIME)GetDwmApiOrdinal(DwmApi, 154);
    GetClockToken = (PFN_DWMP_GET_ANIMATION_CLOCK_TOKEN)GetDwmApiOrdinal(DwmApi, 155);

    if (CreateClock != NULL)
    {
        Result = CreateClock(0, 0, 0);
        RecordDwmApiObservation(150, "zero-clock", Result, 0, 0);
    }
    if (BeginClock != NULL)
    {
        Result = BeginClock(0, 0, 0);
        RecordDwmApiObservation(151, "zero-clock", Result, 0, 0);
    }
    if (EndClock != NULL)
    {
        Result = EndClock(0, 0);
        RecordDwmApiObservation(152, "zero-clock", Result, 0, 0);
    }
    if (GetClockTime != NULL)
    {
        Result = GetClockTime(0, 0, 0, NULL);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(153, "null-output", Result, 0, 0);
        Result = GetClockTime(0, 0, 0, &Time);
        RecordDwmApiObservation(153, "zero-clock", Result, Time, 0);
    }
    if (SetClockTime != NULL)
    {
        Result = SetClockTime(0, 0, 0, &Time);
        RecordDwmApiObservation(154, "zero-clock", Result, Time, 0);
    }
    if (GetClockToken != NULL)
    {
        Result = GetClockToken(0, 0, NULL);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(155, "null-output", Result, 0, 0);
        Result = GetClockToken(0, 0, &Token);
        RecordDwmApiObservation(155, "zero-clock", Result, Token, 0);
    }
}

static void
TestDwmApiPrivateVisualSessions(HMODULE DwmApi, HWND Window)
{
    PFN_DWMP_REGISTER_SWAPCHAIN_RENDER_TARGET RegisterSwapchain;
    PFN_DWMP_UNREGISTER_SWAPCHAIN_RENDER_TARGET UnregisterSwapchain;
    PFN_DWMP_UPDATE_ACCENT_BLUR_RECT UpdateBlurRect;
    PFN_DWMP_SET_IMMERSIVE_ICONIC SetImmersiveIconic;
    PFN_DWMP_SET_IMMERSIVE_ICONIC_NOTIFY_WINDOW SetNotifyWindow;
    PFN_DWMP_QUERY_WINDOW_THUMBNAIL_SOURCE_SIZE QuerySourceSize;
    PFN_DWMP_CREATE_SHARED_MULTI_WINDOW_VISUAL CreateMultiWindowVisual;
    PFN_DWMP_UPDATE_SHARED_MULTI_WINDOW_VISUAL UpdateMultiWindowVisual;
    PFN_DWMP_SET_HOLOGRAPHIC_EXCLUSIVE_VIEW SetHolographicView;
    PFN_DWMP_SET_CHILD_ROOT_VISUAL SetChildRootVisual;
    PFN_DWMP_GET_HMD_STATUS GetHmdStatus;
    RECT Rect = {0, 0, 16, 16};
    SIZE Size = {16, 16};
    BYTE HmdStatus[32];
    ULONGLONG Visual = 0xccccccccccccccccULL;
    ULONGLONG Token = 0xccccccccccccccccULL;
    DWORD Flags = 0xcccccccc;
    HRESULT Result;

    RegisterSwapchain = (PFN_DWMP_REGISTER_SWAPCHAIN_RENDER_TARGET)GetDwmApiOrdinal(DwmApi, 157);
    UnregisterSwapchain = (PFN_DWMP_UNREGISTER_SWAPCHAIN_RENDER_TARGET)GetDwmApiOrdinal(DwmApi, 158);
    UpdateBlurRect = (PFN_DWMP_UPDATE_ACCENT_BLUR_RECT)GetDwmApiOrdinal(DwmApi, 159);
    SetImmersiveIconic = (PFN_DWMP_SET_IMMERSIVE_ICONIC)GetDwmApiOrdinal(DwmApi, 160);
    SetNotifyWindow = (PFN_DWMP_SET_IMMERSIVE_ICONIC_NOTIFY_WINDOW)GetDwmApiOrdinal(DwmApi, 161);
    QuerySourceSize = (PFN_DWMP_QUERY_WINDOW_THUMBNAIL_SOURCE_SIZE)GetDwmApiOrdinal(DwmApi, 162);
    CreateMultiWindowVisual = (PFN_DWMP_CREATE_SHARED_MULTI_WINDOW_VISUAL)GetDwmApiOrdinal(DwmApi, 163);
    UpdateMultiWindowVisual = (PFN_DWMP_UPDATE_SHARED_MULTI_WINDOW_VISUAL)GetDwmApiOrdinal(DwmApi, 164);
    SetHolographicView = (PFN_DWMP_SET_HOLOGRAPHIC_EXCLUSIVE_VIEW)GetDwmApiOrdinal(DwmApi, 165);
    SetChildRootVisual = (PFN_DWMP_SET_CHILD_ROOT_VISUAL)GetDwmApiOrdinal(DwmApi, 166);
    GetHmdStatus = (PFN_DWMP_GET_HMD_STATUS)GetDwmApiOrdinal(DwmApi, 167);

    if (RegisterSwapchain != NULL)
    {
        Result = RegisterSwapchain(NULL, 0, 0, 0);
        RecordDwmApiObservation(157, "null-swapchain", Result, 0, 0);
    }
    if (UnregisterSwapchain != NULL)
    {
        Result = UnregisterSwapchain(0, &Flags);
        ok_hex(Result, E_INVALIDARG);
        ok_eq_ulong(Flags, 0xcccccccc);
        RecordDwmApiObservation(158, "zero-target", Result, Flags, 0);
    }
    if (UpdateBlurRect != NULL)
    {
        Result = UpdateBlurRect(Window, NULL);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(159, "null-rect", Result, 0, 0);
    }
    if (SetImmersiveIconic != NULL)
    {
        Result = SetImmersiveIconic(NULL, NULL, 0, 0);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(160, "null-window", Result, 0, 0);
    }
    if (SetNotifyWindow != NULL)
    {
        Result = SetNotifyWindow(NULL);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(161, "null-window", Result, 0, 0);
    }
    if (QuerySourceSize != NULL)
    {
        Result = QuerySourceSize(0, 0, NULL);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(162, "null-output", Result, 0, 0);
        Size.cx = Size.cy = 0xcccccccc;
        Result = QuerySourceSize(0, 0, &Size);
        RecordDwmApiObservation(162, "zero-thumbnail", Result,
                                (DWORD)Size.cx, (DWORD)Size.cy);
    }
    if (CreateMultiWindowVisual != NULL)
    {
        Result = CreateMultiWindowVisual(NULL,
                                         &TestProvider.IUnknown_iface,
                                         &Visual, &Token);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(163, "null-window", Result,
                                Visual, Token);
    }
    if (UpdateMultiWindowVisual != NULL)
    {
        Result = UpdateMultiWindowVisual(0, NULL, 0, NULL, 0,
                                         &Rect, &Size, 0);
        RecordDwmApiObservation(164, "zero-token", Result, 0, 0);
    }
    if (SetHolographicView != NULL)
    {
        Result = SetHolographicView(FALSE);
        RecordDwmApiObservation(165, "disable", Result, 0, 0);
    }
    if (SetChildRootVisual != NULL)
    {
        Result = SetChildRootVisual(NULL, NULL, NULL, 0, 0);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(166, "null-window", Result, 0, 0);
    }
    if (GetHmdStatus != NULL)
    {
        Result = GetHmdStatus(NULL);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(167, "null-output", Result, 0, 0);
        memset(HmdStatus, 0xcc, sizeof(HmdStatus));
        Result = GetHmdStatus(HmdStatus);
        RecordDwmApiObservation(167, "status", Result,
                                *(ULONGLONG *)&HmdStatus[0],
                                *(ULONGLONG *)&HmdStatus[8]);
    }
}

static void
TestDwmSessionShutdownEventName(HANDLE Event, DWORD SessionId)
{
    struct
    {
        OBJECT_NAME_INFORMATION Information;
        WCHAR Buffer[96];
    } NameBuffer;
    WCHAR Expected[96];
    NTSTATUS Status;
    ULONG ReturnLength = 0;
    SIZE_T ExpectedLength;

    ZeroMemory(&NameBuffer, sizeof(NameBuffer));
    Status = NtQueryObject(Event, ObjectNameInformation, &NameBuffer,
                           sizeof(NameBuffer), &ReturnLength);
    ok_ntstatus(Status, 0);
    if (!NT_SUCCESS(Status))
        return;

    wsprintfW(Expected,
              L"\\Sessions\\%d\\Windows\\DwmCatastrophicShutdown",
              (INT)SessionId);
    ExpectedLength = lstrlenW(Expected) * sizeof(WCHAR);
    ok(NameBuffer.Information.Name.Length == ExpectedLength,
       "shutdown event name length %u, expected %Iu\n",
       NameBuffer.Information.Name.Length, ExpectedLength);
    if (NameBuffer.Information.Name.Length == ExpectedLength)
    {
        ok(!memcmp(NameBuffer.Information.Name.Buffer, Expected,
                   ExpectedLength),
           "shutdown event has unexpected NT object name %s\n",
           wine_dbgstr_wn(NameBuffer.Information.Name.Buffer,
                          NameBuffer.Information.Name.Length /
                          sizeof(WCHAR)));
    }
}

static void
TestDwmApiPrivateColorAndSessionState(HMODULE DwmApi)
{
    PFN_DWMP_SET_COLORIZATION_PARAMETERS SetColorizationParameters;
    PFN_DWMP_RESET_COLORIZATION_PARAMETERS ResetColorizationParameters;
    PFN_DWMP_READ_COLORIZATION_PARAMETERS ReadColorizationParameters;
    PFN_DWMP_CREATE_SESSION_SHUTDOWN_EVENT CreateShutdownEvent;
    PFN_DWMP_SDR_TO_HDR_BOOST SetSdrToHdrBoost;
    DWM_PRIVATE_REG_VALUE_BACKUP Backup[DWM_COLORIZATION_VALUE_COUNT];
    DWM_PRIVATE_COLORIZATION_TEST Parameters;
    DWORD Values[DWM_COLORIZATION_VALUE_COUNT] =
    {
        0x11223344, 0x55667788, 101, 102, 103, 2, 7
    };
    BOOL KeyCreated;
    DWORD SessionId = 0;
    HKEY Key;
    HANDLE Event = NULL;
    HRESULT Result;
    LONG Error;
    UINT Index;

    SetColorizationParameters = (PFN_DWMP_SET_COLORIZATION_PARAMETERS)GetDwmApiOrdinal(DwmApi, 131);
    ResetColorizationParameters = (PFN_DWMP_RESET_COLORIZATION_PARAMETERS)GetDwmApiOrdinal(DwmApi, 168);
    ReadColorizationParameters = (PFN_DWMP_READ_COLORIZATION_PARAMETERS)GetDwmApiOrdinal(DwmApi, 169);
    CreateShutdownEvent = (PFN_DWMP_CREATE_SESSION_SHUTDOWN_EVENT)GetDwmApiOrdinal(DwmApi, 170);
    SetSdrToHdrBoost = (PFN_DWMP_SDR_TO_HDR_BOOST)GetDwmApiOrdinal(DwmApi, 171);

    ZeroMemory(Backup, sizeof(Backup));
    if (BackupDwmColorizationValues(&Key, Backup, &KeyCreated))
    {
        for (Index = 0; Index < ARRAYSIZE(Values); ++Index)
        {
            Error = RegSetValueExW(Key, DwmColorizationValueNames[Index],
                                   0, REG_DWORD, (const BYTE *)&Values[Index],
                                   sizeof(Values[Index]));
            ok_eq_long(Error, ERROR_SUCCESS);
        }

        if (ReadColorizationParameters != NULL)
        {
            Result = ReadColorizationParameters(NULL);
            ok_hex(Result, E_INVALIDARG);
            RecordDwmApiObservation(169, "null-output", Result, 0, 0);
            memset(&Parameters, 0xcc, sizeof(Parameters));
            Result = ReadColorizationParameters(&Parameters);
            RecordDwmApiObservation(169, "controlled-registry", Result,
                                    Parameters.ColorizationColor,
                                    Parameters.ColorizationAfterglow);
            if (SUCCEEDED(Result))
            {
                ok_eq_ulong(Parameters.ColorizationColor, Values[0]);
                ok_eq_ulong(Parameters.ColorizationAfterglow, Values[1]);
                ok_eq_ulong(Parameters.ColorizationColorBalance, 27);
                ok_eq_ulong(Parameters.ColorizationAfterglowBalance, 0);
                ok_eq_ulong(Parameters.ColorizationBlurBalance, 73);
                ok_eq_int(Parameters.EnableWindowColorization, TRUE);
                ok_eq_ulong(Parameters.ColorizationGlassAttribute, Values[6]);
                ok_eq_ulong(Parameters.Reserved, 0xcccccccc);
            }
        }
        if (SetColorizationParameters != NULL)
        {
            Result = SetColorizationParameters(&Parameters, TRUE);
            RecordDwmApiObservation(131, "do-not-persist", Result,
                                    Parameters.ColorizationColor,
                                    Parameters.ColorizationAfterglow);
        }
        if (ResetColorizationParameters != NULL)
        {
            Result = ResetColorizationParameters();
            ok_hex(Result, S_OK);
            RecordDwmApiObservation(168, "reset", Result, 0, 0);
            for (Index = 0; Index < 6; ++Index)
            {
                DWORD Size = sizeof(Values[Index]);
                Error = RegQueryValueExW(Key,
                                         DwmColorizationValueNames[Index],
                                         NULL, NULL, (BYTE *)&Values[Index],
                                         &Size);
                ok_eq_long(Error, ERROR_FILE_NOT_FOUND);
            }
        }
        RestoreDwmColorizationValues(Key, Backup, KeyCreated);
    }
    else
    {
        skip("cannot preserve DWM colorization registry values\n");
    }

    if (CreateShutdownEvent != NULL)
    {
        ProcessIdToSessionId(GetCurrentProcessId(), &SessionId);
        Result = CreateShutdownEvent(SessionId, &Event);
        RecordDwmApiObservation(170, "current-session", Result,
                                (ULONGLONG)(ULONG_PTR)Event, SessionId);
        if (Event != NULL)
        {
            TestDwmSessionShutdownEventName(Event, SessionId);
            CloseHandle(Event);
        }
    }
    if (SetSdrToHdrBoost != NULL)
    {
        Result = SetSdrToHdrBoost(0, 0);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(171, "zero-target", Result, 0, 0);
    }
}

static void
TestDwmApiPrivateCaptureSessions(HMODULE DwmApi, HWND Window)
{
    PFN_DWMP_BEGIN_WINDOW_CAPTURE BeginWindowCapture;
    PFN_DWMP_BEGIN_DISPLAY_CAPTURE BeginDisplayCapture;
    PFN_DWMP_UPDATE_WINDOW_CAPTURE UpdateWindowCapture;
    PFN_DWMP_STOP_WINDOW_CAPTURE StopWindowCapture;
    PFN_DWMP_STOP_DISPLAY_CAPTURE StopDisplayCapture;
    PFN_DWMP_GET_ANIMATION_COMMIT_HANDLE GetAnimationCommitHandle;
    PFN_DWMP_GET_TITLEBAR_INFO GetTitlebarInfo;
    PFN_DWMP_ADD_SHARED_PROJECTED_SHADOW_CASTER AddShadowCaster;
    PFN_DWMP_BEGIN_VIRTUAL_MONITOR_CAPTURE BeginVirtualMonitorCapture;
    PFN_DWMP_STOP_VIRTUAL_MONITOR_CAPTURE StopVirtualMonitorCapture;
    PFN_DWMP_UPDATE_PROXY_WINDOW_FOR_CAPTURE UpdateProxyWindow;
    PFN_DWMP_UPDATE_WINDOW_CAPTURE_BORDER UpdateWindowBorder;
    PFN_DWMP_UPDATE_DISPLAY_CAPTURE_BORDER UpdateDisplayBorder;
    PFN_DWMP_SET_BLURRED_WALLPAPER_SURFACE SetBlurredWallpaperSurface;
    PFN_DWMP_ACTIVATE_LIVE_PREVIEW_EX ActivateLivePreviewEx;
    PFN_DWMP_ENABLE_WINDOW_NOTIFICATIONS EnableWindowNotifications;
    PFN_DWMP_ENABLE_MODE_CHANGE_ANIMATION EnableModeChangeAnimation;
    PFN_DWMP_BEGIN_FILTERED_DISPLAY_CAPTURE BeginFilteredDisplayCapture;
    PFN_DWMP_STOP_FILTERED_DISPLAY_CAPTURE StopFilteredDisplayCapture;
    PFN_DWMP_ADD_REMOVE_WINDOW_FILTERED_CAPTURE ChangeFilteredCaptureWindow;
    PFN_DWMP_UPDATE_FILTERED_DISPLAY_CAPTURE_BORDER UpdateFilteredBorder;
    BYTE TitlebarInfo[128];
    ULONGLONG Token;
    HRESULT Result;

    BeginWindowCapture = (PFN_DWMP_BEGIN_WINDOW_CAPTURE)GetDwmApiOrdinal(DwmApi, 173);
    BeginDisplayCapture = (PFN_DWMP_BEGIN_DISPLAY_CAPTURE)GetDwmApiOrdinal(DwmApi, 174);
    UpdateWindowCapture = (PFN_DWMP_UPDATE_WINDOW_CAPTURE)GetDwmApiOrdinal(DwmApi, 175);
    StopWindowCapture = (PFN_DWMP_STOP_WINDOW_CAPTURE)GetDwmApiOrdinal(DwmApi, 176);
    StopDisplayCapture = (PFN_DWMP_STOP_DISPLAY_CAPTURE)GetDwmApiOrdinal(DwmApi, 177);
    GetAnimationCommitHandle = (PFN_DWMP_GET_ANIMATION_COMMIT_HANDLE)GetDwmApiOrdinal(DwmApi, 178);
    GetTitlebarInfo = (PFN_DWMP_GET_TITLEBAR_INFO)GetDwmApiOrdinal(DwmApi, 179);
    AddShadowCaster = (PFN_DWMP_ADD_SHARED_PROJECTED_SHADOW_CASTER)GetDwmApiOrdinal(DwmApi, 180);
    BeginVirtualMonitorCapture = (PFN_DWMP_BEGIN_VIRTUAL_MONITOR_CAPTURE)GetDwmApiOrdinal(DwmApi, 181);
    StopVirtualMonitorCapture = (PFN_DWMP_STOP_VIRTUAL_MONITOR_CAPTURE)GetDwmApiOrdinal(DwmApi, 182);
    UpdateProxyWindow = (PFN_DWMP_UPDATE_PROXY_WINDOW_FOR_CAPTURE)GetDwmApiOrdinal(DwmApi, 183);
    UpdateWindowBorder = (PFN_DWMP_UPDATE_WINDOW_CAPTURE_BORDER)GetDwmApiOrdinal(DwmApi, 184);
    UpdateDisplayBorder = (PFN_DWMP_UPDATE_DISPLAY_CAPTURE_BORDER)GetDwmApiOrdinal(DwmApi, 185);
    SetBlurredWallpaperSurface = (PFN_DWMP_SET_BLURRED_WALLPAPER_SURFACE)GetDwmApiOrdinal(DwmApi, 186);
    ActivateLivePreviewEx = (PFN_DWMP_ACTIVATE_LIVE_PREVIEW_EX)GetDwmApiOrdinal(DwmApi, 187);
    EnableWindowNotifications = (PFN_DWMP_ENABLE_WINDOW_NOTIFICATIONS)GetDwmApiOrdinal(DwmApi, 188);
    EnableModeChangeAnimation = (PFN_DWMP_ENABLE_MODE_CHANGE_ANIMATION)GetDwmApiOrdinal(DwmApi, 189);
    BeginFilteredDisplayCapture = (PFN_DWMP_BEGIN_FILTERED_DISPLAY_CAPTURE)GetDwmApiOrdinal(DwmApi, 190);
    StopFilteredDisplayCapture = (PFN_DWMP_STOP_FILTERED_DISPLAY_CAPTURE)GetDwmApiOrdinal(DwmApi, 191);
    ChangeFilteredCaptureWindow = (PFN_DWMP_ADD_REMOVE_WINDOW_FILTERED_CAPTURE)GetDwmApiOrdinal(DwmApi, 192);
    UpdateFilteredBorder = (PFN_DWMP_UPDATE_FILTERED_DISPLAY_CAPTURE_BORDER)GetDwmApiOrdinal(DwmApi, 193);

    if (BeginWindowCapture != NULL)
    {
        Token = 0xccccccccccccccccULL;
        Result = BeginWindowCapture((ULONGLONG)(ULONG_PTR)Window, NULL,
                                    &Token);
        ok(Token == 0, "DwmpBeginWindowCapture did not clear token\n");
        RecordDwmApiObservation(173, "null-swapchain", Result, Token, 0);
    }
    if (BeginDisplayCapture != NULL)
    {
        Token = 0xccccccccccccccccULL;
        Result = BeginDisplayCapture(0, NULL, &Token);
        ok(Token == 0, "DwmpBeginDisplayCapture did not clear token\n");
        RecordDwmApiObservation(174, "null-swapchain", Result, Token, 0);
    }
    if (UpdateWindowCapture != NULL)
    {
        Result = UpdateWindowCapture(0, 0);
        RecordDwmApiObservation(175, "zero-token", Result, 0, 0);
    }
    if (StopWindowCapture != NULL)
    {
        Result = StopWindowCapture(0);
        RecordDwmApiObservation(176, "zero-token", Result, 0, 0);
    }
    if (StopDisplayCapture != NULL)
    {
        Result = StopDisplayCapture(0);
        RecordDwmApiObservation(177, "zero-token", Result, 0, 0);
    }
    if (GetAnimationCommitHandle != NULL)
    {
        Result = GetAnimationCommitHandle(0, 0, NULL);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(178, "null-output", Result, 0, 0);
    }
    if (GetTitlebarInfo != NULL)
    {
        memset(TitlebarInfo, 0xcc, sizeof(TitlebarInfo));
        Result = GetTitlebarInfo(NULL, TitlebarInfo);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(179, "null-window", Result,
                                *(ULONGLONG *)&TitlebarInfo[0], 0);
    }
    if (AddShadowCaster != NULL)
    {
        Result = AddShadowCaster(0, (HANDLE)(ULONG_PTR)1, NULL);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(180, "one-sided-handles", Result, 1, 0);
    }
    if (BeginVirtualMonitorCapture != NULL)
    {
        Token = 0xccccccccccccccccULL;
        Result = BeginVirtualMonitorCapture(0, NULL, &Token);
        ok(Token == 0,
           "DwmpBeginVirtualMonitorCapture did not clear token\n");
        RecordDwmApiObservation(181, "null-swapchain", Result, Token, 0);
    }
    if (StopVirtualMonitorCapture != NULL)
    {
        Result = StopVirtualMonitorCapture(0);
        RecordDwmApiObservation(182, "zero-token", Result, 0, 0);
    }
    if (UpdateProxyWindow != NULL)
    {
        Result = UpdateProxyWindow(0, NULL);
        ok_hex(Result, E_ACCESSDENIED);
        RecordDwmApiObservation(183, "null-window", Result, 0, 0);
    }
    if (UpdateWindowBorder != NULL)
    {
        Result = UpdateWindowBorder(0, 0);
        RecordDwmApiObservation(184, "zero-token", Result, 0, 0);
    }
    if (UpdateDisplayBorder != NULL)
    {
        Result = UpdateDisplayBorder(0, 0);
        RecordDwmApiObservation(185, "zero-token", Result, 0, 0);
    }
    if (SetBlurredWallpaperSurface != NULL)
    {
        Result = SetBlurredWallpaperSurface(&TestProvider.IUnknown_iface, 0);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(186, "surface-without-handle", Result, 0, 0);
        Result = SetBlurredWallpaperSurface(NULL, 0);
        RecordDwmApiObservation(186, "clear", Result, 0, 0);
    }
    if (ActivateLivePreviewEx != NULL)
    {
        Result = ActivateLivePreviewEx(0, NULL, 0, NULL, 0, NULL);
        ok_hex(Result, E_INVALIDARG);
        RecordDwmApiObservation(187, "invalid-type", Result, 0, 0);
    }
    if (EnableWindowNotifications != NULL)
    {
        Result = EnableWindowNotifications(FALSE);
        RecordDwmApiObservation(188, "disable", Result, 0, 0);
    }
    if (EnableModeChangeAnimation != NULL)
    {
        Result = EnableModeChangeAnimation(FALSE);
        RecordDwmApiObservation(189, "disable", Result, 0, 0);
    }
    if (BeginFilteredDisplayCapture != NULL)
    {
        Token = 0xccccccccccccccccULL;
        Result = BeginFilteredDisplayCapture(0, NULL, &Token);
        ok(Token == 0,
           "DwmpBeginFilteredDisplayCapture did not clear token\n");
        RecordDwmApiObservation(190, "null-swapchain", Result, Token, 0);
    }
    if (StopFilteredDisplayCapture != NULL)
    {
        Result = StopFilteredDisplayCapture(0);
        RecordDwmApiObservation(191, "zero-token", Result, 0, 0);
    }
    if (ChangeFilteredCaptureWindow != NULL)
    {
        Result = ChangeFilteredCaptureWindow(0, TRUE, NULL);
        ok_hex(Result, E_ACCESSDENIED);
        RecordDwmApiObservation(192, "null-window", Result, 0, 0);
    }
    if (UpdateFilteredBorder != NULL)
    {
        Result = UpdateFilteredBorder(0, 0);
        RecordDwmApiObservation(193, "zero-token", Result, 0, 0);
    }
}

static void
TestDwmApiPrivateBehaviorSessions(HMODULE DwmApi)
{
    HWND FirstWindow, SecondWindow;

    ZeroMemory(DwmApiFormerStubCovered, sizeof(DwmApiFormerStubCovered));
    FirstWindow = CreateWindowExW(0, L"STATIC", L"dwmstack-private-first",
                                  WS_OVERLAPPED, 0, 0, 64, 64,
                                  NULL, NULL, GetModuleHandleW(NULL), NULL);
    SecondWindow = CreateWindowExW(0, L"STATIC", L"dwmstack-private-second",
                                   WS_OVERLAPPED, 72, 0, 64, 64,
                                   NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(FirstWindow != NULL, "first private test window failed: %lu\n",
       GetLastError());
    ok(SecondWindow != NULL, "second private test window failed: %lu\n",
       GetLastError());

    TestDwmApiPrivateCoreSessions(DwmApi, FirstWindow, SecondWindow);
    TestDwmApiPrivateTransitionSessions(DwmApi, FirstWindow);
    TestDwmApiPrivateAnimationSessions(DwmApi);
    TestDwmApiPrivateVisualSessions(DwmApi, FirstWindow);
    TestDwmApiPrivateColorAndSessionState(DwmApi);
    TestDwmApiPrivateCaptureSessions(DwmApi, FirstWindow);
    VerifyDwmApiFormerStubCoverage();

    if (SecondWindow != NULL)
        DestroyWindow(SecondWindow);
    if (FirstWindow != NULL)
        DestroyWindow(FirstWindow);
}

static void
RunInitializedLifecycleProbe(PFN_DWM_CREATE_CHANNEL CreateChannel,
                             PFN_DWM_CREATE_CURSOR CreateCursor,
                             PFN_DWM_GET_EVENT_ID GetEventId,
                             PFN_DWM_INITIALIZE Initialize,
                             PFN_DWM_UNINITIALIZE Uninitialize)
{
    BYTE WrongConnection[64] = {0};
    PVOID Connection = NULL;
    PVOID SecondConnection = NULL;
    PVOID Object = NULL;
    UINT EventId = 0;
    HRESULT Result;

    Result = Initialize(0x0F, &Connection);
    trace("lifecycle: Initialize(0x0f) = 0x%08lx, connection %p\n",
          Result, Connection);
    if (FAILED(Result))
        return;

    Result = GetEventId(&EventId);
    ok_hex(Result, S_OK);
    trace("lifecycle: composed event id = 0x%x\n", EventId);

    Object = (PVOID)(ULONG_PTR)0xDEADBEEF;
    Result = CreateChannel(NULL, &Object);
    ok_hex(Result, E_INVALIDARG);
    ok(Object == (PVOID)(ULONG_PTR)0xDEADBEEF,
       "CreateChannel changed output on failure: %p\n", Object);

    /* Native validates the provider before the output pointer.  Do not pass a
     * fake provider here: a later native build may query it before reporting
     * the null output. */
    Result = CreateChannel(NULL, NULL);
    ok_hex(Result, E_INVALIDARG);

    Object = NULL;
    Result = CreateChannel(&TestProvider.IUnknown_iface, &Object);
    ok_hex(Result, S_OK);
    if (SUCCEEDED(Result) && Object != NULL)
    {
        IDwmChannelPrivate *Channel = (IDwmChannelPrivate *)Object;
        UINT Resource = 0xDEADBEEF;

        TestPrivateChannelInventory(Channel);

        Result = Channel->lpVtbl->CreateResource(Channel, 44, &Resource);
        ok_hex(Result, E_INVALIDARG);
        ok_eq_ulong(Resource, 0);
        Result = Channel->lpVtbl->CreateResource(Channel, 0, NULL);
        ok_hex(Result, E_INVALIDARG);
        Result = Channel->lpVtbl->ReleaseResource(Channel, 0);
        ok_hex(Result, E_INVALIDARG);

        Channel->lpVtbl->Release(Channel);
    }

    Object = NULL;
    Result = CreateCursor(0, &Object);
    trace("lifecycle: CreateCursorController = 0x%08lx, object %p\n",
          Result, Object);
    if (SUCCEEDED(Result) && Object != NULL)
        IUnknown_Release((IUnknown *)Object);

    SecondConnection = (PVOID)(ULONG_PTR)0xDEADBEEF;
    Result = Initialize(0x0F, &SecondConnection);
    ok_hex(Result, DWM_E_ENGINE_NOT_INITIALIZED);
    ok(SecondConnection == (PVOID)(ULONG_PTR)0xDEADBEEF,
       "second Initialize changed output: %p\n", SecondConnection);

    Result = Uninitialize(WrongConnection);
    ok_hex(Result, E_INVALIDARG);

    Result = Uninitialize(Connection);
    trace("lifecycle: Uninitialize = 0x%08lx\n", Result);
    ok_hex(Result, S_OK);
}

START_TEST(dwmstack)
{
    HMODULE Core, UserDwm, ApiSet, DwmApi, DwmExe;
    PFN_DWM_CREATE_CHANNEL CreateChannel;
    PFN_DWM_CREATE_CURSOR CreateCursor;
    PFN_DWM_CREATE_BITMAP CreateBitmap;
    PFN_DWM_GET_EVENT_ID GetEventId;
    PFN_DWM_INITIALIZE Initialize;
    PFN_DWM_UNINITIALIZE Uninitialize;
    UINT EventId = 0xDEADBEEF;
    PVOID Object = (PVOID)(ULONG_PTR)0xDEADBEEF;
    HRESULT Result;
    static const BYTE OnePixelPng[] =
    {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x04, 0x00, 0x00, 0x00, 0xb5, 0x1c, 0x0c, 0x02,
        0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41, 0x54,
        0x78, 0xda, 0x63, 0x64, 0xf8, 0x0f, 0x00, 0x01,
        0x05, 0x01, 0x01, 0x27, 0x18, 0xe3, 0x66, 0x00,
        0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae,
        0x42, 0x60, 0x82
    };

    Core = LoadLibraryW(L"dwmcore.dll");
    ok(Core != NULL, "LoadLibrary(dwmcore.dll) failed: %lu\n", GetLastError());
    if (Core == NULL)
        return;
    TestImageContract(Core, "dwmcore", TRUE);

    UserDwm = LoadLibraryW(L"uDWM.dll");
    ok(UserDwm != NULL, "LoadLibrary(uDWM.dll) failed: %lu\n", GetLastError());
    if (UserDwm != NULL)
        TestImageContract(UserDwm, "uDWM", TRUE);

    ApiSet = LoadLibraryW(L"api-ms-win-composition-windowmanager-l1-1-0.dll");
    ok(ApiSet != NULL, "Window Manager API set failed: %lu\n", GetLastError());

    DwmApi = LoadLibraryW(L"dwmapi.dll");
    ok(DwmApi != NULL, "LoadLibrary(dwmapi.dll) failed: %lu\n", GetLastError());
    if (DwmApi != NULL)
    {
        TestImageContract(DwmApi, "dwmapi", TRUE);
        TestDwmApiExports(DwmApi);
        TestDwmApiPrivateBehaviorSessions(DwmApi);
    }

    DwmExe = LoadSystemImageWithoutImports(L"dwm.exe");
    ok(DwmExe != NULL, "mapping dwm.exe failed: %lu\n", GetLastError());
    if (DwmExe != NULL)
        TestImageContract(DwmExe, "dwm.exe", FALSE);

    TestOrdinalInventory(Core, "dwmcore", 1000, 7, 5);
    if (UserDwm != NULL)
        TestOrdinalInventory(UserDwm, "uDWM", 101, 1, 0);

    ok(GetProcAddress(Core, (LPCSTR)(ULONG_PTR)1000) != NULL,
       "dwmcore ordinal 1000 is absent\n");
    ok(GetProcAddress(Core, (LPCSTR)(ULONG_PTR)1001) != NULL,
       "dwmcore ordinal 1001 is absent\n");
    ok(GetProcAddress(Core, "CreateCompressedSourceBitmap") == NULL,
       "dwmcore ordinal 1000 unexpectedly has a public name\n");
    ok(GetProcAddress(Core, "SignalStartNowEvent") == NULL,
       "dwmcore ordinal 1001 unexpectedly has a public name\n");

    CreateChannel = (PFN_DWM_CREATE_CHANNEL)GetProcAddress(
        Core, "MilCompositionEngine_CreateChannel");
    CreateCursor = (PFN_DWM_CREATE_CURSOR)GetProcAddress(
        Core, "MilCompositionEngine_CreateCursorController");
    CreateBitmap = (PFN_DWM_CREATE_BITMAP)GetProcAddress(
        Core, (LPCSTR)(ULONG_PTR)1000);
    GetEventId = (PFN_DWM_GET_EVENT_ID)GetProcAddress(
        Core, "MilCompositionEngine_GetComposedEventId");
    Initialize = (PFN_DWM_INITIALIZE)GetProcAddress(
        Core, "MilCompositionEngine_Initialize");
    Uninitialize = (PFN_DWM_UNINITIALIZE)GetProcAddress(
        Core, "MilCompositionEngine_Uninitialize");

    ok(CreateChannel != NULL, "dwmcore ordinal 1002 is absent\n");
    ok(CreateCursor != NULL, "dwmcore ordinal 1003 is absent\n");
    ok(GetEventId != NULL, "dwmcore ordinal 1004 is absent\n");
    ok(Initialize != NULL, "dwmcore ordinal 1005 is absent\n");
    ok(Uninitialize != NULL, "dwmcore ordinal 1006 is absent\n");

    if (UserDwm != NULL)
    {
        ok(GetProcAddress(UserDwm, (LPCSTR)(ULONG_PTR)101) != NULL,
           "uDWM ordinal 101 is absent\n");
        ok(GetProcAddress(UserDwm, "DwmClientStartup") == NULL,
           "uDWM ordinal 101 unexpectedly has a public name\n");
    }
    if (ApiSet != NULL)
    {
        ok(GetProcAddress(ApiSet, (LPCSTR)(ULONG_PTR)101) != NULL,
           "Window Manager API-set ordinal 101 is absent\n");
    }

    if (CreateChannel != NULL)
    {
        Result = CreateChannel(NULL, &Object);
        ok_hex(Result, DWM_E_CHANNEL_UNAVAILABLE);
        ok(Object == (PVOID)(ULONG_PTR)0xDEADBEEF,
           "uninitialized CreateChannel changed output: %p\n", Object);
    }
    if (CreateCursor != NULL)
    {
        Result = CreateCursor(0, &Object);
        ok_hex(Result, DWM_E_ENGINE_NOT_INITIALIZED);
    }
    if (GetEventId != NULL)
    {
        Result = GetEventId(&EventId);
        ok_hex(Result, DWM_E_ENGINE_NOT_INITIALIZED);
        ok_eq_ulong(EventId, 0xDEADBEEF);
    }
    if (Initialize != NULL)
    {
        Result = Initialize(0, NULL);
        ok_hex(Result, E_INVALIDARG);
    }
    if (Uninitialize != NULL)
    {
        Result = Uninitialize(NULL);
        ok_hex(Result, E_INVALIDARG);
    }

    if (GetEnvironmentVariableW(L"DWMSTACK_ENGINE_LIFECYCLE", NULL, 0) != 0 &&
        CreateChannel != NULL && CreateCursor != NULL && GetEventId != NULL &&
        Initialize != NULL && Uninitialize != NULL)
    {
        RunInitializedLifecycleProbe(CreateChannel, CreateCursor, GetEventId,
                                     Initialize, Uninitialize);
    }

    if (CreateBitmap != NULL)
    {
        IWICImagingFactory *Factory = NULL;
        IWICBitmap *Bitmap = NULL;
        UINT Width = 0, Height = 0;
        double DpiX = 0.0, DpiY = 0.0;
        HRESULT ComResult = CoInitializeEx(NULL, COINIT_MULTITHREADED);

        ok(SUCCEEDED(ComResult) || ComResult == RPC_E_CHANGED_MODE,
           "CoInitializeEx returned 0x%08lx\n", ComResult);
        Result = CoCreateInstance(&CLSID_WICImagingFactory, NULL,
                                  CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory,
                                  (PVOID *)&Factory);
        ok_hex(Result, S_OK);
        if (SUCCEEDED(Result))
        {
            Result = CreateBitmap(Factory, OnePixelPng,
                                  ARRAYSIZE(OnePixelPng),
                                  120.0, 144.0, &Bitmap);
            ok_hex(Result, S_OK);
            if (SUCCEEDED(Result))
            {
                Result = IWICBitmap_GetSize(Bitmap, &Width, &Height);
                ok_hex(Result, S_OK);
                ok_eq_int(Width, 1);
                ok_eq_int(Height, 1);
                Result = IWICBitmap_GetResolution(Bitmap, &DpiX, &DpiY);
                ok_hex(Result, S_OK);
                ok(DpiX == 120.0, "DpiX is %.17g\n", DpiX);
                ok(DpiY == 144.0, "DpiY is %.17g\n", DpiY);
                IWICBitmap_Release(Bitmap);
            }
            IWICImagingFactory_Release(Factory);
        }
        if (SUCCEEDED(ComResult))
            CoUninitialize();
    }

    if (ApiSet != NULL)
        FreeLibrary(ApiSet);
    if (DwmExe != NULL)
        FreeLibrary(DwmExe);
    if (DwmApi != NULL)
        FreeLibrary(DwmApi);
    if (UserDwm != NULL)
        FreeLibrary(UserDwm);
    FreeLibrary(Core);
}
