/*
 * PROJECT:     ReactOS NTFS driver (ntfslx)
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     FCB-level lock helpers and the canonical lock-ordering
 *              policy for the entire driver.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ============================================================================
 *                CANONICAL LOCK ORDER FOR drivers/filesystems/ntfslx
 * ============================================================================
 *
 *   1. NTFSLX_DEVICE_EXTENSION::VolumeResource    (ERESOURCE)
 *        Volume-wide lock. Exclusive for mount/dismount and any operation
 *        that mutates volume-level metadata layout (file/dir create or
 *        delete, $MFT or $MFTMirr extension, $LogFile reset). Shared for
 *        readers of mount-level state (volume info queries, share-list
 *        walks). Top of the order; nothing above this.
 *
 *   2. NTFSLX_FILE_CONTEXT::MainResource          (ERESOURCE, per-FCB)
 *        Per-file lock for non-paging operations: attribute add/remove,
 *        size mutation under SetInformation, the cleanup-time delete path.
 *        Wired into FcbHeader.Resource so the cache manager and FsRtl
 *        fast-I/O paths see the same lock the dispatch handlers use.
 *        Acquired AFTER VolumeResource (if both are needed by the same
 *        path).
 *
 *   3. NTFSLX_FILE_CONTEXT::PagingIoResource      (ERESOURCE, per-FCB)
 *        Per-file lock for paging I/O: section flushes, lazy-writer page
 *        outs, MmFlushSection. Wired into FcbHeader.PagingIoResource.
 *        Always sub-ordinate to MainResource — a thread that needs both
 *        acquires MainResource first.
 *
 *   4. NTFSLX_DEVICE_EXTENSION::MftAllocLock      (KGUARDED_MUTEX)
 *      NTFSLX_DEVICE_EXTENSION::ClusterAllocLock  (KGUARDED_MUTEX)
 *        Allocator locks. SIBLINGS of each other; never hold both at
 *        the same time. Each guards exactly the in-memory bitmap scan +
 *        reservation + the durable bitmap write. Disk writes for the
 *        reserved range (new MFT record contents, new file data,
 *        $INDEX_ALLOCATION pages) happen OUTSIDE the lock, after the
 *        bitmap reservation is committed. KGUARDED_MUTEX (not
 *        FAST_MUTEX) was chosen so the lock-window contract is
 *        explicit at every call site and so reverting the narrow-scope
 *        discipline triggers a compile error rather than a silent
 *        regression.
 *
 *   5. NTFSLX_DEVICE_EXTENSION::JournalLock        (KGUARDED_MUTEX)
 *        Phase-1 redo-record journal append lock. Acquired UNDER
 *        MftAllocLock or ClusterAllocLock — the bitmap-RMW path
 *        journals the intent, then writes the bitmap to disk, all
 *        within the alloc-lock window. JournalLock thus must never
 *        sit above an alloc lock in any path, and the journal append
 *        helpers must remain APC-safe (see RULES below).
 *        NtfslxJournalAppendRecord uses NtfslxWriteDisk, which is
 *        APC-safe; this is what keeps the inverted ordering legal.
 *
 *   6. NTFSLX_FILE_CONTEXT::ShareMutex            (FAST_MUTEX, per-FCB)
 *        Per-FCB share-access lock. Wraps every IoCheckShareAccess /
 *        IoSetShareAccess / IoUpdateShareAccess / IoRemoveShareAccess
 *        pair so two opens of the same file cannot race on the
 *        SHARE_ACCESS state machine. Acquired in the create path (after
 *        the FCB has been built, before share-access enforcement);
 *        released in cleanup (after the share record is updated).
 *
 *   7. NTFSLX_DEVICE_EXTENSION::NotifySync        (PNOTIFY_SYNC)
 *        FsRtl notify-package handle, manipulated only via
 *        FsRtlNotifyFullReportChange / FsRtlNotifyFilterChangeDirectory.
 *        Lowest mutex-class lock in the order; safe to acquire under
 *        any other lock above it.
 *
 *   8. NTFSLX_DEVICE_EXTENSION::ObsoleteMftRunlistsLock (KSPIN_LOCK)
 *        Leaf-only spinlock guarding the deferred-free list of stale
 *        MftRunlist allocations parked by NtfslxExtendMftRunlist (T0.4).
 *        Acquired/released inline around InsertTailList /
 *        RemoveHeadList only; never held across any other lock or
 *        across pageable code. Initialized in NtfslxMountVolume,
 *        drained from IRP_MJ_SHUTDOWN and FSCTL_DISMOUNT_VOLUME.
 *        DISPATCH_LEVEL primitive — must remain a leaf.
 *
 * RULES
 * -----
 *   - Acquire in this order; release in reverse.
 *   - Never hold MftAllocLock and ClusterAllocLock at the same time.
 *   - Never issue a synchronous IRP that completes via APC under
 *     MftAllocLock / ClusterAllocLock / JournalLock (KGUARDED_MUTEX
 *     disables special kernel APCs, so the wait will deadlock).
 *     NtfslxReadDisk and NtfslxWriteDisk in blockdev.c / diskwrite.c
 *     are APC-safe (they use a custom completion routine signalled via
 *     KEVENT). The JournalLock relies on this — its only synchronous
 *     IO is NtfslxWriteDisk via NtfslxJournalWriteAtOffset.
 *   - Violations of this order are bugs; document any exception with
 *     a comment block at the call site.
 *
 * Touching this file is reasonable when the lock model itself changes;
 * for everyday changes, just follow the order documented here.
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

/*
 * NtfslxFcbAcquireMainExclusive - acquire FCB MainResource exclusive.
 * Returns FALSE if Wait==FALSE and the lock was contended; TRUE on success.
 */
BOOLEAN
NtfslxFcbAcquireMainExclusive(
    _In_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ BOOLEAN Wait)
{
    if (FileContext == NULL || !FileContext->MainResourceInitialized)
    {
        return TRUE;
    }
    return ExAcquireResourceExclusiveLite(&FileContext->MainResource, Wait);
}

/*
 * NtfslxFcbAcquireMainShared - acquire FCB MainResource shared.
 */
BOOLEAN
NtfslxFcbAcquireMainShared(
    _In_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ BOOLEAN Wait)
{
    if (FileContext == NULL || !FileContext->MainResourceInitialized)
    {
        return TRUE;
    }
    return ExAcquireResourceSharedLite(&FileContext->MainResource, Wait);
}

VOID
NtfslxFcbReleaseMain(
    _In_ PNTFSLX_FILE_CONTEXT FileContext)
{
    if (FileContext == NULL || !FileContext->MainResourceInitialized)
    {
        return;
    }
    ExReleaseResourceLite(&FileContext->MainResource);
}

BOOLEAN
NtfslxFcbAcquirePagingIoExclusive(
    _In_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ BOOLEAN Wait)
{
    if (FileContext == NULL || !FileContext->PagingIoResourceInitialized)
    {
        return TRUE;
    }
    return ExAcquireResourceExclusiveLite(&FileContext->PagingIoResource, Wait);
}

BOOLEAN
NtfslxFcbAcquirePagingIoShared(
    _In_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ BOOLEAN Wait)
{
    if (FileContext == NULL || !FileContext->PagingIoResourceInitialized)
    {
        return TRUE;
    }
    return ExAcquireResourceSharedLite(&FileContext->PagingIoResource, Wait);
}

VOID
NtfslxFcbReleasePagingIo(
    _In_ PNTFSLX_FILE_CONTEXT FileContext)
{
    if (FileContext == NULL || !FileContext->PagingIoResourceInitialized)
    {
        return;
    }
    ExReleaseResourceLite(&FileContext->PagingIoResource);
}

/*
 * NtfslxFcbInitializeShareMutex - initialize the per-FCB ShareMutex.
 * Idempotent: a no-op if already initialized.
 */
VOID
NtfslxFcbInitializeShareMutex(
    _Inout_ PNTFSLX_FILE_CONTEXT FileContext)
{
    if (FileContext == NULL || FileContext->ShareMutexInitialized)
    {
        return;
    }
    ExInitializeFastMutex(&FileContext->ShareMutex);
    FileContext->ShareMutexInitialized = TRUE;
}

VOID
NtfslxFcbAcquireShareMutex(
    _In_ PNTFSLX_FILE_CONTEXT FileContext)
{
    if (FileContext == NULL || !FileContext->ShareMutexInitialized)
    {
        return;
    }
    ExAcquireFastMutex(&FileContext->ShareMutex);
}

VOID
NtfslxFcbReleaseShareMutex(
    _In_ PNTFSLX_FILE_CONTEXT FileContext)
{
    if (FileContext == NULL || !FileContext->ShareMutexInitialized)
    {
        return;
    }
    ExReleaseFastMutex(&FileContext->ShareMutex);
}

/*
 * NtfslxVolumeAcquireExclusive - acquire VolumeResource exclusive.
 * Caller becomes the volume-wide writer; blocks all readers until release.
 */
BOOLEAN
NtfslxVolumeAcquireExclusive(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ BOOLEAN Wait)
{
    if (DevExt == NULL || !DevExt->VolumeResourceInitialized)
    {
        return TRUE;
    }
    return ExAcquireResourceExclusiveLite(&DevExt->VolumeResource, Wait);
}

BOOLEAN
NtfslxVolumeAcquireShared(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ BOOLEAN Wait)
{
    if (DevExt == NULL || !DevExt->VolumeResourceInitialized)
    {
        return TRUE;
    }
    return ExAcquireResourceSharedLite(&DevExt->VolumeResource, Wait);
}

VOID
NtfslxVolumeRelease(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt)
{
    if (DevExt == NULL || !DevExt->VolumeResourceInitialized)
    {
        return;
    }
    ExReleaseResourceLite(&DevExt->VolumeResource);
}
