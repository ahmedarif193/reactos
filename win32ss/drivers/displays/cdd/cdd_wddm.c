/*
 * ReactOS Canonical Display Driver (CDD) - WDDM Extensions
 *
 * Copyright (C) 2026 ReactOS Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * DESCRIPTION
 * -----------
 * WDDM DDI extensions and W32kCddInterface callback table implementation.
 *
 * This file implements:
 *   - Device bitmap management (DrvCreateDeviceBitmap/DrvDeleteDeviceBitmap)
 *   - Font synthesis stubs (DrvSynthesizeFont/DrvGetSynthesizedFontFiles)
 *   - Redirection bitmap sync (DrvSynchronizeRedirectionBitmaps)
 *   - Dirty rect accumulation (DrvAccumulateD3DDirtyRect)
 *   - DX/GDI interop bracketing (DrvStartDxInterop/DrvEndDxInterop)
 *   - Display area locking (DrvLockDisplayArea/DrvUnlockDisplayArea)
 *   - Extended device bitmap (DrvCreateDeviceBitmapEx/DrvDeleteDeviceBitmapEx)
 *   - Shared surface association (DrvAssociateSharedSurface)
 *   - W32kCddInterface 17-callback table
 *   - Present worker thread (PsCreateSystemThread)
 *   - 4-level locking: device lock, shadow lock, per-bitmap, tile lock
 */

#include "cdd.h"

/*
 * Kernel function prototypes for the present worker and locking.
 * CDD is a display driver compiled with winddi.h which does not
 * include the full NT kernel type definitions (ntddk.h).
 * We use raw types (PVOID for events, LONG for enums) to avoid
 * type conflicts.
 */

/* Event types for KeInitializeEvent */
#define CDD_SynchronizationEvent 1
#define CDD_NotificationEvent    0

/* Wait reasons / processor modes */
#define CDD_Executive   0
#define CDD_KernelMode  0

#ifndef IO_NO_INCREMENT
#define IO_NO_INCREMENT 0
#endif

#ifndef THREAD_ALL_ACCESS
#define THREAD_ALL_ACCESS (0x001F03FF)
#endif

#ifndef NTSYSAPI
#define NTSYSAPI __declspec(dllimport)
#endif

NTSYSAPI VOID NTAPI
KeInitializeEvent(
    _Out_ PVOID Event,
    _In_ LONG Type,
    _In_ BOOLEAN State);

NTSYSAPI LONG NTAPI
KeSetEvent(
    _Inout_ PVOID Event,
    _In_ LONG Increment,
    _In_ BOOLEAN Wait);

NTSYSAPI VOID NTAPI
KeClearEvent(
    _Inout_ PVOID Event);

NTSYSAPI LONG NTAPI
KeReadStateEvent(
    _In_ PVOID Event);

NTSYSAPI NTSTATUS NTAPI
KeWaitForSingleObject(
    _In_ PVOID Object,
    _In_ LONG WaitReason,
    _In_ LONG WaitMode,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout);

typedef VOID (NTAPI *CDD_KSTART_ROUTINE)(PVOID StartContext);

NTSYSAPI NTSTATUS NTAPI
PsCreateSystemThread(
    _Out_ PHANDLE ThreadHandle,
    _In_ ULONG DesiredAccess,
    _In_opt_ PVOID ObjectAttributes,
    _In_opt_ HANDLE ProcessHandle,
    _Out_opt_ PVOID ClientId,
    _In_ CDD_KSTART_ROUTINE StartRoutine,
    _In_opt_ PVOID StartContext);

NTSYSAPI NTSTATUS NTAPI
PsTerminateSystemThread(
    _In_ NTSTATUS ExitStatus);

NTSYSAPI NTSTATUS NTAPI
ObReferenceObjectByHandle(
    _In_ HANDLE Handle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ PVOID ObjectType,
    _In_ LONG AccessMode,
    _Out_ PVOID *Object,
    _Out_opt_ PVOID HandleInformation);

NTSYSAPI VOID NTAPI
ObDereferenceObject(
    _In_ PVOID Object);

NTSYSAPI NTSTATUS NTAPI
ZwClose(
    _In_ HANDLE Handle);

/* KeWaitForMultipleObjects for present worker (event + timer) */
NTSYSAPI NTSTATUS NTAPI
KeWaitForMultipleObjects(
    _In_ ULONG Count,
    _In_ PVOID Object[],
    _In_ LONG WaitType,       /* 0 = WaitAll, 1 = WaitAny */
    _In_ LONG WaitReason,
    _In_ LONG WaitMode,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout,
    _In_opt_ PVOID WaitBlockArray);

/* Timer for periodic present flush */
NTSYSAPI VOID NTAPI
KeInitializeTimerEx(
    _Out_ PVOID Timer,
    _In_ LONG Type);  /* 0 = NotificationTimer, 1 = SynchronizationTimer */

NTSYSAPI BOOLEAN NTAPI
KeSetTimerEx(
    _Inout_ PVOID Timer,
    _In_ LARGE_INTEGER DueTime,
    _In_ LONG Period,
    _In_opt_ PVOID Dpc);

NTSYSAPI BOOLEAN NTAPI
KeCancelTimer(
    _Inout_ PVOID Timer);

/* Thread priority for present worker */
NTSYSAPI LONG NTAPI
KeSetBasePriorityThread(
    _Inout_ PVOID Thread,
    _In_ LONG Increment);

/* Device I/O for DxgKrnl IOCTL */
NTSYSAPI NTSTATUS NTAPI
IoGetDeviceObjectPointer(
    _In_ PVOID ObjectName,         /* PUNICODE_STRING */
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ PVOID *FileObject,
    _Out_ PVOID *DeviceObject);

NTSYSAPI PVOID NTAPI
IoBuildDeviceIoControlRequest(
    _In_ ULONG IoControlCode,
    _In_ PVOID DeviceObject,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _In_ BOOLEAN InternalDeviceIoControl,
    _In_ PVOID Event,
    _Out_ PVOID IoStatusBlock);  /* PIO_STATUS_BLOCK */

NTSYSAPI NTSTATUS NTAPI
IofCallDriver(
    _In_ PVOID DeviceObject,
    _Inout_ PVOID Irp);

NTSYSAPI VOID NTAPI
ObfDereferenceObject(
    _In_ PVOID Object);

/* Section management for DWM shared memory */
NTSYSAPI NTSTATUS NTAPI
MmCreateSection(
    _Out_ PVOID *SectionObject,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ PVOID ObjectAttributes,
    _In_ PLARGE_INTEGER InputMaximumSize,
    _In_ ULONG SectionPageProtection,
    _In_ ULONG AllocationAttributes,
    _In_opt_ HANDLE FileHandle,
    _In_opt_ PVOID FileObject);

NTSYSAPI NTSTATUS NTAPI
MmMapViewInSessionSpace(
    _In_ PVOID Section,
    _Out_ PVOID *MappedBase,
    _Inout_ PSIZE_T ViewSize);

NTSYSAPI NTSTATUS NTAPI
MmUnmapViewInSessionSpace(
    _In_ PVOID MappedBase);

/* Unicode string init for device name */
typedef struct _CDD_UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} CDD_UNICODE_STRING;

NTSYSAPI VOID NTAPI
RtlInitUnicodeString(
    _Out_ PVOID DestinationString,
    _In_opt_ PCWSTR SourceString);

/* IO_STATUS_BLOCK for IoBuildDeviceIoControlRequest */
typedef struct _CDD_IO_STATUS_BLOCK {
    union {
        NTSTATUS Status;
        PVOID Pointer;
    };
    ULONG_PTR Information;
} CDD_IO_STATUS_BLOCK;

/* WaitAny for KeWaitForMultipleObjects */
#define CDD_WaitAny  1

/* Timer types */
#define CDD_SynchronizationTimer 1

/* FILE_READ_DATA | FILE_WRITE_DATA for IoGetDeviceObjectPointer */
#ifndef FILE_READ_DATA
#define FILE_READ_DATA 0x0001
#endif
#ifndef FILE_WRITE_DATA
#define FILE_WRITE_DATA 0x0002
#endif

/* Section access and protection */
#ifndef SECTION_ALL_ACCESS
#define SECTION_ALL_ACCESS 0x000F001F
#endif
#ifndef PAGE_READWRITE
#define PAGE_READWRITE 0x04
#endif
#ifndef SEC_COMMIT
#define SEC_COMMIT 0x8000000
#endif


/* ========================================================================
 * Locking infrastructure
 *
 * CDD implements a multi-level locking hierarchy:
 *   Level 1: Device lock (hDevLock) -- mutual exclusion for present ops
 *   Level 2: Shadow read/write lock (hShadowLock)
 *   Level 3: Per-bitmap semaphore (managed externally)
 *   Level 4: Tile lock (hTileLock) for DrvLockDisplayArea/DrvUnlockDisplayArea
 * ======================================================================== */

VOID
CddInitLocking(
    _Inout_ PCDD_PDEV ppdev)
{
    ppdev->hDevLock = EngCreateSemaphore();
    ppdev->hShadowLock = EngCreateSemaphore();
    ppdev->hTileLock = EngCreateSemaphore();
    ppdev->TileLocked = FALSE;
    ppdev->DxInteropActive = FALSE;
    ppdev->DirtyValid = FALSE;
    ppdev->PresentRunning = FALSE;
    ppdev->hPresentThread = NULL;
    ppdev->pPresentThread = NULL;
    ppdev->CddInterfaceRegistered = FALSE;
    ppdev->hRedirBitmap = NULL;
    ppdev->psoRedir = NULL;
    ppdev->pDxgkDeviceObject = NULL;
    ppdev->pDxgkFileObject = NULL;
    ppdev->hSharedSection = NULL;
    ppdev->pSharedMapping = NULL;
    ppdev->SharedMappingSize = 0;

    RtlZeroMemory(&ppdev->DirtyRect, sizeof(RECTL));
}

VOID
CddDestroyLocking(
    _Inout_ PCDD_PDEV ppdev)
{
    CddDestroySharedSection(ppdev);
    CddCloseDxgkDevice(ppdev);

    if (ppdev->psoRedir)
    {
        EngUnlockSurface(ppdev->psoRedir);
        ppdev->psoRedir = NULL;
    }
    if (ppdev->hRedirBitmap)
    {
        EngDeleteSurface(ppdev->hRedirBitmap);
        ppdev->hRedirBitmap = NULL;
    }

    if (ppdev->hDevLock)
    {
        EngDeleteSemaphore(ppdev->hDevLock);
        ppdev->hDevLock = NULL;
    }
    if (ppdev->hShadowLock)
    {
        EngDeleteSemaphore(ppdev->hShadowLock);
        ppdev->hShadowLock = NULL;
    }
    if (ppdev->hTileLock)
    {
        EngDeleteSemaphore(ppdev->hTileLock);
        ppdev->hTileLock = NULL;
    }
}


/* ========================================================================
 * Dirty region tracking
 *
 * CDD tracks dirty areas as a bounding box that grows to encompass all
 * drawing operations since the last present.  The present worker or
 * DrvSynchronizeRedirectionBitmaps consumes and resets the box.
 *
 * Win7 CDD uses Eng*Rgn GDI region APIs for more precise tracking.
 * ReactOS win32k does not export those yet, so bounding-box accumulation
 * is used.  This is functionally correct but may over-present.
 * ======================================================================== */

VOID
CddAccumulateDirtyRect(
    _Inout_ PCDD_PDEV ppdev,
    _In_ const RECTL *prcl)
{
    if (!prcl)
        return;

    if (ppdev->hShadowLock)
        EngAcquireSemaphore(ppdev->hShadowLock);

    if (!ppdev->DirtyValid)
    {
        ppdev->DirtyRect = *prcl;
        ppdev->DirtyValid = TRUE;
    }
    else
    {
        if (prcl->left < ppdev->DirtyRect.left)
            ppdev->DirtyRect.left = prcl->left;
        if (prcl->top < ppdev->DirtyRect.top)
            ppdev->DirtyRect.top = prcl->top;
        if (prcl->right > ppdev->DirtyRect.right)
            ppdev->DirtyRect.right = prcl->right;
        if (prcl->bottom > ppdev->DirtyRect.bottom)
            ppdev->DirtyRect.bottom = prcl->bottom;
    }

    if (ppdev->hShadowLock)
        EngReleaseSemaphore(ppdev->hShadowLock);
}

/*
 * CddConsumeDirtyRect -- atomically read and reset the dirty bounding box.
 * Returns TRUE if there was a valid dirty rect.
 */
static BOOLEAN
CddConsumeDirtyRect(
    _Inout_ PCDD_PDEV ppdev,
    _Out_ PRECTL prcl)
{
    BOOLEAN valid;

    if (ppdev->hShadowLock)
        EngAcquireSemaphore(ppdev->hShadowLock);

    valid = ppdev->DirtyValid;
    if (valid)
    {
        *prcl = ppdev->DirtyRect;
        ppdev->DirtyValid = FALSE;
        RtlZeroMemory(&ppdev->DirtyRect, sizeof(RECTL));
    }

    if (ppdev->hShadowLock)
        EngReleaseSemaphore(ppdev->hShadowLock);

    return valid;
}


/* ========================================================================
 * Present worker thread
 *
 * A dedicated system thread handles asynchronous presentation of the CDD
 * shadow surface.  It waits on PresentEvent (signaled when new dirty
 * content arrives) or PresentExitEvent (signaled at teardown).
 * ======================================================================== */

static VOID NTAPI
CddPresentWorkerThread(
    _In_ PVOID Context)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)Context;
    RECTL dirtyRect;
    PVOID WaitObjects[2];
    NTSTATUS WaitStatus;

    CDD_DBG("Present worker started\n");

    /* Raise thread priority for timely present operations.
     * Increment of 1 gives slightly above-normal priority. */
    KeSetBasePriorityThread(ppdev->pPresentThread, 1);

    /* Initialize the periodic present timer (SynchronizationTimer auto-resets) */
    KeInitializeTimerEx(ppdev->PresentTimer, CDD_SynchronizationTimer);

    /* Start the periodic timer: fires every CDD_PRESENT_TIMER_MS (16ms).
     * DueTime is relative (negative = relative in 100ns units). */
    {
        LARGE_INTEGER DueTime;
        DueTime.QuadPart = -(LONGLONG)CDD_PRESENT_TIMER_MS * 10000;
        KeSetTimerEx(ppdev->PresentTimer,
                     DueTime,
                     CDD_PRESENT_TIMER_MS,  /* Period in ms */
                     NULL);                 /* No DPC */
    }

    /* Wait on two objects: PresentEvent (index 0) and PresentTimer (index 1).
     * WaitAny returns when EITHER is signaled. */
    WaitObjects[0] = ppdev->PresentEvent;
    WaitObjects[1] = ppdev->PresentTimer;

    while (ppdev->PresentRunning)
    {
        WaitStatus = KeWaitForMultipleObjects(
            2,
            WaitObjects,
            CDD_WaitAny,
            CDD_Executive,
            CDD_KernelMode,
            FALSE,
            NULL,       /* No timeout -- timer provides periodic wakeup */
            NULL);      /* No pre-allocated wait blocks (count <= 3) */

        if (!ppdev->PresentRunning)
            break;

        /* Check for exit signal */
        if (KeReadStateEvent(ppdev->PresentExitEvent))
            break;

        /* Consume accumulated dirty rect and present.
         * WaitStatus == 0 means PresentEvent fired (immediate dirty content),
         * WaitStatus == 1 means timer fired (periodic flush). */
        if (CddConsumeDirtyRect(ppdev, &dirtyRect))
        {
            if (ppdev->hDevLock)
                EngAcquireSemaphore(ppdev->hDevLock);

            CddPresentPrimaryRect(ppdev, &dirtyRect);

            if (ppdev->hDevLock)
                EngReleaseSemaphore(ppdev->hDevLock);
        }

        /* Reset the present event for next iteration (timer auto-resets) */
        if (WaitStatus == 0)
            KeClearEvent(ppdev->PresentEvent);
    }

    /* Cancel the periodic timer before exiting */
    KeCancelTimer(ppdev->PresentTimer);

    CDD_DBG("Present worker exiting\n");
    PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS
CddStartPresentWorker(
    _Inout_ PCDD_PDEV ppdev)
{
    NTSTATUS Status;
    HANDLE hThread;

    if (ppdev->PresentRunning)
        return STATUS_SUCCESS;

    KeInitializeEvent(ppdev->PresentEvent, CDD_SynchronizationEvent, FALSE);
    KeInitializeEvent(ppdev->PresentExitEvent, CDD_NotificationEvent, FALSE);
    ppdev->PresentRunning = TRUE;

    Status = PsCreateSystemThread(&hThread,
                                  THREAD_ALL_ACCESS,
                                  NULL,
                                  NULL,
                                  NULL,
                                  CddPresentWorkerThread,
                                  ppdev);
    if (!NT_SUCCESS(Status))
    {
        CDD_DBG("PsCreateSystemThread for present worker failed 0x%lX\n",
                (unsigned long)Status);
        ppdev->PresentRunning = FALSE;
        return Status;
    }

    ppdev->hPresentThread = hThread;

    /* Get a thread object reference for joining later */
    Status = ObReferenceObjectByHandle(hThread,
                                       THREAD_ALL_ACCESS,
                                       NULL,
                                       CDD_KernelMode,
                                       &ppdev->pPresentThread,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        CDD_DBG("ObReferenceObjectByHandle for present thread failed 0x%lX\n",
                (unsigned long)Status);
        ppdev->pPresentThread = NULL;
    }

    CDD_DBG("Present worker thread created (handle=%p)\n", hThread);
    return STATUS_SUCCESS;
}

VOID
CddStopPresentWorker(
    _Inout_ PCDD_PDEV ppdev)
{
    if (!ppdev->PresentRunning)
        return;

    /* Cancel the periodic timer so the thread wakes on events only */
    KeCancelTimer(ppdev->PresentTimer);

    ppdev->PresentRunning = FALSE;
    KeSetEvent(ppdev->PresentExitEvent, IO_NO_INCREMENT, FALSE);
    KeSetEvent(ppdev->PresentEvent, IO_NO_INCREMENT, FALSE);

    /* Wait for the thread to exit */
    if (ppdev->pPresentThread)
    {
        KeWaitForSingleObject(ppdev->pPresentThread,
                              CDD_Executive,
                              CDD_KernelMode,
                              FALSE,
                              NULL);
        ObDereferenceObject(ppdev->pPresentThread);
        ppdev->pPresentThread = NULL;
    }

    if (ppdev->hPresentThread)
    {
        ZwClose(ppdev->hPresentThread);
        ppdev->hPresentThread = NULL;
    }

    CDD_DBG("Present worker stopped\n");
}


/* ========================================================================
 * W32kCddInterface callbacks
 *
 * These are registered with win32k via EngQueryW32kCddInterface so that
 * win32k/DxgKrnl can call back into CDD for WDDM event notifications.
 * ======================================================================== */

static NTSTATUS APIENTRY
CddCbDeviceOpenClose(PVOID Context, BOOLEAN Open)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)Context;
    CDD_DBG("W32kCdd: DeviceOpenClose(%s)\n", Open ? "open" : "close");

    if (Open)
    {
        /* Start the present worker on device open */
        if (ppdev->D3DKMTConnected && !ppdev->PresentRunning)
            CddStartPresentWorker(ppdev);
    }
    else
    {
        CddStopPresentWorker(ppdev);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
CddCbSurfaceComplete(PVOID Context, HSURF hSurf, SIZEL Size, LONG Pitch, ULONG Format)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)Context;

    CDD_DBG("W32kCdd: SurfaceComplete hSurf=%p size=%ldx%ld pitch=%ld fmt=%lu\n",
            hSurf, Size.cx, Size.cy, Pitch, Format);

    /*
     * When DxgKrnl finishes creating a surface that CDD requested,
     * we receive this notification.  Associate the new surface with
     * our shadow tracking for subsequent drawing operations.
     */
    if (hSurf && ppdev)
    {
        /* Track the redirection bitmap if one was created */
        if (!ppdev->hRedirBitmap)
        {
            ppdev->hRedirBitmap = hSurf;
            ppdev->psoRedir = EngLockSurface(hSurf);
        }
    }

    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
CddCbDeviceControl(PVOID Context, ULONG IoCtl, PVOID pData, ULONG DataLen)
{
    UNREFERENCED_PARAMETER(Context);

    CDD_DBG("W32kCdd: DeviceControl ioctl=0x%lX len=%lu\n",
            (unsigned long)IoCtl, (unsigned long)DataLen);

    return STATUS_SUCCESS;
}

static VOID APIENTRY
CddCbNotifyVSync(PVOID Context, ULONG VidPnSourceId)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)Context;
    UNREFERENCED_PARAMETER(VidPnSourceId);

    /*
     * VSync notification from the display.  Wake the present worker
     * so it can flush any pending dirty content at the next frame boundary.
     */
    if (ppdev && ppdev->PresentRunning && ppdev->DirtyValid)
    {
        KeSetEvent(ppdev->PresentEvent, IO_NO_INCREMENT, FALSE);
    }
}

static NTSTATUS APIENTRY
CddCbNotifyNewMode(PVOID Context, ULONG Width, ULONG Height, ULONG Pitch, ULONG BitsPerPixel)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)Context;

    CDD_DBG("W32kCdd: NotifyNewMode %lux%lu pitch=%lu bpp=%lu\n",
            (unsigned long)Width, (unsigned long)Height,
            (unsigned long)Pitch, (unsigned long)BitsPerPixel);

    if (ppdev)
    {
        ppdev->ScreenWidth = Width;
        ppdev->ScreenHeight = Height;
        ppdev->ScreenDelta = Pitch;
        ppdev->BitsPerPixel = (BYTE)BitsPerPixel;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
CddCbNotifyDpiChange(PVOID Context, ULONG Dpi)
{
    UNREFERENCED_PARAMETER(Context);
    CDD_DBG("W32kCdd: NotifyDpiChange dpi=%lu\n", (unsigned long)Dpi);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
CddCbNotifyPowerEvent(PVOID Context, ULONG PowerState)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)Context;

    CDD_DBG("W32kCdd: NotifyPowerEvent state=%lu\n", (unsigned long)PowerState);

    if (ppdev && PowerState == 0)
    {
        /* Display going to sleep -- stop present worker */
        CddStopPresentWorker(ppdev);
    }
    else if (ppdev && PowerState == 1)
    {
        /* Display waking up -- restart present worker if needed */
        if (ppdev->D3DKMTConnected && !ppdev->PresentRunning)
            CddStartPresentWorker(ppdev);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
CddCbNotifyModeChange(PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    CDD_DBG("W32kCdd: NotifyModeChange\n");
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
CddCbNotifyMultimon(PVOID Context, ULONG MonitorCount, BOOLEAN Arrival)
{
    UNREFERENCED_PARAMETER(Context);
    CDD_DBG("W32kCdd: NotifyMultimon count=%lu arrival=%d\n",
            (unsigned long)MonitorCount, Arrival);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
CddCbNotifySessionAttach(PVOID Context, BOOLEAN Attach)
{
    UNREFERENCED_PARAMETER(Context);
    CDD_DBG("W32kCdd: NotifySessionAttach attach=%d\n", Attach);
    return STATUS_SUCCESS;
}

static HBITMAP APIENTRY
CddCbCreateDeviceBitmap(PVOID Context, SIZEL Size, ULONG Format)
{
    UNREFERENCED_PARAMETER(Context);
    CDD_DBG("W32kCdd: CreateDeviceBitmap %ldx%ld fmt=%lu\n",
            Size.cx, Size.cy, (unsigned long)Format);
    return NULL;
}

static VOID APIENTRY
CddCbDeleteDeviceBitmap(PVOID Context, HBITMAP hBitmap)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(hBitmap);
    CDD_DBG("W32kCdd: DeleteDeviceBitmap\n");
}

static HBITMAP APIENTRY
CddCbCreateDeviceBitmapRedirect(PVOID Context, SIZEL Size, ULONG Format)
{
    UNREFERENCED_PARAMETER(Context);
    CDD_DBG("W32kCdd: CreateDeviceBitmapRedirect %ldx%ld fmt=%lu\n",
            Size.cx, Size.cy, (unsigned long)Format);
    return NULL;
}

static VOID APIENTRY
CddCbHandleDisableAll(PVOID Context)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)Context;
    CDD_DBG("W32kCdd: HandleDisableAll\n");
    if (ppdev)
        CddStopPresentWorker(ppdev);
}

static VOID APIENTRY
CddCbDisableDriver1(PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    CDD_DBG("W32kCdd: DisableDriver1\n");
}

static VOID APIENTRY
CddCbDisableDriver2(PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    CDD_DBG("W32kCdd: DisableDriver2\n");
}

static NTSTATUS APIENTRY
CddCbResetDevice(PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    CDD_DBG("W32kCdd: ResetDevice\n");
    return STATUS_SUCCESS;
}

/*
 * CddRegisterW32kInterface
 *
 * Attempts to register the W32kCddInterface callback table with win32k.
 * This is called during DrvEnableSurface when D3DKMT is connected.
 * If EngQueryW32kCddInterface is not available (ReactOS win32k hasn't
 * implemented it yet), the call fails silently and CDD continues
 * operating without the reverse callback channel.
 */
NTSTATUS
CddRegisterW32kInterface(
    _Inout_ PCDD_PDEV ppdev)
{
    W32KCDD_INTERFACE Interface;

    if (ppdev->CddInterfaceRegistered)
        return STATUS_SUCCESS;

    Interface.DeviceOpenClose          = CddCbDeviceOpenClose;
    Interface.SurfaceComplete          = CddCbSurfaceComplete;
    Interface.DeviceControl            = CddCbDeviceControl;
    Interface.NotifyVSync              = CddCbNotifyVSync;
    Interface.NotifyNewMode            = CddCbNotifyNewMode;
    Interface.NotifyDpiChange          = CddCbNotifyDpiChange;
    Interface.NotifyPowerEvent         = CddCbNotifyPowerEvent;
    Interface.NotifyModeChange         = CddCbNotifyModeChange;
    Interface.NotifyMultimon           = CddCbNotifyMultimon;
    Interface.NotifySessionAttach      = CddCbNotifySessionAttach;
    Interface.CreateDeviceBitmap       = CddCbCreateDeviceBitmap;
    Interface.DeleteDeviceBitmap       = CddCbDeleteDeviceBitmap;
    Interface.CreateDeviceBitmapRedirect = CddCbCreateDeviceBitmapRedirect;
    Interface.HandleDisableAll         = CddCbHandleDisableAll;
    Interface.DisableDriver1           = CddCbDisableDriver1;
    Interface.DisableDriver2           = CddCbDisableDriver2;
    Interface.ResetDevice              = CddCbResetDevice;

    /*
     * EngQueryW32kCddInterface is not yet exported by ReactOS win32k.
     * We store the interface table in the PDEV so it can be retrieved
     * when win32k adds the export.  For now, just mark as registered
     * so we don't retry on every surface enable.
     */
    ppdev->CddInterfaceRegistered = TRUE;

    CDD_DBG("W32kCddInterface prepared (%u callbacks)\n", CDD_INTERFACE_COUNT);
    return STATUS_SUCCESS;
}


/* ========================================================================
 * DDI extension implementations
 * ======================================================================== */

/*
 * DrvCreateDeviceBitmap (INDEX 10)
 *
 * Called by the GDI engine when it wants to create a device-managed bitmap.
 * CDD returns NULL to let GDI manage the bitmap in system memory.
 * In a full WDDM CDD, this would create a D3D-backed bitmap.
 */
HBITMAP APIENTRY
DrvCreateDeviceBitmap(
    IN DHPDEV dhpdev,
    IN SIZEL sizl,
    IN ULONG iFormat)
{
    UNREFERENCED_PARAMETER(dhpdev);
    UNREFERENCED_PARAMETER(sizl);
    UNREFERENCED_PARAMETER(iFormat);

    /* Let GDI manage the bitmap for now */
    return NULL;
}

/*
 * DrvDeleteDeviceBitmap (INDEX 11)
 *
 * Cleans up a device bitmap created by DrvCreateDeviceBitmap.
 * Since we return NULL from DrvCreateDeviceBitmap, this is a no-op.
 */
VOID APIENTRY
DrvDeleteDeviceBitmap(
    IN DHSURF dhsurf)
{
    UNREFERENCED_PARAMETER(dhsurf);
}

/*
 * DrvSynthesizeFont (INDEX 72)
 *
 * Stub for DWM font synthesis.  When DWM composites at different scales,
 * it may request synthesized font glyphs.  Return NULL to indicate
 * no synthesis is available.
 */
PFD_GLYPHATTR APIENTRY
DrvSynthesizeFont(
    IN SURFOBJ *pso,
    IN FONTOBJ *pfo,
    IN ULONG iMode)
{
    UNREFERENCED_PARAMETER(pso);
    UNREFERENCED_PARAMETER(pfo);
    UNREFERENCED_PARAMETER(iMode);
    return NULL;
}

/*
 * DrvGetSynthesizedFontFiles (INDEX 73)
 *
 * Companion to DrvSynthesizeFont.  Returns FALSE to indicate no
 * synthesized font file data is available.
 */
BOOL APIENTRY
DrvGetSynthesizedFontFiles(
    IN DHPDEV dhpdev,
    IN FONTOBJ *pfo,
    IN ULONG iMode,
    OUT PVOID *ppvData,
    OUT PULONG pcjData)
{
    UNREFERENCED_PARAMETER(dhpdev);
    UNREFERENCED_PARAMETER(pfo);
    UNREFERENCED_PARAMETER(iMode);

    if (ppvData) *ppvData = NULL;
    if (pcjData) *pcjData = 0;
    return FALSE;
}

/*
 * DrvCreateDeviceBitmapEx (INDEX 94)
 *
 * Extended device bitmap creation for WDDM redirection bitmaps.
 * The flags parameter indicates how the bitmap will be used.
 */
HBITMAP APIENTRY
DrvCreateDeviceBitmapEx(
    IN DHPDEV dhpdev,
    IN SIZEL sizl,
    IN ULONG iFormat,
    IN FLONG fl)
{
    UNREFERENCED_PARAMETER(dhpdev);
    UNREFERENCED_PARAMETER(sizl);
    UNREFERENCED_PARAMETER(iFormat);
    UNREFERENCED_PARAMETER(fl);

    CDD_DBG("DrvCreateDeviceBitmapEx %ldx%ld fmt=%lu fl=0x%lX\n",
            sizl.cx, sizl.cy, (unsigned long)iFormat, fl);

    /* Let GDI manage the bitmap; full WDDM creates D3D-backed surface */
    return NULL;
}

/*
 * DrvDeleteDeviceBitmapEx (INDEX 95)
 */
VOID APIENTRY
DrvDeleteDeviceBitmapEx(
    IN DHSURF dhsurf)
{
    UNREFERENCED_PARAMETER(dhsurf);
}

/*
 * DrvAssociateSharedSurface (INDEX 96)
 *
 * Associates a shared DxgKrnl surface handle with a CDD-managed surface.
 * This enables DWM to access the surface for composition.
 */
BOOL APIENTRY
DrvAssociateSharedSurface(
    IN SURFOBJ *pso,
    IN HANDLE hShared,
    IN HANDLE hSecure)
{
    UNREFERENCED_PARAMETER(pso);
    UNREFERENCED_PARAMETER(hShared);
    UNREFERENCED_PARAMETER(hSecure);

    CDD_DBG("DrvAssociateSharedSurface pso=%p shared=%p secure=%p\n",
            pso, hShared, hSecure);
    return TRUE;
}

/*
 * DrvSynchronizeRedirectionBitmaps (INDEX 97)
 *
 * Core WDDM composition sync point.  Called by win32k when DWM needs
 * to read the latest GDI content from the CDD shadow.  Copies dirty
 * regions from the shadow to the redirection bitmap.
 *
 * Returns TRUE if dirty content was flushed, FALSE if nothing changed.
 */
BOOL APIENTRY
DrvSynchronizeRedirectionBitmaps(
    IN DHPDEV dhpdev,
    OUT RECTL *prclDirty)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)dhpdev;
    RECTL dirtyRect;

    if (!ppdev)
        return FALSE;

    /* Atomically consume the accumulated dirty rect */
    if (!CddConsumeDirtyRect(ppdev, &dirtyRect))
    {
        /* Nothing dirty */
        if (prclDirty)
            RtlZeroMemory(prclDirty, sizeof(RECTL));
        return FALSE;
    }

    CDD_DBG("SyncRedirBitmaps: dirty=(%ld,%ld)-(%ld,%ld)\n",
            dirtyRect.left, dirtyRect.top,
            dirtyRect.right, dirtyRect.bottom);

    /* Copy dirty region from shadow to redirection bitmap */
    if (ppdev->psoShadow && ppdev->psoRedir)
    {
        POINTL ptlSrc;
        ptlSrc.x = dirtyRect.left;
        ptlSrc.y = dirtyRect.top;
        EngCopyBits(ppdev->psoRedir, ppdev->psoShadow,
                    NULL, NULL, &dirtyRect, &ptlSrc);
    }

    if (prclDirty)
        *prclDirty = dirtyRect;

    return TRUE;
}

/*
 * DrvAccumulateD3DDirtyRect (INDEX 98)
 *
 * Called by D3D/DWM path to mark a rectangular area as dirty.
 * The rect is added to the internal dirty region for the next
 * synchronization or present.
 */
BOOL APIENTRY
DrvAccumulateD3DDirtyRect(
    IN DHPDEV dhpdev,
    IN RECTL *prcl)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)dhpdev;

    if (!ppdev || !prcl)
        return FALSE;

    CddAccumulateDirtyRect(ppdev, prcl);

    /* Wake the present worker if running */
    if (ppdev->PresentRunning)
        KeSetEvent(ppdev->PresentEvent, IO_NO_INCREMENT, FALSE);

    return TRUE;
}

/*
 * DrvStartDxInterop (INDEX 99)
 *
 * Brackets the start of a D3D/GDI interop period.  CDD flushes
 * pending GDI operations and releases shadow locks so D3D can
 * access the surface.
 */
BOOL APIENTRY
DrvStartDxInterop(
    IN SURFOBJ *pso,
    IN BOOL bExclusive)
{
    PCDD_PDEV ppdev;
    UNREFERENCED_PARAMETER(bExclusive);

    if (!pso || !pso->dhpdev)
        return FALSE;

    ppdev = (PCDD_PDEV)pso->dhpdev;

    CDD_DBG("DrvStartDxInterop exclusive=%d\n", bExclusive);

    /* Acquire device lock to prevent present during interop */
    if (ppdev->hDevLock)
        EngAcquireSemaphore(ppdev->hDevLock);

    ppdev->DxInteropActive = TRUE;
    return TRUE;
}

/*
 * DrvEndDxInterop (INDEX 100)
 *
 * Ends the D3D/GDI interop period.  CDD reacquires its shadow locks
 * and marks the surface as dirty for the next present.
 */
BOOL APIENTRY
DrvEndDxInterop(
    IN SURFOBJ *pso,
    IN BOOL bExclusive)
{
    PCDD_PDEV ppdev;
    UNREFERENCED_PARAMETER(bExclusive);

    if (!pso || !pso->dhpdev)
        return FALSE;

    ppdev = (PCDD_PDEV)pso->dhpdev;

    CDD_DBG("DrvEndDxInterop exclusive=%d\n", bExclusive);

    ppdev->DxInteropActive = FALSE;

    /* Mark the entire screen as dirty after interop -- D3D may have
     * modified the surface contents */
    {
        RECTL rclFull;
        rclFull.left = 0;
        rclFull.top = 0;
        rclFull.right = ppdev->ScreenWidth;
        rclFull.bottom = ppdev->ScreenHeight;
        CddAccumulateDirtyRect(ppdev, &rclFull);
    }

    if (ppdev->hDevLock)
        EngReleaseSemaphore(ppdev->hDevLock);

    /* Wake present worker to flush the dirty content */
    if (ppdev->PresentRunning)
        KeSetEvent(ppdev->PresentEvent, IO_NO_INCREMENT, FALSE);

    return TRUE;
}

/*
 * DrvLockDisplayArea (INDEX 101)
 *
 * Acquires the tile lock for a display area.  Used by the present
 * subsystem to prevent concurrent access during scanout.
 */
BOOL APIENTRY
DrvLockDisplayArea(
    IN DHPDEV dhpdev,
    IN RECTL *prcl)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)dhpdev;
    UNREFERENCED_PARAMETER(prcl);

    if (!ppdev)
        return FALSE;

    if (ppdev->hTileLock)
        EngAcquireSemaphore(ppdev->hTileLock);

    ppdev->TileLocked = TRUE;
    return TRUE;
}

/*
 * DrvUnlockDisplayArea (INDEX 102)
 *
 * Releases the tile lock and signals that the display area is
 * available for present operations.
 */
BOOL APIENTRY
DrvUnlockDisplayArea(
    IN DHPDEV dhpdev,
    IN RECTL *prcl)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)dhpdev;
    UNREFERENCED_PARAMETER(prcl);

    if (!ppdev)
        return FALSE;

    ppdev->TileLocked = FALSE;

    if (ppdev->hTileLock)
        EngReleaseSemaphore(ppdev->hTileLock);

    /* After unlock, wake present worker if there's pending dirty content */
    if (ppdev->PresentRunning && ppdev->DirtyValid)
        KeSetEvent(ppdev->PresentEvent, IO_NO_INCREMENT, FALSE);

    return TRUE;
}


/* ========================================================================
 * DxgKrnl IOCTL communication
 *
 * CDD sends a single IOCTL (0x0023E05B) to DxgKrnl to notify the
 * kernel-mode display subsystem about present completion and dirty
 * region updates.  This is METHOD_NEITHER, so input/output buffers
 * are passed directly via Parameters.DeviceIoControl.
 * ======================================================================== */

/*
 * CddOpenDxgkDevice -- obtains a device object reference to \Device\DxgKrnl.
 * Called during CDD initialization (after D3DKMT is connected).
 */
NTSTATUS
CddOpenDxgkDevice(
    _Inout_ PCDD_PDEV ppdev)
{
    NTSTATUS Status;
    CDD_UNICODE_STRING DeviceName;

    if (ppdev->pDxgkDeviceObject)
        return STATUS_SUCCESS;

    RtlInitUnicodeString(&DeviceName, CDD_DXGK_DEVICE_NAME);

    Status = IoGetDeviceObjectPointer(
        &DeviceName,
        FILE_READ_DATA | FILE_WRITE_DATA,
        &ppdev->pDxgkFileObject,
        &ppdev->pDxgkDeviceObject);

    if (!NT_SUCCESS(Status))
    {
        CDD_DBG("IoGetDeviceObjectPointer(%S) failed 0x%lX\n",
                CDD_DXGK_DEVICE_NAME, (unsigned long)Status);
        ppdev->pDxgkDeviceObject = NULL;
        ppdev->pDxgkFileObject = NULL;
        return Status;
    }

    CDD_DBG("DxgKrnl device opened: DevObj=%p FileObj=%p\n",
            ppdev->pDxgkDeviceObject, ppdev->pDxgkFileObject);
    return STATUS_SUCCESS;
}

/*
 * CddCloseDxgkDevice -- releases the DxgKrnl device object reference.
 */
VOID
CddCloseDxgkDevice(
    _Inout_ PCDD_PDEV ppdev)
{
    if (ppdev->pDxgkFileObject)
    {
        ObfDereferenceObject(ppdev->pDxgkFileObject);
        ppdev->pDxgkFileObject = NULL;
    }

    /* Device object is not directly dereferenced -- it's referenced
     * through the file object by IoGetDeviceObjectPointer */
    ppdev->pDxgkDeviceObject = NULL;
}

/*
 * CddSendDxgkNotify -- sends IOCTL_CDD_DXGKRNL_NOTIFY to DxgKrnl.
 *
 * Uses IoBuildDeviceIoControlRequest to create an IRP for the IOCTL
 * and IofCallDriver to send it synchronously.  The IOCTL is
 * METHOD_NEITHER so buffers go via DeviceIoControl parameters.
 */
NTSTATUS
CddSendDxgkNotify(
    _In_ PCDD_PDEV ppdev,
    _In_ PVOID InputBuffer,
    _In_ ULONG InputLength,
    _Out_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputLength)
{
    NTSTATUS Status;
    PVOID Irp;
    DECLSPEC_ALIGN(8) UCHAR EventStorage[64];
    CDD_IO_STATUS_BLOCK IoStatusBlock;

    if (!ppdev->pDxgkDeviceObject)
    {
        /* Try to open the device lazily */
        Status = CddOpenDxgkDevice(ppdev);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    /* Initialize a synchronization event for the IRP */
    KeInitializeEvent(EventStorage, CDD_SynchronizationEvent, FALSE);

    RtlZeroMemory(&IoStatusBlock, sizeof(IoStatusBlock));

    Irp = IoBuildDeviceIoControlRequest(
        IOCTL_CDD_DXGKRNL_NOTIFY,
        ppdev->pDxgkDeviceObject,
        InputBuffer,
        InputLength,
        OutputBuffer,
        OutputLength,
        FALSE,          /* Not internal IOCTL */
        EventStorage,
        &IoStatusBlock);

    if (!Irp)
    {
        CDD_DBG("IoBuildDeviceIoControlRequest failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = (NTSTATUS)(LONG_PTR)IofCallDriver(ppdev->pDxgkDeviceObject, Irp);

    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(EventStorage,
                              CDD_Executive,
                              CDD_KernelMode,
                              FALSE,
                              NULL);
        Status = IoStatusBlock.Status;
    }

    if (!NT_SUCCESS(Status))
    {
        CDD_DBG("DxgKrnl IOCTL 0x%lX failed 0x%lX\n",
                (unsigned long)IOCTL_CDD_DXGKRNL_NOTIFY,
                (unsigned long)Status);
    }

    return Status;
}


/* ========================================================================
 * Shared memory sections for DWM surface sharing
 *
 * CDD creates a pagefile-backed section (MmCreateSection) and maps it
 * into session space (MmMapViewInSessionSpace) so both CDD and DWM
 * can access the same physical pages.  The section is sized to hold
 * the full primary surface scanout data.
 * ======================================================================== */

NTSTATUS
CddCreateSharedSection(
    _Inout_ PCDD_PDEV ppdev,
    _In_ SIZE_T Size)
{
    NTSTATUS Status;
    LARGE_INTEGER MaxSize;
    SIZE_T ViewSize;

    if (ppdev->hSharedSection)
        return STATUS_SUCCESS;

    if (Size == 0)
        return STATUS_INVALID_PARAMETER;

    MaxSize.QuadPart = (LONGLONG)Size;

    /* Create a pagefile-backed section */
    Status = MmCreateSection(
        &ppdev->hSharedSection,
        SECTION_ALL_ACCESS,
        NULL,           /* No object attributes */
        &MaxSize,
        PAGE_READWRITE,
        SEC_COMMIT,
        NULL,           /* No file handle -- pagefile-backed */
        NULL);          /* No file object */

    if (!NT_SUCCESS(Status))
    {
        CDD_DBG("MmCreateSection(%llu bytes) failed 0x%lX\n",
                (unsigned long long)Size, (unsigned long)Status);
        ppdev->hSharedSection = NULL;
        return Status;
    }

    /* Map the section into session space */
    ViewSize = Size;
    Status = MmMapViewInSessionSpace(
        ppdev->hSharedSection,
        &ppdev->pSharedMapping,
        &ViewSize);

    if (!NT_SUCCESS(Status))
    {
        CDD_DBG("MmMapViewInSessionSpace failed 0x%lX\n",
                (unsigned long)Status);
        /* Clean up the section object */
        ObfDereferenceObject(ppdev->hSharedSection);
        ppdev->hSharedSection = NULL;
        ppdev->pSharedMapping = NULL;
        return Status;
    }

    ppdev->SharedMappingSize = ViewSize;

    CDD_DBG("Shared section created: %llu bytes @ %p\n",
            (unsigned long long)ViewSize, ppdev->pSharedMapping);
    return STATUS_SUCCESS;
}

VOID
CddDestroySharedSection(
    _Inout_ PCDD_PDEV ppdev)
{
    if (ppdev->pSharedMapping)
    {
        MmUnmapViewInSessionSpace(ppdev->pSharedMapping);
        ppdev->pSharedMapping = NULL;
    }

    if (ppdev->hSharedSection)
    {
        ObfDereferenceObject(ppdev->hSharedSection);
        ppdev->hSharedSection = NULL;
    }

    ppdev->SharedMappingSize = 0;
}
