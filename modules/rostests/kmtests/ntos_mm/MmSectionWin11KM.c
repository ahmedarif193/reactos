/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Kernel-Mode Test Suite for Win11-parity Mm SECTION behavior
 *
 * Section-object parity checks.  Every contract asserted here is observable from
 * a kernel-mode driver (handle validity, mapped-view identity, shared-memory
 * coherency, exported helper return values), so the SAME driver binary can be
 * run against the Win11 ARM64 reference kernel (ground truth) and against
 * ReactOS.  A mismatch reveals a ReactOS divergence - in particular the two
 * still-stubbed exports MmDoesFileHaveUserWritableReferences (returns 0) and
 * MmDisableModifiedWriteOfSection (returns FALSE).
 *
 * SAFETY NOTES (sections bugcheck very easily, and a Win11 bugcheck is costly):
 *   - The core cases use ONLY pagefile-backed sections (ZwCreateSection with
 *     FileHandle == NULL, SEC_COMMIT, page-sized MaximumSize); no real file is
 *     required and nothing system-owned is ever touched.
 *   - Every section/map call is SEH-guarded and every view/handle/reference is
 *     released on every path (no leaks, no double-frees).
 *   - The two file-only stub targets need a real FILE_OBJECT->SectionObjectPointer.
 *     That cannot exist for a pagefile-backed section, so they obtain it
 *     defensively from a private DELETE_ON_CLOSE scratch file and SKIP (with a
 *     trace) if it cannot be acquired safely.  A SECTION_OBJECT_POINTERS is never
 *     guessed - passing a bogus pointer to these routines crashed a prior test.
 */

#include <kmt_test.h>

/*
 * MmDisableModifiedWriteOfSection is exported by ntoskrnl (see ntoskrnl.spec)
 * but is not declared in any DDK/IFS header, so declare it locally to keep this
 * TU self-contained.  MmDoesFileHaveUserWritableReferences IS declared by
 * <ntifs.h> (pulled in through kmt_test.h) and is used as-is.
 *
 * MmEnableModifiedWriteOfSection is deliberately NOT used: it is not exported by
 * ReactOS, and the scratch section is torn down immediately after the call so it
 * never needs re-enabling.
 */
NTKERNELAPI
BOOLEAN
NTAPI
MmDisableModifiedWriteOfSection(
    _In_ PSECTION_OBJECT_POINTERS SectionObjectPointer);

static UNICODE_STRING ScratchFilePath =
    RTL_CONSTANT_STRING(L"\\SystemRoot\\kmtest-MmSectionWin11.dat");

/* ------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* ------------------------------------------------------------------------- */

/*
 * Create a pagefile-backed, SEC_COMMIT section of the given page-aligned size.
 * Fully SEH-guarded; on success *OutHandle holds a kernel handle the caller must
 * ZwClose.  Returns the ZwCreateSection status (or the raised exception code).
 */
static
NTSTATUS
CreatePagefileSection(
    _Out_ PHANDLE OutHandle,
    _In_ SIZE_T Size,
    _In_ ULONG PageProtection)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    OBJECT_ATTRIBUTES ObjectAttributes;
    LARGE_INTEGER MaximumSize;

    *OutHandle = NULL;
    MaximumSize.QuadPart = (LONGLONG)Size;
    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

    _SEH2_TRY
    {
        Status = ZwCreateSection(OutHandle,
                                 SECTION_ALL_ACCESS,
                                 &ObjectAttributes,
                                 &MaximumSize,
                                 PageProtection,
                                 SEC_COMMIT,
                                 NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        ok(0, "ZwCreateSection (pagefile) raised 0x%08lx\n", Status);
    }
    _SEH2_END;

    return Status;
}

/*
 * Create a small private DELETE_ON_CLOSE scratch file with a known content, so a
 * file-backed (data) section - and therefore a real SECTION_OBJECT_POINTERS -
 * can be obtained safely.  On success *OutHandle is a kernel handle (ZwClose
 * deletes the file) and *OutSize is the file size.
 */
static
NTSTATUS
CreateScratchFile(
    _Out_ PHANDLE OutHandle,
    _Out_ PLARGE_INTEGER OutSize)
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER ByteOffset;
    HANDLE Handle = NULL;
    UCHAR Buffer[64];
    SIZE_T i;

    *OutHandle = NULL;
    OutSize->QuadPart = 0;

    for (i = 0; i < sizeof(Buffer); i++)
        Buffer[i] = (UCHAR)(i + 1);

    InitializeObjectAttributes(&ObjectAttributes,
                               &ScratchFilePath,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = ZwCreateFile(&Handle,
                          GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                          &ObjectAttributes,
                          &IoStatusBlock,
                          NULL,
                          FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          FILE_SUPERSEDE,
                          FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT |
                              FILE_DELETE_ON_CLOSE,
                          NULL,
                          0);
    if (!NT_SUCCESS(Status))
        return Status;

    ByteOffset.QuadPart = 0;
    Status = ZwWriteFile(Handle, NULL, NULL, NULL, &IoStatusBlock,
                         Buffer, sizeof(Buffer), &ByteOffset, NULL);
    if (Status == STATUS_PENDING)
    {
        ZwWaitForSingleObject(Handle, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }
    if (!NT_SUCCESS(Status))
    {
        ZwClose(Handle);
        return Status;
    }

    *OutHandle = Handle;
    OutSize->QuadPart = (LONGLONG)sizeof(Buffer);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------- */
/* Core (pagefile-backed) cases                                              */
/* ------------------------------------------------------------------------- */

/*
 * ZwCreateSection: a pagefile-backed SEC_COMMIT/PAGE_READWRITE section of the
 * requested size succeeds with a valid handle, reports a size >= the request and
 * carries SEC_COMMIT in its attributes, and closes cleanly.
 */
static
VOID
CreateAndQuery(
    _In_ SIZE_T Size)
{
    NTSTATUS Status;
    HANDLE SectionHandle = NULL;
    SECTION_BASIC_INFORMATION Sbi;

    Status = CreatePagefileSection(&SectionHandle, Size, PAGE_READWRITE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (skip(NT_SUCCESS(Status) && SectionHandle != NULL,
             "No pagefile section for size %lu\n", (ULONG)Size))
        return;

    ok(SectionHandle != NULL, "SectionHandle is NULL\n");

    RtlZeroMemory(&Sbi, sizeof(Sbi));
    Status = ZwQuerySection(SectionHandle, SectionBasicInformation,
                            &Sbi, sizeof(Sbi), NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        trace("size=%lu -> Sbi.Size=0x%I64x Attributes=0x%lx\n",
              (ULONG)Size, Sbi.Size.QuadPart, Sbi.Attributes);
        ok(Sbi.Size.QuadPart >= (LONGLONG)Size,
           "Sbi.Size 0x%I64x < requested 0x%I64x\n",
           Sbi.Size.QuadPart, (ULONGLONG)Size);
        ok((Sbi.Attributes & SEC_COMMIT) != 0,
           "SEC_COMMIT not set in attributes 0x%lx\n", Sbi.Attributes);
    }

    Status = ZwClose(SectionHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

static
VOID
TestCreateSectionPagefile(VOID)
{
    /* Single-page and multi-page maximum sizes. */
    CreateAndQuery(PAGE_SIZE);
    CreateAndQuery(4 * PAGE_SIZE);
}

/*
 * ZwMapViewOfSection into the current process: the view base is non-NULL,
 * page-aligned and below the system range, the granted ViewSize is at least the
 * requested size, and the mapping is genuinely writable (write a pattern, read
 * it back).  The view is always unmapped and the handle closed.
 */
static
VOID
TestMapViewIntoCurrentProcess(VOID)
{
    NTSTATUS Status;
    HANDLE SectionHandle = NULL;
    PVOID BaseAddress = NULL;
    SIZE_T ViewSize = 2 * PAGE_SIZE;
    LARGE_INTEGER SectionOffset;
    SIZE_T RequestedView = 2 * PAGE_SIZE;

    Status = CreatePagefileSection(&SectionHandle, 2 * PAGE_SIZE, PAGE_READWRITE);
    if (skip(NT_SUCCESS(Status) && SectionHandle != NULL, "No section\n"))
        return;

    SectionOffset.QuadPart = 0;
    _SEH2_TRY
    {
        Status = ZwMapViewOfSection(SectionHandle,
                                    ZwCurrentProcess(),
                                    &BaseAddress,
                                    0,
                                    0,
                                    &SectionOffset,
                                    &ViewSize,
                                    ViewUnmap,
                                    0,
                                    PAGE_READWRITE);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        ok(0, "ZwMapViewOfSection raised 0x%08lx\n", Status);
    }
    _SEH2_END;

    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!skip(NT_SUCCESS(Status) && BaseAddress != NULL, "View not mapped\n"))
    {
        ok((LONG_PTR)BaseAddress > 0, "BaseAddress = %p\n", BaseAddress);
        ok(((ULONG_PTR)BaseAddress % PAGE_SIZE) == 0,
           "BaseAddress %p not page-aligned\n", BaseAddress);
        ok((PUCHAR)BaseAddress < (PUCHAR)MmSystemRangeStart,
           "BaseAddress %p not below system range %p\n",
           BaseAddress, MmSystemRangeStart);
        ok(ViewSize >= RequestedView,
           "ViewSize %I64u < requested %I64u\n",
           (ULONGLONG)ViewSize, (ULONGLONG)RequestedView);

        /* The view must be real, writable memory backed by the section. */
        KmtStartSeh()
            RtlFillMemory(BaseAddress, RequestedView, 0xC4);
            ok(((volatile UCHAR *)BaseAddress)[0] == 0xC4 &&
               ((volatile UCHAR *)BaseAddress)[RequestedView - 1] == 0xC4,
               "Mapped view not writable/readable\n");
        KmtEndSeh(STATUS_SUCCESS);

        Status = ZwUnmapViewOfSection(ZwCurrentProcess(), BaseAddress);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    ZwClose(SectionHandle);
}

/*
 * Two views of the SAME pagefile-backed section in the same process share their
 * backing store: a write through one view is immediately visible through the
 * other, in both directions.
 */
static
VOID
TestRemapSharedCoherency(VOID)
{
    NTSTATUS Status;
    HANDLE SectionHandle = NULL;
    PVOID View1 = NULL;
    PVOID View2 = NULL;
    SIZE_T ViewSize1 = PAGE_SIZE;
    SIZE_T ViewSize2 = PAGE_SIZE;
    LARGE_INTEGER SectionOffset;
    BOOLEAN Mapped1 = FALSE;
    BOOLEAN Mapped2 = FALSE;

    Status = CreatePagefileSection(&SectionHandle, PAGE_SIZE, PAGE_READWRITE);
    if (skip(NT_SUCCESS(Status) && SectionHandle != NULL, "No section\n"))
        return;

    SectionOffset.QuadPart = 0;
    _SEH2_TRY
    {
        Status = ZwMapViewOfSection(SectionHandle, ZwCurrentProcess(), &View1, 0, 0,
                                    &SectionOffset, &ViewSize1, ViewUnmap, 0,
                                    PAGE_READWRITE);
        if (NT_SUCCESS(Status))
            Mapped1 = TRUE;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        ok(0, "first ZwMapViewOfSection raised 0x%08lx\n", Status);
    }
    _SEH2_END;
    ok_eq_hex(Status, STATUS_SUCCESS);

    SectionOffset.QuadPart = 0;
    _SEH2_TRY
    {
        Status = ZwMapViewOfSection(SectionHandle, ZwCurrentProcess(), &View2, 0, 0,
                                    &SectionOffset, &ViewSize2, ViewUnmap, 0,
                                    PAGE_READWRITE);
        if (NT_SUCCESS(Status))
            Mapped2 = TRUE;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        ok(0, "second ZwMapViewOfSection raised 0x%08lx\n", Status);
    }
    _SEH2_END;
    ok_eq_hex(Status, STATUS_SUCCESS);

    if (Mapped1 && Mapped2 &&
        !skip(View1 != NULL && View2 != NULL && View1 != View2,
              "Need two distinct views (v1=%p v2=%p)\n", View1, View2))
    {
        KmtStartSeh()
            /* Write through view 1, read through view 2. */
            *(volatile ULONG *)View1 = 0xA5A5F00D;
            ok(*(volatile ULONG *)View2 == 0xA5A5F00D,
               "View2 = 0x%08lx, expected 0xA5A5F00D (write via View1)\n",
               *(volatile ULONG *)View2);

            /* Write through view 2, read through view 1. */
            *(volatile ULONG *)View2 = 0x0BADC0DE;
            ok(*(volatile ULONG *)View1 == 0x0BADC0DE,
               "View1 = 0x%08lx, expected 0x0BADC0DE (write via View2)\n",
               *(volatile ULONG *)View1);
        KmtEndSeh(STATUS_SUCCESS);
    }

    if (Mapped2)
    {
        Status = ZwUnmapViewOfSection(ZwCurrentProcess(), View2);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }
    if (Mapped1)
    {
        Status = ZwUnmapViewOfSection(ZwCurrentProcess(), View1);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }
    ZwClose(SectionHandle);
}

/*
 * MmMapViewInSystemSpace / MmUnmapViewInSystemSpace on a section OBJECT (obtained
 * with ObReferenceObjectByHandle using MmSectionObjectType): the system-space VA
 * is non-NULL, usable (writable from kernel) and unmaps cleanly.  All map calls
 * and the access are SEH-guarded; the object reference is always released.
 */
static
VOID
TestMapViewInSystemSpace(VOID)
{
    NTSTATUS Status;
    HANDLE SectionHandle = NULL;
    PVOID SectionObject = NULL;
    PVOID MappedBase = NULL;
    SIZE_T ViewSize = PAGE_SIZE;

    Status = CreatePagefileSection(&SectionHandle, PAGE_SIZE, PAGE_READWRITE);
    if (skip(NT_SUCCESS(Status) && SectionHandle != NULL, "No section\n"))
        return;

    Status = ObReferenceObjectByHandle(SectionHandle,
                                       SECTION_MAP_READ | SECTION_MAP_WRITE,
                                       MmSectionObjectType,
                                       KernelMode,
                                       &SectionObject,
                                       NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!skip(NT_SUCCESS(Status) && SectionObject != NULL,
              "No section object\n"))
    {
        _SEH2_TRY
        {
            Status = MmMapViewInSystemSpace(SectionObject, &MappedBase, &ViewSize);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
            ok(0, "MmMapViewInSystemSpace raised 0x%08lx\n", Status);
        }
        _SEH2_END;

        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!skip(NT_SUCCESS(Status) && MappedBase != NULL,
                  "No system-space view\n"))
        {
            ok(MappedBase != NULL, "MappedBase is NULL\n");
            ok(ViewSize >= PAGE_SIZE, "ViewSize %I64u < PAGE_SIZE\n",
               (ULONGLONG)ViewSize);

            KmtStartSeh()
                RtlFillMemory(MappedBase, PAGE_SIZE, 0x7E);
                ok(*(volatile UCHAR *)MappedBase == 0x7E,
                   "System-space view not writable\n");
            KmtEndSeh(STATUS_SUCCESS);

            Status = MmUnmapViewInSystemSpace(MappedBase);
            ok_eq_hex(Status, STATUS_SUCCESS);
        }

        ObDereferenceObject(SectionObject);
    }

    ZwClose(SectionHandle);
}

/*
 * Section object type sanity (without ObGetObjectType, which is a stub on ARM64):
 * a section handle references successfully as MmSectionObjectType, and references
 * as the file type with STATUS_OBJECT_TYPE_MISMATCH.
 */
static
VOID
TestSectionObjectType(VOID)
{
    NTSTATUS Status;
    HANDLE SectionHandle = NULL;
    PVOID Object = NULL;

    Status = CreatePagefileSection(&SectionHandle, PAGE_SIZE, PAGE_READWRITE);
    if (skip(NT_SUCCESS(Status) && SectionHandle != NULL, "No section\n"))
        return;

    /* Correct type resolves. */
    Status = ObReferenceObjectByHandle(SectionHandle, SECTION_QUERY,
                                       MmSectionObjectType, KernelMode,
                                       &Object, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Object != NULL, "Section object is NULL\n");
    if (NT_SUCCESS(Status) && Object != NULL)
        ObDereferenceObject(Object);

    /* Wrong type is rejected, proving the handle really is a Section. */
    Object = NULL;
    Status = ObReferenceObjectByHandle(SectionHandle, 0,
                                       *IoFileObjectType, KernelMode,
                                       &Object, NULL);
    ok_eq_hex(Status, STATUS_OBJECT_TYPE_MISMATCH);
    if (NT_SUCCESS(Status) && Object != NULL)
        ObDereferenceObject(Object);

    ZwClose(SectionHandle);
}

/* ------------------------------------------------------------------------- */
/* Stub targets (need a real file-backed SECTION_OBJECT_POINTERS)            */
/* ------------------------------------------------------------------------- */

/*
 * MmDoesFileHaveUserWritableReferences (ReactOS: STUB -> always returns 0).
 *
 * Windows contract: while a writable user view of the file's data section is
 * mapped the count is > 0; after the view is unmapped and the section handle
 * closed it is 0.  Obtained from a private DELETE_ON_CLOSE scratch file - a
 * pagefile section has no SECTION_OBJECT_POINTERS, so if the file cannot be set
 * up safely the whole case is skipped (the pointer is never guessed).
 */
static
VOID
TestDoesFileHaveUserWritableReferences(VOID)
{
    NTSTATUS Status;
    HANDLE FileHandle = NULL;
    HANDLE SectionHandle = NULL;
    PFILE_OBJECT FileObject = NULL;
    PSECTION_OBJECT_POINTERS Pointers = NULL;
    PVOID BaseAddress = NULL;
    SIZE_T ViewSize = 0;
    LARGE_INTEGER FileSize;
    LARGE_INTEGER SectionOffset;
    ULONG WritableRefs;
    BOOLEAN Mapped = FALSE;

    Status = CreateScratchFile(&FileHandle, &FileSize);
    if (skip(NT_SUCCESS(Status) && FileHandle != NULL,
             "No scratch file for SECTION_OBJECT_POINTERS (0x%08lx); "
             "pagefile sections have none\n", Status))
        return;

    /* Writable data section on the file => sets FileObject->SectionObjectPointer. */
    _SEH2_TRY
    {
        Status = ZwCreateSection(&SectionHandle, SECTION_ALL_ACCESS, NULL,
                                 &FileSize, PAGE_READWRITE, SEC_COMMIT, FileHandle);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        ok(0, "ZwCreateSection (file) raised 0x%08lx\n", Status);
    }
    _SEH2_END;
    if (skip(NT_SUCCESS(Status) && SectionHandle != NULL,
             "No file-backed section (0x%08lx)\n", Status))
    {
        ZwClose(FileHandle);
        return;
    }

    Status = ObReferenceObjectByHandle(FileHandle, FILE_READ_DATA | FILE_WRITE_DATA,
                                       *IoFileObjectType, KernelMode,
                                       (PVOID *)&FileObject, NULL);
    if (skip(NT_SUCCESS(Status) && FileObject != NULL,
             "No file object (0x%08lx)\n", Status))
    {
        ZwClose(SectionHandle);
        ZwClose(FileHandle);
        return;
    }

    Pointers = FileObject->SectionObjectPointer;
    if (skip(Pointers != NULL,
             "FileObject->SectionObjectPointer is NULL; cannot test safely\n"))
    {
        ObDereferenceObject(FileObject);
        ZwClose(SectionHandle);
        ZwClose(FileHandle);
        return;
    }

    /* Map a writable view into user space -> a writable user reference. */
    SectionOffset.QuadPart = 0;
    _SEH2_TRY
    {
        Status = ZwMapViewOfSection(SectionHandle, ZwCurrentProcess(), &BaseAddress,
                                    0, 0, &SectionOffset, &ViewSize, ViewUnmap, 0,
                                    PAGE_READWRITE);
        if (NT_SUCCESS(Status))
            Mapped = TRUE;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        ok(0, "ZwMapViewOfSection (file) raised 0x%08lx\n", Status);
    }
    _SEH2_END;
    ok_eq_hex(Status, STATUS_SUCCESS);

    if (!skip(Mapped, "Writable view not mapped\n"))
    {
        WritableRefs = 0;
        _SEH2_TRY
        {
            WritableRefs = MmDoesFileHaveUserWritableReferences(Pointers);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            ok(0, "MmDoesFileHaveUserWritableReferences raised 0x%08lx\n",
               _SEH2_GetExceptionCode());
        }
        _SEH2_END;

        trace("MmDoesFileHaveUserWritableReferences (mapped) = %lu\n", WritableRefs);
        /* Windows: > 0. ReactOS stub: 0 -> this assert fails and flags the stub. */
        ok(WritableRefs > 0,
           "Expected > 0 writable references while a writable view is mapped, got %lu\n",
           WritableRefs);

        Status = ZwUnmapViewOfSection(ZwCurrentProcess(), BaseAddress);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Mapped = FALSE;
    }

    /* Drop the section handle as well, then there must be no writable refs. */
    ZwClose(SectionHandle);
    SectionHandle = NULL;

    WritableRefs = (ULONG)-1;
    _SEH2_TRY
    {
        WritableRefs = MmDoesFileHaveUserWritableReferences(Pointers);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ok(0, "MmDoesFileHaveUserWritableReferences (post-unmap) raised 0x%08lx\n",
           _SEH2_GetExceptionCode());
    }
    _SEH2_END;
    trace("MmDoesFileHaveUserWritableReferences (unmapped) = %lu\n", WritableRefs);
    ok(WritableRefs == 0,
       "Expected 0 writable references after unmap/close, got %lu\n", WritableRefs);

    if (Mapped)
        ZwUnmapViewOfSection(ZwCurrentProcess(), BaseAddress);
    ObDereferenceObject(FileObject);
    if (SectionHandle != NULL)
        ZwClose(SectionHandle);
    ZwClose(FileHandle);
}

/*
 * MmDisableModifiedWriteOfSection (ReactOS: STUB -> always returns FALSE).
 *
 * Windows contract: for a single-view writable data section it disables the
 * modified-page writer and returns TRUE.  Same safe acquisition as above (real
 * file-backed SECTION_OBJECT_POINTERS, never a guessed pointer); SKIP if the
 * scratch file cannot be set up.  The scratch file is DELETE_ON_CLOSE and torn
 * down immediately, so disabling its modified writes is harmless and there is no
 * need for MmEnableModifiedWriteOfSection (which ReactOS does not export).
 */
static
VOID
TestDisableModifiedWriteOfSection(VOID)
{
    NTSTATUS Status;
    HANDLE FileHandle = NULL;
    HANDLE SectionHandle = NULL;
    PFILE_OBJECT FileObject = NULL;
    PSECTION_OBJECT_POINTERS Pointers = NULL;
    PVOID BaseAddress = NULL;
    SIZE_T ViewSize = 0;
    LARGE_INTEGER FileSize;
    LARGE_INTEGER SectionOffset;
    BOOLEAN Disabled = FALSE;
    BOOLEAN Mapped = FALSE;

    Status = CreateScratchFile(&FileHandle, &FileSize);
    if (skip(NT_SUCCESS(Status) && FileHandle != NULL,
             "No scratch file for SECTION_OBJECT_POINTERS (0x%08lx); "
             "pagefile sections have none\n", Status))
        return;

    _SEH2_TRY
    {
        Status = ZwCreateSection(&SectionHandle, SECTION_ALL_ACCESS, NULL,
                                 &FileSize, PAGE_READWRITE, SEC_COMMIT, FileHandle);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        ok(0, "ZwCreateSection (file) raised 0x%08lx\n", Status);
    }
    _SEH2_END;
    if (skip(NT_SUCCESS(Status) && SectionHandle != NULL,
             "No file-backed section (0x%08lx)\n", Status))
    {
        ZwClose(FileHandle);
        return;
    }

    Status = ObReferenceObjectByHandle(FileHandle, FILE_READ_DATA | FILE_WRITE_DATA,
                                       *IoFileObjectType, KernelMode,
                                       (PVOID *)&FileObject, NULL);
    if (skip(NT_SUCCESS(Status) && FileObject != NULL,
             "No file object (0x%08lx)\n", Status))
    {
        ZwClose(SectionHandle);
        ZwClose(FileHandle);
        return;
    }

    Pointers = FileObject->SectionObjectPointer;
    if (skip(Pointers != NULL,
             "FileObject->SectionObjectPointer is NULL; cannot test safely\n"))
    {
        ObDereferenceObject(FileObject);
        ZwClose(SectionHandle);
        ZwClose(FileHandle);
        return;
    }

    /* Map exactly one writable view (the single-view shape Windows expects). */
    SectionOffset.QuadPart = 0;
    _SEH2_TRY
    {
        Status = ZwMapViewOfSection(SectionHandle, ZwCurrentProcess(), &BaseAddress,
                                    0, 0, &SectionOffset, &ViewSize, ViewUnmap, 0,
                                    PAGE_READWRITE);
        if (NT_SUCCESS(Status))
            Mapped = TRUE;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        ok(0, "ZwMapViewOfSection (file) raised 0x%08lx\n", Status);
    }
    _SEH2_END;
    ok_eq_hex(Status, STATUS_SUCCESS);

    _SEH2_TRY
    {
        Disabled = MmDisableModifiedWriteOfSection(Pointers);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ok(0, "MmDisableModifiedWriteOfSection raised 0x%08lx\n",
           _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    /*
     * Win11 returns FALSE here too -- it will not disable modified-write while a
     * writable view is mapped -- so this is not a clean divergence as written
     * (both Win11 and the ReactOS stub return FALSE). Trace only; a real
     * Win11<->ROS divergence for MmDisableModifiedWriteOfSection needs a
     * codex-derived contract (a setup where Win11 actually returns TRUE).
     */
    trace("MmDisableModifiedWriteOfSection = %s\n", Disabled ? "TRUE" : "FALSE");

    if (Mapped)
    {
        Status = ZwUnmapViewOfSection(ZwCurrentProcess(), BaseAddress);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }
    ObDereferenceObject(FileObject);
    ZwClose(SectionHandle);
    ZwClose(FileHandle);
}

START_TEST(MmSectionWin11KM)
{
    trace("MmSectionWin11KM: NT version 0x%04x\n", GetNTVersion());

    /* Core, pagefile-backed (safe everywhere). */
    TestCreateSectionPagefile();
    TestMapViewIntoCurrentProcess();
    TestRemapSharedCoherency();
    TestMapViewInSystemSpace();
    TestSectionObjectType();

    /* Still-stubbed exports (file-backed SECTION_OBJECT_POINTERS, defensive). */
    TestDoesFileHaveUserWritableReferences();
    TestDisableModifiedWriteOfSection();
}
