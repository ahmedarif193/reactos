/*
 * PROJECT:     ReactOS Display Driver Model - Win32k/dxgkrnl Bridge
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NtGdiDdDDI* -> D3DKMT* thin adapter layer
 * COPYRIGHT:   Copyright 2025 ReactOS Contributors
 *
 * OVERVIEW
 * --------
 * This file documents and implements the thin glue between the NtGdiDdDDI*
 * syscall handlers (win32ss/gdi/ntgdi/d3dkmt.c) and the D3DKMT* entry
 * points (d3dkmt_stubs.c in this directory).
 *
 * The full call chain from user mode to dxgkrnl is:
 *
 *   [user mode]
 *   DXGI / D3D runtime
 *       |
 *       | sysenter / syscall instruction
 *       v
 *   [kernel mode — win32k SSDT dispatch]
 *   NtGdiDdDDI<Foo>()          [win32ss/gdi/ntgdi/d3dkmt.c]
 *       |
 *       | DxgAdapterCallbacks.RxgkIntPfn<Foo>()
 *       | (function pointer filled by DxStartupDxgkInt / WddmBridgeInit)
 *       v
 *   D3DKMT<Foo>()              [win32ss/drivers/wddm/d3dkmt_stubs.c]
 *       |
 *       | WddmBridgeSendIoctl() [win32ss/drivers/wddm/wddm_bridge.c]
 *       v
 *   \Device\DxgKrnl            [dxgkrnl.sys]
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * The existing d3dkmt.c dispatches through the REACTOS_WIN32K_DXGKRNL_INTERFACE
 * callback table.  That table is populated at runtime by DxStartupDxgkInt().
 * The private interface is negotiated at bridge startup, then this file
 * installs explicit NtGdiDdDDI* -> D3DKMT* forwarding functions.  Each
 * function:
 *
 *   1. Validates the parameter pointer.
 *   2. Calls the corresponding D3DKMT* bridge entry point.
 *   3. Returns the bridge entry point's status to the original caller.
 *
 * When D3DKMT stubs are fully implemented, this layer becomes a zero-cost
 * abstraction that can be inlined or removed.
 *
 * CALLBACK WIRING
 * ---------------
 * DxStartupDxgkInt() negotiates the bridge and calls
 * WddmBridgeInitCallbacks(), which installs the forwarders below.
 *
 * The exchange table is a version/readiness handshake.  Public handles must
 * still pass through these marshalling shims rather than direct kernel-private
 * callbacks.
 */

#include <ntifs.h>
#include <windef.h>
#include "wddm_bridge.h"
#include <d3dkmthk.h>
#include <reactos/rddm/rxgkinterface.h>
#define NDEBUG
#include <debug.h>

#define DEFINE_DXG_BRIDGE_FORWARDER(Name, Target, Annotation, Type) \
NTSTATUS                                                            \
APIENTRY                                                            \
Name(                                                               \
    Annotation Type *pData)                                         \
{                                                                   \
    if (pData == NULL)                                              \
        return STATUS_INVALID_PARAMETER;                            \
                                                                    \
    DPRINT(#Name ": forwarding to " #Target "\n");                  \
    return Target(pData);                                           \
}

/*
 * Forward declarations for D3DKMT stubs defined in d3dkmt_stubs.c.
 * These are the kernel-mode entry points that eventually call
 * WddmBridgeSendIoctl.
 */
NTSTATUS APIENTRY D3DKMTOpenAdapterFromHdc(D3DKMT_OPENADAPTERFROMHDC *pData);
NTSTATUS APIENTRY D3DKMTCloseAdapter(CONST D3DKMT_CLOSEADAPTER *pData);
NTSTATUS APIENTRY D3DKMTCreateDevice(D3DKMT_CREATEDEVICE *pData);
NTSTATUS APIENTRY D3DKMTDestroyDevice(CONST D3DKMT_DESTROYDEVICE *pData);
NTSTATUS APIENTRY D3DKMTCreateAllocation(D3DKMT_CREATEALLOCATION *pData);
NTSTATUS APIENTRY D3DKMTDestroyAllocation(CONST D3DKMT_DESTROYALLOCATION *pData);
NTSTATUS APIENTRY D3DKMTLock(D3DKMT_LOCK *pData);
NTSTATUS APIENTRY D3DKMTUnlock(CONST D3DKMT_UNLOCK *pData);
NTSTATUS APIENTRY D3DKMTRender(D3DKMT_RENDER *pData);
NTSTATUS APIENTRY D3DKMTPresent(D3DKMT_PRESENT *pData);
NTSTATUS APIENTRY D3DKMTWaitForSynchronizationObject(CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECT *pData);
NTSTATUS APIENTRY D3DKMTSignalSynchronizationObject(CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECT *pData);
NTSTATUS APIENTRY D3DKMTGetDisplayModeList(D3DKMT_GETDISPLAYMODELIST *pData);
NTSTATUS APIENTRY D3DKMTSetDisplayMode(CONST D3DKMT_SETDISPLAYMODE *pData);
NTSTATUS APIENTRY D3DKMTQueryAdapterInfo(CONST D3DKMT_QUERYADAPTERINFO *pData);
NTSTATUS APIENTRY D3DKMTCreateContext(D3DKMT_CREATECONTEXT *pData);
NTSTATUS APIENTRY D3DKMTDestroyContext(CONST D3DKMT_DESTROYCONTEXT *pData);
NTSTATUS APIENTRY D3DKMTCreateSynchronizationObject(D3DKMT_CREATESYNCHRONIZATIONOBJECT *pData);
NTSTATUS APIENTRY D3DKMTDestroySynchronizationObject(CONST D3DKMT_DESTROYSYNCHRONIZATIONOBJECT *pData);
NTSTATUS APIENTRY D3DKMTEscape(CONST D3DKMT_ESCAPE *pData);
NTSTATUS APIENTRY D3DKMTSetVidPnSourceOwner(CONST D3DKMT_SETVIDPNSOURCEOWNER *pData);
NTSTATUS APIENTRY D3DKMTGetDeviceState(D3DKMT_GETDEVICESTATE *pData);
NTSTATUS APIENTRY D3DKMTCheckMonitorPowerState(CONST D3DKMT_CHECKMONITORPOWERSTATE *pData);
NTSTATUS APIENTRY D3DKMTCheckOcclusion(CONST D3DKMT_CHECKOCCLUSION *pData);
NTSTATUS APIENTRY D3DKMTCreateOverlay(D3DKMT_CREATEOVERLAY *pData);
NTSTATUS APIENTRY D3DKMTDestroyOverlay(CONST D3DKMT_DESTROYOVERLAY *pData);
NTSTATUS APIENTRY D3DKMTFlipOverlay(CONST D3DKMT_FLIPOVERLAY *pData);
NTSTATUS APIENTRY D3DKMTUpdateOverlay(CONST D3DKMT_UPDATEOVERLAY *pData);
NTSTATUS APIENTRY D3DKMTGetContextSchedulingPriority(D3DKMT_GETCONTEXTSCHEDULINGPRIORITY *pData);
NTSTATUS APIENTRY D3DKMTSetContextSchedulingPriority(CONST D3DKMT_SETCONTEXTSCHEDULINGPRIORITY *pData);
NTSTATUS APIENTRY D3DKMTGetMultisampleMethodList(D3DKMT_GETMULTISAMPLEMETHODLIST *pData);
NTSTATUS APIENTRY D3DKMTGetPresentHistory(D3DKMT_GETPRESENTHISTORY *pData);
NTSTATUS APIENTRY D3DKMTGetRuntimeData(CONST D3DKMT_GETRUNTIMEDATA *pData);
NTSTATUS APIENTRY D3DKMTGetScanLine(D3DKMT_GETSCANLINE *pData);
NTSTATUS APIENTRY D3DKMTInvalidateActiveVidPn(CONST D3DKMT_INVALIDATEACTIVEVIDPN *pData);
NTSTATUS APIENTRY D3DKMTPollDisplayChildren(CONST D3DKMT_POLLDISPLAYCHILDREN *pData);
NTSTATUS APIENTRY D3DKMTQueryAllocationResidency(CONST D3DKMT_QUERYALLOCATIONRESIDENCY *pData);
NTSTATUS APIENTRY D3DKMTQueryStatistics(CONST D3DKMT_QUERYSTATISTICS *pData);
NTSTATUS APIENTRY D3DKMTReleaseProcessVidPnSourceOwners(HANDLE hProcess);
NTSTATUS APIENTRY D3DKMTSetAllocationPriority(CONST D3DKMT_SETALLOCATIONPRIORITY *pData);
NTSTATUS APIENTRY D3DKMTSetDisplayPrivateDriverFormat(CONST D3DKMT_SETDISPLAYPRIVATEDRIVERFORMAT *pData);
NTSTATUS APIENTRY D3DKMTSetGammaRamp(CONST D3DKMT_SETGAMMARAMP *pData);
NTSTATUS APIENTRY D3DKMTSetQueuedLimit(CONST D3DKMT_SETQUEUEDLIMIT *pData);
NTSTATUS APIENTRY D3DKMTWaitForIdle(CONST D3DKMT_WAITFORIDLE *pData);
NTSTATUS APIENTRY D3DKMTWaitForVerticalBlankEvent(CONST D3DKMT_WAITFORVERTICALBLANKEVENT *pData);

/* WDDM 1.2 additions */
NTSTATUS APIENTRY D3DKMTEnumAdapters(CONST D3DKMT_ENUMADAPTERS *pData);
NTSTATUS APIENTRY D3DKMTOpenAdapterFromLuid(CONST D3DKMT_OPENADAPTERFROMLUID *pData);
NTSTATUS APIENTRY D3DKMTOfferAllocations(CONST D3DKMT_OFFERALLOCATIONS *pData);
NTSTATUS APIENTRY D3DKMTReclaimAllocations(CONST D3DKMT_RECLAIMALLOCATIONS *pData);
NTSTATUS APIENTRY D3DKMTSetVidPnSourceOwner1(CONST D3DKMT_SETVIDPNSOURCEOWNER1 *pData);
NTSTATUS APIENTRY D3DKMTWaitForVerticalBlankEvent2(CONST D3DKMT_WAITFORVERTICALBLANKEVENT2 *pData);
NTSTATUS APIENTRY D3DKMTCreateSynchronizationObject2(D3DKMT_CREATESYNCHRONIZATIONOBJECT2 *pData);
NTSTATUS APIENTRY D3DKMTWaitForSynchronizationObject2(CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECT2 *pData);
NTSTATUS APIENTRY D3DKMTSignalSynchronizationObject2(CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 *pData);

/* WDDM 2.0 additions */
NTSTATUS APIENTRY D3DKMTMakeResident(D3DDDI_MAKERESIDENT *pData);
NTSTATUS APIENTRY D3DKMTEvict(D3DKMT_EVICT *pData);
NTSTATUS APIENTRY D3DKMTQueryVideoMemoryInfo(D3DKMT_QUERYVIDEOMEMORYINFO *pData);
NTSTATUS APIENTRY D3DKMTCreatePagingQueue(D3DKMT_CREATEPAGINGQUEUE *pData);
NTSTATUS APIENTRY D3DKMTDestroyPagingQueue(D3DDDI_DESTROYPAGINGQUEUE *pData);
NTSTATUS APIENTRY D3DKMTReserveGpuVirtualAddress(D3DDDI_RESERVEGPUVIRTUALADDRESS *pData);
NTSTATUS APIENTRY D3DKMTMapGpuVirtualAddress(D3DDDI_MAPGPUVIRTUALADDRESS *pData);
NTSTATUS APIENTRY D3DKMTFreeGpuVirtualAddress(CONST D3DKMT_FREEGPUVIRTUALADDRESS *pData);
NTSTATUS APIENTRY D3DKMTUpdateGpuVirtualAddress(CONST D3DKMT_UPDATEGPUVIRTUALADDRESS *pData);
NTSTATUS APIENTRY D3DKMTWaitForSynchronizationObjectFromCpu(CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *pData);
NTSTATUS APIENTRY D3DKMTSignalSynchronizationObjectFromCpu(CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU *pData);
NTSTATUS APIENTRY D3DKMTCreateContextVirtual(D3DKMT_CREATECONTEXTVIRTUAL *pData);
NTSTATUS APIENTRY D3DKMTSubmitCommand(CONST D3DKMT_SUBMITCOMMAND *pData);

/*
 * ==========================================================================
 *  Bridge adapter functions
 *  (type-safe forwarders; suitable for insertion into
 *   REACTOS_WIN32K_DXGKRNL_INTERFACE as function pointers)
 * ==========================================================================
 */

/*
 * DxgBridgeOpenAdapterFromHdc
 *
 * Bridges NtGdiDdDDIOpenAdapterFromHdc -> D3DKMTOpenAdapterFromHdc.
 * The HDC in pData->hDc must identify a WDDM display; GDI DCs backed by
 * software-only adapters are rejected by dxgkrnl.
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeOpenAdapterFromHdc,
                            D3DKMTOpenAdapterFromHdc,
                            _Inout_,
                            D3DKMT_OPENADAPTERFROMHDC)

/*
 * DxgBridgeCloseAdapter
 *
 * Bridges NtGdiDdDDICloseAdapter -> D3DKMTCloseAdapter.
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeCloseAdapter,
                            D3DKMTCloseAdapter,
                            _In_,
                            CONST D3DKMT_CLOSEADAPTER)

/*
 * DxgBridgeQueryAdapterInfo
 *
 * Bridges NtGdiDdDDIQueryAdapterInfo -> D3DKMTQueryAdapterInfo.
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeQueryAdapterInfo,
                            D3DKMTQueryAdapterInfo,
                            _Inout_,
                            CONST D3DKMT_QUERYADAPTERINFO)

/*
 * DxgBridgeCreateDevice
 *
 * Bridges NtGdiDdDDICreateDevice -> D3DKMTCreateDevice.
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeCreateDevice,
                            D3DKMTCreateDevice,
                            _Inout_,
                            D3DKMT_CREATEDEVICE)

/*
 * DxgBridgeDestroyDevice
 *
 * Bridges NtGdiDdDDIDestroyDevice -> D3DKMTDestroyDevice.
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeDestroyDevice,
                            D3DKMTDestroyDevice,
                            _In_,
                            CONST D3DKMT_DESTROYDEVICE)

/*
 * DxgBridgeCreateAllocation
 *
 * Bridges NtGdiDdDDICreateAllocation -> D3DKMTCreateAllocation.
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeCreateAllocation,
                            D3DKMTCreateAllocation,
                            _Inout_,
                            D3DKMT_CREATEALLOCATION)

/*
 * DxgBridgeDestroyAllocation
 *
 * Bridges NtGdiDdDDIDestroyAllocation -> D3DKMTDestroyAllocation.
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeDestroyAllocation,
                            D3DKMTDestroyAllocation,
                            _In_,
                            CONST D3DKMT_DESTROYALLOCATION)

/*
 * DxgBridgeLock
 *
 * Bridges NtGdiDdDDILock -> D3DKMTLock.
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeLock,
                            D3DKMTLock,
                            _Inout_,
                            D3DKMT_LOCK)

/*
 * DxgBridgeUnlock
 *
 * Bridges NtGdiDdDDIUnlock -> D3DKMTUnlock.
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeUnlock,
                            D3DKMTUnlock,
                            _In_,
                            CONST D3DKMT_UNLOCK)

/*
 * DxgBridgeRender
 *
 * Bridges NtGdiDdDDIRender -> D3DKMTRender.
 * This is the hot path for all GPU command submission.
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeRender,
                            D3DKMTRender,
                            _Inout_,
                            D3DKMT_RENDER)

/*
 * DxgBridgePresent
 *
 * Bridges NtGdiDdDDIPresent -> D3DKMTPresent.
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgePresent,
                            D3DKMTPresent,
                            _Inout_,
                            D3DKMT_PRESENT)

/*
 * DxgBridgeWaitForSynchronizationObject
 *
 * Bridges NtGdiDdDDIWaitForSynchronizationObject ->
 *   D3DKMTWaitForSynchronizationObject.
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeWaitForSynchronizationObject,
                            D3DKMTWaitForSynchronizationObject,
                            _In_,
                            CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECT)

/*
 * DxgBridgeSignalSynchronizationObject
 *
 * Bridges NtGdiDdDDISignalSynchronizationObject ->
 *   D3DKMTSignalSynchronizationObject.
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeSignalSynchronizationObject,
                            D3DKMTSignalSynchronizationObject,
                            _In_,
                            CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECT)

/*
 * DxgBridgeGetDisplayModeList
 *
 * Bridges NtGdiDdDDIGetDisplayModeList -> D3DKMTGetDisplayModeList.
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeGetDisplayModeList,
                            D3DKMTGetDisplayModeList,
                            _Inout_,
                            D3DKMT_GETDISPLAYMODELIST)

/*
 * DxgBridgeSetDisplayMode
 *
 * Bridges NtGdiDdDDISetDisplayMode -> D3DKMTSetDisplayMode.
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeSetDisplayMode,
                            D3DKMTSetDisplayMode,
                            _In_,
                            CONST D3DKMT_SETDISPLAYMODE)

/*
 * DxgBridgeCreateContext
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeCreateContext,
                            D3DKMTCreateContext,
                            _Inout_,
                            D3DKMT_CREATECONTEXT)

/*
 * DxgBridgeDestroyContext
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeDestroyContext,
                            D3DKMTDestroyContext,
                            _In_,
                            CONST D3DKMT_DESTROYCONTEXT)

/*
 * DxgBridgeCreateSynchronizationObject
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeCreateSynchronizationObject,
                            D3DKMTCreateSynchronizationObject,
                            _Inout_,
                            D3DKMT_CREATESYNCHRONIZATIONOBJECT)

/*
 * DxgBridgeDestroySynchronizationObject
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeDestroySynchronizationObject,
                            D3DKMTDestroySynchronizationObject,
                            _In_,
                            CONST D3DKMT_DESTROYSYNCHRONIZATIONOBJECT)

/*
 * DxgBridgeEscape
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeEscape,
                            D3DKMTEscape,
                            _In_,
                            CONST D3DKMT_ESCAPE)

/*
 * DxgBridgeSetVidPnSourceOwner
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeSetVidPnSourceOwner,
                            D3DKMTSetVidPnSourceOwner,
                            _In_,
                            CONST D3DKMT_SETVIDPNSOURCEOWNER)

/*
 * DxgBridgeGetDeviceState
 */
DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeGetDeviceState,
                            D3DKMTGetDeviceState,
                            _Inout_,
                            D3DKMT_GETDEVICESTATE)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeGetSharedPrimaryHandle,
                            D3DKMTGetSharedPrimaryHandle,
                            _Inout_,
                            D3DKMT_GETSHAREDPRIMARYHANDLE)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeCheckMonitorPowerState,
                            D3DKMTCheckMonitorPowerState,
                            _In_,
                            CONST D3DKMT_CHECKMONITORPOWERSTATE)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeCheckOcclusion,
                            D3DKMTCheckOcclusion,
                            _In_,
                            CONST D3DKMT_CHECKOCCLUSION)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeCreateOverlay,
                            D3DKMTCreateOverlay,
                            _Inout_,
                            D3DKMT_CREATEOVERLAY)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeDestroyOverlay,
                            D3DKMTDestroyOverlay,
                            _In_,
                            CONST D3DKMT_DESTROYOVERLAY)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeFlipOverlay,
                            D3DKMTFlipOverlay,
                            _In_,
                            CONST D3DKMT_FLIPOVERLAY)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeUpdateOverlay,
                            D3DKMTUpdateOverlay,
                            _In_,
                            CONST D3DKMT_UPDATEOVERLAY)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeGetContextSchedulingPriority,
                            D3DKMTGetContextSchedulingPriority,
                            _Inout_,
                            D3DKMT_GETCONTEXTSCHEDULINGPRIORITY)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeSetContextSchedulingPriority,
                            D3DKMTSetContextSchedulingPriority,
                            _In_,
                            CONST D3DKMT_SETCONTEXTSCHEDULINGPRIORITY)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeGetMultisampleMethodList,
                            D3DKMTGetMultisampleMethodList,
                            _Inout_,
                            D3DKMT_GETMULTISAMPLEMETHODLIST)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeGetPresentHistory,
                            D3DKMTGetPresentHistory,
                            _Inout_,
                            D3DKMT_GETPRESENTHISTORY)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeGetRuntimeData,
                            D3DKMTGetRuntimeData,
                            _In_,
                            CONST D3DKMT_GETRUNTIMEDATA)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeGetScanLine,
                            D3DKMTGetScanLine,
                            _Inout_,
                            D3DKMT_GETSCANLINE)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeInvalidateActiveVidPn,
                            D3DKMTInvalidateActiveVidPn,
                            _In_,
                            CONST D3DKMT_INVALIDATEACTIVEVIDPN)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgePollDisplayChildren,
                            D3DKMTPollDisplayChildren,
                            _In_,
                            CONST D3DKMT_POLLDISPLAYCHILDREN)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeQueryAllocationResidency,
                            D3DKMTQueryAllocationResidency,
                            _In_,
                            CONST D3DKMT_QUERYALLOCATIONRESIDENCY)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeQueryStatistics,
                            D3DKMTQueryStatistics,
                            _In_,
                            CONST D3DKMT_QUERYSTATISTICS)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeSetAllocationPriority,
                            D3DKMTSetAllocationPriority,
                            _In_,
                            CONST D3DKMT_SETALLOCATIONPRIORITY)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeSetDisplayPrivateDriverFormat,
                            D3DKMTSetDisplayPrivateDriverFormat,
                            _In_,
                            CONST D3DKMT_SETDISPLAYPRIVATEDRIVERFORMAT)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeSetGammaRamp,
                            D3DKMTSetGammaRamp,
                            _In_,
                            CONST D3DKMT_SETGAMMARAMP)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeSetQueuedLimit,
                            D3DKMTSetQueuedLimit,
                            _In_,
                            CONST D3DKMT_SETQUEUEDLIMIT)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeWaitForIdle,
                            D3DKMTWaitForIdle,
                            _In_,
                            CONST D3DKMT_WAITFORIDLE)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeWaitForVerticalBlankEvent,
                            D3DKMTWaitForVerticalBlankEvent,
                            _In_,
                            CONST D3DKMT_WAITFORVERTICALBLANKEVENT)

/* WDDM 1.2 bridge forwarders */

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeEnumAdapters,
                            D3DKMTEnumAdapters,
                            _Inout_,
                            D3DKMT_ENUMADAPTERS)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeOpenAdapterFromLuid,
                            D3DKMTOpenAdapterFromLuid,
                            _Inout_,
                            D3DKMT_OPENADAPTERFROMLUID)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeOfferAllocations,
                            D3DKMTOfferAllocations,
                            _In_,
                            CONST D3DKMT_OFFERALLOCATIONS)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeReclaimAllocations,
                            D3DKMTReclaimAllocations,
                            _Inout_,
                            D3DKMT_RECLAIMALLOCATIONS)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeSetVidPnSourceOwner1,
                            D3DKMTSetVidPnSourceOwner1,
                            _In_,
                            CONST D3DKMT_SETVIDPNSOURCEOWNER1)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeWaitForVerticalBlankEvent2,
                            D3DKMTWaitForVerticalBlankEvent2,
                            _In_,
                            CONST D3DKMT_WAITFORVERTICALBLANKEVENT2)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeCreateSynchronizationObject2,
                            D3DKMTCreateSynchronizationObject2,
                            _Inout_,
                            D3DKMT_CREATESYNCHRONIZATIONOBJECT2)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeWaitForSynchronizationObject2,
                            D3DKMTWaitForSynchronizationObject2,
                            _In_,
                            CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECT2)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeSignalSynchronizationObject2,
                            D3DKMTSignalSynchronizationObject2,
                            _In_,
                            CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2)

/* WDDM 2.0 bridge forwarders */

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeMakeResident,
                            D3DKMTMakeResident,
                            _Inout_,
                            D3DDDI_MAKERESIDENT)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeEvict,
                            D3DKMTEvict,
                            _Inout_,
                            D3DKMT_EVICT)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeQueryVideoMemoryInfo,
                            D3DKMTQueryVideoMemoryInfo,
                            _Inout_,
                            D3DKMT_QUERYVIDEOMEMORYINFO)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeCreatePagingQueue,
                            D3DKMTCreatePagingQueue,
                            _Inout_,
                            D3DKMT_CREATEPAGINGQUEUE)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeDestroyPagingQueue,
                            D3DKMTDestroyPagingQueue,
                            _Inout_,
                            D3DDDI_DESTROYPAGINGQUEUE)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeReserveGpuVirtualAddress,
                            D3DKMTReserveGpuVirtualAddress,
                            _Inout_,
                            D3DDDI_RESERVEGPUVIRTUALADDRESS)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeMapGpuVirtualAddress,
                            D3DKMTMapGpuVirtualAddress,
                            _Inout_,
                            D3DDDI_MAPGPUVIRTUALADDRESS)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeFreeGpuVirtualAddress,
                            D3DKMTFreeGpuVirtualAddress,
                            _In_,
                            CONST D3DKMT_FREEGPUVIRTUALADDRESS)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeUpdateGpuVirtualAddress,
                            D3DKMTUpdateGpuVirtualAddress,
                            _In_,
                            CONST D3DKMT_UPDATEGPUVIRTUALADDRESS)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeWaitForSynchronizationObjectFromCpu,
                            D3DKMTWaitForSynchronizationObjectFromCpu,
                            _In_,
                            CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeSignalSynchronizationObjectFromCpu,
                            D3DKMTSignalSynchronizationObjectFromCpu,
                            _In_,
                            CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeCreateContextVirtual, D3DKMTCreateContextVirtual, _Inout_, D3DKMT_CREATECONTEXTVIRTUAL)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeSubmitCommand, D3DKMTSubmitCommand, _In_, CONST D3DKMT_SUBMITCOMMAND)

/*
 * DxgBridgeReleaseProcessVidPnSourceOwners
 *
 * Special case: takes HANDLE, not a struct pointer.
 */
NTSTATUS
APIENTRY
DxgBridgeReleaseProcessVidPnSourceOwners(
    _In_ HANDLE hProcess)
{
    if (!hProcess)
        return STATUS_INVALID_PARAMETER;

    DPRINT("DxgBridgeReleaseProcessVidPnSourceOwners: forwarding\n");
    return D3DKMTReleaseProcessVidPnSourceOwners(hProcess);
}

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeQueryResourceInfo,
                            D3DKMTQueryResourceInfo,
                            _Inout_,
                            D3DKMT_QUERYRESOURCEINFO)

DEFINE_DXG_BRIDGE_FORWARDER(DxgBridgeOpenResource,
                            D3DKMTOpenResource,
                            _Inout_,
                            D3DKMT_OPENRESOURCE)

/*
 * WddmBridgeInitCallbacks
 *
 * Populate the private win32k <-> dxgkrnl callback table with all
 * available bridge forwarders.  Every entry that has a corresponding
 * D3DKMT* forwarding function is wired up here.
 */
VOID
WddmBridgeInitCallbacks(
    _Out_ PREACTOS_WIN32K_DXGKRNL_INTERFACE Interface)
{
    NTSTATUS Status;

    if (Interface == NULL)
        return;

    /*
     * The dxgkrnl exchange is a version/readiness handshake.  The callback
     * table installed in win32k must remain the bridge table because NtGdiDdDDI
     * callers pass public D3DKMT handles, while dxgkrnl's direct entry points
     * use kernel-private pointers for several operations.
     */
    Status = WddmBridgeRequireReady();
    if (!NT_SUCCESS(Status))
    {
        RtlZeroMemory(Interface, sizeof(*Interface));
        DPRINT1("WddmBridgeInitCallbacks: bridge unavailable "
                "(status=0x%08lx)\n", Status);
        return;
    }

    RtlZeroMemory(Interface, sizeof(*Interface));

    Interface->RxgkIntPfnPresent = DxgBridgePresent;
    Interface->RxgkIntPfnQueryAdapterInfo = DxgBridgeQueryAdapterInfo;
    Interface->RxgkIntPfnQueryAllocationResidency = DxgBridgeQueryAllocationResidency;
    Interface->RxgkIntPfnQueryStatistics = DxgBridgeQueryStatistics;
    Interface->RxgkIntPfnReleaseProcessVidPnSourceOwners = DxgBridgeReleaseProcessVidPnSourceOwners;
    Interface->RxgkIntPfnRender = DxgBridgeRender;
    Interface->RxgkIntPfnSetContextSchedulingPriority = DxgBridgeSetContextSchedulingPriority;
    Interface->RxgkIntPfnOpenResource = DxgBridgeOpenResource;
    Interface->RxgkIntPfnPollDisplayChildren = DxgBridgePollDisplayChildren;
    Interface->RxgkIntPfnLock = DxgBridgeLock;
    Interface->RxgkIntPfnGetSharedPrimaryHandle = DxgBridgeGetSharedPrimaryHandle;
    Interface->RxgkIntPfnInvalidateActiveVidPn = DxgBridgeInvalidateActiveVidPn;
    Interface->RxgkIntPfnSetAllocationPriority = DxgBridgeSetAllocationPriority;
    Interface->RxgkIntPfnGetPresentHistory = DxgBridgeGetPresentHistory;
    Interface->RxgkIntPfnQueryResourceInfo = DxgBridgeQueryResourceInfo;
    Interface->RxgkIntPfnCreateAllocation = DxgBridgeCreateAllocation;
    Interface->RxgkIntPfnCheckMonitorPowerState = DxgBridgeCheckMonitorPowerState;
    Interface->RxgkIntPfnCheckOcclusion = DxgBridgeCheckOcclusion;
    Interface->RxgkIntPfnCloseAdapter = DxgBridgeCloseAdapter;
    Interface->RxgkIntPfnCreateContext = DxgBridgeCreateContext;
    Interface->RxgkIntPfnCreateDevice = DxgBridgeCreateDevice;
    Interface->RxgkIntPfnCreateOverlay = DxgBridgeCreateOverlay;
    Interface->RxgkIntPfnCreateSynchronizationObject = DxgBridgeCreateSynchronizationObject;
    Interface->RxgkIntPfnDestroyContext = DxgBridgeDestroyContext;
    Interface->RxgkIntPfnDestroyDevice = DxgBridgeDestroyDevice;
    Interface->RxgkIntPfnDestroyOverlay = DxgBridgeDestroyOverlay;
    Interface->RxgkIntPfnDestroySynchronizationObject = DxgBridgeDestroySynchronizationObject;
    Interface->RxgkIntPfnEscape = DxgBridgeEscape;
    Interface->RxgkIntPfnDestroyAllocation = DxgBridgeDestroyAllocation;
    Interface->RxgkIntPfnFlipOverlay = DxgBridgeFlipOverlay;
    Interface->RxgkIntPfnGetContextSchedulingPriority = DxgBridgeGetContextSchedulingPriority;
    Interface->RxgkIntPfnGetDeviceState = DxgBridgeGetDeviceState;
    Interface->RxgkIntPfnGetDisplayModeList = DxgBridgeGetDisplayModeList;
    Interface->RxgkIntPfnGetMultisampleMethodList = DxgBridgeGetMultisampleMethodList;
    Interface->RxgkIntPfnGetRuntimeData = DxgBridgeGetRuntimeData;
    Interface->RxgkIntPfnGetScanLine = DxgBridgeGetScanLine;
    Interface->RxgkIntPfnSignalSynchronizationObject = DxgBridgeSignalSynchronizationObject;
    Interface->RxgkIntPfnWaitForVerticalBlankEvent = DxgBridgeWaitForVerticalBlankEvent;
    Interface->RxgkIntPfnWaitForSynchronizationObject = DxgBridgeWaitForSynchronizationObject;
    Interface->RxgkIntPfnSetVidPnSourceOwner = DxgBridgeSetVidPnSourceOwner;
    Interface->RxgkIntPfnWaitForIdle = DxgBridgeWaitForIdle;
    Interface->RxgkIntPfnUpdateOverlay = DxgBridgeUpdateOverlay;
    Interface->RxgkIntPfnSetQueuedLimit = DxgBridgeSetQueuedLimit;
    Interface->RxgkIntPfnSetGammaRamp = DxgBridgeSetGammaRamp;
    Interface->RxgkIntPfnSetDisplayMode = DxgBridgeSetDisplayMode;
    Interface->RxgkIntPfnSetDisplayPrivateDriverFormat = DxgBridgeSetDisplayPrivateDriverFormat;
    Interface->RxgkIntPfnUnlock = DxgBridgeUnlock;
    Interface->RxgkIntPfnEnumAdapters = DxgBridgeEnumAdapters;
    Interface->RxgkIntPfnOpenAdapterFromLuid = DxgBridgeOpenAdapterFromLuid;
    Interface->RxgkIntPfnOfferAllocations = DxgBridgeOfferAllocations;
    Interface->RxgkIntPfnReclaimAllocations = DxgBridgeReclaimAllocations;
    Interface->RxgkIntPfnSetVidPnSourceOwner1 = DxgBridgeSetVidPnSourceOwner1;
    Interface->RxgkIntPfnWaitForVerticalBlankEvent2 = DxgBridgeWaitForVerticalBlankEvent2;
    Interface->RxgkIntPfnCreateSynchronizationObject2 = DxgBridgeCreateSynchronizationObject2;
    Interface->RxgkIntPfnWaitForSynchronizationObject2 = DxgBridgeWaitForSynchronizationObject2;
    Interface->RxgkIntPfnSignalSynchronizationObject2 = DxgBridgeSignalSynchronizationObject2;

    /* WDDM 2.0 additions */
    Interface->RxgkIntPfnMakeResident = DxgBridgeMakeResident;
    Interface->RxgkIntPfnEvict = DxgBridgeEvict;
    Interface->RxgkIntPfnQueryVideoMemoryInfo = DxgBridgeQueryVideoMemoryInfo;
    Interface->RxgkIntPfnCreatePagingQueue = DxgBridgeCreatePagingQueue;
    Interface->RxgkIntPfnDestroyPagingQueue = DxgBridgeDestroyPagingQueue;
    Interface->RxgkIntPfnReserveGpuVirtualAddress = DxgBridgeReserveGpuVirtualAddress;
    Interface->RxgkIntPfnMapGpuVirtualAddress = DxgBridgeMapGpuVirtualAddress;
    Interface->RxgkIntPfnFreeGpuVirtualAddress = DxgBridgeFreeGpuVirtualAddress;
    Interface->RxgkIntPfnUpdateGpuVirtualAddress = DxgBridgeUpdateGpuVirtualAddress;
    Interface->RxgkIntPfnWaitForSynchronizationObjectFromCpu = DxgBridgeWaitForSynchronizationObjectFromCpu;
    Interface->RxgkIntPfnSignalSynchronizationObjectFromCpu = DxgBridgeSignalSynchronizationObjectFromCpu;

    if (WddmBridgeGetInterfaceVersion() >= DXGKRNL_INTERFACE_VERSION_2)
    {
        Interface->RxgkIntPfnCreateContextVirtual = DxgBridgeCreateContextVirtual;
        Interface->RxgkIntPfnSubmitCommand = DxgBridgeSubmitCommand;
    }
}
