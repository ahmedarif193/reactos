/*
 * PROJECT:     ReactOS Raspberry Pi 3 VC4 WDDM miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     BCM2837 HVS4 scanout and cursor-plane programming
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "rpi3vc4.h"
#include "softgpu_2d_core.h"

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
    if (Context->PresentShadow != NULL)
    {
        ExFreePoolWithTag(Context->PresentShadow, RPI3VC4_POOL_TAG);
        Context->PresentShadow = NULL;
    }
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
    Context->PresentShadowSize = 0;
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
    Context->PresentShadow = ExAllocatePoolWithTag(NonPagedPool,
                                                   SurfaceSize,
                                                   RPI3VC4_POOL_TAG);
    if (Context->PresentShadow == NULL)
    {
        Rpi3Vc4ReleaseScanoutBuffers(Context);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Context->PresentShadowSize = SurfaceSize;
    Context->FrontBufferIndex = 0;
    RtlZeroMemory(Context->ScanoutBuffers, AllocationSize);
    RtlZeroMemory(Context->PresentShadow, SurfaceSize);
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

    if (!Rpi3Vc4IsPrivateDisplayList(Head))
        return FALSE;
    DisplayList = Rpi3Vc4Register(Context->HvsBase,
                                  RPI3VC4_HVS_DLIST_OFFSET);
    Cursor = &DisplayList[Head + RPI3VC4_HVS_CURSOR_PLANE_OFFSET];
    Control = READ_REGISTER_ULONG(&Cursor[0]);
    if ((Control & RPI3VC4_HVS_CTL0_VALID) == 0 ||
        ((Control >> RPI3VC4_HVS_CTL0_SIZE_SHIFT) &
         RPI3VC4_HVS_CTL0_SIZE_MASK) !=
            RPI3VC4_HVS_PLANE_DWORDS)
    {
        return FALSE;
    }

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

    if (Context->CursorBuffer != NULL)
    {
        MmFreeContiguousMemorySpecifyCache(Context->CursorBuffer,
                                           RPI3VC4_CURSOR_SIZE,
                                           MmWriteCombined);
    }
    Rpi3Vc4ReleaseScanoutBuffers(Context);
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

    if (!Visible)
    {
        Context->PrimaryConfigured = PrimaryAddress.QuadPart != 0;
        if (!Context->PrimaryVisible)
            return STATUS_SUCCESS;
        Context->PrimaryVisible = FALSE;
    }
    else
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

        if (Context->PrimaryVisible &&
            Context->SourcePrimaryPhysical.QuadPart ==
                PrimaryAddress.QuadPart &&
            Context->PrimaryWidth == Width &&
            Context->PrimaryHeight == Height)
        {
            return STATUS_SUCCESS;
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
    return Status;
}

static NTSTATUS
Rpi3Vc4ApplyPresentRect(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ PVOID Source,
    _In_ SIZE_T SourceSize,
    _In_ ULONG SourcePitch,
    _In_ const RECT *Input,
    _Inout_ PRPI3VC4_CONTEXT Context,
    _In_ ULONG BackBufferIndex,
    _Inout_ RECT *IncomingDamage,
    _Inout_ PBOOLEAN IncomingDamageValid)
{
    RECT Rect;
    NTSTATUS Status;

    if (!Rpi3Vc4ClipDamageRect(Device, Input, &Rect))
        return STATUS_SUCCESS;

    Status = Rpi3Vc4CopyDamageRect(Source,
                                   SourceSize,
                                   SourcePitch,
                                   &Rect,
                                   Context->PresentShadow,
                                   Context->PresentShadowSize,
                                   Context->PrimaryPitch);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = Rpi3Vc4CopyDamageRect(
                 Source,
                 SourceSize,
                 SourcePitch,
                 &Rect,
                 (PUCHAR)Context->ScanoutBuffers +
                     BackBufferIndex * Context->ScanoutBufferStride,
                 Context->ScanoutSurfaceSize,
                 Context->PrimaryPitch);
    if (!NT_SUCCESS(Status))
        return Status;

    Rpi3Vc4UnionDamageRect(IncomingDamage,
                           IncomingDamageValid,
                           &Rect);
    return STATUS_SUCCESS;
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
    if (CopyDamageValid)
    {
        Status = Rpi3Vc4CopyDamageRect(
                     Context->PresentShadow,
                     Context->PresentShadowSize,
                     Context->PrimaryPitch,
                     &CopyDamage,
                     (PUCHAR)Context->ScanoutBuffers +
                         Back * Context->ScanoutBufferStride,
                     Context->ScanoutSurfaceSize,
                     Context->PrimaryPitch);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    for (Index = 0; Index < PresentDisplayOnly->NumMoves; ++Index)
    {
        Status = Rpi3Vc4ApplyPresentRect(
                     Device,
                     PresentDisplayOnly->pSource,
                     SourceSize,
                     (ULONG)PresentDisplayOnly->Pitch,
                     &PresentDisplayOnly->pMoves[Index].DestRect,
                     Context,
                     Back,
                     &IncomingDamage,
                     &IncomingDamageValid);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    for (Index = 0; Index < PresentDisplayOnly->NumDirtyRects; ++Index)
    {
        Status = Rpi3Vc4ApplyPresentRect(
                     Device,
                     PresentDisplayOnly->pSource,
                     SourceSize,
                     (ULONG)PresentDisplayOnly->Pitch,
                     &PresentDisplayOnly->pDirtyRect[Index],
                     Context,
                     Back,
                     &IncomingDamage,
                     &IncomingDamageValid);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    if (!IncomingDamageValid)
        return STATUS_SUCCESS;

#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    KeMemoryBarrier();
    PreviousPrimary = Context->PrimaryPhysical;
    Context->PrimaryPhysical = Context->ScanoutPhysical[Back];
    Status = Rpi3Vc4CommitDisplayList(Device);
    if (!NT_SUCCESS(Status))
    {
        Context->PrimaryPhysical = PreviousPrimary;
        return Status;
    }

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
        GetScanLine->VidPnSourceId != 0)
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
