/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/config/cmse.c
 * PURPOSE:         Configuration Manager - Security Subsystem Interface
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include "ntoskrnl.h"
#define NDEBUG
#include "debug.h"

/* GLOBALS *******************************************************************/

/* FUNCTIONS *****************************************************************/

PSECURITY_DESCRIPTOR
NTAPI
CmpHiveRootSecurityDescriptor(VOID)
{
    NTSTATUS Status;
    PSECURITY_DESCRIPTOR SecurityDescriptor;
    PACL Acl, AclCopy;
    PSID Sid[4];
    SID_IDENTIFIER_AUTHORITY WorldAuthority = {SECURITY_WORLD_SID_AUTHORITY};
    SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};
    ULONG AceLength, AclLength, SidLength;
    PACE_HEADER AceHeader;
    ULONG i;
    PAGED_CODE();

    /* Phase 1: Allocate SIDs */
    SidLength = RtlLengthRequiredSid(1);
    Sid[0] = ExAllocatePoolWithTag(PagedPool, SidLength, TAG_CMSD);
    Sid[1] = ExAllocatePoolWithTag(PagedPool, SidLength, TAG_CMSD);
    Sid[2] = ExAllocatePoolWithTag(PagedPool, SidLength, TAG_CMSD);
    SidLength = RtlLengthRequiredSid(2);
    Sid[3] = ExAllocatePoolWithTag(PagedPool, SidLength, TAG_CMSD);

    /* Make sure all SIDs were allocated */
    if (!Sid[0] || !Sid[1] || !Sid[2] || !Sid[3])
    {
        /* Bugcheck */
        KeBugCheckEx(REGISTRY_ERROR, 11, 1, 0, 0);
    }

    /* Phase 2: Initialize all SIDs */
    Status = RtlInitializeSid(Sid[0], &WorldAuthority, 1);
    Status |= RtlInitializeSid(Sid[1], &NtAuthority, 1);
    Status |= RtlInitializeSid(Sid[2], &NtAuthority, 1);
    Status |= RtlInitializeSid(Sid[3], &NtAuthority, 2);
    if (!NT_SUCCESS(Status)) KeBugCheckEx(REGISTRY_ERROR, 11, 2, 0, 0);

    /* Phase 2: Setup SID Sub Authorities */
    *RtlSubAuthoritySid(Sid[0], 0) = SECURITY_WORLD_RID;
    *RtlSubAuthoritySid(Sid[1], 0) = SECURITY_RESTRICTED_CODE_RID;
    *RtlSubAuthoritySid(Sid[2], 0) = SECURITY_LOCAL_SYSTEM_RID;
    *RtlSubAuthoritySid(Sid[3], 0) = SECURITY_BUILTIN_DOMAIN_RID;
    *RtlSubAuthoritySid(Sid[3], 1) = DOMAIN_ALIAS_RID_ADMINS;

    /* Make sure all SIDs are valid */
    ASSERT(RtlValidSid(Sid[0]));
    ASSERT(RtlValidSid(Sid[1]));
    ASSERT(RtlValidSid(Sid[2]));
    ASSERT(RtlValidSid(Sid[3]));

    /* Phase 3: Calculate ACL Length */
    AclLength = sizeof(ACL);
    for (i = 0; i < 4; i++)
    {
        /* This is what MSDN says to do */
        AceLength = FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart);
        AceLength += SeLengthSid(Sid[i]);
        AclLength += AceLength;
    }
    AclLength += FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + SeLengthSid(SeAllAppPackagesSid);
    AclLength += FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + SeLengthSid(SeAllRestrictedAppPackagesSid);

    /* Phase 3: Allocate the ACL */
    Acl = ExAllocatePoolWithTag(PagedPool, AclLength, TAG_CMSD);
    if (!Acl) KeBugCheckEx(REGISTRY_ERROR, 11, 3, 0, 0);

    /* Phase 4: Create the ACL */
    Status = RtlCreateAcl(Acl, AclLength, ACL_REVISION);
    if (!NT_SUCCESS(Status)) KeBugCheckEx(REGISTRY_ERROR, 11, 4, Status, 0);

    /* Phase 5: Build the ACL */
    Status = RtlAddAccessAllowedAce(Acl, ACL_REVISION, KEY_ALL_ACCESS, Sid[2]);
    Status |= RtlAddAccessAllowedAce(Acl, ACL_REVISION, KEY_ALL_ACCESS, Sid[3]);
    Status |= RtlAddAccessAllowedAce(Acl, ACL_REVISION, KEY_READ, Sid[0]);
    Status |= RtlAddAccessAllowedAce(Acl, ACL_REVISION, KEY_READ, Sid[1]);
    Status |= RtlAddAccessAllowedAce(Acl, ACL_REVISION, KEY_READ, SeAllAppPackagesSid);
    Status |= RtlAddAccessAllowedAce(Acl, ACL_REVISION, KEY_READ, SeAllRestrictedAppPackagesSid);
    if (!NT_SUCCESS(Status)) KeBugCheckEx(REGISTRY_ERROR, 11, 5, Status, 0);

    /* Phase 5: Make the ACEs inheritable */
    Status = RtlGetAce(Acl, 0, (PVOID*)&AceHeader);
    ASSERT(NT_SUCCESS(Status));
    AceHeader->AceFlags |= CONTAINER_INHERIT_ACE;
    Status = RtlGetAce(Acl, 1, (PVOID*)&AceHeader);
    ASSERT(NT_SUCCESS(Status));
    AceHeader->AceFlags |= CONTAINER_INHERIT_ACE;
    Status = RtlGetAce(Acl, 2, (PVOID*)&AceHeader);
    ASSERT(NT_SUCCESS(Status));
    AceHeader->AceFlags |= CONTAINER_INHERIT_ACE;
    Status = RtlGetAce(Acl, 3, (PVOID*)&AceHeader);
    ASSERT(NT_SUCCESS(Status));
    AceHeader->AceFlags |= CONTAINER_INHERIT_ACE;
    Status = RtlGetAce(Acl, 4, (PVOID*)&AceHeader);
    ASSERT(NT_SUCCESS(Status));
    AceHeader->AceFlags |= CONTAINER_INHERIT_ACE;
    Status = RtlGetAce(Acl, 5, (PVOID*)&AceHeader);
    ASSERT(NT_SUCCESS(Status));
    AceHeader->AceFlags |= CONTAINER_INHERIT_ACE;

    /* Phase 6: Allocate the security descriptor and make space for the ACL */
    SecurityDescriptor = ExAllocatePoolWithTag(PagedPool,
                                               sizeof(SECURITY_DESCRIPTOR) +
                                               AclLength,
                                               TAG_CMSD);
    if (!SecurityDescriptor) KeBugCheckEx(REGISTRY_ERROR, 11, 6, 0, 0);

    /* Phase 6: Make a copy of the ACL */
    AclCopy = (PACL)((PISECURITY_DESCRIPTOR)SecurityDescriptor + 1);
    RtlCopyMemory(AclCopy, Acl, AclLength);

    /* Phase 7: Create the security descriptor */
    Status = RtlCreateSecurityDescriptor(SecurityDescriptor,
                                         SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(Status)) KeBugCheckEx(REGISTRY_ERROR, 11, 7, Status, 0);

    /* Phase 8: Set the ACL as a DACL */
    Status = RtlSetDaclSecurityDescriptor(SecurityDescriptor,
                                          TRUE,
                                          AclCopy,
                                          FALSE);
    if (!NT_SUCCESS(Status)) KeBugCheckEx(REGISTRY_ERROR, 11, 8, Status, 0);

    /* Free the SIDs and original ACL */
    for (i = 0; i < 4; i++) ExFreePoolWithTag(Sid[i], TAG_CMSD);
    ExFreePoolWithTag(Acl, TAG_CMSD);

    /* Return the security descriptor */
    return SecurityDescriptor;
}

VOID
NTAPI
CmpLockHiveSecurity(IN PHHIVE Hive)
{
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&((PCMHIVE)Hive)->SecurityLock);
}

VOID
NTAPI
CmpUnlockHiveSecurity(IN PHHIVE Hive)
{
    ExReleasePushLockExclusive(&((PCMHIVE)Hive)->SecurityLock);
    KeLeaveCriticalRegion();
}

static NTSTATUS
CmpGetSecurityCell(IN PHHIVE Hive,
                   IN HCELL_INDEX Cell,
                   OUT PCM_KEY_SECURITY *Security)
{
    LONG Size;

    *Security = NULL;
    if (Cell == HCELL_NIL)
        return STATUS_NO_SECURITY_ON_OBJECT;
    if (!HvIsCellAllocated(Hive, Cell))
        return STATUS_REGISTRY_CORRUPT;
    *Security = (PCM_KEY_SECURITY)HvGetCell(Hive, Cell);
    if (!*Security)
        return STATUS_INSUFFICIENT_RESOURCES;
    Size = HvGetCellSize(Hive, *Security);
    if (Size < sizeof(CM_KEY_SECURITY) ||
        (*Security)->Signature != CM_KEY_SECURITY_SIGNATURE ||
        (*Security)->ReferenceCount == 0 ||
        (*Security)->Flink == HCELL_NIL ||
        (*Security)->Blink == HCELL_NIL ||
        !HvIsCellAllocated(Hive, (*Security)->Flink) ||
        !HvIsCellAllocated(Hive, (*Security)->Blink) ||
        (*Security)->DescriptorLength > (ULONG)Size - FIELD_OFFSET(CM_KEY_SECURITY, Descriptor) ||
        !RtlValidRelativeSecurityDescriptor(&(*Security)->Descriptor,
                                            (*Security)->DescriptorLength, 0))
    {
        HvReleaseCell(Hive, Cell);
        *Security = NULL;
        return STATUS_REGISTRY_CORRUPT;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
CmpGetKeySecurityDescriptor(IN PCM_KEY_CONTROL_BLOCK Kcb,
                            OUT PSECURITY_DESCRIPTOR *Descriptor)
{
    PHHIVE Hive = Kcb->KeyHive;
    PCM_KEY_NODE Node;
    PCM_KEY_SECURITY Security = NULL;
    HCELL_INDEX Cell = HCELL_NIL;
    NTSTATUS Status;

    *Descriptor = NULL;
    CmpLockHiveSecurity(Hive);
    Node = (PCM_KEY_NODE)HvGetCell(Hive, Kcb->KeyCell);
    if (!Node)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    else
    {
        Cell = Node->Security;
        HvReleaseCell(Hive, Kcb->KeyCell);
        Status = CmpGetSecurityCell(Hive, Cell, &Security);
        if (NT_SUCCESS(Status))
        {
            *Descriptor = ExAllocatePoolWithTag(PagedPool, Security->DescriptorLength, TAG_CMSD);
            if (!*Descriptor)
                Status = STATUS_INSUFFICIENT_RESOURCES;
            else
                RtlCopyMemory(*Descriptor, &Security->Descriptor, Security->DescriptorLength);
            HvReleaseCell(Hive, Cell);
        }
    }
    CmpUnlockHiveSecurity(Hive);
    return Status;
}

NTSTATUS
CmpQuerySecurityDescriptor(IN PCM_KEY_CONTROL_BLOCK Kcb,
                           IN SECURITY_INFORMATION SecurityInformation,
                           OUT PSECURITY_DESCRIPTOR SecurityDescriptor,
                           IN OUT PULONG BufferLength)
{
    PSECURITY_DESCRIPTOR StoredDescriptor;
    NTSTATUS Status;

    if (!SecurityInformation)
        return STATUS_ACCESS_DENIED;
    Status = CmpGetKeySecurityDescriptor(Kcb, &StoredDescriptor);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = SeQuerySecurityDescriptorInfo(&SecurityInformation, SecurityDescriptor,
                                           BufferLength, &StoredDescriptor);
    ExFreePoolWithTag(StoredDescriptor, TAG_CMSD);
    return Status;
}

static NTSTATUS
CmpStoreKeySecurityDescriptor(IN PCM_KEY_CONTROL_BLOCK Kcb,
                              IN PSECURITY_DESCRIPTOR Descriptor,
                              IN ULONG Length)
{
    PHHIVE Hive = Kcb->KeyHive;
    PCM_KEY_NODE Node = NULL, RootNode;
    PCM_KEY_SECURITY Security = NULL, Anchor = NULL, Next = NULL, Old = NULL;
    HCELL_INDEX Cell = HCELL_NIL, OldCell = HCELL_NIL;
    HCELL_INDEX AnchorCell = HCELL_NIL, NextCell = HCELL_NIL;
    NTSTATUS Status = STATUS_INSUFFICIENT_RESOURCES;

    Node = (PCM_KEY_NODE)HvGetCell(Hive, Kcb->KeyCell);
    if (!Node)
        goto Done;
    OldCell = Node->Security;
    if (OldCell != HCELL_NIL)
    {
        Status = CmpGetSecurityCell(Hive, OldCell, &Old);
        if (!NT_SUCCESS(Status))
            goto Done;
        if (!HvMarkCellDirty(Hive, OldCell, FALSE) ||
            !HvMarkCellDirty(Hive, Old->Flink, FALSE) ||
            !HvMarkCellDirty(Hive, Old->Blink, FALSE))
        {
            Status = STATUS_NO_LOG_SPACE;
            goto Done;
        }
        AnchorCell = Old->ReferenceCount == 1 ? Old->Blink : OldCell;
        if (AnchorCell == OldCell && Old->ReferenceCount == 1)
            AnchorCell = HCELL_NIL;
    }
    else if (Hive->BaseBlock->RootCell != HCELL_NIL)
    {
        RootNode = (PCM_KEY_NODE)HvGetCell(Hive, Hive->BaseBlock->RootCell);
        if (!RootNode)
            goto Done;
        AnchorCell = RootNode->Security;
        HvReleaseCell(Hive, Hive->BaseBlock->RootCell);
    }
    if (AnchorCell != HCELL_NIL && HvGetCellType(AnchorCell) != HvGetCellType(Kcb->KeyCell))
        AnchorCell = HCELL_NIL;
    if (AnchorCell != HCELL_NIL)
    {
        Status = CmpGetSecurityCell(Hive, AnchorCell, &Anchor);
        if (!NT_SUCCESS(Status))
            goto Done;
        NextCell = Old && Old->ReferenceCount == 1 ? Old->Flink : Anchor->Flink;
        Status = CmpGetSecurityCell(Hive, NextCell, &Next);
        if (!NT_SUCCESS(Status))
            goto Done;
        if ((Old && Old->ReferenceCount == 1)
                ? (Next->Blink != OldCell || Anchor->Flink != OldCell)
                : (Next->Blink != AnchorCell))
        {
            Status = STATUS_REGISTRY_CORRUPT;
            goto Done;
        }
        if (!HvMarkCellDirty(Hive, AnchorCell, FALSE) ||
            !HvMarkCellDirty(Hive, NextCell, FALSE))
        {
            Status = STATUS_NO_LOG_SPACE;
            goto Done;
        }
    }
    if (!HvMarkCellDirty(Hive, Kcb->KeyCell, FALSE))
    {
        Status = STATUS_NO_LOG_SPACE;
        goto Done;
    }
    Cell = HvAllocateCell(Hive, FIELD_OFFSET(CM_KEY_SECURITY, Descriptor) + Length,
                           HvGetCellType(Kcb->KeyCell), HCELL_NIL);
    if (Cell == HCELL_NIL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    Security = (PCM_KEY_SECURITY)HvGetCell(Hive, Cell);
    if (!Security)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    Security->Signature = CM_KEY_SECURITY_SIGNATURE;
    Security->Reserved = 0;
    Security->ReferenceCount = 1;
    Security->DescriptorLength = Length;
    Security->Flink = Anchor ? NextCell : Cell;
    Security->Blink = Anchor ? AnchorCell : Cell;
    RtlCopyMemory(&Security->Descriptor, Descriptor, Length);
    if (Old)
    {
        HvReleaseCell(Hive, OldCell);
        Old = NULL;
        Status = CmpDereferenceSecurityCell(Hive, OldCell);
        if (!NT_SUCCESS(Status))
            goto Done;
    }
    if (Anchor)
    {
        Anchor->Flink = Cell;
        Next->Blink = Cell;
    }
    Node->Security = Cell;
    HvReleaseCell(Hive, Cell);
    Security = NULL;
    Cell = HCELL_NIL;
    Status = STATUS_SUCCESS;

Done:
    if (Security)
        HvReleaseCell(Hive, Cell);
    if (Cell != HCELL_NIL)
        HvFreeCell(Hive, Cell);
    if (Next)
        HvReleaseCell(Hive, NextCell);
    if (Anchor)
        HvReleaseCell(Hive, AnchorCell);
    if (Old)
        HvReleaseCell(Hive, OldCell);
    if (Node)
        HvReleaseCell(Hive, Kcb->KeyCell);
    return Status;
}

NTSTATUS
CmpAssignSecurityDescriptorLocked(IN PCM_KEY_CONTROL_BLOCK Kcb,
                            IN PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    PSECURITY_DESCRIPTOR Relative;
    SECURITY_DESCRIPTOR_CONTROL Control;
    ULONG Revision, Length;
    NTSTATUS Status;

    if (!SecurityDescriptor || !RtlValidSecurityDescriptor(SecurityDescriptor))
        return STATUS_INVALID_SECURITY_DESCR;
    Status = RtlGetControlSecurityDescriptor(SecurityDescriptor, &Control, &Revision);
    if (!NT_SUCCESS(Status))
        return Status;
    Length = RtlLengthSecurityDescriptor(SecurityDescriptor);
    if (Length > MAXLONG - FIELD_OFFSET(CM_KEY_SECURITY, Descriptor))
        return STATUS_INVALID_SECURITY_DESCR;
    Relative = ExAllocatePoolWithTag(PagedPool, Length, TAG_CMSD);
    if (!Relative)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (Control & SE_SELF_RELATIVE)
    {
        RtlCopyMemory(Relative, SecurityDescriptor, Length);
        Status = STATUS_SUCCESS;
    }
    else
    {
        Status = RtlAbsoluteToSelfRelativeSD(SecurityDescriptor, Relative, &Length);
    }
    if (NT_SUCCESS(Status))
    {
        CmpLockHiveSecurity(Kcb->KeyHive);
        Status = CmpStoreKeySecurityDescriptor(Kcb, Relative, Length);
        CmpUnlockHiveSecurity(Kcb->KeyHive);
    }
    ExFreePoolWithTag(Relative, TAG_CMSD);
    return Status;
}

NTSTATUS
CmpAssignSecurityDescriptor(IN PCM_KEY_CONTROL_BLOCK Kcb,
                            IN PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    NTSTATUS Status;

    CmpLockHiveFlusherShared((PCMHIVE)Kcb->KeyHive);
    Status = CmpAssignSecurityDescriptorLocked(Kcb, SecurityDescriptor);
    CmpUnlockHiveFlusher((PCMHIVE)Kcb->KeyHive);
    return Status;
}

NTSTATUS
CmpSetSecurityDescriptor(IN PCM_KEY_CONTROL_BLOCK Kcb,
                         IN PSECURITY_INFORMATION SecurityInformation,
                         IN PSECURITY_DESCRIPTOR SecurityDescriptor,
                         IN POOL_TYPE PoolType,
                         IN PGENERIC_MAPPING GenericMapping)
{
    PSECURITY_DESCRIPTOR StoredDescriptor, ModifiedDescriptor;
    NTSTATUS Status;

    Status = CmpGetKeySecurityDescriptor(Kcb, &StoredDescriptor);
    if (!NT_SUCCESS(Status))
        return Status;
    ModifiedDescriptor = StoredDescriptor;
    Status = SeSetSecurityDescriptorInfoEx(NULL, SecurityInformation,
                                           SecurityDescriptor, &ModifiedDescriptor,
                                           0, PoolType, GenericMapping);
    if (NT_SUCCESS(Status))
        Status = CmpAssignSecurityDescriptor(Kcb, ModifiedDescriptor);
    if (ModifiedDescriptor != StoredDescriptor)
        ExFreePool(ModifiedDescriptor);
    ExFreePoolWithTag(StoredDescriptor, TAG_CMSD);
    return Status;
}

NTSTATUS
NTAPI
CmpAppHiveAccessMode(PCMHIVE Hive,
                     ACCESS_MASK DesiredAccess,
                     KPROCESSOR_MODE AccessMode,
                     KPROCESSOR_MODE *EffectiveMode)
{
    *EffectiveMode = AccessMode;
    if (!(Hive->Flags & CMHIVE_FLAG_APPLICATION_HIVE)) return STATUS_SUCCESS;
    if ((DesiredAccess & ACCESS_SYSTEM_SECURITY) &&
        !SeSinglePrivilegeCheck(SeSecurityPrivilege, AccessMode))
        return STATUS_PRIVILEGE_NOT_HELD;
    *EffectiveMode = KernelMode;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CmpSecurityMethod(IN PVOID ObjectBody,
                  IN SECURITY_OPERATION_CODE OperationCode,
                  IN PSECURITY_INFORMATION SecurityInformation,
                  IN OUT PSECURITY_DESCRIPTOR SecurityDescriptor,
                  IN OUT PULONG BufferLength,
                  IN OUT PSECURITY_DESCRIPTOR *OldSecurityDescriptor,
                  IN POOL_TYPE PoolType,
                  IN PGENERIC_MAPPING GenericMapping,
                  IN KPROCESSOR_MODE AccessMode)
{
    PCM_KEY_CONTROL_BLOCK Kcb;
    NTSTATUS Status = STATUS_SUCCESS;

    DBG_UNREFERENCED_PARAMETER(OldSecurityDescriptor);
    DBG_UNREFERENCED_PARAMETER(GenericMapping);
    UNREFERENCED_PARAMETER(AccessMode);

    Kcb = ((PCM_KEY_BODY)ObjectBody)->KeyControlBlock;

    if (OperationCode == SetSecurityDescriptor)
    {
        Status = CmpTransPrepareWrite((PCM_KEY_BODY)ObjectBody);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    /* Acquire the hive lock */
    CmpLockRegistry();

    /* Acquire the KCB lock */
    if (OperationCode == QuerySecurityDescriptor)
    {
        /* Avoid recursive locking if somebody already holds it */
        if (!((PCM_KEY_BODY)ObjectBody)->KcbLocked)
        {
            CmpAcquireKcbLockShared(Kcb);
        }
    }
    else
    {
        ASSERT(!((PCM_KEY_BODY)ObjectBody)->KcbLocked);
        CmpAcquireKcbLockExclusive(Kcb);
    }

    /* Don't touch deleted keys */
    if (Kcb->Delete)
    {
        /* Release the KCB lock */
        if (!((PCM_KEY_BODY)ObjectBody)->KcbLocked)
        {
            CmpReleaseKcbLock(Kcb);
        }

        /* Release the hive lock */
        CmpUnlockRegistry();
        return STATUS_KEY_DELETED;
    }

    switch (OperationCode)
    {
        case SetSecurityDescriptor:
            if (((PCMHIVE)Kcb->KeyHive)->Flags & CMHIVE_FLAG_APPLICATION_HIVE)
            {
                Status = STATUS_ACCESS_DENIED;
                break;
            }
            DPRINT("Set security descriptor\n");
            ASSERT((PoolType == PagedPool) || ((PoolType & 1) == NonPagedPool));
            Status = CmpSetSecurityDescriptor(Kcb,
                                              SecurityInformation,
                                              SecurityDescriptor,
                                              PoolType,
                                              GenericMapping);
            if (NT_SUCCESS(Status))
            {
                CmpReportNotify(Kcb,
                                Kcb->KeyHive,
                                Kcb->KeyCell,
                                REG_NOTIFY_CHANGE_ATTRIBUTES | REG_NOTIFY_CHANGE_SECURITY);
            }
            break;

        case QuerySecurityDescriptor:
            DPRINT("Query security descriptor\n");
            Status = CmpQuerySecurityDescriptor(Kcb,
                                                *SecurityInformation,
                                                SecurityDescriptor,
                                                BufferLength);
            break;

        case DeleteSecurityDescriptor:
            DPRINT("Delete security descriptor\n");
            /* HACK */
            break;

        case AssignSecurityDescriptor:
            DPRINT("Assign security descriptor\n");
            Status = CmpAssignSecurityDescriptor(Kcb,
                                                 SecurityDescriptor);
            break;

        default:
            KeBugCheckEx(SECURITY_SYSTEM, 0, STATUS_INVALID_PARAMETER, 0, 0);
    }

    /*
     * Release the KCB lock, but only if we locked it ourselves and
     * nobody else was locking it by themselves.
     */
    if (!((PCM_KEY_BODY)ObjectBody)->KcbLocked)
    {
        CmpReleaseKcbLock(Kcb);
    }

    /* Release the hive lock */
    CmpUnlockRegistry();

    return Status;
}
