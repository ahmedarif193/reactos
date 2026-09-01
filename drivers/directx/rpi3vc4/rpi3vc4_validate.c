/*
 * PROJECT:     ReactOS Raspberry Pi 3 VC4 WDDM miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     VC4 command-list capture, validation, and relocation adapter
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "vc4_drv.h"

#define RPI3VC4_VALIDATOR_TAG 'v3VR'
#define RPI3VC4_VALIDATOR_HEADER_MAGIC 0x56414c33UL

typedef struct _RPI3VC4_VALIDATOR_ALLOCATION
{
    ULONG Magic;
    SIZE_T Size;
} RPI3VC4_VALIDATOR_ALLOCATION, *PRPI3VC4_VALIDATOR_ALLOCATION;

typedef struct vc4_validation_context
{
    PRPI3VC4_CONTEXT Platform;
    PUCHAR Arena;
    PHYSICAL_ADDRESS ArenaPhysical;
    SIZE_T ArenaSize;
    SIZE_T ArenaCursor;
    struct drm_vc4_bo *Wrappers;
    ULONG WrapperCapacity;
    ULONG WrapperCount;
} RPI3VC4_VALIDATION_CONTEXT, *PRPI3VC4_VALIDATION_CONTEXT;

PVOID
Rpi3Vc4ValidatorAllocate(
    _In_ SIZE_T Size,
    _In_ BOOLEAN Zero)
{
    PRPI3VC4_VALIDATOR_ALLOCATION Header;

    if (Size == 0 || Size > MAXULONG_PTR - sizeof(*Header))
        return NULL;
    Header = ExAllocatePoolWithTag(NonPagedPool,
                                   sizeof(*Header) + Size,
                                   RPI3VC4_VALIDATOR_TAG);
    if (Header == NULL)
        return NULL;
    Header->Magic = RPI3VC4_VALIDATOR_HEADER_MAGIC;
    Header->Size = Size;
    if (Zero)
        RtlZeroMemory(Header + 1, Size);
    return Header + 1;
}

PVOID
Rpi3Vc4ValidatorCallocate(
    _In_ SIZE_T Count,
    _In_ SIZE_T Size)
{
    if (Count == 0 || Size == 0 || Count > MAXULONG_PTR / Size)
        return NULL;

    return Rpi3Vc4ValidatorAllocate(Count * Size, TRUE);
}

PVOID
Rpi3Vc4ValidatorReallocate(
    _In_opt_ PVOID Buffer,
    _In_ SIZE_T Size)
{
    PRPI3VC4_VALIDATOR_ALLOCATION Header;
    PVOID Replacement;

    if (Buffer == NULL)
        return Rpi3Vc4ValidatorAllocate(Size, FALSE);
    if (Size == 0)
    {
        Rpi3Vc4ValidatorFree(Buffer);
        return NULL;
    }
    Header = (PRPI3VC4_VALIDATOR_ALLOCATION)Buffer - 1;
    if (Header->Magic != RPI3VC4_VALIDATOR_HEADER_MAGIC)
        return NULL;
    Replacement = Rpi3Vc4ValidatorAllocate(Size, FALSE);
    if (Replacement == NULL)
        return NULL;
    RtlCopyMemory(Replacement, Buffer, min(Header->Size, Size));
    Rpi3Vc4ValidatorFree(Buffer);
    return Replacement;
}

VOID
Rpi3Vc4ValidatorFree(
    _In_opt_ PVOID Buffer)
{
    PRPI3VC4_VALIDATOR_ALLOCATION Header;

    if (Buffer == NULL)
        return;
    Header = (PRPI3VC4_VALIDATOR_ALLOCATION)Buffer - 1;
    ASSERT(Header->Magic == RPI3VC4_VALIDATOR_HEADER_MAGIC);
    Header->Magic = 0;
    ExFreePoolWithTag(Header, RPI3VC4_VALIDATOR_TAG);
}

static BOOLEAN
Rpi3Vc4CheckedRange(
    _In_ ULONG Total,
    _In_ ULONG Offset,
    _In_ ULONG Size)
{
    return Offset <= Total && Size <= Total - Offset;
}

static ULONG
Rpi3Vc4BusAddress(
    _In_ PRPI3VC4_CONTEXT Platform,
    _In_ PHYSICAL_ADDRESS Physical)
{
    return Platform->BusAlias |
           ((ULONG)Physical.QuadPart & RPI3VC4_GPU_ADDRESS_MASK);
}

struct drm_gem_cma_object *
drm_gem_cma_create(
    _In_ struct drm_device *Device,
    _In_ size_t Size)
{
    PRPI3VC4_VALIDATION_CONTEXT Validation;
    struct drm_vc4_bo *Wrapper;
    SIZE_T AlignmentBias;
    SIZE_T Start;
    PHYSICAL_ADDRESS Physical;

    if (Device == NULL || Device->Validation == NULL || Size == 0)
        return NULL;
    Validation = Device->Validation;
    /*
     * drm_gem_cma_create() returns a page-aligned DMA object.  The imported
     * VC4 validator relies on that contract for the tile-state/allocation BO,
     * whose allocation pool follows a page-aligned tile-state region.  Keep
     * each object page aligned when carving it out of the WDDM DMA buffer.
     */
    AlignmentBias = (SIZE_T)(Validation->ArenaPhysical.QuadPart &
                             (PAGE_SIZE - 1));
    if (Validation->ArenaCursor > MAXULONG_PTR - AlignmentBias)
        return NULL;
    Start = ALIGN_UP_BY(Validation->ArenaCursor + AlignmentBias, PAGE_SIZE) -
            AlignmentBias;
    if (Start > Validation->ArenaSize || Size > Validation->ArenaSize - Start ||
        Validation->WrapperCount >= Validation->WrapperCapacity)
    {
        return NULL;
    }

    Wrapper = &Validation->Wrappers[Validation->WrapperCount++];
    RtlZeroMemory(Wrapper, sizeof(*Wrapper));
    INIT_LIST_HEAD(&Wrapper->unref_head);
    Wrapper->base.base.dev = Device;
    Wrapper->base.base.size = Size;
    Wrapper->base.vaddr = Validation->Arena + Start;
    Physical.QuadPart = Validation->ArenaPhysical.QuadPart + Start;
    Wrapper->base.paddr = Rpi3Vc4BusAddress(Validation->Platform, Physical);
    RtlZeroMemory(Wrapper->base.vaddr, Size);
    Validation->ArenaCursor = Start + Size;
    return &Wrapper->base;
}

static VOID
Rpi3Vc4FreeValidatedShader(
    _Inout_ struct drm_vc4_bo *Wrapper)
{
    struct vc4_validated_shader_info *Shader = Wrapper->validated_shader;

    if (Shader == NULL)
        return;
    kfree(Shader->texture_samples);
    kfree(Shader->uniform_addr_offsets);
    kfree(Shader);
    Wrapper->validated_shader = NULL;
}

static VOID
Rpi3Vc4CopySurface(
    _Out_ struct drm_vc4_submit_rcl_surface *Destination,
    _In_ const RPI3VC4_SUBMIT_RCL_SURFACE *Source)
{
    Destination->hindex = Source->hindex;
    Destination->offset = Source->offset;
    Destination->bits = Source->bits;
    Destination->flags = Source->flags;
}

static NTSTATUS
Rpi3Vc4ValidateSubmissionHeader(
    _In_ const RPI3VC4_SUBMIT_CL *Submit,
    _In_ ULONG CommandLength,
    _Out_ PULONG InputEnd)
{
    ULONG End = sizeof(*Submit);
    ULONG Index;
    ULONG ResourceFlagBytes;
    ULONG AllowedFlags = RPI3VC4_SUBMIT_CL_USE_CLEAR_COLOR |
                         RPI3VC4_SUBMIT_CL_FIXED_RCL_ORDER |
                         RPI3VC4_SUBMIT_CL_RCL_ORDER_INCREASING_X |
                         RPI3VC4_SUBMIT_CL_RCL_ORDER_INCREASING_Y;

    if (Submit->Version != RPI3VC4_SUBMIT_CL_VERSION ||
        Submit->Size != sizeof(*Submit) ||
        Submit->BoHandleCount == 0 ||
        Submit->BoHandleCount > RPI3VC4_MAX_SUBMIT_RESOURCES ||
        Submit->Width == 0 || Submit->Height == 0 ||
        Submit->Width > 4096 || Submit->Height > 4096 ||
        Submit->MinXTile > Submit->MaxXTile ||
        Submit->MinYTile > Submit->MaxYTile ||
        Submit->ScratchBytes < RPI3VC4_SUBMIT_SCRATCH_MINIMUM ||
        (Submit->ResourceFlagsOffset & (sizeof(ULONG) - 1)) != 0 ||
        (Submit->ShaderRecSize & (sizeof(ULONG) - 1)) != 0 ||
        (Submit->UniformsSize & (sizeof(ULONG) - 1)) != 0 ||
        (Submit->Flags & ~AllowedFlags) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Submit->Reserved0[0] != 0 ||
        Submit->Reserved0[1] != 0 ||
        Submit->Reserved0[2] != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < RTL_NUMBER_OF(Submit->Reserved); Index++)
    {
        if (Submit->Reserved[Index] != 0)
            return STATUS_INVALID_PARAMETER;
    }
    if (Submit->BinClOffset < sizeof(*Submit) ||
        Submit->ShaderRecOffset < sizeof(*Submit) ||
        Submit->UniformsOffset < sizeof(*Submit) ||
        Submit->ResourceFlagsOffset < sizeof(*Submit))
    {
        return STATUS_INVALID_USER_BUFFER;
    }
    if (Submit->BinClSize > RPI3VC4_MAX_SUBMIT_STREAM_BYTES ||
        Submit->ShaderRecSize > RPI3VC4_MAX_SUBMIT_STREAM_BYTES ||
        Submit->UniformsSize > RPI3VC4_MAX_SUBMIT_STREAM_BYTES)
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }
    if (!Rpi3Vc4CheckedRange(CommandLength,
                             Submit->BinClOffset,
                             Submit->BinClSize) ||
        !Rpi3Vc4CheckedRange(CommandLength,
                             Submit->ShaderRecOffset,
                             Submit->ShaderRecSize) ||
        !Rpi3Vc4CheckedRange(CommandLength,
                             Submit->UniformsOffset,
                             Submit->UniformsSize))
    {
        return STATUS_INVALID_USER_BUFFER;
    }
    if (Submit->BoHandleCount > MAXULONG / sizeof(ULONG))
        return STATUS_INTEGER_OVERFLOW;
    ResourceFlagBytes = Submit->BoHandleCount * sizeof(ULONG);
    if (!Rpi3Vc4CheckedRange(CommandLength,
                             Submit->ResourceFlagsOffset,
                             ResourceFlagBytes))
    {
        return STATUS_INVALID_USER_BUFFER;
    }

#define RPI3VC4_ACCUMULATE_RANGE(_Offset, _Size)                         \
    do                                                                   \
    {                                                                    \
        ULONG _RangeEnd = (_Offset) + (_Size);                           \
        if (_RangeEnd < (_Offset))                                       \
            return STATUS_INTEGER_OVERFLOW;                              \
        End = max(End, _RangeEnd);                                       \
    } while (0)
    RPI3VC4_ACCUMULATE_RANGE(Submit->BinClOffset, Submit->BinClSize);
    RPI3VC4_ACCUMULATE_RANGE(Submit->ShaderRecOffset,
                             Submit->ShaderRecSize);
    RPI3VC4_ACCUMULATE_RANGE(Submit->UniformsOffset,
                             Submit->UniformsSize);
    RPI3VC4_ACCUMULATE_RANGE(Submit->ResourceFlagsOffset,
                             ResourceFlagBytes);
#undef RPI3VC4_ACCUMULATE_RANGE

    if (End > CommandLength)
        return STATUS_INVALID_USER_BUFFER;
    *InputEnd = End;
    return STATUS_SUCCESS;
}

static NTSTATUS
Rpi3Vc4PrepareResourceWrappers(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ PSOFTGPU_KMD_DEVICE KmdDevice,
    _In_ const DXGKARG_RENDER *Render,
    _In_reads_(Render->AllocationListSize) const ULONG *ResourceFlags,
    _Inout_ struct drm_device *DrmDevice,
    _Inout_ PRPI3VC4_VALIDATION_CONTEXT Validation,
    _Out_writes_(Render->AllocationListSize)
        struct drm_gem_cma_object **Objects)
{
    ULONG Index;
    ULONGLONG SlabBase = (ULONGLONG)Device->FrameBufferPhys.QuadPart;
    ULONGLONG SlabEnd;

    if (Device->FrameBufferSize > MAXULONGLONG - SlabBase)
        return STATUS_INTEGER_OVERFLOW;
    SlabEnd = SlabBase + Device->FrameBufferSize;

    for (Index = 0; Index < Render->AllocationListSize; Index++)
    {
        const DXGK_ALLOCATIONLIST *Entry = &Render->pAllocationList[Index];
        PSOFTGPU_OPENALLOC Open =
            (PSOFTGPU_OPENALLOC)Entry->hDeviceSpecificAllocation;
        struct drm_vc4_bo *Wrapper = &Validation->Wrappers[Index];
        ULONGLONG Physical;
        SIZE_T Offset;

        if ((ResourceFlags[Index] & ~RPI3VC4_RESOURCE_SHADER) != 0 ||
            Open == NULL || Open->Magic != SOFTGPU_OPENALLOC_MAGIC ||
            Open->Device != KmdDevice || Open->Size == 0 ||
            Entry->SegmentId != SOFTGPU_SEGMENT_ID ||
            Entry->PhysicalAddress.QuadPart <= 0)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Physical = (ULONGLONG)Entry->PhysicalAddress.QuadPart;
        if (Physical < SlabBase || Physical >= SlabEnd ||
            Open->Size > SlabEnd - Physical)
        {
            return STATUS_INVALID_ADDRESS;
        }
        Offset = (SIZE_T)(Physical - SlabBase);
        RtlZeroMemory(Wrapper, sizeof(*Wrapper));
        INIT_LIST_HEAD(&Wrapper->unref_head);
        Wrapper->base.base.dev = DrmDevice;
        Wrapper->base.base.size = Open->Size;
        Wrapper->base.vaddr = (PUCHAR)Device->FrameBuffer + Offset;
        Wrapper->base.paddr = Rpi3Vc4BusAddress(
                                  Validation->Platform,
                                  Entry->PhysicalAddress);
        Objects[Index] = &Wrapper->base;
        Validation->WrapperCount++;

        if ((ResourceFlags[Index] & RPI3VC4_RESOURCE_SHADER) != 0)
        {
            Wrapper->validated_shader = vc4_validate_shader(&Wrapper->base);
            if (Wrapper->validated_shader == NULL)
                return STATUS_INVALID_IMAGE_FORMAT;
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS
Rpi3Vc4ValidateRender(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ PSOFTGPU_KMD_DEVICE KmdDevice,
    _Inout_ PDXGKARG_RENDER Render)
{
    static volatile LONG FailureReports;
    const RPI3VC4_SUBMIT_CL *Submit;
    const ULONG *ResourceFlags;
    PRPI3VC4_CONTEXT Platform;
    PRPI3VC4_VALIDATION_CONTEXT Validation = NULL;
    struct drm_gem_cma_object **Objects = NULL;
    struct drm_device DrmDevice;
    struct drm_vc4_submit_cl Args;
    struct vc4_exec_info Exec;
    PRPI3VC4_DMA_PACKET Packet;
    PVOID Temporary = NULL;
    PUCHAR Bin;
    ULONG InputEnd;
    ULONG ShaderOffset;
    ULONG UniformOffset;
    ULONG ExecSize;
    ULONG TemporarySize;
    ULONG Index;
    ULONG FailureStage = 0;
    int ValidatorStatus = 0;
    NTSTATUS Status;

    if (Device == NULL || KmdDevice == NULL || Render == NULL ||
        Render->pCommand == NULL || Render->pDmaBuffer == NULL ||
        Render->CommandLength < sizeof(*Submit) ||
        Render->DmaSize < sizeof(*Packet))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Submit = (const RPI3VC4_SUBMIT_CL *)Render->pCommand;
    if (Submit->Version != RPI3VC4_SUBMIT_CL_VERSION)
        return STATUS_NOT_SUPPORTED;
    Platform = (PRPI3VC4_CONTEXT)Device->PlatformContext;
    if (Platform == NULL || !Platform->V3dReady || Platform->BusAlias == 0)
        return STATUS_DEVICE_NOT_READY;
    if (Submit->BoHandleCount != Render->AllocationListSize ||
        Render->pAllocationList == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    FailureStage = 1;
    Status = Rpi3Vc4ValidateSubmissionHeader(Submit,
                                             Render->CommandLength,
                                             &InputEnd);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Submit->ScratchBytes > Render->DmaSize ||
        ALIGN_UP_BY(sizeof(*Packet), 16) >
            Render->DmaSize - Submit->ScratchBytes)
    {
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    }
    UNREFERENCED_PARAMETER(InputEnd);
    ResourceFlags = (const ULONG *)((const UCHAR *)Submit +
                                    Submit->ResourceFlagsOffset);

    Validation = Rpi3Vc4ValidatorAllocate(sizeof(*Validation), TRUE);
    Objects = Rpi3Vc4ValidatorAllocate(
                  Submit->BoHandleCount * sizeof(*Objects), TRUE);
    if (Validation == NULL || Objects == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    Validation->Wrappers = Rpi3Vc4ValidatorAllocate(
                               (Submit->BoHandleCount + 3) *
                                   sizeof(*Validation->Wrappers),
                               TRUE);
    if (Validation->Wrappers == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    Validation->WrapperCapacity = Submit->BoHandleCount + 3;
    Validation->Platform = Platform;
    Validation->Arena = (PUCHAR)Render->pDmaBuffer +
                        ALIGN_UP_BY(sizeof(*Packet), 16);
    Validation->ArenaPhysical = Render->DmaBufferPhysicalAddress;
    Validation->ArenaPhysical.QuadPart +=
        ALIGN_UP_BY(sizeof(*Packet), 16);
    Validation->ArenaSize = Submit->ScratchBytes;
    RtlZeroMemory(&DrmDevice, sizeof(DrmDevice));
    DrmDevice.Validation = Validation;

    FailureStage = 2;
    Status = Rpi3Vc4PrepareResourceWrappers(Device,
                                             KmdDevice,
                                             Render,
                                             ResourceFlags,
                                             &DrmDevice,
                                             Validation,
                                             Objects);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    RtlZeroMemory(&Args, sizeof(Args));
    Args.bin_cl = (uint64_t)(ULONG_PTR)
        ((const UCHAR *)Submit + Submit->BinClOffset);
    Args.shader_rec = (uint64_t)(ULONG_PTR)
        ((const UCHAR *)Submit + Submit->ShaderRecOffset);
    Args.uniforms = (uint64_t)(ULONG_PTR)
        ((const UCHAR *)Submit + Submit->UniformsOffset);
    Args.bin_cl_size = Submit->BinClSize;
    Args.shader_rec_size = Submit->ShaderRecSize;
    Args.shader_rec_count = Submit->ShaderRecCount;
    Args.uniforms_size = Submit->UniformsSize;
    Args.bo_handle_count = Submit->BoHandleCount;
    Args.width = Submit->Width;
    Args.height = Submit->Height;
    Args.min_x_tile = Submit->MinXTile;
    Args.min_y_tile = Submit->MinYTile;
    Args.max_x_tile = Submit->MaxXTile;
    Args.max_y_tile = Submit->MaxYTile;
    Rpi3Vc4CopySurface(&Args.color_read, &Submit->ColorRead);
    Rpi3Vc4CopySurface(&Args.color_write, &Submit->ColorWrite);
    Rpi3Vc4CopySurface(&Args.zs_read, &Submit->ZsRead);
    Rpi3Vc4CopySurface(&Args.zs_write, &Submit->ZsWrite);
    Rpi3Vc4CopySurface(&Args.msaa_color_write,
                       &Submit->MsaaColorWrite);
    Rpi3Vc4CopySurface(&Args.msaa_zs_write, &Submit->MsaaZsWrite);
    Args.clear_color[0] = Submit->ClearColor[0];
    Args.clear_color[1] = Submit->ClearColor[1];
    Args.clear_z = Submit->ClearZ;
    Args.clear_s = Submit->ClearS;
    Args.flags = Submit->Flags;

    RtlZeroMemory(&Exec, sizeof(Exec));
    INIT_LIST_HEAD(&Exec.unref_list);
    Exec.args = &Args;
    Exec.bo = Objects;
    Exec.bo_count = Submit->BoHandleCount;
    if (Args.bin_cl_size != 0)
    {
        ShaderOffset = ALIGN_UP_BY(Args.bin_cl_size, 16);
        if (Args.shader_rec_size > MAXULONG - ShaderOffset)
        {
            Status = STATUS_INTEGER_OVERFLOW;
            goto Cleanup;
        }
        UniformOffset = ShaderOffset + Args.shader_rec_size;
        if (Args.uniforms_size > MAXULONG - UniformOffset)
        {
            Status = STATUS_INTEGER_OVERFLOW;
            goto Cleanup;
        }
        ExecSize = UniformOffset + Args.uniforms_size;
        if (Args.shader_rec_count >
            (MAXULONG - ExecSize) / sizeof(*Exec.shader_state))
        {
            Status = STATUS_INTEGER_OVERFLOW;
            goto Cleanup;
        }
        TemporarySize = ExecSize +
                        Args.shader_rec_count * sizeof(*Exec.shader_state);
        Temporary = Rpi3Vc4ValidatorAllocate(TemporarySize, FALSE);
        if (Temporary == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        Bin = Temporary;
        Exec.shader_rec_u = Bin + ShaderOffset;
        Exec.uniforms_u = Bin + UniformOffset;
        Exec.shader_state = (struct vc4_shader_state *)(Bin + ExecSize);
        Exec.shader_state_size = Args.shader_rec_count;
        RtlCopyMemory(Bin, (PVOID)(ULONG_PTR)Args.bin_cl,
                      Args.bin_cl_size);
        RtlCopyMemory(Exec.shader_rec_u,
                      (PVOID)(ULONG_PTR)Args.shader_rec,
                      Args.shader_rec_size);
        RtlCopyMemory(Exec.uniforms_u,
                      (PVOID)(ULONG_PTR)Args.uniforms,
                      Args.uniforms_size);

        Exec.exec_bo = drm_gem_cma_create(&DrmDevice, ExecSize);
        if (Exec.exec_bo == NULL)
        {
            Status = STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
            goto Cleanup;
        }
        Exec.ct0ca = Exec.exec_bo->paddr;
        Exec.bin_u = Bin;
        Exec.shader_rec_v = (PUCHAR)Exec.exec_bo->vaddr + ShaderOffset;
        Exec.shader_rec_p = Exec.exec_bo->paddr + ShaderOffset;
        Exec.shader_rec_size = Args.shader_rec_size;
        Exec.uniforms_v = (PUCHAR)Exec.exec_bo->vaddr + UniformOffset;
        Exec.uniforms_p = Exec.exec_bo->paddr + UniformOffset;
        Exec.uniforms_size = Args.uniforms_size;
        FailureStage = 3;
        ValidatorStatus = vc4_validate_bin_cl(&DrmDevice,
                                               Exec.exec_bo->vaddr,
                                               Bin,
                                               &Exec);
        if (ValidatorStatus != 0)
        {
            Status = STATUS_ILLEGAL_INSTRUCTION;
            goto Cleanup;
        }
        FailureStage = 4;
        ValidatorStatus = vc4_validate_shader_recs(&DrmDevice, &Exec);
        if (ValidatorStatus != 0)
        {
            Status = STATUS_INVALID_IMAGE_FORMAT;
            goto Cleanup;
        }
    }

    Exec.tile_width = (Args.color_write.bits &
                       VC4_RENDER_CONFIG_MS_MODE_4X) ? 32 : 64;
    Exec.tile_height = Exec.tile_width;
    FailureStage = 5;
    ValidatorStatus = vc4_get_rcl(&DrmDevice, &Exec);
    if (ValidatorStatus != 0)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    Packet = (PRPI3VC4_DMA_PACKET)Render->pDmaBuffer;
    RtlZeroMemory(Packet, sizeof(*Packet));
    Packet->Magic = RPI3VC4_DMA_PACKET_MAGIC;
    Packet->Op = RPI3VC4_DMA_OP_VALIDATED_CL;
    Packet->Size = sizeof(*Packet);
    Packet->Ct0Start = Exec.ct0ca;
    Packet->Ct0End = Exec.ct0ea;
    Packet->Ct1Start = Exec.ct1ca;
    Packet->Ct1End = Exec.ct1ea;
    if (Exec.tile_bo != NULL)
    {
        Packet->TileStateAddress = Exec.tile_bo->paddr;
        Packet->TileAllocationAddress =
            Exec.tile_bo->paddr + Exec.tile_alloc_offset;
        Packet->TileAllocationSize =
            (ULONG)(Exec.tile_bo->base.size - Exec.tile_alloc_offset);
    }
    if (Exec.ct0ca != 0)
    {
        if (Platform->V3dBinOverflow == NULL ||
            Platform->V3dBinOverflowPhysical.QuadPart <= 0)
        {
            Status = STATUS_DEVICE_NOT_READY;
            goto Cleanup;
        }
        Packet->BinnerOverflowAddress = Rpi3Vc4BusAddress(
            Platform, Platform->V3dBinOverflowPhysical);
        Packet->BinnerOverflowSize = RPI3VC4_V3D_BIN_OVERFLOW_SIZE;
    }
    if (Validation->ArenaCursor > Validation->ArenaSize)
    {
        Status = STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
        goto Cleanup;
    }
    Render->pDmaBuffer = Validation->Arena + Validation->ArenaCursor;
    Render->MultipassOffset = 0;
    Status = STATUS_SUCCESS;

Cleanup:
    if (!NT_SUCCESS(Status) &&
        InterlockedIncrement(&FailureReports) <= 32)
    {
        DPRINT1("RPI3VC4_VALIDATE_FAIL stage=%lu status=%08lx validator=%d command=%lu bos=%lu bin=%lu shader=%lu/%lu uniforms=%lu size=%lux%lu tiles=%lu,%lu-%lu,%lu color=%lu/%08lx depth=%lu/%08lx flags=%08lx\n",
                FailureStage,
                Status,
                ValidatorStatus,
                Render->CommandLength,
                Submit->BoHandleCount,
                Submit->BinClSize,
                Submit->ShaderRecSize,
                Submit->ShaderRecCount,
                Submit->UniformsSize,
                Submit->Width,
                Submit->Height,
                Submit->MinXTile,
                Submit->MinYTile,
                Submit->MaxXTile,
                Submit->MaxYTile,
                Submit->ColorWrite.hindex,
                (ULONG)Submit->ColorWrite.bits,
                Submit->ZsWrite.hindex,
                (ULONG)Submit->ZsWrite.bits,
                Submit->Flags);
    }
    if (Validation != NULL && Validation->Wrappers != NULL)
    {
        for (Index = 0; Index < Validation->WrapperCount; Index++)
            Rpi3Vc4FreeValidatedShader(&Validation->Wrappers[Index]);
        Rpi3Vc4ValidatorFree(Validation->Wrappers);
    }
    Rpi3Vc4ValidatorFree(Temporary);
    Rpi3Vc4ValidatorFree(Objects);
    Rpi3Vc4ValidatorFree(Validation);
    return Status;
}
