/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Virtio-blk MMIO device emulation
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 *
 * Implements a virtio-blk device using the MMIO transport (virtio 1.0+).
 * The device is backed by a host memory-mapped disk image and handles
 * guest block I/O requests synchronously in the VM-exit path.
 *
 * The guest discovers this device through a kernel command line parameter
 * (virtio_mmio.device=...) and communicates via MMIO register reads/writes
 * that trigger EPT violations, which are dispatched here.
 *
 * Virtqueue processing:
 *   Guest places request descriptors in the available ring, then writes
 *   to QUEUE_NOTIFY. We process all pending requests, copy data between
 *   the host disk image and guest memory (via GPA translation), write
 *   completion status to the used ring, and raise an interrupt.
 */

#include <rosv/rosv.h>
#include <rosv/vm.h>
#include <rosv/virtio_blk.h>
#include <rosv/vhdx.h>

NTSYSAPI
NTSTATUS
NTAPI
ZwWaitForSingleObject(
    _In_ HANDLE Handle,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout);

/* ---- Sector size -------------------------------------------------------- */

#define VIRTIO_SECTOR_SIZE      512ULL
#define VHDX_MAX_LOGICAL_SECTOR 4096U
#define VIRTIO_BLK_IO_CHUNK_SIZE (64U * 1024U)
#define VIRTIO_BLK_DEMAND_WORKER_TIMEOUT_SECONDS 20ULL

/*
 * End-to-end data-path validation (disk -> guest GPA -> readback).
 * Keep enabled for early-boot triage until systemd reaches steady state.
 */
#define VIRTIO_BLK_VERIFY_READBACK_FIRST_CHUNKS  0
/* End-to-end write-path validation (guest GPA -> backend -> reread). */
#define VIRTIO_BLK_VERIFY_WRITEBACK_FIRST_CHUNKS 0

FORCEINLINE BOOLEAN
RosvAreInterruptsEnabled(
    VOID)
{
#if defined(__x86_64__) || defined(_M_AMD64)
    ULONG64 Flags;
    __asm__ __volatile__("pushfq; popq %0" : "=r"(Flags));
    return (Flags & (1ULL << 9)) != 0;
#elif defined(__i386__) || defined(_M_IX86)
    ULONG Flags;
    __asm__ __volatile__("pushf; pop %0" : "=r"(Flags));
    return (Flags & (1U << 9)) != 0;
#else
    return TRUE;
#endif
}

/*
 * Check if a descriptor index has already been visited in a chain walk.
 * Returns TRUE if a cycle is detected (index already visited), FALSE otherwise.
 * VisitedBitmap must be at least 32 bytes (256 bits) and zeroed before the walk.
 */
FORCEINLINE BOOLEAN
RosvVirtqueueCheckAndMarkVisited(
    _Inout_updates_(32) PUCHAR VisitedBitmap,
    _In_ USHORT Index)
{
    if (VisitedBitmap[Index / 8] & (1 << (Index % 8)))
        return TRUE;   /* Cycle detected */
    VisitedBitmap[Index / 8] |= (1 << (Index % 8));
    return FALSE;
}

static
NTSTATUS
RosvVirtioBlkDemandReadDirect(
    _In_ PROSV_VIRTIO_BLK_STATE State,
    _In_ ULONG64 SectorNumber,
    _In_ ULONG SectorCount,
    _Out_writes_bytes_(SectorCount * VIRTIO_SECTOR_SIZE) PVOID Buffer)
{
    NTSTATUS DpStatus;

    if (State == NULL || Buffer == NULL)
        return STATUS_INVALID_PARAMETER;

    if (SectorCount == 0)
        return STATUS_SUCCESS;

    if (State->BackendType == ROSV_DISK_BACKEND_VHDX)
    {
        PROSV_VM Vm = State->OwnerVm;
        HANDLE PrimaryHandle;
        HANDLE SecondaryHandle = NULL;
        BOOLEAN PrimaryIsAlt;

        if (Vm == NULL)
        {
            ROSV_ERR("virtio-blk: demand-paged VHDX read but OwnerVm is NULL");
            return STATUS_INVALID_DEVICE_STATE;
        }
        if (State->DiskFileHandle == NULL &&
            State->DiskFileHandleAlt == NULL)
        {
            ROSV_ERR("virtio-blk: demand-paged VHDX read has no runtime file handle");
            return STATUS_INVALID_DEVICE_STATE;
        }

        PrimaryIsAlt = (State->PreferredReadHandle != 0) &&
                       (State->DiskFileHandleAlt != NULL);
        if (PrimaryIsAlt)
        {
            PrimaryHandle = State->DiskFileHandleAlt;
            SecondaryHandle = State->DiskFileHandle;
        }
        else
        {
            PrimaryHandle = State->DiskFileHandle;
            SecondaryHandle = State->DiskFileHandleAlt;
        }

        if (PrimaryHandle == NULL)
        {
            PrimaryHandle = SecondaryHandle;
            SecondaryHandle = NULL;
            PrimaryIsAlt = (PrimaryHandle == State->DiskFileHandleAlt);
        }

        ROSV_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL,
                    "demand-paged VHDX read must run at PASSIVE_LEVEL");
        DpStatus = RosvVhdxReadSectorsDemandPaged(&Vm->VhdxState,
                                                  PrimaryHandle,
                                                  SectorNumber,
                                                  SectorCount,
                                                  Buffer);

        if (DpStatus == STATUS_IO_TIMEOUT &&
            SecondaryHandle != NULL)
        {
            ROSV_WARN("virtio-blk: primary VHDX handle timed out at sector=%llu count=%u; retrying alternate handle",
                      SectorNumber,
                      SectorCount);
            DpStatus = RosvVhdxReadSectorsDemandPaged(&Vm->VhdxState,
                                                      SecondaryHandle,
                                                      SectorNumber,
                                                      SectorCount,
                                                      Buffer);
            if (NT_SUCCESS(DpStatus))
            {
                if (State->DiskFileHandle != NULL &&
                    State->DiskFileHandleAlt != NULL)
                {
                    State->PreferredReadHandle =
                        (SecondaryHandle == State->DiskFileHandleAlt) ? 0 : 1;
                }
                else
                {
                    State->PreferredReadHandle =
                        (SecondaryHandle == State->DiskFileHandleAlt) ? 1 : 0;
                }
            }
        }
        if (!NT_SUCCESS(DpStatus))
        {
            ROSV_ERR("virtio-blk: demand-paged VHDX read failed at "
                     "sector=%llu count=%u status=0x%08X",
                     SectorNumber, SectorCount, DpStatus);
        }
        return DpStatus;
    }
    else
    {
        IO_STATUS_BLOCK IoStatusBlock;
        LARGE_INTEGER ReadOffset;
        ULONG64 ReadSize64;
        ULONG ReadSize;

        if (State->DiskFileHandle == NULL)
        {
            ROSV_ERR("virtio-blk: demand-paged RAW read but DiskFileHandle is NULL");
            return STATUS_INVALID_DEVICE_STATE;
        }

        ReadSize64 = (ULONG64)SectorCount * VIRTIO_SECTOR_SIZE;
        if (ReadSize64 > MAXULONG)
            return STATUS_INVALID_PARAMETER;
        ReadSize = (ULONG)ReadSize64;

        ROSV_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL,
                    "demand-paged RAW read must run at PASSIVE_LEVEL");
        ReadOffset.QuadPart = (LONGLONG)(SectorNumber * VIRTIO_SECTOR_SIZE);
        DpStatus = ZwReadFile(State->DiskFileHandle, NULL, NULL, NULL,
                              &IoStatusBlock, Buffer, ReadSize,
                              &ReadOffset, NULL);
        if (DpStatus == STATUS_PENDING)
        {
            DpStatus = ZwWaitForSingleObject(State->DiskFileHandle, FALSE, NULL);
            if (NT_SUCCESS(DpStatus))
                DpStatus = IoStatusBlock.Status;
        }
        if (!NT_SUCCESS(DpStatus))
        {
            ROSV_ERR("virtio-blk: demand-paged RAW ZwReadFile failed at "
                     "sector=%llu count=%u status=0x%08X",
                     SectorNumber, SectorCount, DpStatus);
            return DpStatus;
        }
        if (IoStatusBlock.Information < ReadSize)
        {
            RtlZeroMemory((PUCHAR)Buffer + IoStatusBlock.Information,
                          ReadSize - (ULONG)IoStatusBlock.Information);
        }
        return STATUS_SUCCESS;
    }
}

static
VOID
RosvVirtioBlkDemandIoThreadProc(
    _In_ PVOID Context)
{
    PROSV_VIRTIO_BLK_STATE State;
    ULONG64 SectorNumber;
    ULONG SectorCount;
    PVOID Buffer;
    NTSTATUS IoStatus;

    State = (PROSV_VIRTIO_BLK_STATE)Context;
    if (State == NULL)
        PsTerminateSystemThread(STATUS_INVALID_PARAMETER);

    for (;;)
    {
        KeWaitForSingleObject(&State->DemandIoRequestEvent,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);

        if (InterlockedCompareExchange(&State->DemandIoStop, 0, 0) != 0)
            break;

        ExAcquireFastMutex(&State->DemandIoLock);
        if (State->DemandIoRequestPending == 0)
        {
            ExReleaseFastMutex(&State->DemandIoLock);
            continue;
        }

        SectorNumber = State->DemandIoSector;
        SectorCount = State->DemandIoSectorCount;
        Buffer = State->DemandIoBuffer;
        ExReleaseFastMutex(&State->DemandIoLock);

        IoStatus = RosvVirtioBlkDemandReadDirect(State,
                                                 SectorNumber,
                                                 SectorCount,
                                                 Buffer);

        ExAcquireFastMutex(&State->DemandIoLock);
        State->DemandIoStatus = IoStatus;
        State->DemandIoRequestPending = 0;
        ExReleaseFastMutex(&State->DemandIoLock);

        KeSetEvent(&State->DemandIoCompleteEvent, IO_NO_INCREMENT, FALSE);
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
NTSTATUS
RosvVirtioBlkDemandReadViaWorker(
    _Inout_ PROSV_VIRTIO_BLK_STATE State,
    _In_ ULONG64 SectorNumber,
    _In_ ULONG SectorCount,
    _Out_writes_bytes_(SectorCount * VIRTIO_SECTOR_SIZE) PVOID Buffer)
{
    LARGE_INTEGER Timeout;
    NTSTATUS WaitStatus;
    NTSTATUS IoStatus;
    LONG PendingAfterWait;

    if (State == NULL || Buffer == NULL)
        return STATUS_INVALID_PARAMETER;
    if (SectorCount == 0)
        return STATUS_SUCCESS;

    if (State->DemandIoThreadObject == NULL)
    {
        ROSV_WARN("virtio-blk: demand worker unavailable, falling back to direct read");
        return RosvVirtioBlkDemandReadDirect(State,
                                             SectorNumber,
                                             SectorCount,
                                             Buffer);
    }

    ROSV_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL,
                "demand-paged read must run at PASSIVE_LEVEL");

    KeClearEvent(&State->DemandIoCompleteEvent);

    ExAcquireFastMutex(&State->DemandIoLock);
    if (State->DemandIoRequestPending != 0)
    {
        ExReleaseFastMutex(&State->DemandIoLock);
        ROSV_ERR("virtio-blk: demand worker request collision (pending=%ld)",
                 State->DemandIoRequestPending);
        return STATUS_DEVICE_BUSY;
    }

    State->DemandIoSector = SectorNumber;
    State->DemandIoSectorCount = SectorCount;
    State->DemandIoBuffer = Buffer;
    State->DemandIoStatus = STATUS_PENDING;
    State->DemandIoRequestPending = 1;
    ExReleaseFastMutex(&State->DemandIoLock);

    KeSetEvent(&State->DemandIoRequestEvent, IO_NO_INCREMENT, FALSE);

    Timeout.QuadPart =
        -((LONGLONG)VIRTIO_BLK_DEMAND_WORKER_TIMEOUT_SECONDS * 10 * 1000 * 1000);
    WaitStatus = KeWaitForSingleObject(&State->DemandIoCompleteEvent,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       &Timeout);
    if (WaitStatus == STATUS_TIMEOUT)
    {
        /*
         * The worker thread is still running and may write to Buffer at any time.
         * Returning here would cause a use-after-free if the caller frees Buffer.
         * Instead, wait indefinitely for the worker to finish -- this blocks the
         * caller but prevents memory corruption.
         */
        ROSV_WARN("virtio-blk: demand worker slow I/O (>%llu s) sector=%llu count=%u, waiting indefinitely",
                  VIRTIO_BLK_DEMAND_WORKER_TIMEOUT_SECONDS,
                  SectorNumber,
                  SectorCount);
        WaitStatus = KeWaitForSingleObject(&State->DemandIoCompleteEvent,
                                           Executive,
                                           KernelMode,
                                           FALSE,
                                           NULL);
    }
    if (!NT_SUCCESS(WaitStatus))
    {
        ROSV_ERR("virtio-blk: demand worker wait failed status=0x%08X sector=%llu count=%u",
                 WaitStatus,
                 SectorNumber,
                 SectorCount);
        return WaitStatus;
    }

    ExAcquireFastMutex(&State->DemandIoLock);
    IoStatus = State->DemandIoStatus;
    PendingAfterWait = State->DemandIoRequestPending;
    ExReleaseFastMutex(&State->DemandIoLock);

    if (PendingAfterWait != 0)
    {
        ROSV_ERR("virtio-blk: demand worker signaled completion with pending request (sector=%llu count=%u)",
                 SectorNumber,
                 SectorCount);
        return STATUS_DEVICE_BUSY;
    }

    return IoStatus;
}

/* ---- Virtqueue helpers -------------------------------------------------- */

/**
 * Read a descriptor from the guest's descriptor table.
 * Returns TRUE on success, FALSE if the GPA translation or validation fails.
 *
 * Validates:
 *   - Index is within [0, QueueSize)
 *   - Next index (if VRING_DESC_F_NEXT) is within [0, QueueSize)
 *   - VRING_DESC_F_INDIRECT is not set (unsupported)
 */
static BOOLEAN
RosvVirtqueueReadDesc(
    _In_ PROSV_VM Vm,
    _In_ PROSV_VIRTQUEUE Vq,
    _In_ USHORT Index,
    _Out_ PVRING_DESC Desc)
{
    ULONG64 DescGpa;
    NTSTATUS Status;

    if (Index >= Vq->Num)
    {
        ROSV_ERR("virtqueue: descriptor index %u >= queue size %u", Index, Vq->Num);
        return FALSE;
    }

    DescGpa = Vq->DescGpa + (ULONG64)Index * sizeof(VRING_DESC);
    Status = RosvMemoryCopyFromGpa(Vm, Desc, DescGpa, sizeof(VRING_DESC));
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("virtqueue: failed to read desc GPA=0x%llX (index=%u) status=0x%08X",
                 DescGpa, Index, Status);
        return FALSE;
    }

    /* Reject indirect descriptors: not implemented */
    if (Desc->Flags & VRING_DESC_F_INDIRECT)
    {
        ROSV_ERR("virtqueue: VRING_DESC_F_INDIRECT not supported, "
                 "descriptor %u at GPA 0x%llX (addr=0x%llX len=%u flags=0x%04X)",
                 Index, DescGpa, Desc->Addr, Desc->Len, Desc->Flags);
        return FALSE;
    }

    /* Validate the next index if chained */
    if ((Desc->Flags & VRING_DESC_F_NEXT) && Desc->Next >= Vq->Num)
    {
        ROSV_ERR("virtqueue: descriptor %u has NEXT index %u >= queue size %u "
                 "(flags=0x%04X addr=0x%llX len=%u)",
                 Index, Desc->Next, Vq->Num, Desc->Flags, Desc->Addr, Desc->Len);
        return FALSE;
    }

    return TRUE;
}

/**
 * Read the current available ring index from guest memory.
 */
static USHORT
RosvVirtqueueReadAvailIdx(
    _In_ PROSV_VM Vm,
    _In_ PROSV_VIRTQUEUE Vq)
{
    ULONG64 IdxGpa;
    USHORT Idx;
    NTSTATUS Status;

    /* avail->idx is at offset 2 in the available ring structure */
    IdxGpa = Vq->AvailGpa + offsetof(VRING_AVAIL, Idx);
    Status = RosvMemoryCopyFromGpa(Vm, &Idx, IdxGpa, sizeof(Idx));
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("virtqueue: failed to read avail idx GPA=0x%llX status=0x%08X",
                 IdxGpa, Status);
        return Vq->LastAvailIdx;
    }

    return Idx;
}

/**
 * Read an entry from the available ring.
 * ring[index % num] is at avail + 4 + (index % num) * 2
 */
static USHORT
RosvVirtqueueReadAvailRing(
    _In_ PROSV_VM Vm,
    _In_ PROSV_VIRTQUEUE Vq,
    _In_ USHORT RingIndex)
{
    ULONG64 EntryGpa;
    USHORT Wrapped;
    USHORT Entry;
    NTSTATUS Status;

    Wrapped = RingIndex % (USHORT)Vq->Num;
    /* Ring entries start at avail + sizeof(VRING_AVAIL) = avail + 4 */
    EntryGpa = Vq->AvailGpa + sizeof(VRING_AVAIL) + (ULONG64)Wrapped * sizeof(USHORT);
    Status = RosvMemoryCopyFromGpa(Vm, &Entry, EntryGpa, sizeof(Entry));
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("virtqueue: failed to read avail ring GPA=0x%llX (idx=%u) status=0x%08X",
                 EntryGpa, RingIndex, Status);
        return 0;
    }

    return Entry;
}

/**
 * Read the available ring flags.
 */
static USHORT
RosvVirtqueueReadAvailFlags(
    _In_ PROSV_VM Vm,
    _In_ PROSV_VIRTQUEUE Vq)
{
    USHORT Flags;
    NTSTATUS Status;

    Status = RosvMemoryCopyFromGpa(Vm, &Flags, Vq->AvailGpa, sizeof(Flags));
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("virtqueue: failed to read avail flags GPA=0x%llX status=0x%08X",
                 Vq->AvailGpa, Status);
        return 0;
    }

    return Flags;
}

/**
 * Write an entry to the used ring and advance the used index.
 *
 * Ordering guarantee: the used ring element (id + len) is written
 * to guest memory BEFORE the used index is incremented, with a
 * KeMemoryBarrier in between. A second barrier follows the index
 * update so the guest observes a consistent state when polling.
 */
static BOOLEAN
RosvVirtqueuePushUsed(
    _In_ PROSV_VM Vm,
    _In_ PROSV_VIRTQUEUE Vq,
    _In_ ULONG DescChainHead,
    _In_ ULONG BytesWritten)
{
    ULONG64 UsedIdxGpa;
    USHORT UsedIdx;
    USHORT Wrapped;
    ULONG64 ElemGpa;
    VRING_USED_ELEM Elem;
    USHORT NewUsedIdx;
    NTSTATUS Status;

    /* Validate descriptor chain head is within queue bounds */
    if (DescChainHead >= Vq->Num)
    {
        ROSV_ERR("virtqueue: PushUsed descriptor id %u >= queue size %u",
                 DescChainHead, Vq->Num);
        return FALSE;
    }

    /* Read current used index */
    UsedIdxGpa = Vq->UsedGpa + offsetof(VRING_USED, Idx);
    Status = RosvMemoryCopyFromGpa(Vm, &UsedIdx, UsedIdxGpa, sizeof(UsedIdx));
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("virtqueue: failed to read used idx GPA=0x%llX status=0x%08X",
                 UsedIdxGpa, Status);
        return FALSE;
    }
    Wrapped = UsedIdx % (USHORT)Vq->Num;

    /* Step 1: Write the used element (id + len) FIRST */
    ElemGpa = Vq->UsedGpa + sizeof(VRING_USED) + (ULONG64)Wrapped * sizeof(VRING_USED_ELEM);
    Elem.Id = DescChainHead;
    Elem.Len = BytesWritten;
    Status = RosvMemoryCopyToGpa(Vm, ElemGpa, &Elem, sizeof(Elem));
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("virtqueue: failed to write used elem GPA=0x%llX (idx=%u) status=0x%08X",
                 ElemGpa, UsedIdx, Status);
        return FALSE;
    }

    /* Step 2: Write barrier — ensures element is visible before index update */
    KeMemoryBarrier();

    /* Step 3: Advance the used index by exactly 1 */
    NewUsedIdx = UsedIdx + 1;
    Status = RosvMemoryCopyToGpa(Vm, UsedIdxGpa, &NewUsedIdx, sizeof(NewUsedIdx));
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("virtqueue: failed to write used idx GPA=0x%llX status=0x%08X",
                 UsedIdxGpa, Status);
        return FALSE;
    }

    /* Step 4: Write barrier — ensures index update is visible to guest */
    KeMemoryBarrier();

    return TRUE;
}

static ULONG64
RosvVirtioBlkGetCapacityBytes(
    _In_ PROSV_VIRTIO_BLK_STATE State)
{
    if (State->BackendType == ROSV_DISK_BACKEND_VHDX &&
        State->OwnerVm != NULL)
    {
        return State->OwnerVm->VhdxState.VirtualDiskSize;
    }

    return State->DiskImageSize;
}

static NTSTATUS
RosvVirtioBlkReadBytes(
    _In_ PROSV_VIRTIO_BLK_STATE State,
    _In_ ULONG64 ByteOffset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    static ULONG ReadBytesTraceCount;
    ULONG64 CapacityBytes;
    ULONG64 EndOffset;

    if (Length == 0)
        return STATUS_SUCCESS;

    if (ReadBytesTraceCount < 8)
    {
        ROSV_TRACE("virtio-blk: ReadBytes offset=0x%llX len=%u mode=%s backend=%s irql=%lu",
                   ByteOffset,
                   Length,
                   (State->Mode == ROSV_DISK_MODE_DEMAND_PAGED) ? "demand-paged" : "ramdisk",
                   (State->BackendType == ROSV_DISK_BACKEND_VHDX) ? "VHDX" : "RAW",
                   (ULONG)KeGetCurrentIrql());
        ReadBytesTraceCount++;
    }

    CapacityBytes = RosvVirtioBlkGetCapacityBytes(State);
    EndOffset = ByteOffset + (ULONG64)Length;
    if (EndOffset < ByteOffset || EndOffset > CapacityBytes)
    {
        ROSV_ERR("virtio-blk: read out of bounds: offset=0x%llX len=%u capacity=0x%llX",
                 ByteOffset, Length, CapacityBytes);
        return STATUS_END_OF_FILE;
    }

    /* Demand-paged mode: check COW cache first, then fall back to file I/O.
     *
     * For each 512-byte virtio sector in the read range, look up the COW cache.
     * If the sector was previously written (COW hit), copy from the cached entry.
     * If not, read from the backing file (ZwReadFile for RAW, VHDX BAT translation).
     *
     * Interrupt policy:
     * - Host IF is enabled by the vCPU run loop before VM-exit dispatch.
     * - This path must not toggle IF ad hoc; storage I/O runs under that policy.
     */
    if (State->Mode == ROSV_DISK_MODE_DEMAND_PAGED)
    {
        ULONG64 SectorNumber;
        ULONG64 CurrentOffset;
        ULONG Remaining;
        PUCHAR OutPtr;
        UCHAR SectorBuffer[VIRTIO_SECTOR_SIZE];

        CurrentOffset = ByteOffset;
        Remaining = Length;
        OutPtr = (PUCHAR)Buffer;

        while (Remaining > 0)
        {
            ULONG OffsetInSector;
            ULONG BytesThisSector;
            PROSV_COW_ENTRY CowEntry;

            SectorNumber = CurrentOffset / VIRTIO_SECTOR_SIZE;
            OffsetInSector = (ULONG)(CurrentOffset % VIRTIO_SECTOR_SIZE);
            BytesThisSector = (ULONG)(VIRTIO_SECTOR_SIZE - OffsetInSector);
            if (BytesThisSector > Remaining)
                BytesThisSector = Remaining;

            /* Check COW cache first */
            CowEntry = RosvCowLookup(&State->CowCache, SectorNumber);
            if (CowEntry != NULL)
            {
                /* COW hit: copy from cached sector data */
                RtlCopyMemory(OutPtr, CowEntry->Data + OffsetInSector, BytesThisSector);
            }
            else if (OffsetInSector == 0 && BytesThisSector == VIRTIO_SECTOR_SIZE &&
                     Remaining >= VIRTIO_SECTOR_SIZE)
            {
                /*
                 * Fast path: aligned, full-sector read with no COW hit.
                 * Batch as many contiguous non-COW sectors as possible into a
                 * single ZwReadFile call (up to the VHDX block boundary, and
                 * up to what remains in the caller's buffer).
                 *
                 * We scan ahead to count how many consecutive sectors starting
                 * at SectorNumber have no COW entry.  We stop at the first COW
                 * hit or the end of the caller's buffer.  All sectors must lie
                 * within the same VHDX 16MB block (guaranteed by
                 * RosvVhdxReadSectorsDemandPaged's internal batching).
                 */
                ULONG64 BatchSectors;
                ULONG MaxSectors = Remaining / VIRTIO_SECTOR_SIZE;
                NTSTATUS DpStatus;

                /* Count contiguous non-COW sectors */
                for (BatchSectors = 1; BatchSectors < MaxSectors; BatchSectors++)
                {
                    if (RosvCowLookup(&State->CowCache, SectorNumber + BatchSectors) != NULL)
                        break;
                }

                if (BatchSectors > MAXULONG)
                    BatchSectors = MAXULONG;
                ROSV_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL,
                            "demand-paged read dispatcher must run at PASSIVE_LEVEL");
                ROSV_ASSERT(RosvAreInterruptsEnabled(),
                            "demand-paged read dispatcher requires host IF=1");
                DpStatus = RosvVirtioBlkDemandReadViaWorker(State,
                                                            SectorNumber,
                                                            (ULONG)BatchSectors,
                                                            OutPtr);
                if (!NT_SUCCESS(DpStatus))
                {
                    ROSV_ERR("virtio-blk: demand-paged batch read failed at "
                             "sector=%llu count=%llu status=0x%08X",
                             SectorNumber, BatchSectors, DpStatus);
                    return DpStatus;
                }

                /* Advance by the number of sectors actually batched.
                 * RosvVhdxReadSectorsDemandPaged may have stopped at a block
                 * boundary; compute how many full sectors it delivered by
                 * re-checking the batch size actually used. */
                BytesThisSector = (ULONG)(BatchSectors * VIRTIO_SECTOR_SIZE);
            }
            else
            {
                /* Slow path: unaligned or partial sector read, or COW sector
                 * boundary in the middle of a run.  Read exactly one sector. */
                NTSTATUS DpStatus;

                ROSV_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL,
                            "demand-paged read dispatcher must run at PASSIVE_LEVEL");
                ROSV_ASSERT(RosvAreInterruptsEnabled(),
                            "demand-paged read dispatcher requires host IF=1");
                DpStatus = RosvVirtioBlkDemandReadViaWorker(State,
                                                            SectorNumber,
                                                            1,
                                                            SectorBuffer);
                if (!NT_SUCCESS(DpStatus))
                {
                    ROSV_ERR("virtio-blk: demand-paged read failed at "
                             "sector=%llu status=0x%08X", SectorNumber, DpStatus);
                    return DpStatus;
                }

                RtlCopyMemory(OutPtr, SectorBuffer + OffsetInSector, BytesThisSector);
            }

            OutPtr += BytesThisSector;
            CurrentOffset += BytesThisSector;
            Remaining -= BytesThisSector;
        }

        return STATUS_SUCCESS;
    }

    /* Ramdisk mode: memory-mapped paths (original behavior) */
    if (State->BackendType == ROSV_DISK_BACKEND_VHDX)
    {
        PROSV_VM Vm;
        ULONG LogicalSectorSize;
        ULONG64 SectorCount64;
        ULONG64 SectorNumber;
        NTSTATUS Status;
        UCHAR SectorBuffer[VHDX_MAX_LOGICAL_SECTOR];
        ULONG64 CurrentOffset;
        ULONG Remaining;
        PUCHAR OutPtr;

        Vm = State->OwnerVm;
        if (Vm == NULL)
            return STATUS_INVALID_DEVICE_STATE;

        LogicalSectorSize = Vm->VhdxState.LogicalSectorSize;
        if (LogicalSectorSize == 0)
            LogicalSectorSize = (ULONG)VIRTIO_SECTOR_SIZE;

        if (LogicalSectorSize > VHDX_MAX_LOGICAL_SECTOR)
            return STATUS_INVALID_DEVICE_STATE;

        if ((ByteOffset % LogicalSectorSize) == 0 &&
            (Length % LogicalSectorSize) == 0)
        {
            SectorNumber = ByteOffset / LogicalSectorSize;
            SectorCount64 = Length / LogicalSectorSize;
            if (SectorCount64 > MAXULONG)
                return STATUS_INVALID_PARAMETER;
            return RosvVhdxReadSectors(&Vm->VhdxState,
                                       SectorNumber,
                                       (ULONG)SectorCount64,
                                       Buffer);
        }

        CurrentOffset = ByteOffset;
        Remaining = Length;
        OutPtr = (PUCHAR)Buffer;
        while (Remaining > 0)
        {
            ULONG OffsetInSector;
            ULONG BytesThisSector;

            SectorNumber = CurrentOffset / LogicalSectorSize;
            OffsetInSector = (ULONG)(CurrentOffset % LogicalSectorSize);
            BytesThisSector = LogicalSectorSize - OffsetInSector;
            if (BytesThisSector > Remaining)
                BytesThisSector = Remaining;

            Status = RosvVhdxReadSectors(&Vm->VhdxState, SectorNumber, 1, SectorBuffer);
            if (!NT_SUCCESS(Status))
                return Status;

            RtlCopyMemory(OutPtr, SectorBuffer + OffsetInSector, BytesThisSector);

            OutPtr += BytesThisSector;
            CurrentOffset += BytesThisSector;
            Remaining -= BytesThisSector;
        }

        return STATUS_SUCCESS;
    }

    if (State->DiskImageBase == NULL)
        return STATUS_INVALID_DEVICE_STATE;

    RtlCopyMemory(Buffer,
                  (PVOID)((ULONG_PTR)State->DiskImageBase + (ULONG_PTR)ByteOffset),
                  Length);
    return STATUS_SUCCESS;
}

static NTSTATUS
RosvVirtioBlkWriteBytes(
    _Inout_ PROSV_VIRTIO_BLK_STATE State,
    _In_ ULONG64 ByteOffset,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    ULONG64 CapacityBytes;
    ULONG64 EndOffset;

    if (Length == 0)
        return STATUS_SUCCESS;

    CapacityBytes = RosvVirtioBlkGetCapacityBytes(State);
    EndOffset = ByteOffset + (ULONG64)Length;
    if (EndOffset < ByteOffset || EndOffset > CapacityBytes)
    {
        ROSV_ERR("virtio-blk: write out of bounds: offset=0x%llX len=%u capacity=0x%llX",
                 ByteOffset, Length, CapacityBytes);
        return STATUS_END_OF_FILE;
    }

    /* Demand-paged mode: writes go to the COW cache (pure memory operations,
     * no file I/O needed). Each 512-byte virtio sector is stored in the COW
     * hash table. Reads will check COW first, then fall back to file I/O.
     * Right now writes live only in RAM COW.
     * TODO: persist these writes to backend storage and use FLUSH as durability.
     */
    if (State->Mode == ROSV_DISK_MODE_DEMAND_PAGED)
    {
        ULONG64 SectorNumber;
        ULONG64 CurrentOffset;
        ULONG Remaining;
        PUCHAR InPtr;
        NTSTATUS CowStatus;
        UCHAR SectorBuffer[VIRTIO_SECTOR_SIZE];

        CurrentOffset = ByteOffset;
        Remaining = Length;
        InPtr = (PUCHAR)Buffer;

        while (Remaining > 0)
        {
            ULONG OffsetInSector;
            ULONG BytesThisSector;

            SectorNumber = CurrentOffset / VIRTIO_SECTOR_SIZE;
            OffsetInSector = (ULONG)(CurrentOffset % VIRTIO_SECTOR_SIZE);
            BytesThisSector = (ULONG)(VIRTIO_SECTOR_SIZE - OffsetInSector);
            if (BytesThisSector > Remaining)
                BytesThisSector = Remaining;

            if (OffsetInSector != 0 || BytesThisSector != VIRTIO_SECTOR_SIZE)
            {
                /* Partial sector: read-modify-write via COW.
                 * First try COW cache, then fall back to file I/O for the base. */
                PROSV_COW_ENTRY Existing = RosvCowLookup(&State->CowCache, SectorNumber);
                if (Existing != NULL)
                {
                    RtlCopyMemory(SectorBuffer, Existing->Data, VIRTIO_SECTOR_SIZE);
                }
                else
                {
                    /* Read base sector from file */
                    CowStatus = RosvVirtioBlkReadBytes(State, SectorNumber * VIRTIO_SECTOR_SIZE,
                                                       SectorBuffer, (ULONG)VIRTIO_SECTOR_SIZE);
                    if (!NT_SUCCESS(CowStatus))
                    {
                        ROSV_ERR("virtio-blk: COW partial-sector read failed at sector=%llu status=0x%08X",
                                 SectorNumber, CowStatus);
                        return CowStatus;
                    }
                }

                RtlCopyMemory(SectorBuffer + OffsetInSector, InPtr, BytesThisSector);
                CowStatus = RosvCowWrite(&State->CowCache, SectorNumber, SectorBuffer);
                if (!NT_SUCCESS(CowStatus))
                {
                    ROSV_ERR("virtio-blk: COW write failed at sector=%llu status=0x%08X",
                             SectorNumber, CowStatus);
                    return CowStatus;
                }
            }
            else
            {
                /* Full sector write directly to COW */
                CowStatus = RosvCowWrite(&State->CowCache, SectorNumber, InPtr);
                if (!NT_SUCCESS(CowStatus))
                {
                    ROSV_ERR("virtio-blk: COW write failed at sector=%llu status=0x%08X",
                             SectorNumber, CowStatus);
                    return CowStatus;
                }
            }

            InPtr += BytesThisSector;
            CurrentOffset += BytesThisSector;
            Remaining -= BytesThisSector;
        }

        return STATUS_SUCCESS;
    }

    /* Ramdisk mode: memory-mapped paths (original behavior) */
    if (State->BackendType == ROSV_DISK_BACKEND_VHDX)
    {
        PROSV_VM Vm;
        ULONG LogicalSectorSize;
        ULONG64 SectorCount64;
        ULONG64 SectorNumber;
        NTSTATUS Status;
        UCHAR SectorBuffer[VHDX_MAX_LOGICAL_SECTOR];
        ULONG64 CurrentOffset;
        ULONG Remaining;
        PUCHAR InPtr;

        Vm = State->OwnerVm;
        if (Vm == NULL)
            return STATUS_INVALID_DEVICE_STATE;

        LogicalSectorSize = Vm->VhdxState.LogicalSectorSize;
        if (LogicalSectorSize == 0)
            LogicalSectorSize = (ULONG)VIRTIO_SECTOR_SIZE;

        if (LogicalSectorSize > VHDX_MAX_LOGICAL_SECTOR)
            return STATUS_INVALID_DEVICE_STATE;

        if ((ByteOffset % LogicalSectorSize) == 0 &&
            (Length % LogicalSectorSize) == 0)
        {
            SectorNumber = ByteOffset / LogicalSectorSize;
            SectorCount64 = Length / LogicalSectorSize;
            if (SectorCount64 > MAXULONG)
                return STATUS_INVALID_PARAMETER;
            return RosvVhdxWriteSectors(&Vm->VhdxState,
                                        SectorNumber,
                                        (ULONG)SectorCount64,
                                        Buffer);
        }

        CurrentOffset = ByteOffset;
        Remaining = Length;
        InPtr = (PUCHAR)Buffer;
        while (Remaining > 0)
        {
            ULONG OffsetInSector;
            ULONG BytesThisSector;

            SectorNumber = CurrentOffset / LogicalSectorSize;
            OffsetInSector = (ULONG)(CurrentOffset % LogicalSectorSize);
            BytesThisSector = LogicalSectorSize - OffsetInSector;
            if (BytesThisSector > Remaining)
                BytesThisSector = Remaining;

            if (OffsetInSector != 0 || BytesThisSector != LogicalSectorSize)
            {
                Status = RosvVhdxReadSectors(&Vm->VhdxState, SectorNumber, 1, SectorBuffer);
                if (!NT_SUCCESS(Status))
                    return Status;

                RtlCopyMemory(SectorBuffer + OffsetInSector, InPtr, BytesThisSector);
                Status = RosvVhdxWriteSectors(&Vm->VhdxState, SectorNumber, 1, SectorBuffer);
                if (!NT_SUCCESS(Status))
                    return Status;
            }
            else
            {
                Status = RosvVhdxWriteSectors(&Vm->VhdxState, SectorNumber, 1, InPtr);
                if (!NT_SUCCESS(Status))
                    return Status;
            }

            InPtr += BytesThisSector;
            CurrentOffset += BytesThisSector;
            Remaining -= BytesThisSector;
        }

        return STATUS_SUCCESS;
    }

    if (State->DiskImageBase == NULL)
        return STATUS_INVALID_DEVICE_STATE;

    RtlCopyMemory((PVOID)((ULONG_PTR)State->DiskImageBase + (ULONG_PTR)ByteOffset),
                  Buffer,
                  Length);
    return STATUS_SUCCESS;
}

static NTSTATUS
RosvVirtioBlkReadToGuest(
    _Inout_ PROSV_VIRTIO_BLK_STATE State,
    _In_ ULONG64 DiskOffset,
    _In_ ULONG64 GuestDataGpa,
    _In_ ULONG Length)
{
    PROSV_VM Vm = State->OwnerVm;
    PUCHAR Bounce;
#if (VIRTIO_BLK_VERIFY_READBACK_FIRST_CHUNKS > 0)
    static volatile LONG ReadbackChecks;
    PUCHAR Verify = NULL;
#endif
    ULONG Remaining = Length;
    ULONG64 CurrentDiskOffset = DiskOffset;
    ULONG64 CurrentGuestGpa = GuestDataGpa;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Length == 0)
        return STATUS_SUCCESS;

    if (Vm == NULL)
        return STATUS_INVALID_DEVICE_STATE;

    Bounce = (PUCHAR)ExAllocatePoolWithTag(NonPagedPool,
                                           VIRTIO_BLK_IO_CHUNK_SIZE,
                                           ROSV_DRIVER_TAG);
    if (Bounce == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

#if (VIRTIO_BLK_VERIFY_READBACK_FIRST_CHUNKS > 0)
    /*
     * Allocate readback buffer only while verification window is active.
     * This keeps normal runtime overhead low once early-boot validation ends.
     */
    if (InterlockedCompareExchange(&ReadbackChecks, 0, 0) <
        VIRTIO_BLK_VERIFY_READBACK_FIRST_CHUNKS)
    {
        Verify = (PUCHAR)ExAllocatePoolWithTag(NonPagedPool,
                                               VIRTIO_BLK_IO_CHUNK_SIZE,
                                               ROSV_DRIVER_TAG);
        if (Verify == NULL)
        {
            ExFreePool(Bounce);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
#endif

    while (Remaining > 0)
    {
        ULONG ThisChunk = Remaining;
        if (ThisChunk > VIRTIO_BLK_IO_CHUNK_SIZE)
            ThisChunk = VIRTIO_BLK_IO_CHUNK_SIZE;
        ROSV_ASSERT(ThisChunk > 0 && ThisChunk <= VIRTIO_BLK_IO_CHUNK_SIZE,
                    "virtio-blk read chunk must be in (0, VIRTIO_BLK_IO_CHUNK_SIZE]");

        Status = RosvVirtioBlkReadBytes(State, CurrentDiskOffset, Bounce, ThisChunk);
        if (!NT_SUCCESS(Status))
            break;

        Status = RosvMemoryCopyToGpa(Vm, CurrentGuestGpa, Bounce, ThisChunk);
        if (!NT_SUCCESS(Status))
            break;

#if (VIRTIO_BLK_VERIFY_READBACK_FIRST_CHUNKS > 0)
        if (Verify != NULL)
        {
            LONG CheckOrdinal = InterlockedIncrement(&ReadbackChecks);
            if (CheckOrdinal <= VIRTIO_BLK_VERIFY_READBACK_FIRST_CHUNKS)
            {
                SIZE_T PrefixMatch;

                Status = RosvMemoryCopyFromGpa(Vm, Verify, CurrentGuestGpa, ThisChunk);
                if (!NT_SUCCESS(Status))
                {
                    ROSV_ERR("virtio-blk: READBACK copy-from-gpa failed disk_off=0x%llX gpa=0x%llX len=%u status=0x%08X",
                             CurrentDiskOffset, CurrentGuestGpa, ThisChunk, Status);
                    break;
                }

                PrefixMatch = RtlCompareMemory(Bounce, Verify, ThisChunk);
                if (PrefixMatch != ThisChunk)
                {
                    ROSV_ERR("virtio-blk: READBACK MISMATCH disk_off=0x%llX gpa=0x%llX len=%u match_prefix=%zu check=%ld",
                             CurrentDiskOffset, CurrentGuestGpa, ThisChunk, PrefixMatch, CheckOrdinal);
                    ROSV_ERR("virtio-blk: READBACK first mismatch at +0x%zX expected=0x%02X actual=0x%02X",
                             PrefixMatch,
                             (ULONG)Bounce[PrefixMatch],
                             (ULONG)Verify[PrefixMatch]);
                    Status = STATUS_DATA_ERROR;
                    break;
                }
            }
        }
#endif

        CurrentDiskOffset += (ULONG64)ThisChunk;
        CurrentGuestGpa += (ULONG64)ThisChunk;
        Remaining -= ThisChunk;
    }

#if (VIRTIO_BLK_VERIFY_READBACK_FIRST_CHUNKS > 0)
    if (Verify != NULL)
        ExFreePool(Verify);
#endif
    ExFreePool(Bounce);
    return Status;
}

static NTSTATUS
RosvVirtioBlkWriteFromGuest(
    _Inout_ PROSV_VIRTIO_BLK_STATE State,
    _In_ ULONG64 DiskOffset,
    _In_ ULONG64 GuestDataGpa,
    _In_ ULONG Length)
{
    PROSV_VM Vm = State->OwnerVm;
    PUCHAR Bounce;
#if (VIRTIO_BLK_VERIFY_WRITEBACK_FIRST_CHUNKS > 0)
    static volatile LONG WritebackChecks;
    PUCHAR Verify = NULL;
#endif
    ULONG Remaining = Length;
    ULONG64 CurrentDiskOffset = DiskOffset;
    ULONG64 CurrentGuestGpa = GuestDataGpa;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Length == 0)
        return STATUS_SUCCESS;

    if (Vm == NULL)
        return STATUS_INVALID_DEVICE_STATE;

    Bounce = (PUCHAR)ExAllocatePoolWithTag(NonPagedPool,
                                           VIRTIO_BLK_IO_CHUNK_SIZE,
                                           ROSV_DRIVER_TAG);
    if (Bounce == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

#if (VIRTIO_BLK_VERIFY_WRITEBACK_FIRST_CHUNKS > 0)
    if (InterlockedCompareExchange(&WritebackChecks, 0, 0) <
        VIRTIO_BLK_VERIFY_WRITEBACK_FIRST_CHUNKS)
    {
        Verify = (PUCHAR)ExAllocatePoolWithTag(NonPagedPool,
                                               VIRTIO_BLK_IO_CHUNK_SIZE,
                                               ROSV_DRIVER_TAG);
        if (Verify == NULL)
        {
            ExFreePool(Bounce);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
#endif

    while (Remaining > 0)
    {
        ULONG ThisChunk = Remaining;
        if (ThisChunk > VIRTIO_BLK_IO_CHUNK_SIZE)
            ThisChunk = VIRTIO_BLK_IO_CHUNK_SIZE;
        ROSV_ASSERT(ThisChunk > 0 && ThisChunk <= VIRTIO_BLK_IO_CHUNK_SIZE,
                    "virtio-blk write chunk must be in (0, VIRTIO_BLK_IO_CHUNK_SIZE]");

        Status = RosvMemoryCopyFromGpa(Vm, Bounce, CurrentGuestGpa, ThisChunk);
        if (!NT_SUCCESS(Status))
            break;

        Status = RosvVirtioBlkWriteBytes(State, CurrentDiskOffset, Bounce, ThisChunk);
        if (!NT_SUCCESS(Status))
            break;

#if (VIRTIO_BLK_VERIFY_WRITEBACK_FIRST_CHUNKS > 0)
        if (Verify != NULL)
        {
            LONG CheckOrdinal = InterlockedIncrement(&WritebackChecks);
            if (CheckOrdinal <= VIRTIO_BLK_VERIFY_WRITEBACK_FIRST_CHUNKS)
            {
                SIZE_T PrefixMatch;

                Status = RosvVirtioBlkReadBytes(State, CurrentDiskOffset, Verify, ThisChunk);
                if (!NT_SUCCESS(Status))
                {
                    ROSV_ERR("virtio-blk: WRITEBACK verify read failed disk_off=0x%llX len=%u status=0x%08X",
                             CurrentDiskOffset, ThisChunk, Status);
                    break;
                }

                PrefixMatch = RtlCompareMemory(Bounce, Verify, ThisChunk);
                if (PrefixMatch != ThisChunk)
                {
                    ROSV_ERR("virtio-blk: WRITEBACK MISMATCH disk_off=0x%llX gpa=0x%llX len=%u match_prefix=%zu check=%ld",
                             CurrentDiskOffset, CurrentGuestGpa, ThisChunk, PrefixMatch, CheckOrdinal);
                    ROSV_ERR("virtio-blk: WRITEBACK first mismatch at +0x%zX expected=0x%02X actual=0x%02X",
                             PrefixMatch,
                             (ULONG)Bounce[PrefixMatch],
                             (ULONG)Verify[PrefixMatch]);
                    Status = STATUS_DATA_ERROR;
                    break;
                }
            }
        }
#endif

        CurrentDiskOffset += (ULONG64)ThisChunk;
        CurrentGuestGpa += (ULONG64)ThisChunk;
        Remaining -= ThisChunk;
    }

#if (VIRTIO_BLK_VERIFY_WRITEBACK_FIRST_CHUNKS > 0)
    if (Verify != NULL)
        ExFreePool(Verify);
#endif
    ExFreePool(Bounce);
    return Status;
}

/* ---- Block request processing ------------------------------------------- */

/**
 * Write a 1-byte status to the guest's status descriptor via CopyToGpa.
 * Uses the chunk-safe memory copy path instead of direct HVA pointer writes.
 * Returns TRUE on success, FALSE on failure (logged with full context).
 */
static BOOLEAN
RosvVirtioBlkWriteStatus(
    _In_ PROSV_VM Vm,
    _In_ ULONG64 StatusGpa,
    _In_ UCHAR StatusByte)
{
    NTSTATUS Status;

    Status = RosvMemoryCopyToGpa(Vm, StatusGpa, &StatusByte, sizeof(StatusByte));
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("virtio-blk: failed to write status byte %u to GPA 0x%llX status=0x%08X",
                 StatusByte, StatusGpa, Status);
        return FALSE;
    }

    return TRUE;
}

/**
 * Process a single virtio-blk request from the virtqueue.
 *
 * A standard virtio-blk request is a 3-descriptor chain:
 *   [0] Header (VIRTIO_BLK_REQ_HDR, 16 bytes, device-readable)
 *   [1] Data buffer (device-writable for reads, device-readable for writes)
 *   [2] Status byte (1 byte, device-writable)
 *
 * Returns the total number of bytes written to device-writable buffers.
 */
static ULONG
RosvVirtioBlkProcessRequest(
    _Inout_ PROSV_VIRTIO_BLK_STATE State,
    _In_ PROSV_VIRTQUEUE Vq,
    _In_ USHORT HeadIdx)
{
    PROSV_VM Vm = State->OwnerVm;
    VRING_DESC Desc;
    VIRTIO_BLK_REQ_HDR Header;
    ULONG64 RequestDiskOffset;
    NTSTATUS IoStatus;
    ULONG BytesWritten = 0;
    UCHAR StatusByte;
    USHORT CurrentIdx;
    ULONG DataLen;
    ULONG ChainDepth = 0;

    /* Step 1: Read the header descriptor */
    if (!RosvVirtqueueReadDesc(Vm, Vq, HeadIdx, &Desc))
    {
        ROSV_ERR("virtio-blk: failed to read header descriptor at index %u", HeadIdx);
        return 0;
    }

    if (Desc.Len < sizeof(VIRTIO_BLK_REQ_HDR))
    {
        ROSV_ERR("virtio-blk: header descriptor too small: %u bytes (need %u)",
                 Desc.Len, (ULONG)sizeof(VIRTIO_BLK_REQ_HDR));
        return 0;
    }

    IoStatus = RosvMemoryCopyFromGpa(Vm, &Header, Desc.Addr, sizeof(VIRTIO_BLK_REQ_HDR));
    if (!NT_SUCCESS(IoStatus))
    {
        ROSV_ERR("virtio-blk: failed to read header GPA=0x%llX status=0x%08X",
                 Desc.Addr, IoStatus);
        return 0;
    }

    /* Walk the descriptor chain for data and status buffers */
    if (!(Desc.Flags & VRING_DESC_F_NEXT))
    {
        ROSV_ERR("virtio-blk: header descriptor has no NEXT flag");
        return 0;
    }

    StatusByte = VIRTIO_BLK_S_OK;

    switch (Header.Type)
    {
    case VIRTIO_BLK_T_IN: /* Read from device */
    {
        /* Bitmap to detect descriptor chain cycles (supports up to 256 descriptors) */
        UCHAR VisitedBitmapRead[32]; /* 256 bits */
        RtlZeroMemory(VisitedBitmapRead, sizeof(VisitedBitmapRead));

        if (Header.Sector > (((ULONG64)-1) / VIRTIO_SECTOR_SIZE))
        {
            ROSV_ERR("virtio-blk: READ sector overflow: sector=%llu", Header.Sector);
            StatusByte = VIRTIO_BLK_S_IOERR;
            break;
        }
        RequestDiskOffset = Header.Sector * VIRTIO_SECTOR_SIZE;

        /* Walk all data descriptors in the chain */
        CurrentIdx = Desc.Next;
        ChainDepth = 0;

        while (ChainDepth < Vq->Num)
        {
            /* Check for descriptor chain cycle */
            if (CurrentIdx >= Vq->Num)
            {
                ROSV_ERR("VirtIO-blk: descriptor index %u out of range (max %u)", CurrentIdx, Vq->Num);
                break;
            }
            if (VisitedBitmapRead[CurrentIdx / 8] & (1 << (CurrentIdx % 8)))
            {
                ROSV_ERR("VirtIO-blk: descriptor chain cycle detected at index %u", CurrentIdx);
                break;
            }
            VisitedBitmapRead[CurrentIdx / 8] |= (1 << (CurrentIdx % 8));

            if (!RosvVirtqueueReadDesc(Vm, Vq, CurrentIdx, &Desc))
            {
                ROSV_ERR("virtio-blk: failed to read data descriptor at index %u", CurrentIdx);
                StatusByte = VIRTIO_BLK_S_IOERR;
                break;
            }

            /* If this is the last descriptor (no NEXT), it's the status byte */
            if (!(Desc.Flags & VRING_DESC_F_NEXT))
            {
                /* This is the status descriptor */
                if (Desc.Len < 1 || !(Desc.Flags & VRING_DESC_F_WRITE))
                {
                    ROSV_ERR("virtio-blk: invalid status descriptor (len=%u flags=0x%X)",
                             Desc.Len, Desc.Flags);
                    return BytesWritten;
                }
                if (!RosvVirtioBlkWriteStatus(Vm, Desc.Addr, StatusByte))
                {
                    ROSV_ERR("virtio-blk: READ status write failed at GPA=0x%llX "
                             "(head=%u status=%u)", Desc.Addr, HeadIdx, StatusByte);
                    return BytesWritten;
                }
                BytesWritten += 1;
                break;
            }

            /* This is a data descriptor - must be device-writable for reads */
            if (!(Desc.Flags & VRING_DESC_F_WRITE))
            {
                ROSV_ERR("virtio-blk: READ data descriptor not writable (flags=0x%X)", Desc.Flags);
                StatusByte = VIRTIO_BLK_S_IOERR;
                CurrentIdx = Desc.Next;
                ChainDepth++;
                continue;
            }

            DataLen = Desc.Len;
            if ((ULONG64)DataLen > (((ULONG64)-1) - RequestDiskOffset))
            {
                ROSV_ERR("virtio-blk: READ offset overflow: offset=0x%llX len=%u",
                         RequestDiskOffset, DataLen);
                StatusByte = VIRTIO_BLK_S_IOERR;
                CurrentIdx = Desc.Next;
                ChainDepth++;
                continue;
            }

            if ((DataLen & (VIRTIO_SECTOR_SIZE - 1)) != 0)
            {
                ROSV_WARN("virtio-blk: READ descriptor len=%u is not 512-byte aligned (head=%u idx=%u offset=0x%llX)",
                          DataLen, HeadIdx, CurrentIdx, RequestDiskOffset);
            }

            IoStatus = RosvVirtioBlkReadToGuest(State, RequestDiskOffset, Desc.Addr, DataLen);
            if (!NT_SUCCESS(IoStatus))
            {
                ROSV_ERR("virtio-blk: READ failed at offset=0x%llX gpa=0x%llX len=%u status=0x%08X",
                         RequestDiskOffset, Desc.Addr, DataLen, IoStatus);
                StatusByte = VIRTIO_BLK_S_IOERR;
                CurrentIdx = Desc.Next;
                ChainDepth++;
                RequestDiskOffset += (ULONG64)DataLen;
                continue;
            }
            BytesWritten += DataLen;
            RequestDiskOffset += (ULONG64)DataLen;

            State->ReadOps++;
            State->ReadBytes += DataLen;

            CurrentIdx = Desc.Next;
            ChainDepth++;
        }

        if (ChainDepth >= Vq->Num)
        {
            ROSV_ERR("virtio-blk: descriptor chain too long (>%u)", Vq->Num);
        }
        break;
    }

    case VIRTIO_BLK_T_OUT: /* Write to device */
    {
        /* Bitmap to detect descriptor chain cycles (supports up to 256 descriptors) */
        UCHAR VisitedBitmapWrite[32]; /* 256 bits */
        RtlZeroMemory(VisitedBitmapWrite, sizeof(VisitedBitmapWrite));

        if (Header.Sector > (((ULONG64)-1) / VIRTIO_SECTOR_SIZE))
        {
            ROSV_ERR("virtio-blk: WRITE sector overflow: sector=%llu", Header.Sector);
            StatusByte = VIRTIO_BLK_S_IOERR;
            break;
        }
        RequestDiskOffset = Header.Sector * VIRTIO_SECTOR_SIZE;

        if (State->ReadOnly)
        {
            ROSV_WARN("virtio-blk: WRITE rejected (read-only device)");
            StatusByte = VIRTIO_BLK_S_IOERR;
        }

        /* Walk all data descriptors in the chain */
        CurrentIdx = Desc.Next;
        ChainDepth = 0;

        while (ChainDepth < Vq->Num)
        {
            /* Check for descriptor chain cycle */
            if (CurrentIdx >= Vq->Num)
            {
                ROSV_ERR("VirtIO-blk: descriptor index %u out of range (max %u)", CurrentIdx, Vq->Num);
                break;
            }
            if (VisitedBitmapWrite[CurrentIdx / 8] & (1 << (CurrentIdx % 8)))
            {
                ROSV_ERR("VirtIO-blk: descriptor chain cycle detected at index %u", CurrentIdx);
                break;
            }
            VisitedBitmapWrite[CurrentIdx / 8] |= (1 << (CurrentIdx % 8));

            if (!RosvVirtqueueReadDesc(Vm, Vq, CurrentIdx, &Desc))
            {
                ROSV_ERR("virtio-blk: failed to read write-data descriptor at index %u", CurrentIdx);
                StatusByte = VIRTIO_BLK_S_IOERR;
                break;
            }

            /* Last descriptor = status */
            if (!(Desc.Flags & VRING_DESC_F_NEXT))
            {
                if (Desc.Len < 1 || !(Desc.Flags & VRING_DESC_F_WRITE))
                {
                    ROSV_ERR("virtio-blk: invalid status descriptor for write (len=%u flags=0x%X)",
                             Desc.Len, Desc.Flags);
                    return BytesWritten;
                }
                if (!RosvVirtioBlkWriteStatus(Vm, Desc.Addr, StatusByte))
                {
                    ROSV_ERR("virtio-blk: WRITE status write failed at GPA=0x%llX "
                             "(head=%u status=%u)", Desc.Addr, HeadIdx, StatusByte);
                    return BytesWritten;
                }
                BytesWritten += 1;
                break;
            }

            /* Data descriptor for writes - device-readable */
            if (State->ReadOnly)
            {
                /* Skip data copy but continue chain walk */
                CurrentIdx = Desc.Next;
                ChainDepth++;
                if ((ULONG64)Desc.Len <= (((ULONG64)-1) - RequestDiskOffset))
                    RequestDiskOffset += (ULONG64)Desc.Len;
                continue;
            }

            DataLen = Desc.Len;
            if ((ULONG64)DataLen > (((ULONG64)-1) - RequestDiskOffset))
            {
                ROSV_ERR("virtio-blk: WRITE offset overflow: offset=0x%llX len=%u",
                         RequestDiskOffset, DataLen);
                StatusByte = VIRTIO_BLK_S_IOERR;
                CurrentIdx = Desc.Next;
                ChainDepth++;
                continue;
            }

            if ((DataLen & (VIRTIO_SECTOR_SIZE - 1)) != 0)
            {
                ROSV_WARN("virtio-blk: WRITE descriptor len=%u is not 512-byte aligned (head=%u idx=%u offset=0x%llX)",
                          DataLen, HeadIdx, CurrentIdx, RequestDiskOffset);
            }

            IoStatus = RosvVirtioBlkWriteFromGuest(State, RequestDiskOffset, Desc.Addr, DataLen);
            if (!NT_SUCCESS(IoStatus))
            {
                ROSV_ERR("virtio-blk: WRITE failed at offset=0x%llX gpa=0x%llX len=%u status=0x%08X",
                         RequestDiskOffset, Desc.Addr, DataLen, IoStatus);
                StatusByte = VIRTIO_BLK_S_IOERR;
                CurrentIdx = Desc.Next;
                ChainDepth++;
                RequestDiskOffset += (ULONG64)DataLen;
                continue;
            }

            RequestDiskOffset += (ULONG64)DataLen;
            State->WriteOps++;
            State->WriteBytes += DataLen;

            CurrentIdx = Desc.Next;
            ChainDepth++;
        }
        break;
    }

    case VIRTIO_BLK_T_GET_ID: /* Get device ID string */
    {
        CurrentIdx = Desc.Next;
        if (!RosvVirtqueueReadDesc(Vm, Vq, CurrentIdx, &Desc))
        {
            ROSV_ERR("virtio-blk: failed to read GET_ID data descriptor");
            return 0;
        }

        if (Desc.Flags & VRING_DESC_F_WRITE)
        {
            NTSTATUS CopyStatus = STATUS_SUCCESS;
            ULONG Remaining = Desc.Len;
            ULONG64 DataGpa = Desc.Addr;
            UCHAR ZeroChunk[64] = {0};
            ULONG CopyLen = (Desc.Len < ROSV_VIRTIO_BLK_ID_LEN) ?
                             Desc.Len : ROSV_VIRTIO_BLK_ID_LEN;

            while (Remaining > 0 && NT_SUCCESS(CopyStatus))
            {
                ULONG ThisChunk = Remaining;
                if (ThisChunk > sizeof(ZeroChunk))
                    ThisChunk = sizeof(ZeroChunk);

                CopyStatus = RosvMemoryCopyToGpa(Vm, DataGpa, ZeroChunk, ThisChunk);
                DataGpa += (ULONG64)ThisChunk;
                Remaining -= ThisChunk;
            }

            if (!NT_SUCCESS(CopyStatus))
            {
                ROSV_ERR("virtio-blk: GET_ID failed to zero data GPA=0x%llX len=%u status=0x%08X",
                         Desc.Addr, Desc.Len, CopyStatus);
            }
            else if (CopyLen > 0)
            {
                CopyStatus = RosvMemoryCopyToGpa(Vm, Desc.Addr, ROSV_VIRTIO_BLK_ID, CopyLen);
                if (!NT_SUCCESS(CopyStatus))
                {
                    ROSV_ERR("virtio-blk: GET_ID failed to write ID GPA=0x%llX len=%u status=0x%08X",
                             Desc.Addr, CopyLen, CopyStatus);
                }
                else
                {
                    BytesWritten += Desc.Len;
                }
            }
            else
            {
                BytesWritten += Desc.Len;
            }
        }

        /* Find and write status */
        if (Desc.Flags & VRING_DESC_F_NEXT)
        {
            CurrentIdx = Desc.Next;
            if (RosvVirtqueueReadDesc(Vm, Vq, CurrentIdx, &Desc))
            {
                if (Desc.Len >= 1 && (Desc.Flags & VRING_DESC_F_WRITE))
                {
                    if (!RosvVirtioBlkWriteStatus(Vm, Desc.Addr, VIRTIO_BLK_S_OK))
                    {
                        ROSV_ERR("virtio-blk: GET_ID status write failed at GPA=0x%llX",
                                 Desc.Addr);
                    }
                    else
                    {
                        BytesWritten += 1;
                    }
                }
            }
        }
        break;
    }

    case VIRTIO_BLK_T_FLUSH: /* Flush */
    {
        /* Bitmap to detect descriptor chain cycles (supports up to 256 descriptors) */
        UCHAR VisitedBitmapFlush[32]; /* 256 bits */
        RtlZeroMemory(VisitedBitmapFlush, sizeof(VisitedBitmapFlush));

        /* Current behavior ACKs FLUSH without writing anything to disk.
         * TODO: drain dirty COW state and persist metadata before returning OK.
         */
        /* Walk to status descriptor */
        CurrentIdx = Desc.Next;
        while (ChainDepth < Vq->Num)
        {
            /* Check for descriptor chain cycle */
            if (CurrentIdx >= Vq->Num)
            {
                ROSV_ERR("VirtIO-blk: descriptor index %u out of range (max %u)", CurrentIdx, Vq->Num);
                break;
            }
            if (VisitedBitmapFlush[CurrentIdx / 8] & (1 << (CurrentIdx % 8)))
            {
                ROSV_ERR("VirtIO-blk: descriptor chain cycle detected at index %u", CurrentIdx);
                break;
            }
            VisitedBitmapFlush[CurrentIdx / 8] |= (1 << (CurrentIdx % 8));

            if (!RosvVirtqueueReadDesc(Vm, Vq, CurrentIdx, &Desc))
                break;
            if (!(Desc.Flags & VRING_DESC_F_NEXT))
            {
                if (Desc.Len >= 1 && (Desc.Flags & VRING_DESC_F_WRITE))
                {
                    if (!RosvVirtioBlkWriteStatus(Vm, Desc.Addr, VIRTIO_BLK_S_OK))
                    {
                        ROSV_ERR("virtio-blk: FLUSH status write failed at GPA=0x%llX",
                                 Desc.Addr);
                    }
                    else
                    {
                        BytesWritten += 1;
                    }
                }
                break;
            }
            CurrentIdx = Desc.Next;
            ChainDepth++;
        }
        break;
    }

    default:
    {
        /* Bitmap to detect descriptor chain cycles (supports up to 256 descriptors) */
        UCHAR VisitedBitmapUnsupp[32]; /* 256 bits */
        RtlZeroMemory(VisitedBitmapUnsupp, sizeof(VisitedBitmapUnsupp));

        ROSV_WARN("virtio-blk: unsupported request type %u", Header.Type);
        StatusByte = VIRTIO_BLK_S_UNSUPP;

        /* Walk to status descriptor and write UNSUPP */
        CurrentIdx = Desc.Next;
        while (ChainDepth < Vq->Num)
        {
            /* Check for descriptor chain cycle */
            if (CurrentIdx >= Vq->Num)
            {
                ROSV_ERR("VirtIO-blk: descriptor index %u out of range (max %u)", CurrentIdx, Vq->Num);
                break;
            }
            if (VisitedBitmapUnsupp[CurrentIdx / 8] & (1 << (CurrentIdx % 8)))
            {
                ROSV_ERR("VirtIO-blk: descriptor chain cycle detected at index %u", CurrentIdx);
                break;
            }
            VisitedBitmapUnsupp[CurrentIdx / 8] |= (1 << (CurrentIdx % 8));

            if (!RosvVirtqueueReadDesc(Vm, Vq, CurrentIdx, &Desc))
                break;
            if (!(Desc.Flags & VRING_DESC_F_NEXT))
            {
                if (Desc.Len >= 1 && (Desc.Flags & VRING_DESC_F_WRITE))
                {
                    if (!RosvVirtioBlkWriteStatus(Vm, Desc.Addr, VIRTIO_BLK_S_UNSUPP))
                    {
                        ROSV_ERR("virtio-blk: UNSUPP status write failed at GPA=0x%llX "
                                 "(type=%u)", Desc.Addr, Header.Type);
                    }
                    else
                    {
                        BytesWritten += 1;
                    }
                }
                break;
            }
            CurrentIdx = Desc.Next;
            ChainDepth++;
        }
        break;
    }
    }

    return BytesWritten;
}

/* ---- Async request queue helpers ----------------------------------------- */

/**
 * Try to enqueue a parsed request into the async ring buffer.
 * Returns TRUE on success, FALSE if the queue is full.
 */
static BOOLEAN
RosvAsyncQueueEnqueue(
    _Inout_ PROSV_VIRTIO_BLK_STATE State,
    _In_ ULONG DescIdx,
    _In_ ULONG Type,
    _In_ ULONG64 Sector,
    _In_ ULONG DataLen,
    _In_ ULONG64 DataGpa,
    _In_ ULONG64 StatusGpa)
{
    LONG Slot;
    LONG CurrentCount;

    CurrentCount = InterlockedCompareExchange(&State->AsyncQueue.Count, 0, 0);
    if (CurrentCount >= ROSV_BLK_ASYNC_QUEUE_SIZE)
        return FALSE;

    CurrentCount = InterlockedIncrement(&State->AsyncQueue.Count);
    if (CurrentCount > ROSV_BLK_ASYNC_QUEUE_SIZE)
    {
        InterlockedDecrement(&State->AsyncQueue.Count);
        return FALSE;
    }

    Slot = InterlockedIncrement(&State->AsyncQueue.Head) - 1;
    Slot = Slot % ROSV_BLK_ASYNC_QUEUE_SIZE;
    if (Slot < 0)
        Slot += ROSV_BLK_ASYNC_QUEUE_SIZE;

    State->AsyncQueue.Entries[Slot].DescIdx = DescIdx;
    State->AsyncQueue.Entries[Slot].Type = Type;
    State->AsyncQueue.Entries[Slot].Sector = Sector;
    State->AsyncQueue.Entries[Slot].DataLen = DataLen;
    State->AsyncQueue.Entries[Slot].DataGpa = DataGpa;
    State->AsyncQueue.Entries[Slot].StatusGpa = StatusGpa;

    KeMemoryBarrier();
    return TRUE;
}

/**
 * Dequeue one request from the async ring buffer.
 * Returns TRUE if an entry was available, FALSE if empty.
 */
static BOOLEAN
RosvAsyncQueueDequeue(
    _Inout_ PROSV_VIRTIO_BLK_STATE State,
    _Out_ PULONG DescIdx,
    _Out_ PULONG Type,
    _Out_ PULONG64 Sector,
    _Out_ PULONG DataLen,
    _Out_ PULONG64 DataGpa,
    _Out_ PULONG64 StatusGpa)
{
    LONG Slot;
    LONG CurrentCount;

    CurrentCount = InterlockedCompareExchange(&State->AsyncQueue.Count, 0, 0);
    if (CurrentCount <= 0)
        return FALSE;

    KeMemoryBarrier();

    Slot = InterlockedIncrement(&State->AsyncQueue.Tail) - 1;
    Slot = Slot % ROSV_BLK_ASYNC_QUEUE_SIZE;
    if (Slot < 0)
        Slot += ROSV_BLK_ASYNC_QUEUE_SIZE;

    *DescIdx = State->AsyncQueue.Entries[Slot].DescIdx;
    *Type = State->AsyncQueue.Entries[Slot].Type;
    *Sector = State->AsyncQueue.Entries[Slot].Sector;
    *DataLen = State->AsyncQueue.Entries[Slot].DataLen;
    *DataGpa = State->AsyncQueue.Entries[Slot].DataGpa;
    *StatusGpa = State->AsyncQueue.Entries[Slot].StatusGpa;

    InterlockedDecrement(&State->AsyncQueue.Count);
    return TRUE;
}

/**
 * Raise a used-buffer interrupt. Called by the async worker after a batch.
 */
static VOID
RosvAsyncRaiseInterrupt(
    _Inout_ PROSV_VIRTIO_BLK_STATE State)
{
    PROSV_VM Vm = State->OwnerVm;
    if (Vm == NULL)
        return;

    KeMemoryBarrier();

    State->InterruptStatus |= VIRTIO_INT_VRING;
    State->InterruptPending = TRUE;
    KeSetEvent(&Vm->Vcpu.HaltWakeEvent, IO_NO_INCREMENT, FALSE);
}

/**
 * Async worker thread. Waits for WorkAvailable, drains all queued requests,
 * pushes used-ring completions, and raises a single batched interrupt.
 * Runs at PASSIVE_LEVEL for demand-paged file I/O compatibility.
 *
 * TODO: The demand-paged I/O worker is single-threaded and rejects concurrent
 * requests with STATUS_DEVICE_BUSY ("demand worker request collision").
 * This async worker can submit multiple requests in a batch, causing collisions.
 * Fix: serialize demand-paged reads within this worker (process one request at
 * a time when the backend is demand-paged), or allow the demand worker to queue
 * multiple pending requests.
 */
static VOID
RosvVirtioBlkAsyncWorkerThread(
    _In_ PVOID Context)
{
    PROSV_VIRTIO_BLK_STATE State;
    PROSV_VM Vm;
    PROSV_VIRTQUEUE Vq;
    ULONG DescIdx, Type, DataLen, BytesWritten, BatchCount;
    ULONG64 Sector, DataGpa, StatusGpa;

    State = (PROSV_VIRTIO_BLK_STATE)Context;
    if (State == NULL)
    {
        PsTerminateSystemThread(STATUS_INVALID_PARAMETER);
        return;
    }

    Vm = State->OwnerVm;
    ROSV_TRACE("virtio-blk: async worker thread started");

    for (;;)
    {
        KeWaitForSingleObject(&State->AsyncQueue.WorkAvailable,
                              Executive, KernelMode, FALSE, NULL);

        if (InterlockedCompareExchange(&State->AsyncQueue.Running, 0, 0) == 0)
            break;

        BatchCount = 0;

        while (RosvAsyncQueueDequeue(State, &DescIdx, &Type, &Sector,
                                     &DataLen, &DataGpa, &StatusGpa))
        {
            /* Use queue 0 as the primary request queue for async processing.
             * The async path currently handles single-queue mode; multi-queue
             * async support can store QueueIndex in the entry if needed. */
            Vq = &State->Vqs[0];

            switch (Type)
            {
            case VIRTIO_BLK_T_IN:
            case VIRTIO_BLK_T_OUT:
                BytesWritten = RosvVirtioBlkProcessRequest(State, Vq, (USHORT)DescIdx);
                break;
            case VIRTIO_BLK_T_FLUSH:
                BytesWritten = 1;
                break;
            case VIRTIO_BLK_T_GET_ID:
                BytesWritten = DataLen + 1;
                break;
            default:
                BytesWritten = 1;
                break;
            }

            if (!RosvVirtqueuePushUsed(Vm, Vq, DescIdx, BytesWritten))
                ROSV_ERR("virtio-blk: async worker PushUsed failed for desc %u", DescIdx);

            BatchCount++;

            if (InterlockedCompareExchange(&State->AsyncQueue.Running, 0, 0) == 0)
                break;
        }

        if (BatchCount > 0)
            RosvAsyncRaiseInterrupt(State);

        KeSetEvent(&State->AsyncQueue.WorkComplete, IO_NO_INCREMENT, FALSE);
    }

    ROSV_TRACE("virtio-blk: async worker thread exiting");
    PsTerminateSystemThread(STATUS_SUCCESS);
}

/**
 * Parse a virtio-blk descriptor chain and either enqueue it for async
 * processing or fall back to synchronous inline processing.
 */
static BOOLEAN
RosvVirtioBlkEnqueueOrProcess(
    _Inout_ PROSV_VIRTIO_BLK_STATE State,
    _In_ PROSV_VIRTQUEUE Vq,
    _In_ USHORT HeadIdx)
{
    PROSV_VM Vm = State->OwnerVm;
    VRING_DESC Desc;
    VIRTIO_BLK_REQ_HDR Header;
    NTSTATUS IoStatus;
    ULONG BytesWritten;

    if (!RosvVirtqueueReadDesc(Vm, Vq, HeadIdx, &Desc))
        return FALSE;
    if (Desc.Len < sizeof(VIRTIO_BLK_REQ_HDR))
        return FALSE;

    IoStatus = RosvMemoryCopyFromGpa(Vm, &Header, Desc.Addr, sizeof(VIRTIO_BLK_REQ_HDR));
    if (!NT_SUCCESS(IoStatus))
        return FALSE;
    if (!(Desc.Flags & VRING_DESC_F_NEXT))
        return FALSE;

    switch (Header.Type)
    {
    case VIRTIO_BLK_T_IN:
    case VIRTIO_BLK_T_OUT:
        if (RosvAsyncQueueEnqueue(State, (ULONG)HeadIdx, Header.Type,
                                  Header.Sector, 0, 0, 0))
            return TRUE;
        /* Async queue full: synchronous fallback */
        BytesWritten = RosvVirtioBlkProcessRequest(State, Vq, HeadIdx);
        RosvVirtqueuePushUsed(Vm, Vq, (ULONG)HeadIdx, BytesWritten);
        return TRUE;

    case VIRTIO_BLK_T_FLUSH:
    {
        USHORT CurrentIdx = Desc.Next;
        ULONG ChainDepth = 0;
        UCHAR Visited[32];
        BOOLEAN StatusWritten = FALSE;

        RtlZeroMemory(Visited, sizeof(Visited));
        while (ChainDepth < Vq->Num)
        {
            if (CurrentIdx >= Vq->Num)
                break;
            if (RosvVirtqueueCheckAndMarkVisited(Visited, CurrentIdx))
                break;
            if (!RosvVirtqueueReadDesc(Vm, Vq, CurrentIdx, &Desc))
                break;
            if (!(Desc.Flags & VRING_DESC_F_NEXT))
            {
                if (Desc.Len >= 1 && (Desc.Flags & VRING_DESC_F_WRITE))
                {
                    RosvVirtioBlkWriteStatus(Vm, Desc.Addr, VIRTIO_BLK_S_OK);
                    StatusWritten = TRUE;
                }
                break;
            }
            CurrentIdx = Desc.Next;
            ChainDepth++;
        }

        if (RosvAsyncQueueEnqueue(State, (ULONG)HeadIdx, VIRTIO_BLK_T_FLUSH, 0, 0, 0, 0))
            return TRUE;
        RosvVirtqueuePushUsed(Vm, Vq, (ULONG)HeadIdx, StatusWritten ? 1 : 0);
        return TRUE;
    }

    case VIRTIO_BLK_T_GET_ID:
        BytesWritten = RosvVirtioBlkProcessRequest(State, Vq, HeadIdx);
        if (RosvAsyncQueueEnqueue(State, (ULONG)HeadIdx, VIRTIO_BLK_T_GET_ID,
                                  0, BytesWritten > 1 ? BytesWritten - 1 : 0, 0, 0))
            return TRUE;
        RosvVirtqueuePushUsed(Vm, Vq, (ULONG)HeadIdx, BytesWritten);
        return TRUE;

    default:
        BytesWritten = RosvVirtioBlkProcessRequest(State, Vq, HeadIdx);
        if (RosvAsyncQueueEnqueue(State, (ULONG)HeadIdx, Header.Type, 0, 0, 0, 0))
            return TRUE;
        RosvVirtqueuePushUsed(Vm, Vq, (ULONG)HeadIdx, BytesWritten);
        return TRUE;
    }
}

/**
 * Process all pending requests in a specific virtqueue.
 * Called when the guest writes to QUEUE_NOTIFY with the queue index.
 *
 * When the async worker is running, IN/OUT requests are enqueued to the
 * async ring buffer. The worker thread handles I/O and completion.
 * FLUSH/GET_ID/unsupported types are pre-completed inline.
 * If the async queue is full, processing falls back to synchronous inline.
 *
 * @param QueueIndex  The virtqueue index (0..NumQueues-1) being notified.
 */
static VOID
RosvVirtioBlkProcessQueue(
    _Inout_ PROSV_VIRTIO_BLK_STATE State,
    _In_ ULONG QueueIndex)
{
    PROSV_VM Vm = State->OwnerVm;
    PROSV_VIRTQUEUE Vq;
    USHORT AvailIdx;
    USHORT DescIdx;
    ULONG BytesWritten;
    ULONG ProcessedCount = 0;
    ULONG EnqueuedCount = 0;
    USHORT AvailFlags;
    BOOLEAN AsyncEnabled;

    if (QueueIndex >= State->NumQueues)
    {
        ROSV_WARN("virtio-blk: QUEUE_NOTIFY for queue %u but only %u queues exist",
                  QueueIndex, State->NumQueues);
        return;
    }

    Vq = &State->Vqs[QueueIndex];

    if (!Vq->Ready)
    {
        ROSV_WARN("virtio-blk: QUEUE_NOTIFY for queue %u but queue not ready", QueueIndex);
        return;
    }

    if (!Vq->DescGpa || !Vq->AvailGpa || !Vq->UsedGpa)
    {
        ROSV_ERR("virtio-blk: QUEUE_NOTIFY but ring addresses not set "
                 "(desc=0x%llX avail=0x%llX used=0x%llX)",
                 Vq->DescGpa, Vq->AvailGpa, Vq->UsedGpa);
        return;
    }

    if (RosvVirtioBlkGetCapacityBytes(State) == 0)
    {
        ROSV_ERR("virtio-blk: QUEUE_NOTIFY but no disk capacity (no disk attached)");
        return;
    }

    /* Verify the appropriate backing is present based on mode */
    if (State->Mode == ROSV_DISK_MODE_DEMAND_PAGED)
    {
        if (!State->DiskFileHandle)
        {
            ROSV_ERR("virtio-blk: QUEUE_NOTIFY but demand-paged mode has no file handle");
            return;
        }
    }
    else
    {
        if (!State->DiskImageBase)
        {
            ROSV_ERR("virtio-blk: QUEUE_NOTIFY but ramdisk mode has no disk image base");
            return;
        }
    }

    KeMemoryBarrier();
    AvailIdx = RosvVirtqueueReadAvailIdx(Vm, Vq);
    KeMemoryBarrier();

    {
        USHORT NumPending = (USHORT)(AvailIdx - Vq->LastAvailIdx);
        if (NumPending > (USHORT)Vq->Num)
        {
            ROSV_ERR("virtio-blk: avail idx jump too large: avail_idx=%u last_avail=%u "
                     "delta=%u queue_size=%u -- refusing to process",
                     AvailIdx, Vq->LastAvailIdx, NumPending, Vq->Num);
            return;
        }
    }

    /* Determine if async path is available */
    AsyncEnabled = (InterlockedCompareExchange(&State->AsyncQueue.Running, 0, 0) != 0);

    while (Vq->LastAvailIdx != AvailIdx)
    {
        DescIdx = RosvVirtqueueReadAvailRing(Vm, Vq, Vq->LastAvailIdx);

        if (DescIdx >= (USHORT)Vq->Num)
        {
            ROSV_ERR("virtio-blk: avail ring entry %u has desc index %u >= queue size %u",
                     Vq->LastAvailIdx, DescIdx, Vq->Num);
            Vq->LastAvailIdx++;
            ProcessedCount++;
            continue;
        }

        if (AsyncEnabled)
        {
            if (!RosvVirtioBlkEnqueueOrProcess(State, Vq, DescIdx))
            {
                ROSV_ERR("virtio-blk: async parse failed for desc %u, skipping", DescIdx);
            }
            else
            {
                EnqueuedCount++;
            }
        }
        else
        {
            BytesWritten = RosvVirtioBlkProcessRequest(State, Vq, DescIdx);

            if (!RosvVirtqueuePushUsed(Vm, Vq, (ULONG)DescIdx, BytesWritten))
            {
                ROSV_ERR("virtio-blk: PushUsed failed for desc %u, aborting batch", DescIdx);
                Vq->LastAvailIdx++;
                ProcessedCount++;
                break;
            }
        }

        Vq->LastAvailIdx++;
        ProcessedCount++;

        if (ProcessedCount > Vq->Num)
        {
            ROSV_ERR("virtio-blk: processed %u requests (> queue size %u), stopping",
                     ProcessedCount, Vq->Num);
            break;
        }
    }

    if (EnqueuedCount > 0)
    {
        KeSetEvent(&State->AsyncQueue.WorkAvailable, IO_NO_INCREMENT, FALSE);
    }

    /* Raise interrupt for synchronous completions only.
     * Async completions raise their own interrupt from the worker thread. */
    if (ProcessedCount > 0 && ProcessedCount > EnqueuedCount)
    {
        KeMemoryBarrier();

        AvailFlags = RosvVirtqueueReadAvailFlags(Vm, Vq);
        if (AvailFlags & VRING_AVAIL_F_NO_INTERRUPT)
        {
            static ULONG NoInterruptHintCount;
            if (NoInterruptHintCount < 8 || (NoInterruptHintCount % 1024) == 0)
            {
                ROSV_TRACE("virtio-blk: avail flags request NO_INTERRUPT (flags=0x%X), "
                           "raising completion IRQ anyway",
                           AvailFlags);
            }
            NoInterruptHintCount++;
        }

        State->InterruptStatus |= VIRTIO_INT_VRING;
        State->InterruptPending = TRUE;

        if (State->OwnerVm != NULL)
            KeSetEvent(&State->OwnerVm->Vcpu.HaltWakeEvent, IO_NO_INCREMENT, FALSE);
    }
}

/* ---- MMIO register read handler ----------------------------------------- */

/**
 * Read a virtio MMIO register from the device config space.
 */
static ULONG
RosvVirtioBlkReadConfig(
    _In_ PROSV_VIRTIO_BLK_STATE State,
    _In_ ULONG Offset,
    _In_ ULONG Size)
{
    ULONG Value = 0;
    ULONG ConfigSize = sizeof(VIRTIO_BLK_CONFIG);
    PUCHAR ConfigBytes = (PUCHAR)&State->Config;

    if (Offset >= ConfigSize)
    {
        ROSV_WARN("virtio-blk: config read beyond end: offset=0x%X config_size=0x%X",
                  Offset, ConfigSize);
        return 0;
    }

    /* Read 1/2/4 bytes from the config structure */
    switch (Size)
    {
    case 1:
        Value = ConfigBytes[Offset];
        break;
    case 2:
        if (Offset + 2 <= ConfigSize)
            Value = *(USHORT *)(ConfigBytes + Offset);
        else
            Value = ConfigBytes[Offset];
        break;
    case 4:
        if (Offset + 4 <= ConfigSize)
            Value = *(ULONG *)(ConfigBytes + Offset);
        else if (Offset + 2 <= ConfigSize)
            Value = *(USHORT *)(ConfigBytes + Offset);
        else
            Value = ConfigBytes[Offset];
        break;
    default:
        ROSV_WARN("virtio-blk: unsupported config read size %u", Size);
        break;
    }

    return Value;
}

BOOLEAN
RosvVirtioBlkMmioRead(
    _In_ PROSV_VIRTIO_BLK_STATE State,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG Size,
    _Out_ PULONG64 Value)
{
    ULONG Offset;
    ULONG Result = 0;
    PROSV_VIRTQUEUE SelectedVq;

    if (GuestPhysicalAddress < ROSV_VIRTIO_BLK_MMIO_BASE ||
        GuestPhysicalAddress >= ROSV_VIRTIO_BLK_MMIO_BASE + ROSV_VIRTIO_BLK_MMIO_SIZE)
    {
        return FALSE;
    }

    Offset = (ULONG)(GuestPhysicalAddress - ROSV_VIRTIO_BLK_MMIO_BASE);

    /* Config space starts at offset 0x100 */
    if (Offset >= VIRTIO_MMIO_CONFIG_SPACE)
    {
        Result = RosvVirtioBlkReadConfig(State, Offset - VIRTIO_MMIO_CONFIG_SPACE, Size);
        *Value = (ULONG64)Result;
        return TRUE;
    }

    /* Select the appropriate virtqueue for queue-specific registers.
     * Returns NULL for out-of-range QueueSel (returns 0 for those reads). */
    if (State->QueueSel < State->NumQueues)
        SelectedVq = &State->Vqs[State->QueueSel];
    else
        SelectedVq = NULL;

    switch (Offset)
    {
    case VIRTIO_MMIO_MAGIC_VALUE:
        Result = VIRTIO_MMIO_MAGIC;
        break;

    case VIRTIO_MMIO_VERSION:
        Result = VIRTIO_MMIO_VERSION_2;
        break;

    case VIRTIO_MMIO_DEVICE_ID:
        Result = VIRTIO_ID_BLOCK;
        break;

    case VIRTIO_MMIO_VENDOR_ID:
        Result = VIRTIO_VENDOR_ROSV;
        break;

    case VIRTIO_MMIO_DEVICE_FEATURES:
        if (State->DeviceFeaturesSelPage == 0)
            Result = (ULONG)(State->DeviceFeatures & 0xFFFFFFFF);
        else if (State->DeviceFeaturesSelPage == 1)
            Result = (ULONG)(State->DeviceFeatures >> 32);
        else
            Result = 0;
        break;

    case VIRTIO_MMIO_QUEUE_NUM_MAX:
        /* Return 0 for non-existent queues (signals queue not available) */
        Result = SelectedVq ? ROSV_VIRTIO_QUEUE_SIZE_MAX : 0;
        break;

    case VIRTIO_MMIO_QUEUE_READY:
        Result = (SelectedVq && SelectedVq->Ready) ? 1 : 0;
        break;

    case VIRTIO_MMIO_INTERRUPT_STATUS:
        Result = State->InterruptStatus;
        break;

    case VIRTIO_MMIO_STATUS:
        Result = State->Status;
        break;

    case VIRTIO_MMIO_CONFIG_GENERATION:
        Result = State->ConfigGeneration;
        break;

    default:
        Result = 0;
        break;
    }

    *Value = (ULONG64)Result;
    return TRUE;
}

/* ---- MMIO register write handler ---------------------------------------- */

BOOLEAN
RosvVirtioBlkMmioWrite(
    _Inout_ PROSV_VIRTIO_BLK_STATE State,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG Size,
    _In_ ULONG64 Value)
{
    ULONG Offset;
    ULONG Val32 = (ULONG)(Value & 0xFFFFFFFF);
    PROSV_VIRTQUEUE SelectedVq;

    if (GuestPhysicalAddress < ROSV_VIRTIO_BLK_MMIO_BASE ||
        GuestPhysicalAddress >= ROSV_VIRTIO_BLK_MMIO_BASE + ROSV_VIRTIO_BLK_MMIO_SIZE)
    {
        return FALSE;
    }

    Offset = (ULONG)(GuestPhysicalAddress - ROSV_VIRTIO_BLK_MMIO_BASE);

    /* Config space writes (offset >= 0x100) - currently read-only for virtio-blk */
    if (Offset >= VIRTIO_MMIO_CONFIG_SPACE)
    {
        /* Config space is read-only for virtio-blk; ignore writes silently */
        return TRUE;
    }

    /* Select the appropriate virtqueue for queue-specific registers */
    if (State->QueueSel < State->NumQueues)
        SelectedVq = &State->Vqs[State->QueueSel];
    else
        SelectedVq = NULL;

    switch (Offset)
    {
    case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
        State->DeviceFeaturesSelPage = Val32;
        break;

    case VIRTIO_MMIO_DRIVER_FEATURES:
        if (State->DriverFeaturesSelPage == 0)
        {
            State->DriverFeatures = (State->DriverFeatures & 0xFFFFFFFF00000000ULL) | Val32;
        }
        else if (State->DriverFeaturesSelPage == 1)
        {
            State->DriverFeatures = (State->DriverFeatures & 0x00000000FFFFFFFFULL) |
                                    ((ULONG64)Val32 << 32);
        }
        break;

    case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
        State->DriverFeaturesSelPage = Val32;
        break;

    case VIRTIO_MMIO_QUEUE_SEL:
        State->QueueSel = Val32;
        if (Val32 >= State->NumQueues)
        {
            /* Not an error -- the driver probes queues by reading QUEUE_NUM_MAX
             * after setting QUEUE_SEL.  We return 0 for non-existent queues. */
            ROSV_TRACE("virtio-blk: guest selected queue %u (NumQueues=%u)",
                       Val32, State->NumQueues);
        }
        break;

    case VIRTIO_MMIO_QUEUE_NUM:
        if (SelectedVq != NULL)
        {
            if (Val32 > ROSV_VIRTIO_QUEUE_SIZE_MAX)
            {
                ROSV_ERR("virtio-blk: guest requested queue size %u > max %u",
                         Val32, ROSV_VIRTIO_QUEUE_SIZE_MAX);
                Val32 = ROSV_VIRTIO_QUEUE_SIZE_MAX;
            }
            SelectedVq->Num = Val32;
            ROSV_TRACE("virtio-blk: queue %u size set to %u", State->QueueSel, Val32);
        }
        break;

    case VIRTIO_MMIO_QUEUE_READY:
        if (SelectedVq != NULL)
        {
            SelectedVq->Ready = (Val32 != 0);
            ROSV_TRACE("virtio-blk: queue %u ready = %u", State->QueueSel, Val32);
            if (SelectedVq->Ready)
            {
                ROSV_TRACE("virtio-blk: queue %u configured: num=%u desc=0x%llX avail=0x%llX used=0x%llX",
                           State->QueueSel, SelectedVq->Num, SelectedVq->DescGpa,
                           SelectedVq->AvailGpa, SelectedVq->UsedGpa);
            }
        }
        break;

    case VIRTIO_MMIO_QUEUE_NOTIFY:
        /* The value written to QUEUE_NOTIFY is the queue index to notify,
         * NOT State->QueueSel.  Per virtio spec section 4.2.3.2. */
        if (Val32 < State->NumQueues)
        {
            RosvVirtioBlkProcessQueue(State, Val32);
        }
        else
        {
            ROSV_WARN("virtio-blk: QUEUE_NOTIFY for non-existent queue %u (NumQueues=%u)",
                      Val32, State->NumQueues);
        }
        break;

    case VIRTIO_MMIO_INTERRUPT_ACK:
        State->InterruptStatus &= ~Val32;
        if (State->InterruptStatus == 0)
        {
            State->InterruptPending = FALSE;
        }
        else
        {
            State->InterruptPending = TRUE;
            /* Wake vCPU if halted -- unacknowledged status bits remain */
            if (State->OwnerVm != NULL)
                KeSetEvent(&State->OwnerVm->Vcpu.HaltWakeEvent, IO_NO_INCREMENT, FALSE);
        }
        break;

    case VIRTIO_MMIO_STATUS:
        if (Val32 == 0)
        {
            ULONG i;
            /* Device reset -- zero all queues */
            ROSV_TRACE("virtio-blk: device RESET");
            State->Status = 0;
            State->DriverFeatures = 0;
            State->InterruptStatus = 0;
            State->InterruptPending = FALSE;
            State->QueueSel = 0;
            State->DeviceFeaturesSelPage = 0;
            State->DriverFeaturesSelPage = 0;
            for (i = 0; i < State->NumQueues; i++)
                RtlZeroMemory(&State->Vqs[i], sizeof(ROSV_VIRTQUEUE));
        }
        else
        {
            ULONG OldStatus = State->Status;
            State->Status = Val32;
            ROSV_TRACE("virtio-blk: STATUS = 0x%02X (was 0x%02X)%s%s%s%s%s",
                       Val32, OldStatus,
                       (Val32 & VIRTIO_STATUS_ACKNOWLEDGE) ? " ACK" : "",
                       (Val32 & VIRTIO_STATUS_DRIVER) ? " DRIVER" : "",
                       (Val32 & VIRTIO_STATUS_FEATURES_OK) ? " FEATURES_OK" : "",
                       (Val32 & VIRTIO_STATUS_DRIVER_OK) ? " DRIVER_OK" : "",
                       (Val32 & VIRTIO_STATUS_FAILED) ? " FAILED" : "");

            /* Validate feature negotiation when FEATURES_OK is set */
            if ((Val32 & VIRTIO_STATUS_FEATURES_OK) && !(OldStatus & VIRTIO_STATUS_FEATURES_OK))
            {
                /* Check that driver didn't request features we don't offer */
                ULONG64 UnsupportedFeatures = State->DriverFeatures & ~State->DeviceFeatures;
                if (UnsupportedFeatures)
                {
                    ROSV_WARN("virtio-blk: driver requested unsupported features: 0x%llX",
                              UnsupportedFeatures);
                    /* Clear FEATURES_OK to signal failure */
                    State->Status &= ~(ULONG)VIRTIO_STATUS_FEATURES_OK;
                }
                else
                {
                    ROSV_TRACE("virtio-blk: feature negotiation OK (driver=0x%llX)",
                               State->DriverFeatures);
                }
            }
        }
        break;

    case VIRTIO_MMIO_QUEUE_DESC_LOW:
        if (SelectedVq != NULL)
            SelectedVq->DescGpa = (SelectedVq->DescGpa & 0xFFFFFFFF00000000ULL) | Val32;
        break;

    case VIRTIO_MMIO_QUEUE_DESC_HIGH:
        if (SelectedVq != NULL)
            SelectedVq->DescGpa = (SelectedVq->DescGpa & 0x00000000FFFFFFFFULL) |
                                  ((ULONG64)Val32 << 32);
        break;

    case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
        if (SelectedVq != NULL)
            SelectedVq->AvailGpa = (SelectedVq->AvailGpa & 0xFFFFFFFF00000000ULL) | Val32;
        break;

    case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
        if (SelectedVq != NULL)
            SelectedVq->AvailGpa = (SelectedVq->AvailGpa & 0x00000000FFFFFFFFULL) |
                                   ((ULONG64)Val32 << 32);
        break;

    case VIRTIO_MMIO_QUEUE_USED_LOW:
        if (SelectedVq != NULL)
            SelectedVq->UsedGpa = (SelectedVq->UsedGpa & 0xFFFFFFFF00000000ULL) | Val32;
        break;

    case VIRTIO_MMIO_QUEUE_USED_HIGH:
        if (SelectedVq != NULL)
            SelectedVq->UsedGpa = (SelectedVq->UsedGpa & 0x00000000FFFFFFFFULL) |
                                  ((ULONG64)Val32 << 32);
        break;

    default:
        break;
    }

    return TRUE;
}

/* ---- Initialization and lifecycle --------------------------------------- */

NTSTATUS
RosvVirtioBlkInitialize(
    _Out_ PROSV_VIRTIO_BLK_STATE State,
    _In_ PROSV_VM Vm,
    _In_opt_ PVOID DiskImageBase,
    _In_ ULONG64 DiskImageSize,
    _In_ BOOLEAN ReadOnly,
    _In_ ULONG BackendType)
{
    NTSTATUS Status;

    if (BackendType != ROSV_DISK_BACKEND_RAW &&
        BackendType != ROSV_DISK_BACKEND_VHDX)
    {
        ROSV_ERR("virtio-blk: invalid backend type %u", BackendType);
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(State, sizeof(ROSV_VIRTIO_BLK_STATE));

    State->BackendType = BackendType;
    State->Mode = ROSV_DISK_MODE_RAMDISK; /* Default; caller can override after init */
    State->DiskFileHandle = NULL;
    State->DiskFileHandleAlt = NULL;
    State->PreferredReadHandle = 0;
    State->OwnerVm = Vm;
    State->DiskImageBase = DiskImageBase;
    State->DiskImageSize = DiskImageSize;
    State->ReadOnly = ReadOnly;

    /* Multi-queue setup: advertise ROSV_BLK_NUM_QUEUES request queues.
     * If the guest does not negotiate VIRTIO_BLK_F_MQ, only queue 0 will be
     * used (single-queue default).  The NumQueues field in config space tells
     * the driver how many queues the device offers. */
    State->NumQueues = ROSV_BLK_NUM_QUEUES;

    /* Set device features */
    State->DeviceFeatures = VIRTIO_F_VERSION_1;
    if (ReadOnly)
        State->DeviceFeatures |= VIRTIO_BLK_F_RO;
    State->DeviceFeatures |= VIRTIO_BLK_F_BLK_SIZE;
    State->DeviceFeatures |= VIRTIO_BLK_F_FLUSH;
    State->DeviceFeatures |= VIRTIO_BLK_F_SEG_MAX;
    State->DeviceFeatures |= VIRTIO_BLK_F_SIZE_MAX;
    State->DeviceFeatures |= VIRTIO_BLK_F_MQ;

    /* Build device configuration */
    if (State->BackendType == ROSV_DISK_BACKEND_VHDX && Vm)
    {
        /* For VHDX: capacity comes from the parsed virtual disk size */
        ULONG64 VirtSize = Vm->VhdxState.VirtualDiskSize;
        ULONG LogSectorSize = Vm->VhdxState.LogicalSectorSize;
        State->Config.Capacity = VirtSize / VIRTIO_SECTOR_SIZE;
        State->Config.BlkSize = (LogSectorSize > 0) ? LogSectorSize : (ULONG)VIRTIO_SECTOR_SIZE;
        ROSV_TRACE("virtio-blk: VHDX backend: virtual_size=%llu, capacity=%llu sectors, blk_size=%u",
                   VirtSize, State->Config.Capacity, State->Config.BlkSize);
    }
    else
    {
        /* Raw backend: capacity = file size / sector size */
        State->Config.Capacity = DiskImageSize / VIRTIO_SECTOR_SIZE;
        State->Config.BlkSize = (ULONG)VIRTIO_SECTOR_SIZE;
    }
    State->Config.SizeMax = 0x10000;    /* 64KB max segment */
    State->Config.SegMax = 128;         /* Max segments per request */
    State->Config.Geometry.Cylinders = 0;
    State->Config.Geometry.Heads = 0;
    State->Config.Geometry.Sectors = 0;
    /* Topology -- zeroed (present only for correct offset alignment) */
    RtlZeroMemory(&State->Config.Topology, sizeof(State->Config.Topology));
    State->Config.Writeback = 0;
    State->Config.Unused0 = 0;
    /* NumQueues in config space (read by driver when VIRTIO_BLK_F_MQ is set) */
    State->Config.NumQueues = (USHORT)State->NumQueues;

    ROSV_TRACE("virtio-blk: initialized at MMIO 0x%llX-0x%llX, disk=%p file_size=%llu bytes "
               "capacity=%llu bytes (%llu sectors), %s, backend=%s, mode=%s, num_queues=%u",
               ROSV_VIRTIO_BLK_MMIO_BASE,
               ROSV_VIRTIO_BLK_MMIO_BASE + ROSV_VIRTIO_BLK_MMIO_SIZE - 1,
               DiskImageBase,
               DiskImageSize,
               RosvVirtioBlkGetCapacityBytes(State),
               State->Config.Capacity,
               ReadOnly ? "read-only" : "read-write",
               (State->BackendType == ROSV_DISK_BACKEND_VHDX) ? "VHDX" : "RAW",
               (State->Mode == ROSV_DISK_MODE_DEMAND_PAGED) ? "demand-paged" : "ramdisk",
               State->NumQueues);
    ROSV_TRACE("virtio-blk: features offered = 0x%llX", State->DeviceFeatures);

    /* Initialize COW sector cache */
    RosvCowInit(&State->CowCache);

    KeInitializeEvent(&State->DemandIoRequestEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&State->DemandIoCompleteEvent, SynchronizationEvent, FALSE);
    ExInitializeFastMutex(&State->DemandIoLock);
    State->DemandIoStop = 0;
    State->DemandIoRequestPending = 0;
    State->DemandIoStatus = STATUS_SUCCESS;

    {
        OBJECT_ATTRIBUTES ObjectAttributes;

        InitializeObjectAttributes(&ObjectAttributes,
                                   NULL,
                                   OBJ_KERNEL_HANDLE,
                                   NULL,
                                   NULL);

        Status = PsCreateSystemThread(&State->DemandIoThreadHandle,
                                      THREAD_ALL_ACCESS,
                                      &ObjectAttributes,
                                      NULL,
                                      NULL,
                                      RosvVirtioBlkDemandIoThreadProc,
                                      State);
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("virtio-blk: failed to create demand I/O worker thread, Status=0x%08X",
                     Status);
            RosvCowDestroy(&State->CowCache);
            return Status;
        }

        Status = ObReferenceObjectByHandle(State->DemandIoThreadHandle,
                                           THREAD_ALL_ACCESS,
                                           NULL,
                                           KernelMode,
                                           (PVOID *)&State->DemandIoThreadObject,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("virtio-blk: failed to reference demand I/O thread object, Status=0x%08X",
                     Status);
            ZwClose(State->DemandIoThreadHandle);
            State->DemandIoThreadHandle = NULL;
            RosvCowDestroy(&State->CowCache);
            return Status;
        }
    }

    /* Initialize async request queue and worker thread */
    State->AsyncQueue.Head = 0;
    State->AsyncQueue.Tail = 0;
    State->AsyncQueue.Count = 0;
    KeInitializeEvent(&State->AsyncQueue.WorkAvailable, SynchronizationEvent, FALSE);
    KeInitializeEvent(&State->AsyncQueue.WorkComplete, SynchronizationEvent, FALSE);
    InterlockedExchange(&State->AsyncQueue.Running, 1);
    State->AsyncQueue.WorkerThread = NULL;
    State->AsyncQueue.WorkerThreadObject = NULL;

    {
        OBJECT_ATTRIBUTES AsyncObjAttrs;

        InitializeObjectAttributes(&AsyncObjAttrs, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

        Status = PsCreateSystemThread(&State->AsyncQueue.WorkerThread,
                                      THREAD_ALL_ACCESS,
                                      &AsyncObjAttrs,
                                      NULL,
                                      NULL,
                                      RosvVirtioBlkAsyncWorkerThread,
                                      State);
        if (!NT_SUCCESS(Status))
        {
            ROSV_WARN("virtio-blk: failed to create async worker thread (Status=0x%08X), "
                      "falling back to synchronous processing", Status);
            InterlockedExchange(&State->AsyncQueue.Running, 0);
            /* Non-fatal: synchronous path will be used */
        }
        else
        {
            Status = ObReferenceObjectByHandle(State->AsyncQueue.WorkerThread,
                                               THREAD_ALL_ACCESS,
                                               NULL,
                                               KernelMode,
                                               (PVOID *)&State->AsyncQueue.WorkerThreadObject,
                                               NULL);
            if (!NT_SUCCESS(Status))
            {
                ROSV_WARN("virtio-blk: failed to reference async worker thread (Status=0x%08X)",
                          Status);
                InterlockedExchange(&State->AsyncQueue.Running, 0);
                KeSetEvent(&State->AsyncQueue.WorkAvailable, IO_NO_INCREMENT, FALSE);
                ZwClose(State->AsyncQueue.WorkerThread);
                State->AsyncQueue.WorkerThread = NULL;
            }
            else
            {
                ROSV_TRACE("virtio-blk: async worker thread created successfully");
            }
        }
    }

    return STATUS_SUCCESS;
}

VOID
RosvVirtioBlkDestroy(
    _Inout_ PROSV_VIRTIO_BLK_STATE State)
{
    ROSV_TRACE("virtio-blk: destroy (reads=%llu/%llu bytes, writes=%llu/%llu bytes)",
               State->ReadOps, State->ReadBytes,
               State->WriteOps, State->WriteBytes);

    /* Shut down the async worker thread first -- it may issue I/O requests
     * that depend on the demand I/O worker, so stop it before DemandIo. */
    if (State->AsyncQueue.WorkerThreadObject != NULL ||
        State->AsyncQueue.WorkerThread != NULL)
    {
        InterlockedExchange(&State->AsyncQueue.Running, 0);
        KeSetEvent(&State->AsyncQueue.WorkAvailable, IO_NO_INCREMENT, FALSE);

        if (State->AsyncQueue.WorkerThreadObject != NULL)
        {
            KeWaitForSingleObject(State->AsyncQueue.WorkerThreadObject,
                                  Executive,
                                  KernelMode,
                                  FALSE,
                                  NULL);
            ObDereferenceObject(State->AsyncQueue.WorkerThreadObject);
            State->AsyncQueue.WorkerThreadObject = NULL;
        }

        if (State->AsyncQueue.WorkerThread != NULL)
        {
            ZwClose(State->AsyncQueue.WorkerThread);
            State->AsyncQueue.WorkerThread = NULL;
        }

        ROSV_TRACE("virtio-blk: async worker thread stopped (remaining=%ld)",
                   State->AsyncQueue.Count);
    }

    if (State->DemandIoThreadObject != NULL || State->DemandIoThreadHandle != NULL)
    {
        InterlockedExchange(&State->DemandIoStop, 1);
        KeSetEvent(&State->DemandIoRequestEvent, IO_NO_INCREMENT, FALSE);

        if (State->DemandIoThreadObject != NULL)
        {
            KeWaitForSingleObject(State->DemandIoThreadObject,
                                  Executive,
                                  KernelMode,
                                  FALSE,
                                  NULL);
            ObDereferenceObject(State->DemandIoThreadObject);
            State->DemandIoThreadObject = NULL;
        }

        if (State->DemandIoThreadHandle != NULL)
        {
            ZwClose(State->DemandIoThreadHandle);
            State->DemandIoThreadHandle = NULL;
        }
    }

    /* Free COW cache before zeroing state */
    RosvCowDestroy(&State->CowCache);

    RtlZeroMemory(State, sizeof(ROSV_VIRTIO_BLK_STATE));
}

VOID
RosvVirtioBlkSetDiskImage(
    _Inout_ PROSV_VIRTIO_BLK_STATE State,
    _In_ PVOID DiskImageBase,
    _In_ ULONG64 DiskImageSize,
    _In_ BOOLEAN ReadOnly)
{
    ULONG64 CapacityBytes;

    State->DiskImageBase = DiskImageBase;
    State->DiskImageSize = DiskImageSize;
    State->ReadOnly = ReadOnly;

    /* Update config space */
    CapacityBytes = RosvVirtioBlkGetCapacityBytes(State);
    State->Config.Capacity = CapacityBytes / VIRTIO_SECTOR_SIZE;
    if (State->BackendType == ROSV_DISK_BACKEND_VHDX &&
        State->OwnerVm != NULL &&
        State->OwnerVm->VhdxState.LogicalSectorSize != 0)
    {
        State->Config.BlkSize = State->OwnerVm->VhdxState.LogicalSectorSize;
    }
    else
    {
        State->Config.BlkSize = (ULONG)VIRTIO_SECTOR_SIZE;
    }
    State->ConfigGeneration++;

    if (ReadOnly)
        State->DeviceFeatures |= VIRTIO_BLK_F_RO;
    else
        State->DeviceFeatures &= ~VIRTIO_BLK_F_RO;

    ROSV_TRACE("virtio-blk: disk image updated: %p, %llu bytes, %s",
               DiskImageBase, DiskImageSize,
               ReadOnly ? "read-only" : "read-write");
}

/* ---- COW (Copy-On-Write) sector cache ----------------------------------- */

/**
 * Initialize the COW cache. Zeros all hash buckets and counters.
 */
VOID
RosvCowInit(
    _Out_ PROSV_COW_CACHE Cache)
{
    RtlZeroMemory(Cache, sizeof(ROSV_COW_CACHE));
    ROSV_TRACE("COW: cache initialized (%u buckets)", ROSV_COW_HASH_SIZE);
}

/**
 * Destroy the COW cache. Frees all allocated entries and resets counters.
 */
VOID
RosvCowDestroy(
    _Inout_ PROSV_COW_CACHE Cache)
{
    ULONG i;

    for (i = 0; i < ROSV_COW_HASH_SIZE; i++)
    {
        PROSV_COW_ENTRY Entry = Cache->Buckets[i];
        while (Entry != NULL)
        {
            PROSV_COW_ENTRY Next = Entry->Next;
            ExFreePoolWithTag(Entry, ROSV_DRIVER_TAG);
            Entry = Next;
        }
        Cache->Buckets[i] = NULL;
    }

    ROSV_TRACE("COW: freed %llu dirty sectors (%llu KB)",
               Cache->DirtyCount,
               Cache->DirtyBytes / 1024);

    Cache->DirtyCount = 0;
    Cache->DirtyBytes = 0;
}

/**
 * Look up a sector in the COW cache.
 * Returns the entry if found, NULL otherwise.
 */
PROSV_COW_ENTRY
RosvCowLookup(
    _In_ PROSV_COW_CACHE Cache,
    _In_ ULONG64 Sector)
{
    ULONG Hash = (ULONG)(Sector & (ROSV_COW_HASH_SIZE - 1));
    PROSV_COW_ENTRY Entry = Cache->Buckets[Hash];

    while (Entry != NULL)
    {
        if (Entry->SectorNumber == Sector)
            return Entry;
        Entry = Entry->Next;
    }

    return NULL;
}

/**
 * Write a sector into the COW cache.
 * If the sector already exists, its data is overwritten.
 * Otherwise a new entry is allocated and prepended to the hash chain.
 */
NTSTATUS
RosvCowWrite(
    _Inout_ PROSV_COW_CACHE Cache,
    _In_ ULONG64 Sector,
    _In_ const VOID *Data)
{
    PROSV_COW_ENTRY Entry;
    ULONG Hash;

    /* Check if this sector is already cached */
    Entry = RosvCowLookup(Cache, Sector);
    if (Entry != NULL)
    {
        /* Overwrite existing entry */
        RtlCopyMemory(Entry->Data, Data, ROSV_COW_SECTOR_SIZE);
        return STATUS_SUCCESS;
    }

    /* Allocate a new entry */
    Entry = (PROSV_COW_ENTRY)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(ROSV_COW_ENTRY), ROSV_DRIVER_TAG);
    if (Entry == NULL)
    {
        ROSV_ERR("COW: failed to allocate entry for sector %llu", Sector);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Fill and prepend to hash chain */
    Entry->SectorNumber = Sector;
    RtlCopyMemory(Entry->Data, Data, ROSV_COW_SECTOR_SIZE);

    Hash = (ULONG)(Sector & (ROSV_COW_HASH_SIZE - 1));
    Entry->Next = Cache->Buckets[Hash];
    Cache->Buckets[Hash] = Entry;

    Cache->DirtyCount++;
    Cache->DirtyBytes += ROSV_COW_SECTOR_SIZE;

    /* Debug traces for first write and every 1000 sectors */
    if (Cache->DirtyCount == 1)
    {
        ROSV_TRACE("COW: first dirty sector %llu", Sector);
    }
    else if ((Cache->DirtyCount % 1000) == 0)
    {
        ROSV_TRACE("COW: %llu dirty sectors (%llu KB)",
                   Cache->DirtyCount, Cache->DirtyBytes / 1024);
    }

    return STATUS_SUCCESS;
}

/* ---- Interrupt helpers -------------------------------------------------- */

BOOLEAN
RosvVirtioBlkHasPendingInterrupt(
    _In_ PROSV_VIRTIO_BLK_STATE State)
{
    return State->InterruptPending;
}

VOID
RosvVirtioBlkClearPendingInterrupt(
    _Inout_ PROSV_VIRTIO_BLK_STATE State)
{
    /*
     * Mark one interrupt delivery as consumed by VM-entry injection logic.
     * For level-style semantics we may re-arm on LAPIC EOI while
     * InterruptStatus remains asserted.
     */
    State->InterruptPending = (State->InterruptStatus != 0) ? TRUE : FALSE;
}

VOID
RosvVirtioBlkOnGuestEoi(
    _Inout_ PROSV_VIRTIO_BLK_STATE State)
{
    ROSV_ASSERT(State != NULL, "State must not be NULL");
    if (State == NULL)
        return;

    if (State->InterruptStatus != 0)
    {
        State->InterruptPending = TRUE;
        /* Wake vCPU if halted so it can re-inject after EOI */
        if (State->OwnerVm != NULL)
            KeSetEvent(&State->OwnerVm->Vcpu.HaltWakeEvent, IO_NO_INCREMENT, FALSE);
    }
}
