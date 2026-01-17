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

static
PCHAR
NTAPI
KdbpSymUnicodeToAnsi(IN PUNICODE_STRING Unicode,
                     OUT PCHAR Ansi,
                     IN ULONG Length);

static
BOOLEAN
KdbpSafeCopyAnsiString(
    OUT PCHAR Dest,
    IN ULONG DestSize,
    IN PCHAR Src);

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
    while (current_entry && current_entry != end_entry)
    {
        *pLdrEntry = CONTAINING_RECORD(current_entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

        if ((Address && Address >= (PVOID)(*pLdrEntry)->DllBase && Address < (PVOID)((ULONG_PTR)(*pLdrEntry)->DllBase + (*pLdrEntry)->SizeOfImage)) ||
            (Index >= 0 && (*Count)++ == Index))
        {
            return TRUE;
        }

        current_entry = current_entry->Flink;
    }

    return FALSE;
}

/*! \brief Check if an address is in a code section (not data)
 *
 * Parses PE headers to verify the address falls within an executable section
 * (typically .text). This filters out global variables and data structures
 * that may appear in backtraces due to stack scanning.
 *
 * \param Address  The address to check
 * \param LdrEntry The module containing the address
 *
 * \retval TRUE    Address is in a code (executable) section
 * \retval FALSE   Address is in a data section or invalid
 */
BOOLEAN
KdbpSymIsCodeAddress(
    IN PVOID Address,
    IN PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    PIMAGE_DOS_HEADER DosHeader;
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_SECTION_HEADER SectionHeader;
    ULONG_PTR RelativeAddress;
    USHORT i;
    BOOLEAN Result = FALSE;

    if (!Address || !LdrEntry || !LdrEntry->DllBase)
        return FALSE;

    /* Wrap in exception handler - PE headers may be unmapped during crashes */
    _SEH2_TRY
    {
        /* Get DOS and NT headers */
        DosHeader = (PIMAGE_DOS_HEADER)LdrEntry->DllBase;
        if (DosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        {
            _SEH2_YIELD(return FALSE);
        }

        NtHeaders = (PIMAGE_NT_HEADERS)((ULONG_PTR)DosHeader + DosHeader->e_lfanew);
        if (NtHeaders->Signature != IMAGE_NT_SIGNATURE)
        {
            _SEH2_YIELD(return FALSE);
        }

        /* Calculate relative address within the module */
        RelativeAddress = (ULONG_PTR)Address - (ULONG_PTR)LdrEntry->DllBase;

        /* Iterate through section headers */
        SectionHeader = IMAGE_FIRST_SECTION(NtHeaders);
        for (i = 0; i < NtHeaders->FileHeader.NumberOfSections; i++)
        {
            ULONG_PTR SectionStart = SectionHeader[i].VirtualAddress;
            ULONG_PTR SectionEnd = SectionStart + SectionHeader[i].Misc.VirtualSize;

            /* Check if address is in this section AND section is executable */
            if (RelativeAddress >= SectionStart && RelativeAddress < SectionEnd)
            {
                /* IMAGE_SCN_MEM_EXECUTE = 0x20000000 */
                if (SectionHeader[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)
                {
                    Result = TRUE; /* Address is in an executable section */
                }
                break;
            }
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* PE header access faulted - assume it's not code */
        Result = FALSE;
    }
    _SEH2_END;

    return Result;
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

    /* First try to look up the module in the kernel module list. */
    KeAcquireSpinLockAtDpcLevel(&PsLoadedModuleSpinLock);
    if(KdbpSymSearchModuleList(PsLoadedModuleList.Flink,
                               &PsLoadedModuleList,
                               &Count,
                               Address,
                               Index,
                               pLdrEntry))
    {
        KeReleaseSpinLockFromDpcLevel(&PsLoadedModuleSpinLock);
        return TRUE;
    }
    KeReleaseSpinLockFromDpcLevel(&PsLoadedModuleSpinLock);

    /* That didn't succeed. Try the module list of the current process now. */
    CurrentProcess = PsGetCurrentProcess();

    if(!CurrentProcess || !CurrentProcess->Peb || !CurrentProcess->Peb->Ldr)
        return FALSE;

    return KdbpSymSearchModuleList(CurrentProcess->Peb->Ldr->InLoadOrderModuleList.Flink,
                                   &CurrentProcess->Peb->Ldr->InLoadOrderModuleList,
                                   &Count,
                                   Address,
                                   Index,
                                   pLdrEntry);
}

static
BOOLEAN
KdbpSymModuleNameEquals(
    _In_ PCSTR Name,
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    CHAR ModuleNameAnsi[64];
    PCHAR Dot;

    KdbpSymUnicodeToAnsi(&LdrEntry->BaseDllName,
                        ModuleNameAnsi,
                        sizeof(ModuleNameAnsi));

    Dot = strchr(ModuleNameAnsi, '.');
    if (Dot) *Dot = ANSI_NULL;

    if (_stricmp(ModuleNameAnsi, Name) == 0)
        return TRUE;

    if (_stricmp(Name, "nt") == 0 &&
        (_strnicmp(ModuleNameAnsi, "ntoskrnl", 8) == 0 ||
         _strnicmp(ModuleNameAnsi, "ntkrnl", 6) == 0))
    {
        return TRUE;
    }

    return FALSE;
}

BOOLEAN
KdbpSymFindModuleByName(
    IN PCSTR Name,
    OUT PLDR_DATA_TABLE_ENTRY* pLdrEntry)
{
    PEPROCESS CurrentProcess;

    if (!Name || !pLdrEntry)
        return FALSE;

    /* First, try the kernel module list. */
    KeAcquireSpinLockAtDpcLevel(&PsLoadedModuleSpinLock);
    for (PLIST_ENTRY Link = PsLoadedModuleList.Flink;
         Link != &PsLoadedModuleList;
         Link = Link->Flink)
    {
        PLDR_DATA_TABLE_ENTRY LdrEntry;

        LdrEntry = CONTAINING_RECORD(Link, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (KdbpSymModuleNameEquals(Name, LdrEntry))
        {
            *pLdrEntry = LdrEntry;
            KeReleaseSpinLockFromDpcLevel(&PsLoadedModuleSpinLock);
            return TRUE;
        }
    }
    KeReleaseSpinLockFromDpcLevel(&PsLoadedModuleSpinLock);

    /* Fall back to the current process modules. */
    CurrentProcess = PsGetCurrentProcess();
    if (!CurrentProcess || !CurrentProcess->Peb || !CurrentProcess->Peb->Ldr)
        return FALSE;

    for (PLIST_ENTRY Link = CurrentProcess->Peb->Ldr->InLoadOrderModuleList.Flink;
         Link != &CurrentProcess->Peb->Ldr->InLoadOrderModuleList;
         Link = Link->Flink)
    {
        PLDR_DATA_TABLE_ENTRY LdrEntry;

        LdrEntry = CONTAINING_RECORD(Link, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (KdbpSymModuleNameEquals(Name, LdrEntry))
        {
            *pLdrEntry = LdrEntry;
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * @brief   Find cached rossym info for a module by name.
 *
 * @param[in]   ModName
 * Unicode string containing the module name to find.
 *
 * @return
 * Pointer to the ROSSYM_INFO for the module, or NULL if not found.
 **/
PROSSYM_INFO
KdbpSymFindCachedFile(
    IN PUNICODE_STRING ModName)
{
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    ANSI_STRING AnsiName;
    CHAR NameBuffer[64];
    NTSTATUS Status;

    if (!ModName || ModName->Length == 0)
        return NULL;

    /* Convert Unicode name to ANSI */
    AnsiName.Buffer = NameBuffer;
    AnsiName.MaximumLength = sizeof(NameBuffer);
    Status = RtlUnicodeStringToAnsiString(&AnsiName, ModName, FALSE);
    if (!NT_SUCCESS(Status))
        return NULL;

    /* Use existing module lookup */
    if (!KdbpSymFindModuleByName(NameBuffer, &LdrEntry))
        return NULL;

    /* Return the rossym info (PatchInformation) */
    return (PROSSYM_INFO)LdrEntry->PatchInformation;
}

static
BOOLEAN
KdbpSymLookupSymbolInModule(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ PCSTR Name,
    _Out_ PULONG_PTR Address)
{
    if (!LdrEntry || !Name || !Address)
        return FALSE;

#ifndef __ROS_DWARF__
    if (LdrEntry->PatchInformation)
    {
        PROSSYM_INFO RosInfo = LdrEntry->PatchInformation;

        if (RosInfo->Symbols && RosInfo->Strings)
        {
            for (ULONG i = 0; i < RosInfo->SymbolsCount; i++)
            {
                PROSSYM_ENTRY Entry = &RosInfo->Symbols[i];

                if (Entry->FunctionOffset &&
                    _stricmp(Name, RosInfo->Strings + Entry->FunctionOffset) == 0)
                {
                    *Address = (ULONG_PTR)LdrEntry->DllBase + Entry->Address;
                    return TRUE;
                }
            }
        }
    }
#endif

    *Address = (ULONG_PTR)RtlFindExportedRoutineByName(LdrEntry->DllBase, Name);
    return (*Address != 0);
}

BOOLEAN
KdbpSymAddressFromName(
    IN PCSTR Name,
    OUT PULONG_PTR Address)
{
    CHAR ModuleName[64];
    PCSTR SymbolName;
    PCSTR Bang;
    SIZE_T ModuleNameLength;
    PLDR_DATA_TABLE_ENTRY LdrEntry;

    if (!Name || !Address)
        return FALSE;

    SymbolName = Name;
    Bang = strchr(Name, '!');

    /* Handle a module prefix if present. */
    if (Bang)
    {
        ModuleNameLength = (SIZE_T)(Bang - Name);
        if (ModuleNameLength == 0 || ModuleNameLength >= sizeof(ModuleName))
            return FALSE;

        strncpy(ModuleName, Name, ModuleNameLength);
        ModuleName[ModuleNameLength] = ANSI_NULL;
        SymbolName = Bang + 1;
        if (*SymbolName == ANSI_NULL)
            return FALSE;

        if (!KdbpSymFindModuleByName(ModuleName, &LdrEntry))
            return FALSE;

        return KdbpSymLookupSymbolInModule(LdrEntry, SymbolName, Address);
    }

    /* No module specified: search all loaded modules. */
    for (INT Index = 0; KdbpSymFindModule(NULL, Index, &LdrEntry); Index++)
    {
        if (KdbpSymLookupSymbolInModule(LdrEntry, SymbolName, Address))
            return TRUE;
    }

    return FALSE;
}

static
PCHAR
NTAPI
KdbpSymUnicodeToAnsi(IN PUNICODE_STRING Unicode,
                     OUT PCHAR Ansi,
                     IN ULONG Length)
{
    PCHAR p;
    PWCHAR pw;
    ULONG i;

    /* Set length and normalize it */
    i = Unicode->Length / sizeof(WCHAR);
    i = min(i, Length - 1);

    /* Set source and destination, and copy */
    pw = Unicode->Buffer;
    p = Ansi;
    while (i--) *p++ = (CHAR)*pw++;

    /* Null terminate and return */
    *p = ANSI_NULL;
    return Ansi;
}

static
BOOLEAN
KdbpSafeCopyAnsiString(
    OUT PCHAR Dest,
    IN ULONG DestSize,
    IN PCHAR Src)
{
    ULONG i;
    CHAR ch;

    if (!Dest || DestSize == 0)
        return FALSE;

    Dest[0] = ANSI_NULL;
    if (!Src)
        return FALSE;

    for (i = 0; i + 1 < DestSize; i++)
    {
        if (!NT_SUCCESS(KdbpSafeReadMemory(&ch, Src + i, sizeof(ch))))
        {
            Dest[i] = ANSI_NULL;
            return FALSE;
        }
        Dest[i] = ch;
        if (ch == ANSI_NULL)
            return TRUE;
    }

    Dest[DestSize - 1] = ANSI_NULL;
    return TRUE;
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
/*
 * Global debug flag for symbol lookup tracing.
 * Set to TRUE to enable verbose debugging of DWARF symbol lookups.
 */
static BOOLEAN KdbSymDebugTrace = FALSE;

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
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    ULONG_PTR RelativeAddress;
    BOOLEAN Printed = FALSE;
    CHAR ModuleNameAnsi[64];

    if (!KdbpSymFindModule(Address, -1, &LdrEntry))
        return FALSE;

    RelativeAddress = (ULONG_PTR)Address - (ULONG_PTR)LdrEntry->DllBase;

    KdbpSymUnicodeToAnsi(&LdrEntry->BaseDllName,
                        ModuleNameAnsi,
                        sizeof(ModuleNameAnsi));

    if (LdrEntry->PatchInformation)
    {
        PROSSYM_INFO RosSymInfo = (PROSSYM_INFO)LdrEntry->PatchInformation;

        /*
         * Validate the rossym pointer before use.
         * The rossym data is allocated in NonPagedPool so it should always be
         * accessible. We do basic pointer validation to catch corruption.
         */
#if defined(_M_ARM64) || defined(__aarch64__)
        /* Check if RosSymInfo pointer is in kernel address space */
        if ((ULONG_PTR)RosSymInfo >= 0xFFFF000000000000ULL)
#endif /* _M_ARM64 || __aarch64__ */
        {
#ifdef __ROS_DWARF__
            /* DWARF-based rossym: use ROSSYM_LINEINFO structure */
            ROSSYM_LINEINFO LineInfo = {0};
            BOOLEAN SymResult = FALSE;
            CHAR FileNameBuf[256];
            CHAR DirNameBuf[256];
            CHAR FuncNameBuf[256];
            PCHAR FileName = "??";
            PCHAR DirName = NULL;
            PCHAR FuncName = "??";
            ULONG LineNumber = 0;

            _SEH2_TRY
            {
                SymResult = RosSymGetAddressInformation(RosSymInfo,
                                                        RelativeAddress,
                                                        &LineInfo);

                if (KdbSymDebugTrace && !SymResult)
                {
                    KdbPrintf("\n[DWARF-DEBUG] %s: offset %Ix - %s\n",
                              ModuleNameAnsi, (SIZE_T)RelativeAddress,
                              RosSymGetLastErrorString());
                }

                if (SymResult)
                {
                    LineNumber = LineInfo.LineNumber;

                    if (KdbpSafeCopyAnsiString(FileNameBuf, sizeof(FileNameBuf), LineInfo.FileName))
                        FileName = FileNameBuf;
                    if (KdbpSafeCopyAnsiString(FuncNameBuf, sizeof(FuncNameBuf), LineInfo.FunctionName))
                        FuncName = FuncNameBuf;
                    if (KdbpSafeCopyAnsiString(DirNameBuf, sizeof(DirNameBuf), LineInfo.DirectoryName) &&
                        DirNameBuf[0] != ANSI_NULL)
                    {
                        DirName = DirNameBuf;
                    }
                }
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                SymResult = FALSE;
            }
            _SEH2_END;

            if (SymResult)
            {
                /* Print with directory path if available */
                if (DirName)
                {
                    KdbPrintf("<%s:%Ix (%s/%s:%d (%s))>",
                              ModuleNameAnsi, (SIZE_T)RelativeAddress,
                              DirName, FileName, LineNumber, FuncName);
                }
                else
                {
                    KdbPrintf("<%s:%Ix (%s:%d (%s))>",
                              ModuleNameAnsi, (SIZE_T)RelativeAddress,
                              FileName, LineNumber, FuncName);
                }
                Printed = TRUE;
                RosSymFreeInfo(&LineInfo);
            }
#else
            /* Legacy rossym: use separate parameters */
            ULONG LineNumber;
            CHAR FileName[256];
            CHAR FunctionName[256];

            if (RosSymGetAddressInformation(RosSymInfo,
                                            RelativeAddress,
                                            &LineNumber,
                                            FileName,
                                            FunctionName))
            {
                KdbPrintf("<%s:%Ix (%s:%d (%s))>",
                          ModuleNameAnsi, (SIZE_T)RelativeAddress,
                          FileName, LineNumber, FunctionName);
                Printed = TRUE;
            }
#endif /* __ROS_DWARF__ */
        }
    }

    if (!Printed)
    {
        /* Just print module & address */
        KdbPrintf("<%s:%Ix>", ModuleNameAnsi, (SIZE_T)RelativeAddress);
    }

    return TRUE;
}

static KSTART_ROUTINE LoadSymbolsRoutine;
/*! \brief          The symbol loader thread routine.
 *                  This opens the image file for reading and loads the symbols
 *                  section from there.
 *
 * \note            We must do this because KdbSymProcessSymbols is
 *                  called at high IRQL and we can't set the event from here
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
    if (!LoadSymbols)
        return;

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

    /*
     * In-memory symbol loading requires IRQL < DISPATCH_LEVEL because
     * RosSymCreateFromMem internally calls RtlCreateUnicodeStringFromAsciiz
     * which uses RtlAnsiStringToUnicodeString with paged pool allocation.
     * The paged pool allocator has a PAGED_CODE() check that will bugcheck
     * (BAD_POOL_CALLER with POOL_ALLOC_IRQL_INVALID) at DISPATCH_LEVEL.
     *
     * When called at DISPATCH_LEVEL (e.g., while holding PsLoadedModuleSpinLock),
     * we must defer to the worker thread which runs at PASSIVE_LEVEL.
     */
    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        if (RosSymCreateFromMem(LdrEntry->DllBase, LdrEntry->SizeOfImage, (PROSSYM_INFO*)&LdrEntry->PatchInformation))
        {
            /* Symbols loaded successfully from in-memory image */
            return;
        }
    }

    /* Add a ref until we really process it */
    LdrEntry->LoadCount++;

    /*
     * Tell our worker thread to read from it.
     *
     * ARM64 FIX: Use KeAcquireSpinLock/KeReleaseSpinLock instead of the
     * AtDpcLevel variants. This function can be called at PASSIVE_LEVEL
     * (when RosSymCreateFromMem fails at low IRQL), but KeAcquireSpinLockAtDpcLevel
     * requires IRQL >= DISPATCH_LEVEL and will bugcheck IRQL_NOT_GREATER_OR_EQUAL
     * if called at lower IRQL.
     *
     * KeAcquireSpinLock properly raises IRQL to DISPATCH_LEVEL before acquiring
     * the lock, avoiding the bugcheck.
     */
    {
        KIRQL OldIrql;
        KeAcquireSpinLock(&SymbolsToLoadLock, &OldIrql);
        InsertTailList(&SymbolsToLoad, &LdrEntry->InInitializationOrderLinks);
        KeReleaseSpinLock(&SymbolsToLoadLock, OldIrql);
    }

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

        /* By default, load symbols in DBG builds on x86 and x64. */
#if DBG && (defined(_M_IX86) || defined(_M_AMD64) || defined(_AMD64_) || defined(__x86_64__) || defined(_M_ARM64) || defined(__aarch64__))
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
        DPRINT1("starting symbols loader thread: 0x%08x\n", Status);
        RosSymInitKernelMode();

        /*
         * Load symbols for all boot-loaded modules.
         *
         * We iterate through PsLoadedModuleList twice:
         * 1. First pass: Count modules while holding the spinlock
         * 2. Allocate array and copy module entries
         * 3. Second pass: Process symbols at PASSIVE_LEVEL (without spinlock)
         *
         * This is necessary because RosSymCreateFromMem uses paged pool
         * allocations internally (via RtlCreateUnicodeStringFromAsciiz),
         * which cannot be done at DISPATCH_LEVEL when holding a spinlock.
         */
        {
            ULONG ModuleCount = 0;
            PLDR_DATA_TABLE_ENTRY *ModuleArray = NULL;
            ULONG i;

            /* First pass: count modules */
            KeAcquireSpinLock(&PsLoadedModuleSpinLock, &OldIrql);
            for (ListEntry = PsLoadedModuleList.Flink;
                 ListEntry != &PsLoadedModuleList;
                 ListEntry = ListEntry->Flink)
            {
                ModuleCount++;
            }
            KeReleaseSpinLock(&PsLoadedModuleSpinLock, OldIrql);


            if (ModuleCount > 0)
            {
                /* Allocate array at PASSIVE_LEVEL */
                ModuleArray = ExAllocatePoolWithTag(NonPagedPool,
                                                    ModuleCount * sizeof(PLDR_DATA_TABLE_ENTRY),
                                                    'mysk');
                if (ModuleArray)
                {
                    /* Second pass: copy module pointers */
                    i = 0;
                    KeAcquireSpinLock(&PsLoadedModuleSpinLock, &OldIrql);
                    for (ListEntry = PsLoadedModuleList.Flink;
                         ListEntry != &PsLoadedModuleList && i < ModuleCount;
                         ListEntry = ListEntry->Flink)
                    {
                        PLDR_DATA_TABLE_ENTRY LdrEntry = CONTAINING_RECORD(ListEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
                        /* Take a reference to prevent unload while processing */
                        LdrEntry->LoadCount++;
                        ModuleArray[i++] = LdrEntry;
                    }
                    KeReleaseSpinLock(&PsLoadedModuleSpinLock, OldIrql);

                    /* Third pass: process symbols at PASSIVE_LEVEL */
                    for (i = 0; i < ModuleCount && ModuleArray[i]; i++)
                    {
                        PLDR_DATA_TABLE_ENTRY LdrEntry = ModuleArray[i];
                        KdbSymProcessSymbols(LdrEntry, TRUE);
                        /* Release the reference we took */
                        LdrEntry->LoadCount--;
                    }

                    ExFreePoolWithTag(ModuleArray, 'mysk');
                }
            }
        }
    }

    return LoadSymbols;
}

/* EOF */
