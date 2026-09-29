/*
 * PROJECT:     LiberNT Security
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared allocation-free security ACE transformation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

typedef struct _RTL_SECURITY_ACE_VIEW
{
    PSID Sid;
    ULONG SidOffset;
    ULONG SidLength;
    GUID *InheritedType;
    BOOLEAN MapMask;
    BOOLEAN MapSid;
    BOOLEAN Opaque;
} RTL_SECURITY_ACE_VIEW;

typedef struct _RTL_SECURITY_ACL_BUFFER
{
    PACL Acl;
    ULONG Length;
    ULONG Count;
    ULONG Capacity;
    UCHAR Revision;
} RTL_SECURITY_ACL_BUFFER;

static NTSTATUS
RtlpSecurityAceView(PACE_HEADER Ace, RTL_SECURITY_ACE_VIEW *View)
{
    ULONG Offset = FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart);
    ULONG Flags;

    RtlZeroMemory(View, sizeof(*View));
    View->MapMask = TRUE;
    View->MapSid = TRUE;
    switch (Ace->AceType)
    {
        case ACCESS_ALLOWED_ACE_TYPE:
        case ACCESS_DENIED_ACE_TYPE:
        case SYSTEM_AUDIT_ACE_TYPE:
        case SYSTEM_ALARM_ACE_TYPE:
            break;
        case ACCESS_ALLOWED_CALLBACK_ACE_TYPE:
        case ACCESS_DENIED_CALLBACK_ACE_TYPE:
        case SYSTEM_AUDIT_CALLBACK_ACE_TYPE:
        case SYSTEM_ALARM_CALLBACK_ACE_TYPE:
            View->Opaque = TRUE;
            break;
        case ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE:
        case ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE:
        case SYSTEM_AUDIT_CALLBACK_OBJECT_ACE_TYPE:
        case SYSTEM_ALARM_CALLBACK_OBJECT_ACE_TYPE:
            View->MapMask = Ace->AceType != ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE;
            View->Opaque = TRUE;
        case ACCESS_ALLOWED_OBJECT_ACE_TYPE:
        case ACCESS_DENIED_OBJECT_ACE_TYPE:
        case SYSTEM_AUDIT_OBJECT_ACE_TYPE:
        case SYSTEM_ALARM_OBJECT_ACE_TYPE:
            Offset = FIELD_OFFSET(ACCESS_ALLOWED_OBJECT_ACE, ObjectType);
            if (Ace->AceSize < Offset) return STATUS_INVALID_ACL;
            Flags = ((PACCESS_ALLOWED_OBJECT_ACE)Ace)->Flags;
            if (Flags & ~(ACE_OBJECT_TYPE_PRESENT | ACE_INHERITED_OBJECT_TYPE_PRESENT))
                return STATUS_INVALID_ACL;
            if (Flags & ACE_OBJECT_TYPE_PRESENT) Offset += sizeof(GUID);
            if (Flags & ACE_INHERITED_OBJECT_TYPE_PRESENT)
            {
                View->InheritedType = (GUID *)((PUCHAR)Ace + Offset);
                Offset += sizeof(GUID);
            }
            break;
        case SYSTEM_MANDATORY_LABEL_ACE_TYPE:
            View->MapSid = FALSE;
            View->MapMask = FALSE;
            break;
        default:
            return STATUS_NOT_IMPLEMENTED;
    }

    if (Ace->AceSize < Offset + FIELD_OFFSET(SID, SubAuthority))
        return STATUS_INVALID_ACL;
    View->Sid = (PSID)((PUCHAR)Ace + Offset);
    View->SidLength = RtlLengthRequiredSid(((PISID)View->Sid)->SubAuthorityCount);
    if (View->SidLength > Ace->AceSize - Offset || !RtlValidSid(View->Sid))
        return STATUS_INVALID_ACL;
    View->SidOffset = Offset;
    return STATUS_SUCCESS;
}

static PSID
RtlpSecurityMapSid(PSID Sid, PSID Owner, PSID Group)
{
    static const SID_IDENTIFIER_AUTHORITY CreatorAuthority = SECURITY_CREATOR_SID_AUTHORITY;
    PISID Isid = Sid;

    if (Isid->SubAuthorityCount == 1 &&
        !memcmp(&Isid->IdentifierAuthority, &CreatorAuthority, sizeof(CreatorAuthority)))
    {
        if (Isid->SubAuthority[0] == SECURITY_CREATOR_OWNER_RID) return Owner;
        if (Isid->SubAuthority[0] == SECURITY_CREATOR_GROUP_RID) return Group;
    }
    return Sid;
}

static NTSTATUS
RtlpSecurityEmitAce(RTL_SECURITY_ACL_BUFFER *Buffer, PACE_HEADER Ace,
                   UCHAR Flags, BOOLEAN Map, PSID Owner, PSID Group,
                   PGENERIC_MAPPING Mapping, PACCESS_MASK OverrideMask)
{
    RTL_SECURITY_ACE_VIEW View;
    NTSTATUS Status;
    PSID Sid;
    ULONG Length, SidLength;
    PACE_HEADER Dest;
    ACCESS_MASK Mask;

    Status = RtlpSecurityAceView(Ace, &View);
    if (!NT_SUCCESS(Status)) return Status;
    Sid = Map && View.MapSid ? RtlpSecurityMapSid(View.Sid, Owner, Group) : View.Sid;
    if (!Sid) return STATUS_INVALID_SID;
    SidLength = RtlLengthSid(Sid);
    Length = Ace->AceSize - View.SidLength + SidLength;
    if (Length > MAXUSHORT || Buffer->Length > MAXUSHORT - Length ||
        Buffer->Count == MAXUSHORT) return STATUS_ALLOTTED_SPACE_EXCEEDED;
    if (Buffer->Acl && (Buffer->Length > Buffer->Capacity ||
                        Length > Buffer->Capacity - Buffer->Length))
        return STATUS_BUFFER_TOO_SMALL;
    if (Buffer->Acl)
    {
        Dest = (PACE_HEADER)((PUCHAR)Buffer->Acl + Buffer->Length);
        RtlCopyMemory(Dest, Ace, View.SidOffset);
        RtlCopyMemory((PUCHAR)Dest + View.SidOffset, Sid, SidLength);
        RtlCopyMemory((PUCHAR)Dest + View.SidOffset + SidLength,
                      (PUCHAR)Ace + View.SidOffset + View.SidLength,
                      Ace->AceSize - View.SidOffset - View.SidLength);
        Dest->AceFlags = Flags;
        Dest->AceSize = (USHORT)Length;
        Mask = OverrideMask ? *OverrideMask : ((PACCESS_ALLOWED_ACE)Ace)->Mask;
        if (Map && View.MapMask) RtlMapGenericMask(&Mask, Mapping);
        ((PACCESS_ALLOWED_ACE)Dest)->Mask = Mask;
    }
    Buffer->Length += Length;
    Buffer->Count++;
    if (View.SidOffset > FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart))
        Buffer->Revision = ACL_REVISION_DS;
    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpSecurityTransformAce(RTL_SECURITY_ACL_BUFFER *Buffer, PACE_HEADER Ace,
                        BOOLEAN Parent, BOOLEAN ClearInherited, BOOLEAN Container,
                        LPGUID *Types, ULONG TypeCount, PSID Owner, PSID Group,
                        PGENERIC_MAPPING Mapping)
{
    RTL_SECURITY_ACE_VIEW View;
    UCHAR Flags = Ace->AceFlags, EffectiveFlags;
    BOOLEAN Effective, Propagate, Mappable, Matches = TRUE;
    ACCESS_MASK Mask;
    ULONG Index;
    NTSTATUS Status;

    Status = RtlpSecurityAceView(Ace, &View);
    if (!NT_SUCCESS(Status)) return Status;
    Mask = ((PACCESS_ALLOWED_ACE)Ace)->Mask;
    Mappable = (View.MapMask && (Mask & (GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL))) ||
               (View.MapSid && RtlpSecurityMapSid(View.Sid, Owner, Group) != View.Sid);
    if (ClearInherited) Flags &= ~INHERITED_ACE;
    if (Parent)
    {
        if (View.InheritedType)
        {
            Matches = FALSE;
            for (Index = 0; Index < TypeCount; ++Index)
                if (Types[Index] && !memcmp(Types[Index], View.InheritedType, sizeof(GUID)))
                    Matches = TRUE;
        }
        Effective = Matches && (Flags & (Container ? CONTAINER_INHERIT_ACE : OBJECT_INHERIT_ACE));
        Propagate = Container && (Flags & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)) &&
                    !(Flags & NO_PROPAGATE_INHERIT_ACE);
        Flags |= INHERITED_ACE;
        EffectiveFlags = Flags & ~(OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE |
                                   NO_PROPAGATE_INHERIT_ACE | INHERIT_ONLY_ACE);
        if (Effective && Propagate && !Mappable)
            return RtlpSecurityEmitAce(Buffer, Ace, Flags & ~INHERIT_ONLY_ACE, TRUE,
                                      Owner, Group, Mapping, NULL);
        if (Effective)
        {
            Status = RtlpSecurityEmitAce(Buffer, Ace, EffectiveFlags, TRUE,
                                        Owner, Group, Mapping, NULL);
            if (!NT_SUCCESS(Status)) return Status;
        }
        if (Propagate)
            return RtlpSecurityEmitAce(Buffer, Ace, Flags | INHERIT_ONLY_ACE, FALSE,
                                      Owner, Group, Mapping, NULL);
        return STATUS_SUCCESS;
    }

    if (!(Flags & INHERIT_ONLY_ACE) && Mappable &&
        (Flags & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)))
    {
        if (ClearInherited) return STATUS_NOT_IMPLEMENTED;
        EffectiveFlags = (Flags & ~(OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE |
                                    NO_PROPAGATE_INHERIT_ACE | INHERIT_ONLY_ACE)) | INHERITED_ACE;
        Status = RtlpSecurityEmitAce(Buffer, Ace, EffectiveFlags, TRUE,
                                    Owner, Group, Mapping, NULL);
        if (!NT_SUCCESS(Status)) return Status;
        if (Container && !(Flags & NO_PROPAGATE_INHERIT_ACE))
            return RtlpSecurityEmitAce(Buffer, Ace, Flags | INHERIT_ONLY_ACE, FALSE,
                                      Owner, Group, Mapping, NULL);
        return STATUS_SUCCESS;
    }
    return RtlpSecurityEmitAce(Buffer, Ace, Flags, !(Flags & INHERIT_ONLY_ACE),
                              Owner, Group, Mapping, NULL);
}

