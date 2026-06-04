/*
 * PROJECT:     ReactOS RPi5 Display-Only WDDM Miniport (rpi5dod)
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Internal definitions for the Raspberry Pi 5 display-only
 *              WDDM miniport.
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Overview
 * --------
 * rpi5dod is a WDDM "display only" driver (DOD), structurally derived from
 * the Microsoft KMDOD / Basic Display Driver (BDD) model that already works
 * in this tree (drivers/directx/kmdod).  It registers with dxgkrnl via
 * DxgkInitializeDisplayOnlyDriver and implements DxgkDdiPresentDisplayOnly,
 * the complete-frame blt path that the Desktop Window Manager uses for a
 * tear-free kernel D3DKMTPresent.
 *
 * Unlike kmdod (which PnP-binds to a PCI VGA device, e.g. QEMU bochs-display
 * VEN_1234&DEV_1111), rpi5dod targets REAL Raspberry Pi 5 hardware, where the
 * display is NOT a PCI VGA device.  On the Pi 5 the display is driven by the
 * VideoCore VII HVS (Hardware Video Scaler) scanning out a framebuffer that
 * the UEFI firmware set up via the GOP (Graphics Output Protocol).  rpi5dod
 * therefore binds as a ROOT/platform-enumerated software device and obtains
 * its scanout framebuffer from dxgkrnl's DxgkCbAcquirePostDisplayOwnership
 * (which returns the firmware GOP framebuffer).
 *
 * Present model
 * -------------
 * DWM composes a complete frame into a D3DKMT allocation, then calls
 * D3DKMTPresent.  dxgkrnl copies that allocation into its shadow framebuffer
 * and calls DxgkDdiPresentDisplayOnly here.  We blt the shadow framebuffer
 * into the scanout framebuffer (the GOP/HVS framebuffer).  Because the HVS
 * scans that memory out continuously, the composed frame appears on the HDMI
 * output with no tearing (we blt a complete frame each present).
 *
 * On real hardware the scanout can alternatively be steered by reprogramming
 * the HVS SCALER6 display-list to point at our framebuffer (see rpi5dod_hvs.*
 * and the rpi5vc4 miniport's HVS knowledge).  For the firmware-GOP path the
 * blt-to-GOP-framebuffer model above is sufficient and keeps the firmware's
 * HVS configuration intact.
 */

#ifndef _RPI5DOD_H_
#define _RPI5DOD_H_

#include <ntddk.h>
#include <dispmprt.h>
#include <dderror.h>
#include <devioctl.h>

/* dxgkrnl exports this for display-only miniports. */
EXTERN_C
NTSTATUS
APIENTRY
DxgkInitializeDisplayOnlyDriver(
    _In_ PDRIVER_OBJECT             DriverObject,
    _In_ PUNICODE_STRING            RegistryPath,
    _In_ PKMDDOD_INITIALIZATION_DATA DodInitializationData);

/* ---- Limits ----------------------------------------------------------- */

#define RPI5DOD_MAX_VIEWS       1   /* one display source            */
#define RPI5DOD_MAX_CHILDREN    1   /* one connected target (HDMI0)  */

/* The source surface DWM presents is always 32-bpp BGRX. */
#define RPI5DOD_SOURCE_BPP      32
#define RPI5DOD_SOURCE_BYTES    4

#define RPI5DOD_TAG             'DD5R'  /* "R5DD" */

/* ---- Per-adapter current mode ----------------------------------------- */

typedef struct _RPI5DOD_MODE
{
    /* Geometry / scanout info handed back by AcquirePostDisplayOwnership. */
    DXGK_DISPLAY_INFORMATION    DispInfo;

    /* Kernel VA of the scanout framebuffer (GOP/HVS framebuffer), mapped
     * write-combined from DispInfo.PhysicAddress.  This is the blt target. */
    PVOID                       FrameBufferVa;
    SIZE_T                      FrameBufferLength;

    /* Source mode (what the OS/DWM presents).  Equal to DispInfo geometry
     * for the firmware-GOP path. */
    ULONG                       SrcWidth;
    ULONG                       SrcHeight;

    BOOLEAN                     FrameBufferActive;
    BOOLEAN                     SourceVisible;
} RPI5DOD_MODE, *PRPI5DOD_MODE;

/* ---- Per-adapter device context --------------------------------------- */

typedef struct _RPI5DOD_DEVICE
{
    PDEVICE_OBJECT          PhysicalDeviceObject;
    DXGKRNL_INTERFACE       DxgkInterface;
    DXGK_DEVICE_INFO        DeviceInfo;

    DEVICE_POWER_STATE      PowerState;
    BOOLEAN                 Started;

    RPI5DOD_MODE            Mode[RPI5DOD_MAX_VIEWS];

    /* Monitor child status (HDMI hotplug is reported as always-connected for
     * the firmware-GOP path; real HVS/HDMI hotplug can refine this later). */
    BOOLEAN                 ChildConnected;
} RPI5DOD_DEVICE, *PRPI5DOD_DEVICE;

/* ======================================================================
 * DDI entry points (rpi5dod_ddi.c)
 * ====================================================================== */

DRIVER_INITIALIZE DriverEntry;

NTSTATUS APIENTRY Rpi5DodAddDevice(_In_ DEVICE_OBJECT *PhysicalDeviceObject,
                                   _Outptr_ PVOID *DeviceContext);
NTSTATUS APIENTRY Rpi5DodStartDevice(_In_ VOID *DeviceContext,
                                     _In_ DXGK_START_INFO *DxgkStartInfo,
                                     _In_ DXGKRNL_INTERFACE *DxgkInterface,
                                     _Out_ ULONG *NumberOfViews,
                                     _Out_ ULONG *NumberOfChildren);
NTSTATUS APIENTRY Rpi5DodStopDevice(_In_ VOID *DeviceContext);
NTSTATUS APIENTRY Rpi5DodRemoveDevice(_In_ VOID *DeviceContext);
NTSTATUS APIENTRY Rpi5DodDispatchIoRequest(_In_ VOID *DeviceContext, _In_ ULONG VidPnSourceId,
                                           _In_ VIDEO_REQUEST_PACKET *VideoRequestPacket);
NTSTATUS APIENTRY Rpi5DodSetPowerState(_In_ VOID *DeviceContext, _In_ ULONG HardwareUid,
                                       _In_ DEVICE_POWER_STATE DevicePowerState,
                                       _In_ POWER_ACTION ActionType);
NTSTATUS APIENTRY Rpi5DodQueryChildRelations(_In_ VOID *DeviceContext,
                                             _Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR *ChildRelations,
                                             _In_ ULONG ChildRelationsSize);
NTSTATUS APIENTRY Rpi5DodQueryChildStatus(_In_ VOID *DeviceContext,
                                          _Inout_ DXGK_CHILD_STATUS *ChildStatus,
                                          _In_ BOOLEAN NonDestructiveOnly);
NTSTATUS APIENTRY Rpi5DodQueryDeviceDescriptor(_In_ VOID *DeviceContext, _In_ ULONG ChildUid,
                                               _Inout_ DXGK_DEVICE_DESCRIPTOR *DeviceDescriptor);
BOOLEAN  APIENTRY Rpi5DodInterruptRoutine(_In_ VOID *DeviceContext, _In_ ULONG MessageNumber);
VOID     APIENTRY Rpi5DodDpcRoutine(_In_ VOID *DeviceContext);
VOID     APIENTRY Rpi5DodResetDevice(_In_ VOID *DeviceContext);
VOID     APIENTRY Rpi5DodUnload(VOID);
NTSTATUS APIENTRY Rpi5DodQueryAdapterInfo(_In_ VOID *DeviceContext,
                                          _In_ CONST DXGKARG_QUERYADAPTERINFO *QueryAdapterInfo);
NTSTATUS APIENTRY Rpi5DodSetPointerPosition(_In_ VOID *DeviceContext,
                                            _In_ CONST DXGKARG_SETPOINTERPOSITION *SetPointerPosition);
NTSTATUS APIENTRY Rpi5DodSetPointerShape(_In_ VOID *DeviceContext,
                                         _In_ CONST DXGKARG_SETPOINTERSHAPE *SetPointerShape);
NTSTATUS APIENTRY Rpi5DodPresentDisplayOnly(_In_ CONST HANDLE hAdapter,
                                            _In_ CONST DXGKARG_PRESENT_DISPLAYONLY *PresentDisplayOnly);
NTSTATUS APIENTRY Rpi5DodStopDeviceAndReleasePostDisplayOwnership(_In_ VOID *DeviceContext,
                                                                 _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                                                 _Out_ DXGK_DISPLAY_INFORMATION *DisplayInfo);
NTSTATUS APIENTRY Rpi5DodSystemDisplayEnable(_In_ VOID *DeviceContext,
                                             _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                             _In_ PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
                                             _Out_ UINT *Width, _Out_ UINT *Height,
                                             _Out_ D3DDDIFORMAT *ColorFormat);
VOID     APIENTRY Rpi5DodSystemDisplayWrite(_In_ VOID *DeviceContext, _In_ VOID *Source,
                                            _In_ UINT SourceWidth, _In_ UINT SourceHeight,
                                            _In_ UINT SourceStride, _In_ UINT PositionX,
                                            _In_ UINT PositionY);

/* VidPN mode-set DDIs (rpi5dod_vidpn.c) */
NTSTATUS APIENTRY Rpi5DodIsSupportedVidPn(_In_ VOID *DeviceContext,
                                          _Inout_ DXGKARG_ISSUPPORTEDVIDPN *IsSupportedVidPn);
NTSTATUS APIENTRY Rpi5DodRecommendFunctionalVidPn(_In_ VOID *DeviceContext,
                                                  _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *CONST RecommendFunctionalVidPn);
NTSTATUS APIENTRY Rpi5DodEnumVidPnCofuncModality(_In_ VOID *DeviceContext,
                                                 _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *CONST EnumCofuncModality);
NTSTATUS APIENTRY Rpi5DodSetVidPnSourceVisibility(_In_ VOID *DeviceContext,
                                                  _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *SetVidPnSourceVisibility);
NTSTATUS APIENTRY Rpi5DodCommitVidPn(_In_ VOID *DeviceContext,
                                     _In_ CONST DXGKARG_COMMITVIDPN *CONST CommitVidPn);
NTSTATUS APIENTRY Rpi5DodUpdateActiveVidPnPresentPath(_In_ VOID *DeviceContext,
                                                      _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *CONST UpdateActiveVidPnPresentPath);
NTSTATUS APIENTRY Rpi5DodRecommendMonitorModes(_In_ VOID *DeviceContext,
                                               _In_ CONST DXGKARG_RECOMMENDMONITORMODES *CONST RecommendMonitorModes);

/* ======================================================================
 * Present blt (rpi5dod_blt.c)
 * ====================================================================== */

VOID
Rpi5DodBltSourceToScanout(
    _In_ PRPI5DOD_MODE              Mode,
    _In_reads_bytes_(SrcPitch * Height) PVOID Source,
    _In_ ULONG                      SrcPitch,
    _In_ ULONG                      NumDirtyRects,
    _In_reads_opt_(NumDirtyRects) CONST RECT *DirtyRects);

#endif /* _RPI5DOD_H_ */
