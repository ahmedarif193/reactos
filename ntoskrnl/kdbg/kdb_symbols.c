/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/kdbg/kdb_symbols.c
 * PURPOSE:         Getting symbol information...
 *
 * PROGRAMMERS:     David Welch (welch@cwcom.net)
 *                  Colin Finck (colin@reactos.org)
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#include "kdb.h"

#define NDEBUG
#include "debug.h"

/* GLOBALS ******************************************************************/

typedef struct _IMAGE_SYMBOL_INFO_CACHE
{
    LIST_ENTRY ListEntry;
    ULONG RefCount;
    UNICODE_STRING FileName;
    PROSSYM_INFO RosSymInfo;
}
IMAGE_SYMBOL_INFO_CACHE, *PIMAGE_SYMBOL_INFO_CACHE;

static BOOLEAN LoadSymbols = FALSE;
static LIST_ENTRY SymbolsToLoad;
static KSPIN_LOCK SymbolsToLoadLock;
static KEVENT SymbolsToLoadEvent;

#define KDB_MAX_MODULES             4096
#define KDB_MAX_SYMBOLS_PER_MODULE  (4 * 1024 * 1024)
#define KDB_MAX_SYMBOL_SCAN         (8 * 1024 * 1024)
#define KDB_MAX_SYMBOL_NAME         512

/* FUNCTIONS ****************************************************************/

static
BOOLEAN
KdbpSymSearchModuleList(
    IN PLIST_ENTRY current_entry,
    IN PLIST_ENTRY end_entry,
    IN PLONG Count,
    IN PVOID Address,
    IN INT Index,
    OUT PLDR_DATA_TABLE_ENTRY* pLdrEntry)
{
    ULONG Iterations = 0;

    while (current_entry && current_entry != end_entry)
    {
        LIST_ENTRY Links;
        LDR_DATA_TABLE_ENTRY Entry;
        PLDR_DATA_TABLE_ENTRY LdrEntry;

        /* Bound the walk: the list is read lock-free from the debugger and may be torn */
        if (++Iterations > KDB_MAX_MODULES)
            break;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Links, current_entry, sizeof(Links))))
            break;

        LdrEntry = CONTAINING_RECORD(current_entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Entry, LdrEntry, sizeof(Entry))))
            break;

        if ((Address &&
             (ULONG_PTR)Address >= (ULONG_PTR)Entry.DllBase &&
             (ULONG_PTR)Address - (ULONG_PTR)Entry.DllBase < Entry.SizeOfImage) ||
            (Index >= 0 && (*Count)++ == Index))
        {
            *pLdrEntry = LdrEntry;
            return TRUE;
        }

        if (Links.Flink == current_entry)
            break;
        current_entry = Links.Flink;
    }

    return FALSE;
}

/*! \brief Find a module...
 *
 * \param Address      If \a Address is not NULL the module containing \a Address
 *                     is searched.
 * \param Name         If \a Name is not NULL the module named \a Name will be
 *                     searched.
 * \param Index        If \a Index is >= 0 the Index'th module will be returned.
 * \param pLdrEntry    Pointer to a PLDR_DATA_TABLE_ENTRY which is filled.
 *
 * \retval TRUE    Module was found, \a pLdrEntry was filled.
 * \retval FALSE   No module was found.
 */
BOOLEAN
KdbpSymFindModule(
    IN PVOID Address  OPTIONAL,
    IN INT Index  OPTIONAL,
    OUT PLDR_DATA_TABLE_ENTRY* pLdrEntry)
{
    LONG Count = 0;
    PEPROCESS CurrentProcess;
    PPEB PebAddress;
    PEB Peb;
    PPEB_LDR_DATA LdrAddress;
    PEB_LDR_DATA Ldr;
    PLIST_ENTRY LdrListHead;
    LIST_ENTRY KernelListHead;
    EPROCESS ProcessSnapshot;

    /*
     * First try to look up the module in the kernel module list.
     * NOTE: deliberately lock-free. KDB runs with the machine frozen; taking
     * PsLoadedModuleSpinLock here deadlocks the debugger whenever the lock
     * was held at exception time (common for faults at IRQL >= DISPATCH).
     * The walk is bounded in KdbpSymSearchModuleList to survive a torn list.
     */
    if (NT_SUCCESS(KdbpSafeReadMemory(&KernelListHead, &PsLoadedModuleList, sizeof(KernelListHead))) &&
        KdbpSymSearchModuleList(KernelListHead.Flink, &PsLoadedModuleList, &Count, Address, Index, pLdrEntry))
    {
        return TRUE;
    }

    /* That didn't succeed. Try the module list of the current process now. */
    CurrentProcess = PsGetCurrentProcess();

    if (!CurrentProcess)
        return FALSE;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&ProcessSnapshot, CurrentProcess, sizeof(ProcessSnapshot))))
    {
        return FALSE;
    }

    PebAddress = ProcessSnapshot.Peb;
    if (!PebAddress ||
        !NT_SUCCESS(KdbpSafeReadMemory(&Peb, PebAddress, sizeof(Peb))))
    {
        return FALSE;
    }

    LdrAddress = Peb.Ldr;
    if (!LdrAddress ||
        !NT_SUCCESS(KdbpSafeReadMemory(&Ldr, LdrAddress, sizeof(Ldr))))
    {
        return FALSE;
    }

    LdrListHead = (PLIST_ENTRY)((PUCHAR)LdrAddress + FIELD_OFFSET(PEB_LDR_DATA, InLoadOrderModuleList));

    return KdbpSymSearchModuleList(Ldr.InLoadOrderModuleList.Flink, LdrListHead, &Count, Address, Index, pLdrEntry);
}

static
PCHAR
NTAPI
KdbpSymUnicodeToAnsi(IN PUNICODE_STRING Unicode,
                     OUT PCHAR Ansi,
                     IN ULONG Length)
{
    WCHAR Wide[128];
    PCHAR p;
    PWCHAR pw;
    ULONG i;

    if (Length == 0)
        return Ansi;

    Ansi[0] = ANSI_NULL;
    if (Unicode == NULL || Unicode->Buffer == NULL ||
        (Unicode->Length & (sizeof(WCHAR) - 1)) != 0)
    {
        return Ansi;
    }

    /* Set length and normalize it */
    i = Unicode->Length / sizeof(WCHAR);
    i = min(i, Length - 1);
    i = min(i, RTL_NUMBER_OF(Wide));

    if (i != 0 &&
        !NT_SUCCESS(KdbpSafeReadMemory(Wide, Unicode->Buffer, i * sizeof(WCHAR))))
    {
        return Ansi;
    }

    /* Set source and destination, and copy */
    p = Ansi;
    pw = Wide;
    while (i--) *p++ = (CHAR)*pw++;

    /* Null terminate and return */
    *p = ANSI_NULL;
    return Ansi;
}

/*! \brief Print address...
 *
 * Tries to lookup line number, file name and function name for the given
 * address and prints it.
 * If no such information is found the address is printed in the format
 * <module: offset>, otherwise the format will be
 * <module: offset (filename:linenumber (functionname))>
 *
 * \retval TRUE  Module containing \a Address was found, \a Address was printed.
 * \retval FALSE  No module containing \a Address was found, nothing was printed.
 */
BOOLEAN
KdbSymPrintAddress(
    IN PVOID Address,
    IN PCONTEXT Context)
{
    PLDR_DATA_TABLE_ENTRY LdrEntryAddress;
    LDR_DATA_TABLE_ENTRY LdrEntry;
    ULONG_PTR RelativeAddress;
    BOOLEAN Printed = FALSE;
    CHAR ModuleNameAnsi[64];

    if (!KdbpSymFindModule(Address, -1, &LdrEntryAddress) ||
        !NT_SUCCESS(KdbpSafeReadMemory(&LdrEntry, LdrEntryAddress, sizeof(LdrEntry))))
    {
        return FALSE;
    }

    RelativeAddress = (ULONG_PTR)Address - (ULONG_PTR)LdrEntry.DllBase;

    KdbpSymUnicodeToAnsi(&LdrEntry.BaseDllName, ModuleNameAnsi, sizeof(ModuleNameAnsi));

    /* Print the module and offset first so the line is visible even if the
     * symbol data lookup below faults or stalls inside the frozen debugger */
    KdbPrintf("<%s:%Ix", ModuleNameAnsi, RelativeAddress);

    if (LdrEntry.PatchInformation && MmIsAddressValid(LdrEntry.PatchInformation))
    {
        ULONG LineNumber;
        CHAR FileName[256];
        CHAR FunctionName[256];

        if (RosSymGetAddressInformationEx(LdrEntry.PatchInformation, RelativeAddress, NULL, &LineNumber, FileName, sizeof(FileName), FunctionName, sizeof(FunctionName)))
        {
            KdbPrintf(" (%s:%d (%s))", FileName, LineNumber, FunctionName);
            Printed = TRUE;
        }
    }

    KdbPrintf(">");
    DBG_UNREFERENCED_LOCAL_VARIABLE(Printed);

    return TRUE;
}

static
BOOLEAN
KdbpSymGlobMatch(IN PCSTR Pattern, IN PCSTR String)
{
    PCSTR Star = NULL;
    PCSTR Retry = NULL;

    while (*String)
    {
        if (*Pattern == '?' ||
            (*Pattern != ANSI_NULL &&
             tolower((UCHAR)*Pattern) == tolower((UCHAR)*String)))
        {
            Pattern++;
            String++;
        }
        else if (*Pattern == '*')
        {
            Star = Pattern++;
            Retry = String;
        }
        else if (Star != NULL)
        {
            Pattern = Star + 1;
            String = ++Retry;
        }
        else
        {
            return FALSE;
        }
    }

    while (*Pattern == '*')
        Pattern++;
    return *Pattern == ANSI_NULL;
}

static
NTSTATUS
KdbpSymReadAnsiString(IN PCSTR Source, IN ULONG Available, OUT PCHAR Destination, IN ULONG DestinationLength)
{
    ULONG Index;
    ULONG Chunk;
    NTSTATUS Status;

    if (Source == NULL || Destination == NULL ||
        Available == 0 || DestinationLength == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Chunk = min(Available, DestinationLength - 1);
    Status = KdbpSafeReadMemory(Destination, (PVOID)Source, Chunk);
    if (!NT_SUCCESS(Status))
    {
        Destination[0] = ANSI_NULL;
        return Status;
    }

    for (Index = 0; Index < Chunk; Index++)
    {
        if (Destination[Index] == ANSI_NULL)
            return STATUS_SUCCESS;
    }

    Destination[Chunk] = ANSI_NULL;
    return (Chunk < Available) ? STATUS_NAME_TOO_LONG : STATUS_INVALID_IMAGE_FORMAT;
}

static
BOOLEAN
KdbpSymReadInfo(IN PVOID InformationAddress, OUT PROSSYM_INFO Information)
{
    if (InformationAddress == NULL ||
        !NT_SUCCESS(KdbpSafeReadMemory(Information, InformationAddress, sizeof(*Information))) ||
        Information->Symbols == NULL ||
        Information->Strings == NULL ||
        Information->SymbolsCount == 0 ||
        Information->SymbolsCount > KDB_MAX_SYMBOLS_PER_MODULE ||
        Information->StringsLength == 0)
    {
        return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
KdbpSymReadEntry(IN PROSSYM_INFO Information, IN ULONG Index, OUT PROSSYM_ENTRY Entry)
{
    if (Index >= Information->SymbolsCount ||
        Index > (MAXULONG_PTR - (ULONG_PTR)Information->Symbols) / sizeof(*Entry))
    {
        return FALSE;
    }

    return NT_SUCCESS(KdbpSafeReadMemory(Entry, Information->Symbols + Index, sizeof(*Entry)));
}

typedef struct _KDB_SYMBOL_ENUM_CONTEXT
{
    PCSTR ModulePattern;
    PCSTR SymbolPattern;
    ULONG MaximumMatches;
    ULONG Matches;
    ULONG Scanned;
    BOOLEAN Truncated;
    BOOLEAN Stop;
    PKDB_SYMBOL_ENUM_CALLBACK Callback;
    PVOID CallbackContext;
} KDB_SYMBOL_ENUM_CONTEXT, *PKDB_SYMBOL_ENUM_CONTEXT;

static
VOID
KdbpSymEnumerateModule(IN PLDR_DATA_TABLE_ENTRY LdrEntryAddress, IN PLDR_DATA_TABLE_ENTRY LdrEntry, IN OUT PKDB_SYMBOL_ENUM_CONTEXT Enum)
{
    ROSSYM_INFO Information;
    ULONG Index;
    ULONG LastFunctionOffset = MAXULONG;
    CHAR ModuleName[128];

    UNREFERENCED_PARAMETER(LdrEntryAddress);

    KdbpSymUnicodeToAnsi(&LdrEntry->BaseDllName, ModuleName, sizeof(ModuleName));
    if (Enum->Stop || LdrEntry->PatchInformation == NULL ||
        ModuleName[0] == ANSI_NULL ||
        !KdbpSymGlobMatch(Enum->ModulePattern, ModuleName) ||
        !KdbpSymReadInfo(LdrEntry->PatchInformation, &Information))
    {
        return;
    }

    for (Index = 0; Index < Information.SymbolsCount; Index++)
    {
        ROSSYM_ENTRY Entry;
        CHAR FunctionName[KDB_MAX_SYMBOL_NAME];
        CHAR FileName[KDB_MAX_SYMBOL_NAME];
        ULONG_PTR Address;

        if (Enum->Scanned++ >= KDB_MAX_SYMBOL_SCAN)
        {
            Enum->Truncated = TRUE;
            Enum->Stop = TRUE;
            return;
        }

        if (!KdbpSymReadEntry(&Information, Index, &Entry))
        {
            Enum->Truncated = TRUE;
            return;
        }

        /* One result per function, not one duplicate for every source line. */
        if (Entry.FunctionOffset == 0 ||
            Entry.FunctionOffset == LastFunctionOffset)
        {
            continue;
        }
        LastFunctionOffset = Entry.FunctionOffset;

        if (Entry.FunctionOffset >= Information.StringsLength ||
            !NT_SUCCESS(KdbpSymReadAnsiString(Information.Strings + Entry.FunctionOffset, Information.StringsLength - Entry.FunctionOffset, FunctionName, sizeof(FunctionName))) ||
            !KdbpSymGlobMatch(Enum->SymbolPattern, FunctionName))
        {
            continue;
        }

        FileName[0] = ANSI_NULL;
        if (Entry.FileOffset < Information.StringsLength)
        {
            (VOID)KdbpSymReadAnsiString(Information.Strings + Entry.FileOffset, Information.StringsLength - Entry.FileOffset, FileName, sizeof(FileName));
        }

        if (Entry.Address >= LdrEntry->SizeOfImage ||
            (ULONG_PTR)LdrEntry->DllBase > MAXULONG_PTR - Entry.Address)
        {
            continue;
        }
        Address = (ULONG_PTR)LdrEntry->DllBase + Entry.Address;

        if (Enum->Matches >= Enum->MaximumMatches)
        {
            Enum->Truncated = TRUE;
            Enum->Stop = TRUE;
            return;
        }

        Enum->Matches++;
        if (!Enum->Callback(Address, ModuleName, FunctionName, FileName, Entry.SourceLine, Enum->CallbackContext))
        {
            Enum->Stop = TRUE;
            return;
        }
    }
}

static
VOID
KdbpSymEnumerateModuleList(IN PLIST_ENTRY CurrentEntry, IN PLIST_ENTRY EndEntry, IN OUT PKDB_SYMBOL_ENUM_CONTEXT Enum)
{
    ULONG Iterations = 0;

    while (!Enum->Stop && CurrentEntry != NULL && CurrentEntry != EndEntry)
    {
        LIST_ENTRY Links;
        LDR_DATA_TABLE_ENTRY Entry;
        PLDR_DATA_TABLE_ENTRY EntryAddress;

        if (++Iterations > KDB_MAX_MODULES ||
            !NT_SUCCESS(KdbpSafeReadMemory(&Links, CurrentEntry, sizeof(Links))))
        {
            Enum->Truncated = TRUE;
            return;
        }

        EntryAddress = CONTAINING_RECORD(CurrentEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Entry, EntryAddress, sizeof(Entry))))
        {
            Enum->Truncated = TRUE;
            return;
        }

        KdbpSymEnumerateModule(EntryAddress, &Entry, Enum);
        if (Links.Flink == CurrentEntry)
        {
            Enum->Truncated = TRUE;
            return;
        }
        CurrentEntry = Links.Flink;
    }
}

NTSTATUS
KdbSymEnumerate(IN PCSTR ModulePattern, IN PCSTR SymbolPattern, IN ULONG MaximumMatches, IN PKDB_SYMBOL_ENUM_CALLBACK Callback, IN PVOID Context OPTIONAL, OUT PULONG MatchCount, OUT PBOOLEAN Truncated)
{
    KDB_SYMBOL_ENUM_CONTEXT Enum;
    PEPROCESS Process;
    PEB Peb;
    PEB_LDR_DATA Ldr;
    PPEB PebAddress;
    PPEB_LDR_DATA LdrAddress;
    PLIST_ENTRY LdrListHead;
    LIST_ENTRY KernelListHead;
    EPROCESS ProcessSnapshot;

    if (ModulePattern == NULL || SymbolPattern == NULL ||
        MaximumMatches == 0 || Callback == NULL ||
        MatchCount == NULL || Truncated == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&Enum, sizeof(Enum));
    Enum.ModulePattern = ModulePattern;
    Enum.SymbolPattern = SymbolPattern;
    Enum.MaximumMatches = MaximumMatches;
    Enum.Callback = Callback;
    Enum.CallbackContext = Context;

    if (NT_SUCCESS(KdbpSafeReadMemory(&KernelListHead, &PsLoadedModuleList, sizeof(KernelListHead))))
    {
        KdbpSymEnumerateModuleList(KernelListHead.Flink, &PsLoadedModuleList, &Enum);
    }
    else
    {
        Enum.Truncated = TRUE;
    }

    Process = PsGetCurrentProcess();
    if (!Enum.Stop && Process != NULL)
    {
        if (NT_SUCCESS(KdbpSafeReadMemory(&ProcessSnapshot, Process, sizeof(ProcessSnapshot))))
            PebAddress = ProcessSnapshot.Peb;
        else
            PebAddress = NULL;
        if (PebAddress != NULL &&
            NT_SUCCESS(KdbpSafeReadMemory(&Peb, PebAddress, sizeof(Peb))))
        {
            LdrAddress = Peb.Ldr;
            if (LdrAddress != NULL &&
                NT_SUCCESS(KdbpSafeReadMemory(&Ldr, LdrAddress, sizeof(Ldr))))
            {
                LdrListHead = (PLIST_ENTRY)((PUCHAR)LdrAddress + FIELD_OFFSET(PEB_LDR_DATA, InLoadOrderModuleList));
                KdbpSymEnumerateModuleList(Ldr.InLoadOrderModuleList.Flink, LdrListHead, &Enum);
            }
        }
    }

    *MatchCount = Enum.Matches;
    *Truncated = Enum.Truncated;
    return STATUS_SUCCESS;
}

static
BOOLEAN
KdbpSymFindEntryByAddress(IN PROSSYM_INFO Information, IN ULONG_PTR RelativeAddress, OUT PROSSYM_ENTRY Entry, OUT PULONG EntryIndex)
{
    ULONG Low = 0;
    ULONG High = Information->SymbolsCount;

    while (Low < High)
    {
        ULONG Middle = Low + (High - Low) / 2;
        ROSSYM_ENTRY Candidate;

        if (!KdbpSymReadEntry(Information, Middle, &Candidate))
            return FALSE;
        if (Candidate.Address <= RelativeAddress)
            Low = Middle + 1;
        else
            High = Middle;
    }

    if (Low == 0)
        return FALSE;
    *EntryIndex = Low - 1;
    return KdbpSymReadEntry(Information, *EntryIndex, Entry);
}

BOOLEAN
KdbSymPrintNearest(IN PVOID Address, IN PCONTEXT Context)
{
    PLDR_DATA_TABLE_ENTRY LdrEntryAddress;
    LDR_DATA_TABLE_ENTRY LdrEntry;
    ROSSYM_INFO Information;
    ROSSYM_ENTRY Entry;
    ROSSYM_ENTRY Candidate;
    ULONG EntryIndex;
    ULONG FunctionStartIndex;
    ULONG NextIndex;
    ULONG ScanCount;
    ULONG_PTR RelativeAddress;
    CHAR ModuleName[128];
    CHAR FunctionName[KDB_MAX_SYMBOL_NAME];
    CHAR FileName[KDB_MAX_SYMBOL_NAME];
    CHAR NextFunction[KDB_MAX_SYMBOL_NAME];
    BOOLEAN HaveNext = FALSE;

    UNREFERENCED_PARAMETER(Context);

    if (!KdbpSymFindModule(Address, -1, &LdrEntryAddress) ||
        !NT_SUCCESS(KdbpSafeReadMemory(&LdrEntry, LdrEntryAddress, sizeof(LdrEntry))))
    {
        return FALSE;
    }

    KdbpSymUnicodeToAnsi(&LdrEntry.BaseDllName, ModuleName, sizeof(ModuleName));
    RelativeAddress = (ULONG_PTR)Address - (ULONG_PTR)LdrEntry.DllBase;

    if (!KdbpSymReadInfo(LdrEntry.PatchInformation, &Information) ||
        !KdbpSymFindEntryByAddress(&Information, RelativeAddress, &Entry, &EntryIndex) ||
        Entry.FunctionOffset >= Information.StringsLength ||
        !NT_SUCCESS(KdbpSymReadAnsiString(Information.Strings + Entry.FunctionOffset, Information.StringsLength - Entry.FunctionOffset, FunctionName, sizeof(FunctionName))))
    {
        KdbpPrint("%p %s+0x%Ix (no loaded symbol)\n", Address, ModuleName, RelativeAddress);
        return TRUE;
    }

    FileName[0] = ANSI_NULL;
    if (Entry.FileOffset < Information.StringsLength)
    {
        (VOID)KdbpSymReadAnsiString(Information.Strings + Entry.FileOffset, Information.StringsLength - Entry.FileOffset, FileName, sizeof(FileName));
    }

    FunctionStartIndex = EntryIndex;
    for (ScanCount = 0;
         FunctionStartIndex != 0 && ScanCount < 65536;
         ScanCount++)
    {
        if (!KdbpSymReadEntry(&Information, FunctionStartIndex - 1, &Candidate) ||
            Candidate.FunctionOffset != Entry.FunctionOffset)
        {
            break;
        }
        FunctionStartIndex--;
        Entry.Address = Candidate.Address;
    }

    NextFunction[0] = ANSI_NULL;
    for (NextIndex = EntryIndex + 1, ScanCount = 0;
         NextIndex < Information.SymbolsCount && ScanCount < 65536;
         NextIndex++, ScanCount++)
    {
        if (!KdbpSymReadEntry(&Information, NextIndex, &Candidate))
            break;
        if (Candidate.FunctionOffset == 0 ||
            Candidate.FunctionOffset == Entry.FunctionOffset ||
            Candidate.FunctionOffset >= Information.StringsLength)
        {
            continue;
        }
        if (Candidate.Address < LdrEntry.SizeOfImage &&
            (ULONG_PTR)LdrEntry.DllBase <= MAXULONG_PTR - Candidate.Address &&
            NT_SUCCESS(KdbpSymReadAnsiString(Information.Strings + Candidate.FunctionOffset, Information.StringsLength - Candidate.FunctionOffset, NextFunction, sizeof(NextFunction))))
        {
            HaveNext = TRUE;
        }
        break;
    }

    KdbpPrint("%p %s!%s+0x%Ix [%s:%lu]", Address, ModuleName, FunctionName, RelativeAddress - Entry.Address, FileName[0] ? FileName : "?", Entry.SourceLine);
    if (HaveNext)
    {
        KdbpPrint("; next %p %s!%s", (PVOID)((ULONG_PTR)LdrEntry.DllBase + Candidate.Address), ModuleName, NextFunction);
    }
    KdbpPrint("\n");
    return TRUE;
}

static KSTART_ROUTINE LoadSymbolsRoutine;
/*! \brief          The symbol loader thread routine.
 *                  This opens the image file for reading and loads the symbols
 *                  section from there.
 *
 * \note            We must do this because KdbSymProcessSymbols can be
 *                  called at DISPATCH_LEVEL, where file I/O is not allowed.
 *
 * \param Context   Unused
 */
_Use_decl_annotations_
VOID
NTAPI
LoadSymbolsRoutine(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);

    while (TRUE)
    {
        PLIST_ENTRY ListEntry;
        NTSTATUS Status = KeWaitForSingleObject(&SymbolsToLoadEvent, WrKernel, KernelMode, FALSE, NULL);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("KeWaitForSingleObject failed?! 0x%08x\n", Status);
            LoadSymbols = FALSE;
            return;
        }

        while ((ListEntry = ExInterlockedRemoveHeadList(&SymbolsToLoad, &SymbolsToLoadLock)))
        {
            PLDR_DATA_TABLE_ENTRY LdrEntry = CONTAINING_RECORD(ListEntry, LDR_DATA_TABLE_ENTRY, InInitializationOrderLinks);
            HANDLE FileHandle;
            OBJECT_ATTRIBUTES Attrib;
            IO_STATUS_BLOCK Iosb;
            InitializeObjectAttributes(&Attrib, &LdrEntry->FullDllName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
            DPRINT1("Trying %wZ\n", &LdrEntry->FullDllName);
            Status = ZwOpenFile(&FileHandle,
                                FILE_READ_ACCESS | SYNCHRONIZE,
                                &Attrib,
                                &Iosb,
                                FILE_SHARE_READ,
                                FILE_SYNCHRONOUS_IO_NONALERT);
            if (!NT_SUCCESS(Status))
            {
                /* Try system paths */
                static const UNICODE_STRING System32Dir = RTL_CONSTANT_STRING(L"\\SystemRoot\\system32\\");
                UNICODE_STRING ImagePath;
                WCHAR ImagePathBuffer[256];
                RtlInitEmptyUnicodeString(&ImagePath, ImagePathBuffer, sizeof(ImagePathBuffer));
                RtlCopyUnicodeString(&ImagePath, &System32Dir);
                RtlAppendUnicodeStringToString(&ImagePath, &LdrEntry->BaseDllName);
                InitializeObjectAttributes(&Attrib, &ImagePath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
                DPRINT1("Trying %wZ\n", &ImagePath);
                Status = ZwOpenFile(&FileHandle,
                                    FILE_READ_ACCESS | SYNCHRONIZE,
                                    &Attrib,
                                    &Iosb,
                                    FILE_SHARE_READ,
                                    FILE_SYNCHRONOUS_IO_NONALERT);
                if (!NT_SUCCESS(Status))
                {
                    static const UNICODE_STRING DriversDir= RTL_CONSTANT_STRING(L"\\SystemRoot\\system32\\drivers\\");

                    RtlInitEmptyUnicodeString(&ImagePath, ImagePathBuffer, sizeof(ImagePathBuffer));
                    RtlCopyUnicodeString(&ImagePath, &DriversDir);
                    RtlAppendUnicodeStringToString(&ImagePath, &LdrEntry->BaseDllName);
                    InitializeObjectAttributes(&Attrib, &ImagePath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
                    DPRINT1("Trying %wZ\n", &ImagePath);
                    Status = ZwOpenFile(&FileHandle,
                                        FILE_READ_ACCESS | SYNCHRONIZE,
                                        &Attrib,
                                        &Iosb,
                                        FILE_SHARE_READ,
                                        FILE_SYNCHRONOUS_IO_NONALERT);
                }
            }

            if (!NT_SUCCESS(Status))
            {
                DPRINT1("Failed opening file %wZ (%wZ) for reading symbols (0x%08x)\n", &LdrEntry->FullDllName, &LdrEntry->BaseDllName, Status);
                /* We took a ref previously */
                MmUnloadSystemImage(LdrEntry);
                continue;
            }

            /* Hand it to Rossym */
            if (!RosSymCreateFromFile(&FileHandle, (PROSSYM_INFO*)&LdrEntry->PatchInformation))
                LdrEntry->PatchInformation = NULL;

            /* We're done for this one. */
            NtClose(FileHandle);
            MmUnloadSystemImage(LdrEntry);
        }
    }
}

/*! \brief          Load symbols from image mapping. If this fails,
 *
 * \param LdrEntry  The entry to load symbols from
 */
VOID
KdbSymProcessSymbols(
    _Inout_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ BOOLEAN Load)
{
    KIRQL OldIrql;

    if (!LoadSymbols)
        return;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    /* Check if this is unload */
    if (!Load)
    {
        /* Did we process it */
        if (LdrEntry->PatchInformation)
        {
            RosSymDelete(LdrEntry->PatchInformation);
            LdrEntry->PatchInformation = NULL;
        }
        return;
    }

    if (RosSymCreateFromMem(LdrEntry->DllBase, LdrEntry->SizeOfImage, (PROSSYM_INFO*)&LdrEntry->PatchInformation))
    {
        return;
    }

    /* Add a ref until we really process it */
    LdrEntry->LoadCount++;

    /* Tell our worker thread to read from it */
    KeAcquireSpinLock(&SymbolsToLoadLock, &OldIrql);
    InsertTailList(&SymbolsToLoad, &LdrEntry->InInitializationOrderLinks);
    KeReleaseSpinLock(&SymbolsToLoadLock, OldIrql);

    KeSetEvent(&SymbolsToLoadEvent, IO_NO_INCREMENT, FALSE);
}


/**
 * @brief   Initializes the KDB symbols implementation.
 *
 * @param[in]   BootPhase
 * Phase of initialization.
 *
 * @return
 * TRUE if symbols are to be loaded at this given BootPhase; FALSE if not.
 **/
BOOLEAN
KdbSymInit(
    _In_ ULONG BootPhase)
{
#if 1 // FIXME: This is a workaround HACK!!
    static BOOLEAN OrigLoadSymbols = FALSE;
#endif

    DPRINT("KdbSymInit() BootPhase=%d\n", BootPhase);

    if (BootPhase == 0)
    {
        PSTR CommandLine;
        SHORT Found = FALSE;
        CHAR YesNo;

        /* By default, load symbols in DBG builds, but not in REL builds
           or anything other than x86, because they only work on x86
           and can cause the system to hang on x64. */
#if DBG && defined(_M_IX86)
        LoadSymbols = TRUE;
#else
        LoadSymbols = FALSE;
#endif

        /* Check the command line for LOADSYMBOLS, NOLOADSYMBOLS,
         * LOADSYMBOLS={YES|NO}, NOLOADSYMBOLS={YES|NO} */
        ASSERT(KeLoaderBlock);
        CommandLine = KeLoaderBlock->LoadOptions;
        while (*CommandLine)
        {
            /* Skip any whitespace */
            while (isspace(*CommandLine))
                ++CommandLine;

            Found = 0;
            if (_strnicmp(CommandLine, "LOADSYMBOLS", 11) == 0)
            {
                Found = +1;
                CommandLine += 11;
            }
            else if (_strnicmp(CommandLine, "NOLOADSYMBOLS", 13) == 0)
            {
                Found = -1;
                CommandLine += 13;
            }
            if (Found != 0)
            {
                if (*CommandLine == '=')
                {
                    ++CommandLine;
                    YesNo = toupper(*CommandLine);
                    if (YesNo == 'N' || YesNo == '0')
                    {
                        Found = -1 * Found;
                    }
                }
                LoadSymbols = (0 < Found);
            }

            /* Move on to the next option */
            while (*CommandLine && !isspace(*CommandLine))
                ++CommandLine;
        }

#if 1 // FIXME: This is a workaround HACK!!
// Save the actual value of LoadSymbols but disable it for BootPhase 0.
        OrigLoadSymbols = LoadSymbols;
        LoadSymbols = FALSE;
        return OrigLoadSymbols;
#endif
    }
    else if (BootPhase == 1)
    {
        HANDLE Thread;
        NTSTATUS Status;
        KIRQL OldIrql;
        PLIST_ENTRY ListEntry;

#if 1 // FIXME: This is a workaround HACK!!
// Now, restore the actual value of LoadSymbols.
        LoadSymbols = OrigLoadSymbols;
#endif

        /* Do not continue loading symbols if we have less than 96MB of RAM */
        if (MmNumberOfPhysicalPages < (96 * 1024 * 1024 / PAGE_SIZE))
            LoadSymbols = FALSE;

        /* Continue this phase only if we need to load symbols */
        if (!LoadSymbols)
            return LoadSymbols;

        /* Launch our worker thread */
        InitializeListHead(&SymbolsToLoad);
        KeInitializeSpinLock(&SymbolsToLoadLock);
        KeInitializeEvent(&SymbolsToLoadEvent, SynchronizationEvent, FALSE);

        Status = PsCreateSystemThread(&Thread,
                                      THREAD_ALL_ACCESS,
                                      NULL, NULL, NULL,
                                      LoadSymbolsRoutine,
                                      NULL);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Failed starting symbols loader thread: 0x%08x\n", Status);
            LoadSymbols = FALSE;
            return LoadSymbols;
        }

        RosSymInitKernelMode();

        KeAcquireSpinLock(&PsLoadedModuleSpinLock, &OldIrql);

        for (ListEntry = PsLoadedModuleList.Flink;
             ListEntry != &PsLoadedModuleList;
             ListEntry = ListEntry->Flink)
        {
            PLDR_DATA_TABLE_ENTRY LdrEntry = CONTAINING_RECORD(ListEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
            KdbSymProcessSymbols(LdrEntry, TRUE);
        }

        KeReleaseSpinLock(&PsLoadedModuleSpinLock, OldIrql);
    }

    return LoadSymbols;
}

/* EOF */
