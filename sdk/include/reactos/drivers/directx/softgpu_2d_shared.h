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

#define SOFTGPU_ALLOCATION_PRIVATE_MAGIC   0x41504753UL /* 'SGPA' */
#define SOFTGPU_ALLOCATION_PRIVATE_VERSION 1UL

/*
 * Width, Height, and BitsPerPixel are intentionally the first three UINTs.
 * They are the generic compact-allocation prefix consumed by dxgkrnl before
 * the private record reaches the miniport. Keep this record below 32 bytes
 * unless that public prefix contract changes with it.
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
} SOFTGPU_ALLOCATION_PRIVATE_DATA, *PSOFTGPU_ALLOCATION_PRIVATE_DATA;

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
