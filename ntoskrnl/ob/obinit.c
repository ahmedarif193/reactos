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

#ifdef _M_ARM64
extern VOID KiArm64RawPuts(const char *s);
#endif

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

#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] KernelObjectsSD begin\n");
#endif

    AclSize = sizeof(ACL) +
              sizeof(ACE) + RtlLengthSid(SeWorldSid) +
              sizeof(ACE) + RtlLengthSid(SeAliasAdminsSid) +
              sizeof(ACE) + RtlLengthSid(SeLocalSystemSid);

    SdSize = sizeof(SECURITY_DESCRIPTOR) + AclSize;

    /* Allocate the SD and ACL */
    Sd = ExAllocatePoolWithTag(PagedPool, SdSize, TAG_SD);
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] KernelObjectsSD allocated\n");
#endif
    if (Sd == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Initialize the SD */
    Status = RtlCreateSecurityDescriptor(Sd,
                                         SECURITY_DESCRIPTOR_REVISION);
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] KernelObjectsSD descriptor created\n");
#endif
    if (!NT_SUCCESS(Status))
        goto done;

    Dacl = (PACL)((INT_PTR)Sd + sizeof(SECURITY_DESCRIPTOR));

    /* Initialize the DACL */
    RtlCreateAcl(Dacl, AclSize, ACL_REVISION);
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] KernelObjectsSD acl created\n");
#endif

    /* Add the ACEs */
    RtlAddAccessAllowedAce(Dacl,
                           ACL_REVISION,
                           GENERIC_READ,
                           SeWorldSid);
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] KernelObjectsSD world ace added\n");
#endif

    RtlAddAccessAllowedAce(Dacl,
                           ACL_REVISION,
                           GENERIC_ALL,
                           SeAliasAdminsSid);
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] KernelObjectsSD admins ace added\n");
#endif

    RtlAddAccessAllowedAce(Dacl,
                           ACL_REVISION,
                           GENERIC_ALL,
                           SeLocalSystemSid);
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] KernelObjectsSD system ace added\n");
#endif

    /* Attach the DACL to the SD */
    Status = RtlSetDaclSecurityDescriptor(Sd,
                                          TRUE,
                                          Dacl,
                                          FALSE);
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] KernelObjectsSD dacl attached\n");
#endif
    if (!NT_SUCCESS(Status))
        goto done;

#ifdef _M_ARM64
    WriteUnalignedUlongPtr((ULONG_PTR*)SecurityDescriptor, (ULONG_PTR)Sd);
#else
    *SecurityDescriptor = Sd;
#endif

#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] KernelObjectsSD output stored\n");
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
    NTSTATUS Status;

    /* Check if this is actually Phase 1 initialization */
    if (ObpInitializationPhase != 0) goto ObPostPhase0;

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

#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] phase1 begin\n");
#endif

    /* Re-initialize lookaside lists */
    ObInit2();

#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] lookaside ready\n");
#endif

    /* Initialize Object Types directory attributes */
    RtlInitUnicodeString(&Name, L"\\");
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT,
                               NULL,
                               SePublicDefaultUnrestrictedSd);

    /* Create the directory */
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] create root directory\n");
#endif
    Status = NtCreateDirectoryObject(&Handle,
                                     DIRECTORY_ALL_ACCESS,
                                     &ObjectAttributes);
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] create root directory returned\n");
#endif
    if (!NT_SUCCESS(Status)) return FALSE;

    /* Get a handle to it */
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] reference root directory\n");
#endif
    Status = ObReferenceObjectByHandle(Handle,
                                       0,
                                       ObpDirectoryObjectType,
                                       KernelMode,
                                       (PVOID*)&ObpRootDirectoryObject,
                                       NULL);
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] reference root directory returned\n");
#endif
    if (!NT_SUCCESS(Status)) return FALSE;

    /* Close the extra handle */
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] close root handle\n");
#endif
    Status = NtClose(Handle);
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] close root handle returned\n");
#endif
    if (!NT_SUCCESS(Status)) return FALSE;

    /* Create a custom security descriptor for the KernelObjects directory */
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] create KernelObjects SD\n");
#endif
    Status = ObpCreateKernelObjectsSD(&KernelObjectsSD);
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] create KernelObjects SD returned\n");
#endif
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
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] create KernelObjects directory\n");
#endif
    Status = NtCreateDirectoryObject(&Handle,
                                     DIRECTORY_ALL_ACCESS,
                                     &ObjectAttributes);
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] create KernelObjects directory returned\n");
#endif
    ExFreePoolWithTag(KernelObjectsSD, TAG_SD);
    if (!NT_SUCCESS(Status)) return FALSE;

    /* Close the extra handle */
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] close KernelObjects handle\n");
#endif
    Status = NtClose(Handle);
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] close KernelObjects handle returned\n");
#endif
    if (!NT_SUCCESS(Status)) return FALSE;

    /* Initialize ObjectTypes directory attributes */
    RtlInitUnicodeString(&Name, L"\\ObjectTypes");
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT,
                               NULL,
                               NULL);

    /* Create the directory */
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] create ObjectTypes directory\n");
#endif
    Status = NtCreateDirectoryObject(&Handle,
                                     DIRECTORY_ALL_ACCESS,
                                     &ObjectAttributes);
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] create ObjectTypes directory returned\n");
#endif
    if (!NT_SUCCESS(Status)) return FALSE;

    /* Get a handle to it */
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] reference ObjectTypes directory\n");
#endif
    Status = ObReferenceObjectByHandle(Handle,
                                       0,
                                       ObpDirectoryObjectType,
                                       KernelMode,
                                       (PVOID*)&ObpTypeDirectoryObject,
                                       NULL);
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] reference ObjectTypes directory returned\n");
#endif
    if (!NT_SUCCESS(Status)) return FALSE;

    /* Close the extra handle */
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] close ObjectTypes handle\n");
#endif
    Status = NtClose(Handle);
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] close ObjectTypes handle returned\n");
#endif
    if (!NT_SUCCESS(Status)) return FALSE;

    /* Initialize the lookup context and lock it */
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] insert object types begin\n");
#endif
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

#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] insert object types end\n");
#endif

    /* Initialize DOS Devices Directory and related Symbolic Links */
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] create DOS devices begin\n");
#endif
    Status = ObpCreateDosDevicesDirectory();
#ifdef _M_ARM64
    KiArm64RawPuts("[obinit] create DOS devices returned\n");
#endif
    if (!NT_SUCCESS(Status)) return FALSE;
    return TRUE;
}

/* EOF */
