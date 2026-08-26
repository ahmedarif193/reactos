/*
 * PROJECT:         ReactOS Operating System
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Process-manager API-set namespace services
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include <apisetsp.h>

#define NDEBUG
#include <debug.h>

/* TYPES *********************************************************************/

typedef struct _PSP_API_SET_NAMESPACE
{
    ULONG Version;
    ULONG Size;
    ULONG Flags;
    ULONG Count;
    ULONG EntryOffset;
    ULONG HashOffset;
    ULONG HashFactor;
} PSP_API_SET_NAMESPACE, *PPSP_API_SET_NAMESPACE;

typedef struct _PSP_API_SET_NAMESPACE_ENTRY
{
    ULONG Flags;
    ULONG NameOffset;
    ULONG NameLength;
    ULONG HashedLength;
    ULONG ValueOffset;
    ULONG ValueCount;
} PSP_API_SET_NAMESPACE_ENTRY, *PPSP_API_SET_NAMESPACE_ENTRY;

typedef struct _PSP_API_SET_VALUE_ENTRY
{
    ULONG Flags;
    ULONG NameOffset;
    ULONG NameLength;
    ULONG ValueOffset;
    ULONG ValueLength;
} PSP_API_SET_VALUE_ENTRY, *PPSP_API_SET_VALUE_ENTRY;

typedef struct _PSP_API_SET_HASH_ENTRY
{
    ULONG Hash;
    ULONG Index;
} PSP_API_SET_HASH_ENTRY, *PPSP_API_SET_HASH_ENTRY;

/* GLOBALS *******************************************************************/

static PPSP_API_SET_NAMESPACE PspApiSetSchema;

/* PRIVATE FUNCTIONS *********************************************************/

static
ULONG
PspApiSetHashedLength(
    _In_ PCUNICODE_STRING Name)
{
    ULONG CharacterCount = Name->Length / sizeof(WCHAR);
    ULONG Index;

    for (Index = CharacterCount; Index != 0; --Index)
    {
        if (Name->Buffer[Index - 1] == L'-')
            return (Index - 1) * sizeof(WCHAR);
    }

    return Name->Length;
}

static
ULONG
PspApiSetHashName(
    _In_ PCUNICODE_STRING Name,
    _In_ ULONG HashedLength)
{
    ULONG CharacterCount = HashedLength / sizeof(WCHAR);
    ULONG Hash = 0;
    ULONG Index;
    WCHAR Character;

    for (Index = 0; Index != CharacterCount; ++Index)
    {
        Character = RtlDowncaseUnicodeChar(Name->Buffer[Index]);
        Hash = Hash * 31 + Character;
    }

    return Hash;
}

static
int
__cdecl
PspCompareApiSetHashes(
    _In_ const void *First,
    _In_ const void *Second)
{
    const PSP_API_SET_HASH_ENTRY *FirstEntry = First;
    const PSP_API_SET_HASH_ENTRY *SecondEntry = Second;

    if (FirstEntry->Hash < SecondEntry->Hash)
        return -1;
    if (FirstEntry->Hash > SecondEntry->Hash)
        return 1;
    if (FirstEntry->Index < SecondEntry->Index)
        return -1;
    if (FirstEntry->Index > SecondEntry->Index)
        return 1;
    return 0;
}

/* PUBLIC FUNCTIONS **********************************************************/

BOOLEAN
NTAPI
PspInitializeApiSetSchema(VOID)
{
    PPSP_API_SET_NAMESPACE_ENTRY NamespaceEntry;
    PPSP_API_SET_VALUE_ENTRY ValueEntry;
    PPSP_API_SET_HASH_ENTRY HashEntry;
    PPSP_API_SET_NAMESPACE Schema;
    SIZE_T StringsSize = 0;
    SIZE_T SchemaSize;
    ULONG StringOffset;
    ULONG EntryCount = 0;
    ULONG HashedLength;
    ULONG TableIndex;
    ULONG EntryIndex;

    for (TableIndex = 0; TableIndex != (ULONG)g_ApisetsCount; ++TableIndex)
    {
        if ((g_Apisets[TableIndex].dwOsVersions & APISET_WIN10) == 0)
            continue;
        EntryCount++;
        StringsSize += g_Apisets[TableIndex].Name.Length;
        StringsSize += g_Apisets[TableIndex].Target.Length;
    }

    SchemaSize = sizeof(*Schema) +
                 EntryCount * sizeof(*NamespaceEntry) +
                 EntryCount * sizeof(*ValueEntry) +
                 StringsSize;
    SchemaSize = ALIGN_UP_BY(SchemaSize, sizeof(ULONG));
    SchemaSize += EntryCount * sizeof(*HashEntry);
    if ((EntryCount == 0) || (SchemaSize > MAXULONG))
        return FALSE;

    Schema = ExAllocatePoolZero(NonPagedPoolNx, SchemaSize, TAG_PS_API_SET);
    if (Schema == NULL)
        return FALSE;

    Schema->Version = 6;
    Schema->Size = (ULONG)SchemaSize;
    Schema->Count = EntryCount;
    Schema->EntryOffset = sizeof(*Schema);
    Schema->HashFactor = 31;

    NamespaceEntry = (PVOID)((PUCHAR)Schema + Schema->EntryOffset);
    ValueEntry = (PVOID)(NamespaceEntry + EntryCount);
    StringOffset = (ULONG)((PUCHAR)(ValueEntry + EntryCount) - (PUCHAR)Schema);
    Schema->HashOffset = (ULONG)ALIGN_UP_BY(StringOffset + StringsSize, sizeof(ULONG));
    HashEntry = (PVOID)((PUCHAR)Schema + Schema->HashOffset);

    EntryIndex = 0;
    for (TableIndex = 0; TableIndex != (ULONG)g_ApisetsCount; ++TableIndex)
    {
        if ((g_Apisets[TableIndex].dwOsVersions & APISET_WIN10) == 0)
            continue;

        HashedLength = PspApiSetHashedLength(&g_Apisets[TableIndex].Name);
        NamespaceEntry[EntryIndex].Flags = 1;
        NamespaceEntry[EntryIndex].NameOffset = StringOffset;
        NamespaceEntry[EntryIndex].NameLength = g_Apisets[TableIndex].Name.Length;
        NamespaceEntry[EntryIndex].HashedLength = HashedLength;
        NamespaceEntry[EntryIndex].ValueOffset =
            (ULONG)((PUCHAR)&ValueEntry[EntryIndex] - (PUCHAR)Schema);
        NamespaceEntry[EntryIndex].ValueCount = 1;
        RtlCopyMemory((PUCHAR)Schema + StringOffset,
                      g_Apisets[TableIndex].Name.Buffer,
                      g_Apisets[TableIndex].Name.Length);
        StringOffset += g_Apisets[TableIndex].Name.Length;

        ValueEntry[EntryIndex].ValueOffset = StringOffset;
        ValueEntry[EntryIndex].ValueLength = g_Apisets[TableIndex].Target.Length;
        RtlCopyMemory((PUCHAR)Schema + StringOffset,
                      g_Apisets[TableIndex].Target.Buffer,
                      g_Apisets[TableIndex].Target.Length);
        StringOffset += g_Apisets[TableIndex].Target.Length;

        HashEntry[EntryIndex].Hash =
            PspApiSetHashName(&g_Apisets[TableIndex].Name, HashedLength);
        HashEntry[EntryIndex].Index = EntryIndex;
        EntryIndex++;
    }

    qsort(HashEntry, EntryCount, sizeof(*HashEntry), PspCompareApiSetHashes);
    PspApiSetSchema = Schema;
    return TRUE;
}

PVOID
NTAPI
PsQueryCurrentApiSetSchema(VOID)
{
    return PspApiSetSchema;
}
