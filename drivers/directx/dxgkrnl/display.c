/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM display device registration for win32ss integration
 * COPYRIGHT:   Copyright 2024-2025 ReactOS WDDM Team
 *
 * Overview
 * --------
 * This file bridges the gap between dxgkrnl's WDDM adapter lifecycle and
 * the win32ss display subsystem (win32k) which discovers display adapters
 * through the XPDM-era \Device\VideoN device objects and the
 * HARDWARE\DEVICEMAP\VIDEO registry key.
 *
 * After DxgkAdapterStart succeeds we:
 *   1. Create a \Device\Video0 device object owned by dxgkrnl
 *   2. Write the DEVICEMAP\VIDEO registry entries that win32ss reads
 *   3. Write InstalledDisplayDrivers=cdd for full WDDM adapters, or
 *      framebuf for display-only fallbacks
 *   4. Handle IOCTL_VIDEO_* requests from the display driver/win32ss
 *
 * GPU scanout pipeline:
 *   SET_CURRENT_MODE  -> CommitVidPn (creates GPU resource, sets scanout)
 *   MAP_VIDEO_MEMORY  -> Allocate shadow FB, start periodic present timer
 *   Timer DPC -> Work item -> DxgkDdiPresentDisplayOnly (push pixels to GPU)
 *
 * This enables the existing win32ss EngpUpdateGraphicsDeviceList path to
 * discover the WDDM adapter without any changes to win32ss itself.
 *
 * x86/amd64 notes
 * ---------------
 * All code in this file runs at PASSIVE_LEVEL (registry operations, IRP
 * handling) except the present timer DPC which runs at DISPATCH_LEVEL and
 * queues a work item for the actual present call.
 */

#include "dxgkrnl_private.h"
#include "vidpn.h"
#include "d3dkmt.h"
#include <ntddvdeo.h>
#include <ntstrsafe.h>

/*
 * Custom ReactOS video IOCTLs used by framebuf.dll / win32ss to probe
 * for extended miniport capabilities.  These are NOT standard ntddvdeo.h
 * IOCTLs; they are ReactOS-specific extensions.
 *
 * We define them locally to avoid pulling in win32ss headers from dxgkrnl.
 *
 * IOCTL_VIDEO_UEFIFB_QUERY_CAPS  (0x232440): framebuf probes for UEFI FB caps
 * IOCTL_VIDEO_VMWARE_QUERY_CAPS  (0x232400): win32ss probes for VMware SVGA
 */
#ifndef IOCTL_VIDEO_UEFIFB_QUERY_CAPS
#define IOCTL_VIDEO_UEFIFB_QUERY_CAPS \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x910, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

#ifndef IOCTL_VIDEO_VMWARE_QUERY_CAPS
#define IOCTL_VIDEO_VMWARE_QUERY_CAPS \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x900, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

#ifndef IOCTL_VIDEO_DXGK_PRESENT_DIRTY_RECT
#define IOCTL_VIDEO_DXGK_PRESENT_DIRTY_RECT \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x920, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

/* DWM signals composition begin/end so the present worker can avoid
 * copying a partially-drawn shadow framebuffer mid-BitBlt.            */
#define IOCTL_VIDEO_DXGK_COMPOSITION_BEGIN \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x921, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIDEO_DXGK_COMPOSITION_END \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x922, METHOD_BUFFERED, FILE_ANY_ACCESS)

/*
 * UEFIFB capability structure -- matches win32ss/include/uefifb/uefifb_ioctl.h.
 * We define a local copy to avoid a cross-subsystem dependency.
 */
#define DXGK_UEFIFB_CAPS_VERSION       2
#define DXGK_UEFIFB_CAP_LINEAR_ONLY    0x00000001

typedef struct _DXGK_UEFIFB_CAPS
{
    ULONG     Version;
    ULONG     Caps;
    ULONG     OutputCount;
    ULONG     PrimaryChild;
    ULONGLONG FrameBufferLength;
} DXGK_UEFIFB_CAPS, *PDXGK_UEFIFB_CAPS;

/* TAG_DXGK_DISPLAY is now in dxgkrnl_private.h */

/*
 * DriverSpecificAttributeFlags bit that tells framebuf.dll:
 * "The framebuffer returned by MAP_VIDEO_MEMORY is already a system-memory
 *  shadow managed by dxgkrnl.  Do NOT allocate a second shadow on top."
 * This ensures GDI draws directly to the buffer PresentDisplayOnly reads.
 */
#define DXGK_DISP_DRIVERSPEC_SYSMEM_FB  0x0001

/* DXGK_CB is now in dxgkrnl_private.h */

#define DXGK_PRESENT_TRACE_LOG_LIMIT  32
#define DXGK_PRESENT_TRACE_SLOW_US    20000ULL

/* ========================================================================
 * Module-local state
 * ====================================================================== */

/*
 * The \Device\VideoN device object created by DxgkDisplayRegister.
 * This is the device object that win32ss opens via IoGetDeviceObjectPointer
 * when EngpRegisterGraphicsDevice processes the DEVICEMAP\VIDEO entry.
 */
PDEVICE_OBJECT g_DisplayDeviceObject = NULL;

/*
 * Back-pointer to the adapter that owns this display device.
 * Set during DxgkDisplayRegister, cleared during DxgkDisplayUnregister.
 */
static PDXGKRNL_ADAPTER g_DisplayAdapter = NULL;

static
PCWSTR
DxgkpSelectDisplayDriver(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    /*
     * cdd.dll (ReactOS Canonical Display Driver, win32ss/drivers/displays/cdd)
     * is the WDDM GDI display driver: it stands up the primary surface and
     * presents through the WDDM path (currently dxgkrnl's IOCTL_VIDEO_* shadow
     * framebuffer + present timer -> firmware GOP), delegating all drawing to
     * the GDI engine, and exposes the DWM composition escape contract. Full
     * WDDM adapters bind to cdd; if cdd ever fails to load, win32k falls back
     * along the InstalledDisplayDrivers list (framebuf remains on the media as
     * the display-only fallback).
     */
    if (Adapter != NULL &&
        Adapter->MiniportContext != NULL &&
        !Adapter->MiniportContext->IsDisplayOnlyDriver)
    {
        return L"cdd";
    }

    return L"framebuf";
}

/*
 * The device number we actually obtained (e.g. 0, 1, 2...).
 * Needed by DxgkDisplayUnregister to delete the correct symbolic link
 * and needed by the registry path writes.
 */
static ULONG g_DisplayDeviceNumber = 0;
static volatile LONG g_PresentShadowTraceCount = 0;
static volatile LONG g_PresentTimerTraceCount = 0;
static volatile LONG g_PresentDirtyTraceCount = 0;
static volatile LONG g_PresentDispatchBusy = 0;
static BOOLEAN g_PresentDirtyRectValid = FALSE;
static RECTL g_PresentDirtyRect;
static volatile LONGLONG g_LastDirtyNotify100ns = 0;
static volatile LONGLONG g_LastPresentSubmit100ns = 0;

/*
 * Timestamp (100ns) of the last IOCTL_VIDEO_DXGK_COMPOSITION_BEGIN.  The present
 * work item skips presenting while a DWM composition BitBlt is in progress, but
 * if COMPOSITION_END is never delivered (e.g. the compositing thread dies
 * mid-frame — seen as msgqueue "Receiving Thread woken up dead"), the
 * DwmCompositionInProgress flag would stick and freeze the display forever.
 * We treat a composition older than this threshold as abandoned and recover.
 */
static volatile LONGLONG g_DwmCompositionBegin100ns = 0;
#define DXGK_DWM_COMPOSITION_STALE_100NS (100ULL * 10000ULL) /* 100 ms */

typedef struct _DXGK_PRESENT_LOCK_STATE
{
    KIRQL OldIrql;
    BOOLEAN AtDpcLevel;
} DXGK_PRESENT_LOCK_STATE, *PDXGK_PRESENT_LOCK_STATE;

FORCEINLINE ULONGLONG
DxgkpDisplayTraceNow100ns(VOID)
{
    return KeQueryInterruptTime();
}

FORCEINLINE ULONGLONG
DxgkpDisplayTraceElapsedUs(
    _In_ ULONGLONG Start100ns)
{
    ULONGLONG End100ns = KeQueryInterruptTime();

    if (End100ns <= Start100ns)
        return 0;

    return (End100ns - Start100ns) / 10ULL;
}

FORCEINLINE VOID
DxgkpAcquirePresentLock(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Out_ PDXGK_PRESENT_LOCK_STATE LockState)
{
    ASSERT(Adapter != NULL);
    ASSERT(LockState != NULL);

    LockState->AtDpcLevel = (KeGetCurrentIrql() >= DISPATCH_LEVEL);
    if (LockState->AtDpcLevel)
        KeAcquireSpinLockAtDpcLevel(&Adapter->PresentLock);
    else
        KeAcquireSpinLock(&Adapter->PresentLock, &LockState->OldIrql);
}

FORCEINLINE VOID
DxgkpReleasePresentLock(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ const DXGK_PRESENT_LOCK_STATE *LockState)
{
    ASSERT(Adapter != NULL);
    ASSERT(LockState != NULL);

    if (LockState->AtDpcLevel)
        KeReleaseSpinLockFromDpcLevel(&Adapter->PresentLock);
    else
        KeReleaseSpinLock(&Adapter->PresentLock, LockState->OldIrql);
}

/* ========================================================================
 * Registry helper -- write a DWORD value
 * ====================================================================== */
static NTSTATUS
DxgkpRegWriteDword(
    _In_ HANDLE  KeyHandle,
    _In_ PCWSTR  ValueName,
    _In_ ULONG   Value)
{
    UNICODE_STRING Name;
    RtlInitUnicodeString(&Name, ValueName);
    return ZwSetValueKey(KeyHandle, &Name, 0, REG_DWORD,
                         &Value, sizeof(Value));
}

/* ========================================================================
 * Registry helper -- write a REG_SZ value
 * ====================================================================== */
static NTSTATUS
DxgkpRegWriteString(
    _In_ HANDLE  KeyHandle,
    _In_ PCWSTR  ValueName,
    _In_ PCWSTR  Value)
{
    UNICODE_STRING Name;
    RtlInitUnicodeString(&Name, ValueName);
    return ZwSetValueKey(KeyHandle, &Name, 0, REG_SZ,
                         (PVOID)Value,
                         (ULONG)((wcslen(Value) + 1) * sizeof(WCHAR)));
}

/* ========================================================================
 * Registry helper -- write a REG_MULTI_SZ value (single string)
 * ====================================================================== */
static NTSTATUS
DxgkpRegWriteMultiString(
    _In_ HANDLE  KeyHandle,
    _In_ PCWSTR  ValueName,
    _In_ PCWSTR  Value)
{
    UNICODE_STRING Name;
    /* REG_MULTI_SZ: string + NUL + extra NUL terminator */
    ULONG Len = (ULONG)((wcslen(Value) + 2) * sizeof(WCHAR));
    PWCHAR Buffer;

    RtlInitUnicodeString(&Name, ValueName);

    Buffer = ExAllocatePoolWithTag(PagedPool, Len, TAG_DXGK_DISPLAY);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Buffer, Len);
    wcscpy(Buffer, Value);
    /* Buffer[wcslen(Value)] is already NUL from RtlZeroMemory */
    /* Buffer[wcslen(Value)+1] is the double-NUL terminator */

    {
        NTSTATUS Status = ZwSetValueKey(KeyHandle, &Name, 0, REG_MULTI_SZ,
                                        Buffer, Len);
        ExFreePoolWithTag(Buffer, TAG_DXGK_DISPLAY);
        return Status;
    }
}

static VOID
DxgkpGetTargetModeDimensions(
    _In_  CONST D3DKMDT_VIDPN_TARGET_MODE *Mode,
    _Out_ UINT *Width,
    _Out_ UINT *Height)
{
    *Width = Mode->VideoSignalInfo.ActiveSize.cx;
    *Height = Mode->VideoSignalInfo.ActiveSize.cy;

    if ((*Width == 0 || *Height == 0) &&
        Mode->VideoSignalInfo.TotalSize.cx != 0 &&
        Mode->VideoSignalInfo.TotalSize.cy != 0)
    {
        *Width = Mode->VideoSignalInfo.TotalSize.cx;
        *Height = Mode->VideoSignalInfo.TotalSize.cy;
    }
}

/* ========================================================================
 * DxgkDisplayCommitVidPn
 *
 * Calls the full WDDM mode-set sequence on the miniport:
 *   1. IsSupportedVidPn (optional, to validate)
 *   2. EnumVidPnCofuncModality (lets miniport prune/populate modes)
 *   3. Pin source and target modes for 1024x768
 *   4. CommitVidPn (miniport creates GPU resource, sets scanout)
 *   5. SetVidPnSourceVisibility (make the display visible)
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
NTSTATUS
DxgkDisplayCommitVidPn(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    NTSTATUS Status;
    PDXGKP_VIDPN VidPn;
    SIZE_T i;
    BOOLEAN ForceDodPresentOnlyPath;
    BOOLEAN SkipDodVidPnNegotiation;

    PAGED_CODE();

    if (Adapter == NULL || Adapter->VidPn == NULL)
    {
        DXGKRNL_ERR("DxgkpCommitVidPnToMiniport: NULL adapter or VidPN\n");
        return STATUS_INVALID_PARAMETER;
    }

    VidPn = (PDXGKP_VIDPN)Adapter->VidPn;
    SkipDodVidPnNegotiation = FALSE;
    ForceDodPresentOnlyPath = (DXGK_CB(Adapter, DxgkDdiCommitVidPn) == NULL);

    if (VidPn->Signature != DXGKP_VIDPN_SIGNATURE)
    {
        DXGKRNL_ERR("DxgkpCommitVidPnToMiniport: bad VidPN signature\n");
        return STATUS_INVALID_PARAMETER;
    }

    DXGKRNL_TRACE("DxgkpCommitVidPnToMiniport: starting mode-set sequence\n");
    DXGKRNL_TRACE("DxgkpCommitVidPnToMiniport: callbacks IsSupportedVidPn=%p "
                  "EnumCofunc=%p CommitVidPn=%p SetVisibility=%p\n",
                  DXGK_CB(Adapter, DxgkDdiIsSupportedVidPn),
                  DXGK_CB(Adapter, DxgkDdiEnumVidPnCofuncModality),
                  DXGK_CB(Adapter, DxgkDdiCommitVidPn),
                  DXGK_CB(Adapter, DxgkDdiSetVidPnSourceVisibility));

    if (ForceDodPresentOnlyPath)
    {
        DXGKRNL_TRACE("DxgkpCommitVidPnToMiniport: forcing Win8 DOD present-only path\n");
    }

    /*
     * Step 1: Call IsSupportedVidPn to validate the topology.
     * Skip for Win8-era DOD drivers such as viogpudo. Their VidPN callback
     * expectations still do not match our in-kernel VidPN object model, but
     * they can proceed using the pre-seeded mode sets plus CommitVidPn /
     * PresentDisplayOnly.
     */
    if (!ForceDodPresentOnlyPath &&
        !SkipDodVidPnNegotiation &&
        DXGK_CB(Adapter, DxgkDdiCommitVidPn) != NULL &&
        DXGK_CB(Adapter, DxgkDdiIsSupportedVidPn) != NULL)
    {
        DXGKARG_ISSUPPORTEDVIDPN IsSupportedArgs;
        RtlZeroMemory(&IsSupportedArgs, sizeof(IsSupportedArgs));
        IsSupportedArgs.hDesiredVidPn = (D3DKMDT_HVIDPN)VidPn;
        IsSupportedArgs.IsVidPnSupported = FALSE;

        DXGKRNL_TRACE("DxgkpCommitVidPnToMiniport: calling DxgkDdiIsSupportedVidPn\n");
        _SEH2_TRY
        {
            Status = DXGK_CB(Adapter, DxgkDdiIsSupportedVidPn)(
                         Adapter->MiniportDeviceContext,
                         &IsSupportedArgs);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
            DXGKRNL_ERR("DxgkpCommitVidPnToMiniport: IsSupportedVidPn FAULTED 0x%08lX\n",
                        Status);
        }
        _SEH2_END;
        DXGKRNL_TRACE("DxgkpCommitVidPnToMiniport: IsSupportedVidPn returned 0x%08lX, "
                      "supported=%d\n", Status, IsSupportedArgs.IsVidPnSupported);

        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_WARN("DxgkpCommitVidPnToMiniport: IsSupportedVidPn failed 0x%08lX "
                         "(continuing anyway)\n", Status);
        }
    }

    /*
     * Step 2: Call EnumVidPnCofuncModality.
     * This lets the miniport prune the mode sets to only modes it supports,
     * and populate any empty source/target mode sets.
     */
    if (!ForceDodPresentOnlyPath &&
        !SkipDodVidPnNegotiation &&
        DXGK_CB(Adapter, DxgkDdiCommitVidPn) != NULL &&
        DXGK_CB(Adapter, DxgkDdiEnumVidPnCofuncModality) != NULL)
    {
        DXGKARG_ENUMVIDPNCOFUNCMODALITY EnumArgs;
        RtlZeroMemory(&EnumArgs, sizeof(EnumArgs));
        EnumArgs.hConstrainingVidPn = (D3DKMDT_HVIDPN)VidPn;
        EnumArgs.EnumPivotType = D3DKMDT_EPT_NOPIVOT;

        DXGKRNL_TRACE("DxgkpCommitVidPnToMiniport: calling DxgkDdiEnumVidPnCofuncModality\n");
        _SEH2_TRY
        {
            Status = DXGK_CB(Adapter, DxgkDdiEnumVidPnCofuncModality)(
                         Adapter->MiniportDeviceContext,
                         &EnumArgs);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
            DXGKRNL_ERR("DxgkpCommitVidPnToMiniport: EnumCofuncModality FAULTED 0x%08lX\n",
                        Status);
        }
        _SEH2_END;
        DXGKRNL_TRACE("DxgkpCommitVidPnToMiniport: EnumCofuncModality returned 0x%08lX\n",
                      Status);

        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_WARN("DxgkpCommitVidPnToMiniport: EnumCofuncModality failed 0x%08lX "
                         "(continuing anyway)\n", Status);
        }
    }

    /*
     * Step 3: Pin target and source modes.
     *
     * After EnumVidPnCofuncModality, the miniport may have replaced the
     * mode sets with its supported modes.  KMDOD (and many DOD drivers)
     * add a single target mode matching the POST display resolution.
     * Pin the first available target mode, then find a matching source mode.
     */
    {
        UINT TargetWidth = 0, TargetHeight = 0;
        UINT DesiredWidth = 0, DesiredHeight = 0;
        UINT SourceWidth = 0, SourceHeight = 0;
        UINT DodDesktopW = 0, DodDesktopH = 0;
        BOOLEAN PreferPostSourceMode = FALSE;
        BOOLEAN PreferClosestPostSourceMode = FALSE;

        /*
         * Prefer the POST resolution for display-only drivers so we stay in
         * sync with the miniport's boot framebuffer when cofunc negotiation
         * is skipped.
         */
        if (VidPn->TargetModeSets[0] != NULL && VidPn->TargetModeSets[0]->NumModes > 0)
        {
            PDXGKP_VIDPN_TARGET_MODESET TgtSet = VidPn->TargetModeSets[0];
            SIZE_T TargetIndex = 0;

            DXGKRNL_TRACE("DxgkpCommitVidPnToMiniport: %u target modes, "
                          "PostDisplay=%ux%u\n",
                          TgtSet->NumModes,
                          Adapter->PostDisplayWidth,
                          Adapter->PostDisplayHeight);
            for (i = 0; i < TgtSet->NumModes && i < 20; i++)
            {
                UINT w, h;
                DxgkpGetTargetModeDimensions(&TgtSet->Modes[i], &w, &h);
                DXGKRNL_TRACE("  target[%u] id=%u %ux%u\n",
                              i, TgtSet->Modes[i].Id, w, h);
            }

            if (Adapter->PostDisplayWidth > 0 && Adapter->PostDisplayHeight > 0)
            {
                /* Search for the POST/GOP resolution. */
                for (i = 0; i < TgtSet->NumModes; i++)
                {
                    UINT CandidateWidth;
                    UINT CandidateHeight;

                    DxgkpGetTargetModeDimensions(&TgtSet->Modes[i],
                                                 &CandidateWidth,
                                                 &CandidateHeight);

                    if (CandidateWidth == Adapter->PostDisplayWidth &&
                        CandidateHeight == Adapter->PostDisplayHeight)
                    {
                        TargetIndex = i;
                        break;
                    }
                }
            }

            /* If GOP resolution wasn't found, look for a PREFERRED mode. */
            if (TargetIndex == 0 && TgtSet->NumModes > 1)
            {
                for (i = 0; i < TgtSet->NumModes; i++)
                {
                    if (TgtSet->Modes[i].Preference == D3DKMDT_MP_PREFERRED)
                    {
                        TargetIndex = i;
                        break;
                    }
                }
            }

            TgtSet->PinnedModeId = TgtSet->Modes[TargetIndex].Id;
            DxgkpGetTargetModeDimensions(&TgtSet->Modes[TargetIndex],
                                         &TargetWidth,
                                         &TargetHeight);
            DXGKRNL_TRACE("DxgkpCommitVidPnToMiniport: pinned target mode id=%u "
                          "(%ux%u)\n", TgtSet->PinnedModeId,
                          TargetWidth, TargetHeight);
        }

        /*
         * Default to the pinned target resolution as the desired source mode.
         * If the target set collapses to a bogus single mode during early
         * bring-up, prefer the POST/shared-primary resolution instead when
         * the source mode set still advertises it.
         */
        DesiredWidth = TargetWidth;
        DesiredHeight = TargetHeight;

        /*
         * Display-only driver (viogpudo): the POST/GOP resolution is an
         * unreliable fallback (often 640x480) that does NOT match the real
         * desktop mode win32k/the CDD render at (the EDID/source mode, e.g.
         * 1024x768).  Committing the POST size makes dxgkrnl's primary
         * (CommittedWidth -> MAP_VIDEO_MEMORY -> SharedPrimaryWidth -> every
         * SET_SCANOUT) mismatch GDI's live output, so the scanout alternates
         * two resources and freezes on the boot frame.  Pin the LARGEST
         * advertised source mode (== the EDID preferred == win32k's desktop
         * mode) so the target/source pin, CommittedWidth, the primary surface
         * and every SET_SCANOUT agree at the real resolution.
         */
        if (Adapter->MiniportContext != NULL &&
            Adapter->MiniportContext->IsDisplayOnlyDriver &&
            VidPn->SourceModeSets[0] != NULL &&
            VidPn->SourceModeSets[0]->NumModes > 0)
        {
            PDXGKP_VIDPN_SOURCE_MODESET DodSrc = VidPn->SourceModeSets[0];
            for (i = 0; i < DodSrc->NumModes; i++)
            {
                UINT cx = (UINT)DodSrc->Modes[i].Format.Graphics.PrimSurfSize.cx;
                UINT cy = (UINT)DodSrc->Modes[i].Format.Graphics.PrimSurfSize.cy;
                if ((ULONGLONG)cx * cy > (ULONGLONG)DodDesktopW * DodDesktopH)
                {
                    DodDesktopW = cx;
                    DodDesktopH = cy;
                }
            }
        }

        if (DodDesktopW > 0 && DodDesktopH > 0)
        {
            DesiredWidth = DodDesktopW;
            DesiredHeight = DodDesktopH;

            /* Re-pin the target to the desktop mode if the set advertises it. */
            if (VidPn->TargetModeSets[0] != NULL)
            {
                PDXGKP_VIDPN_TARGET_MODESET DodTgt = VidPn->TargetModeSets[0];
                BOOLEAN FoundDodTgt = FALSE;
                SIZE_T PinIdx = 0;

                for (i = 0; i < DodTgt->NumModes; i++)
                {
                    UINT tw, th;
                    DxgkpGetTargetModeDimensions(&DodTgt->Modes[i], &tw, &th);
                    if (tw == DodDesktopW && th == DodDesktopH)
                    {
                        DodTgt->PinnedModeId = DodTgt->Modes[i].Id;
                        TargetWidth = tw;
                        TargetHeight = th;
                        FoundDodTgt = TRUE;
                        break;
                    }
                }

                /*
                 * The DOD's target modeset usually carries only the POST
                 * fallback (640x480), not the real desktop mode.  If so, the
                 * committed TARGET (640) disagrees with the SOURCE (1024),
                 * which makes viogpudo build its framebuffer at 640 then
                 * RE-CREATE at 1024 on the size change — recycling virtio
                 * resource ids (0x1<->0x2) so SET_SCANOUT desyncs from the
                 * live present target and the scanout freezes.  Force the
                 * pinned target mode's dimensions to the desktop size so
                 * TARGET==SOURCE==DodDesktop and viogpudo builds the FB ONCE.
                 */
                if (!FoundDodTgt && DodTgt->NumModes > 0)
                {
                    for (i = 0; i < DodTgt->NumModes; i++)
                    {
                        if (DodTgt->Modes[i].Id == DodTgt->PinnedModeId)
                        {
                            PinIdx = i;
                            break;
                        }
                    }
                    DodTgt->Modes[PinIdx].VideoSignalInfo.ActiveSize.cx = DodDesktopW;
                    DodTgt->Modes[PinIdx].VideoSignalInfo.ActiveSize.cy = DodDesktopH;
                    DodTgt->Modes[PinIdx].VideoSignalInfo.TotalSize.cx  = DodDesktopW;
                    DodTgt->Modes[PinIdx].VideoSignalInfo.TotalSize.cy  = DodDesktopH;
                    DodTgt->PinnedModeId = DodTgt->Modes[PinIdx].Id;
                    TargetWidth = DodDesktopW;
                    TargetHeight = DodDesktopH;
                    DXGKRNL_WARN("DxgkpCommitVidPnToMiniport: DOD — forced target "
                                 "mode to %ux%u (was POST fallback)\n",
                                 DodDesktopW, DodDesktopH);
                }
            }

            DXGKRNL_WARN("DxgkpCommitVidPnToMiniport: DOD — committing real desktop "
                         "mode %ux%u instead of POST %ux%u\n",
                         DodDesktopW, DodDesktopH,
                         Adapter->PostDisplayWidth, Adapter->PostDisplayHeight);
        }

        if (DodDesktopW == 0 &&
            Adapter->PostDisplayWidth > 0 &&
            Adapter->PostDisplayHeight > 0 &&
            VidPn->SourceModeSets[0] != NULL)
        {
            PDXGKP_VIDPN_SOURCE_MODESET SrcSet = VidPn->SourceModeSets[0];

            for (i = 0; i < SrcSet->NumModes; i++)
            {
                if ((UINT)SrcSet->Modes[i].Format.Graphics.PrimSurfSize.cx ==
                        Adapter->PostDisplayWidth &&
                    (UINT)SrcSet->Modes[i].Format.Graphics.PrimSurfSize.cy ==
                        Adapter->PostDisplayHeight)
                {
                    PreferPostSourceMode = TRUE;
                    DesiredWidth = Adapter->PostDisplayWidth;
                    DesiredHeight = Adapter->PostDisplayHeight;
                    break;
                }
            }

            if (PreferPostSourceMode &&
                (TargetWidth != DesiredWidth || TargetHeight != DesiredHeight))
            {
                DXGKRNL_WARN("DxgkpCommitVidPnToMiniport: target set pinned %ux%u "
                             "but source set supports POST mode %ux%u; preferring "
                             "POST mode for source commit\n",
                             TargetWidth,
                             TargetHeight,
                             DesiredWidth,
                             DesiredHeight);
            }

            if (!PreferPostSourceMode &&
                (TargetWidth != Adapter->PostDisplayWidth ||
                 TargetHeight != Adapter->PostDisplayHeight))
            {
                PreferClosestPostSourceMode = TRUE;
            }
        }

        /* Pin a source mode matching the desired display resolution. */
        if (VidPn->SourceModeSets[0] != NULL)
        {
            PDXGKP_VIDPN_SOURCE_MODESET SrcSet = VidPn->SourceModeSets[0];
            BOOLEAN FoundSource = FALSE;
            SIZE_T BestSourceIndex = (SIZE_T)-1;
            ULONGLONG BestSourceScore = ~0ULL;
            for (i = 0; i < SrcSet->NumModes; i++)
            {
                if (!PreferClosestPostSourceMode &&
                    (UINT)SrcSet->Modes[i].Format.Graphics.PrimSurfSize.cx == DesiredWidth &&
                    (UINT)SrcSet->Modes[i].Format.Graphics.PrimSurfSize.cy == DesiredHeight)
                {
                    SrcSet->PinnedModeId = SrcSet->Modes[i].Id;
                    SourceWidth = SrcSet->Modes[i].Format.Graphics.PrimSurfSize.cx;
                    SourceHeight = SrcSet->Modes[i].Format.Graphics.PrimSurfSize.cy;
                    FoundSource = TRUE;
                    DXGKRNL_TRACE("DxgkpCommitVidPnToMiniport: pinned source mode id=%u "
                                  "(%ux%u)\n", SrcSet->PinnedModeId,
                                  SourceWidth, SourceHeight);
                    break;
                }

                if (Adapter->PostDisplayWidth > 0 && Adapter->PostDisplayHeight > 0)
                {
                    UINT CandidateWidth =
                        (UINT)SrcSet->Modes[i].Format.Graphics.PrimSurfSize.cx;
                    UINT CandidateHeight =
                        (UINT)SrcSet->Modes[i].Format.Graphics.PrimSurfSize.cy;
                    ULONGLONG WidthDelta =
                        (CandidateWidth > Adapter->PostDisplayWidth)
                            ? (ULONGLONG)(CandidateWidth - Adapter->PostDisplayWidth)
                            : (ULONGLONG)(Adapter->PostDisplayWidth - CandidateWidth);
                    ULONGLONG HeightDelta =
                        (CandidateHeight > Adapter->PostDisplayHeight)
                            ? (ULONGLONG)(CandidateHeight - Adapter->PostDisplayHeight)
                            : (ULONGLONG)(Adapter->PostDisplayHeight - CandidateHeight);
                    ULONGLONG Score = (WidthDelta << 20) +
                                      (HeightDelta << 8) +
                                      WidthDelta + HeightDelta;

                    if (Score < BestSourceScore)
                    {
                        BestSourceScore = Score;
                        BestSourceIndex = i;
                    }
                }
            }

            if (!FoundSource &&
                BestSourceIndex != (SIZE_T)-1 &&
                Adapter->PostDisplayWidth > 0 &&
                Adapter->PostDisplayHeight > 0)
            {
                SrcSet->PinnedModeId = SrcSet->Modes[BestSourceIndex].Id;
                SourceWidth =
                    (UINT)SrcSet->Modes[BestSourceIndex].Format.Graphics.PrimSurfSize.cx;
                SourceHeight =
                    (UINT)SrcSet->Modes[BestSourceIndex].Format.Graphics.PrimSurfSize.cy;
                FoundSource = TRUE;

                DXGKRNL_WARN("DxgkpCommitVidPnToMiniport: source mode %ux%u "
                             "%s; using closest source mode %ux%u for POST size "
                             "%ux%u\n",
                             DesiredWidth,
                             DesiredHeight,
                             PreferClosestPostSourceMode
                                 ? "discarded by target selection"
                                 : "unavailable",
                             SourceWidth,
                             SourceHeight,
                             Adapter->PostDisplayWidth,
                             Adapter->PostDisplayHeight);
            }

            if (!FoundSource && SrcSet->NumModes > 0)
            {
                SrcSet->PinnedModeId = SrcSet->Modes[0].Id;
                SourceWidth  = SrcSet->Modes[0].Format.Graphics.PrimSurfSize.cx;
                SourceHeight = SrcSet->Modes[0].Format.Graphics.PrimSurfSize.cy;
                DXGKRNL_WARN("DxgkpCommitVidPnToMiniport: no matching source mode, "
                             "using first (%ux%u)\n", SourceWidth, SourceHeight);
            }

            if (TargetWidth != 0 && TargetHeight != 0 &&
                (TargetWidth != SourceWidth || TargetHeight != SourceHeight))
            {
                DXGKRNL_WARN("DxgkpCommitVidPnToMiniport: target mode %ux%u differs from "
                             "source mode %ux%u; using source mode for commit\n",
                             TargetWidth, TargetHeight, SourceWidth, SourceHeight);
            }
        }

        /* Store the committed resolution for MAP_VIDEO_MEMORY and present. */
        Adapter->CommittedWidth  = SourceWidth;
        Adapter->CommittedHeight = SourceHeight;
    }

    /*
     * Step 4: Call CommitVidPn.
     * This is the key call that makes the miniport create GPU resources
     * (RESOURCE_CREATE_2D, RESOURCE_ATTACH_BACKING, SET_SCANOUT).
     */
    if (!ForceDodPresentOnlyPath &&
        DXGK_CB(Adapter, DxgkDdiCommitVidPn) != NULL)
    {
        DXGKARG_COMMITVIDPN CommitArgs;
        RtlZeroMemory(&CommitArgs, sizeof(CommitArgs));
        CommitArgs.hFunctionalVidPn = (D3DKMDT_HVIDPN)VidPn;
        CommitArgs.AffectedVidPnSourceId = 0;
        CommitArgs.MonitorConnectivityChecks = D3DKMDT_MCC_IGNORE;
        CommitArgs.hPrimaryAllocation = NULL;
        CommitArgs.Flags.PathPowerTransition = 0;
        CommitArgs.Flags.PathPoweredOff = 0;

        DXGKRNL_TRACE("DxgkpCommitVidPnToMiniport: calling DxgkDdiCommitVidPn "
                      "(hVidPn=%p)\n", CommitArgs.hFunctionalVidPn);
        Status = DXGK_CB(Adapter, DxgkDdiCommitVidPn)(
                     Adapter->MiniportDeviceContext,
                     &CommitArgs);
        DXGKRNL_TRACE("DxgkpCommitVidPnToMiniport: CommitVidPn returned 0x%08lX\n",
                      Status);

        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkpCommitVidPnToMiniport: CommitVidPn failed 0x%08lX\n",
                        Status);
            return Status;
        }
    }
    else
    {
        /*
         * DOD drivers (viogpudo) may not implement CommitVidPn.
         * Call SystemDisplayEnable to initialize the GPU display.
         * This tells the miniport to create the 2D resource, map the
         * framebuffer, and set scanout — enabling PresentDisplayOnly.
         */
        DXGKRNL_TRACE("DxgkpCommitVidPnToMiniport: no CommitVidPn — "
                      "DOD present-only path\n");

        /*
         * Use the POST display resolution for DOD present-only drivers.
         * viogpudo's StartDevice already initialized the display from the
         * POST framebuffer via DxgkCbAcquirePostDisplayOwnership.
         */
        if (Adapter->CommittedWidth == 0 &&
            Adapter->PostDisplayWidth > 0 && Adapter->PostDisplayHeight > 0)
        {
            Adapter->CommittedWidth  = Adapter->PostDisplayWidth;
            Adapter->CommittedHeight = Adapter->PostDisplayHeight;
        }
    }

    /*
     * Step 5: Call SetVidPnSourceVisibility to make the display visible.
     * Only call when CommitVidPn succeeded — DOD drivers without CommitVidPn
     * don't have framebuffer state ready for visibility changes.
     */
    if (!ForceDodPresentOnlyPath &&
        DXGK_CB(Adapter, DxgkDdiCommitVidPn) != NULL &&
        DXGK_CB(Adapter, DxgkDdiSetVidPnSourceVisibility) != NULL)
    {
        DXGKARG_SETVIDPNSOURCEVISIBILITY VisArgs;
        RtlZeroMemory(&VisArgs, sizeof(VisArgs));
        VisArgs.VidPnSourceId = 0;
        VisArgs.Visible = TRUE;

        DXGKRNL_TRACE("DxgkpCommitVidPnToMiniport: calling SetVidPnSourceVisibility\n");
        Status = DXGK_CB(Adapter, DxgkDdiSetVidPnSourceVisibility)(
                     Adapter->MiniportDeviceContext,
                     &VisArgs);
        DXGKRNL_TRACE("DxgkpCommitVidPnToMiniport: SetVidPnSourceVisibility returned "
                      "0x%08lX\n", Status);

        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_WARN("DxgkpCommitVidPnToMiniport: SetVisibility failed 0x%08lX\n",
                         Status);
        }
    }

    Adapter->VidPnCommitted = TRUE;
    /* CommittedWidth/Height were set during mode pinning above. */

    DXGKRNL_TRACE("DxgkpCommitVidPnToMiniport: mode-set complete (%ux%u)\n",
                  Adapter->CommittedWidth, Adapter->CommittedHeight);
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkDisplayEstablishInitialMode(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Adapter->MiniportContext == NULL ||
        !Adapter->MiniportContext->IsDisplayOnlyDriver ||
        Adapter->VidPnCommitted ||
        (Adapter->PostDisplayWidth != 0 && Adapter->PostDisplayHeight != 0))
    {
        return STATUS_SUCCESS;
    }

    Status = DxgkDisplayCommitVidPn(Adapter);
    if (NT_SUCCESS(Status))
    {
        ASSERT(Adapter->VidPnCommitted);
        ASSERT(Adapter->CommittedWidth != 0);
        ASSERT(Adapter->CommittedHeight != 0);
    }

    return Status;
}

/* ========================================================================
 * DxgkpBlitShadowToGop
 *
 * Direct shadow-framebuffer -> firmware-GOP copy used when the miniport does
 * not expose DxgkDdiPresentDisplayOnly (e.g. softgpu, a WDDM 1.0 null/software
 * miniport). dxgkrnl already maps the GOP to kernel VA (PostDisplayVirtualAddress
 * via DxgkpEnsurePostDisplayResolution / DxgkCbAcquirePostDisplayOwnership), so
 * we blit the dirty region row by row, honouring the (possibly different) shadow
 * and GOP pitches and clamping to both the committed mode and the mapped GOP
 * extent so we never write past the mapping.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
static NTSTATUS
DxgkpBlitShadowToGop(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ const RECT *Rect)
{
    PUCHAR Src = (PUCHAR)Adapter->ShadowFb;
    PUCHAR Dst = (PUCHAR)Adapter->PostDisplayVirtualAddress;
    LONG   SrcPitch = (LONG)(Adapter->ShadowFbPitch ? Adapter->ShadowFbPitch
                                                    : Adapter->CommittedWidth * 4);
    LONG   DstPitch = (LONG)Adapter->PostDisplayPitch;
    SIZE_T GopSize  = Adapter->PostDisplayMappingSize;
    LONG   Left, Top, Right, Bottom, y, BytesPerRow, GopRows;

    if (Src == NULL || Dst == NULL || SrcPitch <= 0 || DstPitch <= 0 || GopSize == 0)
        return STATUS_NOT_SUPPORTED;

    /* One-time geometry diagnostic (always-on, single line) to pin the black-
     * region cause: shows whether committed mode / shadow pitch / GOP pitch /
     * GOP extent all agree. Remove once the present path is confirmed. */
    {
        static LONG s_BlitDiag = 0;
        if (InterlockedCompareExchange(&s_BlitDiag, 1, 0) == 0)
        {
            DXGKRNL_ERR("BLITDIAG: committed=%lux%lu srcPitch=%ld dstPitch=%ld "
                        "gopSize=%Iu gopRows=%ld rect=(%ld,%ld)-(%ld,%ld) "
                        "shadow=%p gop=%p\n",
                        Adapter->CommittedWidth, Adapter->CommittedHeight,
                        SrcPitch, DstPitch, GopSize,
                        (LONG)(GopSize / (SIZE_T)DstPitch),
                        Rect->left, Rect->top, Rect->right, Rect->bottom,
                        Src, Dst);
        }
    }

    Left = Rect->left; Top = Rect->top; Right = Rect->right; Bottom = Rect->bottom;
    if (Left < 0) Left = 0;
    if (Top < 0) Top = 0;
    if (Right > (LONG)Adapter->CommittedWidth)  Right  = (LONG)Adapter->CommittedWidth;
    if (Bottom > (LONG)Adapter->CommittedHeight) Bottom = (LONG)Adapter->CommittedHeight;

    /* Never write past the mapped GOP. */
    GopRows = (LONG)(GopSize / (SIZE_T)DstPitch);
    if (Bottom > GopRows) Bottom = GopRows;
    if (Left >= Right || Top >= Bottom)
        return STATUS_SUCCESS;

    BytesPerRow = (Right - Left) * 4;
    if ((Left * 4 + BytesPerRow) > DstPitch)
        BytesPerRow = DstPitch - Left * 4;
    if (BytesPerRow <= 0)
        return STATUS_SUCCESS;

    for (y = Top; y < Bottom; y++)
    {
        RtlCopyMemory(Dst + (SIZE_T)y * DstPitch + (SIZE_T)Left * 4,
                      Src + (SIZE_T)y * SrcPitch + (SIZE_T)Left * 4,
                      (SIZE_T)BytesPerRow);
    }

    InterlockedExchange64(&g_LastPresentSubmit100ns,
                          (LONGLONG)DxgkpDisplayTraceNow100ns());
    return STATUS_SUCCESS;
}

/* ========================================================================
 * DxgkpPresentShadowFb
 *
 * Calls DxgkDdiPresentDisplayOnly to push the shadow framebuffer contents
 * to the GPU.  This is how pixels actually appear on screen for DOD
 * (Display Only Driver) miniports.  When the miniport has no such DDI we
 * fall back to DxgkpBlitShadowToGop (direct copy to the firmware GOP).
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
static NTSTATUS
DxgkpPresentShadowFbInternal(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ const RECT *DirtyRectOpt,
    _In_ PCSTR TraceReason)
{
    typedef NTSTATUS (APIENTRY *PFN_PRESENT_DISPLAY_ONLY)(
        _In_ PVOID MiniportDeviceContext,
        _In_ CONST DXGKARG_PRESENT_DISPLAYONLY *PresentDisplayOnly);

    PFN_PRESENT_DISPLAY_ONLY PfnPresent;
    DXGKARG_PRESENT_DISPLAYONLY PresentArgs;
    RECT DirtyRect;
    ULONGLONG Start100ns;
    ULONGLONG ElapsedUs;
    LONG ShadowPitch;
    LONG TraceSeq;

    if (Adapter == NULL || Adapter->ShadowFb == NULL || !Adapter->VidPnCommitted)
        return STATUS_UNSUCCESSFUL;

    /*
     * DxgkDdiPresentDisplayOnly is a Win8+ (WDDM 1.2) field.
     * For DOD drivers, it's in the KMDDOD_INITIALIZATION_DATA struct.
     * For full WDDM drivers, it's after the Win7 extension fields.
     */
    if (Adapter->MiniportContext->IsDisplayOnlyDriver)
    {
        /* DOD drivers: DxgkDdiPresentDisplayOnly is in the KMDDOD struct. */
        SIZE_T Offset = FIELD_OFFSET(KMDDOD_INITIALIZATION_DATA,
                                     DxgkDdiPresentDisplayOnly);
        if (Adapter->MiniportContext->InitDataSize >= Offset + sizeof(PVOID))
        {
            PfnPresent = *(PFN_PRESENT_DISPLAY_ONLY *)
                ((PUCHAR)&Adapter->MiniportContext->InitData + Offset);
        }
        else
        {
            PfnPresent = NULL;
        }
    }
    else
    {
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
        SIZE_T Offset = FIELD_OFFSET(DRIVER_INITIALIZATION_DATA,
                                     DxgkDdiPresentDisplayOnly);

        if (Adapter->MiniportContext->InitDataSize >= Offset + sizeof(PVOID))
        {
            PfnPresent = *(PFN_PRESENT_DISPLAY_ONLY *)
                ((PUCHAR)&Adapter->MiniportContext->InitData + Offset);
        }
        else
        {
            PfnPresent = NULL;
        }
#else
        /* Win7 full WDDM miniports do not expose PresentDisplayOnly. */
        PfnPresent = NULL;
#endif
    }

    /*
     * Validate the function pointer.  DOD drivers may have a KMDDOD struct
     * smaller than our definition, causing the PresentDisplayOnly offset
     * to read stack garbage.  Compare against a known good callback
     * (DxgkDdiStartDevice) — PresentDisplayOnly must be in the same
     * driver image, so they should share the same high address bits.
     */
    /*
     * PfnPresent is NULL for a software / WDDM 1.0 miniport (softgpu) that has
     * no DxgkDdiPresentDisplayOnly. Don't bail here — fall through to the
     * direct shadow->GOP blit after the dirty rect is computed. Only validate
     * the pointer when the miniport actually exposes one.
     */
    if (PfnPresent != NULL)
    {
        PVOID KnownGoodCb = (PVOID)DXGK_CB(Adapter, DxgkDdiStartDevice);
        if (KnownGoodCb != NULL)
        {
            /* Both pointers should be in the same ~256KB image region */
            ULONG_PTR Delta = ((ULONG_PTR)PfnPresent > (ULONG_PTR)KnownGoodCb)
                ? (ULONG_PTR)PfnPresent - (ULONG_PTR)KnownGoodCb
                : (ULONG_PTR)KnownGoodCb - (ULONG_PTR)PfnPresent;
            if (Delta > 0x100000) /* > 1MB apart = different module = garbage */
            {
                static LONG s_Logged = 0;
                if (InterlockedCompareExchange(&s_Logged, 1, 0) == 0)
                {
                    DXGKRNL_WARN("DxgkpPresentShadowFb: PfnPresent=%p is garbage "
                                 "(StartDevice=%p delta=0x%IX) — using direct blit\n",
                                 PfnPresent, KnownGoodCb, Delta);
                }
                PfnPresent = NULL; /* treat garbage as "no present DDI" */
            }
        }
    }

    if (DirtyRectOpt != NULL)
    {
        DirtyRect = *DirtyRectOpt;

        if (DirtyRect.left < 0)
            DirtyRect.left = 0;
        if (DirtyRect.top < 0)
            DirtyRect.top = 0;
        if (DirtyRect.right > (LONG)Adapter->CommittedWidth)
            DirtyRect.right = (LONG)Adapter->CommittedWidth;
        if (DirtyRect.bottom > (LONG)Adapter->CommittedHeight)
            DirtyRect.bottom = (LONG)Adapter->CommittedHeight;

        if (DirtyRect.left >= DirtyRect.right ||
            DirtyRect.top >= DirtyRect.bottom)
        {
            DirtyRect.left   = 0;
            DirtyRect.top    = 0;
            DirtyRect.right  = (LONG)Adapter->CommittedWidth;
            DirtyRect.bottom = (LONG)Adapter->CommittedHeight;
        }
    }
    else
    {
        /* Periodic fallback: push the whole surface. */
        DirtyRect.left   = 0;
        DirtyRect.top    = 0;
        DirtyRect.right  = (LONG)Adapter->CommittedWidth;
        DirtyRect.bottom = (LONG)Adapter->CommittedHeight;
    }

    /*
     * No miniport present DDI (software/WDDM 1.0 miniport): copy the shadow
     * framebuffer straight to the firmware GOP ourselves.
     */
    if (PfnPresent == NULL)
        return DxgkpBlitShadowToGop(Adapter, &DirtyRect);

    ASSERT(Adapter->ShadowFbPitch != 0);
    ShadowPitch = (LONG)(Adapter->ShadowFbPitch != 0 ?
                         Adapter->ShadowFbPitch :
                         (Adapter->CommittedWidth * 4));

    RtlZeroMemory(&PresentArgs, sizeof(PresentArgs));
    PresentArgs.VidPnSourceId = 0;
    PresentArgs.pSource       = Adapter->ShadowFb;
    PresentArgs.BytesPerPixel = 4;
    PresentArgs.Pitch         = ShadowPitch;
    PresentArgs.Flags.Value   = 0;
    PresentArgs.NumMoves      = 0;
    PresentArgs.pMoves        = NULL;
    PresentArgs.NumDirtyRects = 1;
    PresentArgs.pDirtyRect    = &DirtyRect;
    PresentArgs.pfnPresentDisplayOnlyProgress = NULL;

    {
        NTSTATUS PresentStatus;
        Start100ns = DxgkpDisplayTraceNow100ns();
        PresentStatus = PfnPresent(Adapter->MiniportDeviceContext, &PresentArgs);
        ElapsedUs = DxgkpDisplayTraceElapsedUs(Start100ns);
        TraceSeq = InterlockedIncrement(&g_PresentShadowTraceCount);
        if (TraceSeq <= DXGK_PRESENT_TRACE_LOG_LIMIT ||
            ElapsedUs >= DXGK_PRESENT_TRACE_SLOW_US)
        {
            DXGKRNL_TRACE("DxgkpPresentShadowFb[%s]: seq=%ld PfnPresent=%p "
                          "status=0x%08lX dur=%I64u us rect=(%ld,%ld)-(%ld,%ld) "
                          "size=%ux%u pitch=%ld\n",
                          TraceReason,
                          TraceSeq,
                          PfnPresent,
                          PresentStatus,
                          ElapsedUs,
                          DirtyRect.left,
                          DirtyRect.top,
                          DirtyRect.right,
                          DirtyRect.bottom,
                          Adapter->CommittedWidth,
                          Adapter->CommittedHeight,
                          PresentArgs.Pitch);
        }
        InterlockedExchange64(&g_LastPresentSubmit100ns,
                              (LONGLONG)DxgkpDisplayTraceNow100ns());
        return PresentStatus;
    }
}

static NTSTATUS
DxgkpPresentShadowFb(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    return DxgkpPresentShadowFbInternal(Adapter, NULL, "timer");
}

static VOID
DxgkpRecordDirtyRect(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ const RECTL *DirtyRect)
{
    RECTL Clipped;
    DXGK_PRESENT_LOCK_STATE LockState;

    if (Adapter == NULL || DirtyRect == NULL)
        return;

    Clipped = *DirtyRect;

    if (Clipped.left < 0)
        Clipped.left = 0;
    if (Clipped.top < 0)
        Clipped.top = 0;
    if (Clipped.right > (LONG)Adapter->CommittedWidth)
        Clipped.right = (LONG)Adapter->CommittedWidth;
    if (Clipped.bottom > (LONG)Adapter->CommittedHeight)
        Clipped.bottom = (LONG)Adapter->CommittedHeight;

    if (Clipped.left >= Clipped.right || Clipped.top >= Clipped.bottom)
        return;

    DxgkpAcquirePresentLock(Adapter, &LockState);

    if (!g_PresentDirtyRectValid)
    {
        g_PresentDirtyRect = Clipped;
        g_PresentDirtyRectValid = TRUE;
    }
    else
    {
        if (Clipped.left < g_PresentDirtyRect.left)
            g_PresentDirtyRect.left = Clipped.left;
        if (Clipped.top < g_PresentDirtyRect.top)
            g_PresentDirtyRect.top = Clipped.top;
        if (Clipped.right > g_PresentDirtyRect.right)
            g_PresentDirtyRect.right = Clipped.right;
        if (Clipped.bottom > g_PresentDirtyRect.bottom)
            g_PresentDirtyRect.bottom = Clipped.bottom;
    }

    DxgkpReleasePresentLock(Adapter, &LockState);
}

static BOOLEAN
DxgkpConsumeDirtyRect(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Out_ PRECTL DirtyRect)
{
    BOOLEAN HasDirty;
    DXGK_PRESENT_LOCK_STATE LockState;

    if (Adapter == NULL || DirtyRect == NULL)
        return FALSE;

    DxgkpAcquirePresentLock(Adapter, &LockState);
    HasDirty = g_PresentDirtyRectValid;
    if (HasDirty)
    {
        *DirtyRect = g_PresentDirtyRect;
        g_PresentDirtyRectValid = FALSE;
    }
    DxgkpReleasePresentLock(Adapter, &LockState);

    return HasDirty;
}

static BOOLEAN
DxgkpHasPendingDirtyRect(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    BOOLEAN HasDirty;
    DXGK_PRESENT_LOCK_STATE LockState;

    if (Adapter == NULL)
        return FALSE;

    DxgkpAcquirePresentLock(Adapter, &LockState);
    HasDirty = g_PresentDirtyRectValid;
    DxgkpReleasePresentLock(Adapter, &LockState);

    return HasDirty;
}

/* Pre-allocated work item for the present timer DPC.
 * Using a pre-allocated work item avoids pool allocation at DISPATCH_LEVEL
 * and eliminates the work item leak that was exhausting nonpaged pool. */
static PIO_WORKITEM g_PresentWorkItem = NULL;

static VOID
NTAPI
DxgkpPresentWorkItemRoutineEx(
    _In_     PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID          Context);

static BOOLEAN
DxgkpQueuePresentWorkItem(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL || Adapter->FunctionalDeviceObject == NULL)
        return FALSE;

    if (g_PresentWorkItem == NULL)
        g_PresentWorkItem = IoAllocateWorkItem(Adapter->FunctionalDeviceObject);

    if (g_PresentWorkItem == NULL)
        return FALSE;

    IoQueueWorkItem(g_PresentWorkItem, DxgkpPresentWorkItemRoutineEx,
                    DelayedWorkQueue, Adapter);
    return TRUE;
}

/* ========================================================================
 * DxgkpPresentTimerDpc
 *
 * DPC callback for the periodic present timer.  Queues a work item to
 * call DxgkDdiPresentDisplayOnly at PASSIVE_LEVEL.
 *
 * IRQL: DISPATCH_LEVEL
 * ====================================================================== */
static VOID
NTAPI
DxgkpPresentWorkItemRoutineEx(
    _In_     PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID          Context)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)Context;
    RECTL DirtyRect;
    BOOLEAN HasDirty = FALSE;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (Adapter != NULL && Adapter->ShadowFb != NULL && Adapter->VidPnCommitted)
    {
        /*
         * If DWM is mid-composition BitBlt, skip this present to avoid
         * copying a partially-drawn frame.  The dirty rect stays pending
         * and will be picked up on the next timer tick or re-check below.
         */
        if (InterlockedCompareExchange(&Adapter->DwmCompositionInProgress, 0, 0) != 0)
        {
            /*
             * A composition BitBlt is in progress — normally skip so we don't
             * present a partially-drawn frame.  But if COMPOSITION_END was never
             * delivered (compositing thread died mid-frame), the flag sticks and
             * every present is skipped, freezing the display.  Recover: if the
             * BEGIN is older than the stale threshold, treat it as abandoned,
             * clear the flag and present anyway.
             */
            ULONGLONG NowC   = (ULONGLONG)DxgkpDisplayTraceNow100ns();
            ULONGLONG BeginC = (ULONGLONG)InterlockedCompareExchange64(
                                   &g_DwmCompositionBegin100ns, 0, 0);
            if (BeginC == 0 || NowC <= BeginC ||
                (NowC - BeginC) < DXGK_DWM_COMPOSITION_STALE_100NS)
            {
                InterlockedExchange(&g_PresentDispatchBusy, 0);
                return;
            }
            InterlockedExchange(&Adapter->DwmCompositionInProgress, 0);
            {
                static volatile LONG s_StaleLogged = 0;
                if (InterlockedIncrement(&s_StaleLogged) <= 5)
                    DXGKRNL_WARN("DxgkpPresentWorkItem: stale DWM composition "
                                 "(no COMPOSITION_END for >%lu ms) — clearing and "
                                 "presenting\n",
                                 (ULONG)(DXGK_DWM_COMPOSITION_STALE_100NS / 10000ULL));
            }
        }

        HasDirty = DxgkpConsumeDirtyRect(Adapter, &DirtyRect);
        if (HasDirty)
            DxgkpPresentShadowFbInternal(Adapter, (const RECT *)&DirtyRect, "dirty");
        else
            DxgkpPresentShadowFb(Adapter);
    }

    /* The periodic present timer owns dispatch pacing. */
    InterlockedExchange(&g_PresentDispatchBusy, 0);

    /*
     * Re-check: a new dirty rect may have arrived while we were busy
     * presenting the previous one.  Re-queue immediately so we don't
     * wait for the next 15 ms timer tick — this eliminates dropped
     * frames during heavy GUI activity.
     */
    if (Adapter != NULL && DxgkpHasPendingDirtyRect(Adapter))
    {
        if (InterlockedCompareExchange(&g_PresentDispatchBusy, 1, 0) == 0)
        {
            if (!DxgkpQueuePresentWorkItem(Adapter))
                InterlockedExchange(&g_PresentDispatchBusy, 0);
        }
    }
}

static VOID
NTAPI
DxgkpPresentTimerDpc(
    _In_     PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)DeferredContext;
    LONG TimerSeq;
    BOOLEAN HasDirty;
    ULONGLONG Now100ns;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (Adapter == NULL || Adapter->ShadowFb == NULL ||
        !Adapter->VidPnCommitted || Adapter->FunctionalDeviceObject == NULL)
        return;

    TimerSeq = InterlockedIncrement(&g_PresentTimerTraceCount);
    Now100ns = DxgkpDisplayTraceNow100ns();
    HasDirty = DxgkpHasPendingDirtyRect(Adapter);

    if (HasDirty)
    {
        if (InterlockedCompareExchange(&g_PresentDispatchBusy, 1, 0) == 0)
        {
            if (TimerSeq <= DXGK_PRESENT_TRACE_LOG_LIMIT)
            {
                DXGKRNL_TRACE("DxgkpPresentTimerDpc: seq=%ld queueing dirty present work item\n",
                              TimerSeq);
            }

            if (!DxgkpQueuePresentWorkItem(Adapter))
                InterlockedExchange(&g_PresentDispatchBusy, 0);
        }
        else if (TimerSeq <= DXGK_PRESENT_TRACE_LOG_LIMIT)
        {
            DXGKRNL_TRACE("DxgkpPresentTimerDpc: seq=%ld skipping tick while present worker is busy\n",
                          TimerSeq);
        }
        return;
    }

    if ((ULONGLONG)InterlockedCompareExchange64(&g_LastDirtyNotify100ns, 0, 0) != 0)
    {
        ULONGLONG LastDirty100ns = (ULONGLONG)InterlockedCompareExchange64(&g_LastDirtyNotify100ns, 0, 0);

        if (Now100ns > LastDirty100ns &&
            (Now100ns - LastDirty100ns) < (50ULL * 10000ULL))
        {
            if (TimerSeq <= DXGK_PRESENT_TRACE_LOG_LIMIT)
            {
                DXGKRNL_TRACE("DxgkpPresentTimerDpc: seq=%ld skipping fallback due to recent dirty activity\n",
                              TimerSeq);
            }
            return;
        }
    }

    if ((ULONGLONG)InterlockedCompareExchange64(&g_LastPresentSubmit100ns, 0, 0) != 0)
    {
        ULONGLONG LastPresent100ns = (ULONGLONG)InterlockedCompareExchange64(&g_LastPresentSubmit100ns, 0, 0);

        if (Now100ns > LastPresent100ns &&
            (Now100ns - LastPresent100ns) < (30ULL * 10000ULL))
        {
            if (TimerSeq <= DXGK_PRESENT_TRACE_LOG_LIMIT)
            {
                DXGKRNL_TRACE("DxgkpPresentTimerDpc: seq=%ld skipping fallback due to recent present\n",
                              TimerSeq);
            }
            return;
        }
    }

    if (InterlockedCompareExchange(&g_PresentDispatchBusy, 1, 0) == 0)
    {
        if (TimerSeq <= DXGK_PRESENT_TRACE_LOG_LIMIT)
        {
            DXGKRNL_TRACE("DxgkpPresentTimerDpc: seq=%ld queueing work item "
                          "(periodic present active)\n", TimerSeq);
        }

        if (!DxgkpQueuePresentWorkItem(Adapter))
        {
            InterlockedExchange(&g_PresentDispatchBusy, 0);
        }
    }
    else if (TimerSeq <= DXGK_PRESENT_TRACE_LOG_LIMIT)
    {
        DXGKRNL_TRACE("DxgkpPresentTimerDpc: seq=%ld skipping fallback while present worker is busy\n",
                      TimerSeq);
    }
}

/* ========================================================================
 * DxgkpStartPresentTimer
 *
 * Starts a periodic timer that fires every 33ms (~30 fps) to push the
 * shadow framebuffer to the GPU via DxgkDdiPresentDisplayOnly.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
VOID
DxgkpStartPresentTimer(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LARGE_INTEGER DueTime;
    LONG PeriodMs = 15; /* one kernel tick on the current clock, dirty-present paced */

    if (Adapter->PresentTimerActive)
        return;

    KeInitializeSpinLock(&Adapter->PresentLock);
    g_PresentDirtyRectValid = FALSE;
    InterlockedExchange64(&g_LastDirtyNotify100ns, 0);
    InterlockedExchange64(&g_LastPresentSubmit100ns, 0);
    InterlockedExchange(&g_PresentDispatchBusy, 0);

    KeInitializeTimer(&Adapter->PresentTimer);
    KeInitializeDpc(&Adapter->PresentDpc, DxgkpPresentTimerDpc, Adapter);

    /* First fire after 100ms to give the mode-set time to complete. */
    DueTime.QuadPart = -100LL * 10000LL; /* 100ms in 100ns units, negative = relative */
    KeSetTimerEx(&Adapter->PresentTimer, DueTime, PeriodMs, &Adapter->PresentDpc);
    Adapter->PresentTimerActive = TRUE;

    DXGKRNL_TRACE("DxgkpStartPresentTimer: started (%ld ms present period)\n", PeriodMs);
}

/* ========================================================================
 * DxgkpStopPresentTimer
 *
 * Stops the periodic present timer.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
VOID
DxgkpStopPresentTimer(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter->PresentTimerActive)
    {
        KeCancelTimer(&Adapter->PresentTimer);
        Adapter->PresentTimerActive = FALSE;
        g_PresentDirtyRectValid = FALSE;
        InterlockedExchange64(&g_LastDirtyNotify100ns, 0);
        InterlockedExchange64(&g_LastPresentSubmit100ns, 0);
        InterlockedExchange(&g_PresentDispatchBusy, 0);
        DXGKRNL_TRACE("DxgkpStopPresentTimer: stopped\n");
    }
}

/* ========================================================================
 * DxgkpDisplayDispatch -- IRP_MJ_DEVICE_CONTROL handler for \Device\Video0
 *
 * Handles IOCTL_VIDEO_* requests from framebuf.dll and win32ss.
 * ====================================================================== */
static NTSTATUS
NTAPI
DxgkpDisplayDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS           Status = STATUS_NOT_SUPPORTED;
    ULONG              BytesReturned = 0;

    UNREFERENCED_PARAMETER(DeviceObject);

    switch (Stack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_VIDEO_INIT_WIN32K_CALLBACKS:
        {
            /*
             * win32ss sends this to register its VideoPortCallout callback
             * and retrieve the physical device object.  We fill in the PDO
             * from the adapter so win32ss can use it for PnP queries.
             */
            PVIDEO_WIN32K_CALLBACKS Callbacks =
                (PVIDEO_WIN32K_CALLBACKS)Irp->AssociatedIrp.SystemBuffer;

            DXGKRNL_TRACE("DxgkpDisplayDispatch: IOCTL_VIDEO_INIT_WIN32K_CALLBACKS\n");

            if (Callbacks != NULL &&
                Stack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(VIDEO_WIN32K_CALLBACKS) &&
                Stack->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(VIDEO_WIN32K_CALLBACKS))
            {
                /* Return the physical device object for PnP queries */
                if (g_DisplayAdapter != NULL)
                    Callbacks->pPhysDeviceObject = g_DisplayAdapter->PhysicalDeviceObject;
                else
                    Callbacks->pPhysDeviceObject = NULL;

                Callbacks->bACPI = FALSE;
                Callbacks->DualviewFlags = 0;
                BytesReturned = sizeof(VIDEO_WIN32K_CALLBACKS);
                Status = STATUS_SUCCESS;
            }
            else
            {
                Status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_VIDEO_ENUM_MONITOR_PDO:
        {
            /*
             * win32ss calls this to get a POINTER to an array of
             * VIDEO_MONITOR_DEVICE structures.  The output buffer is
             * &pMonitorDevices (a pointer-sized slot).  We allocate
             * a pool array with a single NULL-pdo terminator entry
             * and write its address into the output buffer.
             *
             * The caller (EngpUpdateMonitorDevices) will iterate until
             * it finds pdo==NULL, then free the array with ExFreePool.
             */
            DXGKRNL_TRACE("DxgkpDisplayDispatch: IOCTL_VIDEO_ENUM_MONITOR_PDO\n");

            if (Irp->AssociatedIrp.SystemBuffer != NULL &&
                Stack->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(PVOID))
            {
                PVIDEO_MONITOR_DEVICE MonArray;

                /* Allocate a single-entry array (just the NULL terminator) */
                MonArray = ExAllocatePoolZero(PagedPool,
                                             sizeof(VIDEO_MONITOR_DEVICE),
                                             TAG_DXGK_DISPLAY);
                if (MonArray != NULL)
                {
                    /* NULL pdo terminator = 0 monitors */
                    MonArray[0].pdo = NULL;
                    MonArray[0].flag = 0;
                    MonArray[0].HwID = 0;

                    /* Write the pointer to the array into the output buffer */
                    *(PVIDEO_MONITOR_DEVICE *)Irp->AssociatedIrp.SystemBuffer = MonArray;
                    BytesReturned = sizeof(PVOID);  /* sizeof the pointer written */
                    Status = STATUS_SUCCESS;
                }
                else
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                }
            }
            else
            {
                Status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_VIDEO_QUERY_NUM_AVAIL_MODES:
        {
            /*
             * framebuf.dll asks how many video modes are available.
             * Return 1 mode: our committed 1024x768x32 mode.
             */
            PVIDEO_NUM_MODES NumModes =
                (PVIDEO_NUM_MODES)Irp->AssociatedIrp.SystemBuffer;

            DXGKRNL_TRACE("DxgkpDisplayDispatch: IOCTL_VIDEO_QUERY_NUM_AVAIL_MODES\n");

            if (NumModes != NULL &&
                Stack->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(VIDEO_NUM_MODES))
            {
                NumModes->NumModes = 1;
                NumModes->ModeInformationLength = sizeof(VIDEO_MODE_INFORMATION);
                BytesReturned = sizeof(VIDEO_NUM_MODES);
                Status = STATUS_SUCCESS;
            }
            else
            {
                Status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_VIDEO_QUERY_AVAIL_MODES:
        case IOCTL_VIDEO_QUERY_CURRENT_MODE:
        {
            /*
             * Return the single 1024x768x32 mode.
             * framebuf.dll uses this to set up GDI rendering.
             */
            PVIDEO_MODE_INFORMATION ModeInfo =
                (PVIDEO_MODE_INFORMATION)Irp->AssociatedIrp.SystemBuffer;

            DXGKRNL_TRACE("DxgkpDisplayDispatch: IOCTL_VIDEO_QUERY_%s\n",
                          Stack->Parameters.DeviceIoControl.IoControlCode ==
                          IOCTL_VIDEO_QUERY_AVAIL_MODES ? "AVAIL_MODES" : "CURRENT_MODE");

            if (ModeInfo != NULL &&
                Stack->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(VIDEO_MODE_INFORMATION))
            {
                RtlZeroMemory(ModeInfo, sizeof(VIDEO_MODE_INFORMATION));

                ModeInfo->Length = sizeof(VIDEO_MODE_INFORMATION);
                ModeInfo->ModeIndex = 0;
                ModeInfo->VisScreenWidth = (g_DisplayAdapter && g_DisplayAdapter->CommittedWidth) ?
                                           g_DisplayAdapter->CommittedWidth :
                                           (g_DisplayAdapter && g_DisplayAdapter->PostDisplayWidth) ?
                                           g_DisplayAdapter->PostDisplayWidth : 1024;
                ModeInfo->VisScreenHeight = (g_DisplayAdapter && g_DisplayAdapter->CommittedHeight) ?
                                            g_DisplayAdapter->CommittedHeight :
                                            (g_DisplayAdapter && g_DisplayAdapter->PostDisplayHeight) ?
                                            g_DisplayAdapter->PostDisplayHeight : 768;
                ModeInfo->ScreenStride = ModeInfo->VisScreenWidth * 4;
                ModeInfo->NumberOfPlanes = 1;
                ModeInfo->BitsPerPlane = 32;
                ModeInfo->Frequency = 60;
                ModeInfo->XMillimeter = 320;
                ModeInfo->YMillimeter = 240;
                ModeInfo->NumberRedBits = 8;
                ModeInfo->NumberGreenBits = 8;
                ModeInfo->NumberBlueBits = 8;
                ModeInfo->RedMask = 0x00FF0000;
                ModeInfo->GreenMask = 0x0000FF00;
                ModeInfo->BlueMask = 0x000000FF;
                ModeInfo->AttributeFlags = VIDEO_MODE_COLOR | VIDEO_MODE_GRAPHICS;
                ModeInfo->VideoMemoryBitmapWidth = ModeInfo->VisScreenWidth;
                ModeInfo->VideoMemoryBitmapHeight = ModeInfo->VisScreenHeight;
                ModeInfo->DriverSpecificAttributeFlags = DXGK_DISP_DRIVERSPEC_SYSMEM_FB;

                BytesReturned = sizeof(VIDEO_MODE_INFORMATION);
                Status = STATUS_SUCCESS;
            }
            else
            {
                Status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_VIDEO_SET_CURRENT_MODE:
        {
            /*
             * framebuf.dll requests a mode change.
             * This is our trigger to call CommitVidPn on the miniport,
             * which creates the GPU scanout resource and configures the
             * display pipeline.
             */
            DXGKRNL_TRACE("DxgkpDisplayDispatch: IOCTL_VIDEO_SET_CURRENT_MODE\n");

            if (g_DisplayAdapter != NULL && !g_DisplayAdapter->VidPnCommitted)
            {
                NTSTATUS CommitStatus = DxgkDisplayCommitVidPn(g_DisplayAdapter);
                if (!NT_SUCCESS(CommitStatus))
                {
                    DXGKRNL_ERR("DxgkpDisplayDispatch: CommitVidPn failed 0x%08lX\n",
                                CommitStatus);
                    Status = CommitStatus;
                    break;
                }
            }

            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_VIDEO_MAP_VIDEO_MEMORY:
        {
            /*
             * framebuf.dll requests a mapping of the video framebuffer.
             *
             * For DOD (Display Only Driver) miniports like viogpudo, there
             * is no memory-mapped framebuffer BAR.  The GPU uses
             * command-based updates via VirtIO queues.  We allocate a
             * shadow framebuffer in system memory and return it.
             *
             * DxgkDdiPresentDisplayOnly will be called periodically to
             * copy from this shadow buffer to the GPU via virtio commands
             * (TRANSFER_TO_HOST_2D + RESOURCE_FLUSH).
             */
            PVIDEO_MEMORY VideoMemory =
                (PVIDEO_MEMORY)Irp->AssociatedIrp.SystemBuffer;
            PVIDEO_MEMORY_INFORMATION MemoryInfo =
                (PVIDEO_MEMORY_INFORMATION)Irp->AssociatedIrp.SystemBuffer;

            DXGKRNL_TRACE("DxgkpDisplayDispatch: IOCTL_VIDEO_MAP_VIDEO_MEMORY\n");

            if (VideoMemory != NULL &&
                Stack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(VIDEO_MEMORY) &&
                Stack->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(VIDEO_MEMORY_INFORMATION))
            {
                ULONG Width = (g_DisplayAdapter && g_DisplayAdapter->CommittedWidth) ?
                              g_DisplayAdapter->CommittedWidth : 1024;
                ULONG Height = (g_DisplayAdapter && g_DisplayAdapter->CommittedHeight) ?
                               g_DisplayAdapter->CommittedHeight : 768;
                ULONG FbSize;
                PVOID FbVa;

                if (g_DisplayAdapter == NULL)
                {
                    Status = STATUS_DEVICE_NOT_READY;
                    break;
                }

                if (Width == 0 ||
                    Height == 0 ||
                    Width > (MAXULONG / 4) ||
                    Height > (MAXULONG / (Width * 4)))
                {
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                FbSize = Width * Height * 4;

                /*
                 * Allocate a shadow framebuffer from NonPagedPool.
                 * This is where framebuf.dll GDI rendering goes.
                 * The periodic present timer copies from here to the GPU.
                 */
                FbVa = ExAllocatePoolZero(NonPagedPool, FbSize, TAG_DXGK_DISPLAY);
                if (FbVa == NULL)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }

                DXGKRNL_TRACE("DxgkpDisplayDispatch: shadow FB at VA=%p size=0x%lX\n",
                              FbVa, FbSize);

                /* Store the shadow FB pointer in the adapter for PresentDisplayOnly. */
                if (g_DisplayAdapter != NULL)
                {
                    /*
                     * Display-only driver with an active WDDM shared-primary:
                     * DxgkpEnsureSharedPrimary already pointed ShadowFb at the
                     * CDD/WDDM primary's CPU VA — that surface is where GDI draws
                     * the live desktop and what the present timer scans out.
                     * framebuf.dll's MAP_VIDEO_MEMORY must NOT replace it with a
                     * private NonPagedPool buffer: doing so splits the primary
                     * into two virtio-gpu resources (a frozen framebuf resource
                     * vs the live WDDM resource) that fight over the scanout, and
                     * would ExFreePoolWithTag() a mapped VA never pool-allocated.
                     * Hand framebuf the WDDM primary so there is exactly one.
                     */
                    if (g_DisplayAdapter->MiniportContext != NULL &&
                        g_DisplayAdapter->MiniportContext->IsDisplayOnlyDriver &&
                        g_DisplayAdapter->SharedPrimaryAllocationHandle != NULL &&
                        g_DisplayAdapter->ShadowFb != NULL)
                    {
                        ExFreePoolWithTag(FbVa, TAG_DXGK_DISPLAY);
                        FbVa = g_DisplayAdapter->ShadowFb;
                        FbSize = g_DisplayAdapter->ShadowFbSize;
                        if (g_DisplayAdapter->VidPnCommitted)
                            DxgkpStartPresentTimer(g_DisplayAdapter);
                    }
                    else
                    {
                        if (g_DisplayAdapter->ShadowFb != NULL)
                        {
                            DxgkpStopPresentTimer(g_DisplayAdapter);
                            ExFreePoolWithTag(g_DisplayAdapter->ShadowFb, TAG_DXGK_DISPLAY);
                        }

                        g_DisplayAdapter->ShadowFb = FbVa;
                        g_DisplayAdapter->ShadowFbPitch = Width * 4;
                        g_DisplayAdapter->ShadowFbSize = FbSize;

                        /* Start the periodic present timer if CommitVidPn succeeded. */
                        if (g_DisplayAdapter->VidPnCommitted)
                        {
                            DxgkpStartPresentTimer(g_DisplayAdapter);
                        }
                    }
                }

                MemoryInfo->VideoRamBase = FbVa;
                MemoryInfo->VideoRamLength = FbSize;
                MemoryInfo->FrameBufferBase = FbVa;
                MemoryInfo->FrameBufferLength = FbSize;

                BytesReturned = sizeof(VIDEO_MEMORY_INFORMATION);
                Status = STATUS_SUCCESS;
            }
            else
            {
                Status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_VIDEO_UNMAP_VIDEO_MEMORY:
        {
            /*
             * Unmap request.  Stop the present timer and free the shadow buffer.
             */
            DXGKRNL_TRACE("DxgkpDisplayDispatch: IOCTL_VIDEO_UNMAP_VIDEO_MEMORY\n");

            if (g_DisplayAdapter != NULL)
            {
                DxgkpStopPresentTimer(g_DisplayAdapter);
                if (g_DisplayAdapter->ShadowFb != NULL)
                {
                    ExFreePoolWithTag(g_DisplayAdapter->ShadowFb, TAG_DXGK_DISPLAY);
                    g_DisplayAdapter->ShadowFb = NULL;
                    g_DisplayAdapter->ShadowFbPitch = 0;
                    g_DisplayAdapter->ShadowFbSize = 0;
                }
            }
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_VIDEO_DXGK_PRESENT_DIRTY_RECT:
        {
            PRECTL DirtyRect = (PRECTL)Irp->AssociatedIrp.SystemBuffer;
            LONG TraceSeq;

            if (DirtyRect == NULL ||
                Stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(RECTL))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            if (g_DisplayAdapter == NULL || g_DisplayAdapter->ShadowFb == NULL)
            {
                Status = STATUS_DEVICE_NOT_READY;
                break;
            }

            TraceSeq = InterlockedIncrement(&g_PresentDirtyTraceCount);
            if (TraceSeq <= DXGK_PRESENT_TRACE_LOG_LIMIT)
            {
                DXGKRNL_TRACE("DxgkpDisplayDispatch: IOCTL_VIDEO_DXGK_PRESENT_DIRTY_RECT "
                              "seq=%ld rect=(%ld,%ld)-(%ld,%ld)\n",
                              TraceSeq,
                              DirtyRect->left,
                              DirtyRect->top,
                              DirtyRect->right,
                              DirtyRect->bottom);
            }

            DxgkpRecordDirtyRect(g_DisplayAdapter, DirtyRect);
            InterlockedExchange64(&g_LastDirtyNotify100ns, (LONGLONG)DxgkpDisplayTraceNow100ns());
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_VIDEO_DXGK_COMPOSITION_BEGIN:
        {
            if (g_DisplayAdapter != NULL)
            {
                InterlockedExchange64(&g_DwmCompositionBegin100ns,
                                      (LONGLONG)DxgkpDisplayTraceNow100ns());
                InterlockedExchange(&g_DisplayAdapter->DwmCompositionInProgress, 1);
            }
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_VIDEO_DXGK_COMPOSITION_END:
        {
            if (g_DisplayAdapter != NULL)
                InterlockedExchange(&g_DisplayAdapter->DwmCompositionInProgress, 0);
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_VIDEO_RESET_DEVICE:
        {
            DXGKRNL_TRACE("DxgkpDisplayDispatch: IOCTL_VIDEO_RESET_DEVICE\n");
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_VIDEO_QUERY_COLOR_CAPABILITIES:
        {
            DXGKRNL_TRACE("DxgkpDisplayDispatch: IOCTL_VIDEO_QUERY_COLOR_CAPABILITIES\n");
            Status = STATUS_NOT_SUPPORTED;
            break;
        }

        /*
         * Pointer/cursor IOCTLs.
         *
         * DOD (Display Only Driver) miniports do not support hardware cursors.
         * win32ss will fall back to software cursor rendering if we return
         * STATUS_SUCCESS for ENABLE_POINTER and STATUS_NOT_SUPPORTED for
         * SET_POINTER_ATTR (indicating no hardware pointer capability).
         *
         * DISABLE_POINTER and SET_POINTER_POSITION are also handled as no-ops.
         */
        case IOCTL_VIDEO_ENABLE_POINTER:
        {
            DXGKRNL_TRACE("DxgkpDisplayDispatch: IOCTL_VIDEO_ENABLE_POINTER (stub)\n");
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_VIDEO_DISABLE_POINTER:
        {
            DXGKRNL_TRACE("DxgkpDisplayDispatch: IOCTL_VIDEO_DISABLE_POINTER (stub)\n");
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_VIDEO_SET_POINTER_ATTR:
        {
            DXGKRNL_TRACE("DxgkpDisplayDispatch: IOCTL_VIDEO_SET_POINTER_ATTR (not supported)\n");
            Status = STATUS_NOT_SUPPORTED;
            break;
        }

        case IOCTL_VIDEO_SET_POINTER_POSITION:
        {
            DXGKRNL_TRACE("DxgkpDisplayDispatch: IOCTL_VIDEO_SET_POINTER_POSITION (stub)\n");
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_VIDEO_QUERY_POINTER_POSITION:
        {
            DXGKRNL_TRACE("DxgkpDisplayDispatch: IOCTL_VIDEO_QUERY_POINTER_POSITION (stub)\n");
            Status = STATUS_NOT_SUPPORTED;
            break;
        }

        case IOCTL_VIDEO_QUERY_POINTER_CAPABILITIES:
        {
            DXGKRNL_TRACE("DxgkpDisplayDispatch: IOCTL_VIDEO_QUERY_POINTER_CAPABILITIES (stub)\n");
            Status = STATUS_NOT_SUPPORTED;
            break;
        }

        /*
         * UEFI framebuffer capability query.
         *
         * framebuf.dll sends this to discover if the miniport is a UEFI
         * framebuffer driver with extended capabilities (multi-output,
         * large FB, linear-only mode, etc.).  For WDDM DOD adapters we
         * return a minimal UEFIFB_CAPS with no special capabilities.
         * This avoids the "unhandled IOCTL" warning and tells framebuf
         * to use the standard mode query path.
         */
        case IOCTL_VIDEO_UEFIFB_QUERY_CAPS:
        {
            PDXGK_UEFIFB_CAPS pCaps =
                (PDXGK_UEFIFB_CAPS)Irp->AssociatedIrp.SystemBuffer;

            DXGKRNL_TRACE("DxgkpDisplayDispatch: IOCTL_VIDEO_UEFIFB_QUERY_CAPS\n");

            if (pCaps != NULL &&
                Stack->Parameters.DeviceIoControl.OutputBufferLength >=
                    sizeof(DXGK_UEFIFB_CAPS))
            {
                RtlZeroMemory(pCaps, sizeof(DXGK_UEFIFB_CAPS));
                pCaps->Version      = DXGK_UEFIFB_CAPS_VERSION;
                pCaps->Caps         = DXGK_UEFIFB_CAP_LINEAR_ONLY;
                pCaps->OutputCount  = 1;
                pCaps->PrimaryChild = 0;
                pCaps->FrameBufferLength =
                    (g_DisplayAdapter && g_DisplayAdapter->ShadowFbSize) ?
                    g_DisplayAdapter->ShadowFbSize : (16ULL * 1024 * 1024);
                BytesReturned = sizeof(DXGK_UEFIFB_CAPS);
                Status = STATUS_SUCCESS;
            }
            else
            {
                Status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        /*
         * VMware SVGA capability query.
         *
         * win32ss probes for VMware SVGA FIFO features.  Since we are
         * a WDDM adapter (not a VMware miniport), return NOT_SUPPORTED
         * so the caller falls back to the standard GDI path.
         */
        case IOCTL_VIDEO_VMWARE_QUERY_CAPS:
        {
            DXGKRNL_TRACE("DxgkpDisplayDispatch: IOCTL_VIDEO_VMWARE_QUERY_CAPS (not supported)\n");
            Status = STATUS_NOT_SUPPORTED;
            break;
        }

        default:
        {
            /*
             * Forward D3DKMT IOCTLs (0x2300xx) to the main dxgkrnl dispatcher.
             * This lets CDD call D3DKMTGetSharedPrimaryHandle, D3DKMTLock, etc.
             * through its existing \Device\Video0 handle without needing a
             * separate handle to \Device\DxgKrnl.
             */
            ULONG IoCode = Stack->Parameters.DeviceIoControl.IoControlCode;
            if ((IoCode & 0xFFFF0000) == 0x00230000)
            {
                Status = DxgkpDispatchBufferedIoctl(Irp, Stack);
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }
            DXGKRNL_WARN("DxgkpDisplayDispatch: unhandled IOCTL 0x%08lX\n", IoCode);
            Status = STATUS_NOT_SUPPORTED;
            break;
        }
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = BytesReturned;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

/* ========================================================================
 * DxgkpDisplayCreate -- IRP_MJ_CREATE handler for \Device\Video0
 * ====================================================================== */
static NTSTATUS
NTAPI
DxgkpDisplayCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    DXGKRNL_TRACE("DxgkpDisplayCreate: opened\n");
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/* ========================================================================
 * DxgkpDisplayClose -- IRP_MJ_CLOSE handler for \Device\Video0
 * ====================================================================== */
static NTSTATUS
NTAPI
DxgkpDisplayClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    DXGKRNL_TRACE("DxgkpDisplayClose: closed\n");
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/* ========================================================================
 * DxgkpReadMaxObjectNumber
 *
 * Reads the current MaxObjectNumber from HARDWARE\DEVICEMAP\VIDEO.
 * Returns (ULONG)-1 if the key or value does not exist yet (meaning no
 * video device has been registered).
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
static ULONG
DxgkpReadMaxObjectNumber(VOID)
{
    NTSTATUS                        Status;
    OBJECT_ATTRIBUTES               ObjectAttributes;
    UNICODE_STRING                  KeyName;
    UNICODE_STRING                  ValueName;
    HANDLE                          hKey = NULL;
    ULONG                           ResultLength;
    ULONG                           MaxObj = (ULONG)-1;

    /* Buffer for KEY_VALUE_PARTIAL_INFORMATION + a DWORD */
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION  ValueInfo = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;

    RtlInitUnicodeString(&KeyName,
                         L"\\Registry\\Machine\\HARDWARE\\DEVICEMAP\\VIDEO");
    InitializeObjectAttributes(&ObjectAttributes, &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL, NULL);

    Status = ZwOpenKey(&hKey, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
        return MaxObj;

    RtlInitUnicodeString(&ValueName, L"MaxObjectNumber");
    Status = ZwQueryValueKey(hKey, &ValueName, KeyValuePartialInformation,
                             ValueInfo, sizeof(Buffer), &ResultLength);
    if (NT_SUCCESS(Status) &&
        ValueInfo->Type == REG_DWORD &&
        ValueInfo->DataLength == sizeof(ULONG))
    {
        MaxObj = *(PULONG)ValueInfo->Data;
    }

    ZwClose(hKey);
    return MaxObj;
}

/* ========================================================================
 * DxgkDisplayRegister
 *
 * Called after DxgkAdapterStart succeeds.  Creates a \Device\VideoN
 * device object (trying Video0 first, then Video1, Video2, etc. if
 * the name is already taken by videoprt / VGA) and populates the
 * registry entries so that win32ss can discover the WDDM adapter.
 *
 * When another driver (typically videoprt for the VGA/BIOS framebuffer)
 * already owns \Device\Video0, we:
 *   1. Accept the next available device number.
 *   2. Write DEVICEMAP\VIDEO\Device\VideoN pointing to our driver key.
 *   3. Update MaxObjectNumber to cover all registered devices.
 *
 * This avoids bugcheck 0xB4 (VIDEO_DRIVER_INIT_FAILURE) that previously
 * occurred when UEFIFB/VGA stole Video0 before dxgkrnl.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
NTSTATUS
DxgkDisplayRegister(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    NTSTATUS        Status;
    UNICODE_STRING  DeviceName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING  KeyName;
    HANDLE          hVideoMap = NULL;
    HANDLE          hDriverKey = NULL;
    ULONG           DeviceNumber;
    ULONG           ExistingMax;
    WCHAR           DeviceBuffer[24];     /* L"\\Device\\Video%lu" */
    WCHAR           DeviceValueName[24];  /* L"\\Device\\Video%lu" */
    WCHAR           DriverKeyBuf[128];    /* dxgkrnl\DeviceN key path */

    PAGED_CODE();

    DXGKRNL_TRACE("DxgkDisplayRegister: Adapter=%p\n", Adapter);

    /*
     * Anchor the POST display resolution to the real firmware GOP before we
     * write DefaultSettings below. A minimal miniport (softgpu) never calls
     * DxgkCbAcquirePostDisplayOwnership, leaving PostDisplayWidth/Height 0, so
     * DefaultSettings would fall back to 1024x768 while the committed VidPn and
     * the shadow framebuffer use the GOP's 800x600 — framebuf would then render
     * past the shadow FB and corrupt NonPagedPool. Keep all three consistent.
     */
    DxgkpEnsurePostDisplayResolution(Adapter);

    /* Already registered? */
    if (g_DisplayDeviceObject != NULL)
    {
        DXGKRNL_TRACE("DxgkDisplayRegister: already registered\n");
        return STATUS_SUCCESS;
    }

    /* ---- Step 1: Find a free \Device\VideoN name ---- */

    /*
     * Read the current MaxObjectNumber set by videoprt (or whoever
     * registered before us).  If it is valid, start one past that so we
     * don't collide.  If nobody has registered yet, start at 0.
     */
    ExistingMax = DxgkpReadMaxObjectNumber();

    if (ExistingMax != (ULONG)-1)
    {
        /* Other video devices exist -- start one past the last. */
        DeviceNumber = ExistingMax + 1;
        DXGKRNL_TRACE("DxgkDisplayRegister: existing MaxObjectNumber=%lu, "
                       "starting at Video%lu\n", ExistingMax, DeviceNumber);
    }
    else
    {
        /* Nobody has registered yet -- start at Video0. */
        DeviceNumber = 0;
    }

    /*
     * Try to create \Device\VideoN.  If the name is already taken
     * (STATUS_OBJECT_NAME_COLLISION), try the next number.  Limit the
     * search to avoid an infinite loop on pathological setups.
     */
    Status = STATUS_OBJECT_NAME_COLLISION;
    for (; DeviceNumber < 16 && Status == STATUS_OBJECT_NAME_COLLISION;
         DeviceNumber++)
    {
        RtlStringCchPrintfW(DeviceBuffer,
                            RTL_NUMBER_OF(DeviceBuffer),
                            L"\\Device\\Video%lu", DeviceNumber);
        RtlInitUnicodeString(&DeviceName, DeviceBuffer);

        Status = IoCreateDevice(
                     GDxgControlDeviceObject->DriverObject,
                     sizeof(PVOID),  /* DeviceExtension stores adapter pointer */
                     &DeviceName,
                     FILE_DEVICE_VIDEO,  /* Match videoprt device type */
                     FILE_DEVICE_SECURE_OPEN,
                     FALSE,
                     &g_DisplayDeviceObject);

        if (NT_SUCCESS(Status))
        {
            /* Success -- we own this device number. */
            DXGKRNL_TRACE("DxgkDisplayRegister: created \\Device\\Video%lu at %p\n",
                          DeviceNumber, g_DisplayDeviceObject);
            break;
        }

        if (Status == STATUS_OBJECT_NAME_COLLISION)
        {
            DXGKRNL_TRACE("DxgkDisplayRegister: \\Device\\Video%lu already "
                          "exists (owned by another driver), trying next\n",
                          DeviceNumber);
            continue;
        }

        /* Some other failure -- bail out. */
        DXGKRNL_ERR("DxgkDisplayRegister: IoCreateDevice(\\Device\\Video%lu) "
                     "failed 0x%08lX\n", DeviceNumber, Status);
        return Status;
    }

    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkDisplayRegister: could not find a free VideoN "
                     "device name (tried up to Video15)\n");
        return Status;
    }

    /* Store the device number we actually got. */
    g_DisplayDeviceNumber = DeviceNumber;

    /* Store adapter back-pointer in the device extension */
    *(PDXGKRNL_ADAPTER *)g_DisplayDeviceObject->DeviceExtension = Adapter;
    g_DisplayAdapter = Adapter;
    g_DisplayDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    g_DisplayDeviceObject->Flags |= DO_BUFFERED_IO;

    /* ---- Step 2: Write DEVICEMAP\VIDEO registry entries ---- */

    {
        static const WCHAR DeviceMapPath[] =
            L"\\Registry\\Machine\\HARDWARE\\DEVICEMAP\\VIDEO";

        /* Build the driver settings key path for our device number:
         * \Registry\Machine\SYSTEM\CurrentControlSet\Services\dxgkrnl\DeviceN */
        RtlStringCchPrintfW(DriverKeyBuf,
                            RTL_NUMBER_OF(DriverKeyBuf),
                            L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet"
                            L"\\Services\\dxgkrnl\\Device%lu",
                            DeviceNumber);

        /* Build the DEVICEMAP value name: "\Device\VideoN" */
        RtlStringCchPrintfW(DeviceValueName,
                            RTL_NUMBER_OF(DeviceValueName),
                            L"\\Device\\Video%lu", DeviceNumber);

        /* Create/open the DEVICEMAP\VIDEO key */
        RtlInitUnicodeString(&KeyName, DeviceMapPath);
        InitializeObjectAttributes(&ObjectAttributes, &KeyName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   NULL, NULL);

        Status = ZwCreateKey(&hVideoMap, KEY_ALL_ACCESS, &ObjectAttributes,
                             0, NULL, REG_OPTION_VOLATILE, NULL);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkDisplayRegister: ZwCreateKey(DEVICEMAP\\VIDEO) "
                         "failed 0x%08lX\n", Status);
            goto Cleanup;
        }

        /*
         * Update MaxObjectNumber.  It must cover ALL registered devices:
         * both any pre-existing videoprt devices AND our new device.
         */
        {
            ULONG NewMax = DeviceNumber;
            if (ExistingMax != (ULONG)-1 && ExistingMax > NewMax)
                NewMax = ExistingMax;
            DxgkpRegWriteDword(hVideoMap, L"MaxObjectNumber", NewMax);
            DXGKRNL_TRACE("DxgkDisplayRegister: MaxObjectNumber set to %lu\n",
                          NewMax);
        }

        /* Write \Device\VideoN = <driver key path> */
        DxgkpRegWriteString(hVideoMap, DeviceValueName, DriverKeyBuf);

        DXGKRNL_TRACE("DxgkDisplayRegister: DEVICEMAP\\VIDEO\\%ls -> %ls\n",
                      DeviceValueName, DriverKeyBuf);

        /* Create/open the driver settings key */
        RtlInitUnicodeString(&KeyName, DriverKeyBuf);
        InitializeObjectAttributes(&ObjectAttributes, &KeyName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   NULL, NULL);

        Status = ZwCreateKey(&hDriverKey, KEY_ALL_ACCESS, &ObjectAttributes,
                             0, NULL, REG_OPTION_VOLATILE, NULL);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkDisplayRegister: ZwCreateKey(%ls) "
                         "failed 0x%08lX\n", DriverKeyBuf, Status);
            goto Cleanup;
        }

        PCWSTR DisplayDriverName;

        DisplayDriverName = DxgkpSelectDisplayDriver(Adapter);

        /* Full WDDM adapters bind to cdd; DOD/base-video stays on framebuf. */
        DxgkpRegWriteMultiString(hDriverKey, L"InstalledDisplayDrivers",
                                 DisplayDriverName);

        /* Write Device Description */
        DxgkpRegWriteString(hDriverKey, L"Device Description",
                            (DisplayDriverName[0] == L'c')
                                ? L"WDDM Display Adapter (dxgkrnl/cdd)"
                                : L"WDDM Display-Only Adapter (dxgkrnl/framebuf)");

        /* Write default display settings using POST display resolution. */
        {
            ULONG DefW = Adapter->CommittedWidth ? Adapter->CommittedWidth :
                         Adapter->PostDisplayWidth ? Adapter->PostDisplayWidth : 1024;
            ULONG DefH = Adapter->CommittedHeight ? Adapter->CommittedHeight :
                         Adapter->PostDisplayHeight ? Adapter->PostDisplayHeight : 768;
            DxgkpRegWriteDword(hDriverKey, L"DefaultSettings.XResolution", DefW);
            DxgkpRegWriteDword(hDriverKey, L"DefaultSettings.YResolution", DefH);
            DxgkpRegWriteDword(hDriverKey, L"DefaultSettings.BitsPerPel", 32);
            DxgkpRegWriteDword(hDriverKey, L"DefaultSettings.VRefresh", 60);
            DxgkpRegWriteDword(hDriverKey, L"DefaultSettings.Flags", 0);
            DxgkpRegWriteDword(hDriverKey, L"DefaultSettings.XPanning", DefW);
            DxgkpRegWriteDword(hDriverKey, L"DefaultSettings.YPanning", DefH);
        }
        DxgkpRegWriteDword(hDriverKey, L"DefaultSettings.Orientation", 0);
        DxgkpRegWriteDword(hDriverKey, L"DefaultSettings.FixedOutput", 0);
        DxgkpRegWriteDword(hDriverKey, L"Attach.RelativeX", 0);
        DxgkpRegWriteDword(hDriverKey, L"Attach.RelativeY", 0);

        DXGKRNL_TRACE("DxgkDisplayRegister: driver settings key written "
                       "(Device%lu, display=%ls)\n",
                       DeviceNumber,
                       DisplayDriverName);

        /*
         * Win32k's EngpHasVgaDriver replaces the last path component of
         * the DEVICEMAP value (e.g. "dxgkrnl\Device0") with "Video" and
         * opens ...\Services\dxgkrnl\Video to read the Service value.
         * videoprt creates this key; dxgkrnl must too.
         */
        {
            HANDLE hVideoKey = NULL;
            WCHAR  VideoKeyBuf[128];

            RtlStringCchPrintfW(VideoKeyBuf,
                                RTL_NUMBER_OF(VideoKeyBuf),
                                L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet"
                                L"\\Services\\dxgkrnl\\Video");
            RtlInitUnicodeString(&KeyName, VideoKeyBuf);
            InitializeObjectAttributes(&ObjectAttributes, &KeyName,
                                       OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                       NULL, NULL);

            if (NT_SUCCESS(ZwCreateKey(&hVideoKey, KEY_ALL_ACCESS,
                                       &ObjectAttributes, 0, NULL,
                                       REG_OPTION_VOLATILE, NULL)))
            {
                DxgkpRegWriteString(hVideoKey, L"Service", L"dxgkrnl");
                ZwClose(hVideoKey);
                DXGKRNL_TRACE("DxgkDisplayRegister: created dxgkrnl\\Video key\n");
            }
        }

        /*
         * Full WDDM adapters do not rely on IOCTL_VIDEO_SET_CURRENT_MODE.
         * Commit the default VidPN immediately so cdd can open a live
         * shared primary without going through the legacy video path.
         */
        if (DisplayDriverName[0] == L'c' && !Adapter->VidPnCommitted)
        {
            Status = DxgkDisplayCommitVidPn(Adapter);
            if (!NT_SUCCESS(Status))
            {
                DXGKRNL_ERR("DxgkDisplayRegister: initial VidPN commit failed 0x%08lX\n",
                            Status);
                goto Cleanup;
            }
        }
    }

    Status = STATUS_SUCCESS;

Cleanup:
    if (hDriverKey)
        ZwClose(hDriverKey);
    if (hVideoMap)
        ZwClose(hVideoMap);

    if (!NT_SUCCESS(Status) && g_DisplayDeviceObject != NULL)
    {
        IoDeleteDevice(g_DisplayDeviceObject);
        g_DisplayDeviceObject = NULL;
        g_DisplayAdapter = NULL;
        g_DisplayDeviceNumber = 0;
    }

    return Status;
}

/* ========================================================================
 * DxgkDisplayUnregister
 *
 * Called during DxgkAdapterStop/Remove to tear down the display device.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
VOID
DxgkDisplayUnregister(VOID)
{
    PAGED_CODE();

    if (g_DisplayAdapter != NULL)
    {
        DxgkpStopPresentTimer(g_DisplayAdapter);
    }

    if (g_DisplayDeviceObject != NULL)
    {
        DXGKRNL_TRACE("DxgkDisplayUnregister: deleting \\Device\\Video%lu\n",
                      g_DisplayDeviceNumber);
        IoDeleteDevice(g_DisplayDeviceObject);
        g_DisplayDeviceObject = NULL;
        g_DisplayAdapter = NULL;
        g_DisplayDeviceNumber = 0;
    }
}

/* ========================================================================
 * DxgkpDisplayPnpDispatch -- IRP_MJ_PNP handler for \Device\Video0
 *
 * Handles PnP IRPs that win32ss sends to the display device object.
 * In particular, EngpPnPTargetRelationRequest sends
 * IRP_MN_QUERY_DEVICE_RELATIONS(TargetDeviceRelation) which must return
 * a DEVICE_RELATIONS pointing to the physical device object.
 * ====================================================================== */
static NTSTATUS
NTAPI
DxgkpDisplayPnpDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_DEVICE_RELATIONS:
        {
            if (Stack->Parameters.QueryDeviceRelations.Type == TargetDeviceRelation)
            {
                if (g_DisplayAdapter != NULL &&
                    g_DisplayAdapter->PhysicalDeviceObject != NULL)
                {
                    PDEVICE_RELATIONS Rel;
                    Rel = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(
                              PagedPool, sizeof(DEVICE_RELATIONS), TAG_DXGK_DISPLAY);
                    if (Rel != NULL)
                    {
                        Rel->Count = 1;
                        Rel->Objects[0] = g_DisplayAdapter->PhysicalDeviceObject;
                        ObReferenceObject(g_DisplayAdapter->PhysicalDeviceObject);
                        Irp->IoStatus.Information = (ULONG_PTR)Rel;
                        Status = STATUS_SUCCESS;
                    }
                    else
                    {
                        Status = STATUS_INSUFFICIENT_RESOURCES;
                    }
                }
                else
                {
                    Status = STATUS_DEVICE_NOT_CONNECTED;
                }
            }
            else
            {
                Status = Irp->IoStatus.Status;
            }

            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        default:
        {
            /* Complete all other PnP IRPs with success. */
            Status = STATUS_SUCCESS;
            Irp->IoStatus.Status = Status;
            Irp->IoStatus.Information = 0;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
    }
}

/* ========================================================================
 * DxgkDisplayDispatchPnp
 *
 * Called from DxgkDispatchPnp when the target is \Device\Video0.
 * ====================================================================== */
BOOLEAN
DxgkDisplayDispatchPnp(
    _In_  PDEVICE_OBJECT DeviceObject,
    _In_  PIRP           Irp)
{
    if (DeviceObject != g_DisplayDeviceObject)
        return FALSE;

    DxgkpDisplayPnpDispatch(DeviceObject, Irp);
    return TRUE;
}

/* ========================================================================
 * DxgkDisplayDispatchIoctl
 *
 * Called from the dxgkrnl dispatch path (DxgkDispatchDeviceControl) when
 * the target device object is \Device\Video0 rather than \Device\DxgKrnl.
 *
 * Returns TRUE if the IRP was handled (IRP completed), FALSE if not
 * (caller should use normal DxgKrnl dispatch).
 * ====================================================================== */
BOOLEAN
DxgkDisplayDispatchIoctl(
    _In_  PDEVICE_OBJECT DeviceObject,
    _In_  PIRP           Irp)
{
    if (DeviceObject == NULL || g_DisplayDeviceObject == NULL ||
        DeviceObject != g_DisplayDeviceObject)
    {
        return FALSE;
    }

    /* DxgkpDisplayDispatch completes the IRP and returns NTSTATUS */
    DxgkpDisplayDispatch(DeviceObject, Irp);
    return TRUE;
}

/* ========================================================================
 * DxgkDisplayDispatchCreate
 *
 * Called from DxgkDispatchCreate when the target is \Device\Video0.
 * ====================================================================== */
BOOLEAN
DxgkDisplayDispatchCreate(
    _In_  PDEVICE_OBJECT DeviceObject,
    _In_  PIRP           Irp)
{
    if (DeviceObject != g_DisplayDeviceObject)
        return FALSE;

    DxgkpDisplayCreate(DeviceObject, Irp);
    return TRUE;
}

/* ========================================================================
 * DxgkDisplayDispatchClose
 *
 * Called from DxgkDispatchClose when the target is \Device\Video0.
 * ====================================================================== */
BOOLEAN
DxgkDisplayDispatchClose(
    _In_  PDEVICE_OBJECT DeviceObject,
    _In_  PIRP           Irp)
{
    if (DeviceObject != g_DisplayDeviceObject)
        return FALSE;

    DxgkpDisplayClose(DeviceObject, Irp);
    return TRUE;
}
