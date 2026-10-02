/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/config/cminit.c
 * PURPOSE:         Configuration Manager - Hive Initialization
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include "ntoskrnl.h"
#define NDEBUG
#include "debug.h"

/* FUNCTIONS *****************************************************************/

NTSTATUS
NTAPI
CmpInitializeHive(
    _Out_ PCMHIVE *CmHive,
    _In_ ULONG OperationType,
    _In_ ULONG HiveFlags,
    _In_ ULONG FileType,
    _In_opt_ PVOID HiveData,
    _In_ HANDLE Primary,
    _In_ HANDLE Log,
    _In_ HANDLE External,
    _In_ HANDLE Alternate,
    _In_opt_ PCUNICODE_STRING FileName,
    _In_ ULONG CheckFlags)
{
    PCMHIVE Hive;
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_FS_SIZE_INFORMATION FileSizeInformation;
    NTSTATUS Status;
    ULONG Cluster;

    /* Assume failure */
    *CmHive = NULL;

    /*
     * The following are invalid:
     * - An external hive that is also internal.
     * - A log hive that is not a primary hive too.
     * - A volatile hive that is linked to permanent storage,
     *   unless this hive is a shared system hive.
     * - An in-memory initialization without hive data.
     * - A log hive that is not linked to a correct file type.
     * - An alternate hive that is not linked to a correct file type.
     * - A lonely alternate hive not backed up with its corresponding primary hive.
     */
    if ((External && (Primary || Log)) ||
        (Log && !Primary) ||
        (!CmpShareSystemHives && (HiveFlags & HIVE_VOLATILE) &&
            (Primary || External || Log)) ||
        ((OperationType == HINIT_MEMORY) && !HiveData) ||
        (Log && (FileType != HFILE_TYPE_LOG)) ||
        (Alternate && (FileType != HFILE_TYPE_ALTERNATE)) ||
        (Alternate && !Primary))
    {
        /* Fail the request */
        return STATUS_INVALID_PARAMETER;
    }

    /* Check if this is a primary hive */
    if (Primary)
    {
        /* Get the cluster size */
        Status = ZwQueryVolumeInformationFile(Primary,
                                              &IoStatusBlock,
                                              &FileSizeInformation,
                                              sizeof(FILE_FS_SIZE_INFORMATION),
                                              FileFsSizeInformation);
        if (!NT_SUCCESS(Status)) return Status;

        /* Make sure it's not larger then the block size */
        if (FileSizeInformation.BytesPerSector > HBLOCK_SIZE)
        {
            /* Fail */
            return STATUS_REGISTRY_IO_FAILED;
        }

        /* Otherwise, calculate the cluster */
        Cluster = FileSizeInformation.BytesPerSector / HSECTOR_SIZE;
        Cluster = max(1, Cluster);
    }
    else
    {
        /* Otherwise use cluster 1 */
        Cluster = 1;
    }

    /* Allocate the hive */
    Hive = ExAllocatePoolWithTag(NonPagedPool, sizeof(CMHIVE), TAG_CMHIVE);
    if (!Hive) return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Hive, sizeof(*Hive));

    /* Set the current thread as creator */
    Hive->CreatorOwner = KeGetCurrentThread();

    /* Initialize lists */
    InitializeListHead(&Hive->TrustClassEntry);

    /* Setup the handles */
    Hive->FileHandles[HFILE_TYPE_PRIMARY] = Primary;
    Hive->FileHandles[HFILE_TYPE_LOG] = Log;
    Hive->FileHandles[HFILE_TYPE_EXTERNAL] = External;
    Hive->FileHandles[HFILE_TYPE_ALTERNATE] = Alternate;

    /* Setup hive locks */
    ExInitializePushLock((PEX_PUSH_LOCK)&Hive->HiveLock.Reserved);
    ExInitializePushLock(&Hive->SecurityLock);

    /* Clear file names */
    RtlInitEmptyUnicodeString(&Hive->FileUserName, NULL, 0);
    RtlInitEmptyUnicodeString(&Hive->FileFullPath, NULL, 0);

    /* Initialize the view list */
    CmpInitHiveViewList(Hive);

    /* Initailize the security cache */
    CmpInitSecurityCache(Hive);

    /* Setup flags */
    Hive->Flags = 0;

    /* Initialize it */
    Status = HvInitialize(&Hive->Hive,
                          OperationType,
                          HiveFlags,
                          FileType,
                          HiveData,
                          CmpAllocate,
                          CmpFree,
                          CmpFileWrite,
                          CmpFileRead,
                          Cluster,
                          FileName);
    if (!NT_SUCCESS(Status))
    {
        /* Cleanup allocations and fail */
        ExFreePoolWithTag(Hive, TAG_CMHIVE);
        return Status;
    }

    /* Check if we should verify the registry */
    if ((OperationType == HINIT_FILE) ||
        (OperationType == HINIT_MEMORY) ||
        (OperationType == HINIT_MEMORY_INPLACE) ||
        (OperationType == HINIT_MAPFILE))
    {
        /* Verify integrity */
        CM_CHECK_REGISTRY_STATUS CheckStatus = CmCheckRegistry(Hive, CheckFlags);
        if (!CM_CHECK_REGISTRY_SUCCESS(CheckStatus))
        {
            /* Cleanup allocations and fail */
            CmpDestroySecurityCache(Hive);
            CmpDestroyHiveViewList(Hive);
            HvFree(&Hive->Hive);
            ExFreePoolWithTag(Hive, TAG_CMHIVE);
            return STATUS_REGISTRY_CORRUPT;
        }
    }

    Hive->Hive.HiveFlags |= HiveFlags & HIVE_NOLAZYFLUSH;
    Hive->ReferenceCount = 1;

    /* Lock the hive list */
    ExAcquirePushLockExclusive(&CmpHiveListHeadLock);

    /* Insert this hive */
    InsertHeadList(&CmpHiveListHead, &Hive->HiveList);

    /* Release the lock */
    ExReleasePushLock(&CmpHiveListHeadLock);

    /* Return the hive and success */
    *CmHive = Hive;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CmpDestroyHive(IN PCMHIVE CmHive)
{
    /* Remove the hive from the list */
    ExAcquirePushLockExclusive(&CmpHiveListHeadLock);
    RemoveEntryList(&CmHive->HiveList);
    ExReleasePushLock(&CmpHiveListHeadLock);

    /* Destroy the security descriptor cache */
    CmpDestroySecurityCache(CmHive);

    /* Destroy the view list */
    CmpDestroyHiveViewList(CmHive);


    /* Free the hive storage */
    HvFree(&CmHive->Hive);

    /* Free the hive */
    CmpFree(CmHive, TAG_CM);

    return STATUS_SUCCESS;
}

static NTSTATUS
CmpGetAppHiveRootName(POBJECT_ATTRIBUTES TargetKey,
                      PUNICODE_STRING RootPath)
{
    POBJECT_NAME_INFORMATION Parent = NULL;
    UNICODE_STRING Path;
    ULONG Size, Length;
    NTSTATUS Status;

    if (!TargetKey->RootDirectory)
    {
        if (TargetKey->ObjectName->Buffer[0] != L'\\')
            return STATUS_OBJECT_PATH_SYNTAX_BAD;
        return RtlDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
                                         TargetKey->ObjectName, RootPath);
    }
    if (TargetKey->ObjectName->Buffer[0] == L'\\')
        return STATUS_OBJECT_PATH_SYNTAX_BAD;
    Status = ZwQueryObject(TargetKey->RootDirectory, ObjectNameInformation,
                            NULL, 0, &Size);
    if (Status != STATUS_INFO_LENGTH_MISMATCH && Status != STATUS_BUFFER_TOO_SMALL)
        return Status;
    Parent = ExAllocatePoolWithTag(PagedPool, Size, TAG_CM);
    if (!Parent) return STATUS_INSUFFICIENT_RESOURCES;
    Status = ZwQueryObject(TargetKey->RootDirectory, ObjectNameInformation,
                            Parent, Size, &Size);
    if (!NT_SUCCESS(Status)) goto Exit;
    Length = Parent->Name.Length + sizeof(WCHAR) + TargetKey->ObjectName->Length;
    if (Length > MAXUSHORT - sizeof(WCHAR))
    {
        Status = STATUS_NAME_TOO_LONG;
        goto Exit;
    }
    Path.Length = Length;
    Path.MaximumLength = Length + sizeof(WCHAR);
    Path.Buffer = ExAllocatePoolWithTag(PagedPool, Path.MaximumLength, TAG_CM);
    if (!Path.Buffer)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    RtlCopyMemory(Path.Buffer, Parent->Name.Buffer, Parent->Name.Length);
    Path.Buffer[Parent->Name.Length / sizeof(WCHAR)] = L'\\';
    RtlCopyMemory((PUCHAR)Path.Buffer + Parent->Name.Length + sizeof(WCHAR),
                  TargetKey->ObjectName->Buffer, TargetKey->ObjectName->Length);
    Path.Buffer[Path.Length / sizeof(WCHAR)] = UNICODE_NULL;
    Status = RtlDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
                                       &Path, RootPath);
    ExFreePoolWithTag(Path.Buffer, TAG_CM);
Exit:
    ExFreePoolWithTag(Parent, TAG_CM);
    return Status;
}

static NTSTATUS
CmpCheckAppHiveFileAccess(PFILE_OBJECT FileObject,
                          KPROCESSOR_MODE AccessMode)
{
    SECURITY_SUBJECT_CONTEXT Subject;
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    BOOLEAN Allocated = FALSE;
    ACCESS_MASK Granted;
    NTSTATUS Status;

    if (AccessMode == KernelMode) return STATUS_SUCCESS;
    Status = ObGetObjectSecurity(FileObject, &Descriptor, &Allocated);
    if (!NT_SUCCESS(Status)) return Status;
    if (!Descriptor)
    {
        ObReleaseObjectSecurity(Descriptor, Allocated);
        return STATUS_INVALID_SECURITY_DESCR;
    }
    SeCaptureSubjectContext(&Subject);
    SeLockSubjectContext(&Subject);
    SeAccessCheck(Descriptor, &Subject, TRUE,
                  FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE, 0,
                  NULL, IoGetFileObjectGenericMapping(), AccessMode,
                  &Granted, &Status);
    SeUnlockSubjectContext(&Subject);
    SeReleaseSubjectContext(&Subject);
    ObReleaseObjectSecurity(Descriptor, Allocated);
    return Status;
}

static NTSTATUS
CmpFindLoadedAppHive(HANDLE Probe,
                     KPROCESSOR_MODE AccessMode,
                     PCMHIVE *LoadedHive)
{
    FILE_INTERNAL_INFORMATION ProbeInfo, HiveInfo;
    IO_STATUS_BLOCK IoStatus;
    PFILE_OBJECT ProbeObject = NULL, HiveObject;
    PLIST_ENTRY Entry;
    PCMHIVE Hive, *Hives = NULL;
    SIZE_T Count = 0, Index = 0, i;
    NTSTATUS Status;

    *LoadedHive = NULL;
    Status = ZwQueryInformationFile(Probe, &IoStatus, &ProbeInfo,
                                    sizeof(ProbeInfo), FileInternalInformation);
    if (!NT_SUCCESS(Status)) return Status;
    Status = ObReferenceObjectByHandle(Probe, 0, IoFileObjectType, KernelMode,
                                       (PVOID *)&ProbeObject, NULL);
    if (!NT_SUCCESS(Status)) return Status;
    ExAcquirePushLockShared(&CmpHiveListHeadLock);
    for (Entry = CmpHiveListHead.Flink; Entry != &CmpHiveListHead; Entry = Entry->Flink)
    {
        Hive = CONTAINING_RECORD(Entry, CMHIVE, HiveList);
        if ((Hive->Flags & CMHIVE_FLAG_APPLICATION_HIVE) &&
            !(Hive->Hive.HiveFlags & HIVE_IS_UNLOADING)) ++Count;
    }
    ExReleasePushLockShared(&CmpHiveListHeadLock);
    if (!Count) goto Exit;
    if (Count > MAXULONG_PTR / sizeof(*Hives))
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    Hives = ExAllocatePoolWithTag(PagedPool, Count * sizeof(*Hives), TAG_CM);
    if (!Hives)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    ExAcquirePushLockShared(&CmpHiveListHeadLock);
    for (Entry = CmpHiveListHead.Flink; Entry != &CmpHiveListHead; Entry = Entry->Flink)
    {
        Hive = CONTAINING_RECORD(Entry, CMHIVE, HiveList);
        if (!(Hive->Flags & CMHIVE_FLAG_APPLICATION_HIVE) ||
            (Hive->Hive.HiveFlags & HIVE_IS_UNLOADING)) continue;
        ASSERT(Index < Count);
        CmpReferenceHive(Hive);
        Hives[Index++] = Hive;
    }
    ExReleasePushLockShared(&CmpHiveListHeadLock);
    Status = STATUS_SUCCESS;
    for (i = 0; i < Index; ++i)
    {
        Hive = Hives[i];
        if (!Hive->FileHandles[HFILE_TYPE_PRIMARY]) continue;
        Status = ObReferenceObjectByHandle(Hive->FileHandles[HFILE_TYPE_PRIMARY],
                                           0, IoFileObjectType, KernelMode,
                                           (PVOID *)&HiveObject, NULL);
        if (!NT_SUCCESS(Status)) break;
        if (IoGetBaseFileSystemDeviceObject(ProbeObject) !=
            IoGetBaseFileSystemDeviceObject(HiveObject))
        {
            ObDereferenceObject(HiveObject);
            continue;
        }
        Status = ZwQueryInformationFile(Hive->FileHandles[HFILE_TYPE_PRIMARY],
                                        &IoStatus, &HiveInfo, sizeof(HiveInfo),
                                        FileInternalInformation);
        ObDereferenceObject(HiveObject);
        if (!NT_SUCCESS(Status)) break;
        if (ProbeInfo.IndexNumber.QuadPart != HiveInfo.IndexNumber.QuadPart) continue;
        Status = CmpCheckAppHiveFileAccess(ProbeObject, AccessMode);
        if (NT_SUCCESS(Status))
        {
            *LoadedHive = Hive;
            Hives[i] = NULL;
        }
        break;
    }
    for (i = 0; i < Index; ++i)
        if (Hives[i]) CmpDereferenceHive(Hives[i]);
Exit:
    if (Hives) ExFreePoolWithTag(Hives, TAG_CM);
    ObDereferenceObject(ProbeObject);
    return Status;
}

static NTSTATUS
CmpOpenAppHiveFiles(POBJECT_ATTRIBUTES SourceFile,
                    KPROCESSOR_MODE AccessMode,
                    HANDLE *Primary,
                    HANDLE *Log,
                    PBOOLEAN NewHive)
{
    OBJECT_ATTRIBUTES Attributes = *SourceFile;
    IO_STATUS_BLOCK IoStatus;
    FILE_STANDARD_INFORMATION Standard;
    UNICODE_STRING LogName;
    NTSTATUS Status;

    *Primary = NULL;
    *Log = NULL;
    *NewHive = FALSE;
    Attributes.Attributes |= OBJ_KERNEL_HANDLE;
    if (AccessMode != KernelMode) Attributes.Attributes |= OBJ_FORCE_ACCESS_CHECK;
    Attributes.SecurityDescriptor = NULL;
    Attributes.SecurityQualityOfService = NULL;
    Status = ZwCreateFile(Primary,
                          FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE,
                          &Attributes, &IoStatus, NULL, FILE_ATTRIBUTE_NORMAL,
                          0, FILE_OPEN_IF,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                          NULL, 0);
    if (!NT_SUCCESS(Status)) return Status;
    *NewHive = IoStatus.Information == FILE_CREATED;
    Status = ZwQueryInformationFile(*Primary, &IoStatus, &Standard,
                                    sizeof(Standard), FileStandardInformation);
    if (!NT_SUCCESS(Status)) goto Exit;
    if (!Standard.EndOfFile.QuadPart) *NewHive = TRUE;
    if (SourceFile->ObjectName->Length > MAXUSHORT - 5 * sizeof(WCHAR))
    {
        Status = STATUS_NAME_TOO_LONG;
        goto Exit;
    }
    LogName.Length = SourceFile->ObjectName->Length + 4 * sizeof(WCHAR);
    LogName.MaximumLength = LogName.Length + sizeof(WCHAR);
    LogName.Buffer = ExAllocatePoolWithTag(PagedPool, LogName.MaximumLength, TAG_CM);
    if (!LogName.Buffer)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    RtlCopyMemory(LogName.Buffer, SourceFile->ObjectName->Buffer,
                  SourceFile->ObjectName->Length);
    RtlCopyMemory((PUCHAR)LogName.Buffer + SourceFile->ObjectName->Length,
                  L".LOG", 5 * sizeof(WCHAR));
    Attributes.ObjectName = &LogName;
    Status = ZwCreateFile(Log,
                          FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE,
                          &Attributes, &IoStatus, NULL, FILE_ATTRIBUTE_HIDDEN,
                          0, FILE_OPEN_IF,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                          NULL, 0);
    ExFreePoolWithTag(LogName.Buffer, TAG_CM);
Exit:
    if (!NT_SUCCESS(Status))
    {
        if (*Log) ZwClose(*Log);
        ZwClose(*Primary);
        *Log = NULL;
        *Primary = NULL;
    }
    return Status;
}

NTSTATUS
NTAPI
CmpLoadAppHive(POBJECT_ATTRIBUTES TargetKey,
               POBJECT_ATTRIBUTES SourceFile,
               ULONG Flags,
               KPROCESSOR_MODE AccessMode,
               PCM_KEY_BODY *RootBody,
               PCMHIVE *LoadedHive)
{
    OBJECT_ATTRIBUTES Attributes = *SourceFile;
    IO_STATUS_BLOCK IoStatus;
    HANDLE Probe = NULL, Primary = NULL, Log = NULL;
    PCMHIVE Hive = NULL;
    PCM_KEY_BODY Body = NULL;
    PCM_KEY_CONTROL_BLOCK Kcb;
    PCM_KEY_NODE Node;
    HCELL_INDEX RootCell;
    SECURITY_SUBJECT_CONTEXT Subject;
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    NTSTATUS Status;
    BOOLEAN NewHive = FALSE, Constructed = FALSE;
    PWCHAR RootName;
    UNICODE_STRING RootPath = {0};

    PAGED_CODE();
    *RootBody = NULL;
    *LoadedHive = NULL;
    if (!TargetKey->ObjectName || !TargetKey->ObjectName->Length ||
        !SourceFile->ObjectName || !SourceFile->ObjectName->Length ||
        !TargetKey->ObjectName->Buffer || !SourceFile->ObjectName->Buffer ||
        (TargetKey->ObjectName->Length & (sizeof(WCHAR) - 1)) ||
        (SourceFile->ObjectName->Length & (sizeof(WCHAR) - 1)))
        return STATUS_OBJECT_NAME_INVALID;
    if (TargetKey->ObjectName->Length > MAXUSHORT - sizeof(WCHAR) ||
        SourceFile->ObjectName->Length > MAXUSHORT - sizeof(WCHAR))
        return STATUS_NAME_TOO_LONG;
    if ((SourceFile->Attributes | TargetKey->Attributes) & OBJ_FORCE_ACCESS_CHECK)
        AccessMode = UserMode;
    Status = CmpGetAppHiveRootName(TargetKey, &RootPath);
    if (!NT_SUCCESS(Status)) return Status;
    CmpLockRegistryExclusive();
    Attributes.Attributes |= OBJ_KERNEL_HANDLE;
    if (AccessMode != KernelMode) Attributes.Attributes |= OBJ_FORCE_ACCESS_CHECK;
    Attributes.SecurityDescriptor = NULL;
    Attributes.SecurityQualityOfService = NULL;
    Status = ZwOpenFile(&Probe, 0, &Attributes, &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_NON_DIRECTORY_FILE);
    if (NT_SUCCESS(Status))
    {
        Status = CmpFindLoadedAppHive(Probe, AccessMode, &Hive);
        ZwClose(Probe);
        Probe = NULL;
        if (!NT_SUCCESS(Status)) goto Exit;
    }
    else if (Status != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        goto Exit;
    }
    if (!Hive)
    {
        if (HvShutdownComplete)
        {
            Status = STATUS_TOO_LATE;
            goto Exit;
        }
        Status = CmpOpenAppHiveFiles(SourceFile, AccessMode, &Primary, &Log, &NewHive);
        if (!NT_SUCCESS(Status)) goto Exit;
        Status = CmpInitializeHive(&Hive, NewHive ? HINIT_CREATE : HINIT_FILE,
                                   HIVE_NOLAZYFLUSH, HFILE_TYPE_LOG, NULL, Primary, Log,
                                   NULL, NULL, SourceFile->ObjectName,
                                   CM_CHECK_REGISTRY_PURGE_VOLATILES);
        if (!NT_SUCCESS(Status)) goto Exit;
        Primary = Log = NULL;
        Hive->Flags |= CMHIVE_FLAG_APPLICATION_HIVE;
        Hive->Hive.HiveFlags |= HIVE_NOLAZYFLUSH;
        Constructed = TRUE;
        Hive->DeletedKcbTable = ExAllocatePoolWithTag(PagedPool,
                                                     32 * sizeof(*Hive->DeletedKcbTable),
                                                     TAG_CM);
        if (!Hive->DeletedKcbTable)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Exit;
        }
        RtlZeroMemory(Hive->DeletedKcbTable, 32 * sizeof(*Hive->DeletedKcbTable));
        Hive->DeletedKcbTableSize = 32;
        Hive->HiveRootPath = RootPath;
        RtlZeroMemory(&RootPath, sizeof(RootPath));
        Status = RtlDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
                                           SourceFile->ObjectName, &Hive->FileUserName);
        if (!NT_SUCCESS(Status)) goto Exit;
        if (NewHive)
        {
            RootName = wcsrchr(Hive->HiveRootPath.Buffer, L'\\');
            RootName = RootName ? RootName + 1 : Hive->HiveRootPath.Buffer;
            if (!*RootName)
            {
                Status = STATUS_OBJECT_NAME_INVALID;
                goto Exit;
            }
            if (!CmpCreateRootNode(&Hive->Hive, RootName, &RootCell))
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Exit;
            }
        }
    }
    Status = ObCreateObject(AccessMode, CmpKeyObjectType, NULL, KernelMode,
                            NULL, sizeof(*Body), 0, 0, (PVOID *)&Body);
    if (!NT_SUCCESS(Status)) goto Exit;
    RtlZeroMemory(Body, sizeof(*Body));
    Body->Type = CM_KEY_BODY_TYPE;
    InitializeListHead(&Body->ContextListHead);
    Body->ProcessID = PsGetCurrentProcessId();
    RootCell = Hive->Hive.BaseBlock->RootCell;
    Node = (PCM_KEY_NODE)HvGetCell(&Hive->Hive, RootCell);
    if (!Node)
    {
        Status = STATUS_REGISTRY_CORRUPT;
        goto Exit;
    }
    Kcb = CmpCreateKeyControlBlock(&Hive->Hive, RootCell, Node, NULL,
                                   0, &Hive->HiveRootPath);
    HvReleaseCell(&Hive->Hive, RootCell);
    if (!Kcb)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    Kcb->Flags |= KEY_HIVE_ENTRY | KEY_NO_DELETE;
    Hive->RootKcb = Kcb;
    Body->KeyControlBlock = Kcb;
    EnlistKeyBodyWithKCB(Body, 0);
    if (NewHive)
    {
        SeCaptureSubjectContext(&Subject);
        Status = SeAssignSecurity(NULL, NULL, &Descriptor, TRUE, &Subject,
                                   &CmpKeyObjectType->TypeInfo.GenericMapping,
                                   PagedPool);
        SeReleaseSubjectContext(&Subject);
        if (!NT_SUCCESS(Status)) goto Exit;
        CmpLockHiveFlusherExclusive(Hive);
        Status = CmpAssignSecurityDescriptorLocked(Kcb, Descriptor);
        CmpUnlockHiveFlusher(Hive);
        SeDeassignSecurity(&Descriptor);
        if (!NT_SUCCESS(Status)) goto Exit;
    }
    if (Constructed)
    {
        CmpLockHiveFlusherExclusive(Hive);
        if (NewHive && !HvSyncHive(&Hive->Hive)) Status = STATUS_REGISTRY_IO_FAILED;
        CmpUnlockHiveFlusher(Hive);
        if (!NT_SUCCESS(Status)) goto Exit;
        if (!(Flags & REG_NO_LAZY_FLUSH)) Hive->Hive.HiveFlags &= ~HIVE_NOLAZYFLUSH;
    }
    *RootBody = Body;
    *LoadedHive = Hive;
    Body = NULL;
    Hive = NULL;
    Status = STATUS_SUCCESS;
Exit:
    if (Hive && Constructed && !NT_SUCCESS(Status))
        Hive->Hive.HiveFlags |= HIVE_IS_UNLOADING;
    CmpUnlockRegistry();
    if (Body) ObDereferenceObject(Body);
    if (Hive) CmpCompleteAppHiveLoad(Hive);
    if (Log) ZwClose(Log);
    if (Primary) ZwClose(Primary);
    RtlFreeUnicodeString(&RootPath);
    return Status;
}

NTSTATUS
NTAPI
CmpOpenHiveFiles(IN PCUNICODE_STRING BaseName,
                 IN PCWSTR Extension OPTIONAL,
                 OUT PHANDLE Primary,
                 OUT PHANDLE Log,
                 OUT PULONG PrimaryDisposition,
                 OUT PULONG LogDisposition,
                 IN BOOLEAN CreateAllowed,
                 IN BOOLEAN MarkAsSystemHive,
                 IN BOOLEAN NoBuffering,
                 OUT PULONG ClusterSize OPTIONAL)
{
    HANDLE EventHandle;
    PKEVENT Event;
    NTSTATUS Status;
    UNICODE_STRING FullName, ExtensionName;
    PWCHAR NameBuffer;
    USHORT Length;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    ULONG AttributeFlags, ShareMode, DesiredAccess, CreateDisposition, IoFlags;
    USHORT CompressionState;
    FILE_STANDARD_INFORMATION FileInformation;
    FILE_FS_SIZE_INFORMATION FsSizeInformation;

    /* Create event */
    Status = CmpCreateEvent(NotificationEvent, &EventHandle, &Event);
    if (!NT_SUCCESS(Status)) return Status;

    /* Initialize the full name */
    RtlInitEmptyUnicodeString(&FullName, NULL, 0);
    Length = BaseName->Length;

    /* Check if we have an extension */
    if (Extension)
    {
        /* Update the name length */
        Length += (USHORT)wcslen(Extension) * sizeof(WCHAR) + sizeof(UNICODE_NULL);

        /* Allocate the buffer for the full name */
        NameBuffer = ExAllocatePoolWithTag(PagedPool, Length, TAG_CM);
        if (!NameBuffer)
        {
            /* Fail */
            ObDereferenceObject(Event);
            ZwClose(EventHandle);
            return STATUS_NO_MEMORY;
        }

        /* Build the full name */
        FullName.Buffer = NameBuffer;
        FullName.MaximumLength = Length;
        RtlCopyUnicodeString(&FullName, BaseName);
    }
    else
    {
        /* The base name is the full name */
        FullName = *BaseName;
        NameBuffer = NULL;
    }

    /* Initialize the attributes */
    InitializeObjectAttributes(&ObjectAttributes,
                               &FullName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    /* Check if we can create the hive */
    if (CreateAllowed && !CmpShareSystemHives)
    {
        /* Open only or create */
        CreateDisposition = FILE_OPEN_IF;
    }
    else
    {
        /* Open only */
        CreateDisposition = FILE_OPEN;
    }

    /* Setup the flags */
    // FIXME : FILE_OPEN_FOR_BACKUP_INTENT is unimplemented and breaks 3rd stage boot
    IoFlags = //FILE_OPEN_FOR_BACKUP_INTENT |
              FILE_NO_COMPRESSION |
              FILE_RANDOM_ACCESS |
              (NoBuffering ? FILE_NO_INTERMEDIATE_BUFFERING : 0);

    /* Set share and access modes */
    if (CmpMiniNTBoot && CmpShareSystemHives)
    {
        /* We're on Live CD or otherwise sharing */
        DesiredAccess = FILE_READ_DATA;
        ShareMode = FILE_SHARE_READ;
    }
    else
    {
        /* We want to write exclusively */
        ShareMode = 0;
        DesiredAccess = FILE_READ_DATA | FILE_WRITE_DATA;
    }

    /* Default attributes */
    AttributeFlags = FILE_ATTRIBUTE_NORMAL;

    /* Now create the file.
     * Note: We use FILE_SYNCHRONOUS_IO_NONALERT here to simplify CmpFileRead/CmpFileWrite.
     *       Windows does async I/O and therefore does not use this flag (or SYNCHRONIZE).
     */
    Status = ZwCreateFile(Primary,
                          DesiredAccess | SYNCHRONIZE,
                          &ObjectAttributes,
                          &IoStatusBlock,
                          NULL,
                          AttributeFlags,
                          ShareMode,
                          CreateDisposition,
                          FILE_SYNCHRONOUS_IO_NONALERT | IoFlags,
                          NULL,
                          0);
    /* Check if anything failed until now */
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ZwCreateFile(%wZ) failed, Status 0x%08lx.\n", ObjectAttributes.ObjectName, Status);

        /* Close handles and free buffers */
        if (NameBuffer) ExFreePoolWithTag(NameBuffer, TAG_CM);
        ObDereferenceObject(Event);
        ZwClose(EventHandle);
        *Primary = NULL;
        return Status;
    }

    if (MarkAsSystemHive)
    {
        /* We opened it, mark it as a system hive */
        Status = ZwFsControlFile(*Primary,
                                 EventHandle,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 FSCTL_MARK_AS_SYSTEM_HIVE,
                                 NULL,
                                 0,
                                 NULL,
                                 0);
        if (Status == STATUS_PENDING)
        {
            /* Wait for completion */
            KeWaitForSingleObject(Event,
                                  Executive,
                                  KernelMode,
                                  FALSE,
                                  NULL);
            Status = IoStatusBlock.Status;
        }

        /* If we don't support it, ignore the failure */
        if (Status == STATUS_INVALID_DEVICE_REQUEST) Status = STATUS_SUCCESS;

        if (!NT_SUCCESS(Status))
        {
            /* Close handles and free buffers */
            if (NameBuffer) ExFreePoolWithTag(NameBuffer, TAG_CM);
            ObDereferenceObject(Event);
            ZwClose(EventHandle);
            ZwClose(*Primary);
            *Primary = NULL;
            return Status;
        }
    }

    /* Disable compression */
    CompressionState = 0;
    Status = ZwFsControlFile(*Primary,
                             EventHandle,
                             NULL,
                             NULL,
                             &IoStatusBlock,
                             FSCTL_SET_COMPRESSION,
                             &CompressionState,
                             sizeof(CompressionState),
                             NULL,
                             0);
    if (Status == STATUS_PENDING)
    {
        /* Wait for completion */
        KeWaitForSingleObject(Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    }

    /* Get the disposition */
    *PrimaryDisposition = (ULONG)IoStatusBlock.Information;
    if (IoStatusBlock.Information != FILE_CREATED)
    {
        /* Check how large the file is */
        Status = ZwQueryInformationFile(*Primary,
                                        &IoStatusBlock,
                                        &FileInformation,
                                        sizeof(FileInformation),
                                        FileStandardInformation);
        if (NT_SUCCESS(Status))
        {
            /* Check if it's 0 bytes */
            if (!FileInformation.EndOfFile.QuadPart)
            {
                /* Assume it's a new file */
                *PrimaryDisposition = FILE_CREATED;
            }
        }
    }

    /* Check if the caller wants cluster size returned */
    if (ClusterSize)
    {
        /* Query it */
        Status = ZwQueryVolumeInformationFile(*Primary,
                                              &IoStatusBlock,
                                              &FsSizeInformation,
                                              sizeof(FsSizeInformation),
                                              FileFsSizeInformation);
        if (!NT_SUCCESS(Status))
        {
            /* Close handles and free buffers */
            if (NameBuffer) ExFreePoolWithTag(NameBuffer, TAG_CM);
            ObDereferenceObject(Event);
            ZwClose(EventHandle);
            return Status;
        }

        /* Check if the sector size is invalid */
        if (FsSizeInformation.BytesPerSector > HBLOCK_SIZE)
        {
            /* Close handles and free buffers */
            if (NameBuffer) ExFreePoolWithTag(NameBuffer, TAG_CM);
            ObDereferenceObject(Event);
            ZwClose(EventHandle);
            return STATUS_CANNOT_LOAD_REGISTRY_FILE;
        }

        /* Return cluster size */
        *ClusterSize = max(1, FsSizeInformation.BytesPerSector / HSECTOR_SIZE);
    }

    /* Check if we don't need to create a log file */
    if (!Extension)
    {
        /* We're done, close handles */
        ObDereferenceObject(Event);
        ZwClose(EventHandle);
        return STATUS_SUCCESS;
    }

    /* Check if we can create the hive */
    CreateDisposition = CmpShareSystemHives ? FILE_OPEN : FILE_OPEN_IF;
    if (*PrimaryDisposition == FILE_CREATED)
    {
        /* Over-write the existing log file, since this is a new hive */
        CreateDisposition = FILE_SUPERSEDE;
    }

    /* Setup the name */
    RtlInitUnicodeString(&ExtensionName, Extension);
    RtlAppendUnicodeStringToString(&FullName, &ExtensionName);

    /* Initialize the attributes */
    InitializeObjectAttributes(&ObjectAttributes,
                               &FullName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    /* Setup the flags */
    IoFlags = FILE_NO_COMPRESSION | FILE_NO_INTERMEDIATE_BUFFERING;

    /* Check if this is a log file */
    if (!_wcsnicmp(Extension, L".log", 4))
    {
        /* Hide log files */
        AttributeFlags |= FILE_ATTRIBUTE_HIDDEN;
    }

    /* Now create the file.
     * Note: We use FILE_SYNCHRONOUS_IO_NONALERT here to simplify CmpFileRead/CmpFileWrite.
     *       Windows does async I/O and therefore does not use this flag (or SYNCHRONIZE).
     */
    Status = ZwCreateFile(Log,
                          DesiredAccess | SYNCHRONIZE,
                          &ObjectAttributes,
                          &IoStatusBlock,
                          NULL,
                          AttributeFlags,
                          ShareMode,
                          CreateDisposition,
                          FILE_SYNCHRONOUS_IO_NONALERT | IoFlags,
                          NULL,
                          0);
    if (NT_SUCCESS(Status) && MarkAsSystemHive)
    {
        /* We opened it, mark it as a system hive */
        Status = ZwFsControlFile(*Log,
                                 EventHandle,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 FSCTL_MARK_AS_SYSTEM_HIVE,
                                 NULL,
                                 0,
                                 NULL,
                                 0);
        if (Status == STATUS_PENDING)
        {
            /* Wait for completion */
            KeWaitForSingleObject(Event,
                                  Executive,
                                  KernelMode,
                                  FALSE,
                                  NULL);
            Status = IoStatusBlock.Status;
        }

        /* If we don't support it, ignore the failure */
        if (Status == STATUS_INVALID_DEVICE_REQUEST) Status = STATUS_SUCCESS;

        /* If we failed, close the handle */
        if (!NT_SUCCESS(Status)) ZwClose(*Log);
    }

    /* Check if anything failed until now */
    if (!NT_SUCCESS(Status))
    {
        /* Clear the handle */
        *Log = NULL;
    }
    else
    {
        /* Disable compression */
        Status = ZwFsControlFile(*Log,
                                 EventHandle,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 FSCTL_SET_COMPRESSION,
                                 &CompressionState,
                                 sizeof(CompressionState),
                                 NULL,
                                 0);
        if (Status == STATUS_PENDING)
        {
            /* Wait for completion */
            KeWaitForSingleObject(Event,
                                  Executive,
                                  KernelMode,
                                  FALSE,
                                  NULL);
        }

        /* Return the disposition */
        *LogDisposition = (ULONG)IoStatusBlock.Information;
    }

    /* We're done, close handles and free buffers */
    if (NameBuffer) ExFreePoolWithTag(NameBuffer, TAG_CM);
    ObDereferenceObject(Event);
    ZwClose(EventHandle);
    return STATUS_SUCCESS;
}

VOID
NTAPI
CmpCloseHiveFiles(IN PCMHIVE Hive)
{
    ULONG i;

    for (i = 0; i < HFILE_TYPE_MAX; i++)
    {
        if (Hive->FileHandles[i] != NULL)
        {
            ZwClose(Hive->FileHandles[i]);
            Hive->FileHandles[i] = NULL;
        }
    }
}
