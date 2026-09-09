/*
 * PROJECT:     ReactOS Raspberry Pi 3 VC4 WDDM miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     BCM2837 VC4 display-engine definitions
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#pragma once

#include "softgpu.h"
#include <reactos/rpi3vc4_umd.h>

#define RPI3VC4_POOL_TAG                  '3cVR'

#define RPI3VC4_HVS_PHYSICAL_BASE         0x3f400000ULL
#define RPI3VC4_HVS_LENGTH                0x00006000UL
#define RPI3VC4_PV2_PHYSICAL_BASE         0x3f807000ULL
#define RPI3VC4_PV2_LENGTH                0x00000100UL
#define RPI3VC4_V3D_PHYSICAL_BASE         0x3fc00000ULL
#define RPI3VC4_V3D_LENGTH                0x00001000UL

#define RPI3VC4_V3D_IDENT0                0x0000UL
#define RPI3VC4_V3D_IDENT1                0x0004UL
#define RPI3VC4_V3D_IDENT2                0x0008UL
#define RPI3VC4_V3D_EXPECTED_IDENT0       0x02443356UL
#define RPI3VC4_V3D_INTCTL                0x0030UL
#define RPI3VC4_V3D_INTENA                0x0034UL
#define RPI3VC4_V3D_INTDIS                0x0038UL
#define RPI3VC4_V3D_INT_FRDONE            (1UL << 0)
#define RPI3VC4_V3D_INT_FLDONE            (1UL << 1)
#define RPI3VC4_V3D_INT_OUTOMEM           (1UL << 2)
#define RPI3VC4_V3D_INTERRUPT_MASK        \
    (RPI3VC4_V3D_INT_FRDONE |             \
     RPI3VC4_V3D_INT_FLDONE |             \
     RPI3VC4_V3D_INT_OUTOMEM)
#define RPI3VC4_V3D_L2CACTL               0x0020UL
#define RPI3VC4_V3D_SLCACTL               0x0024UL
#define RPI3VC4_V3D_L2CCLR                (1UL << 2)
#define RPI3VC4_V3D_SLCACTL_ALL           0x0f0f0f0fUL
#define RPI3VC4_V3D_SLCACTL_TEXTURE       0x0f0f0000UL
#define RPI3VC4_V3D_CT0CS                 0x0100UL
#define RPI3VC4_V3D_CT1CS                 0x0104UL
#define RPI3VC4_V3D_CT0EA                 0x0108UL
#define RPI3VC4_V3D_CT1EA                 0x010cUL
#define RPI3VC4_V3D_CT0CA                 0x0110UL
#define RPI3VC4_V3D_CT1CA                 0x0114UL
#define RPI3VC4_V3D_PCS                   0x0130UL
#define RPI3VC4_V3D_BFC                   0x0134UL
#define RPI3VC4_V3D_RFC                   0x0138UL
#define RPI3VC4_V3D_ERRSTAT               0x0f20UL
#define RPI3VC4_V3D_CTRSTA                (1UL << 15)
#define RPI3VC4_V3D_CTRUN                 (1UL << 5)
#define RPI3VC4_V3D_CTERR                 (1UL << 3)
#define RPI3VC4_V3D_BPOA                  0x0308UL
#define RPI3VC4_V3D_BPOS                  0x030cUL
#define RPI3VC4_V3D_VPMBASE               0x0504UL

#define RPI3VC4_GPU_ADDRESS_MASK          0x3fffffffUL
#define RPI3VC4_GPU_ALIAS_MASK            0xc0000000UL
#define RPI3VC4_HIGHEST_SCANOUT_ADDRESS   0x3fffffffULL

#define RPI3VC4_HVS_DISPCTRL              0x0000UL
#define RPI3VC4_HVS_DISPCTRL_ENABLE       (1UL << 31)
#define RPI3VC4_HVS_DISPLIST1             0x0024UL
#define RPI3VC4_HVS_DISPLACT1             0x0034UL
#define RPI3VC4_HVS_DISPCTRL1             0x0050UL
#define RPI3VC4_HVS_DISPSTAT1             0x0058UL
#define RPI3VC4_HVS_CHANNEL_ENABLE        (1UL << 31)
#define RPI3VC4_HVS_CHANNEL_RESET         (1UL << 30)
#define RPI3VC4_HVS_STATUS_MODE_MASK      (3UL << 30)
#define RPI3VC4_HVS_STATUS_MODE_DISABLED  0UL
#define RPI3VC4_HVS_STATUS_MODE_RUN       (2UL << 30)
#define RPI3VC4_HVS_STATUS_EMPTY          (1UL << 28)
#define RPI3VC4_HVS_STATUS_LINE_MASK      0x00000fffUL
#define RPI3VC4_HVS_DLIST_OFFSET          0x2000UL
#define RPI3VC4_HVS_DLIST_DWORDS          (0x4000UL / sizeof(ULONG))
#define RPI3VC4_HVS_LIST_HEAD_MASK        0x00000fffUL
#define RPI3VC4_HVS_DLIST_PRIVATE_BASE    32UL
#define RPI3VC4_HVS_DLIST_SLOT_STRIDE     64UL
#define RPI3VC4_HVS_DLIST_SLOT_COUNT      8UL
#define RPI3VC4_HVS_DLIST_SLOT(Index)     \
    (RPI3VC4_HVS_DLIST_PRIVATE_BASE +     \
     (Index) * RPI3VC4_HVS_DLIST_SLOT_STRIDE)
#define RPI3VC4_HVS_DLIST_SLOT_DWORDS     \
    RPI3VC4_HVS_DLIST_SLOT_STRIDE
#define RPI3VC4_HVS_FILTER_DWORDS         11UL
#define RPI3VC4_HVS_FILTER_OFFSET         \
    (RPI3VC4_HVS_DLIST_PRIVATE_BASE +     \
     RPI3VC4_HVS_DLIST_SLOT_STRIDE *      \
         RPI3VC4_HVS_DLIST_SLOT_COUNT)
#define RPI3VC4_HVS_LBM_BYTES             (96UL * 1024UL)
#define RPI3VC4_HVS_LBM_ALIGNMENT         32UL

#define RPI3VC4_HVS_CTL0_END              (1UL << 31)
#define RPI3VC4_HVS_CTL0_VALID            (1UL << 30)
#define RPI3VC4_HVS_CTL0_SIZE_SHIFT       24
#define RPI3VC4_HVS_CTL0_SIZE_MASK        0x3fUL
#define RPI3VC4_HVS_CTL0_ORDER_SHIFT      13
#define RPI3VC4_HVS_CTL0_SCL1_SHIFT       8
#define RPI3VC4_HVS_CTL0_SCL0_SHIFT       5
#define RPI3VC4_HVS_CTL0_RGBA_SHIFT       11
#define RPI3VC4_HVS_CTL0_RGBA_ROUND       3UL
#define RPI3VC4_HVS_CTL0_UNITY            (1UL << 4)
#define RPI3VC4_HVS_FORMAT_RGBA8888       7UL
#define RPI3VC4_HVS_FORMAT_NV12           9UL
#define RPI3VC4_HVS_ORDER_ABGR            3UL
#define RPI3VC4_HVS_PLANE_DWORDS          7UL
#define RPI3VC4_HVS_CONTEXT_INIT          0xc0c0c0c0UL

#define RPI3VC4_HVS_SCL_H_PPF_V_PPF       0UL
#define RPI3VC4_HVS_SCL_H_TPZ_V_PPF       1UL
#define RPI3VC4_HVS_SCL_H_PPF_V_TPZ       2UL
#define RPI3VC4_HVS_SCL_H_TPZ_V_TPZ       3UL
#define RPI3VC4_HVS_SCL_H_PPF_V_NONE      4UL
#define RPI3VC4_HVS_SCL_H_NONE_V_PPF      5UL
#define RPI3VC4_HVS_SCL_H_NONE_V_TPZ      6UL
#define RPI3VC4_HVS_SCL_H_TPZ_V_NONE      7UL

#define RPI3VC4_HVS_POS0_ALPHA_SHIFT      24
#define RPI3VC4_HVS_POS0_Y_SHIFT          12
#define RPI3VC4_HVS_POS1_HEIGHT_SHIFT     16
#define RPI3VC4_HVS_POS2_ALPHA_SHIFT      30
#define RPI3VC4_HVS_POS2_ALPHA_PIPELINE   0UL
#define RPI3VC4_HVS_POS2_ALPHA_FIXED      1UL
#define RPI3VC4_HVS_POS2_HEIGHT_SHIFT     16

#define RPI3VC4_HVS_CSC0_LIMITED          0x00f00000UL
#define RPI3VC4_HVS_CSC1_601_LIMITED      0xe73304a8UL
#define RPI3VC4_HVS_CSC2_601_LIMITED      0x00066604UL
#define RPI3VC4_HVS_CSC1_709_LIMITED      0xf27784a8UL
#define RPI3VC4_HVS_CSC2_709_LIMITED      0x00072e1dUL
#define RPI3VC4_HVS_CSC0_FULL             0x00000000UL
#define RPI3VC4_HVS_CSC1_601_FULL         0xea349400UL
#define RPI3VC4_HVS_CSC2_601_FULL         0x00059dc6UL
#define RPI3VC4_HVS_CSC1_709_FULL         0xf4388400UL
#define RPI3VC4_HVS_CSC2_709_FULL         0x00064ddbUL

#define RPI3VC4_HVS_PPF_AGC               (1UL << 30)
#define RPI3VC4_HVS_PPF_SCALE_SHIFT       8
#define RPI3VC4_HVS_PPF_PHASE_MASK        0x0000007fUL
#define RPI3VC4_HVS_TPZ_SCALE_SHIFT       8
#define RPI3VC4_HVS_PPF_KERNEL_MASK       0x3fffUL
#define RPI3VC4_HVS_TPZ_SCALE_MASK        0x001fffffUL
#define RPI3VC4_HVS_PPF_SCALE_MASK        0x0001ffffUL
#define RPI3VC4_HVS_TPZ_RECIP_MASK        0x0000ffffUL

#define RPI3VC4_PV_CONTROL                0x0000UL
#define RPI3VC4_PV_CONTROL_ENABLE         (1UL << 0)
#define RPI3VC4_PV_V_CONTROL              0x0004UL
#define RPI3VC4_PV_V_CONTROL_ENABLE       (1UL << 0)

#define RPI3VC4_CURSOR_PITCH              \
    (SOFTGPU_POINTER_MAX_WIDTH * sizeof(ULONG))
#define RPI3VC4_CURSOR_SIZE               \
    (RPI3VC4_CURSOR_PITCH * SOFTGPU_POINTER_MAX_HEIGHT)
#define RPI3VC4_SCANOUT_BUFFER_COUNT      3UL
#define RPI3VC4_V3D_SUBMIT_RING_SIZE      64UL
#define RPI3VC4_V3D_BIN_OVERFLOW_SLOT_SIZE (512UL * 1024UL)
#define RPI3VC4_V3D_BIN_OVERFLOW_SIZE      (16UL * 1024UL * 1024UL)
#define RPI3VC4_V3D_DMA_WORKSPACE_SIZE     (64UL * 1024UL * 1024UL)
#define RPI3VC4_V3D_DMA_SEGMENT_ID         2UL
#define RPI3VC4_V3D_WORKING_SIZE          (RPI3VC4_V3D_BIN_OVERFLOW_SIZE + RPI3VC4_V3D_DMA_WORKSPACE_SIZE)

#define RPI3VC4_OVERLAY_MAGIC              0x4f334356UL
#define RPI3VC4_OVERLAY_POOL_TAG           'o3VR'

typedef struct _RPI3VC4_OVERLAY RPI3VC4_OVERLAY, *PRPI3VC4_OVERLAY;

typedef struct _RPI3VC4_V3D_SUBMIT
{
    RPI3VC4_DMA_PACKET Packet;
    ULONG Fence;
    ULONG BinnerOverflowSlots;
    ULONG BinLastAddress;
    ULONG RenderLastAddress;
    ULONGLONG BinStartTime100ns;
    ULONGLONG RenderStartTime100ns;
    ULONGLONG BinLastProgressTime100ns;
    ULONGLONG RenderLastProgressTime100ns;
    BOOLEAN BinComplete;
    BOOLEAN RenderStarted;
    BOOLEAN RenderComplete;
} RPI3VC4_V3D_SUBMIT, *PRPI3VC4_V3D_SUBMIT;

typedef struct _RPI3VC4_CONTEXT
{
    PVOID HvsBase;
    PVOID Pv2Base;
    PVOID V3dBase;
    PHYSICAL_ADDRESS HvsPhysical;
    PHYSICAL_ADDRESS Pv2Physical;
    PHYSICAL_ADDRESS V3dPhysical;

    ULONG V3dIdent0;
    ULONG V3dIdent1;
    ULONG V3dIdent2;
    NTSTATUS V3dStatus;
    NTSTATUS V3dRenderTestStatus;
    KMUTEX V3dPowerMutex;
    BOOLEAN V3dReady;
    BOOLEAN V3dPowerOwned;
    BOOLEAN V3dRenderTestRan;
    ULONG V3dRenderTestPixel;
    PSOFTGPU_DEVICE Device;
    KSPIN_LOCK V3dQueueLock;
    RPI3VC4_V3D_SUBMIT V3dSubmitRing[RPI3VC4_V3D_SUBMIT_RING_SIZE];
    ULONG V3dSubmitHead;
    ULONG V3dSubmitTail;
    ULONG V3dBinNext;
    ULONG V3dBinActiveIndex;
    ULONG V3dRenderActiveIndex;
    ULONG V3dInterruptPending;
    ULONG V3dNotifiedFaultFence; /* FenceLock + synchronized publication */
    ULONG V3dBinOverflowUsed;
    ULONG V3dBinOverflowCurrent;
    BOOLEAN V3dBinActive;
    BOOLEAN V3dRenderActive;
    BOOLEAN V3dRecovering;
    PVOID V3dBinOverflow;
    PHYSICAL_ADDRESS V3dBinOverflowPhysical;
    KTIMER V3dPollTimer;
    KDPC V3dPollDpc;
    BOOLEAN V3dPollInitialized;

    ULONG OriginalDisplayList;
    ULONG LastSubmittedDisplayList;
    ULONG BusAlias;
    BOOLEAN DirectScanoutReady;
    BOOLEAN PrivateDisplayListActive;
    BOOLEAN CursorPlaneInstalled;
    PRPI3VC4_OVERLAY Overlay;

    PVOID CursorBuffer;
    PHYSICAL_ADDRESS CursorPhysical;
    ULONG CursorShapeGeneration;

    PVOID ScanoutBuffers;
    PHYSICAL_ADDRESS ScanoutPhysical[RPI3VC4_SCANOUT_BUFFER_COUNT];
    SIZE_T ScanoutSurfaceSize;
    SIZE_T ScanoutBufferStride;
    SIZE_T ScanoutAllocationSize;
    ULONG FrontBufferIndex;
    BOOLEAN FullDamage[RPI3VC4_SCANOUT_BUFFER_COUNT];
    BOOLEAN PendingDamageValid[RPI3VC4_SCANOUT_BUFFER_COUNT];
    RECT PendingDamage[RPI3VC4_SCANOUT_BUFFER_COUNT];

    PHYSICAL_ADDRESS SourcePrimaryPhysical;
    PHYSICAL_ADDRESS PrimaryPhysical;
    ULONG PrimaryPitch;
    ULONG PrimaryWidth;
    ULONG PrimaryHeight;
    BOOLEAN PrimaryConfigured;
    BOOLEAN PrimaryVisible;
} RPI3VC4_CONTEXT, *PRPI3VC4_CONTEXT;

struct _RPI3VC4_OVERLAY
{
    ULONG Magic;
    PRPI3VC4_CONTEXT Context;
    PSOFTGPU_ALLOC Allocation;
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG SegmentId;
    RECT SourceRect;
    RECT DestinationRect;
    ULONG InfoFlags;
    BOOLEAN Enabled;
};

NTSTATUS
Rpi3Vc4QueryPlatform(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ PDXGK_INTERFACE DxgkInterface,
    _Out_ PSOFTGPU_PLATFORM_CONFIG Config);

NTSTATUS
Rpi3Vc4InitializeV3d(
    _Inout_ PRPI3VC4_CONTEXT Context);

NTSTATUS
Rpi3Vc4ReserveV3dMemory(
    _Inout_ PRPI3VC4_CONTEXT Context);

NTSTATUS
Rpi3Vc4EnsureV3dReady(
    _Inout_ PRPI3VC4_CONTEXT Context);

NTSTATUS
Rpi3Vc4OpenAllocation(
    _Inout_ PSOFTGPU_OPENALLOC OpenAllocation);

VOID
Rpi3Vc4CloseAllocation(
    _Inout_ PSOFTGPU_OPENALLOC OpenAllocation);

NTSTATUS
Rpi3Vc4ValidateRender(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ PSOFTGPU_KMD_DEVICE KmdDevice,
    _Inout_ PDXGKARG_RENDER Render);

NTSTATUS
Rpi3Vc4SubmitCommand(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const DXGKARG_SUBMITCOMMAND *SubmitCommand);

BOOLEAN
Rpi3Vc4Interrupt(
    _Inout_ PSOFTGPU_DEVICE Device);

VOID
Rpi3Vc4Dpc(
    _Inout_ PSOFTGPU_DEVICE Device);

VOID
Rpi3Vc4StopV3d(
    _Inout_ PRPI3VC4_CONTEXT Context);

NTSTATUS
Rpi3Vc4StartScanout(
    _Inout_ PSOFTGPU_DEVICE Device);

NTSTATUS
Rpi3Vc4StopScanout(
    _Inout_ PSOFTGPU_DEVICE Device);

NTSTATUS
Rpi3Vc4SetPrimary(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ PHYSICAL_ADDRESS PrimaryAddress,
    _In_ ULONG Pitch,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ BOOLEAN Visible);

NTSTATUS
Rpi3Vc4PresentDisplayOnly(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const DXGKARG_PRESENT_DISPLAYONLY *PresentDisplayOnly);

NTSTATUS
Rpi3Vc4UpdatePointer(
    _Inout_ PSOFTGPU_DEVICE Device);

BOOLEAN
Rpi3Vc4WaitForVerticalBlank(
    _Inout_ PSOFTGPU_DEVICE Device);

NTSTATUS
Rpi3Vc4QueryScanLine(
    _In_ PSOFTGPU_DEVICE Device,
    _Inout_ PDXGKARG_GETSCANLINE GetScanLine);
