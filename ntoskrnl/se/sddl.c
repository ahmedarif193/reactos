/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Security Descriptor Definition Language parsing
 * COPYRIGHT:   Copyright 2003 CodeWeavers Inc. (Ulrich Czekalla)
 *              Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#ifndef ERROR_INVALID_ACL
#define ERROR_INVALID_ACL 1336L
#endif
#ifndef ERROR_INVALID_SID
#define ERROR_INVALID_SID 1337L
#endif
#ifndef ERROR_INVALID_PARAMETER
#define ERROR_INVALID_PARAMETER 87L
#endif
#ifndef ERROR_NOT_ENOUGH_MEMORY
#define ERROR_NOT_ENOUGH_MEMORY 8L
#endif
#ifndef ERROR_UNKNOWN_REVISION
#define ERROR_UNKNOWN_REVISION 1305L
#endif

typedef struct _SEP_SDDL_ALIAS
{
    WCHAR Name[2];
    UCHAR Authority;
    UCHAR SubAuthorityCount;
    ULONG SubAuthority[2];
} SEP_SDDL_ALIAS, *PSEP_SDDL_ALIAS;

typedef struct _SEP_SDDL_COMPONENT
{
    PCWSTR Start;
    PCWSTR End;
    BOOLEAN Present;
} SEP_SDDL_COMPONENT, *PSEP_SDDL_COMPONENT;

typedef struct _SEP_SDDL_ACE_DESCRIPTION
{
    UCHAR Type;
    UCHAR Flags;
    ACCESS_MASK Mask;
    ULONG SidLength;
    PCWSTR SidStart;
    PCWSTR SidEnd;
} SEP_SDDL_ACE_DESCRIPTION, *PSEP_SDDL_ACE_DESCRIPTION;

static const SEP_SDDL_ALIAS SepSddlAliases[] =
{
    {{L'W', L'D'}, 1, 1, {0, 0}},
    {{L'C', L'O'}, 3, 1, {0, 0}},
    {{L'C', L'G'}, 3, 1, {1, 0}},
    {{L'O', L'W'}, 3, 1, {4, 0}},
    {{L'N', L'U'}, 5, 1, {2, 0}},
    {{L'I', L'U'}, 5, 1, {4, 0}},
    {{L'S', L'U'}, 5, 1, {6, 0}},
    {{L'A', L'N'}, 5, 1, {7, 0}},
    {{L'E', L'D'}, 5, 1, {9, 0}},
    {{L'P', L'S'}, 5, 1, {10, 0}},
    {{L'A', L'U'}, 5, 1, {11, 0}},
    {{L'R', L'C'}, 5, 1, {12, 0}},
    {{L'S', L'Y'}, 5, 1, {18, 0}},
    {{L'L', L'S'}, 5, 1, {19, 0}},
    {{L'N', L'S'}, 5, 1, {20, 0}},
    {{L'B', L'A'}, 5, 2, {32, 544}},
    {{L'B', L'U'}, 5, 2, {32, 545}},
    {{L'B', L'G'}, 5, 2, {32, 546}},
    {{L'P', L'U'}, 5, 2, {32, 547}},
    {{L'A', L'O'}, 5, 2, {32, 548}},
    {{L'S', L'O'}, 5, 2, {32, 549}},
    {{L'P', L'O'}, 5, 2, {32, 550}},
    {{L'B', L'O'}, 5, 2, {32, 551}},
    {{L'R', L'E'}, 5, 2, {32, 552}},
    {{L'R', L'U'}, 5, 2, {32, 554}},
    {{L'R', L'D'}, 5, 2, {32, 555}},
    {{L'N', L'O'}, 5, 2, {32, 556}},
    {{L'L', L'W'}, 16, 1, {SECURITY_MANDATORY_LOW_RID, 0}},
    {{L'M', L'E'}, 16, 1, {SECURITY_MANDATORY_MEDIUM_RID, 0}},
    {{L'H', L'I'}, 16, 1, {SECURITY_MANDATORY_HIGH_RID, 0}},
    {{L'S', L'I'}, 16, 1, {SECURITY_MANDATORY_SYSTEM_RID, 0}},
    {{L'A', L'C'}, 15, 2, {SECURITY_APP_PACKAGE_BASE_RID, SECURITY_BUILTIN_PACKAGE_ANY_PACKAGE}}
};

static const struct
{
    WCHAR Name[2];
    ACCESS_MASK Value;
} SepSddlRights[] =
{
    {{L'G', L'A'}, GENERIC_ALL},
    {{L'G', L'R'}, GENERIC_READ},
    {{L'G', L'W'}, GENERIC_WRITE},
    {{L'G', L'X'}, GENERIC_EXECUTE},
    {{L'R', L'C'}, READ_CONTROL},
    {{L'S', L'D'}, DELETE},
    {{L'W', L'D'}, WRITE_DAC},
    {{L'W', L'O'}, WRITE_OWNER},
    {{L'R', L'P'}, 0x10},
    {{L'W', L'P'}, 0x20},
    {{L'C', L'C'}, 0x1},
    {{L'D', L'C'}, 0x2},
    {{L'L', L'C'}, 0x4},
    {{L'S', L'W'}, 0x8},
    {{L'L', L'O'}, 0x80},
    {{L'D', L'T'}, 0x40},
    {{L'C', L'R'}, 0x100},
    {{L'F', L'A'}, FILE_ALL_ACCESS},
    {{L'F', L'R'}, FILE_GENERIC_READ},
    {{L'F', L'W'}, FILE_GENERIC_WRITE},
    {{L'F', L'X'}, FILE_GENERIC_EXECUTE},
    {{L'K', L'A'}, KEY_ALL_ACCESS},
    {{L'K', L'R'}, KEY_READ},
    {{L'K', L'W'}, KEY_WRITE},
    {{L'K', L'X'}, KEY_EXECUTE},
    {{L'N', L'R'}, SYSTEM_MANDATORY_LABEL_NO_READ_UP},
    {{L'N', L'W'}, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP},
    {{L'N', L'X'}, SYSTEM_MANDATORY_LABEL_NO_EXECUTE_UP}
};

static const struct
{
    WCHAR Name[2];
    UCHAR Value;
} SepSddlAceFlags[] =
{
    {{L'C', L'I'}, CONTAINER_INHERIT_ACE},
    {{L'F', L'A'}, FAILED_ACCESS_ACE_FLAG},
    {{L'I', L'D'}, INHERITED_ACE},
    {{L'I', L'O'}, INHERIT_ONLY_ACE},
    {{L'N', L'P'}, NO_PROPAGATE_INHERIT_ACE},
    {{L'O', L'I'}, OBJECT_INHERIT_ACE},
    {{L'S', L'A'}, SUCCESSFUL_ACCESS_ACE_FLAG}
};

static
VOID
SepSddlTrimRange(
    _Inout_ PCWSTR *Start,
    _Inout_ PCWSTR *End)
{
    while ((*Start < *End) && (**Start == L' '))
        (*Start)++;
    while ((*End > *Start) && ((*End)[-1] == L' '))
        (*End)--;
}

static
BOOLEAN
SepSddlParseUnsigned(
    _Inout_ PCWSTR *Current,
    _In_ PCWSTR End,
    _In_ ULONG Base,
    _In_ ULONGLONG Maximum,
    _Out_ PULONGLONG Value)
{
    ULONGLONG Result = 0;
    ULONG Digit;
    BOOLEAN Any = FALSE;

    while (*Current < End)
    {
        if ((**Current >= L'0') && (**Current <= L'9'))
            Digit = **Current - L'0';
        else if ((**Current >= L'a') && (**Current <= L'f'))
            Digit = **Current - L'a' + 10;
        else if ((**Current >= L'A') && (**Current <= L'F'))
            Digit = **Current - L'A' + 10;
        else
            break;
        if ((Digit >= Base) || (Result > (Maximum - Digit) / Base))
            return FALSE;
        Result = Result * Base + Digit;
        (*Current)++;
        Any = TRUE;
    }

    *Value = Result;
    return Any;
}

static
NTSTATUS
SepSddlParseNumericSid(
    _In_ PCWSTR Start,
    _In_ PCWSTR End,
    _Out_writes_bytes_opt_(*SidLength) PSID Sid,
    _Out_ PULONG SidLength)
{
    SID_IDENTIFIER_AUTHORITY Authority = {{0, 0, 0, 0, 0, 0}};
    ULONG SubAuthorities[SID_MAX_SUB_AUTHORITIES];
    ULONGLONG Value;
    ULONG Base;
    ULONG Index;
    ULONG Count = 0;
    PCWSTR Current = Start;
    NTSTATUS Status;

    if (((End - Start) < 5) || ((Start[0] != L'S') && (Start[0] != L's')) || (Start[1] != L'-'))
        return NTSTATUS_FROM_WIN32(ERROR_INVALID_SID);
    Current += 2;
    if (!SepSddlParseUnsigned(&Current, End, 10, UCHAR_MAX, &Value) || (Value != SID_REVISION) || (Current >= End) || (*Current != L'-'))
        return NTSTATUS_FROM_WIN32(ERROR_INVALID_SID);
    Current++;
    Base = 10;
    if (((End - Current) > 2) && (Current[0] == L'0') && ((Current[1] == L'x') || (Current[1] == L'X')))
    {
        Base = 16;
        Current += 2;
    }
    if (!SepSddlParseUnsigned(&Current, End, Base, 0xFFFFFFFFFFFFULL, &Value))
        return NTSTATUS_FROM_WIN32(ERROR_INVALID_SID);
    for (Index = 0; Index != sizeof(Authority.Value); Index++)
        Authority.Value[sizeof(Authority.Value) - Index - 1] = (UCHAR)(Value >> (Index * 8));

    while (Current < End)
    {
        if ((*Current != L'-') || (Count == SID_MAX_SUB_AUTHORITIES))
            return NTSTATUS_FROM_WIN32(ERROR_INVALID_SID);
        Current++;
        Base = 10;
        if (((End - Current) > 2) && (Current[0] == L'0') && ((Current[1] == L'x') || (Current[1] == L'X')))
        {
            Base = 16;
            Current += 2;
        }
        if (!SepSddlParseUnsigned(&Current, End, Base, MAXULONG, &Value))
            return NTSTATUS_FROM_WIN32(ERROR_INVALID_SID);
        SubAuthorities[Count++] = (ULONG)Value;
    }
    if (Count == 0)
        return NTSTATUS_FROM_WIN32(ERROR_INVALID_SID);

    *SidLength = RtlLengthRequiredSid((UCHAR)Count);
    if (Sid == NULL)
        return STATUS_SUCCESS;
    Status = RtlInitializeSid(Sid, &Authority, (UCHAR)Count);
    if (!NT_SUCCESS(Status))
        return Status;
    for (Index = 0; Index != Count; Index++)
        *RtlSubAuthoritySid(Sid, Index) = SubAuthorities[Index];
    return STATUS_SUCCESS;
}

static
NTSTATUS
SepSddlParseSid(
    _In_ PCWSTR Start,
    _In_ PCWSTR End,
    _Out_writes_bytes_opt_(*SidLength) PSID Sid,
    _Out_ PULONG SidLength)
{
    SID_IDENTIFIER_AUTHORITY Authority = {{0, 0, 0, 0, 0, 0}};
    const SEP_SDDL_ALIAS *Alias;
    ULONG Index;
    NTSTATUS Status;

    SepSddlTrimRange(&Start, &End);
    if (((End - Start) >= 2) && ((Start[0] == L'S') || (Start[0] == L's')) && (Start[1] == L'-'))
        return SepSddlParseNumericSid(Start, End, Sid, SidLength);
    if ((End - Start) != 2)
        return NTSTATUS_FROM_WIN32(ERROR_INVALID_SID);

    for (Index = 0; Index != RTL_NUMBER_OF(SepSddlAliases); Index++)
    {
        Alias = &SepSddlAliases[Index];
        if ((Alias->Name[0] != Start[0]) || (Alias->Name[1] != Start[1]))
            continue;
        *SidLength = RtlLengthRequiredSid(Alias->SubAuthorityCount);
        if (Sid == NULL)
            return STATUS_SUCCESS;
        Authority.Value[5] = Alias->Authority;
        Status = RtlInitializeSid(Sid, &Authority, Alias->SubAuthorityCount);
        if (!NT_SUCCESS(Status))
            return Status;
        for (Index = 0; Index != Alias->SubAuthorityCount; Index++)
            *RtlSubAuthoritySid(Sid, Index) = Alias->SubAuthority[Index];
        return STATUS_SUCCESS;
    }
    return NTSTATUS_FROM_WIN32(ERROR_INVALID_SID);
}

static
NTSTATUS
SepSddlParseAceFlags(
    _In_ PCWSTR Start,
    _In_ PCWSTR End,
    _Out_ PUCHAR Flags)
{
    ULONG Index;

    *Flags = 0;
    SepSddlTrimRange(&Start, &End);
    while (Start < End)
    {
        if ((End - Start) < 2)
            return NTSTATUS_FROM_WIN32(ERROR_INVALID_ACL);
        for (Index = 0; Index != RTL_NUMBER_OF(SepSddlAceFlags); Index++)
        {
            if ((SepSddlAceFlags[Index].Name[0] == Start[0]) && (SepSddlAceFlags[Index].Name[1] == Start[1]))
                break;
        }
        if (Index == RTL_NUMBER_OF(SepSddlAceFlags))
            return NTSTATUS_FROM_WIN32(ERROR_INVALID_ACL);
        *Flags |= SepSddlAceFlags[Index].Value;
        Start += 2;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
SepSddlParseRights(
    _In_ PCWSTR Start,
    _In_ PCWSTR End,
    _Out_ PACCESS_MASK Mask)
{
    ULONGLONG Value;
    ULONG Index;
    PCWSTR Current;

    *Mask = 0;
    SepSddlTrimRange(&Start, &End);
    if (Start == End)
        return STATUS_SUCCESS;
    if (((End - Start) > 2) && (Start[0] == L'0') && ((Start[1] == L'x') || (Start[1] == L'X')))
    {
        Current = Start + 2;
        if (!SepSddlParseUnsigned(&Current, End, 16, MAXULONG, &Value) || (Current != End))
            return NTSTATUS_FROM_WIN32(ERROR_INVALID_ACL);
        *Mask = (ACCESS_MASK)Value;
        return STATUS_SUCCESS;
    }
    if ((Start[0] >= L'0') && (Start[0] <= L'9'))
    {
        Current = Start;
        if (!SepSddlParseUnsigned(&Current, End, 10, MAXULONG, &Value) || (Current != End))
            return NTSTATUS_FROM_WIN32(ERROR_INVALID_ACL);
        *Mask = (ACCESS_MASK)Value;
        return STATUS_SUCCESS;
    }

    while (Start < End)
    {
        if ((End - Start) < 2)
            return NTSTATUS_FROM_WIN32(ERROR_INVALID_ACL);
        for (Index = 0; Index != RTL_NUMBER_OF(SepSddlRights); Index++)
        {
            if ((SepSddlRights[Index].Name[0] == Start[0]) && (SepSddlRights[Index].Name[1] == Start[1]))
                break;
        }
        if (Index == RTL_NUMBER_OF(SepSddlRights))
            return NTSTATUS_FROM_WIN32(ERROR_INVALID_ACL);
        *Mask |= SepSddlRights[Index].Value;
        Start += 2;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
SepSddlDescribeAce(
    _In_ PCWSTR Start,
    _In_ PCWSTR End,
    _Out_ PSEP_SDDL_ACE_DESCRIPTION Description)
{
    PCWSTR FieldStart[6];
    PCWSTR FieldEnd[6];
    PCWSTR Current;
    ULONG Field;
    NTSTATUS Status;

    RtlZeroMemory(Description, sizeof(*Description));
    if (((End - Start) < 2) || (*Start != L'(') || (End[-1] != L')'))
        return NTSTATUS_FROM_WIN32(ERROR_INVALID_ACL);
    Current = Start + 1;
    for (Field = 0; Field != 5; Field++)
    {
        FieldStart[Field] = Current;
        while ((Current < End - 1) && (*Current != L';'))
            Current++;
        if (Current == End - 1)
            return NTSTATUS_FROM_WIN32(ERROR_INVALID_ACL);
        FieldEnd[Field] = Current++;
    }
    FieldStart[5] = Current;
    FieldEnd[5] = End - 1;

    SepSddlTrimRange(&FieldStart[0], &FieldEnd[0]);
    if (((FieldEnd[0] - FieldStart[0]) == 1) && (FieldStart[0][0] == L'A'))
        Description->Type = ACCESS_ALLOWED_ACE_TYPE;
    else if (((FieldEnd[0] - FieldStart[0]) == 1) && (FieldStart[0][0] == L'D'))
        Description->Type = ACCESS_DENIED_ACE_TYPE;
    else if (((FieldEnd[0] - FieldStart[0]) == 2) && (FieldStart[0][0] == L'A') && (FieldStart[0][1] == L'U'))
        Description->Type = SYSTEM_AUDIT_ACE_TYPE;
    else if (((FieldEnd[0] - FieldStart[0]) == 2) && (FieldStart[0][0] == L'A') && (FieldStart[0][1] == L'L'))
        Description->Type = SYSTEM_ALARM_ACE_TYPE;
    else if (((FieldEnd[0] - FieldStart[0]) == 2) && (FieldStart[0][0] == L'M') && (FieldStart[0][1] == L'L'))
        Description->Type = SYSTEM_MANDATORY_LABEL_ACE_TYPE;
    else
        return NTSTATUS_FROM_WIN32(ERROR_INVALID_ACL);

    Status = SepSddlParseAceFlags(FieldStart[1], FieldEnd[1], &Description->Flags);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = SepSddlParseRights(FieldStart[2], FieldEnd[2], &Description->Mask);
    if (!NT_SUCCESS(Status))
        return Status;
    SepSddlTrimRange(&FieldStart[3], &FieldEnd[3]);
    SepSddlTrimRange(&FieldStart[4], &FieldEnd[4]);
    if ((FieldStart[3] != FieldEnd[3]) || (FieldStart[4] != FieldEnd[4]))
        return NTSTATUS_FROM_WIN32(ERROR_INVALID_ACL);
    Description->SidStart = FieldStart[5];
    Description->SidEnd = FieldEnd[5];
    return SepSddlParseSid(Description->SidStart, Description->SidEnd, NULL, &Description->SidLength);
}

static
NTSTATUS
SepSddlParseAcl(
    _In_ PCWSTR Start,
    _In_ PCWSTR End,
    _In_ BOOLEAN Sacl,
    _Out_writes_bytes_opt_(*AclLength) PACL Acl,
    _Out_ PULONG AclLength,
    _Out_ PSECURITY_DESCRIPTOR_CONTROL Control)
{
    SEP_SDDL_ACE_DESCRIPTION Description;
    PACCESS_ALLOWED_ACE Ace;
    ULONG AceCount = 0;
    ULONG Length = sizeof(ACL);
    ULONG AceLength;
    PCWSTR AceEnd;
    NTSTATUS Status;

    *Control = Sacl ? SE_SACL_PRESENT : SE_DACL_PRESENT;
    SepSddlTrimRange(&Start, &End);
    while ((Start < End) && (*Start != L'('))
    {
        if (*Start == L'P')
        {
            *Control |= Sacl ? SE_SACL_PROTECTED : SE_DACL_PROTECTED;
            Start++;
        }
        else if (((End - Start) >= 2) && (Start[0] == L'A') && (Start[1] == L'R'))
        {
            *Control |= Sacl ? SE_SACL_AUTO_INHERIT_REQ : SE_DACL_AUTO_INHERIT_REQ;
            Start += 2;
        }
        else if (((End - Start) >= 2) && (Start[0] == L'A') && (Start[1] == L'I'))
        {
            *Control |= Sacl ? SE_SACL_AUTO_INHERITED : SE_DACL_AUTO_INHERITED;
            Start += 2;
        }
        else if (*Start == L' ')
            Start++;
        else
            return NTSTATUS_FROM_WIN32(ERROR_INVALID_ACL);
    }

    while (Start < End)
    {
        if (*Start != L'(')
            return NTSTATUS_FROM_WIN32(ERROR_INVALID_ACL);
        AceEnd = Start + 1;
        while ((AceEnd < End) && (*AceEnd != L')'))
            AceEnd++;
        if (AceEnd == End)
            return NTSTATUS_FROM_WIN32(ERROR_INVALID_ACL);
        AceEnd++;
        Status = SepSddlDescribeAce(Start, AceEnd, &Description);
        if (!NT_SUCCESS(Status))
            return Status;
        AceLength = FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + Description.SidLength;
        if ((AceLength > MAXUSHORT) || (Length > MAXUSHORT - AceLength))
            return NTSTATUS_FROM_WIN32(ERROR_INVALID_ACL);
        if (Acl != NULL)
        {
            Ace = (PACCESS_ALLOWED_ACE)((PUCHAR)Acl + Length);
            Ace->Header.AceType = Description.Type;
            Ace->Header.AceFlags = Description.Flags;
            Ace->Header.AceSize = (USHORT)AceLength;
            Ace->Mask = Description.Mask;
            Status = SepSddlParseSid(Description.SidStart, Description.SidEnd, (PSID)&Ace->SidStart, &Description.SidLength);
            if (!NT_SUCCESS(Status))
                return Status;
        }
        Length += AceLength;
        AceCount++;
        Start = AceEnd;
        while ((Start < End) && (*Start == L' '))
            Start++;
    }

    *AclLength = Length;
    if (Acl != NULL)
    {
        Acl->AclRevision = ACL_REVISION;
        Acl->Sbz1 = 0;
        Acl->AclSize = (USHORT)Length;
        Acl->AceCount = (USHORT)AceCount;
        Acl->Sbz2 = 0;
    }
    return STATUS_SUCCESS;
}

static
BOOLEAN
SepSddlIsComponentName(
    _In_ WCHAR Character)
{
    return (Character == L'O') || (Character == L'G') || (Character == L'D') || (Character == L'S');
}

static
NTSTATUS
SepSddlSplitComponents(
    _In_ PCWSTR String,
    _Out_ PSEP_SDDL_COMPONENT Owner,
    _Out_ PSEP_SDDL_COMPONENT Group,
    _Out_ PSEP_SDDL_COMPONENT Dacl,
    _Out_ PSEP_SDDL_COMPONENT Sacl)
{
    PSEP_SDDL_COMPONENT Component;
    PCWSTR Current = String;
    PCWSTR End = String;
    PCWSTR ValueEnd;
    ULONG Parentheses;
    WCHAR Type;

    RtlZeroMemory(Owner, sizeof(*Owner));
    RtlZeroMemory(Group, sizeof(*Group));
    RtlZeroMemory(Dacl, sizeof(*Dacl));
    RtlZeroMemory(Sacl, sizeof(*Sacl));
    while (*End != UNICODE_NULL)
        End++;
    while (Current < End)
    {
        while ((Current < End) && (*Current == L' '))
            Current++;
        if (Current == End)
            break;
        if (((End - Current) < 2) || !SepSddlIsComponentName(Current[0]) || (Current[1] != L':'))
            return NTSTATUS_FROM_WIN32(ERROR_INVALID_PARAMETER);
        Type = Current[0];
        Component = Type == L'O' ? Owner : Type == L'G' ? Group : Type == L'D' ? Dacl : Sacl;
        if (Component->Present)
            return NTSTATUS_FROM_WIN32(ERROR_INVALID_PARAMETER);
        Current += 2;
        Component->Start = Current;
        Component->Present = TRUE;
        Parentheses = 0;
        ValueEnd = Current;
        while (ValueEnd < End)
        {
            if (*ValueEnd == L'(')
                Parentheses++;
            else if ((*ValueEnd == L')') && (Parentheses != 0))
                Parentheses--;
            if ((Parentheses == 0) && ((End - ValueEnd) >= 2) && SepSddlIsComponentName(ValueEnd[0]) && (ValueEnd[1] == L':'))
                break;
            ValueEnd++;
        }
        if (Parentheses != 0)
            return NTSTATUS_FROM_WIN32(ERROR_INVALID_ACL);
        Component->End = ValueEnd;
        Current = ValueEnd;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
SeConvertStringSecurityDescriptorToSecurityDescriptor(
    _In_ PCWSTR StringSecurityDescriptor,
    _In_ ULONG StringSecurityDescriptorRevision,
    _Outptr_ PSECURITY_DESCRIPTOR *SecurityDescriptor,
    _Out_opt_ PULONG SecurityDescriptorSize)
{
    SEP_SDDL_COMPONENT Owner;
    SEP_SDDL_COMPONENT Group;
    SEP_SDDL_COMPONENT Dacl;
    SEP_SDDL_COMPONENT Sacl;
    PISECURITY_DESCRIPTOR_RELATIVE RelativeDescriptor;
    SECURITY_DESCRIPTOR_CONTROL DaclControl = 0;
    SECURITY_DESCRIPTOR_CONTROL SaclControl = 0;
    ULONG OwnerLength = 0;
    ULONG GroupLength = 0;
    ULONG DaclLength = 0;
    ULONG SaclLength = 0;
    ULONG Length;
    ULONG Offset;
    NTSTATUS Status;

    if ((StringSecurityDescriptor == NULL) || (SecurityDescriptor == NULL))
        return NTSTATUS_FROM_WIN32(ERROR_INVALID_PARAMETER);
    if (StringSecurityDescriptorRevision != 1)
        return NTSTATUS_FROM_WIN32(ERROR_UNKNOWN_REVISION);
    if (SecurityDescriptorSize != NULL)
        *SecurityDescriptorSize = 0;

    Status = SepSddlSplitComponents(StringSecurityDescriptor, &Owner, &Group, &Dacl, &Sacl);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Owner.Present)
    {
        Status = SepSddlParseSid(Owner.Start, Owner.End, NULL, &OwnerLength);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    if (Group.Present)
    {
        Status = SepSddlParseSid(Group.Start, Group.End, NULL, &GroupLength);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    if (Dacl.Present)
    {
        Status = SepSddlParseAcl(Dacl.Start, Dacl.End, FALSE, NULL, &DaclLength, &DaclControl);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    if (Sacl.Present)
    {
        Status = SepSddlParseAcl(Sacl.Start, Sacl.End, TRUE, NULL, &SaclLength, &SaclControl);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Length = sizeof(*RelativeDescriptor) + SaclLength + DaclLength + OwnerLength + GroupLength;
    RelativeDescriptor = ExAllocatePoolWithTag(PagedPool, Length, TAG_SD);
    if (RelativeDescriptor == NULL)
        return NTSTATUS_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY);
    RtlZeroMemory(RelativeDescriptor, Length);
    RelativeDescriptor->Revision = SECURITY_DESCRIPTOR_REVISION;
    RelativeDescriptor->Control = SE_SELF_RELATIVE | DaclControl | SaclControl;
    Offset = sizeof(*RelativeDescriptor);

    if (Sacl.Present)
    {
        RelativeDescriptor->Sacl = Offset;
        Status = SepSddlParseAcl(Sacl.Start, Sacl.End, TRUE, (PACL)((PUCHAR)RelativeDescriptor + Offset), &SaclLength, &SaclControl);
        if (!NT_SUCCESS(Status))
            goto Failure;
        Offset += SaclLength;
    }
    if (Dacl.Present)
    {
        RelativeDescriptor->Dacl = Offset;
        Status = SepSddlParseAcl(Dacl.Start, Dacl.End, FALSE, (PACL)((PUCHAR)RelativeDescriptor + Offset), &DaclLength, &DaclControl);
        if (!NT_SUCCESS(Status))
            goto Failure;
        Offset += DaclLength;
    }
    if (Owner.Present)
    {
        RelativeDescriptor->Owner = Offset;
        Status = SepSddlParseSid(Owner.Start, Owner.End, (PSID)((PUCHAR)RelativeDescriptor + Offset), &OwnerLength);
        if (!NT_SUCCESS(Status))
            goto Failure;
        Offset += OwnerLength;
    }
    if (Group.Present)
    {
        RelativeDescriptor->Group = Offset;
        Status = SepSddlParseSid(Group.Start, Group.End, (PSID)((PUCHAR)RelativeDescriptor + Offset), &GroupLength);
        if (!NT_SUCCESS(Status))
            goto Failure;
    }

    *SecurityDescriptor = RelativeDescriptor;
    if (SecurityDescriptorSize != NULL)
        *SecurityDescriptorSize = Length;
    return STATUS_SUCCESS;

Failure:
    ExFreePoolWithTag(RelativeDescriptor, TAG_SD);
    return Status;
}
