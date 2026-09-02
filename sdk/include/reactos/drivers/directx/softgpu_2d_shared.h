/*
 * PROJECT:     ReactOS WDDM software GPU
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Shared user/kernel linear 2D command and allocation ABI
 */

#pragma once

/*
 * Bound firmware-controlled mode geometry before allocating or mapping
 * memory. The geometry envelope is deliberately wider than the allocation
 * working-set limit below; StartDevice accepts a mode only when four
 * page-aligned mode-sized surfaces fit in the contiguous software segment.
 */
#define SOFTGPU_DISPLAY_BYTES_PER_PIXEL 4UL
#define SOFTGPU_DISPLAY_BITS_PER_PIXEL  32UL
#define SOFTGPU_MAX_DISPLAY_WIDTH       8192UL
#define SOFTGPU_MAX_DISPLAY_HEIGHT      8192UL
#define SOFTGPU_MAX_DISPLAY_PITCH       \
    (SOFTGPU_MAX_DISPLAY_WIDTH * SOFTGPU_DISPLAY_BYTES_PER_PIXEL)
#define SOFTGPU_MAX_SURFACE_SIZE        \
    ((SIZE_T)SOFTGPU_MAX_DISPLAY_PITCH * SOFTGPU_MAX_DISPLAY_HEIGHT)

/*
 * A present can need the shared primary, shared shadow, source, and a distinct
 * destination or staging allocation resident at the same time. The software
 * engine addresses only this contiguous segment, so undersizing it would make
 * VidMm system-memory fallback produce commands the engine cannot execute.
 */
#define SOFTGPU_2D_WORKING_SURFACE_COUNT 4UL
#define SOFTGPU_MAX_ALLOCATION_SLAB_SIZE ((SIZE_T)256 * 1024 * 1024)

#define SOFTGPU_D3DDDIFMT_NV12 ((D3DDDIFORMAT)0x3231564eUL)

#define SOFTGPU_ALLOCATION_PRIVATE_MAGIC     0x41504753UL /* 'SGPA' */
#define SOFTGPU_ALLOCATION_PRIVATE_VERSION_1 1UL
#define SOFTGPU_ALLOCATION_PRIVATE_VERSION_2 2UL
#define SOFTGPU_ALLOCATION_PRIVATE_VERSION   3UL
#define SOFTGPU_ALLOCATION_MAX_PLANES        3UL

/*
 * Width, Height, and BitsPerPixel are intentionally the first three UINTs.
 * They are the generic compact-allocation prefix consumed by dxgkrnl before
 * the private record reaches the miniport. StorageHeight describes padding
 * between the visible luma rows and any following planar data. PlaneOffsets
 * and PlanePitches make the byte layout explicit for decoder and display
 * consumers; the miniport remains authoritative for the final validated
 * allocation size.
 */
typedef struct _SOFTGPU_ALLOCATION_PRIVATE_DATA
{
    ULONG        Width;
    ULONG        Height;
    ULONG        BitsPerPixel;
    ULONG        Magic;
    ULONG        Version;
    ULONG        Pitch;
    D3DDDIFORMAT Format;
    ULONG        StorageHeight;
    ULONG        PlaneCount;
    ULONG        PlaneOffsets[SOFTGPU_ALLOCATION_MAX_PLANES];
    ULONG        PlanePitches[SOFTGPU_ALLOCATION_MAX_PLANES];
} SOFTGPU_ALLOCATION_PRIVATE_DATA, *PSOFTGPU_ALLOCATION_PRIVATE_DATA;

/* Versions 1 and 2 ended before StorageHeight and PlaneCount, respectively.
 * Their prefixes remain accepted so allocations created by an older UMD can
 * still be opened while user and kernel display components are upgraded. */
#define SOFTGPU_ALLOCATION_PRIVATE_VERSION_1_SIZE \
    FIELD_OFFSET(SOFTGPU_ALLOCATION_PRIVATE_DATA, StorageHeight)
#define SOFTGPU_ALLOCATION_PRIVATE_VERSION_2_SIZE \
    FIELD_OFFSET(SOFTGPU_ALLOCATION_PRIVATE_DATA, PlaneCount)

/*
 * Overlay state passed from the generic UMD to the display miniport.  Keep
 * this record independent of a particular board: the allocation itself owns
 * the format and plane layout, while this record carries only presentation
 * policy that is not represented by DXGK_OVERLAYINFO.
 */
#define SOFTGPU_OVERLAY_PRIVATE_MAGIC   0x4f504753UL /* 'SGPO' */
#define SOFTGPU_OVERLAY_PRIVATE_VERSION 1UL

#define SOFTGPU_OVERLAY_INFO_LIMITED_RGB 0x00000200UL
#define SOFTGPU_OVERLAY_INFO_BT709       0x00000400UL
#define SOFTGPU_OVERLAY_INFO_ALLOWED \
    (SOFTGPU_OVERLAY_INFO_LIMITED_RGB | SOFTGPU_OVERLAY_INFO_BT709)

typedef struct _SOFTGPU_OVERLAY_PRIVATE_DATA
{
    ULONG Magic;
    ULONG Version;
    ULONG InfoFlags;
    ULONG FlipFlags;
} SOFTGPU_OVERLAY_PRIVATE_DATA, *PSOFTGPU_OVERLAY_PRIVATE_DATA;

/*
 * Every submitted DMA buffer is a sequence of these fixed-size records.
 * Address fields are zero in the UMD stream and are patched by the KMD after
 * dxgkrnl has resolved and pinned each referenced allocation.
 */
#define SOFTGPU_CMD_MAGIC       0x444D4753UL /* 'SGMD' */

#define SOFTGPU_CMD_OP_NOP          1UL
#define SOFTGPU_CMD_OP_BLT          2UL
#define SOFTGPU_CMD_OP_FILL         3UL
#define SOFTGPU_CMD_OP_PAGE         4UL
#define SOFTGPU_CMD_OP_FILL_LINEAR  5UL
#define SOFTGPU_CMD_OP_SIGNAL_FENCE 6UL
#define SOFTGPU_CMD_OP_WAIT_FENCE   7UL

#define SOFTGPU_CMD_FLAG_TO_SLAB    0x00000001UL

typedef struct _SOFTGPU_CMD
{
    ULONG       Magic;
    ULONG       Op;
    ULONG       Size;
    ULONG       Color;
    RECT        SrcRect;
    RECT        DstRect;
    ULONG       SrcPitch;
    ULONG       DstPitch;
    ULONGLONG   SrcAddress;
    ULONGLONG   DstAddress;
    ULONGLONG   SlabAddress;
    ULONGLONG   SystemAddress;
    ULONGLONG   ByteCount;
    ULONG       Flags;
    ULONG       Reserved;
    ULONGLONG   FenceGpuVa;
    ULONGLONG   FenceValue;
} SOFTGPU_CMD, *PSOFTGPU_CMD;
