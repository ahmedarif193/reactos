/*
 * PROJECT:     ReactOS exFAT filesystem driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Directory enumeration
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "exfat.h"

#define NDEBUG
#include <debug.h>

#define EXFAT_ALIGN_DIRECTORY_ENTRY(Size) (((Size) + 7) & ~7UL)

static NTSTATUS
ExFatSetSearchPattern(
    PEXFAT_CCB Ccb,
    PUNICODE_STRING Pattern)
{
    static const WCHAR MatchAll[] = L"*";
    UNICODE_STRING Source;
    PWCHAR Buffer;
    ULONG Index;

    if (Pattern && Pattern->Length)
    {
        Source = *Pattern;
    }
    else
    {
        RtlInitUnicodeString(&Source, MatchAll);
    }

    Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                   Source.Length + sizeof(WCHAR),
                                   TAG_EXFAT_PATH);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(Buffer, Source.Buffer, Source.Length);
    for (Index = 0; Index < Source.Length / sizeof(WCHAR); ++Index)
        Buffer[Index] = RtlUpcaseUnicodeChar(Buffer[Index]);
    Buffer[Source.Length / sizeof(WCHAR)] = UNICODE_NULL;

    ExFatFreeUnicodeString(&Ccb->SearchPattern);
    Ccb->SearchPattern.Buffer = Buffer;
    Ccb->SearchPattern.Length = Source.Length;
    Ccb->SearchPattern.MaximumLength = Source.Length + sizeof(WCHAR);
    return STATUS_SUCCESS;
}

static ULONG
ExFatDirectoryEntrySize(
    FILE_INFORMATION_CLASS InformationClass,
    ULONG NameLength)
{
    switch (InformationClass)
    {
        case FileDirectoryInformation:
            return FIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName) + NameLength;
        case FileFullDirectoryInformation:
            return FIELD_OFFSET(FILE_FULL_DIR_INFORMATION, FileName) + NameLength;
        case FileBothDirectoryInformation:
            return FIELD_OFFSET(FILE_BOTH_DIR_INFORMATION, FileName) + NameLength;
        case FileNamesInformation:
            return FIELD_OFFSET(FILE_NAMES_INFORMATION, FileName) + NameLength;
        case FileIdBothDirectoryInformation:
            return FIELD_OFFSET(FILE_ID_BOTH_DIR_INFORMATION, FileName) + NameLength;
        case FileIdFullDirectoryInformation:
            return FIELD_OFFSET(FILE_ID_FULL_DIR_INFORMATION, FileName) + NameLength;
        default:
            return 0;
    }
}

static VOID
ExFatFillDirectoryEntry(
    FILE_INFORMATION_CLASS InformationClass,
    PVOID Buffer,
    ULONG FileIndex,
    FILINFO* FatInformation,
    PUNICODE_STRING Name,
    ULONG ClusterSize)
{
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER WriteTime;
    LARGE_INTEGER FileSize;
    LARGE_INTEGER AllocationSize;
    ULONG Attributes;
    ULONGLONG FileId;

    CreationTime = ExFatFatTimeToSystemTime(FatInformation->crdate,
                                             FatInformation->crtime);
    WriteTime = ExFatFatTimeToSystemTime(FatInformation->fdate,
                                          FatInformation->ftime);
    FileSize.QuadPart = (FatInformation->fattrib & AM_DIR) ? 0 : FatInformation->fsize;
    AllocationSize.QuadPart = (FatInformation->fattrib & AM_DIR) ? 0 :
                              ExFatRoundUp(FatInformation->fsize, ClusterSize);
    Attributes = ExFatFatAttributesToNt(FatInformation->fattrib);
    FileId = 1469598103934665603ULL;
    for (ULONG Index = 0; Index < Name->Length / sizeof(WCHAR); ++Index)
    {
        FileId ^= RtlUpcaseUnicodeChar(Name->Buffer[Index]);
        FileId *= 1099511628211ULL;
    }

#define FILL_COMMON(Entry) \
    do { \
        (Entry)->NextEntryOffset = 0; \
        (Entry)->FileIndex = FileIndex; \
        (Entry)->CreationTime = CreationTime; \
        (Entry)->LastAccessTime = WriteTime; \
        (Entry)->LastWriteTime = WriteTime; \
        (Entry)->ChangeTime = WriteTime; \
        (Entry)->EndOfFile = FileSize; \
        (Entry)->AllocationSize = AllocationSize; \
        (Entry)->FileAttributes = Attributes; \
        (Entry)->FileNameLength = Name->Length; \
        RtlCopyMemory((Entry)->FileName, Name->Buffer, Name->Length); \
    } while (0)

    switch (InformationClass)
    {
        case FileDirectoryInformation:
            FILL_COMMON((PFILE_DIRECTORY_INFORMATION)Buffer);
            break;
        case FileFullDirectoryInformation:
            FILL_COMMON((PFILE_FULL_DIR_INFORMATION)Buffer);
            ((PFILE_FULL_DIR_INFORMATION)Buffer)->EaSize = 0;
            break;
        case FileBothDirectoryInformation:
            FILL_COMMON((PFILE_BOTH_DIR_INFORMATION)Buffer);
            ((PFILE_BOTH_DIR_INFORMATION)Buffer)->EaSize = 0;
            ((PFILE_BOTH_DIR_INFORMATION)Buffer)->ShortNameLength = 0;
            RtlZeroMemory(((PFILE_BOTH_DIR_INFORMATION)Buffer)->ShortName,
                          sizeof(((PFILE_BOTH_DIR_INFORMATION)Buffer)->ShortName));
            break;
        case FileNamesInformation:
        {
            PFILE_NAMES_INFORMATION Entry = Buffer;
            Entry->NextEntryOffset = 0;
            Entry->FileIndex = FileIndex;
            Entry->FileNameLength = Name->Length;
            RtlCopyMemory(Entry->FileName, Name->Buffer, Name->Length);
            break;
        }
        case FileIdBothDirectoryInformation:
            FILL_COMMON((PFILE_ID_BOTH_DIR_INFORMATION)Buffer);
            ((PFILE_ID_BOTH_DIR_INFORMATION)Buffer)->EaSize = 0;
            ((PFILE_ID_BOTH_DIR_INFORMATION)Buffer)->ShortNameLength = 0;
            RtlZeroMemory(((PFILE_ID_BOTH_DIR_INFORMATION)Buffer)->ShortName,
                          sizeof(((PFILE_ID_BOTH_DIR_INFORMATION)Buffer)->ShortName));
            ((PFILE_ID_BOTH_DIR_INFORMATION)Buffer)->FileId.QuadPart = FileId;
            break;
        case FileIdFullDirectoryInformation:
            FILL_COMMON((PFILE_ID_FULL_DIR_INFORMATION)Buffer);
            ((PFILE_ID_FULL_DIR_INFORMATION)Buffer)->EaSize = 0;
            ((PFILE_ID_FULL_DIR_INFORMATION)Buffer)->FileId.QuadPart = FileId;
            break;
        default:
            break;
    }

#undef FILL_COMMON
}

static NTSTATUS
ExFatQueryDirectory(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PEXFAT_VCB Vcb = DeviceObject->DeviceExtension;
    PEXFAT_FCB Fcb;
    PEXFAT_CCB Ccb;
    FILE_INFORMATION_CLASS InformationClass;
    ULONG BufferLength;
    PUCHAR Buffer;
    ULONG Offset = 0;
    PULONG PreviousNextOffset = NULL;
    FILINFO FatInformation;
    DIR SavedDirectory;
    ULONG SavedIndex;
    UNICODE_STRING Name;
    ULONG Required;
    ULONG AlignedRequired;
    ULONG ClusterSize;
    FRESULT Result;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN ReturnSingle;
    BOOLEAN Found = FALSE;

    if (!FileObject)
        return STATUS_INVALID_PARAMETER;
    Fcb = FileObject->FsContext;
    Ccb = FileObject->FsContext2;
    if (!Fcb || !Ccb || !Fcb->IsDirectory || !Ccb->IsDirectory || !Ccb->HandleOpen)
        return STATUS_NOT_A_DIRECTORY;

    InformationClass = Stack->Parameters.QueryDirectory.FileInformationClass;
    if (!ExFatDirectoryEntrySize(InformationClass, 0))
        return STATUS_INVALID_INFO_CLASS;

    BufferLength = Stack->Parameters.QueryDirectory.Length;
    Status = ExFatLockUserBuffer(Irp, BufferLength, IoWriteAccess);
    if (!NT_SUCCESS(Status))
        return Status;
    Buffer = ExFatGetUserBuffer(Irp, FALSE);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Buffer, BufferLength);

    ExAcquireResourceSharedLite(&Fcb->MainResource, TRUE);
    ExFatAcquireFatFs(Fcb->Vcb);

    if ((Stack->Flags & SL_RESTART_SCAN) || !Ccb->SearchPattern.Buffer)
    {
        Status = ExFatSetSearchPattern(Ccb,
                                       Stack->Parameters.QueryDirectory.FileName);
        if (!NT_SUCCESS(Status))
            goto Done;
        Result = f_rewinddir(&Ccb->Handle.Directory);
        if (Result != FR_OK)
        {
            Status = ExFatMapResult(Result);
            goto Done;
        }
        Ccb->DirectoryIndex = 0;
    }

    ReturnSingle = !!(Stack->Flags & SL_RETURN_SINGLE_ENTRY);
    ClusterSize = Vcb->FileSystem.csize * Vcb->BytesPerSector;
    for (;;)
    {
        SavedDirectory = Ccb->Handle.Directory;
        SavedIndex = Ccb->DirectoryIndex;
        RtlZeroMemory(&FatInformation, sizeof(FatInformation));
        Result = f_readdir(&Ccb->Handle.Directory, &FatInformation);
        if (Result != FR_OK)
        {
            Status = ExFatMapResult(Result);
            break;
        }
        if (!FatInformation.fname[0])
            break;
        Ccb->DirectoryIndex++;

        Status = ExFatUtf8ToUnicode(FatInformation.fname, &Name);
        if (!NT_SUCCESS(Status))
            break;
        if (!FsRtlIsNameInExpression(&Ccb->SearchPattern, &Name, TRUE, NULL))
        {
            ExFatFreeUnicodeString(&Name);
            continue;
        }

        Required = ExFatDirectoryEntrySize(InformationClass, Name.Length);
        AlignedRequired = EXFAT_ALIGN_DIRECTORY_ENTRY(Required);
        if (Required > BufferLength - Offset)
        {
            Ccb->Handle.Directory = SavedDirectory;
            Ccb->DirectoryIndex = SavedIndex;
            ExFatFreeUnicodeString(&Name);
            Status = Found ? STATUS_SUCCESS : STATUS_BUFFER_OVERFLOW;
            break;
        }

        if (PreviousNextOffset)
            *PreviousNextOffset = Offset - ((PUCHAR)PreviousNextOffset - Buffer);
        PreviousNextOffset = (PULONG)(Buffer + Offset);
        ExFatFillDirectoryEntry(InformationClass,
                                Buffer + Offset,
                                Ccb->DirectoryIndex,
                                &FatInformation,
                                &Name,
                                ClusterSize);
        ExFatFreeUnicodeString(&Name);
        Found = TRUE;
        Offset += min(AlignedRequired, BufferLength - Offset);
        if (ReturnSingle)
            break;
    }

    if (!Found && NT_SUCCESS(Status))
        Status = Ccb->DirectoryIndex ? STATUS_NO_MORE_FILES : STATUS_NO_SUCH_FILE;

Done:
    ExFatReleaseFatFs(Fcb->Vcb);
    ExReleaseResourceLite(&Fcb->MainResource);
    Irp->IoStatus.Information = Offset;
    return Status;
}

NTSTATUS
ExFatDirectoryControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);

    if (DeviceObject == ExFatGlobalData->DeviceObject)
        return STATUS_INVALID_DEVICE_REQUEST;
    if (Stack->MinorFunction == IRP_MN_QUERY_DIRECTORY)
        return ExFatQueryDirectory(DeviceObject, Irp);
    if (Stack->MinorFunction == IRP_MN_NOTIFY_CHANGE_DIRECTORY)
        return STATUS_NOT_SUPPORTED;
    return STATUS_INVALID_DEVICE_REQUEST;
}
