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
#define MI_PAGEFILE_GROWTH_PAGES ((64ULL * 1024 * 1024) >> PAGE_SHIFT)
#define MI_PAGEFILE_EXTEND_WAIT (30ULL * 1000 * 1000 * 10)
#define MI_PAGEFILE_EXTEND_BACKOFF (10ULL * 1000 * 1000 * 10)

typedef struct _MI_NT_PAGEFILE
{
    MI_PAGEFILE Core;
    PFILE_OBJECT FileObject;
    HANDLE FileHandle;
    UNICODE_STRING Name;
    ULONG64 MaximumSlots;
} MI_NT_PAGEFILE, *PMI_NT_PAGEFILE;

ULONG MmNumberOfPagingFiles;
PFN_COUNT MiFreeSwapPages;
PFN_COUNT MiUsedSwapPages;
UCHAR MmDisablePagingExecutive = 1;

static PMI_NT_PAGEFILE MiPagingFile;
static KGUARDED_MUTEX MiPagingFileCreationLock;
static BOOLEAN MiPagingFileLockReady;
static WORK_QUEUE_ITEM MiPagingFileExtendItem;
static KEVENT MiPagingFileExtendEvent;
static volatile LONG MiPagingFileExtendQueued;
static volatile LONG64 MiPagingFileExtendPages;
static ULONG64 MiPagingFileExtendFailTime;
static PKTHREAD MiPagingFileExtendThread;

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

static
BOOLEAN
MiExtendPagingFileTo(
    _Inout_ PMI_NT_PAGEFILE PagingFile,
    _In_ ULONG64 Target)
{
    FILE_END_OF_FILE_INFORMATION EndOfFile;
    IO_STATUS_BLOCK IoStatus;
    ULONG64 Current = PagingFile->Core.SlotCount;

    EndOfFile.EndOfFile.QuadPart = (LONGLONG)(Target << PAGE_SHIFT);
    if (!NT_SUCCESS(ZwSetInformationFile(PagingFile->FileHandle, &IoStatus, &EndOfFile, sizeof(EndOfFile),
                                         FileEndOfFileInformation)))
    {
        return FALSE;
    }

    if (!NT_SUCCESS(MiPageFileExtend(&PagingFile->Core, Target)))
        return FALSE;

    MI_ATOMIC_ADD64(&MiSystem.CommitLimit, (LONG64)(Target - Current));
    MmTotalCommitLimit = (SIZE_T)MI_ATOMIC_READ64(&MiSystem.CommitLimit);
    MiFreeSwapPages += (PFN_COUNT)(Target - Current);
    DbgPrint("MM: paging file %wZ extended to %I64u MB, commit limit %I64d pages\n", &PagingFile->Name,
             Target >> (20 - PAGE_SHIFT), MI_ATOMIC_READ64(&MiSystem.CommitLimit));
    return TRUE;
}

static
VOID
NTAPI
MiExtendPagingFileWorker(
    _In_ PVOID Context)
{
    PMI_NT_PAGEFILE PagingFile = Context;
    ULONG64 Current = PagingFile->Core.SlotCount;
    LONG64 Committed;
    LONG64 Request;
    LONG64 Wanted;
    ULONG64 Target;

    MiPagingFileExtendThread = KeGetCurrentThread();

    Request = InterlockedExchange64(&MiPagingFileExtendPages, 0);
    Committed = MI_ATOMIC_READ64(&MiSystem.CommittedPages) + Request;
    Wanted = Committed + Committed / 9 - MI_ATOMIC_READ64(&MiSystem.CommitLimit);
    if (Wanted < (LONG64)MI_PAGEFILE_GROWTH_PAGES)
        Wanted = (LONG64)MI_PAGEFILE_GROWTH_PAGES;

    Target = Current + (ULONG64)Wanted;
    if (Target > PagingFile->MaximumSlots)
        Target = PagingFile->MaximumSlots;

    if (Target > Current && !MiExtendPagingFileTo(PagingFile, Target))
    {
        if (Request <= 0 || Current + (ULONG64)Request >= Target ||
            !MiExtendPagingFileTo(PagingFile, Current + (ULONG64)Request))
        {
            MiPagingFileExtendFailTime = KeQueryInterruptTime();
        }
    }

    MiPagingFileExtendThread = NULL;
    InterlockedExchange(&MiPagingFileExtendQueued, 0);
    KeSetEvent(&MiPagingFileExtendEvent, IO_NO_INCREMENT, FALSE);
}

static
BOOLEAN
MiExpandCommit(
    _Inout_ PMI_SYSTEM System,
    _In_ LONG64 Pages,
    _In_ LONG64 Limit,
    _In_ BOOLEAN Wait)
{
    PMI_NT_PAGEFILE PagingFile = MiPagingFile;
    LARGE_INTEGER Timeout;
    ULONG64 Deadline;
    LONG64 Pending;
    LONG64 Seen;

    if (MI_ATOMIC_READ64(&System->CommitLimit) > Limit)
        return TRUE;

    if (PagingFile == NULL || PagingFile->Core.SlotCount >= PagingFile->MaximumSlots)
        return FALSE;

    if (Pages == 0 && MiPagingFileExtendFailTime != 0 &&
        KeQueryInterruptTime() - MiPagingFileExtendFailTime < MI_PAGEFILE_EXTEND_BACKOFF)
    {
        return FALSE;
    }

    Pending = MI_ATOMIC_READ64(&MiPagingFileExtendPages);
    while (Pages > Pending)
    {
        Seen = InterlockedCompareExchange64(&MiPagingFileExtendPages, Pages, Pending);
        if (Seen == Pending)
            break;

        Pending = Seen;
    }

    if (InterlockedCompareExchange(&MiPagingFileExtendQueued, 1, 0) == 0)
    {
        KeClearEvent(&MiPagingFileExtendEvent);
        ExQueueWorkItem(&MiPagingFileExtendItem, CriticalWorkQueue);
    }

    if (!Wait || KeGetCurrentIrql() > APC_LEVEL || MiPagingFileExtendThread == KeGetCurrentThread())
        return FALSE;

    Timeout.QuadPart = -10 * 1000 * 1000;
    Deadline = KeQueryInterruptTime() + MI_PAGEFILE_EXTEND_WAIT;

    do
    {
        KeWaitForSingleObject(&MiPagingFileExtendEvent, Executive, KernelMode, FALSE, &Timeout);
        if (MI_ATOMIC_READ64(&System->CommitLimit) > Limit)
            return TRUE;
    } while (MI_ATOMIC_READ32(&MiPagingFileExtendQueued) != 0 && KeQueryInterruptTime() < Deadline);

    return (BOOLEAN)(MI_ATOMIC_READ64(&System->CommitLimit) > Limit);
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

    PagingFile->MaximumSlots = (ULONG64)SafeMaximum.QuadPart >> PAGE_SHIFT;
    if (PagingFile->MaximumSlots > (MI_MAXIMUM_PAGEFILE_SIZE >> PAGE_SHIFT))
        PagingFile->MaximumSlots = MI_MAXIMUM_PAGEFILE_SIZE >> PAGE_SHIFT;
    if (PagingFile->MaximumSlots < PagingFile->Core.SlotCount)
        PagingFile->MaximumSlots = PagingFile->Core.SlotCount;

    KeInitializeEvent(&MiPagingFileExtendEvent, NotificationEvent, FALSE);
    ExInitializeWorkItem(&MiPagingFileExtendItem, MiExtendPagingFileWorker, PagingFile);

    MiPagingFile = PagingFile;
    MiSystem.PageFile = &PagingFile->Core;
    MiSystem.CommitLimit += (LONG64)PagingFile->Core.SlotCount;
    MmTotalCommitLimit = (SIZE_T)MiSystem.CommitLimit;
    MmTotalCommitLimitMaximum = MmTotalCommitLimit + (SIZE_T)(PagingFile->MaximumSlots - PagingFile->Core.SlotCount);
    MmtotalCommitLimitMaximum = MmTotalCommitLimitMaximum;
    MiFreeSwapPages = (PFN_COUNT)PagingFile->Core.SlotCount;
    MmNumberOfPagingFiles = 1;
    MiSystem.ExpandCommit = MiExpandCommit;
    DbgPrint("MM: paging file %wZ created, %I64u MB, maximum %I64u MB\n", &PagingFile->Name,
             PagingFile->Core.SlotCount >> (20 - PAGE_SHIFT), PagingFile->MaximumSlots >> (20 - PAGE_SHIFT));

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
