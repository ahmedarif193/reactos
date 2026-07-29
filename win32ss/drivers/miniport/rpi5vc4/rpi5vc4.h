/*
 * PROJECT:     ReactOS Raspberry Pi 5 framebuffer miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared declarations for the Raspberry Pi 5 framebuffer miniport.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#ifndef _RPI5VC4_PCH_
#define _RPI5VC4_PCH_

#include <ntddk.h>
#include <dderror.h>
#define __BROKEN__
#include <miniport.h>
#undef __BROKEN__
#include <video.h>
#include <devioctl.h>
#include <reactos/rpi5vc4_xpdm.h>

#define RPI5VC4_CURSOR_WIDTH 64
#define RPI5VC4_CURSOR_HEIGHT 64
#define RPI5VC4_ACPI_MEMORY_RESOURCE_COUNT 10

typedef struct _LOADER_PARAMETER_FRAMEBUFFER
{
    LARGE_INTEGER FrameBufferBase;
    ULONG FrameBufferSize;
    ULONG HorizontalResolution;
    ULONG VerticalResolution;
    ULONG PixelsPerScanLine;
    ULONG PixelFormat;
    ULONG RedMask;
    ULONG GreenMask;
    ULONG BlueMask;
    ULONG Reserved;
    ULONG Dpi;
} LOADER_PARAMETER_FRAMEBUFFER, *PLOADER_PARAMETER_FRAMEBUFFER;

typedef struct _RPI5VC4_V3D_GRAPH_LEVEL_STATE
{
    ULONG Width;
    ULONG Height;
    ULONG BaseOffset;
    ULONG Stride;
    ULONG PaddedHeight;
    ULONG UbPad;
    ULONG Tiling;
} RPI5VC4_V3D_GRAPH_LEVEL_STATE,
  *PRPI5VC4_V3D_GRAPH_LEVEL_STATE;

typedef struct _RPI5VC4_V3D_GRAPH_RESOURCE_STATE
{
    ULONG Width;
    ULONG Height;
    ULONG Format;
    ULONG Flags;
    ULONG LevelCount;
    ULONG BaseOffset;
    ULONG StorageBytes;
    ULONG Stride;
    ULONG PaddedHeight;
    ULONG UbPad;
    ULONG Tiling;
    RPI5VC4_V3D_GRAPH_LEVEL_STATE
        Levels[RPI5VC4_V3D_TEXTURE_MAX_LEVELS];
} RPI5VC4_V3D_GRAPH_RESOURCE_STATE,
  *PRPI5VC4_V3D_GRAPH_RESOURCE_STATE;

typedef struct _RPI5VC4_DEVICE_EXTENSION
{
    VIDEO_ACCESS_RANGE V3dHubRange;
    VIDEO_ACCESS_RANGE V3dCoreRange;
    VIDEO_ACCESS_RANGE V3dSmsRange;
    VIDEO_ACCESS_RANGE HvsRange;
    VIDEO_ACCESS_RANGE PixelValveRange[2];

    PHYSICAL_ADDRESS FirmwareFrameBufferPhysical;
    PHYSICAL_ADDRESS FrameBufferPhysical;
    PVOID FrameBufferVirtual;
    PVOID HeadlessBuffer;
    ULONG FrameBufferSize;
    ULONG ScreenWidth;
    ULONG ScreenHeight;
    ULONG PixelsPerScanLine;
    ULONG BitsPerPixel;
    ULONG BytesPerScanLine;
    ULONG RedMask;
    ULONG GreenMask;
    ULONG BlueMask;
    ULONG ReservedMask;
    VIDEO_MODE_INFORMATION ModeInfo;
    PVOID MappedFrameBuffer;
    ULONG CurrentMode;

    /* Cached MMIO mapping of the HVS register block (mapped once, reused). */
    PVOID HvsBase;

    /* Read-only V3D discovery state; submission remains disabled. */
    PVOID V3dSmsBase;
    PVOID V3dHubBase;
    PVOID V3dCoreBase;
    ULONG V3dFlags;
    ULONG V3dVersion;
    ULONG V3dCoreCount;
    ULONG V3dSmsReeCs;
    ULONG V3dSmsTeeCs;
    ULONG V3dHubIdent[4];
    ULONG V3dCoreIdent[3];
    ULONG V3dMmuDebugInfo;

    /* GPU-addressable buffers owned by this ACPI GPU0 miniport instance. */
    PVP_DMA_ADAPTER V3dDmaAdapter;
    PVOID V3dPageTableVa;
    PHYSICAL_ADDRESS V3dPageTableLogical;
    PVOID V3dWorkVa;
    PHYSICAL_ADDRESS V3dWorkLogical;
    PMDL V3dWorkControlMdl;
    PMDL V3dWorkOutputMdl;
    PMDL V3dWorkVertexMdl;
    PMDL V3dWorkTerrainTransientMdl;
    PMDL V3dWorkTextureMdl;
    PMDL V3dWorkGdiTextureMdl;
    KEVENT V3dCompletionEvent;
    LONG V3dExecutionBusy;
    BOOLEAN V3dExecutionPoisoned;
    BOOLEAN V3dPageTableReady;
    BOOLEAN V3dMmuReady;
    BOOLEAN V3dDirectPresentReady;
    BOOLEAN V3dWorkCached;
    BOOLEAN V3dInterruptAvailable;
    BOOLEAN V3dInterruptValidated;
    ULONG V3dFrameBufferGpuVa;
    ULONG V3dTextureGeneration;
    ULONG V3dTextureWidth;
    ULONG V3dTextureHeight;
    ULONG V3dTextureFormat;
    ULONG V3dTextureLevelCount;
    ULONG V3dTextureLevel0Offset;
    ULONG V3dTextureLevel0UbPad;
    BOOLEAN V3dTextureLevel0StrictUif;
    BOOLEAN V3dTextureLevel0Xor;
    ULONG V3dTexture1Generation;
    ULONG V3dTexture1Width;
    ULONG V3dTexture1Height;
    ULONG V3dTexture1Format;
    ULONG V3dTexture1LevelCount;
    ULONG V3dTexture1Level0Offset;
    ULONG V3dTexture1Level0UbPad;
    BOOLEAN V3dTexture1Level0StrictUif;
    BOOLEAN V3dTexture1Level0Xor;
    ULONG V3dGraphCacheId;
    ULONG V3dGraphResourceCount;
    ULONG V3dGraphReadbackResource;
    ULONG V3dGraphStorageBytes;
    ULONG V3dGraphTerrainCacheId;
    ULONG V3dGraphTerrainGpuCacheId;
    PRPI5VC4_V3D_TERRAIN_VERTEX V3dGraphTerrainVertices;
    RPI5VC4_V3D_GRAPH_RESOURCE_STATE
        V3dGraphResources[RPI5VC4_V3D_GRAPH_MAX_RESOURCES];

    /* Cached MMIO mapping of the active PixelValve (mapped once, reused). */
    PVOID PixelValveBase;

    /* Validated cursor-plane location for the low-MMIO cursor move fast path. */
    ULONG HvsLptrsReg;
    ULONG HvsLptrsVal;
    ULONG HvsCursorHead;
    BOOLEAN HvsCursorFastValid;

    BOOLEAN PixelValveValid;
    PHYSICAL_ADDRESS PixelValvePhysical;
    ULONG PixelValveLength;
    ULONG PixelValveControl;
    ULONG PixelValveVControl;
    ULONG PixelValveVsyncEven;
    ULONG PixelValveHorzA;
    ULONG PixelValveHorzB;
    ULONG PixelValveVertA;
    ULONG PixelValveVertB;
    ULONG PixelValveHactAct;

    /* Hardware cursor composited by the HVS as a second display-list plane. */
    PVOID CursorVa;                   /* CPU mapping of the cursor surface      */
    PHYSICAL_ADDRESS CursorPhys;      /* physical base the HVS scans the cursor */
    ULONG CursorWidth;
    ULONG CursorHeight;
    LONG CursorX;                     /* top-left, screen coordinates          */
    LONG CursorY;
    BOOLEAN CursorVisible;
    BOOLEAN CursorShapeValid;
} RPI5VC4_DEVICE_EXTENSION, *PRPI5VC4_DEVICE_EXTENSION;

BOOLEAN
NTAPI
InbvGetGopFrameBufferInfo(
    _Out_ PLOADER_PARAMETER_FRAMEBUFFER FrameBufferInfo);

VP_STATUS
NTAPI
Rpi5Vc4FindAdapter(
    _In_ PVOID HwDeviceExtension,
    _In_ PVOID HwContext,
    _In_ PWSTR ArgumentString,
    _Inout_ PVIDEO_PORT_CONFIG_INFO ConfigInfo,
    _Out_ PUCHAR Again);

BOOLEAN
NTAPI
Rpi5Vc4Initialize(
    _In_ PVOID HwDeviceExtension);

BOOLEAN
NTAPI
Rpi5Vc4StartIO(
    _In_ PVOID HwDeviceExtension,
    _In_ PVIDEO_REQUEST_PACKET RequestPacket);

VP_STATUS
Rpi5Vc4QueryPlatformInfo(
    _Out_ PRPI5VC4_PLATFORM_INFO PlatformInfo);

BOOLEAN
NTAPI
Rpi5Vc4ResetHw(
    _In_ PVOID DeviceExtension,
    _In_ ULONG Columns,
    _In_ ULONG Rows);

VP_STATUS
NTAPI
Rpi5Vc4GetPowerState(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG HwId,
    _In_ PVIDEO_POWER_MANAGEMENT VideoPowerControl);

VP_STATUS
NTAPI
Rpi5Vc4SetPowerState(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG HwId,
    _In_ PVIDEO_POWER_MANAGEMENT VideoPowerControl);

VP_STATUS
NTAPI
Rpi5Vc4GetVideoChildDescriptor(
    _In_ PVOID HwDeviceExtension,
    _In_ PVIDEO_CHILD_ENUM_INFO ChildEnumInfo,
    _Out_ PVIDEO_CHILD_TYPE VideoChildType,
    _Out_writes_bytes_(ChildEnumInfo->ChildDescriptorSize) PUCHAR ChildDescriptor,
    _Out_ PULONG UId,
    _Out_ PULONG Unused);

#endif /* _RPI5VC4_PCH_ */
