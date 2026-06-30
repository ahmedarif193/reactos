/*
 * PROJECT:     ReactOS Desktop Window Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DWM internal structures and declarations
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Overview
 * --------
 * This header defines the internal state for the ReactOS Desktop Window
 * Manager (dwm.exe).  DWM is a user-mode service process that composites
 * all visible top-level windows into a single full-screen back buffer,
 * then presents that buffer to the display through the WDDM D3DKMT
 * interface.
 *
 * Architecture (Phase 1 -- CPU compositor)
 * ----------------------------------------
 * Phase 1 uses software (CPU) compositing.  Each top-level window is
 * assigned a "redirection surface" -- an off-screen system-memory buffer
 * that captures the window's rendered content.  DWM's composition loop
 * walks the visible window list in bottom-to-top Z-order, blitting each
 * window's redirection surface into the full-screen composition buffer.
 * After all windows are composited, DWM calls D3DKMTPresent to push the
 * composed frame to the display via dxgkrnl.
 *
 * This is architecturally equivalent to what Windows Vista DWM does,
 * except Phase 1 replaces the D3D GPU compositor with memcpy/BitBlt.
 * Phase 2 will add GPU-accelerated compositing once the D3D runtime
 * is functional.
 *
 * D3DKMT integration
 * ------------------
 * DWM opens the WDDM adapter via D3DKMTOpenAdapterFromGdiDisplayName,
 * creates a D3DKMT device, allocates the composition surface as a
 * D3DKMT allocation, and calls D3DKMTPresent to flip/blit the composed
 * frame.  This mirrors the real Windows DWM -> D3DKMT path.
 */

#ifndef _DWM_H_
#define _DWM_H_

/*
 * Include order: ntstatus.h first to get STATUS_SUCCESS etc., then
 * define WIN32_NO_STATUS to prevent winnt.h from redefining them,
 * then the standard Windows headers.
 */
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <winsvc.h>
#include <winreg.h>
#include <d3dkmthk.h>
#include <stdio.h>

/* NDK includes for NT native API types (PORT_MESSAGE, LPC, etc.) */
#define _NTSYSTEM_  /* avoid dllimport issues */
#include <ndk/iotypes.h>
#include <ndk/lpcfuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/rtlfuncs.h>

#include <wine/debug.h>

/* NtCreateWaitablePort is not in NDK lpcfuncs.h -- declare it */
NTSYSAPI NTSTATUS NTAPI NtCreateWaitablePort(
    _Out_ PHANDLE PortHandle,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ULONG MaxConnectionInfoLength,
    _In_ ULONG MaxMessageLength,
    _In_opt_ ULONG MaxPoolUsage);

/* ========================================================================
 * Constants
 * ====================================================================== */

/* Service name registered with the SCM */
#define DWM_SERVICE_NAME        L"DWM"
#define DWM_SERVICE_DISPLAY     L"Desktop Window Manager"

/* Maximum number of top-level windows we track for composition.
 * This is a soft limit for the statically-sized initial array;
 * the dynamic list grows beyond this if needed. */
#define DWM_MAX_INITIAL_WINDOWS     256

/* Composition timer interval in milliseconds.
 * ~16ms targets 60 Hz refresh; the timer auto-adjusts to VSync later. */
#define DWM_COMPOSE_INTERVAL_MS     16

/* Pool tag equivalents for HeapAlloc tracking (debug builds) */
#define DWM_TAG_SURFACE     'SfwD'
#define DWM_TAG_WINDOW      'WnwD'

/* LPC port configuration */
#define DWM_LPC_MAX_MSG_LENGTH      512
#define DWM_LPC_MAX_CONNECT_INFO    128

#ifndef DWM_E_COMPOSITIONDISABLED
#define DWM_E_COMPOSITIONDISABLED   _HRESULT_TYPEDEF_(0x80263001)
#endif

/* Window class names */
#define DWM_WINDOW_CLASS            L"DWMWindow"
#define DWM_NOTIFICATION_CLASS      L"DWM Notification Window"

/* Registry path for DWM settings */
#define DWM_REGISTRY_PATH           L"Software\\Microsoft\\Windows\\DWM"

/* Named event prefix (under \Sessions\<n>) */
#define DWM_STARTUP_EVENT_NAME      L"DwmStartupEvent"

/* LPC port name pattern */
#define DWM_PORT_NAME_FMT           L"\\BaseNamedObjects\\Dwm-%04X-ApiPort-%04X"

/* ========================================================================
 * DWM Registry configuration
 *
 * Read from HKLM\Software\Microsoft\Windows\DWM on startup.
 * ====================================================================== */
typedef struct _DWM_CONFIG
{
    BOOL        CompositionEnabled;         /* Composition (1=enabled) */
    BOOL        DisallowComposition;        /* DisallowComposition policy */
    BOOL        DisallowAnimations;         /* DisallowAnimations */
    DWORD       ColorizationColor;          /* ColorizationColor (ARGB) */
    DWORD       ColorizationColorBalance;   /* ColorizationColorBalance */
    DWORD       ColorizationAfterglow;      /* ColorizationAfterglow */
    DWORD       ColorizationAfterglowBalance;
    DWORD       ColorizationBlurBalance;    /* ColorizationBlurBalance */
    DWORD       ColorizationGlassReflectionIntensity;
    BOOL        ColorizationOpaqueBlend;    /* ColorizationOpaqueBlend */
    BOOL        DisableLockingMemory;       /* DisableLockingMemory */
    BOOL        UseDPIScaling;              /* UseDPIScaling */
} DWM_CONFIG, *PDWM_CONFIG;

/* ========================================================================
 * LPC port state
 * ====================================================================== */
typedef struct _DWM_LPC_STATE
{
    /* Server port handle (waitable) */
    HANDLE      hServerPort;

    /* Port name string */
    WCHAR       PortName[128];

    /* Server thread handle */
    HANDLE      hServerThread;

    /* TRUE if the LPC server is running */
    volatile BOOL Running;
} DWM_LPC_STATE, *PDWM_LPC_STATE;

/* ========================================================================
 * DWM Window infrastructure state
 * ====================================================================== */
typedef struct _DWM_WINDOW_STATE
{
    /* Main DWM overlay window */
    HWND        hMainWindow;

    /* Notification window */
    HWND        hNotifyWindow;

    /* Window class atoms */
    ATOM        atomMainClass;
    ATOM        atomNotifyClass;

    /* Registered custom message IDs */
    UINT        WM_DWM_REDIR_CHANGED;  /* DwmRedirectionEnvironmentChangedHint */
} DWM_WINDOW_STATE, *PDWM_WINDOW_STATE;

/* ========================================================================
 * Redirection surface
 *
 * Each tracked window gets one of these.  The pixel buffer is allocated
 * in system memory (Phase 1).  Phase 2 will replace this with a D3DKMT
 * allocation that can be shared between the window's rendering process
 * and DWM.
 * ====================================================================== */
typedef struct _DWM_SURFACE
{
    /* Dimensions in pixels */
    UINT        Width;
    UINT        Height;

    /* Stride in bytes (Width * BytesPerPixel, padded to 4-byte boundary) */
    UINT        Stride;

    /* Bytes per pixel -- always 4 for 32-bpp XRGB */
    UINT        BytesPerPixel;

    /* System-memory pixel buffer.  Format: 32-bit XRGB (B8G8R8X8).
     * This is the window's off-screen rendering target. */
    PVOID       pPixels;

    /* Size of the pPixels allocation in bytes */
    SIZE_T      AllocationSize;

    /* D3DKMT allocation handle (zero until Phase 2 GPU surfaces) */
    D3DKMT_HANDLE   hAllocation;

    /* TRUE if the surface content has been modified since last compose */
    BOOL        Dirty;
} DWM_SURFACE, *PDWM_SURFACE;

/* ========================================================================
 * Per-window tracking entry
 *
 * DWM maintains a list of all visible top-level windows.  Each entry
 * records the window handle, its position/size in screen coordinates,
 * Z-order index, and a pointer to its redirection surface.
 * ====================================================================== */
typedef struct _DWM_WINDOW_ENTRY
{
    /* Win32 window handle */
    HWND        hWnd;

    /* Screen-coordinate rectangle (updated on WM_WINDOWPOSCHANGED) */
    RECT        rcWindow;

    /* Is this window currently visible and eligible for composition? */
    BOOL        Visible;

    /* TRUE if window has an OpenGL pixel format -- use BitBlt capture */
    BOOL        IsOpenGLWindow;

    /* TRUE if the last capture sampled live screen pixels instead of the
     * window's own render path. These surfaces must be refreshed every
     * frame so occluding windows do not get cached into lower layers. */
    BOOL        CaptureFromScreen;

    /* Z-order: 0 = bottom-most.  Rebuilt each compose cycle. */
    UINT        ZOrder;

    /* Redirection surface for this window's content */
    DWM_SURFACE Surface;

    /* Linked list pointers for the global window list */
    struct _DWM_WINDOW_ENTRY *pNext;
    struct _DWM_WINDOW_ENTRY *pPrev;
} DWM_WINDOW_ENTRY, *PDWM_WINDOW_ENTRY;

/* ========================================================================
 * Composition back buffer
 *
 * The full-screen buffer that DWM composites all windows into before
 * presenting to the display.
 * ====================================================================== */
typedef struct _DWM_COMPOSITION_BUFFER
{
    /* Screen dimensions */
    UINT        Width;
    UINT        Height;
    UINT        Stride;
    UINT        BytesPerPixel;

    /* System-memory pixel buffer for the composed frame */
    PVOID       pPixels;
    SIZE_T      AllocationSize;

    /* D3DKMT allocation handle for the composition surface */
    D3DKMT_HANDLE   hAllocation;

    /* D3DKMT resource handle (wraps the allocation) */
    D3DKMT_HANDLE   hResource;
} DWM_COMPOSITION_BUFFER, *PDWM_COMPOSITION_BUFFER;

/* ========================================================================
 * Dirty region tracker
 *
 * Tracks which rectangular regions of the composition buffer need
 * recompositing.  Phase 1 uses a simple bounding rectangle; Phase 2
 * will use a region list for finer granularity.
 * ====================================================================== */
typedef struct _DWM_DIRTY_REGION
{
    /* Bounding rectangle of all dirty areas (screen coordinates) */
    RECT        rcBounds;

    /* TRUE if any region is dirty (rcBounds is valid) */
    BOOL        HasDirty;

    /* TRUE to force a full recomposite (e.g. after mode change) */
    BOOL        FullRecomposite;
} DWM_DIRTY_REGION, *PDWM_DIRTY_REGION;

/* ========================================================================
 * D3DKMT device state
 *
 * Holds all the D3DKMT handles needed for adapter access and present.
 * ====================================================================== */
typedef struct _DWM_D3DKMT_STATE
{
    /* Adapter handle from D3DKMTOpenAdapterFromGdiDisplayName */
    D3DKMT_HANDLE   hAdapter;

    /* Adapter LUID (identifies the GPU) */
    LUID            AdapterLuid;

    /* VidPN source ID for the primary display */
    D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId;

    /* D3DKMT device handle from D3DKMTCreateDevice */
    D3DKMT_HANDLE   hDevice;

    /* TRUE if the adapter/device are initialized and usable */
    BOOL            Initialized;
} DWM_D3DKMT_STATE, *PDWM_D3DKMT_STATE;

/* ========================================================================
 * Global DWM context
 *
 * Single instance -- holds all compositor state.
 * ====================================================================== */
typedef struct _DWM_CONTEXT
{
    /* Service control */
    SERVICE_STATUS          ServiceStatus;
    SERVICE_STATUS_HANDLE   hServiceStatus;
    HANDLE                  hStopEvent;

    /* D3DKMT device state */
    DWM_D3DKMT_STATE        D3dkmt;

    /* Composition buffer (the back buffer we present from) */
    DWM_COMPOSITION_BUFFER  CompBuffer;

    /* Dirty region tracker */
    DWM_DIRTY_REGION        DirtyRegion;

    /* Window list -- doubly-linked, sorted by Z-order during compose */
    PDWM_WINDOW_ENTRY       pWindowListHead;
    UINT                    WindowCount;

    /* Critical section protecting the window list and dirty regions */
    CRITICAL_SECTION        csWindowList;

    /* Composition timer handle */
    HANDLE                  hComposeTimer;

    /* Screen dimensions (cached from adapter query) */
    UINT                    ScreenWidth;
    UINT                    ScreenHeight;

    /* Statistics */
    UINT64                  FramesComposed;
    UINT64                  FramesPresented;

    /* Set TRUE when the compositor should exit its loop */
    volatile BOOL           ShutdownRequested;

    /* --- New infrastructure (Phase 2) --- */

    /* Registry-based configuration */
    DWM_CONFIG              Config;

    /* LPC server port state */
    DWM_LPC_STATE           Lpc;

    /* Window infrastructure (main window, notification window, classes) */
    DWM_WINDOW_STATE        WndState;

    /* Named events */
    HANDLE                  hStartupEvent;      /* DwmStartupEvent */
    HANDLE                  hComposedEvent;     /* DwmComposedEvent_<PID> */

    /* Single-instance mutex */
    HANDLE                  hInstanceMutex;

    /* Session ID for this DWM instance */
    DWORD                   SessionId;

    /* GPU scheduling priority elevated */
    BOOL                    GpuPrioritySet;
} DWM_CONTEXT, *PDWM_CONTEXT;

/* ========================================================================
 * Function prototypes -- dwm.c (service entry, D3DKMT setup)
 * ====================================================================== */

/*
 * DwmServiceMain
 *
 * SERVICE_TABLE entry point.  Called by the SCM when the DWM service
 * is started.  Initializes D3DKMT, creates the composition surface,
 * and enters the composition loop.
 */
VOID WINAPI
DwmServiceMain(
    _In_ DWORD dwArgc,
    _In_ LPWSTR *lpszArgv);

/*
 * DwmServiceCtrlHandler
 *
 * SERVICE_HANDLER callback.  Handles SERVICE_CONTROL_STOP and
 * SERVICE_CONTROL_INTERROGATE.
 */
VOID WINAPI
DwmServiceCtrlHandler(
    _In_ DWORD dwControl);

/*
 * DwmInitD3dkmt
 *
 * Opens the primary WDDM adapter via D3DKMTOpenAdapterFromGdiDisplayName
 * and creates a D3DKMT device.  This establishes DWM's connection to
 * dxgkrnl for surface allocation and present.
 *
 * Returns TRUE on success.
 */
BOOL
DwmInitD3dkmt(
    _Inout_ PDWM_CONTEXT pCtx);

/*
 * DwmShutdownD3dkmt
 *
 * Tears down the D3DKMT device and closes the adapter handle.
 * Called during service shutdown.
 */
VOID
DwmShutdownD3dkmt(
    _Inout_ PDWM_CONTEXT pCtx);

/* ========================================================================
 * Function prototypes -- compositor.c (composition logic)
 * ====================================================================== */

/*
 * DwmCompositorInit
 *
 * Allocates the full-screen composition buffer and initializes the
 * dirty region tracker.  Called after D3DKMT is initialized.
 *
 * Returns TRUE on success.
 */
BOOL
DwmCompositorInit(
    _Inout_ PDWM_CONTEXT pCtx);

/*
 * DwmCompositorShutdown
 *
 * Frees the composition buffer, all window redirection surfaces,
 * and the window tracking list.
 */
VOID
DwmCompositorShutdown(
    _Inout_ PDWM_CONTEXT pCtx);

/*
 * DwmCompositorRun
 *
 * Main composition loop.  Runs until ShutdownRequested is set.
 * On each tick:
 *   1. Enumerate visible top-level windows
 *   2. Update redirection surfaces for changed windows
 *   3. Composite all windows into the composition buffer
 *   4. Present the composition buffer to the display
 */
VOID
DwmCompositorRun(
    _Inout_ PDWM_CONTEXT pCtx);

/*
 * DwmComposeSingleFrame
 *
 * Composites one frame: walks the window list in Z-order (bottom to
 * top), blits each window's redirection surface into the composition
 * buffer, then calls DwmPresentFrame.
 */
VOID
DwmComposeSingleFrame(
    _Inout_ PDWM_CONTEXT pCtx);

/*
 * DwmPresentFrame
 *
 * Calls D3DKMTPresent to push the composed frame to the display.
 * Returns TRUE on success.
 */
BOOL
DwmPresentFrame(
    _Inout_ PDWM_CONTEXT pCtx);

/*
 * DwmUpdateWindowList
 *
 * Enumerates all visible top-level windows, updates the tracking
 * list (adds new windows, removes destroyed ones, updates positions).
 */
VOID
DwmUpdateWindowList(
    _Inout_ PDWM_CONTEXT pCtx);

/*
 * DwmCaptureWindowContent
 *
 * Captures the current visual content of a window into its
 * redirection surface using PrintWindow/BitBlt.
 */
BOOL
DwmCaptureWindowContent(
    _Inout_ PDWM_CONTEXT pCtx,
    _Inout_ PDWM_WINDOW_ENTRY pEntry);

/*
 * DwmSurfaceAlloc
 *
 * Allocates a system-memory redirection surface of the given size.
 * Returns TRUE on success.
 */
BOOL
DwmSurfaceAlloc(
    _Out_ PDWM_SURFACE pSurface,
    _In_ UINT Width,
    _In_ UINT Height);

/*
 * DwmSurfaceFree
 *
 * Frees a previously allocated redirection surface.
 */
VOID
DwmSurfaceFree(
    _Inout_ PDWM_SURFACE pSurface);

/*
 * DwmSurfaceResize
 *
 * Resizes a redirection surface if the window dimensions changed.
 * Frees the old buffer and allocates a new one.
 * Returns TRUE on success.
 */
BOOL
DwmSurfaceResize(
    _Inout_ PDWM_SURFACE pSurface,
    _In_ UINT NewWidth,
    _In_ UINT NewHeight);

/*
 * DwmBlitSurface
 *
 * CPU blit: copies a rectangular region from a source surface into
 * the composition buffer at the specified screen position.
 * Handles clipping to the composition buffer bounds.
 */
VOID
DwmBlitSurface(
    _In_ PDWM_COMPOSITION_BUFFER pDst,
    _In_ INT DstX,
    _In_ INT DstY,
    _In_ PDWM_SURFACE pSrc,
    _In_ PRECT pSrcRect);

/*
 * DwmMarkDirty
 *
 * Marks a screen-coordinate rectangle as dirty, expanding the
 * dirty region bounding box.
 */
VOID
DwmMarkDirty(
    _Inout_ PDWM_DIRTY_REGION pDirty,
    _In_ PRECT pRect);

/*
 * DwmClearDirty
 *
 * Resets the dirty region tracker after a compose cycle.
 */
VOID
DwmClearDirty(
    _Inout_ PDWM_DIRTY_REGION pDirty);

/* ========================================================================
 * Function prototypes -- dwm.c (infrastructure)
 * ====================================================================== */

/* Read DWM configuration from registry */
VOID DwmReadConfig(_Inout_ PDWM_CONTEXT pCtx);

/* LPC port server */
BOOL DwmCreateLpcPort(_Inout_ PDWM_CONTEXT pCtx);
VOID DwmDestroyLpcPort(_Inout_ PDWM_CONTEXT pCtx);

/* Named events */
BOOL DwmCreateNamedEvents(_Inout_ PDWM_CONTEXT pCtx);
VOID DwmDestroyNamedEvents(_Inout_ PDWM_CONTEXT pCtx);

/* Window class and main window */
BOOL DwmRegisterWindowClasses(_Inout_ PDWM_CONTEXT pCtx);
VOID DwmUnregisterWindowClasses(_Inout_ PDWM_CONTEXT pCtx);
BOOL DwmCreateMainWindow(_Inout_ PDWM_CONTEXT pCtx);
VOID DwmDestroyMainWindow(_Inout_ PDWM_CONTEXT pCtx);

/* GPU scheduling priority */
VOID DwmSetGpuPriority(_Inout_ PDWM_CONTEXT pCtx);

/* Message-based main loop (replaces simple timer loop) */
VOID DwmMessageLoop(_Inout_ PDWM_CONTEXT pCtx);

#endif /* _DWM_H_ */
