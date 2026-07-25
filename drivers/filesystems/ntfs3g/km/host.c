/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel callbacks for the shared NTFS-3G core
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <ntdddisk.h>

#include <errno.h>
#include <stdint.h>

#undef STATUS_NOT_FOUND
#include "host.h"
#include "ntfs3g_ros_km.h"

#define NTFS3G_POOL_TAG 'G3TN'
#define NTFS3G_IO_BUFFER_SIZE (256 * 1024)

/*
 * Metadata block cache. libntfs-3g caches parsed objects but not the blocks
 * underneath them, so without this every directory lookup costs one device
 * round trip per MFT record it touches. Blocks are cached set-associatively and
 * written through: deferring metadata writes was tried three ways and each one
 * corrupted the directory B-trees, so the device always holds current bytes.
 */
#define NTFS3G_CACHE_BLOCK    4096
#define NTFS3G_CACHE_SIZE     (1024 * 1024)
#define NTFS3G_CACHE_WAYS     4
#define NTFS3G_CACHE_EMPTY    ((uint64_t)-1)
#define NTFS3G_STREAM_RUN     8
/*
 * Which writes may be held back in the cache. Deferring index blocks corrupted
 * the directory B-trees when it was tried, and index and log records are the
 * multi-sector-transfer protected ones, so they are recognised by their record
 * signature and always written straight through. Everything else — MFT records
 * and the raw bitmap streams — is rewritten repeatedly by a single create or
 * delete, so coalescing it is where the win is.
 */
#define NTFS3G_ABSORB_MAX     NTFS3G_CACHE_BLOCK

typedef struct _NTFS3G_KERNEL_DEVICE
{
    PDEVICE_OBJECT DeviceObject;
    PVOID ReadBuffer;
    uint32_t SectorSize;
    PVOID CacheAllocation;
    uint8_t *CacheData;
    uint64_t *CacheTags;
    uint32_t *CacheValid;
    uint8_t *CacheDirty;
    uint8_t *CacheNextWay;
    uint32_t CacheBlockSize;
    uint32_t CacheBlocks;
    uint32_t CacheSetsMask;
    uint64_t StreamNextOffset;
    uint32_t StreamRun;
    BOOLEAN CacheUnavailable;
} NTFS3G_KERNEL_DEVICE;

static ERESOURCE Ntfs3gKernelRuntimeLock;
static LONG Ntfs3gKernelRuntimeInitialized;

NTSTATUS
Ntfs3gRosStatusFromError(int Error)
{
    switch (Error) {
        case ENOMEM:
            return STATUS_INSUFFICIENT_RESOURCES;
        case ENOENT:
            return STATUS_OBJECT_NAME_NOT_FOUND;
        case EEXIST:
            return STATUS_OBJECT_NAME_COLLISION;
        case EXDEV:
            return STATUS_NOT_SAME_DEVICE;
        case ENOTDIR:
            return STATUS_NOT_A_DIRECTORY;
        case EISDIR:
            return STATUS_FILE_IS_A_DIRECTORY;
        case ENOTEMPTY:
            return STATUS_DIRECTORY_NOT_EMPTY;
        case ENAMETOOLONG:
            return STATUS_NAME_TOO_LONG;
        case ENODATA:
            return STATUS_NO_EAS_ON_FILE;
        case ERANGE:
            return STATUS_BUFFER_TOO_SMALL;
        case ENOSPC:
            return STATUS_DISK_FULL;
#ifdef EFBIG
        case EFBIG:
            return STATUS_FILE_TOO_LARGE;
#endif
        case EILSEQ:
            return STATUS_OBJECT_NAME_INVALID;
        case EINVAL:
            return STATUS_INVALID_PARAMETER;
        case EACCES:
        case EROFS:
            return STATUS_MEDIA_WRITE_PROTECTED;
        case EOPNOTSUPP:
            return STATUS_NOT_SUPPORTED;
        case EBUSY:
        case EPERM:
            return STATUS_CANNOT_DELETE;
        case EIO:
            return STATUS_IO_DEVICE_ERROR;
        case ENODEV:
            return STATUS_NO_SUCH_DEVICE;
        default:
            return STATUS_UNRECOGNIZED_VOLUME;
    }
}

static int
Ntfs3gStatusToErrno(NTSTATUS Status)
{
    if (Status == STATUS_INSUFFICIENT_RESOURCES)
        return ENOMEM;
    if (Status == STATUS_ACCESS_DENIED ||
        Status == STATUS_MEDIA_WRITE_PROTECTED)
        return EROFS;
    if (Status == STATUS_INVALID_PARAMETER)
        return EINVAL;
    return EIO;
}

void *
Ntfs3gRosHostAllocate(size_t Size)
{
    return ExAllocatePoolWithTag(PagedPool, Size, NTFS3G_POOL_TAG);
}

void
Ntfs3gRosHostFree(void *Buffer)
{
    if (Buffer)
        ExFreePoolWithTag(Buffer, NTFS3G_POOL_TAG);
}

void
Ntfs3gRosHostAcquire(void)
{
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Ntfs3gKernelRuntimeLock, TRUE);
}

void
Ntfs3gRosHostRelease(void)
{
    ExReleaseResourceLite(&Ntfs3gKernelRuntimeLock);
    KeLeaveCriticalRegion();
}

int64_t
Ntfs3gRosHostGetTime(void)
{
    LARGE_INTEGER SystemTime;
    ULONG Seconds;

    KeQuerySystemTime(&SystemTime);
    if (!RtlTimeToSecondsSince1970(&SystemTime, &Seconds))
        return 0;
    return Seconds;
}

void
Ntfs3gRosHostLog(int IsError,
                 const char *Message)
{
    DbgPrintEx(DPFLTR_NTFS_ID,
               IsError ? DPFLTR_ERROR_LEVEL : DPFLTR_INFO_LEVEL,
               "NTFS3G: %s",
               Message);
}

static NTSTATUS
Ntfs3gSubmitSynchronousIrp(PDEVICE_OBJECT DeviceObject,
                           PIRP Irp,
                           PKEVENT Event,
                           PIO_STATUS_BLOCK IoStatus)
{
    NTSTATUS Status;

    IoGetNextIrpStackLocation(Irp)->Flags |= SL_OVERRIDE_VERIFY_VOLUME;
    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING) {
        KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus->Status;
    } else if (NT_SUCCESS(Status)) {
        Status = IoStatus->Status;
    }
    return Status;
}

static NTSTATUS
NTAPI
Ntfs3gOwnedIrpCompletion(PDEVICE_OBJECT DeviceObject,
                         PIRP Irp,
                         PVOID Context)
{
    PKEVENT Event = Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent(Event, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/*
 * Cache Manager can issue paging writes while holding a guarded mutex.
 * Such a caller cannot receive the completion APC used by
 * IoBuildSynchronousFsdRequest.  Stop completion in our own routine, signal
 * an event directly, and release the IRP after the lower stack is finished.
 */
static NTSTATUS
Ntfs3gSubmitOwnedIrp(PDEVICE_OBJECT DeviceObject,
                     PIRP Irp,
                     PIO_STATUS_BLOCK IoStatus)
{
    PMDL Mdl;
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp,
                           Ntfs3gOwnedIrpCompletion,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);
    IoGetNextIrpStackLocation(Irp)->Flags |= SL_OVERRIDE_VERIFY_VOLUME;
    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING || !KeReadStateEvent(&Event))
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

    *IoStatus = Irp->IoStatus;
    Status = IoStatus->Status;
    if ((Irp->Flags & (IRP_BUFFERED_IO | IRP_INPUT_OPERATION)) ==
        (IRP_BUFFERED_IO | IRP_INPUT_OPERATION) &&
        !NT_ERROR(Status) &&
        Irp->UserBuffer &&
        Irp->AssociatedIrp.SystemBuffer) {
        RtlCopyMemory(Irp->UserBuffer,
                      Irp->AssociatedIrp.SystemBuffer,
                      IoStatus->Information);
    }
    if ((Irp->Flags & IRP_DEALLOCATE_BUFFER) &&
        Irp->AssociatedIrp.SystemBuffer)
        ExFreePool(Irp->AssociatedIrp.SystemBuffer);
    while ((Mdl = Irp->MdlAddress)) {
        Irp->MdlAddress = Mdl->Next;
        /*
         * Returning STATUS_MORE_PROCESSING_REQUIRED prevents
         * IofCompleteRequest from reaching its normal MDL-unlock pass.
         * We own the stopped IRP, so mirror that cleanup before freeing
         * each MDL.
         */
        if (Mdl->MdlFlags & MDL_PAGES_LOCKED)
            MmUnlockPages(Mdl);
        IoFreeMdl(Mdl);
    }
    IoFreeIrp(Irp);
    return Status;
}

static NTSTATUS
Ntfs3gDeviceControl(PDEVICE_OBJECT DeviceObject,
                    ULONG ControlCode,
                    void *OutputBuffer,
                    ULONG OutputLength)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(ControlCode, DeviceObject, NULL, 0,
                                        OutputBuffer, OutputLength, FALSE,
                                        &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    return Ntfs3gSubmitSynchronousIrp(DeviceObject, Irp, &Event, &IoStatus);
}

static int
Ntfs3gKernelReadChunk(NTFS3G_KERNEL_DEVICE *Context,
                      uint64_t Offset,
                      uint32_t Length,
                      uint32_t *BytesRead,
                      PVOID Destination)
{
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER ByteOffset;
    PIRP Irp;
    NTSTATUS Status;

    ByteOffset.QuadPart = Offset;
    Irp = IoBuildAsynchronousFsdRequest(IRP_MJ_READ,
                                        Context->DeviceObject,
                                        Destination,
                                        Length,
                                        &ByteOffset,
                                        &IoStatus);
    if (!Irp)
        return ENOMEM;
    Status = Ntfs3gSubmitOwnedIrp(Context->DeviceObject, Irp, &IoStatus);
    if (!NT_SUCCESS(Status))
        return Ntfs3gStatusToErrno(Status);
    *BytesRead = (uint32_t)IoStatus.Information;
    return 0;
}

static void
Ntfs3gCacheEnsure(NTFS3G_KERNEL_DEVICE *Context)
{
    uint32_t BlockSize, Blocks, Sets, TagBytes, ValidBytes, WayBytes, Index;
    uint8_t *Base;

    if (Context->CacheData || Context->CacheUnavailable)
        return;

    BlockSize = Context->SectorSize > NTFS3G_CACHE_BLOCK ? Context->SectorSize : NTFS3G_CACHE_BLOCK;
    Blocks = NTFS3G_CACHE_SIZE / BlockSize;
    Blocks -= Blocks % NTFS3G_CACHE_WAYS;
    Sets = Blocks / NTFS3G_CACHE_WAYS;
    if (!Blocks || !Sets || (Sets & (Sets - 1))) {
        Context->CacheUnavailable = TRUE;    /* set count must be a power of two */
        return;
    }

    TagBytes = Blocks * sizeof(uint64_t);
    ValidBytes = Blocks * sizeof(uint32_t);
    WayBytes = Sets + Blocks;   /* replacement cursors, then a dirty flag per block */
    Base = ExAllocatePoolWithTag(NonPagedPool,
                                 TagBytes + ValidBytes + WayBytes + (SIZE_T)Blocks * BlockSize,
                                 NTFS3G_POOL_TAG);
    if (!Base) {
        Context->CacheUnavailable = TRUE;
        return;
    }

    Context->CacheAllocation = Base;
    Context->CacheTags = (uint64_t *)Base;
    Context->CacheValid = (uint32_t *)(Base + TagBytes);
    Context->CacheNextWay = Base + TagBytes + ValidBytes;
    Context->CacheDirty = Context->CacheNextWay + Sets;
    Context->CacheData = Base + TagBytes + ValidBytes + WayBytes;
    Context->CacheBlockSize = BlockSize;
    Context->CacheBlocks = Blocks;
    Context->CacheSetsMask = Sets - 1;
    RtlZeroMemory(Context->CacheNextWay, WayBytes);
    for (Index = 0; Index < Blocks; Index++)
        Context->CacheTags[Index] = NTFS3G_CACHE_EMPTY;
}

static int
Ntfs3gKernelWriteChunk(NTFS3G_KERNEL_DEVICE *Context, uint64_t Offset, uint32_t Length,
                       PVOID Source);

static int
Ntfs3gCacheFlushSlot(NTFS3G_KERNEL_DEVICE *Context, uint32_t Slot)
{
    int Error;

    if (!Context->CacheDirty[Slot])
        return 0;

    Error = Ntfs3gKernelWriteChunk(Context,
                                   Context->CacheTags[Slot] * Context->CacheBlockSize,
                                   Context->CacheValid[Slot],
                                   Context->CacheData +
                                       (SIZE_T)Slot * Context->CacheBlockSize);
    if (Error)
        return Error;
    Context->CacheDirty[Slot] = 0;
    return 0;
}

static int
Ntfs3gCacheFlushAll(NTFS3G_KERNEL_DEVICE *Context)
{
    uint32_t Slot;

    if (!Context->CacheData)
        return 0;
    for (Slot = 0; Slot < Context->CacheBlocks; Slot++) {
        int Error = Ntfs3gCacheFlushSlot(Context, Slot);

        if (Error)
            return Error;
    }
    return 0;
}

static uint32_t
Ntfs3gCacheSet(NTFS3G_KERNEL_DEVICE *Context, uint64_t Block)
{
    uint64_t Hash = Block * 0x9E3779B97F4A7C15ULL;

    return (uint32_t)(Hash >> 32) & Context->CacheSetsMask;
}

/*
 * Return the cached block, filling it from the device on a miss. Valid reports
 * how much of the block the device returned, which is short only at the end of
 * the volume.
 */
static int
Ntfs3gCacheGet(NTFS3G_KERNEL_DEVICE *Context, uint64_t Block, uint8_t **Data, uint32_t *Valid)
{
    uint32_t Set = Ntfs3gCacheSet(Context, Block);
    uint32_t FirstSlot = Set * NTFS3G_CACHE_WAYS;
    uint32_t Way, Slot, Got;
    int Error;

    for (Way = 0; Way < NTFS3G_CACHE_WAYS; Way++) {
        if (Context->CacheTags[FirstSlot + Way] == Block) {
            Slot = FirstSlot + Way;
            *Data = Context->CacheData + (SIZE_T)Slot * Context->CacheBlockSize;
            *Valid = Context->CacheValid[Slot];
            return 0;
        }
    }

    for (Way = 0; Way < NTFS3G_CACHE_WAYS; Way++) {
        if (Context->CacheTags[FirstSlot + Way] == NTFS3G_CACHE_EMPTY)
            break;
    }
    if (Way == NTFS3G_CACHE_WAYS) {
        Way = Context->CacheNextWay[Set];
        Context->CacheNextWay[Set] = (uint8_t)((Way + 1) % NTFS3G_CACHE_WAYS);
    }
    Slot = FirstSlot + Way;
    Error = Ntfs3gCacheFlushSlot(Context, Slot);   /* never discard unwritten bytes */
    if (Error)
        return Error;
    Context->CacheTags[Slot] = NTFS3G_CACHE_EMPTY;

    /* Fill straight into the slot: the write path holds its staged data in
     * ReadBuffer, so a fault-in must never route through it. */
    Error = Ntfs3gKernelReadChunk(Context, Block * Context->CacheBlockSize,
                                  Context->CacheBlockSize, &Got,
                                  Context->CacheData +
                                      (SIZE_T)Slot * Context->CacheBlockSize);
    if (Error)
        return Error;
    if (Got > Context->CacheBlockSize)
        Got = Context->CacheBlockSize;
    Context->CacheDirty[Slot] = 0;
    Context->CacheTags[Slot] = Block;
    Context->CacheValid[Slot] = Got;
    *Data = Context->CacheData + (SIZE_T)Slot * Context->CacheBlockSize;
    *Valid = Got;
    return 0;
}

/*
 * True for the record types NTFS protects with an update sequence array and
 * that must not be deferred: index allocation blocks and log file records.
 */
static BOOLEAN
Ntfs3gIsProtectedRecord(const uint8_t *Head, uint32_t Length)
{
    if (Length < 4)
        return FALSE;
    if (Head[0] == 'I' && Head[1] == 'N' && Head[2] == 'D' && Head[3] == 'X')
        return TRUE;
    if (Head[0] == 'R' && Head[1] == 'C' && Head[2] == 'R' && Head[3] == 'D')
        return TRUE;
    if (Head[0] == 'R' && Head[1] == 'S' && Head[2] == 'T' && Head[3] == 'R')
        return TRUE;
    return FALSE;
}

/*
 * Hold back one write. The block is faulted in first so the
 * records the write does not cover stay correct. Returns 1 when the write
 * cannot be held and the caller must issue it.
 */
static int
Ntfs3gCacheAbsorb(NTFS3G_KERNEL_DEVICE *Context, uint64_t Offset,
                  uint32_t Length, const uint8_t *Source)
{
    uint64_t Block = Offset / Context->CacheBlockSize;
    uint32_t BlockOffset = (uint32_t)(Offset % Context->CacheBlockSize);
    uint32_t Valid, FirstSlot, Way;
    uint8_t *Data;
    int Error;

    Error = Ntfs3gCacheGet(Context, Block, &Data, &Valid);
    if (Error)
        return Error;
    if (Valid < BlockOffset + Length)
        return 1;      /* volume tail: let the caller write it through */

    RtlCopyMemory(Data + BlockOffset, Source, Length);
    FirstSlot = Ntfs3gCacheSet(Context, Block) * NTFS3G_CACHE_WAYS;
    for (Way = 0; Way < NTFS3G_CACHE_WAYS; Way++) {
        if (Context->CacheTags[FirstSlot + Way] == Block) {
            Context->CacheDirty[FirstSlot + Way] = 1;
            return 0;
        }
    }
    return 1;
}

/*
 * A transfer that bypasses the cache reads the device, which is behind for any
 * block still held back, so copy the newer bytes over what the device returned.
 * Flushing instead would be undone immediately: libntfs-3g rewrites the same
 * records, which turns one write into several.
 */
static void
Ntfs3gCacheOverlay(NTFS3G_KERNEL_DEVICE *Context, uint64_t Offset, uint32_t Length,
                   uint8_t *Destination)
{
    uint32_t BlockSize;
    uint64_t Block, LastBlock;

    if (!Context->CacheData || !Length)
        return;

    BlockSize = Context->CacheBlockSize;
    LastBlock = (Offset + Length - 1) / BlockSize;
    for (Block = Offset / BlockSize; Block <= LastBlock; Block++) {
        uint32_t FirstSlot = Ntfs3gCacheSet(Context, Block) * NTFS3G_CACHE_WAYS;
        uint32_t Way;

        for (Way = 0; Way < NTFS3G_CACHE_WAYS; Way++) {
            uint32_t Slot = FirstSlot + Way;
            uint64_t BlockStart, CopyStart, CopyEnd;

            if (Context->CacheTags[Slot] != Block || !Context->CacheDirty[Slot])
                continue;
            BlockStart = Block * BlockSize;
            CopyStart = BlockStart > Offset ? BlockStart : Offset;
            CopyEnd = BlockStart + Context->CacheValid[Slot];
            if (CopyEnd > Offset + Length)
                CopyEnd = Offset + Length;
            if (CopyEnd > CopyStart) {
                RtlCopyMemory(Destination + (CopyStart - Offset),
                              Context->CacheData + (SIZE_T)Slot * BlockSize +
                                  (CopyStart - BlockStart),
                              (SIZE_T)(CopyEnd - CopyStart));
            }
            break;
        }
    }
}

/*
 * Keep the cache coherent with a write that has already reached the device. A
 * block the write covers completely is installed outright, a cached block it
 * covers partially is patched, and an uncached block it covers partially is
 * left out because the rest of it is unknown.
 */
static void
Ntfs3gCacheUpdate(NTFS3G_KERNEL_DEVICE *Context, uint64_t Offset,
                  uint32_t Length, const uint8_t *Source)
{
    uint32_t BlockSize = Context->CacheBlockSize;
    uint64_t Block;

    if (!Context->CacheData || !Length)
        return;

    for (Block = Offset / BlockSize; Block <= (Offset + Length - 1) / BlockSize; Block++) {
        uint64_t BlockStart = Block * BlockSize;
        uint64_t CopyStart = BlockStart > Offset ? BlockStart : Offset;
        uint64_t CopyEnd = BlockStart + BlockSize;
        uint32_t FirstSlot = Ntfs3gCacheSet(Context, Block) * NTFS3G_CACHE_WAYS;
        uint32_t Way, Slot = NTFS3G_CACHE_WAYS;
        BOOLEAN Whole;

        if (CopyEnd > Offset + Length)
            CopyEnd = Offset + Length;
        Whole = (CopyStart == BlockStart) && (CopyEnd == BlockStart + BlockSize);

        for (Way = 0; Way < NTFS3G_CACHE_WAYS; Way++) {
            if (Context->CacheTags[FirstSlot + Way] == Block) {
                Slot = Way;
                break;
            }
        }
        if (Slot == NTFS3G_CACHE_WAYS) {
            if (!Whole)
                continue;
            for (Way = 0; Way < NTFS3G_CACHE_WAYS; Way++) {
                if (Context->CacheTags[FirstSlot + Way] == NTFS3G_CACHE_EMPTY) {
                    Slot = Way;
                    break;
                }
            }
            if (Slot == NTFS3G_CACHE_WAYS) {
                Slot = Context->CacheNextWay[FirstSlot / NTFS3G_CACHE_WAYS];
                Context->CacheNextWay[FirstSlot / NTFS3G_CACHE_WAYS] =
                    (uint8_t)((Slot + 1) % NTFS3G_CACHE_WAYS);
            }
            Context->CacheValid[FirstSlot + Slot] = BlockSize;
        } else if (Context->CacheValid[FirstSlot + Slot] < CopyEnd - BlockStart) {
            Context->CacheValid[FirstSlot + Slot] = (uint32_t)(CopyEnd - BlockStart);
        }

        RtlCopyMemory(Context->CacheData +
                          (SIZE_T)(FirstSlot + Slot) * BlockSize + (CopyStart - BlockStart),
                      Source + (CopyStart - Offset),
                      (SIZE_T)(CopyEnd - CopyStart));
        Context->CacheTags[FirstSlot + Slot] = Block;
        if (Whole)
            Context->CacheDirty[FirstSlot + Slot] = 0;   /* the device has all of it */
    }
}

static int
Ntfs3gKernelRead(void *OpaqueContext,
                 uint64_t Offset,
                 void *Buffer,
                 uint32_t Length,
                 uint32_t *BytesRead)
{
    NTFS3G_KERNEL_DEVICE *Context = OpaqueContext;
    uint8_t *Destination = Buffer;

    if (Offset > INT64_MAX || Length > INT64_MAX - Offset)
        return EINVAL;

    *BytesRead = 0;

    /*
     * Only large adjacent transfers count as streaming. Walking consecutive MFT
     * records is also a run of adjacent reads, and treating that as a stream
     * would switch the cache off where it pays most.
     */
    if (Offset != Context->StreamNextOffset)
        Context->StreamRun = 0;
    else if (Length > Context->SectorSize * 8 && Context->StreamRun < NTFS3G_STREAM_RUN)
        Context->StreamRun++;
    Context->StreamNextOffset = Offset + Length;

    /*
     * Serve from the block cache only when the whole request lies inside one
     * block and is not part of a long run of adjacent reads. Metadata records
     * qualify; splitting a larger request across blocks, or caching streamed
     * file data, costs more than it saves.
     */
    if (Length && Length <= Context->SectorSize * 8 &&
        Context->StreamRun < NTFS3G_STREAM_RUN &&
        Offset / NTFS3G_CACHE_BLOCK == (Offset + Length - 1) / NTFS3G_CACHE_BLOCK) {
        Ntfs3gCacheEnsure(Context);
        if (Context->CacheData) {
            uint64_t Block = Offset / Context->CacheBlockSize;
            uint32_t BlockOffset = (uint32_t)(Offset % Context->CacheBlockSize);
            uint32_t Valid;
            uint8_t *Data;
            int Error;

            Error = Ntfs3gCacheGet(Context, Block, &Data, &Valid);
            if (Error)
                return Error;
            if (Valid > BlockOffset) {
                *BytesRead = min(Length, Valid - BlockOffset);
                RtlCopyMemory(Destination, Data + BlockOffset, *BytesRead);
            }
            return 0;
        }
    }

    /*
     * A sector-aligned request can be read straight into the caller's buffer.
     * Staging it through ReadBuffer costs a full copy of the transfer, which on
     * a streamed read is the same order as the transfer itself.
     */
    if ((Offset & (Context->SectorSize - 1)) == 0 &&
        (Length & (Context->SectorSize - 1)) == 0 &&
        ((ULONG_PTR)Destination & Context->DeviceObject->AlignmentRequirement) == 0) {
        while (*BytesRead < Length) {
            uint32_t ChunkLength = min(Length - *BytesRead, NTFS3G_IO_BUFFER_SIZE);
            uint32_t ChunkRead;
            int Error;

            Error = Ntfs3gKernelReadChunk(Context, Offset + *BytesRead, ChunkLength,
                                          &ChunkRead, Destination + *BytesRead);
            if (Error)
                return Error;
            if (!ChunkRead)
                break;
            Ntfs3gCacheOverlay(Context, Offset + *BytesRead, ChunkRead,
                               Destination + *BytesRead);
            *BytesRead += ChunkRead;
            if (ChunkRead != ChunkLength)
                break;
        }
        return 0;
    }

    while (*BytesRead < Length) {
        uint64_t Position = Offset + *BytesRead;
        uint32_t SectorOffset = (uint32_t)(Position & (Context->SectorSize - 1));
        uint32_t ChunkLength = min(Length - *BytesRead,
                                   NTFS3G_IO_BUFFER_SIZE - SectorOffset);
        uint32_t ReadLength = ALIGN_UP_BY(SectorOffset + ChunkLength,
                                          Context->SectorSize);
        uint32_t ChunkRead;
        uint32_t CopyLength;
        int Error;

        Error = Ntfs3gKernelReadChunk(Context, Position - SectorOffset,
                                      ReadLength, &ChunkRead, Context->ReadBuffer);
        if (Error)
            return Error;
        if (ChunkRead <= SectorOffset)
            break;
        CopyLength = min(ChunkLength, ChunkRead - SectorOffset);
        RtlCopyMemory(Destination + *BytesRead,
                      (uint8_t *)Context->ReadBuffer + SectorOffset,
                      CopyLength);
        Ntfs3gCacheOverlay(Context, Position, CopyLength, Destination + *BytesRead);
        *BytesRead += CopyLength;
        if (CopyLength != ChunkLength)
            break;
    }
    return 0;
}

static int
Ntfs3gKernelWriteChunk(NTFS3G_KERNEL_DEVICE *Context,
                       uint64_t Offset,
                       uint32_t Length,
                       PVOID Source)
{
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER ByteOffset;
    PIRP Irp;
    NTSTATUS Status;

    ByteOffset.QuadPart = Offset;
    Irp = IoBuildAsynchronousFsdRequest(IRP_MJ_WRITE,
                                        Context->DeviceObject,
                                        Source,
                                        Length,
                                        &ByteOffset,
                                        &IoStatus);
    if (!Irp)
        return ENOMEM;
    Status = Ntfs3gSubmitOwnedIrp(Context->DeviceObject, Irp, &IoStatus);
    if (!NT_SUCCESS(Status))
        return Ntfs3gStatusToErrno(Status);
    return IoStatus.Information == Length ? 0 : EIO;
}

static int
Ntfs3gKernelWrite(void *OpaqueContext,
                  uint64_t Offset,
                  const void *Buffer,
                  uint32_t Length,
                  uint32_t *BytesWritten)
{
    NTFS3G_KERNEL_DEVICE *Context = OpaqueContext;
    const uint8_t *Source = Buffer;

    if (Offset > INT64_MAX || Length > INT64_MAX - Offset)
        return EINVAL;

    *BytesWritten = 0;

    /*
     * A sector-aligned request needs no staging: the caller's buffer is both
     * what the device transfers and what the cache absorbs, so the copy through
     * ReadBuffer — a full pass over the transfer — is skipped entirely.
     */
    if ((Offset & (Context->SectorSize - 1)) == 0 &&
        (Length & (Context->SectorSize - 1)) == 0 &&
        ((ULONG_PTR)Source & Context->DeviceObject->AlignmentRequirement) == 0) {
        while (*BytesWritten < Length) {
            uint64_t Position = Offset + *BytesWritten;
            uint32_t ChunkLength = min(Length - *BytesWritten, NTFS3G_IO_BUFFER_SIZE);
            const uint8_t *Chunk = Source + *BytesWritten;
            int Error = 1;

            if (ChunkLength <= NTFS3G_ABSORB_MAX &&
                !Ntfs3gIsProtectedRecord(Chunk, ChunkLength) &&
                Position / NTFS3G_CACHE_BLOCK ==
                    (Position + ChunkLength - 1) / NTFS3G_CACHE_BLOCK) {
                Ntfs3gCacheEnsure(Context);
                if (Context->CacheData) {
                    Error = Ntfs3gCacheAbsorb(Context, Position, ChunkLength, Chunk);
                    if (Error < 0)
                        return Error;
                }
            }
            if (Error) {
                Error = Ntfs3gKernelWriteChunk(Context, Position, ChunkLength,
                                               (PVOID)Chunk);
                if (Error)
                    return Error;
                Ntfs3gCacheUpdate(Context, Position, ChunkLength, Chunk);
            }
            *BytesWritten += ChunkLength;
        }
        return 0;
    }

    while (*BytesWritten < Length) {
        uint64_t Position = Offset + *BytesWritten;
        uint32_t SectorOffset = (uint32_t)(Position & (Context->SectorSize - 1));
        uint32_t ChunkLength = min(Length - *BytesWritten,
                                   NTFS3G_IO_BUFFER_SIZE - SectorOffset);
        uint32_t WriteLength = ALIGN_UP_BY(SectorOffset + ChunkLength,
                                           Context->SectorSize);
        int Error;

        if (SectorOffset || ChunkLength != WriteLength) {
            uint32_t ReadLength;

            Error = Ntfs3gKernelReadChunk(Context, Position - SectorOffset,
                                          WriteLength, &ReadLength, Context->ReadBuffer);
            if (Error)
                return Error;
            if (ReadLength != WriteLength)
                return EIO;
            Ntfs3gCacheOverlay(Context, Position - SectorOffset, WriteLength,
                               (uint8_t *)Context->ReadBuffer);
        }
        RtlCopyMemory((uint8_t *)Context->ReadBuffer + SectorOffset,
                      Source + *BytesWritten,
                      ChunkLength);
        Error = 1;
        if (WriteLength <= NTFS3G_ABSORB_MAX &&
            !Ntfs3gIsProtectedRecord((const uint8_t *)Context->ReadBuffer + SectorOffset,
                                     ChunkLength) &&
            (Position - SectorOffset) / NTFS3G_CACHE_BLOCK ==
                (Position - SectorOffset + WriteLength - 1) / NTFS3G_CACHE_BLOCK) {
            Ntfs3gCacheEnsure(Context);
            if (Context->CacheData) {
                Error = Ntfs3gCacheAbsorb(Context, Position - SectorOffset, WriteLength,
                                          (const uint8_t *)Context->ReadBuffer);
                if (Error < 0)
                    return Error;
            }
        }
        if (Error) {
            Error = Ntfs3gKernelWriteChunk(Context, Position - SectorOffset,
                                           WriteLength, Context->ReadBuffer);
            if (Error)
                return Error;
            Ntfs3gCacheUpdate(Context, Position - SectorOffset, WriteLength,
                              (const uint8_t *)Context->ReadBuffer);
        }
        *BytesWritten += ChunkLength;
    }
    return 0;
}

static int
Ntfs3gKernelSync(void *OpaqueContext)
{
    NTFS3G_KERNEL_DEVICE *Context = OpaqueContext;
    IO_STATUS_BLOCK IoStatus;
    PIRP Irp;
    NTSTATUS Status;
    int CacheError;

    CacheError = Ntfs3gCacheFlushAll(Context);
    if (CacheError)
        return CacheError;

    Irp = IoBuildAsynchronousFsdRequest(IRP_MJ_FLUSH_BUFFERS,
                                        Context->DeviceObject,
                                        NULL,
                                        0,
                                        NULL,
                                        &IoStatus);
    if (!Irp)
        return ENOMEM;
    Status = Ntfs3gSubmitOwnedIrp(Context->DeviceObject, Irp, &IoStatus);
    return NT_SUCCESS(Status) ? 0 : Ntfs3gStatusToErrno(Status);
}

static void
Ntfs3gKernelClose(void *OpaqueContext)
{
    NTFS3G_KERNEL_DEVICE *Context = OpaqueContext;

    Ntfs3gCacheFlushAll(Context);
    if (Context->CacheAllocation)
        ExFreePoolWithTag(Context->CacheAllocation, NTFS3G_POOL_TAG);
    MmFreeContiguousMemory(Context->ReadBuffer);
    ObDereferenceObject(Context->DeviceObject);
    Ntfs3gRosHostFree(Context);
}

static const NTFS3G_ROS_DEVICE_OPERATIONS Ntfs3gKernelReadOnlyOperations = {
    Ntfs3gKernelRead,
    Ntfs3gKernelClose,
    NULL,
    NULL
};

static const NTFS3G_ROS_DEVICE_OPERATIONS Ntfs3gKernelReadWriteOperations = {
    Ntfs3gKernelRead,
    Ntfs3gKernelClose,
    Ntfs3gKernelWrite,
    Ntfs3gKernelSync
};

NTSTATUS
Ntfs3gRosInitializeKernelLibrary(void)
{
    NTSTATUS Status;

    Status = ExInitializeResourceLite(&Ntfs3gKernelRuntimeLock);
    if (!NT_SUCCESS(Status))
        return Status;
    InterlockedExchange(&Ntfs3gKernelRuntimeInitialized, 1);
    return STATUS_SUCCESS;
}

void
Ntfs3gRosUninitializeKernelLibrary(void)
{
    if (InterlockedExchange(&Ntfs3gKernelRuntimeInitialized, 0))
        ExDeleteResourceLite(&Ntfs3gKernelRuntimeLock);
}

NTSTATUS
Ntfs3gRosMountDevice(PDEVICE_OBJECT DeviceObject,
                     int ReadOnly,
                     PNTFS3G_ROS_KM_VOLUME *Volume)
{
    NTFS3G_KERNEL_DEVICE *Context;
    GET_LENGTH_INFORMATION Length;
    DISK_GEOMETRY Geometry;
    PHYSICAL_ADDRESS HighestAddress;
    uint64_t DeviceLength = 0;
    uint32_t SectorSize = 512;
    int Result;

    if (!DeviceObject || !Volume)
        return STATUS_INVALID_PARAMETER;
    if (!InterlockedCompareExchange(&Ntfs3gKernelRuntimeInitialized, 1, 1))
        return STATUS_INVALID_DEVICE_STATE;

    Context = Ntfs3gRosHostAllocate(sizeof(*Context));
    if (!Context)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Context, sizeof(*Context));   /* the pool allocator does not zero */
    HighestAddress.QuadPart = -1;
    Context->ReadBuffer = MmAllocateContiguousMemory(NTFS3G_IO_BUFFER_SIZE,
                                                     HighestAddress);
    if (!Context->ReadBuffer) {
        Ntfs3gRosHostFree(Context);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Context->DeviceObject = DeviceObject;
    ObReferenceObject(DeviceObject);

    if (NT_SUCCESS(Ntfs3gDeviceControl(DeviceObject,
                                       IOCTL_DISK_GET_DRIVE_GEOMETRY,
                                       &Geometry, sizeof(Geometry))))
        SectorSize = Geometry.BytesPerSector;
    if (NT_SUCCESS(Ntfs3gDeviceControl(DeviceObject,
                                       IOCTL_DISK_GET_LENGTH_INFO,
                                       &Length, sizeof(Length))))
        DeviceLength = Length.Length.QuadPart;
    Context->SectorSize = SectorSize;

    Result = Ntfs3gRosMount(Context,
                            ReadOnly ? &Ntfs3gKernelReadOnlyOperations :
                                       &Ntfs3gKernelReadWriteOperations,
                            DeviceLength, SectorSize, Volume);
    if (Result < 0)
        return Ntfs3gRosStatusFromError(-Result);
    return STATUS_SUCCESS;
}

NTSTATUS
Ntfs3gRosUnmountDevice(PNTFS3G_ROS_KM_VOLUME Volume)
{
    int Result = Ntfs3gRosUnmount(Volume);

    if (Result < 0)
        return Ntfs3gRosStatusFromError(-Result);
    return STATUS_SUCCESS;
}

/* Open a path whose MFT record the caller already resolved, skipping the walk. */
NTSTATUS
Ntfs3gRosOpenUnicodeFileById(PNTFS3G_ROS_KM_VOLUME Volume,
                             PCUNICODE_STRING Path,
                             ULONGLONG FileId,
                             NTFS3G_ROS_FILE **File)
{
    int Result;

    if (!Volume || !Path || !File)
        return STATUS_INVALID_PARAMETER;

    Result = Ntfs3gRosOpenFileByIdUtf16(Volume,
                                        (uint64_t)FileId,
                                        (const uint16_t *)Path->Buffer,
                                        Path->Length / sizeof(WCHAR),
                                        File);
    if (Result < 0)
        return Ntfs3gRosStatusFromError(-Result);
    return STATUS_SUCCESS;
}

NTSTATUS
Ntfs3gRosOpenUnicodeFile(PNTFS3G_ROS_KM_VOLUME Volume,
                         PCUNICODE_STRING Path,
                         NTFS3G_ROS_FILE **File)
{
    int Result;

    if (!Volume || !Path || !File)
        return STATUS_INVALID_PARAMETER;
    Result = Ntfs3gRosOpenFileUtf16(Volume,
                                    (const uint16_t *)Path->Buffer,
                                    Path->Length / sizeof(WCHAR),
                                    File);
    if (Result < 0)
        return Ntfs3gRosStatusFromError(-Result);
    return STATUS_SUCCESS;
}
