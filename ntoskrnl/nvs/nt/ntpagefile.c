/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntpagefile.c
 * PURPOSE:     NT paging file management interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/nt/mint.h>

#define MI_MINIMUM_PAGEFILE_SIZE (256ULL * PAGE_SIZE)
#define MI_MAXIMUM_PAGEFILE_SIZE (16ULL * 1024 * 1024 * 1024 * 1024)

typedef struct _MI_NT_PAGEFILE
{
    MI_PAGEFILE Core;
    PFILE_OBJECT FileObject;
    HANDLE FileHandle;
    UNICODE_STRING Name;
} MI_NT_PAGEFILE, *PMI_NT_PAGEFILE;

ULONG MmNumberOfPagingFiles;
PFN_COUNT MiFreeSwapPages;
PFN_COUNT MiUsedSwapPages;
UCHAR MmDisablePagingExecutive = 1;

static PMI_NT_PAGEFILE MiPagingFile;
static KGUARDED_MUTEX MiPagingFileCreationLock;
static BOOLEAN MiPagingFileLockReady;

static
NTSTATUS
MiPageFileRead(
    _In_opt_ PVOID Context,
    _In_ ULONG64 Slot,
    _Out_ PVOID PageBuffer)
{
    PMI_NT_PAGEFILE PagingFile = Context;
    ULONG Transferred;
    NTSTATUS Status;

    Status = MiPagingIo(PagingFile->FileObject, Slot << PAGE_SHIFT, PAGE_SIZE, PageBuffer, FALSE, &Transferred);
    if (NT_SUCCESS(Status) && Transferred != PAGE_SIZE)
        Status = STATUS_IN_PAGE_ERROR;

    return Status;
}

static
NTSTATUS
MiPageFileWrite(
    _In_opt_ PVOID Context,
    _In_ ULONG64 Slot,
    _In_ PVOID PageBuffer)
{
    PMI_NT_PAGEFILE PagingFile = Context;
    ULONG Transferred;

    return MiPagingIo(PagingFile->FileObject, Slot << PAGE_SHIFT, PAGE_SIZE, PageBuffer, TRUE, &Transferred);
}

BOOLEAN
NTAPI
MmIsFileObjectAPagingFile(
    _In_ PFILE_OBJECT FileObject)
{
    PMI_NT_PAGEFILE PagingFile = MiPagingFile;

    return (BOOLEAN)(PagingFile != NULL && PagingFile->FileObject != NULL &&
                     PagingFile->FileObject->SectionObjectPointer == FileObject->SectionObjectPointer);
}

NTSTATUS
NTAPI
NtCreatePagingFile(
    _In_ PUNICODE_STRING FileName,
    _In_ PLARGE_INTEGER MinimumSize,
    _In_ PLARGE_INTEGER MaximumSize,
    _In_ ULONG Reserved)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    MI_PAGEFILE_OPS Ops = { MiPageFileRead, MiPageFileWrite };
    FILE_END_OF_FILE_INFORMATION EndOfFile;
    LARGE_INTEGER SafeMinimum = {0}, SafeMaximum = {0}, AllocationSize;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING CapturedName;
    PMI_NT_PAGEFILE PagingFile;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;
    PWSTR Buffer = NULL;

    UNREFERENCED_PARAMETER(Reserved);
    PAGED_CODE();

    if (!MiPagingFileLockReady)
    {
        KeInitializeGuardedMutex(&MiPagingFileCreationLock);
        MiPagingFileLockReady = TRUE;
    }

    if (PreviousMode != KernelMode && !SeSinglePrivilegeCheck(SeCreatePagefilePrivilege, PreviousMode))
        return STATUS_PRIVILEGE_NOT_HELD;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForRead(MinimumSize, sizeof(LARGE_INTEGER), sizeof(ULONG));
            ProbeForRead(MaximumSize, sizeof(LARGE_INTEGER), sizeof(ULONG));
            ProbeForRead(FileName, sizeof(UNICODE_STRING), sizeof(ULONG));
        }

        SafeMinimum = *MinimumSize;
        SafeMaximum = *MaximumSize;
        CapturedName = *FileName;

        if (CapturedName.Length == 0 || CapturedName.Length > 1024 || (CapturedName.Length & 1))
            _SEH2_YIELD(return STATUS_OBJECT_NAME_INVALID);

        if (PreviousMode != KernelMode)
            ProbeForRead(CapturedName.Buffer, CapturedName.Length, sizeof(WCHAR));

        Buffer = ExAllocatePoolWithTag(PagedPool, CapturedName.Length + sizeof(WCHAR), 'nPmM');
        if (Buffer == NULL)
            _SEH2_YIELD(return STATUS_INSUFFICIENT_RESOURCES);

        RtlCopyMemory(Buffer, CapturedName.Buffer, CapturedName.Length);
        Buffer[CapturedName.Length / sizeof(WCHAR)] = UNICODE_NULL;
        CapturedName.Buffer = Buffer;
        CapturedName.MaximumLength = CapturedName.Length + sizeof(WCHAR);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if ((ULONG64)SafeMinimum.QuadPart < MI_MINIMUM_PAGEFILE_SIZE ||
        (ULONG64)SafeMinimum.QuadPart > MI_MAXIMUM_PAGEFILE_SIZE ||
        SafeMaximum.QuadPart < SafeMinimum.QuadPart)
    {
        ExFreePoolWithTag(Buffer, 'nPmM');
        return STATUS_INVALID_PARAMETER_2;
    }

    KeAcquireGuardedMutex(&MiPagingFileCreationLock);

    if (MiPagingFile != NULL)
    {
        KeReleaseGuardedMutex(&MiPagingFileCreationLock);
        ExFreePoolWithTag(Buffer, 'nPmM');
        return STATUS_TOO_MANY_PAGING_FILES;
    }

    PagingFile = ExAllocatePoolWithTag(NonPagedPool, sizeof(*PagingFile), 'fPmM');
    if (PagingFile == NULL)
    {
        KeReleaseGuardedMutex(&MiPagingFileCreationLock);
        ExFreePoolWithTag(Buffer, 'nPmM');
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(PagingFile, sizeof(*PagingFile));
    PagingFile->Name = CapturedName;

    InitializeObjectAttributes(&ObjectAttributes, &CapturedName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL,
                               NULL);
    AllocationSize = SafeMinimum;

    Status = IoCreateFile(&PagingFile->FileHandle, SYNCHRONIZE | FILE_READ_DATA | FILE_WRITE_DATA,
                          &ObjectAttributes, &IoStatus, &AllocationSize,
                          FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN, FILE_SHARE_WRITE, FILE_SUPERSEDE,
                          FILE_NO_COMPRESSION | FILE_NO_INTERMEDIATE_BUFFERING, NULL, 0, CreateFileTypeNone, NULL,
                          IO_OPEN_PAGING_FILE | IO_NO_PARAMETER_CHECKING);

    if (NT_SUCCESS(Status))
    {
        EndOfFile.EndOfFile = SafeMinimum;
        Status = ZwSetInformationFile(PagingFile->FileHandle, &IoStatus, &EndOfFile, sizeof(EndOfFile),
                                      FileEndOfFileInformation);
    }

    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(PagingFile->FileHandle, FILE_READ_DATA | FILE_WRITE_DATA,
                                           IoFileObjectType, KernelMode, (PVOID *)&PagingFile->FileObject, NULL);
    }

    if (NT_SUCCESS(Status))
    {
        Status = MiPageFileInitialize(&PagingFile->Core, &Ops, PagingFile,
                                      (ULONG64)SafeMinimum.QuadPart >> PAGE_SHIFT);
    }

    if (!NT_SUCCESS(Status))
    {
        if (PagingFile->FileObject != NULL)
            ObDereferenceObject(PagingFile->FileObject);

        if (PagingFile->FileHandle != NULL)
            ZwClose(PagingFile->FileHandle);

        ExFreePoolWithTag(PagingFile, 'fPmM');
        ExFreePoolWithTag(Buffer, 'nPmM');
        KeReleaseGuardedMutex(&MiPagingFileCreationLock);
        return Status;
    }

    MiPagingFile = PagingFile;
    MiSystem.PageFile = &PagingFile->Core;
    MiSystem.CommitLimit += (LONG64)PagingFile->Core.SlotCount;
    MmTotalCommitLimit = (SIZE_T)MiSystem.CommitLimit;
    MmTotalCommitLimitMaximum = MmTotalCommitLimit;
    MiFreeSwapPages = (PFN_COUNT)PagingFile->Core.SlotCount;
    MmNumberOfPagingFiles = 1;

    KeReleaseGuardedMutex(&MiPagingFileCreationLock);
    return STATUS_SUCCESS;
}

VOID
MmShutdownSystem(
    _In_ ULONG Phase)
{
    ULONG Rounds;

    if (Phase != 0)
        return;

    MiSystem.UnusedSegmentLimit = 0;
    MiSegmentPurgeUnused(&MiSystem, MAXULONG);

    for (Rounds = 0; Rounds < 64; Rounds++)
    {
        if (MiWriteModifiedPages(&MiSystem, 1024) == 0)
            break;
    }
}

BOOLEAN MmZeroPageFile;

PFN_NUMBER
NTAPI
MmBuildDumpPageBitmap(
    _Inout_ PRTL_BITMAP Bitmap,
    _In_ BOOLEAN IncludeUserPages)
{
    PFN_NUMBER Count = 0;
    ULONG Frame;

    UNREFERENCED_PARAMETER(IncludeUserPages);

    RtlClearAllBits(Bitmap);

    for (Frame = 0; Frame < MiSystem.Pfn.FrameCount && Frame < Bitmap->SizeOfBitMap; Frame++)
    {
        UCHAR State = MiSystem.Pfn.Pfn[Frame].State;

        if (State == MiPageActive || State == MiPageTransition || State == MiPageModified)
        {
            RtlSetBit(Bitmap, Frame);
            Count++;
        }
    }

    return Count;
}
