/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     Check resource cleanup after file-information IRPs
 */

#include <kmt_test.h>
#include <ntstrsafe.h>

static
VOID
TestWriteFlushCleanup(VOID)
{
    WCHAR NameBuffer[128];
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    HANDLE Handle;
    PFILE_OBJECT FileObject;
    PFSRTL_COMMON_FCB_HEADER Header;
    NTSTATUS Status;
    LARGE_INTEGER Offset;
    UCHAR Byte = 0x5a;
    ULONG Owners;
    union
    {
        FILE_RENAME_INFORMATION Information;
        UCHAR Bytes[FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) + sizeof(NameBuffer)];
    } Rename;

    RtlStringCchPrintfW(NameBuffer, RTL_NUMBER_OF(NameBuffer),
                       L"\\SystemRoot\\Temp\\kmtest-file-lock-%p.tmp", PsGetCurrentThread());
    RtlInitUnicodeString(&Name, NameBuffer);
    InitializeObjectAttributes(&Attributes, &Name,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    /* FILE_CREATE never overwrites an existing file; this test's file is
     * removed on close, including when an assertion below fails. */
    Status = ZwCreateFile(&Handle, FILE_WRITE_DATA | DELETE | SYNCHRONIZE,
                          &Attributes, &IoStatus, NULL, FILE_ATTRIBUTE_NORMAL,
                          0, FILE_CREATE,
                          FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT |
                          FILE_DELETE_ON_CLOSE, NULL, 0);
    if (skip(NT_SUCCESS(Status), "Cannot create temporary test file: %lx\n", Status))
        return;
    Status = ObReferenceObjectByHandle(Handle, 0, *IoFileObjectType,
                                       KernelMode, (PVOID *)&FileObject, NULL);
    if (!NT_SUCCESS(Status))
    {
        ok_eq_hex(Status, STATUS_SUCCESS);
        ZwClose(Handle);
        return;
    }
    Header = FileObject->FsContext;
    if (!skip(Header != NULL && Header->Resource != NULL,
              "File system does not expose a main file resource\n"))
    {
        KeEnterCriticalRegion();
        Owners = ExIsResourceAcquiredSharedLite(Header->Resource);
        ok_eq_ulong(Owners, 0);
        if (Owners == 0)
        {
            Offset.QuadPart = 0;
            Status = ZwWriteFile(Handle, NULL, NULL, NULL, &IoStatus,
                                 &Byte, sizeof(Byte), &Offset, NULL);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_uint(ExIsResourceAcquiredSharedLite(Header->Resource), 0);
            /* Repair only ownership leaked by the call being tested, so a
             * subsequent flush or close can report failures without hanging. */
            while (ExIsResourceAcquiredSharedLite(Header->Resource) != 0)
                ExReleaseResourceLite(Header->Resource);

            Status = ZwFlushBuffersFile(Handle, &IoStatus);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_uint(ExIsResourceAcquiredSharedLite(Header->Resource), 0);
            while (ExIsResourceAcquiredSharedLite(Header->Resource) != 0)
                ExReleaseResourceLite(Header->Resource);

            /* Exercise both rename phases. The old PSEH cleanup path freed
             * their shared name buffer at the end of each phase. */
            RtlStringCchCatW(NameBuffer, RTL_NUMBER_OF(NameBuffer), L".renamed");
            RtlInitUnicodeString(&Name, NameBuffer);
            RtlZeroMemory(&Rename, sizeof(Rename));
            Rename.Information.ReplaceIfExists = FALSE;
            Rename.Information.FileNameLength = Name.Length;
            RtlCopyMemory(Rename.Information.FileName, Name.Buffer, Name.Length);
            Status = ZwSetInformationFile(Handle, &IoStatus, &Rename,
                                           FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) + Name.Length,
                                           FileRenameInformation);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_uint(ExIsResourceAcquiredSharedLite(Header->Resource), 0);
            while (ExIsResourceAcquiredSharedLite(Header->Resource) != 0)
                ExReleaseResourceLite(Header->Resource);
        }
        KeLeaveCriticalRegion();
    }
    ObDereferenceObject(FileObject);
    ZwClose(Handle);
}

START_TEST(FsRtlQueryFileLock)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\SystemRoot\\System32\\ntdll.dll");
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    FILE_INTERNAL_INFORMATION Information;
    HANDLE Handle;
    PFILE_OBJECT FileObject;
    PFSRTL_COMMON_FCB_HEADER Header;
    PERESOURCE Resource;
    NTSTATUS Status;
    ULONG Iteration;
    ULONG Owners;
    LARGE_INTEGER Offset;
    PUCHAR ReadBuffer;

    InitializeObjectAttributes(&Attributes, &Name,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL, NULL);
    Status = ZwOpenFile(&Handle, FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                        &Attributes, &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT |
                        FILE_NO_INTERMEDIATE_BUFFERING);
    if (skip(NT_SUCCESS(Status), "Cannot open ntdll.dll: %lx\n", Status))
        return;

    Status = ObReferenceObjectByHandle(Handle, 0, *IoFileObjectType,
                                       KernelMode, (PVOID *)&FileObject, NULL);
    if (skip(NT_SUCCESS(Status), "Cannot reference file: %lx\n", Status))
    {
        ZwClose(Handle);
        return;
    }

    Header = FileObject->FsContext;
    if (skip(Header != NULL && Header->Resource != NULL,
             "File system does not expose a main file resource\n"))
        goto Cleanup;

    Resource = Header->Resource;
    KeEnterCriticalRegion();
    Owners = ExIsResourceAcquiredSharedLite(Resource);
    ok_eq_ulong(Owners, 0);
    if (Owners != 0)
    {
        KeLeaveCriticalRegion();
        goto Cleanup;
    }
    if (Owners == 0)
    {
        for (Iteration = 0; Iteration < 3; ++Iteration)
        {
            /* Unlike FileStandardInformation, this class has no fast-I/O
             * query callback: it exercises the IRP handler's finally block. */
            Status = ZwQueryInformationFile(Handle, &IoStatus, &Information,
                                            sizeof(Information), FileInternalInformation);
            ok_eq_hex(Status, STATUS_SUCCESS);
            Owners = ExIsResourceAcquiredSharedLite(Resource);
            ok_eq_ulong(Owners, 0);
            ok_eq_uint(ExIsResourceAcquiredExclusiveLite(Resource), 0);
            if (Owners != 0)
            {
                /* Only release ownership leaked onto this test thread, so
                 * reporting a failure cannot deadlock file-handle cleanup. */
                while (ExIsResourceAcquiredSharedLite(Resource) != 0)
                    ExReleaseResourceLite(Resource);
                break;
            }
        }
    }
    /* Force the read IRP path too, rather than satisfying it through fast I/O.
     * A page-sized nonpaged allocation meets unbuffered I/O alignment. */
    ReadBuffer = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, 'qFsK');
    if (!skip(ReadBuffer != NULL, "Cannot allocate read buffer\n"))
    {
        Offset.QuadPart = 0;
        Status = ZwReadFile(Handle, NULL, NULL, NULL, &IoStatus,
                            ReadBuffer, PAGE_SIZE, &Offset, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ok_eq_ulong(IoStatus.Information, PAGE_SIZE);
            ok(ReadBuffer[0] == 'M' && ReadBuffer[1] == 'Z', "Invalid image header\n");
        }
        Owners = ExIsResourceAcquiredSharedLite(Resource);
        ok_eq_ulong(Owners, 0);
        while (ExIsResourceAcquiredSharedLite(Resource) != 0)
            ExReleaseResourceLite(Resource);
        ExFreePoolWithTag(ReadBuffer, 'qFsK');
    }
    KeLeaveCriticalRegion();

Cleanup:
    ObDereferenceObject(FileObject);
    ZwClose(Handle);
    TestWriteFlushCleanup();
}
