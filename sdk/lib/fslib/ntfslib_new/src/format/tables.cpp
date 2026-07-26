/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS volume formatter: $UpCase, $AttrDef and default ACL
 */

#include "formatint.h"

/*
 * $UpCase is the volume's own case-folding table: the driver reads it back
 * from the volume rather than assuming one, so the table only has to be
 * self-consistent. Deriving it from the system's NLS data keeps it consistent
 * with the rest of the OS as well.
 */
void
FormatBuildUpCaseTable(_Out_ PWCHAR Table)
{
    ULONG Index;

    for (Index = 0; Index < NTFS_UPCASE_LENGTH; Index++)
        Table[Index] = (WCHAR)Index;

    /* Surrogates are not independently case-folded. */
    for (Index = 0; Index < 0xD800; Index++)
        Table[Index] = RtlUpcaseUnicodeChar((WCHAR)Index);

    for (Index = 0xE000; Index < NTFS_UPCASE_LENGTH; Index++)
        Table[Index] = RtlUpcaseUnicodeChar((WCHAR)Index);
}

typedef struct FormatAttrDefTemplate
{
    PCWSTR Label;
    ULONG AttributeType;
    ULONG CollationRule;
    ULONG Flags;
    ULONGLONG MinimumSize;
    ULONGLONG MaximumSize;
} FormatAttrDefTemplate;

static const FormatAttrDefTemplate FormatAttrDefTemplates[] =
{
    { L"$STANDARD_INFORMATION", 0x10,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_RESIDENT, 48, 72 },
    { L"$ATTRIBUTE_LIST",       0x20,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_NON_RESIDENT, 0, 0 },
    { L"$FILE_NAME",            0x30,  ATTRDEF_COLLATION_FILENAME,
      ATTRDEF_INDEXED | ATTRDEF_RESIDENT, 68, 578 },
    { L"$OBJECT_ID",            0x40,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_RESIDENT, 0, 256 },
    { L"$SECURITY_DESCRIPTOR",  0x50,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_NON_RESIDENT, 0, 0 },
    { L"$VOLUME_NAME",          0x60,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_RESIDENT, 2, 256 },
    { L"$VOLUME_INFORMATION",   0x70,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_RESIDENT, 12, 12 },
    { L"$DATA",                 0x80,  ATTRDEF_COLLATION_BINARY,
      0, 0, 0 },
    { L"$INDEX_ROOT",           0x90,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_RESIDENT, 0, 0 },
    { L"$INDEX_ALLOCATION",     0xA0,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_NON_RESIDENT, 0, 0 },
    { L"$BITMAP",               0xB0,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_NON_RESIDENT, 0, 0 },
    { L"$REPARSE_POINT",        0xC0,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_NON_RESIDENT, 0, 16384 },
    { L"$EA_INFORMATION",       0xD0,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_RESIDENT, 8, 8 },
    { L"$EA",                   0xE0,  ATTRDEF_COLLATION_BINARY,
      0, 0, 65536 },
    { L"$LOGGED_UTILITY_STREAM", 0x100, ATTRDEF_COLLATION_BINARY,
      ATTRDEF_NON_RESIDENT, 0, 65536 },
};

C_ASSERT(RTL_NUMBER_OF(FormatAttrDefTemplates) == NTFS_ATTRDEF_ENTRIES - 1);

/*
 * The table is terminated by a zeroed entry: Volume::LoadAttributeDefinitions
 * stops at the first entry whose AttributeType is 0.
 */
void
FormatBuildAttrDefTable(_Out_ PAttrDefEntry Table)
{
    ULONG Index;

    RtlZeroMemory(Table, NTFS_ATTRDEF_SIZE);

    for (Index = 0; Index < RTL_NUMBER_OF(FormatAttrDefTemplates); Index++)
    {
        const FormatAttrDefTemplate* Template = &FormatAttrDefTemplates[Index];
        ULONG Character;

        for (Character = 0;
             Character < RTL_NUMBER_OF(Table[Index].Label) - 1 &&
             Template->Label[Character] != L'\0';
             Character++)
        {
            Table[Index].Label[Character] = Template->Label[Character];
        }

        Table[Index].AttributeType = Template->AttributeType;
        Table[Index].DisplayRule = 0;
        Table[Index].CollationRule = Template->CollationRule;
        Table[Index].Flags = Template->Flags;
        Table[Index].MinimumSize = Template->MinimumSize;
        Table[Index].MaximumSize = Template->MaximumSize;
    }
}

/* Self-relative security descriptor pieces, laid out by hand so that this
 * file stays free of the SID/ACL helpers, which are not available in every
 * environment ntfslib builds for. */
#define SD_CONTROL_SELF_RELATIVE 0x8000
#define SD_CONTROL_DACL_PRESENT  0x0004
#define SD_ACL_REVISION          2
#define SD_ACCESS_ALLOWED        0
#define SD_OBJECT_INHERIT        0x01
#define SD_CONTAINER_INHERIT     0x02
#define SD_FILE_ALL_ACCESS       0x001F01FF

static ULONG
FormatWriteSid(_Out_ PUCHAR Buffer,
               _In_ UCHAR Authority,
               _In_ const ULONG* SubAuthorities,
               _In_ UCHAR SubAuthorityCount)
{
    ULONG Offset = 0;
    UCHAR Index;

    Buffer[Offset++] = 1;                   /* SID revision */
    Buffer[Offset++] = SubAuthorityCount;

    /* 48 bit big-endian identifier authority. */
    Buffer[Offset++] = 0;
    Buffer[Offset++] = 0;
    Buffer[Offset++] = 0;
    Buffer[Offset++] = 0;
    Buffer[Offset++] = 0;
    Buffer[Offset++] = Authority;

    for (Index = 0; Index < SubAuthorityCount; Index++)
    {
        Buffer[Offset++] = (UCHAR)(SubAuthorities[Index]);
        Buffer[Offset++] = (UCHAR)(SubAuthorities[Index] >> 8);
        Buffer[Offset++] = (UCHAR)(SubAuthorities[Index] >> 16);
        Buffer[Offset++] = (UCHAR)(SubAuthorities[Index] >> 24);
    }

    return Offset;
}

static void
FormatWriteUlong(_Out_ PUCHAR Buffer,
                 _In_ ULONG Value)
{
    Buffer[0] = (UCHAR)Value;
    Buffer[1] = (UCHAR)(Value >> 8);
    Buffer[2] = (UCHAR)(Value >> 16);
    Buffer[3] = (UCHAR)(Value >> 24);
}

static void
FormatWriteUshort(_Out_ PUCHAR Buffer,
                  _In_ USHORT Value)
{
    Buffer[0] = (UCHAR)Value;
    Buffer[1] = (UCHAR)(Value >> 8);
}

/*
 * Builds "Everyone: full control, inheritable" owned by Administrators. This
 * mirrors what a freshly formatted Windows volume's root carries before any
 * ACL is applied, and gives the volume a valid descriptor to inherit from.
 */
ULONG
FormatBuildDefaultSecurityDescriptor(_Out_ PUCHAR Buffer,
                                     _In_ ULONG BufferLength)
{
    static const ULONG AdministratorsSubAuthorities[] = { 32, 544 };
    static const ULONG LocalSystemSubAuthorities[] = { 18 };
    static const ULONG WorldSubAuthorities[] = { 0 };

    const ULONG DaclOffset = 20;
    const ULONG AceOffset = DaclOffset + 8;
    const ULONG AceSidOffset = AceOffset + 8;
    ULONG OwnerOffset;
    ULONG GroupOffset;
    ULONG AceSidLength;
    ULONG Total;

    if (BufferLength < 128)
        return 0;

    RtlZeroMemory(Buffer, BufferLength);

    /* DACL: one inheritable allow-all ACE for S-1-1-0. */
    AceSidLength = FormatWriteSid(Buffer + AceSidOffset,
                                  1,
                                  WorldSubAuthorities,
                                  1);

    Buffer[AceOffset + 0] = SD_ACCESS_ALLOWED;
    Buffer[AceOffset + 1] = SD_OBJECT_INHERIT | SD_CONTAINER_INHERIT;
    FormatWriteUshort(Buffer + AceOffset + 2, (USHORT)(8 + AceSidLength));
    FormatWriteUlong(Buffer + AceOffset + 4, SD_FILE_ALL_ACCESS);

    Buffer[DaclOffset + 0] = SD_ACL_REVISION;
    Buffer[DaclOffset + 1] = 0;
    FormatWriteUshort(Buffer + DaclOffset + 2,
                      (USHORT)(8 + 8 + AceSidLength));
    FormatWriteUshort(Buffer + DaclOffset + 4, 1);   /* AceCount */
    FormatWriteUshort(Buffer + DaclOffset + 6, 0);

    OwnerOffset = AceSidOffset + AceSidLength;
    GroupOffset = OwnerOffset + FormatWriteSid(Buffer + OwnerOffset,
                                               5,
                                               AdministratorsSubAuthorities,
                                               2);
    Total = GroupOffset + FormatWriteSid(Buffer + GroupOffset,
                                         5,
                                         LocalSystemSubAuthorities,
                                         1);

    /* Header. */
    Buffer[0] = 1;      /* SECURITY_DESCRIPTOR_REVISION */
    Buffer[1] = 0;
    FormatWriteUshort(Buffer + 2,
                      SD_CONTROL_SELF_RELATIVE | SD_CONTROL_DACL_PRESENT);
    FormatWriteUlong(Buffer + 4, OwnerOffset);
    FormatWriteUlong(Buffer + 8, GroupOffset);
    FormatWriteUlong(Buffer + 12, 0);           /* no SACL */
    FormatWriteUlong(Buffer + 16, DaclOffset);

    return Total;
}
