/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __WINE_DWMAPI_H
#define __WINE_DWMAPI_H

#include "wtypes.h"
#include "uxtheme.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DWMAPI
# define DWMAPI        STDAPI
# define DWMAPI_(type) STDAPI_(type)
#endif

DECLARE_HANDLE(HTHUMBNAIL);
typedef HTHUMBNAIL *PHTHUMBNAIL;

#pragma pack(push,1)

static const UINT c_DwmMaxQueuedBuffers = 8;
static const UINT c_DwmMaxMonitors = 16;
static const UINT c_DwmMaxAdapters = 16;

typedef ULONGLONG DWM_FRAME_COUNT;
typedef ULONGLONG QPC_TIME;

enum DWMWINDOWATTRIBUTE {
    DWMWA_NCRENDERING_ENABLED = 1,
    DWMWA_NCRENDERING_POLICY,
    DWMWA_TRANSITIONS_FORCEDISABLED,
    DWMWA_ALLOW_NCPAINT,
    DWMWA_CAPTION_BUTTON_BOUNDS,
    DWMWA_NONCLIENT_RTL_LAYOUT,
    DWMWA_FORCE_ICONIC_REPRESENTATION,
    DWMWA_FLIP3D_POLICY,
    DWMWA_EXTENDED_FRAME_BOUNDS,
    DWMWA_HAS_ICONIC_BITMAP,
    DWMWA_DISALLOW_PEEK,
    DWMWA_EXCLUDED_FROM_PEEK,
    DWMWA_CLOAK,
    DWMWA_CLOAKED,
    DWMWA_FREEZE_REPRESENTATION,
    DWMWA_PASSIVE_UPDATE_MODE,
    DWMWA_USE_HOSTBACKDROPBRUSH,
    DWMWA_USE_IMMERSIVE_DARK_MODE = 20,
    DWMWA_WINDOW_CORNER_PREFERENCE = 33,
    DWMWA_BORDER_COLOR,
    DWMWA_CAPTION_COLOR,
    DWMWA_TEXT_COLOR,
    DWMWA_VISIBLE_FRAME_BORDER_THICKNESS,
    DWMWA_SYSTEMBACKDROP_TYPE,
    DWMWA_LAST
};

enum DWMNCRENDERINGPOLICY {
    DWMNCRP_USEWINDOWSTYLE,
    DWMNCRP_DISABLED,
    DWMNCRP_ENABLED,
    DWMNCRP_LAST
};

enum DWM_SYSTEMBACKDROP_TYPE {
    DWMSBT_AUTO,
    DWMSBT_NONE,
    DWMSBT_MAINWINDOW,
    DWMSBT_TRANSIENTWINDOW,
    DWMSBT_TABBEDWINDOW
};

enum DWMFLIP3DWINDOWPOLICY {
    DWMFLIP3D_DEFAULT,
    DWMFLIP3D_EXCLUDEBELOW,
    DWMFLIP3D_EXCLUDEABOVE,
    DWMFLIP3D_LAST
};

typedef enum _DWM_SOURCE_FRAME_SAMPLING {
    DWM_SOURCE_FRAME_SAMPLING_POINT,
    DWM_SOURCE_FRAME_SAMPLING_COVERAGE,
    DWM_SOURCE_FRAME_SAMPLING_LAST
} DWM_SOURCE_FRAME_SAMPLING;

enum DWM_SHOWCONTACT
{
    DWMSC_NONE      = 0x00000000,
    DWMSC_DOWN      = 0x00000001,
    DWMSC_UP        = 0x00000002,
    DWMSC_DRAG      = 0x00000004,
    DWMSC_HOLD      = 0x00000008,
    DWMSC_PENBARREL = 0x00000010,
    DWMSC_ALL       = 0xFFFFFFFF
};

enum DWM_TAB_WINDOW_REQUIREMENTS
{
    DWMTWR_NONE                    = 0x0000,
    DWMTWR_IMPLEMENTED_BY_SYSTEM   = 0x0001,
    DWMTWR_WINDOW_RELATIONSHIP     = 0x0002,
    DWMTWR_WINDOW_STYLES           = 0x0004,
    DWMTWR_WINDOW_REGION           = 0x0008,
    DWMTWR_WINDOW_DWM_ATTRIBUTES   = 0x0010,
    DWMTWR_WINDOW_MARGINS          = 0x0020,
    DWMTWR_TABBING_ENABLED         = 0x0040,
    DWMTWR_USER_POLICY             = 0x0080,
    DWMTWR_GROUP_POLICY            = 0x0100,
    DWMTWR_APP_COMPAT              = 0x0200
};

enum DWMTRANSITION_OWNEDWINDOW_TARGET
{
    DWMTRANSITION_OWNEDWINDOW_NULL       = -1,
    DWMTRANSITION_OWNEDWINDOW_REPOSITION = 0
};

enum GESTURE_TYPE
{
    GT_PEN_TAP               = 0,
    GT_PEN_DOUBLETAP         = 1,
    GT_PEN_RIGHTTAP          = 2,
    GT_PEN_PRESSANDHOLD      = 3,
    GT_PEN_PRESSANDHOLDABORT = 4,
    GT_TOUCH_TAP             = 5,
    GT_TOUCH_DOUBLETAP       = 6,
    GT_TOUCH_RIGHTTAP        = 7,
    GT_TOUCH_PRESSANDHOLD    = 8,
    GT_TOUCH_PRESSANDHOLDABORT = 9,
    GT_TOUCH_PRESSANDTAP     = 10
};

typedef struct _UNSIGNED_RATIO {
    UINT32 uiNumerator;
    UINT32 uiDenominator;
} UNSIGNED_RATIO;

typedef struct _DWM_TIMING_INFO {
    UINT32 cbSize;
    UNSIGNED_RATIO rateRefresh;
    QPC_TIME qpcRefreshPeriod;
    UNSIGNED_RATIO rateCompose;
    QPC_TIME qpcVBlank;
    DWM_FRAME_COUNT cRefresh;
    UINT cDXRefresh;
    QPC_TIME qpcCompose;
    DWM_FRAME_COUNT cFrame;
    UINT cDXPresent;
    DWM_FRAME_COUNT cRefreshFrame;
    DWM_FRAME_COUNT cFrameSubmitted;
    UINT cDXPresentSubmitted;
    DWM_FRAME_COUNT cFrameConfirmed;
    UINT cDXPresentConfirmed;
    DWM_FRAME_COUNT cRefreshConfirmed;
    UINT cDXRefreshConfirmed;
    DWM_FRAME_COUNT cFramesLate;
    UINT cFramesOutstanding;
    DWM_FRAME_COUNT cFrameDisplayed;
    QPC_TIME qpcFrameDisplayed;
    DWM_FRAME_COUNT cRefreshFrameDisplayed;
    DWM_FRAME_COUNT cFrameComplete;
    QPC_TIME qpcFrameComplete;
    DWM_FRAME_COUNT cFramePending;
    QPC_TIME qpcFramePending;
    DWM_FRAME_COUNT cFramesDisplayed;
    DWM_FRAME_COUNT cFramesComplete;
    DWM_FRAME_COUNT cFramesPending;
    DWM_FRAME_COUNT cFramesAvailable;
    DWM_FRAME_COUNT cFramesDropped;
    DWM_FRAME_COUNT cFramesMissed;
    DWM_FRAME_COUNT cRefreshNextDisplayed;
    DWM_FRAME_COUNT cRefreshNextPresented;
    DWM_FRAME_COUNT cRefreshesDisplayed;
    DWM_FRAME_COUNT cRefreshesPresented;
    DWM_FRAME_COUNT cRefreshStarted;
    ULONGLONG cPixelsReceived;
    ULONGLONG cPixelsDrawn;
    DWM_FRAME_COUNT cBuffersEmpty;
} DWM_TIMING_INFO;

typedef struct _MilMatrix3x2D
{
    DOUBLE S_11;
    DOUBLE S_12;
    DOUBLE S_21;
    DOUBLE S_22;
    DOUBLE DX;
    DOUBLE DY;
} MilMatrix3x2D;

#define DWM_FRAME_DURATION_DEFAULT    -1

#define DWM_EC_DISABLECOMPOSITION     0
#define DWM_EC_ENABLECOMPOSITION      1

#define DWM_BB_ENABLE                 0x00000001
#define DWM_BB_BLURREGION             0x00000002
#define DWM_BB_TRANSITIONONMAXIMIZED  0x00000004

typedef struct _DWM_BLURBEHIND
{
    DWORD dwFlags;
    BOOL fEnable;
    HRGN hRgnBlur;
    BOOL fTransitionOnMaximized;
} DWM_BLURBEHIND, *PDWM_BLURBEHIND;

#define DWM_SIT_DISPLAYFRAME          0x00000001

#define DWM_CLOAKED_APP               0x00000001
#define DWM_CLOAKED_SHELL             0x00000002
#define DWM_CLOAKED_INHERITED         0x00000004

#define DWM_TNP_RECTDESTINATION       0x00000001
#define DWM_TNP_RECTSOURCE            0x00000002
#define DWM_TNP_OPACITY               0x00000004
#define DWM_TNP_VISIBLE               0x00000008
#define DWM_TNP_SOURCECLIENTAREAONLY  0x00000010

typedef struct _DWM_THUMBNAIL_PROPERTIES
{
    DWORD dwFlags;
    RECT  rcDestination;
    RECT  rcSource;
    BYTE  opacity;
    BOOL  fVisible;
    BOOL  fSourceClientAreaOnly;
} DWM_THUMBNAIL_PROPERTIES, *PDWM_THUMBNAIL_PROPERTIES;

typedef struct _DWM_PRESENT_PARAMETERS {
    UINT32 cbSize;
    BOOL fQueue;
    DWM_FRAME_COUNT cRefreshStart;
    UINT cBuffer;
    BOOL fUseSourceRate;
    UNSIGNED_RATIO rateSource;
    UINT cRefreshesPerFrame;
    DWM_SOURCE_FRAME_SAMPLING eSampling;
} DWM_PRESENT_PARAMETERS;

#pragma pack(pop)

DWMAPI DwmAttachMilContent(HWND);
DWMAPI_(BOOL) DwmDefWindowProc(HWND, UINT, WPARAM, LPARAM, LRESULT*);
DWMAPI DwmDetachMilContent(HWND);
DWMAPI DwmEnableBlurBehindWindow(HWND, const DWM_BLURBEHIND *);
DWMAPI DwmEnableComposition(UINT);
DWMAPI DwmEnableMMCSS(BOOL);
DWMAPI DwmExtendFrameIntoClientArea(HWND,const MARGINS*);
DWMAPI DwmFlush(void);
DWMAPI DwmGetColorizationColor(DWORD *, BOOL *);
DWMAPI DwmGetCompositionTimingInfo(HWND,DWM_TIMING_INFO*);
DWMAPI DwmGetGraphicsStreamClient(UINT, UUID *);
DWMAPI DwmGetGraphicsStreamTransformHint(UINT, MilMatrix3x2D *);
DWMAPI DwmGetTransportAttributes(BOOL*, BOOL*, DWORD*);
DWMAPI DwmGetUnmetTabRequirements(HWND, enum DWM_TAB_WINDOW_REQUIREMENTS *);
DWMAPI DwmGetWindowAttribute(HWND, DWORD, PVOID, DWORD);
DWMAPI DwmInvalidateIconicBitmaps(HWND);
DWMAPI DwmIsCompositionEnabled(BOOL*);
DWMAPI DwmRegisterThumbnail(HWND, HWND, PHTHUMBNAIL);
DWMAPI DwmQueryThumbnailSourceSize(HTHUMBNAIL, SIZE *);
DWMAPI DwmRenderGesture(enum GESTURE_TYPE, UINT, const DWORD *, const POINT *);
DWMAPI DwmModifyPreviousDxFrameDuration(HWND, INT, BOOL);
DWMAPI DwmSetDxFrameDuration(HWND, INT);
DWMAPI DwmSetIconicLivePreviewBitmap(HWND, HBITMAP, POINT*, DWORD);
DWMAPI DwmSetIconicThumbnail(HWND, HBITMAP, DWORD);
DWMAPI DwmSetPresentParameters(HWND, DWM_PRESENT_PARAMETERS *);
DWMAPI DwmSetWindowAttribute(HWND, DWORD, LPCVOID, DWORD);
DWMAPI DwmShowContact(DWORD, enum DWM_SHOWCONTACT);
DWMAPI DwmTetherContact(DWORD, BOOL, POINT);
DWMAPI DwmTransitionOwnedWindow(HWND, enum DWMTRANSITION_OWNEDWINDOW_TARGET);
DWMAPI DwmUnregisterThumbnail(HTHUMBNAIL);
DWMAPI DwmUpdateThumbnailProperties(HTHUMBNAIL, const DWM_THUMBNAIL_PROPERTIES *);

#ifdef __cplusplus
}
#endif

#endif  /* __WINE_DWMAPI_H */
