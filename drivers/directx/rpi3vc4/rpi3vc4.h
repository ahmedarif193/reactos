/*
 * PROJECT:     ReactOS Raspberry Pi 3 VC4 WDDM miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     BCM2837 VC4 display-engine definitions
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#pragma once

#include "softgpu.h"

#define RPI3VC4_POOL_TAG                  '3cVR'

#define RPI3VC4_HVS_PHYSICAL_BASE         0x3f400000ULL
#define RPI3VC4_HVS_LENGTH                0x00006000UL
#define RPI3VC4_PV2_PHYSICAL_BASE         0x3f807000ULL
#define RPI3VC4_PV2_LENGTH                0x00000100UL

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
#define RPI3VC4_HVS_DLIST_SLOT_STRIDE     16UL
#define RPI3VC4_HVS_DLIST_SLOT_COUNT      8UL
#define RPI3VC4_HVS_DLIST_SLOT(Index)     \
    (RPI3VC4_HVS_DLIST_PRIVATE_BASE +     \
     (Index) * RPI3VC4_HVS_DLIST_SLOT_STRIDE)
#define RPI3VC4_HVS_DLIST_SLOT_DWORDS     16UL

#define RPI3VC4_HVS_CTL0_END              (1UL << 31)
#define RPI3VC4_HVS_CTL0_VALID            (1UL << 30)
#define RPI3VC4_HVS_CTL0_SIZE_SHIFT       24
#define RPI3VC4_HVS_CTL0_SIZE_MASK        0x3fUL
#define RPI3VC4_HVS_CTL0_ORDER_SHIFT      13
#define RPI3VC4_HVS_CTL0_RGBA_SHIFT       11
#define RPI3VC4_HVS_CTL0_RGBA_ROUND       3UL
#define RPI3VC4_HVS_CTL0_UNITY            (1UL << 4)
#define RPI3VC4_HVS_FORMAT_RGBA8888       7UL
#define RPI3VC4_HVS_ORDER_ABGR            3UL
#define RPI3VC4_HVS_PLANE_DWORDS          7UL
#define RPI3VC4_HVS_CURSOR_PLANE_OFFSET   RPI3VC4_HVS_PLANE_DWORDS
#define RPI3VC4_HVS_CONTEXT_INIT          0xc0c0c0c0UL

#define RPI3VC4_HVS_POS0_ALPHA_SHIFT      24
#define RPI3VC4_HVS_POS0_Y_SHIFT          12
#define RPI3VC4_HVS_POS2_ALPHA_SHIFT      30
#define RPI3VC4_HVS_POS2_ALPHA_PIPELINE   0UL
#define RPI3VC4_HVS_POS2_ALPHA_FIXED      1UL
#define RPI3VC4_HVS_POS2_HEIGHT_SHIFT     16

#define RPI3VC4_PV_CONTROL                0x0000UL
#define RPI3VC4_PV_CONTROL_ENABLE         (1UL << 0)
#define RPI3VC4_PV_V_CONTROL              0x0004UL
#define RPI3VC4_PV_V_CONTROL_ENABLE       (1UL << 0)

#define RPI3VC4_CURSOR_PITCH              \
    (SOFTGPU_POINTER_MAX_WIDTH * sizeof(ULONG))
#define RPI3VC4_CURSOR_SIZE               \
    (RPI3VC4_CURSOR_PITCH * SOFTGPU_POINTER_MAX_HEIGHT)
#define RPI3VC4_SCANOUT_BUFFER_COUNT      3UL

typedef struct _RPI3VC4_CONTEXT
{
    PVOID HvsBase;
    PVOID Pv2Base;
    PHYSICAL_ADDRESS HvsPhysical;
    PHYSICAL_ADDRESS Pv2Physical;

    ULONG OriginalDisplayList;
    ULONG LastSubmittedDisplayList;
    ULONG BusAlias;
    BOOLEAN DirectScanoutReady;
    BOOLEAN PrivateDisplayListActive;
    BOOLEAN CursorPlaneInstalled;

    PVOID CursorBuffer;
    PHYSICAL_ADDRESS CursorPhysical;
    ULONG CursorShapeGeneration;

    PVOID ScanoutBuffers;
    PHYSICAL_ADDRESS ScanoutPhysical[RPI3VC4_SCANOUT_BUFFER_COUNT];
    SIZE_T ScanoutSurfaceSize;
    SIZE_T ScanoutBufferStride;
    SIZE_T ScanoutAllocationSize;
    PVOID PresentShadow;
    SIZE_T PresentShadowSize;
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

NTSTATUS
Rpi3Vc4QueryPlatform(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ PDXGK_INTERFACE DxgkInterface,
    _Out_ PSOFTGPU_PLATFORM_CONFIG Config);

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
