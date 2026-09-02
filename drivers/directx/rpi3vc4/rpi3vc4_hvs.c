/*
 * PROJECT:     ReactOS Raspberry Pi 3 VC4 WDDM miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     BCM2837 HVS4 scanout and cursor-plane programming
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "rpi3vc4.h"
#include "softgpu_2d_core.h"

typedef enum _RPI3VC4_HVS_SCALING
{
    Rpi3Vc4HvsScalingNone,
    Rpi3Vc4HvsScalingPpf,
    Rpi3Vc4HvsScalingTpz
} RPI3VC4_HVS_SCALING;

typedef struct _RPI3VC4_HVS_OVERLAY_PLANE
{
    PHYSICAL_ADDRESS LumaPhysical;
    PHYSICAL_ADDRESS ChromaPhysical;
    ULONG LumaPitch;
    ULONG ChromaPitch;
    ULONG SourceWidth;
    ULONG SourceHeight;
    ULONG DestinationX;
    ULONG DestinationY;
    ULONG DestinationWidth;
    ULONG DestinationHeight;
    ULONG InfoFlags;
} RPI3VC4_HVS_OVERLAY_PLANE, *PRPI3VC4_HVS_OVERLAY_PLANE;

static BOOLEAN
Rpi3Vc4WaitForDisplayList(
    _In_ PRPI3VC4_CONTEXT Context,
    _In_ ULONG Head);

static PULONG
Rpi3Vc4Register(
    _In_ PVOID Base,
    _In_ ULONG Offset)
{
    return (PULONG)((PUCHAR)Base + Offset);
}

static BOOLEAN
Rpi3Vc4FindMemoryResource(
    _In_ PCM_RESOURCE_LIST Resources,
    _In_ ULONGLONG PhysicalBase,
    _In_ ULONG MinimumLength,
    _Out_ PPHYSICAL_ADDRESS Address)
{
    ULONG ListIndex;

    for (ListIndex = 0; ListIndex < Resources->Count; ++ListIndex)
    {
        PCM_PARTIAL_RESOURCE_LIST Partial;
        ULONG DescriptorIndex;

        Partial = &Resources->List[ListIndex].PartialResourceList;
        for (DescriptorIndex = 0;
             DescriptorIndex < Partial->Count;
             ++DescriptorIndex)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;

            Descriptor = &Partial->PartialDescriptors[DescriptorIndex];
            if (Descriptor->Type == CmResourceTypeMemory &&
                Descriptor->u.Memory.Start.QuadPart == PhysicalBase &&
                Descriptor->u.Memory.Length >= MinimumLength)
            {
                *Address = Descriptor->u.Memory.Start;
                return TRUE;
            }
        }
    }

    return FALSE;
}

static NTSTATUS
Rpi3Vc4MapResources(
    _Inout_ PRPI3VC4_CONTEXT Context,
    _In_ PDXGK_INTERFACE DxgkInterface)
{
    DXGK_DEVICE_INFO DeviceInfo;
    PCM_RESOURCE_LIST Resources;

    if (DxgkInterface->Size <
            FIELD_OFFSET(DXGK_INTERFACE,
                         DxgkCbGetDeviceInformation) +
                sizeof(DxgkInterface->DxgkCbGetDeviceInformation) ||
        DxgkInterface->DxgkCbGetDeviceInformation == NULL)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    RtlZeroMemory(&DeviceInfo, sizeof(DeviceInfo));
    if (!NT_SUCCESS(DxgkInterface->DxgkCbGetDeviceInformation(
                        DxgkInterface->DeviceHandle,
                        &DeviceInfo)))
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Resources = DeviceInfo.TranslatedResourceList;
    if (Resources == NULL ||
        !Rpi3Vc4FindMemoryResource(Resources,
                                   RPI3VC4_HVS_PHYSICAL_BASE,
                                   RPI3VC4_HVS_LENGTH,
                                   &Context->HvsPhysical) ||
        !Rpi3Vc4FindMemoryResource(Resources,
                                   RPI3VC4_PV2_PHYSICAL_BASE,
                                   RPI3VC4_PV2_LENGTH,
                                   &Context->Pv2Physical))
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (Rpi3Vc4FindMemoryResource(Resources,
                                  RPI3VC4_V3D_PHYSICAL_BASE,
                                  RPI3VC4_V3D_LENGTH,
                                  &Context->V3dPhysical))
    {
        Context->V3dBase = MmMapIoSpace(Context->V3dPhysical,
                                        RPI3VC4_V3D_LENGTH,
                                        MmNonCached);
    }

    Context->HvsBase = MmMapIoSpace(Context->HvsPhysical,
                                    RPI3VC4_HVS_LENGTH,
                                    MmNonCached);
    Context->Pv2Base = MmMapIoSpace(Context->Pv2Physical,
                                    RPI3VC4_PV2_LENGTH,
                                    MmNonCached);
    if (Context->HvsBase == NULL ||
        Context->Pv2Base == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
Rpi3Vc4ValidateHardware(
    _Inout_ PRPI3VC4_CONTEXT Context)
{
    ULONG HvsControl;
    ULONG ChannelControl;
    ULONG ChannelStatus;
    ULONG PvControl;
    ULONG PvVControl;

    HvsControl = READ_REGISTER_ULONG(
                     Rpi3Vc4Register(Context->HvsBase,
                                     RPI3VC4_HVS_DISPCTRL));
    ChannelControl = READ_REGISTER_ULONG(
                         Rpi3Vc4Register(Context->HvsBase,
                                         RPI3VC4_HVS_DISPCTRL1));
    ChannelStatus = READ_REGISTER_ULONG(
                        Rpi3Vc4Register(Context->HvsBase,
                                        RPI3VC4_HVS_DISPSTAT1));
    PvControl = READ_REGISTER_ULONG(
                    Rpi3Vc4Register(Context->Pv2Base,
                                    RPI3VC4_PV_CONTROL));
    PvVControl = READ_REGISTER_ULONG(
                     Rpi3Vc4Register(Context->Pv2Base,
                                     RPI3VC4_PV_V_CONTROL));

    if ((HvsControl & RPI3VC4_HVS_DISPCTRL_ENABLE) == 0 ||
        (ChannelControl & RPI3VC4_HVS_CHANNEL_ENABLE) == 0 ||
        (ChannelStatus & RPI3VC4_HVS_STATUS_MODE_MASK) !=
            RPI3VC4_HVS_STATUS_MODE_RUN ||
        (PvControl & RPI3VC4_PV_CONTROL_ENABLE) == 0 ||
        (PvVControl & RPI3VC4_PV_V_CONTROL_ENABLE) == 0)
    {
        return STATUS_DEVICE_NOT_READY;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
Rpi3Vc4QueryPlatform(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ PDXGK_INTERFACE DxgkInterface,
    _Out_ PSOFTGPU_PLATFORM_CONFIG Config)
{
    PRPI3VC4_CONTEXT Context;
    DXGK_DISPLAY_INFORMATION PostDisplayInfo;
    ULONGLONG VisibleLength;
    NTSTATUS Status;

    if (Device == NULL || DxgkInterface == NULL || Config == NULL ||
        Device->PlatformContext != NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Context = ExAllocatePoolWithTag(NonPagedPool,
                                    sizeof(*Context),
                                    RPI3VC4_POOL_TAG);
    if (Context == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Context, sizeof(*Context));
    KeInitializeMutex(&Context->V3dPowerMutex, 0);
    KeInitializeSpinLock(&Context->V3dQueueLock);
    Context->Device = Device;
    Context->V3dStatus = STATUS_DEVICE_NOT_READY;
    Device->PlatformContext = Context;

    Status = Rpi3Vc4MapResources(Context, DxgkInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = Rpi3Vc4ValidateHardware(Context);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(Config, sizeof(*Config));
    Status = SoftGpuAcquirePostDisplay(DxgkInterface,
                                       &PostDisplayInfo,
                                       &VisibleLength);
    if (!NT_SUCCESS(Status) ||
        PostDisplayInfo.Width == 0 ||
        PostDisplayInfo.Width > 0xfffUL ||
        PostDisplayInfo.Height > 0xfffUL ||
        PostDisplayInfo.Pitch > 0xffffUL)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    Config->Width = PostDisplayInfo.Width;
    Config->Height = PostDisplayInfo.Height;
    Config->Format = PostDisplayInfo.ColorFormat;
    Config->ScanoutPhysicalAddress = PostDisplayInfo.PhysicAddress;
    Config->ScanoutPitch = PostDisplayInfo.Pitch;
    Config->ScanoutSize = VisibleLength;
    Config->HighestFrameBufferAddress.QuadPart =
        RPI3VC4_HIGHEST_SCANOUT_ADDRESS;
    Config->MinimumAllocationSlabSize = SOFTGPU_MAX_ALLOCATION_SLAB_SIZE;
    return STATUS_SUCCESS;
}

static BOOLEAN
Rpi3Vc4DeriveBusAlias(
    _Inout_ PRPI3VC4_CONTEXT Context,
    _In_ PHYSICAL_ADDRESS FirmwareScanout)
{
    PULONG DisplayList;
    ULONG Head;
    ULONG Iteration;

    if ((ULONGLONG)FirmwareScanout.QuadPart >
            RPI3VC4_HIGHEST_SCANOUT_ADDRESS)
    {
        return FALSE;
    }

    DisplayList = Rpi3Vc4Register(Context->HvsBase,
                                  RPI3VC4_HVS_DLIST_OFFSET);
    Head = Context->OriginalDisplayList &
           RPI3VC4_HVS_LIST_HEAD_MASK;
    for (Iteration = 0;
         Iteration < RPI3VC4_HVS_DLIST_DWORDS;
         ++Iteration)
    {
        ULONG Control;
        ULONG Size;

        if (Head >= RPI3VC4_HVS_DLIST_DWORDS)
            return FALSE;
        Control = READ_REGISTER_ULONG(&DisplayList[Head]);
        if ((Control & RPI3VC4_HVS_CTL0_END) != 0)
            break;

        Size = (Control >> RPI3VC4_HVS_CTL0_SIZE_SHIFT) &
               RPI3VC4_HVS_CTL0_SIZE_MASK;
        if (Size < RPI3VC4_HVS_PLANE_DWORDS ||
            Head > RPI3VC4_HVS_DLIST_DWORDS - Size)
        {
            return FALSE;
        }

        if ((Control & RPI3VC4_HVS_CTL0_VALID) != 0)
        {
            ULONG PointerOffset;
            ULONG Pointer;

            PointerOffset =
                (Control & RPI3VC4_HVS_CTL0_UNITY) != 0 ? 4 : 5;
            if (PointerOffset >= Size)
                return FALSE;
            Pointer = READ_REGISTER_ULONG(
                          &DisplayList[Head + PointerOffset]);
            if ((Pointer & RPI3VC4_GPU_ADDRESS_MASK) ==
                    ((ULONG)FirmwareScanout.QuadPart &
                     RPI3VC4_GPU_ADDRESS_MASK))
            {
                Context->BusAlias =
                    Pointer & RPI3VC4_GPU_ALIAS_MASK;
                return TRUE;
            }
        }

        Head += Size;
    }

    return FALSE;
}

static ULONG
Rpi3Vc4GpuAddress(
    _In_ PRPI3VC4_CONTEXT Context,
    _In_ PHYSICAL_ADDRESS PhysicalAddress)
{
    return Context->BusAlias |
           ((ULONG)PhysicalAddress.QuadPart &
            RPI3VC4_GPU_ADDRESS_MASK);
}

static VOID
Rpi3Vc4ReleaseScanoutBuffers(
    _Inout_ PRPI3VC4_CONTEXT Context)
{
    if (Context->ScanoutBuffers != NULL)
    {
        MmFreeContiguousMemorySpecifyCache(Context->ScanoutBuffers,
                                           Context->ScanoutAllocationSize,
                                           MmWriteCombined);
        Context->ScanoutBuffers = NULL;
    }
    Context->ScanoutSurfaceSize = 0;
    Context->ScanoutBufferStride = 0;
    Context->ScanoutAllocationSize = 0;
}

static NTSTATUS
Rpi3Vc4AllocateScanoutBuffers(
    _Inout_ PSOFTGPU_DEVICE Device,
    _Inout_ PRPI3VC4_CONTEXT Context)
{
    PHYSICAL_ADDRESS LowAddress;
    PHYSICAL_ADDRESS HighAddress;
    PHYSICAL_ADDRESS SkipBytes;
    SIZE_T SurfaceSize;
    SIZE_T BufferStride;
    SIZE_T AllocationSize;
    ULONG Index;

    if (Device->Width == 0 || Device->Height == 0 ||
        Device->ScanoutPitch < Device->Width * sizeof(ULONG) ||
        Device->Height > MAXULONG_PTR / Device->ScanoutPitch)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    SurfaceSize = (SIZE_T)Device->ScanoutPitch * Device->Height;
    if (SurfaceSize > MAXULONG_PTR - ((SIZE_T)PAGE_SIZE - 1))
        return STATUS_INTEGER_OVERFLOW;
    BufferStride = (SurfaceSize + PAGE_SIZE - 1) &
                   ~((SIZE_T)PAGE_SIZE - 1);
    if (BufferStride > MAXULONG_PTR / RPI3VC4_SCANOUT_BUFFER_COUNT)
        return STATUS_INTEGER_OVERFLOW;
    AllocationSize = BufferStride * RPI3VC4_SCANOUT_BUFFER_COUNT;

    LowAddress.QuadPart = 0;
    HighAddress.QuadPart = RPI3VC4_HIGHEST_SCANOUT_ADDRESS;
    SkipBytes.QuadPart = 0;
    Context->ScanoutBuffers = MmAllocateContiguousMemorySpecifyCache(
                                  AllocationSize,
                                  LowAddress,
                                  HighAddress,
                                  SkipBytes,
                                  MmWriteCombined);
    if (Context->ScanoutBuffers == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Context->ScanoutSurfaceSize = SurfaceSize;
    Context->ScanoutBufferStride = BufferStride;
    Context->ScanoutAllocationSize = AllocationSize;
    Context->FrontBufferIndex = 0;
    RtlZeroMemory(Context->ScanoutBuffers, AllocationSize);
    for (Index = 0; Index < RPI3VC4_SCANOUT_BUFFER_COUNT; ++Index)
    {
        Context->ScanoutPhysical[Index] = MmGetPhysicalAddress(
            (PUCHAR)Context->ScanoutBuffers + Index * BufferStride);
        Context->FullDamage[Index] = TRUE;
        Context->PendingDamageValid[Index] = FALSE;
    }
    return STATUS_SUCCESS;
}

static BOOLEAN
Rpi3Vc4ClipDamageRect(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ const RECT *Input,
    _Out_ RECT *Output)
{
    *Output = *Input;
    Output->left = max(Output->left, 0);
    Output->top = max(Output->top, 0);
    Output->right = min(Output->right, (LONG)Device->Width);
    Output->bottom = min(Output->bottom, (LONG)Device->Height);
    return Output->left < Output->right && Output->top < Output->bottom;
}

static VOID
Rpi3Vc4UnionDamageRect(
    _Inout_ RECT *Destination,
    _Inout_ PBOOLEAN Valid,
    _In_ const RECT *Source)
{
    if (!*Valid)
    {
        *Destination = *Source;
        *Valid = TRUE;
        return;
    }

    Destination->left = min(Destination->left, Source->left);
    Destination->top = min(Destination->top, Source->top);
    Destination->right = max(Destination->right, Source->right);
    Destination->bottom = max(Destination->bottom, Source->bottom);
}

static NTSTATUS
Rpi3Vc4CopyDamageRect(
    _In_ PVOID Source,
    _In_ SIZE_T SourceSize,
    _In_ ULONG SourcePitch,
    _In_ const RECT *Rect,
    _In_ PVOID Destination,
    _In_ SIZE_T DestinationSize,
    _In_ ULONG DestinationPitch)
{
    return SoftGpu2dCopyRect(Source,
                             SourceSize,
                             SourcePitch,
                             Rect,
                             Destination,
                             DestinationSize,
                             DestinationPitch,
                             Rect);
}

static ULONG
Rpi3Vc4EmitPlane(
    _Out_writes_(RPI3VC4_HVS_PLANE_DWORDS) PULONG List,
    _In_ ULONG BusAddress,
    _In_ ULONG Pitch,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ BOOLEAN PerPixelAlpha)
{
    List[0] = RPI3VC4_HVS_CTL0_VALID |
              (RPI3VC4_HVS_PLANE_DWORDS <<
               RPI3VC4_HVS_CTL0_SIZE_SHIFT) |
              (RPI3VC4_HVS_ORDER_ABGR <<
               RPI3VC4_HVS_CTL0_ORDER_SHIFT) |
              (RPI3VC4_HVS_CTL0_RGBA_ROUND <<
               RPI3VC4_HVS_CTL0_RGBA_SHIFT) |
              RPI3VC4_HVS_CTL0_UNITY |
              RPI3VC4_HVS_FORMAT_RGBA8888;
    List[1] = (0xffUL << RPI3VC4_HVS_POS0_ALPHA_SHIFT) |
              ((Y & 0xfffUL) << RPI3VC4_HVS_POS0_Y_SHIFT) |
              (X & 0xfffUL);
    List[2] = ((PerPixelAlpha ?
                    RPI3VC4_HVS_POS2_ALPHA_PIPELINE :
                    RPI3VC4_HVS_POS2_ALPHA_FIXED) <<
               RPI3VC4_HVS_POS2_ALPHA_SHIFT) |
              ((Height & 0xfffUL) <<
               RPI3VC4_HVS_POS2_HEIGHT_SHIFT) |
              (Width & 0xfffUL);
    List[3] = RPI3VC4_HVS_CONTEXT_INIT;
    List[4] = BusAddress;
    List[5] = RPI3VC4_HVS_CONTEXT_INIT;
    List[6] = Pitch;
    return RPI3VC4_HVS_PLANE_DWORDS;
}

static RPI3VC4_HVS_SCALING
Rpi3Vc4GetScalingMode(
    _In_ ULONG Source,
    _In_ ULONG Destination)
{
    if (Destination == Source)
        return Rpi3Vc4HvsScalingNone;
    if ((ULONGLONG)3 * Destination >= (ULONGLONG)2 * Source)
        return Rpi3Vc4HvsScalingPpf;
    return Rpi3Vc4HvsScalingTpz;
}

static ULONG
Rpi3Vc4GetScalerField(
    _In_ RPI3VC4_HVS_SCALING Horizontal,
    _In_ RPI3VC4_HVS_SCALING Vertical)
{
    if (Horizontal == Rpi3Vc4HvsScalingPpf &&
        Vertical == Rpi3Vc4HvsScalingPpf)
    {
        return RPI3VC4_HVS_SCL_H_PPF_V_PPF;
    }
    if (Horizontal == Rpi3Vc4HvsScalingTpz &&
        Vertical == Rpi3Vc4HvsScalingPpf)
    {
        return RPI3VC4_HVS_SCL_H_TPZ_V_PPF;
    }
    if (Horizontal == Rpi3Vc4HvsScalingPpf &&
        Vertical == Rpi3Vc4HvsScalingTpz)
    {
        return RPI3VC4_HVS_SCL_H_PPF_V_TPZ;
    }
    if (Horizontal == Rpi3Vc4HvsScalingTpz &&
        Vertical == Rpi3Vc4HvsScalingTpz)
    {
        return RPI3VC4_HVS_SCL_H_TPZ_V_TPZ;
    }
    if (Horizontal == Rpi3Vc4HvsScalingPpf &&
        Vertical == Rpi3Vc4HvsScalingNone)
    {
        return RPI3VC4_HVS_SCL_H_PPF_V_NONE;
    }
    if (Horizontal == Rpi3Vc4HvsScalingNone &&
        Vertical == Rpi3Vc4HvsScalingPpf)
    {
        return RPI3VC4_HVS_SCL_H_NONE_V_PPF;
    }
    if (Horizontal == Rpi3Vc4HvsScalingNone &&
        Vertical == Rpi3Vc4HvsScalingTpz)
    {
        return RPI3VC4_HVS_SCL_H_NONE_V_TPZ;
    }
    if (Horizontal == Rpi3Vc4HvsScalingTpz &&
        Vertical == Rpi3Vc4HvsScalingNone)
    {
        return RPI3VC4_HVS_SCL_H_TPZ_V_NONE;
    }
    return 0;
}

static BOOLEAN
Rpi3Vc4AppendDisplayWord(
    _Out_writes_(Capacity) PULONG List,
    _In_ ULONG Capacity,
    _Inout_ PULONG Count,
    _In_ ULONG Value)
{
    if (*Count >= Capacity)
        return FALSE;
    List[(*Count)++] = Value;
    return TRUE;
}

static BOOLEAN
Rpi3Vc4WriteTpz(
    _Out_writes_(Capacity) PULONG List,
    _In_ ULONG Capacity,
    _Inout_ PULONG Count,
    _In_ ULONG Source,
    _In_ ULONG Destination)
{
    ULONGLONG Scale64;
    ULONG Scale;
    ULONG Reciprocal;

    if (Source == 0 || Destination == 0)
        return FALSE;
    Scale64 = ((ULONGLONG)Source << 16) / Destination;
    if (Scale64 == 0 || Scale64 > RPI3VC4_HVS_TPZ_SCALE_MASK)
        return FALSE;
    Scale = (ULONG)Scale64;
    Reciprocal = MAXULONG / Scale;
    if (Reciprocal > RPI3VC4_HVS_TPZ_RECIP_MASK)
        Reciprocal = RPI3VC4_HVS_TPZ_RECIP_MASK;
    return Rpi3Vc4AppendDisplayWord(
               List,
               Capacity,
               Count,
               Scale << RPI3VC4_HVS_TPZ_SCALE_SHIFT) &&
           Rpi3Vc4AppendDisplayWord(List,
                                    Capacity,
                                    Count,
                                    Reciprocal);
}

static BOOLEAN
Rpi3Vc4WritePpf(
    _Out_writes_(Capacity) PULONG List,
    _In_ ULONG Capacity,
    _Inout_ PULONG Count,
    _In_ ULONG Source,
    _In_ ULONG Destination,
    _In_ BOOLEAN Chroma)
{
    ULONGLONG Scale64;
    ULONGLONG Remainder;
    LONG Phase;

    if (Source == 0 || Destination == 0)
        return FALSE;
    Scale64 = ((ULONGLONG)Source << 16) / Destination;
    if (Scale64 == 0 || Scale64 > RPI3VC4_HVS_PPF_SCALE_MASK)
        return FALSE;
    if (!Chroma)
        Scale64 &= ~1ULL;
    Remainder = ((ULONGLONG)Source << 16) -
                (ULONGLONG)Destination * Scale64;
    Phase = Chroma ? -16 : -32;
    Phase += (LONG)(Remainder >> 11);
    if (Phase >= 64)
        Phase = 63;
    return Rpi3Vc4AppendDisplayWord(
               List,
               Capacity,
               Count,
               RPI3VC4_HVS_PPF_AGC |
                   ((ULONG)Scale64 <<
                    RPI3VC4_HVS_PPF_SCALE_SHIFT) |
                   ((ULONG)Phase & RPI3VC4_HVS_PPF_PHASE_MASK));
}

static BOOLEAN
Rpi3Vc4WriteScalingParameters(
    _Out_writes_(Capacity) PULONG List,
    _In_ ULONG Capacity,
    _Inout_ PULONG Count,
    _In_ ULONG SourceWidth,
    _In_ ULONG SourceHeight,
    _In_ ULONG DestinationWidth,
    _In_ ULONG DestinationHeight,
    _In_ RPI3VC4_HVS_SCALING Horizontal,
    _In_ RPI3VC4_HVS_SCALING Vertical,
    _In_ BOOLEAN Chroma)
{
    if (Horizontal == Rpi3Vc4HvsScalingPpf &&
        !Rpi3Vc4WritePpf(List,
                          Capacity,
                          Count,
                          SourceWidth,
                          DestinationWidth,
                          Chroma))
    {
        return FALSE;
    }
    if (Vertical == Rpi3Vc4HvsScalingPpf &&
        (!Rpi3Vc4WritePpf(List,
                           Capacity,
                           Count,
                           SourceHeight,
                           DestinationHeight,
                           Chroma) ||
         !Rpi3Vc4AppendDisplayWord(List,
                                   Capacity,
                                   Count,
                                   RPI3VC4_HVS_CONTEXT_INIT)))
    {
        return FALSE;
    }
    if (Horizontal == Rpi3Vc4HvsScalingTpz &&
        !Rpi3Vc4WriteTpz(List,
                          Capacity,
                          Count,
                          SourceWidth,
                          DestinationWidth))
    {
        return FALSE;
    }
    if (Vertical == Rpi3Vc4HvsScalingTpz &&
        (!Rpi3Vc4WriteTpz(List,
                           Capacity,
                           Count,
                           SourceHeight,
                           DestinationHeight) ||
         !Rpi3Vc4AppendDisplayWord(List,
                                   Capacity,
                                   Count,
                                   RPI3VC4_HVS_CONTEXT_INIT)))
    {
        return FALSE;
    }
    return TRUE;
}

static ULONG
Rpi3Vc4PackFilterWord(
    _In_ LONG Coefficient0,
    _In_ LONG Coefficient1,
    _In_ LONG Coefficient2)
{
    return ((ULONG)Coefficient0 & 0x1ffUL) |
           (((ULONG)Coefficient1 & 0x1ffUL) << 9) |
           (((ULONG)Coefficient2 & 0x1ffUL) << 18);
}

static VOID
Rpi3Vc4UploadScalingFilter(
    _Inout_ PRPI3VC4_CONTEXT Context)
{
    static const LONG Coefficients[16] =
    {
        0, -2, -6, -8, -10, -8, -3, 2,
        18, 50, 82, 119, 155, 187, 213, 227
    };
    PULONG DisplayList;
    ULONG Kernel[6];
    ULONG Index;

    Kernel[0] = Rpi3Vc4PackFilterWord(Coefficients[0],
                                      Coefficients[1],
                                      Coefficients[2]);
    Kernel[1] = Rpi3Vc4PackFilterWord(Coefficients[3],
                                      Coefficients[4],
                                      Coefficients[5]);
    Kernel[2] = Rpi3Vc4PackFilterWord(Coefficients[6],
                                      Coefficients[7],
                                      Coefficients[8]);
    Kernel[3] = Rpi3Vc4PackFilterWord(Coefficients[9],
                                      Coefficients[10],
                                      Coefficients[11]);
    Kernel[4] = Rpi3Vc4PackFilterWord(Coefficients[12],
                                      Coefficients[13],
                                      Coefficients[14]);
    Kernel[5] = Rpi3Vc4PackFilterWord(Coefficients[15],
                                      Coefficients[15],
                                      0);

    DisplayList = Rpi3Vc4Register(Context->HvsBase,
                                  RPI3VC4_HVS_DLIST_OFFSET);
    for (Index = 0; Index < RPI3VC4_HVS_FILTER_DWORDS; ++Index)
    {
        ULONG SourceIndex;

        SourceIndex = Index < RTL_NUMBER_OF(Kernel) ?
                          Index :
                          RPI3VC4_HVS_FILTER_DWORDS - Index - 1;
        WRITE_REGISTER_ULONG(
            &DisplayList[RPI3VC4_HVS_FILTER_OFFSET + Index],
            Kernel[SourceIndex]);
    }
}

static BOOLEAN
Rpi3Vc4SurfacePlaneFits(
    _In_ SIZE_T AllocationSize,
    _In_ ULONG Offset,
    _In_ ULONG Pitch,
    _In_ ULONG Rows,
    _In_ ULONG RowBytes)
{
    ULONGLONG End;

    if (Pitch == 0 || Rows == 0 || RowBytes == 0 || RowBytes > Pitch)
        return FALSE;
    End = (ULONGLONG)Offset +
          (ULONGLONG)(Rows - 1) * Pitch + RowBytes;
    return End <= AllocationSize;
}

static NTSTATUS
Rpi3Vc4ReadOverlayPrivateData(
    _In_reads_bytes_(PrivateDataSize) const VOID *PrivateData,
    _In_ ULONG PrivateDataSize,
    _Out_ PULONG InfoFlags)
{
    const SOFTGPU_OVERLAY_PRIVATE_DATA *Data;

    if (PrivateData == NULL ||
        PrivateDataSize != sizeof(SOFTGPU_OVERLAY_PRIVATE_DATA))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Data = PrivateData;
    if (Data->Magic != SOFTGPU_OVERLAY_PRIVATE_MAGIC ||
        Data->Version != SOFTGPU_OVERLAY_PRIVATE_VERSION ||
        (Data->InfoFlags & ~SOFTGPU_OVERLAY_INFO_ALLOWED) != 0 ||
        Data->FlipFlags != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *InfoFlags = Data->InfoFlags;
    return STATUS_SUCCESS;
}

static NTSTATUS
Rpi3Vc4ReadOverlayInfo(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ const DXGK_OVERLAYINFO *Info,
    _Out_ PRPI3VC4_OVERLAY State)
{
    PSOFTGPU_ALLOC Allocation;
    ULONGLONG AllocationEnd;
    ULONGLONG LumaStorageEnd;
    NTSTATUS Status;

    if (Device == NULL || Info == NULL || State == NULL ||
        Info->hAllocation == NULL ||
        Info->SegmentId != SOFTGPU_SEGMENT_ID ||
        Info->PhysicalAddress.QuadPart < 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Allocation = (PSOFTGPU_ALLOC)Info->hAllocation;
    if (Allocation->Magic != SOFTGPU_ALLOC_MAGIC ||
        Allocation->Format != SOFTGPU_D3DDDIFMT_NV12 ||
        Allocation->PlaneCount != 2 ||
        Allocation->Width == 0 || Allocation->Height == 0 ||
        Allocation->Width > 0xfffUL ||
        Allocation->Height > 0xfffUL ||
        (Allocation->Width & 1) != 0 ||
        (Allocation->Height & 1) != 0 ||
        Allocation->StorageHeight < Allocation->Height ||
        Allocation->Pitch != Allocation->PlanePitches[0] ||
        Allocation->PlanePitches[0] > 0xffffUL ||
        Allocation->PlanePitches[1] > 0xffffUL ||
        !Rpi3Vc4SurfacePlaneFits(Allocation->Size,
                                 Allocation->PlaneOffsets[0],
                                 Allocation->PlanePitches[0],
                                 Allocation->Height,
                                 Allocation->Width) ||
        !Rpi3Vc4SurfacePlaneFits(Allocation->Size,
                                 Allocation->PlaneOffsets[1],
                                 Allocation->PlanePitches[1],
                                 Allocation->Height / 2,
                                 Allocation->Width))
    {
        return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
    }
    LumaStorageEnd =
        (ULONGLONG)Allocation->PlaneOffsets[0] +
        (ULONGLONG)Allocation->PlanePitches[0] *
            Allocation->StorageHeight;
    if (LumaStorageEnd > Allocation->Size ||
        Allocation->PlaneOffsets[1] < LumaStorageEnd)
    {
        return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
    }
    if (Info->SrcRect.left < 0 || Info->SrcRect.top < 0 ||
        Info->SrcRect.right <= Info->SrcRect.left ||
        Info->SrcRect.bottom <= Info->SrcRect.top ||
        Info->SrcRect.right > (LONG)Allocation->Width ||
        Info->SrcRect.bottom > (LONG)Allocation->Height ||
        ((Info->SrcRect.left | Info->SrcRect.top |
          Info->SrcRect.right | Info->SrcRect.bottom) & 1) != 0 ||
        Info->DstRect.right <= Info->DstRect.left ||
        Info->DstRect.bottom <= Info->DstRect.top)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Allocation->Size == 0 ||
        (ULONGLONG)Info->PhysicalAddress.QuadPart >
            RPI3VC4_HIGHEST_SCANOUT_ADDRESS)
    {
        return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
    }
    AllocationEnd = (ULONGLONG)Info->PhysicalAddress.QuadPart +
                    Allocation->Size - 1;
    if (AllocationEnd < (ULONGLONG)Info->PhysicalAddress.QuadPart ||
        AllocationEnd > RPI3VC4_HIGHEST_SCANOUT_ADDRESS)
    {
        return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
    }

    RtlZeroMemory(State, sizeof(*State));
    Status = Rpi3Vc4ReadOverlayPrivateData(
                 Info->pPrivateDriverData,
                 Info->PrivateDriverDataSize,
                 &State->InfoFlags);
    if (!NT_SUCCESS(Status))
        return Status;
    State->Allocation = Allocation;
    State->PhysicalAddress = Info->PhysicalAddress;
    State->SegmentId = Info->SegmentId;
    State->SourceRect = Info->SrcRect;
    State->DestinationRect = Info->DstRect;
    State->Enabled = TRUE;
    return STATUS_SUCCESS;
}

static BOOLEAN
Rpi3Vc4BuildOverlayPlane(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ const RPI3VC4_OVERLAY *Overlay,
    _Out_ PRPI3VC4_HVS_OVERLAY_PLANE Plane)
{
    const RECT *Source;
    const RECT *Destination;
    RECT Clipped;
    ULONGLONG SourceWidth;
    ULONGLONG SourceHeight;
    ULONGLONG DestinationWidth;
    ULONGLONG DestinationHeight;
    ULONGLONG MappedLeft;
    ULONGLONG MappedTop;
    ULONGLONG MappedRight;
    ULONGLONG MappedBottom;
    ULONGLONG LumaOffset;
    ULONGLONG ChromaOffset;

    if (!Overlay->Enabled)
        return FALSE;
    Source = &Overlay->SourceRect;
    Destination = &Overlay->DestinationRect;
    Clipped.left = max(Destination->left, 0);
    Clipped.top = max(Destination->top, 0);
    Clipped.right = min(Destination->right, (LONG)Device->Width);
    Clipped.bottom = min(Destination->bottom, (LONG)Device->Height);
    if (Clipped.right <= Clipped.left || Clipped.bottom <= Clipped.top)
        return FALSE;

    SourceWidth = Source->right - Source->left;
    SourceHeight = Source->bottom - Source->top;
    DestinationWidth = (ULONGLONG)
        ((LONGLONG)Destination->right - Destination->left);
    DestinationHeight = (ULONGLONG)
        ((LONGLONG)Destination->bottom - Destination->top);
    MappedLeft = Source->left +
                 ((ULONGLONG)((LONGLONG)Clipped.left -
                              Destination->left) *
                  SourceWidth) / DestinationWidth;
    MappedTop = Source->top +
                ((ULONGLONG)((LONGLONG)Clipped.top -
                             Destination->top) *
                 SourceHeight) / DestinationHeight;
    MappedRight = Source->left +
                  (((ULONGLONG)((LONGLONG)Clipped.right -
                                Destination->left) *
                    SourceWidth) + DestinationWidth - 1) /
                  DestinationWidth;
    MappedBottom = Source->top +
                   (((ULONGLONG)((LONGLONG)Clipped.bottom -
                                 Destination->top) *
                     SourceHeight) + DestinationHeight - 1) /
                   DestinationHeight;

    MappedLeft &= ~1ULL;
    MappedTop &= ~1ULL;
    MappedRight = (MappedRight + 1) & ~1ULL;
    MappedBottom = (MappedBottom + 1) & ~1ULL;
    MappedLeft = max(MappedLeft, (ULONGLONG)Source->left);
    MappedTop = max(MappedTop, (ULONGLONG)Source->top);
    MappedRight = min(MappedRight, (ULONGLONG)Source->right);
    MappedBottom = min(MappedBottom, (ULONGLONG)Source->bottom);
    if (MappedRight <= MappedLeft || MappedBottom <= MappedTop)
        return FALSE;

    LumaOffset = Overlay->Allocation->PlaneOffsets[0] +
                 MappedTop * Overlay->Allocation->PlanePitches[0] +
                 MappedLeft;
    ChromaOffset = Overlay->Allocation->PlaneOffsets[1] +
                   (MappedTop / 2) *
                       Overlay->Allocation->PlanePitches[1] +
                   MappedLeft;
    Plane->LumaPhysical.QuadPart =
        Overlay->PhysicalAddress.QuadPart + LumaOffset;
    Plane->ChromaPhysical.QuadPart =
        Overlay->PhysicalAddress.QuadPart + ChromaOffset;
    Plane->LumaPitch = Overlay->Allocation->PlanePitches[0];
    Plane->ChromaPitch = Overlay->Allocation->PlanePitches[1];
    Plane->SourceWidth = (ULONG)(MappedRight - MappedLeft);
    Plane->SourceHeight = (ULONG)(MappedBottom - MappedTop);
    Plane->DestinationX = Clipped.left;
    Plane->DestinationY = Clipped.top;
    Plane->DestinationWidth = Clipped.right - Clipped.left;
    Plane->DestinationHeight = Clipped.bottom - Clipped.top;
    Plane->InfoFlags = Overlay->InfoFlags;
    return TRUE;
}

static NTSTATUS
Rpi3Vc4EmitNv12Overlay(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ PRPI3VC4_CONTEXT Context,
    _In_ const RPI3VC4_OVERLAY *Overlay,
    _Out_writes_(Capacity) PULONG List,
    _In_ ULONG Capacity,
    _Inout_ PULONG Count)
{
    RPI3VC4_HVS_OVERLAY_PLANE Plane;
    RPI3VC4_HVS_SCALING ChromaHorizontal;
    RPI3VC4_HVS_SCALING ChromaVertical;
    RPI3VC4_HVS_SCALING LumaHorizontal;
    RPI3VC4_HVS_SCALING LumaVertical;
    ULONG ControlIndex;
    ULONG PlaneStart;
    ULONG PlaneDwords;
    ULONG LbmSize;
    ULONG Kernel;
    BOOLEAN UsesPpf;

    if (!Rpi3Vc4BuildOverlayPlane(Device, Overlay, &Plane))
        return STATUS_SUCCESS;

    ChromaHorizontal = Rpi3Vc4GetScalingMode(
                           Plane.SourceWidth / 2,
                           Plane.DestinationWidth);
    ChromaVertical = Rpi3Vc4GetScalingMode(
                         Plane.SourceHeight / 2,
                         Plane.DestinationHeight);
    LumaHorizontal = Rpi3Vc4GetScalingMode(
                         Plane.SourceWidth,
                         Plane.DestinationWidth);
    LumaVertical = Rpi3Vc4GetScalingMode(
                       Plane.SourceHeight,
                       Plane.DestinationHeight);

    /* VC4 performs YUV-to-RGB conversion in the scaler. The chroma channel
     * must remain enabled when its subsampled dimensions otherwise happen
     * to match the destination. */
    if (ChromaHorizontal == Rpi3Vc4HvsScalingNone)
        ChromaHorizontal = Rpi3Vc4HvsScalingPpf;
    if (ChromaVertical == Rpi3Vc4HvsScalingNone)
        ChromaVertical = Rpi3Vc4HvsScalingPpf;

    LbmSize = max(Plane.SourceWidth, Plane.DestinationWidth) * 16UL;
    LbmSize = (LbmSize + RPI3VC4_HVS_LBM_ALIGNMENT - 1) &
              ~(RPI3VC4_HVS_LBM_ALIGNMENT - 1);
    if (LbmSize == 0 || LbmSize > RPI3VC4_HVS_LBM_BYTES)
        return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;

    PlaneStart = *Count;
    ControlIndex = *Count;
    if (!Rpi3Vc4AppendDisplayWord(List, Capacity, Count, 0) ||
        !Rpi3Vc4AppendDisplayWord(
             List,
             Capacity,
             Count,
             (0xffUL << RPI3VC4_HVS_POS0_ALPHA_SHIFT) |
                 ((Plane.DestinationY & 0xfffUL) <<
                  RPI3VC4_HVS_POS0_Y_SHIFT) |
                 (Plane.DestinationX & 0xfffUL)) ||
        !Rpi3Vc4AppendDisplayWord(
             List,
             Capacity,
             Count,
             ((Plane.DestinationHeight & 0xfffUL) <<
              RPI3VC4_HVS_POS1_HEIGHT_SHIFT) |
                 (Plane.DestinationWidth & 0xfffUL)) ||
        !Rpi3Vc4AppendDisplayWord(
             List,
             Capacity,
             Count,
             (RPI3VC4_HVS_POS2_ALPHA_FIXED <<
              RPI3VC4_HVS_POS2_ALPHA_SHIFT) |
                 ((Plane.SourceHeight & 0xfffUL) <<
                  RPI3VC4_HVS_POS2_HEIGHT_SHIFT) |
                 (Plane.SourceWidth & 0xfffUL)) ||
        !Rpi3Vc4AppendDisplayWord(List,
                                  Capacity,
                                  Count,
                                  RPI3VC4_HVS_CONTEXT_INIT) ||
        !Rpi3Vc4AppendDisplayWord(
             List,
             Capacity,
             Count,
             Rpi3Vc4GpuAddress(Context, Plane.LumaPhysical)) ||
        !Rpi3Vc4AppendDisplayWord(
             List,
             Capacity,
             Count,
             Rpi3Vc4GpuAddress(Context, Plane.ChromaPhysical)) ||
        !Rpi3Vc4AppendDisplayWord(List,
                                  Capacity,
                                  Count,
                                  RPI3VC4_HVS_CONTEXT_INIT) ||
        !Rpi3Vc4AppendDisplayWord(List,
                                  Capacity,
                                  Count,
                                  RPI3VC4_HVS_CONTEXT_INIT) ||
        !Rpi3Vc4AppendDisplayWord(List,
                                  Capacity,
                                  Count,
                                  Plane.LumaPitch) ||
        !Rpi3Vc4AppendDisplayWord(List,
                                  Capacity,
                                  Count,
                                  Plane.ChromaPitch))
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    if ((Plane.InfoFlags & SOFTGPU_OVERLAY_INFO_LIMITED_RGB) == 0)
    {
        if (!Rpi3Vc4AppendDisplayWord(List,
                                      Capacity,
                                      Count,
                                      RPI3VC4_HVS_CSC0_FULL) ||
            !Rpi3Vc4AppendDisplayWord(List,
                                      Capacity,
                                      Count,
                                      (Plane.InfoFlags & SOFTGPU_OVERLAY_INFO_BT709)
                                          ? RPI3VC4_HVS_CSC1_709_FULL
                                          : RPI3VC4_HVS_CSC1_601_FULL) ||
            !Rpi3Vc4AppendDisplayWord(List,
                                      Capacity,
                                      Count,
                                      (Plane.InfoFlags & SOFTGPU_OVERLAY_INFO_BT709)
                                          ? RPI3VC4_HVS_CSC2_709_FULL
                                          : RPI3VC4_HVS_CSC2_601_FULL))
        {
            return STATUS_BUFFER_OVERFLOW;
        }
    }
    else if ((Plane.InfoFlags & SOFTGPU_OVERLAY_INFO_BT709) != 0)
    {
        if (!Rpi3Vc4AppendDisplayWord(List,
                                      Capacity,
                                      Count,
                                      RPI3VC4_HVS_CSC0_LIMITED) ||
            !Rpi3Vc4AppendDisplayWord(List,
                                      Capacity,
                                      Count,
                                      RPI3VC4_HVS_CSC1_709_LIMITED) ||
            !Rpi3Vc4AppendDisplayWord(List,
                                      Capacity,
                                      Count,
                                      RPI3VC4_HVS_CSC2_709_LIMITED))
        {
            return STATUS_BUFFER_OVERFLOW;
        }
    }
    else if (!Rpi3Vc4AppendDisplayWord(List,
                                       Capacity,
                                       Count,
                                       RPI3VC4_HVS_CSC0_LIMITED) ||
             !Rpi3Vc4AppendDisplayWord(List,
                                       Capacity,
                                       Count,
                                       RPI3VC4_HVS_CSC1_601_LIMITED) ||
             !Rpi3Vc4AppendDisplayWord(List,
                                       Capacity,
                                       Count,
                                       RPI3VC4_HVS_CSC2_601_LIMITED))
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    if (!Rpi3Vc4AppendDisplayWord(List, Capacity, Count, 0) ||
        !Rpi3Vc4WriteScalingParameters(
             List,
             Capacity,
             Count,
             Plane.SourceWidth / 2,
             Plane.SourceHeight / 2,
             Plane.DestinationWidth,
             Plane.DestinationHeight,
             ChromaHorizontal,
             ChromaVertical,
             TRUE) ||
        !Rpi3Vc4WriteScalingParameters(
             List,
             Capacity,
             Count,
             Plane.SourceWidth,
             Plane.SourceHeight,
             Plane.DestinationWidth,
             Plane.DestinationHeight,
             LumaHorizontal,
             LumaVertical,
             FALSE))
    {
        return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
    }

    UsesPpf = ChromaHorizontal == Rpi3Vc4HvsScalingPpf ||
              ChromaVertical == Rpi3Vc4HvsScalingPpf ||
              LumaHorizontal == Rpi3Vc4HvsScalingPpf ||
              LumaVertical == Rpi3Vc4HvsScalingPpf;
    if (UsesPpf)
    {
        Kernel = RPI3VC4_HVS_FILTER_OFFSET &
                 RPI3VC4_HVS_PPF_KERNEL_MASK;
        if (!Rpi3Vc4AppendDisplayWord(List, Capacity, Count, Kernel) ||
            !Rpi3Vc4AppendDisplayWord(List, Capacity, Count, Kernel) ||
            !Rpi3Vc4AppendDisplayWord(List, Capacity, Count, Kernel) ||
            !Rpi3Vc4AppendDisplayWord(List, Capacity, Count, Kernel))
        {
            return STATUS_BUFFER_OVERFLOW;
        }
    }

    PlaneDwords = *Count - PlaneStart;
    if (PlaneDwords > RPI3VC4_HVS_CTL0_SIZE_MASK)
        return STATUS_BUFFER_OVERFLOW;
    List[ControlIndex] =
        RPI3VC4_HVS_CTL0_VALID |
        (PlaneDwords << RPI3VC4_HVS_CTL0_SIZE_SHIFT) |
        (Rpi3Vc4GetScalerField(ChromaHorizontal,
                               ChromaVertical) <<
         RPI3VC4_HVS_CTL0_SCL0_SHIFT) |
        (Rpi3Vc4GetScalerField(LumaHorizontal,
                               LumaVertical) <<
         RPI3VC4_HVS_CTL0_SCL1_SHIFT) |
        RPI3VC4_HVS_FORMAT_NV12;
    return STATUS_SUCCESS;
}

static BOOLEAN
Rpi3Vc4ClipPointer(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ PRPI3VC4_CONTEXT Context,
    _Out_ PULONG X,
    _Out_ PULONG Y,
    _Out_ PULONG Width,
    _Out_ PULONG Height,
    _Out_ PULONG BusAddress)
{
    LONGLONG Left;
    LONGLONG Top;
    LONGLONG Right;
    LONGLONG Bottom;
    ULONG SourceX;
    ULONG SourceY;
    PHYSICAL_ADDRESS CursorAddress;

    if (!Context->PrimaryVisible ||
        !Device->PointerShapeValid ||
        !Device->PointerVisible ||
        Device->PointerWidth == 0 ||
        Device->PointerHeight == 0)
    {
        return FALSE;
    }

    Left = (LONGLONG)Device->PointerX - Device->PointerHotX;
    Top = (LONGLONG)Device->PointerY - Device->PointerHotY;
    Right = Left + Device->PointerWidth;
    Bottom = Top + Device->PointerHeight;
    SourceX = 0;
    SourceY = 0;
    if (Left < 0)
    {
        SourceX = (ULONG)-Left;
        Left = 0;
    }
    if (Top < 0)
    {
        SourceY = (ULONG)-Top;
        Top = 0;
    }
    if (Right > (LONG)Device->Width)
        Right = Device->Width;
    if (Bottom > (LONG)Device->Height)
        Bottom = Device->Height;
    if (Left >= Right || Top >= Bottom)
        return FALSE;

    CursorAddress.QuadPart =
        Context->CursorPhysical.QuadPart +
        ((ULONGLONG)SourceY * RPI3VC4_CURSOR_PITCH) +
        ((ULONGLONG)SourceX * sizeof(ULONG));
    *X = (ULONG)Left;
    *Y = (ULONG)Top;
    *Width = (ULONG)(Right - Left);
    *Height = (ULONG)(Bottom - Top);
    *BusAddress = Rpi3Vc4GpuAddress(Context, CursorAddress);
    return TRUE;
}

static BOOLEAN
Rpi3Vc4IsPrivateDisplayList(
    _In_ ULONG Head)
{
    ULONG LastSlot;

    LastSlot = RPI3VC4_HVS_DLIST_SLOT(
                   RPI3VC4_HVS_DLIST_SLOT_COUNT - 1);
    return Head >= RPI3VC4_HVS_DLIST_PRIVATE_BASE &&
           Head <= LastSlot &&
           (Head - RPI3VC4_HVS_DLIST_PRIVATE_BASE) %
               RPI3VC4_HVS_DLIST_SLOT_STRIDE == 0;
}

static BOOLEAN
Rpi3Vc4ChooseDisplayList(
    _In_ PRPI3VC4_CONTEXT Context,
    _Out_ PULONG Slot)
{
    ULONG Active;
    ULONG Requested;
    ULONG Start;
    ULONG Offset;

    Active = READ_REGISTER_ULONG(
                 Rpi3Vc4Register(Context->HvsBase,
                                 RPI3VC4_HVS_DISPLACT1)) &
             RPI3VC4_HVS_LIST_HEAD_MASK;
    Requested = READ_REGISTER_ULONG(
                    Rpi3Vc4Register(Context->HvsBase,
                                    RPI3VC4_HVS_DISPLIST1)) &
                RPI3VC4_HVS_LIST_HEAD_MASK;
    Start = 0;
    if (Rpi3Vc4IsPrivateDisplayList(
            Context->LastSubmittedDisplayList))
    {
        Start = ((Context->LastSubmittedDisplayList -
                  RPI3VC4_HVS_DLIST_PRIVATE_BASE) /
                 RPI3VC4_HVS_DLIST_SLOT_STRIDE + 1) %
                RPI3VC4_HVS_DLIST_SLOT_COUNT;
    }

    for (Offset = 0;
         Offset < RPI3VC4_HVS_DLIST_SLOT_COUNT;
         ++Offset)
    {
        ULONG Candidate;

        Candidate = RPI3VC4_HVS_DLIST_SLOT(
                        (Start + Offset) %
                        RPI3VC4_HVS_DLIST_SLOT_COUNT);
        if (Candidate != Active && Candidate != Requested)
        {
            *Slot = Candidate;
            return TRUE;
        }
    }

    return FALSE;
}

static BOOLEAN
Rpi3Vc4DisplayListBuffer(
    _In_ PRPI3VC4_CONTEXT Context,
    _In_ ULONG Head,
    _Out_ PULONG BufferIndex)
{
    PULONG DisplayList;
    ULONG Control;
    ULONG BusAddress;
    ULONG Index;

    if (!Rpi3Vc4IsPrivateDisplayList(Head))
        return FALSE;

    DisplayList = Rpi3Vc4Register(Context->HvsBase,
                                  RPI3VC4_HVS_DLIST_OFFSET);
    Control = READ_REGISTER_ULONG(&DisplayList[Head]);
    if ((Control & RPI3VC4_HVS_CTL0_VALID) == 0 ||
        ((Control >> RPI3VC4_HVS_CTL0_SIZE_SHIFT) &
         RPI3VC4_HVS_CTL0_SIZE_MASK) != RPI3VC4_HVS_PLANE_DWORDS)
    {
        return FALSE;
    }

    BusAddress = READ_REGISTER_ULONG(&DisplayList[Head + 4]) &
                 RPI3VC4_GPU_ADDRESS_MASK;
    for (Index = 0; Index < RPI3VC4_SCANOUT_BUFFER_COUNT; ++Index)
    {
        if (BusAddress ==
            ((ULONG)Context->ScanoutPhysical[Index].QuadPart &
             RPI3VC4_GPU_ADDRESS_MASK))
        {
            *BufferIndex = Index;
            return TRUE;
        }
    }

    return FALSE;
}

static BOOLEAN
Rpi3Vc4ChooseScanoutBuffer(
    _In_ PRPI3VC4_CONTEXT Context,
    _Out_ PULONG BufferIndex)
{
    ULONG ActiveHead;
    ULONG RequestedHead;
    ULONG ActiveBuffer = MAXULONG;
    ULONG RequestedBuffer = MAXULONG;
    ULONG Offset;

    ActiveHead = READ_REGISTER_ULONG(
                     Rpi3Vc4Register(Context->HvsBase,
                                     RPI3VC4_HVS_DISPLACT1)) &
                 RPI3VC4_HVS_LIST_HEAD_MASK;
    RequestedHead = READ_REGISTER_ULONG(
                        Rpi3Vc4Register(Context->HvsBase,
                                        RPI3VC4_HVS_DISPLIST1)) &
                    RPI3VC4_HVS_LIST_HEAD_MASK;
    (VOID)Rpi3Vc4DisplayListBuffer(Context,
                                   ActiveHead,
                                   &ActiveBuffer);
    (VOID)Rpi3Vc4DisplayListBuffer(Context,
                                   RequestedHead,
                                   &RequestedBuffer);

    for (Offset = 1; Offset <= RPI3VC4_SCANOUT_BUFFER_COUNT; ++Offset)
    {
        ULONG Candidate;

        Candidate = (Context->FrontBufferIndex + Offset) %
                    RPI3VC4_SCANOUT_BUFFER_COUNT;
        if (Candidate != ActiveBuffer && Candidate != RequestedBuffer)
        {
            *BufferIndex = Candidate;
            return TRUE;
        }
    }

    return FALSE;
}

static NTSTATUS
Rpi3Vc4CommitDisplayList(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PRPI3VC4_CONTEXT Context;
    PULONG DisplayList;
    ULONG List[RPI3VC4_HVS_DLIST_SLOT_DWORDS];
    ULONG Count;
    ULONG Slot;
    BOOLEAN CursorIncluded;
    NTSTATUS Status;

    Context = (PRPI3VC4_CONTEXT)Device->PlatformContext;
    if (Context == NULL || !Context->DirectScanoutReady)
        return STATUS_DEVICE_NOT_READY;

    RtlZeroMemory(List, sizeof(List));
    Count = 0;
    CursorIncluded = FALSE;
    if (Context->PrimaryVisible)
    {
        Count += Rpi3Vc4EmitPlane(
                     &List[Count],
                     Rpi3Vc4GpuAddress(Context,
                                       Context->PrimaryPhysical),
                     Context->PrimaryPitch,
                     0,
                     0,
                     Context->PrimaryWidth,
                     Context->PrimaryHeight,
                     FALSE);

        if (Context->Overlay != NULL)
        {
            Status = Rpi3Vc4EmitNv12Overlay(
                         Device,
                         Context,
                         Context->Overlay,
                         List,
                         RTL_NUMBER_OF(List),
                         &Count);
            if (!NT_SUCCESS(Status))
                return Status;
        }

        {
            ULONG CursorX;
            ULONG CursorY;
            ULONG CursorWidth;
            ULONG CursorHeight;
            ULONG CursorBusAddress;

            if (Rpi3Vc4ClipPointer(Device,
                                   Context,
                                   &CursorX,
                                   &CursorY,
                                   &CursorWidth,
                                   &CursorHeight,
                                   &CursorBusAddress))
            {
                Count += Rpi3Vc4EmitPlane(
                             &List[Count],
                             CursorBusAddress,
                             RPI3VC4_CURSOR_PITCH,
                             CursorX,
                             CursorY,
                             CursorWidth,
                             CursorHeight,
                             TRUE);
                CursorIncluded = TRUE;
            }
        }
    }
    List[Count++] = RPI3VC4_HVS_CTL0_END;
    if (Count > RPI3VC4_HVS_DLIST_SLOT_DWORDS)
        return STATUS_BUFFER_OVERFLOW;

    if (!Rpi3Vc4ChooseDisplayList(Context, &Slot))
        return STATUS_DEVICE_BUSY;
    DisplayList = Rpi3Vc4Register(Context->HvsBase,
                                  RPI3VC4_HVS_DLIST_OFFSET);
    for (Count = 0;
         Count < RPI3VC4_HVS_DLIST_SLOT_DWORDS;
         ++Count)
    {
        WRITE_REGISTER_ULONG(&DisplayList[Slot + Count],
                             List[Count]);
    }
#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    KeMemoryBarrier();
    WRITE_REGISTER_ULONG(
        Rpi3Vc4Register(Context->HvsBase,
                        RPI3VC4_HVS_DISPLIST1),
        Slot);
    Context->LastSubmittedDisplayList = Slot;
    Context->PrivateDisplayListActive = TRUE;
    Context->CursorPlaneInstalled = CursorIncluded;
    return STATUS_SUCCESS;
}

static BOOLEAN
Rpi3Vc4UpdateCursorInDisplayList(
    _In_ PRPI3VC4_CONTEXT Context,
    _In_ ULONG Head,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG BusAddress)
{
    PULONG DisplayList;
    PULONG Cursor;
    ULONG Control;
    ULONG Offset;

    if (!Rpi3Vc4IsPrivateDisplayList(Head))
        return FALSE;
    DisplayList = Rpi3Vc4Register(Context->HvsBase,
                                  RPI3VC4_HVS_DLIST_OFFSET);
    Cursor = NULL;
    Offset = 0;
    while (Offset < RPI3VC4_HVS_DLIST_SLOT_DWORDS)
    {
        ULONG Size;

        Control = READ_REGISTER_ULONG(&DisplayList[Head + Offset]);
        if ((Control & RPI3VC4_HVS_CTL0_END) != 0)
            break;
        Size = (Control >> RPI3VC4_HVS_CTL0_SIZE_SHIFT) &
               RPI3VC4_HVS_CTL0_SIZE_MASK;
        if ((Control & RPI3VC4_HVS_CTL0_VALID) == 0 ||
            Size == 0 ||
            Offset > RPI3VC4_HVS_DLIST_SLOT_DWORDS - Size)
        {
            return FALSE;
        }
        if (Offset != 0 &&
            Size == RPI3VC4_HVS_PLANE_DWORDS &&
            (Control & 0xfUL) == RPI3VC4_HVS_FORMAT_RGBA8888)
        {
            Cursor = &DisplayList[Head + Offset];
        }
        Offset += Size;
    }
    if (Cursor == NULL)
        return FALSE;

    WRITE_REGISTER_ULONG(
        &Cursor[1],
        (0xffUL << RPI3VC4_HVS_POS0_ALPHA_SHIFT) |
        ((Y & 0xfffUL) << RPI3VC4_HVS_POS0_Y_SHIFT) |
        (X & 0xfffUL));
    WRITE_REGISTER_ULONG(
        &Cursor[2],
        (RPI3VC4_HVS_POS2_ALPHA_PIPELINE <<
         RPI3VC4_HVS_POS2_ALPHA_SHIFT) |
        ((Height & 0xfffUL) <<
         RPI3VC4_HVS_POS2_HEIGHT_SHIFT) |
        (Width & 0xfffUL));
    WRITE_REGISTER_ULONG(&Cursor[4], BusAddress);
    return TRUE;
}

static BOOLEAN
Rpi3Vc4UpdateCursorPosition(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ PRPI3VC4_CONTEXT Context)
{
    ULONG Active;
    ULONG Requested;
    ULONG X;
    ULONG Y;
    ULONG Width;
    ULONG Height;
    ULONG BusAddress;
    BOOLEAN RequestedUpdated;

    if (!Rpi3Vc4ClipPointer(Device,
                            Context,
                            &X,
                            &Y,
                            &Width,
                            &Height,
                            &BusAddress))
    {
        return FALSE;
    }

    Active = READ_REGISTER_ULONG(
                 Rpi3Vc4Register(Context->HvsBase,
                                 RPI3VC4_HVS_DISPLACT1)) &
             RPI3VC4_HVS_LIST_HEAD_MASK;
    Requested = READ_REGISTER_ULONG(
                    Rpi3Vc4Register(Context->HvsBase,
                                    RPI3VC4_HVS_DISPLIST1)) &
                RPI3VC4_HVS_LIST_HEAD_MASK;
    RequestedUpdated = Rpi3Vc4UpdateCursorInDisplayList(
                           Context,
                           Requested,
                           X,
                           Y,
                           Width,
                           Height,
                           BusAddress);
    if (Active != Requested)
    {
        (VOID)Rpi3Vc4UpdateCursorInDisplayList(Context,
                                               Active,
                                               X,
                                               Y,
                                               Width,
                                               Height,
                                               BusAddress);
    }
#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    KeMemoryBarrier();
    return RequestedUpdated;
}

NTSTATUS
Rpi3Vc4StartScanout(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PRPI3VC4_CONTEXT Context;
    PHYSICAL_ADDRESS LowAddress;
    PHYSICAL_ADDRESS HighAddress;
    PHYSICAL_ADDRESS SkipBytes;
    NTSTATUS Status;

    if (Device == NULL)
        return STATUS_INVALID_PARAMETER;

    Context = (PRPI3VC4_CONTEXT)Device->PlatformContext;
    if (Context == NULL || Context->HvsBase == NULL)
        return STATUS_DEVICE_NOT_READY;

    Context->OriginalDisplayList = READ_REGISTER_ULONG(
        Rpi3Vc4Register(Context->HvsBase,
                        RPI3VC4_HVS_DISPLIST1));
    Context->LastSubmittedDisplayList =
        Context->OriginalDisplayList & RPI3VC4_HVS_LIST_HEAD_MASK;
    if (!Rpi3Vc4DeriveBusAlias(Context, Device->ScanoutPhys))
        return STATUS_NOT_SUPPORTED;
    if (RPI3VC4_HVS_FILTER_OFFSET + RPI3VC4_HVS_FILTER_DWORDS >
            RPI3VC4_HVS_DLIST_DWORDS)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Rpi3Vc4UploadScalingFilter(Context);
#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    KeMemoryBarrier();

    Status = Rpi3Vc4AllocateScanoutBuffers(Device, Context);
    if (!NT_SUCCESS(Status))
        return Status;

    LowAddress.QuadPart = 0;
    HighAddress.QuadPart = RPI3VC4_HIGHEST_SCANOUT_ADDRESS;
    SkipBytes.QuadPart = 0;
    Context->CursorBuffer = MmAllocateContiguousMemorySpecifyCache(
                                RPI3VC4_CURSOR_SIZE,
                                LowAddress,
                                HighAddress,
                                SkipBytes,
                                MmWriteCombined);
    if (Context->CursorBuffer == NULL)
    {
        Rpi3Vc4ReleaseScanoutBuffers(Context);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Context->CursorBuffer, RPI3VC4_CURSOR_SIZE);
    Context->CursorPhysical =
        MmGetPhysicalAddress(Context->CursorBuffer);
    Context->DirectScanoutReady = TRUE;
    Device->PlatformDirectScanout = TRUE;
    Device->PlatformHardwarePointer = TRUE;
    InterlockedExchange(&Device->ScanoutVBlankAvailable, 1);

    return STATUS_SUCCESS;
}

static BOOLEAN
Rpi3Vc4WaitForDisplayList(
    _In_ PRPI3VC4_CONTEXT Context,
    _In_ ULONG Head)
{
    ULONG Iteration;

    Head &= RPI3VC4_HVS_LIST_HEAD_MASK;
    for (Iteration = 0; Iteration < 5000; ++Iteration)
    {
        ULONG Active;
        ULONG Status;

        Active = READ_REGISTER_ULONG(
                     Rpi3Vc4Register(Context->HvsBase,
                                     RPI3VC4_HVS_DISPLACT1)) &
                 RPI3VC4_HVS_LIST_HEAD_MASK;
        if (Active == Head)
            return TRUE;

        Status = READ_REGISTER_ULONG(
                     Rpi3Vc4Register(Context->HvsBase,
                                     RPI3VC4_HVS_DISPSTAT1));
        if ((Status & RPI3VC4_HVS_STATUS_MODE_MASK) !=
                RPI3VC4_HVS_STATUS_MODE_RUN)
        {
            return TRUE;
        }
        KeStallExecutionProcessor(10);
    }

    return FALSE;
}

static BOOLEAN
Rpi3Vc4QuiesceChannel(
    _In_ PRPI3VC4_CONTEXT Context)
{
    PULONG Control;
    ULONG Iteration;

    Control = Rpi3Vc4Register(Context->HvsBase,
                              RPI3VC4_HVS_DISPCTRL1);
    WRITE_REGISTER_ULONG(Control, RPI3VC4_HVS_CHANNEL_RESET);
    WRITE_REGISTER_ULONG(Control, 0);

    for (Iteration = 0; Iteration < 5000; ++Iteration)
    {
        ULONG ChannelControl;
        ULONG ChannelStatus;

        ChannelControl = READ_REGISTER_ULONG(Control);
        ChannelStatus = READ_REGISTER_ULONG(
                            Rpi3Vc4Register(
                                Context->HvsBase,
                                RPI3VC4_HVS_DISPSTAT1));
        if ((ChannelControl &
             (RPI3VC4_HVS_CHANNEL_ENABLE |
              RPI3VC4_HVS_CHANNEL_RESET)) == 0 &&
            (ChannelStatus & RPI3VC4_HVS_STATUS_MODE_MASK) ==
                RPI3VC4_HVS_STATUS_MODE_DISABLED &&
            (ChannelStatus & RPI3VC4_HVS_STATUS_EMPTY) != 0)
        {
            return TRUE;
        }
        KeStallExecutionProcessor(10);
    }

    return FALSE;
}

NTSTATUS
Rpi3Vc4StopScanout(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PRPI3VC4_CONTEXT Context;

    if (Device == NULL)
        return STATUS_INVALID_PARAMETER;

    Context = (PRPI3VC4_CONTEXT)Device->PlatformContext;
    if (Context == NULL)
        return STATUS_SUCCESS;

    Device->PlatformDirectScanout = FALSE;
    Device->PlatformHardwarePointer = FALSE;
    InterlockedExchange(&Device->ScanoutVBlankAvailable, 0);
    if (Context->PrivateDisplayListActive && Context->HvsBase != NULL)
    {
        WRITE_REGISTER_ULONG(
            Rpi3Vc4Register(Context->HvsBase,
                            RPI3VC4_HVS_DISPLIST1),
            Context->OriginalDisplayList);
#if defined(_M_ARM64)
        __dsb(_ARM64_BARRIER_SY);
#endif
        KeMemoryBarrier();
        if (!Rpi3Vc4WaitForDisplayList(
                 Context,
                 Context->OriginalDisplayList))
        {
            if (!Rpi3Vc4QuiesceChannel(Context))
                return STATUS_DEVICE_BUSY;
        }
    }

    Context->DirectScanoutReady = FALSE;
    if (Context->Overlay != NULL)
    {
        Context->Overlay->Magic = 0;
        ExFreePoolWithTag(Context->Overlay,
                          RPI3VC4_OVERLAY_POOL_TAG);
        Context->Overlay = NULL;
    }

    if (Context->CursorBuffer != NULL)
    {
        MmFreeContiguousMemorySpecifyCache(Context->CursorBuffer,
                                           RPI3VC4_CURSOR_SIZE,
                                           MmWriteCombined);
    }
    Rpi3Vc4ReleaseScanoutBuffers(Context);
    Rpi3Vc4StopV3d(Context);
    if (Context->V3dBase != NULL)
        MmUnmapIoSpace(Context->V3dBase, RPI3VC4_V3D_LENGTH);
    if (Context->Pv2Base != NULL)
        MmUnmapIoSpace(Context->Pv2Base, RPI3VC4_PV2_LENGTH);
    if (Context->HvsBase != NULL)
        MmUnmapIoSpace(Context->HvsBase, RPI3VC4_HVS_LENGTH);
    Device->PlatformContext = NULL;
    ExFreePoolWithTag(Context, RPI3VC4_POOL_TAG);
    return STATUS_SUCCESS;
}

NTSTATUS
Rpi3Vc4SetPrimary(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ PHYSICAL_ADDRESS PrimaryAddress,
    _In_ ULONG Pitch,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ BOOLEAN Visible)
{
    PRPI3VC4_CONTEXT Context;
    ULONGLONG Span;
    NTSTATUS Status;

    if (Device == NULL)
        return STATUS_INVALID_PARAMETER;
    Context = (PRPI3VC4_CONTEXT)Device->PlatformContext;
    if (Context == NULL || !Context->DirectScanoutReady)
        return STATUS_DEVICE_NOT_READY;

    if (Visible)
    {
        if (PrimaryAddress.QuadPart < 0 ||
            (ULONGLONG)PrimaryAddress.QuadPart >
                RPI3VC4_HIGHEST_SCANOUT_ADDRESS ||
            Width == 0 || Height == 0 ||
            Width > 0xfffUL || Height > 0xfffUL ||
            Pitch < Width * sizeof(ULONG) ||
            Pitch > 0xffffUL ||
            Height > (~(ULONGLONG)0) / Pitch)
        {
            return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
        }

        Span = (ULONGLONG)Pitch * Height;
        if (Span == 0 ||
            Span - 1 > RPI3VC4_HIGHEST_SCANOUT_ADDRESS -
                (ULONGLONG)PrimaryAddress.QuadPart)
        {
            return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
        }

    }

    Status = KeWaitForSingleObject(&Device->PointerMutex,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!Visible)
    {
        Context->PrimaryConfigured = PrimaryAddress.QuadPart != 0;
        if (!Context->PrimaryVisible)
        {
            Status = STATUS_SUCCESS;
            goto Cleanup;
        }
        Context->PrimaryVisible = FALSE;
    }
    else
    {
        if (Context->PrimaryVisible &&
            Context->SourcePrimaryPhysical.QuadPart ==
                PrimaryAddress.QuadPart &&
            Context->PrimaryWidth == Width &&
            Context->PrimaryHeight == Height)
        {
            Status = STATUS_SUCCESS;
            goto Cleanup;
        }

        Context->SourcePrimaryPhysical = PrimaryAddress;
        Context->PrimaryPhysical =
            Context->ScanoutPhysical[Context->FrontBufferIndex];
        Context->PrimaryPitch = Device->ScanoutPitch;
        Context->PrimaryWidth = Width;
        Context->PrimaryHeight = Height;
        Context->PrimaryConfigured = TRUE;
        Context->PrimaryVisible = TRUE;
    }

    Status = Rpi3Vc4CommitDisplayList(Device);
Cleanup:
    KeReleaseMutex(&Device->PointerMutex, FALSE);
    return Status;
}

static VOID
Rpi3Vc4AccumulatePresentRect(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const RECT *Input,
    _Inout_ RECT *Damage,
    _Inout_ PBOOLEAN DamageValid)
{
    RECT Rect;

    if (!Rpi3Vc4ClipDamageRect(Device, Input, &Rect))
        return;

    Rpi3Vc4UnionDamageRect(Damage, DamageValid, &Rect);
}

NTSTATUS
Rpi3Vc4PresentDisplayOnly(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const DXGKARG_PRESENT_DISPLAYONLY *PresentDisplayOnly)
{
    PRPI3VC4_CONTEXT Context;
    PHYSICAL_ADDRESS PreviousPrimary;
    SIZE_T SourceSize;
    RECT IncomingDamage;
    RECT CopyDamage;
    BOOLEAN IncomingDamageValid;
    BOOLEAN CopyDamageValid;
    ULONG Back;
    ULONG Index;
    NTSTATUS Status;

    if (Device == NULL || PresentDisplayOnly == NULL ||
        PresentDisplayOnly->pSource == NULL ||
        PresentDisplayOnly->VidPnSourceId != 0 ||
        PresentDisplayOnly->BytesPerPixel != sizeof(ULONG) ||
        PresentDisplayOnly->Pitch <= 0 ||
        PresentDisplayOnly->Flags.Value != 0 ||
        (PresentDisplayOnly->NumMoves != 0 &&
         PresentDisplayOnly->pMoves == NULL) ||
        (PresentDisplayOnly->NumDirtyRects != 0 &&
         PresentDisplayOnly->pDirtyRect == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Context = (PRPI3VC4_CONTEXT)Device->PlatformContext;
    if (Context == NULL || !Context->DirectScanoutReady ||
        Context->ScanoutBuffers == NULL)
    {
        return STATUS_DEVICE_NOT_READY;
    }
    if ((ULONG)PresentDisplayOnly->Pitch <
            Device->Width * sizeof(ULONG) ||
        Device->Height >
            MAXULONG_PTR / (ULONG)PresentDisplayOnly->Pitch)
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }
    if (Context->PrimaryConfigured && !Context->PrimaryVisible)
        return STATUS_SUCCESS;
    if (PresentDisplayOnly->NumMoves == 0 &&
        PresentDisplayOnly->NumDirtyRects == 0)
        return STATUS_SUCCESS;

    if (!Context->PrimaryConfigured)
    {
        Context->PrimaryPhysical =
            Context->ScanoutPhysical[Context->FrontBufferIndex];
        Context->PrimaryPitch = Device->ScanoutPitch;
        Context->PrimaryWidth = Device->Width;
        Context->PrimaryHeight = Device->Height;
        Context->PrimaryConfigured = TRUE;
        Context->PrimaryVisible = TRUE;
    }

    SourceSize = (SIZE_T)(ULONG)PresentDisplayOnly->Pitch *
                 Device->Height;
    IncomingDamageValid = FALSE;
    RtlZeroMemory(&IncomingDamage, sizeof(IncomingDamage));
    if (!Rpi3Vc4ChooseScanoutBuffer(Context, &Back))
        return STATUS_DEVICE_BUSY;

    CopyDamageValid = FALSE;
    RtlZeroMemory(&CopyDamage, sizeof(CopyDamage));
    if (Context->FullDamage[Back])
    {
        CopyDamage.left = 0;
        CopyDamage.top = 0;
        CopyDamage.right = Device->Width;
        CopyDamage.bottom = Device->Height;
        CopyDamageValid = TRUE;
    }
    else if (Context->PendingDamageValid[Back])
    {
        CopyDamage = Context->PendingDamage[Back];
        CopyDamageValid = TRUE;
    }
    for (Index = 0; Index < PresentDisplayOnly->NumMoves; ++Index)
    {
        Rpi3Vc4AccumulatePresentRect(
            Device,
            &PresentDisplayOnly->pMoves[Index].DestRect,
            &IncomingDamage,
            &IncomingDamageValid);
    }
    for (Index = 0; Index < PresentDisplayOnly->NumDirtyRects; ++Index)
    {
        Rpi3Vc4AccumulatePresentRect(
            Device,
            &PresentDisplayOnly->pDirtyRect[Index],
            &IncomingDamage,
            &IncomingDamageValid);
    }
    if (!IncomingDamageValid)
        return STATUS_SUCCESS;

    /*
     * ReactOS gives display-only miniports the complete shared primary in
     * pSource. Merge this frame's damage with the damage missed by the chosen
     * scanout buffer and upload the final pixels once. This preserves an
     * immutable active HVS buffer without maintaining and copying through a
     * second full-frame shadow inside the miniport.
     */
    Rpi3Vc4UnionDamageRect(&CopyDamage,
                           &CopyDamageValid,
                           &IncomingDamage);
    Status = Rpi3Vc4CopyDamageRect(
                 PresentDisplayOnly->pSource,
                 SourceSize,
                 (ULONG)PresentDisplayOnly->Pitch,
                 &CopyDamage,
                 (PUCHAR)Context->ScanoutBuffers +
                     Back * Context->ScanoutBufferStride,
                 Context->ScanoutSurfaceSize,
                 Context->PrimaryPitch);
    if (!NT_SUCCESS(Status))
        return Status;

#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    KeMemoryBarrier();
    Status = KeWaitForSingleObject(&Device->PointerMutex,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    PreviousPrimary = Context->PrimaryPhysical;
    Context->PrimaryPhysical = Context->ScanoutPhysical[Back];
    Status = Rpi3Vc4CommitDisplayList(Device);
    if (!NT_SUCCESS(Status))
        Context->PrimaryPhysical = PreviousPrimary;
    KeReleaseMutex(&Device->PointerMutex, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;

    Context->FrontBufferIndex = Back;
    Context->FullDamage[Back] = FALSE;
    Context->PendingDamageValid[Back] = FALSE;
    for (Index = 0; Index < RPI3VC4_SCANOUT_BUFFER_COUNT; ++Index)
    {
        if (Index != Back && !Context->FullDamage[Index])
        {
            Rpi3Vc4UnionDamageRect(
                &Context->PendingDamage[Index],
                &Context->PendingDamageValid[Index],
                &IncomingDamage);
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
Rpi3Vc4CommitOverlayLocked(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PRPI3VC4_CONTEXT Context;
    NTSTATUS Status;

    Context = (PRPI3VC4_CONTEXT)Device->PlatformContext;
    Status = Rpi3Vc4CommitDisplayList(Device);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!Rpi3Vc4WaitForDisplayList(Context,
                                   Context->LastSubmittedDisplayList))
    {
        return STATUS_DEVICE_BUSY;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
SoftGpuPlatformCreateOverlay(
    _In_ HANDLE AdapterContext,
    _Inout_ PDXGKARG_CREATEOVERLAY CreateOverlay)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)AdapterContext;
    PRPI3VC4_CONTEXT Context;
    PRPI3VC4_OVERLAY Overlay;
    RPI3VC4_OVERLAY State;
    NTSTATUS RollbackStatus;
    NTSTATUS Status;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        CreateOverlay == NULL || CreateOverlay->VidPnSourceId != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    CreateOverlay->hOverlay = NULL;
    Context = (PRPI3VC4_CONTEXT)Device->PlatformContext;
    if (Context == NULL || !Context->DirectScanoutReady)
        return STATUS_DEVICE_NOT_READY;

    Status = Rpi3Vc4ReadOverlayInfo(Device,
                                    &CreateOverlay->OverlayInfo,
                                    &State);
    if (!NT_SUCCESS(Status))
        return Status;
    Overlay = ExAllocatePoolWithTag(NonPagedPool,
                                    sizeof(*Overlay),
                                    RPI3VC4_OVERLAY_POOL_TAG);
    if (Overlay == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    *Overlay = State;
    Overlay->Magic = RPI3VC4_OVERLAY_MAGIC;
    Overlay->Context = Context;

    Status = KeWaitForSingleObject(&Device->PointerMutex,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
        goto Failure;
    if (Context->Overlay != NULL)
    {
        Status = STATUS_GRAPHICS_TOO_MANY_REFERENCES;
        goto Unlock;
    }
    Context->Overlay = Overlay;
    Status = Rpi3Vc4CommitOverlayLocked(Device);
    if (!NT_SUCCESS(Status))
    {
        Context->Overlay = NULL;
        RollbackStatus = Rpi3Vc4CommitOverlayLocked(Device);
        if (!NT_SUCCESS(RollbackStatus))
            Status = RollbackStatus;
        goto Unlock;
    }
    CreateOverlay->hOverlay = (HANDLE)Overlay;

Unlock:
    KeReleaseMutex(&Device->PointerMutex, FALSE);
Failure:
    if (!NT_SUCCESS(Status))
    {
        Overlay->Magic = 0;
        ExFreePoolWithTag(Overlay, RPI3VC4_OVERLAY_POOL_TAG);
    }
    return Status;
}

NTSTATUS
APIENTRY
SoftGpuPlatformUpdateOverlay(
    _In_ HANDLE OverlayContext,
    _In_ const DXGKARG_UPDATEOVERLAY *UpdateOverlay)
{
    PRPI3VC4_OVERLAY Overlay = (PRPI3VC4_OVERLAY)OverlayContext;
    PRPI3VC4_CONTEXT Context;
    PSOFTGPU_DEVICE Device;
    RPI3VC4_OVERLAY Candidate;
    RPI3VC4_OVERLAY Previous;
    NTSTATUS RollbackStatus;
    NTSTATUS Status;

    if (Overlay == NULL ||
        Overlay->Magic != RPI3VC4_OVERLAY_MAGIC ||
        Overlay->Context == NULL || UpdateOverlay == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Context = Overlay->Context;
    Device = Context->Device;
    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        !Context->DirectScanoutReady)
    {
        return STATUS_DEVICE_NOT_READY;
    }
    Status = Rpi3Vc4ReadOverlayInfo(Device,
                                    &UpdateOverlay->OverlayInfo,
                                    &Candidate);
    if (!NT_SUCCESS(Status))
        return Status;
    Candidate.Magic = RPI3VC4_OVERLAY_MAGIC;
    Candidate.Context = Context;

    Status = KeWaitForSingleObject(&Device->PointerMutex,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Context->Overlay != Overlay)
    {
        Status = STATUS_INVALID_HANDLE;
        goto Unlock;
    }
    Previous = *Overlay;
    *Overlay = Candidate;
    Status = Rpi3Vc4CommitOverlayLocked(Device);
    if (!NT_SUCCESS(Status))
    {
        *Overlay = Previous;
        RollbackStatus = Rpi3Vc4CommitOverlayLocked(Device);
        if (!NT_SUCCESS(RollbackStatus))
            Status = RollbackStatus;
    }

Unlock:
    KeReleaseMutex(&Device->PointerMutex, FALSE);
    return Status;
}

NTSTATUS
APIENTRY
SoftGpuPlatformFlipOverlay(
    _In_ HANDLE OverlayContext,
    _In_ const DXGKARG_FLIPOVERLAY *FlipOverlay)
{
    PRPI3VC4_OVERLAY Overlay = (PRPI3VC4_OVERLAY)OverlayContext;
    DXGK_OVERLAYINFO Info;

    if (Overlay == NULL ||
        Overlay->Magic != RPI3VC4_OVERLAY_MAGIC ||
        FlipOverlay == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(&Info, sizeof(Info));
    Info.hAllocation = FlipOverlay->hSource;
    Info.PhysicalAddress = FlipOverlay->SrcPhysicalAddress;
    Info.SegmentId = FlipOverlay->SrcSegmentId;
    Info.SrcRect = Overlay->SourceRect;
    Info.DstRect = Overlay->DestinationRect;
    Info.pPrivateDriverData = FlipOverlay->pPrivateDriverData;
    Info.PrivateDriverDataSize = FlipOverlay->PrivateDriverDataSize;
    {
        DXGKARG_UPDATEOVERLAY Update;

        RtlZeroMemory(&Update, sizeof(Update));
        Update.OverlayInfo = Info;
        return SoftGpuPlatformUpdateOverlay(OverlayContext, &Update);
    }
}

NTSTATUS
APIENTRY
SoftGpuPlatformDestroyOverlay(
    _In_ HANDLE OverlayContext)
{
    PRPI3VC4_OVERLAY Overlay = (PRPI3VC4_OVERLAY)OverlayContext;
    PRPI3VC4_CONTEXT Context;
    PSOFTGPU_DEVICE Device;
    NTSTATUS RollbackStatus;
    NTSTATUS Status;

    if (Overlay == NULL ||
        Overlay->Magic != RPI3VC4_OVERLAY_MAGIC ||
        Overlay->Context == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Context = Overlay->Context;
    Device = Context->Device;
    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC)
        return STATUS_DEVICE_NOT_READY;

    Status = KeWaitForSingleObject(&Device->PointerMutex,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Context->Overlay != Overlay)
    {
        Status = STATUS_INVALID_HANDLE;
        goto Unlock;
    }
    Context->Overlay = NULL;
    Status = Rpi3Vc4CommitOverlayLocked(Device);
    if (!NT_SUCCESS(Status))
    {
        Context->Overlay = Overlay;
        RollbackStatus = Rpi3Vc4CommitOverlayLocked(Device);
        if (!NT_SUCCESS(RollbackStatus))
            Status = RollbackStatus;
        goto Unlock;
    }
    Overlay->Magic = 0;

Unlock:
    KeReleaseMutex(&Device->PointerMutex, FALSE);
    if (NT_SUCCESS(Status))
        ExFreePoolWithTag(Overlay, RPI3VC4_OVERLAY_POOL_TAG);
    return Status;
}

NTSTATUS
Rpi3Vc4UpdatePointer(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PRPI3VC4_CONTEXT Context;

    if (Device == NULL)
        return STATUS_INVALID_PARAMETER;
    Context = (PRPI3VC4_CONTEXT)Device->PlatformContext;
    if (Context == NULL || !Context->DirectScanoutReady ||
        Context->CursorBuffer == NULL)
    {
        return STATUS_DEVICE_NOT_READY;
    }

    if (Device->PointerShapeValid &&
        Context->CursorShapeGeneration !=
            Device->PointerShapeGeneration)
    {
        RtlCopyMemory(Context->CursorBuffer,
                      Device->PointerPixels,
                      RPI3VC4_CURSOR_SIZE);
#if defined(_M_ARM64)
        __dsb(_ARM64_BARRIER_SY);
#endif
        KeMemoryBarrier();
        Context->CursorShapeGeneration =
            Device->PointerShapeGeneration;
    }

    if (!Context->PrivateDisplayListActive)
        return STATUS_SUCCESS;
    if (Context->CursorPlaneInstalled &&
        Rpi3Vc4UpdateCursorPosition(Device, Context))
    {
        return STATUS_SUCCESS;
    }
    if (!Context->CursorPlaneInstalled &&
        !Device->PointerVisible)
    {
        return STATUS_SUCCESS;
    }
    return Rpi3Vc4CommitDisplayList(Device);
}

BOOLEAN
Rpi3Vc4WaitForVerticalBlank(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PRPI3VC4_CONTEXT Context;
    ULONG PreviousLine;
    ULONG Iteration;

    if (Device == NULL)
        return FALSE;
    Context = (PRPI3VC4_CONTEXT)Device->PlatformContext;
    if (Context == NULL || !Context->DirectScanoutReady)
        return FALSE;

    PreviousLine = READ_REGISTER_ULONG(
                       Rpi3Vc4Register(Context->HvsBase,
                                       RPI3VC4_HVS_DISPSTAT1)) &
                   RPI3VC4_HVS_STATUS_LINE_MASK;
    for (Iteration = 0; Iteration < 2000; ++Iteration)
    {
        ULONG Line;

        KeStallExecutionProcessor(10);
        Line = READ_REGISTER_ULONG(
                   Rpi3Vc4Register(Context->HvsBase,
                                   RPI3VC4_HVS_DISPSTAT1)) &
               RPI3VC4_HVS_STATUS_LINE_MASK;
        if (Line < PreviousLine)
            return TRUE;
        PreviousLine = Line;
    }

    return FALSE;
}

NTSTATUS
Rpi3Vc4QueryScanLine(
    _In_ PSOFTGPU_DEVICE Device,
    _Inout_ PDXGKARG_GETSCANLINE GetScanLine)
{
    PRPI3VC4_CONTEXT Context;
    ULONG Line;

    if (Device == NULL || GetScanLine == NULL ||
        GetScanLine->VidPnTargetId != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Context = (PRPI3VC4_CONTEXT)Device->PlatformContext;
    if (Context == NULL || !Context->DirectScanoutReady)
        return STATUS_DEVICE_NOT_READY;

    Line = READ_REGISTER_ULONG(
               Rpi3Vc4Register(Context->HvsBase,
                               RPI3VC4_HVS_DISPSTAT1)) &
           RPI3VC4_HVS_STATUS_LINE_MASK;
    GetScanLine->InVerticalBlank = Line >= Device->Height;
    GetScanLine->ScanLine = GetScanLine->InVerticalBlank ?
                                0 : Line;
    return STATUS_SUCCESS;
}
