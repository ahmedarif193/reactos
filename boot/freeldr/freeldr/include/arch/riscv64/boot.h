/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     RISC-V firmware-stage dependencies. No NT kernel ABI is defined yet.
 */

#pragma once

#include <ntdef.h>
#include <ntstatus.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <ntstrsafe.h>

typedef ULONG_PTR PFN_NUMBER;
typedef struct _PARTITION_INFORMATION *PPARTITION_INFORMATION;
#define MAX_PATH 260
#include <arc/arc.h>
#define RTL_STATIC_LIST_HEAD(Name) LIST_ENTRY Name = { &Name, &Name }

static inline VOID InitializeListHead(PLIST_ENTRY Head) { Head->Flink = Head->Blink = Head; }
static inline BOOLEAN IsListEmpty(const LIST_ENTRY *Head) { return Head->Flink == Head; }
static inline VOID InsertTailList(PLIST_ENTRY Head, PLIST_ENTRY Entry)
{
    Entry->Flink = Head;
    Entry->Blink = Head->Blink;
    Head->Blink->Flink = Entry;
    Head->Blink = Entry;
}
static inline VOID InsertHeadList(PLIST_ENTRY Head, PLIST_ENTRY Entry)
{
    Entry->Blink = Head;
    Entry->Flink = Head->Flink;
    Head->Flink->Blink = Entry;
    Head->Flink = Entry;
}
static inline BOOLEAN RemoveEntryList(PLIST_ENTRY Entry)
{
    Entry->Blink->Flink = Entry->Flink;
    Entry->Flink->Blink = Entry->Blink;
    return Entry->Flink == Entry->Blink;
}
static inline PLIST_ENTRY RemoveHeadList(PLIST_ENTRY Head)
{
    PLIST_ENTRY Entry = Head->Flink;
    RemoveEntryList(Entry);
    return Entry;
}

#define RtlZeroMemory(Destination, Length) memset(Destination, 0, Length)
#define RtlCopyMemory(Destination, Source, Length) memcpy(Destination, Source, Length)
#define RtlMoveMemory(Destination, Source, Length) memmove(Destination, Source, Length)
#define RtlFillMemory(Destination, Length, Fill) memset(Destination, Fill, Length)

VOID RtlAssert(PVOID Assertion, PVOID File, ULONG Line, PCHAR Message);
#define ASSERT(Condition) do { if (!(Condition)) RtlAssert(#Condition, __FILE__, __LINE__, NULL); } while (0)
#define UNREACHABLE __builtin_unreachable()

_Static_assert(sizeof(ULONG) == 4, "NT ULONG must be 32 bits");
_Static_assert(sizeof(LONG) == 4, "NT LONG must be 32 bits");
_Static_assert(sizeof(ULONG_PTR) == 8, "RISC-V boot pointers must be 64 bits");
_Static_assert(sizeof(WCHAR) == 2, "UEFI characters must be UTF-16");

#include <arcsupp.h>
#include <arcname.h>
#include <ramdisk.h>
#include <disk.h>
#include <fs.h>
#include <inifile.h>
#include <keycodes.h>
#include <mm.h>
#include <machine.h>
#include <oslist.h>
#include <options.h>
#include <custom.h>
#include <settings.h>
#include <ver.h>
#include <include/ntldr/winldr.h>

#define printf TuiPrintf
#include <ui.h>
#include <ui/video.h>

VOID Reboot(VOID);
VOID StallExecutionProcessor(ULONG Microseconds);
ULONG DbgPrint(PCSTR Format, ...);
UCHAR DriveMapGetBiosDriveNumber(PCSTR DeviceName);
