/*
 * PROJECT:     ReactOS WDDM Null/Software GPU Miniport
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     DMA command scheduling DDIs for softgpu.sys.
 *              Implements SubmitCommand, PreemptCommand, BuildPagingBuffer,
 *              QueryCurrentFence, Patch, and the fence-completion DPC.
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * Architecture notes (amd64/x86)
 * ================================
 * SubmitCommand is called at DISPATCH_LEVEL.  It stores the fence ID under
 * FenceLock (KSPIN_LOCK) and queues the per-device DPC.
 *
 * SoftGpuDpcRoutine fires at DISPATCH_LEVEL.  It:
 *   1. Acquires FenceLock.
 *   2. Copies CurrentFence -> CompletedFence.
 *   3. Releases FenceLock.
 *   4. Calls DxgkCbNotifyInterrupt(DXGK_INTERRUPT_TYPE_DMA_COMPLETED).
 *   5. Calls DxgkCbNotifyDpc.
 *
 * Steps 4 and 5 must be called in this order per the WDDM contract.
 * DxgkCbNotifyInterrupt is called inside the conceptual "interrupt context"
 * (no real interrupt on softgpu) and DxgkCbNotifyDpc signals dxgkrnl to
 * wake up the scheduling thread.
 *
 * On x86 TSO the store to CompletedFence inside FenceLock is visible to
 * QueryCurrentFence (also inside FenceLock) without an explicit fence
 * instruction, but we keep the spinlock for correct IRQL management.
 */

/* INCLUDES ******************************************************************/

#include "softgpu.h"
#include "softgpu_2d_core.h"
#include "fault_policy_core.h"

#define SOFTGPU_TRACE_LOG_LIMIT  32
#define SOFTGPU_TRACE_SLOW_US    1000ULL

static volatile LONG g_SoftGpuSubmitTraceCount = 0;
static volatile LONG g_SoftGpuDpcTraceCount = 0;
static volatile LONG g_SoftGpuPointerTraceCount = 0;

FORCEINLINE ULONGLONG
SoftGpuTraceNow100ns(VOID)
{
    return KeQueryInterruptTime();
}

FORCEINLINE ULONGLONG
SoftGpuTraceElapsedUs(
    _In_ ULONGLONG Start100ns)
{
    ULONGLONG End100ns = KeQueryInterruptTime();

    if (End100ns <= Start100ns)
        return 0;

    return (End100ns - Start100ns) / 10ULL;
}

/* =========================================================================
 * SoftGpuDpcRoutine  — KDPC callback
 * =========================================================================
 */

/*
 * SoftGpuDpcRoutine
 *
 * Fires at DISPATCH_LEVEL after SubmitCommand queues the DPC.
 * DeferredContext is the PSOFTGPU_DEVICE pointer.
 *
 * Notifies dxgkrnl that the last submitted fence has been "completed" by
 * our simulated GPU.
 */
static NTSTATUS
SoftGpuExecuteBlt(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ CONST SOFTGPU_CMD *Cmd)
{
    ULONGLONG SlabBase = (ULONGLONG)Device->FrameBufferPhys.QuadPart;
    SIZE_T SrcOffset;
    SIZE_T DstOffset;
    NTSTATUS Status;

    Status = SoftGpu2dResolveSlabRange(
                 SlabBase,
                 Device->FrameBufferSize,
                 Cmd->SrcAddress,
                 1,
                 &SrcOffset);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = SoftGpu2dResolveSlabRange(
                 SlabBase,
                 Device->FrameBufferSize,
                 Cmd->DstAddress,
                 1,
                 &DstOffset);
    if (!NT_SUCCESS(Status))
        return Status;

    return SoftGpu2dCopyRect(
        (PUCHAR)Device->FrameBuffer + SrcOffset,
        Device->FrameBufferSize - SrcOffset,
        Cmd->SrcPitch,
        &Cmd->SrcRect,
        (PUCHAR)Device->FrameBuffer + DstOffset,
        Device->FrameBufferSize - DstOffset,
        Cmd->DstPitch,
        &Cmd->DstRect);
}

static NTSTATUS
SoftGpuExecuteFill(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ CONST SOFTGPU_CMD *Cmd)
{
    ULONGLONG SlabBase = (ULONGLONG)Device->FrameBufferPhys.QuadPart;
    SIZE_T DstOffset;
    NTSTATUS Status;

    Status = SoftGpu2dResolveSlabRange(
                 SlabBase,
                 Device->FrameBufferSize,
                 Cmd->DstAddress,
                 1,
                 &DstOffset);
    if (!NT_SUCCESS(Status))
        return Status;

    return SoftGpu2dFillRect(
        (PUCHAR)Device->FrameBuffer + DstOffset,
        Device->FrameBufferSize - DstOffset,
        Cmd->DstPitch,
        &Cmd->DstRect,
        Cmd->Color);
}

static BOOLEAN
SoftGpuReadStablePte(
    _In_ volatile DXGK_PTE *Source,
    _Out_ DXGK_PTE *Snapshot)
{
    ULONG Attempt;

    for (Attempt = 0; Attempt != 4; ++Attempt)
    {
        LONG64 FlagsBefore;
        LONG64 AddressBefore;
        LONG64 FlagsAfter;
        LONG64 AddressAfter;

        FlagsBefore = InterlockedCompareExchange64(
            (volatile LONG64 *)&Source->Flags, 0, 0);
        AddressBefore = InterlockedCompareExchange64(
            (volatile LONG64 *)&Source->PageAddress, 0, 0);
        KeMemoryBarrier();
        FlagsAfter = InterlockedCompareExchange64(
            (volatile LONG64 *)&Source->Flags, 0, 0);
        AddressAfter = InterlockedCompareExchange64(
            (volatile LONG64 *)&Source->PageAddress, 0, 0);
        if (FlagsBefore == FlagsAfter && AddressBefore == AddressAfter)
        {
            Snapshot->Flags = (ULONGLONG)FlagsBefore;
            Snapshot->PageAddress = (ULONGLONG)AddressBefore;
            return TRUE;
        }
    }

    return FALSE;
}

static BOOLEAN
SoftGpuWalkGpuVaPage(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ ULONGLONG RootPhysical,
    _In_ ULONG RootEntryCount,
    _In_ ULONGLONG Va,
    _In_ SOFTGPU_GPUVA_ACCESS Access,
    _Out_ ULONGLONG *PhysicalAddress,
    _Out_ PBOOLEAN ZeroPage,
    _Out_opt_ PULONG FaultLevel)
{
    ULONGLONG TablePhysical = RootPhysical;
    LONG Level;

    UNREFERENCED_PARAMETER(Device);

    if (FaultLevel != NULL)
        *FaultLevel = SOFTGPU_GPUVA_LEVELS - 1;
    if (PhysicalAddress == NULL ||
        ZeroPage == NULL ||
        RootPhysical == 0 ||
        RootEntryCount == 0 ||
        RootEntryCount > (1u << SOFTGPU_GPUVA_INDEX_BITS) ||
        Va >= SOFTGPU_GPUVA_LIMIT ||
        (Access & ~(SoftGpuGpuVaRead |
                    SoftGpuGpuVaWrite |
                    SoftGpuGpuVaExecute)) != 0 ||
        Access == 0)
    {
        return FALSE;
    }

    *PhysicalAddress = 0;
    *ZeroPage = FALSE;
    for (Level = SOFTGPU_GPUVA_LEVELS - 1; Level >= 0; Level--)
    {
        PHYSICAL_ADDRESS TableAddress;
        volatile DXGK_PTE *Entries;
        ULONG Index;
        ULONG TableEntryCount;
        SIZE_T TableSize;
        ULONG Shift = 12 + SOFTGPU_GPUVA_INDEX_BITS * (ULONG)Level;
        DXGK_PTE Entry;

        if (FaultLevel != NULL)
            *FaultLevel = (ULONG)Level;
        if (TablePhysical == 0)
            return FALSE;
        Index = (ULONG)((Va >> Shift) &
                        ((1u << SOFTGPU_GPUVA_INDEX_BITS) - 1u));
        TableEntryCount = 1u << SOFTGPU_GPUVA_INDEX_BITS;
        if (Level == SOFTGPU_GPUVA_LEVELS - 1)
        {
            if (Index >= RootEntryCount)
                return FALSE;
            TableEntryCount = RootEntryCount;
        }
        TableSize = TableEntryCount * sizeof(DXGK_PTE);
        TableAddress.QuadPart = (LONGLONG)TablePhysical;
        Entries = (volatile DXGK_PTE *)MmMapIoSpace(TableAddress,
                                                    TableSize,
                                                    MmCached);
        if (Entries == NULL)
            return FALSE;
        if (!SoftGpuReadStablePte(&Entries[Index], &Entry))
        {
            MmUnmapIoSpace((PVOID)Entries, TableSize);
            return FALSE;
        }
        MmUnmapIoSpace((PVOID)Entries, TableSize);

        if (Level != 0)
        {
            if (!Entry.Valid ||
                Entry.Zero ||
                Entry.LargePage ||
                Entry.PageTablePageSize !=
                    DXGK_PTE_PAGE_TABLE_PAGE_4KB)
            {
                return FALSE;
            }
            TablePhysical =
                Entry.PageTableAddress & ~(ULONGLONG)(PAGE_SIZE - 1);
            continue;
        }

        if (Entry.Zero)
        {
            if ((Access & (SoftGpuGpuVaWrite |
                           SoftGpuGpuVaExecute)) != 0)
            {
                return FALSE;
            }
            *ZeroPage = TRUE;
            return TRUE;
        }
        if (!Entry.Valid ||
            Entry.LargePage ||
            Entry.PageTablePageSize !=
                DXGK_PTE_PAGE_TABLE_PAGE_4KB ||
            ((Access & SoftGpuGpuVaWrite) != 0 && Entry.ReadOnly) ||
            ((Access & SoftGpuGpuVaExecute) != 0 && Entry.NoExecute))
        {
            return FALSE;
        }
        *PhysicalAddress =
            (Entry.PageAddress & ~(ULONGLONG)(PAGE_SIZE - 1)) +
            (Va & (PAGE_SIZE - 1));
        return TRUE;
    }

    return FALSE;
}

BOOLEAN
SoftGpuValidateGpuVaRange(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ ULONGLONG RootPhysical,
    _In_ ULONG RootEntryCount,
    _In_ ULONGLONG Va,
    _In_ ULONGLONG SizeInBytes,
    _In_ SOFTGPU_GPUVA_ACCESS Access)
{
    ULONGLONG Offset = 0;

    if (SizeInBytes == 0 ||
        Va >= SOFTGPU_GPUVA_LIMIT ||
        SizeInBytes > SOFTGPU_GPUVA_LIMIT - Va)
    {
        return FALSE;
    }

    while (Offset < SizeInBytes)
    {
        ULONGLONG Physical;
        BOOLEAN ZeroPage;
        ULONGLONG CurrentVa = Va + Offset;
        ULONGLONG Chunk = min(
            SizeInBytes - Offset,
            (ULONGLONG)PAGE_SIZE -
                (CurrentVa & (PAGE_SIZE - 1)));

        if (!SoftGpuWalkGpuVaPage(Device,
                                  RootPhysical,
                                  RootEntryCount,
                                  CurrentVa,
                                  Access,
                                  &Physical,
                                  &ZeroPage,
                                  NULL))
        {
            return FALSE;
        }
        Offset += Chunk;
    }
    return TRUE;
}

static NTSTATUS
SoftGpuExecutePage(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ CONST SOFTGPU_CMD *Cmd)
{
    ULONGLONG SlabBase = (ULONGLONG)Device->FrameBufferPhys.QuadPart;
    SIZE_T SlabOffset;
    PUCHAR SlabVa;
    NTSTATUS Status;

    Status = SoftGpu2dResolveSlabRange(
                 SlabBase,
                 Device->FrameBufferSize,
                 Cmd->SlabAddress,
                 Cmd->ByteCount,
                 &SlabOffset);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Cmd->SystemAddress == 0)
        return STATUS_INVALID_ADDRESS;

    SlabVa = (PUCHAR)Device->FrameBuffer + SlabOffset;
    if ((Cmd->Flags & SOFTGPU_CMD_FLAG_TO_SLAB) != 0)
        RtlCopyMemory(SlabVa, (PVOID)(ULONG_PTR)Cmd->SystemAddress, (SIZE_T)Cmd->ByteCount);
    else
        RtlCopyMemory((PVOID)(ULONG_PTR)Cmd->SystemAddress, SlabVa, (SIZE_T)Cmd->ByteCount);
    return STATUS_SUCCESS;
}

static NTSTATUS
SoftGpuExecuteFillLinear(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ CONST SOFTGPU_CMD *Cmd)
{
    ULONGLONG SlabBase = (ULONGLONG)Device->FrameBufferPhys.QuadPart;
    SIZE_T SlabOffset;
    PULONG SlabVa;
    SIZE_T Count;
    SIZE_T Index;
    NTSTATUS Status;

    if ((Cmd->ByteCount % sizeof(ULONG)) != 0)
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }
    Status = SoftGpu2dResolveSlabRange(
                 SlabBase,
                 Device->FrameBufferSize,
                 Cmd->SlabAddress,
                 Cmd->ByteCount,
                 &SlabOffset);
    if (!NT_SUCCESS(Status))
        return Status;

    SlabVa = (PULONG)((PUCHAR)Device->FrameBuffer + SlabOffset);
    Count = (SIZE_T)(Cmd->ByteCount / sizeof(ULONG));
    for (Index = 0; Index < Count; Index++)
        SlabVa[Index] = Cmd->Color;
    return STATUS_SUCCESS;
}

static BOOLEAN
SoftGpuStoreFenceValue(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ CONST SOFTGPU_GPUVA_ROOT *Root,
    _In_ ULONGLONG FenceGpuVa,
    _In_ ULONGLONG FenceValue,
    _Out_opt_ PULONG FaultLevel)
{
    ULONGLONG Physical;
    BOOLEAN ZeroPage;
    PHYSICAL_ADDRESS Address;
    PUCHAR Mapping;
    volatile LONG64 *Value;

    if (FaultLevel != NULL)
        *FaultLevel = SOFTGPU_GPUVA_LEVELS - 1;
    if (Root == NULL ||
        Root->PhysicalAddress == 0 ||
        Root->EntryCount == 0 ||
        FenceGpuVa == 0 ||
        (FenceGpuVa & (sizeof(UINT64) - 1)) != 0)
    {
        return FALSE;
    }
    if (!SoftGpuWalkGpuVaPage(Device,
                              Root->PhysicalAddress,
                              Root->EntryCount,
                              FenceGpuVa,
                              SoftGpuGpuVaWrite,
                              &Physical,
                              &ZeroPage,
                              FaultLevel) ||
        ZeroPage)
    {
        return FALSE;
    }

    Address.QuadPart =
        (LONGLONG)(Physical & ~(ULONGLONG)(PAGE_SIZE - 1));
    Mapping = MmMapIoSpace(Address, PAGE_SIZE, MmCached);
    if (Mapping == NULL)
        return FALSE;
    Value = (volatile LONG64 *)(Mapping +
                                (Physical & (PAGE_SIZE - 1)));
    InterlockedExchange64(Value, (LONG64)FenceValue);
    MmUnmapIoSpace(Mapping, PAGE_SIZE);
    return TRUE;
}

static BOOLEAN
SoftGpuLoadFenceValue(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ CONST SOFTGPU_GPUVA_ROOT *Root,
    _In_ ULONGLONG FenceGpuVa,
    _Out_ ULONGLONG *FenceValue,
    _Out_opt_ PULONG FaultLevel)
{
    ULONGLONG Physical;
    BOOLEAN ZeroPage;
    PHYSICAL_ADDRESS Address;
    PUCHAR Mapping;
    volatile LONG64 *Value;

    if (FaultLevel != NULL)
        *FaultLevel = SOFTGPU_GPUVA_LEVELS - 1;
    if (Root == NULL ||
        FenceValue == NULL ||
        Root->PhysicalAddress == 0 ||
        Root->EntryCount == 0 ||
        FenceGpuVa == 0 ||
        (FenceGpuVa & (sizeof(UINT64) - 1)) != 0)
    {
        return FALSE;
    }
    if (!SoftGpuWalkGpuVaPage(Device,
                              Root->PhysicalAddress,
                              Root->EntryCount,
                              FenceGpuVa,
                              SoftGpuGpuVaRead,
                              &Physical,
                              &ZeroPage,
                              FaultLevel))
    {
        return FALSE;
    }
    if (ZeroPage)
    {
        *FenceValue = 0;
        return TRUE;
    }

    Address.QuadPart =
        (LONGLONG)(Physical & ~(ULONGLONG)(PAGE_SIZE - 1));
    Mapping = MmMapIoSpace(Address, PAGE_SIZE, MmCached);
    if (Mapping == NULL)
        return FALSE;
    Value = (volatile LONG64 *)(Mapping +
                                (Physical & (PAGE_SIZE - 1)));
    *FenceValue = (ULONGLONG)InterlockedCompareExchange64(Value, 0, 0);
    MmUnmapIoSpace(Mapping, PAGE_SIZE);
    return TRUE;
}

typedef enum _SOFTGPU_FENCE_WAIT_RESULT
{
    SoftGpuFenceWaitInvalid,
    SoftGpuFenceWaitPending,
    SoftGpuFenceWaitSatisfied
} SOFTGPU_FENCE_WAIT_RESULT;

static SOFTGPU_FENCE_WAIT_RESULT
SoftGpuTestFenceWait(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ CONST SOFTGPU_SUBMIT *Submit,
    _In_ CONST SOFTGPU_CMD *Cmd,
    _Out_opt_ PULONG FaultLevel)
{
    ULONGLONG Value;

    if (!SoftGpuLoadFenceValue(Device,
                               &Submit->Root,
                               Cmd->FenceGpuVa,
                               &Value,
                               FaultLevel))
    {
        return SoftGpuFenceWaitInvalid;
    }
    return Value >= Cmd->FenceValue
               ? SoftGpuFenceWaitSatisfied
               : SoftGpuFenceWaitPending;
}

/*
 * Copy a GPUVA range through a captured root one page at a time.  GPU virtual
 * memory is allowed to map adjacent virtual pages to unrelated physical
 * pages, so callers must not turn the first translation into one large
 * MmMapIoSpace mapping.
 */
static BOOLEAN
SoftGpuReadGpuVa(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ CONST SOFTGPU_GPUVA_ROOT *Root,
    _In_ ULONGLONG GpuVa,
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ SIZE_T Size,
    _Out_opt_ PULONGLONG FaultGpuVa,
    _Out_opt_ PULONG FaultLevel)
{
    PUCHAR Destination = (PUCHAR)Buffer;
    SIZE_T Remaining = Size;

    if (FaultGpuVa != NULL)
        *FaultGpuVa = GpuVa;
    if (FaultLevel != NULL)
        *FaultLevel = SOFTGPU_GPUVA_LEVELS - 1;
    if (Root == NULL ||
        Root->PhysicalAddress == 0 ||
        Root->EntryCount == 0 ||
        Buffer == NULL ||
        Size == 0 ||
        GpuVa >= SOFTGPU_GPUVA_LIMIT ||
        (ULONGLONG)Size > SOFTGPU_GPUVA_LIMIT - GpuVa)
    {
        return FALSE;
    }

    while (Remaining != 0)
    {
        SIZE_T PageOffset = (SIZE_T)(GpuVa & (PAGE_SIZE - 1));
        SIZE_T Chunk = min(Remaining, PAGE_SIZE - PageOffset);
        ULONGLONG Physical;
        BOOLEAN ZeroPage;
        PHYSICAL_ADDRESS PageAddress;
        PVOID Mapping;

        if (FaultGpuVa != NULL)
            *FaultGpuVa = GpuVa;
        if (!SoftGpuWalkGpuVaPage(Device,
                                  Root->PhysicalAddress,
                                  Root->EntryCount,
                                  GpuVa,
                                  SoftGpuGpuVaExecute,
                                  &Physical,
                                  &ZeroPage,
                                  FaultLevel) ||
            ZeroPage)
        {
            return FALSE;
        }

        PageAddress.QuadPart =
            (LONGLONG)(Physical & ~(ULONGLONG)(PAGE_SIZE - 1));
        Mapping = MmMapIoSpace(PageAddress, PAGE_SIZE, MmCached);
        if (Mapping == NULL)
            return FALSE;

        RtlCopyMemory(Destination,
                      (PUCHAR)Mapping +
                          (SIZE_T)(Physical & (PAGE_SIZE - 1)),
                      Chunk);
        MmUnmapIoSpace(Mapping, PAGE_SIZE);

        Destination += Chunk;
        GpuVa += Chunk;
        Remaining -= Chunk;
    }

    return TRUE;
}

typedef enum _SOFTGPU_EXECUTION_OUTCOME
{
    SoftGpuExecutionCompleted,
    SoftGpuExecutionPending,
    SoftGpuExecutionFaulted
} SOFTGPU_EXECUTION_OUTCOME;

typedef struct _SOFTGPU_EXECUTION_RESULT
{
    SOFTGPU_EXECUTION_OUTCOME Outcome;
    ULONG                     ResumeOffset;
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
    BOOLEAN                   MonitoredFenceSignaled;
#endif
    D3DGPU_VIRTUAL_ADDRESS    FaultedVirtualAddress;
    ULONG                     PageTableLevel;
    DXGK_PAGE_FAULT_FLAGS     PageFaultFlags;
    DXGK_FAULT_ERROR_CODE     FaultErrorCode;
} SOFTGPU_EXECUTION_RESULT, *PSOFTGPU_EXECUTION_RESULT;

static VOID
SoftGpuSetExecutionFault(
    _Out_ PSOFTGPU_EXECUTION_RESULT Result,
    _In_ D3DGPU_VIRTUAL_ADDRESS FaultedVirtualAddress,
    _In_ ULONG PageTableLevel,
    _In_ DXGK_PAGE_FAULT_FLAGS PageFaultFlags,
    _In_ DXGK_GENERAL_ERROR_CODE ErrorCode)
{
    Result->Outcome = SoftGpuExecutionFaulted;
    Result->FaultedVirtualAddress = FaultedVirtualAddress;
    Result->PageTableLevel = PageTableLevel;
    Result->PageFaultFlags = PageFaultFlags;
    RtlZeroMemory(&Result->FaultErrorCode, sizeof(Result->FaultErrorCode));
    Result->FaultErrorCode.IsDeviceSpecificCode = 0;
    Result->FaultErrorCode.GeneralErrorCode = ErrorCode;
}

/*
 * SoftGpuExecuteDmaBuffer
 *
 * Executes one submitted DMA buffer and distinguishes a valid unsatisfied GPU
 * wait from a terminal execution fault.  GPUVA translation or permission
 * failures carry the exact address and page-table level into the WDDM 2.0
 * page-fault interrupt; malformed command records use the same interrupt with
 * FaultedVirtualAddress == 0 and INVALID_INSTRUCTION as required by the public
 * fault-error-code contract.
 */
static VOID
SoftGpuExecuteDmaBuffer(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ CONST SOFTGPU_SUBMIT *Submit,
    _Out_ PSOFTGPU_EXECUTION_RESULT Result)
{
    PUCHAR MapVa = NULL;
    ULONG Offset;

    RtlZeroMemory(Result, sizeof(*Result));
    Result->Outcome = SoftGpuExecutionCompleted;
    Result->ResumeOffset = Submit->StartOffset;
    Result->PageTableLevel = SOFTGPU_GPUVA_LEVELS - 1;
    if (Submit->NullRendering)
        return;

    if (Submit->EndOffset <= Submit->StartOffset ||
        Submit->EndOffset - Submit->StartOffset < sizeof(SOFTGPU_CMD))
    {
        SoftGpuSetExecutionFault(Result,
                                 0,
                                 0,
                                 (DXGK_PAGE_FAULT_FLAGS)0,
                                 DXGK_GENERAL_ERROR_INVALID_INSTRUCTION);
        return;
    }

    if (Submit->VirtualAddressing)
    {
        if (Submit->DmaGpuVa == 0 ||
            Submit->Root.PhysicalAddress == 0 ||
            Submit->Root.EntryCount == 0)
        {
            SoftGpuSetExecutionFault(Result,
                                     Submit->DmaGpuVa,
                                     SOFTGPU_GPUVA_LEVELS - 1,
                                     (DXGK_PAGE_FAULT_FLAGS)0,
                                     DXGK_GENERAL_ERROR_PAGE_FAULT);
            return;
        }
    }
    else
    {
        if (Submit->DmaPhys.QuadPart == 0)
        {
            SoftGpuSetExecutionFault(Result,
                                     0,
                                     0,
                                     (DXGK_PAGE_FAULT_FLAGS)0,
                                     DXGK_GENERAL_ERROR_INVALID_INSTRUCTION);
            return;
        }
        MapVa = MmMapIoSpace(Submit->DmaPhys,
                             Submit->EndOffset,
                             MmCached);
        if (MapVa == NULL)
        {
            SoftGpuSetExecutionFault(Result,
                                     0,
                                     0,
                                     (DXGK_PAGE_FAULT_FLAGS)0,
                                     DXGK_GENERAL_ERROR_PAGE_FAULT);
            return;
        }
    }

    Offset = Submit->StartOffset;
    while (Offset <= Submit->EndOffset - sizeof(SOFTGPU_CMD))
    {
        SOFTGPU_CMD VirtualCommand;
        CONST SOFTGPU_CMD *Cmd;
        NTSTATUS ExecuteStatus = STATUS_SUCCESS;

        if (Submit->VirtualAddressing)
        {
            ULONGLONG FaultGpuVa;
            ULONG FaultLevel;

            if (!SoftGpuReadGpuVa(Device,
                                  &Submit->Root,
                                  Submit->DmaGpuVa + Offset,
                                  &VirtualCommand,
                                  sizeof(VirtualCommand),
                                  &FaultGpuVa,
                                  &FaultLevel))
            {
                Result->ResumeOffset = Offset;
                SoftGpuSetExecutionFault(Result,
                                         FaultGpuVa,
                                         FaultLevel,
                                         (DXGK_PAGE_FAULT_FLAGS)0,
                                         DXGK_GENERAL_ERROR_PAGE_FAULT);
                break;
            }
            Cmd = &VirtualCommand;
        }
        else
        {
            Cmd = (CONST SOFTGPU_CMD *)(MapVa + Offset);
        }

        if (Cmd->Magic != SOFTGPU_CMD_MAGIC ||
            Cmd->Size < sizeof(SOFTGPU_CMD) ||
            Cmd->Size > Submit->EndOffset - Offset)
        {
            Result->ResumeOffset = Offset;
            SoftGpuSetExecutionFault(Result,
                                     0,
                                     0,
                                     (DXGK_PAGE_FAULT_FLAGS)0,
                                     DXGK_GENERAL_ERROR_INVALID_INSTRUCTION);
            break;
        }
        if (Cmd->Op == SOFTGPU_CMD_OP_WAIT_FENCE)
        {
            SOFTGPU_FENCE_WAIT_RESULT WaitResult;
            ULONG FaultLevel;

            if (Cmd->FenceGpuVa == 0 ||
                (Cmd->FenceGpuVa & (sizeof(UINT64) - 1)) != 0)
            {
                Result->ResumeOffset = Offset;
                SoftGpuSetExecutionFault(Result,
                                         0,
                                         0,
                                         (DXGK_PAGE_FAULT_FLAGS)0,
                                         DXGK_GENERAL_ERROR_INVALID_INSTRUCTION);
                break;
            }
            if (Cmd->FenceGpuVa >= SOFTGPU_GPUVA_LIMIT ||
                Cmd->FenceGpuVa > SOFTGPU_GPUVA_LIMIT - sizeof(UINT64))
            {
                Result->ResumeOffset = Offset;
                SoftGpuSetExecutionFault(Result,
                                         Cmd->FenceGpuVa,
                                         SOFTGPU_GPUVA_LEVELS - 1,
                                         (DXGK_PAGE_FAULT_FLAGS)0,
                                         DXGK_GENERAL_ERROR_PAGE_FAULT);
                break;
            }
            WaitResult = SoftGpuTestFenceWait(Device,
                                              Submit,
                                              Cmd,
                                              &FaultLevel);
            if (WaitResult == SoftGpuFenceWaitInvalid)
            {
                Result->ResumeOffset = Offset;
                SoftGpuSetExecutionFault(Result,
                                         Cmd->FenceGpuVa,
                                         FaultLevel,
                                         (DXGK_PAGE_FAULT_FLAGS)0,
                                         DXGK_GENERAL_ERROR_PAGE_FAULT);
                break;
            }
            if (WaitResult == SoftGpuFenceWaitPending)
            {
                Result->ResumeOffset = Offset;
                Result->Outcome = SoftGpuExecutionPending;
                break;
            }
        }
        if (Cmd->Op == SOFTGPU_CMD_OP_SIGNAL_FENCE)
        {
            ULONG FaultLevel;

            if (Cmd->FenceGpuVa == 0 ||
                (Cmd->FenceGpuVa & (sizeof(UINT64) - 1)) != 0)
            {
                Result->ResumeOffset = Offset;
                SoftGpuSetExecutionFault(Result,
                                         0,
                                         0,
                                         (DXGK_PAGE_FAULT_FLAGS)0,
                                         DXGK_GENERAL_ERROR_INVALID_INSTRUCTION);
                break;
            }
            if (Cmd->FenceGpuVa >= SOFTGPU_GPUVA_LIMIT ||
                Cmd->FenceGpuVa > SOFTGPU_GPUVA_LIMIT - sizeof(UINT64))
            {
                Result->ResumeOffset = Offset;
                SoftGpuSetExecutionFault(Result,
                                         Cmd->FenceGpuVa,
                                         SOFTGPU_GPUVA_LEVELS - 1,
                                         DXGK_PAGE_FAULT_WRITE,
                                         DXGK_GENERAL_ERROR_PAGE_FAULT);
                break;
            }
            if (!SoftGpuStoreFenceValue(Device,
                                        &Submit->Root,
                                        Cmd->FenceGpuVa,
                                        Cmd->FenceValue,
                                        &FaultLevel))
            {
                Result->ResumeOffset = Offset;
                SoftGpuSetExecutionFault(Result,
                                         Cmd->FenceGpuVa,
                                         FaultLevel,
                                         DXGK_PAGE_FAULT_WRITE,
                                         DXGK_GENERAL_ERROR_PAGE_FAULT);
                break;
            }
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
            Result->MonitoredFenceSignaled = TRUE;
#endif
        }
        switch (Cmd->Op)
        {
            case SOFTGPU_CMD_OP_BLT:
                if (!SoftGpuDmaCommandAddressClassAllowed(
                        Submit->VirtualAddressing,
                        SoftGpuDmaAddressPhysicalSurface))
                {
                    ExecuteStatus = STATUS_NOT_SUPPORTED;
                    break;
                }
                ExecuteStatus = SoftGpuExecuteBlt(Device, Cmd);
                break;
            case SOFTGPU_CMD_OP_FILL:
                if (!SoftGpuDmaCommandAddressClassAllowed(
                        Submit->VirtualAddressing,
                        SoftGpuDmaAddressPhysicalSurface))
                {
                    ExecuteStatus = STATUS_NOT_SUPPORTED;
                    break;
                }
                ExecuteStatus = SoftGpuExecuteFill(Device, Cmd);
                break;
            case SOFTGPU_CMD_OP_PAGE:
                if (!SoftGpuDmaCommandAddressClassAllowed(
                        Submit->VirtualAddressing,
                        SoftGpuDmaAddressKernelPaging))
                {
                    ExecuteStatus = STATUS_PRIVILEGED_INSTRUCTION;
                    break;
                }
                ExecuteStatus = SoftGpuExecutePage(Device, Cmd);
                break;
            case SOFTGPU_CMD_OP_FILL_LINEAR:
                if (!SoftGpuDmaCommandAddressClassAllowed(
                        Submit->VirtualAddressing,
                        SoftGpuDmaAddressKernelPaging))
                {
                    ExecuteStatus = STATUS_PRIVILEGED_INSTRUCTION;
                    break;
                }
                ExecuteStatus = SoftGpuExecuteFillLinear(Device, Cmd);
                break;
            case SOFTGPU_CMD_OP_SIGNAL_FENCE:
            case SOFTGPU_CMD_OP_WAIT_FENCE:
            case SOFTGPU_CMD_OP_NOP:
                break;
            default:
                Result->ResumeOffset = Offset;
                SoftGpuSetExecutionFault(Result,
                                         0,
                                         0,
                                         (DXGK_PAGE_FAULT_FLAGS)0,
                                         DXGK_GENERAL_ERROR_INVALID_INSTRUCTION);
                break;
        }
        if (!NT_SUCCESS(ExecuteStatus))
        {
            Result->ResumeOffset = Offset;
            SoftGpuSetExecutionFault(
                Result,
                0,
                0,
                (DXGK_PAGE_FAULT_FLAGS)0,
                DXGK_GENERAL_ERROR_INVALID_INSTRUCTION);
        }
        if (Result->Outcome != SoftGpuExecutionCompleted)
            break;
        Offset += Cmd->Size;
    }

    if (Result->Outcome == SoftGpuExecutionCompleted &&
        Offset != Submit->EndOffset)
    {
        Result->ResumeOffset = Offset;
        SoftGpuSetExecutionFault(Result,
                                 0,
                                 0,
                                 (DXGK_PAGE_FAULT_FLAGS)0,
                                 DXGK_GENERAL_ERROR_INVALID_INSTRUCTION);
    }
    if (MapVa != NULL)
        MmUnmapIoSpace(MapVa, Submit->EndOffset);
}

VOID
NTAPI
SoftGpuDpcRoutine(
    _In_     PKDPC   Dpc,
    _In_opt_ PVOID   DeferredContext,
    _In_opt_ PVOID   SystemArgument1,
    _In_opt_ PVOID   SystemArgument2)
{
    PSOFTGPU_DEVICE              Device;
    KIRQL                        OldIrql;
    ULONG                        CompletedFence = 0;
    DXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyData;
    ULONGLONG                    Start100ns;
    ULONGLONG                    ElapsedUs;
    LONG                         TraceSeq;
    SOFTGPU_SUBMIT               Submit;
    SOFTGPU_SUBMIT               FaultedSubmit;
    SOFTGPU_EXECUTION_RESULT     FaultResult;
    ULONG                        HeadIndex;
    BOOLEAN                      HaveSubmit;
    BOOLEAN                      HaveCompletion = FALSE;
    BOOLEAN                      HaveFault = FALSE;
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
    BOOLEAN                      HaveMonitoredSignal = FALSE;
#endif

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    Device = (PSOFTGPU_DEVICE)DeferredContext;
    if (Device == NULL)
        return;

    ASSERT(Device->Magic == SOFTGPU_DEVICE_MAGIC);
    Start100ns = SoftGpuTraceNow100ns();

    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    if (Device->Stopped)
    {
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        return;
    }
    HaveSubmit = Device->EngineActive == 0;
    if (HaveSubmit)
        Device->EngineActive = 1;
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);

    if (HaveSubmit)
    {
        for (;;)
        {
            KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
            if (Device->Stopped || Device->SubmitRingHead == Device->SubmitRingTail)
            {
                Device->EngineActive = 0;
                KeReleaseSpinLock(&Device->FenceLock, OldIrql);
                break;
            }
            /* Peek the head without consuming it: a buffer that blocks on a
             * GPU wait must keep owning its slot, otherwise a producer could
             * refill it while the engine is parked.  Only this drainer moves
             * the head, and the full check keeps producers off it. */
            HeadIndex = Device->SubmitRingHead;
            Submit = Device->SubmitRing[HeadIndex % SOFTGPU_SUBMIT_RING_SIZE];
            KeReleaseSpinLock(&Device->FenceLock, OldIrql);
            {
                SOFTGPU_EXECUTION_RESULT ExecuteResult;

                SoftGpuExecuteDmaBuffer(Device, &Submit, &ExecuteResult);
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
                if (ExecuteResult.MonitoredFenceSignaled)
                    HaveMonitoredSignal = TRUE;
#endif
                KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
                if (Device->Stopped)
                {
                    Device->EngineActive = 0;
                    KeReleaseSpinLock(&Device->FenceLock, OldIrql);
                    break;
                }
                if (ExecuteResult.Outcome == SoftGpuExecutionPending)
                {
                    /* Blocked on a GPU wait: record where to resume and stop
                     * draining without completing the fence.  The refresh
                     * timer re-kicks the engine. */
                    Device->SubmitRing[HeadIndex % SOFTGPU_SUBMIT_RING_SIZE].StartOffset =
                        ExecuteResult.ResumeOffset;
                    Device->EngineActive = 0;
                    KeReleaseSpinLock(&Device->FenceLock, OldIrql);
                    break;
                }
                if (ExecuteResult.Outcome == SoftGpuExecutionFaulted)
                {
                    SOFTGPU_FAULT_RETIREMENT FaultRetirement;

                    SoftGpuFaultPolicyRetire(
                        REACTOS_WDDM_TARGET_LEVEL,
                        HeadIndex,
                        Submit.Fence,
                        Device->CompletedFence,
                        &FaultRetirement);
                    Device->SubmitRingHead =
                        FaultRetirement.NextHead;
                    Device->CompletedFence =
                        FaultRetirement.CompletedFence;
                    Device->EngineActive = 0;
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
                    /*
                     * A fault is terminal for this one packet, not a completed
                     * fence watermark. Consume its ring slot and stop so the OS
                     * can retire it with failure before later work executes.
                     */
                    ASSERT(FaultRetirement.NotifyPageFault);
                    FaultedSubmit = Submit;
                    FaultResult = ExecuteResult;
                    HaveFault = TRUE;
                    KeReleaseSpinLock(&Device->FenceLock, OldIrql);
                    break;
#else
                    /*
                     * DMA_PAGE_FAULTED does not exist below WDDM 2.0. Consume
                     * the malformed packet as the historical no-op completion
                     * and continue draining so a lower target cannot livelock.
                     */
                    ASSERT(!FaultRetirement.NotifyPageFault);
                    KeReleaseSpinLock(&Device->FenceLock, OldIrql);
                    continue;
#endif
                }
                Device->SubmitRingHead = HeadIndex + 1;
                if ((LONG)(Submit.Fence - Device->CompletedFence) > 0)
                    Device->CompletedFence = Submit.Fence;
                KeReleaseSpinLock(&Device->FenceLock, OldIrql);
            }
        }
    }

    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    if (Device->Stopped)
    {
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        return;
    }
    if (Device->CompletedFence != Device->NotifiedFence)
    {
        CompletedFence = Device->CompletedFence;
        Device->NotifiedFence = CompletedFence;
        HaveCompletion = TRUE;
    }
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);
    if (!HaveCompletion && !HaveFault
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
        && !HaveMonitoredSignal
#endif
        )
        return;

#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
    /*
     * The engine has already performed the mapped fence write.  Publish its
     * type-11 handoff before the completion/fault payload for the same
     * ordered stream; dxgkrnl evaluates the page before retiring the packet.
     */
    if (HaveMonitoredSignal &&
        Device->DxgkInterface.DxgkCbNotifyInterrupt != NULL)
    {
        KeMemoryBarrier();
        RtlZeroMemory(&NotifyData, sizeof(NotifyData));
        NotifyData.InterruptType =
            DXGK_INTERRUPT_MONITORED_FENCE_SIGNALED;
        NotifyData.MonitoredFenceSignaled.NodeOrdinal = 0;
        NotifyData.MonitoredFenceSignaled.EngineOrdinal = 0;
        Device->DxgkInterface.DxgkCbNotifyInterrupt(
            Device->DxgkInterface.DeviceHandle,
            &NotifyData);
    }
#endif

    /*
     * Preserve packet order when one DPC completed earlier work before hitting
     * a fault: publish the valid completion watermark first, then the fault.
     */
    if (HaveCompletion &&
        Device->DxgkInterface.DxgkCbNotifyInterrupt != NULL)
    {
        RtlZeroMemory(&NotifyData, sizeof(NotifyData));
        NotifyData.InterruptType =
            DXGK_INTERRUPT_TYPE_DMA_COMPLETED;
        NotifyData.DmaCompleted.SubmissionFenceId = CompletedFence;
        NotifyData.DmaCompleted.NodeOrdinal = 0;
        NotifyData.DmaCompleted.EngineOrdinal = 0;
        Device->DxgkInterface.DxgkCbNotifyInterrupt(
            Device->DxgkInterface.DeviceHandle,
            &NotifyData);
    }

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    if (HaveFault &&
        Device->DxgkInterface.DxgkCbNotifyInterrupt != NULL)
    {
        RtlZeroMemory(&NotifyData, sizeof(NotifyData));
        NotifyData.InterruptType = DXGK_INTERRUPT_DMA_PAGE_FAULTED;
        NotifyData.DmaPageFaulted.FaultedFenceId =
            FaultedSubmit.Fence;
        NotifyData.DmaPageFaulted.FaultedPrimitiveAPISequenceNumber =
            ~(UINT64)0;
        NotifyData.DmaPageFaulted.FaultedPipelineStage =
            DXGK_RENDER_PIPELINE_STAGE_UNKNOWN;
        NotifyData.DmaPageFaulted.FaultedBindTableEntry =
            ~(UINT)0;
        NotifyData.DmaPageFaulted.PageFaultFlags =
            FaultResult.PageFaultFlags;
        NotifyData.DmaPageFaulted.FaultedVirtualAddress =
            FaultResult.FaultedVirtualAddress;
        NotifyData.DmaPageFaulted.NodeOrdinal = 0;
        NotifyData.DmaPageFaulted.EngineOrdinal = 0;
        NotifyData.DmaPageFaulted.PageTableLevel =
            FaultResult.PageTableLevel;
        NotifyData.DmaPageFaulted.FaultErrorCode =
            FaultResult.FaultErrorCode;
        NotifyData.DmaPageFaulted.FaultedProcessHandle =
            FaultedSubmit.DxgkProcessHandle;
        Device->DxgkInterface.DxgkCbNotifyInterrupt(
            Device->DxgkInterface.DeviceHandle,
            &NotifyData);
    }
#endif

    /* Signal dxgkrnl only after every notification payload is published. */
    if (Device->DxgkInterface.DxgkCbNotifyDpc != NULL)
    {
        Device->DxgkInterface.DxgkCbNotifyDpc(
            Device->DxgkInterface.DeviceHandle);
    }

    ElapsedUs = SoftGpuTraceElapsedUs(Start100ns);
    TraceSeq = InterlockedIncrement(&g_SoftGpuDpcTraceCount);
    if (TraceSeq <= SOFTGPU_TRACE_LOG_LIMIT ||
        ElapsedUs >= SOFTGPU_TRACE_SLOW_US)
    {
        DPRINT("SOFTGPU: DpcRoutine seq=%ld completed=%lu faulted=%lu "
               "faultVa=0x%I64x dur=%I64u us\n",
               TraceSeq,
               CompletedFence,
               HaveFault ? FaultedSubmit.Fence : 0,
               HaveFault ? FaultResult.FaultedVirtualAddress : 0,
               ElapsedUs);
    }
}


VOID
NTAPI
SoftGpuVsyncDpcRoutine(
    _In_     PKDPC   Dpc,
    _In_opt_ PVOID   DeferredContext,
    _In_opt_ PVOID   SystemArgument1,
    _In_opt_ PVOID   SystemArgument2)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)DeferredContext;
    DXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyData;
    KIRQL OldIrql;
    BOOLEAN PhaseActive;
    BOOLEAN Deliver;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC)
        return;

    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    PhaseActive = !Device->Stopped &&
                  InterlockedCompareExchange(
                      &Device->VsyncPhaseEnabled, 0, 0) != 0;
    Deliver = PhaseActive &&
              InterlockedCompareExchange(&Device->VsyncEnabled, 0, 0) != 0 &&
              Device->DxgkInterface.DxgkCbNotifyInterrupt != NULL;
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);
    if (!PhaseActive)
        return;

    /* Re-kick the engine: a buffer parked on an unsatisfied GPU wait retries
     * on this cadence, so a fence signaled by another engine or by the CPU
     * always unblocks it. */
    KeInsertQueueDpc(&Device->DpcObject, NULL, NULL);

    if (!Deliver)
        return;

    RtlZeroMemory(&NotifyData, sizeof(NotifyData));
    NotifyData.InterruptType = DXGK_INTERRUPT_CRTC_VSYNC;
    NotifyData.CrtcVsync.VidPnTargetId = 0;
    NotifyData.CrtcVsync.PhysicalAddress = Device->FrameBufferPhys;
    NotifyData.CrtcVsync.PhysicalAdapterMask = 0;
    Device->DxgkInterface.DxgkCbNotifyInterrupt(
        Device->DxgkInterface.DeviceHandle,
        &NotifyData);
}


/* =========================================================================
 * DxgkDdiDpcRoutine  — miniport DPC forwarding
 * =========================================================================
 */

/*
 * SoftGpuDdiDpcRoutine
 *
 * Called by dxgkrnl from its own DPC when the miniport's KDPC fires.
 * On softgpu we already complete all work in SoftGpuDpcRoutine (the raw
 * KDPC); this entry point is a no-op.
 */
VOID
APIENTRY
SoftGpuDdiDpcRoutine(
    _In_ PVOID MiniportDeviceContext)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
}


/* =========================================================================
 * DxgkDdiInterruptRoutine
 * =========================================================================
 */

/*
 * SoftGpuDdiInterruptRoutine
 *
 * softgpu does not use a real interrupt line.  This routine will never be
 * invoked by the kernel; it is registered as a placeholder in case dxgkrnl
 * ever calls into it.
 *
 * Returns FALSE (interrupt not claimed) unconditionally.
 */
BOOLEAN
APIENTRY
SoftGpuDdiInterruptRoutine(
    _In_ PVOID MiniportDeviceContext,
    _In_ ULONG MessageNumber)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(MessageNumber);
    return FALSE;
}


/* =========================================================================
 * DxgkDdiSubmitCommand
 * =========================================================================
 */

/*
 * SoftGpuDdiSubmitCommand
 *
 * Records the submission fence ID and queues a DPC to "complete" it.
 * This simulates a GPU that executes all commands instantaneously.
 *
 * IRQL: DISPATCH_LEVEL (called from dxgkrnl scheduler)
 */
NTSTATUS
APIENTRY
SoftGpuDdiSubmitCommand(
    _In_ PVOID                        MiniportDeviceContext,
    _In_ CONST DXGKARG_SUBMITCOMMAND *SubmitCommand)
{
    PSOFTGPU_DEVICE     Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    PSOFTGPU_KMD_DEVICE KmdDevice;
    PSOFTGPU_PROCESS    Process = NULL;
    KIRQL               OldIrql;
    ULONGLONG           Start100ns;
    ULONGLONG           ElapsedUs;
    LONG                TraceSeq;
    BOOLEAN             Queued;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        SubmitCommand == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (SubmitCommand->NodeOrdinal != 0 || SubmitCommand->EngineOrdinal != 0)
        return STATUS_INVALID_PARAMETER;

    Start100ns = SoftGpuTraceNow100ns();

    /* The same lock serializes StopDevice's gate with DPC insertion. */
    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    if (Device->Stopped)
    {
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        return STATUS_DELETE_PENDING;
    }
    /*
     * softgpu does not advertise MultiEngineAware, so this union member is
     * the per-device handle returned by DxgkDdiCreateDevice.
     */
    KmdDevice = (PSOFTGPU_KMD_DEVICE)SubmitCommand->hDevice;
    if (KmdDevice == NULL ||
        KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC ||
        KmdDevice->Adapter != Device)
    {
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        return STATUS_INVALID_HANDLE;
    }
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    Process = KmdDevice->Process;
    if (Process == NULL ||
        Process->Magic != SOFTGPU_PROCESS_MAGIC ||
        Process->Adapter != Device)
    {
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        return STATUS_INVALID_HANDLE;
    }
#endif
    if (!SubmitCommand->Flags.NullRendering &&
        (SubmitCommand->DmaBufferPhysicalAddress.QuadPart == 0 ||
         SubmitCommand->DmaBufferSubmissionEndOffset <=
             SubmitCommand->DmaBufferSubmissionStartOffset))
    {
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        return STATUS_INVALID_PARAMETER;
    }
    if (Device->SubmitRingTail - Device->SubmitRingHead >= SOFTGPU_SUBMIT_RING_SIZE)
    {
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        return STATUS_DEVICE_BUSY;
    }
    Device->CurrentFence = SubmitCommand->SubmissionFenceId;
    {
        PSOFTGPU_SUBMIT Entry = &Device->SubmitRing[Device->SubmitRingTail % SOFTGPU_SUBMIT_RING_SIZE];

        RtlZeroMemory(Entry, sizeof(*Entry));
        Entry->DmaPhys = SubmitCommand->DmaBufferPhysicalAddress;
        Entry->StartOffset = SubmitCommand->DmaBufferSubmissionStartOffset;
        Entry->EndOffset = SubmitCommand->DmaBufferSubmissionEndOffset;
        Entry->Fence = SubmitCommand->SubmissionFenceId;
        Entry->NullRendering =
            SubmitCommand->Flags.NullRendering ? TRUE : FALSE;
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
        SoftGpuGpuVaSubmissionSnapshot(
            &Process->Root,
            Process->hDxgkProcess,
            &Entry->Root,
            &Entry->DxgkProcessHandle);
#endif
        Device->SubmitRingTail++;
    }
    Queued = KeInsertQueueDpc(&Device->DpcObject, NULL, NULL);
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);

    /*
     * Queue the DPC so completion is notified asynchronously.
     * KeInsertQueueDpc returns FALSE if the DPC is already queued; that is
     * acceptable since the DPC will fire and pick up CurrentFence.
     */
    ElapsedUs = SoftGpuTraceElapsedUs(Start100ns);
    TraceSeq = InterlockedIncrement(&g_SoftGpuSubmitTraceCount);

    if (TraceSeq <= SOFTGPU_TRACE_LOG_LIMIT ||
        ElapsedUs >= SOFTGPU_TRACE_SLOW_US)
    {
        DPRINT("SOFTGPU: SubmitCommand seq=%ld fence=%u node=%u queued=%u dur=%I64u us completed=%lu\n",
               TraceSeq,
               SubmitCommand->SubmissionFenceId,
               SubmitCommand->NodeOrdinal,
               Queued,
               ElapsedUs,
               Device->CompletedFence);
    }

    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiPreemptCommand
 * =========================================================================
 */

/*
 * SoftGpuDdiPreemptCommand
 *
 * The software queue has no atomic cancellation primitive.  Refuse preemption
 * instead of reporting a fence that no engine actually preempted.
 *
 * IRQL: DISPATCH_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiPreemptCommand(
    _In_ PVOID                         MiniportDeviceContext,
    _In_ CONST DXGKARG_PREEMPTCOMMAND *PreemptCommand)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        PreemptCommand == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (PreemptCommand->NodeOrdinal != 0 ||
        PreemptCommand->EngineOrdinal != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_NOT_SUPPORTED;
}


/* =========================================================================
 * DxgkDdiRender
 * =========================================================================
 */

static NTSTATUS
SoftGpuResolveDmaOwner(
    _In_ PVOID Handle,
    _Out_ PSOFTGPU_KMD_DEVICE *KmdDeviceOut,
    _Out_ PSOFTGPU_DEVICE *DeviceOut)
{
    PSOFTGPU_CONTEXT Context;
    PSOFTGPU_KMD_DEVICE KmdDevice;
    PSOFTGPU_DEVICE Device;

    if (KmdDeviceOut == NULL || DeviceOut == NULL)
        return STATUS_INVALID_PARAMETER;
    *KmdDeviceOut = NULL;
    *DeviceOut = NULL;
    if (Handle == NULL)
        return STATUS_INVALID_HANDLE;

    Context = (PSOFTGPU_CONTEXT)Handle;
    if (Context->Magic == SOFTGPU_CONTEXT_MAGIC)
    {
        KmdDevice = Context->Device;
        if (KmdDevice == NULL ||
            KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC)
        {
            return STATUS_INVALID_HANDLE;
        }
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
        if (Context->Process == NULL ||
            Context->Process != KmdDevice->Process)
        {
            return STATUS_INVALID_HANDLE;
        }
#endif
    }
    else
    {
        /*
         * The WDDM Render/Present contract permits dxgkrnl to pass hDevice
         * when no context was created. softgpu normally creates contexts, but
         * the present path also legitimately uses this fallback.
         */
        KmdDevice = (PSOFTGPU_KMD_DEVICE)Handle;
        if (KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC)
            return STATUS_INVALID_HANDLE;
    }

    Device = KmdDevice->Adapter;
    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC)
        return STATUS_INVALID_HANDLE;
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    if (KmdDevice->Process == NULL ||
        KmdDevice->Process->Magic != SOFTGPU_PROCESS_MAGIC ||
        KmdDevice->Process->Adapter != Device)
    {
        return STATUS_INVALID_HANDLE;
    }
#endif

    *KmdDeviceOut = KmdDevice;
    *DeviceOut = Device;
    return STATUS_SUCCESS;
}

static NTSTATUS
SoftGpuValidateSurfaceOpen(
    _In_ PSOFTGPU_KMD_DEVICE KmdDevice,
    _In_ PSOFTGPU_OPENALLOC Open)
{
    ULONGLONG RequiredSize;

    if (KmdDevice == NULL ||
        Open == NULL ||
        Open->Magic != SOFTGPU_OPENALLOC_MAGIC ||
        Open->Device != KmdDevice)
    {
        return STATUS_INVALID_HANDLE;
    }
    if (Open->Size == 0 ||
        Open->Width == 0 ||
        Open->Height == 0 ||
        Open->Width > SOFTGPU_MAX_DISPLAY_WIDTH ||
        Open->Height > SOFTGPU_MAX_DISPLAY_HEIGHT ||
        Open->Width > MAXULONG / SOFTGPU_DISPLAY_BYTES_PER_PIXEL ||
        Open->Pitch <
            Open->Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL ||
        Open->Pitch > SOFTGPU_MAX_DISPLAY_PITCH ||
        (Open->Pitch % SOFTGPU_DISPLAY_BYTES_PER_PIXEL) != 0 ||
        (Open->Format != D3DDDIFMT_X8R8G8B8 &&
         Open->Format != D3DDDIFMT_A8R8G8B8))
    {
        return STATUS_NOT_SUPPORTED;
    }

    RequiredSize = (ULONGLONG)Open->Pitch * Open->Height;
    if (RequiredSize == 0 ||
        RequiredSize > (ULONGLONG)MAXULONG_PTR ||
        Open->Size != (SIZE_T)RequiredSize)
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
SoftGpuResolveRenderPlacement(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ CONST DXGK_ALLOCATIONLIST *Allocation,
    _In_ CONST D3DDDI_PATCHLOCATIONLIST *Patch,
    _In_ PSOFTGPU_OPENALLOC Open,
    _Out_ PULONGLONG Address)
{
    SIZE_T SlabOffset;
    SIZE_T Remaining;

    if (Device == NULL || Allocation == NULL || Patch == NULL ||
        Open == NULL || Address == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *Address = 0;
    if (Patch->AllocationOffset >= Open->Size)
        return STATUS_INVALID_PARAMETER;
    if (Allocation->SegmentId == 0)
        return STATUS_SUCCESS;
    if (Allocation->SegmentId != SOFTGPU_SEGMENT_ID ||
        Allocation->PhysicalAddress.QuadPart <= 0 ||
        (ULONGLONG)Allocation->PhysicalAddress.QuadPart >
            MAXULONGLONG - Patch->AllocationOffset)
    {
        return STATUS_INVALID_ADDRESS;
    }

    *Address =
        (ULONGLONG)Allocation->PhysicalAddress.QuadPart +
        Patch->AllocationOffset;
    Remaining = Open->Size - Patch->AllocationOffset;
    return SoftGpu2dResolveSlabRange(
               (ULONGLONG)Device->FrameBufferPhys.QuadPart,
               Device->FrameBufferSize,
               *Address,
               Remaining,
               &SlabOffset);
}

static NTSTATUS
SoftGpuValidateRenderSurfaceReference(
    _In_ PSOFTGPU_KMD_DEVICE KmdDevice,
    _In_ CONST DXGKARG_RENDER *Render,
    _In_ UINT RequiredPatchOffset,
    _In_ CONST RECT *Rect,
    _In_ ULONG Pitch,
    _In_ BOOLEAN WriteOperation)
{
    CONST D3DDDI_PATCHLOCATIONLIST *Patch = NULL;
    CONST DXGK_ALLOCATIONLIST *Allocation;
    PSOFTGPU_OPENALLOC Open;
    SIZE_T Offset;
    SIZE_T RowBytes;
    SIZE_T Span;
    UINT Index;
    NTSTATUS Status;

    for (Index = 0; Index < Render->PatchLocationListInSize; ++Index)
    {
        if (Render->pPatchLocationListIn[Index].PatchOffset !=
                RequiredPatchOffset)
        {
            continue;
        }
        if (Patch != NULL)
            return STATUS_INVALID_PARAMETER;
        Patch = &Render->pPatchLocationListIn[Index];
    }
    if (Patch == NULL || Patch->AllocationIndex >= Render->AllocationListSize)
        return STATUS_INVALID_PARAMETER;

    Allocation = &Render->pAllocationList[Patch->AllocationIndex];
    Open = (PSOFTGPU_OPENALLOC)Allocation->hDeviceSpecificAllocation;
    Status = SoftGpuValidateSurfaceOpen(KmdDevice, Open);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Patch->AllocationOffset != 0 ||
        (WriteOperation && !Allocation->WriteOperation) ||
        Rect == NULL ||
        Rect->left < 0 ||
        Rect->top < 0 ||
        Rect->right > (LONG)Open->Width ||
        Rect->bottom > (LONG)Open->Height ||
        Pitch != Open->Pitch)
    {
        return STATUS_INVALID_PARAMETER;
    }

    return SoftGpu2dRectRange(Open->Size,
                              Pitch,
                              Rect,
                              &Offset,
                              &RowBytes,
                              &Span);
}

/*
 * SoftGpuDdiRender
 *
 * Validates and translates the user command stream into the device's fixed
 * DMA record format. Every surface reference must name an allocation opened
 * on this KMD device and carrying the explicit linear-surface contract.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiRender(
    _In_    PVOID           hContext,
    _Inout_ DXGKARG_RENDER *pRender)
{
    CONST UCHAR *Command;
    PSOFTGPU_KMD_DEVICE KmdDevice;
    PSOFTGPU_DEVICE Device;
    UINT CopyLength;
    UINT Offset;
    UINT Records = 0;
    UINT ExpectedPatches = 0;
    UINT i;
    NTSTATUS Status;

    Status = SoftGpuResolveDmaOwner(hContext, &KmdDevice, &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    if (pRender == NULL ||
        pRender->pCommand == NULL ||
        pRender->pDmaBuffer == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pRender->CommandLength == 0 || pRender->CommandLength > pRender->DmaSize)
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    if (pRender->PatchLocationListInSize > pRender->PatchLocationListOutSize)
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    if (pRender->PatchLocationListInSize != 0 &&
        (pRender->pPatchLocationListIn == NULL || pRender->pPatchLocationListOut == NULL))
        return STATUS_INVALID_PARAMETER;
    if (pRender->PatchLocationListInSize != 0 &&
        (pRender->pAllocationList == NULL ||
         pRender->AllocationListSize == 0))
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (i = 0; i < pRender->PatchLocationListInSize; ++i)
    {
        CONST D3DDDI_PATCHLOCATIONLIST *Patch =
            &pRender->pPatchLocationListIn[i];
        CONST DXGK_ALLOCATIONLIST *Allocation;
        PSOFTGPU_OPENALLOC Open;
        UINT RecordOffset;
        ULONGLONG Placement;

        if (Patch->PatchOffset > pRender->CommandLength ||
            sizeof(ULONGLONG) >
                pRender->CommandLength - Patch->PatchOffset)
        {
            return STATUS_INVALID_USER_BUFFER;
        }
        RecordOffset = Patch->PatchOffset % sizeof(SOFTGPU_CMD);
        if (RecordOffset != FIELD_OFFSET(SOFTGPU_CMD, SrcAddress) &&
            RecordOffset != FIELD_OFFSET(SOFTGPU_CMD, DstAddress))
        {
            return STATUS_INVALID_PARAMETER;
        }
        if (Patch->AllocationIndex >= pRender->AllocationListSize ||
            Patch->AllocationOffset != 0)
        {
            return STATUS_INVALID_PARAMETER;
        }

        Allocation =
            &pRender->pAllocationList[Patch->AllocationIndex];
        Open = (PSOFTGPU_OPENALLOC)
            Allocation->hDeviceSpecificAllocation;
        Status = SoftGpuValidateSurfaceOpen(KmdDevice, Open);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = SoftGpuResolveRenderPlacement(Device,
                                               Allocation,
                                               Patch,
                                               Open,
                                               &Placement);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    /*
     * Validate the SOFTGPU_CMD stream before it becomes a DMA buffer: the
     * record chain must tile the command exactly, every opcode must be one
     * this engine implements, and every patch location must land inside a
     * record's address field.  A stream that fails here is rejected instead
     * of being handed to the engine to skip at execution time.
     */
    Command = (CONST UCHAR *)pRender->pCommand;
    for (Offset = 0; Offset < pRender->CommandLength; Records++)
    {
        CONST SOFTGPU_CMD *Cmd;

        if (pRender->CommandLength - Offset < sizeof(SOFTGPU_CMD))
            return STATUS_INVALID_PARAMETER;
        Cmd = (CONST SOFTGPU_CMD *)(Command + Offset);
        if (Cmd->Magic != SOFTGPU_CMD_MAGIC || Cmd->Size != sizeof(SOFTGPU_CMD))
            return STATUS_INVALID_PARAMETER;
        switch (Cmd->Op)
        {
            case SOFTGPU_CMD_OP_NOP:
                break;

            case SOFTGPU_CMD_OP_BLT:
                if (ExpectedPatches > MAXULONG - 2)
                    return STATUS_INTEGER_OVERFLOW;
                Status = SoftGpuValidateRenderSurfaceReference(
                             KmdDevice,
                             pRender,
                             Offset +
                                 FIELD_OFFSET(SOFTGPU_CMD, SrcAddress),
                             &Cmd->SrcRect,
                             Cmd->SrcPitch,
                             FALSE);
                if (!NT_SUCCESS(Status))
                    return Status;
                Status = SoftGpuValidateRenderSurfaceReference(
                             KmdDevice,
                             pRender,
                             Offset +
                                 FIELD_OFFSET(SOFTGPU_CMD, DstAddress),
                             &Cmd->DstRect,
                             Cmd->DstPitch,
                             TRUE);
                if (!NT_SUCCESS(Status))
                    return Status;
                if (Cmd->SrcRect.right - Cmd->SrcRect.left !=
                        Cmd->DstRect.right - Cmd->DstRect.left ||
                    Cmd->SrcRect.bottom - Cmd->SrcRect.top !=
                        Cmd->DstRect.bottom - Cmd->DstRect.top)
                {
                    return STATUS_INVALID_PARAMETER;
                }
                ExpectedPatches += 2;
                break;

            case SOFTGPU_CMD_OP_FILL:
                if (ExpectedPatches == MAXULONG)
                    return STATUS_INTEGER_OVERFLOW;
                Status = SoftGpuValidateRenderSurfaceReference(
                             KmdDevice,
                             pRender,
                             Offset +
                                 FIELD_OFFSET(SOFTGPU_CMD, DstAddress),
                             &Cmd->DstRect,
                             Cmd->DstPitch,
                             TRUE);
                if (!NT_SUCCESS(Status))
                    return Status;
                ExpectedPatches++;
                break;

            case SOFTGPU_CMD_OP_SIGNAL_FENCE:
            case SOFTGPU_CMD_OP_WAIT_FENCE:
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
                /* GPU synchronization against a monitored fence the caller
                 * mapped into its own GPU address space; the engine resolves
                 * the address through that process's page tables, so an
                 * unmapped or foreign fence simply cannot be reached. */
                if (Cmd->FenceGpuVa == 0 || (Cmd->FenceGpuVa & (sizeof(ULONGLONG) - 1)) != 0)
                    return STATUS_INVALID_PARAMETER;
                break;
#else
                return STATUS_ILLEGAL_INSTRUCTION;
#endif

            default:
                /* Paging opcodes are KMD-generated and are not accepted from
                 * a user-mode command stream. */
                return STATUS_INVALID_PARAMETER;
        }
        Offset += Cmd->Size;
    }
    if (Offset != pRender->CommandLength || Records == 0)
        return STATUS_INVALID_PARAMETER;
    if (ExpectedPatches != pRender->PatchLocationListInSize)
        return STATUS_INVALID_PARAMETER;

    CopyLength = pRender->CommandLength;
    RtlCopyMemory(pRender->pDmaBuffer, pRender->pCommand, CopyLength);
    for (i = 0; i < pRender->PatchLocationListInSize; ++i)
    {
        CONST D3DDDI_PATCHLOCATIONLIST *Patch =
            &pRender->pPatchLocationListIn[i];
        D3DDDI_PATCHLOCATIONLIST *PatchOut =
            &pRender->pPatchLocationListOut[i];
        CONST DXGK_ALLOCATIONLIST *Allocation =
            &pRender->pAllocationList[Patch->AllocationIndex];
        PSOFTGPU_OPENALLOC Open =
            (PSOFTGPU_OPENALLOC)
                Allocation->hDeviceSpecificAllocation;
        ULONGLONG Placement;

        RtlZeroMemory(PatchOut, sizeof(*PatchOut));
        PatchOut->AllocationIndex = Patch->AllocationIndex;
        PatchOut->AllocationOffset = Patch->AllocationOffset;
        PatchOut->PatchOffset = Patch->PatchOffset;

        /*
         * Pre-patch every resident allocation as required by DxgkDdiRender.
         * A paged-out entry is deliberately written as zero; dxgkrnl will call
         * Patch after choosing its submission placement.
         */
        Status = SoftGpuResolveRenderPlacement(Device,
                                               Allocation,
                                               Patch,
                                               Open,
                                               &Placement);
        ASSERT(NT_SUCCESS(Status));
        *(ULONGLONG UNALIGNED *)
            ((PUCHAR)pRender->pDmaBuffer + Patch->PatchOffset) =
                Placement;
    }

    pRender->pDmaBuffer = (PUCHAR)pRender->pDmaBuffer + CopyLength;
    pRender->pPatchLocationListOut += pRender->PatchLocationListInSize;
    pRender->MultipassOffset = 0;

    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiBuildPagingBuffer
 * =========================================================================
 */

static PSOFTGPU_CMD
SoftGpuBeginPagingCommand(
    _Inout_ PDXGKARG_BUILDPAGINGBUFFER BuildPagingBuffer)
{
    PSOFTGPU_CMD Cmd;

    if (BuildPagingBuffer->pDmaBuffer == NULL || BuildPagingBuffer->DmaSize < sizeof(SOFTGPU_CMD))
        return NULL;
    Cmd = (PSOFTGPU_CMD)BuildPagingBuffer->pDmaBuffer;
    RtlZeroMemory(Cmd, sizeof(*Cmd));
    Cmd->Magic = SOFTGPU_CMD_MAGIC;
    Cmd->Size = sizeof(*Cmd);
    Cmd->Op = SOFTGPU_CMD_OP_NOP;
    return Cmd;
}

static VOID
SoftGpuEndPagingCommand(
    _Inout_ PDXGKARG_BUILDPAGINGBUFFER BuildPagingBuffer)
{
    BuildPagingBuffer->pDmaBuffer = (PUCHAR)BuildPagingBuffer->pDmaBuffer + sizeof(SOFTGPU_CMD);
    BuildPagingBuffer->MultipassOffset = 0;
}

/*
 * SoftGpuDdiBuildPagingBuffer
 *
 * Describes each supported paging operation as one SOFTGPU_CMD record the
 * engine executes at submission time.  The framebuffer slab is the memory
 * segment, so a transfer is a linear move between a slab physical address and
 * the kernel mapping of the backing MDL dxgkrnl supplied.  Operations this
 * software device genuinely has nothing to execute for (aperture mapping,
 * discard, residency notification) still emit an ordered no-op record so the
 * packet retires through the normal fence path rather than reporting false
 * completion without a packet.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiBuildPagingBuffer(
    _In_    PVOID                      MiniportDeviceContext,
    _Inout_ PDXGKARG_BUILDPAGINGBUFFER BuildPagingBuffer)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    PSOFTGPU_CMD Cmd;
    ULONGLONG SlabBase;
    ULONGLONG SlabOffset;
    PVOID SystemVa;
    PMDL Mdl;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC || BuildPagingBuffer == NULL)
        return STATUS_INVALID_PARAMETER;

    SlabBase = (ULONGLONG)Device->FrameBufferPhys.QuadPart;

    switch (BuildPagingBuffer->Operation)
    {
        case DXGK_OPERATION_TRANSFER:
        {
            CONST DXGK_BUILDPAGINGBUFFER_TRANSFER *Transfer = &BuildPagingBuffer->Transfer;
            BOOLEAN ToSlab;

            if (Transfer->TransferSize == 0)
                return STATUS_INVALID_PARAMETER;
            if (Transfer->Source.SegmentId == 0 && Transfer->Destination.SegmentId == SOFTGPU_SEGMENT_ID)
            {
                ToSlab = TRUE;
                Mdl = Transfer->Source.pMdl;
                SlabOffset = (ULONGLONG)Transfer->Destination.SegmentAddress.QuadPart;
            }
            else if (Transfer->Source.SegmentId == SOFTGPU_SEGMENT_ID && Transfer->Destination.SegmentId == 0)
            {
                ToSlab = FALSE;
                Mdl = Transfer->Destination.pMdl;
                SlabOffset = (ULONGLONG)Transfer->Source.SegmentAddress.QuadPart;
            }
            else
            {
                return STATUS_NOT_SUPPORTED;
            }
            if (Mdl == NULL)
                return STATUS_INVALID_PARAMETER;
            if (SlabOffset + Transfer->TransferSize > Device->FrameBufferSize)
                return STATUS_INVALID_PARAMETER;

            SystemVa = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
            if (SystemVa == NULL)
                return STATUS_INSUFFICIENT_RESOURCES;

            Cmd = SoftGpuBeginPagingCommand(BuildPagingBuffer);
            if (Cmd == NULL)
                return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
            Cmd->Op = SOFTGPU_CMD_OP_PAGE;
            Cmd->Flags = ToSlab ? SOFTGPU_CMD_FLAG_TO_SLAB : 0;
            Cmd->SlabAddress = SlabBase + SlabOffset;
            Cmd->SystemAddress = (ULONGLONG)(ULONG_PTR)SystemVa + Transfer->MdlOffset;
            Cmd->ByteCount = Transfer->TransferSize;
            SoftGpuEndPagingCommand(BuildPagingBuffer);
            return STATUS_SUCCESS;
        }

        case DXGK_OPERATION_FILL:
        {
            CONST DXGK_BUILDPAGINGBUFFER_FILL *Fill = &BuildPagingBuffer->Fill;

            if (Fill->Destination.SegmentId != SOFTGPU_SEGMENT_ID || Fill->FillSize == 0)
                return STATUS_NOT_SUPPORTED;
            SlabOffset = (ULONGLONG)Fill->Destination.SegmentAddress.QuadPart;
            if (SlabOffset + Fill->FillSize > Device->FrameBufferSize)
                return STATUS_INVALID_PARAMETER;

            Cmd = SoftGpuBeginPagingCommand(BuildPagingBuffer);
            if (Cmd == NULL)
                return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
            Cmd->Op = SOFTGPU_CMD_OP_FILL_LINEAR;
            Cmd->SlabAddress = SlabBase + SlabOffset;
            Cmd->ByteCount = Fill->FillSize;
            Cmd->Color = Fill->FillPattern;
            SoftGpuEndPagingCommand(BuildPagingBuffer);
            return STATUS_SUCCESS;
        }

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
        case DXGK_OPERATION_UPDATE_PAGE_TABLE:
        {
            /*
             * CPU_VIRTUAL mode: dxgkrnl already wrote the generic DXGK_PTE
             * descriptors into the table this device handed it, and this
             * device's translation reads exactly that format, so the update
             * needs validation rather than a format conversion.
             */
            CONST DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *Update = &BuildPagingBuffer->UpdatePageTable;

            if (Update->UpdateMode != DXGK_PAGETABLEUPDATE_CPU_VIRTUAL)
                return STATUS_NOT_SUPPORTED;
            if (Update->PageTableLevel >= SOFTGPU_GPUVA_LEVELS)
                return STATUS_INVALID_PARAMETER;
            if (Update->pPageTableEntries == NULL || Update->NumPageTableEntries == 0)
                return STATUS_INVALID_PARAMETER;
            if (Update->StartIndex >= (1u << SOFTGPU_GPUVA_INDEX_BITS) ||
                Update->NumPageTableEntries > (1u << SOFTGPU_GPUVA_INDEX_BITS) - Update->StartIndex)
                return STATUS_INVALID_PARAMETER;
            if (Update->PageTableAddress.CpuVirtual == NULL)
                return STATUS_INVALID_PARAMETER;
            Cmd = SoftGpuBeginPagingCommand(BuildPagingBuffer);
            if (Cmd == NULL)
                return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
            SoftGpuEndPagingCommand(BuildPagingBuffer);
            return STATUS_SUCCESS;
        }

        case DXGK_OPERATION_FLUSH_TLB:
        {
            /* No translation cache exists behind the CPU-written tables, so
             * the invalidation is ordered rather than executed. */
            CONST DXGK_BUILDPAGINGBUFFER_FLUSHTLB *Flush = &BuildPagingBuffer->FlushTlb;

            if (Flush->EndVirtualAddress < Flush->StartVirtualAddress)
                return STATUS_INVALID_PARAMETER;
            Cmd = SoftGpuBeginPagingCommand(BuildPagingBuffer);
            if (Cmd == NULL)
                return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
            SoftGpuEndPagingCommand(BuildPagingBuffer);
            return STATUS_SUCCESS;
        }

        case DXGK_OPERATION_NOTIFY_RESIDENCY:
#endif
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
        case DXGK_OPERATION_SIGNAL_MONITORED_FENCE:
        {
            CONST DXGK_BUILDPAGINGBUFFER_SIGNALMONITOREDFENCE *Signal =
                &BuildPagingBuffer->SignalMonitoredFence;

            if (Signal->MonitoredFenceGpuVa == 0 ||
                (Signal->MonitoredFenceGpuVa &
                 (sizeof(UINT64) - 1)) != 0)
            {
                return STATUS_INVALID_PARAMETER;
            }
            if (Signal->MonitoredFenceGpuVa >=
                    SOFTGPU_GPUVA_LIMIT ||
                Signal->MonitoredFenceGpuVa >
                    SOFTGPU_GPUVA_LIMIT - sizeof(UINT64))
            {
                return STATUS_INVALID_PARAMETER;
            }

            Cmd = SoftGpuBeginPagingCommand(BuildPagingBuffer);
            if (Cmd == NULL)
                return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
            Cmd->Op = SOFTGPU_CMD_OP_SIGNAL_FENCE;
            Cmd->FenceGpuVa =
                Signal->MonitoredFenceGpuVa;
            Cmd->FenceValue =
                Signal->MonitoredFenceValue;
            SoftGpuEndPagingCommand(BuildPagingBuffer);
            return STATUS_SUCCESS;
        }
#endif
        case DXGK_OPERATION_DISCARD_CONTENT:
            Cmd = SoftGpuBeginPagingCommand(BuildPagingBuffer);
            if (Cmd == NULL)
                return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
            SoftGpuEndPagingCommand(BuildPagingBuffer);
            return STATUS_SUCCESS;

        default:
            return STATUS_NOT_SUPPORTED;
    }
}


/* =========================================================================
 * DxgkDdiQueryCurrentFence
 * =========================================================================
 */

/*
 * SoftGpuDdiQueryCurrentFence
 *
 * Reports the most recently completed GPU fence to dxgkrnl.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiQueryCurrentFence(
    _In_    PVOID                       MiniportDeviceContext,
    _Inout_ PDXGKARG_QUERYCURRENTFENCE  pCurrentFence)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    KIRQL           OldIrql;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        pCurrentFence == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    pCurrentFence->CurrentFence = Device->CompletedFence;
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);

    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiPatch
 * =========================================================================
 */

/*
 * SoftGpuDdiPatch
 *
 * Writes the absolute physical placement of each referenced allocation into
 * the DMA buffer at the recorded patch offsets.
 *
 * IRQL: PASSIVE_LEVEL or DISPATCH_LEVEL (called from dxgkrnl scheduler)
 */
NTSTATUS
APIENTRY
SoftGpuDdiPatch(
    _In_ PVOID                   MiniportDeviceContext,
    _In_ CONST DXGKARG_PATCH    *Patch)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    PSOFTGPU_KMD_DEVICE KmdDevice;
    UINT i;

    if (Device == NULL ||
        Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        Patch == NULL)
        return STATUS_INVALID_PARAMETER;
    KmdDevice = (PSOFTGPU_KMD_DEVICE)Patch->hDevice;
    if (KmdDevice == NULL ||
        KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC ||
        KmdDevice->Adapter != Device)
    {
        return STATUS_INVALID_HANDLE;
    }
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    if (KmdDevice->Process == NULL ||
        KmdDevice->Process->Magic != SOFTGPU_PROCESS_MAGIC ||
        KmdDevice->Process->Adapter != Device)
    {
        return STATUS_INVALID_HANDLE;
    }
#endif
    if (Patch->PatchLocationListSubmissionLength == 0)
        return STATUS_SUCCESS;
    if (Patch->pPatchLocationList == NULL || Patch->pDmaBuffer == NULL ||
        Patch->pAllocationList == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Patch->DmaBufferSubmissionStartOffset >
            Patch->DmaBufferSubmissionEndOffset ||
        Patch->DmaBufferSubmissionEndOffset > Patch->DmaBufferSize ||
        Patch->PatchLocationListSubmissionStart >
            Patch->PatchLocationListSize ||
        Patch->PatchLocationListSubmissionLength >
            Patch->PatchLocationListSize -
                Patch->PatchLocationListSubmissionStart)
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (i = Patch->PatchLocationListSubmissionStart;
         i < Patch->PatchLocationListSubmissionStart + Patch->PatchLocationListSubmissionLength;
         i++)
    {
        CONST D3DDDI_PATCHLOCATIONLIST *Entry = &Patch->pPatchLocationList[i];
        CONST DXGK_ALLOCATIONLIST *Allocation;
        PSOFTGPU_OPENALLOC Open;
        ULONGLONG Address;
        NTSTATUS Status;

        if (Entry->AllocationIndex >= Patch->AllocationListSize)
            return STATUS_INVALID_PARAMETER;
        if (Entry->PatchOffset < Patch->DmaBufferSubmissionStartOffset ||
            Entry->PatchOffset > Patch->DmaBufferSubmissionEndOffset ||
            sizeof(ULONGLONG) >
                Patch->DmaBufferSubmissionEndOffset - Entry->PatchOffset)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Allocation = &Patch->pAllocationList[Entry->AllocationIndex];
        Open = (PSOFTGPU_OPENALLOC)
            Allocation->hDeviceSpecificAllocation;
        Status = SoftGpuValidateSurfaceOpen(KmdDevice, Open);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Allocation->SegmentId == 0)
            return STATUS_GRAPHICS_ALLOCATION_BUSY;
        Status = SoftGpuResolveRenderPlacement(Device,
                                               Allocation,
                                               Entry,
                                               Open,
                                               &Address);
        if (!NT_SUCCESS(Status))
            return Status;
        *(ULONGLONG UNALIGNED *)((PUCHAR)Patch->pDmaBuffer + Entry->PatchOffset) =
            Address;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
SoftGpuPresentSurfacePitch(
    _In_ PSOFTGPU_KMD_DEVICE KmdDevice,
    _In_ const DXGK_ALLOCATIONLIST *AllocationList,
    _In_ UINT AllocationIndex,
    _In_ const RECT *Rect,
    _Out_ PULONG Pitch)
{
    PSOFTGPU_OPENALLOC Open;
    NTSTATUS Status;

    if (KmdDevice == NULL ||
        AllocationList == NULL ||
        Rect == NULL ||
        Pitch == NULL ||
        Rect->left < 0 || Rect->top < 0 ||
        Rect->left >= Rect->right || Rect->top >= Rect->bottom)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Open = (PSOFTGPU_OPENALLOC)
        AllocationList[AllocationIndex].hDeviceSpecificAllocation;
    Status = SoftGpuValidateSurfaceOpen(KmdDevice, Open);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Rect->right > (LONG)Open->Width ||
        Rect->bottom > (LONG)Open->Height)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *Pitch = Open->Pitch;
    return STATUS_SUCCESS;
}

/*
 * Build one command per destination sub-rectangle. Source coordinates are
 * translated by the source/destination rectangle origins, and pitches come
 * from the allocation metadata produced by the standard-allocation callback.
 * Destination-less blits and flips are ordered no-ops in the command stream.
 * dxgkrnl programs a flip source through SetVidPnSourceAddress only after the
 * submitted fence retires.
 */
NTSTATUS
APIENTRY
SoftGpuDdiPresent(
    _In_    PVOID            hContext,
    _Inout_ DXGKARG_PRESENT *pPresent)
{
    PSOFTGPU_KMD_DEVICE KmdDevice;
    PSOFTGPU_DEVICE Device;
    PSOFTGPU_CMD Commands;
    PSOFTGPU_OPENALLOC SourceOpen;
    PSOFTGPU_OPENALLOC DestinationOpen;
    SOFTGPU_2D_PRESENT_POLICY PresentPolicy;
    SIZE_T CommandBytes;
    UINT CommandCount;
    UINT PatchesPerCommand;
    UINT PatchesNeeded;
    UINT CommandIndex;
    ULONG SourcePitch;
    ULONG DestinationPitch;
    LONG SourceWidth;
    LONG SourceHeight;
    LONG DestinationWidth;
    LONG DestinationHeight;
    NTSTATUS OwnerStatus;

    OwnerStatus = SoftGpuResolveDmaOwner(hContext,
                                         &KmdDevice,
                                         &Device);
    if (!NT_SUCCESS(OwnerStatus))
        return OwnerStatus;
    if (Device->Stopped)
        return STATUS_DELETE_PENDING;
    if (pPresent == NULL || pPresent->pDmaBuffer == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pPresent->DmaSize < sizeof(SOFTGPU_CMD))
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    if (pPresent->Flags.Flip && pPresent->FlipInterval > D3DDDI_FLIPINTERVAL_FOUR)
        return STATUS_INVALID_PARAMETER;

    PresentPolicy = SoftGpu2dPresentEvaluate(
                        pPresent->Flags.Flip,
                        pPresent->Flags.Blt,
                        pPresent->Flags.ColorFill,
                        pPresent->NumSrcAllocations,
                        pPresent->NumDstAllocations);
    if (PresentPolicy == SoftGpu2dPresentInvalid)
        return STATUS_INVALID_PARAMETER;
    if ((pPresent->NumSrcAllocations != 0 ||
         pPresent->NumDstAllocations != 0) &&
        pPresent->pAllocationList == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    PatchesPerCommand =
        PresentPolicy == SoftGpu2dPresentBlt ? 2 :
        PresentPolicy == SoftGpu2dPresentFill ? 1 : 0;
    if (PresentPolicy == SoftGpu2dPresentNoOp &&
        pPresent->NumSrcAllocations == 1)
    {
        SourceOpen = (PSOFTGPU_OPENALLOC)
            pPresent->pAllocationList[DXGK_PRESENT_SOURCE_INDEX]
                .hDeviceSpecificAllocation;
        OwnerStatus =
            SoftGpuValidateSurfaceOpen(KmdDevice, SourceOpen);
        if (!NT_SUCCESS(OwnerStatus))
            return OwnerStatus;
    }

    CommandCount = PatchesPerCommand != 0 ?
                       pPresent->SubRectCnt :
                       1;
    if (CommandCount == 0 ||
        (PatchesPerCommand != 0 && pPresent->pDstSubRects == NULL) ||
        CommandCount > pPresent->DmaSize / sizeof(SOFTGPU_CMD))
    {
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    }
    CommandBytes = (SIZE_T)CommandCount * sizeof(SOFTGPU_CMD);

    if (PatchesPerCommand != 0 &&
        CommandCount > MAXULONG / PatchesPerCommand)
    {
        return STATUS_INTEGER_OVERFLOW;
    }
    PatchesNeeded = CommandCount * PatchesPerCommand;
    if (PatchesNeeded != 0 &&
        (pPresent->pPatchLocationListOut == NULL ||
         pPresent->PatchLocationListOutSize < PatchesNeeded))
    {
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    }

    Commands = (PSOFTGPU_CMD)pPresent->pDmaBuffer;
    RtlZeroMemory(Commands, CommandBytes);
    if (PatchesNeeded != 0)
    {
        RtlZeroMemory(
            pPresent->pPatchLocationListOut,
            (SIZE_T)PatchesNeeded *
                sizeof(*pPresent->pPatchLocationListOut));
    }

    if (PatchesNeeded != 0)
    {
        if (pPresent->pAllocationList == NULL ||
            pPresent->SrcRect.left < 0 ||
            pPresent->SrcRect.top < 0 ||
            pPresent->DstRect.left < 0 ||
            pPresent->DstRect.top < 0 ||
            pPresent->SrcRect.left >= pPresent->SrcRect.right ||
            pPresent->SrcRect.top >= pPresent->SrcRect.bottom ||
            pPresent->DstRect.left >= pPresent->DstRect.right ||
            pPresent->DstRect.top >= pPresent->DstRect.bottom)
        {
            return STATUS_INVALID_PARAMETER;
        }

        SourceWidth =
            pPresent->SrcRect.right - pPresent->SrcRect.left;
        SourceHeight =
            pPresent->SrcRect.bottom - pPresent->SrcRect.top;
        DestinationWidth =
            pPresent->DstRect.right - pPresent->DstRect.left;
        DestinationHeight =
            pPresent->DstRect.bottom - pPresent->DstRect.top;
        if (pPresent->Flags.Blt &&
            (SourceWidth != DestinationWidth ||
             SourceHeight != DestinationHeight))
        {
            return STATUS_NOT_SUPPORTED;
        }

        DestinationOpen = (PSOFTGPU_OPENALLOC)
            pPresent->pAllocationList[DXGK_PRESENT_DESTINATION_INDEX]
                .hDeviceSpecificAllocation;
        OwnerStatus =
            SoftGpuValidateSurfaceOpen(KmdDevice, DestinationOpen);
        if (!NT_SUCCESS(OwnerStatus))
            return OwnerStatus;
        SourceOpen = NULL;
        if (pPresent->Flags.Blt)
        {
            SourceOpen = (PSOFTGPU_OPENALLOC)
                pPresent->pAllocationList[DXGK_PRESENT_SOURCE_INDEX]
                    .hDeviceSpecificAllocation;
            OwnerStatus =
                SoftGpuValidateSurfaceOpen(KmdDevice, SourceOpen);
            if (!NT_SUCCESS(OwnerStatus))
                return OwnerStatus;
        }

        for (CommandIndex = 0;
             CommandIndex < CommandCount;
             ++CommandIndex)
        {
            PSOFTGPU_CMD Cmd = &Commands[CommandIndex];
            RECT DestinationRect = pPresent->pDstSubRects[CommandIndex];
            NTSTATUS Status;

            if (DestinationRect.left < pPresent->DstRect.left ||
                DestinationRect.top < pPresent->DstRect.top ||
                DestinationRect.right > pPresent->DstRect.right ||
                DestinationRect.bottom > pPresent->DstRect.bottom ||
                DestinationRect.left >= DestinationRect.right ||
                DestinationRect.top >= DestinationRect.bottom)
            {
                return STATUS_INVALID_PARAMETER;
            }

            Cmd->Magic = SOFTGPU_CMD_MAGIC;
            Cmd->Size = sizeof(*Cmd);
            Cmd->Color = pPresent->Color;
            Cmd->DstRect = DestinationRect;

            Status = SoftGpuPresentSurfacePitch(
                         KmdDevice,
                         pPresent->pAllocationList,
                         DXGK_PRESENT_DESTINATION_INDEX,
                         &Cmd->DstRect,
                         &DestinationPitch);
            if (!NT_SUCCESS(Status))
                return Status;
            Cmd->DstPitch = DestinationPitch;

            if (pPresent->Flags.Blt)
            {
                Cmd->SrcRect.left =
                    pPresent->SrcRect.left +
                    (DestinationRect.left - pPresent->DstRect.left);
                Cmd->SrcRect.top =
                    pPresent->SrcRect.top +
                    (DestinationRect.top - pPresent->DstRect.top);
                Cmd->SrcRect.right =
                    Cmd->SrcRect.left +
                    (DestinationRect.right - DestinationRect.left);
                Cmd->SrcRect.bottom =
                    Cmd->SrcRect.top +
                    (DestinationRect.bottom - DestinationRect.top);

                Status = SoftGpuPresentSurfacePitch(
                             KmdDevice,
                             pPresent->pAllocationList,
                             DXGK_PRESENT_SOURCE_INDEX,
                             &Cmd->SrcRect,
                             &SourcePitch);
                if (!NT_SUCCESS(Status))
                    return Status;
                Cmd->SrcPitch = SourcePitch;
                Cmd->Op = SOFTGPU_CMD_OP_BLT;

                pPresent->pPatchLocationListOut[
                    CommandIndex * PatchesPerCommand]
                        .AllocationIndex =
                            DXGK_PRESENT_SOURCE_INDEX;
                pPresent->pPatchLocationListOut[
                    CommandIndex * PatchesPerCommand]
                        .PatchOffset =
                            CommandIndex * sizeof(SOFTGPU_CMD) +
                            FIELD_OFFSET(SOFTGPU_CMD, SrcAddress);
                pPresent->pPatchLocationListOut[
                    CommandIndex * PatchesPerCommand + 1]
                        .AllocationIndex =
                            DXGK_PRESENT_DESTINATION_INDEX;
                pPresent->pPatchLocationListOut[
                    CommandIndex * PatchesPerCommand + 1]
                        .PatchOffset =
                            CommandIndex * sizeof(SOFTGPU_CMD) +
                            FIELD_OFFSET(SOFTGPU_CMD, DstAddress);
            }
            else
            {
                Cmd->Op = SOFTGPU_CMD_OP_FILL;
                pPresent->pPatchLocationListOut[CommandIndex]
                    .AllocationIndex =
                        DXGK_PRESENT_DESTINATION_INDEX;
                pPresent->pPatchLocationListOut[CommandIndex]
                    .PatchOffset =
                        CommandIndex * sizeof(SOFTGPU_CMD) +
                        FIELD_OFFSET(SOFTGPU_CMD, DstAddress);
            }
        }
        pPresent->pPatchLocationListOut += PatchesNeeded;
    }
    else
    {
        Commands[0].Magic = SOFTGPU_CMD_MAGIC;
        Commands[0].Size = sizeof(Commands[0]);
        Commands[0].Op = SOFTGPU_CMD_OP_NOP;
    }

    pPresent->pDmaBuffer =
        (PUCHAR)pPresent->pDmaBuffer + CommandBytes;
    return STATUS_SUCCESS;
}


/* =========================================================================
 * Cursor / palette stubs
 * =========================================================================
 */

NTSTATUS
APIENTRY
SoftGpuDdiSetPointerPosition(
    _In_ PVOID                            MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPOINTERPOSITION *SetPointerPosition)
{
    LONG TraceSeq = InterlockedIncrement(&g_SoftGpuPointerTraceCount);

    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (SetPointerPosition == NULL)
        return STATUS_INVALID_PARAMETER;

    if (TraceSeq <= SOFTGPU_TRACE_LOG_LIMIT)
    {
        DPRINT("SOFTGPU: SetPointerPosition seq=%ld src=%u visible=%u procedural=%u x=%d y=%d\n",
               TraceSeq,
               SetPointerPosition->VidPnSourceId,
               SetPointerPosition->Flags.Visible,
               SetPointerPosition->Flags.Procedural,
               SetPointerPosition->X,
               SetPointerPosition->Y);
    }

    /* No hardware cursor; dxgkrnl should use a software cursor path. */
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
APIENTRY
SoftGpuDdiSetPointerShape(
    _In_ PVOID                         MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPOINTERSHAPE *SetPointerShape)
{
    LONG TraceSeq = InterlockedIncrement(&g_SoftGpuPointerTraceCount);

    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (SetPointerShape == NULL)
        return STATUS_INVALID_PARAMETER;

    if (TraceSeq <= SOFTGPU_TRACE_LOG_LIMIT)
    {
        DPRINT("SOFTGPU: SetPointerShape seq=%ld src=%u width=%u height=%u pitch=%u flags=0x%lx\n",
               TraceSeq,
               SetPointerShape->VidPnSourceId,
               SetPointerShape->Width,
               SetPointerShape->Height,
               SetPointerShape->Pitch,
               SetPointerShape->Flags.Value);
    }

    /* No hardware cursor; dxgkrnl should use a software cursor path. */
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
APIENTRY
SoftGpuDdiSetPalette(
    _In_ PVOID                   MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPALETTE *SetPalette)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(SetPalette);

    return STATUS_NOT_SUPPORTED;
}


/* =========================================================================
 * DxgkDdiGetScanLine
 * =========================================================================
 */

/*
 * SoftGpuDdiGetScanLine
 *
 * Simulates a display that is always in vertical blank.  Returns ScanLine=0
 * and InVerticalBlank=TRUE so that any vblank wait completes immediately.
 */
NTSTATUS
APIENTRY
SoftGpuDdiGetScanLine(
    _In_    PVOID                  MiniportDeviceContext,
    _Inout_ PDXGKARG_GETSCANLINE   GetScanLine)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (GetScanLine == NULL || GetScanLine->VidPnSourceId != 0)
        return STATUS_INVALID_PARAMETER;

    GetScanLine->ScanLine       = 0;
    GetScanLine->InVerticalBlank= TRUE;
    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiCreateContext / DxgkDdiDestroyContext
 * =========================================================================
 */

/*
 * SoftGpuDdiCreateContext
 *
 * Allocates a SOFTGPU_CONTEXT and fills in the DMA buffer geometry that
 * dxgkrnl propagates to the user-mode driver.
 *
 * DmaBufferSize: 64 KB — sufficient for a Vista-era render command batch.
 * AllocationListSize: 256 entries.
 * PatchLocationListSize: 256 entries.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiCreateContext(
    _In_    PVOID                   MiniportDeviceContext,
    _Inout_ PDXGKARG_CREATECONTEXT  CreateContext)
{
    PSOFTGPU_KMD_DEVICE KmdDevice =
        (PSOFTGPU_KMD_DEVICE)MiniportDeviceContext;
    PSOFTGPU_DEVICE     Device;
    PSOFTGPU_PROCESS    Process;
    PSOFTGPU_CONTEXT    Ctx;

    if (CreateContext == NULL ||
        KmdDevice == NULL ||
        KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC)
        return STATUS_INVALID_PARAMETER;

    Device = KmdDevice->Adapter;
    Process = KmdDevice->Process;
    if (Device == NULL ||
        Device->Magic != SOFTGPU_DEVICE_MAGIC)
    {
        return STATUS_INVALID_PARAMETER;
    }
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    if (Process == NULL ||
        Process->Magic != SOFTGPU_PROCESS_MAGIC ||
        Process->Adapter != Device)
    {
        return STATUS_INVALID_PARAMETER;
    }
#else
    Process = NULL;
#endif

    if (CreateContext->NodeOrdinal != 0)
        return STATUS_INVALID_PARAMETER;

    DPRINT("SOFTGPU: CreateContext hContext=%p NodeOrdinal=%u\n",
           CreateContext->hContext, CreateContext->NodeOrdinal);

    Ctx = (PSOFTGPU_CONTEXT)ExAllocatePoolWithTag(NonPagedPool,
                                                   sizeof(SOFTGPU_CONTEXT),
                                                   SOFTGPU_POOL_TAG);
    if (Ctx == NULL)
    {
        DPRINT1("SOFTGPU: CreateContext: pool alloc failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Ctx, sizeof(*Ctx));
    Ctx->Magic          = SOFTGPU_CONTEXT_MAGIC;
    Ctx->NodeOrdinal    = CreateContext->NodeOrdinal;
    Ctx->EngineAffinity = CreateContext->EngineAffinity;
    Ctx->Device         = KmdDevice;
    Ctx->Process        = Process;

    /*
     * Fill in DMA buffer geometry for the UMD via DXGK_CONTEXTINFO.
     * DmaBufferSize:            64 KB — sufficient for a Vista-era command batch.
     * AllocationListSize:       256 entries.
     * PatchLocationListSize:    256 entries.
     * DmaBufferSegmentSet:      0 requests physically contiguous system memory.
     * DmaBufferPrivateDataSize: 0 (softgpu has no per-DMA private state).
     */
    CreateContext->ContextInfo.DmaBufferSize            = 64 * 1024;
    CreateContext->ContextInfo.DmaBufferSegmentSet      = 0;
    CreateContext->ContextInfo.DmaBufferPrivateDataSize = 0;
    CreateContext->ContextInfo.AllocationListSize       = 256;
    CreateContext->ContextInfo.PatchLocationListSize    = 256;
    CreateContext->ContextInfo.Caps.Value               = 0;

    CreateContext->hContext = (HANDLE)Ctx;

    DPRINT("SOFTGPU: CreateContext: Ctx=%p\n", Ctx);
    return STATUS_SUCCESS;
}

/*
 * SoftGpuDdiDestroyContext
 *
 * Frees the SOFTGPU_CONTEXT.  The argument is the hContext written back
 * by CreateContext, not the MiniportDeviceContext.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiDestroyContext(
    _In_ PVOID hContext)
{
    PSOFTGPU_CONTEXT Ctx = (PSOFTGPU_CONTEXT)hContext;
    PSOFTGPU_KMD_DEVICE KmdDevice;
    PSOFTGPU_DEVICE Device;
    KIRQL OldIrql;

    DPRINT("SOFTGPU: DestroyContext Ctx=%p\n", Ctx);

    if (Ctx != NULL)
    {
        if (Ctx->Magic != SOFTGPU_CONTEXT_MAGIC)
            return STATUS_INVALID_PARAMETER;

        KmdDevice = Ctx->Device;
        if (KmdDevice == NULL ||
            KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC ||
            KmdDevice->Adapter == NULL ||
            KmdDevice->Adapter->Magic != SOFTGPU_DEVICE_MAGIC)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Device = KmdDevice->Adapter;

        KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
        Ctx->Magic = 0xDEADC047UL;
        Ctx->Device = NULL;
        Ctx->Process = NULL;
        SoftGpuGpuVaRootClear(&Ctx->Root);
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);

        ExFreePoolWithTag(Ctx, SOFTGPU_POOL_TAG);
    }

    return STATUS_SUCCESS;
}

/* EOF */
