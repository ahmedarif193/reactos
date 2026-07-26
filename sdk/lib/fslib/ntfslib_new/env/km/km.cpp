/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Kernelmode glue
 * COPYRIGHT:   Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 */

#include <ntifs.h>
#include <ntfs_km.h>
#include <ntfslib_new_internal.h>

PDEVICE_OBJECT PartDeviceObj = NULL;
ULONG BytesPerSector = 0;

#ifdef __cplusplus
extern "C" {
#endif

void*
NtfsAllocatePoolWithTag(POOL_TYPE PoolType, size_t Size, ULONG Tag)
{
    return ExAllocatePoolWithTag(PoolType, Size, Tag);
}

void NtfsFreePool(void* pObject)
{
    ExFreePool(pObject);
}

NTSTATUS
NtfsQuerySystemTime(_Out_ PULONGLONG NtfsTime)
{
    LARGE_INTEGER Current;

    if (!NtfsTime)
        return STATUS_INVALID_PARAMETER;
    KeQuerySystemTime(&Current);
    if (Current.QuadPart < 0)
        return STATUS_INVALID_DEVICE_STATE;
    *NtfsTime = (ULONGLONG)Current.QuadPart;
    return STATUS_SUCCESS;
}

#ifdef __cplusplus
}
#endif


static NTSTATUS WriteDisk(_In_ PDEVICE_OBJECT DeviceObject, _In_ ULONGLONG Offset,
                          _In_ ULONG Length, _In_ PUCHAR Buffer);

/* METADATA BLOCK CACHE *****************************************************/

/*
 * Every metadata lookup otherwise costs a synchronous disk round trip, and
 * directory walks revisit the same index and MFT blocks constantly. A small
 * direct-mapped cache of fixed blocks removes the repeats. Writes go straight
 * to the disk and drop whatever they overlap, so the cache never holds data
 * the volume does not.
 */
#define NTFS_CACHE_BLOCK_SHIFT 12
#define NTFS_CACHE_BLOCK_SIZE  (1UL << NTFS_CACHE_BLOCK_SHIFT)
#define NTFS_CACHE_SLOTS       2048
#define NTFS_CACHE_SLOT_MASK   (NTFS_CACHE_SLOTS - 1)

typedef struct _NTFS_CACHE_SLOT
{
    ULONGLONG Tag;
    PUCHAR Data;
    PDEVICE_OBJECT Owner;
    BOOLEAN Dirty;
} NTFS_CACHE_SLOT;

static NTFS_CACHE_SLOT NtfsCacheSlots[NTFS_CACHE_SLOTS];
static ULONG NtfsCacheDirtyCount = 0;

/* Above this many blocks held back, write some out before taking on more.
 * Only a slice is committed at a time so no single write pays for the whole
 * backlog. */
#define NTFS_CACHE_DIRTY_LIMIT 384
#define NTFS_CACHE_DIRTY_SLICE 128
static FAST_MUTEX NtfsCacheMutex;
static BOOLEAN NtfsCacheReady = FALSE;

#define NTFS_CACHE_EMPTY ((ULONGLONG)~0ULL)

static
VOID
NtfsCacheDiscardAll(VOID)
{
    ULONG Index;

    for (Index = 0; Index < NTFS_CACHE_SLOTS; Index++)
    {
        NtfsCacheSlots[Index].Tag = NTFS_CACHE_EMPTY;
        NtfsCacheSlots[Index].Dirty = FALSE;
    }
    NtfsCacheDirtyCount = 0;
}

static
VOID
NtfsCacheInitialize(VOID)
{
    if (!NtfsCacheReady)
    {
        ExInitializeFastMutex(&NtfsCacheMutex);
        RtlZeroMemory(NtfsCacheSlots, sizeof(NtfsCacheSlots));
        NtfsCacheDiscardAll();
        NtfsCacheReady = TRUE;
    }
}

/* Drops every block overlapping [Offset, Offset + Length). */
static
VOID
NtfsCacheInvalidateRange(_In_ ULONGLONG Offset, _In_ ULONG Length)
{
    ULONGLONG Block;
    ULONGLONG LastBlock;

    if (!NtfsCacheReady || !Length)
        return;

    Block = Offset >> NTFS_CACHE_BLOCK_SHIFT;
    LastBlock = (Offset + Length - 1) >> NTFS_CACHE_BLOCK_SHIFT;

    ExAcquireFastMutex(&NtfsCacheMutex);
    for (; Block <= LastBlock; Block++)
    {
        NTFS_CACHE_SLOT* Slot = &NtfsCacheSlots[Block & NTFS_CACHE_SLOT_MASK];

        if (Slot->Tag == Block && Slot->Owner == PartDeviceObj)
        {
            if (Slot->Dirty)
            {
                Slot->Dirty = FALSE;
                NtfsCacheDirtyCount--;
            }
            Slot->Tag = NTFS_CACHE_EMPTY;
        }
    }
    ExReleaseFastMutex(&NtfsCacheMutex);
}

/*
 * Writes go to the volume, so the blocks we already hold can simply take the
 * same bytes instead of being dropped. Discarding them would make every
 * metadata update cost a re-read of the surrounding index and MFT blocks.
 */
static
VOID
NtfsCacheUpdateRange(_In_ ULONGLONG Offset, _In_ ULONG Length,
                     _In_reads_bytes_(Length) PUCHAR Data)
{
    ULONG Remaining = Length;
    ULONGLONG Current = Offset;
    PUCHAR In = Data;

    if (!NtfsCacheReady || !Length)
        return;

    ExAcquireFastMutex(&NtfsCacheMutex);
    while (Remaining)
    {
        ULONGLONG Block = Current >> NTFS_CACHE_BLOCK_SHIFT;
        ULONG BlockOffset = (ULONG)(Current & (NTFS_CACHE_BLOCK_SIZE - 1));
        ULONG Chunk = min(Remaining, NTFS_CACHE_BLOCK_SIZE - BlockOffset);
        NTFS_CACHE_SLOT* Slot = &NtfsCacheSlots[Block & NTFS_CACHE_SLOT_MASK];

        if (Slot->Tag == Block && Slot->Data && Slot->Owner == PartDeviceObj)
            RtlCopyMemory(Slot->Data + BlockOffset, In, Chunk);

        In += Chunk;
        Current += Chunk;
        Remaining -= Chunk;
    }
    ExReleaseFastMutex(&NtfsCacheMutex);
}

/*
 * Metadata is rewritten constantly and usually overwritten again moments
 * later, so updates are held in the cache and committed in batches. Reads are
 * served from the same blocks, so a held-back write is never invisible to the
 * code that reads it back; callers that bypass the cache flush first.
 *
 * The disk transfer must happen with the mutex dropped: it waits on an IRP,
 * and the fast mutex holds APC_LEVEL where that completion could never run.
 */
static
NTSTATUS
NtfsCacheWriteBackOne(_In_ ULONGLONG Block)
{
    PUCHAR Staged;
    BOOLEAN Pending = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;
    NTFS_CACHE_SLOT* Slot;
    PDEVICE_OBJECT Target = NULL;

    Staged = (PUCHAR)ExAllocatePoolUninitialized(NonPagedPool,
                                                 NTFS_CACHE_BLOCK_SIZE,
                                                 'CftN');
    if (!Staged)
        return STATUS_INSUFFICIENT_RESOURCES;

    ExAcquireFastMutex(&NtfsCacheMutex);
    Slot = &NtfsCacheSlots[Block & NTFS_CACHE_SLOT_MASK];
    if (Slot->Tag == Block && Slot->Dirty && Slot->Data)
    {
        RtlCopyMemory(Staged, Slot->Data, NTFS_CACHE_BLOCK_SIZE);
        Target = Slot->Owner;
        Slot->Dirty = FALSE;
        NtfsCacheDirtyCount--;
        Pending = TRUE;
    }
    ExReleaseFastMutex(&NtfsCacheMutex);

    if (Pending)
    {
        Status = WriteDisk(Target,
                           Block << NTFS_CACHE_BLOCK_SHIFT,
                           NTFS_CACHE_BLOCK_SIZE,
                           Staged);
        if (!NT_SUCCESS(Status))
        {
            /* Put it back so the data is not simply lost. */
            ExAcquireFastMutex(&NtfsCacheMutex);
            Slot = &NtfsCacheSlots[Block & NTFS_CACHE_SLOT_MASK];
            if (Slot->Tag == Block && !Slot->Dirty)
            {
                Slot->Dirty = TRUE;
                NtfsCacheDirtyCount++;
            }
            ExReleaseFastMutex(&NtfsCacheMutex);
        }
    }

    ExFreePoolWithTag(Staged, 'CftN');
    return Status;
}

static
NTSTATUS
NtfsCacheFlushSome(_In_ ULONG Budget)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;
    ULONG Written = 0;

    if (!NtfsCacheReady)
        return STATUS_SUCCESS;

    for (Index = 0; Index < NTFS_CACHE_SLOTS && Written < Budget; Index++)
    {
        ULONGLONG Block;
        BOOLEAN Dirty;

        ExAcquireFastMutex(&NtfsCacheMutex);
        Block = NtfsCacheSlots[Index].Tag;
        Dirty = NtfsCacheSlots[Index].Dirty;
        ExReleaseFastMutex(&NtfsCacheMutex);

        if (Dirty && Block != NTFS_CACHE_EMPTY)
        {
            NTSTATUS One = NtfsCacheWriteBackOne(Block);

            if (!NT_SUCCESS(One))
                Status = One;
            Written++;
        }
    }
    return Status;
}

static
NTSTATUS
NtfsCacheFlushAll(VOID)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;

    if (!NtfsCacheReady)
        return STATUS_SUCCESS;

    for (Index = 0; Index < NTFS_CACHE_SLOTS; Index++)
    {
        ULONGLONG Block;
        BOOLEAN Dirty;

        ExAcquireFastMutex(&NtfsCacheMutex);
        Block = NtfsCacheSlots[Index].Tag;
        Dirty = NtfsCacheSlots[Index].Dirty;
        ExReleaseFastMutex(&NtfsCacheMutex);

        if (Dirty && Block != NTFS_CACHE_EMPTY)
        {
            NTSTATUS One = NtfsCacheWriteBackOne(Block);

            if (!NT_SUCCESS(One))
                Status = One;
        }
    }
    return Status;
}

/* Commits anything held for [Offset, Offset + Length) so the caller may read
 * or write those bytes on the disk directly. */
static
NTSTATUS
NtfsCacheFlushRange(_In_ ULONGLONG Offset, _In_ ULONG Length)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONGLONG Block;
    ULONGLONG LastBlock;

    if (!NtfsCacheReady || !Length || NtfsCacheDirtyCount == 0)
        return STATUS_SUCCESS;


    Block = Offset >> NTFS_CACHE_BLOCK_SHIFT;
    LastBlock = (Offset + Length - 1) >> NTFS_CACHE_BLOCK_SHIFT;

    for (; Block <= LastBlock; Block++)
    {
        NTSTATUS One;
        BOOLEAN Dirty;

        /* Nearly every block a large transfer walks is clean, and committing
         * one costs a staging buffer, so look before paying for it. */
        ExAcquireFastMutex(&NtfsCacheMutex);
        Dirty = (NtfsCacheSlots[Block & NTFS_CACHE_SLOT_MASK].Tag == Block &&
                 NtfsCacheSlots[Block & NTFS_CACHE_SLOT_MASK].Dirty);
        ExReleaseFastMutex(&NtfsCacheMutex);
        if (!Dirty)
            continue;

        One = NtfsCacheWriteBackOne(Block);
        if (!NT_SUCCESS(One))
            Status = One;
    }
    return Status;
}

/* TRUE when the whole block was served from memory. */
static
BOOLEAN
NtfsCacheCopyOut(_In_ ULONGLONG Block, _In_ ULONG BlockOffset,
                 _In_ ULONG Length, _Out_writes_bytes_(Length) PUCHAR Buffer)
{
    NTFS_CACHE_SLOT* Slot;
    BOOLEAN Hit = FALSE;

    if (!NtfsCacheReady)
        return FALSE;

    ExAcquireFastMutex(&NtfsCacheMutex);
    Slot = &NtfsCacheSlots[Block & NTFS_CACHE_SLOT_MASK];
    if (Slot->Tag == Block && Slot->Data && Slot->Owner == PartDeviceObj)
    {
        RtlCopyMemory(Buffer, Slot->Data + BlockOffset, Length);
        Hit = TRUE;
    }
    ExReleaseFastMutex(&NtfsCacheMutex);
    return Hit;
}

static
VOID
NtfsCacheInstall(_In_ ULONGLONG Block, _In_reads_bytes_(NTFS_CACHE_BLOCK_SIZE) PUCHAR Data)
{
    NTFS_CACHE_SLOT* Slot;

    if (!NtfsCacheReady)
        return;

    /* Never lose an update by dropping the block that is being replaced. */
    ExAcquireFastMutex(&NtfsCacheMutex);
    Slot = &NtfsCacheSlots[Block & NTFS_CACHE_SLOT_MASK];
    if (Slot->Dirty && (Slot->Tag != Block || Slot->Owner != PartDeviceObj))
    {
        ULONGLONG Victim = Slot->Tag;

        ExReleaseFastMutex(&NtfsCacheMutex);
        NtfsCacheWriteBackOne(Victim);
        ExAcquireFastMutex(&NtfsCacheMutex);
        Slot = &NtfsCacheSlots[Block & NTFS_CACHE_SLOT_MASK];
    }
    if (!Slot->Data)
    {
        Slot->Data = (PUCHAR)ExAllocatePoolUninitialized(NonPagedPool,
                                                         NTFS_CACHE_BLOCK_SIZE,
                                                         'CftN');
        if (!Slot->Data)
        {
            ExReleaseFastMutex(&NtfsCacheMutex);
            return;
        }
    }
    RtlCopyMemory(Slot->Data, Data, NTFS_CACHE_BLOCK_SIZE);
    Slot->Tag = Block;
    Slot->Owner = PartDeviceObj;
    Slot->Dirty = FALSE;
    ExReleaseFastMutex(&NtfsCacheMutex);
}

NTSTATUS
NtfsDiskInitializeKm(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG SectorBytes)
{
    if (!DeviceObject ||
        (SectorBytes != 512 && SectorBytes != 4096))
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Mount probes re-point the library at other volumes constantly. Blocks
     * remember which device they came from, so switching device keeps them
     * and simply stops them matching; nothing is discarded and no write is
     * lost. */
    NtfsCacheInitialize();
    PartDeviceObj = DeviceObject;
    BytesPerSector = SectorBytes;
    return STATUS_SUCCESS;
}

typedef struct _NTFS_DISK_IO_CONTEXT
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
    PVOID Buffer;
    ULONG Length;
} NTFS_DISK_IO_CONTEXT, *PNTFS_DISK_IO_CONTEXT;

static
NTSTATUS
NTAPI
NtfsDiskIoCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PNTFS_DISK_IO_CONTEXT IoContext;

    UNREFERENCED_PARAMETER(DeviceObject);

    IoContext = static_cast<PNTFS_DISK_IO_CONTEXT>(Context);
    IoContext->IoStatus = Irp->IoStatus;
    KeSetEvent(&IoContext->Event, IO_NO_INCREMENT, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
VOID
NtfsFreeDiskIoIrp(
    _In_ PIRP Irp,
    _In_ PNTFS_DISK_IO_CONTEXT IoContext)
{
    PMDL Mdl;

    if (FlagOn(Irp->Flags, IRP_BUFFERED_IO))
    {
        if (FlagOn(Irp->Flags, IRP_INPUT_OPERATION) &&
            !NT_ERROR(IoContext->IoStatus.Status) &&
            IoContext->IoStatus.Status != STATUS_VERIFY_REQUIRED)
        {
            ULONG_PTR BytesToCopy;

            BytesToCopy = min(IoContext->IoStatus.Information,
                              static_cast<ULONG_PTR>(IoContext->Length));
            RtlCopyMemory(IoContext->Buffer,
                          Irp->AssociatedIrp.SystemBuffer,
                          BytesToCopy);
        }

        if (FlagOn(Irp->Flags, IRP_DEALLOCATE_BUFFER))
            ExFreePool(Irp->AssociatedIrp.SystemBuffer);
    }

    while ((Mdl = Irp->MdlAddress))
    {
        Irp->MdlAddress = Mdl->Next;
        if (FlagOn(Mdl->MdlFlags, MDL_PAGES_LOCKED))
            MmUnlockPages(Mdl);
        IoFreeMdl(Mdl);
    }

    IoFreeIrp(Irp);
}

static
NTSTATUS
NtfsPerformDiskIo(
    _In_ UCHAR MajorFunction,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _Inout_updates_bytes_(Length) PUCHAR Buffer)
{
    NTFS_DISK_IO_CONTEXT IoContext;
    PIO_STACK_LOCATION IoStack;
    PIRP Irp;
    PUCHAR IoBuffer;
    ULONG_PTR BytesToCopy;
    NTSTATUS Status;

    PAGED_CODE();

    if (!DeviceObject || (Length != 0 && !Buffer))
        return STATUS_INVALID_PARAMETER;

    IoBuffer = Buffer;
    if (Length != 0)
    {
        IoBuffer = new(NonPagedPool, TAG_NTFS) UCHAR[Length];
        if (!IoBuffer)
            return STATUS_INSUFFICIENT_RESOURCES;
        if (MajorFunction == IRP_MJ_WRITE)
            RtlCopyMemory(IoBuffer, Buffer, Length);
    }

    KeInitializeEvent(&IoContext.Event, NotificationEvent, FALSE);
    IoContext.IoStatus.Status = STATUS_PENDING;
    IoContext.IoStatus.Information = 0;
    IoContext.Buffer = IoBuffer;
    IoContext.Length = Length;

    /*
     * IoBuildSynchronousFsdRequest completes through a kernel APC. Filesystem
     * callers can have normal kernel APCs disabled, so waiting on its event
     * would deadlock. Stop completion at our routine and own the IRP instead.
     */
    Irp = IoBuildAsynchronousFsdRequest(MajorFunction,
                                        DeviceObject,
                                        IoBuffer,
                                        Length,
                                        reinterpret_cast<PLARGE_INTEGER>(&Offset),
                                        &IoContext.IoStatus);
    if (!Irp)
    {
        if (IoBuffer != Buffer)
            delete[] IoBuffer;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    IoStack = IoGetNextIrpStackLocation(Irp);
    SetFlag(IoStack->Flags, SL_OVERRIDE_VERIFY_VOLUME);
    IoSetCompletionRoutine(Irp,
                           NtfsDiskIoCompletion,
                           &IoContext,
                           TRUE,
                           TRUE,
                           TRUE);

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&IoContext.Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    }

    NT_ASSERT(IoContext.IoStatus.Status != STATUS_PENDING);
    Status = IoContext.IoStatus.Status;
    NtfsFreeDiskIoIrp(Irp, &IoContext);

    if (MajorFunction == IRP_MJ_READ &&
        NT_SUCCESS(Status) &&
        IoBuffer != Buffer)
    {
        BytesToCopy = min(IoContext.IoStatus.Information,
                          static_cast<ULONG_PTR>(Length));
        RtlCopyMemory(Buffer, IoBuffer, BytesToCopy);
    }
    if (IoBuffer != Buffer)
        delete[] IoBuffer;

    NT_ASSERT(Status != STATUS_VERIFY_REQUIRED);
    return Status;
}

NTSTATUS
ReadDisk(_In_ PDEVICE_OBJECT DeviceObject,
         _In_ ULONGLONG Offset,
         _In_ ULONG Length,
         _Out_ PUCHAR Buffer)
{
    return NtfsPerformDiskIo(IRP_MJ_READ,
                             DeviceObject,
                             Offset,
                             Length,
                             Buffer);
}

NTSTATUS
WriteDisk(_In_ PDEVICE_OBJECT DeviceObject,
          _In_ ULONGLONG Offset,
          _In_ ULONG Length,
          _In_ PUCHAR Buffer)
{
    return NtfsPerformDiskIo(IRP_MJ_WRITE,
                             DeviceObject,
                             Offset,
                             Length,
                             Buffer);
}

#ifdef __cplusplus
extern "C" {
#endif

NTSTATUS
NtfsReadVolume(_In_    ULONGLONG Offset,
               _In_    ULONG Length,
               _Inout_ PUCHAR Buffer)
{
    NTSTATUS Status;
    PUCHAR ReadBuffer;
    ULONGLONG SectorAlignedOffset;
    ULONG SectorAlignedLength;

    ASSERT(Length);

    /*
     * Metadata is read over and over from the same handful of index and MFT
     * blocks, so satisfy the request from whole cached blocks when we can and
     * otherwise fetch the covering block once, keeping it for next time.
     */
    /*
     * Only metadata-sized requests go through the cache. Splitting a bulk
     * streaming read into block-sized fetches would cost far more than the
     * repeats it saves.
     */
    if (NtfsCacheReady && Length <= NTFS_CACHE_BLOCK_SIZE)
    {
        ULONG Remaining = Length;
        ULONGLONG Current = Offset;
        PUCHAR Out = Buffer;

        while (Remaining)
        {
            ULONGLONG Block = Current >> NTFS_CACHE_BLOCK_SHIFT;
            ULONG BlockOffset = (ULONG)(Current & (NTFS_CACHE_BLOCK_SIZE - 1));
            ULONG Chunk = min(Remaining, NTFS_CACHE_BLOCK_SIZE - BlockOffset);

            if (!NtfsCacheCopyOut(Block, BlockOffset, Chunk, Out))
            {
                PUCHAR BlockData = (PUCHAR)ExAllocatePoolUninitialized(
                    NonPagedPool, NTFS_CACHE_BLOCK_SIZE, 'CftN');

                if (!BlockData)
                    break;

                Status = ReadDisk(PartDeviceObj,
                                  Block << NTFS_CACHE_BLOCK_SHIFT,
                                  NTFS_CACHE_BLOCK_SIZE,
                                  BlockData);
                if (!NT_SUCCESS(Status))
                {
                    ExFreePoolWithTag(BlockData, 'CftN');
                    break;
                }

                RtlCopyMemory(Out, BlockData + BlockOffset, Chunk);
                NtfsCacheInstall(Block, BlockData);
                ExFreePoolWithTag(BlockData, 'CftN');
            }

            Out += Chunk;
            Current += Chunk;
            Remaining -= Chunk;
        }

        if (!Remaining)
            return STATUS_SUCCESS;
        /* Anything left over falls through to a plain read below. */
    }

    /* This read goes to the disk, so anything held back for it must be there. */
    NtfsCacheFlushRange(Offset, Length);

    SectorAlignedOffset = Offset - (Offset % BytesPerSector);
    SectorAlignedLength = ALIGN_UP_BY((Offset - SectorAlignedOffset) + Length,
                                      BytesPerSector);

    if (SectorAlignedOffset == Offset
        && SectorAlignedLength == Length)
    {
        // Read directly to the supplied buffer.
        Status = ReadDisk(PartDeviceObj,
                          SectorAlignedOffset,
                          SectorAlignedLength,
                          Buffer);
    }

    else
    {
        // Create the read buffer
        ReadBuffer = new(NonPagedPool) UCHAR[SectorAlignedLength];

        // Fill the read buffer.
        Status = ReadDisk(PartDeviceObj,
                          SectorAlignedOffset,
                          SectorAlignedLength,
                          ReadBuffer);

        if (NT_SUCCESS(Status))
        {
            // Copy the contents we need into the supplied buffer.
            RtlCopyMemory(Buffer,
                          ReadBuffer + (Offset % BytesPerSector),
                          Length);
        }

        // Free read buffer
        delete[] ReadBuffer;
    }

    return Status;
}

NTSTATUS
NtfsWriteVolume(_In_    ULONGLONG Offset,
                _In_    ULONG Length,
                _Inout_ PUCHAR Buffer)
{
    /*
     * Metadata is rewritten constantly and usually superseded moments later,
     * so a write that fits inside one block is held in the cache and
     * committed in batches. Reads come from those same blocks, so a held-back
     * write is never invisible; anything reaching the volume another way
     * flushes first.
     */
    if (NtfsCacheReady && Length <= NTFS_CACHE_BLOCK_SIZE)
    {
        ULONGLONG Block = Offset >> NTFS_CACHE_BLOCK_SHIFT;
        ULONG BlockOffset = (ULONG)(Offset & (NTFS_CACHE_BLOCK_SIZE - 1));

        if (BlockOffset + Length <= NTFS_CACHE_BLOCK_SIZE)
        {
            NTFS_CACHE_SLOT* Slot;
            BOOLEAN Absorbed = FALSE;

            ExAcquireFastMutex(&NtfsCacheMutex);
            Slot = &NtfsCacheSlots[Block & NTFS_CACHE_SLOT_MASK];
            if (Slot->Tag == Block && Slot->Data && Slot->Owner == PartDeviceObj)
            {
                RtlCopyMemory(Slot->Data + BlockOffset, Buffer, Length);
                if (!Slot->Dirty)
                {
                    Slot->Dirty = TRUE;
                    NtfsCacheDirtyCount++;
                }
                Absorbed = TRUE;
            }
            ExReleaseFastMutex(&NtfsCacheMutex);

            /*
             * First touch of a metadata block: bring it in once so this
             * update and every later one to the same block stay in memory.
             * A fresh MFT record block otherwise costs a synchronous disk
             * write per record, and installs nothing, so its neighbours all
             * miss too. Only sub-block writes qualify -- those are the
             * record-sized metadata updates; whole-block writes are bulk
             * data and keep their existing path.
             */
            if (!Absorbed)
            {
                PUCHAR Staged = (PUCHAR)ExAllocatePoolUninitialized(
                    NonPagedPool, NTFS_CACHE_BLOCK_SIZE, 'CftN');

                if (Staged)
                {
                    NTSTATUS FillStatus;

                    if (Length == NTFS_CACHE_BLOCK_SIZE)
                    {
                        /* The write covers the whole block, so there is
                         * nothing on the disk worth fetching first. */
                        FillStatus = STATUS_SUCCESS;
                    }
                    else
                    {
                        FillStatus = ReadDisk(
                            PartDeviceObj,
                            Block << NTFS_CACHE_BLOCK_SHIFT,
                            NTFS_CACHE_BLOCK_SIZE,
                            Staged);
                    }

                    if (NT_SUCCESS(FillStatus))
                    {
                        RtlCopyMemory(Staged + BlockOffset, Buffer, Length);
                        NtfsCacheInstall(Block, Staged);

                        /* Mark it held-back; if the slot was lost meanwhile,
                         * fall through and write through instead. */
                        ExAcquireFastMutex(&NtfsCacheMutex);
                        Slot = &NtfsCacheSlots[Block & NTFS_CACHE_SLOT_MASK];
                        if (Slot->Tag == Block && Slot->Data &&
                            Slot->Owner == PartDeviceObj)
                        {
                            if (!Slot->Dirty)
                            {
                                Slot->Dirty = TRUE;
                                NtfsCacheDirtyCount++;
                            }
                            Absorbed = TRUE;
                        }
                        ExReleaseFastMutex(&NtfsCacheMutex);
                    }
                    ExFreePoolWithTag(Staged, 'CftN');
                }
            }

            if (Absorbed)
            {
                if (NtfsCacheDirtyCount > NTFS_CACHE_DIRTY_LIMIT)
                    NtfsCacheFlushSome(NTFS_CACHE_DIRTY_SLICE);
                return STATUS_SUCCESS;
            }
        }
    }

    /* Going to the disk directly: commit anything held for these bytes. */
    NtfsCacheFlushRange(Offset, Length);

    NTSTATUS Status;
    PUCHAR WriteBuffer;
    ULONGLONG SectorAlignedOffset;
    ULONG SectorAlignedLength;

    SectorAlignedOffset = Offset - (Offset % BytesPerSector);
    SectorAlignedLength = ALIGN_UP_BY((Offset - SectorAlignedOffset) + Length,
                                      BytesPerSector);

    if (SectorAlignedOffset == Offset
        && SectorAlignedLength == Length)
    {
        // Write directly to the disk using the supplied buffer.
        Status = WriteDisk(PartDeviceObj,
                           SectorAlignedOffset,
                           SectorAlignedLength,
                           Buffer);
    }

    else
    {
        // Create the write buffer
        WriteBuffer = new(NonPagedPool) UCHAR[SectorAlignedLength];

        // Fill the write buffer with what's on disk.
        Status = ReadDisk(PartDeviceObj,
                          SectorAlignedOffset,
                          SectorAlignedLength,
                          WriteBuffer);

        if (NT_SUCCESS(Status))
        {
            // Copy the buffer contents we want to write into the write buffer.
            RtlCopyMemory(WriteBuffer + (Offset % BytesPerSector),
                          Buffer,
                          Length);

            // Write to the disk.
            Status = WriteDisk(PartDeviceObj,
                               SectorAlignedOffset,
                               SectorAlignedLength,
                               WriteBuffer);
        }

        // Free write buffer
        delete[] WriteBuffer;
    }

    /* Keep whatever we still hold in step with what just went to the disk. */
    if (NT_SUCCESS(Status))
        NtfsCacheUpdateRange(Offset, Length, Buffer);
    else
        NtfsCacheInvalidateRange(Offset, Length);

    return Status;
}

BOOLEAN
NtfsIsNameInExpression(_In_     PUNICODE_STRING Expression,
                       _In_     PUNICODE_STRING Name,
                       _In_     BOOLEAN IgnoreCase,
                       _In_opt_ PWCHAR UpcaseTable)
{
    return FsRtlIsNameInExpression(Expression,
                                   Name,
                                   IgnoreCase,
                                   UpcaseTable);
}

#ifdef __cplusplus
}
#endif

extern "C"
NTSTATUS
NtfsDiskFlushKm(VOID)
{
    return NtfsCacheFlushAll();
}
