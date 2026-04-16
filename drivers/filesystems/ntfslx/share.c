/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Share-access enforcement across multiple opens of the same file
 *
 * Each MFT index gets a single NTFSLX_SHARE_RECORD on the volume's ShareList.
 * The first opener allocates the record and seeds it via IoSetShareAccess;
 * subsequent openers IoCheckShareAccess against the existing state and
 * IoUpdateShareAccess on success. Last-closer (RefCount==0) unlinks and frees
 * the record. Without this, every conflicting open succeeds and writers can
 * silently scribble over readers' state — a corruption-class bug for any
 * filesystem the OS is going to boot from.
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

VOID
NtfslxShareInitialize(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt)
{
    InitializeListHead(&DevExt->ShareList);
    ExInitializeFastMutex(&DevExt->ShareMutex);
    DevExt->ShareInitialized = TRUE;
}

/*
 * Look up an existing share record for MftIndex while holding ShareLock.
 * Returns NULL if no opener has registered yet.
 */
static
PNTFSLX_SHARE_RECORD
NtfslxShareFindLocked(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ ULONGLONG MftIndex)
{
    PLIST_ENTRY Entry;
    PNTFSLX_SHARE_RECORD Record;

    for (Entry = DevExt->ShareList.Flink;
         Entry != &DevExt->ShareList;
         Entry = Entry->Flink)
    {
        Record = CONTAINING_RECORD(Entry, NTFSLX_SHARE_RECORD, Link);
        if (Record->MftIndex == MftIndex)
        {
            return Record;
        }
    }
    return NULL;
}

/*
 * Acquire share access for a new open. On success returns STATUS_SUCCESS
 * and stores the share record into *OutRecord (which the caller must hand
 * back via NtfslxShareAccessRelease at cleanup time). On failure returns
 * STATUS_SHARING_VIOLATION (or another NTSTATUS) and *OutRecord is NULL.
 *
 * The matrix follows IoCheckShareAccess semantics:
 *   - First opener: IoSetShareAccess seeds the record from this open's
 *     desired access + share mode; always succeeds.
 *   - Subsequent: IoCheckShareAccess validates this open's request against
 *     the cumulative state, and on success IoUpdateShareAccess folds it in.
 */
NTSTATUS
NtfslxShareAccessAcquire(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ ULONGLONG MftIndex,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG ShareAccess,
    _In_ PFILE_OBJECT FileObject,
    _Outptr_ PNTFSLX_SHARE_RECORD *OutRecord)
{
    PNTFSLX_SHARE_RECORD Existing;
    PNTFSLX_SHARE_RECORD Allocated = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    *OutRecord = NULL;

    if (!DevExt->ShareInitialized)
    {
        return STATUS_SUCCESS;
    }

    Allocated = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Allocated),
                                      NTFSLX_TAG);
    if (Allocated == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Allocated, sizeof(*Allocated));
    Allocated->MftIndex = MftIndex;
    Allocated->RefCount = 1;

    ExAcquireFastMutex(&DevExt->ShareMutex);

    Existing = NtfslxShareFindLocked(DevExt, MftIndex);
    if (Existing != NULL)
    {
        Status = IoCheckShareAccess(DesiredAccess, ShareAccess, FileObject,
                                    &Existing->ShareAccess, TRUE);
        if (NT_SUCCESS(Status))
        {
            Existing->RefCount++;
            *OutRecord = Existing;
        }
        ExReleaseFastMutex(&DevExt->ShareMutex);
        ExFreePoolWithTag(Allocated, NTFSLX_TAG);
        return Status;
    }

    /* First opener for this MFT. Seed and link. */
    IoSetShareAccess(DesiredAccess, ShareAccess, FileObject,
                     &Allocated->ShareAccess);
    InsertTailList(&DevExt->ShareList, &Allocated->Link);
    *OutRecord = Allocated;
    ExReleaseFastMutex(&DevExt->ShareMutex);
    return STATUS_SUCCESS;
}

/*
 * Release a previously-acquired share record. Symmetric with Acquire: each
 * call decrements the refcount and removes the FILE_OBJECT's contribution
 * via IoRemoveShareAccess. When the last opener releases, the record is
 * unlinked and freed.
 */
VOID
NtfslxShareAccessRelease(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_opt_ PNTFSLX_SHARE_RECORD Record,
    _In_opt_ PFILE_OBJECT FileObject)
{
    BOOLEAN FreeIt = FALSE;

    if (Record == NULL || !DevExt->ShareInitialized)
    {
        return;
    }

    ExAcquireFastMutex(&DevExt->ShareMutex);

    if (FileObject != NULL)
    {
        IoRemoveShareAccess(FileObject, &Record->ShareAccess);
    }
    if (Record->RefCount > 0)
    {
        Record->RefCount--;
    }
    if (Record->RefCount == 0)
    {
        RemoveEntryList(&Record->Link);
        FreeIt = TRUE;
    }

    ExReleaseFastMutex(&DevExt->ShareMutex);

    if (FreeIt)
    {
        ExFreePoolWithTag(Record, NTFSLX_TAG);
    }
}
