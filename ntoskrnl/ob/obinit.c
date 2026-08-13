/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ob/obinit.c
 * PURPOSE:         Handles Object Manager Initialization and Shutdown
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 *                  Eric Kohl
 *                  Thomas Weidenmueller (w3seek@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include <reactos/unaligned.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

GENERIC_MAPPING ObpTypeMapping =
{
    STANDARD_RIGHTS_READ,
    STANDARD_RIGHTS_WRITE,
    STANDARD_RIGHTS_EXECUTE,
    0x000F0001
};

GENERIC_MAPPING ObpDirectoryMapping =
{
    STANDARD_RIGHTS_READ    | DIRECTORY_QUERY               |
    DIRECTORY_TRAVERSE,
    STANDARD_RIGHTS_WRITE   | DIRECTORY_CREATE_SUBDIRECTORY |
    DIRECTORY_CREATE_OBJECT,
    STANDARD_RIGHTS_EXECUTE | DIRECTORY_QUERY               |
    DIRECTORY_TRAVERSE,
    DIRECTORY_ALL_ACCESS
};

GENERIC_MAPPING ObpSymbolicLinkMapping =
{
    STANDARD_RIGHTS_READ    | SYMBOLIC_LINK_QUERY,
    STANDARD_RIGHTS_WRITE,
    STANDARD_RIGHTS_EXECUTE | SYMBOLIC_LINK_QUERY,
    SYMBOLIC_LINK_ALL_ACCESS
};

PDEVICE_MAP ObSystemDeviceMap = NULL;
ULONG ObpTraceLevel = 0;

CODE_SEG("INIT")
VOID
NTAPI
PsInitializeQuotaSystem(VOID);

ULONG ObpInitializationPhase;

ULONG ObpObjectSecurityMode = 0;
ULONG ObpProtectionMode = 0;

#ifdef _WIN64
C_ASSERT(sizeof(OBJECT_HEADER) == 56);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER, PointerCount) == 0);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER, HandleCount) == 8);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER, Lock) == 16);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER, TypeIndex) == 24);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER, TraceFlags) == 25);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER, InfoMask) == 26);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER, Flags) == 27);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER, ObjectCreateInfo) == 32);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER, SecurityDescriptor) == 40);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER, Body) == 48);
C_ASSERT(sizeof(OBJECT_HEADER_QUOTA_INFO) == 32);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER_QUOTA_INFO, SecurityDescriptorQuotaBlock) == 16);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER_QUOTA_INFO, Reserved2) == 24);
C_ASSERT(sizeof(OBJECT_HEADER_PROCESS_INFO) == 16);
C_ASSERT(sizeof(OBJECT_HEADER_HANDLE_INFO) == 16);
C_ASSERT(sizeof(OBJECT_HEADER_NAME_INFO) == 32);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER_NAME_INFO, Directory) == 0);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER_NAME_INFO, Name) == 8);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER_NAME_INFO, ReferenceCount) == 24);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER_NAME_INFO, Reserved) == 28);
C_ASSERT(sizeof(OBJECT_HEADER_CREATOR_INFO) == 32);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER_CREATOR_INFO, CreatorUniqueProcess) == 16);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER_CREATOR_INFO, CreatorBackTraceIndex) == 24);
C_ASSERT(sizeof(OBJECT_HEADER_AUDIT_INFO) == 16);
C_ASSERT(sizeof(OBJECT_HEADER_EXTENDED_INFO) == 16);
C_ASSERT(sizeof(OBJECT_HEADER_PADDING_INFO) == 4);
C_ASSERT(sizeof(OB_EXTENDED_PARSE_PARAMETERS) == 16);
C_ASSERT(FIELD_OFFSET(OB_EXTENDED_PARSE_PARAMETERS, Length) == 0);
C_ASSERT(FIELD_OFFSET(OB_EXTENDED_PARSE_PARAMETERS, RestrictedAccessMask) == 4);
C_ASSERT(FIELD_OFFSET(OB_EXTENDED_PARSE_PARAMETERS, Silo) == 8);
C_ASSERT(sizeof(OBJECT_TYPE_INITIALIZER) == 120);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, ObjectTypeFlags) == 2);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, ObjectTypeCode) == 4);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, InvalidAttributes) == 8);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, GenericMapping) == 12);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, ValidAccessMask) == 28);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, RetainAccess) == 32);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, PoolType) == 36);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, DefaultPagedPoolCharge) == 40);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, DefaultNonPagedPoolCharge) == 44);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, DumpProcedure) == 48);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, OkayToCloseProcedure) == 104);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, WaitObjectFlagMask) == 112);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, WaitObjectFlagOffset) == 116);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, WaitObjectPointerOffset) == 118);
C_ASSERT(sizeof(OBJECT_TYPE) == 224);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, TypeList) == 0);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, Name) == 16);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, DefaultObject) == 32);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, Index) == 40);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, TotalNumberOfObjects) == 44);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, TotalNumberOfHandles) == 48);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, HighWaterNumberOfObjects) == 52);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, HighWaterNumberOfHandles) == 56);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, TypeInfo) == 64);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, TypeLock) == 184);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, Key) == 192);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, CallbackList) == 200);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, SeMandatoryLabelMask) == 216);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, SeTrustConstraintMask) == 220);
#endif

/* PRIVATE FUNCTIONS *********************************************************/

static
CODE_SEG("INIT")
NTSTATUS
NTAPI
ObpCreateKernelObjectsSD(OUT PSECURITY_DESCRIPTOR *SecurityDescriptor)
{
    PSECURITY_DESCRIPTOR Sd = NULL;
    PACL Dacl;
    ULONG AclSize, SdSize;
    NTSTATUS Status;

    AclSize = sizeof(ACL) +
              sizeof(ACE) + RtlLengthSid(SeWorldSid) +
              sizeof(ACE) + RtlLengthSid(SeAliasAdminsSid) +
              sizeof(ACE) + RtlLengthSid(SeLocalSystemSid);

    SdSize = sizeof(SECURITY_DESCRIPTOR) + AclSize;

    /* Allocate the SD and ACL */
    Sd = ExAllocatePoolWithTag(PagedPool, SdSize, TAG_SD);
    if (Sd == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Initialize the SD */
    Status = RtlCreateSecurityDescriptor(Sd,
                                         SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(Status))
        goto done;

    Dacl = (PACL)((INT_PTR)Sd + sizeof(SECURITY_DESCRIPTOR));

    /* Initialize the DACL */
    RtlCreateAcl(Dacl, AclSize, ACL_REVISION);

    /* Add the ACEs */
    RtlAddAccessAllowedAce(Dacl,
                           ACL_REVISION,
                           GENERIC_READ,
                           SeWorldSid);

    RtlAddAccessAllowedAce(Dacl,
                           ACL_REVISION,
                           GENERIC_ALL,
                           SeAliasAdminsSid);

    RtlAddAccessAllowedAce(Dacl,
                           ACL_REVISION,
                           GENERIC_ALL,
                           SeLocalSystemSid);

    /* Attach the DACL to the SD */
    Status = RtlSetDaclSecurityDescriptor(Sd,
                                          TRUE,
                                          Dacl,
                                          FALSE);
    if (!NT_SUCCESS(Status))
        goto done;

#ifdef _M_ARM64
    WriteUnalignedUlongPtr((ULONG_PTR*)SecurityDescriptor, (ULONG_PTR)Sd);
#else
    *SecurityDescriptor = Sd;
#endif

done:
    if (!NT_SUCCESS(Status))
    {
        if (Sd != NULL)
            ExFreePoolWithTag(Sd, TAG_SD);
    }

    return Status;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
ObInit2(VOID)
{
    CCHAR i;
    PKPRCB Prcb;
    PGENERAL_LOOKASIDE CurrentList = NULL;

    /* Now allocate the per-processor lists */
    for (i = 0; i < KeNumberProcessors; i++)
    {
        /* Get the PRCB for this CPU */
        Prcb = KiProcessorBlock[(int)i];

        /* Set the OBJECT_CREATE_INFORMATION List */
        Prcb->PPLookasideList[LookasideCreateInfoList].L = &ObpCreateInfoLookasideList;
        CurrentList = ExAllocatePoolWithTag(NonPagedPool,
                                            sizeof(GENERAL_LOOKASIDE),
                                            'ICbO');
        if (CurrentList)
        {
            /* Initialize it */
            ExInitializeSystemLookasideList(CurrentList,
                                            NonPagedPool,
                                            sizeof(OBJECT_CREATE_INFORMATION),
                                            'ICbO',
                                            32,
                                            &ExSystemLookasideListHead);
        }
        else
        {
            /* No list, use the static buffer */
            CurrentList = &ObpCreateInfoLookasideList;
        }

        /* Link it */
        Prcb->PPLookasideList[LookasideCreateInfoList].P = CurrentList;

        /* Set the captured UNICODE_STRING Object Name List */
        Prcb->PPLookasideList[LookasideNameBufferList].L = &ObpNameBufferLookasideList;
        CurrentList = ExAllocatePoolWithTag(NonPagedPool,
                                            sizeof(GENERAL_LOOKASIDE),
                                            'MNbO');
        if (CurrentList)
        {
            /* Initialize it */
            ExInitializeSystemLookasideList(CurrentList,
                                            PagedPool,
                                            248,
                                            'MNbO',
                                            16,
                                            &ExSystemLookasideListHead);
        }
        else
        {
            /* No list, use the static buffer */
            CurrentList = &ObpNameBufferLookasideList;
        }

        /* Link it */
        Prcb->PPLookasideList[LookasideNameBufferList].P = CurrentList;
    }

    return TRUE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
ObInitSystem(VOID)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;
    OBJECT_TYPE_INITIALIZER ObjectTypeInitializer;
    OBP_LOOKUP_CONTEXT Context;
    HANDLE Handle;
    PKPRCB Prcb = KeGetCurrentPrcb();
    PLIST_ENTRY ListHead, NextEntry;
    POBJECT_HEADER Header;
    POBJECT_HEADER_CREATOR_INFO CreatorInfo;
    POBJECT_HEADER_NAME_INFO NameInfo;
    PSECURITY_DESCRIPTOR KernelObjectsSD = NULL;
    LARGE_INTEGER Counter;
    ULONG InfoMask;
    UCHAR InfoOffset;
    NTSTATUS Status;

    /* Check if this is actually Phase 1 initialization */
    if (ObpInitializationPhase != 0) goto ObPostPhase0;

    /* Initialize native object-header metadata before creating any objects */
    RtlZeroMemory(ObTypeIndexTable, sizeof(ObTypeIndexTable));
    for (InfoMask = 0; InfoMask < RTL_NUMBER_OF(ObpInfoMaskToOffset); InfoMask++)
    {
        InfoOffset = 0;
        if (InfoMask & OBP_CREATOR_INFO_MASK) InfoOffset += sizeof(OBJECT_HEADER_CREATOR_INFO);
        if (InfoMask & OBP_NAME_INFO_MASK) InfoOffset += sizeof(OBJECT_HEADER_NAME_INFO);
        if (InfoMask & OBP_HANDLE_INFO_MASK) InfoOffset += sizeof(OBJECT_HEADER_HANDLE_INFO);
        if (InfoMask & OBP_QUOTA_INFO_MASK) InfoOffset += sizeof(OBJECT_HEADER_QUOTA_INFO);
        if (InfoMask & OBP_PROCESS_INFO_MASK) InfoOffset += sizeof(OBJECT_HEADER_PROCESS_INFO);
        if (InfoMask & OBP_AUDIT_INFO_MASK) InfoOffset += sizeof(OBJECT_HEADER_AUDIT_INFO);
        if (InfoMask & OBP_EXTENDED_INFO_MASK) InfoOffset += sizeof(OBJECT_HEADER_EXTENDED_INFO);
        if (InfoMask & OBP_PADDING_INFO_MASK) InfoOffset += sizeof(OBJECT_HEADER_PADDING_INFO);
        ObpInfoMaskToOffset[InfoMask] = InfoOffset;
    }
    Counter = KeQueryPerformanceCounter(NULL);
    ObHeaderCookie = (UCHAR)(Counter.LowPart ^ Counter.HighPart ^ (ULONG_PTR)Prcb);

    /* Initialize the OBJECT_CREATE_INFORMATION List */
    ExInitializeSystemLookasideList(&ObpCreateInfoLookasideList,
                                    NonPagedPool,
                                    sizeof(OBJECT_CREATE_INFORMATION),
                                    'ICbO',
                                    32,
                                    &ExSystemLookasideListHead);

    /* Set the captured UNICODE_STRING Object Name List */
    ExInitializeSystemLookasideList(&ObpNameBufferLookasideList,
                                    PagedPool,
                                    248,
                                    'MNbO',
                                    16,
                                    &ExSystemLookasideListHead);

    /* Temporarily setup both pointers to the shared list */
    Prcb->PPLookasideList[LookasideCreateInfoList].L = &ObpCreateInfoLookasideList;
    Prcb->PPLookasideList[LookasideCreateInfoList].P = &ObpCreateInfoLookasideList;
    Prcb->PPLookasideList[LookasideNameBufferList].L = &ObpNameBufferLookasideList;
    Prcb->PPLookasideList[LookasideNameBufferList].P = &ObpNameBufferLookasideList;

    /* Initialize the security descriptor cache */
    ObpInitSdCache();

    /* Initialize the Default Event */
    KeInitializeEvent(&ObpDefaultObject, NotificationEvent, TRUE);

    /* Initialize the Dos Device Map mutex */
    KeInitializeGuardedMutex(&ObpDeviceMapLock);

    /* Setup default access for the system process */
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    PsGetCurrentProcess()->ImagePathHash = PROCESS_ALL_ACCESS;
    PsGetCurrentThread()->SpareUlong0 = THREAD_ALL_ACCESS;
#else
    PsGetCurrentProcess()->GrantedAccess = PROCESS_ALL_ACCESS;
    PsGetCurrentThread()->GrantedAccess = THREAD_ALL_ACCESS;
#endif

    /* Setup the Object Reaper */
    ExInitializeWorkItem(&ObpReaperWorkItem, ObpReapObject, NULL);

    /* Initialize default Quota block */
    PsInitializeQuotaSystem();

    /* Create kernel handle table */
    PsGetCurrentProcess()->ObjectTable = ExCreateHandleTable(NULL);
    ObpKernelHandleTable = PsGetCurrentProcess()->ObjectTable;

    /* Create the Type Type */
    RtlZeroMemory(&ObjectTypeInitializer, sizeof(ObjectTypeInitializer));
    RtlInitUnicodeString(&Name, L"Type");
    ObjectTypeInitializer.Length = sizeof(ObjectTypeInitializer);
    ObjectTypeInitializer.ValidAccessMask = OBJECT_TYPE_ALL_ACCESS;
    ObjectTypeInitializer.UseDefaultObject = TRUE;
    ObjectTypeInitializer.MaintainTypeList = TRUE;
    ObjectTypeInitializer.PoolType = NonPagedPool;
    ObjectTypeInitializer.GenericMapping = ObpTypeMapping;
    ObjectTypeInitializer.DefaultNonPagedPoolCharge = sizeof(OBJECT_TYPE);
    ObjectTypeInitializer.InvalidAttributes = OBJ_OPENLINK;
    ObjectTypeInitializer.DeleteProcedure = ObpDeleteObjectType;
    ObCreateObjectType(&Name, &ObjectTypeInitializer, NULL, &ObpTypeObjectType);

    /* Create the Directory Type */
    RtlInitUnicodeString(&Name, L"Directory");
    ObjectTypeInitializer.PoolType = PagedPool;
    ObjectTypeInitializer.ValidAccessMask = DIRECTORY_ALL_ACCESS;
    ObjectTypeInitializer.CaseInsensitive = TRUE;
    ObjectTypeInitializer.MaintainTypeList = FALSE;
    ObjectTypeInitializer.GenericMapping = ObpDirectoryMapping;
    ObjectTypeInitializer.DeleteProcedure = NULL;
    ObjectTypeInitializer.DefaultNonPagedPoolCharge = sizeof(OBJECT_DIRECTORY);
    ObCreateObjectType(&Name, &ObjectTypeInitializer, NULL, &ObpDirectoryObjectType);
    ObpDirectoryObjectType->TypeInfo.ValidAccessMask &= ~SYNCHRONIZE;

    /* Create 'symbolic link' object type */
    RtlInitUnicodeString(&Name, L"SymbolicLink");
    ObjectTypeInitializer.DefaultNonPagedPoolCharge = sizeof(OBJECT_SYMBOLIC_LINK);
    ObjectTypeInitializer.GenericMapping = ObpSymbolicLinkMapping;
    ObjectTypeInitializer.ValidAccessMask = SYMBOLIC_LINK_ALL_ACCESS;
    ObjectTypeInitializer.ParseProcedure = ObpParseSymbolicLink;
    ObjectTypeInitializer.DeleteProcedure = ObpDeleteSymbolicLink;
    ObCreateObjectType(&Name, &ObjectTypeInitializer, NULL, &ObpSymbolicLinkObjectType);
    ObpSymbolicLinkObjectType->TypeInfo.ValidAccessMask &= ~SYNCHRONIZE;

    /* Phase 0 initialization complete */
    ObpInitializationPhase++;
    return TRUE;

ObPostPhase0:

    /* Re-initialize lookaside lists */
    ObInit2();

    /* Initialize Object Types directory attributes */
    RtlInitUnicodeString(&Name, L"\\");
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT,
                               NULL,
                               SePublicDefaultUnrestrictedSd);

    /* Create the directory */
    Status = NtCreateDirectoryObject(&Handle,
                                     DIRECTORY_ALL_ACCESS,
                                     &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return FALSE;

    /* Get a handle to it */
    Status = ObReferenceObjectByHandle(Handle,
                                       0,
                                       ObpDirectoryObjectType,
                                       KernelMode,
                                       (PVOID*)&ObpRootDirectoryObject,
                                       NULL);
    if (!NT_SUCCESS(Status)) return FALSE;

    /* Close the extra handle */
    Status = NtClose(Handle);
    if (!NT_SUCCESS(Status)) return FALSE;

    /* Create a custom security descriptor for the KernelObjects directory */
    Status = ObpCreateKernelObjectsSD(&KernelObjectsSD);
    if (!NT_SUCCESS(Status))
        return FALSE;

    /* Initialize the KernelObjects directory attributes */
    RtlInitUnicodeString(&Name, L"\\KernelObjects");
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT,
                               NULL,
                               KernelObjectsSD);

    /* Create the directory */
    Status = NtCreateDirectoryObject(&Handle,
                                     DIRECTORY_ALL_ACCESS,
                                     &ObjectAttributes);
    ExFreePoolWithTag(KernelObjectsSD, TAG_SD);
    if (!NT_SUCCESS(Status)) return FALSE;

    /* Close the extra handle */
    Status = NtClose(Handle);
    if (!NT_SUCCESS(Status)) return FALSE;

    /* Initialize ObjectTypes directory attributes */
    RtlInitUnicodeString(&Name, L"\\ObjectTypes");
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT,
                               NULL,
                               NULL);

    /* Create the directory */
    Status = NtCreateDirectoryObject(&Handle,
                                     DIRECTORY_ALL_ACCESS,
                                     &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return FALSE;

    /* Get a handle to it */
    Status = ObReferenceObjectByHandle(Handle,
                                       0,
                                       ObpDirectoryObjectType,
                                       KernelMode,
                                       (PVOID*)&ObpTypeDirectoryObject,
                                       NULL);
    if (!NT_SUCCESS(Status)) return FALSE;

    /* Close the extra handle */
    Status = NtClose(Handle);
    if (!NT_SUCCESS(Status)) return FALSE;

    /* Initialize the lookup context and lock it */
    ObpInitializeLookupContext(&Context);
    ObpAcquireLookupContextLock(&Context, ObpTypeDirectoryObject);

    /* Loop the object types */
    ListHead = &ObpTypeObjectType->TypeList;
    NextEntry = ListHead->Flink;
    while (ListHead != NextEntry)
    {
        /* Get the creator info from the list */
        CreatorInfo = CONTAINING_RECORD(NextEntry,
                                        OBJECT_HEADER_CREATOR_INFO,
                                        TypeList);

        /* Recover the header and the name header from the creator info */
        Header = (POBJECT_HEADER)(CreatorInfo + 1);
        NameInfo = OBJECT_HEADER_TO_NAME_INFO(Header);

        /* Make sure we have a name, and aren't inserted yet */
        if ((NameInfo) && !(NameInfo->Directory))
        {
            /* Do the initial lookup to setup the context */
            if (!ObpLookupEntryDirectory(ObpTypeDirectoryObject,
                                         &NameInfo->Name,
                                         OBJ_CASE_INSENSITIVE,
                                         FALSE,
                                         &Context))
            {
                /* Insert this object type */
                ObpInsertEntryDirectory(ObpTypeDirectoryObject,
                                        &Context,
                                        Header);
            }
        }

        /* Move to the next entry */
        NextEntry = NextEntry->Flink;
    }

    /* Cleanup after lookup */
    ObpReleaseLookupContext(&Context);

    /* Initialize DOS Devices Directory and related Symbolic Links */
    Status = ObpCreateDosDevicesDirectory();
    if (!NT_SUCCESS(Status)) return FALSE;
    return TRUE;
}

/* EOF */
