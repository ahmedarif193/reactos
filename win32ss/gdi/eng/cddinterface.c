/*
 * PROJECT:     ReactOS Win32 Subsystem
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     WDDM CDD (Canonical Display Driver) integration
 * COPYRIGHT:   Copyright 2026 ReactOS Contributors
 *
 * OVERVIEW
 * --------
 * In Windows Vista+ with WDDM, win32k communicates with the Canonical
 * Display Driver (cdd.dll) through two interfaces:
 *
 *   1. The standard DDI function table (DrvBitBlt, DrvCopyBits, etc.)
 *      populated by DrvEnableDriver -- this is the GDI -> CDD direction.
 *
 *   2. The W32kCddInterface callback table registered via
 *      EngQueryW32kCddInterface -- this is the win32k -> CDD direction
 *      (mode changes, VSync notifications, surface events, etc.).
 *
 * This file implements:
 *   EngQueryW32kCddInterface  (win32k.sys ordinal 133)
 *   EngCreateRedirectionDeviceBitmap (win32k.sys ordinal 41)
 *
 * Both are exported by win32k.sys and called by CDD during its
 * initialisation (DrvEnablePDEV / DrvEnableSurface).
 */

#include <win32k.h>

#define NDEBUG
#include <debug.h>

/*
 * Global CDD interface state.
 *
 * Only one CDD instance is active per session; the interface table and
 * context are stored in globals protected by the GDI HMGR semaphore
 * (which is held during display driver enable/disable sequences).
 */

/* Maximum number of CDD callback slots we accept */
#define CDD_MAX_INTERFACE_SLOTS   32

/* Opaque function pointer slot */
typedef NTSTATUS (APIENTRY *PFN_CDD_CALLBACK)(VOID);

static PFN_CDD_CALLBACK g_CddCallbackTable[CDD_MAX_INTERFACE_SLOTS];
static PVOID             g_CddContext = NULL;
static ULONG             g_CddInterfaceVersion = 0;
static ULONG             g_CddInterfaceCount = 0;
static BOOLEAN           g_CddInterfaceRegistered = FALSE;

/*
 * EngQueryW32kCddInterface
 *
 * Called by CDD to register its callback table with the GDI engine.
 * CDD passes:
 *   InterfaceVersion  - version of the interface structure
 *   Context           - opaque CDD_PDEV pointer passed back in callbacks
 *   pInterface        - pointer to CDD's filled-in W32KCDD_INTERFACE
 *   InterfaceCount    - number of function pointers in the table
 *
 * win32k stores a copy of the callback table and uses it to notify CDD
 * of WDDM events (VSync, mode change, surface creation, etc.).
 *
 * Returns STATUS_SUCCESS on success.
 */
NTSTATUS
APIENTRY
EngQueryW32kCddInterface(
    _In_ ULONG InterfaceVersion,
    _In_ PVOID Context,
    _In_ PVOID pInterface,
    _In_ ULONG InterfaceCount)
{
    PFN_CDD_CALLBACK *pCallbacks;
    ULONG CopyCount;

    DPRINT("EngQueryW32kCddInterface: version=%lu context=%p count=%lu\n",
           InterfaceVersion, Context, InterfaceCount);

    if (pInterface == NULL || Context == NULL)
    {
        DPRINT1("EngQueryW32kCddInterface: invalid parameter "
                "(pInterface=%p, Context=%p)\n", pInterface, Context);
        return STATUS_INVALID_PARAMETER;
    }

    if (InterfaceCount == 0)
    {
        DPRINT1("EngQueryW32kCddInterface: InterfaceCount is 0\n");
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Clamp the copy to the number of slots we support.  If CDD provides
     * more callbacks than we have room for, we store only what we can
     * handle.  This allows a newer CDD to work with an older win32k
     * (the extra callbacks simply won't be called).
     */
    CopyCount = min(InterfaceCount, CDD_MAX_INTERFACE_SLOTS);
    pCallbacks = (PFN_CDD_CALLBACK *)pInterface;

    RtlCopyMemory(g_CddCallbackTable, pCallbacks,
                   CopyCount * sizeof(PFN_CDD_CALLBACK));

    /* Zero any remaining slots so stale pointers are never called */
    if (CopyCount < CDD_MAX_INTERFACE_SLOTS)
    {
        RtlZeroMemory(&g_CddCallbackTable[CopyCount],
                       (CDD_MAX_INTERFACE_SLOTS - CopyCount) *
                       sizeof(PFN_CDD_CALLBACK));
    }

    g_CddContext = Context;
    g_CddInterfaceVersion = InterfaceVersion;
    g_CddInterfaceCount = CopyCount;
    g_CddInterfaceRegistered = TRUE;

    DPRINT("EngQueryW32kCddInterface: registered %lu callbacks "
           "(version %lu)\n", CopyCount, InterfaceVersion);

    return STATUS_SUCCESS;
}

/*
 * EngCreateRedirectionDeviceBitmap
 *
 * Creates a device-format bitmap surface for DWM redirection.
 * Under WDDM, GDI output is rendered to off-screen surfaces that DWM
 * later composites.  This function creates such a redirection bitmap.
 *
 * Parameters:
 *   dhsurf       - Driver surface handle (from the display driver)
 *   sizl         - Dimensions of the bitmap
 *   iFormatCompat - Desired bitmap format (BMF_32BPP typically)
 *
 * Returns:
 *   HBITMAP handle on success, NULL on failure.
 *
 * The returned bitmap is engine-managed (backed by system memory) and
 * associated with the current display device.  CDD uses this for its
 * shadow surface that receives GDI rendering output.
 */
HBITMAP
APIENTRY
EngCreateRedirectionDeviceBitmap(
    _In_ DHSURF dhsurf,
    _In_ SIZEL sizl,
    _In_ ULONG iFormatCompat)
{
    HBITMAP hbm;

    DPRINT("EngCreateRedirectionDeviceBitmap: dhsurf=%p size=(%ld,%ld) "
           "format=%lu\n", dhsurf, sizl.cx, sizl.cy, iFormatCompat);

    if (sizl.cx <= 0 || sizl.cy <= 0)
    {
        DPRINT1("EngCreateRedirectionDeviceBitmap: invalid size (%ld,%ld)\n",
                sizl.cx, sizl.cy);
        return NULL;
    }

    /*
     * Create an engine-managed bitmap.  This is the same as
     * EngCreateBitmap but tagged for redirection use.
     *
     * The bitmap format matches the display surface format so that
     * CDD can blit directly without conversion.
     *
     * Parameters to EngCreateBitmap:
     *   sizl         - dimensions
     *   lWidth       - 0 (let the engine compute stride)
     *   iFormat      - surface format
     *   flFlags      - BMF_TOPDOWN for standard top-down layout
     *   pvBits       - NULL (engine allocates)
     */
    hbm = EngCreateBitmap(sizl,
                          0,        /* lWidth: auto-compute */
                          iFormatCompat,
                          BMF_TOPDOWN,
                          NULL);    /* pvBits: engine allocates */

    if (hbm == NULL)
    {
        DPRINT1("EngCreateRedirectionDeviceBitmap: EngCreateBitmap "
                "failed for (%ld,%ld) format %lu\n",
                sizl.cx, sizl.cy, iFormatCompat);
        return NULL;
    }

    DPRINT("EngCreateRedirectionDeviceBitmap: created hbm=%p\n", hbm);
    return hbm;
}
