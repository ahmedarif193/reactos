/*
 * PROJECT:         ReactOS Boot Loader
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            win32ss/drivers/miniport/vmx_svga/vmx_svga.c
 * PURPOSE:         VMWARE SVGA-II Card Main Driver File
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include "precomp.h"
#include <vmware/vmx_ioctl.h>
#include <stddef.h>

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_InterlockedExchange)
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedOr)
#pragma intrinsic(_InterlockedAnd)
#endif

#define NDEBUG
#include <debug.h>

/* Local helpers & constants ***********************************************/

/*
 * NOTE(workaround backlog):
 *  - IRQ support now rides on the videoprt trampoline; vmx_svga enables SVGA_REG_IRQMASK, batches
 *    interrupts into a single DPC, and signals fence completions for callers waiting on fences.
 *  - Multi-pass legacy detection now advertises additional heads when the device exposes the
 *    SVGA_CAP_MULTIMON capability. Displays are currently mirrored; extend the DISPLAY_* register
 *    programming to support independent modes and positioning.
 *  - Child/PnP reporting emits synthetic EDIDs per head and handles hotplug, but registry-backed
 *    UIds/INF binding remain TODO. Consider fetching host EDIDs via SVGA FIFO once videoprt mapping
 *    issues are solved.
 *  - FIFO acceleration remains register-only. Once stability is proven, plumb SVGA FIFO
 *    commands (front buffer moves, blits) and add a fence/interrupt handler.
 *  - ReactOS videoprt maps only the front-buffer aperture in legacy mode; zeroing the full
 *    VRAM quota tramples adjacent driver code. Clamp framebuffer clearing to the mapped
 *    aperture (FrameBufferLength) until the port exposes a larger mapping primitive.
 *  - Service integration: vmx_svga is installed as a boot-start miniport in the `Video`
 *    load group (see vmx_svga.inf). Keep that StartType when packaging LiveCD images.
 */

/* SVGA_REG_ENABLE values (kept here to avoid touching headers) */
#define SVGA_REG_ENABLE_DISABLE   0
#define SVGA_REG_ENABLE_ENABLE    1
#define SVGA_REG_ENABLE_HIDE      2

/* Minimal FIFO dword indices (byte offsets live in the values) */
#define SVGA_FIFO_MIN        0
#define SVGA_FIFO_MAX        1
#define SVGA_FIFO_NEXT_CMD   2
#define SVGA_FIFO_STOP       3

#define VMX_TAG 'amvV'

#define VMX_FLAG_NO_FB_MAP        0x0001
#define VMX_FLAG_NO_FIFO_MAP      0x0002
#define VMX_FLAG_NO_PORT_MAP      0x0004
#define VMX_FLAG_HWERR_REPORTED   0x0008
#define VMX_FLAG_VERBOSE_LOGGING  0x0010

#define VMX_INTR_STATE_DPC_QUEUED   0x00000001

static const WCHAR VmxRunModeRegisterOnly[] = L"register-only";
static const WCHAR VmxRunModeFramebuffer[]  = L"framebuffer-mapped";

#define VMX_FENCE_WAIT_SLICE_MS     100
#define VMX_FENCE_WAIT_TIMEOUT_MS   5000

static __inline BOOLEAN VmxFramebufferMapped(_In_ PHW_DEVICE_EXTENSION DevExt)
{
    return ((DevExt->Flags & VMX_FLAG_NO_FB_MAP) == 0) && (DevExt->FrameBufferBase != NULL);
}

static __inline BOOLEAN VmxFifoMapped(_In_ PHW_DEVICE_EXTENSION DevExt)
{
    return ((DevExt->Flags & VMX_FLAG_NO_FIFO_MAP) == 0) && (DevExt->Fifo != NULL);
}

static __inline BOOLEAN VmxRegisterOnlyMode(_In_ PHW_DEVICE_EXTENSION DevExt)
{
    return !VmxFramebufferMapped(DevExt);
}

#define PCI_ADDRESS_IO_SPACE              0x00000001
#define PCI_ADDRESS_MEMORY_TYPE_MASK      0x00000006
#define PCI_ADDRESS_MEMORY_TYPE_64BIT     0x00000004
#define PCI_ADDRESS_MEMORY_PREFETCH       0x00000008
#define PCI_ADDRESS_MEMORY_ADDRESS_MASK   0xFFFFFFF0
#define PCI_ADDRESS_IO_ADDRESS_MASK       0xFFFFFFFC
#define PCI_ENABLE_IO_SPACE               0x0001
#define PCI_ENABLE_MEMORY_SPACE           0x0002
#define PCI_ENABLE_BUS_MASTER             0x0004

typedef struct _VMX_PCI_COMMON_HEADER
{
    USHORT VendorID;
    USHORT DeviceID;
    USHORT Command;
    USHORT Status;
    UCHAR RevisionID;
    UCHAR ProgIf;
    UCHAR SubClass;
    UCHAR BaseClass;
    UCHAR CacheLineSize;
    UCHAR LatencyTimer;
    UCHAR HeaderType;
    UCHAR BIST;
    ULONG BaseAddresses[6];
    ULONG CIS;
    USHORT SubVendorID;
    USHORT SubSystemID;
    ULONG ROMBaseAddress;
    UCHAR CapabilitiesPtr;
    UCHAR Reserved1[3];
    ULONG Reserved2;
    UCHAR InterruptLine;
    UCHAR InterruptPin;
    UCHAR MinimumGrant;
    UCHAR MaximumLatency;
} VMX_PCI_COMMON_HEADER, *PVMX_PCI_COMMON_HEADER;

/* A small, ordered catalogue of common 60Hz modes; will be filtered by caps */
typedef struct _VMX_SIZE { USHORT X, Y; } VMX_SIZE;
static const VMX_SIZE VmxCommonModes[] = {
    {  640,  480 }, {  800,  600 }, { 1024,  768 }, { 1152,  864 },
    { 1280,  720 }, { 1280,  800 }, { 1280, 1024 }, { 1360,  768 },
    { 1366,  768 }, { 1440,  900 }, { 1600,  900 }, { 1600, 1200 },
    { 1680, 1050 }, { 1920, 1080 }, { 1920, 1200 }, { 2048, 1152 },
    { 2560, 1440 }, { 2560, 1600 }, { 2880, 1800 }, { 3200, 1800 },
    { 3840, 2160 }
};

/* Forward decls */
static BOOLEAN VmxSetMode(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                          _In_ ULONG Width, _In_ ULONG Height, _In_ ULONG Bpp);

static VOID VmxClearFrameBuffer(_Inout_ PHW_DEVICE_EXTENSION DevExt);

static VOID VmxBuildModeInfo(_In_ VMX_SIZE Size,
                             _Out_ PVIDEO_MODE_INFORMATION Mode);

static ULONG VmxQueryAndClampModes(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                                   _Out_writes_(MaxCount) PVIDEO_MODE_INFORMATION Modes,
                                   _In_ ULONG MaxCount);

static VOID VmxFifoInit(_Inout_ PHW_DEVICE_EXTENSION DevExt);
static VOID NTAPI VmxInterruptDpc(PVOID HwDeviceExtension, PVOID Context);

static VP_STATUS VmxMapResources(_Inout_ PHW_DEVICE_EXTENSION DevExt);
static VOID VmxEnsureFrameBufferCapacity(_Inout_ PHW_DEVICE_EXTENSION DevExt, _In_ ULONG RequiredBytes);
static VOID VmxComputeSurfaceBounds(_Out_ PULONG OutWidth,
                                    _Out_ PULONG OutHeight,
                                    _In_ ULONG FallbackWidth,
                                    _In_ ULONG FallbackHeight);
static VOID VmxSyncDisplayCount(_Inout_ PHW_DEVICE_EXTENSION DevExt);
static BOOLEAN VmxFifoAvailable(_In_ PHW_DEVICE_EXTENSION DevExt);
static BOOLEAN VmxFifoReserveBytes(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                                   _In_ ULONG Bytes,
                                   _Out_ PULONG Offset,
                                   _Outptr_result_bytebuffer_(Bytes) PULONG *FifoPtr);
static VOID VmxFifoCommitBytes(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                               _In_ ULONG Offset,
                               _In_ ULONG Bytes);
static VOID VmxFifoSync(_Inout_ PHW_DEVICE_EXTENSION DevExt);
static BOOLEAN VmxFifoEmitFence(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                                _Out_opt_ PULONG FenceValue);
static BOOLEAN VmxFifoWaitOnFence(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                                  _In_ ULONG FenceValue);
static BOOLEAN VmxFifoCmdUpdate(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                                _In_ const VMWARE_VIDEO_RECT *Rect);
static BOOLEAN VmxFifoCmdRectCopy(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                                  _In_ ULONG SrcX,
                                  _In_ ULONG SrcY,
                                  _In_ ULONG DestX,
                                  _In_ ULONG DestY,
                                  _In_ ULONG Width,
                                  _In_ ULONG Height);
static BOOLEAN VmxFifoCmdFrontFill(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                                   _In_ ULONG Color,
                                   _In_ ULONG X,
                                   _In_ ULONG Y,
                                   _In_ ULONG Width,
                                   _In_ ULONG Height);

ULONG
NTAPI
VmxReadUlong(IN PHW_DEVICE_EXTENSION DeviceExtension,
             IN ULONG Index);

VOID
NTAPI
VmxWriteUlong(IN PHW_DEVICE_EXTENSION DeviceExtension,
              IN ULONG Index,
              IN ULONG Value);

BOOLEAN
NTAPI
VmxResetHw(IN PVOID DeviceExtension,
           IN ULONG Columns,
           IN ULONG Rows);

static __inline VOID VmxWaitNotBusy(_Inout_ PHW_DEVICE_EXTENSION DevExt)
{
    /* SVGA_REG_BUSY == non-zero while device is processing */
    while (VmxReadUlong(DevExt, SVGA_REG_BUSY) != 0) { /* spin */ }
}

static __inline VOID VmxMemoryBarrier(VOID)
{
#if defined(_MSC_VER)
    MemoryBarrier();
#else
    __sync_synchronize();
#endif
}

static __inline BOOLEAN VmxFencePending(_In_ ULONG CompletedFence,
                                        _In_ ULONG TargetFence)
{
    return (LONG)(CompletedFence - TargetFence) < 0;
}

#if DBG
static BOOLEAN VmxDefaultVerboseLogging = TRUE;
#else
static BOOLEAN VmxDefaultVerboseLogging = FALSE;
#endif

static __inline LONG VmxAtomicExchangeLong(volatile LONG *Target, LONG Value)
{
#if defined(_MSC_VER)
    return _InterlockedExchange((volatile long*)Target, Value);
#else
    return __sync_lock_test_and_set(Target, Value);
#endif
}

static __inline BOOLEAN VmxAtomicCompareExchangeLong(volatile LONG *Target,
                                                     LONG Expected,
                                                     LONG Value)
{
#if defined(_MSC_VER)
    return _InterlockedCompareExchange((volatile long*)Target, Value, Expected) == Expected;
#else
    return __sync_bool_compare_and_swap(Target, Expected, Value);
#endif
}

static __inline LONG VmxAtomicOrFetchLong(volatile LONG *Target, LONG Mask)
{
#if defined(_MSC_VER)
    return _InterlockedOr((volatile long*)Target, Mask) | Mask;
#else
    return __sync_or_and_fetch(Target, Mask);
#endif
}

static __inline LONG VmxAtomicAndFetchLong(volatile LONG *Target, LONG Mask)
{
#if defined(_MSC_VER)
    return _InterlockedAnd((volatile long*)Target, Mask) & Mask;
#else
    return __sync_and_and_fetch(Target, Mask);
#endif
}

static __inline BOOLEAN VmxClampSpanToLimit(_In_ ULONG Position,
                                            _In_ ULONG Limit,
                                            _Inout_ PULONG Length)
{
    if (Limit == 0)
    {
        *Length = 0;
        return FALSE;
    }

    if (Position >= Limit)
    {
        *Length = 0;
        return FALSE;
    }

    if (*Length > (Limit - Position))
        *Length = Limit - Position;

    return (*Length != 0);
}

static __inline BOOLEAN VmxGetSurfaceBounds(_In_ PHW_DEVICE_EXTENSION DevExt,
                                            _Out_ PULONG Width,
                                            _Out_ PULONG Height)
{
    ULONG surfaceWidth = DevExt->CurrentMode.VisScreenWidth;
    ULONG surfaceHeight = DevExt->CurrentMode.VisScreenHeight;

    if (surfaceWidth == 0 || surfaceHeight == 0)
    {
        surfaceWidth = VmxReadUlong(DevExt, SVGA_REG_WIDTH);
        surfaceHeight = VmxReadUlong(DevExt, SVGA_REG_HEIGHT);
    }

    if (surfaceWidth == 0 || surfaceHeight == 0)
        return FALSE;

    *Width = surfaceWidth;
    *Height = surfaceHeight;
    return TRUE;
}

static VOID
VmxTraceEdidDescriptor(_In_ PHW_DEVICE_EXTENSION DevExt,
                       _In_reads_(128) const UCHAR *Edid)
{
    ULONG row;

    if ((DevExt->Flags & VMX_FLAG_VERBOSE_LOGGING) == 0)
        return;

    for (row = 0; row < 128; row += 16)
    {
        DPRINT1("VMX: EDID[%02lu] %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
                row,
                Edid[row + 0],
                Edid[row + 1],
                Edid[row + 2],
                Edid[row + 3],
                Edid[row + 4],
                Edid[row + 5],
                Edid[row + 6],
                Edid[row + 7],
                Edid[row + 8],
                Edid[row + 9],
                Edid[row + 10],
                Edid[row + 11],
                Edid[row + 12],
                Edid[row + 13],
                Edid[row + 14],
                Edid[row + 15]);
    }
}

#if DBG
static VOID VmxFenceSelfTest(VOID)
{
    ASSERT(VmxFencePending(0, 1));
    ASSERT(!VmxFencePending(1, 0));
    ASSERT(VmxFencePending(0xFFFFFFFFu, 1));
    ASSERT(!VmxFencePending(1, 0xFFFFFFFFu));
}
#else
#define VmxFenceSelfTest()
#endif

/* GLOBALS ********************************************************************/

PHW_DEVICE_EXTENSION VmxDeviceExtensionArray[SVGA_MAX_DISPLAYS];
static ULONG VmxExpectedDisplays = 1;
static ULONG VmxEnumeratedDisplays = 0;
static ULONG VmxDisplayWidths[SVGA_MAX_DISPLAYS];
static ULONG VmxDisplayHeights[SVGA_MAX_DISPLAYS];
static ULONG VmxDisplayXOffsets[SVGA_MAX_DISPLAYS];
static ULONG VmxDisplayYOffsets[SVGA_MAX_DISPLAYS];
static WCHAR AdapterString[] = L"VMware SVGA II";

/* FUNCTIONS ******************************************************************/

ULONG
NTAPI
VmxReadUlong(IN PHW_DEVICE_EXTENSION DeviceExtension,
             IN ULONG Index)
{
    /* Program the index first, then read the value */
    VideoPortWritePortUlong(DeviceExtension->IndexPort, Index);
    return VideoPortReadPortUlong(DeviceExtension->ValuePort);
}

VOID
NTAPI
VmxWriteUlong(IN PHW_DEVICE_EXTENSION DeviceExtension,
              IN ULONG Index,
              IN ULONG Value)
{
    /* Program the index first, then write the value */
    VideoPortWritePortUlong(DeviceExtension->IndexPort, Index);
    VideoPortWritePortUlong(DeviceExtension->ValuePort, Value);
}

#define VMX_IRQMASK_DEFAULT (SVGA_IRQFLAG_ANY_FENCE | SVGA_IRQFLAG_FIFO_PROGRESS)

static VOID
VmxConfigureIrq(_Inout_ PHW_DEVICE_EXTENSION DevExt, _In_ BOOLEAN Enable)
{
    if (!(DevExt->Capabilities & SVGA_CAP_IRQMASK) || DevExt->IndexPort == NULL)
        return;

    {
        PULONG irqStatusPort = (PULONG)((PUCHAR)DevExt->IndexPort + (SVGA_IRQSTATUS_PORT * sizeof(ULONG)));

        if (Enable)
        {
            (VOID)VideoPortReadPortUlong(irqStatusPort);
            VmxAtomicExchangeLong((volatile LONG *)&DevExt->PendingIrqStatus, 0);
            VmxAtomicExchangeLong((volatile LONG *)&DevExt->InterruptState, 0);
            VmxWriteUlong(DevExt, SVGA_REG_IRQMASK, VMX_IRQMASK_DEFAULT);
        }
        else
        {
            VmxWriteUlong(DevExt, SVGA_REG_IRQMASK, 0);
            (VOID)VideoPortReadPortUlong(irqStatusPort);
            VmxAtomicExchangeLong((volatile LONG *)&DevExt->PendingIrqStatus, 0);
            VmxAtomicExchangeLong((volatile LONG *)&DevExt->InterruptState, 0);
            if (DevExt->SyncEvent) VideoPortSetEvent(DevExt, DevExt->SyncEvent);
        }
    }
}

static VOID
VmxProgramDisplayTopology(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                          _In_ ULONG Width,
                          _In_ ULONG Height)
{
    if (!(DevExt->Capabilities & SVGA_CAP_DISPLAY_TOPOLOGY))
        return;

    if (DevExt->DisplayIndex >= SVGA_MAX_DISPLAYS)
        return;

    VmxDisplayWidths[DevExt->DisplayIndex] = Width;
    VmxDisplayHeights[DevExt->DisplayIndex] = Height;

    if (DevExt->DisplayIndex != 0 &&
        VmxDisplayXOffsets[DevExt->DisplayIndex] == 0 &&
        VmxDisplayYOffsets[DevExt->DisplayIndex] == 0)
    {
        ULONG offsetX = 0;
        ULONG idx;

        for (idx = 0; idx < DevExt->DisplayIndex && idx < SVGA_MAX_DISPLAYS; ++idx)
        {
            ULONG prevWidth = VmxDisplayWidths[idx];
            if (prevWidth == 0)
                prevWidth = Width;
            offsetX += prevWidth;
        }

        VmxDisplayXOffsets[DevExt->DisplayIndex] = offsetX;
    }

    /* Push the cached placement for this head down to the SVGA registers. */
    VmxWriteUlong(DevExt, SVGA_REG_DISPLAY_ID, DevExt->DisplayIndex);
    VmxWriteUlong(DevExt, SVGA_REG_DISPLAY_IS_PRIMARY, (DevExt->DisplayIndex == 0) ? 1 : 0);
    VmxWriteUlong(DevExt, SVGA_REG_DISPLAY_WIDTH, Width);
    VmxWriteUlong(DevExt, SVGA_REG_DISPLAY_HEIGHT, Height);
    VmxWriteUlong(DevExt, SVGA_REG_DISPLAY_POSITION_X, VmxDisplayXOffsets[DevExt->DisplayIndex]);
    VmxWriteUlong(DevExt, SVGA_REG_DISPLAY_POSITION_Y, VmxDisplayYOffsets[DevExt->DisplayIndex]);
    VmxWriteUlong(DevExt, SVGA_REG_DISPLAY_ID, SVGA_INVALID_DISPLAY_ID);
}

static VOID
VmxUpdateGlobalSurface(_Inout_ PHW_DEVICE_EXTENSION PrimaryExt,
                       _In_ ULONG FallbackWidth,
                       _In_ ULONG FallbackHeight,
                       _In_ ULONG BytesPerPixel)
{
    ULONG totalWidth;
    ULONG totalHeight;

    if (!PrimaryExt)
        return;

    if (FallbackWidth == 0)
        FallbackWidth = PrimaryExt->CurrentMode.VisScreenWidth ? PrimaryExt->CurrentMode.VisScreenWidth : 800;

    if (FallbackHeight == 0)
        FallbackHeight = PrimaryExt->CurrentMode.VisScreenHeight ? PrimaryExt->CurrentMode.VisScreenHeight : 600;

    if (BytesPerPixel == 0)
    {
        ULONG bpp = PrimaryExt->CurrentMode.BitsPerPlane ? PrimaryExt->CurrentMode.BitsPerPlane : 32;
        BytesPerPixel = (bpp >= 8) ? (bpp / 8) : 4;
    }

    VmxComputeSurfaceBounds(&totalWidth, &totalHeight, FallbackWidth, FallbackHeight);

    VmxWriteUlong(PrimaryExt, SVGA_REG_WIDTH, totalWidth);
    VmxWriteUlong(PrimaryExt, SVGA_REG_HEIGHT, totalHeight);
    if (PrimaryExt->Capabilities & SVGA_CAP_PITCHLOCK)
        VmxWriteUlong(PrimaryExt, SVGA_REG_PITCHLOCK, totalWidth * BytesPerPixel);
}

/* Keeps the guest-visible front buffer mapped to the actual surface size. */
static VOID
VmxEnsureFrameBufferCapacity(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                             _In_ ULONG RequiredBytes)
{
    PVOID oldBase;
    ULONG oldLength;

    if (DevExt->FrameBuffer.QuadPart == 0)
        return;

    if (RequiredBytes == 0)
        return;

    ASSERT(VideoPortGetCurrentIrql() == 0); /* PASSIVE_LEVEL */

    if (DevExt->FrameBufferBase != NULL && DevExt->FrameBufferLength == RequiredBytes)
        return;

    oldBase = DevExt->FrameBufferBase;
    oldLength = DevExt->FrameBufferLength;

    if (oldBase)
    {
        VideoPortFreeDeviceBase(DevExt, oldBase);
        DevExt->FrameBufferBase = NULL;
    }

    /*
     * Use VIDEO_MEMORY_SPACE_P6CACHE to request write-combined caching for the
     * framebuffer. This matches the cache attributes used by UEFI GOP during boot.
     * Without this flag, MmMapIoSpace uses MmNonCached which conflicts with the
     * existing write-combined mapping, causing the memory manager to reject it.
     */
    DevExt->FrameBufferBase = VideoPortGetDeviceBase(DevExt,
                                                     DevExt->FrameBuffer,
                                                     RequiredBytes,
                                                     VIDEO_MEMORY_SPACE_MEMORY | VIDEO_MEMORY_SPACE_P6CACHE);
    if (!DevExt->FrameBufferBase)
    {
        DPRINT1("VMX: framebuffer remap len=0x%lx failed; restoring previous mapping\n",
                RequiredBytes);

        DevExt->Flags |= VMX_FLAG_NO_FB_MAP;

        if (oldBase)
        {
            DevExt->FrameBufferBase = VideoPortGetDeviceBase(DevExt,
                                                             DevExt->FrameBuffer,
                                                             oldLength,
                                                             VIDEO_MEMORY_SPACE_MEMORY | VIDEO_MEMORY_SPACE_P6CACHE);
            if (DevExt->FrameBufferBase)
            {
                DevExt->FrameBufferLength = oldLength;
                DevExt->Flags &= ~VMX_FLAG_NO_FB_MAP;
            }
        }

        return;
    }

    DevExt->FrameBufferLength = RequiredBytes;
    DevExt->Flags &= ~VMX_FLAG_NO_FB_MAP;

    DPRINT1("VMX: framebuffer remapped len=0x%lx -> %p\n",
            DevExt->FrameBufferLength,
            DevExt->FrameBufferBase);
}

/* Returns the bounding box that contains every programmed head. */
static VOID
VmxComputeSurfaceBounds(_Out_ PULONG OutWidth,
                        _Out_ PULONG OutHeight,
                        _In_ ULONG FallbackWidth,
                        _In_ ULONG FallbackHeight)
{
    ULONG totalWidth = 0;
    ULONG totalHeight = 0;
    ULONG idx;

    for (idx = 0; idx < VmxExpectedDisplays && idx < SVGA_MAX_DISPLAYS; ++idx)
    {
        ULONG headWidth = VmxDisplayWidths[idx];
        ULONG headHeight = VmxDisplayHeights[idx];
        ULONG headX = VmxDisplayXOffsets[idx];
        ULONG headY = VmxDisplayYOffsets[idx];

        if (headWidth == 0)
            headWidth = FallbackWidth;
        if (headHeight == 0)
            headHeight = FallbackHeight;

        if (headX + headWidth > totalWidth)
            totalWidth = headX + headWidth;
        if (headY + headHeight > totalHeight)
            totalHeight = headY + headHeight;
    }

    if (totalWidth == 0)
        totalWidth = FallbackWidth;
    if (totalHeight == 0)
        totalHeight = FallbackHeight;

    *OutWidth = totalWidth;
    *OutHeight = totalHeight;
}

/* Detect host display count changes and trigger a child re-enumeration. */
static VOID
VmxSyncDisplayCount(_Inout_ PHW_DEVICE_EXTENSION DevExt)
{
    ULONG hostDisplays;
    ULONG idx;

    if (!(DevExt->Capabilities & SVGA_CAP_MULTIMON))
        return;

    if (DevExt->DisplayIndex != 0)
        return;

    hostDisplays = VmxReadUlong(DevExt, SVGA_REG_NUM_DISPLAYS);
    if (hostDisplays == 0)
        hostDisplays = 1;
    if (hostDisplays > SVGA_MAX_DISPLAYS)
        hostDisplays = SVGA_MAX_DISPLAYS;

    if (hostDisplays == VmxExpectedDisplays)
        return;

    DPRINT1("VMX: host display topology changed (%lu -> %lu)\n",
            VmxExpectedDisplays,
            hostDisplays);

    VmxExpectedDisplays = hostDisplays;
    VmxWriteUlong(DevExt, SVGA_REG_NUM_GUEST_DISPLAYS, hostDisplays);

    for (idx = hostDisplays; idx < SVGA_MAX_DISPLAYS; ++idx)
    {
        VmxDisplayWidths[idx] = 0;
        VmxDisplayHeights[idx] = 0;
        VmxDisplayXOffsets[idx] = 0;
        VmxDisplayYOffsets[idx] = 0;
    }

    VideoPortEnumerateChildren(DevExt, NULL);
}

static BOOLEAN
VmxFifoAvailable(_In_ PHW_DEVICE_EXTENSION DevExt)
{
    if (!VmxFifoMapped(DevExt))
        return FALSE;

    return (DevExt->Capabilities & SVGA_CAP_EXTENDED_FIFO) != 0;
}

static VOID
VmxFifoSync(_Inout_ PHW_DEVICE_EXTENSION DevExt)
{
    VmxWriteUlong(DevExt, SVGA_REG_SYNC, 1);
    VmxWaitNotBusy(DevExt);
}

static BOOLEAN
VmxFifoReserveBytes(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                    _In_ ULONG Bytes,
                    _Out_ PULONG Offset,
                    _Outptr_result_bytebuffer_(Bytes) PULONG *FifoPtr)
{
    ULONG attempts = 0;

    if (!VmxFifoAvailable(DevExt))
        return FALSE;

    Bytes = (Bytes + 3u) & ~3u;

    for (;;)
    {
        volatile ULONG *fifo = (volatile ULONG *)DevExt->Fifo;
        ULONG min = fifo[SVGA_FIFO_MIN];
        ULONG max = fifo[SVGA_FIFO_MAX];
        ULONG next = fifo[SVGA_FIFO_NEXT_CMD];
        ULONG stop = fifo[SVGA_FIFO_STOP];
        ULONG available;

        if (next >= stop)
            available = (max - next) + (stop - min);
        else
            available = stop - next;

        if (Bytes <= available)
        {
            if (next + Bytes <= max)
            {
                *Offset = next;
                *FifoPtr = (PULONG)((PUCHAR)DevExt->Fifo + next);
                return TRUE;
            }

            if (Bytes <= (stop - min))
            {
                VmxMemoryBarrier();
                fifo[SVGA_FIFO_NEXT_CMD] = min;
                VmxMemoryBarrier();
                next = min;
                *Offset = next;
                *FifoPtr = (PULONG)((PUCHAR)DevExt->Fifo + next);
                return TRUE;
            }
        }

        if (attempts++ > 1000)
            return FALSE;

        VmxFifoSync(DevExt);
    }
}

static VOID
VmxFifoCommitBytes(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                   _In_ ULONG Offset,
                   _In_ ULONG Bytes)
{
    volatile ULONG *fifo = (volatile ULONG *)DevExt->Fifo;
    ULONG min = fifo[SVGA_FIFO_MIN];
    ULONG max = fifo[SVGA_FIFO_MAX];
    ULONG next = Offset + ((Bytes + 3u) & ~3u);

    if (next >= max)
        next = min + (next - max);

    VmxMemoryBarrier();
    fifo[SVGA_FIFO_NEXT_CMD] = next;
    VmxMemoryBarrier();

    VmxFifoSync(DevExt);
}

static BOOLEAN
VmxFifoEmitFence(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                 _Out_opt_ PULONG FenceValue)
{
    ULONG offset;
    PULONG cmd;
    ULONG fence;

    if (!VmxFifoReserveBytes(DevExt, sizeof(VMX_SVGA_CMD_FENCE) + sizeof(ULONG), &offset, &cmd))
        return FALSE;

    fence = DevExt->NextFenceValue;
    if (fence == 0)
        fence = 1;
    DevExt->NextFenceValue = fence;
    DevExt->NextFenceValue++;
    if (DevExt->NextFenceValue == 0)
        DevExt->NextFenceValue = 1;

    cmd[0] = SVGA_CMD_FENCE;
    cmd[1] = fence;

    VmxFifoCommitBytes(DevExt, offset, sizeof(VMX_SVGA_CMD_FENCE) + sizeof(ULONG));

    if (FenceValue)
        *FenceValue = fence;

    return TRUE;
}

static BOOLEAN
VmxFifoWaitOnFence(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                   _In_ ULONG FenceValue)
{
    const ULONG maxAttempts = (VMX_FENCE_WAIT_TIMEOUT_MS + VMX_FENCE_WAIT_SLICE_MS - 1) /
                              VMX_FENCE_WAIT_SLICE_MS;
    ULONG attempts = 0;
    LARGE_INTEGER timeout;

    DevExt->LastCompletedFence = VmxReadUlong(DevExt, SVGA_REG_FENCE);

    while (VmxFencePending(DevExt->LastCompletedFence, FenceValue))
    {
        if ((DevExt->Capabilities & SVGA_CAP_IRQMASK) && DevExt->SyncEvent)
        {
            VP_STATUS waitStatus;

            VideoPortClearEvent(DevExt, DevExt->SyncEvent);
            timeout.QuadPart = -(LONGLONG)VMX_FENCE_WAIT_SLICE_MS * 10000;
            waitStatus = VideoPortWaitForSingleObject(DevExt,
                                                      DevExt->SyncEvent,
                                                      &timeout);

            /* Fall back to direct polling if the wait failed or timed out. */
            if (waitStatus != NO_ERROR)
                VmxFifoSync(DevExt);
        }
        else
        {
            VmxFifoSync(DevExt);
        }

        DevExt->LastCompletedFence = VmxReadUlong(DevExt, SVGA_REG_FENCE);

        if (++attempts > maxAttempts)
        {
            VideoDebugPrint((Warn,
                             "VMX: fence wait timed out (target=%lu last=%lu)\n",
                             FenceValue,
                             DevExt->LastCompletedFence));
            return FALSE;
        }
    }

    return TRUE;
}

static BOOLEAN
VmxFifoCmdUpdate(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                 _In_ const VMWARE_VIDEO_RECT *Rect)
{
    ULONG offset;
    PULONG cmd;
    ULONG surfaceWidth, surfaceHeight;
    ULONG x, y;
    ULONG width, height;

    if (!VmxFifoAvailable(DevExt))
        return FALSE;

    width = Rect->Width;
    height = Rect->Height;

    if (width == 0 || height == 0)
        return TRUE;

    if (!VmxGetSurfaceBounds(DevExt, &surfaceWidth, &surfaceHeight))
        return TRUE;

    x = Rect->X;
    y = Rect->Y;

    if (!VmxClampSpanToLimit(x, surfaceWidth, &width) ||
        !VmxClampSpanToLimit(y, surfaceHeight, &height))
        return TRUE;

    if (!VmxFifoReserveBytes(DevExt, sizeof(VMX_SVGA_CMD_UPDATE) + sizeof(ULONG), &offset, &cmd))
        return FALSE;

    cmd[0] = SVGA_CMD_UPDATE;
    cmd[1] = x;
    cmd[2] = y;
    cmd[3] = width;
    cmd[4] = height;

    VmxFifoCommitBytes(DevExt, offset, sizeof(VMX_SVGA_CMD_UPDATE) + sizeof(ULONG));
    return TRUE;
}

static BOOLEAN
VmxFifoCmdRectCopy(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                   _In_ ULONG SrcX,
                   _In_ ULONG SrcY,
                   _In_ ULONG DestX,
                   _In_ ULONG DestY,
                   _In_ ULONG Width,
                   _In_ ULONG Height)
{
    ULONG offset;
    PULONG cmd;
    ULONG surfaceWidth, surfaceHeight;
    ULONG rectWidth = Width;
    ULONG rectHeight = Height;

    if (!VmxFifoAvailable(DevExt))
        return FALSE;

    if (rectWidth == 0 || rectHeight == 0)
        return TRUE;

    if (!VmxGetSurfaceBounds(DevExt, &surfaceWidth, &surfaceHeight))
        return TRUE;

    if (!VmxClampSpanToLimit(DestX, surfaceWidth, &rectWidth) ||
        !VmxClampSpanToLimit(DestY, surfaceHeight, &rectHeight))
        return TRUE;

    if (!VmxClampSpanToLimit(SrcX, surfaceWidth, &rectWidth) ||
        !VmxClampSpanToLimit(SrcY, surfaceHeight, &rectHeight))
        return TRUE;

    if (!VmxFifoReserveBytes(DevExt, sizeof(VMX_SVGA_CMD_RECT_COPY) + sizeof(ULONG), &offset, &cmd))
        return FALSE;

    cmd[0] = SVGA_CMD_RECT_COPY;
    cmd[1] = SrcX;
    cmd[2] = SrcY;
    cmd[3] = DestX;
    cmd[4] = DestY;
    cmd[5] = rectWidth;
    cmd[6] = rectHeight;

    VmxFifoCommitBytes(DevExt, offset, sizeof(VMX_SVGA_CMD_RECT_COPY) + sizeof(ULONG));
    return TRUE;
}

static BOOLEAN
VmxFifoCmdFrontFill(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                    _In_ ULONG Color,
                    _In_ ULONG X,
                    _In_ ULONG Y,
                    _In_ ULONG Width,
                    _In_ ULONG Height)
{
    ULONG offset;
    PULONG cmd;
    ULONG surfaceWidth, surfaceHeight;
    ULONG rectWidth = Width;
    ULONG rectHeight = Height;

    if (!VmxFifoAvailable(DevExt))
        return FALSE;

    if (rectWidth == 0 || rectHeight == 0)
        return TRUE;

    if (!VmxGetSurfaceBounds(DevExt, &surfaceWidth, &surfaceHeight))
        return TRUE;

    if (!VmxClampSpanToLimit(X, surfaceWidth, &rectWidth) ||
        !VmxClampSpanToLimit(Y, surfaceHeight, &rectHeight))
        return TRUE;

    if (!VmxFifoReserveBytes(DevExt, sizeof(VMX_SVGA_CMD_FRONT_ROP_FILL) + sizeof(ULONG), &offset, &cmd))
        return FALSE;

    cmd[0] = SVGA_CMD_FRONT_ROP_FILL;
    cmd[1] = Color;
    cmd[2] = X;
    cmd[3] = Y;
    cmd[4] = rectWidth;
    cmd[5] = rectHeight;
    cmd[6] = SVGA_ROP_PATCOPY;

    VmxFifoCommitBytes(DevExt, offset, sizeof(VMX_SVGA_CMD_FRONT_ROP_FILL) + sizeof(ULONG));
    return TRUE;
}

typedef struct _VMX_REG_VALUE_CONTEXT
{
    PVOID Buffer;
    ULONG BufferLength;
    ULONG ValueLength;
    BOOLEAN Found;
} VMX_REG_VALUE_CONTEXT, *PVMX_REG_VALUE_CONTEXT;

static VP_STATUS NTAPI
VmxRegistryReadCallback(PVOID HwDeviceExtension,
                        PVOID Context,
                        PWSTR ValueName,
                        PVOID ValueData,
                        ULONG ValueLength)
{
    PVMX_REG_VALUE_CONTEXT ctx = Context;
    ULONG copyLength = ValueLength;

    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(ValueName);

    if (copyLength > ctx->BufferLength)
        copyLength = ctx->BufferLength;

    if (copyLength && ctx->Buffer)
        VideoPortMoveMemory(ctx->Buffer, ValueData, copyLength);

    ctx->ValueLength = copyLength;
    ctx->Found = TRUE;
    return NO_ERROR;
}

static BOOLEAN
VmxLoadPersistedValue(_In_ PHW_DEVICE_EXTENSION DevExt,
                      _In_z_ const WCHAR *Format,
                      _Out_writes_(ValueBufferLength) PVOID ValueBuffer,
                      _In_ ULONG ValueBufferLength,
                      _Out_opt_ PULONG ValueLength)
{
    WCHAR valueName[64];
    VMX_REG_VALUE_CONTEXT ctx;
    VP_STATUS status;

    if (!NT_SUCCESS(RtlStringCchPrintfW(valueName,
                                        RTL_NUMBER_OF(valueName),
                                        Format,
                                        (ULONG)DevExt->DisplayIndex)))
    {
        return FALSE;
    }

    ctx.Buffer = ValueBuffer;
    ctx.BufferLength = ValueBufferLength;
    ctx.ValueLength = 0;
    ctx.Found = FALSE;

    status = VideoPortGetRegistryParameters(DevExt,
                                            valueName,
                                            0,
                                            VmxRegistryReadCallback,
                                            &ctx);

    if (status == NO_ERROR && ctx.Found)
    {
        if (ValueLength)
            *ValueLength = ctx.ValueLength;
        return TRUE;
    }

    return FALSE;
}

static BOOLEAN
VmxLoadPersistedEdid(_In_ PHW_DEVICE_EXTENSION DevExt,
                     _Out_writes_(128) UCHAR *Edid)
{
    ULONG length = 0;

    VideoPortZeroMemory(Edid, 128);

    if (!VmxLoadPersistedValue(DevExt,
                               L"HardwareInformation.Edid.Head%u",
                               Edid,
                               128,
                               &length))
    {
        return FALSE;
    }

    return (length == 128);
}

/* Persist the per-head UId so future boots reuse the same monitor instance. */
static VOID
VmxPersistMonitorUid(_In_ PHW_DEVICE_EXTENSION DevExt,
                     _In_ ULONG Uid)
{
    WCHAR valueName[64];
    NTSTATUS status;

    if (DevExt->DisplayIndex >= SVGA_MAX_DISPLAYS)
        return;

    status = RtlStringCchPrintfW(valueName,
                                 RTL_NUMBER_OF(valueName),
                                 L"HardwareInformation.Uid.Head%u",
                                 (ULONG)DevExt->DisplayIndex);
    if (NT_SUCCESS(status))
    {
        (VOID)VideoPortSetRegistryParameters(DevExt,
                                             valueName,
                                             &Uid,
                                             sizeof(Uid));
    }
}

/* Load a previously persisted per-head UId, if any. */
static BOOLEAN
VmxLoadPersistedUid(_In_ PHW_DEVICE_EXTENSION DevExt,
                    _Out_ PULONG Uid)
{
    ULONG stored = 0;
    ULONG length = 0;

    if (!VmxLoadPersistedValue(DevExt,
                               L"HardwareInformation.Uid.Head%u",
                               &stored,
                               sizeof(stored),
                               &length))
    {
        return FALSE;
    }

    if (length != sizeof(ULONG))
        return FALSE;

    *Uid = stored;
    return TRUE;
}

static VOID
VmxConfigureVerboseLogging(_Inout_ PHW_DEVICE_EXTENSION DevExt)
{
    VMX_REG_VALUE_CONTEXT ctx;
    ULONG verbose = 0;
    BOOLEAN enableVerbose = VmxDefaultVerboseLogging;
    VP_STATUS status;

    VideoPortZeroMemory(&ctx, sizeof(ctx));
    ctx.Buffer = &verbose;
    ctx.BufferLength = sizeof(verbose);

    status = VideoPortGetRegistryParameters(DevExt,
                                            L"VmwareVerboseLogging",
                                            0,
                                            VmxRegistryReadCallback,
                                            &ctx);

    if (status == NO_ERROR && ctx.Found && ctx.ValueLength >= sizeof(ULONG))
        enableVerbose = (verbose != 0);

    if (enableVerbose)
        DevExt->Flags |= VMX_FLAG_VERBOSE_LOGGING;
    else
        DevExt->Flags &= ~VMX_FLAG_VERBOSE_LOGGING;
}

static ULONG
VmxComputeMonitorUid(_In_reads_(128) const UCHAR *Edid,
                     _In_ ULONG DisplayIndex)
{
    ULONG vendor = ((ULONG)Edid[8] << 8) | (ULONG)Edid[9];
    ULONG product = ((ULONG)Edid[11] << 8) | (ULONG)Edid[10];
    ULONG serial = ((ULONG)Edid[15] << 24) |
                   ((ULONG)Edid[14] << 16) |
                   ((ULONG)Edid[13] << 8) |
                   (ULONG)Edid[12];
    ULONG manufacture = ((ULONG)Edid[16] << 8) | (ULONG)Edid[17];
    ULONG uid = (vendor << 16) ^ (product << 8) ^ serial ^ manufacture;

    if (uid == 0 || uid == 0xFFFFFFFF)
        uid = 0x80000000u | (DisplayIndex & 0x7FFFFFFFu);

    return uid;
}

static VOID
VmxPersistMonitorIdentity(_In_ PHW_DEVICE_EXTENSION DevExt,
                          _In_reads_(128) const UCHAR *Edid)
{
    WCHAR valueName[64];
    NTSTATUS status;
    ULONG uid;

    if (DevExt->DisplayIndex >= SVGA_MAX_DISPLAYS)
        return;

    uid = VmxComputeMonitorUid(Edid, DevExt->DisplayIndex);
    VmxPersistMonitorUid(DevExt, uid);

    status = RtlStringCchPrintfW(valueName,
                                 RTL_NUMBER_OF(valueName),
                                 L"HardwareInformation.Edid.Head%u",
                                 (ULONG)DevExt->DisplayIndex);
    if (NT_SUCCESS(status))
    {
        (VOID)VideoPortSetRegistryParameters(DevExt,
                                             valueName,
                                             (PVOID)Edid,
                                             128);
    }

    status = RtlStringCchPrintfW(valueName,
                                 RTL_NUMBER_OF(valueName),
                                 L"HardwareInformation.MonitorId.Head%u",
                                 (ULONG)DevExt->DisplayIndex);
    if (NT_SUCCESS(status))
    {
        (VOID)VideoPortSetRegistryParameters(DevExt,
                                             valueName,
                                             (PVOID)(Edid + 8),
                                             10);
    }

    {
        USHORT manufacturer = ((USHORT)Edid[8] << 8) | (USHORT)Edid[9];
        USHORT model = ((USHORT)Edid[11] << 8) | (USHORT)Edid[10];
        WCHAR monitorKey[8] = {0};

        if (manufacturer != 0)
        {
            WCHAR c1 = (WCHAR)(((manufacturer >> 10) & 0x1F) + 'A' - 1);
            WCHAR c2 = (WCHAR)(((manufacturer >> 5) & 0x1F) + 'A' - 1);
            WCHAR c3 = (WCHAR)((manufacturer & 0x1F) + 'A' - 1);

            if (c1 >= L'A' && c1 <= L'Z' &&
                c2 >= L'A' && c2 <= L'Z' &&
                c3 >= L'A' && c3 <= L'Z')
            {
                status = RtlStringCchPrintfW(monitorKey,
                                             RTL_NUMBER_OF(monitorKey),
                                             L"%c%c%c%04X",
                                             c1,
                                             c2,
                                             c3,
                                             model);
                if (NT_SUCCESS(status))
                {
                    size_t chars = 0;

                    status = RtlStringCchLengthW(monitorKey,
                                                 RTL_NUMBER_OF(monitorKey),
                                                 &chars);
                    if (NT_SUCCESS(status))
                    {
                        status = RtlStringCchPrintfW(valueName,
                                                     RTL_NUMBER_OF(valueName),
                                                     L"HardwareInformation.MonitorString.Head%u",
                                                     (ULONG)DevExt->DisplayIndex);
                        if (NT_SUCCESS(status))
                        {
                            (VOID)VideoPortSetRegistryParameters(DevExt,
                                                                 valueName,
                                                                 monitorKey,
                                                                 (ULONG)((chars + 1) * sizeof(WCHAR)));
                        }
                    }
                }
            }
        }
    }
}

static VOID
VmxUpdateEdidDescriptor(_In_ PHW_DEVICE_EXTENSION DevExt,
                        _Inout_updates_(128) UCHAR *Edid)
{
    ULONG sum = 0;
    ULONG i;

    Edid[0x12] = (UCHAR)(DevExt->DisplayIndex & 0xFF);
    Edid[0x13] = (UCHAR)((DevExt->DisplayIndex >> 8) & 0xFF);

    {
        CHAR name[13] = "VMwareSVGA00";

        name[10] = '0' + (CHAR)((DevExt->DisplayIndex / 10) % 10);
        name[11] = '0' + (CHAR)(DevExt->DisplayIndex % 10);

        for (i = 0; i < sizeof(name); ++i)
            Edid[0xFC + i] = name[i];

        Edid[0xFC + sizeof(name)] = '\n';
        Edid[0xFC + sizeof(name) + 1] = ' ';
        Edid[0xFC + sizeof(name) + 2] = ' ';
    }

    for (i = 0; i < 127; ++i)
        sum += Edid[i];

    Edid[127] = (UCHAR)((0x100 - (sum & 0xFF)) & 0xFF);

    VmxTraceEdidDescriptor(DevExt, Edid);

    VmxPersistMonitorIdentity(DevExt, Edid);
}

static VP_STATUS
VmxMapResources(_Inout_ PHW_DEVICE_EXTENSION DevExt)
{
    enum { RangeCount = 3 };
    VIDEO_ACCESS_RANGE AccessRanges[RangeCount] = {0};
    VIDEO_ACCESS_RANGE PortRange = {0};
    VP_STATUS Status;
    ULONG i;
    USHORT VendorId = 0x80EE;
    USHORT DeviceId = 0xBEEF;
    ULONG Slot = 0;
    BOOLEAN AccessRangesValid = FALSE;
    BOOLEAN Found = FALSE;
    BOOLEAN PciConfigValid = FALSE;
    BOOLEAN CommandBitsEnabled = FALSE;
    VMX_PCI_COMMON_HEADER Probe = {0};
    VMX_PCI_COMMON_HEADER PciConfig = {0};
    ULONG bytesRead = 0;
    PHYSICAL_ADDRESS PrefetchBar = {0};
    PHYSICAL_ADDRESS NonPrefetchBar = {0};

    Status = VideoPortGetAccessRanges(DevExt,
                                      0,
                                      NULL,
                                      RangeCount,
                                      AccessRanges,
                                      &VendorId,
                                      &DeviceId,
                                      &Slot);

    DevExt->PciSlot = Slot;
    DevExt->IoPortBase = 0;
    DevExt->FrameBufferLength = 0;
    DevExt->FifoLength = 0;
    DevExt->FrameBuffer.QuadPart = 0;
    DevExt->FifoPhys.QuadPart = 0;
    DevExt->FrameBufferBase = NULL;
    DevExt->Fifo = NULL;
    DevExt->Flags = 0;

    if (Status == NO_ERROR)
    {
        AccessRangesValid = TRUE;
        goto ProcessAccessRanges;
    }

    {
        ULONG Bus = DevExt->PciBus;

        VideoDebugPrint((Warn,
            "VMX: VideoPortGetAccessRanges failed (0x%lx) for VEN_%04X DEV_%04X (initial slot %lu)\n",
            Status,
            VendorId,
            DeviceId,
            Slot));

        if (DevExt->AdapterInterfaceType == PCIBus)
        {
            for (ULONG DeviceNumber = 0; DeviceNumber < 32 && !Found; ++DeviceNumber)
            {
                for (ULONG Function = 0; Function < 8; ++Function)
                {
                    ULONG CandidateSlot = (DeviceNumber << 5) | Function;

                    VideoPortZeroMemory(&Probe, sizeof(Probe));
                    bytesRead = VideoPortGetBusData(DevExt,
                                                    PCIConfiguration,
                                                    CandidateSlot,
                                                    &Probe,
                                                    0,
                                                    sizeof(Probe));

                    if (bytesRead < offsetof(VMX_PCI_COMMON_HEADER, Command))
                        continue;

                    if (Probe.VendorID == VendorId && Probe.DeviceID == DeviceId)
                    {
                        Slot = CandidateSlot;
                        DevExt->PciSlot = Slot;
                        Found = TRUE;
                        VideoDebugPrint((Info,
                            "VMX: located device on bus %lu slot %lu (func %lu)\n",
                            Bus,
                            DeviceNumber,
                            Function));
                        break;
                    }
                }
            }
        }
        else
        {
            VideoDebugPrint((Warn,
                "VMX: adapter interface type %u is not PCI; skipping bus scan\n",
                DevExt->AdapterInterfaceType));
        }

        if (!Found)
        {
            VideoDebugPrint((Error,
                "VMX: unable to locate PCI slot for VEN_%04X DEV_%04X; falling back to legacy ports\n",
                VendorId,
                DeviceId));
        }
        else
        {
            VideoPortZeroMemory(&PciConfig, sizeof(PciConfig));
            bytesRead = VideoPortGetBusData(DevExt,
                                            PCIConfiguration,
                                            Slot,
                                            &PciConfig,
                                            0,
                                            sizeof(PciConfig));

            if (bytesRead >= offsetof(VMX_PCI_COMMON_HEADER, BaseAddresses) + sizeof(PciConfig.BaseAddresses))
            {
                PciConfigValid = TRUE;

                DevExt->PciInterruptLine = PciConfig.InterruptLine;
                DevExt->PciInterruptPin = PciConfig.InterruptPin;

                DPRINT1("VMX: PCI cfg bus=%lu slot=%lu vid=%04x did=%04x hdr=0x%x intr=%u bars=%08lx %08lx %08lx %08lx %08lx %08lx\n",
                        DevExt->PciBus,
                        Slot,
                        PciConfig.VendorID,
                        PciConfig.DeviceID,
                        PciConfig.HeaderType,
                        PciConfig.InterruptLine,
                        PciConfig.BaseAddresses[0],
                        PciConfig.BaseAddresses[1],
                        PciConfig.BaseAddresses[2],
                        PciConfig.BaseAddresses[3],
                        PciConfig.BaseAddresses[4],
                        PciConfig.BaseAddresses[5]);

                if ((PciConfig.Command & (PCI_ENABLE_IO_SPACE | PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_BUS_MASTER)) !=
                    (PCI_ENABLE_IO_SPACE | PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_BUS_MASTER))
                {
                    USHORT DesiredCommand = PciConfig.Command |
                                             PCI_ENABLE_IO_SPACE |
                                             PCI_ENABLE_MEMORY_SPACE |
                                             PCI_ENABLE_BUS_MASTER;
                    ULONG written = VideoPortSetBusData(DevExt,
                                                         PCIConfiguration,
                                                         Slot,
                                                         &DesiredCommand,
                                                         offsetof(VMX_PCI_COMMON_HEADER, Command),
                                                         sizeof(DesiredCommand));

                    DPRINT1("VMX: enabling PCI command bits old=0x%04x new=0x%04x wrote=%lu\n",
                            PciConfig.Command,
                            DesiredCommand,
                            written);

                    if (written == sizeof(DesiredCommand))
                    {
                        PciConfig.Command = DesiredCommand;
                        CommandBitsEnabled = TRUE;
                    }
                }
                else
                {
                    CommandBitsEnabled = TRUE;
                }

                if (CommandBitsEnabled)
                {
                    VIDEO_ACCESS_RANGE RetryRanges[RangeCount] = {0};
                    USHORT RetryVendor = VendorId;
                    USHORT RetryDevice = DeviceId;
                    ULONG RetrySlot = Slot;
                    VP_STATUS RetryStatus;

                    RetryStatus = VideoPortGetAccessRanges(DevExt,
                                                           0,
                                                           NULL,
                                                           RangeCount,
                                                           RetryRanges,
                                                           &RetryVendor,
                                                           &RetryDevice,
                                                           &RetrySlot);
                    if (RetryStatus == NO_ERROR)
                    {
                        VideoPortMoveMemory(AccessRanges, RetryRanges, sizeof(AccessRanges));
                        AccessRangesValid = TRUE;
                        Status = NO_ERROR;
                        Slot = RetrySlot;
                        DevExt->PciSlot = Slot;
                        VendorId = RetryVendor;
                        DeviceId = RetryDevice;
                        goto ProcessAccessRanges;
                    }

                    Status = RetryStatus;
                }

                for (i = 0; i < ARRAYSIZE(PciConfig.BaseAddresses); ++i)
                {
                    ULONG bar = PciConfig.BaseAddresses[i];

                    if (bar == 0)
                        continue;

                    if (bar & PCI_ADDRESS_IO_SPACE)
                    {
                        ULONG ioBase = bar & PCI_ADDRESS_IO_ADDRESS_MASK;
                        if (ioBase && PortRange.RangeLength == 0)
                        {
                            PortRange.RangeStart.QuadPart = ioBase;
                            PortRange.RangeLength = SVGA_NUM_PORTS * sizeof(ULONG);
                            PortRange.RangeInIoSpace = TRUE;
                            DPRINT1("VMX: selecting IO BAR%lu @0x%lx len=0x%lx\n",
                                    i,
                                    ioBase,
                                    PortRange.RangeLength);
                            DevExt->IoPortBase = ioBase;
                        }
                        continue;
                    }

                    if ((bar & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_ADDRESS_MEMORY_TYPE_64BIT)
                    {
                        ULONGLONG combined;

                        if (i + 1 >= ARRAYSIZE(PciConfig.BaseAddresses))
                            break;

                        combined = bar & PCI_ADDRESS_MEMORY_ADDRESS_MASK;
                        ++i;
                        combined |= ((ULONGLONG)PciConfig.BaseAddresses[i]) << 32;

                        if (bar & PCI_ADDRESS_MEMORY_PREFETCH)
                            PrefetchBar.QuadPart = combined;
                        else
                            NonPrefetchBar.QuadPart = combined;

                        continue;
                    }

                    if (bar & PCI_ADDRESS_MEMORY_PREFETCH)
                        PrefetchBar.QuadPart = bar & PCI_ADDRESS_MEMORY_ADDRESS_MASK;
                    else if (NonPrefetchBar.QuadPart == 0)
                        NonPrefetchBar.QuadPart = bar & PCI_ADDRESS_MEMORY_ADDRESS_MASK;
                }
            }
            else
            {
                DPRINT1("VMX: VideoPortGetBusData returned %lu bytes; cannot decode PCI BARs\n", bytesRead);
            }
        }
    }

ProcessAccessRanges:
    if (!CommandBitsEnabled)
    {
        VideoPortZeroMemory(&PciConfig, sizeof(PciConfig));
        bytesRead = VideoPortGetBusData(DevExt,
                                        PCIConfiguration,
                                        Slot,
                                        &PciConfig,
                                        0,
                                        sizeof(PciConfig));

        if (bytesRead >= offsetof(VMX_PCI_COMMON_HEADER, Command) + sizeof(PciConfig.Command))
        {
            PciConfigValid = TRUE;
            DevExt->PciInterruptLine = PciConfig.InterruptLine;
            DevExt->PciInterruptPin = PciConfig.InterruptPin;

            USHORT desiredCommand = PciConfig.Command |
                                     PCI_ENABLE_IO_SPACE |
                                     PCI_ENABLE_MEMORY_SPACE |
                                     PCI_ENABLE_BUS_MASTER;

            if ((PciConfig.Command & (PCI_ENABLE_IO_SPACE | PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_BUS_MASTER)) !=
                (PCI_ENABLE_IO_SPACE | PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_BUS_MASTER))
            {
                ULONG written = VideoPortSetBusData(DevExt,
                                                     PCIConfiguration,
                                                     Slot,
                                                     &desiredCommand,
                                                     offsetof(VMX_PCI_COMMON_HEADER, Command),
                                                     sizeof(desiredCommand));

                DPRINT1("VMX: enabling PCI command bits (post-AccessRanges) old=0x%04x new=0x%04x wrote=%lu\n",
                        PciConfig.Command,
                        desiredCommand,
                        written);

                if (written == sizeof(desiredCommand))
                {
                    PciConfig.Command = desiredCommand;
                    CommandBitsEnabled = TRUE;

                    if (AccessRangesValid)
                    {
                        VIDEO_ACCESS_RANGE refreshed[RangeCount] = {0};
                        USHORT refreshVendor = VendorId;
                        USHORT refreshDevice = DeviceId;
                        ULONG refreshSlot = Slot;

                        if (VideoPortGetAccessRanges(DevExt,
                                                    0,
                                                    NULL,
                                                    RangeCount,
                                                    refreshed,
                                                    &refreshVendor,
                                                    &refreshDevice,
                                                    &refreshSlot) == NO_ERROR)
                        {
                            VideoPortMoveMemory(AccessRanges, refreshed, sizeof(AccessRanges));
                            VendorId = refreshVendor;
                            DeviceId = refreshDevice;
                            Slot = refreshSlot;
                        }
                    }
                }
            }
            else
            {
                CommandBitsEnabled = TRUE;
            }
        }
    }

    if (AccessRangesValid)
    {
        for (i = 0; i < RangeCount; ++i)
        {
            if (!AccessRanges[i].RangeLength)
                continue;

            if (AccessRanges[i].RangeInIoSpace)
            {
                PortRange = AccessRanges[i];
                DevExt->IoPortBase = PortRange.RangeStart.LowPart;
                break;
            }
        }

        for (i = 0; i < RangeCount; ++i)
        {
            if (!AccessRanges[i].RangeLength || AccessRanges[i].RangeInIoSpace)
                continue;

            if (DevExt->FrameBufferLength == 0 || AccessRanges[i].RangeLength > DevExt->FrameBufferLength)
            {
                DevExt->FrameBuffer = AccessRanges[i].RangeStart;
                DevExt->FrameBufferLength = AccessRanges[i].RangeLength;
                continue;
            }

            if (DevExt->FifoLength == 0)
            {
                DevExt->FifoPhys = AccessRanges[i].RangeStart;
                DevExt->FifoLength = AccessRanges[i].RangeLength;
            }
        }

        DPRINT1("VMX: AccessRanges[0]=0x%I64x/0x%lx io=%u, [1]=0x%I64x/0x%lx io=%u, [2]=0x%I64x/0x%lx io=%u (slot %lu)\n",
                AccessRanges[0].RangeStart.QuadPart, AccessRanges[0].RangeLength, AccessRanges[0].RangeInIoSpace,
                AccessRanges[1].RangeStart.QuadPart, AccessRanges[1].RangeLength, AccessRanges[1].RangeInIoSpace,
                AccessRanges[2].RangeStart.QuadPart, AccessRanges[2].RangeLength, AccessRanges[2].RangeInIoSpace,
                DevExt->PciSlot);

        if (DevExt->FrameBufferLength)
        {
            DPRINT1("VMX: BAR framebuffer phys=0x%I64x len=0x%lx\n",
                    DevExt->FrameBuffer.QuadPart,
                    DevExt->FrameBufferLength);
        }

        if (DevExt->FifoLength)
        {
            DPRINT1("VMX: BAR fifo phys=0x%I64x len=0x%lx\n",
                    DevExt->FifoPhys.QuadPart,
                    DevExt->FifoLength);
        }
    }

    if (PortRange.RangeLength == 0)
    {
        if (!PciConfigValid)
        {
            VideoPortZeroMemory(&PciConfig, sizeof(PciConfig));
            bytesRead = VideoPortGetBusData(DevExt,
                                            PCIConfiguration,
                                            Slot,
                                            &PciConfig,
                                            0,
                                            sizeof(PciConfig));

            if (bytesRead >= offsetof(VMX_PCI_COMMON_HEADER, BaseAddresses) + sizeof(PciConfig.BaseAddresses))
                PciConfigValid = TRUE;
        }

        if (PciConfigValid && PrefetchBar.QuadPart == 0 && NonPrefetchBar.QuadPart == 0)
        {
            for (i = 0; i < ARRAYSIZE(PciConfig.BaseAddresses); ++i)
            {
                ULONG bar = PciConfig.BaseAddresses[i];

                if (bar == 0)
                    continue;

                if (bar & PCI_ADDRESS_IO_SPACE)
                    continue;

                if ((bar & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_ADDRESS_MEMORY_TYPE_64BIT)
                {
                    ULONGLONG combined;

                    if (i + 1 >= ARRAYSIZE(PciConfig.BaseAddresses))
                        break;

                    combined = bar & PCI_ADDRESS_MEMORY_ADDRESS_MASK;
                    ++i;
                    combined |= ((ULONGLONG)PciConfig.BaseAddresses[i]) << 32;

                    if (bar & PCI_ADDRESS_MEMORY_PREFETCH)
                        PrefetchBar.QuadPart = combined;
                    else
                        NonPrefetchBar.QuadPart = combined;

                    continue;
                }

                if (bar & PCI_ADDRESS_MEMORY_PREFETCH)
                    PrefetchBar.QuadPart = bar & PCI_ADDRESS_MEMORY_ADDRESS_MASK;
                else if (NonPrefetchBar.QuadPart == 0)
                    NonPrefetchBar.QuadPart = bar & PCI_ADDRESS_MEMORY_ADDRESS_MASK;
            }
        }

        if (PciConfigValid)
        {
            if (PrefetchBar.QuadPart)
            {
                DPRINT1("VMX: Prefetch BAR @0x%I64x\n", PrefetchBar.QuadPart);
                if (DevExt->FrameBufferLength == 0)
                    DevExt->FrameBuffer = PrefetchBar;
            }
            if (NonPrefetchBar.QuadPart)
            {
                DPRINT1("VMX: Non-prefetch BAR @0x%I64x\n", NonPrefetchBar.QuadPart);
                if (DevExt->FifoLength == 0)
                    DevExt->FifoPhys = NonPrefetchBar;
            }
        }

        if (PortRange.RangeLength == 0)
        {
            VIDEO_ACCESS_RANGE Legacy = {0};

            Legacy.RangeStart.LowPart = SVGA_LEGACY_BASE_PORT;
            Legacy.RangeLength = SVGA_NUM_PORTS * sizeof(ULONG);
            Legacy.RangeInIoSpace = TRUE;

            PortRange = Legacy;
        }
    }

    DevExt->IndexPort = (PULONG)VideoPortGetDeviceBase(DevExt,
                                                       PortRange.RangeStart,
                                                       PortRange.RangeLength,
                                                       VIDEO_MEMORY_SPACE_IO);
    if (!DevExt->IndexPort)
    {
        DPRINT1("VMX: VideoPortGetDeviceBase failed for I/O range base 0x%lx length 0x%lx\n",
                PortRange.RangeStart.LowPart,
                PortRange.RangeLength);
        DevExt->Flags |= VMX_FLAG_NO_PORT_MAP;
        return ERROR_DEV_NOT_EXIST;
    }

    DevExt->ValuePort = (PULONG)((ULONG_PTR)DevExt->IndexPort + SVGA_VALUE_PORT);
    DevExt->InterruptPort = PortRange.RangeStart.LowPart + SVGA_IRQSTATUS_PORT;

    if (DevExt->FrameBufferLength && DevExt->FrameBufferBase == NULL)
    {
        /*
         * Use write-combined caching for the framebuffer to match UEFI GOP's mapping.
         */
        DevExt->FrameBufferBase = VideoPortGetDeviceBase(DevExt,
                                                         DevExt->FrameBuffer,
                                                         DevExt->FrameBufferLength,
                                                         VIDEO_MEMORY_SPACE_MEMORY | VIDEO_MEMORY_SPACE_P6CACHE);
        DPRINT1("VMX: mapped framebuffer @0x%I64x len=0x%lx -> %p\n",
                DevExt->FrameBuffer.QuadPart,
                DevExt->FrameBufferLength,
                DevExt->FrameBufferBase);
        if (!DevExt->FrameBufferBase)
        {
            DPRINT1("VMX: framebuffer mapping failed; continuing without kernel VA\n");
            DevExt->Flags |= VMX_FLAG_NO_FB_MAP;
        }
        else
        {
            DevExt->Flags &= ~VMX_FLAG_NO_FB_MAP;
        }
    }

    if (DevExt->FifoLength && DevExt->Fifo == NULL)
    {
        DevExt->Fifo = VideoPortGetDeviceBase(DevExt,
                                              DevExt->FifoPhys,
                                              DevExt->FifoLength,
                                              VIDEO_MEMORY_SPACE_MEMORY);
        DPRINT1("VMX: mapped fifo @0x%I64x len=0x%lx -> %p\n",
                DevExt->FifoPhys.QuadPart,
                DevExt->FifoLength,
                DevExt->Fifo);
        if (!DevExt->Fifo)
        {
            DPRINT1("VMX: FIFO mapping failed; continuing without kernel VA\n");
            DevExt->Flags |= VMX_FLAG_NO_FIFO_MAP;
        }
        else
        {
            DevExt->Flags &= ~VMX_FLAG_NO_FIFO_MAP;
        }
    }

    DevExt->VramBase.QuadPart = 0;
    DevExt->VramSize.QuadPart = 0;
    DevExt->MemSize = 0;

    return NO_ERROR;
}

ULONG
NTAPI
VmxInitModes(IN PHW_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Count;
    PVIDEO_MODE_INFORMATION table = NULL;

    /* Build a filtered, contiguous mode table in paged pool for StartIO to use */
    table = (PVIDEO_MODE_INFORMATION)VideoPortAllocatePool(DeviceExtension,
                                                           0 /* Paged */,
                                                           sizeof(VIDEO_MODE_INFORMATION) * ARRAYSIZE(VmxCommonModes),
                                                           VMX_TAG);
    if (!table)
        return 0;

    Count = VmxQueryAndClampModes(DeviceExtension, table, ARRAYSIZE(VmxCommonModes));

    /* Cache the count; CurrentMode will be set by VmxInitialize to an entry */
    DeviceExtension->VideoModeCount = Count;

    /*
     * We don't persist this table in the extension to keep it small.
     * StartIO regenerates on demand with the same predicate, so we can free it.
     */
    VideoPortFreePool(DeviceExtension, table);

    return Count;
}

VP_STATUS
NTAPI
VmxInitDevice(IN PHW_DEVICE_EXTENSION DeviceExtension)
{
    PHYSICAL_ADDRESS FbStart, FifoStart;
    ULONG FbSize, FifoSize, Id, Caps, VramBytes;
    VP_STATUS Status;

    VideoDebugPrint((Info, "VMX: probing SVGA-II\n"));

    if (!DeviceExtension->IndexPort)
    {
        Status = VmxMapResources(DeviceExtension);
        if (Status != NO_ERROR)
        {
            VideoDebugPrint((Error, "VMX: failed to map adapter resources (0x%lx)\n", Status));
            return Status;
        }
    }

    /* Handshake: write highest ID we support, read back negotiated */
    VmxWriteUlong(DeviceExtension, SVGA_REG_ID, SVGA_ID_2);
    Id = VmxReadUlong(DeviceExtension, SVGA_REG_ID);
    if (Id == SVGA_ID_INVALID)
    {
        VideoDebugPrint((Error, "VMX: SVGA_ID invalid\n"));
        return ERROR_DEV_NOT_EXIST;
    }
    if ((Id != SVGA_ID_2) && (Id != SVGA_ID_1) && (Id != SVGA_ID_0))
    {
        VideoDebugPrint((Error, "VMX: unexpected SVGA_ID 0x%lx\n", Id));
        return ERROR_DEV_NOT_EXIST;
    }

    /* Cache capabilities and VRAM / framebuffer parameters */
    Caps       = VmxReadUlong(DeviceExtension, SVGA_REG_CAPABILITIES);
    VramBytes  = VmxReadUlong(DeviceExtension, SVGA_REG_VRAM_SIZE);
    FbStart.QuadPart  = VmxReadUlong(DeviceExtension, SVGA_REG_FB_START);
    FbSize            = VmxReadUlong(DeviceExtension, SVGA_REG_FB_SIZE);
    FifoStart.QuadPart = VmxReadUlong(DeviceExtension, SVGA_REG_MEM_START);
    FifoSize           = VmxReadUlong(DeviceExtension, SVGA_REG_MEM_SIZE);

    if (!FbStart.QuadPart && DeviceExtension->FrameBufferLength)
        FbStart = DeviceExtension->FrameBuffer;
    if (!FbSize && DeviceExtension->FrameBufferLength)
        FbSize = DeviceExtension->FrameBufferLength;
    if (!FifoStart.QuadPart && DeviceExtension->FifoLength)
        FifoStart = DeviceExtension->FifoPhys;
    if (!FifoSize && DeviceExtension->FifoLength)
        FifoSize = DeviceExtension->FifoLength;

    DPRINT1("VMX: caps=0x%lx vram=%lu fb=0x%I64x size=0x%lx fifo=0x%I64x size=0x%lx\n",
            Caps,
            VramBytes,
            FbStart.QuadPart,
            FbSize,
            FifoStart.QuadPart,
            FifoSize);

    DeviceExtension->Capabilities     = Caps;
    if (VramBytes)
        DeviceExtension->VramSize.LowPart = VramBytes;

    if (FbStart.QuadPart)
    {
        DeviceExtension->FrameBuffer = FbStart;
        DeviceExtension->VramBase    = FbStart;
    }

    if (FbSize)
        DeviceExtension->FrameBufferLength = FbSize;

    if (FifoSize)
        DeviceExtension->MemSize = FifoSize;

    if (FifoSize)
        DeviceExtension->FifoLength = FifoSize;

    if (!DeviceExtension->FrameBufferBase && FbSize)
    {
        /*
         * Use write-combined caching to match UEFI GOP's framebuffer mapping.
         */
        DeviceExtension->FrameBufferBase = VideoPortGetDeviceBase(DeviceExtension,
                                                                  FbStart,
                                                                  FbSize,
                                                                  VIDEO_MEMORY_SPACE_MEMORY | VIDEO_MEMORY_SPACE_P6CACHE);
        DPRINT1("VMX: deferred map framebuffer -> %p\n", DeviceExtension->FrameBufferBase);
        if (!DeviceExtension->FrameBufferBase)
        {
            VideoDebugPrint((Error, "VMX: deferred framebuffer map failed; operating without kernel VA\n"));
            DeviceExtension->Flags |= VMX_FLAG_NO_FB_MAP;
        }
        else
        {
            DeviceExtension->Flags &= ~VMX_FLAG_NO_FB_MAP;
        }
    }

    if (DeviceExtension->Fifo == NULL && FifoStart.QuadPart && FifoSize)
    {
        DeviceExtension->Fifo = VideoPortGetDeviceBase(DeviceExtension,
                                                       FifoStart,
                                                       FifoSize,
                                                       VIDEO_MEMORY_SPACE_MEMORY);
        DPRINT1("VMX: deferred map fifo -> %p\n", DeviceExtension->Fifo);
        if (!DeviceExtension->Fifo)
        {
            VideoDebugPrint((Error, "VMX: deferred FIFO map failed; FIFO disabled\n"));
            DeviceExtension->Flags |= VMX_FLAG_NO_FIFO_MAP;
        }
        else
        {
            DeviceExtension->Flags &= ~VMX_FLAG_NO_FIFO_MAP;
        }
    }

    if (VmxRegisterOnlyMode(DeviceExtension))
        DPRINT1("VMX: operating without framebuffer VA (register-only mode)\n");

    /* If the reported caps include multimon but we don't manage heads yet, clamp to one */
    if ((Caps & SVGA_CAP_MULTIMON) != 0)
    {
        VmxWriteUlong(DeviceExtension, SVGA_REG_NUM_GUEST_DISPLAYS, 1);
    }

    if (VmxRegisterOnlyMode(DeviceExtension))
    {
        if ((DeviceExtension->Flags & VMX_FLAG_HWERR_REPORTED) == 0)
        {
            VideoPortLogError(DeviceExtension,
                              NULL,
                              ERROR_DEV_NOT_EXIST,
                              'VMXF');
            DeviceExtension->Flags |= VMX_FLAG_HWERR_REPORTED;
        }
    }

    VideoDebugPrint((Info,
                     "VMX: InitDevice complete (caps=0x%lx, vram=%lu) runMode=%s\n",
                     Caps,
                     DeviceExtension->VramSize.LowPart,
                     VmxRegisterOnlyMode(DeviceExtension) ? "register-only" : "framebuffer-mapped"));
    return NO_ERROR;
}

BOOLEAN
NTAPI
VmxIsMultiMon(IN PHW_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Capabilities;

    /* Get the caps */
    Capabilities = DeviceExtension->Capabilities;

    /* Check for multi-mon support */
    if ((Capabilities & SVGA_CAP_MULTIMON) && (Capabilities & SVGA_CAP_PITCHLOCK))
    {
        /* Query the monitor count */
        if (VmxReadUlong(DeviceExtension, SVGA_REG_NUM_DISPLAYS) > 1) return TRUE;
    }

    /* Either no support, or just one screen */
    return FALSE;
}

VP_STATUS
NTAPI
VmxFindAdapter(IN PVOID HwDeviceExtension,
               IN PVOID HwContext,
               IN PWSTR ArgumentString,
               IN OUT PVIDEO_PORT_CONFIG_INFO ConfigInfo,
               OUT PUCHAR Again)
{
    VP_STATUS Status;
    PHW_DEVICE_EXTENSION DeviceExtension = HwDeviceExtension;
    DPRINT1("VMX searching for adapter\n");

    /* Reset enumeration counters when starting a fresh detection cycle */
    if (VmxEnumeratedDisplays >= VmxExpectedDisplays)
    {
        VmxExpectedDisplays = 1;
        VmxEnumeratedDisplays = 0;
        VideoPortZeroMemory(VmxDeviceExtensionArray, sizeof(VmxDeviceExtensionArray));
        VideoPortZeroMemory(VmxDisplayWidths, sizeof(VmxDisplayWidths));
        VideoPortZeroMemory(VmxDisplayHeights, sizeof(VmxDisplayHeights));
        VideoPortZeroMemory(VmxDisplayXOffsets, sizeof(VmxDisplayXOffsets));
        VideoPortZeroMemory(VmxDisplayYOffsets, sizeof(VmxDisplayYOffsets));
    }

    /* Zero out the fields */
    VideoPortZeroMemory(DeviceExtension, sizeof(HW_DEVICE_EXTENSION));
    DeviceExtension->NextFenceValue = 1;

    VmxConfigureVerboseLogging(DeviceExtension);

    if (VmxEnumeratedDisplays >= SVGA_MAX_DISPLAYS)
        DeviceExtension->DisplayIndex = SVGA_MAX_DISPLAYS - 1;
    else
        DeviceExtension->DisplayIndex = (USHORT)VmxEnumeratedDisplays;

    DeviceExtension->PowerState = VideoPowerOn;
    DeviceExtension->DpmsVersion = 0x0102;

    {
        ULONG cfgLen = ConfigInfo->Length;
        DPRINT1("VMX: ConfigInfo length=%lu bytes\n", cfgLen);
        if (cfgLen < SIZE_OF_NT4_VIDEO_PORT_CONFIG_INFO)
        {
            DPRINT1("VMX: accepting legacy NT4-sized ConfigInfo\n");
        }
    }

    DeviceExtension->PciBus = ConfigInfo->SystemIoBusNumber;
    DeviceExtension->PciSlot = 0;
    DeviceExtension->AdapterInterfaceType = (USHORT)ConfigInfo->AdapterInterfaceType;

    DPRINT1("VMX: ConfigInfo len=%lu adapterIf=%lu sysIoBus=%lu busIntVec=%lu busIntLvl=%lu irqShare=%u mode=%u master=%u\n",
            ConfigInfo->Length,
            ConfigInfo->AdapterInterfaceType,
            ConfigInfo->SystemIoBusNumber,
            ConfigInfo->BusInterruptVector,
            ConfigInfo->BusInterruptLevel,
            ConfigInfo->InterruptShareable,
            ConfigInfo->InterruptMode,
            ConfigInfo->Master);

    DPRINT1("VMX: DMA cfg channel=%lu port=%lu share=%u width=%u speed=%u mapBuffers=%u needPhys=%u demand=%u sg=%u maxXfer=0x%lx breaks=%lu maxChunk=0x%lx\n",
            ConfigInfo->DmaChannel,
            ConfigInfo->DmaPort,
            ConfigInfo->DmaShareable,
            ConfigInfo->DmaWidth,
            ConfigInfo->DmaSpeed,
            ConfigInfo->bMapBuffers,
            ConfigInfo->NeedPhysicalAddresses,
            ConfigInfo->DemandMode,
            ConfigInfo->ScatterGather,
            ConfigInfo->MaximumTransferLength,
            ConfigInfo->NumberOfPhysicalBreaks,
            ConfigInfo->MaximumScatterGatherChunkSize);

    if (ConfigInfo->Length >= FIELD_OFFSET(VIDEO_PORT_CONFIG_INFO, DriverRegistryPath) + sizeof(ConfigInfo->DriverRegistryPath))
        DPRINT1("VMX: RegistryPath=%S\n", ConfigInfo->DriverRegistryPath ? ConfigInfo->DriverRegistryPath : L"<null>");
    if (ConfigInfo->Length >= SIZE_OF_WXP_VIDEO_PORT_CONFIG_INFO)
        DPRINT1("VMX: SystemMemorySize=%I64u\n", ConfigInfo->SystemMemorySize);

    ConfigInfo->InterruptMode = LevelSensitive;
    ConfigInfo->Master = TRUE;

    if (DeviceExtension->PciInterruptLine != 0 && DeviceExtension->PciInterruptLine != 0xFF)
    {
        ConfigInfo->BusInterruptLevel  = DeviceExtension->PciInterruptLine;
        ConfigInfo->BusInterruptVector = DeviceExtension->PciInterruptLine;
        ConfigInfo->InterruptShareable = TRUE;
        DPRINT1("VMX: exposing IRQ line %u (from PCI config)\n", DeviceExtension->PciInterruptLine);
    }
    else if (ConfigInfo->BusInterruptLevel != 0)
    {
        /* PCI config InterruptLine is 0/0xFF but the video port already
         * assigned an interrupt via PnP/ACPI routing.  Trust it. */
        ConfigInfo->InterruptShareable = TRUE;
        DPRINT1("VMX: using PnP-assigned IRQ %lu (PCI config line was %u)\n",
                ConfigInfo->BusInterruptLevel, DeviceExtension->PciInterruptLine);
    }
    else
    {
        ConfigInfo->BusInterruptLevel  = 0;
        ConfigInfo->BusInterruptVector = 0;
        ConfigInfo->InterruptShareable = FALSE;
        DPRINT1("VMX: no usable interrupt line reported; operating in polled mode\n");
    }

    /* Initialize the device extension and find the adapter */
    Status = VmxInitDevice(DeviceExtension);
    VideoDebugPrint((Info, "VMX: VmxInitDevice status=%lx\n", Status));
    if (Status != NO_ERROR) return ERROR_DEV_NOT_EXIST;

    /* Save this adapter extension */
    if (DeviceExtension->DisplayIndex < SVGA_MAX_DISPLAYS)
        VmxDeviceExtensionArray[DeviceExtension->DisplayIndex] = DeviceExtension;

    /* Create the sync event */
    VideoPortCreateEvent(DeviceExtension,
                         NotificationEvent,
                         NULL,
                         &DeviceExtension->SyncEvent);

    /* Check for multi-monitor configuration */
    if (VmxEnumeratedDisplays == 0)
    {
        ULONG hostDisplays = 1;

        if (VmxIsMultiMon(DeviceExtension))
        {
            hostDisplays = VmxReadUlong(DeviceExtension, SVGA_REG_NUM_DISPLAYS);
            if (hostDisplays == 0)
                hostDisplays = 1;
            if (hostDisplays > SVGA_MAX_DISPLAYS)
                hostDisplays = SVGA_MAX_DISPLAYS;
        }

        VmxExpectedDisplays = hostDisplays;
        VmxWriteUlong(DeviceExtension, SVGA_REG_NUM_GUEST_DISPLAYS, hostDisplays);
        DPRINT1("VMX: host reports %lu display(s); scheduling enumeration of %lu head(s)\n",
                hostDisplays,
                VmxExpectedDisplays);
    }

    DPRINT1("VMX: assigned DisplayIndex=%u (enumerated %lu/%lu)\n",
            DeviceExtension->DisplayIndex,
            VmxEnumeratedDisplays + 1,
            VmxExpectedDisplays);

    /* Zero the frame buffer if we have a mapping */
    if (VmxFramebufferMapped(DeviceExtension))
    {
        VmxClearFrameBuffer(DeviceExtension);
    }
    else
    {
        DPRINT1("VMX: framebuffer VA unavailable; skipping clear during FindAdapter\n");
    }

    /* Initialize the video modes */
    VmxInitModes(DeviceExtension);

    {
        ULONG dev = (DeviceExtension->PciSlot >> 5) & 0x1F;
        ULONG fn = DeviceExtension->PciSlot & 0x7;
        struct
        {
            ULONG Bus;
            ULONG Device;
            ULONG Function;
        } pciLoc;

        DPRINT1("VMX: adapter PCI location bus=%lu device=%lu function=%lu\n",
                DeviceExtension->PciBus,
                dev,
                fn);

        pciLoc.Bus = DeviceExtension->PciBus;
        pciLoc.Device = dev;
        pciLoc.Function = fn;
        VideoPortSetRegistryParameters(DeviceExtension,
                                       L"HardwareInformation.PciLocation",
                                       &pciLoc,
                                       sizeof(pciLoc));
    }

    /* Setup registry keys */
    {
        WCHAR headDescriptor[64];
        size_t headDescriptorLen = 0;
        ULONG headOrdinal = (ULONG)DeviceExtension->DisplayIndex + 1;
        ULONG headDescriptorBytes;
        NTSTATUS strStatus;

        strStatus = RtlStringCchPrintfW(headDescriptor,
                                        RTL_NUMBER_OF(headDescriptor),
                                        L"%s (Head %lu)",
                                        AdapterString,
                                        headOrdinal);
        if (!NT_SUCCESS(strStatus))
        {
            (void)RtlStringCchCopyW(headDescriptor,
                                     RTL_NUMBER_OF(headDescriptor),
                                     AdapterString);
        }

        strStatus = RtlStringCchLengthW(headDescriptor,
                                        RTL_NUMBER_OF(headDescriptor),
                                        &headDescriptorLen);
        if (!NT_SUCCESS(strStatus))
        {
            if (!NT_SUCCESS(RtlStringCchLengthW(AdapterString,
                                                RTL_NUMBER_OF(AdapterString),
                                                &headDescriptorLen)))
            {
                headDescriptorLen = RTL_NUMBER_OF(AdapterString) - 1;
            }
        }

        headDescriptorBytes = (ULONG)((headDescriptorLen + 1) * sizeof(WCHAR));

        VideoPortSetRegistryParameters(DeviceExtension,
                                       L"HardwareInformation.ChipType",
                                       headDescriptor,
                                       headDescriptorBytes);
        VideoPortSetRegistryParameters(DeviceExtension,
                                       L"HardwareInformation.Description",
                                       headDescriptor,
                                       headDescriptorBytes);
        VideoPortSetRegistryParameters(DeviceExtension,
                                       L"HardwareInformation.AdapterString",
                                       headDescriptor,
                                       headDescriptorBytes);
        VideoPortSetRegistryParameters(DeviceExtension,
                                       L"HardwareInformation.DacType",
                                       AdapterString,
                                       sizeof(AdapterString));
        VideoPortSetRegistryParameters(DeviceExtension,
                                       L"HardwareInformation.BiosString",
                                       AdapterString,
                                       sizeof(AdapterString));
    }
    VideoPortSetRegistryParameters(DeviceExtension,
                                   L"HardwareInformation.MemorySize",
                                   &DeviceExtension->VramSize.LowPart,
                                   sizeof(ULONG));
    {
        const WCHAR *runMode;
        ULONG runModeSize;

        if (VmxRegisterOnlyMode(DeviceExtension))
        {
            runMode = VmxRunModeRegisterOnly;
            runModeSize = sizeof(VmxRunModeRegisterOnly);
        }
        else
        {
            runMode = VmxRunModeFramebuffer;
            runModeSize = sizeof(VmxRunModeFramebuffer);
        }

        VideoPortSetRegistryParameters(DeviceExtension,
                                       L"HardwareInformation.RunMode",
                                       (PVOID)runMode,
                                       runModeSize);
    }
    VideoPortSetRegistryParameters(DeviceExtension,
                                   L"HardwareInformation.DisplayIndex",
                                   &DeviceExtension->DisplayIndex,
                                   sizeof(DeviceExtension->DisplayIndex));

    /* No VDM support */
    ConfigInfo->NumEmulatorAccessEntries = 0;
    ConfigInfo->EmulatorAccessEntries = 0;
    ConfigInfo->EmulatorAccessEntriesContext = 0;
    ConfigInfo->HardwareStateSize = 0;
    ConfigInfo->VdmPhysicalVideoMemoryAddress.QuadPart = 0;
    ConfigInfo->VdmPhysicalVideoMemoryLength = 0;

    /* Write that this is Windows XP or higher */
    VmxWriteUlong(DeviceExtension, SVGA_REG_GUEST_ID, 0x5000 | 0x08);

    VmxEnumeratedDisplays++;
    if (Again)
    {
        *Again = (VmxEnumeratedDisplays < VmxExpectedDisplays) ? TRUE : FALSE;
        if (*Again)
        {
            DPRINT1("VMX: requesting videoprt to probe additional head (%lu of %lu enumerated)\n",
                    VmxEnumeratedDisplays,
                    VmxExpectedDisplays);
        }
    }

    VmxConfigureIrq(DeviceExtension, FALSE);

    return NO_ERROR;
}

BOOLEAN
NTAPI
VmxInitialize(IN PVOID HwDeviceExtension)
{
    PHW_DEVICE_EXTENSION DevExt = HwDeviceExtension;
    ULONG MaxW, MaxH;

    /* Initialize a small, safe FIFO if it’s mapped */
    if (VmxFifoMapped(DevExt))
        VmxFifoInit(DevExt);

    if (!VmxRegisterOnlyMode(DevExt))
    {
        /* Select a conservative default mode */
        MaxW = VmxReadUlong(DevExt, SVGA_REG_MAX_WIDTH);
        MaxH = VmxReadUlong(DevExt, SVGA_REG_MAX_HEIGHT);

        /* Prefer 1024x768 or clamp to device */
        if (MaxW >= 1024 && MaxH >= 768)
            VmxSetMode(DevExt, 1024, 768, 32);
        else
            VmxSetMode(DevExt, 800, 600, 32);
    }
    else
    {
        VMX_SIZE safe = {800, 600};
        DPRINT1("VMX: register-only mode detected; preserving firmware display state\n");
        VmxBuildModeInfo(safe, &DevExt->CurrentMode);
        DevExt->CurrentMode.ModeIndex = 0;
    }

    /* Clear the framebuffer to black */
    VmxClearFrameBuffer(DevExt);

    if (VmxRegisterOnlyMode(DevExt))
        VmxConfigureIrq(DevExt, FALSE);
    else
        VmxConfigureIrq(DevExt, TRUE);

    DevExt->PowerState = VideoPowerOn;
    if (DevExt->DpmsVersion == 0)
        DevExt->DpmsVersion = 0x0102;

    /* Ready our event (already created in FindAdapter) */
    if (DevExt->SyncEvent) VideoPortSetEvent(DevExt, DevExt->SyncEvent);

    return TRUE;
}

/* Helpers specific to StartIO *********************************************************/

static BOOLEAN
VmxMapVideoMemory(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                  _In_ PVIDEO_MEMORY RequestedAddress,
                  _Out_ PVIDEO_MEMORY_INFORMATION MapInformation,
                  _Out_ PSTATUS_BLOCK StatusBlock)
{
    VP_STATUS Status;
    /*
     * Use write-combined caching for framebuffer mappings to match UEFI GOP.
     */
    ULONG     inIoSpace = VIDEO_MEMORY_SPACE_MEMORY | VIDEO_MEMORY_SPACE_P6CACHE;
    ULONG     length;

    StatusBlock->Information = sizeof(VIDEO_MEMORY_INFORMATION);

    VideoPortZeroMemory(MapInformation, sizeof(*MapInformation));

    length = DevExt->FrameBufferLength ? DevExt->FrameBufferLength : DevExt->VramSize.LowPart;

    if (length == 0)
    {
        DPRINT1("VMX: MAP_VIDEO_MEMORY returning zero-length aperture (no framebuffer length)\n");
        StatusBlock->Status = NO_ERROR;
        return TRUE;
    }

    MapInformation->VideoRamBase = RequestedAddress->RequestedVirtualAddress;
    MapInformation->VideoRamLength = length;

    Status = VideoPortMapMemory(DevExt,
                                DevExt->FrameBuffer,
                                &MapInformation->VideoRamLength,
                                &inIoSpace,
                                &MapInformation->VideoRamBase);
    if (Status != NO_ERROR)
    {
        DPRINT1("VMX: VideoPortMapMemory failed (0x%lx); exposing zero-length mapping\n", Status);
        MapInformation->VideoRamBase = NULL;
        MapInformation->VideoRamLength = 0;
        StatusBlock->Status = NO_ERROR;
        return TRUE;
    }

    MapInformation->FrameBufferBase   = MapInformation->VideoRamBase;
    MapInformation->FrameBufferLength = MapInformation->VideoRamLength;

    StatusBlock->Status = NO_ERROR;
    return TRUE;
}

static BOOLEAN
NTAPI
VmxUnmapVideoMemory(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                    _In_ PVIDEO_MEMORY VideoMemory,
                    _Out_ PSTATUS_BLOCK StatusBlock)
{
    VP_STATUS Status = VideoPortUnmapMemory(DevExt,
                                            VideoMemory->RequestedVirtualAddress,
                                            NULL);
    StatusBlock->Status = Status;
    StatusBlock->Information = 0;
    return (Status == NO_ERROR);
}

static VOID
VmxFillNumModes(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                _Out_ PVIDEO_NUM_MODES Num, _Out_ PSTATUS_BLOCK St)
{
    Num->NumModes = VmxInitModes(DevExt);
    Num->ModeInformationLength = sizeof(VIDEO_MODE_INFORMATION);
    St->Status = NO_ERROR;
    St->Information = sizeof(VIDEO_NUM_MODES);
}

static ULONG
VmxWriteAvailableModes(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                       _Out_writes_bytes_(OutLen) PVOID OutBuf,
                       _In_ ULONG OutLen)
{
    ULONG count = 0;
    PVIDEO_MODE_INFORMATION modes;

    modes = (PVIDEO_MODE_INFORMATION)VideoPortAllocatePool(DevExt, 0,
                sizeof(VIDEO_MODE_INFORMATION) * ARRAYSIZE(VmxCommonModes), VMX_TAG);
    if (!modes) return 0;

    count = VmxQueryAndClampModes(DevExt, modes, ARRAYSIZE(VmxCommonModes));

    if (count * sizeof(VIDEO_MODE_INFORMATION) <= OutLen)
    {
        VideoPortMoveMemory(OutBuf, modes, count * sizeof(VIDEO_MODE_INFORMATION));
    }
    else
    {
        count = 0; /* caller's buffer too small */
    }

    VideoPortFreePool(DevExt, modes);
    return count;
}

/* Main dispatch **********************************************************************/

BOOLEAN
NTAPI
VmxStartIO(IN PVOID HwDeviceExtension,
           IN PVIDEO_REQUEST_PACKET RequestPacket)
{
    PHW_DEVICE_EXTENSION DevExt = HwDeviceExtension;
    PSTATUS_BLOCK        St     = RequestPacket->StatusBlock;
    BOOLEAN RegisterOnly = VmxRegisterOnlyMode(DevExt);
    St->Status = ERROR_INVALID_FUNCTION;
    St->Information = 0;

    switch (RequestPacket->IoControlCode)
    {
    case IOCTL_VIDEO_QUERY_NUM_AVAIL_MODES:
        if (RequestPacket->OutputBufferLength < sizeof(VIDEO_NUM_MODES))
            break;
        VmxFillNumModes(DevExt, (PVIDEO_NUM_MODES)RequestPacket->OutputBuffer, St);
        return TRUE;

    case IOCTL_VIDEO_QUERY_AVAIL_MODES:
    {
        ULONG wrote = VmxWriteAvailableModes(DevExt,
                         RequestPacket->OutputBuffer,
                         RequestPacket->OutputBufferLength);
        if (!wrote) break;
        St->Status = NO_ERROR;
        St->Information = wrote * sizeof(VIDEO_MODE_INFORMATION);
        return TRUE;
    }

    case IOCTL_VIDEO_QUERY_CURRENT_MODE:
        if (RequestPacket->OutputBufferLength < sizeof(VIDEO_MODE_INFORMATION))
            break;
        *(PVIDEO_MODE_INFORMATION)RequestPacket->OutputBuffer = DevExt->CurrentMode;
        St->Status = NO_ERROR;
        St->Information = sizeof(VIDEO_MODE_INFORMATION);
        return TRUE;

    case IOCTL_VIDEO_SET_CURRENT_MODE:
    {
        if (RegisterOnly)
        {
            DPRINT1("VMX: SET_CURRENT_MODE rejected (register-only mode)\n");
            St->Status = ERROR_INVALID_FUNCTION;
            St->Information = 0;
            return TRUE;
        }

        PVIDEO_MODE Mode = (PVIDEO_MODE)RequestPacket->InputBuffer;
        PVIDEO_MODE_INFORMATION modes;
        ULONG count, need = sizeof(VIDEO_MODE_INFORMATION) * ARRAYSIZE(VmxCommonModes);

        if (RequestPacket->InputBufferLength < sizeof(VIDEO_MODE))
            break;

        modes = (PVIDEO_MODE_INFORMATION)VideoPortAllocatePool(DevExt, 0, need, VMX_TAG);
        if (!modes) break;

        count = VmxQueryAndClampModes(DevExt, modes, ARRAYSIZE(VmxCommonModes));
        if (Mode->RequestedMode >= count)
        {
            VideoPortFreePool(DevExt, modes);
            break;
        }

        /* Program hardware */
#if DBG
        ASSERT(!RegisterOnly);
#endif
        if (!VmxSetMode(DevExt,
                        modes[Mode->RequestedMode].VisScreenWidth,
                        modes[Mode->RequestedMode].VisScreenHeight,
                        modes[Mode->RequestedMode].BitsPerPlane * modes[Mode->RequestedMode].NumberOfPlanes))
        {
            VideoPortFreePool(DevExt, modes);
            break;
        }

        DevExt->CurrentMode = modes[Mode->RequestedMode];
        VideoPortFreePool(DevExt, modes);

        St->Status = NO_ERROR;
        St->Information = 0;
        return TRUE;
    }

    case IOCTL_VIDEO_MAP_VIDEO_MEMORY:
        if (RequestPacket->OutputBufferLength < sizeof(VIDEO_MEMORY_INFORMATION) ||
            RequestPacket->InputBufferLength  < sizeof(VIDEO_MEMORY))
            break;
        if (!VmxMapVideoMemory(DevExt,
                               (PVIDEO_MEMORY)RequestPacket->InputBuffer,
                               (PVIDEO_MEMORY_INFORMATION)RequestPacket->OutputBuffer,
                               St))
            break;
        return TRUE;

    case IOCTL_VIDEO_UNMAP_VIDEO_MEMORY:
        if (RequestPacket->InputBufferLength < sizeof(VIDEO_MEMORY))
            break;
        if (!VmxUnmapVideoMemory(DevExt,
                                 (PVIDEO_MEMORY)RequestPacket->InputBuffer,
                                 St))
            break;
        return TRUE;

    case IOCTL_VIDEO_VMWARE_QUERY_CAPS:
        if (RequestPacket->OutputBufferLength < sizeof(VMWARE_VIDEO_CAPS))
            break;
        else
        {
            PVMWARE_VIDEO_CAPS caps = (PVMWARE_VIDEO_CAPS)RequestPacket->OutputBuffer;

            VideoPortZeroMemory(caps, sizeof(*caps));
            caps->Version = VMWARE_VIDEO_CAPS_VERSION;

            if (VmxFifoAvailable(DevExt))
                caps->Caps |= VMWARE_VIDEO_CAP_FIFO | VMWARE_VIDEO_CAP_FENCE;

            if (DevExt->Capabilities & SVGA_CAP_IRQMASK)
                caps->Caps |= VMWARE_VIDEO_CAP_IRQ;

            St->Status = NO_ERROR;
            St->Information = sizeof(*caps);
            return TRUE;
        }

    case IOCTL_VIDEO_VMWARE_FIFO_PRESENT:
        if (!VmxFifoAvailable(DevExt))
        {
            St->Status = ERROR_INVALID_FUNCTION;
            St->Information = 0;
            return TRUE;
        }
        if (RequestPacket->InputBufferLength < sizeof(VMWARE_VIDEO_PRESENT))
            break;
        else
        {
            const VMWARE_VIDEO_PRESENT *present =
                (const VMWARE_VIDEO_PRESENT *)RequestPacket->InputBuffer;
            ULONG header = FIELD_OFFSET(VMWARE_VIDEO_PRESENT, Rects);
            ULONG availableRects = 0;

            if (RequestPacket->InputBufferLength >= header)
            {
                availableRects = (RequestPacket->InputBufferLength - header) /
                                 sizeof(VMWARE_VIDEO_RECT);
            }

            if (present->RectCount > availableRects)
            {
                St->Status = ERROR_INVALID_PARAMETER;
                St->Information = 0;
                return TRUE;
            }

            for (ULONG i = 0; i < present->RectCount; ++i)
            {
                if (!VmxFifoCmdUpdate(DevExt, &present->Rects[i]))
                {
                    St->Status = ERROR_INVALID_FUNCTION;
                    St->Information = 0;
                    return TRUE;
                }
            }

            if ((present->Flags & VMWARE_VIDEO_PRESENT_ASYNC) == 0 && present->RectCount != 0)
            {
                ULONG fence;
                if (!VmxFifoEmitFence(DevExt, &fence))
                {
                    St->Status = ERROR_INVALID_FUNCTION;
                    St->Information = 0;
                    return TRUE;
                }

                if (!VmxFifoWaitOnFence(DevExt, fence))
                {
                    St->Status = ERROR_TIMEOUT;
                    St->Information = 0;
                    return TRUE;
                }
            }

            St->Status = NO_ERROR;
            St->Information = 0;
            return TRUE;
        }

    case IOCTL_VIDEO_VMWARE_FIFO_BLIT:
        if (!VmxFifoAvailable(DevExt) || !(DevExt->Capabilities & SVGA_CAP_RECT_COPY))
        {
            St->Status = ERROR_INVALID_FUNCTION;
            St->Information = 0;
            return TRUE;
        }
        if (RequestPacket->InputBufferLength < sizeof(VMWARE_VIDEO_BLIT))
            break;
        else
        {
            const VMWARE_VIDEO_BLIT *blit = (const VMWARE_VIDEO_BLIT *)RequestPacket->InputBuffer;

            if (!VmxFifoCmdRectCopy(DevExt,
                                     blit->SrcX,
                                     blit->SrcY,
                                     blit->DestX,
                                     blit->DestY,
                                     blit->Width,
                                     blit->Height))
            {
                St->Status = ERROR_INVALID_FUNCTION;
                St->Information = 0;
                return TRUE;
            }

            if ((blit->Flags & VMWARE_VIDEO_PRESENT_ASYNC) == 0 && blit->Width && blit->Height)
            {
                ULONG fence;
                if (!VmxFifoEmitFence(DevExt, &fence))
                {
                    St->Status = ERROR_INVALID_FUNCTION;
                    St->Information = 0;
                    return TRUE;
                }

                if (!VmxFifoWaitOnFence(DevExt, fence))
                {
                    St->Status = ERROR_TIMEOUT;
                    St->Information = 0;
                    return TRUE;
                }
            }

            St->Status = NO_ERROR;
            St->Information = 0;
            return TRUE;
        }

    case IOCTL_VIDEO_VMWARE_FIFO_FILL:
        if (!VmxFifoAvailable(DevExt) ||
            !(DevExt->Capabilities & SVGA_CAP_RECT_FILL) ||
            !(DevExt->Capabilities & SVGA_CAP_RASTER_OP))
        {
            St->Status = ERROR_INVALID_FUNCTION;
            St->Information = 0;
            return TRUE;
        }
        if (RequestPacket->InputBufferLength < sizeof(VMWARE_VIDEO_FILL))
            break;
        else
        {
            const VMWARE_VIDEO_FILL *fill = (const VMWARE_VIDEO_FILL *)RequestPacket->InputBuffer;

            if (!VmxFifoCmdFrontFill(DevExt,
                                     fill->Color,
                                     fill->X,
                                     fill->Y,
                                     fill->Width,
                                     fill->Height))
            {
                St->Status = ERROR_INVALID_FUNCTION;
                St->Information = 0;
                return TRUE;
            }

            if ((fill->Flags & VMWARE_VIDEO_PRESENT_ASYNC) == 0 && fill->Width && fill->Height)
            {
                ULONG fence;
                if (!VmxFifoEmitFence(DevExt, &fence))
                {
                    St->Status = ERROR_INVALID_FUNCTION;
                    St->Information = 0;
                    return TRUE;
                }

                if (!VmxFifoWaitOnFence(DevExt, fence))
                {
                    St->Status = ERROR_TIMEOUT;
                    St->Information = 0;
                    return TRUE;
                }
            }

            St->Status = NO_ERROR;
            St->Information = 0;
            return TRUE;
        }

    case IOCTL_VIDEO_RESET_DEVICE:
        if (RegisterOnly)
        {
            DPRINT1("VMX: RESET_DEVICE rejected (register-only mode)\n");
            St->Status = ERROR_INVALID_FUNCTION;
            St->Information = 0;
            return TRUE;
        }

        /* Restore a text/graphics state usable by boot splash or VGA fallback */
        if (!VmxResetHw(DevExt, 80, 25))
            break;
        St->Status = NO_ERROR;
        St->Information = 0;
        return TRUE;

    /* Optional/unsupported in 32bpp truecolor path */
    case IOCTL_VIDEO_SET_COLOR_REGISTERS:
    case IOCTL_VIDEO_SET_PALETTE_REGISTERS:
    case IOCTL_VIDEO_SET_POINTER_ATTR:
    case IOCTL_VIDEO_SET_POINTER_POSITION:
    case IOCTL_VIDEO_ENABLE_CURSOR:
    case IOCTL_VIDEO_DISABLE_CURSOR:
        St->Status = ERROR_INVALID_FUNCTION;
        St->Information = 0;
        return TRUE;

    default:
        break;
    }

    return TRUE; /* return TRUE even for unsupported to keep port happy, but Status remains error */
}

BOOLEAN
NTAPI
VmxResetHw(IN PVOID DeviceExtension,
           IN ULONG Columns,
           IN ULONG Rows)
{
    PHW_DEVICE_EXTENSION DevExt = DeviceExtension;

    UNREFERENCED_PARAMETER(Columns);
    UNREFERENCED_PARAMETER(Rows);

    if (DevExt->DisplayIndex != 0)
    {
        if (DevExt->Capabilities & SVGA_CAP_DISPLAY_TOPOLOGY)
        {
            ULONG width = DevExt->CurrentMode.VisScreenWidth ? DevExt->CurrentMode.VisScreenWidth : 800;
            ULONG height = DevExt->CurrentMode.VisScreenHeight ? DevExt->CurrentMode.VisScreenHeight : 600;
            VmxProgramDisplayTopology(DevExt, width, height);

            {
                PHW_DEVICE_EXTENSION primary = VmxDeviceExtensionArray[0];
                if (primary)
                {
                    ULONG baseWidth = VmxDisplayWidths[0] ? VmxDisplayWidths[0] : width;
                    VmxUpdateGlobalSurface(primary, baseWidth, height, 0);
                }
            }
        }
        DevExt->PowerState = VideoPowerOn;
        if (DevExt->DpmsVersion == 0)
            DevExt->DpmsVersion = 0x0102;
        return TRUE;
    }

    if (VmxRegisterOnlyMode(DevExt))
        return FALSE;

    /* Return to a conservative 800x600x32 graphics mode (good VGA fallback) */
    if (!VmxSetMode(DevExt, 800, 600, 32))
        return FALSE;

    VmxClearFrameBuffer(DevExt);
    VmxConfigureIrq(DevExt, TRUE);
    DevExt->PowerState = VideoPowerOn;
    if (DevExt->DpmsVersion == 0)
        DevExt->DpmsVersion = 0x0102;
    return TRUE;
}

VP_STATUS
NTAPI
VmxGetPowerState(IN PVOID HwDeviceExtension,
                 IN ULONG HwId,
                 IN PVIDEO_POWER_MANAGEMENT VideoPowerControl)
{
    PHW_DEVICE_EXTENSION DevExt = HwDeviceExtension;
    UNREFERENCED_PARAMETER(HwId);

    if (!VideoPowerControl)
        return ERROR_INVALID_PARAMETER;

    VideoPowerControl->Length = sizeof(VIDEO_POWER_MANAGEMENT);
    VideoPowerControl->DPMSVersion = DevExt->DpmsVersion ? DevExt->DpmsVersion : 0x0102;
    VideoPowerControl->PowerState = DevExt->PowerState;

    return NO_ERROR;
}

VP_STATUS
NTAPI
VmxSetPowerState(IN PVOID HwDeviceExtension,
                 IN ULONG HwId,
                 IN PVIDEO_POWER_MANAGEMENT VideoPowerControl)
{
    PHW_DEVICE_EXTENSION DevExt = HwDeviceExtension;

    UNREFERENCED_PARAMETER(HwId);

    if (!VideoPowerControl)
        return ERROR_INVALID_PARAMETER;

    switch (VideoPowerControl->PowerState)
    {
    case VideoPowerOn:
        VmxConfigureIrq(DevExt, TRUE);
        VmxWriteUlong(DevExt, SVGA_REG_ENABLE, SVGA_REG_ENABLE_ENABLE);
        break;
    case VideoPowerStandBy:
    case VideoPowerSuspend:
        /* Hide output without tearing down the mode */
        VmxWriteUlong(DevExt, SVGA_REG_ENABLE, SVGA_REG_ENABLE_HIDE);
        VmxConfigureIrq(DevExt, FALSE);
        break;
    case VideoPowerOff:
        VmxWriteUlong(DevExt, SVGA_REG_ENABLE, SVGA_REG_ENABLE_DISABLE);
        VmxConfigureIrq(DevExt, FALSE);
        break;
    default:
        return ERROR_INVALID_PARAMETER;
    }

    DevExt->PowerState = VideoPowerControl->PowerState;
    if (VideoPowerControl->DPMSVersion)
        DevExt->DpmsVersion = VideoPowerControl->DPMSVersion;
    else
        VideoPowerControl->DPMSVersion = DevExt->DpmsVersion ? DevExt->DpmsVersion : 0x0102;

    VideoPowerControl->Length = sizeof(VIDEO_POWER_MANAGEMENT);

    return NO_ERROR;
}

BOOLEAN
NTAPI
VmxInterrupt(IN PVOID HwDeviceExtension)
{
    PHW_DEVICE_EXTENSION DevExt = HwDeviceExtension;

    /* If IRQs are supported, acknowledge and tell the port we handled it. */
    if (DevExt->Capabilities & SVGA_CAP_IRQMASK)
    {
        PULONG irqStatusPort = (PULONG)((PUCHAR)DevExt->IndexPort + (SVGA_IRQSTATUS_PORT * sizeof(ULONG)));
        ULONG status = VideoPortReadPortUlong(irqStatusPort);

        if (status != 0)
        {
            VmxAtomicOrFetchLong((volatile LONG *)&DevExt->PendingIrqStatus, (LONG)status);

            for (;;)
            {
                ULONG state = DevExt->InterruptState;
                if (state & VMX_INTR_STATE_DPC_QUEUED)
                    break;

                if (VmxAtomicCompareExchangeLong((volatile LONG *)&DevExt->InterruptState,
                                                 (LONG)state,
                                                 (LONG)(state | VMX_INTR_STATE_DPC_QUEUED)))
                {
                    if (!VideoPortQueueDpc(DevExt, VmxInterruptDpc, NULL))
                    {
                        VmxAtomicAndFetchLong((volatile LONG *)&DevExt->InterruptState,
                                              ~(LONG)VMX_INTR_STATE_DPC_QUEUED);
                    }
                    break;
                }
            }

            return TRUE;
        }
    }

    return FALSE;
}

static VOID NTAPI
VmxInterruptDpc(PVOID HwDeviceExtension, PVOID Context)
{
    PHW_DEVICE_EXTENSION DevExt = HwDeviceExtension;
    UNREFERENCED_PARAMETER(Context);

    for (;;)
    {
        ULONG status = (ULONG)VmxAtomicExchangeLong((volatile LONG *)&DevExt->PendingIrqStatus, 0);
        if (status == 0)
            break;

        if (status & SVGA_IRQFLAG_FIFO_PROGRESS)
        {
            if (DevExt->Flags & VMX_FLAG_VERBOSE_LOGGING)
            {
                DPRINT1("VMX: IRQ fifo progress\n");
            }
        }

        if (status & SVGA_IRQFLAG_ANY_FENCE)
        {
            DevExt->LastCompletedFence = VmxReadUlong(DevExt, SVGA_REG_FENCE);
            if (DevExt->Flags & VMX_FLAG_VERBOSE_LOGGING)
            {
                DPRINT1("VMX: IRQ fence signalled (last=%lu)\n",
                        DevExt->LastCompletedFence);
            }
        }

        if (DevExt->SyncEvent)
            VideoPortSetEvent(DevExt, DevExt->SyncEvent);
    }

    VmxAtomicAndFetchLong((volatile LONG *)&DevExt->InterruptState,
                          ~(LONG)VMX_INTR_STATE_DPC_QUEUED);

    if (DevExt->PendingIrqStatus != 0)
    {
        for (;;)
        {
            ULONG state = DevExt->InterruptState;
            if (state & VMX_INTR_STATE_DPC_QUEUED)
                break;

            if (VmxAtomicCompareExchangeLong((volatile LONG *)&DevExt->InterruptState,
                                             (LONG)state,
                                             (LONG)(state | VMX_INTR_STATE_DPC_QUEUED)))
            {
                if (!VideoPortQueueDpc(DevExt, VmxInterruptDpc, NULL))
                {
                    VmxAtomicAndFetchLong((volatile LONG *)&DevExt->InterruptState,
                                          ~(LONG)VMX_INTR_STATE_DPC_QUEUED);
                }
                break;
            }
        }
    }
}

VP_STATUS
NTAPI
VmxGetVideoChildDescriptor(IN PVOID HwDeviceExtension,
                           IN PVIDEO_CHILD_ENUM_INFO ChildEnumInfo,
                           OUT PVIDEO_CHILD_TYPE VideoChildType,
                           OUT PUCHAR pChildDescriptor,
                           OUT PULONG UId,
                           OUT PULONG pUnused)
{
    static const UCHAR VmxSyntheticEdid[128] = {
        0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00, 0x4C,0x2D,0xA0,0x01,0x01,0x01,0x01,0x01,
        0x01,0x15,0x01,0x03,0x80,0x1A,0x11,0x78, 0x0A,0xEE,0x91,0xA3,0x54,0x4C,0x99,0x26,
        0x0F,0x50,0x54,0xAD,0x00,0x81,0x80,0x81, 0x40,0x81,0x00,0x95,0x00,0xA9,0x40,0xB3,
        0x00,0x01,0x01,0x01,0x01,0x02,0x3A,0x80, 0x18,0x71,0x38,0x2D,0x40,0x58,0x2C,0x45,
        0x00,0xFD,0x1E,0x11,0x00,0x00,0x1E,0x00, 0x00,0x00,0xFC,0x00,0x56,0x4D,0x57,0x61,
        0x72,0x65,0x20,0x56,0x42,0x4F,0x58,0x0A, 0x00,0x00,0x00,0xFF,0x00,0x30,0x30,0x30,
        0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x0A, 0x00,0x00,0x00,0xFD,0x00,0x3A,0x3E,0x0F,
        0x2E,0x08,0x00,0x0A,0x20,0x20,0x20,0x20, 0x20,0x20,0x00,0x00,0x00,0x00,0x00,0x2E
    };
    PHW_DEVICE_EXTENSION DevExt = HwDeviceExtension;
    UNREFERENCED_PARAMETER(pUnused);

    if (DevExt->DisplayIndex >= VmxExpectedDisplays)
        return ERROR_NO_MORE_DEVICES;

    if (ChildEnumInfo->ChildIndex > 0)
        return ERROR_NO_MORE_DEVICES;

    *VideoChildType = Monitor;

    if (ChildEnumInfo->ChildIndex == 0)
    {
        UCHAR edid[sizeof(VmxSyntheticEdid)];
        BOOLEAN havePersistedEdid;
        BOOLEAN havePersistedUid;
        ULONG uid = 0;

        havePersistedEdid = VmxLoadPersistedEdid(DevExt, edid);
        havePersistedUid = VmxLoadPersistedUid(DevExt, &uid);

        if (!havePersistedEdid)
        {
            VideoPortMoveMemory(edid, (PVOID)(ULONG_PTR)VmxSyntheticEdid, sizeof(edid));
            VmxUpdateEdidDescriptor(DevExt, edid);

            /* Synthetic EDIDs persist a fresh UId; reload so we return the stored value. */
            havePersistedUid = VmxLoadPersistedUid(DevExt, &uid);
        }

        /* Keep the reported UId aligned with the Control\\Video copy for INF binding. */
        {
            ULONG computedUid = VmxComputeMonitorUid(edid, DevExt->DisplayIndex);

            if (!havePersistedUid || uid == 0 || uid == 0xFFFFFFFF || uid != computedUid)
            {
                uid = computedUid;
                VmxPersistMonitorUid(DevExt, uid);
            }
        }

        VideoPortMoveMemory(pChildDescriptor, edid, sizeof(edid));
        *UId = uid;
        if (pUnused)
            *pUnused = 0;
        return NO_ERROR;
    }

    return ERROR_NO_MORE_DEVICES;
}

static BOOLEAN
VmxSetMode(_Inout_ PHW_DEVICE_EXTENSION DevExt,
           _In_ ULONG Width, _In_ ULONG Height, _In_ ULONG Bpp)
{
    ULONG bytesPerPixel = (Bpp >= 8) ? (Bpp / 8) : 4;
    BOOLEAN primary = (DevExt->DisplayIndex == 0);
    ULONG idx;
    ULONG positionX = 0;
    ULONG positionY = 0;
    ULONG surfaceWidth = Width;
    ULONG surfaceHeight = Height;

    if (DevExt->DisplayIndex < SVGA_MAX_DISPLAYS)
    {
        VmxDisplayWidths[DevExt->DisplayIndex] = Width;
        VmxDisplayHeights[DevExt->DisplayIndex] = Height;
    }

    /* Work out the origin for this head; honour caller-provided offsets, otherwise chain displays. */
    if (DevExt->DisplayIndex == 0)
    {
        positionX = 0;
        positionY = 0;
    }
    else
    {
        positionX = DevExt->XOrigin;
        positionY = DevExt->YOrigin;

        if (positionX == 0 && positionY == 0)
        {
            for (idx = 0; idx < DevExt->DisplayIndex && idx < SVGA_MAX_DISPLAYS; ++idx)
            {
                ULONG prevWidth = VmxDisplayWidths[idx];
                if (prevWidth == 0)
                    prevWidth = Width;
                positionX += prevWidth;
            }
        }
    }

    VmxDisplayXOffsets[DevExt->DisplayIndex] = positionX;
    VmxDisplayYOffsets[DevExt->DisplayIndex] = positionY;
    DevExt->XOrigin = positionX;
    DevExt->YOrigin = positionY;

#if DBG
    if (primary)
        ASSERT(!VmxRegisterOnlyMode(DevExt));
#endif

    if (!primary)
    {
        VMX_SIZE size;

        VmxSyncDisplayCount(DevExt);

        VmxProgramDisplayTopology(DevExt, Width, Height);

        /* Track the mode locally so user-mode queries have data */
        size.X = (USHORT)Width;
        size.Y = (USHORT)Height;
        VmxBuildModeInfo(size, &DevExt->CurrentMode);

        {
            PHW_DEVICE_EXTENSION primaryExt = VmxDeviceExtensionArray[0];
            if (primaryExt)
            {
                ULONG baseWidth = VmxDisplayWidths[0] ? VmxDisplayWidths[0] : Width;
                ULONG baseHeight = VmxDisplayHeights[0] ? VmxDisplayHeights[0] : Height;

                VmxUpdateGlobalSurface(primaryExt, baseWidth, baseHeight, 0);
            }
        }

        DevExt->CurrentMode.ModeIndex = 0;
        return TRUE;
    }

    if (bytesPerPixel == 0)
        bytesPerPixel = 4;

    /* Compute the full surface footprint so vertical stacks and tall heads stay visible. */
    VmxSyncDisplayCount(DevExt);
    VmxComputeSurfaceBounds(&surfaceWidth, &surfaceHeight, Width, Height);

    DPRINT1("VMX: programming mode %lux%lu %lu bpp totalWidth=%lu stride=0x%lx\n",
            Width,
            Height,
            Bpp,
            surfaceWidth,
            (unsigned long)(surfaceWidth * bytesPerPixel));

    /* Disable output while programming */
    VmxWriteUlong(DevExt, SVGA_REG_ENABLE, SVGA_REG_ENABLE_DISABLE);
    VmxWaitNotBusy(DevExt);

    /* Program geometry */
    VmxWriteUlong(DevExt, SVGA_REG_WIDTH, surfaceWidth);
    VmxWriteUlong(DevExt, SVGA_REG_HEIGHT, surfaceHeight);
    VmxWriteUlong(DevExt, SVGA_REG_BITS_PER_PIXEL, Bpp);

    if (DevExt->Capabilities & SVGA_CAP_PITCHLOCK)
        VmxWriteUlong(DevExt, SVGA_REG_PITCHLOCK, surfaceWidth * bytesPerPixel);

    /* Enable output */
    VmxWriteUlong(DevExt, SVGA_REG_ENABLE, SVGA_REG_ENABLE_ENABLE);
    VmxWaitNotBusy(DevExt);

    DPRINT1("VMX: mode set complete\n");

    VmxProgramDisplayTopology(DevExt, Width, Height);

    /* Update cached current mode info */
    {
        VIDEO_MODE_INFORMATION *m = &DevExt->CurrentMode;
        VMX_SIZE S = { (USHORT)Width, (USHORT)Height };
        VmxBuildModeInfo(S, m);
    }

    VmxUpdateGlobalSurface(DevExt, Width, Height, bytesPerPixel);

    {
        /* Grow/shrink the aperture so the new surface is always entirely mapped. */
        ULONGLONG requiredBytes = (ULONGLONG)surfaceWidth * surfaceHeight * bytesPerPixel;
        ULONGLONG fbRegister = VmxReadUlong(DevExt, SVGA_REG_FB_SIZE);

        if (fbRegister > requiredBytes)
            requiredBytes = fbRegister;

        if (requiredBytes > MAXULONG)
            requiredBytes = MAXULONG;

        VmxEnsureFrameBufferCapacity(DevExt, (ULONG)requiredBytes);
    }

    DevExt->PowerState = VideoPowerOn;

    return TRUE;
}

static VOID
VmxClearFrameBuffer(_Inout_ PHW_DEVICE_EXTENSION DevExt)
{
    ULONG length;

    if (DevExt->DisplayIndex != 0)
        return;

    if (!DevExt->FrameBufferBase)
        return;

    length = DevExt->FrameBufferLength ? DevExt->FrameBufferLength : DevExt->VramSize.LowPart;
    if (length == 0)
        return;

#if DBG
    ASSERT(VmxFramebufferMapped(DevExt));
#endif

    DPRINT1("VMX: clearing framebuffer len=0x%lx at %p\n",
            length,
            DevExt->FrameBufferBase);
    VideoPortZeroMemory(DevExt->FrameBufferBase, length);
}

static VOID
VmxBuildModeInfo(_In_ VMX_SIZE Size, _Out_ PVIDEO_MODE_INFORMATION Mode)
{
    VideoPortZeroMemory(Mode, sizeof(*Mode));
    Mode->Length              = sizeof(*Mode);
    Mode->VisScreenWidth      = Size.X;
    Mode->VisScreenHeight     = Size.Y;
    Mode->ScreenStride        = Size.X * 4; /* 32bpp */
    Mode->NumberOfPlanes      = 1;
    Mode->BitsPerPlane        = 32;
    Mode->Frequency           = 60;
    Mode->XMillimeter         = Size.X * 254 / 960; /* ~96 DPI */
    Mode->YMillimeter         = Size.Y * 254 / 960;
    Mode->NumberRedBits       = 8;
    Mode->NumberGreenBits     = 8;
    Mode->NumberBlueBits      = 8;
    Mode->RedMask             = 0x00FF0000;
    Mode->GreenMask           = 0x0000FF00;
    Mode->BlueMask            = 0x000000FF;
    Mode->AttributeFlags      = VIDEO_MODE_GRAPHICS |
                                VIDEO_MODE_COLOR |
                                VIDEO_MODE_NO_OFF_SCREEN;
    Mode->VideoMemoryBitmapWidth  = Size.X;
    Mode->VideoMemoryBitmapHeight = Size.Y;
}

static ULONG
VmxQueryAndClampModes(_Inout_ PHW_DEVICE_EXTENSION DevExt,
                      _Out_writes_(MaxCount) PVIDEO_MODE_INFORMATION Modes,
                      _In_ ULONG MaxCount)
{
    ULONG MaxW  = VmxReadUlong(DevExt, SVGA_REG_MAX_WIDTH);
    ULONG MaxH  = VmxReadUlong(DevExt, SVGA_REG_MAX_HEIGHT);
    ULONGLONG Vram = DevExt->VramSize.LowPart;
    ULONG     count = 0;

    ULONG i;

    for (i = 0; i < ARRAYSIZE(VmxCommonModes) && count < MaxCount; ++i)
    {
        const VMX_SIZE s = VmxCommonModes[i];
        ULONGLONG bytes = (ULONGLONG)s.X * s.Y * 4; /* 32bpp */

        if (s.X > MaxW || s.Y > MaxH) continue;
        if (bytes > Vram) continue;

        VmxBuildModeInfo(s, &Modes[count]);
        Modes[count].ModeIndex = count;
        ++count;
    }

    /* Guarantee at least one VGA-ish mode */
    if (count == 0)
    {
        VMX_SIZE vga = {640, 480};
        VmxBuildModeInfo(vga, &Modes[0]);
        Modes[0].ModeIndex = 0;
        count = 1;
    }

    return count;
}

static VOID
VmxFifoInit(_Inout_ PHW_DEVICE_EXTENSION DevExt)
{
    volatile ULONG *fifo = (volatile ULONG*)DevExt->Fifo;
    ULONG memBytes = DevExt->MemSize;

    if (!VmxFifoMapped(DevExt))
    {
        VmxWriteUlong(DevExt, SVGA_REG_CONFIG_DONE, 0);
        return;
    }

    if (!fifo || memBytes < 16 + (10 * 1024))
    {
        /* Not enough FIFO space; leave config disabled */
        VmxWriteUlong(DevExt, SVGA_REG_CONFIG_DONE, 0);
        return;
    }

    /* Per VMware docs: the first 4 dwords are FIFO registers, byte offsets. */
    fifo[SVGA_FIFO_MIN]      = 16;
    fifo[SVGA_FIFO_MAX]      = 16 + (10 * 1024);
    fifo[SVGA_FIFO_NEXT_CMD] = 16;
    fifo[SVGA_FIFO_STOP]     = 16;

    /* Enable FIFO operation */
    VmxWriteUlong(DevExt, SVGA_REG_CONFIG_DONE, 1);

    VmxConfigureIrq(DevExt, FALSE);
}

ULONG
NTAPI
DriverEntry(IN PVOID Context1,
            IN PVOID Context2)
{
    VIDEO_HW_INITIALIZATION_DATA InitData;

    /* Zero initialization structure and array of extensions, one per screen */
    DPRINT1("VMX-SVGAII Loading...\n");
    VmxFenceSelfTest();
    VideoPortZeroMemory(VmxDeviceExtensionArray, sizeof(VmxDeviceExtensionArray));
    VideoPortZeroMemory(VmxDisplayWidths, sizeof(VmxDisplayWidths));
    VideoPortZeroMemory(VmxDisplayHeights, sizeof(VmxDisplayHeights));
    VideoPortZeroMemory(VmxDisplayXOffsets, sizeof(VmxDisplayXOffsets));
    VideoPortZeroMemory(VmxDisplayYOffsets, sizeof(VmxDisplayYOffsets));
    VmxExpectedDisplays = 1;
    VmxEnumeratedDisplays = 0;
    VideoPortZeroMemory(&InitData, sizeof(InitData));

    /* Setup the initialization structure with VideoPort */
    InitData.HwInitDataSize = sizeof(VIDEO_HW_INITIALIZATION_DATA);
    InitData.HwFindAdapter = VmxFindAdapter;
    InitData.HwInitialize = VmxInitialize;
    InitData.HwInterrupt = VmxInterrupt;
    InitData.HwStartIO = VmxStartIO;
    InitData.HwResetHw = VmxResetHw;
    InitData.HwGetPowerState = VmxGetPowerState;
    InitData.HwSetPowerState = VmxSetPowerState;
    InitData.HwGetVideoChildDescriptor = VmxGetVideoChildDescriptor;
    InitData.AdapterInterfaceType = PCIBus;
    InitData.HwInitDataSize = sizeof(VIDEO_HW_INITIALIZATION_DATA);
    InitData.HwDeviceExtensionSize = sizeof(HW_DEVICE_EXTENSION);
    return VideoPortInitialize(Context1, Context2, &InitData, NULL);
}
