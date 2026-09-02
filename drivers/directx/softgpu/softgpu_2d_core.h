/*
 * PROJECT:     ReactOS WDDM software GPU miniport
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Pure validation and linear 2D copy/fill policy
 */

#pragma once

#include <ntddk.h>
#include <windef.h>
#include <d3dkmddi.h>

#include "softgpu_2d_contract.h"

typedef enum _SOFTGPU_2D_PRESENT_POLICY
{
    SoftGpu2dPresentInvalid = 0,
    SoftGpu2dPresentNoOp,
    SoftGpu2dPresentBlt,
    SoftGpu2dPresentFill
} SOFTGPU_2D_PRESENT_POLICY;

FORCEINLINE NTSTATUS
SoftGpu2dComputeAllocationSlabSize(
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG SurfaceCount,
    _In_ SIZE_T MaximumSlabSize,
    _Out_ PSIZE_T SlabSize)
{
    ULONGLONG SurfaceSize;
    ULONGLONG AlignedSurfaceSize;
    ULONGLONG RequiredSize;

    if (SlabSize == NULL)
        return STATUS_INVALID_PARAMETER;

    *SlabSize = 0;
    if (Width == 0 ||
        Height == 0 ||
        SurfaceCount == 0 ||
        MaximumSlabSize == 0 ||
        Width > SOFTGPU_MAX_DISPLAY_WIDTH ||
        Height > SOFTGPU_MAX_DISPLAY_HEIGHT ||
        Width > MAXULONG / SOFTGPU_DISPLAY_BYTES_PER_PIXEL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    SurfaceSize =
        (ULONGLONG)Width *
        SOFTGPU_DISPLAY_BYTES_PER_PIXEL *
        Height;
    if (SurfaceSize == 0 ||
        SurfaceSize > (ULONGLONG)SOFTGPU_MAX_SURFACE_SIZE ||
        SurfaceSize > MAXULONGLONG - (PAGE_SIZE - 1))
    {
        return STATUS_GRAPHICS_NO_VIDEO_MEMORY;
    }

    AlignedSurfaceSize =
        (SurfaceSize + PAGE_SIZE - 1) &
        ~((ULONGLONG)PAGE_SIZE - 1);
    if (AlignedSurfaceSize >
        MAXULONGLONG / SurfaceCount)
    {
        return STATUS_INTEGER_OVERFLOW;
    }
    RequiredSize = AlignedSurfaceSize * SurfaceCount;
    if (RequiredSize > (ULONGLONG)MaximumSlabSize ||
        RequiredSize > (ULONGLONG)MAXULONG_PTR)
    {
        return STATUS_GRAPHICS_NO_VIDEO_MEMORY;
    }

    *SlabSize = (SIZE_T)RequiredSize;
    return STATUS_SUCCESS;
}

FORCEINLINE NTSTATUS
SoftGpu2dResolveSlabRange(
    _In_ ULONGLONG SlabBase,
    _In_ SIZE_T SlabSize,
    _In_ ULONGLONG Address,
    _In_ ULONGLONG Length,
    _Out_ PSIZE_T Offset)
{
    ULONGLONG RangeOffset;

    if (Offset == NULL)
        return STATUS_INVALID_PARAMETER;

    *Offset = 0;
    if (SlabBase == 0 || SlabSize == 0 || Length == 0)
        return STATUS_INVALID_PARAMETER;
    if (Address < SlabBase)
        return STATUS_INVALID_ADDRESS;

    RangeOffset = Address - SlabBase;
    if (RangeOffset > (ULONGLONG)SlabSize ||
        Length > (ULONGLONG)SlabSize - RangeOffset ||
        RangeOffset > (ULONGLONG)MAXULONG_PTR)
    {
        return STATUS_INVALID_ADDRESS;
    }

    *Offset = (SIZE_T)RangeOffset;
    return STATUS_SUCCESS;
}

FORCEINLINE SOFTGPU_2D_PRESENT_POLICY
SoftGpu2dPresentEvaluate(
    _In_ BOOLEAN Flip,
    _In_ BOOLEAN Blt,
    _In_ BOOLEAN ColorFill,
    _In_ UINT SourceAllocationCount,
    _In_ UINT DestinationAllocationCount)
{
    if (Flip)
    {
        return !Blt &&
               !ColorFill &&
               SourceAllocationCount == 1 &&
               DestinationAllocationCount == 0
                   ? SoftGpu2dPresentNoOp
                   : SoftGpu2dPresentInvalid;
    }
    if (Blt)
    {
        if (ColorFill ||
            SourceAllocationCount != 1 ||
            DestinationAllocationCount > 1)
        {
            return SoftGpu2dPresentInvalid;
        }
        return DestinationAllocationCount == 1
                   ? SoftGpu2dPresentBlt
                   : SoftGpu2dPresentNoOp;
    }
    if (ColorFill)
    {
        return SourceAllocationCount == 0 &&
               DestinationAllocationCount == 1
                   ? SoftGpu2dPresentFill
                   : SoftGpu2dPresentInvalid;
    }
    return SoftGpu2dPresentInvalid;
}

FORCEINLINE BOOLEAN
SoftGpuAllocationPrivateDataSize(
    _In_ const SOFTGPU_ALLOCATION_PRIVATE_DATA *PrivateData,
    _Out_ PULONGLONG RequiredSize)
{
    ULONGLONG Size;
    ULONG Plane;

    if (PrivateData == NULL || RequiredSize == NULL ||
        PrivateData->Magic != SOFTGPU_ALLOCATION_PRIVATE_MAGIC ||
        PrivateData->Version != SOFTGPU_ALLOCATION_PRIVATE_VERSION ||
        PrivateData->Width == 0 ||
        PrivateData->Height == 0 || PrivateData->Pitch == 0 ||
        PrivateData->PlaneCount == 0 ||
        PrivateData->PlaneCount > SOFTGPU_ALLOCATION_MAX_PLANES)
    {
        return FALSE;
    }

    for (Plane = PrivateData->PlaneCount;
         Plane < SOFTGPU_ALLOCATION_MAX_PLANES;
         ++Plane)
    {
        if (PrivateData->PlaneOffsets[Plane] != 0 ||
            PrivateData->PlanePitches[Plane] != 0)
        {
            return FALSE;
        }
    }

    if (PrivateData->Format == D3DDDIFMT_X8R8G8B8 ||
        PrivateData->Format == D3DDDIFMT_A8R8G8B8)
    {
        if (PrivateData->BitsPerPixel != SOFTGPU_DISPLAY_BITS_PER_PIXEL ||
            PrivateData->Width > SOFTGPU_MAX_DISPLAY_WIDTH ||
            PrivateData->Height > SOFTGPU_MAX_DISPLAY_HEIGHT ||
            PrivateData->Width >
                MAXULONG / SOFTGPU_DISPLAY_BYTES_PER_PIXEL ||
            PrivateData->Pitch <
                PrivateData->Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL ||
            PrivateData->Pitch > SOFTGPU_MAX_DISPLAY_PITCH ||
            (PrivateData->Pitch % SOFTGPU_DISPLAY_BYTES_PER_PIXEL) != 0 ||
            PrivateData->PlaneCount != 1 ||
            PrivateData->PlaneOffsets[0] != 0 ||
            PrivateData->PlanePitches[0] != PrivateData->Pitch)
        {
            return FALSE;
        }
        if (PrivateData->StorageHeight != PrivateData->Height)
            return FALSE;
        Size = (ULONGLONG)PrivateData->Pitch * PrivateData->Height;
        if (Size == 0 ||
            Size > (ULONGLONG)SOFTGPU_MAX_SURFACE_SIZE)
        {
            return FALSE;
        }
    }
    else if (PrivateData->Format == SOFTGPU_D3DDDIFMT_NV12)
    {
        if (PrivateData->BitsPerPixel != 12 ||
            (PrivateData->Width & 1) != 0 ||
            (PrivateData->Height & 1) != 0 ||
            PrivateData->Width > SOFTGPU_MAX_DISPLAY_WIDTH ||
            PrivateData->Height > SOFTGPU_MAX_DISPLAY_HEIGHT ||
            PrivateData->StorageHeight < PrivateData->Height ||
            PrivateData->StorageHeight > SOFTGPU_MAX_DISPLAY_HEIGHT ||
            (PrivateData->StorageHeight & 1) != 0 ||
            PrivateData->Pitch < PrivateData->Width ||
            (PrivateData->Pitch & 1) != 0 ||
            PrivateData->PlaneCount != 2 ||
            PrivateData->PlaneOffsets[0] != 0 ||
            PrivateData->PlanePitches[0] != PrivateData->Pitch ||
            PrivateData->PlanePitches[1] != PrivateData->Pitch)
        {
            return FALSE;
        }
        Size = (ULONGLONG)PrivateData->Pitch *
               PrivateData->StorageHeight;
        if (Size > MAXULONG ||
            PrivateData->PlaneOffsets[1] != (ULONG)Size)
        {
            return FALSE;
        }
        Size += (ULONGLONG)PrivateData->Pitch *
                (PrivateData->StorageHeight / 2);
        if (Size == 0 ||
            Size > (ULONGLONG)SOFTGPU_MAX_ALLOCATION_SLAB_SIZE)
        {
            return FALSE;
        }
    }
    else if (PrivateData->Format >= D3DDDIFMT_DXVACOMPBUFFER_BASE &&
             PrivateData->Format <= D3DDDIFMT_DXVA_RESERVED31)
    {
        if (PrivateData->BitsPerPixel != 8 ||
            PrivateData->Pitch < PrivateData->Width ||
            PrivateData->StorageHeight != PrivateData->Height ||
            PrivateData->PlaneCount != 1 ||
            PrivateData->PlaneOffsets[0] != 0 ||
            PrivateData->PlanePitches[0] != PrivateData->Pitch)
        {
            return FALSE;
        }
        Size = (ULONGLONG)PrivateData->Pitch * PrivateData->Height;
        if (Size == 0 ||
            Size > (ULONGLONG)SOFTGPU_MAX_ALLOCATION_SLAB_SIZE)
        {
            return FALSE;
        }
    }
    else
    {
        return FALSE;
    }

    *RequiredSize = Size;
    return TRUE;
}

FORCEINLINE BOOLEAN
SoftGpuReadAllocationPrivateData(
    _In_reads_bytes_(PrivateDataSize) const VOID *PrivateDataBuffer,
    _In_ SIZE_T PrivateDataSize,
    _Out_ SOFTGPU_ALLOCATION_PRIVATE_DATA *PrivateData,
    _Out_ PULONGLONG RequiredSize)
{
    const SOFTGPU_ALLOCATION_PRIVATE_DATA *Source;
    ULONGLONG ChromaOffset;

    if (PrivateDataBuffer == NULL || PrivateData == NULL ||
        RequiredSize == NULL ||
        PrivateDataSize < SOFTGPU_ALLOCATION_PRIVATE_VERSION_1_SIZE)
    {
        return FALSE;
    }

    Source = (const SOFTGPU_ALLOCATION_PRIVATE_DATA *)PrivateDataBuffer;
    RtlZeroMemory(PrivateData, sizeof(*PrivateData));
    if (Source->Magic != SOFTGPU_ALLOCATION_PRIVATE_MAGIC)
        return FALSE;

    if (Source->Version == SOFTGPU_ALLOCATION_PRIVATE_VERSION_1)
    {
        RtlCopyMemory(PrivateData,
                      Source,
                      SOFTGPU_ALLOCATION_PRIVATE_VERSION_1_SIZE);
        PrivateData->Version = SOFTGPU_ALLOCATION_PRIVATE_VERSION;
        PrivateData->StorageHeight = PrivateData->Height;
    }
    else if (Source->Version == SOFTGPU_ALLOCATION_PRIVATE_VERSION_2)
    {
        if (PrivateDataSize < SOFTGPU_ALLOCATION_PRIVATE_VERSION_2_SIZE)
            return FALSE;
        RtlCopyMemory(PrivateData,
                      Source,
                      SOFTGPU_ALLOCATION_PRIVATE_VERSION_2_SIZE);
        PrivateData->Version = SOFTGPU_ALLOCATION_PRIVATE_VERSION;
    }
    else if (Source->Version == SOFTGPU_ALLOCATION_PRIVATE_VERSION)
    {
        if (PrivateDataSize < sizeof(*PrivateData))
            return FALSE;
        RtlCopyMemory(PrivateData, Source, sizeof(*PrivateData));
        return SoftGpuAllocationPrivateDataSize(PrivateData, RequiredSize);
    }
    else
    {
        return FALSE;
    }

    PrivateData->PlaneCount =
        PrivateData->Format == SOFTGPU_D3DDDIFMT_NV12 ? 2 : 1;
    PrivateData->PlaneOffsets[0] = 0;
    PrivateData->PlanePitches[0] = PrivateData->Pitch;
    if (PrivateData->PlaneCount == 2)
    {
        ChromaOffset = (ULONGLONG)PrivateData->Pitch *
                       PrivateData->StorageHeight;
        if (ChromaOffset > MAXULONG)
            return FALSE;
        PrivateData->PlaneOffsets[1] = (ULONG)ChromaOffset;
        PrivateData->PlanePitches[1] = PrivateData->Pitch;
    }

    return SoftGpuAllocationPrivateDataSize(PrivateData, RequiredSize);
}

FORCEINLINE BOOLEAN
SoftGpuAllocationPrivateDataValid(
    _In_ const SOFTGPU_ALLOCATION_PRIVATE_DATA *PrivateData)
{
    ULONGLONG RequiredSize;

    return PrivateData != NULL &&
           (PrivateData->Format == D3DDDIFMT_X8R8G8B8 ||
            PrivateData->Format == D3DDDIFMT_A8R8G8B8) &&
           SoftGpuAllocationPrivateDataSize(PrivateData, &RequiredSize);
}

FORCEINLINE NTSTATUS
SoftGpu2dRectRange(
    _In_ SIZE_T BufferSize,
    _In_ ULONG Pitch,
    _In_ const RECT *Rect,
    _Out_ PSIZE_T Offset,
    _Out_ PSIZE_T RowBytes,
    _Out_ PSIZE_T Span)
{
    SIZE_T LeftBytes;
    SIZE_T TopBytes;
    SIZE_T LastRowBytes;
    LONG Width;
    LONG Height;

    if (Rect == NULL || Offset == NULL ||
        RowBytes == NULL || Span == NULL ||
        Rect->left < 0 || Rect->top < 0 ||
        Rect->left >= Rect->right ||
        Rect->top >= Rect->bottom)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Width = Rect->right - Rect->left;
    Height = Rect->bottom - Rect->top;
    if ((ULONG)Width >
            MAXULONG / SOFTGPU_DISPLAY_BYTES_PER_PIXEL)
    {
        return STATUS_INTEGER_OVERFLOW;
    }

    *RowBytes =
        (SIZE_T)(ULONG)Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL;
    LeftBytes =
        (SIZE_T)(ULONG)Rect->left *
        SOFTGPU_DISPLAY_BYTES_PER_PIXEL;
    if (Pitch < *RowBytes ||
        LeftBytes > (SIZE_T)Pitch - *RowBytes ||
        (SIZE_T)(ULONG)Rect->top >
            (MAXULONG_PTR - LeftBytes) / Pitch)
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    TopBytes = (SIZE_T)(ULONG)Rect->top * Pitch;
    *Offset = TopBytes + LeftBytes;
    if ((SIZE_T)(ULONG)(Height - 1) >
            (MAXULONG_PTR - *RowBytes) / Pitch)
    {
        return STATUS_INTEGER_OVERFLOW;
    }
    LastRowBytes =
        (SIZE_T)(ULONG)(Height - 1) * Pitch + *RowBytes;
    *Span = LastRowBytes;
    if (*Offset > BufferSize ||
        *Span > BufferSize - *Offset)
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    return STATUS_SUCCESS;
}

FORCEINLINE NTSTATUS
SoftGpu2dCopyRect(
    _In_reads_bytes_(SourceSize) const VOID *Source,
    _In_ SIZE_T SourceSize,
    _In_ ULONG SourcePitch,
    _In_ const RECT *SourceRect,
    _Out_writes_bytes_(DestinationSize) VOID *Destination,
    _In_ SIZE_T DestinationSize,
    _In_ ULONG DestinationPitch,
    _In_ const RECT *DestinationRect)
{
    SIZE_T SourceOffset;
    SIZE_T DestinationOffset;
    SIZE_T SourceRowBytes;
    SIZE_T DestinationRowBytes;
    SIZE_T SourceSpan;
    SIZE_T DestinationSpan;
    PUCHAR SourceBytes;
    PUCHAR DestinationBytes;
    LONG Width;
    LONG Height;
    LONG Row;
    NTSTATUS Status;

    if (Source == NULL || Destination == NULL ||
        SourceRect == NULL || DestinationRect == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Width = DestinationRect->right - DestinationRect->left;
    Height = DestinationRect->bottom - DestinationRect->top;
    if (Width != SourceRect->right - SourceRect->left ||
        Height != SourceRect->bottom - SourceRect->top)
    {
        return STATUS_NOT_SUPPORTED;
    }

    Status = SoftGpu2dRectRange(SourceSize,
                                SourcePitch,
                                SourceRect,
                                &SourceOffset,
                                &SourceRowBytes,
                                &SourceSpan);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = SoftGpu2dRectRange(DestinationSize,
                                DestinationPitch,
                                DestinationRect,
                                &DestinationOffset,
                                &DestinationRowBytes,
                                &DestinationSpan);
    if (!NT_SUCCESS(Status))
        return Status;
    if (SourceRowBytes != DestinationRowBytes)
        return STATUS_INVALID_BUFFER_SIZE;

    SourceBytes = (PUCHAR)Source + SourceOffset;
    DestinationBytes =
        (PUCHAR)Destination + DestinationOffset;
    if ((ULONG_PTR)DestinationBytes >
            (ULONG_PTR)SourceBytes &&
        (ULONG_PTR)DestinationBytes -
            (ULONG_PTR)SourceBytes < SourceSpan)
    {
        for (Row = Height; Row-- > 0; )
        {
            RtlMoveMemory(
                DestinationBytes +
                    (SIZE_T)Row * DestinationPitch,
                SourceBytes + (SIZE_T)Row * SourcePitch,
                SourceRowBytes);
        }
    }
    else
    {
        for (Row = 0; Row < Height; ++Row)
        {
            RtlMoveMemory(
                DestinationBytes +
                    (SIZE_T)Row * DestinationPitch,
                SourceBytes + (SIZE_T)Row * SourcePitch,
                SourceRowBytes);
        }
    }

    return STATUS_SUCCESS;
}

FORCEINLINE NTSTATUS
SoftGpu2dFillRect(
    _Out_writes_bytes_(DestinationSize) VOID *Destination,
    _In_ SIZE_T DestinationSize,
    _In_ ULONG DestinationPitch,
    _In_ const RECT *DestinationRect,
    _In_ ULONG Color)
{
    SIZE_T DestinationOffset;
    SIZE_T RowBytes;
    SIZE_T Span;
    PUCHAR DestinationBytes;
    LONG Width;
    LONG Height;
    LONG Row;
    LONG Column;
    NTSTATUS Status;

    if (Destination == NULL || DestinationRect == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = SoftGpu2dRectRange(DestinationSize,
                                DestinationPitch,
                                DestinationRect,
                                &DestinationOffset,
                                &RowBytes,
                                &Span);
    if (!NT_SUCCESS(Status))
        return Status;

    UNREFERENCED_PARAMETER(RowBytes);
    UNREFERENCED_PARAMETER(Span);
    Width = DestinationRect->right - DestinationRect->left;
    Height = DestinationRect->bottom - DestinationRect->top;
    DestinationBytes =
        (PUCHAR)Destination + DestinationOffset;
    for (Row = 0; Row < Height; ++Row)
    {
        PULONG Pixels =
            (PULONG)(DestinationBytes +
                     (SIZE_T)Row * DestinationPitch);

        for (Column = 0; Column < Width; ++Column)
            Pixels[Column] = Color;
    }

    return STATUS_SUCCESS;
}
