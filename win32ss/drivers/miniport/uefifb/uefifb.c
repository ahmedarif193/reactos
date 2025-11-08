/*
 * UEFI GOP Framebuffer miniport (XPDM/VideoPort-style)
 * Drop-in replacement for the earlier "logical-only" version.
 *
 * Key differences:
 *  - Only real GOP modes are exposed.
 *  - IOCTL_VIDEO_SET_CURRENT_MODE performs a real firmware mode switch.
 *  - Robust mapping: VideoPortMapMemory -> MmMapIoSpace fallback; no "low VA" rejection.
 *  - Framebuffer size/stride always derived from active GOP mode.
 */

#include "uefifb.h"
#include "../../../include/uefifb/uefifb_ioctl.h"

#if DBG
#define UEFIFB_LOG(_lvl, _fmt, ...) \
    VideoPortDebugPrint((_lvl), "UEFIFB: " _fmt, ##__VA_ARGS__)
#else
#define UEFIFB_LOG(_lvl, _fmt, ...) do { } while (0)
#endif

#ifndef PAGE_SIZE
#define PAGE_SIZE 0x1000
#endif

#define UEFIFB_FAKE_VENDOR_ID 0x1414 /* Microsoft */
#define UEFIFB_FAKE_DEVICE_ID 0x5101 /* arbitrary but stable */

#ifndef _MEMORY_CACHING_TYPE_DEFINED
typedef enum _MEMORY_CACHING_TYPE
{
    MmNonCached = 0,
    MmCached = 1,
    MmWriteCombined = 6,
} MEMORY_CACHING_TYPE;
#define _MEMORY_CACHING_TYPE_DEFINED
#endif

static BOOLEAN UefiFbPopulateModeInformation(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt);
static BOOLEAN UefiFbBuildModeTable(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt);
static VOID UefiFbSelectPreferredMode(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt);
static BOOLEAN UefiFbRegisterAccessRange(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt);
static BOOLEAN NTAPI UefiFbUnmapVideoMemory(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt,
                                            _In_ PVIDEO_MEMORY VideoMemory,
                                            _Out_ PSTATUS_BLOCK StatusBlock);
static VOID UefiFbSetMapInformation(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt,
                                    _Inout_ PVOID MapInfoBuffer,
                                    _In_ ULONG BufferLength,
                                    _In_ PVOID FrameBufferBase,
                                    _In_ ULONGLONG VisibleLength,
                                    _Out_ PSTATUS_BLOCK StatusBlock);

PVOID
NTAPI
MmMapIoSpace(
    PHYSICAL_ADDRESS PhysicalAddress,
    SIZE_T NumberOfBytes,
    MEMORY_CACHING_TYPE CacheType);

VOID
NTAPI
MmUnmapIoSpace(
    PVOID BaseAddress,
    SIZE_T NumberOfBytes);

/* ------------------------------------------------------------------------- */
/* Synthetic EDID block (harmless; helps user-mode pick sane defaults)       */
/* ------------------------------------------------------------------------- */

static const UCHAR UefiFbSyntheticEdid[128] = {
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00, 0x4C,0x2D,0xA0,0x01,0x01,0x01,0x01,0x01,
    0x01,0x15,0x01,0x03,0x80,0x1A,0x11,0x78, 0x0A,0xEE,0x91,0xA3,0x54,0x4C,0x99,0x26,
    0x0F,0x50,0x54,0xAD,0x00,0x81,0x80,0x81, 0x40,0x81,0x00,0x95,0x00,0xA9,0x40,0xB3,
    0x00,0x01,0x01,0x01,0x01,0x02,0x3A,0x80, 0x18,0x71,0x38,0x2D,0x40,0x58,0x2C,0x45,
    0x00,0xFD,0x1E,0x11,0x00,0x00,0x1E,0x00, 0x00,0x00,0xFC,0x00,0x52,0x4F,0x53,0x20,
    0x55,0x45,0x46,0x49,0x20,0x46,0x42,0x0A, 0x00,0x00,0x00,0xFF,0x00,0x30,0x30,0x30,
    0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x0A, 0x00,0x00,0x00,0xFD,0x00,0x3A,0x3E,0x0F,
    0x2E,0x08,0x00,0x0A,0x20,0x20,0x20,0x20, 0x20,0x20,0x00,0x00,0x00,0x00,0x00,0x2E
};

static VOID
UefiFbFinalizeEdidChecksum(_Inout_updates_(UEFIFB_EDID_LENGTH) PUCHAR Edid)
{
    ULONG i;
    ULONG sum = 0;

    for (i = 0; i < UEFIFB_EDID_LENGTH - 1; ++i)
        sum += Edid[i];

    Edid[UEFIFB_EDID_LENGTH - 1] = (UCHAR)((256 - (sum & 0xFF)) & 0xFF);
}

static VOID
UefiFbBuildEdidForFramebuffer(_In_ const LOADER_PARAMETER_FRAMEBUFFER *Fb,
                              _In_ ULONG ChildIndex,
                              _Out_writes_bytes_(UEFIFB_EDID_LENGTH) PUCHAR Edid)
{
    ULONG width = Fb->HorizontalResolution ? Fb->HorizontalResolution : 1024;
    ULONG height = Fb->VerticalResolution ? Fb->VerticalResolution : 768;
    ULONG refresh = 60;
    ULONGLONG pixelClock;
    ULONG pixelClock10KHz;
    ULONG hBlank;
    ULONG vBlank;
    ULONG hTotal;
    ULONG vTotal;
    ULONG hFrontPorch;
    ULONG hSyncWidth;
    ULONG vFrontPorch;
    ULONG vSyncWidth;
    ULONG imageWidthMm;
    ULONG imageHeightMm;

    VideoPortMoveMemory(Edid, (PVOID)UefiFbSyntheticEdid, sizeof(UefiFbSyntheticEdid));

    hBlank = width / 5;
    if (hBlank < 80) hBlank = 80;
    if (hBlank > 0x0FFF) hBlank = 0x0FFF;

    vBlank = height / 10;
    if (vBlank < 10) vBlank = 10;
    if (vBlank > 0x0FFF) vBlank = 0x0FFF;

    hTotal = width + hBlank;
    vTotal = height + vBlank;

    pixelClock = (ULONGLONG)hTotal * (ULONGLONG)vTotal * refresh;
    pixelClock10KHz = (ULONG)(pixelClock / 10000);
    if (pixelClock10KHz == 0)
        pixelClock10KHz = 1;
    if (pixelClock10KHz > 0xFFFF)
        pixelClock10KHz = 0xFFFF;

    hFrontPorch = hBlank / 3;
    if (hFrontPorch < 8) hFrontPorch = 8;
    if (hFrontPorch > 0x3FF) hFrontPorch = 0x3FF;

    hSyncWidth = hBlank / 4;
    if (hSyncWidth < 8) hSyncWidth = 8;
    if (hSyncWidth > 0x3FF) hSyncWidth = 0x3FF;

    if (hFrontPorch + hSyncWidth >= hBlank)
    {
        ULONG excess = (hFrontPorch + hSyncWidth) - hBlank + 1;
        if (hFrontPorch > excess)
            hFrontPorch -= excess;
        else if (hSyncWidth > excess)
            hSyncWidth -= excess;
    }

    vFrontPorch = vBlank / 3;
    if (vFrontPorch < 1) vFrontPorch = 1;
    if (vFrontPorch > 0x3F) vFrontPorch = 0x3F;

    vSyncWidth = vBlank / 4;
    if (vSyncWidth < 3) vSyncWidth = 3;
    if (vSyncWidth > 0x3F) vSyncWidth = 0x3F;

    if (vFrontPorch + vSyncWidth >= vBlank)
    {
        ULONG excess = (vFrontPorch + vSyncWidth) - vBlank + 1;
        if (vFrontPorch > excess)
            vFrontPorch -= excess;
        else if (vSyncWidth > excess)
            vSyncWidth -= excess;
    }

    imageWidthMm = width * 10 / 37;
    imageHeightMm = height * 10 / 37;
    if (imageWidthMm > 0x0FFF) imageWidthMm = 0x0FFF;
    if (imageHeightMm > 0x0FFF) imageHeightMm = 0x0FFF;

    Edid[54] = (UCHAR)(pixelClock10KHz & 0xFF);
    Edid[55] = (UCHAR)((pixelClock10KHz >> 8) & 0xFF);

    Edid[56] = (UCHAR)(width & 0xFF);
    Edid[57] = (UCHAR)(hBlank & 0xFF);
    Edid[58] = (UCHAR)(((width >> 8) & 0xF) << 4 | ((hBlank >> 8) & 0xF));

    Edid[59] = (UCHAR)(height & 0xFF);
    Edid[60] = (UCHAR)(vBlank & 0xFF);
    Edid[61] = (UCHAR)(((height >> 8) & 0xF) << 4 | ((vBlank >> 8) & 0xF));

    Edid[62] = (UCHAR)(hFrontPorch & 0xFF);
    Edid[63] = (UCHAR)(hSyncWidth & 0xFF);
    Edid[64] = (UCHAR)((((hFrontPorch >> 8) & 0x3) << 6) |
                       (((hSyncWidth >> 8) & 0x3) << 4) |
                       (((vFrontPorch >> 4) & 0x3) << 2) |
                       ((vSyncWidth >> 4) & 0x3));
    Edid[65] = (UCHAR)(((vFrontPorch & 0xF) << 4) |
                       (vSyncWidth & 0xF));

    Edid[66] = (UCHAR)(imageWidthMm & 0xFF);
    Edid[67] = (UCHAR)(imageHeightMm & 0xFF);
    Edid[68] = (UCHAR)(((imageWidthMm >> 8) & 0xF) << 4 |
                       ((imageHeightMm >> 8) & 0xF));

    Edid[69] = 0;
    Edid[70] = 0;

    Edid[0x6E] = (UCHAR)('0' + (ChildIndex % 10));

    UefiFbFinalizeEdidChecksum(Edid);
}

static PUEFIFB_CHILD_OUTPUT
UefiFbGetActiveChild(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt)
{
    if (!DevExt->ChildOutputs || DevExt->ChildCount == 0)
        return NULL;

    if (DevExt->ActiveChild >= DevExt->ChildCount)
        DevExt->ActiveChild = 0;

    return &DevExt->ChildOutputs[DevExt->ActiveChild];
}

static VOID
UefiFbMirrorActiveChildToDevExt(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt)
{
    PUEFIFB_CHILD_OUTPUT child = UefiFbGetActiveChild(DevExt);
    if (!child)
        return;

    DevExt->FrameBufferInfo = child->FrameBuffer;
    DevExt->FrameBufferOffset = child->FrameBufferOffset;
    DevExt->FrameBufferMapLength = child->FrameBufferMapLength;
    DevExt->FrameBufferLength64 = child->FrameBufferLength64;
    DevExt->ModeInfo = child->ModeInfo;
    DevExt->ModeTable = child->ModeTable;
    DevExt->ModeCount = child->ModeCount;
    DevExt->CurrentModeIndex = child->CurrentModeIndex;
    DevExt->ModeSet = child->ModeSet;
}

static VOID
UefiFbMirrorDevExtToActiveChild(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt)
{
    PUEFIFB_CHILD_OUTPUT child = UefiFbGetActiveChild(DevExt);
    if (!child)
        return;

    child->FrameBuffer = DevExt->FrameBufferInfo;
    child->FrameBufferOffset = DevExt->FrameBufferOffset;
    child->FrameBufferMapLength = DevExt->FrameBufferMapLength;
    child->FrameBufferLength64 = DevExt->FrameBufferLength64;
    child->ModeInfo = DevExt->ModeInfo;
    child->ModeTable = DevExt->ModeTable;
    child->ModeCount = DevExt->ModeCount;
    child->CurrentModeIndex = DevExt->CurrentModeIndex;
    child->ModeSet = DevExt->ModeSet;
}

static VOID
UefiFbReleaseChildModeTable(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt,
                            _Inout_ PUEFIFB_CHILD_OUTPUT Child)
{
    PVIDEO_MODE_INFORMATION table;

    if (!Child || Child->ModeTable == NULL)
        return;

    table = Child->ModeTable;
    Child->ModeTable = NULL;
    Child->ModeCount = 0;
    Child->CurrentModeIndex = 0;
    Child->ModeSet = FALSE;

    if (DevExt->ModeTable == table)
    {
        DevExt->ModeTable = NULL;
        DevExt->ModeCount = 0;
        DevExt->CurrentModeIndex = 0;
        DevExt->ModeSet = FALSE;
    }

    VideoPortFreePool(DevExt, table);
}

static VOID
UefiFbDestroyChildOutputs(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt)
{
    ULONG i;

    if (!DevExt->ChildOutputs)
        return;

    for (i = 0; i < DevExt->ChildCount; ++i)
        UefiFbReleaseChildModeTable(DevExt, &DevExt->ChildOutputs[i]);

    VideoPortFreePool(DevExt, DevExt->ChildOutputs);
    DevExt->ChildOutputs = NULL;
    DevExt->ChildCount = 0;
    DevExt->ActiveChild = 0;
}

static ULONG
UefiFbFindPrimaryChild(_In_ PUEFIFB_DEVICE_EXTENSION DevExt)
{
    ULONG i;

    if (!DevExt->ChildOutputs || DevExt->ChildCount == 0)
        return 0;

    for (i = 0; i < DevExt->ChildCount; ++i)
    {
        const LOADER_PARAMETER_FRAMEBUFFER *fb = &DevExt->ChildOutputs[i].FrameBuffer;

        if ((fb->FrameBufferBase.QuadPart == DevExt->FrameBufferInfo.FrameBufferBase.QuadPart) &&
            (fb->HorizontalResolution == DevExt->FrameBufferInfo.HorizontalResolution) &&
            (fb->VerticalResolution == DevExt->FrameBufferInfo.VerticalResolution))
        {
            return i;
        }
    }

    return 0;
}

static VOID
UefiFbInitializeChildOutputs(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt)
{
    ULONG fbCount = InbvGetGopFrameBufferCount();
    ULONG recorded = 0;
    SIZE_T allocSize;
    ULONG i;
    PUEFIFB_CHILD_OUTPUT outputs = NULL;

    UefiFbDestroyChildOutputs(DevExt);

    if (fbCount)
    {
        allocSize = sizeof(UEFIFB_CHILD_OUTPUT) * fbCount;
        outputs = VideoPortAllocatePool(DevExt, 0, allocSize, 'bfEU');
        if (outputs)
        {
            VideoPortZeroMemory(outputs, allocSize);
            for (i = 0; i < fbCount; ++i)
            {
                if (InbvGetGopFrameBufferInfoByIndex(i, &outputs[recorded].FrameBuffer))
                {
                    UefiFbBuildEdidForFramebuffer(&outputs[recorded].FrameBuffer,
                                                  recorded,
                                                  outputs[recorded].SyntheticEdid);
                    outputs[recorded].FrameBufferLength64 =
                        outputs[recorded].FrameBuffer.FrameBufferSize;
                    recorded++;
                }
            }
        }
    }

    if (!outputs || recorded == 0)
    {
        allocSize = sizeof(UEFIFB_CHILD_OUTPUT);
        outputs = VideoPortAllocatePool(DevExt, 0, allocSize, 'bfEU');
        if (!outputs)
        {
            DevExt->ChildOutputs = NULL;
            DevExt->ChildCount = 0;
            DevExt->ActiveChild = 0;
            return;
        }

        VideoPortZeroMemory(outputs, allocSize);
        outputs[0].FrameBuffer = DevExt->FrameBufferInfo;
        UefiFbBuildEdidForFramebuffer(&outputs[0].FrameBuffer, 0, outputs[0].SyntheticEdid);
        outputs[0].FrameBufferLength64 = DevExt->FrameBufferLength64;
        recorded = 1;
    }

    DevExt->ChildOutputs = outputs;
    DevExt->ChildCount = recorded;
    DevExt->ActiveChild = UefiFbFindPrimaryChild(DevExt);
    UefiFbMirrorDevExtToActiveChild(DevExt);
}

static BOOLEAN
UefiFbActivateChild(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt,
                    _In_ ULONG ChildIndex,
                    _Out_ PSTATUS_BLOCK StatusBlock)
{
    ULONG previousChild = DevExt->ActiveChild;
    PUEFIFB_CHILD_OUTPUT child;

    if (!DevExt->ChildOutputs || ChildIndex >= DevExt->ChildCount)
    {
        StatusBlock->Status = ERROR_INVALID_PARAMETER;
        return FALSE;
    }

    if (DevExt->ActiveChild == ChildIndex)
    {
        StatusBlock->Status = NO_ERROR;
        return TRUE;
    }

    if (DevExt->MappedFrameBuffer != NULL)
    {
        VIDEO_MEMORY video = {0};
        STATUS_BLOCK unmap = {0};
        video.RequestedVirtualAddress = DevExt->MappedFrameBuffer;
        (VOID)UefiFbUnmapVideoMemory(DevExt, &video, &unmap);
    }

    DevExt->ActiveChild = ChildIndex;
    UefiFbMirrorActiveChildToDevExt(DevExt);
    child = &DevExt->ChildOutputs[ChildIndex];

    if (child->ModeTable && child->ModeCount)
    {
        if (!UefiFbRegisterAccessRange(DevExt))
        {
            StatusBlock->Status = ERROR_INVALID_PARAMETER;
            DevExt->ActiveChild = previousChild;
            UefiFbMirrorActiveChildToDevExt(DevExt);
            return FALSE;
        }

        StatusBlock->Status = NO_ERROR;
        return TRUE;
    }

    if (!UefiFbPopulateModeInformation(DevExt))
    {
        StatusBlock->Status = ERROR_DEV_NOT_EXIST;
        DevExt->ActiveChild = previousChild;
        UefiFbMirrorActiveChildToDevExt(DevExt);
        return FALSE;
    }

    if (!UefiFbBuildModeTable(DevExt))
    {
        StatusBlock->Status = ERROR_NOT_ENOUGH_MEMORY;
        DevExt->ActiveChild = previousChild;
        UefiFbMirrorActiveChildToDevExt(DevExt);
        return FALSE;
    }

    UefiFbSelectPreferredMode(DevExt);
    if (!UefiFbRegisterAccessRange(DevExt))
    {
        StatusBlock->Status = ERROR_INVALID_PARAMETER;
        DevExt->ActiveChild = previousChild;
        UefiFbMirrorActiveChildToDevExt(DevExt);
        return FALSE;
    }

    UefiFbMirrorDevExtToActiveChild(DevExt);
    StatusBlock->Status = NO_ERROR;
    return TRUE;
}

static BOOLEAN
UefiFbEnsureChildModeTable(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt,
                           _In_ ULONG ChildIndex,
                           _Out_ PSTATUS_BLOCK StatusBlock)
{
    ULONG previousChild;
    STATUS_BLOCK localStatus = {0};

    if (!DevExt->ChildOutputs || ChildIndex >= DevExt->ChildCount)
    {
        StatusBlock->Status = ERROR_INVALID_PARAMETER;
        return FALSE;
    }

    if (DevExt->ChildOutputs[ChildIndex].ModeTable &&
        DevExt->ChildOutputs[ChildIndex].ModeCount)
    {
        StatusBlock->Status = NO_ERROR;
        return TRUE;
    }

    previousChild = DevExt->ActiveChild;
    if (!UefiFbActivateChild(DevExt, ChildIndex, &localStatus))
    {
        *StatusBlock = localStatus;
        return FALSE;
    }

    if (!DevExt->ChildOutputs[ChildIndex].ModeTable ||
        DevExt->ChildOutputs[ChildIndex].ModeCount == 0)
    {
        StatusBlock->Status = ERROR_DEV_NOT_EXIST;
        return FALSE;
    }

    if (previousChild != ChildIndex)
    {
        STATUS_BLOCK restore = {0};
        if (!UefiFbActivateChild(DevExt, previousChild, &restore))
        {
            *StatusBlock = restore;
            return FALSE;
        }
    }

    StatusBlock->Status = NO_ERROR;
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static ULONGLONG
UefiFbRoundToPages(ULONGLONG Size)
{
    ULONGLONG mask;
    ULONGLONG maxAligned;

    if (Size == 0)
        return 0;

    mask = PAGE_SIZE - 1;
    maxAligned = ((ULONGLONG)-1) & ~mask;
    if (Size > maxAligned)
        return maxAligned;

    return (Size + mask) & ~mask;
}

static ULONG
UefiFbCountMaskBits(_In_ ULONG Mask)
{
    ULONG Count = 0;
    while (Mask != 0) { Count += Mask & 1u; Mask >>= 1; }
    return Count;
}

static BOOLEAN
UefiFbGetFramebufferBitsPerPixel(_In_ const LOADER_PARAMETER_FRAMEBUFFER *Fb,
                                 _Out_ PULONG BitsPerPixel)
{
    ULONG bpp;

    switch (Fb->PixelFormat)
    {
        case PixelRedGreenBlueReserved8BitPerColor:
        case PixelBlueGreenRedReserved8BitPerColor:
            bpp = 32;
            break;

        case PixelBitMask:
            bpp = UefiFbCountMaskBits(Fb->RedMask | Fb->GreenMask | Fb->BlueMask);
            if (bpp == 0)
                return FALSE;
            bpp = (bpp + 7u) & ~7u;
            break;

        default:
            return FALSE;
    }

    *BitsPerPixel = bpp;
    return TRUE;
}

static BOOLEAN
UefiFbValidateFrameBufferInfo(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt)
{
    PLOADER_PARAMETER_FRAMEBUFFER Fb = &DevExt->FrameBufferInfo;
    ULONGLONG base;

    if ((Fb->FrameBufferBase.QuadPart == 0) ||
        (Fb->HorizontalResolution == 0) ||
        (Fb->VerticalResolution == 0))
    {
        UEFIFB_LOG(0, "ValidateFrameBufferInfo: invalid base/res (%I64x, %ux%u)\n",
                   Fb->FrameBufferBase.QuadPart,
                   Fb->HorizontalResolution,
                   Fb->VerticalResolution);
        return FALSE;
    }

    base = Fb->FrameBufferBase.QuadPart;
    DevExt->FrameBufferOffset = (ULONG)(base & ((ULONGLONG)PAGE_SIZE - 1));
    Fb->FrameBufferBase.QuadPart = base & ~((ULONGLONG)PAGE_SIZE - 1);

    {
        ULONGLONG mapLength64;

        mapLength64 =
            UefiFbRoundToPages((ULONGLONG)Fb->FrameBufferSize + DevExt->FrameBufferOffset);
        if (mapLength64 == 0)
            mapLength64 = UefiFbRoundToPages(Fb->FrameBufferSize);

        if (mapLength64 > MAXULONG)
            DevExt->FrameBufferMapLength = MAXULONG;
        else
            DevExt->FrameBufferMapLength = (ULONG)mapLength64;
    }

    if ((Fb->PixelsPerScanLine == 0) ||
        (Fb->PixelsPerScanLine < Fb->HorizontalResolution))
    {
        Fb->PixelsPerScanLine = Fb->HorizontalResolution;
    }

    return TRUE;
}

static VOID
UefiFbEnsureFrameBufferSize(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt)
{
    ULONGLONG stride = DevExt->ModeInfo.ScreenStride;
    ULONGLONG height = DevExt->ModeInfo.VisScreenHeight;
    ULONGLONG requiredBytes;
    ULONGLONG requiredAligned64;
    ULONGLONG reportedAligned64;
    ULONGLONG mapLength64;
    ULONG requiredAligned;
    ULONG reportedAligned;

    if ((stride == 0) || (height == 0))
        return;

    if (height != 0 && stride <= (((ULONGLONG)-1) / height))
        requiredBytes = stride * height;
    else
        requiredBytes = (ULONGLONG)-1;

    requiredAligned64 = UefiFbRoundToPages(requiredBytes);
    reportedAligned64 = UefiFbRoundToPages(DevExt->FrameBufferInfo.FrameBufferSize);

    if (requiredAligned64 == 0)
        requiredAligned64 = reportedAligned64;

    if (requiredAligned64 == 0)
        return;

    requiredAligned = (requiredAligned64 > MAXULONG)
                        ? MAXULONG
                        : (ULONG)requiredAligned64;
    reportedAligned = (reportedAligned64 > MAXULONG)
                        ? MAXULONG
                        : (ULONG)reportedAligned64;

    DevExt->FrameBufferLength64 = requiredAligned64;
    DevExt->FrameBufferInfo.FrameBufferSize = requiredAligned;

    mapLength64 = UefiFbRoundToPages(requiredAligned64 + DevExt->FrameBufferOffset);
    if (mapLength64 == 0)
        mapLength64 = requiredAligned64;

    if (mapLength64 > MAXULONG)
        DevExt->FrameBufferMapLength = MAXULONG;
    else
        DevExt->FrameBufferMapLength = (ULONG)mapLength64;

    UEFIFB_LOG(1,
               "EnsureFrameBufferSize: stride=%I64u height=%I64u required=%lu reported=%lu mapLen=%lu offset=%lu\n",
               stride,
               height,
               (unsigned long)requiredAligned,
               (unsigned long)reportedAligned,
               (unsigned long)DevExt->FrameBufferMapLength,
               (unsigned long)DevExt->FrameBufferOffset);

    UefiFbMirrorDevExtToActiveChild(DevExt);
}

static VOID
UefiFbSetMapInformation(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt,
                                    _Inout_ PVOID MapInfoBuffer,
                                    _In_ ULONG BufferLength,
                                    _In_ PVOID FrameBufferBase,
                                    _In_ ULONGLONG VisibleLength,
                                    _Out_ PSTATUS_BLOCK StatusBlock)
{
    UNREFERENCED_PARAMETER(DevExt);

    if (BufferLength >= sizeof(VIDEO_MEMORY_INFORMATION64))
    {
        PVIDEO_MEMORY_INFORMATION64 info64 = MapInfoBuffer;
        info64->VideoRamBase = FrameBufferBase;
        info64->VideoRamLength = VisibleLength;
        info64->FrameBufferBase = FrameBufferBase;
        info64->FrameBufferLength = VisibleLength;
        StatusBlock->Information = sizeof(VIDEO_MEMORY_INFORMATION64);
    }
    else
    {
        PVIDEO_MEMORY_INFORMATION info = MapInfoBuffer;
        ULONG truncated = (VisibleLength > MAXULONG) ? MAXULONG : (ULONG)VisibleLength;
        info->VideoRamBase = FrameBufferBase;
        info->VideoRamLength = truncated;
        info->FrameBufferBase = FrameBufferBase;
        info->FrameBufferLength = truncated;
        StatusBlock->Information = sizeof(VIDEO_MEMORY_INFORMATION);
    }

    StatusBlock->Status = NO_ERROR;
}

static BOOLEAN
UefiFbRestoreCurrentFrameBufferInfo(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt)
{
    if (!InbvGetGopFrameBufferInfo(&DevExt->FrameBufferInfo))
        return FALSE;

    return UefiFbValidateFrameBufferInfo(DevExt);
}

static BOOLEAN
UefiFbRegisterAccessRange(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt)
{
    ULONG mapLength = DevExt->FrameBufferMapLength;

    if (mapLength == 0)
    {
        ULONGLONG mapLength64 =
            UefiFbRoundToPages((ULONGLONG)DevExt->FrameBufferInfo.FrameBufferSize +
                               DevExt->FrameBufferOffset);
        if (mapLength64 == 0)
            return FALSE;

        mapLength = (mapLength64 > MAXULONG) ? MAXULONG : (ULONG)mapLength64;
    }

    DevExt->AccessRanges[0].RangeStart = DevExt->FrameBufferInfo.FrameBufferBase;
    DevExt->AccessRanges[0].RangeLength = mapLength;
    DevExt->AccessRanges[0].RangeInIoSpace = FALSE;
    DevExt->AccessRanges[0].RangeVisible = FALSE;
    DevExt->AccessRanges[0].RangeShareable = TRUE;
    DevExt->AccessRanges[0].RangePassive = 0;

    if (VideoPortSetAccessRanges(DevExt, 1, DevExt->AccessRanges) != NO_ERROR)
    {
        UEFIFB_LOG(0,
                   "SetAccessRanges failed (base=%I64x len=%lu)\n",
                   DevExt->AccessRanges[0].RangeStart.QuadPart,
                   (unsigned long)DevExt->AccessRanges[0].RangeLength);
        return FALSE;
    }

    return TRUE;
}

static VOID
UefiFbSelectPreferredMode(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt)
{
    ULONG preferred = InbvGetGopPreferredMode();
    LOADER_PARAMETER_FRAMEBUFFER fb;
    ULONG desiredBpp;
    ULONG i;
    ULONG gopCount;

    if (preferred == MAXULONG || DevExt->ModeCount == 0 || DevExt->ModeTable == NULL)
        return;

    if (DevExt->CurrentModeIndex >= DevExt->ModeCount)
        DevExt->CurrentModeIndex = DevExt->ModeTable[0].ModeIndex;

    if (!InbvQueryGopModeCount(&gopCount) || preferred >= gopCount)
        return;

    if (!InbvQueryGopModeInfo(preferred, &fb))
        return;

    if (!UefiFbGetFramebufferBitsPerPixel(&fb, &desiredBpp))
        return;

    for (i = 0; i < DevExt->ModeCount; ++i)
    {
        PVIDEO_MODE_INFORMATION mode = &DevExt->ModeTable[i];
        ULONG modeBpp = mode->BitsPerPlane * mode->NumberOfPlanes;
        if (modeBpp == 0)
            modeBpp = mode->BitsPerPlane;

        if ((mode->VisScreenWidth == fb.HorizontalResolution) &&
            (mode->VisScreenHeight == fb.VerticalResolution) &&
            (modeBpp == desiredBpp))
        {
            DevExt->CurrentModeIndex = mode->ModeIndex;
            UefiFbMirrorDevExtToActiveChild(DevExt);
            return;
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Mode information                                                          */
/* ------------------------------------------------------------------------- */

static BOOLEAN
UefiFbPopulateModeInformation(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt)
{
    const LOADER_PARAMETER_FRAMEBUFFER *Fb = &DevExt->FrameBufferInfo;
    PVIDEO_MODE_INFORMATION Mode = &DevExt->ModeInfo;
    ULONG RedMask, GreenMask, BlueMask;
    ULONG BitsPerPixel;
    ULONG PixelsPerScanLine;
    ULONG BytesPerPixel;
    ULONGLONG StrideBytes;

    if (!UefiFbValidateFrameBufferInfo(DevExt))
        return FALSE;

    switch (Fb->PixelFormat)
    {
        case PixelRedGreenBlueReserved8BitPerColor: /* 0: R8G8B8A8 (little-endian) */
            RedMask = 0x000000FFu;
            GreenMask = 0x0000FF00u;
            BlueMask = 0x00FF0000u;
            BitsPerPixel = 32;
            break;

        case PixelBlueGreenRedReserved8BitPerColor: /* 1: B8G8R8A8 (little-endian) */
            RedMask = 0x00FF0000u;
            GreenMask = 0x0000FF00u;
            BlueMask = 0x000000FFu;
            BitsPerPixel = 32;
            break;

        case PixelBitMask:
            RedMask = Fb->RedMask;
            GreenMask = Fb->GreenMask;
            BlueMask = Fb->BlueMask;
            BitsPerPixel = UefiFbCountMaskBits(RedMask | GreenMask | BlueMask);
            if (BitsPerPixel == 0) return FALSE;
            BitsPerPixel = (BitsPerPixel + 7u) & ~7u; /* byte align (24->24, 18->24, etc.) */
            break;

        default:
            return FALSE;
    }

    PixelsPerScanLine = Fb->PixelsPerScanLine ? Fb->PixelsPerScanLine : Fb->HorizontalResolution;
    if (PixelsPerScanLine == 0)
        return FALSE;

    BytesPerPixel = (BitsPerPixel + 7u) / 8u;
    StrideBytes = (ULONGLONG)PixelsPerScanLine * (ULONGLONG)BytesPerPixel;
    if (StrideBytes == 0 || StrideBytes > MAXULONG)
    {
        UEFIFB_LOG(0,
                   "PopulateModeInfo: stride overflow (%lu px, %u bpp)\n",
                   PixelsPerScanLine,
                   (unsigned int)BitsPerPixel);
        return FALSE;
    }

    VideoPortZeroMemory(Mode, sizeof(*Mode));
    Mode->Length = sizeof(*Mode);
    Mode->ModeIndex = 0;
    Mode->VisScreenWidth = Fb->HorizontalResolution;
    Mode->VisScreenHeight = Fb->VerticalResolution;
    Mode->ScreenStride = (ULONG)StrideBytes;
    Mode->NumberOfPlanes = 1;
    Mode->BitsPerPlane = BitsPerPixel;
    Mode->Frequency = 60;
    Mode->NumberRedBits = UefiFbCountMaskBits(RedMask);
    Mode->NumberGreenBits = UefiFbCountMaskBits(GreenMask);
    Mode->NumberBlueBits = UefiFbCountMaskBits(BlueMask);
    Mode->RedMask = RedMask;
    Mode->GreenMask = GreenMask;
    Mode->BlueMask = BlueMask;
    Mode->AttributeFlags = VIDEO_MODE_COLOR | VIDEO_MODE_GRAPHICS | VIDEO_MODE_LINEAR;
    Mode->VideoMemoryBitmapWidth = Fb->HorizontalResolution;
    Mode->VideoMemoryBitmapHeight = Fb->VerticalResolution;
    Mode->DriverSpecificAttributeFlags = 0;

    UefiFbEnsureFrameBufferSize(DevExt);
    UEFIFB_LOG(1,
               "PopulateModeInfo: %ux%u stride=%lu bpp=%u pixelFmt=%u\n",
               Mode->VisScreenWidth,
               Mode->VisScreenHeight,
               (unsigned long)Mode->ScreenStride,
               (unsigned int)(Mode->BitsPerPlane * Mode->NumberOfPlanes),
               Fb->PixelFormat);
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Build/Query the mode table (GOP-only; no synthetic entries)               */
/* ------------------------------------------------------------------------- */

static BOOLEAN
UefiFbBuildModeTable(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt)
{
    ULONG gopCount = 0, i, built = 0;
    LOADER_PARAMETER_FRAMEBUFFER fb;
    PVIDEO_MODE_INFORMATION modeTable;
    SIZE_T allocSize;

    if (!InbvQueryGopModeCount(&gopCount) || gopCount == 0)
    {
        UEFIFB_LOG(0, "BuildModeTable: no GOP modes reported\n");
        return FALSE;
    }

    UEFIFB_LOG(1, "BuildModeTable: GOP reports %lu modes\n", gopCount);

    allocSize = sizeof(VIDEO_MODE_INFORMATION) * gopCount;
    modeTable = VideoPortAllocatePool(DevExt, 0, allocSize, 'bfEU');
    if (!modeTable)
        return FALSE;

    for (i = 0; i < gopCount; ++i)
    {
        if (!InbvQueryGopModeInfo(i, &fb))
            continue;

        DevExt->FrameBufferInfo = fb;
        if (!UefiFbPopulateModeInformation(DevExt))
        {
            UEFIFB_LOG(0,
                       "  GOP mode %lu rejected: invalid framebuffer info\n",
                       (unsigned long)i);
            continue;
        }

        {
            ULONG modeBpp =
                DevExt->ModeInfo.BitsPerPlane * DevExt->ModeInfo.NumberOfPlanes;

            if (modeBpp == 0)
                modeBpp = DevExt->ModeInfo.BitsPerPlane;

            if ((modeBpp != 32) && (modeBpp != 24))
            {
                UEFIFB_LOG(1,
                           "  skipping GOP mode %lu (%ux%u %lubpp)\n",
                           (unsigned long)i,
                           (unsigned int)DevExt->ModeInfo.VisScreenWidth,
                           (unsigned int)DevExt->ModeInfo.VisScreenHeight,
                           (unsigned long)modeBpp);
                continue;
            }
        }

        VideoPortMoveMemory(&modeTable[built],
                            &DevExt->ModeInfo,
                            sizeof(VIDEO_MODE_INFORMATION));
        modeTable[built].ModeIndex = built;
        UEFIFB_LOG(1,
                   "  mode[%lu] = %ux%u %ubpp stride=%lu\n",
                   (unsigned long)built,
                   (unsigned int)DevExt->ModeInfo.VisScreenWidth,
                   (unsigned int)DevExt->ModeInfo.VisScreenHeight,
                   (unsigned int)DevExt->ModeInfo.BitsPerPlane,
                   (unsigned long)DevExt->ModeInfo.ScreenStride);
        built++;
    }

    if (!UefiFbRestoreCurrentFrameBufferInfo(DevExt))
    {
        VideoPortFreePool(DevExt, modeTable);
        DevExt->ModeCount = 0;
        return FALSE;
    }

    if (built == 0)
    {
        UEFIFB_LOG(0, "BuildModeTable: no usable modes built\n");
        VideoPortFreePool(DevExt, modeTable);
        DevExt->ModeCount = 0;
        return FALSE;
    }

    DevExt->ModeTable = modeTable;
    DevExt->ModeCount = built;
    UEFIFB_LOG(1, "BuildModeTable: built %lu modes\n", built);
    UefiFbMirrorDevExtToActiveChild(DevExt);
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Mode set: actually switch the firmware to a matching GOP mode             */
/* ------------------------------------------------------------------------- */

static BOOLEAN NTAPI
UefiFbSetCurrentMode(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt,
                     _In_ ULONG RequestedMode,
                     _Out_ PSTATUS_BLOCK StatusBlock)
{
    ULONG i, gopCount = 0;
    LOADER_PARAMETER_FRAMEBUFFER fb;
    VIDEO_MODE_INFORMATION req;
    BOOLEAN switched = FALSE;
    ULONG requestedBpp;

    UEFIFB_LOG(1,
               "SetCurrentMode: request=%lu current=%lu modeSet=%u\n",
               (unsigned long)RequestedMode,
               (unsigned long)DevExt->CurrentModeIndex,
               DevExt->ModeSet ? 1u : 0u);

    if (RequestedMode >= DevExt->ModeCount || DevExt->ModeTable == NULL)
    {
        UEFIFB_LOG(0,
                   "SetCurrentMode: invalid request %lu (count=%lu)\n",
                   (unsigned long)RequestedMode,
                   (unsigned long)DevExt->ModeCount);
        StatusBlock->Status = ERROR_INVALID_PARAMETER;
        return FALSE;
    }

    if (RequestedMode == DevExt->CurrentModeIndex && DevExt->ModeSet)
    {
        UEFIFB_LOG(1, "SetCurrentMode: already at mode %lu\n", (unsigned long)RequestedMode);
        StatusBlock->Status = NO_ERROR;
        return TRUE;
    }

    VideoPortMoveMemory(&req, &DevExt->ModeTable[RequestedMode], sizeof(req));
    requestedBpp = req.BitsPerPlane * req.NumberOfPlanes;
    if (requestedBpp == 0)
        requestedBpp = req.BitsPerPlane;
    UEFIFB_LOG(1,
               "SetCurrentMode: req[%lu]=%ux%u %ubpp stride=%lu\n",
               (unsigned long)RequestedMode,
               (unsigned int)req.VisScreenWidth,
               (unsigned int)req.VisScreenHeight,
               (unsigned int)requestedBpp,
               (unsigned long)req.ScreenStride);

    if (!InbvQueryGopModeCount(&gopCount) || gopCount == 0)
    {
        UEFIFB_LOG(0, "SetCurrentMode: InbvQueryGopModeCount failed\n");
        StatusBlock->Status = ERROR_DEV_NOT_EXIST;
        return FALSE;
    }

    /* Find a GOP mode that matches requested WxH and bpp */
    for (i = 0; i < gopCount; ++i)
    {
        ULONG bpp;

        if (!InbvQueryGopModeInfo(i, &fb))
        {
            UEFIFB_LOG(0, "  GOP mode %lu query failed\n", (unsigned long)i);
            continue;
        }

        UEFIFB_LOG(1,
                   "  evaluating GOP mode %lu (%ux%u fmt=%u)\n",
                   (unsigned long)i,
                   (unsigned int)fb.HorizontalResolution,
                   (unsigned int)fb.VerticalResolution,
                   fb.PixelFormat);

        if (!UefiFbGetFramebufferBitsPerPixel(&fb, &bpp))
            continue;

        if (fb.HorizontalResolution == req.VisScreenWidth &&
            fb.VerticalResolution   == req.VisScreenHeight &&
            bpp == requestedBpp)
        {
            if (!InbvSetGopMode(i))
            {
                UEFIFB_LOG(0, "  firmware rejected GOP mode %lu\n", (unsigned long)i);
                break;
            }

            switched = TRUE;
            UEFIFB_LOG(1,
                       "  switched firmware to GOP mode %lu (%ux%u %ubpp)\n",
                       (unsigned long)i,
                       (unsigned int)fb.HorizontalResolution,
                       (unsigned int)fb.VerticalResolution,
                       (unsigned int)bpp);
            break;
        }
    }

    if (!switched)
    {
        LOADER_PARAMETER_FRAMEBUFFER currentFb;
        ULONG currentBpp;

        if (!InbvGetGopFrameBufferInfo(&currentFb) ||
            !UefiFbGetFramebufferBitsPerPixel(&currentFb, &currentBpp) ||
            currentFb.HorizontalResolution != req.VisScreenWidth ||
            currentFb.VerticalResolution   != req.VisScreenHeight ||
            currentBpp != requestedBpp)
        {
            UEFIFB_LOG(0,
                       "SetCurrentMode: firmware refused matching mode (%ux%u %ubpp, %lu GOP modes)\n",
                       (unsigned int)req.VisScreenWidth,
                       (unsigned int)req.VisScreenHeight,
                       (unsigned int)requestedBpp,
                       (unsigned long)gopCount);
            StatusBlock->Status = ERROR_INVALID_PARAMETER;
            return FALSE;
        }

        switched = TRUE;
        UEFIFB_LOG(1,
                   "SetCurrentMode: firmware already at requested mode (%ux%u %ubpp), skipping switch\n",
                   (unsigned int)req.VisScreenWidth,
                   (unsigned int)req.VisScreenHeight,
                   (unsigned int)requestedBpp);
    }

    /* Unmap any existing mapping before we refresh FB info */
    if (DevExt->MappedFrameBuffer)
    {
        if (DevExt->DirectMap)
            MmUnmapIoSpace(DevExt->MappingBase, DevExt->MappingLength);
        else
            (VOID)VideoPortUnmapMemory(DevExt, DevExt->MappingBase, NULL);

        DevExt->MappedFrameBuffer = NULL;
        DevExt->MappedLength = 0;
        DevExt->MappingBase = NULL;
        DevExt->MappingLength = 0;
        DevExt->DirectMap = FALSE;
    }

    if (!InbvGetGopFrameBufferInfo(&DevExt->FrameBufferInfo))
    {
        UEFIFB_LOG(0, "SetCurrentMode: InbvGetGopFrameBufferInfo failed\n");
        StatusBlock->Status = ERROR_DEV_NOT_EXIST;
        return FALSE;
    }

    if (!UefiFbPopulateModeInformation(DevExt))
    {
        UEFIFB_LOG(0, "SetCurrentMode: UefiFbPopulateModeInformation failed\n");
        StatusBlock->Status = ERROR_DEV_NOT_EXIST;
        return FALSE;
    }

    UefiFbEnsureFrameBufferSize(DevExt);
    if (!UefiFbRegisterAccessRange(DevExt))
    {
        UEFIFB_LOG(0, "SetCurrentMode: access range registration failed\n");
        StatusBlock->Status = ERROR_INVALID_PARAMETER;
        return FALSE;
    }

    DevExt->CurrentModeIndex = RequestedMode;
    DevExt->ModeSet = TRUE;
    UEFIFB_LOG(1,
               "SetCurrentMode: now %ux%u %ubpp stride=%lu\n",
               (unsigned int)DevExt->ModeInfo.VisScreenWidth,
               (unsigned int)DevExt->ModeInfo.VisScreenHeight,
               (unsigned int)(DevExt->ModeInfo.BitsPerPlane * DevExt->ModeInfo.NumberOfPlanes),
               (unsigned long)DevExt->ModeInfo.ScreenStride);
    UefiFbMirrorDevExtToActiveChild(DevExt);
    StatusBlock->Status = NO_ERROR;
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Queries                                                                   */
/* ------------------------------------------------------------------------- */

static BOOLEAN NTAPI
UefiFbQueryCurrentMode(_In_ PUEFIFB_DEVICE_EXTENSION DevExt,
                       _Out_ PVIDEO_MODE_INFORMATION ModeInfo,
                       _Out_ PSTATUS_BLOCK StatusBlock)
{
    VideoPortMoveMemory(ModeInfo, &DevExt->ModeInfo, sizeof(*ModeInfo));
    StatusBlock->Information = sizeof(*ModeInfo);
    StatusBlock->Status = NO_ERROR;
    return TRUE;
}

static BOOLEAN NTAPI
UefiFbQueryAvailableModes(_In_ PUEFIFB_DEVICE_EXTENSION DevExt,
                         _Out_writes_bytes_(OutputBytes) PVIDEO_MODE_INFORMATION ModeInfo,
                         _In_ ULONG OutputBytes,
                         _Out_ PSTATUS_BLOCK StatusBlock)
{
    ULONG need;

    if (DevExt->ModeCount == 0 || DevExt->ModeTable == NULL)
    {
        StatusBlock->Status = ERROR_DEV_NOT_EXIST;
        return FALSE;
    }

    need = DevExt->ModeCount * sizeof(VIDEO_MODE_INFORMATION);
    if (OutputBytes < need)
    {
        StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
        return FALSE;
    }

    VideoPortMoveMemory(ModeInfo, DevExt->ModeTable, need);
    StatusBlock->Information = need;
    StatusBlock->Status = NO_ERROR;
    return TRUE;
}

static BOOLEAN NTAPI
UefiFbQueryNumModes(_In_ PUEFIFB_DEVICE_EXTENSION DevExt,
                    _Out_ PVIDEO_NUM_MODES NumModes,
                    _Out_ PSTATUS_BLOCK StatusBlock)
{
    NumModes->NumModes = (DevExt->ModeCount != 0) ? DevExt->ModeCount : 1;
    NumModes->ModeInformationLength = sizeof(VIDEO_MODE_INFORMATION);

    StatusBlock->Information = sizeof(*NumModes);
    StatusBlock->Status = NO_ERROR;
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Mapping                                                                   */
/* ------------------------------------------------------------------------- */

static BOOLEAN
UefiFbMapFrameBufferFallback(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt,
                             _Out_ PVIDEO_MEMORY_INFORMATION MapInfo,
                             _In_ ULONG OutputBufferLength,
                             _Out_ PSTATUS_BLOCK StatusBlock)
{
    PVOID BaseAddress;
    ULONG mapLength;

    if ((DevExt->FrameBufferInfo.FrameBufferBase.QuadPart == 0) ||
        (DevExt->FrameBufferInfo.FrameBufferSize == 0))
    {
        StatusBlock->Status = ERROR_INVALID_PARAMETER;
        return FALSE;
    }

    mapLength = DevExt->FrameBufferMapLength;
    if (mapLength == 0)
    {
        ULONGLONG mapLength64 =
            UefiFbRoundToPages((ULONGLONG)DevExt->FrameBufferInfo.FrameBufferSize +
                               DevExt->FrameBufferOffset);

        if (mapLength64 == 0)
            mapLength64 = DevExt->FrameBufferInfo.FrameBufferSize;

        if (mapLength64 == 0)
        {
            StatusBlock->Status = ERROR_INVALID_PARAMETER;
            return FALSE;
        }

        mapLength = (mapLength64 > MAXULONG) ? MAXULONG : (ULONG)mapLength64;
    }

    BaseAddress = MmMapIoSpace(DevExt->FrameBufferInfo.FrameBufferBase,
                               mapLength,
                               MmWriteCombined);
    if (!BaseAddress)
        BaseAddress = MmMapIoSpace(DevExt->FrameBufferInfo.FrameBufferBase,
                                   mapLength,
                                   MmNonCached);

    if (!BaseAddress)
    {
        UEFIFB_LOG(0, "MapVideoMemory: MmMapIoSpace failed for fallback (len=%lu)\n",
                   (unsigned long)mapLength);
        StatusBlock->Status = ERROR_NOT_ENOUGH_MEMORY;
        return FALSE;
    }

    DevExt->MappingBase       = BaseAddress;
    DevExt->MappingLength     = mapLength;
    DevExt->MappedFrameBuffer = (PUCHAR)BaseAddress + DevExt->FrameBufferOffset;
    DevExt->MappedLength      = DevExt->FrameBufferInfo.FrameBufferSize;
    DevExt->DirectMap         = TRUE;

    UEFIFB_LOG(1,
               "MapVideoMemory: MmMapIoSpace base=%p mappedLen=%lu visible=%lu\n",
               DevExt->MappingBase,
               (unsigned long)DevExt->MappingLength,
               (unsigned long)DevExt->MappedLength);

    UefiFbSetMapInformation(DevExt,
                             MapInfo,
                             OutputBufferLength,
                             DevExt->MappedFrameBuffer,
                             DevExt->MappedLength,
                             StatusBlock);
    return TRUE;
}

static BOOLEAN NTAPI
UefiFbMapVideoMemory(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt,
                     _Inout_ PVIDEO_MEMORY RequestedAddress,
                     _Out_ PVIDEO_MEMORY_INFORMATION MapInfo,
                     _In_ ULONG OutputBufferLength,
                     _Out_ PSTATUS_BLOCK StatusBlock)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG VisibleLength;
    ULONG RequestedLength;
    ULONG InIoSpace = VIDEO_MEMORY_SPACE_MEMORY | VIDEO_MEMORY_SPACE_P6CACHE;
    BOOLEAN TriedWithoutCache = FALSE;
    VP_STATUS Status;

    UEFIFB_LOG(1,
               "MapVideoMemory: requestVirt=%p existing=%p size=%lu offset=%lu forceMm=%u\n",
               RequestedAddress ? RequestedAddress->RequestedVirtualAddress : NULL,
               DevExt->MappedFrameBuffer,
               (unsigned long)DevExt->FrameBufferInfo.FrameBufferSize,
               (unsigned long)DevExt->FrameBufferOffset,
               DevExt->ForceMmMap ? 1u : 0u);

    if (VideoPortGetCurrentIrql() != 0)
    {
        StatusBlock->Status = ERROR_INVALID_FUNCTION;
        return FALSE;
    }

    if (DevExt->MappedFrameBuffer != NULL)
    {
        UEFIFB_LOG(1, "MapVideoMemory: reusing existing mapping %p len=%lu\n",
                   DevExt->MappedFrameBuffer,
                   (unsigned long)DevExt->MappedLength);

        UefiFbSetMapInformation(DevExt,
                                 MapInfo,
                                 OutputBufferLength,
                                 DevExt->MappedFrameBuffer,
                                 DevExt->MappedLength,
                                 StatusBlock);
        return TRUE;
    }

    PhysicalAddress = DevExt->FrameBufferInfo.FrameBufferBase;
    VisibleLength   = DevExt->FrameBufferInfo.FrameBufferSize;
    RequestedLength = DevExt->FrameBufferMapLength;

    if ((DevExt->AccessRanges[0].RangeStart.QuadPart != PhysicalAddress.QuadPart) ||
        (DevExt->AccessRanges[0].RangeLength != RequestedLength))
    {
        (VOID)UefiFbRegisterAccessRange(DevExt);
    }

    UEFIFB_LOG(1,
               "MapVideoMemory: physical=%I64x visible=%lu requestLen=%lu\n",
               PhysicalAddress.QuadPart,
               (unsigned long)VisibleLength,
               (unsigned long)RequestedLength);

    if ((PhysicalAddress.QuadPart == 0) || (VisibleLength == 0) || (RequestedLength == 0))
    {
        UEFIFB_LOG(0,
                   "MapVideoMemory: invalid parameters (PA=%I64x vis=%lu req=%lu)\n",
                   PhysicalAddress.QuadPart,
                   (unsigned long)VisibleLength,
                   (unsigned long)RequestedLength);
        StatusBlock->Status = ERROR_INVALID_PARAMETER;
        return FALSE;
    }

    if (DevExt->ForceMmMap)
    {
        UEFIFB_LOG(1, "MapVideoMemory: using MmMapIoSpace fallback (VideoPort mapping disabled)\n");
        return UefiFbMapFrameBufferFallback(DevExt, MapInfo, OutputBufferLength, StatusBlock);
    }

    while (TRUE)
    {
        MapInfo->VideoRamBase = RequestedAddress->RequestedVirtualAddress;
        MapInfo->VideoRamLength = RequestedLength;
        ULONG MappingLength = RequestedLength;

        Status = VideoPortMapMemory(DevExt,
                                    PhysicalAddress,
                                    &MappingLength,
                                    &InIoSpace,
                                    &MapInfo->VideoRamBase);
        if (Status != NO_ERROR)
        {
            VideoPortDebugPrint(0,
                "UEFIFB: VideoPortMapMemory failed (Status=%lu, PA=%I64x, Len=%lu, Flags=0x%lx)\n",
                (unsigned long)Status,
                (ULONGLONG)PhysicalAddress.QuadPart,
                (unsigned long)RequestedLength,
                (unsigned long)InIoSpace);

            if (!TriedWithoutCache)
            {
                TriedWithoutCache = TRUE;
                InIoSpace = VIDEO_MEMORY_SPACE_MEMORY;
                continue;
            }
            break;
        }

        /* Success via VideoPort; remember the real mapping base (without the page offset) */
        DevExt->MappingBase       = MapInfo->VideoRamBase;
        DevExt->MappingLength     = MappingLength;
        DevExt->MappedFrameBuffer = (PUCHAR)DevExt->MappingBase + DevExt->FrameBufferOffset;
        DevExt->MappedLength      = VisibleLength;
        DevExt->DirectMap         = FALSE;

        if (!MmIsAddressValid(DevExt->MappedFrameBuffer))
        {
            UEFIFB_LOG(0,
                       "MapVideoMemory: VideoPort returned invalid VA %p, forcing MmMapIoSpace fallback\n",
                       DevExt->MappedFrameBuffer);
            (VOID)VideoPortUnmapMemory(DevExt, DevExt->MappingBase, NULL);
            DevExt->MappedFrameBuffer = NULL;
            DevExt->MappingBase = NULL;
            DevExt->MappingLength = 0;
            DevExt->MappedLength = 0;
            DevExt->ForceMmMap = TRUE;
            break;
        }

        UEFIFB_LOG(1,
                   "MapVideoMemory: VideoPort mapped %p (base %p) len=%lu flags=0x%lx\n",
                   DevExt->MappedFrameBuffer,
                   DevExt->MappingBase,
                   (unsigned long)VisibleLength,
                   (unsigned long)InIoSpace);

        UefiFbSetMapInformation(DevExt,
                                 MapInfo,
                                 OutputBufferLength,
                                 DevExt->MappedFrameBuffer,
                                 VisibleLength,
                                 StatusBlock);
        return TRUE;
    }

    UEFIFB_LOG(1, "MapVideoMemory: falling back to MmMapIoSpace\n");
    return UefiFbMapFrameBufferFallback(DevExt, MapInfo, OutputBufferLength, StatusBlock);
}

static BOOLEAN NTAPI
UefiFbUnmapVideoMemory(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt,
                       _In_ PVIDEO_MEMORY VideoMemory,
                       _Out_ PSTATUS_BLOCK StatusBlock)
{
    VP_STATUS Status;

    if (VideoPortGetCurrentIrql() != 0)
    {
        StatusBlock->Status = ERROR_INVALID_FUNCTION;
        return FALSE;
    }

    if (DevExt->MappedFrameBuffer == NULL ||
        VideoMemory->RequestedVirtualAddress == NULL ||
        VideoMemory->RequestedVirtualAddress != DevExt->MappedFrameBuffer)
    {
        UEFIFB_LOG(0,
                   "UnmapVideoMemory: invalid parameters (current=%p requested=%p)\n",
                   DevExt->MappedFrameBuffer,
                   VideoMemory->RequestedVirtualAddress);
        StatusBlock->Status = ERROR_INVALID_PARAMETER;
        return FALSE;
    }

    UEFIFB_LOG(1,
               "UnmapVideoMemory: base=%p direct=%u\n",
               DevExt->MappedFrameBuffer,
               DevExt->DirectMap ? 1u : 0u);

    if (DevExt->DirectMap)
    {
        MmUnmapIoSpace(DevExt->MappingBase, DevExt->MappingLength);
        Status = NO_ERROR;
    }
    else
    {
        Status = VideoPortUnmapMemory(DevExt, DevExt->MappingBase, NULL);
        if (Status != NO_ERROR)
            UEFIFB_LOG(0,
                       "UnmapVideoMemory: VideoPortUnmapMemory failed %lu\n",
                       (unsigned long)Status);
    }

    if (Status == NO_ERROR)
    {
        DevExt->MappedFrameBuffer = NULL;
        DevExt->MappedLength = 0;
        DevExt->MappingBase = NULL;
        DevExt->MappingLength = 0;
        DevExt->DirectMap = FALSE;
    }

    StatusBlock->Status = Status;
    return (Status == NO_ERROR);
}

/* ------------------------------------------------------------------------- */
/* Color caps / Power / Child                                                */
/* ------------------------------------------------------------------------- */

static BOOLEAN NTAPI
UefiFbQueryColorCapabilities(_Out_ PVIDEO_COLOR_CAPABILITIES Caps,
                             _Out_ PSTATUS_BLOCK StatusBlock)
{
    VideoPortZeroMemory(Caps, sizeof(*Caps));

    Caps->Length = sizeof(*Caps);
    Caps->AttributeFlags = VIDEO_DEVICE_COLOR;
    Caps->WhiteChromaticity_x = 3127;
    Caps->WhiteChromaticity_y = 3290;
    Caps->RedChromaticity_x = 6700;
    Caps->RedChromaticity_y = 3300;
    Caps->GreenChromaticity_x = 2100;
    Caps->GreenChromaticity_y = 7100;
    Caps->BlueChromaticity_x = 1400;
    Caps->BlueChromaticity_y = 800;
    Caps->WhiteGamma = 20000;
    Caps->RedGamma = 20000;
    Caps->GreenGamma = 20000;
    Caps->BlueGamma = 20000;

    StatusBlock->Information = sizeof(*Caps);
    StatusBlock->Status = NO_ERROR;
    return TRUE;
}

static VP_STATUS NTAPI
UefiFbSetPowerState(_In_ PVOID HwDeviceExtension,
                    _In_ ULONG HwId,
                    _In_ PVIDEO_POWER_MANAGEMENT VideoPowerControl)
{
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(HwId);
    UNREFERENCED_PARAMETER(VideoPowerControl);
    return NO_ERROR;
}

static VP_STATUS NTAPI
UefiFbGetPowerState(_In_ PVOID HwDeviceExtension,
                    _In_ ULONG HwId,
                    _Out_ PVIDEO_POWER_MANAGEMENT VideoPowerControl)
{
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(HwId);

    if (VideoPowerControl == NULL)
        return ERROR_INVALID_PARAMETER;

    VideoPowerControl->Length = sizeof(*VideoPowerControl);
    VideoPowerControl->DPMSVersion = 0x0100;
    VideoPowerControl->PowerState = VideoPowerOn;
    return NO_ERROR;
}

static BOOLEAN NTAPI
UefiFbResetHw(_In_ PVOID HwDeviceExtension,
              _In_ ULONG Columns,
              _In_ ULONG Rows)
{
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(Columns);
    UNREFERENCED_PARAMETER(Rows);
    return TRUE;
}

static VP_STATUS NTAPI
UefiFbGetVideoChildDescriptor(_In_ PVOID HwDeviceExtension,
                              _In_ PVIDEO_CHILD_ENUM_INFO ChildEnumInfo,
                              _Out_ PVIDEO_CHILD_TYPE VideoChildType,
                              _Out_writes_bytes_(ChildEnumInfo->ChildDescriptorSize) PUCHAR ChildDescriptor,
                              _Out_ PULONG UId,
                              _Out_ PULONG Unused)
{
    UNREFERENCED_PARAMETER(HwDeviceExtension);

    if (ChildEnumInfo->ChildIndex > 0)
        return ERROR_NO_MORE_DEVICES;

    if (ChildEnumInfo->ChildDescriptorSize < sizeof(UefiFbSyntheticEdid))
        return ERROR_INSUFFICIENT_BUFFER;

    *VideoChildType = Monitor;
    VideoPortMoveMemory(ChildDescriptor, (PVOID)UefiFbSyntheticEdid, sizeof(UefiFbSyntheticEdid));
    if (UId)    *UId = 0;
    if (Unused) *Unused = 0;
    return NO_ERROR;
}

/* ------------------------------------------------------------------------- */
/* StartIO / Initialize / FindAdapter / DriverEntry                          */
/* ------------------------------------------------------------------------- */

static BOOLEAN NTAPI
UefiFbStartIO(_In_ PVOID HwDeviceExtension,
              _Inout_ PVIDEO_REQUEST_PACKET RequestPacket)
{
    PUEFIFB_DEVICE_EXTENSION DevExt = HwDeviceExtension;

    UEFIFB_LOG(1, "StartIO: Ioctl=0x%lx\n", RequestPacket->IoControlCode);

    switch (RequestPacket->IoControlCode)
    {
        case IOCTL_VIDEO_QUERY_NUM_AVAIL_MODES:
        {
            if (RequestPacket->OutputBufferLength < sizeof(VIDEO_NUM_MODES))
            {
                UEFIFB_LOG(0, "StartIO: QUERY_NUM_AVAIL_MODES short buffer (%lu)\n",
                           (unsigned long)RequestPacket->OutputBufferLength);
                RequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return FALSE;
            }

            UEFIFB_LOG(1, "StartIO: QUERY_NUM_AVAIL_MODES -> %lu\n",
                       (unsigned long)((DevExt->ModeCount != 0) ? DevExt->ModeCount : 1));
            return UefiFbQueryNumModes(DevExt,
                                       (PVIDEO_NUM_MODES)RequestPacket->OutputBuffer,
                                       RequestPacket->StatusBlock);
        }

        case IOCTL_VIDEO_QUERY_AVAIL_MODES:
        {
            if (RequestPacket->OutputBufferLength < sizeof(VIDEO_MODE_INFORMATION))
            {
                UEFIFB_LOG(0, "StartIO: QUERY_AVAIL_MODES short buffer (%lu)\n",
                           (unsigned long)RequestPacket->OutputBufferLength);
                RequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return FALSE;
            }

            UEFIFB_LOG(1, "StartIO: QUERY_AVAIL_MODES returning %lu entries\n",
                       (unsigned long)((DevExt->ModeCount != 0) ? DevExt->ModeCount : 1));
            return UefiFbQueryAvailableModes(DevExt,
                                             (PVIDEO_MODE_INFORMATION)RequestPacket->OutputBuffer,
                                             RequestPacket->OutputBufferLength,
                                             RequestPacket->StatusBlock);
        }

        case IOCTL_VIDEO_QUERY_CURRENT_MODE:
        {
            if (RequestPacket->OutputBufferLength < sizeof(VIDEO_MODE_INFORMATION))
            {
                UEFIFB_LOG(0, "StartIO: QUERY_CURRENT_MODE short buffer (%lu)\n",
                           (unsigned long)RequestPacket->OutputBufferLength);
                RequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return FALSE;
            }

            UEFIFB_LOG(1, "StartIO: QUERY_CURRENT_MODE\n");
            return UefiFbQueryCurrentMode(DevExt,
                                          (PVIDEO_MODE_INFORMATION)RequestPacket->OutputBuffer,
                                          RequestPacket->StatusBlock);
        }

        case IOCTL_VIDEO_SET_CURRENT_MODE:
        {
            ULONG RequestedMode;

            if (RequestPacket->InputBufferLength < sizeof(VIDEO_MODE))
            {
                UEFIFB_LOG(0, "StartIO: SET_CURRENT_MODE short buffer (%lu)\n",
                           (unsigned long)RequestPacket->InputBufferLength);
                RequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return FALSE;
            }

            RequestedMode = ((PVIDEO_MODE)RequestPacket->InputBuffer)->RequestedMode;
            UEFIFB_LOG(1, "StartIO: SET_CURRENT_MODE RequestedMode=%lu\n",
                       (unsigned long)RequestedMode);
            return UefiFbSetCurrentMode(DevExt, RequestedMode, RequestPacket->StatusBlock);
        }

        case IOCTL_VIDEO_MAP_VIDEO_MEMORY:
        {
            if ((RequestPacket->InputBufferLength < sizeof(VIDEO_MEMORY)) ||
                (RequestPacket->OutputBufferLength < sizeof(VIDEO_MEMORY_INFORMATION)))
            {
                UEFIFB_LOG(0, "StartIO: MAP_VIDEO_MEMORY short buffers (in=%lu out=%lu)\n",
                           (unsigned long)RequestPacket->InputBufferLength,
                           (unsigned long)RequestPacket->OutputBufferLength);
                RequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return FALSE;
            }

            return UefiFbMapVideoMemory(DevExt,
                                        (PVIDEO_MEMORY)RequestPacket->InputBuffer,
                                        (PVIDEO_MEMORY_INFORMATION)RequestPacket->OutputBuffer,
                                        RequestPacket->OutputBufferLength,
                                        RequestPacket->StatusBlock);
        }

        case IOCTL_VIDEO_UNMAP_VIDEO_MEMORY:
        {
            if (RequestPacket->InputBufferLength < sizeof(VIDEO_MEMORY))
            {
                UEFIFB_LOG(0, "StartIO: UNMAP_VIDEO_MEMORY short buffer (%lu)\n",
                           (unsigned long)RequestPacket->InputBufferLength);
                RequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return FALSE;
            }

            return UefiFbUnmapVideoMemory(DevExt,
                                          (PVIDEO_MEMORY)RequestPacket->InputBuffer,
                                          RequestPacket->StatusBlock);
        }

        case IOCTL_VIDEO_QUERY_COLOR_CAPABILITIES:
        {
            if (RequestPacket->OutputBufferLength < sizeof(VIDEO_COLOR_CAPABILITIES))
            {
                RequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return FALSE;
            }

            return UefiFbQueryColorCapabilities((PVIDEO_COLOR_CAPABILITIES)RequestPacket->OutputBuffer,
                                                RequestPacket->StatusBlock);
        }

        case IOCTL_VIDEO_RESET_DEVICE:
        {
            RequestPacket->StatusBlock->Status = NO_ERROR;
            return TRUE;
        }

        case IOCTL_VIDEO_UEFIFB_QUERY_CHILD_MODES:
        {
            PUEFIFB_CHILD_MODE_RESPONSE response;
            ULONG availableModes;
            ULONG copyModes;
            ULONG required;
            PUEFIFB_CHILD_OUTPUT child;
            UEFIFB_SELECT_CHILD query;
            STATUS_BLOCK ensure = {0};

            if (RequestPacket->InputBufferLength < sizeof(UEFIFB_SELECT_CHILD) ||
                RequestPacket->OutputBufferLength <
                    FIELD_OFFSET(UEFIFB_CHILD_MODE_RESPONSE, Modes))
            {
                RequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return FALSE;
            }

            VideoPortMoveMemory(&query,
                                 RequestPacket->InputBuffer,
                                 sizeof(query));

            if (!UefiFbEnsureChildModeTable(DevExt, query.ChildIndex, &ensure))
            {
                RequestPacket->StatusBlock->Status = ensure.Status;
                return FALSE;
            }

            child = &DevExt->ChildOutputs[query.ChildIndex];
            response = (PUEFIFB_CHILD_MODE_RESPONSE)RequestPacket->OutputBuffer;
            availableModes = 0;
            if (RequestPacket->OutputBufferLength >
                FIELD_OFFSET(UEFIFB_CHILD_MODE_RESPONSE, Modes))
            {
                availableModes =
                    (RequestPacket->OutputBufferLength -
                     FIELD_OFFSET(UEFIFB_CHILD_MODE_RESPONSE, Modes)) /
                    sizeof(VIDEO_MODE_INFORMATION);
            }

            copyModes = (child->ModeCount < availableModes)
                          ? child->ModeCount
                          : availableModes;
            if (copyModes && child->ModeTable)
            {
                VideoPortMoveMemory(response->Modes,
                                     child->ModeTable,
                                     copyModes * sizeof(VIDEO_MODE_INFORMATION));
            }

            response->ChildIndex = query.ChildIndex;
            response->ModeCount = child->ModeCount;
            response->ReturnedModes = copyModes;

            required = FIELD_OFFSET(UEFIFB_CHILD_MODE_RESPONSE, Modes) +
                       copyModes * sizeof(VIDEO_MODE_INFORMATION);
            RequestPacket->StatusBlock->Information = required;
            RequestPacket->StatusBlock->Status =
                (copyModes == child->ModeCount) ? NO_ERROR : ERROR_MORE_DATA;
            return TRUE;
        }

        /* Optional vendor IOCTL: advertise that we DO real mode switches now */
        case IOCTL_VIDEO_UEFIFB_SELECT_CHILD:
        {
            if (RequestPacket->InputBufferLength < sizeof(UEFIFB_SELECT_CHILD))
            {
                RequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return FALSE;
            }

            PUEFIFB_SELECT_CHILD select = (PUEFIFB_SELECT_CHILD)RequestPacket->InputBuffer;
            return UefiFbActivateChild(DevExt, select->ChildIndex, RequestPacket->StatusBlock);
        }

        case IOCTL_VIDEO_UEFIFB_QUERY_CAPS:
        {
            if (RequestPacket->OutputBufferLength < sizeof(UEFIFB_CAPS))
            {
                RequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return FALSE;
            }

            PUEFIFB_CAPS caps = (PUEFIFB_CAPS)RequestPacket->OutputBuffer;
            caps->Version = UEFIFB_CAPS_VERSION;
            caps->Caps = 0;
            if (DevExt->FrameBufferLength64 > MAXULONG)
                caps->Caps |= UEFIFB_CAP_LARGE_FB;
            if (DevExt->ChildCount > 1)
                caps->Caps |= UEFIFB_CAP_MULTI_OUTPUT;
            caps->OutputCount = DevExt->ChildCount;
            caps->PrimaryChild = DevExt->ActiveChild;
            caps->FrameBufferLength = DevExt->FrameBufferLength64;
            RequestPacket->StatusBlock->Information = sizeof(*caps);
            RequestPacket->StatusBlock->Status = NO_ERROR;
            return TRUE;
        }

        default:
            break;
    }

    RequestPacket->StatusBlock->Status = ERROR_INVALID_FUNCTION;
    return FALSE;
}

static BOOLEAN NTAPI
UefiFbInitialize(_In_ PVOID HwDeviceExtension)
{
    PUEFIFB_DEVICE_EXTENSION DevExt = HwDeviceExtension;
    UEFIFB_LOG(0,
               "Initialize: DevExt=%p modes=%lu currentRange=%I64x len=%lu\n",
               DevExt,
               (unsigned long)DevExt->ModeCount,
               DevExt->AccessRanges[0].RangeStart.QuadPart,
               (unsigned long)DevExt->AccessRanges[0].RangeLength);
    DevExt->ModeSet = FALSE;
    return TRUE;
}

static VP_STATUS NTAPI
UefiFbFindAdapter(_In_ PVOID HwDeviceExtension,
                  _In_opt_ PVOID HwContext,
                  _In_opt_ PWSTR ArgumentString,
                  _Inout_ PVIDEO_PORT_CONFIG_INFO ConfigInfo,
                  _Out_ PUCHAR Again)
{
    PUEFIFB_DEVICE_EXTENSION DevExt = HwDeviceExtension;
    LOADER_PARAMETER_FRAMEBUFFER Fb;
    static const WCHAR AdapterString[] = L"UEFI GOP Framebuffer";
    static const WCHAR ChipType[] = L"Firmware GOP";
    static const WCHAR DacType[] = L"Internal DAC";

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(ArgumentString);

    if (ConfigInfo->Length < sizeof(*ConfigInfo))
        return ERROR_INVALID_PARAMETER;

    UEFIFB_LOG(1, "FindAdapter: Context=%p Arg=%p\n", HwContext, ArgumentString);

    if (!InbvGetGopFrameBufferInfo(&Fb))
    {
        VideoPortDebugPrint(0,
                            "UEFIFB: loader did not provide GOP framebuffer info; miniport unavailable\n");
        return ERROR_DEV_NOT_EXIST;
    }

    if ((Fb.FrameBufferBase.QuadPart == 0) ||
        (Fb.FrameBufferSize == 0) ||
        (Fb.HorizontalResolution == 0) ||
        (Fb.VerticalResolution == 0))
    {
        return ERROR_DEV_NOT_EXIST;
    }

    DevExt->FrameBufferInfo = Fb;
    DevExt->FrameBufferLength64 = Fb.FrameBufferSize;
    UEFIFB_LOG(1, "FindAdapter: GOP base=%I64x size=%lu res=%ux%u fmt=%u\n",
               Fb.FrameBufferBase.QuadPart,
               Fb.FrameBufferSize,
               Fb.HorizontalResolution,
               Fb.VerticalResolution,
               Fb.PixelFormat);
    DevExt->ModeCount = 0;
    DevExt->ModeTable = NULL;
    DevExt->MappedFrameBuffer = NULL;
    DevExt->MappingBase = NULL;
    DevExt->MappedLength = 0;
    DevExt->MappingLength = 0;
    DevExt->FrameBufferOffset = 0;
    DevExt->FrameBufferMapLength = 0;
    DevExt->ModeSet = FALSE;
    DevExt->DirectMap = FALSE;
    DevExt->ForceMmMap = FALSE;
    DevExt->CurrentModeIndex = 0;

    if (!UefiFbPopulateModeInformation(DevExt))
        return ERROR_DEV_NOT_EXIST;

    if (!UefiFbBuildModeTable(DevExt))
        return ERROR_NOT_ENOUGH_MEMORY;

    if (DevExt->ModeCount != 0 && DevExt->ModeTable != NULL)
        DevExt->CurrentModeIndex = DevExt->ModeTable[0].ModeIndex;
    else
        DevExt->CurrentModeIndex = 0;

    UefiFbSelectPreferredMode(DevExt);
    UefiFbInitializeChildOutputs(DevExt);

    ConfigInfo->NumEmulatorAccessEntries = 0;
    ConfigInfo->EmulatorAccessEntries = NULL;
    ConfigInfo->EmulatorAccessEntriesContext = 0;
    ConfigInfo->HardwareStateSize = 0;
    ConfigInfo->VdmPhysicalVideoMemoryAddress.QuadPart = Fb.FrameBufferBase.QuadPart;
    ConfigInfo->VdmPhysicalVideoMemoryLength = Fb.FrameBufferSize;
    ConfigInfo->SystemIoBusNumber = 0;
    ConfigInfo->AdapterInterfaceType = PCIBus;
    ConfigInfo->BusInterruptLevel = 0;
    ConfigInfo->BusInterruptVector = 0;
    ConfigInfo->InterruptMode = LevelSensitive;

    UEFIFB_LOG(0,
               "FindAdapter: DevExt=%p GOP base=%I64x size=%lu bus=%lu\n",
               DevExt,
               DevExt->FrameBufferInfo.FrameBufferBase.QuadPart,
               (unsigned long)DevExt->FrameBufferInfo.FrameBufferSize,
               (unsigned long)ConfigInfo->SystemIoBusNumber);
    UEFIFB_LOG(0, "FindAdapter: registering access range base=%I64x length=%lu\n",
               DevExt->FrameBufferInfo.FrameBufferBase.QuadPart,
               (unsigned long)DevExt->FrameBufferMapLength);

    if (!UefiFbRegisterAccessRange(DevExt))
    {
        UEFIFB_LOG(0, "FindAdapter: access range registration failed\n");
        return ERROR_INVALID_PARAMETER;
    }

    *Again = 0;

    VideoPortSetRegistryParameters(DevExt,
                                   L"HardwareInformation.AdapterString",
                                   (PVOID)AdapterString,
                                   sizeof(AdapterString));
    VideoPortSetRegistryParameters(DevExt,
                                   L"HardwareInformation.ChipType",
                                   (PVOID)ChipType,
                                   sizeof(ChipType));
    VideoPortSetRegistryParameters(DevExt,
                                   L"HardwareInformation.DacType",
                                   (PVOID)DacType,
                                   sizeof(DacType));
    VideoPortSetRegistryParameters(DevExt,
                                   L"HardwareInformation.MemorySize",
                                   &Fb.FrameBufferSize,
                                   sizeof(Fb.FrameBufferSize));

    UEFIFB_LOG(1, "FindAdapter: initialization complete (modes=%lu)\n", DevExt->ModeCount);
    return NO_ERROR;
}

ULONG
NTAPI
DriverEntry(_In_ PVOID Context1,
            _In_ PVOID Context2)
{
    VIDEO_HW_INITIALIZATION_DATA InitData;
    VideoPortZeroMemory(&InitData, sizeof(InitData));

    InitData.HwInitDataSize = sizeof(InitData);
    InitData.AdapterInterfaceType = PCIBus;
    InitData.HwFindAdapter = UefiFbFindAdapter;
    InitData.HwInitialize = UefiFbInitialize;
    InitData.HwStartIO = UefiFbStartIO;
    InitData.HwResetHw = UefiFbResetHw;
    InitData.HwGetPowerState = UefiFbGetPowerState;
    InitData.HwSetPowerState = UefiFbSetPowerState;
    InitData.HwGetVideoChildDescriptor = UefiFbGetVideoChildDescriptor;
    InitData.HwDeviceExtensionSize = sizeof(UEFIFB_DEVICE_EXTENSION);
    InitData.HwLegacyResourceList = NULL;
    InitData.HwLegacyResourceCount = 0;

    return VideoPortInitialize(Context1, Context2, &InitData, NULL);
}
