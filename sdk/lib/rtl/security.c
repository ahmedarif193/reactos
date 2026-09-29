/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS system libraries
 * FILE:              lib/rtl/security.c
 * PURPOSE:           Security related functions and Security Objects
 * PROGRAMMER:        Eric Kohl
 */

/* INCLUDES *******************************************************************/

#include <rtl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

#include <reactos/acltransform.h>

static NTSTATUS
RtlpSecurityTransformAcl(PACL Source, BOOLEAN Parent, BOOLEAN ClearInherited,
                        INT Filter, BOOLEAN Container, LPGUID *Types, ULONG TypeCount,
                        PSID Owner, PSID Group, PGENERIC_MAPPING Mapping, PACL *Result)
{
    RTL_SECURITY_ACL_BUFFER Buffer;
    PACE_HEADER Ace;
    ULONG Index, Pass;
    NTSTATUS Status = STATUS_SUCCESS;

    *Result = NULL;
    if (!Source) return STATUS_SUCCESS;
    if (!RtlValidAcl(Source)) return STATUS_INVALID_ACL;
    RtlZeroMemory(&Buffer, sizeof(Buffer));
    for (Pass = 0; Pass < 2; ++Pass)
    {
        Buffer.Length = sizeof(ACL);
        Buffer.Count = 0;
        Buffer.Revision = Source->AclRevision;
        for (Index = 0; Index < Source->AceCount; ++Index)
        {
            Status = RtlGetAce(Source, Index, (PVOID *)&Ace);
            if (!NT_SUCCESS(Status)) goto Done;
            if (Filter >= 0 && !!(Ace->AceFlags & INHERITED_ACE) != Filter) continue;
            Status = RtlpSecurityTransformAce(&Buffer, Ace, Parent, ClearInherited,
                                              Container, Types, TypeCount, Owner, Group, Mapping);
            if (!NT_SUCCESS(Status)) goto Done;
        }
        if (!Pass)
        {
            Buffer.Capacity = Buffer.Length;
            Buffer.Acl = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, Buffer.Length);
            if (!Buffer.Acl)
            {
                Status = STATUS_NO_MEMORY;
                goto Done;
            }
        }
    }
    Buffer.Acl->AclRevision = Buffer.Revision;
    Buffer.Acl->AclSize = (USHORT)Buffer.Length;
    Buffer.Acl->AceCount = (USHORT)Buffer.Count;
    *Result = Buffer.Acl;
    return STATUS_SUCCESS;
Done:
    RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer.Acl);
    return Status;
}

static NTSTATUS
RtlpSecurityMergeAcls(PACL First, PACL Second, PACL *Result)
{
    ACL_SIZE_INFORMATION FirstSize = {0}, SecondSize = {0};
    ULONG Length;
    PACL Acl;
    NTSTATUS Status;

    *Result = NULL;
    if (!First && !Second) return STATUS_SUCCESS;
    FirstSize.AclBytesInUse = SecondSize.AclBytesInUse = sizeof(ACL);
    if (First)
    {
        Status = RtlQueryInformationAcl(First, &FirstSize, sizeof(FirstSize), AclSizeInformation);
        if (!NT_SUCCESS(Status)) return Status;
    }
    if (Second)
    {
        Status = RtlQueryInformationAcl(Second, &SecondSize, sizeof(SecondSize), AclSizeInformation);
        if (!NT_SUCCESS(Status)) return Status;
    }
    Length = FirstSize.AclBytesInUse + SecondSize.AclBytesInUse - sizeof(ACL);
    if (Length > MAXUSHORT || FirstSize.AceCount + SecondSize.AceCount > MAXUSHORT)
        return STATUS_ALLOTTED_SPACE_EXCEEDED;
    Acl = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, Length);
    if (!Acl) return STATUS_NO_MEMORY;
    Acl->AclRevision = max(First ? First->AclRevision : ACL_REVISION,
                           Second ? Second->AclRevision : ACL_REVISION);
    Acl->AclSize = (USHORT)Length;
    Acl->AceCount = (USHORT)(FirstSize.AceCount + SecondSize.AceCount);
    if (First) RtlCopyMemory(Acl + 1, First + 1, FirstSize.AclBytesInUse - sizeof(ACL));
    if (Second) RtlCopyMemory((PUCHAR)Acl + FirstSize.AclBytesInUse,
                              Second + 1, SecondSize.AclBytesInUse - sizeof(ACL));
    *Result = Acl;
    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpSecurityReadDescriptor(PSECURITY_DESCRIPTOR Source, PISECURITY_DESCRIPTOR Dest)
{
    BOOLEAN Present, Defaulted;
    ULONG Revision;
    NTSTATUS Status;

    RtlCreateSecurityDescriptor(Dest, SECURITY_DESCRIPTOR_REVISION);
    if (!Source) return STATUS_SUCCESS;
    if (!RtlValidSecurityDescriptor(Source)) return STATUS_INVALID_SECURITY_DESCR;
    Status = RtlGetControlSecurityDescriptor(Source, &Dest->Control, &Revision);
    if (!NT_SUCCESS(Status)) return Status;
    Dest->Control &= ~SE_SELF_RELATIVE;
    Dest->Sbz1 = ((PISECURITY_DESCRIPTOR)Source)->Sbz1;
    RtlGetOwnerSecurityDescriptor(Source, &Dest->Owner, &Defaulted);
    RtlGetGroupSecurityDescriptor(Source, &Dest->Group, &Defaulted);
    RtlGetDaclSecurityDescriptor(Source, &Present, &Dest->Dacl, &Defaulted);
    RtlGetSaclSecurityDescriptor(Source, &Present, &Dest->Sacl, &Defaulted);
    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpSecurityPublishDescriptor(PSECURITY_DESCRIPTOR Source, PSECURITY_DESCRIPTOR *Dest)
{
    ULONG Size = 0;
    PSECURITY_DESCRIPTOR Buffer;
    NTSTATUS Status;

    Status = RtlMakeSelfRelativeSD(Source, NULL, &Size);
    if (Status != STATUS_BUFFER_TOO_SMALL) return Status;
    Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, Size);
    if (!Buffer) return STATUS_NO_MEMORY;
    Status = RtlMakeSelfRelativeSD(Source, Buffer, &Size);
    if (!NT_SUCCESS(Status)) RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
    else *Dest = Buffer;
    return Status;
}

static NTSTATUS
RtlpSecurityQueryToken(HANDLE Token, TOKEN_INFORMATION_CLASS Class, PVOID *Result)
{
    ULONG Size = 0;
    NTSTATUS Status;

    *Result = NULL;
    if (!Token) return STATUS_NO_TOKEN;
    Status = NtQueryInformationToken(Token, Class, NULL, 0, &Size);
    if (Status != STATUS_BUFFER_TOO_SMALL)
        return NT_SUCCESS(Status) ? STATUS_INVALID_PARAMETER : Status;
    if (!Size) return STATUS_INVALID_PARAMETER;
    *Result = RtlAllocateHeap(RtlGetProcessHeap(), 0, Size);
    if (!*Result) return STATUS_NO_MEMORY;
    Status = NtQueryInformationToken(Token, Class, *Result, Size, &Size);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, *Result);
        *Result = NULL;
    }
    return Status;
}

static NTSTATUS
RtlpSecurityValidateOwner(HANDLE Token, PSID Owner)
{
    PTOKEN_USER User = NULL;
    PTOKEN_GROUPS Groups = NULL;
    NTSTATUS Status;
    ULONG Index;

    Status = RtlpSecurityQueryToken(Token, TokenUser, (PVOID *)&User);
    if (!NT_SUCCESS(Status)) return Status;
    if (RtlEqualSid(User->User.Sid, Owner)) goto Done;
    Status = RtlpSecurityQueryToken(Token, TokenGroups, (PVOID *)&Groups);
    if (!NT_SUCCESS(Status)) goto Done;
    Status = STATUS_INVALID_OWNER;
    for (Index = 0; Index < Groups->GroupCount; ++Index)
        if ((Groups->Groups[Index].Attributes & (SE_GROUP_OWNER | SE_GROUP_USE_FOR_DENY_ONLY)) == SE_GROUP_OWNER &&
            RtlEqualSid(Groups->Groups[Index].Sid, Owner))
        {
            Status = STATUS_SUCCESS;
            break;
        }
Done:
    RtlFreeHeap(RtlGetProcessHeap(), 0, Groups);
    RtlFreeHeap(RtlGetProcessHeap(), 0, User);
    return Status;
}

static NTSTATUS
RtlpSecurityCheckPrivilege(HANDLE Token)
{
    PRIVILEGE_SET Privileges = {0};
    BOOLEAN Result;
    NTSTATUS Status;

    if (!Token) return STATUS_NO_TOKEN;
    Privileges.PrivilegeCount = 1;
    Privileges.Control = PRIVILEGE_SET_ALL_NECESSARY;
    Privileges.Privilege[0].Luid.LowPart = SE_SECURITY_PRIVILEGE;
    Status = NtPrivilegeCheck(Token, &Privileges, &Result);
    if (!NT_SUCCESS(Status)) return Status;
    return Result ? STATUS_SUCCESS : STATUS_PRIVILEGE_NOT_HELD;
}



static NTSTATUS
RtlpSecurityHasOwnerRights(PACL Acl, PBOOLEAN Found)
{
    static const SID_IDENTIFIER_AUTHORITY CreatorAuthority = SECURITY_CREATOR_SID_AUTHORITY;
    RTL_SECURITY_ACE_VIEW View;
    PACE_HEADER Ace;
    PISID Sid;
    ULONG Index;
    NTSTATUS Status;

    *Found = FALSE;
    if (!Acl) return STATUS_SUCCESS;
    if (!RtlValidAcl(Acl)) return STATUS_INVALID_ACL;
    for (Index = 0; Index < Acl->AceCount; ++Index)
    {
        Status = RtlGetAce(Acl, Index, (PVOID *)&Ace);
        if (!NT_SUCCESS(Status)) return Status;
        Status = RtlpSecurityAceView(Ace, &View);
        if (!NT_SUCCESS(Status)) return Status;
        Sid = View.Sid;
        if (Sid->SubAuthorityCount == 1 &&
            Sid->SubAuthority[0] == SECURITY_CREATOR_OWNER_RIGHTS_RID &&
            !memcmp(&Sid->IdentifierAuthority, &CreatorAuthority, sizeof(CreatorAuthority)))
        {
            *Found = TRUE;
            break;
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpSecurityCheckOwnerRestriction(PACL ParentAcl, PACL DefaultAcl, HANDLE Token)
{
    PSECURITY_DESCRIPTOR TokenDescriptor = NULL;
    SECURITY_DESCRIPTOR Descriptor;
    HANDLE QueryToken = Token, Duplicate = NULL;
    ULONG Length = 0;
    BOOLEAN Found;
    NTSTATUS Status;

    Status = RtlpSecurityHasOwnerRights(ParentAcl, &Found);
    if (!NT_SUCCESS(Status)) return Status;
    if (Found) return STATUS_NOT_IMPLEMENTED;
    Status = RtlpSecurityHasOwnerRights(DefaultAcl, &Found);
    if (!NT_SUCCESS(Status)) return Status;
    if (Found) return STATUS_NOT_IMPLEMENTED;
    if (!Token) return STATUS_SUCCESS;
    Status = NtQuerySecurityObject(Token, DACL_SECURITY_INFORMATION, NULL, 0, &Length);
    if (Status == STATUS_ACCESS_DENIED)
    {
        Status = NtDuplicateObject(NtCurrentProcess(), Token, NtCurrentProcess(),
                                   &Duplicate, READ_CONTROL, 0, 0);
        if (!NT_SUCCESS(Status))
        {
            if (Status == STATUS_ACCESS_DENIED) Status = STATUS_NOT_IMPLEMENTED;
            goto Done;
        }
        QueryToken = Duplicate;
        Status = NtQuerySecurityObject(QueryToken, DACL_SECURITY_INFORMATION, NULL, 0, &Length);
    }
    if (Status == STATUS_ACCESS_DENIED)
    {
        Status = STATUS_NOT_IMPLEMENTED;
        goto Done;
    }
    if (Status != STATUS_BUFFER_TOO_SMALL)
    {
        if (NT_SUCCESS(Status)) Status = STATUS_INVALID_SECURITY_DESCR;
        goto Done;
    }
    if (Length < sizeof(SECURITY_DESCRIPTOR_RELATIVE))
    {
        Status = STATUS_INVALID_SECURITY_DESCR;
        goto Done;
    }
    TokenDescriptor = RtlAllocateHeap(RtlGetProcessHeap(), 0, Length);
    if (!TokenDescriptor)
    {
        Status = STATUS_NO_MEMORY;
        goto Done;
    }
    Status = NtQuerySecurityObject(QueryToken, DACL_SECURITY_INFORMATION, TokenDescriptor, Length, &Length);
    if (Status == STATUS_ACCESS_DENIED) Status = STATUS_NOT_IMPLEMENTED;
    if (NT_SUCCESS(Status))
        Status = RtlpSecurityReadDescriptor(TokenDescriptor, &Descriptor);
    if (NT_SUCCESS(Status))
    {
        Status = RtlpSecurityHasOwnerRights(Descriptor.Dacl, &Found);
        if (NT_SUCCESS(Status) && Found) Status = STATUS_NOT_IMPLEMENTED;
    }
Done:
    RtlFreeHeap(RtlGetProcessHeap(), 0, TokenDescriptor);
    if (Duplicate) NtClose(Duplicate);
    return Status;
}


static NTSTATUS
RtlpSecuritySetAcl(PACL Current, PACL Modification,
                   SECURITY_DESCRIPTOR_CONTROL CurrentControl,
                   SECURITY_DESCRIPTOR_CONTROL ModificationControl,
                   BOOLEAN Sacl, BOOLEAN Auto, PSID Owner, PSID Group,
                   PGENERIC_MAPPING Mapping, PACL *Result)
{
    SECURITY_DESCRIPTOR_CONTROL Protected = Sacl ? SE_SACL_PROTECTED : SE_DACL_PROTECTED;
    PACL ExplicitAcl = NULL, InheritedAcl = NULL;
    INT Filter = -1;
    NTSTATUS Status;

    *Result = NULL;
    if (Auto && !(CurrentControl & Protected) && !(ModificationControl & Protected))
    {
        Filter = 0;
        Status = RtlpSecurityTransformAcl(Current, FALSE, FALSE, 1, TRUE, NULL, 0,
                                          Owner, Group, Mapping, &InheritedAcl);
        if (!NT_SUCCESS(Status)) return Status;
        if (InheritedAcl && !InheritedAcl->AceCount)
        {
            RtlFreeHeap(RtlGetProcessHeap(), 0, InheritedAcl);
            InheritedAcl = NULL;
        }
    }
    Status = RtlpSecurityTransformAcl(Modification, FALSE,
                                      Auto && (ModificationControl & Protected), Filter,
                                      TRUE, NULL, 0, Owner, Group, Mapping, &ExplicitAcl);
    if (NT_SUCCESS(Status)) Status = RtlpSecurityMergeAcls(ExplicitAcl, InheritedAcl, Result);
    RtlFreeHeap(RtlGetProcessHeap(), 0, ExplicitAcl);
    RtlFreeHeap(RtlGetProcessHeap(), 0, InheritedAcl);
    return Status;
}

NTSTATUS
NTAPI
RtlpSetSecurityObject(IN PVOID Object OPTIONAL,
                      IN SECURITY_INFORMATION SecurityInformation,
                      IN PSECURITY_DESCRIPTOR ModificationDescriptor,
                      IN OUT PSECURITY_DESCRIPTOR *ObjectsSecurityDescriptor,
                      IN ULONG AutoInheritFlags,
                      IN ULONG PoolType,
                      IN PGENERIC_MAPPING GenericMapping,
                      IN HANDLE Token OPTIONAL)
{
    SECURITY_DESCRIPTOR Current, Modification, Descriptor;
    PSECURITY_DESCRIPTOR Result = NULL;
    PACL Dacl = NULL, Sacl = NULL;
    SECURITY_DESCRIPTOR_CONTROL Bits;
    BOOLEAN SetDacl, SetSacl, Restricted;
    NTSTATUS Status;
    ULONG SupportedFlags = SEF_DACL_AUTO_INHERIT | SEF_SACL_AUTO_INHERIT |
                           SEF_AVOID_PRIVILEGE_CHECK | SEF_AVOID_OWNER_CHECK |
                           SEF_AVOID_OWNER_RESTRICTION;
    SECURITY_INFORMATION SupportedInformation = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
        DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION |
        PROTECTED_DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION |
        PROTECTED_SACL_SECURITY_INFORMATION | UNPROTECTED_SACL_SECURITY_INFORMATION;

    UNREFERENCED_PARAMETER(Object);
    UNREFERENCED_PARAMETER(PoolType);
    if (!ObjectsSecurityDescriptor || !*ObjectsSecurityDescriptor || !ModificationDescriptor ||
        !GenericMapping) return STATUS_INVALID_PARAMETER;
    if ((AutoInheritFlags & ~SupportedFlags) || (SecurityInformation & ~SupportedInformation))
        return STATUS_NOT_IMPLEMENTED;
    if ((SecurityInformation & (PROTECTED_DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION)) &&
        !(SecurityInformation & DACL_SECURITY_INFORMATION)) return STATUS_NOT_IMPLEMENTED;
    if ((SecurityInformation & (PROTECTED_SACL_SECURITY_INFORMATION | UNPROTECTED_SACL_SECURITY_INFORMATION)) &&
        !(SecurityInformation & SACL_SECURITY_INFORMATION)) return STATUS_NOT_IMPLEMENTED;
    Status = RtlpSecurityReadDescriptor(*ObjectsSecurityDescriptor, &Current);
    if (!NT_SUCCESS(Status)) return Status;
    Status = RtlpSecurityReadDescriptor(ModificationDescriptor, &Modification);
    if (!NT_SUCCESS(Status)) return Status;
    if ((SecurityInformation & DACL_SECURITY_INFORMATION) &&
        ((Current.Control | Modification.Control) & SE_SERVER_SECURITY)) return STATUS_NOT_IMPLEMENTED;
    Descriptor = Current;
    if (SecurityInformation & OWNER_SECURITY_INFORMATION)
    {
        Descriptor.Owner = Modification.Owner;
        if (!Descriptor.Owner || !RtlValidSid(Descriptor.Owner)) return STATUS_INVALID_OWNER;
        if (!(AutoInheritFlags & (SEF_AVOID_OWNER_CHECK | SEF_AVOID_PRIVILEGE_CHECK)))
        {
            Status = RtlpSecurityValidateOwner(Token, Descriptor.Owner);
            if (!NT_SUCCESS(Status)) return Status;
        }
        Descriptor.Control = (Descriptor.Control & ~SE_OWNER_DEFAULTED) |
                             (Modification.Control & SE_OWNER_DEFAULTED);
    }
    if (SecurityInformation & GROUP_SECURITY_INFORMATION)
    {
        Descriptor.Group = Modification.Group;
        if (!Descriptor.Group || !RtlValidSid(Descriptor.Group)) return STATUS_INVALID_PRIMARY_GROUP;
        Descriptor.Control = (Descriptor.Control & ~SE_GROUP_DEFAULTED) |
                             (Modification.Control & SE_GROUP_DEFAULTED);
    }
    SetDacl = !!(SecurityInformation & (DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION |
                                       UNPROTECTED_DACL_SECURITY_INFORMATION));
    SetSacl = !!(SecurityInformation & (SACL_SECURITY_INFORMATION | PROTECTED_SACL_SECURITY_INFORMATION |
                                       UNPROTECTED_SACL_SECURITY_INFORMATION));
    if (!(SecurityInformation & DACL_SECURITY_INFORMATION))
    {
        Modification.Dacl = Current.Dacl;
        Modification.Control = (Modification.Control & ~(SE_DACL_PRESENT | SE_DACL_PROTECTED)) |
                               (Current.Control & (SE_DACL_PRESENT | SE_DACL_PROTECTED));
    }
    if (!(SecurityInformation & SACL_SECURITY_INFORMATION))
    {
        Modification.Sacl = Current.Sacl;
        Modification.Control = (Modification.Control & ~(SE_SACL_PRESENT | SE_SACL_PROTECTED)) |
                               (Current.Control & (SE_SACL_PRESENT | SE_SACL_PROTECTED));
    }
    if (SecurityInformation & PROTECTED_DACL_SECURITY_INFORMATION)
        Modification.Control |= SE_DACL_PROTECTED;
    if (SecurityInformation & UNPROTECTED_DACL_SECURITY_INFORMATION)
        Modification.Control &= ~SE_DACL_PROTECTED;
    if (SecurityInformation & PROTECTED_SACL_SECURITY_INFORMATION)
        Modification.Control |= SE_SACL_PROTECTED;
    if (SecurityInformation & UNPROTECTED_SACL_SECURITY_INFORMATION)
        Modification.Control &= ~SE_SACL_PROTECTED;
    if (SetDacl && !(AutoInheritFlags & SEF_AVOID_OWNER_RESTRICTION))
    {
        Status = RtlpSecurityHasOwnerRights(Current.Dacl, &Restricted);
        if (!NT_SUCCESS(Status)) return Status;
        if (Restricted) return STATUS_NOT_IMPLEMENTED;
    }
    if (SetDacl)
    {
        Status = RtlpSecuritySetAcl(Current.Dacl, Modification.Dacl, Current.Control,
                                    Modification.Control, FALSE,
                                    !!(AutoInheritFlags & SEF_DACL_AUTO_INHERIT),
                                    Descriptor.Owner, Descriptor.Group, GenericMapping, &Dacl);
        if (!NT_SUCCESS(Status)) goto Done;
        Descriptor.Dacl = Dacl;
        Bits = SE_DACL_PRESENT | SE_DACL_DEFAULTED | SE_DACL_PROTECTED |
               SE_DACL_AUTO_INHERITED | SE_DACL_AUTO_INHERIT_REQ;
        Descriptor.Control = (Descriptor.Control & ~Bits) | SE_DACL_PRESENT |
                             (Modification.Control & SE_DACL_PROTECTED);
        if (AutoInheritFlags & SEF_DACL_AUTO_INHERIT) Descriptor.Control |= SE_DACL_AUTO_INHERITED;
        else Descriptor.Control |= Modification.Control & SE_DACL_AUTO_INHERITED;
    }
    if (SetSacl)
    {
        Status = RtlpSecuritySetAcl(Current.Sacl, Modification.Sacl, Current.Control,
                                    Modification.Control, TRUE,
                                    !!(AutoInheritFlags & SEF_SACL_AUTO_INHERIT),
                                    Descriptor.Owner, Descriptor.Group, GenericMapping, &Sacl);
        if (!NT_SUCCESS(Status)) goto Done;
        Descriptor.Sacl = Sacl;
        Bits = SE_SACL_PRESENT | SE_SACL_DEFAULTED | SE_SACL_PROTECTED |
               SE_SACL_AUTO_INHERITED | SE_SACL_AUTO_INHERIT_REQ;
        Descriptor.Control = (Descriptor.Control & ~Bits) | SE_SACL_PRESENT |
                             (Modification.Control & SE_SACL_PROTECTED);
        if (AutoInheritFlags & SEF_SACL_AUTO_INHERIT) Descriptor.Control |= SE_SACL_AUTO_INHERITED;
        else Descriptor.Control |= Modification.Control & SE_SACL_AUTO_INHERITED;
    }
    Status = RtlpSecurityPublishDescriptor(&Descriptor, &Result);
    if (NT_SUCCESS(Status))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, *ObjectsSecurityDescriptor);
        *ObjectsSecurityDescriptor = Result;
    }
Done:
    RtlFreeHeap(RtlGetProcessHeap(), 0, Dacl);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Sacl);
    return Status;
}


static NTSTATUS
RtlpSecurityNewAcl(PACL Parent, PACL Creator, PACL DefaultAcl,
                   SECURITY_DESCRIPTOR_CONTROL CreatorControl,
                   BOOLEAN Sacl, BOOLEAN Container, ULONG Flags,
                   LPGUID *Types, ULONG TypeCount, PSID Owner, PSID Group,
                   PGENERIC_MAPPING Mapping, PACL *Result,
                   PSECURITY_DESCRIPTOR_CONTROL ResultControl)
{
    SECURITY_DESCRIPTOR_CONTROL Present = Sacl ? SE_SACL_PRESENT : SE_DACL_PRESENT;
    SECURITY_DESCRIPTOR_CONTROL Defaulted = Sacl ? SE_SACL_DEFAULTED : SE_DACL_DEFAULTED;
    SECURITY_DESCRIPTOR_CONTROL Protected = Sacl ? SE_SACL_PROTECTED : SE_DACL_PROTECTED;
    SECURITY_DESCRIPTOR_CONTROL Inherited = Sacl ? SE_SACL_AUTO_INHERITED : SE_DACL_AUTO_INHERITED;
    BOOLEAN Auto = !!(Flags & (Sacl ? SEF_SACL_AUTO_INHERIT : SEF_DACL_AUTO_INHERIT));
    BOOLEAN CreatorPresent = !!(CreatorControl & Present);
    BOOLEAN UseDefault = FALSE;
    BOOLEAN ParentUsed = FALSE;
    PACL ParentAcl = NULL, CreatorAcl = NULL, Selected;
    NTSTATUS Status;

    *Result = NULL;
    if (Auto) *ResultControl |= Inherited;
    if (CreatorControl & Protected) *ResultControl |= Protected;
    if (!(CreatorControl & Protected) && Parent &&
        (Auto || !CreatorPresent ||
         (Flags & SEF_DEFAULT_DESCRIPTOR_FOR_OBJECT)))
    {
        Status = RtlpSecurityTransformAcl(Parent, TRUE, FALSE, -1, Container,
                                          Types, TypeCount, Owner, Group, Mapping, &ParentAcl);
        if (!NT_SUCCESS(Status)) return Status;
        if (!ParentAcl->AceCount)
        {
            RtlFreeHeap(RtlGetProcessHeap(), 0, ParentAcl);
            ParentAcl = NULL;
        }
    }
    if (ParentAcl && (Flags & SEF_DEFAULT_DESCRIPTOR_FOR_OBJECT))
    {
        Status = STATUS_NOT_IMPLEMENTED;
        goto Done;
    }
    Selected = CreatorPresent ? Creator : NULL;
    if (ParentAcl && !CreatorPresent)
    {
        Selected = NULL;
        ParentUsed = TRUE;
    }
    else if (ParentAcl && Auto)
    {
        ParentUsed = TRUE;
    }
    if (!CreatorPresent && !ParentUsed && !Sacl)
    {
        Selected = DefaultAcl;
        UseDefault = TRUE;
    }
    if (Selected)
    {
        Status = RtlpSecurityTransformAcl(Selected, FALSE,
                                          !!(CreatorControl & Protected),
                                          UseDefault || (CreatorControl & Protected) ? -1 : 0,
                                          Container, Types, TypeCount, Owner, Group, Mapping,
                                          &CreatorAcl);
        if (!NT_SUCCESS(Status)) goto Done;
    }
    Status = RtlpSecurityMergeAcls(CreatorAcl, ParentUsed ? ParentAcl : NULL, Result);
    if (!NT_SUCCESS(Status)) goto Done;
    if (CreatorPresent || ParentUsed || DefaultAcl) *ResultControl |= Present;
    if (!ParentUsed && (UseDefault || (CreatorControl & Defaulted)))
        *ResultControl |= Defaulted;
Done:
    RtlFreeHeap(RtlGetProcessHeap(), 0, CreatorAcl);
    RtlFreeHeap(RtlGetProcessHeap(), 0, ParentAcl);
    return Status;
}

NTSTATUS
NTAPI
RtlpNewSecurityObject(IN PSECURITY_DESCRIPTOR ParentDescriptor,
                      IN PSECURITY_DESCRIPTOR CreatorDescriptor,
                      OUT PSECURITY_DESCRIPTOR *NewDescriptor,
                      IN LPGUID *ObjectTypes,
                      IN ULONG GuidCount,
                      IN BOOLEAN IsDirectoryObject,
                      IN ULONG AutoInheritFlags,
                      IN HANDLE Token,
                      IN PGENERIC_MAPPING GenericMapping)
{
    SECURITY_DESCRIPTOR Parent, Creator, Descriptor;
    TOKEN_STATISTICS Statistics;
    PTOKEN_OWNER TokenOwnerInfo = NULL;
    PTOKEN_PRIMARY_GROUP TokenGroupInfo = NULL;
    PTOKEN_DEFAULT_DACL TokenDaclInfo = NULL;
    ULONG Length;
    NTSTATUS Status;
    ULONG SupportedFlags = SEF_DACL_AUTO_INHERIT | SEF_SACL_AUTO_INHERIT |
                           SEF_DEFAULT_DESCRIPTOR_FOR_OBJECT | SEF_AVOID_PRIVILEGE_CHECK |
                           SEF_AVOID_OWNER_CHECK | SEF_DEFAULT_OWNER_FROM_PARENT |
                           SEF_DEFAULT_GROUP_FROM_PARENT | SEF_AVOID_OWNER_RESTRICTION;

    if (!NewDescriptor || !GenericMapping || (GuidCount && !ObjectTypes))
        return STATUS_INVALID_PARAMETER;
    if (AutoInheritFlags & ~SupportedFlags) return STATUS_NOT_IMPLEMENTED;
    Status = RtlpSecurityReadDescriptor(ParentDescriptor, &Parent);
    if (!NT_SUCCESS(Status)) return Status;
    Status = RtlpSecurityReadDescriptor(CreatorDescriptor, &Creator);
    if (!NT_SUCCESS(Status)) return Status;
    if (Creator.Control & SE_SERVER_SECURITY) return STATUS_NOT_IMPLEMENTED;
    RtlCreateSecurityDescriptor(&Descriptor, SECURITY_DESCRIPTOR_REVISION);
    Descriptor.Control = Creator.Control & (SE_DACL_UNTRUSTED | SE_SERVER_SECURITY | SE_RM_CONTROL_VALID);
    Descriptor.Sbz1 = Creator.Sbz1;
    if (Token)
    {
        Status = NtQueryInformationToken(Token, TokenStatistics, &Statistics,
                                          sizeof(Statistics), &Length);
        if (!NT_SUCCESS(Status)) return Status;
        if (Statistics.TokenType == TokenImpersonation &&
            Statistics.ImpersonationLevel < SecurityIdentification)
            return STATUS_BAD_IMPERSONATION_LEVEL;
    }
    Descriptor.Owner = Creator.Owner;
    if (!Descriptor.Owner)
    {
        if (AutoInheritFlags & SEF_DEFAULT_OWNER_FROM_PARENT)
            Descriptor.Owner = Parent.Owner;
        else
        {
            Status = RtlpSecurityQueryToken(Token, TokenOwner, (PVOID *)&TokenOwnerInfo);
            if (!NT_SUCCESS(Status)) goto Done;
            Descriptor.Owner = TokenOwnerInfo->Owner;
        }
        Descriptor.Control |= SE_OWNER_DEFAULTED;
    }
    else Descriptor.Control |= Creator.Control & SE_OWNER_DEFAULTED;
    if (!Descriptor.Owner || !RtlValidSid(Descriptor.Owner))
    {
        Status = STATUS_INVALID_OWNER;
        goto Done;
    }
    Descriptor.Group = Creator.Group;
    if (!Descriptor.Group)
    {
        if (AutoInheritFlags & SEF_DEFAULT_GROUP_FROM_PARENT)
            Descriptor.Group = Parent.Group;
        else
        {
            Status = RtlpSecurityQueryToken(Token, TokenPrimaryGroup, (PVOID *)&TokenGroupInfo);
            if (!NT_SUCCESS(Status)) goto Done;
            Descriptor.Group = TokenGroupInfo->PrimaryGroup;
        }
        Descriptor.Control |= SE_GROUP_DEFAULTED;
    }
    else Descriptor.Control |= Creator.Control & SE_GROUP_DEFAULTED;
    if (!Descriptor.Group || !RtlValidSid(Descriptor.Group))
    {
        Status = STATUS_INVALID_PRIMARY_GROUP;
        goto Done;
    }
    if (!(AutoInheritFlags & SEF_AVOID_OWNER_CHECK))
    {
        Status = RtlpSecurityValidateOwner(Token, Descriptor.Owner);
        if (!NT_SUCCESS(Status)) goto Done;
    }
    if ((Creator.Control & SE_SACL_PRESENT) && !(AutoInheritFlags & SEF_AVOID_PRIVILEGE_CHECK))
    {
        Status = RtlpSecurityCheckPrivilege(Token);
        if (!NT_SUCCESS(Status)) goto Done;
    }
    if (!(Creator.Control & SE_DACL_PRESENT) && Token)
    {
        Status = RtlpSecurityQueryToken(Token, TokenDefaultDacl, (PVOID *)&TokenDaclInfo);
        if (!NT_SUCCESS(Status)) goto Done;
    }
    if (CreatorDescriptor && !(AutoInheritFlags & SEF_AVOID_OWNER_RESTRICTION))
    {
        Status = RtlpSecurityCheckOwnerRestriction(Parent.Dacl,
                    TokenDaclInfo ? TokenDaclInfo->DefaultDacl : NULL, Token);
        if (!NT_SUCCESS(Status)) goto Done;
    }
    Status = RtlpSecurityNewAcl(Parent.Dacl, Creator.Dacl,
                                TokenDaclInfo ? TokenDaclInfo->DefaultDacl : NULL,
                                Creator.Control, FALSE, IsDirectoryObject, AutoInheritFlags,
                                ObjectTypes, GuidCount, Descriptor.Owner, Descriptor.Group,
                                GenericMapping, &Descriptor.Dacl, &Descriptor.Control);
    if (!NT_SUCCESS(Status)) goto Done;
    Status = RtlpSecurityNewAcl(Parent.Sacl, Creator.Sacl, NULL, Creator.Control,
                                TRUE, IsDirectoryObject, AutoInheritFlags,
                                ObjectTypes, GuidCount, Descriptor.Owner, Descriptor.Group,
                                GenericMapping, &Descriptor.Sacl, &Descriptor.Control);
    if (!NT_SUCCESS(Status)) goto Done;
    Status = RtlpSecurityPublishDescriptor(&Descriptor, NewDescriptor);
Done:
    RtlFreeHeap(RtlGetProcessHeap(), 0, Descriptor.Dacl);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Descriptor.Sacl);
    RtlFreeHeap(RtlGetProcessHeap(), 0, TokenOwnerInfo);
    RtlFreeHeap(RtlGetProcessHeap(), 0, TokenGroupInfo);
    RtlFreeHeap(RtlGetProcessHeap(), 0, TokenDaclInfo);
    return Status;
}


typedef struct _RTL_SECURITY_CONVERSION_ACE
{
    PACE_HEADER Ace;
    ACCESS_MASK InheritedMask;
    BOOLEAN Inherited;
} RTL_SECURITY_CONVERSION_ACE;

static BOOLEAN
RtlpSecuritySameAceSubject(PACE_HEADER First, PACE_HEADER Second)
{
    ULONG Offset = FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart);

    return First->AceType == Second->AceType &&
           (First->AceFlags & ~INHERITED_ACE) == (Second->AceFlags & ~INHERITED_ACE) &&
           First->AceSize == Second->AceSize && First->AceSize >= Offset &&
           !memcmp((PUCHAR)First + Offset, (PUCHAR)Second + Offset, First->AceSize - Offset);
}

static INT
RtlpSecurityAceAccessKind(PACE_HEADER Ace)
{
    switch (Ace->AceType)
    {
        case ACCESS_ALLOWED_ACE_TYPE:
        case ACCESS_ALLOWED_OBJECT_ACE_TYPE:
        case ACCESS_ALLOWED_CALLBACK_ACE_TYPE:
        case ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE:
            return 1;
        case ACCESS_DENIED_ACE_TYPE:
        case ACCESS_DENIED_OBJECT_ACE_TYPE:
        case ACCESS_DENIED_CALLBACK_ACE_TYPE:
        case ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE:
            return -1;
        default:
            return 0;
    }
}

static NTSTATUS
RtlpSecurityConvertAcl(PACL Current, PACL Parent, BOOLEAN Container,
                       LPGUID *Types, ULONG TypeCount, PSID Owner, PSID Group,
                       PGENERIC_MAPPING Mapping, PBOOLEAN Protected, PACL *Result)
{
    PACL ParentAcl = NULL;
    RTL_SECURITY_CONVERSION_ACE *Entries = NULL;
    RTL_SECURITY_ACL_BUFFER Buffer;
    RTL_SECURITY_ACE_VIEW View;
    PACE_HEADER ParentAce;
    ULONG Index, Other, Pass, Kind;
    ACCESS_MASK ParentMask, CurrentMask, Mask, ExplicitMask;
    BOOLEAN AnyInherited = FALSE;
    NTSTATUS Status;
    INT AccessKind;

    *Result = NULL;
    if (!Current)
    {
        *Protected = TRUE;
        return STATUS_SUCCESS;
    }
    if (!RtlValidAcl(Current)) return STATUS_INVALID_ACL;
    RtlZeroMemory(&Buffer, sizeof(Buffer));
    Entries = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY,
                               max(Current->AceCount, 1) * sizeof(*Entries));
    if (!Entries) return STATUS_NO_MEMORY;
    if (!*Protected)
    {
        Status = RtlpSecurityTransformAcl(Parent, TRUE, FALSE, -1, Container,
                                          Types, TypeCount, Owner, Group, Mapping, &ParentAcl);
        if (!NT_SUCCESS(Status)) goto Done;
    }
    for (Index = 0; Index < Current->AceCount; ++Index)
    {
        Status = RtlGetAce(Current, Index, (PVOID *)&Entries[Index].Ace);
        if (!NT_SUCCESS(Status)) goto Done;
        Status = RtlpSecurityAceView(Entries[Index].Ace, &View);
        if (!NT_SUCCESS(Status)) goto Done;
    }
    if (!*Protected && ParentAcl)
    {
        for (Index = 0; Index < Current->AceCount; ++Index)
        {
            Status = RtlpSecurityAceView(Entries[Index].Ace, &View);
            if (!NT_SUCCESS(Status)) goto Done;
            Mask = ((PACCESS_ALLOWED_ACE)Entries[Index].Ace)->Mask;
            ParentMask = 0;
            for (Other = 0; Other < ParentAcl->AceCount; ++Other)
            {
                Status = RtlGetAce(ParentAcl, Other, (PVOID *)&ParentAce);
                if (!NT_SUCCESS(Status)) goto Done;
                if (!RtlpSecuritySameAceSubject(Entries[Index].Ace, ParentAce)) continue;
                ParentMask |= ((PACCESS_ALLOWED_ACE)ParentAce)->Mask;
                if (!Mask && !((PACCESS_ALLOWED_ACE)ParentAce)->Mask)
                    Entries[Index].Inherited = TRUE;
            }
            if (ParentMask && View.MapMask &&
                (Mask & (GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL)))
            {
                Status = STATUS_NOT_IMPLEMENTED;
                goto Done;
            }
            CurrentMask = 0;
            for (Other = 0; Other < Current->AceCount; ++Other)
                if (RtlpSecuritySameAceSubject(Entries[Index].Ace, Entries[Other].Ace))
                    CurrentMask |= ((PACCESS_ALLOWED_ACE)Entries[Other].Ace)->Mask;
            if ((ParentMask & CurrentMask) && (ParentMask & ~CurrentMask))
            {
                Status = STATUS_NOT_IMPLEMENTED;
                goto Done;
            }
            if ((Mask & ParentMask) && (Mask & ~ParentMask))
            {
                Status = STATUS_NOT_IMPLEMENTED;
                goto Done;
            }
            if (!(ParentMask & ~CurrentMask))
            {
                Entries[Index].InheritedMask = Mask & ParentMask;
                if (Entries[Index].InheritedMask) Entries[Index].Inherited = TRUE;
                if (View.Opaque && Entries[Index].InheritedMask != Mask &&
                    Entries[Index].InheritedMask)
                {
                    Status = STATUS_NOT_IMPLEMENTED;
                    goto Done;
                }
            }
            AnyInherited |= Entries[Index].Inherited;
        }
        for (Index = 0; Index < Current->AceCount && !*Protected; ++Index)
        {
            if (!Entries[Index].Inherited) continue;
            AccessKind = RtlpSecurityAceAccessKind(Entries[Index].Ace);
            if (!AccessKind) continue;
            for (Other = Index + 1; Other < Current->AceCount; ++Other)
            {
                Mask = ((PACCESS_ALLOWED_ACE)Entries[Other].Ace)->Mask;
                if ((Mask & ~Entries[Other].InheritedMask) &&
                    RtlpSecurityAceAccessKind(Entries[Other].Ace) == -AccessKind)
                {
                    *Protected = TRUE;
                    break;
                }
            }
        }
    }
    if (!AnyInherited) *Protected = TRUE;
    for (Pass = 0; Pass < 2; ++Pass)
    {
        Buffer.Length = sizeof(ACL);
        Buffer.Count = 0;
        Buffer.Revision = Current->AclRevision;
        for (Kind = 0; Kind < (*Protected ? 1u : 2u); ++Kind)
        {
            for (Index = 0; Index < Current->AceCount; ++Index)
            {
                Mask = ((PACCESS_ALLOWED_ACE)Entries[Index].Ace)->Mask;
                if (*Protected)
                {
                    Status = RtlpSecurityEmitAce(&Buffer, Entries[Index].Ace,
                              Entries[Index].Ace->AceFlags & ~INHERITED_ACE, FALSE,
                              Owner, Group, Mapping, NULL);
                }
                else if (!Kind)
                {
                    ExplicitMask = Mask & ~Entries[Index].InheritedMask;
                    if (!ExplicitMask && (Mask || Entries[Index].Inherited)) continue;
                    Status = RtlpSecurityEmitAce(&Buffer, Entries[Index].Ace,
                              Entries[Index].Ace->AceFlags & ~INHERITED_ACE, FALSE,
                              Owner, Group, Mapping, &ExplicitMask);
                }
                else
                {
                    if (!Entries[Index].Inherited) continue;
                    Status = RtlpSecurityEmitAce(&Buffer, Entries[Index].Ace,
                              Entries[Index].Ace->AceFlags | INHERITED_ACE, FALSE,
                              Owner, Group, Mapping, &Entries[Index].InheritedMask);
                }
                if (!NT_SUCCESS(Status)) goto Done;
            }
        }
        if (!Pass)
        {
            Buffer.Capacity = Buffer.Length;
            Buffer.Acl = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, Buffer.Capacity);
            if (!Buffer.Acl)
            {
                Status = STATUS_NO_MEMORY;
                goto Done;
            }
        }
    }
    Buffer.Acl->AclRevision = Buffer.Revision;
    Buffer.Acl->AclSize = (USHORT)Buffer.Length;
    Buffer.Acl->AceCount = (USHORT)Buffer.Count;
    *Result = Buffer.Acl;
    Buffer.Acl = NULL;
    Status = STATUS_SUCCESS;
Done:
    RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer.Acl);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Entries);
    RtlFreeHeap(RtlGetProcessHeap(), 0, ParentAcl);
    return Status;
}

NTSTATUS
NTAPI
RtlpConvertToAutoInheritSecurityObject(IN PSECURITY_DESCRIPTOR ParentDescriptor,
                                       IN PSECURITY_DESCRIPTOR CreatorDescriptor,
                                       OUT PSECURITY_DESCRIPTOR *NewDescriptor,
                                       IN LPGUID ObjectType,
                                       IN BOOLEAN IsDirectoryObject,
                                       IN PGENERIC_MAPPING GenericMapping)
{
    SECURITY_DESCRIPTOR Parent, Descriptor;
    PACL Dacl = NULL, Sacl = NULL;
    BOOLEAN Protected;
    NTSTATUS Status;

    if (!CreatorDescriptor || !NewDescriptor || !GenericMapping) return STATUS_INVALID_PARAMETER;
    Status = RtlpSecurityReadDescriptor(ParentDescriptor, &Parent);
    if (!NT_SUCCESS(Status)) return Status;
    Status = RtlpSecurityReadDescriptor(CreatorDescriptor, &Descriptor);
    if (!NT_SUCCESS(Status)) return Status;
    if ((Parent.Control | Descriptor.Control) & SE_SERVER_SECURITY) return STATUS_NOT_IMPLEMENTED;
    Protected = !!(Descriptor.Control & SE_DACL_PROTECTED);
    Status = RtlpSecurityConvertAcl(Descriptor.Dacl, Parent.Dacl, IsDirectoryObject,
                                    ObjectType ? &ObjectType : NULL, ObjectType ? 1 : 0,
                                    Descriptor.Owner, Descriptor.Group, GenericMapping,
                                    &Protected, &Dacl);
    if (!NT_SUCCESS(Status)) goto Done;
    Descriptor.Dacl = Dacl;
    Descriptor.Control |= SE_DACL_AUTO_INHERITED;
    Descriptor.Control &= ~SE_DACL_AUTO_INHERIT_REQ;
    if (Protected) Descriptor.Control |= SE_DACL_PROTECTED;
    Protected = !!(Descriptor.Control & SE_SACL_PROTECTED);
    Status = RtlpSecurityConvertAcl(Descriptor.Sacl, Parent.Sacl, IsDirectoryObject,
                                    ObjectType ? &ObjectType : NULL, ObjectType ? 1 : 0,
                                    Descriptor.Owner, Descriptor.Group, GenericMapping,
                                    &Protected, &Sacl);
    if (!NT_SUCCESS(Status)) goto Done;
    Descriptor.Sacl = Sacl;
    Descriptor.Control |= SE_SACL_AUTO_INHERITED;
    Descriptor.Control &= ~SE_SACL_AUTO_INHERIT_REQ;
    if (Protected) Descriptor.Control |= SE_SACL_PROTECTED;
    Status = RtlpSecurityPublishDescriptor(&Descriptor, NewDescriptor);
Done:
    RtlFreeHeap(RtlGetProcessHeap(), 0, Dacl);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Sacl);
    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlDefaultNpAcl(OUT PACL *pAcl)
{
    NTSTATUS Status;
    HANDLE TokenHandle;
    PTOKEN_OWNER OwnerSid;
    ULONG ReturnLength = 0;
    ULONG AclSize;
    SID_IDENTIFIER_AUTHORITY NtAuthority    = {SECURITY_NT_AUTHORITY};
    SID_IDENTIFIER_AUTHORITY WorldAuthority = {SECURITY_WORLD_SID_AUTHORITY};

    C_ASSERT(sizeof(ACE) == FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart));

    /*
     * Temporary buffer large enough to hold a maximum of two SIDs.
     * An alternative is to call RtlAllocateAndInitializeSid many times...
     */
    UCHAR SidBuffer[FIELD_OFFSET(SID, SubAuthority)
                    + 2*RTL_FIELD_SIZE(SID, SubAuthority)];
    PSID Sid = (PSID)&SidBuffer;

    ASSERT(RtlLengthRequiredSid(2) == sizeof(SidBuffer));

    /* Initialize the user ACL pointer */
    *pAcl = NULL;

    /*
     * Try to retrieve the SID of the current owner. For that,
     * we first attempt to get the current thread level token.
     */
    Status = NtOpenThreadToken(NtCurrentThread(),
                               TOKEN_QUERY,
                               TRUE,
                               &TokenHandle);
    if (Status == STATUS_NO_TOKEN)
    {
        /*
         * No thread level token, so use the process level token.
         * This is the common case since the only time a thread
         * has a token is when it is impersonating.
         */
        Status = NtOpenProcessToken(NtCurrentProcess(),
                                    TOKEN_QUERY,
                                    &TokenHandle);
    }
    /* Fail if we didn't succeed in retrieving a handle to the token */
    if (!NT_SUCCESS(Status)) return Status;

    /*
     * Retrieve the owner SID from the token.
     */

    /* Query the needed size... */
    Status = NtQueryInformationToken(TokenHandle,
                                     TokenOwner,
                                     NULL, 0,
                                     &ReturnLength);
    /* ... so that we must fail with STATUS_BUFFER_TOO_SMALL error */
    if (Status != STATUS_BUFFER_TOO_SMALL) goto Cleanup1;

    /* Allocate space for the owner SID */
    OwnerSid = RtlAllocateHeap(RtlGetProcessHeap(), 0, ReturnLength);
    if (OwnerSid == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup1;
    }

    /* Retrieve the owner SID; we must succeed */
    Status = NtQueryInformationToken(TokenHandle,
                                     TokenOwner,
                                     OwnerSid,
                                     ReturnLength,
                                     &ReturnLength);
    if (!NT_SUCCESS(Status)) goto Cleanup2;

    /*
     * Allocate one ACL with 5 ACEs.
     */
    AclSize = sizeof(ACL) +                     // Header
              5 * sizeof(ACE) +                 // 5 ACEs:
              RtlLengthRequiredSid(1) +         // LocalSystem
              RtlLengthRequiredSid(2) +         // Administrators
              RtlLengthRequiredSid(1) +         // Anonymous
              RtlLengthRequiredSid(1) +         // World
              RtlLengthSid(OwnerSid->Owner);    // Owner

    *pAcl = RtlAllocateHeap(RtlGetProcessHeap(), 0, AclSize);
    if (*pAcl == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup2;
    }

    /*
     * Build the ACL and add the five ACEs.
     */
    Status = RtlCreateAcl(*pAcl, AclSize, ACL_REVISION2);
    ASSERT(NT_SUCCESS(Status));

    /* Local System SID - Generic All */
    Status = RtlInitializeSid(Sid, &NtAuthority, 1);
    ASSERT(NT_SUCCESS(Status));
    *RtlSubAuthoritySid(Sid, 0) = SECURITY_LOCAL_SYSTEM_RID;
    Status = RtlAddAccessAllowedAce(*pAcl, ACL_REVISION2, GENERIC_ALL, Sid);
    ASSERT(NT_SUCCESS(Status));

    /* Administrators SID - Generic All */
    Status = RtlInitializeSid(Sid, &NtAuthority, 2);
    ASSERT(NT_SUCCESS(Status));
    *RtlSubAuthoritySid(Sid, 0) = SECURITY_BUILTIN_DOMAIN_RID;
    *RtlSubAuthoritySid(Sid, 1) = DOMAIN_ALIAS_RID_ADMINS;
    Status = RtlAddAccessAllowedAce(*pAcl, ACL_REVISION2, GENERIC_ALL, Sid);
    ASSERT(NT_SUCCESS(Status));

    /* Owner SID - Generic All */
    RtlAddAccessAllowedAce(*pAcl, ACL_REVISION2, GENERIC_ALL, OwnerSid->Owner);
    ASSERT(NT_SUCCESS(Status));

    /* Anonymous SID - Generic Read */
    Status = RtlInitializeSid(Sid, &NtAuthority, 1);
    ASSERT(NT_SUCCESS(Status));
    *RtlSubAuthoritySid(Sid, 0) = SECURITY_ANONYMOUS_LOGON_RID;
    Status = RtlAddAccessAllowedAce(*pAcl, ACL_REVISION2, GENERIC_READ, Sid);
    ASSERT(NT_SUCCESS(Status));

    /* World SID - Generic Read */
    Status = RtlInitializeSid(Sid, &WorldAuthority, 1);
    ASSERT(NT_SUCCESS(Status));
    *RtlSubAuthoritySid(Sid, 0) = SECURITY_WORLD_RID;
    Status = RtlAddAccessAllowedAce(*pAcl, ACL_REVISION2, GENERIC_READ, Sid);
    ASSERT(NT_SUCCESS(Status));

    /* If some problem happened, cleanup everything */
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, *pAcl);
        *pAcl = NULL;
    }

Cleanup2:
    /* Get rid of the owner SID */
    RtlFreeHeap(RtlGetProcessHeap(), 0, OwnerSid);

Cleanup1:
    /* Close the token handle */
    NtClose(TokenHandle);

    /* Done */
    return Status;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
RtlCreateAndSetSD(IN PRTL_ACE_DATA AceData,
                  IN ULONG AceCount,
                  IN PSID OwnerSid OPTIONAL,
                  IN PSID GroupSid OPTIONAL,
                  OUT PSECURITY_DESCRIPTOR *NewDescriptor)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlDeleteSecurityObject(IN PSECURITY_DESCRIPTOR *ObjectDescriptor)
{
    DPRINT1("RtlDeleteSecurityObject(%p)\n", ObjectDescriptor);

    /* Free the object from the heap */
    RtlFreeHeap(RtlGetProcessHeap(), 0, *ObjectDescriptor);
    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlNewSecurityObject(IN PSECURITY_DESCRIPTOR ParentDescriptor,
                     IN PSECURITY_DESCRIPTOR CreatorDescriptor,
                     OUT PSECURITY_DESCRIPTOR *NewDescriptor,
                     IN BOOLEAN IsDirectoryObject,
                     IN HANDLE Token,
                     IN PGENERIC_MAPPING GenericMapping)
{
    DPRINT1("RtlNewSecurityObject(%p)\n", ParentDescriptor);

    /* Call the internal API */
    return RtlpNewSecurityObject(ParentDescriptor,
                                 CreatorDescriptor,
                                 NewDescriptor,
                                 NULL,
                                 0,
                                 IsDirectoryObject,
                                 0,
                                 Token,
                                 GenericMapping);
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlNewSecurityObjectEx(IN PSECURITY_DESCRIPTOR ParentDescriptor,
                       IN PSECURITY_DESCRIPTOR CreatorDescriptor,
                       OUT PSECURITY_DESCRIPTOR *NewDescriptor,
                       IN LPGUID ObjectType,
                       IN BOOLEAN IsDirectoryObject,
                       IN ULONG AutoInheritFlags,
                       IN HANDLE Token,
                       IN PGENERIC_MAPPING GenericMapping)
{
    DPRINT1("RtlNewSecurityObjectEx(%p)\n", ParentDescriptor);

    /* Call the internal API */
    return RtlpNewSecurityObject(ParentDescriptor,
                                 CreatorDescriptor,
                                 NewDescriptor,
                                 ObjectType ? &ObjectType : NULL,
                                 ObjectType ? 1 : 0,
                                 IsDirectoryObject,
                                 AutoInheritFlags,
                                 Token,
                                 GenericMapping);
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlNewSecurityObjectWithMultipleInheritance(IN PSECURITY_DESCRIPTOR ParentDescriptor,
                                            IN PSECURITY_DESCRIPTOR CreatorDescriptor,
                                            OUT PSECURITY_DESCRIPTOR *NewDescriptor,
                                            IN LPGUID *ObjectTypes,
                                            IN ULONG GuidCount,
                                            IN BOOLEAN IsDirectoryObject,
                                            IN ULONG AutoInheritFlags,
                                            IN HANDLE Token,
                                            IN PGENERIC_MAPPING GenericMapping)
{
    DPRINT1("RtlNewSecurityObjectWithMultipleInheritance(%p)\n", ParentDescriptor);

    /* Call the internal API */
    return RtlpNewSecurityObject(ParentDescriptor,
                                 CreatorDescriptor,
                                 NewDescriptor,
                                 ObjectTypes,
                                 GuidCount,
                                 IsDirectoryObject,
                                 AutoInheritFlags,
                                 Token,
                                 GenericMapping);
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlNewInstanceSecurityObject(IN BOOLEAN ParentDescriptorChanged,
                             IN BOOLEAN CreatorDescriptorChanged,
                             IN PLUID OldClientTokenModifiedId,
                             OUT PLUID NewClientTokenModifiedId,
                             IN PSECURITY_DESCRIPTOR ParentDescriptor,
                             IN PSECURITY_DESCRIPTOR CreatorDescriptor,
                             OUT PSECURITY_DESCRIPTOR *NewDescriptor,
                             IN BOOLEAN IsDirectoryObject,
                             IN HANDLE Token,
                             IN PGENERIC_MAPPING GenericMapping)
{
    TOKEN_STATISTICS TokenStats;
    ULONG Size;
    NTSTATUS Status;
    DPRINT1("RtlNewInstanceSecurityObject(%p)\n", ParentDescriptor);

    /* Query the token statistics */
    Status = NtQueryInformationToken(Token,
                                     TokenStatistics,
                                     &TokenStats,
                                     sizeof(TokenStats),
                                     &Size);
    if (!NT_SUCCESS(Status)) return Status;

    /* Return the LUID */
    *NewClientTokenModifiedId = TokenStats.ModifiedId;

    /* Check if the LUID changed */
    if (RtlEqualLuid(NewClientTokenModifiedId, OldClientTokenModifiedId))
    {
        /* Did nothing change? */
        if (!(ParentDescriptorChanged) && !(CreatorDescriptorChanged))
        {
            /* There's no new descriptor, we're done */
            *NewDescriptor = NULL;
            return STATUS_SUCCESS;
        }
    }

    /* Call the standard API */
    return RtlNewSecurityObject(ParentDescriptor,
                                CreatorDescriptor,
                                NewDescriptor,
                                IsDirectoryObject,
                                Token,
                                GenericMapping);
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlCreateUserSecurityObject(IN PRTL_ACE_DATA AceData,
                            IN ULONG AceCount,
                            IN PSID OwnerSid,
                            IN PSID GroupSid,
                            IN BOOLEAN IsDirectoryObject,
                            IN PGENERIC_MAPPING GenericMapping,
                            OUT PSECURITY_DESCRIPTOR *NewDescriptor)
{
    NTSTATUS Status;
    PSECURITY_DESCRIPTOR Sd;
    HANDLE TokenHandle;
    DPRINT1("RtlCreateUserSecurityObject(%p)\n", AceData);

    /* Create the security descriptor based on the ACE Data */
    Status = RtlCreateAndSetSD(AceData,
                               AceCount,
                               OwnerSid,
                               GroupSid,
                               &Sd);
    if (!NT_SUCCESS(Status)) return Status;

    /* Open the process token */
    Status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &TokenHandle);
    if (!NT_SUCCESS(Status)) goto Quickie;

    /* Create the security object */
    Status = RtlNewSecurityObject(NULL,
                                  Sd,
                                  NewDescriptor,
                                  IsDirectoryObject,
                                  TokenHandle,
                                  GenericMapping);

    /* We're done, close the token handle */
    NtClose(TokenHandle);

Quickie:
    /* Free the SD and return status */
    RtlFreeHeap(RtlGetProcessHeap(), 0, Sd);
    return Status;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlNewSecurityGrantedAccess(IN ACCESS_MASK DesiredAccess,
                            OUT PPRIVILEGE_SET Privileges,
                            IN OUT PULONG Length,
                            IN HANDLE Token,
                            IN PGENERIC_MAPPING GenericMapping,
                            OUT PACCESS_MASK RemainingDesiredAccess)
{
    NTSTATUS Status;
    BOOLEAN Granted, CallerToken;
    TOKEN_STATISTICS TokenStats;
    ULONG Size;
    DPRINT1("RtlNewSecurityGrantedAccess(%lx)\n", DesiredAccess);

    /* Has the caller passed a token? */
    if (!Token)
    {
        /* Remember that we'll have to close the handle */
        CallerToken = FALSE;

        /* Nope, open it */
        Status = NtOpenThreadToken(NtCurrentThread(), TOKEN_QUERY, TRUE, &Token);
        if (!NT_SUCCESS(Status)) return Status;
    }
    else
    {
        /* Yep, use it */
        CallerToken = TRUE;
    }

    /* Get information on the token */
    Status = NtQueryInformationToken(Token,
                                     TokenStatistics,
                                     &TokenStats,
                                     sizeof(TokenStats),
                                     &Size);
    ASSERT(NT_SUCCESS(Status));

    /* Windows doesn't do anything with the token statistics! */

    /* Map the access and return it back decoded */
    RtlMapGenericMask(&DesiredAccess, GenericMapping);
    *RemainingDesiredAccess = DesiredAccess;

    /* Check if one of the rights requested was the SACL right */
    if (DesiredAccess & ACCESS_SYSTEM_SECURITY)
    {
        /* Pretend that it's allowed FIXME: Do privilege check */
        DPRINT1("Missing privilege check for SE_SECURITY_PRIVILEGE");
        Granted = TRUE;
        *RemainingDesiredAccess &= ~ACCESS_SYSTEM_SECURITY;
    }
    else
    {
        /* Nothing to grant */
        Granted = FALSE;
    }

    /* If the caller did not pass in a token, close the handle to ours */
    if (!CallerToken) NtClose(Token);

    /* We need space to return only 1 privilege -- already part of the struct */
    Size = sizeof(PRIVILEGE_SET);
    if (Size > *Length)
    {
        /* Tell the caller how much space we need and fail */
        *Length = Size;
        return STATUS_BUFFER_TOO_SMALL;
    }

    /* Check if the SACL right was granted */
    RtlZeroMemory(Privileges, Size);
    if (Granted)
    {
        /* Yes, return it in the structure */
        Privileges->PrivilegeCount = 1;
        Privileges->Privilege[0].Luid.LowPart = SE_SECURITY_PRIVILEGE;
        Privileges->Privilege[0].Luid.HighPart = 0;
        Privileges->Privilege[0].Attributes = SE_PRIVILEGE_USED_FOR_ACCESS;
    }

    /* All done */
    return STATUS_SUCCESS;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
RtlQuerySecurityObject(IN PSECURITY_DESCRIPTOR ObjectDescriptor,
                       IN SECURITY_INFORMATION SecurityInformation,
                       OUT PSECURITY_DESCRIPTOR ResultantDescriptor,
                       IN ULONG DescriptorLength,
                       OUT PULONG ReturnLength)
{
    NTSTATUS Status;
    SECURITY_DESCRIPTOR desc;
    BOOLEAN defaulted, present;
    PACL pacl;
    PSID psid;

    Status = RtlCreateSecurityDescriptor(&desc, SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(Status)) return Status;

    if (SecurityInformation & OWNER_SECURITY_INFORMATION)
    {
        Status = RtlGetOwnerSecurityDescriptor(ObjectDescriptor, &psid, &defaulted);
        if (!NT_SUCCESS(Status)) return Status;
        Status = RtlSetOwnerSecurityDescriptor(&desc, psid, defaulted);
        if (!NT_SUCCESS(Status)) return Status;
    }

    if (SecurityInformation & GROUP_SECURITY_INFORMATION)
    {
        Status = RtlGetGroupSecurityDescriptor(ObjectDescriptor, &psid, &defaulted);
        if (!NT_SUCCESS(Status)) return Status;
        Status = RtlSetGroupSecurityDescriptor(&desc, psid, defaulted);
        if (!NT_SUCCESS(Status)) return Status;
    }

    if (SecurityInformation & DACL_SECURITY_INFORMATION)
    {
        Status = RtlGetDaclSecurityDescriptor(ObjectDescriptor, &present, &pacl, &defaulted);
        if (!NT_SUCCESS(Status)) return Status;
        Status = RtlSetDaclSecurityDescriptor(&desc, present, pacl, defaulted);
        if (!NT_SUCCESS(Status)) return Status;
    }

    if (SecurityInformation & SACL_SECURITY_INFORMATION)
    {
        Status = RtlGetSaclSecurityDescriptor(ObjectDescriptor, &present, &pacl, &defaulted);
        if (!NT_SUCCESS(Status)) return Status;
        Status = RtlSetSaclSecurityDescriptor(&desc, present, pacl, defaulted);
        if (!NT_SUCCESS(Status)) return Status;
    }

    *ReturnLength = DescriptorLength;
    return RtlAbsoluteToSelfRelativeSD(&desc, ResultantDescriptor, ReturnLength);
}


/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlSetSecurityObject(IN SECURITY_INFORMATION SecurityInformation,
                     IN PSECURITY_DESCRIPTOR ModificationDescriptor,
                     IN OUT PSECURITY_DESCRIPTOR *ObjectsSecurityDescriptor,
                     IN PGENERIC_MAPPING GenericMapping,
                     IN HANDLE Token OPTIONAL)
{
    /* Call the internal API */
    return RtlpSetSecurityObject(NULL,
                                 SecurityInformation,
                                 ModificationDescriptor,
                                 ObjectsSecurityDescriptor,
                                 0,
                                 PagedPool,
                                 GenericMapping,
                                 Token);
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlSetSecurityObjectEx(IN SECURITY_INFORMATION SecurityInformation,
                       IN PSECURITY_DESCRIPTOR ModificationDescriptor,
                       IN OUT PSECURITY_DESCRIPTOR *ObjectsSecurityDescriptor,
                       IN ULONG AutoInheritFlags,
                       IN PGENERIC_MAPPING GenericMapping,
                       IN HANDLE Token OPTIONAL)
{
    /* Call the internal API */
    return RtlpSetSecurityObject(NULL,
                                 SecurityInformation,
                                 ModificationDescriptor,
                                 ObjectsSecurityDescriptor,
                                 AutoInheritFlags,
                                 PagedPool,
                                 GenericMapping,
                                 Token);

}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlConvertToAutoInheritSecurityObject(IN PSECURITY_DESCRIPTOR ParentDescriptor,
                                      IN PSECURITY_DESCRIPTOR CreatorDescriptor,
                                      OUT PSECURITY_DESCRIPTOR *NewDescriptor,
                                      IN LPGUID ObjectType,
                                      IN BOOLEAN IsDirectoryObject,
                                      IN PGENERIC_MAPPING GenericMapping)
{
    /* Call the internal API */
    return RtlpConvertToAutoInheritSecurityObject(ParentDescriptor,
                                                  CreatorDescriptor,
                                                  NewDescriptor,
                                                  ObjectType,
                                                  IsDirectoryObject,
                                                  GenericMapping);
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
RtlRegisterSecureMemoryCacheCallback(IN PRTL_SECURE_MEMORY_CACHE_CALLBACK Callback)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
BOOLEAN
NTAPI
RtlFlushSecureMemoryCache(IN PVOID MemoryCache,
                          IN OPTIONAL SIZE_T MemoryLength)
{
    UNIMPLEMENTED;
    return FALSE;
}
