/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS-3G file create and open handling
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfspch.h"

static GENERIC_MAPPING NtfsFileGenericMapping = {
    FILE_GENERIC_READ,
    FILE_GENERIC_WRITE,
    FILE_GENERIC_EXECUTE,
    FILE_ALL_ACCESS
};

/* Case-insensitive FNV-1a over the path, matching how lookups compare names. */
static ULONGLONG
NtfsHashName(_In_ PCUNICODE_STRING Name)
{
    ULONGLONG Hash = 1469598103934665603ULL;
    ULONG Index;

    for (Index = 0; Index < Name->Length / sizeof(WCHAR); Index++) {
        WCHAR Character = Name->Buffer[Index];

        if (Character >= L'a' && Character <= L'z')
            Character = Character - (L'a' - L'A');
        else if (Character > 0x7F)
            Character = RtlUpcaseUnicodeChar(Character);
        Hash ^= Character;
        Hash *= 1099511628211ULL;
    }
    return Hash;
}

/* Caller holds FcbListResource. */
static BOOLEAN
NtfsLookupNegative(_In_ PVolumeContextBlock Volume,
                   _In_ PCUNICODE_STRING Name,
                   _In_ ULONGLONG Hash)
{
    NtfsNegativeEntry *Entry;
    UNICODE_STRING Cached;

    if (Name->Length > NTFS_NEGATIVE_NAME_MAX * sizeof(WCHAR))
        return FALSE;

    Entry = &Volume->NegativeCache[Hash % NTFS_NEGATIVE_CACHE_SIZE];
    if (Entry->Generation != Volume->NamespaceGeneration ||
        Entry->Hash != Hash || Entry->Length != Name->Length)
        return FALSE;

    Cached.Buffer = Entry->Name;
    Cached.Length = Entry->Length;
    Cached.MaximumLength = Entry->Length;
    return RtlEqualUnicodeString(&Cached, Name, TRUE);
}

/* Caller holds FcbListResource. Returns the known MFT record, or 0. */
static ULONGLONG
NtfsLookupPath(_In_ PVolumeContextBlock Volume,
               _In_ PCUNICODE_STRING Name,
               _In_ ULONGLONG Hash)
{
    NtfsPathEntry *Entry;
    UNICODE_STRING Cached;

    if (Name->Length > NTFS_NEGATIVE_NAME_MAX * sizeof(WCHAR))
        return 0;

    Entry = &Volume->PathCache[Hash % NTFS_PATH_CACHE_SIZE];
    if (Entry->Generation != Volume->NamespaceGeneration ||
        Entry->Hash != Hash || Entry->Length != Name->Length || !Entry->FileId)
        return 0;

    Cached.Buffer = Entry->Name;
    Cached.Length = Entry->Length;
    Cached.MaximumLength = Entry->Length;
    return RtlEqualUnicodeString(&Cached, Name, TRUE) ? Entry->FileId : 0;
}

/* Caller holds FcbListResource. */
static VOID
NtfsRememberPath(_In_ PVolumeContextBlock Volume,
                 _In_ PCUNICODE_STRING Name,
                 _In_ ULONGLONG Hash,
                 _In_ ULONGLONG FileId)
{
    NtfsPathEntry *Entry;

    if (!Name->Length || !FileId ||
        Name->Length > NTFS_NEGATIVE_NAME_MAX * sizeof(WCHAR))
        return;

    Entry = &Volume->PathCache[Hash % NTFS_PATH_CACHE_SIZE];
    RtlCopyMemory(Entry->Name, Name->Buffer, Name->Length);
    Entry->Length = Name->Length;
    Entry->Hash = Hash;
    Entry->FileId = FileId;
    Entry->Generation = Volume->NamespaceGeneration;
}

/* Caller holds FcbListResource. */
static VOID
NtfsRememberNegative(_In_ PVolumeContextBlock Volume,
                     _In_ PCUNICODE_STRING Name,
                     _In_ ULONGLONG Hash)
{
    NtfsNegativeEntry *Entry;

    if (!Name->Length || Name->Length > NTFS_NEGATIVE_NAME_MAX * sizeof(WCHAR))
        return;

    Entry = &Volume->NegativeCache[Hash % NTFS_NEGATIVE_CACHE_SIZE];
    RtlCopyMemory(Entry->Name, Name->Buffer, Name->Length);
    Entry->Length = Name->Length;
    Entry->Hash = Hash;
    Entry->Generation = Volume->NamespaceGeneration;
}

static PFileContextBlock
NtfsFindFcbLocked(_In_ PVolumeContextBlock Volume,
                  _In_ const NTFS3G_ROS_FILE_INFORMATION *Information,
                  _In_ PCUNICODE_STRING FileName)
{
    PLIST_ENTRY Entry;
    PFileContextBlock File;

    for (Entry = Volume->FcbListHead.Flink;
         Entry != &Volume->FcbListHead;
         Entry = Entry->Flink) {
        File = CONTAINING_RECORD(Entry, FileContextBlock, ListEntry);
        if (File->Information.FileId == Information->FileId ||
            RtlEqualUnicodeString(&File->FileName, FileName, TRUE))
            return File;
    }
    return NULL;
}

static PFileContextBlock
NtfsAllocateFcbLocked(
    _In_ PVolumeContextBlock Volume,
    _Inout_ PUNICODE_STRING FileName,
    _In_ const NTFS3G_ROS_FILE_INFORMATION *Information,
    _In_opt_ NTFS3G_ROS_FILE *CoreFile)
{
    PFileContextBlock File;
    NTSTATUS Status;

    File = ExAllocatePoolZero(NonPagedPool, sizeof(*File), TAG_NTFS);
    if (!File)
        return NULL;

    FsRtlInitializeFileLock(&File->FileLock, NULL, NULL);
    Status = ExInitializeResourceLite(&File->MainResource);
    if (!NT_SUCCESS(Status)) {
        FsRtlUninitializeFileLock(&File->FileLock);
        ExFreePoolWithTag(File, TAG_NTFS);
        return NULL;
    }
    Status = ExInitializeResourceLite(&File->PagingIoResource);
    if (!NT_SUCCESS(Status)) {
        ExDeleteResourceLite(&File->MainResource);
        FsRtlUninitializeFileLock(&File->FileLock);
        ExFreePoolWithTag(File, TAG_NTFS);
        return NULL;
    }

    ExInitializeFastMutex(&File->HeaderMutex);
    FsRtlSetupAdvancedHeader(&File->CommonFCBHeader, &File->HeaderMutex);
    File->CommonFCBHeader.NodeTypeCode = NTFS3G_FCB_NODE_TYPE;
    File->CommonFCBHeader.NodeByteSize = sizeof(*File);
    File->CommonFCBHeader.Resource = &File->MainResource;
    File->CommonFCBHeader.PagingIoResource = &File->PagingIoResource;
    File->CommonFCBHeader.IsFastIoPossible = FastIoIsQuestionable;
    File->CommonFCBHeader.AllocationSize.QuadPart =
        Information->AllocationSize;
    File->CommonFCBHeader.FileSize.QuadPart = Information->FileSize;
    File->CommonFCBHeader.ValidDataLength.QuadPart =
        Information->FileSize;
    File->Volume = Volume;
    File->ReferenceCount = 1;
    File->File = CoreFile;
    File->Information = *Information;
    File->FileName = *FileName;
    RtlZeroMemory(FileName, sizeof(*FileName));
    InsertTailList(&Volume->FcbListHead, &File->ListEntry);
    return File;
}

static NTSTATUS
NtfsBuildFileName(_In_ PFILE_OBJECT FileObject,
                  _Out_ PUNICODE_STRING FileName)
{
    PFileContextBlock Related = NULL;
    UNICODE_STRING Suffix = FileObject->FileName;
    ULONG Length;
    BOOLEAN AddSeparator = FALSE;

    RtlZeroMemory(FileName, sizeof(*FileName));
    if (FileObject->RelatedFileObject &&
        (!Suffix.Length || Suffix.Buffer[0] != L'\\')) {
        Related = FileObject->RelatedFileObject->FsContext;
        if (!Related)
            return STATUS_INVALID_PARAMETER;
        if (Related->FileName.Length && Suffix.Length &&
            Related->FileName.Buffer[Related->FileName.Length / sizeof(WCHAR) - 1] != L'\\')
            AddSeparator = TRUE;
    }

    Length = Suffix.Length + (Related ? Related->FileName.Length : 0) +
             (AddSeparator ? sizeof(WCHAR) : 0);
    if (Length > MAXUSHORT - sizeof(WCHAR))
        return STATUS_NAME_TOO_LONG;

    FileName->Buffer = ExAllocatePoolWithTag(PagedPool,
                                              Length + sizeof(WCHAR),
                                              TAG_NTFS);
    if (!FileName->Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    FileName->Length = 0;
    FileName->MaximumLength = (USHORT)(Length + sizeof(WCHAR));
    if (Related) {
        RtlCopyMemory(FileName->Buffer,
                      Related->FileName.Buffer,
                      Related->FileName.Length);
        FileName->Length = Related->FileName.Length;
    }
    if (AddSeparator) {
        FileName->Buffer[FileName->Length / sizeof(WCHAR)] = L'\\';
        FileName->Length += sizeof(WCHAR);
    }
    if (Suffix.Length) {
        RtlCopyMemory((PUCHAR)FileName->Buffer + FileName->Length,
                      Suffix.Buffer,
                      Suffix.Length);
        FileName->Length += Suffix.Length;
    }
    FileName->Buffer[FileName->Length / sizeof(WCHAR)] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

static NTSTATUS
NtfsOpenVolume(_In_ PDEVICE_OBJECT DeviceObject,
               _Inout_ PIRP Irp,
               _Inout_ PVolumeContextBlock Volume,
               _Inout_ PFILE_OBJECT FileObject,
               _In_ ACCESS_MASK DesiredAccess,
               _In_ ULONG ShareAccess,
               _In_ UCHAR Disposition,
               _In_ ULONG CreateOptions)
{
    PFileContextBlock File = NULL;
    PHandleContextBlock Handle;
    NTFS3G_ROS_FILE_INFORMATION Information;
    UNICODE_STRING EmptyName;
    BOOLEAN OpenCounted = FALSE;
    NTSTATUS Status;

    if (Disposition != FILE_OPEN && Disposition != FILE_OPEN_IF)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
    if (CreateOptions & (FILE_DIRECTORY_FILE |
                         FILE_DELETE_ON_CLOSE))
        return NtfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
    Handle = ExAllocatePoolZero(
        NonPagedPool, sizeof(*Handle), TAG_NTFS);
    if (!Handle)
        return NtfsCompleteRequest(
            Irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    Handle->FileObject = FileObject;
    Handle->DesiredAccess = DesiredAccess;
    Handle->CreateOptions = CreateOptions;
    RtlZeroMemory(&Information, sizeof(Information));
    RtlZeroMemory(&EmptyName, sizeof(EmptyName));
    Information.FileId = MAXULONGLONG;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(
        &Volume->FcbListResource, TRUE);
    if (Volume->ShutdownStarted) {
        Status = STATUS_SYSTEM_SHUTDOWN;
        goto Failure;
    }
    if (Volume->Dismounted || !Volume->Volume) {
        Status = STATUS_VOLUME_DISMOUNTED;
        goto Failure;
    }
    if (Volume->LockFileObject &&
        Volume->LockFileObject != FileObject) {
        Status = STATUS_ACCESS_DENIED;
        goto Failure;
    }
    if (Ntfs3gRosIsReadOnly(Volume->Volume) &&
        NtfsIsWriteAccess(DesiredAccess)) {
        Status = STATUS_MEDIA_WRITE_PROTECTED;
        goto Failure;
    }

    File = Volume->VolumeFcb;
    if (File) {
        NtfsReferenceFcb(File);
    } else {
        File = NtfsAllocateFcbLocked(
            Volume, &EmptyName, &Information, NULL);
        if (!File) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Failure;
        }
        File->IsVolume = TRUE;
        Volume->VolumeFcb = File;
    }

    if (File->OpenHandleCount) {
        Status = IoCheckShareAccess(DesiredAccess,
                                    ShareAccess,
                                    FileObject,
                                    &File->ShareAccess,
                                    TRUE);
        if (!NT_SUCCESS(Status))
            goto Failure;
    } else {
        IoSetShareAccess(DesiredAccess,
                         ShareAccess,
                         FileObject,
                         &File->ShareAccess);
    }
    Handle->ShareAccessSet = TRUE;
    File->OpenHandleCount++;
    OpenCounted = TRUE;

    FileObject->FsContext = File;
    FileObject->FsContext2 = Handle;
    FileObject->SectionObjectPointer =
        &File->SectionObjectPointers;
    FileObject->Vpb = DeviceObject->Vpb;
    FileObject->Flags |= FO_NO_INTERMEDIATE_BUFFERING;
    ExReleaseResourceLite(&Volume->FcbListResource);
    KeLeaveCriticalRegion();
    return NtfsCompleteRequest(Irp, STATUS_SUCCESS, FILE_OPENED);

Failure:
    if (OpenCounted && File->OpenHandleCount)
        File->OpenHandleCount--;
    if (Handle->ShareAccessSet && File) {
        IoRemoveShareAccess(FileObject, &File->ShareAccess);
        Handle->ShareAccessSet = FALSE;
    }
    if (File)
        NtfsDereferenceFcb(File);
    ExReleaseResourceLite(&Volume->FcbListResource);
    KeLeaveCriticalRegion();
    ExFreePoolWithTag(Handle, TAG_NTFS);
    return NtfsCompleteRequest(Irp, Status, 0);
}

NTSTATUS
NTAPI
NtfsFsdCreate(_In_ PDEVICE_OBJECT DeviceObject,
              _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PVolumeContextBlock Volume;
    PFileContextBlock File = NULL;
    PHandleContextBlock Handle = NULL;
    NTFS3G_ROS_FILE *CoreFile = NULL;
    NTFS3G_ROS_FILE_INFORMATION Information;
    UNICODE_STRING FileName;
    ULONGLONG NameHashValue;
    ULONGLONG KnownFileId;
    ACCESS_MASK DesiredAccess;
    ULONG ShareAccess;
    ULONG CreateOptions;
    UCHAR Disposition;
    ULONG CreateResult = FILE_OPENED;
    BOOLEAN OpenTargetDirectory;
    BOOLEAN OpenHandleCounted = FALSE;
    NTSTATUS Status;
    int Result;

    if (DeviceObject == NtfsDiskFileSystemDeviceObject)
        return NtfsCompleteRequest(Irp, STATUS_SUCCESS, FILE_OPENED);
    if (!FileObject || !IrpSp->Parameters.Create.SecurityContext)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);

    Volume = DeviceObject->DeviceExtension;
    CreateOptions = GetCreateOptions(IrpSp->Parameters.Create.Options);
    Disposition = GetDisposition(IrpSp->Parameters.Create.Options);
    OpenTargetDirectory =
        BooleanFlagOn(IrpSp->Flags, SL_OPEN_TARGET_DIRECTORY);
    DesiredAccess =
        IrpSp->Parameters.Create.SecurityContext->DesiredAccess;
    if (IrpSp->Parameters.Create.SecurityContext->AccessState) {
        DesiredAccess |=
            IrpSp->Parameters.Create.SecurityContext->AccessState->
                PreviouslyGrantedAccess;
    }
    RtlMapGenericMask(
        &DesiredAccess, &NtfsFileGenericMapping);
    ShareAccess = IrpSp->Parameters.Create.ShareAccess;

    if (CreateOptions & FILE_OPEN_BY_FILE_ID)
        return NtfsCompleteRequest(Irp, STATUS_NOT_SUPPORTED, 0);
    if ((CreateOptions & FILE_DELETE_ON_CLOSE) &&
        !(DesiredAccess & DELETE))
        return NtfsCompleteRequest(Irp, STATUS_ACCESS_DENIED, 0);
    if (!FileObject->FileName.Length &&
        !FileObject->RelatedFileObject)
        return NtfsOpenVolume(DeviceObject,
                              Irp,
                              Volume,
                              FileObject,
                              DesiredAccess,
                              ShareAccess,
                              Disposition,
                              CreateOptions);

    Status = NtfsBuildFileName(FileObject, &FileName);
    if (!NT_SUCCESS(Status))
        return NtfsCompleteRequest(Irp, Status, 0);

    if (OpenTargetDirectory) {
        USHORT CharacterCount = FileName.Length / sizeof(WCHAR);
        USHORT Separator;
        USHORT TargetCharacters;

        if (!CharacterCount) {
            ExFreePoolWithTag(FileName.Buffer, TAG_NTFS);
            return NtfsCompleteRequest(
                Irp, STATUS_OBJECT_NAME_INVALID, 0);
        }
        Separator = CharacterCount;
        while (Separator &&
               FileName.Buffer[Separator - 1] != L'\\' &&
               FileName.Buffer[Separator - 1] != L'/')
            Separator--;
        if (!Separator || Separator == CharacterCount) {
            ExFreePoolWithTag(FileName.Buffer, TAG_NTFS);
            return NtfsCompleteRequest(
                Irp, STATUS_OBJECT_NAME_INVALID, 0);
        }

        /*
         * FileRenameInformation performs its own collision check.  Avoid a
         * second inode/parent view here immediately before the directory
         * entry is linked.
         */
        CreateResult = FILE_DOES_NOT_EXIST;

        TargetCharacters = CharacterCount - Separator;
        FileObject->FileName.Length =
            TargetCharacters * sizeof(WCHAR);
        RtlMoveMemory(FileObject->FileName.Buffer,
                      FileName.Buffer + Separator,
                      FileObject->FileName.Length);
        if (FileObject->FileName.MaximumLength >
            FileObject->FileName.Length) {
            FileObject->FileName.Buffer[TargetCharacters] =
                UNICODE_NULL;
        }

        if (Separator == 1)
            FileName.Length = sizeof(WCHAR);
        else
            FileName.Length = (Separator - 1) * sizeof(WCHAR);
        FileName.Buffer[
            FileName.Length / sizeof(WCHAR)] = UNICODE_NULL;
    }

    Handle = ExAllocatePoolZero(NonPagedPool, sizeof(*Handle), TAG_NTFS);
    if (!Handle) {
        ExFreePoolWithTag(FileName.Buffer, TAG_NTFS);
        return NtfsCompleteRequest(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    }
    Handle->FileObject = FileObject;
    Handle->DesiredAccess = DesiredAccess;
    Handle->CreateOptions = CreateOptions;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Volume->FcbListResource, TRUE);
    if (Volume->ShutdownStarted) {
        Status = STATUS_SYSTEM_SHUTDOWN;
        goto FailureLocked;
    }
    if (Volume->Dismounted || !Volume->Volume) {
        Status = STATUS_VOLUME_DISMOUNTED;
        goto FailureLocked;
    }
    if (Volume->LockFileObject &&
        Volume->LockFileObject != FileObject) {
        Status = STATUS_ACCESS_DENIED;
        goto FailureLocked;
    }
    if (Ntfs3gRosIsReadOnly(Volume->Volume) &&
        (NtfsIsWriteAccess(DesiredAccess) ||
         Disposition == FILE_CREATE ||
         Disposition == FILE_OPEN_IF ||
         Disposition == FILE_OVERWRITE ||
         Disposition == FILE_OVERWRITE_IF ||
         Disposition == FILE_SUPERSEDE ||
         (CreateOptions & FILE_DELETE_ON_CLOSE))) {
        Status = STATUS_MEDIA_WRITE_PROTECTED;
        goto FailureLocked;
    }
    NameHashValue = NtfsHashName(&FileName);
    KnownFileId = NtfsLookupPath(Volume, &FileName, NameHashValue);
    if (NtfsLookupNegative(Volume, &FileName, NameHashValue)) {
        Status = STATUS_OBJECT_NAME_NOT_FOUND;   /* proved absent already */
    } else if (KnownFileId) {
        /* the record is known, so the path does not have to be walked again */
        Status = Ntfs3gRosOpenUnicodeFileById(Volume->Volume, &FileName,
                                              KnownFileId, &CoreFile);
        if (!NT_SUCCESS(Status)) {
            Status = Ntfs3gRosOpenUnicodeFile(Volume->Volume, &FileName, &CoreFile);
        }
    } else {
        Status = Ntfs3gRosOpenUnicodeFile(Volume->Volume, &FileName, &CoreFile);
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND ||
            Status == STATUS_OBJECT_PATH_NOT_FOUND) {
            NtfsRememberNegative(Volume, &FileName, NameHashValue);
        }
    }
    if (!NT_SUCCESS(Status)) {
        if ((Status == STATUS_OBJECT_NAME_NOT_FOUND ||
             Status == STATUS_OBJECT_PATH_NOT_FOUND) &&
            (Disposition == FILE_CREATE || Disposition == FILE_OPEN_IF ||
             Disposition == FILE_OVERWRITE_IF ||
             Disposition == FILE_SUPERSEDE)) {
            if (CreateOptions & FILE_DIRECTORY_FILE) {
                Result = Ntfs3gRosCreateDirectoryUtf16(
                    Volume->Volume,
                    (const uint16_t *)FileName.Buffer,
                    FileName.Length / sizeof(WCHAR));
                if (!Result) {
                    Status = Ntfs3gRosOpenUnicodeFile(
                        Volume->Volume, &FileName, &CoreFile);
                } else {
                    Status = Ntfs3gRosStatusFromError(-Result);
                }
            } else {
                Result = Ntfs3gRosCreateFileUtf16(
                    Volume->Volume,
                    (const uint16_t *)FileName.Buffer,
                    FileName.Length / sizeof(WCHAR),
                    FALSE,
                    &CoreFile);
                Status = Result < 0 ?
                    Ntfs3gRosStatusFromError(-Result) : STATUS_SUCCESS;
            }
            if (NT_SUCCESS(Status)) {
                CreateResult = FILE_CREATED;
                NtfsInvalidateNamespace(Volume);
            }
        }
        if (!NT_SUCCESS(Status))
            goto FailureLocked;
    }

    if (CreateResult != FILE_CREATED && Disposition == FILE_CREATE) {
        Status = STATUS_OBJECT_NAME_COLLISION;
        goto FailureLocked;
    }
    Result = Ntfs3gRosGetFileInformation(CoreFile, &Information);
    if (Result < 0) {
        Status = Ntfs3gRosStatusFromError(-Result);
        goto FailureLocked;
    }
    if ((CreateOptions & FILE_DIRECTORY_FILE) &&
        !(Information.Attributes & NTFS3G_ROS_FILE_DIRECTORY)) {
        Status = STATUS_NOT_A_DIRECTORY;
        goto FailureLocked;
    }
    if ((CreateOptions & FILE_NON_DIRECTORY_FILE) &&
        (Information.Attributes & NTFS3G_ROS_FILE_DIRECTORY)) {
        Status = STATUS_FILE_IS_A_DIRECTORY;
        goto FailureLocked;
    }

    NtfsRememberPath(Volume, &FileName, NameHashValue, Information.FileId);
    File = NtfsFindFcbLocked(Volume, &Information, &FileName);
    if (File) {
        NtfsReferenceFcb(File);
        if (File->DeletePending) {
            Status = STATUS_DELETE_PENDING;
            goto FailureLocked;
        }
        if (!(Information.Attributes & NTFS3G_ROS_FILE_DIRECTORY) &&
            !File->File) {
            File->File = CoreFile;
            CoreFile = NULL;
        }
    } else {
        File = NtfsAllocateFcbLocked(
            Volume,
            &FileName,
            &Information,
            (Information.Attributes & NTFS3G_ROS_FILE_DIRECTORY) ?
                NULL : CoreFile);
        if (!File) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto FailureLocked;
        }
        if (!(Information.Attributes & NTFS3G_ROS_FILE_DIRECTORY))
            CoreFile = NULL;
    }

    if (File->OpenHandleCount) {
        Status = IoCheckShareAccess(DesiredAccess,
                                    ShareAccess,
                                    FileObject,
                                    &File->ShareAccess,
                                    TRUE);
        if (!NT_SUCCESS(Status))
            goto FailureLocked;
    } else {
        IoSetShareAccess(DesiredAccess,
                         ShareAccess,
                         FileObject,
                         &File->ShareAccess);
    }
    Handle->ShareAccessSet = TRUE;
    File->OpenHandleCount++;
    OpenHandleCounted = TRUE;

    if (CreateResult != FILE_CREATED &&
        (Disposition == FILE_SUPERSEDE || Disposition == FILE_OVERWRITE ||
         Disposition == FILE_OVERWRITE_IF)) {
        LARGE_INTEGER ZeroSize;

        if (File->Information.Attributes & NTFS3G_ROS_FILE_DIRECTORY) {
            Status = STATUS_FILE_IS_A_DIRECTORY;
            goto FailureLocked;
        }
        ZeroSize.QuadPart = 0;
        FileObject->SectionObjectPointer = &File->SectionObjectPointers;
        ExReleaseResourceLite(&Volume->FcbListResource);
        KeLeaveCriticalRegion();
        Status = NtfsResizeFile(FileObject, File, &ZeroSize, FALSE);
        KeEnterCriticalRegion();
        ExAcquireResourceExclusiveLite(&Volume->FcbListResource, TRUE);
        if (!NT_SUCCESS(Status))
            goto FailureLocked;
        CreateResult = Disposition == FILE_SUPERSEDE ?
            FILE_SUPERSEDED : FILE_OVERWRITTEN;
    }

    if (File->Information.Attributes & NTFS3G_ROS_FILE_DIRECTORY) {
        Handle->DirectoryFile = CoreFile;
        CoreFile = NULL;
    }
    if ((CreateResult == FILE_CREATED ||
         CreateResult == FILE_SUPERSEDED) &&
        IrpSp->Parameters.Create.EaLength) {
        PFILE_FULL_EA_INFORMATION EaBuffer =
            Irp->AssociatedIrp.SystemBuffer;
        NTFS3G_ROS_FILE *EaFile = File->File ?
            File->File : Handle->DirectoryFile;

        if (!EaBuffer || !EaFile) {
            Status = STATUS_INVALID_USER_BUFFER;
            goto FailureLocked;
        }
        Status = NtfsSetEaBuffer(
            FileObject,
            EaFile,
            EaBuffer,
            IrpSp->Parameters.Create.EaLength);
        if (!NT_SUCCESS(Status))
            goto FailureLocked;
    }
    if (CreateOptions & FILE_DELETE_ON_CLOSE) {
        File->DeletePending = TRUE;
        FileObject->DeletePending = TRUE;
    }

    FileObject->FsContext = File;
    FileObject->FsContext2 = Handle;
    FileObject->SectionObjectPointer = &File->SectionObjectPointers;
    FileObject->Vpb = DeviceObject->Vpb;
    if (!(File->Information.Attributes & NTFS3G_ROS_FILE_DIRECTORY)) {
        if (CreateOptions & FILE_NO_INTERMEDIATE_BUFFERING)
            FileObject->Flags |= FO_NO_INTERMEDIATE_BUFFERING;
        else
            FileObject->Flags |= FO_CACHE_SUPPORTED;
    }
    ExReleaseResourceLite(&Volume->FcbListResource);
    KeLeaveCriticalRegion();
    if (CoreFile)
        Ntfs3gRosCloseFile(CoreFile);
    if (FileName.Buffer)
        ExFreePoolWithTag(FileName.Buffer, TAG_NTFS);
    return NtfsCompleteRequest(Irp, STATUS_SUCCESS, CreateResult);

FailureLocked:
    FileObject->SectionObjectPointer = NULL;
    if (OpenHandleCounted && File->OpenHandleCount) {
        File->OpenHandleCount--;
        OpenHandleCounted = FALSE;
    }
    if (Handle->ShareAccessSet && File) {
        IoRemoveShareAccess(FileObject, &File->ShareAccess);
        Handle->ShareAccessSet = FALSE;
    }
    if (File)
        NtfsDereferenceFcb(File);
    ExReleaseResourceLite(&Volume->FcbListResource);
    KeLeaveCriticalRegion();
    if (CoreFile)
        Ntfs3gRosCloseFile(CoreFile);
    if (Handle->DirectoryFile)
        Ntfs3gRosCloseFile(Handle->DirectoryFile);
    if (FileName.Buffer)
        ExFreePoolWithTag(FileName.Buffer, TAG_NTFS);
    ExFreePoolWithTag(Handle, TAG_NTFS);
    return NtfsCompleteRequest(Irp, Status,
                               Status == STATUS_OBJECT_NAME_COLLISION ?
                               FILE_EXISTS :
                               (Status == STATUS_OBJECT_NAME_NOT_FOUND ||
                                Status == STATUS_OBJECT_PATH_NOT_FOUND ?
                                FILE_DOES_NOT_EXIST : 0));
}
