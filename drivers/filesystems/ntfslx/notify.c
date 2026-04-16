/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     FsRtl directory change notification glue
 *
 * Explorer (and any caller of NtNotifyChangeDirectoryFile) subscribes via
 * IRP_MJ_DIRECTORY_CONTROL / IRP_MN_NOTIFY_CHANGE_DIRECTORY. The filesystem
 * parks those IRPs in an FsRtl-managed list and completes them lazily when
 * a matching change (create, write, delete, rename) is reported. Without
 * this plumbing the shell's cached directory view stays stale until F5 —
 * which is exactly what we saw: new files appearing as 0 KB until refresh.
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

NTSTATUS
NtfslxSetFileContextFullPath(
    _Inout_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ PCUNICODE_STRING Path)
{
    USHORT Length;
    USHORT Leaf;
    USHORT I;
    PWCHAR Buffer;

    if (FileContext == NULL || Path == NULL || Path->Buffer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Length = Path->Length;
    if (Length == 0)
    {
        /* Root open (volume). No meaningful path; treat as empty. */
        if (FileContext->FullPath.Buffer != NULL)
        {
            ExFreePoolWithTag(FileContext->FullPath.Buffer, NTFSLX_TAG);
        }
        FileContext->FullPath.Buffer = NULL;
        FileContext->FullPath.Length = 0;
        FileContext->FullPath.MaximumLength = 0;
        FileContext->LeafNameLength = 0;
        return STATUS_SUCCESS;
    }

    Buffer = ExAllocatePoolWithTag(NonPagedPool, Length + sizeof(WCHAR),
                                   NTFSLX_TAG);
    if (Buffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(Buffer, Path->Buffer, Length);
    Buffer[Length / sizeof(WCHAR)] = 0;

    if (FileContext->FullPath.Buffer != NULL)
    {
        ExFreePoolWithTag(FileContext->FullPath.Buffer, NTFSLX_TAG);
    }
    FileContext->FullPath.Buffer = Buffer;
    FileContext->FullPath.Length = Length;
    FileContext->FullPath.MaximumLength = Length + sizeof(WCHAR);

    /* Leaf name = bytes after the last backslash, or the whole path if
     * no backslash (shouldn't happen for a well-formed absolute path). */
    Leaf = Length;
    for (I = 0; I < Length / sizeof(WCHAR); I++)
    {
        if (Buffer[I] == L'\\')
        {
            Leaf = Length - (USHORT)((I + 1) * sizeof(WCHAR));
        }
    }
    FileContext->LeafNameLength = Leaf;

    return STATUS_SUCCESS;
}

VOID
NtfslxNotifyReportChange(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_opt_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ ULONG Filter,
    _In_ ULONG Action)
{
    USHORT TargetNameOffset;

    if (DevExt == NULL || !DevExt->NotifyInitialized)
    {
        return;
    }
    if (FileContext == NULL)
    {
        return;
    }
    if (FileContext->FullPath.Buffer == NULL ||
        FileContext->FullPath.Length == 0)
    {
        return;
    }

    /* FsRtl expects the byte offset of the leaf name within the full path.
     * Length >= LeafNameLength is invariant, enforced by the path walk in
     * NtfslxSetFileContextFullPath. */
    if (FileContext->LeafNameLength > FileContext->FullPath.Length)
    {
        return;
    }
    TargetNameOffset = (USHORT)(FileContext->FullPath.Length -
                                FileContext->LeafNameLength);

    NTFSDBG("ntfslx: NotifyReport path='%wZ' leafOfs=%u filter=0x%08lx action=%lu\n",
             &FileContext->FullPath, (ULONG)TargetNameOffset, Filter, Action);

    FsRtlNotifyFullReportChange(DevExt->NotifySync,
                                &DevExt->NotifyList,
                                (PSTRING)&FileContext->FullPath,
                                TargetNameOffset,
                                (PSTRING)NULL,
                                (PSTRING)NULL,
                                Filter,
                                Action,
                                NULL);
}

NTSTATUS
NtfslxNotifyChangeDirectory(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    PNTFSLX_DEVICE_EXTENSION DevExt;
    PNTFSLX_FILE_CONTEXT FileContext;
    PNTFSLX_CCB Ccb;
    ULONG CompletionFilter;
    BOOLEAN WatchTree;
    PCSTR RejectReason;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    DevExt = DeviceObject->DeviceExtension;
    RejectReason = "unknown";

    if (!NtfslxIsVolumeDevice(DevExt))
    {
        RejectReason = "not-volume-device";
        goto Reject;
    }
    if (!DevExt->NotifyInitialized)
    {
        RejectReason = "notify-not-initialized";
        goto Reject;
    }
    if (Stack->FileObject == NULL)
    {
        RejectReason = "no-file-object";
        goto Reject;
    }

    FileContext = (PNTFSLX_FILE_CONTEXT)Stack->FileObject->FsContext;
    if (FileContext == NULL)
    {
        RejectReason = "no-file-context";
        goto Reject;
    }
    if (FileContext->Signature != NTFSLX_FILE_CONTEXT_SIGNATURE)
    {
        RejectReason = "bad-file-context-signature";
        goto Reject;
    }
    if (!FileContext->IsDirectory)
    {
        RejectReason = "not-directory";
        goto Reject;
    }

    Ccb = (PNTFSLX_CCB)Stack->FileObject->FsContext2;
    if (Ccb == NULL)
    {
        RejectReason = "no-ccb";
        goto Reject;
    }
    if (Ccb->Signature != NTFSLX_CCB_SIGNATURE)
    {
        RejectReason = "bad-ccb-signature";
        goto Reject;
    }

    CompletionFilter = Stack->Parameters.NotifyDirectory.CompletionFilter;
    WatchTree = BooleanFlagOn(Stack->Flags, SL_WATCH_TREE);

    NTFSDBG("ntfslx: NotifyChangeDirectory mft=%I64u path='%wZ' filter=0x%08lx watchTree=%u ccb=%p fileObj=%p\n",
             FileContext->MftIndex,
             &FileContext->FullPath,
             CompletionFilter,
             WatchTree,
             Ccb,
             Stack->FileObject);

    /* Hand the IRP to FsRtl. It will sit on NotifyList until a matching
     * NtfslxNotifyReportChange fires or we cancel it via NtfslxNotifyCleanupCcb. */
    FsRtlNotifyFullChangeDirectory(DevExt->NotifySync,
                                   &DevExt->NotifyList,
                                   Ccb,
                                   (PSTRING)&FileContext->FullPath,
                                   WatchTree,
                                   FALSE,
                                   CompletionFilter,
                                   Irp,
                                   NULL,
                                   NULL);

    NTFSDBG("ntfslx: NotifyChangeDirectory queued path='%wZ' filter=0x%08lx watchTree=%u ccb=%p\n",
             &FileContext->FullPath,
             CompletionFilter,
             WatchTree,
             Ccb);

    /* IRP is now owned by FsRtl; do not complete it ourselves. */
    return STATUS_PENDING;

Reject:
    NTFSDBG("ntfslx: NotifyChangeDirectory reject reason=%s DevObj=%p FileObj=%p flags=0x%02x\n",
             RejectReason,
             DeviceObject,
             Stack->FileObject,
             (ULONG)Stack->Flags);
    Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_INVALID_DEVICE_REQUEST;
}

VOID
NtfslxNotifyCleanupCcb(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_opt_ PNTFSLX_CCB Ccb)
{
    if (DevExt == NULL || !DevExt->NotifyInitialized || Ccb == NULL)
    {
        return;
    }

    NTFSDBG("ntfslx: NotifyCleanup ccb=%p\n",
             Ccb);

    /* Use the dedicated cleanup helper, not the change-directory entrypoint.
     * The latter only drives the delete path when NotifyIrp == NULL and leaves
     * the notify package record itself allocated, which leaks quota-backed
     * buffered change state across repeated watcher open/close cycles. */
    FsRtlNotifyCleanup(DevExt->NotifySync,
                       &DevExt->NotifyList,
                       Ccb);
}
