/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     SoftGPU linear 2D validation/copy/fill tests
 */

#include <kmt_test.h>
#include "softgpu_2d_core.h"

static VOID
InitializeRows(
    _Out_writes_(Count) PULONG Pixels,
    _In_ ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; ++Index)
        Pixels[Index] = 0x10000000u + Index;
}

START_TEST(SoftGpu2DPolicy)
{
    SOFTGPU_ALLOCATION_PRIVATE_DATA PrivateData;
    SOFTGPU_ALLOCATION_PRIVATE_DATA NormalizedPrivateData;
    ULONG Source[4 * 6];
    ULONG Destination[4 * 8];
    ULONG Overlap[4 * 4];
    RECT SourceRect;
    RECT DestinationRect;
    NTSTATUS Status;
    SIZE_T SlabSize;
    ULONGLONG RequiredSize;
    ULONG Index;

    Status = SoftGpu2dComputeAllocationSlabSize(
                 3840,
                 2160,
                 SOFTGPU_2D_WORKING_SURFACE_COUNT,
                 SOFTGPU_MAX_ALLOCATION_SLAB_SIZE,
                 &SlabSize);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(SlabSize, 132710400ULL);
    Status = SoftGpu2dComputeAllocationSlabSize(
                 8192,
                 8192,
                 SOFTGPU_2D_WORKING_SURFACE_COUNT,
                 SOFTGPU_MAX_ALLOCATION_SLAB_SIZE,
                 &SlabSize);
    ok_eq_hex(Status, STATUS_GRAPHICS_NO_VIDEO_MEMORY);
    ok_eq_ulonglong(SlabSize, 0);
    Status = SoftGpu2dComputeAllocationSlabSize(
                 0,
                 2160,
                 SOFTGPU_2D_WORKING_SURFACE_COUNT,
                 SOFTGPU_MAX_ALLOCATION_SLAB_SIZE,
                 &SlabSize);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = SoftGpu2dResolveSlabRange(
                 0x100000,
                 0x2000,
                 0x101fff,
                 1,
                 &SlabSize);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(SlabSize, 0x1fff);
    Status = SoftGpu2dResolveSlabRange(
                 0x100000,
                 0x2000,
                 0x101fff,
                 2,
                 &SlabSize);
    ok_eq_hex(Status, STATUS_INVALID_ADDRESS);
    ok_eq_ulonglong(SlabSize, 0);
    Status = SoftGpu2dResolveSlabRange(
                 0x100000,
                 0x2000,
                 0x0fffff,
                 1,
                 &SlabSize);
    ok_eq_hex(Status, STATUS_INVALID_ADDRESS);

    RtlZeroMemory(&PrivateData, sizeof(PrivateData));
    ok_eq_ulong(sizeof(PrivateData), 15 * sizeof(ULONG));
    ok_eq_ulong(SOFTGPU_ALLOCATION_PRIVATE_VERSION_2_SIZE,
                8 * sizeof(ULONG));
    ok_eq_ulong(FIELD_OFFSET(SOFTGPU_ALLOCATION_PRIVATE_DATA, Width), 0);
    ok_eq_ulong(FIELD_OFFSET(SOFTGPU_ALLOCATION_PRIVATE_DATA, Height),
                sizeof(ULONG));
    ok_eq_ulong(FIELD_OFFSET(SOFTGPU_ALLOCATION_PRIVATE_DATA, BitsPerPixel),
                2 * sizeof(ULONG));
    ok_eq_ulong(FIELD_OFFSET(SOFTGPU_ALLOCATION_PRIVATE_DATA, Magic),
                3 * sizeof(ULONG));
    PrivateData.Width = 4;
    PrivateData.Height = 3;
    PrivateData.BitsPerPixel = SOFTGPU_DISPLAY_BITS_PER_PIXEL;
    PrivateData.Magic = SOFTGPU_ALLOCATION_PRIVATE_MAGIC;
    PrivateData.Version = SOFTGPU_ALLOCATION_PRIVATE_VERSION;
    PrivateData.Pitch = 6 * sizeof(ULONG);
    PrivateData.Format = D3DDDIFMT_X8R8G8B8;
    PrivateData.StorageHeight = PrivateData.Height;
    PrivateData.PlaneCount = 1;
    PrivateData.PlanePitches[0] = PrivateData.Pitch;
    ok_bool_true(
        SoftGpuAllocationPrivateDataValid(&PrivateData),
        "valid padded linear surface");
    PrivateData.Pitch = 3 * sizeof(ULONG);
    ok_bool_false(
        SoftGpuAllocationPrivateDataValid(&PrivateData),
        "pitch shorter than visible row");
    PrivateData.Pitch = 6 * sizeof(ULONG);
    PrivateData.Format = D3DDDIFMT_R5G6B5;
    ok_bool_false(
        SoftGpuAllocationPrivateDataValid(&PrivateData),
        "unsupported pixel format");
    PrivateData.Format = D3DDDIFMT_X8R8G8B8;
    PrivateData.BitsPerPixel = 16;
    ok_bool_false(
        SoftGpuAllocationPrivateDataValid(&PrivateData),
        "non-32-bit compact allocation prefix");
    PrivateData.BitsPerPixel = SOFTGPU_DISPLAY_BITS_PER_PIXEL;
    PrivateData.Version++;
    ok_bool_false(
        SoftGpuAllocationPrivateDataValid(&PrivateData),
        "unknown private-data version");

    RtlZeroMemory(&PrivateData, sizeof(PrivateData));
    PrivateData.Width = 1920;
    PrivateData.Height = 1080;
    PrivateData.BitsPerPixel = 12;
    PrivateData.Magic = SOFTGPU_ALLOCATION_PRIVATE_MAGIC;
    PrivateData.Version = SOFTGPU_ALLOCATION_PRIVATE_VERSION;
    PrivateData.Pitch = 1920;
    PrivateData.Format = SOFTGPU_D3DDDIFMT_NV12;
    PrivateData.StorageHeight = 1088;
    PrivateData.PlaneCount = 2;
    PrivateData.PlaneOffsets[1] = 1920 * 1088;
    PrivateData.PlanePitches[0] = 1920;
    PrivateData.PlanePitches[1] = 1920;
    ok_bool_true(
        SoftGpuAllocationPrivateDataSize(&PrivateData, &RequiredSize),
        "valid NV12 storage height");
    ok_eq_ulonglong(RequiredSize, 3133440ULL);
    PrivateData.StorageHeight = 1072;
    ok_bool_false(
        SoftGpuAllocationPrivateDataSize(&PrivateData, &RequiredSize),
        "storage height shorter than visible height");
    PrivateData.StorageHeight = 1089;
    ok_bool_false(
        SoftGpuAllocationPrivateDataSize(&PrivateData, &RequiredSize),
        "odd NV12 storage height");
    PrivateData.StorageHeight = 1088;
    PrivateData.PlaneOffsets[1]++;
    ok_bool_false(
        SoftGpuAllocationPrivateDataSize(&PrivateData, &RequiredSize),
        "NV12 chroma offset does not follow padded luma");
    PrivateData.PlaneOffsets[1]--;
    PrivateData.PlanePitches[1] = 2048;
    ok_bool_false(
        SoftGpuAllocationPrivateDataSize(&PrivateData, &RequiredSize),
        "NV12 chroma pitch differs from the allocation layout");

    PrivateData.PlanePitches[1] = PrivateData.Pitch;
    PrivateData.Version = SOFTGPU_ALLOCATION_PRIVATE_VERSION_2;
    PrivateData.PlaneCount = 0;
    PrivateData.PlaneOffsets[1] = 0;
    PrivateData.PlanePitches[0] = 0;
    PrivateData.PlanePitches[1] = 0;
    ok_bool_true(
        SoftGpuReadAllocationPrivateData(
            &PrivateData,
            SOFTGPU_ALLOCATION_PRIVATE_VERSION_2_SIZE,
            &NormalizedPrivateData,
            &RequiredSize),
        "version 2 NV12 metadata is normalized");
    ok_eq_ulong(NormalizedPrivateData.Version,
                SOFTGPU_ALLOCATION_PRIVATE_VERSION);
    ok_eq_ulong(NormalizedPrivateData.PlaneCount, 2);
    ok_eq_ulong(NormalizedPrivateData.PlaneOffsets[1], 1920 * 1088);
    ok_eq_ulong(NormalizedPrivateData.PlanePitches[0], 1920);
    ok_eq_ulong(NormalizedPrivateData.PlanePitches[1], 1920);
    ok_eq_ulonglong(RequiredSize, 3133440ULL);
    ok_bool_false(
        SoftGpuReadAllocationPrivateData(
            &PrivateData,
            SOFTGPU_ALLOCATION_PRIVATE_VERSION_2_SIZE - 1,
            &NormalizedPrivateData,
            &RequiredSize),
        "truncated version 2 metadata");

    ok_eq_ulong(
        SoftGpu2dPresentEvaluate(TRUE, FALSE, FALSE, 1, 0),
        SoftGpu2dPresentNoOp);
    ok_eq_ulong(
        SoftGpu2dPresentEvaluate(FALSE, TRUE, FALSE, 1, 0),
        SoftGpu2dPresentNoOp);
    ok_eq_ulong(
        SoftGpu2dPresentEvaluate(FALSE, TRUE, FALSE, 1, 1),
        SoftGpu2dPresentBlt);
    ok_eq_ulong(
        SoftGpu2dPresentEvaluate(FALSE, FALSE, TRUE, 0, 1),
        SoftGpu2dPresentFill);
    ok_eq_ulong(
        SoftGpu2dPresentEvaluate(TRUE, TRUE, FALSE, 1, 0),
        SoftGpu2dPresentInvalid);
    ok_eq_ulong(
        SoftGpu2dPresentEvaluate(TRUE, FALSE, FALSE, 1, 1),
        SoftGpu2dPresentInvalid);
    ok_eq_ulong(
        SoftGpu2dPresentEvaluate(FALSE, TRUE, FALSE, 0, 1),
        SoftGpu2dPresentInvalid);
    ok_eq_ulong(
        SoftGpu2dPresentEvaluate(FALSE, FALSE, TRUE, 1, 1),
        SoftGpu2dPresentInvalid);

    InitializeRows(Source, RTL_NUMBER_OF(Source));
    for (Index = 0; Index < RTL_NUMBER_OF(Destination); ++Index)
        Destination[Index] = 0xcccccccc;

    SourceRect.left = 1;
    SourceRect.top = 1;
    SourceRect.right = 4;
    SourceRect.bottom = 3;
    DestinationRect.left = 2;
    DestinationRect.top = 0;
    DestinationRect.right = 5;
    DestinationRect.bottom = 2;
    Status = SoftGpu2dCopyRect(
                 Source,
                 sizeof(Source),
                 6 * sizeof(ULONG),
                 &SourceRect,
                 Destination,
                 sizeof(Destination),
                 8 * sizeof(ULONG),
                 &DestinationRect);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Destination[2], Source[7]);
    ok_eq_ulong(Destination[3], Source[8]);
    ok_eq_ulong(Destination[4], Source[9]);
    ok_eq_ulong(Destination[10], Source[13]);
    ok_eq_ulong(Destination[11], Source[14]);
    ok_eq_ulong(Destination[12], Source[15]);
    ok_eq_ulong(Destination[1], 0xcccccccc);
    ok_eq_ulong(Destination[5], 0xcccccccc);

    DestinationRect.right = 6;
    Status = SoftGpu2dCopyRect(
                 Source,
                 sizeof(Source),
                 6 * sizeof(ULONG),
                 &SourceRect,
                 Destination,
                 sizeof(Destination),
                 8 * sizeof(ULONG),
                 &DestinationRect);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);

    DestinationRect.left = 6;
    DestinationRect.right = 9;
    Status = SoftGpu2dCopyRect(
                 Source,
                 sizeof(Source),
                 6 * sizeof(ULONG),
                 &SourceRect,
                 Destination,
                 sizeof(Destination),
                 8 * sizeof(ULONG),
                 &DestinationRect);
    ok_eq_hex(Status, STATUS_INVALID_BUFFER_SIZE);

    RtlZeroMemory(Destination, sizeof(Destination));
    DestinationRect.left = 1;
    DestinationRect.top = 1;
    DestinationRect.right = 4;
    DestinationRect.bottom = 3;
    Status = SoftGpu2dFillRect(
                 Destination,
                 sizeof(Destination),
                 8 * sizeof(ULONG),
                 &DestinationRect,
                 0xaabbccdd);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Destination[9], 0xaabbccdd);
    ok_eq_ulong(Destination[10], 0xaabbccdd);
    ok_eq_ulong(Destination[11], 0xaabbccdd);
    ok_eq_ulong(Destination[17], 0xaabbccdd);
    ok_eq_ulong(Destination[20], 0);

    InitializeRows(Overlap, RTL_NUMBER_OF(Overlap));
    SourceRect.left = 0;
    SourceRect.top = 0;
    SourceRect.right = 4;
    SourceRect.bottom = 3;
    DestinationRect.left = 0;
    DestinationRect.top = 1;
    DestinationRect.right = 4;
    DestinationRect.bottom = 4;
    Status = SoftGpu2dCopyRect(
                 Overlap,
                 sizeof(Overlap),
                 4 * sizeof(ULONG),
                 &SourceRect,
                 Overlap,
                 sizeof(Overlap),
                 4 * sizeof(ULONG),
                 &DestinationRect);
    ok_eq_hex(Status, STATUS_SUCCESS);
    for (Index = 0; Index < 4; ++Index)
    {
        ok_eq_ulong(Overlap[4 + Index], 0x10000000u + Index);
        ok_eq_ulong(Overlap[8 + Index], 0x10000004u + Index);
        ok_eq_ulong(Overlap[12 + Index], 0x10000008u + Index);
    }

    /* Full-frame copy keeps both source and firmware padding untouched. */
    InitializeRows(Source, RTL_NUMBER_OF(Source));
    for (Index = 0; Index < RTL_NUMBER_OF(Destination); ++Index)
        Destination[Index] = 0xeeeeeeee;
    SourceRect.left = 0;
    SourceRect.top = 0;
    SourceRect.right = 3;
    SourceRect.bottom = 2;
    Status = SoftGpu2dCopyRect(
                 Source,
                 sizeof(Source),
                 6 * sizeof(ULONG),
                 &SourceRect,
                 Destination,
                 sizeof(Destination),
                 8 * sizeof(ULONG),
                 &SourceRect);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Destination[0], Source[0]);
    ok_eq_ulong(Destination[1], Source[1]);
    ok_eq_ulong(Destination[2], Source[2]);
    ok_eq_ulong(Destination[8], Source[6]);
    ok_eq_ulong(Destination[10], Source[8]);
    ok_eq_ulong(Destination[3], 0xeeeeeeee);
    ok_eq_ulong(Destination[11], 0xeeeeeeee);
}

/* EOF */
