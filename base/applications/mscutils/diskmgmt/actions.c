/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Command/action dispatch helpers.
 */

#include "precomp.h"

#include <fmifs/fmifs.h>
#include <mountmgrutil.h>

typedef enum _DM_ACTION_REQUIREMENT
{
    DmActionRequiresNone = 0,
    DmActionRequiresDisk,
    DmActionRequiresVolume,
    DmActionRequiresSelection
} DM_ACTION_REQUIREMENT;

typedef struct _DM_ACTION_ENTRY
{
    UINT CommandId;
    UINT VerbStringId;
    UINT StatusStringId;
    DM_ACTION_REQUIREMENT Requirement;
    BOOLEAN Mutating;
} DM_ACTION_ENTRY;

typedef struct _DM_ASSIGN_LETTER_DIALOG
{
    WCHAR Letters[26];
    ULONG LetterCount;
    WCHAR SelectedLetter;
} DM_ASSIGN_LETTER_DIALOG, *PDM_ASSIGN_LETTER_DIALOG;

typedef struct _DM_CREATE_PARTITION_DIALOG
{
    ULONGLONG MaxSizeMb;
    ULONGLONG LogicalMaxSizeMb;
    ULONGLONG SelectedSizeMb;
    PARTITION_STYLE PartitionStyle;
    ULONG PartitionKinds[4];
    ULONG PartitionKindCount;
    WCHAR Letters[26];
    ULONG LetterCount;
    WCHAR SelectedLetter;
    WCHAR SelectedFileSystem[32];
    WCHAR Label[MAX_PATH];
    BOOL AssignLetter;
    BOOL FormatVolume;
    BOOL QuickFormat;
    BOOL PreferNtfs;
    ULONG PartitionKind;
    PCWSTR Caption;
} DM_CREATE_PARTITION_DIALOG, *PDM_CREATE_PARTITION_DIALOG;

typedef struct _DM_FORMAT_VOLUME_DIALOG
{
    WCHAR SelectedFileSystem[32];
    WCHAR Label[MAX_PATH];
    BOOL QuickFormat;
} DM_FORMAT_VOLUME_DIALOG, *PDM_FORMAT_VOLUME_DIALOG;

typedef struct _DM_RESIZE_VOLUME_DIALOG
{
    ULONGLONG CurrentSizeMb;
    ULONGLONG MaxDeltaMb;
    ULONGLONG SelectedDeltaMb;
} DM_RESIZE_VOLUME_DIALOG, *PDM_RESIZE_VOLUME_DIALOG;

typedef struct _DM_SELECT_MOUNT_PATH_DIALOG
{
    WCHAR Paths[DM_MAX_MOUNT_PATHS][MAX_PATH];
    ULONG PathCount;
    ULONG SelectedIndex;
    BOOL AllowReplace;
    BOOL ReplaceExisting;
    PCWSTR Caption;
    PCWSTR Prompt;
} DM_SELECT_MOUNT_PATH_DIALOG, *PDM_SELECT_MOUNT_PATH_DIALOG;

typedef enum _DM_FORMAT_ERROR
{
    DmFormatErrorNone = 0,
    DmFormatErrorVolumeInUse,
    DmFormatErrorInsufficientRights,
    DmFormatErrorFsNotSupported,
    DmFormatErrorClusterSizeTooSmall,
    DmFormatErrorUnknown
} DM_FORMAT_ERROR;

typedef struct _DM_FORMAT_RUNTIME_CONTEXT
{
    const DM_ACTION_CONTEXT *ActionContext;
    WCHAR DriveRoot[4];
    WCHAR Label[MAX_PATH];
    DWORD LastProgress;
    DM_FORMAT_ERROR Error;
    BOOL Success;
} DM_FORMAT_RUNTIME_CONTEXT, *PDM_FORMAT_RUNTIME_CONTEXT;

static PDM_FORMAT_RUNTIME_CONTEXT DmCurrentFormatContext;

static PCWSTR
DmActionGetPartitionKindFallback(
    _In_ ULONG PartitionKind);

static ULONGLONG
DmActionAlignedBytesFromSizeMb(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ ULONGLONG SizeMb,
    _In_ ULONGLONG MaxBytes);

static const DM_ACTION_ENTRY DmActionTable[] =
{
    { IDM_FILE_EXIT,               IDS_HINT_FILE_EXIT,               IDS_STATUS_READY,               DmActionRequiresNone,       FALSE },
    { IDM_ACTION_REFRESH,          IDS_HINT_ACTION_REFRESH,          IDS_STATUS_REFRESHING,          DmActionRequiresNone,       FALSE },
    { IDM_ACTION_RESCAN,           IDS_HINT_ACTION_RESCAN,           IDS_STATUS_RESCANNING,          DmActionRequiresNone,       FALSE },
    { IDM_ACTION_INITIALIZE_DISK,   IDS_HINT_ACTION_INITIALIZE_DISK,   IDS_STATUS_INITIALIZING_DISK,   DmActionRequiresDisk,       TRUE  },
    { IDM_ACTION_CONVERT_GPT,       IDS_HINT_ACTION_CONVERT_GPT,       IDS_STATUS_CONVERTING_GPT,      DmActionRequiresDisk,       TRUE  },
    { IDM_ACTION_CONVERT_MBR,       IDS_HINT_ACTION_CONVERT_MBR,       IDS_STATUS_CONVERTING_MBR,      DmActionRequiresDisk,       TRUE  },
    { IDM_ACTION_CREATE_PARTITION,  IDS_HINT_ACTION_CREATE_PARTITION,  IDS_STATUS_CREATING_PARTITION,  DmActionRequiresDisk,       TRUE  },
    { IDM_ACTION_DELETE_VOLUME,     IDS_HINT_ACTION_DELETE_VOLUME,     IDS_STATUS_DELETING_VOLUME,     DmActionRequiresVolume,     TRUE  },
    { IDM_ACTION_EXTEND_VOLUME,     IDS_HINT_ACTION_EXTEND_VOLUME,     IDS_STATUS_EXTENDING_VOLUME,    DmActionRequiresVolume,     TRUE  },
    { IDM_ACTION_SHRINK_VOLUME,     IDS_HINT_ACTION_SHRINK_VOLUME,     IDS_STATUS_SHRINKING_VOLUME,    DmActionRequiresVolume,     TRUE  },
    { IDM_ACTION_FORMAT,            IDS_HINT_ACTION_FORMAT,            IDS_STATUS_FORMATTING_VOLUME,    DmActionRequiresVolume,     TRUE  },
    { IDM_ACTION_ASSIGN_LETTER,     IDS_HINT_ACTION_ASSIGN_LETTER,     IDS_STATUS_ASSIGNING_LETTER,    DmActionRequiresVolume,     TRUE  },
    { IDM_ACTION_REMOVE_LETTER,     IDS_HINT_ACTION_REMOVE_LETTER,     IDS_STATUS_REMOVING_LETTER,     DmActionRequiresVolume,     TRUE  },
    { IDM_ACTION_CHANGE_MOUNT_PATH, IDS_HINT_ACTION_CHANGE_MOUNT_PATH, IDS_STATUS_CHANGING_MOUNT_PATH, DmActionRequiresVolume,     TRUE  },
    { IDM_ACTION_REMOVE_MOUNT_PATH, IDS_HINT_ACTION_REMOVE_MOUNT_PATH, IDS_STATUS_REMOVING_MOUNT_PATH, DmActionRequiresVolume,     TRUE  },
    { IDM_ACTION_ONLINE,            IDS_HINT_ACTION_ONLINE,            IDS_STATUS_ONLINING_DISK,       DmActionRequiresDisk,       TRUE  },
    { IDM_ACTION_OFFLINE,           IDS_HINT_ACTION_OFFLINE,           IDS_STATUS_OFFLINING_DISK,      DmActionRequiresDisk,       TRUE  },
    { IDM_ACTION_SET_READ_ONLY,     IDS_HINT_ACTION_SET_READ_ONLY,     IDS_STATUS_SETTING_READ_ONLY,   DmActionRequiresDisk,       TRUE  },
    { IDM_ACTION_CLEAR_READ_ONLY,   IDS_HINT_ACTION_CLEAR_READ_ONLY,   IDS_STATUS_CLEARING_READ_ONLY,  DmActionRequiresDisk,       TRUE  },
    { IDM_ACTION_MARK_ACTIVE,       IDS_HINT_ACTION_MARK_ACTIVE,       IDS_STATUS_MARKING_ACTIVE,      DmActionRequiresSelection,  TRUE  },
    { IDM_ACTION_MARK_INACTIVE,     IDS_HINT_ACTION_MARK_INACTIVE,     IDS_STATUS_MARKING_INACTIVE,    DmActionRequiresSelection,  TRUE  },
    { IDM_ACTION_PROPERTIES,        IDS_HINT_ACTION_PROPERTIES,        IDS_STATUS_PROPERTIES,          DmActionRequiresSelection,  FALSE },
    { IDM_HELP_ABOUT,               IDS_HINT_HELP_ABOUT,               IDS_STATUS_READY,               DmActionRequiresNone,       FALSE }
};

static const WCHAR DmActionStubPrefix[] = L"Disk Management";
static const GUID DmActionGptBasicDataPartitionGuid =
{ 0xEBD0A0A2, 0xB9E5, 0x4433, { 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 } };
static const GUID DmActionGptSystemPartitionGuid =
{ 0xC12A7328, 0xF81F, 0x11D2, { 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B } };
static const GUID DmActionGptMsftReservedPartitionGuid =
{ 0xE3C9E316, 0x0B5C, 0x4DB8, { 0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE } };
static const GUID DmActionGptRecoveryPartitionGuid =
{ 0xDE94BBA4, 0x06D1, 0x4D40, { 0xA1, 0x6A, 0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC } };
static const GUID DmActionUnusedPartitionGuid =
{ 0x00000000, 0x0000, 0x0000, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };

typedef enum _DM_CREATE_PARTITION_KIND
{
    DmCreatePartitionMbrPrimary = 0,
    DmCreatePartitionMbrExtended,
    DmCreatePartitionMbrLogical,
    DmCreatePartitionBasic,
    DmCreatePartitionEfiSystem,
    DmCreatePartitionMsr,
    DmCreatePartitionRecovery
} DM_CREATE_PARTITION_KIND;

typedef struct _DM_MBR_LOGICAL_LAYOUT_ENTRY
{
    ULONGLONG StartOffset;
    ULONGLONG PartitionLength;
    ULONG PartitionNumber;
    UCHAR PartitionType;
} DM_MBR_LOGICAL_LAYOUT_ENTRY, *PDM_MBR_LOGICAL_LAYOUT_ENTRY;

static VOID
DmActionInitializeMbrLayoutEntry(
    _Out_ PPARTITION_INFORMATION_EX Entry,
    _In_ ULONGLONG StartingOffset,
    _In_ ULONGLONG PartitionLength,
    _In_ ULONG HiddenSectors,
    _In_ UCHAR PartitionType,
    _In_ BOOLEAN Recognized);

static BOOL
DmActionExpandLayout(
    _Inout_ PDRIVE_LAYOUT_INFORMATION_EX *Layout,
    _In_ ULONG NewPartitionCount);

static VOID
DmActionFormatErrorMessage(
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer,
    _In_z_ PCWSTR Prefix,
    _In_ DWORD Error)
{
    WCHAR SystemMessage[256];
    DWORD Length;

    if (Buffer == NULL || cchBuffer == 0)
        return;

    SystemMessage[0] = UNICODE_NULL;
    Length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
                            FORMAT_MESSAGE_IGNORE_INSERTS,
                            NULL,
                            Error,
                            0,
                            SystemMessage,
                            ARRAYSIZE(SystemMessage),
                            NULL);

    while (Length > 0 &&
           (SystemMessage[Length - 1] == L'\r' ||
            SystemMessage[Length - 1] == L'\n' ||
            SystemMessage[Length - 1] == L' '))
    {
        SystemMessage[--Length] = UNICODE_NULL;
    }

    if (Length != 0)
    {
        StringCchPrintfW(Buffer,
                         cchBuffer,
                         L"%s\r\n\r\n%s (error %lu).",
                         Prefix,
                         SystemMessage,
                         Error);
    }
    else
    {
        StringCchPrintfW(Buffer,
                         cchBuffer,
                         L"%s\r\n\r\nError %lu.",
                         Prefix,
                         Error);
    }
}

static VOID
DmActionShowWin32Error(
    _In_opt_ HWND hWnd,
    _In_z_ PCWSTR Prefix,
    _In_ DWORD Error)
{
    WCHAR Message[512];

    DmActionFormatErrorMessage(Message, ARRAYSIZE(Message), Prefix, Error);
    MessageBoxW(hWnd,
                Message,
                L"Disk Management",
                MB_OK | MB_ICONERROR);
}

static BOOL
DmActionEnsureSingleExtentVolume(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_z_ PCWSTR Verb)
{
    WCHAR Message[256];

    if (Context == NULL || Context->Volume == NULL || Context->Volume->ExtentCount <= 1)
        return TRUE;

    StringCchPrintfW(Message,
                     ARRAYSIZE(Message),
                     L"%s is not supported yet for multi-extent volumes.",
                     Verb);
    MessageBoxW(Context->hWnd,
                Message,
                L"Disk Management",
                MB_OK | MB_ICONINFORMATION);
    return FALSE;
}

static BOOL
DmActionEnsureBasicDiskLayout(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_z_ PCWSTR Verb)
{
    WCHAR Message[256];

    if (Context == NULL)
        return FALSE;

    if ((Context->Disk != NULL && Context->Disk->IsDynamic) ||
        (Context->Region != NULL && Context->Region->IsDynamic) ||
        (Context->Volume != NULL && Context->Volume->IsDynamic))
    {
        StringCchPrintfW(Message,
                         ARRAYSIZE(Message),
                         L"%s is not supported yet on dynamic disks or LDM volumes.",
                         Verb);
        MessageBoxW(Context->hWnd,
                    Message,
                    L"Disk Management",
                    MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    return TRUE;
}

static BOOL
DmActionEnsureSimpleBasicVolume(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_z_ PCWSTR Verb)
{
    if (!DmActionEnsureSingleExtentVolume(Context, Verb))
        return FALSE;

    if (!DmActionEnsureBasicDiskLayout(Context, Verb))
        return FALSE;

    return TRUE;
}

static VOID
DmActionCreateGuid(
    _Out_ GUID *Guid)
{
    RtlGenRandom(Guid, sizeof(*Guid));
    Guid->Data3 &= 0x0FFF;
    Guid->Data3 |= (4 << 12);
    Guid->Data4[0] &= 0x3F;
    Guid->Data4[0] |= 0x80;
}

static VOID
DmActionCreateSignature(
    _Out_ PULONG Signature)
{
    FILETIME SystemTime;
    ULONGLONG Combined;

    GetSystemTimeAsFileTime(&SystemTime);
    Combined = ((ULONGLONG)SystemTime.dwHighDateTime << 32) | SystemTime.dwLowDateTime;
    *Signature = (ULONG)(Combined ^ (Combined >> 32) ^ GetTickCount());
    if (*Signature == 0)
        *Signature = 0xA5A5A5A5;
}

static BOOL
DmActionPartitionKindSupportsAssign(
    _In_ ULONG PartitionKind)
{
    return (PartitionKind == DmCreatePartitionMbrPrimary ||
            PartitionKind == DmCreatePartitionMbrLogical ||
            PartitionKind == DmCreatePartitionBasic);
}

static BOOL
DmActionPartitionKindSupportsFormat(
    _In_ ULONG PartitionKind)
{
    return DmActionPartitionKindSupportsAssign(PartitionKind);
}

static ULONG
DmActionGetUsedPrimaryMbrSlotCount(
    _In_ const DM_DISK *Disk)
{
    ULONG Count;
    ULONG Index;

    if (Disk == NULL)
        return 0;

    Count = Disk->HasExtendedPartition ? 1UL : 0UL;
    for (Index = 0; Index < Disk->RegionCount; Index++)
    {
        if (Disk->Regions[Index].Type == DmRegionPartition &&
            !Disk->Regions[Index].IsLogical &&
            !Disk->Regions[Index].IsHidden)
        {
            Count++;
        }
    }

    return Count;
}

static ULONG
DmActionCountMbrLogicalEntries(
    _In_ const DRIVE_LAYOUT_INFORMATION_EX *Layout)
{
    ULONG Count;
    ULONG Index;

    if (Layout == NULL || Layout->PartitionStyle != PARTITION_STYLE_MBR)
        return 0;

    Count = 0;
    for (Index = 4; Index < Layout->PartitionCount; Index++)
    {
        const PARTITION_INFORMATION_EX *Entry;

        Entry = &Layout->PartitionEntry[Index];
        if (Entry->Mbr.PartitionType == PARTITION_ENTRY_UNUSED ||
            (IsContainerPartition(Entry->Mbr.PartitionType) &&
             Index >= 4 &&
             Entry->PartitionNumber == 0) ||
            IsContainerPartition(Entry->Mbr.PartitionType) ||
            Entry->PartitionNumber == 0)
        {
            continue;
        }

        Count++;
    }

    return Count;
}

static INT __cdecl
DmActionCompareMbrLogicalLayoutEntries(
    _In_ const VOID *Left,
    _In_ const VOID *Right)
{
    const DM_MBR_LOGICAL_LAYOUT_ENTRY *LeftEntry;
    const DM_MBR_LOGICAL_LAYOUT_ENTRY *RightEntry;

    LeftEntry = (const DM_MBR_LOGICAL_LAYOUT_ENTRY *)Left;
    RightEntry = (const DM_MBR_LOGICAL_LAYOUT_ENTRY *)Right;

    if (LeftEntry->StartOffset < RightEntry->StartOffset)
        return -1;

    if (LeftEntry->StartOffset > RightEntry->StartOffset)
        return 1;

    return 0;
}

static BOOL
DmActionCollectMbrLogicalEntries(
    _In_ const DRIVE_LAYOUT_INFORMATION_EX *Layout,
    _Out_writes_(cEntries) PDM_MBR_LOGICAL_LAYOUT_ENTRY Entries,
    _In_ ULONG cEntries,
    _Out_ PULONG EntryCount)
{
    ULONG Count;
    ULONG Index;

    if (Layout == NULL || Layout->PartitionStyle != PARTITION_STYLE_MBR ||
        Entries == NULL || EntryCount == NULL)
    {
        return FALSE;
    }

    Count = 0;
    for (Index = 4; Index < Layout->PartitionCount; Index++)
    {
        const PARTITION_INFORMATION_EX *Entry;

        Entry = &Layout->PartitionEntry[Index];
        if (Entry->Mbr.PartitionType == PARTITION_ENTRY_UNUSED ||
            IsContainerPartition(Entry->Mbr.PartitionType) ||
            Entry->PartitionNumber == 0)
        {
            continue;
        }

        if (Count >= cEntries)
            return FALSE;

        Entries[Count].StartOffset = Entry->StartingOffset.QuadPart;
        Entries[Count].PartitionLength = Entry->PartitionLength.QuadPart;
        Entries[Count].PartitionNumber = Entry->PartitionNumber;
        Entries[Count].PartitionType = Entry->Mbr.PartitionType;
        Count++;
    }

    *EntryCount = Count;
    return TRUE;
}

static BOOL
DmActionRewriteMbrLogicalEntries(
    _Inout_ PDRIVE_LAYOUT_INFORMATION_EX *Layout,
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ ULONGLONG ExtendedStartOffset,
    _In_ ULONGLONG StartOffset,
    _In_ ULONGLONG PartitionLength,
    _In_ UCHAR PartitionType)
{
    DM_MBR_LOGICAL_LAYOUT_ENTRY *Entries;
    ULONG ExistingCount;
    ULONG NewCount;
    ULONG TargetPartitionCount;
    ULONG Index;
    ULONGLONG BytesPerSector;
    ULONGLONG AlignmentSectors;
    ULONGLONG ExtendedStartSector;

    if (Layout == NULL || *Layout == NULL || Context == NULL || Context->Disk == NULL)
        return FALSE;

    ExistingCount = DmActionCountMbrLogicalEntries(*Layout);
    Entries = HeapAlloc(ProcessHeap,
                        HEAP_ZERO_MEMORY,
                        sizeof(*Entries) * (ExistingCount + 1));
    if (Entries == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    if (!DmActionCollectMbrLogicalEntries(*Layout,
                                          Entries,
                                          ExistingCount + 1,
                                          &ExistingCount))
    {
        HeapFree(ProcessHeap, 0, Entries);
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }

    Entries[ExistingCount].StartOffset = StartOffset;
    Entries[ExistingCount].PartitionLength = PartitionLength;
    Entries[ExistingCount].PartitionNumber = 0;
    Entries[ExistingCount].PartitionType = PartitionType;
    NewCount = ExistingCount + 1;

    qsort(Entries,
          NewCount,
          sizeof(*Entries),
          DmActionCompareMbrLogicalLayoutEntries);

    TargetPartitionCount = 4 + (NewCount * 4);
    if ((*Layout)->PartitionCount < TargetPartitionCount &&
        !DmActionExpandLayout(Layout, TargetPartitionCount))
    {
        HeapFree(ProcessHeap, 0, Entries);
        return FALSE;
    }

    BytesPerSector = max(Context->Disk->BytesPerSector, 512ULL);
    AlignmentSectors = max((ULONGLONG)Context->Disk->SectorAlignment, 1ULL);
    ExtendedStartSector = ExtendedStartOffset / BytesPerSector;

    for (Index = 4; Index < (*Layout)->PartitionCount; Index++)
    {
        ZeroMemory(&(*Layout)->PartitionEntry[Index], sizeof((*Layout)->PartitionEntry[Index]));
        (*Layout)->PartitionEntry[Index].PartitionStyle = PARTITION_STYLE_MBR;
        (*Layout)->PartitionEntry[Index].RewritePartition = TRUE;
    }

    for (Index = 0; Index < NewCount; Index++)
    {
        ULONG EntryIndex;

        EntryIndex = 4 + (Index * 4);
        DmActionInitializeMbrLayoutEntry(&(*Layout)->PartitionEntry[EntryIndex],
                                         Entries[Index].StartOffset,
                                         Entries[Index].PartitionLength,
                                         (ULONG)AlignmentSectors,
                                         Entries[Index].PartitionType,
                                         IsRecognizedPartition(Entries[Index].PartitionType));
        (*Layout)->PartitionEntry[EntryIndex].PartitionNumber = Entries[Index].PartitionNumber;

        if (Index + 1 < NewCount)
        {
            ULONGLONG NextStartSector;
            ULONGLONG HiddenSectors;

            NextStartSector = Entries[Index + 1].StartOffset / BytesPerSector;
            HiddenSectors = NextStartSector - AlignmentSectors - ExtendedStartSector;
            DmActionInitializeMbrLayoutEntry(&(*Layout)->PartitionEntry[EntryIndex + 1],
                                             (NextStartSector - AlignmentSectors) * BytesPerSector,
                                             (NextStartSector + AlignmentSectors) * BytesPerSector,
                                             (ULONG)HiddenSectors,
                                             PARTITION_EXTENDED,
                                             FALSE);
        }
    }

    HeapFree(ProcessHeap, 0, Entries);
    return TRUE;
}

static BOOL
DmActionQueryDiskLayout(
    _In_ HANDLE Handle,
    _Outptr_ PDRIVE_LAYOUT_INFORMATION_EX *Layout)
{
    DWORD BufferSize;
    DWORD BytesReturned;
    PDRIVE_LAYOUT_INFORMATION_EX Buffer;

    if (Layout == NULL)
        return FALSE;

    *Layout = NULL;
    BufferSize = sizeof(DRIVE_LAYOUT_INFORMATION_EX) +
                 ((4 - ANYSIZE_ARRAY) * sizeof(PARTITION_INFORMATION_EX));

    for (;;)
    {
        Buffer = HeapAlloc(ProcessHeap, HEAP_ZERO_MEMORY, BufferSize);
        if (Buffer == NULL)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }

        BytesReturned = 0;
        if (DeviceIoControl(Handle,
                            IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                            NULL,
                            0,
                            Buffer,
                            BufferSize,
                            &BytesReturned,
                            NULL))
        {
            *Layout = Buffer;
            return TRUE;
        }

        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
            HeapFree(ProcessHeap, 0, Buffer);
            return FALSE;
        }

        HeapFree(ProcessHeap, 0, Buffer);
        BufferSize += 4 * sizeof(PARTITION_INFORMATION_EX);
    }
}

static PPARTITION_INFORMATION_EX
DmActionFindLayoutPartition(
    _Inout_ PDRIVE_LAYOUT_INFORMATION_EX Layout,
    _In_ const DM_REGION *Region)
{
    ULONG Index;

    if (Layout == NULL || Region == NULL)
        return NULL;

    if (Region->PartitionIndex < Layout->PartitionCount)
    {
        PPARTITION_INFORMATION_EX Entry;

        Entry = &Layout->PartitionEntry[Region->PartitionIndex];
        if (Entry->StartingOffset.QuadPart == Region->StartOffset &&
            Entry->PartitionLength.QuadPart == Region->Length)
        {
            return Entry;
        }
    }

    for (Index = 0; Index < Layout->PartitionCount; Index++)
    {
        PPARTITION_INFORMATION_EX Entry;

        Entry = &Layout->PartitionEntry[Index];
        if (Entry->StartingOffset.QuadPart == Region->StartOffset &&
            Entry->PartitionLength.QuadPart == Region->Length)
        {
            return Entry;
        }

        if (Region->PartitionNumber != 0 &&
            Entry->PartitionNumber == Region->PartitionNumber)
        {
            return Entry;
        }
    }

    return NULL;
}

static PPARTITION_INFORMATION_EX
DmActionFindFreePrimaryMbrSlot(
    _Inout_ PDRIVE_LAYOUT_INFORMATION_EX Layout)
{
    ULONG Index;

    if (Layout == NULL || Layout->PartitionStyle != PARTITION_STYLE_MBR)
        return NULL;

    for (Index = 0; Index < min(Layout->PartitionCount, 4UL); Index++)
    {
        PPARTITION_INFORMATION_EX Candidate;

        Candidate = &Layout->PartitionEntry[Index];
        if (Candidate->Mbr.PartitionType == PARTITION_ENTRY_UNUSED ||
            (Candidate->StartingOffset.QuadPart == 0 &&
             Candidate->PartitionLength.QuadPart == 0))
        {
            return Candidate;
        }
    }

    return NULL;
}

static VOID
DmActionInitializeMbrLayoutEntry(
    _Out_ PPARTITION_INFORMATION_EX Entry,
    _In_ ULONGLONG StartingOffset,
    _In_ ULONGLONG PartitionLength,
    _In_ ULONG HiddenSectors,
    _In_ UCHAR PartitionType,
    _In_ BOOLEAN Recognized)
{
    ZeroMemory(Entry, sizeof(*Entry));
    Entry->PartitionStyle = PARTITION_STYLE_MBR;
    Entry->StartingOffset.QuadPart = StartingOffset;
    Entry->PartitionLength.QuadPart = PartitionLength;
    Entry->PartitionNumber = 0;
    Entry->Mbr.PartitionType = PartitionType;
    Entry->Mbr.BootIndicator = FALSE;
    Entry->Mbr.RecognizedPartition = Recognized;
    Entry->Mbr.HiddenSectors = HiddenSectors;
    Entry->RewritePartition = TRUE;
}

static BOOL
DmActionExpandLayout(
    _Inout_ PDRIVE_LAYOUT_INFORMATION_EX *Layout,
    _In_ ULONG NewPartitionCount)
{
    PDRIVE_LAYOUT_INFORMATION_EX NewLayout;
    SIZE_T NewSize;
    ULONG OldPartitionCount;
    ULONG Index;

    if (Layout == NULL || *Layout == NULL || NewPartitionCount == 0)
        return FALSE;

    OldPartitionCount = (*Layout)->PartitionCount;
    NewSize = sizeof(DRIVE_LAYOUT_INFORMATION_EX) +
              ((NewPartitionCount - ANYSIZE_ARRAY) * sizeof(PARTITION_INFORMATION_EX));
    NewLayout = HeapReAlloc(ProcessHeap,
                            HEAP_ZERO_MEMORY,
                            *Layout,
                            NewSize);
    if (NewLayout == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    NewLayout->PartitionCount = NewPartitionCount;
    if (NewPartitionCount > OldPartitionCount)
    {
        for (Index = OldPartitionCount; Index < NewPartitionCount; Index++)
            NewLayout->PartitionEntry[Index].RewritePartition = TRUE;
    }
    *Layout = NewLayout;
    return TRUE;
}

static ULONGLONG
DmActionAlignmentBytes(
    _In_ const DM_ACTION_CONTEXT *Context)
{
    ULONGLONG BytesPerSector;
    ULONGLONG SectorAlignment;

    if (Context == NULL || Context->Disk == NULL)
        return 1024ULL * 1024ULL;

    BytesPerSector = max(Context->Disk->BytesPerSector, 512ULL);
    SectorAlignment = max((ULONGLONG)Context->Disk->SectorAlignment, 1ULL);
    return max(BytesPerSector * SectorAlignment, BytesPerSector);
}

static ULONGLONG
DmActionRegionMaxSizeMb(
    _In_ ULONGLONG RegionLength)
{
    const ULONGLONG Megabyte = 1024ULL * 1024ULL;

    if (RegionLength == 0)
        return 0;

    return (RegionLength + Megabyte - 1) / Megabyte;
}

static ULONGLONG
DmActionPartitionBytesFromSizeMb(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ ULONGLONG SizeMb)
{
    const ULONGLONG Megabyte = 1024ULL * 1024ULL;
    ULONGLONG RegionLength;
    ULONGLONG RequestedBytes;
    ULONGLONG AlignmentBytes;
    ULONGLONG MaxSizeMb;

    if (Context == NULL || Context->Region == NULL)
        return 0;

    RegionLength = Context->Region->Length;
    MaxSizeMb = DmActionRegionMaxSizeMb(RegionLength);
    if (RegionLength == 0 || SizeMb == 0 || MaxSizeMb == 0)
        return 0;

    if (SizeMb >= MaxSizeMb)
        return RegionLength;

    RequestedBytes = SizeMb * Megabyte;
    AlignmentBytes = DmActionAlignmentBytes(Context);
    if (AlignmentBytes > 1)
        RequestedBytes -= (RequestedBytes % AlignmentBytes);

    if (RequestedBytes == 0)
        RequestedBytes = min(AlignmentBytes, RegionLength);

    return min(RequestedBytes, RegionLength);
}

static ULONGLONG
DmActionPartitionBytesFromCreateDialog(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ const DM_CREATE_PARTITION_DIALOG *Dialog)
{
    ULONGLONG MaxBytes;

    if (Dialog != NULL &&
        Dialog->PartitionKind == DmCreatePartitionMbrLogical &&
        Dialog->LogicalMaxSizeMb != 0)
    {
        MaxBytes = Context->Region->Length - DmActionAlignmentBytes(Context);
        return DmActionAlignedBytesFromSizeMb(Context,
                                              Dialog->SelectedSizeMb,
                                              MaxBytes);
    }

    return DmActionPartitionBytesFromSizeMb(Context,
                                            Dialog != NULL ? Dialog->SelectedSizeMb : 0);
}

static ULONGLONG
DmActionAlignedBytesFromSizeMb(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ ULONGLONG SizeMb,
    _In_ ULONGLONG MaxBytes)
{
    const ULONGLONG Megabyte = 1024ULL * 1024ULL;
    ULONGLONG RequestedBytes;
    ULONGLONG AlignmentBytes;
    ULONGLONG MaxSizeMb;

    if (Context == NULL || SizeMb == 0 || MaxBytes == 0)
        return 0;

    MaxSizeMb = DmActionRegionMaxSizeMb(MaxBytes);
    if (SizeMb >= MaxSizeMb)
        return MaxBytes;

    RequestedBytes = SizeMb * Megabyte;
    AlignmentBytes = DmActionAlignmentBytes(Context);
    if (AlignmentBytes > 1)
        RequestedBytes -= (RequestedBytes % AlignmentBytes);

    if (RequestedBytes == 0)
        RequestedBytes = min(AlignmentBytes, MaxBytes);

    return min(RequestedBytes, MaxBytes);
}

static BOOL
DmActionIsRawVolume(
    _In_opt_ const DM_VOLUME *Volume)
{
    return (Volume != NULL &&
            Volume->FileSystem[0] != UNICODE_NULL &&
            _wcsicmp(Volume->FileSystem, L"RAW") == 0);
}

static const DM_REGION *
DmActionFindRightAdjacentFreeRegion(
    _In_ const DM_ACTION_CONTEXT *Context)
{
    ULONGLONG RegionEnd;
    ULONG Index;

    if (Context == NULL || Context->Disk == NULL || Context->Region == NULL)
        return NULL;

    RegionEnd = Context->Region->StartOffset + Context->Region->Length;
    for (Index = 0; Index < Context->Disk->RegionCount; Index++)
    {
        const DM_REGION *Candidate;

        Candidate = &Context->Disk->Regions[Index];
        if (Candidate->Type != DmRegionFree ||
            Candidate->Length == 0 ||
            Candidate->PartitionStyle != Context->Region->PartitionStyle ||
            Candidate->IsLogical != Context->Region->IsLogical)
        {
            continue;
        }

        if (Candidate->StartOffset == RegionEnd)
            return Candidate;
    }

    return NULL;
}

static BOOL
DmActionCanResizeRawVolume(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ BOOL Extend)
{
    ULONGLONG AlignmentBytes;

    if (Context == NULL || Context->Disk == NULL || Context->Volume == NULL || Context->Region == NULL)
        return FALSE;

    if (Context->Region->Type != DmRegionPartition ||
        Context->Region->IsContainer ||
        Context->Region->IsHidden ||
        Context->Volume->ExtentCount > 1 ||
        Context->Disk->IsDynamic ||
        Context->Region->IsDynamic ||
        Context->Volume->IsDynamic ||
        Context->Disk->IsOffline ||
        Context->Disk->IsReadOnly ||
        Context->Region->IsBoot ||
        Context->Region->IsSystem ||
        Context->Volume->IsBoot ||
        Context->Volume->IsSystem ||
        !DmActionIsRawVolume(Context->Volume))
    {
        return FALSE;
    }

    if (Extend)
        return (DmActionFindRightAdjacentFreeRegion(Context) != NULL);

    AlignmentBytes = DmActionAlignmentBytes(Context);
    return (Context->Region->Length > AlignmentBytes);
}

static BOOL
DmActionCanMutateSimpleBasicVolume(
    _In_ const DM_ACTION_CONTEXT *Context)
{
    return (Context != NULL &&
            Context->Volume != NULL &&
            Context->Volume->ExtentCount <= 1 &&
            (Context->Disk == NULL || !Context->Disk->IsDynamic) &&
            (Context->Region == NULL || !Context->Region->IsDynamic) &&
            !Context->Volume->IsDynamic);
}

static BOOL
DmActionFindAvailableDriveLetters(
    _Out_writes_(cLetters) PWCHAR Letters,
    _In_ ULONG cLetters,
    _Out_ PULONG LetterCount);

static ULONGLONG
DmCreatePartitionGetEffectiveMaxSizeMb(
    _In_opt_ const DM_CREATE_PARTITION_DIALOG *Context)
{
    if (Context == NULL)
        return 0;

    if (Context->PartitionKind == DmCreatePartitionMbrLogical &&
        Context->LogicalMaxSizeMb != 0)
    {
        return Context->LogicalMaxSizeMb;
    }

    return Context->MaxSizeMb;
}

static VOID
DmCreatePartitionUpdateSizeDisplay(
    _In_ HWND hwndDlg,
    _Inout_ PDM_CREATE_PARTITION_DIALOG Context)
{
    WCHAR Buffer[32];
    ULONGLONG MaxSizeMb;

    if (hwndDlg == NULL || Context == NULL)
        return;

    MaxSizeMb = DmCreatePartitionGetEffectiveMaxSizeMb(Context);
    if (Context->SelectedSizeMb > MaxSizeMb && MaxSizeMb != 0)
        Context->SelectedSizeMb = MaxSizeMb;

    StringCchPrintfW(Buffer, ARRAYSIZE(Buffer), L"%I64u MB", MaxSizeMb);
    SetDlgItemTextW(hwndDlg, IDC_CREATE_PARTITION_MAX, Buffer);
    StringCchPrintfW(Buffer, ARRAYSIZE(Buffer), L"%I64u", Context->SelectedSizeMb);
    SetDlgItemTextW(hwndDlg, IDC_CREATE_PARTITION_SIZE, Buffer);
}

static VOID
DmActionLoadDialogString(
    _In_ UINT StringId,
    _In_z_ PCWSTR Fallback,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (Buffer == NULL || cchBuffer == 0)
        return;

    if (LoadStringW(hInstance, StringId, Buffer, cchBuffer) == 0)
        StringCchCopyW(Buffer, cchBuffer, Fallback);
}

static ULONGLONG
DmActionGetSuggestedCreateSizeMb(
    _In_ const DM_CREATE_PARTITION_DIALOG *Context,
    _In_ ULONG PartitionKind)
{
    ULONGLONG MaxSizeMb;

    if (Context == NULL)
        return 0;

    MaxSizeMb = DmCreatePartitionGetEffectiveMaxSizeMb(Context);
    switch (PartitionKind)
    {
        case DmCreatePartitionEfiSystem:
            return min(MaxSizeMb, 100ULL);

        case DmCreatePartitionMsr:
            return min(MaxSizeMb, 16ULL);

        case DmCreatePartitionRecovery:
            return min(MaxSizeMb, 500ULL);

        default:
            return MaxSizeMb;
    }
}

static BOOL
DmActionPopulateFileSystemCombo(
    _In_ HWND hwndDlg,
    _In_ UINT ComboId,
    _In_z_ PCWSTR SelectedFileSystem,
    _In_ BOOL PreferNtfs)
{
    WCHAR FileSystem[32];
    UCHAR Major;
    UCHAR Minor;
    BOOLEAN Latest;
    DWORD Index;
    INT DefaultIndex;
    INT NtfsIndex;
    INT FatIndex;

    DefaultIndex = CB_ERR;
    NtfsIndex = CB_ERR;
    FatIndex = CB_ERR;
    Index = 0;

    while (QueryAvailableFileSystemFormat(Index, FileSystem, &Major, &Minor, &Latest))
    {
        INT ComboIndex;

        UNREFERENCED_PARAMETER(Major);
        UNREFERENCED_PARAMETER(Minor);
        UNREFERENCED_PARAMETER(Latest);

        ComboIndex = (INT)SendDlgItemMessageW(hwndDlg,
                                              ComboId,
                                              CB_ADDSTRING,
                                              0,
                                              (LPARAM)FileSystem);
        if (ComboIndex != CB_ERR)
        {
            if (SelectedFileSystem != NULL &&
                SelectedFileSystem[0] != UNICODE_NULL &&
                _wcsicmp(SelectedFileSystem, FileSystem) == 0)
            {
                DefaultIndex = ComboIndex;
            }

            if (NtfsIndex == CB_ERR && _wcsicmp(FileSystem, L"NTFS") == 0)
                NtfsIndex = ComboIndex;

            if (FatIndex == CB_ERR &&
                (_wcsicmp(FileSystem, L"FAT32") == 0 ||
                 _wcsicmp(FileSystem, L"FAT") == 0))
            {
                FatIndex = ComboIndex;
            }
        }

        Index++;
    }

    if (Index == 0)
        return FALSE;

    if (DefaultIndex == CB_ERR)
    {
        if (PreferNtfs && NtfsIndex != CB_ERR)
            DefaultIndex = NtfsIndex;
        else if (!PreferNtfs && FatIndex != CB_ERR)
            DefaultIndex = FatIndex;
        else
            DefaultIndex = 0;
    }

    SendDlgItemMessageW(hwndDlg,
                        ComboId,
                        CB_SETCURSEL,
                        (WPARAM)DefaultIndex,
                        0);
    return TRUE;
}

static VOID
DmCreatePartitionUpdateDialogState(
    _In_ HWND hwndDlg,
    _In_ const DM_CREATE_PARTITION_DIALOG *Context)
{
    BOOL EnableAssign;
    BOOL EnableFormat;
    BOOL SupportsAssign;
    BOOL SupportsFormat;

    SupportsAssign = (Context != NULL &&
                      DmActionPartitionKindSupportsAssign(Context->PartitionKind));
    SupportsFormat = (Context != NULL &&
                      DmActionPartitionKindSupportsFormat(Context->PartitionKind));

    EnableAssign = (Context != NULL &&
                    SupportsAssign &&
                    Context->LetterCount != 0 &&
                    SendDlgItemMessageW(hwndDlg, IDC_CREATE_PARTITION_ASSIGN, BM_GETCHECK, 0, 0) == BST_CHECKED);
    EnableWindow(GetDlgItem(hwndDlg, IDC_CREATE_PARTITION_LETTER), EnableAssign);
    EnableWindow(GetDlgItem(hwndDlg, IDC_CREATE_PARTITION_ASSIGN),
                 (Context != NULL && SupportsAssign && Context->LetterCount != 0));

    EnableFormat = (SupportsFormat &&
                    ((SupportsAssign && EnableAssign) || !SupportsAssign) &&
                    SendDlgItemMessageW(hwndDlg, IDC_CREATE_PARTITION_FORMAT, BM_GETCHECK, 0, 0) == BST_CHECKED);
    EnableWindow(GetDlgItem(hwndDlg, IDC_CREATE_PARTITION_FORMAT),
                 (Context != NULL && SupportsFormat));
    EnableWindow(GetDlgItem(hwndDlg, IDC_CREATE_PARTITION_FS), EnableFormat);
    EnableWindow(GetDlgItem(hwndDlg, IDC_CREATE_PARTITION_LABEL), EnableFormat);
    EnableWindow(GetDlgItem(hwndDlg, IDC_CREATE_PARTITION_QUICK), EnableFormat);
}

static VOID
DmActionAddPartitionTypeString(
    _In_ HWND hwndDlg,
    _In_ UINT ComboId,
    _In_ UINT StringId,
    _In_z_ PCWSTR Fallback)
{
    WCHAR Buffer[64];

    if (LoadStringW(hInstance, StringId, Buffer, ARRAYSIZE(Buffer)) == 0)
        StringCchCopyW(Buffer, ARRAYSIZE(Buffer), Fallback);

    SendDlgItemMessageW(hwndDlg, ComboId, CB_ADDSTRING, 0, (LPARAM)Buffer);
}

static UINT
DmActionGetPartitionKindStringId(
    _In_ ULONG PartitionKind)
{
    switch (PartitionKind)
    {
        case DmCreatePartitionMbrPrimary:
            return IDS_PARTITION_TYPE_PRIMARY;

        case DmCreatePartitionMbrExtended:
            return IDS_PARTITION_TYPE_EXTENDED;

        case DmCreatePartitionMbrLogical:
            return IDS_PARTITION_TYPE_LOGICAL;

        case DmCreatePartitionBasic:
            return IDS_PARTITION_TYPE_BASIC;

        case DmCreatePartitionEfiSystem:
            return IDS_PARTITION_TYPE_EFI;

        case DmCreatePartitionMsr:
            return IDS_PARTITION_TYPE_MSR;

        case DmCreatePartitionRecovery:
            return IDS_PARTITION_TYPE_RECOVERY;

        default:
            return IDS_PARTITION_TYPE_PRIMARY;
    }
}

static UINT
DmActionGetCreatePromptStringId(
    _In_ ULONG PartitionKind)
{
    switch (PartitionKind)
    {
        case DmCreatePartitionMbrLogical:
            return IDS_CREATE_PROMPT_LOGICAL;

        case DmCreatePartitionBasic:
        case DmCreatePartitionEfiSystem:
        case DmCreatePartitionMsr:
        case DmCreatePartitionRecovery:
            return IDS_CREATE_PROMPT_VOLUME;

        case DmCreatePartitionMbrPrimary:
        case DmCreatePartitionMbrExtended:
        default:
            return IDS_CREATE_PROMPT_PARTITION;
    }
}

static UINT
DmActionGetCreateSizeLabelStringId(
    _In_ ULONG PartitionKind)
{
    switch (PartitionKind)
    {
        case DmCreatePartitionMbrLogical:
            return IDS_CREATE_SIZE_LABEL_LOGICAL;

        case DmCreatePartitionBasic:
        case DmCreatePartitionEfiSystem:
        case DmCreatePartitionMsr:
        case DmCreatePartitionRecovery:
            return IDS_CREATE_SIZE_LABEL_VOLUME;

        case DmCreatePartitionMbrPrimary:
        case DmCreatePartitionMbrExtended:
        default:
            return IDS_CREATE_SIZE_LABEL_PARTITION;
    }
}

static UCHAR
DmActionFileSystemToMbrPartitionType(
    _In_z_ PCWSTR FileSystem,
    _In_ ULONGLONG StartSector,
    _In_ ULONGLONG SectorCount)
{
    if (FileSystem == NULL || FileSystem[0] == UNICODE_NULL || SectorCount == 0)
        return PARTITION_IFS;

    if (_wcsicmp(FileSystem, L"FAT") == 0 ||
        _wcsicmp(FileSystem, L"FAT32") == 0)
    {
        if (SectorCount < 8192ULL)
            return PARTITION_FAT_12;

        if (StartSector < 1450560ULL)
        {
            if (SectorCount < 65536ULL)
                return PARTITION_FAT_16;

            if (SectorCount < 1048576ULL)
                return PARTITION_HUGE;

            return PARTITION_FAT32;
        }

        if (SectorCount < 1048576ULL)
            return PARTITION_XINT13;

        return PARTITION_FAT32_XINT13;
    }

    if (_wcsicmp(FileSystem, L"NTFS") == 0)
        return PARTITION_IFS;

#ifdef PARTITION_LINUX
    if (_wcsicmp(FileSystem, L"BTRFS") == 0 ||
        _wcsicmp(FileSystem, L"EXT2") == 0 ||
        _wcsicmp(FileSystem, L"EXT3") == 0 ||
        _wcsicmp(FileSystem, L"EXT4") == 0)
    {
        return PARTITION_LINUX;
    }
#endif

    return PARTITION_IFS;
}

static UCHAR
DmActionChooseMbrContainerType(
    _In_ const DM_DISK *Disk,
    _In_ ULONGLONG StartOffset,
    _In_ ULONGLONG PartitionLength)
{
    ULONGLONG BytesPerSector;
    ULONGLONG SectorsPerCylinder;
    ULONGLONG EndSector;

    if (Disk == NULL || PartitionLength == 0)
        return PARTITION_XINT13_EXTENDED;

    BytesPerSector = max(Disk->BytesPerSector, 512ULL);
    EndSector = (StartOffset + PartitionLength - 1) / BytesPerSector;
    SectorsPerCylinder = Disk->TracksPerCylinder * Disk->SectorsPerTrack;

    if (SectorsPerCylinder != 0 &&
        (EndSector / SectorsPerCylinder) < 1024ULL)
    {
        return PARTITION_EXTENDED;
    }

    return PARTITION_XINT13_EXTENDED;
}

static UCHAR
DmActionChooseMbrDataPartitionType(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ const DM_CREATE_PARTITION_DIALOG *Dialog,
    _In_ ULONGLONG StartOffset,
    _In_ ULONGLONG PartitionLength)
{
    ULONGLONG BytesPerSector;
    ULONGLONG StartSector;
    ULONGLONG SectorCount;
    PCWSTR FileSystem;

    if (Context == NULL || Context->Disk == NULL || PartitionLength == 0)
        return PARTITION_IFS;

    BytesPerSector = max(Context->Disk->BytesPerSector, 512ULL);
    StartSector = StartOffset / BytesPerSector;
    SectorCount = PartitionLength / BytesPerSector;
    FileSystem = (Dialog != NULL) ? Dialog->SelectedFileSystem : NULL;

    return DmActionFileSystemToMbrPartitionType(FileSystem,
                                                StartSector,
                                                SectorCount);
}

static PCWSTR
DmActionGetCreateVerb(
    _In_opt_ const DM_ACTION_CONTEXT *Context)
{
    if (Context != NULL && Context->Region != NULL && Context->Region->Type == DmRegionFree)
    {
        if (Context->Region->IsLogical)
            return L"Create Logical Drive";

        if (Context->Disk != NULL && Context->Disk->PartitionStyle == PARTITION_STYLE_GPT)
            return L"Create Volume";
    }

    return L"Create Partition";
}

static PCWSTR
DmActionGetCreateDialogTitle(
    _In_opt_ const DM_CREATE_PARTITION_DIALOG *Dialog)
{
    return (Dialog != NULL && Dialog->Caption != NULL) ? Dialog->Caption : L"Create Volume";
}

static PCWSTR
DmActionGetCreateObjectLabel(
    _In_ ULONG PartitionKind)
{
    switch (PartitionKind)
    {
        case DmCreatePartitionMbrExtended:
            return L"extended partition";

        case DmCreatePartitionMbrLogical:
            return L"logical drive";

        case DmCreatePartitionMbrPrimary:
            return L"partition";

        case DmCreatePartitionBasic:
        case DmCreatePartitionEfiSystem:
        case DmCreatePartitionMsr:
        case DmCreatePartitionRecovery:
        default:
            return L"volume";
    }
}

static PCWSTR
DmActionGetCreateDialogTitleForKind(
    _In_opt_ const DM_CREATE_PARTITION_DIALOG *Dialog,
    _In_ ULONG PartitionKind)
{
    switch (PartitionKind)
    {
        case DmCreatePartitionMbrLogical:
            return L"Create Logical Drive";

        case DmCreatePartitionMbrPrimary:
        case DmCreatePartitionMbrExtended:
            return L"Create Partition";

        case DmCreatePartitionBasic:
        case DmCreatePartitionEfiSystem:
        case DmCreatePartitionMsr:
        case DmCreatePartitionRecovery:
            return L"Create Volume";

        default:
            return DmActionGetCreateDialogTitle(Dialog);
    }
}

static VOID
DmCreatePartitionUpdateDialogText(
    _In_ HWND hwndDlg,
    _In_ const DM_CREATE_PARTITION_DIALOG *Context)
{
    WCHAR Buffer[256];
    UINT StringId;

    if (hwndDlg == NULL || Context == NULL)
        return;

    SetWindowTextW(hwndDlg,
                   DmActionGetCreateDialogTitleForKind(Context,
                                                       Context->PartitionKind));

    StringId = DmActionGetCreatePromptStringId(Context->PartitionKind);
    DmActionLoadDialogString(StringId,
                             L"Enter the size and initial settings for the selected unallocated region:",
                             Buffer,
                             ARRAYSIZE(Buffer));
    SetDlgItemTextW(hwndDlg, IDC_CREATE_PARTITION_PROMPT, Buffer);

    StringId = DmActionGetCreateSizeLabelStringId(Context->PartitionKind);
    DmActionLoadDialogString(StringId,
                             L"Partition size (MB):",
                             Buffer,
                             ARRAYSIZE(Buffer));
    SetDlgItemTextW(hwndDlg, IDC_CREATE_PARTITION_SIZE_LABEL, Buffer);
}

static VOID
DmActionAppendCreateSettingLine(
    _Inout_updates_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer,
    _In_z_ PCWSTR Label,
    _In_z_ PCWSTR Value)
{
    SIZE_T Length;

    if (Buffer == NULL || cchBuffer == 0 || Label == NULL || Value == NULL)
        return;

    Length = wcslen(Buffer);
    if (Length >= cchBuffer)
        return;

    StringCchPrintfW(Buffer + Length,
                     cchBuffer - Length,
                     L"%s: %s\r\n",
                     Label,
                     Value);
}

static VOID
DmActionBuildCreateConfirmation(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ const DM_CREATE_PARTITION_DIALOG *Dialog,
    _In_ ULONGLONG PartitionLength,
    _Out_writes_(cchPrompt) PWSTR Prompt,
    _In_ SIZE_T cchPrompt)
{
    WCHAR SizeText[32];
    WCHAR TypeText[64];
    WCHAR LetterText[16];
    WCHAR FormatText[96];
    WCHAR LabelText[96];

    if (Prompt == NULL || cchPrompt == 0)
        return;

    Prompt[0] = UNICODE_NULL;
    if (Context == NULL || Context->Disk == NULL || Dialog == NULL)
        return;

    StringCchPrintfW(SizeText,
                     ARRAYSIZE(SizeText),
                     L"%I64u MB",
                     DmActionRegionMaxSizeMb(PartitionLength));
    StringCchPrintfW(Prompt,
                     cchPrompt,
                     L"Create the following %s on Disk %lu now?\r\n\r\n",
                     DmActionGetCreateObjectLabel(Dialog->PartitionKind),
                     Context->Disk->DiskNumber);

    StringCchCopyW(TypeText,
                   ARRAYSIZE(TypeText),
                   DmActionGetPartitionKindFallback(Dialog->PartitionKind));
    DmActionAppendCreateSettingLine(Prompt, cchPrompt, L"Type", TypeText);
    DmActionAppendCreateSettingLine(Prompt, cchPrompt, L"Size", SizeText);

    if (DmActionPartitionKindSupportsAssign(Dialog->PartitionKind))
    {
        if (Dialog->PartitionKind == DmCreatePartitionMbrLogical &&
            Context->Region != NULL &&
            !Context->Region->IsLogical &&
            Context->Disk != NULL &&
            !Context->Disk->HasExtendedPartition)
        {
            DmActionAppendCreateSettingLine(Prompt,
                                            cchPrompt,
                                            L"Container",
                                            L"Create a new extended partition");
        }

        if (Dialog->AssignLetter && Dialog->SelectedLetter != UNICODE_NULL)
        {
            StringCchPrintfW(LetterText,
                             ARRAYSIZE(LetterText),
                             L"%C:",
                             towupper(Dialog->SelectedLetter));
            DmActionAppendCreateSettingLine(Prompt, cchPrompt, L"Drive letter", LetterText);
        }
        else
        {
            DmActionAppendCreateSettingLine(Prompt, cchPrompt, L"Drive letter", L"None");
        }
    }

    if (DmActionPartitionKindSupportsFormat(Dialog->PartitionKind))
    {
        if (Dialog->FormatVolume && Dialog->SelectedFileSystem[0] != UNICODE_NULL)
        {
            StringCchCopyW(FormatText, ARRAYSIZE(FormatText), Dialog->SelectedFileSystem);
            if (Dialog->QuickFormat)
                StringCchCatW(FormatText, ARRAYSIZE(FormatText), L" (Quick)");
            DmActionAppendCreateSettingLine(Prompt, cchPrompt, L"Format", FormatText);

            if (Dialog->Label[0] != UNICODE_NULL)
                StringCchCopyW(LabelText, ARRAYSIZE(LabelText), Dialog->Label);
            else
                StringCchCopyW(LabelText, ARRAYSIZE(LabelText), L"(none)");
            DmActionAppendCreateSettingLine(Prompt, cchPrompt, L"Label", LabelText);
        }
        else
        {
            DmActionAppendCreateSettingLine(Prompt, cchPrompt, L"Format", L"Do not format");
        }
    }
}

static PCWSTR
DmActionGetPartitionKindFallback(
    _In_ ULONG PartitionKind)
{
    switch (PartitionKind)
    {
        case DmCreatePartitionMbrPrimary:
            return L"Primary";

        case DmCreatePartitionMbrExtended:
            return L"Extended";

        case DmCreatePartitionMbrLogical:
            return L"Logical";

        case DmCreatePartitionBasic:
            return L"Basic data";

        case DmCreatePartitionEfiSystem:
            return L"EFI system";

        case DmCreatePartitionMsr:
            return L"Microsoft reserved";

        case DmCreatePartitionRecovery:
            return L"Recovery";

        default:
            return L"Primary";
    }
}

static INT_PTR CALLBACK
DmCreatePartitionDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PDM_CREATE_PARTITION_DIALOG Context;
    WCHAR Buffer[32];
    WCHAR *EndPtr;
    ULONGLONG Value;

    Context = (PDM_CREATE_PARTITION_DIALOG)GetWindowLongPtrW(hwndDlg, DWLP_USER);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            Context = (PDM_CREATE_PARTITION_DIALOG)lParam;
            SetWindowLongPtrW(hwndDlg, DWLP_USER, (LONG_PTR)Context);

            if (Context == NULL || Context->MaxSizeMb == 0)
            {
                EndDialog(hwndDlg, IDCANCEL);
                return FALSE;
            }

            SendDlgItemMessageW(hwndDlg, IDC_CREATE_PARTITION_TYPE, CB_RESETCONTENT, 0, 0);
            if (Context->PartitionKindCount == 0)
            {
                Context->PartitionKinds[0] = DmCreatePartitionMbrPrimary;
                Context->PartitionKindCount = 1;
                Context->PartitionKind = DmCreatePartitionMbrPrimary;
            }

            {
                ULONG Index;
                INT DefaultIndex;

                DefaultIndex = 0;
                for (Index = 0; Index < Context->PartitionKindCount; Index++)
                {
                    DmActionAddPartitionTypeString(hwndDlg,
                                                   IDC_CREATE_PARTITION_TYPE,
                                                   DmActionGetPartitionKindStringId(Context->PartitionKinds[Index]),
                                                   DmActionGetPartitionKindFallback(Context->PartitionKinds[Index]));
                    if (Context->PartitionKinds[Index] == Context->PartitionKind)
                        DefaultIndex = (INT)Index;
                }

                SendDlgItemMessageW(hwndDlg,
                                    IDC_CREATE_PARTITION_TYPE,
                                    CB_SETCURSEL,
                                    (WPARAM)DefaultIndex,
                                    0);
                EnableWindow(GetDlgItem(hwndDlg, IDC_CREATE_PARTITION_TYPE),
                             (Context->PartitionKindCount > 1));
                DmCreatePartitionUpdateDialogText(hwndDlg, Context);
            }
            DmCreatePartitionUpdateSizeDisplay(hwndDlg, Context);
            SendDlgItemMessageW(hwndDlg, IDC_CREATE_PARTITION_SIZE, EM_SETSEL, 0, -1);
            SendDlgItemMessageW(hwndDlg,
                                IDC_CREATE_PARTITION_ASSIGN,
                                BM_SETCHECK,
                                Context->AssignLetter ? BST_CHECKED : BST_UNCHECKED,
                                0);
            SendDlgItemMessageW(hwndDlg,
                                IDC_CREATE_PARTITION_FORMAT,
                                BM_SETCHECK,
                                Context->FormatVolume ? BST_CHECKED : BST_UNCHECKED,
                                0);
            SendDlgItemMessageW(hwndDlg,
                                IDC_CREATE_PARTITION_QUICK,
                                BM_SETCHECK,
                                Context->QuickFormat ? BST_CHECKED : BST_UNCHECKED,
                                0);
            if (Context->LetterCount != 0)
            {
                ULONG Index;
                WCHAR Item[4];

                for (Index = 0; Index < Context->LetterCount; Index++)
                {
                    Item[0] = Context->Letters[Index];
                    Item[1] = L':';
                    Item[2] = UNICODE_NULL;
                    SendDlgItemMessageW(hwndDlg,
                                        IDC_CREATE_PARTITION_LETTER,
                                        CB_ADDSTRING,
                                        0,
                                        (LPARAM)Item);
                }

                SendDlgItemMessageW(hwndDlg,
                                    IDC_CREATE_PARTITION_LETTER,
                                    CB_SETCURSEL,
                                    0,
                                    0);
            }

            if (!DmActionPopulateFileSystemCombo(hwndDlg,
                                                IDC_CREATE_PARTITION_FS,
                                                Context->SelectedFileSystem,
                                                Context->PreferNtfs))
            {
                EnableWindow(GetDlgItem(hwndDlg, IDC_CREATE_PARTITION_FORMAT), FALSE);
                Context->FormatVolume = FALSE;
                SendDlgItemMessageW(hwndDlg,
                                    IDC_CREATE_PARTITION_FORMAT,
                                    BM_SETCHECK,
                                    BST_UNCHECKED,
                                    0);
            }

            SetDlgItemTextW(hwndDlg, IDC_CREATE_PARTITION_LABEL, Context->Label);
            SendDlgItemMessageW(hwndDlg, IDC_CREATE_PARTITION_LABEL, EM_LIMITTEXT, 32, 0);
            if (Context->LetterCount == 0)
            {
                EnableWindow(GetDlgItem(hwndDlg, IDC_CREATE_PARTITION_ASSIGN), FALSE);
                SendDlgItemMessageW(hwndDlg,
                                    IDC_CREATE_PARTITION_ASSIGN,
                                    BM_SETCHECK,
                                    BST_UNCHECKED,
                                    0);
                SendDlgItemMessageW(hwndDlg,
                                    IDC_CREATE_PARTITION_FORMAT,
                                    BM_SETCHECK,
                                    BST_UNCHECKED,
                                    0);
            }

            DmCreatePartitionUpdateDialogState(hwndDlg, Context);
            return TRUE;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDC_CREATE_PARTITION_TYPE:
                    if (Context != NULL && HIWORD(wParam) == CBN_SELCHANGE)
                    {
                        INT Selection;

                        Selection = (INT)SendDlgItemMessageW(hwndDlg,
                                                             IDC_CREATE_PARTITION_TYPE,
                                                             CB_GETCURSEL,
                                                             0,
                                                             0);
                        if (Selection == CB_ERR ||
                            Selection < 0 ||
                            (ULONG)Selection >= Context->PartitionKindCount)
                        {
                            return TRUE;
                        }

                        Context->PartitionKind = Context->PartitionKinds[Selection];
                        if (Context->PartitionKind == DmCreatePartitionEfiSystem ||
                            Context->PartitionKind == DmCreatePartitionMsr ||
                            Context->PartitionKind == DmCreatePartitionRecovery)
                        {
                            Context->SelectedSizeMb = DmActionGetSuggestedCreateSizeMb(Context,
                                                                                       Context->PartitionKind);
                        }
                        if (Context->PartitionKind == DmCreatePartitionEfiSystem)
                        {
                            SendDlgItemMessageW(hwndDlg, IDC_CREATE_PARTITION_ASSIGN, BM_SETCHECK, BST_UNCHECKED, 0);
                            SendDlgItemMessageW(hwndDlg, IDC_CREATE_PARTITION_FORMAT, BM_SETCHECK, BST_UNCHECKED, 0);
                            SetDlgItemTextW(hwndDlg, IDC_CREATE_PARTITION_LABEL, L"");
                        }
                        else if (Context->PartitionKind == DmCreatePartitionMsr ||
                                 Context->PartitionKind == DmCreatePartitionRecovery)
                        {
                            SendDlgItemMessageW(hwndDlg, IDC_CREATE_PARTITION_ASSIGN, BM_SETCHECK, BST_UNCHECKED, 0);
                            SendDlgItemMessageW(hwndDlg, IDC_CREATE_PARTITION_FORMAT, BM_SETCHECK, BST_UNCHECKED, 0);
                            SetDlgItemTextW(hwndDlg, IDC_CREATE_PARTITION_LABEL, L"");
                        }
                        DmCreatePartitionUpdateDialogText(hwndDlg, Context);
                        DmCreatePartitionUpdateSizeDisplay(hwndDlg, Context);
                        DmCreatePartitionUpdateDialogState(hwndDlg, Context);
                    }
                    return TRUE;

                case IDC_CREATE_PARTITION_ASSIGN:
                    if (Context != NULL &&
                        SendDlgItemMessageW(hwndDlg, IDC_CREATE_PARTITION_ASSIGN, BM_GETCHECK, 0, 0) != BST_CHECKED)
                    {
                        SendDlgItemMessageW(hwndDlg,
                                            IDC_CREATE_PARTITION_FORMAT,
                                            BM_SETCHECK,
                                            BST_UNCHECKED,
                                            0);
                    }
                    DmCreatePartitionUpdateDialogState(hwndDlg, Context);
                    return TRUE;

                case IDC_CREATE_PARTITION_FORMAT:
                    DmCreatePartitionUpdateDialogState(hwndDlg, Context);
                    return TRUE;

                case IDOK:
                {
                    INT LetterSelection;
                    INT FileSystemSelection;

                    if (Context == NULL)
                    {
                        EndDialog(hwndDlg, IDCANCEL);
                        return TRUE;
                    }

                    if (GetDlgItemTextW(hwndDlg,
                                        IDC_CREATE_PARTITION_SIZE,
                                        Buffer,
                                        ARRAYSIZE(Buffer)) == 0)
                    {
                        MessageBoxW(hwndDlg,
                                    L"Enter a partition size in MB.",
                                    DmActionGetCreateDialogTitleForKind(Context,
                                                                        Context->PartitionKind),
                                    MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }

                    Value = _wcstoui64(Buffer, &EndPtr, 10);
                    if (EndPtr == Buffer ||
                        *EndPtr != UNICODE_NULL ||
                        Value == 0 ||
                        Value > DmCreatePartitionGetEffectiveMaxSizeMb(Context))
                    {
                        MessageBoxW(hwndDlg,
                                    L"Enter a valid partition size in MB that does not exceed the maximum size.",
                                    DmActionGetCreateDialogTitleForKind(Context,
                                                                        Context->PartitionKind),
                                    MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }

                    Context->SelectedSizeMb = Value;
                    Context->AssignLetter =
                        (SendDlgItemMessageW(hwndDlg, IDC_CREATE_PARTITION_ASSIGN, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    Context->FormatVolume =
                        (SendDlgItemMessageW(hwndDlg, IDC_CREATE_PARTITION_FORMAT, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    Context->QuickFormat =
                        (SendDlgItemMessageW(hwndDlg, IDC_CREATE_PARTITION_QUICK, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    {
                        INT Selection;

                        Selection = (INT)SendDlgItemMessageW(hwndDlg,
                                                             IDC_CREATE_PARTITION_TYPE,
                                                             CB_GETCURSEL,
                                                             0,
                                                             0);
                        if (Selection == CB_ERR ||
                            Selection < 0 ||
                            (ULONG)Selection >= Context->PartitionKindCount)
                        {
                            MessageBoxW(hwndDlg,
                                        L"Select a partition type for the new partition.",
                                        DmActionGetCreateDialogTitle(Context),
                                        MB_OK | MB_ICONWARNING);
                            return TRUE;
                        }

                        Context->PartitionKind = Context->PartitionKinds[Selection];
                    }
                    Context->SelectedLetter = UNICODE_NULL;
                    Context->SelectedFileSystem[0] = UNICODE_NULL;

                    FileSystemSelection = (INT)SendDlgItemMessageW(hwndDlg,
                                                                   IDC_CREATE_PARTITION_FS,
                                                                   CB_GETCURSEL,
                                                                   0,
                                                                   0);
                    if (FileSystemSelection != CB_ERR)
                    {
                        if (SendDlgItemMessageW(hwndDlg,
                                                IDC_CREATE_PARTITION_FS,
                                                CB_GETLBTEXT,
                                                (WPARAM)FileSystemSelection,
                                                (LPARAM)Context->SelectedFileSystem) == CB_ERR)
                        {
                            Context->SelectedFileSystem[0] = UNICODE_NULL;
                        }
                    }

                    if (Context->AssignLetter)
                    {
                        LetterSelection = (INT)SendDlgItemMessageW(hwndDlg,
                                                                   IDC_CREATE_PARTITION_LETTER,
                                                                   CB_GETCURSEL,
                                                                   0,
                                                                   0);
                        if (LetterSelection == CB_ERR ||
                            LetterSelection < 0 ||
                            (ULONG)LetterSelection >= Context->LetterCount)
                        {
                            MessageBoxW(hwndDlg,
                                        L"Select a drive letter for the new volume.",
                                        DmActionGetCreateDialogTitleForKind(Context,
                                                                            Context->PartitionKind),
                                        MB_OK | MB_ICONWARNING);
                            return TRUE;
                        }

                        Context->SelectedLetter = Context->Letters[LetterSelection];
                    }
                    else if (Context->FormatVolume)
                    {
                        MessageBoxW(hwndDlg,
                                    L"The new volume must have a drive letter before it can be formatted here.",
                                    DmActionGetCreateDialogTitleForKind(Context,
                                                                        Context->PartitionKind),
                                    MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }

                    if (Context->FormatVolume)
                    {
                        if (FileSystemSelection == CB_ERR)
                        {
                            MessageBoxW(hwndDlg,
                                        L"Select a filesystem for the new volume.",
                                        DmActionGetCreateDialogTitleForKind(Context,
                                                                            Context->PartitionKind),
                                        MB_OK | MB_ICONWARNING);
                            return TRUE;
                        }

                        if (SendDlgItemMessageW(hwndDlg,
                                                IDC_CREATE_PARTITION_FS,
                                                CB_GETLBTEXT,
                                                (WPARAM)FileSystemSelection,
                                                (LPARAM)Context->SelectedFileSystem) == CB_ERR)
                        {
                            Context->SelectedFileSystem[0] = UNICODE_NULL;
                        }

                        GetDlgItemTextW(hwndDlg,
                                        IDC_CREATE_PARTITION_LABEL,
                                        Context->Label,
                                        ARRAYSIZE(Context->Label));
                    }

                    EndDialog(hwndDlg, IDOK);
                    return TRUE;
                }

                case IDCANCEL:
                    EndDialog(hwndDlg, IDCANCEL);
                    return TRUE;

                default:
                    break;
            }
            break;

        default:
            break;
    }

    return FALSE;
}

static BOOL
DmActionPromptForCreateVolume(
    _In_ const DM_ACTION_CONTEXT *Context,
    _Out_ PDM_CREATE_PARTITION_DIALOG Dialog,
    _Out_ PULONGLONG PartitionLength)
{
    if (Context == NULL || Context->Region == NULL || Dialog == NULL || PartitionLength == NULL)
        return FALSE;

    ZeroMemory(Dialog, sizeof(*Dialog));
    Dialog->MaxSizeMb = DmActionRegionMaxSizeMb(Context->Region->Length);
    Dialog->SelectedSizeMb = Dialog->MaxSizeMb;
    Dialog->PartitionStyle = Context->Disk->PartitionStyle;
    Dialog->PreferNtfs = (Context->Disk == NULL || !Context->Disk->IsRemovable);
    Dialog->AssignLetter = TRUE;
    Dialog->FormatVolume = TRUE;
    Dialog->QuickFormat = TRUE;
    Dialog->PartitionKindCount = 0;
    Dialog->Caption = DmActionGetCreateVerb(Context);
    Dialog->LogicalMaxSizeMb = 0;
    if (Dialog->PartitionStyle == PARTITION_STYLE_GPT)
    {
        Dialog->PartitionKinds[Dialog->PartitionKindCount++] = DmCreatePartitionBasic;
        Dialog->PartitionKinds[Dialog->PartitionKindCount++] = DmCreatePartitionEfiSystem;
        Dialog->PartitionKinds[Dialog->PartitionKindCount++] = DmCreatePartitionMsr;
        Dialog->PartitionKinds[Dialog->PartitionKindCount++] = DmCreatePartitionRecovery;
        Dialog->PartitionKind = DmCreatePartitionBasic;
    }
    else
    {
        ULONG UsedPrimarySlots;

        UsedPrimarySlots = DmActionGetUsedPrimaryMbrSlotCount(Context->Disk);
        if (Context->Region->IsLogical)
        {
            Dialog->PartitionKinds[Dialog->PartitionKindCount++] = DmCreatePartitionMbrLogical;
            Dialog->PartitionKind = DmCreatePartitionMbrLogical;
        }
        else
        {
            ULONGLONG AlignmentBytes;

            if (UsedPrimarySlots < 4)
                Dialog->PartitionKinds[Dialog->PartitionKindCount++] = DmCreatePartitionMbrPrimary;
            if (!Context->Disk->HasExtendedPartition && UsedPrimarySlots < 4)
            {
                Dialog->PartitionKinds[Dialog->PartitionKindCount++] = DmCreatePartitionMbrExtended;
                AlignmentBytes = DmActionAlignmentBytes(Context);
                if (Context->Region->Length > AlignmentBytes)
                {
                    Dialog->PartitionKinds[Dialog->PartitionKindCount++] = DmCreatePartitionMbrLogical;
                    Dialog->LogicalMaxSizeMb = DmActionRegionMaxSizeMb(Context->Region->Length - AlignmentBytes);
                }
            }
            Dialog->PartitionKind = (Dialog->PartitionKindCount != 0) ?
                                    Dialog->PartitionKinds[0] :
                                    DmCreatePartitionMbrPrimary;
        }
    }
    Dialog->Caption = DmActionGetCreateDialogTitleForKind(Dialog, Dialog->PartitionKind);
    StringCchCopyW(Dialog->SelectedFileSystem,
                   ARRAYSIZE(Dialog->SelectedFileSystem),
                   Dialog->PreferNtfs ? L"NTFS" : L"FAT32");
    if (!DmActionFindAvailableDriveLetters(Dialog->Letters,
                                           ARRAYSIZE(Dialog->Letters),
                                           &Dialog->LetterCount))
    {
        Dialog->LetterCount = 0;
        Dialog->AssignLetter = FALSE;
        Dialog->FormatVolume = FALSE;
        Dialog->SelectedLetter = UNICODE_NULL;
    }
    else
    {
        Dialog->SelectedLetter = Dialog->Letters[0];
    }

    if (!DmActionPartitionKindSupportsAssign(Dialog->PartitionKind))
    {
        Dialog->AssignLetter = FALSE;
        Dialog->FormatVolume = FALSE;
    }

    if (Dialog->MaxSizeMb == 0)
    {
        MessageBoxW(Context->hWnd,
                    L"The selected unallocated region is too small to create a partition here.",
                    DmActionGetCreateDialogTitle(Dialog),
                    MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    if (Dialog->PartitionKindCount == 0)
    {
        MessageBoxW(Context->hWnd,
                    L"No supported MBR partition type can be created from the selected region yet.",
                    DmActionGetCreateDialogTitle(Dialog),
                    MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    if (DialogBoxParamW(hInstance,
                        MAKEINTRESOURCEW(IDD_CREATE_PARTITION),
                        Context->hWnd,
                        DmCreatePartitionDlgProc,
                        (LPARAM)Dialog) != IDOK)
    {
        return FALSE;
    }

    *PartitionLength = DmActionPartitionBytesFromCreateDialog(Context, Dialog);
    if (*PartitionLength == 0)
    {
        MessageBoxW(Context->hWnd,
                    L"The requested partition size is not valid for the selected region.",
                    DmActionGetCreateDialogTitleForKind(Dialog,
                                                        Dialog->PartitionKind),
                    MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    return TRUE;
}

static INT_PTR CALLBACK
DmResizeVolumeDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PDM_RESIZE_VOLUME_DIALOG Dialog;
    WCHAR Buffer[32];
    WCHAR *EndPtr;
    ULONGLONG Value;

    Dialog = (PDM_RESIZE_VOLUME_DIALOG)GetWindowLongPtrW(hwndDlg, DWLP_USER);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            Dialog = (PDM_RESIZE_VOLUME_DIALOG)lParam;
            SetWindowLongPtrW(hwndDlg, DWLP_USER, (LONG_PTR)Dialog);

            if (Dialog == NULL || Dialog->MaxDeltaMb == 0)
            {
                EndDialog(hwndDlg, IDCANCEL);
                return FALSE;
            }

            StringCchPrintfW(Buffer, ARRAYSIZE(Buffer), L"%I64u MB", Dialog->CurrentSizeMb);
            SetDlgItemTextW(hwndDlg, IDC_RESIZE_CURRENT, Buffer);
            StringCchPrintfW(Buffer, ARRAYSIZE(Buffer), L"%I64u MB", Dialog->MaxDeltaMb);
            SetDlgItemTextW(hwndDlg, IDC_RESIZE_LIMIT, Buffer);
            StringCchPrintfW(Buffer, ARRAYSIZE(Buffer), L"%I64u", Dialog->SelectedDeltaMb);
            SetDlgItemTextW(hwndDlg, IDC_RESIZE_AMOUNT, Buffer);
            SendDlgItemMessageW(hwndDlg, IDC_RESIZE_AMOUNT, EM_SETSEL, 0, -1);
            return TRUE;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDOK:
                    if (Dialog == NULL)
                    {
                        EndDialog(hwndDlg, IDCANCEL);
                        return TRUE;
                    }

                    if (GetDlgItemTextW(hwndDlg,
                                        IDC_RESIZE_AMOUNT,
                                        Buffer,
                                        ARRAYSIZE(Buffer)) == 0)
                    {
                        MessageBoxW(hwndDlg,
                                    L"Enter a resize amount in MB.",
                                    L"Disk Management",
                                    MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }

                    Value = _wcstoui64(Buffer, &EndPtr, 10);
                    if (EndPtr == Buffer ||
                        *EndPtr != UNICODE_NULL ||
                        Value == 0 ||
                        Value > Dialog->MaxDeltaMb)
                    {
                        MessageBoxW(hwndDlg,
                                    L"Enter a valid size in MB that does not exceed the maximum amount shown.",
                                    L"Disk Management",
                                    MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }

                    Dialog->SelectedDeltaMb = Value;
                    EndDialog(hwndDlg, IDOK);
                    return TRUE;

                case IDCANCEL:
                    EndDialog(hwndDlg, IDCANCEL);
                    return TRUE;

                default:
                    break;
            }
            break;

        default:
            break;
    }

    return FALSE;
}

static BOOL
DmActionPromptForResizeVolume(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ BOOL Extend,
    _In_ ULONGLONG MaxDeltaBytes,
    _Out_ PULONGLONG DeltaBytes)
{
    DM_RESIZE_VOLUME_DIALOG Dialog;
    INT DialogId;

    if (Context == NULL || Context->Region == NULL || DeltaBytes == NULL || MaxDeltaBytes == 0)
        return FALSE;

    ZeroMemory(&Dialog, sizeof(Dialog));
    Dialog.CurrentSizeMb = DmActionRegionMaxSizeMb(Context->Region->Length);
    Dialog.MaxDeltaMb = DmActionRegionMaxSizeMb(MaxDeltaBytes);
    Dialog.SelectedDeltaMb = Dialog.MaxDeltaMb;
    DialogId = Extend ? IDD_EXTEND_VOLUME : IDD_SHRINK_VOLUME;

    if (Dialog.MaxDeltaMb == 0)
        return FALSE;

    if (DialogBoxParamW(hInstance,
                        MAKEINTRESOURCEW(DialogId),
                        Context->hWnd,
                        DmResizeVolumeDlgProc,
                        (LPARAM)&Dialog) != IDOK)
    {
        return FALSE;
    }

    *DeltaBytes = DmActionAlignedBytesFromSizeMb(Context,
                                                 Dialog.SelectedDeltaMb,
                                                 MaxDeltaBytes);
    if (*DeltaBytes == 0)
    {
        MessageBoxW(Context->hWnd,
                    L"The requested resize amount is not valid for the selected volume.",
                    L"Disk Management",
                    MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    return TRUE;
}

static BOOL
DmActionDismountVolume(
    _In_opt_ HWND hWnd,
    _In_ const DM_VOLUME *Volume)
{
    WCHAR VolumePath[MAX_PATH];
    SIZE_T Length;
    HANDLE Handle;
    DWORD BytesReturned;

    if (Volume == NULL || Volume->VolumeName[0] == UNICODE_NULL)
        return TRUE;

    StringCchCopyW(VolumePath, ARRAYSIZE(VolumePath), Volume->VolumeName);
    Length = wcslen(VolumePath);
    if (Length > 0 && VolumePath[Length - 1] == L'\\')
        VolumePath[Length - 1] = UNICODE_NULL;

    Handle = CreateFileW(VolumePath,
                         GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        DmActionShowWin32Error(hWnd,
                               L"Unable to open the selected volume for dismount.",
                               GetLastError());
        return FALSE;
    }

    BytesReturned = 0;
    DeviceIoControl(Handle,
                    FSCTL_LOCK_VOLUME,
                    NULL,
                    0,
                    NULL,
                    0,
                    &BytesReturned,
                    NULL);

    if (!DeviceIoControl(Handle,
                         FSCTL_DISMOUNT_VOLUME,
                         NULL,
                         0,
                         NULL,
                         0,
                         &BytesReturned,
                         NULL))
    {
        DWORD Error;

        Error = GetLastError();
        DeviceIoControl(Handle,
                        FSCTL_UNLOCK_VOLUME,
                        NULL,
                        0,
                        NULL,
                        0,
                        &BytesReturned,
                        NULL);
        CloseHandle(Handle);
        DmActionShowWin32Error(hWnd,
                               L"Unable to dismount the selected volume.",
                               Error);
        return FALSE;
    }

    DeviceIoControl(Handle,
                    FSCTL_UNLOCK_VOLUME,
                    NULL,
                    0,
                    NULL,
                    0,
                    &BytesReturned,
                    NULL);
    CloseHandle(Handle);
    return TRUE;
}

static BOOL
DmActionFindAvailableDriveLetters(
    _Out_writes_(cLetters) PWCHAR Letters,
    _In_ ULONG cLetters,
    _Out_ PULONG LetterCount)
{
    DWORD Mask;
    WCHAR Candidate;
    ULONG Count;

    if (Letters == NULL || cLetters == 0 || LetterCount == NULL)
        return FALSE;

    Count = 0;
    Mask = GetLogicalDrives();
    for (Candidate = L'D'; Candidate <= L'Z' && Count < cLetters; Candidate++)
    {
        if ((Mask & (1u << (Candidate - L'A'))) == 0)
            Letters[Count++] = Candidate;
    }

    *LetterCount = Count;
    return (Count != 0);
}

static INT_PTR CALLBACK
DmAssignLetterDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PDM_ASSIGN_LETTER_DIALOG Context;
    WCHAR Item[4];
    ULONG Index;
    INT Selection;

    Context = (PDM_ASSIGN_LETTER_DIALOG)GetWindowLongPtrW(hwndDlg, DWLP_USER);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            Context = (PDM_ASSIGN_LETTER_DIALOG)lParam;
            SetWindowLongPtrW(hwndDlg, DWLP_USER, (LONG_PTR)Context);

            if (Context == NULL || Context->LetterCount == 0)
            {
                EndDialog(hwndDlg, IDCANCEL);
                return FALSE;
            }

            for (Index = 0; Index < Context->LetterCount; Index++)
            {
                Item[0] = Context->Letters[Index];
                Item[1] = L':';
                Item[2] = UNICODE_NULL;
                SendDlgItemMessageW(hwndDlg,
                                    IDC_ASSIGN_LETTER_COMBO,
                                    CB_ADDSTRING,
                                    0,
                                    (LPARAM)Item);
            }

            SendDlgItemMessageW(hwndDlg,
                                IDC_ASSIGN_LETTER_COMBO,
                                CB_SETCURSEL,
                                0,
                                0);
            return TRUE;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDOK:
                    if (Context == NULL)
                    {
                        EndDialog(hwndDlg, IDCANCEL);
                        return TRUE;
                    }

                    Selection = (INT)SendDlgItemMessageW(hwndDlg,
                                                         IDC_ASSIGN_LETTER_COMBO,
                                                         CB_GETCURSEL,
                                                         0,
                                                         0);
                    if (Selection == CB_ERR ||
                        Selection < 0 ||
                        (ULONG)Selection >= Context->LetterCount)
                    {
                        EndDialog(hwndDlg, IDCANCEL);
                        return TRUE;
                    }

                    Context->SelectedLetter = Context->Letters[Selection];
                    EndDialog(hwndDlg, IDOK);
                    return TRUE;

                case IDCANCEL:
                    EndDialog(hwndDlg, IDCANCEL);
                    return TRUE;

                default:
                    break;
            }
            break;

        default:
            break;
    }

    return FALSE;
}

static BOOL
DmActionPromptForDriveLetter(
    _In_ HWND hWnd,
    _Out_ PWCHAR Letter)
{
    DM_ASSIGN_LETTER_DIALOG Dialog;

    if (Letter == NULL)
        return FALSE;

    ZeroMemory(&Dialog, sizeof(Dialog));
    if (!DmActionFindAvailableDriveLetters(Dialog.Letters,
                                           ARRAYSIZE(Dialog.Letters),
                                           &Dialog.LetterCount))
    {
        MessageBoxW(hWnd,
                    L"No free drive letters are available.",
                    L"Disk Management",
                    MB_OK | MB_ICONERROR);
        return FALSE;
    }

    Dialog.SelectedLetter = Dialog.Letters[0];
    if (DialogBoxParamW(hInstance,
                        MAKEINTRESOURCEW(IDD_ASSIGN_LETTER),
                        hWnd,
                        DmAssignLetterDlgProc,
                        (LPARAM)&Dialog) != IDOK)
    {
        return FALSE;
    }

    if (Dialog.SelectedLetter == UNICODE_NULL)
        return FALSE;

    *Letter = Dialog.SelectedLetter;
    return TRUE;
}

static BOOL
DmActionAssignDriveLetter(
    _In_ const DM_ACTION_CONTEXT *Context)
{
    WCHAR Letter;

    if (Context == NULL || Context->Volume == NULL)
        return FALSE;

    if (!DmActionEnsureSimpleBasicVolume(Context, L"Assign Drive Letter"))
        return FALSE;

    if (Context->Volume->HasDriveLetter)
    {
        MessageBoxW(Context->hWnd,
                    L"The selected volume already has a drive letter.",
                    L"Disk Management",
                    MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    if (Context->Volume->IsBoot || Context->Volume->IsSystem)
    {
        MessageBoxW(Context->hWnd,
                    L"A drive letter cannot be assigned here for the selected boot or system volume.",
                    L"Disk Management",
                    MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    if (!DmActionPromptForDriveLetter(Context->hWnd, &Letter))
    {
        return FALSE;
    }

    if (!StorageUtilAssignDriveLetter(Context->Volume->DeviceName, Letter))
    {
        DmActionShowWin32Error(Context->hWnd,
                               L"Assigning the drive letter failed.",
                               GetLastError());
        return FALSE;
    }

    return TRUE;
}

static BOOL
DmActionPopulateFormatFileSystems(
    _In_ HWND hwndDlg,
    _In_ const DM_FORMAT_VOLUME_DIALOG *Dialog,
    _In_ BOOL PreferNtfs)
{
    if (Dialog == NULL)
        return FALSE;

    if (!DmActionPopulateFileSystemCombo(hwndDlg,
                                         IDC_FORMAT_FILESYSTEM,
                                         Dialog->SelectedFileSystem,
                                         PreferNtfs))
    {
        return FALSE;
    }

    SetDlgItemTextW(hwndDlg, IDC_FORMAT_LABEL, Dialog->Label);
    SendDlgItemMessageW(hwndDlg,
                        IDC_FORMAT_QUICK,
                        BM_SETCHECK,
                        Dialog->QuickFormat ? BST_CHECKED : BST_UNCHECKED,
                        0);
    return TRUE;
}

static INT_PTR CALLBACK
DmFormatVolumeDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PDM_FORMAT_VOLUME_DIALOG Dialog;
    BOOL PreferNtfs;

    Dialog = (PDM_FORMAT_VOLUME_DIALOG)GetWindowLongPtrW(hwndDlg, DWLP_USER);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            Dialog = (PDM_FORMAT_VOLUME_DIALOG)lParam;
            SetWindowLongPtrW(hwndDlg, DWLP_USER, (LONG_PTR)Dialog);

            if (Dialog == NULL)
            {
                EndDialog(hwndDlg, IDCANCEL);
                return FALSE;
            }

            PreferNtfs = (_wcsicmp(Dialog->SelectedFileSystem, L"FAT") != 0 &&
                          _wcsicmp(Dialog->SelectedFileSystem, L"FAT32") != 0);
            if (!DmActionPopulateFormatFileSystems(hwndDlg, Dialog, PreferNtfs))
            {
                MessageBoxW(hwndDlg,
                            L"No filesystem providers are available for formatting.",
                            L"Format",
                            MB_OK | MB_ICONERROR);
                EndDialog(hwndDlg, IDCANCEL);
                return FALSE;
            }

            SendDlgItemMessageW(hwndDlg, IDC_FORMAT_LABEL, EM_LIMITTEXT, 32, 0);
            return TRUE;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDOK:
                {
                    INT Selection;

                    if (Dialog == NULL)
                    {
                        EndDialog(hwndDlg, IDCANCEL);
                        return TRUE;
                    }

                    Selection = (INT)SendDlgItemMessageW(hwndDlg,
                                                         IDC_FORMAT_FILESYSTEM,
                                                         CB_GETCURSEL,
                                                         0,
                                                         0);
                    if (Selection == CB_ERR)
                    {
                        MessageBoxW(hwndDlg,
                                    L"Select a filesystem for the format operation.",
                                    L"Format",
                                    MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }

                    if (SendDlgItemMessageW(hwndDlg,
                                            IDC_FORMAT_FILESYSTEM,
                                            CB_GETLBTEXT,
                                            (WPARAM)Selection,
                                            (LPARAM)Dialog->SelectedFileSystem) == CB_ERR)
                    {
                        Dialog->SelectedFileSystem[0] = UNICODE_NULL;
                    }

                    GetDlgItemTextW(hwndDlg,
                                    IDC_FORMAT_LABEL,
                                    Dialog->Label,
                                    ARRAYSIZE(Dialog->Label));
                    Dialog->QuickFormat =
                        (SendDlgItemMessageW(hwndDlg, IDC_FORMAT_QUICK, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    EndDialog(hwndDlg, IDOK);
                    return TRUE;
                }

                case IDCANCEL:
                    EndDialog(hwndDlg, IDCANCEL);
                    return TRUE;

                default:
                    break;
            }
            break;

        default:
            break;
    }

    return FALSE;
}

static BOOL
DmActionBuildDriveRoot(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchDriveRoot) PWSTR DriveRoot,
    _In_ SIZE_T cchDriveRoot)
{
    if (Volume == NULL || DriveRoot == NULL || cchDriveRoot < 4 || !Volume->HasDriveLetter)
        return FALSE;

    DriveRoot[0] = towupper(Volume->DriveLetter);
    DriveRoot[1] = L':';
    DriveRoot[2] = L'\\';
    DriveRoot[3] = UNICODE_NULL;
    return TRUE;
}

static UINT
DmActionGetVolumeDriveType(
    _In_ const DM_VOLUME *Volume)
{
    WCHAR DriveRoot[4];

    if (!DmActionBuildDriveRoot(Volume, DriveRoot, ARRAYSIZE(DriveRoot)))
        return DRIVE_NO_ROOT_DIR;

    return GetDriveTypeW(DriveRoot);
}

static BOOL
DmActionPromptForFormat(
    _In_ const DM_ACTION_CONTEXT *Context,
    _Out_ PDM_FORMAT_VOLUME_DIALOG Dialog)
{
    DWORD DriveType;
    WCHAR DriveRoot[4];

    if (Context == NULL || Context->Volume == NULL || Dialog == NULL)
        return FALSE;

    ZeroMemory(Dialog, sizeof(*Dialog));
    StringCchCopyW(Dialog->Label, ARRAYSIZE(Dialog->Label), Context->Volume->Label);
    if (Context->Volume->FileSystem[0] != UNICODE_NULL &&
        _wcsicmp(Context->Volume->FileSystem, L"RAW") != 0)
    {
        StringCchCopyW(Dialog->SelectedFileSystem,
                       ARRAYSIZE(Dialog->SelectedFileSystem),
                       Context->Volume->FileSystem);
    }

    if (!DmActionBuildDriveRoot(Context->Volume, DriveRoot, ARRAYSIZE(DriveRoot)))
        return FALSE;

    DriveType = GetDriveTypeW(DriveRoot);
    Dialog->QuickFormat = (DriveType != DRIVE_REMOVABLE);

    if (DialogBoxParamW(hInstance,
                        MAKEINTRESOURCEW(IDD_FORMAT_VOLUME),
                        Context->hWnd,
                        DmFormatVolumeDlgProc,
                        (LPARAM)Dialog) != IDOK)
    {
        return FALSE;
    }

    return (Dialog->SelectedFileSystem[0] != UNICODE_NULL);
}

static FMIFS_MEDIA_FLAG
DmActionGetFormatMediaFlag(
    _In_z_ PCWSTR DriveRoot)
{
    switch (GetDriveTypeW(DriveRoot))
    {
        case DRIVE_REMOVABLE:
            return FMIFS_FLOPPY;

        case DRIVE_FIXED:
        case DRIVE_RAMDISK:
            return FMIFS_HARDDISK;

        default:
            return FMIFS_HARDDISK;
    }
}

static BOOLEAN NTAPI
DmActionFormatCallback(
    _In_ CALLBACKCOMMAND Command,
    _In_ ULONG SubAction,
    _In_opt_ PVOID ActionInfo)
{
    PDM_FORMAT_RUNTIME_CONTEXT Runtime;
    WCHAR StatusText[64];

    UNREFERENCED_PARAMETER(SubAction);

    Runtime = DmCurrentFormatContext;
    if (Runtime == NULL)
        return TRUE;

    switch (Command)
    {
        case PROGRESS:
            if (ActionInfo != NULL)
            {
                DWORD Progress;

                Progress = *(PDWORD)ActionInfo;
                if (Progress != Runtime->LastProgress &&
                    Runtime->ActionContext != NULL &&
                    Runtime->ActionContext->hStatusBar != NULL)
                {
                    Runtime->LastProgress = Progress;
                    StringCchPrintfW(StatusText,
                                     ARRAYSIZE(StatusText),
                                     L"Formatting volume... %lu%%",
                                     Progress);
                    SendMessageW(Runtime->ActionContext->hStatusBar,
                                 SB_SETTEXTW,
                                 1,
                                 (LPARAM)StatusText);
                    UpdateWindow(Runtime->ActionContext->hStatusBar);
                }
            }
            break;

        case DONE:
            if (ActionInfo != NULL)
                Runtime->Success = (*(PBOOLEAN)ActionInfo != FALSE);
            break;

        case VOLUMEINUSE:
            Runtime->Error = DmFormatErrorVolumeInUse;
            break;

        case INSUFFICIENTRIGHTS:
            Runtime->Error = DmFormatErrorInsufficientRights;
            break;

        case FSNOTSUPPORTED:
            Runtime->Error = DmFormatErrorFsNotSupported;
            break;

        case CLUSTERSIZETOOSMALL:
            Runtime->Error = DmFormatErrorClusterSizeTooSmall;
            break;

        default:
            break;
    }

    return TRUE;
}

static PCWSTR
DmActionGetFormatFailureText(
    _In_ DM_FORMAT_ERROR Error)
{
    PCWSTR Message;

    Message = L"Formatting the selected volume failed.";
    switch (Error)
    {
        case DmFormatErrorVolumeInUse:
            Message = L"The selected volume is currently in use and could not be formatted.";
            break;

        case DmFormatErrorInsufficientRights:
            Message = L"Administrative rights are required to format the selected volume.";
            break;

        case DmFormatErrorFsNotSupported:
            Message = L"The selected filesystem is not supported for this format operation.";
            break;

        case DmFormatErrorClusterSizeTooSmall:
            Message = L"The requested cluster size is too small for the selected filesystem.";
            break;

        case DmFormatErrorUnknown:
        case DmFormatErrorNone:
        default:
            break;
    }

    return Message;
}

static VOID
DmActionShowFormatFailure(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ DM_FORMAT_ERROR Error)
{
    MessageBoxW(Context != NULL ? Context->hWnd : NULL,
                DmActionGetFormatFailureText(Error),
                L"Format",
                MB_OK | MB_ICONERROR);
}

static BOOL
DmActionRunFormat(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_z_ PCWSTR DriveRoot,
    _In_z_ PCWSTR FileSystem,
    _In_z_ PCWSTR Label,
    _In_ BOOL QuickFormat,
    _In_ BOOL ShowErrors,
    _Out_opt_ DM_FORMAT_ERROR *FormatError)
{
    DM_FORMAT_RUNTIME_CONTEXT Runtime;
    FMIFS_MEDIA_FLAG MediaFlag;
    WCHAR MutableDriveRoot[4];
    WCHAR MutableFileSystem[32];
    WCHAR MutableLabel[MAX_PATH];

    if (DriveRoot == NULL || FileSystem == NULL || FileSystem[0] == UNICODE_NULL)
    {
        if (FormatError != NULL)
            *FormatError = DmFormatErrorUnknown;
        return FALSE;
    }

    ZeroMemory(&Runtime, sizeof(Runtime));
    Runtime.ActionContext = Context;
    Runtime.Error = DmFormatErrorUnknown;
    StringCchCopyW(Runtime.DriveRoot, ARRAYSIZE(Runtime.DriveRoot), DriveRoot);
    StringCchCopyW(Runtime.Label, ARRAYSIZE(Runtime.Label), Label != NULL ? Label : L"");
    StringCchCopyW(MutableDriveRoot, ARRAYSIZE(MutableDriveRoot), DriveRoot);
    StringCchCopyW(MutableFileSystem, ARRAYSIZE(MutableFileSystem), FileSystem);
    StringCchCopyW(MutableLabel, ARRAYSIZE(MutableLabel), Label != NULL ? Label : L"");
    MediaFlag = DmActionGetFormatMediaFlag(DriveRoot);

    DmCurrentFormatContext = &Runtime;
    FormatEx(MutableDriveRoot,
             MediaFlag,
             MutableFileSystem,
             MutableLabel,
             QuickFormat,
             0,
             DmActionFormatCallback);
    DmCurrentFormatContext = NULL;

    if (!Runtime.Success)
    {
        if (FormatError != NULL)
            *FormatError = Runtime.Error;
        if (ShowErrors)
            DmActionShowFormatFailure(Context, Runtime.Error);
        return FALSE;
    }

    if (FormatError != NULL)
        *FormatError = DmFormatErrorNone;
    SetVolumeLabelW(DriveRoot, (Label != NULL && Label[0] != UNICODE_NULL) ? Label : NULL);
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, DriveRoot, NULL);
    return TRUE;
}

static BOOL
DmActionWaitForDriveRoot(
    _In_z_ PCWSTR DriveRoot)
{
    ULONG Attempt;

    if (DriveRoot == NULL || DriveRoot[0] == UNICODE_NULL)
        return FALSE;

    for (Attempt = 0; Attempt < 20; Attempt++)
    {
        UINT DriveType;

        DriveType = GetDriveTypeW(DriveRoot);
        if (DriveType != DRIVE_UNKNOWN && DriveType != DRIVE_NO_ROOT_DIR)
            return TRUE;

        Sleep(200);
    }

    return FALSE;
}

static PDM_VOLUME
DmActionFindCreatedVolumeInSnapshot(
    _In_ PDM_SNAPSHOT Snapshot,
    _In_ ULONG DiskNumber,
    _In_ ULONGLONG StartOffset,
    _In_ ULONGLONG PartitionLength)
{
    PDM_DISK Disk;
    ULONG RegionIndex;

    if (Snapshot == NULL)
        return NULL;

    Disk = DmSnapshotFindDiskByNumber(Snapshot, DiskNumber);
    if (Disk == NULL)
        return NULL;

    for (RegionIndex = 0; RegionIndex < Disk->RegionCount; RegionIndex++)
    {
        PDM_REGION Region;
        ULONG VolumeIndex;

        Region = &Disk->Regions[RegionIndex];
        if (Region->Type != DmRegionPartition ||
            Region->StartOffset != StartOffset ||
            Region->Length != PartitionLength)
        {
            continue;
        }

        if (Region->Volume != NULL)
            return Region->Volume;

        for (VolumeIndex = 0; VolumeIndex < Snapshot->VolumeCount; VolumeIndex++)
        {
            PDM_VOLUME Volume;

            Volume = &Snapshot->Volumes[VolumeIndex];
            if (Volume->HasStorageDeviceNumber &&
                Volume->StorageDiskNumber == DiskNumber &&
                Volume->StoragePartitionNumber == Region->PartitionNumber)
            {
                return Volume;
            }
        }
    }

    return NULL;
}

static BOOL
DmActionWaitForCreatedVolume(
    _In_ ULONG DiskNumber,
    _In_ ULONGLONG StartOffset,
    _In_ ULONGLONG PartitionLength,
    _Out_ PDM_SNAPSHOT Snapshot,
    _Outptr_ PDM_VOLUME *Volume)
{
    ULONG Attempt;
    NTSTATUS Status;

    if (Snapshot == NULL || Volume == NULL)
        return FALSE;

    DmSnapshotInitialize(Snapshot);
    *Volume = NULL;

    for (Attempt = 0; Attempt < 20; Attempt++)
    {
        Status = DmSnapshotRefresh(Snapshot);
        if (NT_SUCCESS(Status))
        {
            *Volume = DmActionFindCreatedVolumeInSnapshot(Snapshot,
                                                          DiskNumber,
                                                          StartOffset,
                                                          PartitionLength);
            if (*Volume != NULL)
                return TRUE;
        }

        Sleep(200);
    }

    return FALSE;
}

static VOID
DmActionShowCreateVolumeWarning(
    _In_opt_ HWND hWnd,
    _In_z_ PCWSTR Title,
    _In_z_ PCWSTR Message)
{
    MessageBoxW(hWnd,
                Message,
                Title,
                MB_OK | MB_ICONWARNING);
}

static BOOL
DmActionResizeVolume(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ BOOL Extend)
{
    const DM_REGION *FreeRegion;
    WCHAR TargetText[128];
    WCHAR Prompt[512];
    HANDLE Handle;
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    PPARTITION_INFORMATION_EX Entry;
    DWORD BytesReturned;
    DWORD LayoutSize;
    ULONGLONG DeltaBytes;
    ULONGLONG MaxDeltaBytes;
    PCWSTR Title;

    if (Context == NULL || Context->Disk == NULL || Context->Volume == NULL || Context->Region == NULL)
        return FALSE;

    if (!DmActionEnsureBasicDiskLayout(Context, Extend ? L"Extend Volume" : L"Shrink Volume"))
        return FALSE;

    if (!DmActionCanResizeRawVolume(Context, Extend))
    {
        MessageBoxW(Context->hWnd,
                    Extend
                        ? L"Only single-extent RAW volumes with right-adjacent free space can be extended here right now."
                        : L"Only single-extent RAW volumes can be shrunk here right now.",
                    L"Disk Management",
                    MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    FreeRegion = DmActionFindRightAdjacentFreeRegion(Context);
    MaxDeltaBytes = Extend ? (FreeRegion != NULL ? FreeRegion->Length : 0) :
                             (Context->Region->Length - DmActionAlignmentBytes(Context));
    if (MaxDeltaBytes == 0 ||
        !DmActionPromptForResizeVolume(Context, Extend, MaxDeltaBytes, &DeltaBytes))
    {
        return FALSE;
    }

    DmVolumeListBuildName(Context->Volume, TargetText, ARRAYSIZE(TargetText));
    Title = Extend ? L"Extend Volume" : L"Shrink Volume";
    StringCchPrintfW(Prompt,
                     ARRAYSIZE(Prompt),
                     Extend
                         ? L"Extend %s by %I64u MB now?\r\n\r\nThis first-pass implementation is limited to single-extent RAW volumes with right-adjacent free space."
                         : L"Shrink %s by %I64u MB now?\r\n\r\nThis first-pass implementation is limited to single-extent RAW volumes.",
                     (TargetText[0] != UNICODE_NULL) ? TargetText : L"the selected volume",
                     DmActionRegionMaxSizeMb(DeltaBytes));
    if (MessageBoxW(Context->hWnd,
                    Prompt,
                    Title,
                    MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
    {
        return FALSE;
    }

    if (!DmActionDismountVolume(Context->hWnd, Context->Volume))
        return FALSE;

    Handle = CreateFileW(Context->Disk->DeviceName,
                         GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        DmActionShowWin32Error(Context->hWnd,
                               Extend
                                   ? L"Unable to open the selected disk for volume extension."
                                   : L"Unable to open the selected disk for volume shrink.",
                               GetLastError());
        return FALSE;
    }

    if (!DmActionQueryDiskLayout(Handle, &Layout))
    {
        DWORD Error;

        Error = GetLastError();
        CloseHandle(Handle);
        DmActionShowWin32Error(Context->hWnd,
                               L"Unable to query the current disk layout.",
                               Error);
        return FALSE;
    }

    Entry = DmActionFindLayoutPartition(Layout, Context->Region);
    if (Entry == NULL)
    {
        HeapFree(ProcessHeap, 0, Layout);
        CloseHandle(Handle);
        MessageBoxW(Context->hWnd,
                    L"The selected volume could not be matched to the current disk layout.",
                    L"Disk Management",
                    MB_OK | MB_ICONERROR);
        return FALSE;
    }

    if (Extend)
        Entry->PartitionLength.QuadPart += DeltaBytes;
    else
        Entry->PartitionLength.QuadPart -= DeltaBytes;
    Entry->RewritePartition = TRUE;

    LayoutSize = sizeof(DRIVE_LAYOUT_INFORMATION_EX) +
                 ((Layout->PartitionCount - ANYSIZE_ARRAY) * sizeof(PARTITION_INFORMATION_EX));
    BytesReturned = 0;
    if (!DeviceIoControl(Handle,
                         IOCTL_DISK_SET_DRIVE_LAYOUT_EX,
                         Layout,
                         LayoutSize,
                         Layout,
                         LayoutSize,
                         &BytesReturned,
                         NULL))
    {
        DWORD Error;

        Error = GetLastError();
        HeapFree(ProcessHeap, 0, Layout);
        CloseHandle(Handle);
        DmActionShowWin32Error(Context->hWnd,
                               Extend
                                   ? L"Extending the selected volume failed."
                                   : L"Shrinking the selected volume failed.",
                               Error);
        return FALSE;
    }

    DeviceIoControl(Handle,
                    IOCTL_DISK_UPDATE_PROPERTIES,
                    NULL,
                    0,
                    NULL,
                    0,
                    &BytesReturned,
                    NULL);
    HeapFree(ProcessHeap, 0, Layout);
    CloseHandle(Handle);
    return TRUE;
}

static BOOL
DmActionExtendVolume(
    _In_ const DM_ACTION_CONTEXT *Context)
{
    return DmActionResizeVolume(Context, TRUE);
}

static BOOL
DmActionShrinkVolume(
    _In_ const DM_ACTION_CONTEXT *Context)
{
    return DmActionResizeVolume(Context, FALSE);
}

static BOOL
DmActionFormatVolume(
    _In_ const DM_ACTION_CONTEXT *Context)
{
    DM_FORMAT_VOLUME_DIALOG Dialog;
    WCHAR DriveRoot[4];
    WCHAR Prompt[384];

    if (Context == NULL || Context->Volume == NULL)
        return FALSE;

    if (!DmActionEnsureSimpleBasicVolume(Context, L"Format"))
        return FALSE;

    if (!Context->Volume->HasDriveLetter)
    {
        MessageBoxW(Context->hWnd,
                    L"The selected volume must have a drive letter before it can be formatted here.",
                    L"Disk Management",
                    MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    if (Context->Volume->IsBoot || Context->Volume->IsSystem)
    {
        MessageBoxW(Context->hWnd,
                    L"The current boot or system volume cannot be formatted here.",
                    L"Disk Management",
                    MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    if (Context->Disk != NULL && (Context->Disk->IsOffline || Context->Disk->IsReadOnly))
    {
        MessageBoxW(Context->hWnd,
                    L"The selected disk must be online and writable before formatting a volume.",
                    L"Disk Management",
                    MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    if (!DmActionBuildDriveRoot(Context->Volume, DriveRoot, ARRAYSIZE(DriveRoot)))
        return FALSE;

    switch (GetDriveTypeW(DriveRoot))
    {
        case DRIVE_UNKNOWN:
        case DRIVE_NO_ROOT_DIR:
        case DRIVE_REMOTE:
        case DRIVE_CDROM:
            MessageBoxW(Context->hWnd,
                        L"The selected volume cannot be formatted through Disk Management yet.",
                        L"Format",
                        MB_OK | MB_ICONWARNING);
            return FALSE;

        default:
            break;
    }

    if (!DmActionPromptForFormat(Context, &Dialog))
    {
        return FALSE;
    }

    StringCchPrintfW(Prompt,
                     ARRAYSIZE(Prompt),
                     L"Format %C: as %s%s%s now?\r\n\r\nAll data on the volume will be lost.",
                     towupper(Context->Volume->DriveLetter),
                     Dialog.SelectedFileSystem,
                     Dialog.Label[0] != UNICODE_NULL ? L" with label " : L"",
                     Dialog.Label[0] != UNICODE_NULL ? Dialog.Label : L"");
    if (MessageBoxW(Context->hWnd,
                    Prompt,
                    L"Format",
                    MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
    {
        return FALSE;
    }

    return DmActionRunFormat(Context,
                             DriveRoot,
                             Dialog.SelectedFileSystem,
                             Dialog.Label,
                             Dialog.QuickFormat,
                             TRUE,
                             NULL);
}

static BOOL
DmActionEnsureTrailingBackslash(
    _Inout_updates_(cchPath) PWSTR Path,
    _In_ SIZE_T cchPath)
{
    SIZE_T Length;

    if (Path == NULL || cchPath == 0)
        return FALSE;

    Length = wcslen(Path);
    if (Length == 0)
        return FALSE;

    if (Path[Length - 1] == L'\\')
        return TRUE;

    if (Length + 1 >= cchPath)
        return FALSE;

    Path[Length] = L'\\';
    Path[Length + 1] = UNICODE_NULL;
    return TRUE;
}

static BOOL
DmActionIsRootPath(
    _In_z_ PCWSTR Path)
{
    return (Path != NULL &&
            wcslen(Path) == 3 &&
            Path[0] != UNICODE_NULL &&
            Path[1] == L':' &&
            Path[2] == L'\\');
}

static BOOL
DmActionIsDirectoryEmpty(
    _In_z_ PCWSTR Path)
{
    WCHAR SearchPattern[MAX_PATH];
    WIN32_FIND_DATAW FindData;
    HANDLE FindHandle;
    BOOL Empty = TRUE;

    StringCchPrintfW(SearchPattern, ARRAYSIZE(SearchPattern), L"%s*", Path);
    FindHandle = FindFirstFileW(SearchPattern, &FindData);
    if (FindHandle == INVALID_HANDLE_VALUE)
        return FALSE;

    do
    {
        if (wcscmp(FindData.cFileName, L".") != 0 &&
            wcscmp(FindData.cFileName, L"..") != 0)
        {
            Empty = FALSE;
            break;
        }
    } while (FindNextFileW(FindHandle, &FindData));

    FindClose(FindHandle);
    return Empty;
}

static BOOL
DmActionBrowseForMountPath(
    _In_ HWND hWnd,
    _Out_writes_(cchPath) PWSTR Path,
    _In_ SIZE_T cchPath)
{
    BROWSEINFOW BrowseInfo;
    WCHAR DisplayName[MAX_PATH];
    PIDLIST_ABSOLUTE ItemIdList;

    ZeroMemory(&BrowseInfo, sizeof(BrowseInfo));
    ZeroMemory(DisplayName, sizeof(DisplayName));

    BrowseInfo.hwndOwner = hWnd;
    BrowseInfo.pszDisplayName = DisplayName;
    BrowseInfo.lpszTitle = L"Select an empty folder to use as a volume mount path.";
    BrowseInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;

    ItemIdList = SHBrowseForFolderW(&BrowseInfo);
    if (ItemIdList == NULL)
        return FALSE;

    Path[0] = UNICODE_NULL;
    if (!SHGetPathFromIDListW(ItemIdList, Path))
    {
        ILFree(ItemIdList);
        return FALSE;
    }

    ILFree(ItemIdList);
    return DmActionEnsureTrailingBackslash(Path, cchPath);
}

static BOOL
DmActionQueryVolumePaths(
    _In_ const DM_VOLUME *Volume,
    _Outptr_result_buffer_(*PathCount) PWSTR *PathBuffer,
    _Out_ PDWORD PathCount)
{
    PWSTR Buffer;
    DWORD BufferCount;
    DWORD RequiredCount;
    BOOL Success;
    DWORD Error;

    if (Volume == NULL ||
        Volume->VolumeName[0] == UNICODE_NULL ||
        PathBuffer == NULL ||
        PathCount == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *PathBuffer = NULL;
    *PathCount = 0;
    BufferCount = 512;

    for (;;)
    {
        Buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, BufferCount * sizeof(WCHAR));
        if (Buffer == NULL)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }

        RequiredCount = 0;
        Success = GetVolumePathNamesForVolumeNameW(Volume->VolumeName,
                                                   Buffer,
                                                   BufferCount,
                                                   &RequiredCount);
        if (Success)
        {
            *PathBuffer = Buffer;
            *PathCount = BufferCount;
            return TRUE;
        }

        Error = GetLastError();
        HeapFree(GetProcessHeap(), 0, Buffer);
        if (Error != ERROR_MORE_DATA || RequiredCount <= BufferCount)
        {
            SetLastError(Error);
            return FALSE;
        }

        BufferCount = RequiredCount + 2;
    }
}

static BOOL
DmActionCollectFolderMountPoints(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cPaths) WCHAR (*Paths)[MAX_PATH],
    _In_ ULONG cPaths,
    _Out_ PULONG PathCount)
{
    PWSTR PathBuffer;
    DWORD BufferCount;
    PWSTR Path;
    ULONG Count;

    if (Volume == NULL || Paths == NULL || cPaths == 0 || PathCount == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *PathCount = 0;
    if (!DmActionQueryVolumePaths(Volume, &PathBuffer, &BufferCount))
        return FALSE;

    UNREFERENCED_PARAMETER(BufferCount);
    Count = 0;
    Path = PathBuffer;
    while (*Path != UNICODE_NULL)
    {
        if (!DmActionIsRootPath(Path) && Count < cPaths)
        {
            StringCchCopyW(Paths[Count], MAX_PATH, Path);
            Count++;
        }

        Path += wcslen(Path) + 1;
    }

    HeapFree(GetProcessHeap(), 0, PathBuffer);
    *PathCount = Count;
    return TRUE;
}

static INT_PTR CALLBACK
DmSelectMountPathDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PDM_SELECT_MOUNT_PATH_DIALOG Context;
    ULONG Index;
    INT Selection;

    Context = (PDM_SELECT_MOUNT_PATH_DIALOG)GetWindowLongPtrW(hwndDlg, DWLP_USER);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            Context = (PDM_SELECT_MOUNT_PATH_DIALOG)lParam;
            SetWindowLongPtrW(hwndDlg, DWLP_USER, (LONG_PTR)Context);

            if (Context == NULL || Context->PathCount == 0)
            {
                EndDialog(hwndDlg, IDCANCEL);
                return FALSE;
            }

            if (Context->Caption != NULL)
                SetWindowTextW(hwndDlg, Context->Caption);
            if (Context->Prompt != NULL)
                SetDlgItemTextW(hwndDlg, IDC_SELECT_MOUNT_PATH_PROMPT, Context->Prompt);

            for (Index = 0; Index < Context->PathCount; Index++)
            {
                SendDlgItemMessageW(hwndDlg,
                                    IDC_SELECT_MOUNT_PATH_LIST,
                                    LB_ADDSTRING,
                                    0,
                                    (LPARAM)Context->Paths[Index]);
            }

            SendDlgItemMessageW(hwndDlg,
                                IDC_SELECT_MOUNT_PATH_LIST,
                                LB_SETCURSEL,
                                0,
                                0);
            Context->SelectedIndex = 0;
            if (Context->AllowReplace)
            {
                CheckDlgButton(hwndDlg,
                               IDC_SELECT_MOUNT_PATH_REPLACE,
                               Context->ReplaceExisting ? BST_CHECKED : BST_UNCHECKED);
            }
            else
            {
                ShowWindow(GetDlgItem(hwndDlg, IDC_SELECT_MOUNT_PATH_REPLACE), SW_HIDE);
            }
            return TRUE;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDOK:
                    if (Context == NULL)
                    {
                        EndDialog(hwndDlg, IDCANCEL);
                        return TRUE;
                    }

                    Selection = (INT)SendDlgItemMessageW(hwndDlg,
                                                         IDC_SELECT_MOUNT_PATH_LIST,
                                                         LB_GETCURSEL,
                                                         0,
                                                         0);
                    if (Selection == LB_ERR)
                    {
                        MessageBoxW(hwndDlg,
                                    L"Select a folder mount path first.",
                                    L"Disk Management",
                                    MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }

                    Context->SelectedIndex = (ULONG)Selection;
                    Context->ReplaceExisting =
                        Context->AllowReplace &&
                        (IsDlgButtonChecked(hwndDlg, IDC_SELECT_MOUNT_PATH_REPLACE) == BST_CHECKED);
                    EndDialog(hwndDlg, IDOK);
                    return TRUE;

                case IDCANCEL:
                    EndDialog(hwndDlg, IDCANCEL);
                    return TRUE;

                default:
                    break;
            }
            break;

        default:
            break;
    }

    return FALSE;
}

static BOOL
DmActionChangeMountPath(
    _In_ const DM_ACTION_CONTEXT *Context)
{
    WCHAR NewPath[MAX_PATH];
    DM_SELECT_MOUNT_PATH_DIALOG Dialog;
    WCHAR Prompt[512];
    WCHAR VolumeText[128];
    ULONG ExistingPathCount;
    WCHAR ExistingPaths[DM_MAX_MOUNT_PATHS][MAX_PATH];
    PCWSTR ReplacedPath;

    if (Context == NULL || Context->Volume == NULL)
        return FALSE;

    if (!DmActionEnsureSimpleBasicVolume(Context, L"Change Mount Path"))
        return FALSE;

    if (Context->Volume->IsBoot || Context->Volume->IsSystem)
    {
        MessageBoxW(Context->hWnd,
                    L"The current boot or system volume cannot have its folder mount path changed here.",
                    L"Disk Management",
                    MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    if (Context->Disk != NULL && (Context->Disk->IsOffline || Context->Disk->IsReadOnly))
    {
        MessageBoxW(Context->hWnd,
                    L"The selected disk must be online and writable before changing a mount path.",
                    L"Disk Management",
                    MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    if (!DmActionBrowseForMountPath(Context->hWnd, NewPath, ARRAYSIZE(NewPath)))
        return FALSE;

    if (!DmActionCollectFolderMountPoints(Context->Volume,
                                          ExistingPaths,
                                          ARRAYSIZE(ExistingPaths),
                                          &ExistingPathCount))
    {
        ExistingPathCount = 0;
    }

    if (DmActionIsRootPath(NewPath))
    {
        MessageBoxW(Context->hWnd,
                    L"Select an empty folder, not a drive root, for the volume mount path.",
                    L"Disk Management",
                    MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    if (!DmActionIsDirectoryEmpty(NewPath))
    {
        MessageBoxW(Context->hWnd,
                    L"The selected folder must already exist and be empty before it can be used as a mount path.",
                    L"Disk Management",
                    MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    ReplacedPath = NULL;
    ZeroMemory(&Dialog, sizeof(Dialog));
    if (ExistingPathCount != 0)
    {
        ULONG Index;

        for (Index = 0; Index < ExistingPathCount; Index++)
        {
            if (_wcsicmp(ExistingPaths[Index], NewPath) == 0)
            {
                MessageBoxW(Context->hWnd,
                            L"The selected volume is already mounted at that folder path.",
                            L"Disk Management",
                            MB_OK | MB_ICONINFORMATION);
                return FALSE;
            }

            StringCchCopyW(Dialog.Paths[Index], ARRAYSIZE(Dialog.Paths[Index]), ExistingPaths[Index]);
        }

        Dialog.PathCount = ExistingPathCount;
        Dialog.AllowReplace = TRUE;
        Dialog.ReplaceExisting = TRUE;
        Dialog.Caption = L"Change Mount Path";
        Dialog.Prompt = L"Select the existing folder mount path to replace, or clear the checkbox to add the new path without removing an existing one.";

        if (DialogBoxParamW(hInstance,
                            MAKEINTRESOURCEW(IDD_SELECT_MOUNT_PATH),
                            Context->hWnd,
                            DmSelectMountPathDlgProc,
                            (LPARAM)&Dialog) != IDOK)
        {
            return FALSE;
        }

        if (Dialog.ReplaceExisting && Dialog.SelectedIndex < ExistingPathCount)
            ReplacedPath = ExistingPaths[Dialog.SelectedIndex];
    }

    DmVolumeListBuildName(Context->Volume, VolumeText, ARRAYSIZE(VolumeText));
    if (ReplacedPath != NULL)
    {
        StringCchPrintfW(Prompt,
                         ARRAYSIZE(Prompt),
                         L"Change the folder mount path for %s from\r\n%s\r\nto\r\n%s\r\nnow?",
                         (VolumeText[0] != UNICODE_NULL) ? VolumeText : L"the selected volume",
                         ReplacedPath,
                         NewPath);
    }
    else
    {
        StringCchPrintfW(Prompt,
                         ARRAYSIZE(Prompt),
                         L"Assign folder mount path\r\n%s\r\nto %s now?",
                         NewPath,
                         (VolumeText[0] != UNICODE_NULL) ? VolumeText : L"the selected volume");
    }

    if (MessageBoxW(Context->hWnd,
                    Prompt,
                    L"Change Mount Path",
                    MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
    {
        return FALSE;
    }

    if (!SetVolumeMountPointW(NewPath, Context->Volume->VolumeName))
    {
        DmActionShowWin32Error(Context->hWnd,
                               L"Assigning the folder mount path failed.",
                               GetLastError());
        return FALSE;
    }

    if (ReplacedPath != NULL)
    {
        if (!DeleteVolumeMountPointW(ReplacedPath))
        {
            DmActionShowWin32Error(Context->hWnd,
                                   L"The new folder mount path was assigned, but removing the previous folder mount path failed.",
                                   GetLastError());
        }
    }

    return TRUE;
}

static BOOL
DmActionRemoveMountPath(
    _In_ const DM_ACTION_CONTEXT *Context)
{
    DM_SELECT_MOUNT_PATH_DIALOG Dialog;
    WCHAR Prompt[384];
    WCHAR SelectedPath[MAX_PATH];
    WCHAR VolumeText[128];
    ULONG ExistingPathCount;
    WCHAR ExistingPaths[DM_MAX_MOUNT_PATHS][MAX_PATH];

    if (Context == NULL || Context->Volume == NULL)
        return FALSE;

    if (!DmActionEnsureSimpleBasicVolume(Context, L"Remove Mount Path"))
        return FALSE;

    if (!DmActionCollectFolderMountPoints(Context->Volume,
                                          ExistingPaths,
                                          ARRAYSIZE(ExistingPaths),
                                          &ExistingPathCount))
    {
        ExistingPathCount = 0;
    }

    if (ExistingPathCount == 0)
    {
        MessageBoxW(Context->hWnd,
                    L"The selected volume does not have a folder mount path to remove.",
                    L"Disk Management",
                    MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    if (Context->Volume->IsBoot || Context->Volume->IsSystem)
    {
        MessageBoxW(Context->hWnd,
                    L"The current boot or system volume cannot have its folder mount path removed here.",
                    L"Disk Management",
                    MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    if (Context->Disk != NULL && (Context->Disk->IsOffline || Context->Disk->IsReadOnly))
    {
        MessageBoxW(Context->hWnd,
                    L"The selected disk must be online and writable before removing a mount path.",
                    L"Disk Management",
                    MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    StringCchCopyW(SelectedPath, ARRAYSIZE(SelectedPath), ExistingPaths[0]);
    if (ExistingPathCount > 1)
    {
        ULONG Index;

        ZeroMemory(&Dialog, sizeof(Dialog));
        for (Index = 0; Index < ExistingPathCount; Index++)
            StringCchCopyW(Dialog.Paths[Index], ARRAYSIZE(Dialog.Paths[Index]), ExistingPaths[Index]);
        Dialog.PathCount = ExistingPathCount;
        Dialog.AllowReplace = FALSE;
        Dialog.Caption = L"Remove Mount Path";
        Dialog.Prompt = L"Select the folder mount path to remove.";

        if (DialogBoxParamW(hInstance,
                            MAKEINTRESOURCEW(IDD_SELECT_MOUNT_PATH),
                            Context->hWnd,
                            DmSelectMountPathDlgProc,
                            (LPARAM)&Dialog) != IDOK)
        {
            return FALSE;
        }

        StringCchCopyW(SelectedPath,
                       ARRAYSIZE(SelectedPath),
                       ExistingPaths[Dialog.SelectedIndex]);
    }

    DmVolumeListBuildName(Context->Volume, VolumeText, ARRAYSIZE(VolumeText));
    StringCchPrintfW(Prompt,
                     ARRAYSIZE(Prompt),
                     L"Remove folder mount path\r\n%s\r\nfrom %s now?",
                     SelectedPath,
                     (VolumeText[0] != UNICODE_NULL) ? VolumeText : L"the selected volume");
    if (MessageBoxW(Context->hWnd,
                    Prompt,
                    L"Remove Mount Path",
                    MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
    {
        return FALSE;
    }

    if (!DeleteVolumeMountPointW(SelectedPath))
    {
        DmActionShowWin32Error(Context->hWnd,
                               L"Removing the folder mount path failed.",
                               GetLastError());
        return FALSE;
    }

    return TRUE;
}

static BOOL
DmActionRemoveDriveLetter(
    _In_ const DM_ACTION_CONTEXT *Context)
{
    WCHAR VolumeText[128];
    WCHAR Prompt[256];

    if (Context == NULL || Context->Volume == NULL)
        return FALSE;

    if (!DmActionEnsureSimpleBasicVolume(Context, L"Remove Drive Letter"))
        return FALSE;

    if (!Context->Volume->HasDriveLetter)
    {
        MessageBoxW(Context->hWnd,
                    L"The selected volume does not have a drive letter.",
                    L"Disk Management",
                    MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    if (Context->Volume->IsBoot || Context->Volume->IsSystem)
    {
        MessageBoxW(Context->hWnd,
                    L"The drive letter for the selected system or boot volume cannot be removed.",
                    L"Disk Management",
                    MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    DmVolumeListBuildName(Context->Volume, VolumeText, ARRAYSIZE(VolumeText));
    StringCchPrintfW(Prompt,
                     ARRAYSIZE(Prompt),
                     L"Remove drive letter %C: from %s now?",
                     towupper(Context->Volume->DriveLetter),
                     (VolumeText[0] != UNICODE_NULL) ? VolumeText : L"the selected volume");
    if (MessageBoxW(Context->hWnd,
                    Prompt,
                    L"Remove Drive Letter",
                    MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
    {
        return FALSE;
    }

    if (!StorageUtilDeleteDriveLetter(Context->Volume->DriveLetter))
    {
        DmActionShowWin32Error(Context->hWnd,
                               L"Removing the drive letter failed.",
                               GetLastError());
        return FALSE;
    }

    return TRUE;
}

static BOOL
DmActionCreatePartition(
    _In_ const DM_ACTION_CONTEXT *Context)
{
    DM_CREATE_PARTITION_DIALOG Dialog;
    DM_SNAPSHOT CreatedSnapshot;
    PDM_VOLUME CreatedVolume;
    WCHAR Prompt[384];
    WCHAR DriveRoot[4];
    WCHAR Message[512];
    HANDLE Handle;
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    PPARTITION_INFORMATION_EX Entry;
    DWORD BytesReturned;
    DWORD LayoutSize;
    ULONGLONG PartitionLength;
    ULONGLONG RegionStartOffset;
    ULONG Index;
    ULONG ExistingLogicalCount;
    BOOL Created;
    BOOL CreatesVolume;
    BOOL AutoCreateExtendedLogical;
    DM_FORMAT_ERROR FormatError;
    ULONGLONG AlignmentBytes;
    ULONGLONG ExtendedLength;
    ULONGLONG ExtendedStartOffset;
    UCHAR MbrPartitionType;
    PCWSTR CreateTitle;

    if (Context == NULL || Context->Disk == NULL || Context->Region == NULL)
        return FALSE;

    if (!DmActionEnsureBasicDiskLayout(Context, DmActionGetCreateVerb(Context)))
        return FALSE;

    if (Context->Region->Type != DmRegionFree)
        return FALSE;

    if (Context->Disk->PartitionStyle == PARTITION_STYLE_RAW)
    {
        MessageBoxW(Context->hWnd,
                    L"The selected disk must be initialized before creating partitions.",
                    DmActionGetCreateVerb(Context),
                    MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    if (!DmActionPromptForCreateVolume(Context, &Dialog, &PartitionLength))
        return FALSE;

    RegionStartOffset = Context->Region->StartOffset;
    CreatesVolume = (Dialog.PartitionKind != DmCreatePartitionMbrExtended);
    AutoCreateExtendedLogical = (Dialog.PartitionKind == DmCreatePartitionMbrLogical &&
                                 !Context->Region->IsLogical &&
                                 !Context->Disk->HasExtendedPartition);
    AlignmentBytes = DmActionAlignmentBytes(Context);
    ExtendedLength = 0;
    ExtendedStartOffset = Context->Disk->ExtendedPartitionOffset;
    CreateTitle = DmActionGetCreateDialogTitleForKind(&Dialog, Dialog.PartitionKind);
    DmActionBuildCreateConfirmation(Context, &Dialog, PartitionLength, Prompt, ARRAYSIZE(Prompt));
    if (MessageBoxW(Context->hWnd,
                    Prompt,
                    CreateTitle,
                    MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
    {
        return FALSE;
    }

    Handle = CreateFileW(Context->Disk->DeviceName,
                         GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        DmActionShowWin32Error(Context->hWnd,
                               L"Unable to open the selected disk for partition creation.",
                               GetLastError());
        return FALSE;
    }

    if (!DmActionQueryDiskLayout(Handle, &Layout))
    {
        DWORD Error;

        Error = GetLastError();
        CloseHandle(Handle);
        DmActionShowWin32Error(Context->hWnd,
                               L"Unable to query the current disk layout.",
                               Error);
        return FALSE;
    }

    Entry = NULL;
    if (Context->Disk->PartitionStyle == PARTITION_STYLE_MBR)
    {
        Entry = DmActionFindFreePrimaryMbrSlot(Layout);
        switch (Dialog.PartitionKind)
        {
            case DmCreatePartitionMbrPrimary:
                if (Entry == NULL)
                {
                    HeapFree(ProcessHeap, 0, Layout);
                    CloseHandle(Handle);
                    MessageBoxW(Context->hWnd,
                                L"No free primary MBR slot is available for another primary partition.",
                                CreateTitle,
                                MB_OK | MB_ICONINFORMATION);
                    return FALSE;
                }

                MbrPartitionType = DmActionChooseMbrDataPartitionType(Context,
                                                                      &Dialog,
                                                                      Context->Region->StartOffset,
                                                                      PartitionLength);
                DmActionInitializeMbrLayoutEntry(Entry,
                                                 Context->Region->StartOffset,
                                                 PartitionLength,
                                                 (ULONG)(Context->Region->StartOffset / max(Context->Disk->BytesPerSector, 1ULL)),
                                                 MbrPartitionType,
                                                 IsRecognizedPartition(MbrPartitionType));
                break;

            case DmCreatePartitionMbrExtended:
                if (Entry == NULL)
                {
                    HeapFree(ProcessHeap, 0, Layout);
                    CloseHandle(Handle);
                    MessageBoxW(Context->hWnd,
                                L"No free primary MBR slot is available for an extended partition.",
                                CreateTitle,
                                MB_OK | MB_ICONINFORMATION);
                    return FALSE;
                }

                MbrPartitionType = DmActionChooseMbrContainerType(Context->Disk,
                                                                  Context->Region->StartOffset,
                                                                  PartitionLength);
                DmActionInitializeMbrLayoutEntry(Entry,
                                                 Context->Region->StartOffset,
                                                 PartitionLength,
                                                 (ULONG)(Context->Region->StartOffset / max(Context->Disk->BytesPerSector, 1ULL)),
                                                 MbrPartitionType,
                                                 FALSE);
                break;

            case DmCreatePartitionMbrLogical:
                ExistingLogicalCount = DmActionCountMbrLogicalEntries(Layout);
                if (AutoCreateExtendedLogical)
                {
                    if (Entry == NULL)
                    {
                        HeapFree(ProcessHeap, 0, Layout);
                        CloseHandle(Handle);
                        MessageBoxW(Context->hWnd,
                                    L"No free primary MBR slot is available for the extended partition that must host the new logical drive.",
                                    CreateTitle,
                                    MB_OK | MB_ICONINFORMATION);
                        return FALSE;
                    }

                    if (PartitionLength == 0 || Context->Region->Length <= AlignmentBytes)
                    {
                        HeapFree(ProcessHeap, 0, Layout);
                        CloseHandle(Handle);
                        MessageBoxW(Context->hWnd,
                                    L"The selected unallocated region is too small to host a logical drive and its extended partition container.",
                                    CreateTitle,
                                    MB_OK | MB_ICONINFORMATION);
                        return FALSE;
                    }

                    ExtendedStartOffset = Context->Region->StartOffset;
                    ExtendedLength = min(Context->Region->Length,
                                         PartitionLength + AlignmentBytes);
                    MbrPartitionType = DmActionChooseMbrContainerType(Context->Disk,
                                                                      ExtendedStartOffset,
                                                                      ExtendedLength);
                    DmActionInitializeMbrLayoutEntry(Entry,
                                                     ExtendedStartOffset,
                                                     ExtendedLength,
                                                     (ULONG)(ExtendedStartOffset / max(Context->Disk->BytesPerSector, 1ULL)),
                                                     MbrPartitionType,
                                                     FALSE);
                    RegionStartOffset = ExtendedStartOffset + AlignmentBytes;
                }
                else
                {
                    if (Context->Region->IsLogical == FALSE)
                    {
                        HeapFree(ProcessHeap, 0, Layout);
                        CloseHandle(Handle);
                        MessageBoxW(Context->hWnd,
                                    L"Logical drives can only be created inside free space within an extended partition.",
                                    CreateTitle,
                                    MB_OK | MB_ICONINFORMATION);
                        return FALSE;
                    }

                    if (Context->Disk->HasExtendedPartition == FALSE ||
                        Context->Disk->ExtendedPartitionLength == 0)
                    {
                        HeapFree(ProcessHeap, 0, Layout);
                        CloseHandle(Handle);
                        MessageBoxW(Context->hWnd,
                                    L"The selected disk does not have a usable extended partition for logical drives.",
                                    CreateTitle,
                                    MB_OK | MB_ICONINFORMATION);
                        return FALSE;
                    }

                    ExtendedStartOffset = Context->Disk->ExtendedPartitionOffset;
                    RegionStartOffset = Context->Region->StartOffset;
                }

                MbrPartitionType = DmActionChooseMbrDataPartitionType(Context,
                                                                      &Dialog,
                                                                      RegionStartOffset,
                                                                      PartitionLength);
                if (!DmActionRewriteMbrLogicalEntries(&Layout,
                                                     Context,
                                                     ExtendedStartOffset,
                                                     RegionStartOffset,
                                                     PartitionLength,
                                                     MbrPartitionType))
                {
                    DWORD Error;

                    Error = GetLastError();
                    HeapFree(ProcessHeap, 0, Layout);
                    CloseHandle(Handle);
                    DmActionShowWin32Error(Context->hWnd,
                                           ExistingLogicalCount == 0
                                               ? L"Unable to build the MBR layout for the first logical drive."
                                               : L"Unable to rebuild the MBR logical partition table for the new logical drive.",
                                           Error);
                    return FALSE;
                }
                break;

            default:
                HeapFree(ProcessHeap, 0, Layout);
                CloseHandle(Handle);
                MessageBoxW(Context->hWnd,
                            L"The selected MBR partition type is not supported yet.",
                            CreateTitle,
                            MB_OK | MB_ICONERROR);
                return FALSE;
        }
    }
    else if (Context->Disk->PartitionStyle == PARTITION_STYLE_GPT)
    {
        for (Index = 0; Index < Layout->PartitionCount; Index++)
        {
            PPARTITION_INFORMATION_EX Candidate;

            Candidate = &Layout->PartitionEntry[Index];
            if (IsEqualGUID(&Candidate->Gpt.PartitionType, &DmActionUnusedPartitionGuid) ||
                (Candidate->StartingOffset.QuadPart == 0 &&
                 Candidate->PartitionLength.QuadPart == 0))
            {
                Entry = Candidate;
                break;
            }
        }

        if (Entry == NULL)
        {
            if (!DmActionExpandLayout(&Layout, Layout->PartitionCount + 1))
            {
                DWORD Error;

                Error = GetLastError();
                HeapFree(ProcessHeap, 0, Layout);
                CloseHandle(Handle);
                DmActionShowWin32Error(Context->hWnd,
                                       L"Unable to expand the GPT layout buffer.",
                                       Error);
                return FALSE;
            }

            Entry = &Layout->PartitionEntry[Layout->PartitionCount - 1];
        }

        ZeroMemory(Entry, sizeof(*Entry));
        Entry->PartitionStyle = PARTITION_STYLE_GPT;
        Entry->StartingOffset.QuadPart = Context->Region->StartOffset;
        Entry->PartitionLength.QuadPart = PartitionLength;
        Entry->PartitionNumber = Index + 1;
        switch (Dialog.PartitionKind)
        {
            case DmCreatePartitionEfiSystem:
                Entry->Gpt.PartitionType = DmActionGptSystemPartitionGuid;
                break;

            case DmCreatePartitionMsr:
                Entry->Gpt.PartitionType = DmActionGptMsftReservedPartitionGuid;
                break;

            case DmCreatePartitionRecovery:
                Entry->Gpt.PartitionType = DmActionGptRecoveryPartitionGuid;
                break;

            case DmCreatePartitionBasic:
            default:
                Entry->Gpt.PartitionType = DmActionGptBasicDataPartitionGuid;
                break;
        }
        DmActionCreateGuid(&Entry->Gpt.PartitionId);
        Entry->Gpt.Attributes = 0;
        Entry->RewritePartition = TRUE;
    }
    else
    {
        HeapFree(ProcessHeap, 0, Layout);
        CloseHandle(Handle);
        MessageBoxW(Context->hWnd,
                    L"The selected disk style is not supported for partition creation.",
                    CreateTitle,
                    MB_OK | MB_ICONERROR);
        return FALSE;
    }

    LayoutSize = sizeof(DRIVE_LAYOUT_INFORMATION_EX) +
                 ((Layout->PartitionCount - ANYSIZE_ARRAY) * sizeof(PARTITION_INFORMATION_EX));
    BytesReturned = 0;
    if (!DeviceIoControl(Handle,
                         IOCTL_DISK_SET_DRIVE_LAYOUT_EX,
                         Layout,
                         LayoutSize,
                         Layout,
                         LayoutSize,
                         &BytesReturned,
                         NULL))
    {
        DWORD Error;

        Error = GetLastError();
        HeapFree(ProcessHeap, 0, Layout);
        CloseHandle(Handle);
        DmActionShowWin32Error(Context->hWnd,
                               L"Creating the selected partition failed.",
                               Error);
        return FALSE;
    }

    DeviceIoControl(Handle,
                    IOCTL_DISK_UPDATE_PROPERTIES,
                    NULL,
                    0,
                    NULL,
                    0,
                    &BytesReturned,
                    NULL);
    HeapFree(ProcessHeap, 0, Layout);
    CloseHandle(Handle);

    Created = TRUE;
    if (!CreatesVolume || (!Dialog.AssignLetter && !Dialog.FormatVolume))
        return TRUE;

    CreatedVolume = NULL;
    if (!DmActionWaitForCreatedVolume(Context->Disk->DiskNumber,
                                      RegionStartOffset,
                                      PartitionLength,
                                      &CreatedSnapshot,
                                      &CreatedVolume))
    {
        DmActionShowCreateVolumeWarning(Context->hWnd,
                                        CreateTitle,
                                        L"The volume was created, but Disk Management could not find it in time to apply the requested drive letter or format settings.");
        DmSnapshotClear(&CreatedSnapshot);
        return Created;
    }

    if (Dialog.AssignLetter)
    {
        if (!StorageUtilAssignDriveLetter(CreatedVolume->DeviceName, Dialog.SelectedLetter))
        {
            DmActionFormatErrorMessage(Message,
                                       ARRAYSIZE(Message),
                                       L"The volume was created, but assigning the requested drive letter failed.",
                                       GetLastError());
            MessageBoxW(Context->hWnd,
                        Message,
                        CreateTitle,
                        MB_OK | MB_ICONWARNING);
            DmSnapshotClear(&CreatedSnapshot);
            return Created;
        }
    }

    if (Dialog.FormatVolume)
    {
        DriveRoot[0] = towupper(Dialog.SelectedLetter);
        DriveRoot[1] = L':';
        DriveRoot[2] = L'\\';
        DriveRoot[3] = UNICODE_NULL;

        if (!DmActionWaitForDriveRoot(DriveRoot))
        {
            DmActionShowCreateVolumeWarning(Context->hWnd,
                                            CreateTitle,
                                            L"The volume was created, but the new drive letter did not become ready in time for formatting.");
            DmSnapshotClear(&CreatedSnapshot);
            return Created;
        }

        if (!DmActionRunFormat(Context,
                               DriveRoot,
                               Dialog.SelectedFileSystem,
                               Dialog.Label,
                               Dialog.QuickFormat,
                               FALSE,
                               &FormatError))
        {
            StringCchPrintfW(Message,
                             ARRAYSIZE(Message),
                             L"The volume was created and drive letter %C: was assigned, but formatting it failed.\r\n\r\n%s",
                             towupper(Dialog.SelectedLetter),
                             DmActionGetFormatFailureText(FormatError));
            MessageBoxW(Context->hWnd,
                        Message,
                        CreateTitle,
                        MB_OK | MB_ICONWARNING);
            DmSnapshotClear(&CreatedSnapshot);
            return Created;
        }
    }

    DmSnapshotClear(&CreatedSnapshot);
    return Created;
}

static BOOL
DmActionSetDiskAttributes(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_z_ PCWSTR Title,
    _In_z_ PCWSTR Prompt,
    _In_z_ PCWSTR ErrorPrefix,
    _In_ ULONGLONG Attributes,
    _In_ ULONGLONG AttributesMask)
{
    HANDLE Handle;
    SET_DISK_ATTRIBUTES SetAttributes;
    DWORD BytesReturned;

    if (Context == NULL || Context->Disk == NULL)
        return FALSE;

    if (MessageBoxW(Context->hWnd,
                    Prompt,
                    Title,
                    MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
    {
        return FALSE;
    }

    Handle = CreateFileW(Context->Disk->DeviceName,
                         GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        DmActionShowWin32Error(Context->hWnd,
                               ErrorPrefix,
                               GetLastError());
        return FALSE;
    }

    ZeroMemory(&SetAttributes, sizeof(SetAttributes));
    SetAttributes.Version = sizeof(SetAttributes);
    SetAttributes.Persist = TRUE;
    SetAttributes.Attributes = Attributes;
    SetAttributes.AttributesMask = AttributesMask;
    BytesReturned = 0;

    if (!DeviceIoControl(Handle,
                         IOCTL_DISK_SET_DISK_ATTRIBUTES,
                         &SetAttributes,
                         sizeof(SetAttributes),
                         NULL,
                         0,
                         &BytesReturned,
                         NULL))
    {
        DWORD Error;

        Error = GetLastError();
        CloseHandle(Handle);
        DmActionShowWin32Error(Context->hWnd, ErrorPrefix, Error);
        return FALSE;
    }

    DeviceIoControl(Handle,
                    IOCTL_DISK_UPDATE_PROPERTIES,
                    NULL,
                    0,
                    NULL,
                    0,
                    &BytesReturned,
                    NULL);
    CloseHandle(Handle);
    return TRUE;
}

static BOOL
DmActionBringDiskOnline(
    _In_ const DM_ACTION_CONTEXT *Context)
{
    WCHAR Prompt[256];

    if (Context == NULL || Context->Disk == NULL)
        return FALSE;

    if (!Context->Disk->IsOffline)
    {
        MessageBoxW(Context->hWnd,
                    L"The selected disk is already online.",
                    L"Disk Management",
                    MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    StringCchPrintfW(Prompt,
                     ARRAYSIZE(Prompt),
                     L"Bring Disk %lu online now?",
                     Context->Disk->DiskNumber);
    return DmActionSetDiskAttributes(Context,
                                     L"Online Disk",
                                     Prompt,
                                     L"Bringing the selected disk online failed.",
                                     0,
                                     DISK_ATTRIBUTE_OFFLINE);
}

static BOOL
DmActionTakeDiskOffline(
    _In_ const DM_ACTION_CONTEXT *Context)
{
    WCHAR Prompt[320];

    if (Context == NULL || Context->Disk == NULL)
        return FALSE;

    if (Context->Disk->IsBoot || Context->Disk->IsSystem)
    {
        MessageBoxW(Context->hWnd,
                    L"The selected boot or system disk cannot be taken offline.",
                    L"Disk Management",
                    MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    if (Context->Disk->IsOffline)
    {
        MessageBoxW(Context->hWnd,
                    L"The selected disk is already offline.",
                    L"Disk Management",
                    MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    StringCchPrintfW(Prompt,
                     ARRAYSIZE(Prompt),
                     L"Take Disk %lu offline now?\r\n\r\n"
                     L"Volumes on the disk will become unavailable until the disk is brought online again.",
                     Context->Disk->DiskNumber);
    return DmActionSetDiskAttributes(Context,
                                     L"Offline Disk",
                                     Prompt,
                                     L"Taking the selected disk offline failed.",
                                     DISK_ATTRIBUTE_OFFLINE,
                                     DISK_ATTRIBUTE_OFFLINE);
}

static BOOL
DmActionSetDiskReadOnly(
    _In_ const DM_ACTION_CONTEXT *Context)
{
    WCHAR Prompt[256];

    if (Context == NULL || Context->Disk == NULL)
        return FALSE;

    if (Context->Disk->IsBoot || Context->Disk->IsSystem)
    {
        MessageBoxW(Context->hWnd,
                    L"The selected boot or system disk cannot be forced read-only.",
                    L"Disk Management",
                    MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    if (Context->Disk->IsReadOnly)
    {
        MessageBoxW(Context->hWnd,
                    L"The selected disk is already read-only.",
                    L"Disk Management",
                    MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    StringCchPrintfW(Prompt,
                     ARRAYSIZE(Prompt),
                     L"Set Disk %lu read-only now?",
                     Context->Disk->DiskNumber);
    return DmActionSetDiskAttributes(Context,
                                     L"Set Read-only",
                                     Prompt,
                                     L"Setting the selected disk read-only failed.",
                                     DISK_ATTRIBUTE_READ_ONLY,
                                     DISK_ATTRIBUTE_READ_ONLY);
}

static BOOL
DmActionClearDiskReadOnly(
    _In_ const DM_ACTION_CONTEXT *Context)
{
    WCHAR Prompt[256];

    if (Context == NULL || Context->Disk == NULL)
        return FALSE;

    if (!Context->Disk->IsReadOnly)
    {
        MessageBoxW(Context->hWnd,
                    L"The selected disk is not read-only.",
                    L"Disk Management",
                    MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    StringCchPrintfW(Prompt,
                     ARRAYSIZE(Prompt),
                     L"Clear the read-only flag on Disk %lu now?",
                     Context->Disk->DiskNumber);
    return DmActionSetDiskAttributes(Context,
                                     L"Clear Read-only",
                                     Prompt,
                                     L"Clearing the selected disk read-only flag failed.",
                                     0,
                                     DISK_ATTRIBUTE_READ_ONLY);
}

static BOOL
DmActionSetPartitionActive(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ BOOL Active)
{
    WCHAR Prompt[384];
    HANDLE Handle;
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    PPARTITION_INFORMATION_EX Entry;
    DWORD BytesReturned;
    DWORD LayoutSize;
    ULONG Index;

    if (Context == NULL || Context->Disk == NULL || Context->Region == NULL)
        return FALSE;

    if (!DmActionEnsureBasicDiskLayout(Context, Active ? L"Mark Partition Active" : L"Mark Partition Inactive"))
        return FALSE;

    if (Context->Region->Type != DmRegionPartition ||
        Context->Region->PartitionStyle != PARTITION_STYLE_MBR ||
        Context->Region->IsLogical ||
        Context->Region->IsContainer)
    {
        MessageBoxW(Context->hWnd,
                    L"The selected partition does not support active/inactive changes.",
                    L"Disk Management",
                    MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    if (Context->Disk->IsOffline || Context->Disk->IsReadOnly)
    {
        MessageBoxW(Context->hWnd,
                    L"The selected disk must be online and writable before changing the active flag.",
                    L"Disk Management",
                    MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    if (Context->Region->IsBoot || Context->Region->IsSystem)
    {
        MessageBoxW(Context->hWnd,
                    L"The active flag for the current boot or system partition cannot be changed here.",
                    L"Disk Management",
                    MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    if ((Context->Region->Data.Mbr.BootIndicator != FALSE) == Active)
    {
        MessageBoxW(Context->hWnd,
                    Active ? L"The selected partition is already active."
                           : L"The selected partition is already inactive.",
                    L"Disk Management",
                    MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    StringCchPrintfW(Prompt,
                     ARRAYSIZE(Prompt),
                     Active
                         ? L"Mark Disk %lu partition %lu as active now?\r\n\r\nThis is only supported for MBR primary partitions."
                         : L"Clear the active flag on Disk %lu partition %lu now?",
                     Context->Disk->DiskNumber,
                     Context->Region->PartitionNumber);
    if (MessageBoxW(Context->hWnd,
                    Prompt,
                    Active ? L"Mark Partition Active" : L"Mark Partition Inactive",
                    MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
    {
        return FALSE;
    }

    Handle = CreateFileW(Context->Disk->DeviceName,
                         GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        DmActionShowWin32Error(Context->hWnd,
                               L"Unable to open the selected disk for active flag changes.",
                               GetLastError());
        return FALSE;
    }

    if (!DmActionQueryDiskLayout(Handle, &Layout))
    {
        DWORD Error;

        Error = GetLastError();
        CloseHandle(Handle);
        DmActionShowWin32Error(Context->hWnd,
                               L"Unable to query the current disk layout.",
                               Error);
        return FALSE;
    }

    Entry = DmActionFindLayoutPartition(Layout, Context->Region);
    if (Entry == NULL || Layout->PartitionStyle != PARTITION_STYLE_MBR)
    {
        HeapFree(ProcessHeap, 0, Layout);
        CloseHandle(Handle);
        MessageBoxW(Context->hWnd,
                    L"The selected partition could not be matched to an MBR layout entry.",
                    L"Disk Management",
                    MB_OK | MB_ICONERROR);
        return FALSE;
    }

    if (Active)
    {
        for (Index = 0; Index < Layout->PartitionCount; Index++)
        {
            PPARTITION_INFORMATION_EX Candidate;

            Candidate = &Layout->PartitionEntry[Index];
            if (Candidate == Entry || Candidate->PartitionStyle != PARTITION_STYLE_MBR)
                continue;

            if (Candidate->Mbr.BootIndicator != FALSE)
            {
                Candidate->Mbr.BootIndicator = FALSE;
                Candidate->RewritePartition = TRUE;
            }
        }
    }

    Entry->Mbr.BootIndicator = Active ? TRUE : FALSE;
    Entry->RewritePartition = TRUE;

    LayoutSize = sizeof(DRIVE_LAYOUT_INFORMATION_EX) +
                 ((Layout->PartitionCount - ANYSIZE_ARRAY) * sizeof(PARTITION_INFORMATION_EX));
    BytesReturned = 0;
    if (!DeviceIoControl(Handle,
                         IOCTL_DISK_SET_DRIVE_LAYOUT_EX,
                         Layout,
                         LayoutSize,
                         Layout,
                         LayoutSize,
                         &BytesReturned,
                         NULL))
    {
        DWORD Error;

        Error = GetLastError();
        HeapFree(ProcessHeap, 0, Layout);
        CloseHandle(Handle);
        DmActionShowWin32Error(Context->hWnd,
                               L"Updating the partition active flag failed.",
                               Error);
        return FALSE;
    }

    DeviceIoControl(Handle,
                    IOCTL_DISK_UPDATE_PROPERTIES,
                    NULL,
                    0,
                    NULL,
                    0,
                    &BytesReturned,
                    NULL);
    HeapFree(ProcessHeap, 0, Layout);
    CloseHandle(Handle);
    return TRUE;
}

static VOID
DmActionBuildDeleteTargetText(
    _In_ const DM_ACTION_CONTEXT *Context,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (Buffer == NULL || cchBuffer == 0)
        return;

    if (Context == NULL)
    {
        Buffer[0] = UNICODE_NULL;
        return;
    }

    if (Context->Volume != NULL)
    {
        DmVolumeListBuildName(Context->Volume, Buffer, cchBuffer);
        return;
    }

    if (Context->Region != NULL && Context->Disk != NULL)
    {
        StringCchPrintfW(Buffer,
                         cchBuffer,
                         L"Disk %lu partition %lu",
                         Context->Disk->DiskNumber,
                         Context->Region->PartitionNumber);
        return;
    }

    StringCchCopyW(Buffer, cchBuffer, L"the selected partition");
}

static PCWSTR
DmActionGetDeleteVerb(
    _In_opt_ const DM_ACTION_CONTEXT *Context)
{
    if (Context != NULL && Context->Volume == NULL && Context->Region != NULL)
        return L"Delete Partition";

    return L"Delete Volume";
}

static BOOL
DmActionDeleteVolume(
    _In_ const DM_ACTION_CONTEXT *Context)
{
    WCHAR TargetText[128];
    WCHAR Prompt[384];
    PCWSTR Verb;
    HANDLE Handle;
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    PPARTITION_INFORMATION_EX Entry;
    DWORD BytesReturned;
    DWORD LayoutSize;

    if (Context == NULL || Context->Disk == NULL || Context->Region == NULL)
        return FALSE;

    Verb = DmActionGetDeleteVerb(Context);

    if (!DmActionEnsureBasicDiskLayout(Context, Verb))
        return FALSE;

    if (Context->Region->Type != DmRegionPartition)
        return FALSE;

    if (Context->Volume != NULL &&
        !DmActionEnsureSingleExtentVolume(Context, Verb))
    {
        return FALSE;
    }

    if (Context->Region->IsBoot || Context->Region->IsSystem ||
        (Context->Volume != NULL && (Context->Volume->IsBoot || Context->Volume->IsSystem)))
    {
        MessageBoxW(Context->hWnd,
                    L"The selected system or boot partition cannot be deleted.",
                    L"Disk Management",
                    MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    if (Context->Region->PartitionStyle == PARTITION_STYLE_MBR &&
        (Context->Region->IsLogical || Context->Region->IsContainer))
    {
        MessageBoxW(Context->hWnd,
                    L"Deleting logical or extended MBR partitions is not implemented yet.",
                    L"Disk Management",
                    MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    DmActionBuildDeleteTargetText(Context, TargetText, ARRAYSIZE(TargetText));
    StringCchPrintfW(Prompt,
                     ARRAYSIZE(Prompt),
                     L"Delete %s now?\r\n\r\nThis operation removes the partition entry and cannot be undone.",
                     TargetText);
    if (MessageBoxW(Context->hWnd,
                    Prompt,
                    Verb,
                    MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
    {
        return FALSE;
    }

    if (!DmActionDismountVolume(Context->hWnd, Context->Volume))
        return FALSE;

    Handle = CreateFileW(Context->Disk->DeviceName,
                         GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        DmActionShowWin32Error(Context->hWnd,
                               L"Unable to open the selected disk for deletion.",
                               GetLastError());
        return FALSE;
    }

    if (!DmActionQueryDiskLayout(Handle, &Layout))
    {
        DWORD Error;

        Error = GetLastError();
        CloseHandle(Handle);
        DmActionShowWin32Error(Context->hWnd,
                               L"Unable to query the current disk layout.",
                               Error);
        return FALSE;
    }

    Entry = DmActionFindLayoutPartition(Layout, Context->Region);
    if (Entry == NULL)
    {
        HeapFree(ProcessHeap, 0, Layout);
        CloseHandle(Handle);
        MessageBoxW(Context->hWnd,
                    L"The selected partition could not be matched to the current disk layout.",
                    L"Disk Management",
                    MB_OK | MB_ICONERROR);
        return FALSE;
    }

    if (Context->Region->PartitionStyle == PARTITION_STYLE_MBR)
    {
        Entry->PartitionStyle = PARTITION_STYLE_MBR;
        Entry->StartingOffset.QuadPart = 0;
        Entry->PartitionLength.QuadPart = 0;
        Entry->PartitionNumber = 0;
        Entry->Mbr.PartitionType = PARTITION_ENTRY_UNUSED;
        Entry->Mbr.BootIndicator = FALSE;
        Entry->Mbr.RecognizedPartition = FALSE;
        Entry->Mbr.HiddenSectors = 0;
        Entry->RewritePartition = TRUE;
    }
    else if (Context->Region->PartitionStyle == PARTITION_STYLE_GPT)
    {
        ZeroMemory(Entry, sizeof(*Entry));
        Entry->RewritePartition = TRUE;
    }
    else
    {
        HeapFree(ProcessHeap, 0, Layout);
        CloseHandle(Handle);
        MessageBoxW(Context->hWnd,
                    L"The selected partition style is not supported for deletion.",
                    L"Disk Management",
                    MB_OK | MB_ICONERROR);
        return FALSE;
    }

    LayoutSize = sizeof(DRIVE_LAYOUT_INFORMATION_EX) +
                 ((Layout->PartitionCount - ANYSIZE_ARRAY) * sizeof(PARTITION_INFORMATION_EX));
    BytesReturned = 0;
    if (!DeviceIoControl(Handle,
                         IOCTL_DISK_SET_DRIVE_LAYOUT_EX,
                         Layout,
                         LayoutSize,
                         Layout,
                         LayoutSize,
                         &BytesReturned,
                         NULL))
    {
        DWORD Error;

        Error = GetLastError();
        HeapFree(ProcessHeap, 0, Layout);
        CloseHandle(Handle);
        DmActionShowWin32Error(Context->hWnd,
                               L"Deleting the selected partition failed.",
                               Error);
        return FALSE;
    }

    DeviceIoControl(Handle,
                    IOCTL_DISK_UPDATE_PROPERTIES,
                    NULL,
                    0,
                    NULL,
                    0,
                    &BytesReturned,
                    NULL);
    HeapFree(ProcessHeap, 0, Layout);
    CloseHandle(Handle);
    return TRUE;
}

static BOOL
DmActionInitializeDisk(
    _In_ const DM_ACTION_CONTEXT *Context)
{
    WCHAR Prompt[256];
    CREATE_DISK DiskInfo;
    DWORD BytesReturned;
    DWORD Choice;
    HANDLE Handle;

    if (Context == NULL || Context->Disk == NULL)
        return FALSE;

    if (Context->Disk->PartitionStyle != PARTITION_STYLE_RAW)
    {
        MessageBoxW(Context->hWnd,
                    L"The selected disk is already initialized.",
                    L"Disk Management",
                    MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    StringCchPrintfW(Prompt,
                     ARRAYSIZE(Prompt),
                     L"Initialize Disk %lu now?\r\n\r\n"
                     L"Yes = GPT\r\n"
                     L"No = MBR\r\n"
                     L"Cancel = abort",
                     Context->Disk->DiskNumber);
    Choice = MessageBoxW(Context->hWnd,
                         Prompt,
                         L"Initialize Disk",
                         MB_YESNOCANCEL | MB_ICONQUESTION);
    if (Choice == IDCANCEL)
        return FALSE;

    ZeroMemory(&DiskInfo, sizeof(DiskInfo));
    if (Choice == IDYES)
    {
        DiskInfo.PartitionStyle = PARTITION_STYLE_GPT;
        DmActionCreateGuid(&DiskInfo.Gpt.DiskId);
        DiskInfo.Gpt.MaxPartitionCount = 128;
    }
    else
    {
        DiskInfo.PartitionStyle = PARTITION_STYLE_MBR;
        DmActionCreateSignature(&DiskInfo.Mbr.Signature);
    }

    Handle = CreateFileW(Context->Disk->DeviceName,
                         GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        DmActionShowWin32Error(Context->hWnd,
                               L"Unable to open the selected disk for initialization.",
                               GetLastError());
        return FALSE;
    }

    BytesReturned = 0;
    if (!DeviceIoControl(Handle,
                         IOCTL_DISK_CREATE_DISK,
                         &DiskInfo,
                         sizeof(DiskInfo),
                         NULL,
                         0,
                         &BytesReturned,
                         NULL))
    {
        DWORD Error;

        Error = GetLastError();
        CloseHandle(Handle);
        DmActionShowWin32Error(Context->hWnd,
                               L"Disk initialization failed.",
                               Error);
        return FALSE;
    }

    DeviceIoControl(Handle,
                    IOCTL_DISK_UPDATE_PROPERTIES,
                    NULL,
                    0,
                    NULL,
                    0,
                    &BytesReturned,
                    NULL);
    CloseHandle(Handle);
    return TRUE;
}

static BOOL
DmActionCanConvertDiskStyle(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ PARTITION_STYLE TargetStyle)
{
    if (Context == NULL || Context->Disk == NULL)
        return FALSE;

    if (Context->Disk->PartitionStyle == TargetStyle)
        return FALSE;

    if (Context->Disk->PartitionStyle != PARTITION_STYLE_RAW &&
        Context->Disk->PartitionCount != 0)
    {
        return FALSE;
    }

    if (Context->Disk->IsDynamic ||
        Context->Disk->IsOffline ||
        Context->Disk->IsReadOnly ||
        Context->Disk->IsBoot ||
        Context->Disk->IsSystem)
    {
        return FALSE;
    }

    return TRUE;
}

static BOOL
DmActionConvertDiskStyle(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ PARTITION_STYLE TargetStyle)
{
    WCHAR Prompt[320];
    CREATE_DISK DiskInfo;
    DWORD BytesReturned;
    HANDLE Handle;
    PCWSTR Title;
    PCWSTR ErrorPrefix;
    BOOL IsRaw;

    if (Context == NULL || Context->Disk == NULL)
        return FALSE;

    if (!DmActionCanConvertDiskStyle(Context, TargetStyle))
    {
        MessageBoxW(Context->hWnd,
                    L"Only empty online writable basic disks can be converted here.",
                    L"Disk Management",
                    MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    IsRaw = (Context->Disk->PartitionStyle == PARTITION_STYLE_RAW);
    if (IsRaw)
    {
        Title = (TargetStyle == PARTITION_STYLE_GPT) ? L"Initialize as GPT Disk" : L"Initialize as MBR Disk";
        ErrorPrefix = (TargetStyle == PARTITION_STYLE_GPT)
                        ? L"Initializing the selected RAW disk as GPT failed."
                        : L"Initializing the selected RAW disk as MBR failed.";
        StringCchPrintfW(Prompt,
                         ARRAYSIZE(Prompt),
                         (TargetStyle == PARTITION_STYLE_GPT)
                            ? L"Initialize RAW Disk %lu as GPT now?"
                            : L"Initialize RAW Disk %lu as MBR now?",
                         Context->Disk->DiskNumber);
    }
    else
    {
        Title = (TargetStyle == PARTITION_STYLE_GPT) ? L"Convert to GPT Disk" : L"Convert to MBR Disk";
        ErrorPrefix = (TargetStyle == PARTITION_STYLE_GPT)
                        ? L"Converting the selected disk to GPT failed."
                        : L"Converting the selected disk to MBR failed.";

        StringCchPrintfW(Prompt,
                         ARRAYSIZE(Prompt),
                         (TargetStyle == PARTITION_STYLE_GPT)
                            ? L"Convert Disk %lu to GPT now?\r\n\r\nThe disk must stay empty. Existing partitioning metadata will be replaced."
                            : L"Convert Disk %lu to MBR now?\r\n\r\nThe disk must stay empty. Existing partitioning metadata will be replaced.",
                         Context->Disk->DiskNumber);
    }
    if (MessageBoxW(Context->hWnd,
                    Prompt,
                    Title,
                    MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
    {
        return FALSE;
    }

    ZeroMemory(&DiskInfo, sizeof(DiskInfo));
    DiskInfo.PartitionStyle = TargetStyle;
    if (TargetStyle == PARTITION_STYLE_GPT)
    {
        DmActionCreateGuid(&DiskInfo.Gpt.DiskId);
        DiskInfo.Gpt.MaxPartitionCount = 128;
    }
    else
    {
        DmActionCreateSignature(&DiskInfo.Mbr.Signature);
    }

    Handle = CreateFileW(Context->Disk->DeviceName,
                         GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        DmActionShowWin32Error(Context->hWnd,
                               ErrorPrefix,
                               GetLastError());
        return FALSE;
    }

    BytesReturned = 0;
    if (!DeviceIoControl(Handle,
                         IOCTL_DISK_CREATE_DISK,
                         &DiskInfo,
                         sizeof(DiskInfo),
                         NULL,
                         0,
                         &BytesReturned,
                         NULL))
    {
        DWORD Error;

        Error = GetLastError();
        CloseHandle(Handle);
        DmActionShowWin32Error(Context->hWnd,
                               ErrorPrefix,
                               Error);
        return FALSE;
    }

    DeviceIoControl(Handle,
                    IOCTL_DISK_UPDATE_PROPERTIES,
                    NULL,
                    0,
                    NULL,
                    0,
                    &BytesReturned,
                    NULL);
    CloseHandle(Handle);
    return TRUE;
}

static const DM_ACTION_ENTRY *
DmFindActionEntry(
    _In_ UINT CommandId)
{
    UINT Index;

    for (Index = 0; Index < ARRAYSIZE(DmActionTable); ++Index)
    {
        if (DmActionTable[Index].CommandId == CommandId)
            return &DmActionTable[Index];
    }

    return NULL;
}

const DM_ACTION_DESCRIPTOR *
DmActionGetDescriptor(
    _In_ UINT CommandId)
{
    static DM_ACTION_DESCRIPTOR Descriptor;
    const DM_ACTION_ENTRY *Entry;

    Entry = DmFindActionEntry(CommandId);
    if (Entry == NULL)
        return NULL;

    Descriptor.CommandId = Entry->CommandId;
    Descriptor.VerbStringId = Entry->VerbStringId;
    Descriptor.StatusStringId = Entry->StatusStringId;
    return &Descriptor;
}

UINT
DmActionGetVerbStringId(
    _In_ UINT CommandId)
{
    const DM_ACTION_DESCRIPTOR *Descriptor;

    Descriptor = DmActionGetDescriptor(CommandId);
    return (Descriptor != NULL) ? Descriptor->VerbStringId : 0;
}

UINT
DmActionGetStatusStringId(
    _In_ UINT CommandId)
{
    const DM_ACTION_DESCRIPTOR *Descriptor;

    Descriptor = DmActionGetDescriptor(CommandId);
    return (Descriptor != NULL) ? Descriptor->StatusStringId : 0;
}

BOOL
DmActionIsAvailable(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ UINT CommandId)
{
    const DM_ACTION_ENTRY *Entry;

    Entry = DmFindActionEntry(CommandId);
    if (Entry == NULL)
        return FALSE;

    switch (CommandId)
    {
        case IDM_ACTION_INITIALIZE_DISK:
            return (Context != NULL &&
                    Context->Disk != NULL &&
                    Context->Disk->PartitionStyle == PARTITION_STYLE_RAW &&
                    !Context->Disk->IsOffline &&
                    !Context->Disk->IsReadOnly);

        case IDM_ACTION_CONVERT_GPT:
            return DmActionCanConvertDiskStyle(Context, PARTITION_STYLE_GPT);

        case IDM_ACTION_CONVERT_MBR:
            return DmActionCanConvertDiskStyle(Context, PARTITION_STYLE_MBR);

        case IDM_ACTION_CREATE_PARTITION:
            return (Context != NULL &&
                    Context->Disk != NULL &&
                    Context->Region != NULL &&
                    Context->Region->Type == DmRegionFree &&
                    Context->Disk->PartitionStyle != PARTITION_STYLE_RAW &&
                    !Context->Disk->IsDynamic &&
                    !Context->Disk->IsOffline &&
                    !Context->Disk->IsReadOnly);

        case IDM_ACTION_DELETE_VOLUME:
            return (Context != NULL &&
                    Context->Disk != NULL &&
                    Context->Region != NULL &&
                    Context->Region->Type == DmRegionPartition &&
                    !Context->Disk->IsDynamic &&
                    !Context->Region->IsDynamic &&
                    (Context->Volume == NULL || !Context->Volume->IsDynamic) &&
                    (Context->Volume == NULL || Context->Volume->ExtentCount <= 1) &&
                    !Context->Region->IsBoot &&
                    !Context->Region->IsSystem &&
                    (Context->Volume == NULL ||
                     (!Context->Volume->IsBoot && !Context->Volume->IsSystem)) &&
                    (Context->Region->PartitionStyle != PARTITION_STYLE_MBR ||
                     (!Context->Region->IsLogical && !Context->Region->IsContainer)) &&
                    !Context->Disk->IsOffline &&
                    !Context->Disk->IsReadOnly);

        case IDM_ACTION_EXTEND_VOLUME:
            return DmActionCanResizeRawVolume(Context, TRUE);

        case IDM_ACTION_SHRINK_VOLUME:
            return DmActionCanResizeRawVolume(Context, FALSE);

        case IDM_ACTION_FORMAT:
            return (DmActionCanMutateSimpleBasicVolume(Context) &&
                    Context->Volume->HasDriveLetter &&
                    (DmActionGetVolumeDriveType(Context->Volume) == DRIVE_FIXED ||
                     DmActionGetVolumeDriveType(Context->Volume) == DRIVE_REMOVABLE ||
                     DmActionGetVolumeDriveType(Context->Volume) == DRIVE_RAMDISK) &&
                    !Context->Volume->IsBoot &&
                    !Context->Volume->IsSystem &&
                    (Context->Disk == NULL ||
                     (!Context->Disk->IsOffline && !Context->Disk->IsReadOnly)));

        case IDM_ACTION_CHANGE_MOUNT_PATH:
            return (DmActionCanMutateSimpleBasicVolume(Context) &&
                    !Context->Volume->IsBoot &&
                    !Context->Volume->IsSystem &&
                    (Context->Disk == NULL ||
                     (!Context->Disk->IsOffline && !Context->Disk->IsReadOnly)));

        case IDM_ACTION_REMOVE_MOUNT_PATH:
            return (DmActionCanMutateSimpleBasicVolume(Context) &&
                    Context->Volume->FolderMountPointCount != 0 &&
                    !Context->Volume->IsBoot &&
                    !Context->Volume->IsSystem &&
                    (Context->Disk == NULL ||
                     (!Context->Disk->IsOffline && !Context->Disk->IsReadOnly)));

        case IDM_ACTION_ASSIGN_LETTER:
            return (DmActionCanMutateSimpleBasicVolume(Context) &&
                    (Context->Disk == NULL || !Context->Disk->IsOffline) &&
                    !Context->Volume->IsBoot &&
                    !Context->Volume->IsSystem &&
                    !Context->Volume->HasDriveLetter);

        case IDM_ACTION_REMOVE_LETTER:
            return (DmActionCanMutateSimpleBasicVolume(Context) &&
                    (Context->Disk == NULL || !Context->Disk->IsOffline) &&
                    !Context->Volume->IsBoot &&
                    !Context->Volume->IsSystem &&
                    Context->Volume->HasDriveLetter);

        case IDM_ACTION_ONLINE:
            return (Context != NULL &&
                    Context->Disk != NULL &&
                    Context->Disk->IsOffline);

        case IDM_ACTION_OFFLINE:
            return (Context != NULL &&
                    Context->Disk != NULL &&
                    !Context->Disk->IsOffline &&
                    !Context->Disk->IsBoot &&
                    !Context->Disk->IsSystem);

        case IDM_ACTION_SET_READ_ONLY:
            return (Context != NULL &&
                    Context->Disk != NULL &&
                    !Context->Disk->IsReadOnly &&
                    !Context->Disk->IsBoot &&
                    !Context->Disk->IsSystem);

        case IDM_ACTION_CLEAR_READ_ONLY:
            return (Context != NULL &&
                    Context->Disk != NULL &&
                    Context->Disk->IsReadOnly);

        case IDM_ACTION_MARK_ACTIVE:
            return (Context != NULL &&
                    Context->Disk != NULL &&
                    Context->Region != NULL &&
                    Context->Region->Type == DmRegionPartition &&
                    Context->Region->PartitionStyle == PARTITION_STYLE_MBR &&
                    !Context->Disk->IsDynamic &&
                    !Context->Region->IsDynamic &&
                    !Context->Region->IsLogical &&
                    !Context->Region->IsContainer &&
                    !Context->Region->IsBoot &&
                    !Context->Region->IsSystem &&
                    !Context->Disk->IsOffline &&
                    !Context->Disk->IsReadOnly &&
                    Context->Region->Data.Mbr.BootIndicator == FALSE);

        case IDM_ACTION_MARK_INACTIVE:
            return (Context != NULL &&
                    Context->Disk != NULL &&
                    Context->Region != NULL &&
                    Context->Region->Type == DmRegionPartition &&
                    Context->Region->PartitionStyle == PARTITION_STYLE_MBR &&
                    !Context->Disk->IsDynamic &&
                    !Context->Region->IsDynamic &&
                    !Context->Region->IsLogical &&
                    !Context->Region->IsContainer &&
                    !Context->Region->IsBoot &&
                    !Context->Region->IsSystem &&
                    !Context->Disk->IsOffline &&
                    !Context->Disk->IsReadOnly &&
                    Context->Region->Data.Mbr.BootIndicator != FALSE);

        default:
            break;
    }

    switch (Entry->Requirement)
    {
        case DmActionRequiresNone:
            return TRUE;

        case DmActionRequiresDisk:
            return (Context != NULL && Context->Disk != NULL);

        case DmActionRequiresVolume:
            return (Context != NULL && Context->Volume != NULL);

        case DmActionRequiresSelection:
            return (Context != NULL &&
                    (Context->Disk != NULL ||
                     Context->Volume != NULL ||
                     Context->Region != NULL));

        default:
            return FALSE;
    }
}

BOOL
DmActionMutatesSnapshot(
    _In_ UINT CommandId)
{
    const DM_ACTION_ENTRY *Entry;

    Entry = DmFindActionEntry(CommandId);
    return (Entry != NULL && Entry->Mutating != FALSE);
}

VOID
DmActionBuildVerbText(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ UINT CommandId,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    const DM_ACTION_DESCRIPTOR *Descriptor;
    WCHAR VerbText[128];

    if (Buffer == NULL || cchBuffer == 0)
        return;

    if (CommandId == IDM_ACTION_DELETE_VOLUME)
    {
        StringCchCopyW(Buffer, cchBuffer, DmActionGetDeleteVerb(Context));
        return;
    }

    if (CommandId == IDM_ACTION_CREATE_PARTITION)
    {
        StringCchCopyW(Buffer, cchBuffer, DmActionGetCreateVerb(Context));
        return;
    }

    Descriptor = DmActionGetDescriptor(CommandId);
    if (Descriptor == NULL)
    {
        StringCchCopyW(Buffer, cchBuffer, L"Action");
        return;
    }

    if (LoadStringW(hInstance, Descriptor->VerbStringId, VerbText, ARRAYSIZE(VerbText)) == 0)
        StringCchCopyW(VerbText, ARRAYSIZE(VerbText), L"Action");

    StringCchCopyW(Buffer, cchBuffer, VerbText);
}

VOID
DmActionShowStubMessage(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ UINT CommandId)
{
    WCHAR VerbText[128];
    WCHAR Message[256];
    const DM_ACTION_DESCRIPTOR *Descriptor;

    if (Context == NULL || Context->hWnd == NULL)
        return;

    Descriptor = DmActionGetDescriptor(CommandId);
    if (Descriptor == NULL)
        return;

    DmActionBuildVerbText(Context, CommandId, VerbText, ARRAYSIZE(VerbText));
    StringCchPrintfW(Message,
                     ARRAYSIZE(Message),
                     L"%s is wired as part of the Disk Management skeleton, but the mutating backend is not implemented yet.",
                     VerbText);
    MessageBoxW(Context->hWnd,
                Message,
                L"Disk Management",
                MB_OK | MB_ICONINFORMATION);
}

BOOL
DmActionExecute(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ UINT CommandId)
{
    const DM_ACTION_DESCRIPTOR *Descriptor;
    NTSTATUS Status;

    Descriptor = DmActionGetDescriptor(CommandId);
    if (Descriptor == NULL || Context == NULL)
        return FALSE;

    switch (CommandId)
    {
        case IDM_ACTION_REFRESH:
        case IDM_ACTION_RESCAN:
            if (Context->Snapshot != NULL)
            {
                Status = DmSnapshotRefresh(Context->Snapshot);
                if (!NT_SUCCESS(Status))
                {
                    MessageBoxW(Context->hWnd,
                                L"Unable to refresh the disk snapshot.",
                                L"Disk Management",
                                MB_OK | MB_ICONERROR);
                    return FALSE;
                }
            }
            return TRUE;

        case IDM_ACTION_PROPERTIES:
            if (Context->Volume != NULL)
                return DmShowVolumeProperties(Context->hWnd, Context->Volume);

            if (Context->Disk != NULL)
                return DmShowDiskProperties(Context->hWnd, Context->Disk);

            return FALSE;

        case IDM_ACTION_INITIALIZE_DISK:
            return DmActionInitializeDisk(Context);

        case IDM_ACTION_CONVERT_GPT:
            return DmActionConvertDiskStyle(Context, PARTITION_STYLE_GPT);

        case IDM_ACTION_CONVERT_MBR:
            return DmActionConvertDiskStyle(Context, PARTITION_STYLE_MBR);

        case IDM_ACTION_CREATE_PARTITION:
            return DmActionCreatePartition(Context);

        case IDM_ACTION_DELETE_VOLUME:
            return DmActionDeleteVolume(Context);

        case IDM_ACTION_EXTEND_VOLUME:
            return DmActionExtendVolume(Context);

        case IDM_ACTION_SHRINK_VOLUME:
            return DmActionShrinkVolume(Context);

        case IDM_ACTION_FORMAT:
            return DmActionFormatVolume(Context);

        case IDM_ACTION_ASSIGN_LETTER:
            return DmActionAssignDriveLetter(Context);

        case IDM_ACTION_REMOVE_LETTER:
            return DmActionRemoveDriveLetter(Context);

        case IDM_ACTION_CHANGE_MOUNT_PATH:
            return DmActionChangeMountPath(Context);

        case IDM_ACTION_REMOVE_MOUNT_PATH:
            return DmActionRemoveMountPath(Context);

        case IDM_ACTION_ONLINE:
            return DmActionBringDiskOnline(Context);

        case IDM_ACTION_OFFLINE:
            return DmActionTakeDiskOffline(Context);

        case IDM_ACTION_SET_READ_ONLY:
            return DmActionSetDiskReadOnly(Context);

        case IDM_ACTION_CLEAR_READ_ONLY:
            return DmActionClearDiskReadOnly(Context);

        case IDM_ACTION_MARK_ACTIVE:
            return DmActionSetPartitionActive(Context, TRUE);

        case IDM_ACTION_MARK_INACTIVE:
            return DmActionSetPartitionActive(Context, FALSE);

        case IDM_FILE_EXIT:
        case IDM_HELP_ABOUT:
            return TRUE;

        default:
            return FALSE;
    }
}
