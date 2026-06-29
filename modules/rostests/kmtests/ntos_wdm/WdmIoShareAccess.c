/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     WDM share-access behavior tests
 */

#include <kmt_test.h>

static
VOID
CheckShareAccess(
    _In_ PSHARE_ACCESS ShareAccess,
    _In_ ULONG OpenCount,
    _In_ ULONG Readers,
    _In_ ULONG Writers,
    _In_ ULONG Deleters,
    _In_ ULONG SharedRead,
    _In_ ULONG SharedWrite,
    _In_ ULONG SharedDelete)
{
    ok_eq_ulong(ShareAccess->OpenCount, OpenCount);
    ok_eq_ulong(ShareAccess->Readers, Readers);
    ok_eq_ulong(ShareAccess->Writers, Writers);
    ok_eq_ulong(ShareAccess->Deleters, Deleters);
    ok_eq_ulong(ShareAccess->SharedRead, SharedRead);
    ok_eq_ulong(ShareAccess->SharedWrite, SharedWrite);
    ok_eq_ulong(ShareAccess->SharedDelete, SharedDelete);
}

static
VOID
CheckFileAccess(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN ReadAccess,
    _In_ BOOLEAN WriteAccess,
    _In_ BOOLEAN DeleteAccess,
    _In_ BOOLEAN SharedRead,
    _In_ BOOLEAN SharedWrite,
    _In_ BOOLEAN SharedDelete)
{
    ok_eq_bool(FileObject->ReadAccess, ReadAccess);
    ok_eq_bool(FileObject->WriteAccess, WriteAccess);
    ok_eq_bool(FileObject->DeleteAccess, DeleteAccess);
    ok_eq_bool(FileObject->SharedRead, SharedRead);
    ok_eq_bool(FileObject->SharedWrite, SharedWrite);
    ok_eq_bool(FileObject->SharedDelete, SharedDelete);
}

static
VOID
TestShareAccessConflictMatrix(VOID)
{
    SHARE_ACCESS ShareAccess;
    FILE_OBJECT Reader1;
    FILE_OBJECT Reader2;
    FILE_OBJECT Writer;
    NTSTATUS Status;

    RtlZeroMemory(&ShareAccess, sizeof(ShareAccess));
    RtlZeroMemory(&Reader1, sizeof(Reader1));
    RtlZeroMemory(&Reader2, sizeof(Reader2));
    RtlZeroMemory(&Writer, sizeof(Writer));

    IoSetShareAccess(FILE_READ_DATA, FILE_SHARE_READ, &Reader1, &ShareAccess);
    CheckFileAccess(&Reader1, TRUE, FALSE, FALSE, TRUE, FALSE, FALSE);
    CheckShareAccess(&ShareAccess, 1, 1, 0, 0, 1, 0, 0);

    Status = IoCheckShareAccess(FILE_READ_DATA, FILE_SHARE_READ, &Reader2, &ShareAccess, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckFileAccess(&Reader2, TRUE, FALSE, FALSE, TRUE, FALSE, FALSE);
    CheckShareAccess(&ShareAccess, 2, 2, 0, 0, 2, 0, 0);

    Status = IoCheckShareAccess(FILE_WRITE_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &Writer, &ShareAccess, TRUE);
    ok_eq_hex(Status, STATUS_SHARING_VIOLATION);
    CheckShareAccess(&ShareAccess, 2, 2, 0, 0, 2, 0, 0);

    IoRemoveShareAccess(&Reader2, &ShareAccess);
    CheckShareAccess(&ShareAccess, 1, 1, 0, 0, 1, 0, 0);

    Status = IoCheckShareAccess(FILE_WRITE_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &Writer, &ShareAccess, TRUE);
    ok_eq_hex(Status, STATUS_SHARING_VIOLATION);
    CheckShareAccess(&ShareAccess, 1, 1, 0, 0, 1, 0, 0);

    IoRemoveShareAccess(&Reader1, &ShareAccess);
    CheckShareAccess(&ShareAccess, 0, 0, 0, 0, 0, 0, 0);

    RtlZeroMemory(&Writer, sizeof(Writer));
    Status = IoCheckShareAccess(FILE_WRITE_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &Writer, &ShareAccess, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckFileAccess(&Writer, FALSE, TRUE, FALSE, TRUE, TRUE, FALSE);
    CheckShareAccess(&ShareAccess, 1, 0, 1, 0, 1, 1, 0);

    RtlZeroMemory(&Reader2, sizeof(Reader2));
    Status = IoCheckShareAccess(FILE_READ_DATA, FILE_SHARE_READ, &Reader2, &ShareAccess, TRUE);
    ok_eq_hex(Status, STATUS_SHARING_VIOLATION);
    CheckShareAccess(&ShareAccess, 1, 0, 1, 0, 1, 1, 0);

    IoRemoveShareAccess(&Writer, &ShareAccess);
    CheckShareAccess(&ShareAccess, 0, 0, 0, 0, 0, 0, 0);
}

static
VOID
TestShareAccessUpdateAndClear(VOID)
{
    SHARE_ACCESS ShareAccess;
    FILE_OBJECT FileObject;
    NTSTATUS Status;

    RtlZeroMemory(&ShareAccess, sizeof(ShareAccess));
    RtlZeroMemory(&FileObject, sizeof(FileObject));

    Status = IoCheckShareAccess(FILE_READ_DATA | DELETE,
                                FILE_SHARE_READ | FILE_SHARE_DELETE,
                                &FileObject,
                                &ShareAccess,
                                FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckFileAccess(&FileObject, TRUE, FALSE, TRUE, TRUE, FALSE, TRUE);
    CheckShareAccess(&ShareAccess, 0, 0, 0, 0, 0, 0, 0);

    IoUpdateShareAccess(&FileObject, &ShareAccess);
    CheckShareAccess(&ShareAccess, 1, 1, 0, 1, 1, 0, 1);

    IoRemoveShareAccess(&FileObject, &ShareAccess);
    CheckShareAccess(&ShareAccess, 0, 0, 0, 0, 0, 0, 0);

    RtlFillMemory(&ShareAccess, sizeof(ShareAccess), 0x55);
    RtlZeroMemory(&FileObject, sizeof(FileObject));
    IoSetShareAccess(0, 0, &FileObject, &ShareAccess);
    CheckFileAccess(&FileObject, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE);
    CheckShareAccess(&ShareAccess, 0, 0, 0, 0, 0, 0, 0);
}

START_TEST(WdmIoShareAccess)
{
    TestShareAccessConflictMatrix();
    TestShareAccessUpdateAndClear();
}
