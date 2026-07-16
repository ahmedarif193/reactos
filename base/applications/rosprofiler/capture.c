/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     User-mode stack capture, log I/O, and profile aggregation
 */

#include "rosprofiler.h"
#include "profiler_pe.h"

#include <dbghelp.h>
#include <tlhelp32.h>
#include <reactos/rperf.h>
#include <ndk/exfuncs.h>
#include <ndk/ketypes.h>
#include <ndk/rtlfuncs.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct _RPERF_CAPTURE_CONTEXT
{
    RPERF_SESSION *Session;
} RPERF_CAPTURE_CONTEXT;

typedef enum _RPERF_SWEEP_RESULT
{
    RperfSweepContinue,
    RperfSweepStopped,
    RperfSweepTargetExit,
    RperfSweepDuration,
    RperfSweepError
} RPERF_SWEEP_RESULT;

typedef struct _RPERF_FUNCTION_INDEX_ENTRY
{
    DWORD64 FunctionAddress;
    DWORD64 RawAddress;
    SIZE_T SymbolIndex;
} RPERF_FUNCTION_INDEX_ENTRY;

#define RPERF_MODULE_REFRESH_MS 250

static BOOL
RperfResizeArray(PVOID *Array,
                 SIZE_T ElementSize,
                 SIZE_T *Capacity,
                 SIZE_T Required);

static ULONGLONG
RperfCounterDeltaToUs(LONGLONG Delta,
                      LONGLONG Frequency)
{
    ULONGLONG Whole;
    ULONGLONG Remainder;

    if (Delta <= 0 || Frequency <= 0)
        return 0;

    Whole = (ULONGLONG)(Delta / Frequency);
    Remainder = (ULONGLONG)(Delta % Frequency);
    return Whole * 1000000 + (Remainder * 1000000) / Frequency;
}

static BOOL
RperfMillisecondsToCounterTicks(DWORD Milliseconds,
                                LONGLONG Frequency,
                                LONGLONG *Ticks)
{
    ULONGLONG Whole, Remainder, Value;
    const ULONGLONG Maximum = 0x7fffffffffffffffULL;

    if (Frequency <= 0 || Ticks == NULL)
        return FALSE;
    Whole = (ULONGLONG)Frequency / 1000;
    Remainder = (ULONGLONG)Frequency % 1000;
    if (Milliseconds != 0 && Whole > Maximum / Milliseconds)
        return FALSE;
    Value = Whole * Milliseconds;
    Remainder = (Remainder * Milliseconds) / 1000;
    if (Value > Maximum - Remainder)
        return FALSE;
    *Ticks = (LONGLONG)(Value + Remainder);
    return TRUE;
}

static PBYTE
RperfQuerySystemProcesses(VOID)
{
    PBYTE Buffer = NULL;
    ULONG BufferSize = 0;
    ULONG RequiredSize = 0;
    NTSTATUS Status;
    ULONG Retry;

    Status = NtQuerySystemInformation(SystemProcessInformation,
                                      NULL,
                                      0,
                                      &RequiredSize);
    if (RequiredSize == 0)
        RequiredSize = 64 * 1024;

    for (Retry = 0; Retry < 8; ++Retry)
    {
        PBYTE NewBuffer;

        BufferSize = RequiredSize + 16 * 1024;
        if (Buffer != NULL)
        {
            NewBuffer = HeapReAlloc(GetProcessHeap(),
                                    0,
                                    Buffer,
                                    BufferSize);
        }
        else
        {
            NewBuffer = HeapAlloc(GetProcessHeap(), 0, BufferSize);
        }
        if (NewBuffer == NULL)
        {
            if (Buffer != NULL)
                HeapFree(GetProcessHeap(), 0, Buffer);
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return NULL;
        }
        Buffer = NewBuffer;

        Status = NtQuerySystemInformation(SystemProcessInformation,
                                          Buffer,
                                          BufferSize,
                                          &RequiredSize);
        if (Status != STATUS_INFO_LENGTH_MISMATCH)
            break;
        if (RequiredSize <= BufferSize)
            RequiredSize = BufferSize * 2;
    }

    if (!NT_SUCCESS(Status))
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        SetLastError(RtlNtStatusToDosError(Status));
        return NULL;
    }
    return Buffer;
}

BOOL
RperfEnumerateProcesses(RPERF_PROCESS_INFO **Processes,
                        SIZE_T *ProcessCount)
{
    PBYTE Buffer;
    PSYSTEM_PROCESS_INFORMATION Process;
    RPERF_PROCESS_INFO *Result = NULL;
    SIZE_T Count = 0;
    SIZE_T Capacity = 0;

    if (Processes == NULL || ProcessCount == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *Processes = NULL;
    *ProcessCount = 0;

    Buffer = RperfQuerySystemProcesses();
    if (Buffer == NULL)
        return FALSE;

    Process = (PSYSTEM_PROCESS_INFORMATION)Buffer;
    for (;;)
    {
        DWORD ProcessId = (DWORD)(ULONG_PTR)Process->UniqueProcessId;

        if (ProcessId != 0 && ProcessId != GetCurrentProcessId())
        {
            SIZE_T NameLength;
            if (!RperfResizeArray((PVOID *)&Result,
                                  sizeof(*Result),
                                  &Capacity,
                                  Count + 1))
            {
                HeapFree(GetProcessHeap(), 0, Buffer);
                if (Result != NULL)
                    HeapFree(GetProcessHeap(), 0, Result);
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return FALSE;
            }

            ZeroMemory(&Result[Count], sizeof(Result[Count]));
            Result[Count].ProcessId = ProcessId;
            Result[Count].ThreadCount = Process->NumberOfThreads;
            NameLength = Process->ImageName.Length / sizeof(WCHAR);
            if (Process->ImageName.Buffer != NULL && NameLength != 0)
            {
                if (NameLength >= ARRAYSIZE(Result[Count].Name))
                    NameLength = ARRAYSIZE(Result[Count].Name) - 1;
                CopyMemory(Result[Count].Name,
                           Process->ImageName.Buffer,
                           NameLength * sizeof(WCHAR));
                Result[Count].Name[NameLength] = UNICODE_NULL;
            }
            else
            {
                lstrcpyW(Result[Count].Name, L"<system>");
            }
            Count++;
        }

        if (Process->NextEntryOffset == 0)
            break;
        Process = (PSYSTEM_PROCESS_INFORMATION)
            ((PBYTE)Process + Process->NextEntryOffset);
    }

    HeapFree(GetProcessHeap(), 0, Buffer);
    *Processes = Result;
    *ProcessCount = Count;
    return TRUE;
}

VOID
RperfFreeProcesses(RPERF_PROCESS_INFO *Processes)
{
    if (Processes != NULL)
        HeapFree(GetProcessHeap(), 0, Processes);
}

static BOOL
RperfResizeArray(PVOID *Array,
                 SIZE_T ElementSize,
                 SIZE_T *Capacity,
                 SIZE_T Required)
{
    SIZE_T NewCapacity;
    PVOID NewArray;

    if (Required <= *Capacity)
        return TRUE;

    NewCapacity = (*Capacity != 0) ? *Capacity : 256;
    while (NewCapacity < Required)
    {
        if (NewCapacity > ((SIZE_T)-1) / 2)
        {
            SetLastError(ERROR_ARITHMETIC_OVERFLOW);
            return FALSE;
        }
        NewCapacity *= 2;
    }

    if (NewCapacity > ((SIZE_T)-1) / ElementSize)
    {
        SetLastError(ERROR_ARITHMETIC_OVERFLOW);
        return FALSE;
    }

    if (*Array != NULL)
    {
        NewArray = HeapReAlloc(GetProcessHeap(),
                               0,
                               *Array,
                               NewCapacity * ElementSize);
    }
    else
    {
        NewArray = HeapAlloc(GetProcessHeap(),
                             0,
                             NewCapacity * ElementSize);
    }

    if (NewArray == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    *Array = NewArray;
    *Capacity = NewCapacity;
    return TRUE;
}

static SIZE_T
RperfSymbolLowerBound(const RPERF_SESSION *Session,
                      DWORD64 Address,
                      BOOL *Found)
{
    SIZE_T First = 0;
    SIZE_T Count = Session->SymbolCount;

    while (Count != 0)
    {
        SIZE_T Step = Count / 2;
        SIZE_T Index = First + Step;

        if (Session->Symbols[Index].Address < Address)
        {
            First = Index + 1;
            Count -= Step + 1;
        }
        else
        {
            Count = Step;
        }
    }

    *Found = (First < Session->SymbolCount &&
              Session->Symbols[First].Address == Address);
    return First;
}

static SIZE_T
RperfHashAddress(DWORD64 Address)
{
    Address ^= Address >> 33;
    Address *= 0xff51afd7ed558ccdULL;
    Address ^= Address >> 33;
    Address *= 0xc4ceb9fe1a85ec53ULL;
    Address ^= Address >> 33;
    return (SIZE_T)(Address ^ (Address >> (sizeof(SIZE_T) * 4)));
}

static BOOL
RperfRebuildSymbolHash(RPERF_SESSION *Session,
                       SIZE_T RequiredCount)
{
    SIZE_T Capacity = 512;
    SIZE_T *Hash;
    SIZE_T Index;

    while (Capacity < RequiredCount * 2)
    {
        if (Capacity > ((SIZE_T)-1) / 2)
        {
            SetLastError(ERROR_ARITHMETIC_OVERFLOW);
            return FALSE;
        }
        Capacity *= 2;
    }
    if (Capacity > ((SIZE_T)-1) / sizeof(*Hash))
    {
        SetLastError(ERROR_ARITHMETIC_OVERFLOW);
        return FALSE;
    }

    Hash = HeapAlloc(GetProcessHeap(),
                     HEAP_ZERO_MEMORY,
                     Capacity * sizeof(*Hash));
    if (Hash == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    for (Index = 0; Index < Session->SymbolCount; ++Index)
    {
        SIZE_T Slot = RperfHashAddress(Session->Symbols[Index].Address) &
                      (Capacity - 1);
        while (Hash[Slot] != 0)
            Slot = (Slot + 1) & (Capacity - 1);
        Hash[Slot] = Index + 1;
    }

    if (Session->SymbolHash != NULL)
        HeapFree(GetProcessHeap(), 0, Session->SymbolHash);
    Session->SymbolHash = Hash;
    Session->SymbolHashCapacity = Capacity;
    return TRUE;
}

static BOOL
RperfFindRawSymbolIndex(const RPERF_SESSION *Session,
                        DWORD64 Address,
                        SIZE_T *Index)
{
    BOOL Found;

    if (Session->SymbolsSorted)
    {
        SIZE_T Position = RperfSymbolLowerBound(Session, Address, &Found);
        if (Found && Index != NULL)
            *Index = Position;
        return Found;
    }

    if (Session->SymbolHashCapacity != 0)
    {
        SIZE_T Slot = RperfHashAddress(Address) &
                      (Session->SymbolHashCapacity - 1);
        SIZE_T Start = Slot;

        do
        {
            SIZE_T Entry = Session->SymbolHash[Slot];
            if (Entry == 0)
                return FALSE;
            if (Session->Symbols[Entry - 1].Address == Address)
            {
                if (Index != NULL)
                    *Index = Entry - 1;
                return TRUE;
            }
            Slot = (Slot + 1) & (Session->SymbolHashCapacity - 1);
        } while (Slot != Start);
    }
    return FALSE;
}

const RPERF_SYMBOL *
RperfFindSymbol(const RPERF_SESSION *Session,
                DWORD64 Address)
{
    BOOL Found;
    SIZE_T Index, First, Count;

    if (Session == NULL || Session->SymbolCount == 0)
        return NULL;

    Found = RperfFindRawSymbolIndex(Session, Address, &Index);
    if (Found)
        return &Session->Symbols[Index];

    if (Session->FunctionIndexValid)
    {
        First = 0;
        Count = Session->FunctionCount;
        while (Count != 0)
        {
            SIZE_T Step = Count / 2;
            SIZE_T FunctionIndex = First + Step;
            const RPERF_SYMBOL *Symbol = &Session->Symbols[
                Session->FunctionSymbols[FunctionIndex]];

            if (Symbol->FunctionAddress < Address)
            {
                First = FunctionIndex + 1;
                Count -= Step + 1;
            }
            else
            {
                Count = Step;
            }
        }
        if (First < Session->FunctionCount)
        {
            const RPERF_SYMBOL *Symbol = &Session->Symbols[
                Session->FunctionSymbols[First]];
            if (Symbol->FunctionAddress == Address)
                return Symbol;
        }
    }
    return NULL;
}

static VOID
RperfCopyLogField(PSTR Destination,
                  SIZE_T DestinationCount,
                  PCSTR Source)
{
    SIZE_T Index = 0;

    if (DestinationCount == 0)
        return;

    if (Source != NULL)
    {
        while (*Source != ANSI_NULL && Index + 1 < DestinationCount)
        {
            CHAR Character = *Source++;
            if (Character == '\t' || Character == '\r' || Character == '\n')
                Character = ' ';
            Destination[Index++] = Character;
        }
    }

    Destination[Index] = ANSI_NULL;
}

static BOOL
RperfRememberModule(RPERF_SESSION *Session,
                    const MODULEENTRY32W *Entry,
                    const IMAGEHLP_MODULE64 *DbgModule)
{
    RPERF_CAPTURE_MODULE *Module = NULL;
    RPERF_PE_IDENTITY Identity;
    SIZE_T Index;

    for (Index = 0; Index < Session->ModuleCount; ++Index)
    {
        if (Session->Modules[Index].Base ==
            (DWORD64)(ULONG_PTR)Entry->modBaseAddr)
        {
            Module = &Session->Modules[Index];
            break;
        }
    }
    if (Module == NULL)
    {
        if (!RperfResizeArray((PVOID *)&Session->Modules,
                              sizeof(*Session->Modules),
                              &Session->ModuleCapacity,
                              Session->ModuleCount + 1))
            return FALSE;
        Module = &Session->Modules[Session->ModuleCount++];
    }
    ZeroMemory(Module, sizeof(*Module));
    Module->Base = (DWORD64)(ULONG_PTR)Entry->modBaseAddr;
    Module->Size = Entry->modBaseSize;
    lstrcpynW(Module->Path, Entry->szExePath,
              ARRAYSIZE(Module->Path));
    if (RperfReadPeIdentity(Entry->szExePath, &Identity))
    {
        Module->Architecture = Identity.Architecture;
        Module->TimeDateStamp = Identity.TimeDateStamp;
        Module->Checksum = Identity.Checksum;
        if (Identity.ImageSize != 0)
            Module->Size = Identity.ImageSize;
        CopyMemory(Module->DebugId, Identity.DebugId,
                   sizeof(Module->DebugId));
        Module->DebugAge = Identity.DebugAge;
        if (Identity.HasRosSym)
            Module->Flags |= RPERF_MODULE_FLAG_EMBEDDED_ROSSYM;
    }
    else if (DbgModule != NULL)
    {
        Module->TimeDateStamp = DbgModule->TimeDateStamp;
        Module->Checksum = DbgModule->CheckSum;
        if (DbgModule->ImageSize != 0)
            Module->Size = DbgModule->ImageSize;
        CopyMemory(Module->DebugId, &DbgModule->PdbSig70,
                   sizeof(Module->DebugId));
        Module->DebugAge = DbgModule->PdbAge;
    }
    return TRUE;
}

static BOOL
RperfSynchronizeModules(RPERF_SESSION *Session,
                        HANDLE Process)
{
    HANDLE Snapshot = INVALID_HANDLE_VALUE;
    MODULEENTRY32W Module;
    DWORD ProcessId = GetProcessId(Process);
    DWORD Retry;
    BOOL Available = FALSE;

    if (ProcessId == 0)
        return FALSE;
    for (Retry = 0; Retry < 3; ++Retry)
    {
        Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,
                                            ProcessId);
        if (Snapshot != INVALID_HANDLE_VALUE ||
            GetLastError() != ERROR_BAD_LENGTH)
        {
            break;
        }
    }
    if (Snapshot == INVALID_HANDLE_VALUE)
        return FALSE;

    ZeroMemory(&Module, sizeof(Module));
    Module.dwSize = sizeof(Module);
    if (Module32FirstW(Snapshot, &Module))
    {
        do
        {
            IMAGEHLP_MODULE64 ModuleInfo;
            DWORD64 Base = (DWORD64)(ULONG_PTR)Module.modBaseAddr;
            BOOL ModuleInfoAvailable;

            ZeroMemory(&ModuleInfo, sizeof(ModuleInfo));
            ModuleInfo.SizeOfStruct = sizeof(ModuleInfo);
            ModuleInfoAvailable = SymGetModuleInfo64(Process, Base,
                                                     &ModuleInfo);
            if (!ModuleInfoAvailable)
            {
                SymLoadModuleExW(Process,
                                 NULL,
                                 Module.szExePath,
                                 Module.szModule,
                                 Base,
                                 Module.modBaseSize,
                                 NULL,
                                 0);
                ZeroMemory(&ModuleInfo, sizeof(ModuleInfo));
                ModuleInfo.SizeOfStruct = sizeof(ModuleInfo);
                ModuleInfoAvailable = SymGetModuleInfo64(Process, Base,
                                                         &ModuleInfo);
            }
            if (!RperfRememberModule(Session, &Module,
                                     ModuleInfoAvailable ?
                                     &ModuleInfo : NULL))
            {
                CloseHandle(Snapshot);
                return FALSE;
            }
            Available = TRUE;
            Module.dwSize = sizeof(Module);
        } while (Module32NextW(Snapshot, &Module));
    }
    CloseHandle(Snapshot);
    return Available;
}

static VOID
RperfResolveSymbol(RPERF_SESSION *Session,
                   HANDLE Process,
                   DWORD64 Address,
                   RPERF_SYMBOL *Symbol)
{
    BYTE SymbolBuffer[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *SymbolInfo = (SYMBOL_INFO *)SymbolBuffer;
    IMAGEHLP_MODULE64 ModuleInfo;
    DWORD64 Displacement = 0;

    ZeroMemory(Symbol, sizeof(*Symbol));
    Symbol->Address = Address;
    Symbol->FunctionAddress = Address;
    RperfCopyLogField(Symbol->Module,
                      ARRAYSIZE(Symbol->Module),
                      "<unknown>");
    RperfCopyLogField(Symbol->Name,
                      ARRAYSIZE(Symbol->Name),
                      "<unknown>");

    ZeroMemory(&ModuleInfo, sizeof(ModuleInfo));
    ModuleInfo.SizeOfStruct = sizeof(ModuleInfo);
    if (SymGetModuleInfo64(Process, Address, &ModuleInfo) ||
        (RperfSynchronizeModules(Session, Process) &&
         SymGetModuleInfo64(Process, Address, &ModuleInfo)))
    {
        Symbol->ModuleBase = ModuleInfo.BaseOfImage;
        RperfCopyLogField(Symbol->Module,
                          ARRAYSIZE(Symbol->Module),
                          ModuleInfo.ModuleName);
    }
    else
    {
        Symbol->ModuleBase = SymGetModuleBase64(Process, Address);
    }

    ZeroMemory(SymbolBuffer, sizeof(SymbolBuffer));
    SymbolInfo->SizeOfStruct = sizeof(*SymbolInfo);
    SymbolInfo->MaxNameLen = 255;
    if (SymFromAddr(Process, Address, &Displacement, SymbolInfo))
    {
        SIZE_T ModuleIndex;

        Symbol->FunctionAddress = SymbolInfo->Address;
        Symbol->Displacement = Displacement;
        Symbol->Status = RperfSymbolStatusResolved;
        RperfCopyLogField(Symbol->Name,
                          ARRAYSIZE(Symbol->Name),
                          SymbolInfo->Name);
        ZeroMemory(&ModuleInfo, sizeof(ModuleInfo));
        ModuleInfo.SizeOfStruct = sizeof(ModuleInfo);
        if (SymGetModuleInfo64(Process, Address, &ModuleInfo))
        {
            switch (ModuleInfo.SymType)
            {
                case SymPdb:
                    Symbol->Source = RperfSymbolSourcePdb;
                    break;
                case SymDia:
                    Symbol->Source = RperfSymbolSourceDwarf;
                    break;
                case SymCv:
                case SymCoff:
                    Symbol->Source = RperfSymbolSourceCoff;
                    break;
                case SymExport:
                    Symbol->Source = RperfSymbolSourceExport;
                    break;
                default:
                    Symbol->Source = RperfSymbolSourceUnknown;
                    break;
            }
        }
        for (ModuleIndex = 0;
             ModuleIndex < Session->ModuleCount;
             ++ModuleIndex)
        {
            if (Session->Modules[ModuleIndex].Base == Symbol->ModuleBase &&
                (Session->Modules[ModuleIndex].Flags &
                 RPERF_MODULE_FLAG_EMBEDDED_ROSSYM))
            {
                Symbol->Source = RperfSymbolSourceRosSym;
                break;
            }
        }
    }
    else if (Symbol->ModuleBase != 0 && Address >= Symbol->ModuleBase)
    {
        Symbol->Displacement = Address - Symbol->ModuleBase;
        Symbol->Source = RperfSymbolSourceModuleOffset;
        Symbol->Status = RperfSymbolStatusSymbolsMissing;
        _snprintf(Symbol->Name, ARRAYSIZE(Symbol->Name),
                  "+0x%I64x", Symbol->Displacement);
        Symbol->Name[ARRAYSIZE(Symbol->Name) - 1] = ANSI_NULL;
    }
}

static BOOL
RperfInsertSymbol(RPERF_SESSION *Session,
                  const RPERF_SYMBOL *Symbol,
                  SIZE_T *Index)
{
    SIZE_T InsertAt;

    if (Session->SymbolsSorted)
    {
        BOOL Found;

        InsertAt = RperfSymbolLowerBound(Session, Symbol->Address, &Found);
        if (Found)
        {
            if (Index != NULL)
                *Index = InsertAt;
            return TRUE;
        }
        if (!RperfRebuildSymbolHash(Session, Session->SymbolCount + 1))
            return FALSE;
        Session->SymbolsSorted = FALSE;
    }
    else
    {
        if (Session->SymbolHashCapacity == 0 ||
            Session->SymbolCount + 1 > Session->SymbolHashCapacity / 2)
        {
            if (!RperfRebuildSymbolHash(Session, Session->SymbolCount + 1))
                return FALSE;
        }
        if (RperfFindRawSymbolIndex(Session, Symbol->Address, &InsertAt))
        {
            if (Index != NULL)
                *Index = InsertAt;
            return TRUE;
        }
    }

    if (Session->SymbolCount >= RPERF_MAX_SYMBOLS)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }

    if (!RperfResizeArray((PVOID *)&Session->Symbols,
                          sizeof(*Session->Symbols),
                          &Session->SymbolCapacity,
                          Session->SymbolCount + 1))
    {
        return FALSE;
    }

    InsertAt = Session->SymbolCount;
    Session->Symbols[InsertAt] = *Symbol;
    Session->SymbolCount++;
    {
        SIZE_T Slot = RperfHashAddress(Symbol->Address) &
                      (Session->SymbolHashCapacity - 1);
        while (Session->SymbolHash[Slot] != 0)
            Slot = (Slot + 1) & (Session->SymbolHashCapacity - 1);
        Session->SymbolHash[Slot] = InsertAt + 1;
    }
    Session->FunctionIndexValid = FALSE;
    if (Index != NULL)
        *Index = InsertAt;
    return TRUE;
}

static BOOL
RperfEnsureSymbol(RPERF_SESSION *Session,
                  HANDLE Process,
                  DWORD64 Address,
                  FILE *Log)
{
    SIZE_T Index;
    RPERF_SYMBOL Symbol;

    if (RperfFindRawSymbolIndex(Session, Address, &Index))
        return TRUE;

    if (Process != NULL)
    {
        RperfResolveSymbol(Session, Process, Address, &Symbol);
    }
    else
    {
        ZeroMemory(&Symbol, sizeof(Symbol));
        Symbol.Address = Address;
        Symbol.FunctionAddress = Address;
        RperfCopyLogField(Symbol.Module,
                          ARRAYSIZE(Symbol.Module),
                          "<unknown>");
        RperfCopyLogField(Symbol.Name,
                          ARRAYSIZE(Symbol.Name),
                          "<unknown>");
        Symbol.Source = RperfSymbolSourceModuleOffset;
        Symbol.Status = RperfSymbolStatusImageMissing;
    }

    if (!RperfInsertSymbol(Session, &Symbol, &Index))
        return FALSE;

    if (Log != NULL)
    {
        const RPERF_SYMBOL *Inserted = &Session->Symbols[Index];
        if (fprintf(Log,
                    "y\t%I64x\t%I64x\t%I64x\t%I64x\t%s\t%s\n",
                    Inserted->Address,
                    Inserted->FunctionAddress,
                    Inserted->ModuleBase,
                    Inserted->Displacement,
                    Inserted->Module,
                    Inserted->Name) < 0)
        {
            SetLastError(ERROR_WRITE_FAULT);
            return FALSE;
        }
    }

    return TRUE;
}

VOID
RperfFormatSymbol(const RPERF_SESSION *Session,
                  DWORD64 Address,
                  PWSTR Buffer,
                  SIZE_T BufferCount)
{
    const RPERF_SYMBOL *Symbol = RperfFindSymbol(Session, Address);

    if (BufferCount == 0)
        return;

    if (Symbol == NULL)
    {
        _snwprintf(Buffer, BufferCount, L"0x%I64x", Address);
    }
    else if (Address > Symbol->FunctionAddress)
    {
        _snwprintf(Buffer,
                   BufferCount,
                   L"%S!%S+0x%I64x",
                   Symbol->Module,
                   Symbol->Name,
                   Address - Symbol->FunctionAddress);
    }
    else
    {
        _snwprintf(Buffer,
                   BufferCount,
                   L"%S!%S",
                   Symbol->Module,
                   Symbol->Name);
    }

    Buffer[BufferCount - 1] = UNICODE_NULL;
}

VOID
RperfSessionInitialize(RPERF_SESSION *Session)
{
    ZeroMemory(Session, sizeof(*Session));
}

VOID
RperfCaptureStop(RPERF_SESSION *Session)
{
    if (Session->StopEvent != NULL)
        SetEvent(Session->StopEvent);
}

VOID
RperfCaptureWait(RPERF_SESSION *Session)
{
    if (Session->WorkerThread != NULL)
    {
        WaitForSingleObject(Session->WorkerThread, INFINITE);
        CloseHandle(Session->WorkerThread);
        Session->WorkerThread = NULL;
    }

    if (Session->StopEvent != NULL)
    {
        CloseHandle(Session->StopEvent);
        Session->StopEvent = NULL;
    }
}

VOID
RperfSessionClear(RPERF_SESSION *Session)
{
    RperfCaptureStop(Session);
    RperfCaptureWait(Session);

    if (Session->Samples != NULL)
        HeapFree(GetProcessHeap(), 0, Session->Samples);
    if (Session->Symbols != NULL)
        HeapFree(GetProcessHeap(), 0, Session->Symbols);
    if (Session->SymbolHash != NULL)
        HeapFree(GetProcessHeap(), 0, Session->SymbolHash);
    if (Session->FunctionSymbols != NULL)
        HeapFree(GetProcessHeap(), 0, Session->FunctionSymbols);
    if (Session->Modules != NULL)
        HeapFree(GetProcessHeap(), 0, Session->Modules);
    if (Session->Nodes != NULL)
        HeapFree(GetProcessHeap(), 0, Session->Nodes);
    if (Session->NodeHash != NULL)
        HeapFree(GetProcessHeap(), 0, Session->NodeHash);

    ZeroMemory(Session, sizeof(*Session));
}

static BOOL
RperfAppendSample(RPERF_SESSION *Session,
                  const RPERF_SAMPLE *Sample)
{
    if (Session->SampleCount >= RPERF_MAX_SAMPLES)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }

    if (!RperfResizeArray((PVOID *)&Session->Samples,
                          sizeof(*Session->Samples),
                          &Session->SampleCapacity,
                          Session->SampleCount + 1))
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    Session->Samples[Session->SampleCount++] = *Sample;
    return TRUE;
}

static BOOL
RperfReserveSample(RPERF_SESSION *Session)
{
    if (Session->SampleCount >= RPERF_MAX_SAMPLES)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }

    if (!RperfResizeArray((PVOID *)&Session->Samples,
                          sizeof(*Session->Samples),
                          &Session->SampleCapacity,
                          Session->SampleCount + 1))
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    return TRUE;
}

static VOID
RperfIncrementCaptureCounter(ULONGLONG *Counter)
{
    if (*Counter != (ULONGLONG)-1)
        (*Counter)++;
}

static VOID
RperfAccountSampleState(RPERF_SESSION *Session,
                        USHORT Flags)
{
    if (Flags & RPERF_SAMPLE_STATE_KNOWN)
    {
        RperfIncrementCaptureCounter(&Session->StateTaggedSamples);
        if (Flags & RPERF_SAMPLE_WAITING)
            RperfIncrementCaptureCounter(&Session->WaitingSamples);
    }
}

static USHORT
RperfCaptureStack(HANDLE Process,
                  HANDLE Thread,
                  DWORD64 *Frames,
                  PUSHORT Flags,
                  PBOOL ContextCaptured)
{
    CONTEXT Context;
    STACKFRAME64 StackFrame;
    DWORD MachineType;
    DWORD64 LastPc;
    DWORD64 LastStack;
    ULONG RepeatedPc = 0;
    USHORT Depth = 0;

    *ContextCaptured = FALSE;
    ZeroMemory(&Context, sizeof(Context));
    Context.ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext(Thread, &Context))
        return 0;
    *ContextCaptured = TRUE;

    ZeroMemory(&StackFrame, sizeof(StackFrame));
    StackFrame.AddrPC.Mode = AddrModeFlat;
    StackFrame.AddrReturn.Mode = AddrModeFlat;
    StackFrame.AddrFrame.Mode = AddrModeFlat;
    StackFrame.AddrStack.Mode = AddrModeFlat;
    StackFrame.AddrBStore.Mode = AddrModeFlat;

#if defined(_M_IX86)
    MachineType = IMAGE_FILE_MACHINE_I386;
    StackFrame.AddrPC.Offset = Context.Eip;
    StackFrame.AddrStack.Offset = Context.Esp;
    StackFrame.AddrFrame.Offset = Context.Ebp;
#elif defined(_M_AMD64)
    MachineType = IMAGE_FILE_MACHINE_AMD64;
    StackFrame.AddrPC.Offset = Context.Rip;
    StackFrame.AddrStack.Offset = Context.Rsp;
    StackFrame.AddrFrame.Offset = Context.Rbp;
#elif defined(_M_ARM)
    MachineType = IMAGE_FILE_MACHINE_ARMNT;
    StackFrame.AddrPC.Offset = Context.Pc;
    StackFrame.AddrStack.Offset = Context.Sp;
    StackFrame.AddrFrame.Offset = Context.R11;
#elif defined(_M_ARM64)
    MachineType = IMAGE_FILE_MACHINE_ARM64;
    StackFrame.AddrPC.Offset = Context.Pc;
    StackFrame.AddrStack.Offset = Context.Sp;
    StackFrame.AddrFrame.Offset = Context.Fp;
#else
#error Unsupported architecture
#endif

    if (StackFrame.AddrPC.Offset == 0)
        return 0;

    Frames[Depth++] = StackFrame.AddrPC.Offset;
    LastPc = StackFrame.AddrPC.Offset;
    LastStack = StackFrame.AddrStack.Offset;

    while (Depth < RPERF_MAX_FRAMES &&
           StackWalk64(MachineType,
                       Process,
                       Thread,
                       &StackFrame,
                       &Context,
                       NULL,
                       SymFunctionTableAccess64,
                       SymGetModuleBase64,
                       NULL))
    {
        DWORD64 Pc = StackFrame.AddrPC.Offset;

        if (Pc == 0)
            break;

        if (Pc == LastPc && StackFrame.AddrStack.Offset == LastStack)
        {
            if (++RepeatedPc > 2)
            {
                *Flags |= RPERF_SAMPLE_UNWIND_FAILED;
                break;
            }
        }
        else
        {
            RepeatedPc = 0;
        }

        if (!(Depth == 1 && Pc == Frames[0]))
            Frames[Depth++] = Pc;
        LastPc = Pc;
        LastStack = StackFrame.AddrStack.Offset;
    }

    if (Depth == RPERF_MAX_FRAMES)
        *Flags |= RPERF_SAMPLE_TRUNCATED;

    return Depth;
}

static BOOL
RperfWriteSample(FILE *Log,
                 const RPERF_SAMPLE *Sample)
{
    USHORT Index;

    if (fprintf(Log,
                "s\t%I64u\t%lu\t%lu\t%u\t%u",
                Sample->TimeUs,
                Sample->ProcessId,
                Sample->ThreadId,
                Sample->Flags,
                Sample->Depth) < 0)
    {
        return FALSE;
    }

    for (Index = 0; Index < Sample->Depth; ++Index)
    {
        if (fprintf(Log, "\t%I64x", Sample->Frames[Index]) < 0)
            return FALSE;
    }

    return fputc('\n', Log) != EOF;
}

static BOOL
RperfCaptureThread(RPERF_SESSION *Session,
                   HANDLE Process,
                   DWORD ThreadId,
                   ULONG ThreadState,
                   ULONG WaitReason,
                   LARGE_INTEGER StartCounter,
                   LARGE_INTEGER Frequency,
                   FILE *Log)
{
    HANDLE Thread;
    DWORD SuspendCount;
    DWORD ResumeResult;
    DWORD ResumeError;
    LARGE_INTEGER Counter;
    RPERF_SAMPLE Sample;
    USHORT Index;
    BOOL ContextCaptured;
    BOOL Result = FALSE;

    RperfIncrementCaptureCounter(&Session->Counters.AttemptedSamples);
    SetLastError(ERROR_SUCCESS);
    Thread = OpenThread(THREAD_SUSPEND_RESUME |
                        THREAD_GET_CONTEXT |
                        THREAD_QUERY_INFORMATION,
                        FALSE,
                        ThreadId);
    if (Thread == NULL)
    {
        /* A thread that exited between the enumeration snapshot and the open
         * is expected churn, not a capture failure; anything else is. */
        if (GetLastError() == ERROR_INVALID_PARAMETER)
        {
            RperfIncrementCaptureCounter(&Session->Counters.SkippedSamples);
            SetLastError(ERROR_SUCCESS);
            return TRUE;
        }
        RperfIncrementCaptureCounter(&Session->Counters.FailedSamples);
        RperfIncrementCaptureCounter(&Session->Counters.ThreadOpenFailures);
        SetLastError(ERROR_SUCCESS);
        return FALSE;
    }

    if (GetProcessIdOfThread(Thread) != Session->ProcessId)
    {
        /* The snapshot's thread id was reused by another process: churn. */
        RperfIncrementCaptureCounter(&Session->Counters.SkippedSamples);
        RperfIncrementCaptureCounter(&Session->Counters.ThreadOwnershipFailures);
        CloseHandle(Thread);
        SetLastError(ERROR_SUCCESS);
        return TRUE;
    }

    /* Balance invariant: this is the only suspend site, and the matching
     * ResumeThread below runs unconditionally before any return.  Between
     * the two calls the code only reads the counter, captures the context,
     * and walks the stack; stop, duration, and target-exit checks happen in
     * the sweep between threads, and application shutdown joins the worker,
     * so no path can leave a target thread suspended. */
    SuspendCount = SuspendThread(Thread);
    if (SuspendCount == (DWORD)-1)
    {
        RperfIncrementCaptureCounter(&Session->Counters.FailedSamples);
        RperfIncrementCaptureCounter(&Session->Counters.SuspendFailures);
        CloseHandle(Thread);
        SetLastError(ERROR_SUCCESS);
        return FALSE;
    }

    ZeroMemory(&Sample, sizeof(Sample));
    Sample.ProcessId = Session->ProcessId;
    Sample.ThreadId = ThreadId;

    /* The scheduler state comes from the sweep's enumeration snapshot, taken
     * before this thread was suspended; samples landing in a wait must not be
     * presented as CPU cost. */
    Sample.Flags = RPERF_SAMPLE_STATE_KNOWN;
    if (ThreadState == Waiting)
    {
        Sample.Flags |= RPERF_SAMPLE_WAITING;
        Sample.Flags |= (USHORT)((WaitReason & 0xFF) << RPERF_SAMPLE_WAIT_REASON_SHIFT);
    }

    QueryPerformanceCounter(&Counter);
    Sample.TimeUs = RperfCounterDeltaToUs(Counter.QuadPart -
                                          StartCounter.QuadPart,
                                          Frequency.QuadPart);
    Sample.Depth = RperfCaptureStack(Process,
                                     Thread,
                                     Sample.Frames,
                                     &Sample.Flags,
                                     &ContextCaptured);

    ResumeResult = ResumeThread(Thread);
    ResumeError = GetLastError();
    CloseHandle(Thread);

    if (ResumeResult == (DWORD)-1)
    {
        RperfIncrementCaptureCounter(&Session->Counters.FailedSamples);
        RperfIncrementCaptureCounter(&Session->Counters.ResumeFailures);
        SetLastError(ResumeError != ERROR_SUCCESS ?
                     ResumeError : ERROR_GEN_FAILURE);
        return FALSE;
    }

    if (Sample.Depth == 0)
    {
        RperfIncrementCaptureCounter(&Session->Counters.FailedSamples);
        RperfIncrementCaptureCounter(
            ContextCaptured ? &Session->Counters.UnwindFailures :
                              &Session->Counters.ContextFailures);
        SetLastError(ERROR_SUCCESS);
        return FALSE;
    }

    if (!RperfReserveSample(Session))
    {
        RperfIncrementCaptureCounter(&Session->Counters.FailedSamples);
        return FALSE;
    }

    for (Index = 0; Index < Sample.Depth; ++Index)
    {
        if (!RperfEnsureSymbol(Session, Process, Sample.Frames[Index], Log))
        {
            if (GetLastError() == ERROR_SUCCESS)
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            RperfIncrementCaptureCounter(&Session->Counters.FailedSamples);
            return FALSE;
        }
    }

    Result = RperfWriteSample(Log, &Sample);
    if (!Result)
    {
        RperfIncrementCaptureCounter(&Session->Counters.FailedSamples);
        SetLastError(ERROR_WRITE_FAULT);
        return FALSE;
    }

    Session->Samples[Session->SampleCount++] = Sample;
    RperfIncrementCaptureCounter(&Session->Counters.SuccessfulSamples);
    RperfAccountSampleState(Session, Sample.Flags);
    if (Sample.Flags & RPERF_SAMPLE_TRUNCATED)
    {
        Session->TruncatedStacks++;
        RperfIncrementCaptureCounter(&Session->Counters.TruncatedSamples);
    }
    if (Sample.Flags & RPERF_SAMPLE_UNWIND_FAILED)
        RperfIncrementCaptureCounter(&Session->Counters.UnwindFailures);
    return Result;
}

static RPERF_SWEEP_RESULT
RperfCaptureAllThreads(RPERF_SESSION *Session,
                       HANDLE Process,
                       LARGE_INTEGER StartCounter,
                       LARGE_INTEGER Frequency,
                       LONGLONG EndCounter,
                       FILE *Log)
{
    PBYTE Buffer = RperfQuerySystemProcesses();
    PSYSTEM_PROCESS_INFORMATION ProcessInfo;

    if (Buffer == NULL)
        return RperfSweepError;

    ProcessInfo = (PSYSTEM_PROCESS_INFORMATION)Buffer;
    for (;;)
    {
        if ((DWORD)(ULONG_PTR)ProcessInfo->UniqueProcessId ==
            Session->ProcessId)
        {
            PSYSTEM_THREAD_INFORMATION Threads =
                (PSYSTEM_THREAD_INFORMATION)(ProcessInfo + 1);
            ULONG Index;

            for (Index = 0; Index < ProcessInfo->NumberOfThreads; ++Index)
            {
                LARGE_INTEGER Counter;
                DWORD ThreadId;

                if (WaitForSingleObject(Session->StopEvent, 0) == WAIT_OBJECT_0)
                {
                    HeapFree(GetProcessHeap(), 0, Buffer);
                    return RperfSweepStopped;
                }
                if (WaitForSingleObject(Process, 0) == WAIT_OBJECT_0)
                {
                    HeapFree(GetProcessHeap(), 0, Buffer);
                    return RperfSweepTargetExit;
                }
                QueryPerformanceCounter(&Counter);
                if (EndCounter != 0 && Counter.QuadPart >= EndCounter)
                {
                    HeapFree(GetProcessHeap(), 0, Buffer);
                    return RperfSweepDuration;
                }

                ThreadId = (DWORD)(ULONG_PTR)
                    Threads[Index].ClientId.UniqueThread;
                SetLastError(ERROR_SUCCESS);
                if (!RperfCaptureThread(Session, Process, ThreadId, Threads[Index].ThreadState, Threads[Index].WaitReason, StartCounter, Frequency, Log))
                {
                    if (GetLastError() != ERROR_SUCCESS)
                    {
                        HeapFree(GetProcessHeap(), 0, Buffer);
                        return RperfSweepError;
                    }
                    Session->MissedThreads++;
                }
            }

            HeapFree(GetProcessHeap(), 0, Buffer);
            return RperfSweepContinue;
        }

        if (ProcessInfo->NextEntryOffset == 0)
            break;
        ProcessInfo = (PSYSTEM_PROCESS_INFORMATION)
            ((PBYTE)ProcessInfo + ProcessInfo->NextEntryOffset);
    }

    HeapFree(GetProcessHeap(), 0, Buffer);
    return RperfSweepTargetExit;
}

static VOID
RperfGetProcessTimes(HANDLE Process,
                     ULONGLONG *UserTime,
                     ULONGLONG *KernelTime)
{
    FILETIME CreationTime, ExitTime, Kernel, User;
    ULARGE_INTEGER Value;

    *UserTime = 0;
    *KernelTime = 0;
    if (!GetProcessTimes(Process,
                         &CreationTime,
                         &ExitTime,
                         &Kernel,
                         &User))
    {
        return;
    }

    Value.LowPart = User.dwLowDateTime;
    Value.HighPart = User.dwHighDateTime;
    *UserTime = Value.QuadPart;
    Value.LowPart = Kernel.dwLowDateTime;
    Value.HighPart = Kernel.dwHighDateTime;
    *KernelTime = Value.QuadPart;
}

static BOOL
RperfWriteHeader(FILE *Log,
                 const RPERF_SESSION *Session)
{
    CHAR ProcessName[MAX_PATH * 3];

    if (!WideCharToMultiByte(CP_UTF8,
                             0,
                             Session->ProcessName,
                             -1,
                             ProcessName,
                             ARRAYSIZE(ProcessName),
                             NULL,
                             NULL))
    {
        strcpy(ProcessName, "<unknown>");
    }
    RperfCopyLogField(ProcessName, ARRAYSIZE(ProcessName), ProcessName);

    return fprintf(Log, "RPERF\t%d\n", RPERF_LOG_VERSION) >= 0 &&
           fprintf(Log, "p\t%lu\t%s\n", Session->ProcessId, ProcessName) >= 0 &&
           fprintf(Log,
                   "c\t%lu\t%lu\twall-clock-all-threads\n",
                   Session->IntervalMs,
                   Session->RequestedDurationMs) >= 0;
}

static DWORD WINAPI
RperfCaptureThreadProc(PVOID Parameter)
{
    RPERF_CAPTURE_CONTEXT *Context = Parameter;
    RPERF_SESSION *Session = Context->Session;
    HANDLE Process = NULL;
    FILE *Log = NULL;
    LARGE_INTEGER Frequency, StartCounter, Counter, NextCounter;
    ULONGLONG InitialUser = 0, InitialKernel = 0;
    ULONGLONG FinalUser = 0, FinalKernel = 0;
    LONGLONG IntervalTicks, DurationTicks, EndCounter = 0;
    BOOL SymbolsInitialized = FALSE;
    BOOL SelfWow64 = FALSE, TargetWow64 = FALSE;
    DWORD Error = ERROR_SUCCESS;
    DWORD LastProgressTick = 0;
    DWORD LastModuleRefreshTick = 0;
    HANDLE WaitHandles[2];

    HeapFree(GetProcessHeap(), 0, Context);

    Process = OpenProcess(PROCESS_QUERY_INFORMATION |
                          PROCESS_VM_READ |
                          SYNCHRONIZE,
                          FALSE,
                          Session->ProcessId);
    if (Process == NULL)
    {
        Error = GetLastError();
        Session->CompletionReason = RperfCompletionError;
        goto Cleanup;
    }

    if (IsWow64Process(GetCurrentProcess(), &SelfWow64) &&
        IsWow64Process(Process, &TargetWow64) &&
        SelfWow64 != TargetWow64)
    {
        Session->CrossBitnessTargetBits = TargetWow64 ? 32 : 64;
        Error = ERROR_NOT_SUPPORTED;
        Session->CompletionReason = RperfCompletionError;
        goto Cleanup;
    }

    Log = _wfopen(Session->SourcePath, L"wt");
    if (Log == NULL)
    {
        Error = ERROR_OPEN_FAILED;
        Session->CompletionReason = RperfCompletionError;
        goto Cleanup;
    }

    if (!QueryPerformanceFrequency(&Frequency) || Frequency.QuadPart <= 0 ||
        !QueryPerformanceCounter(&StartCounter) || StartCounter.QuadPart < 0)
    {
        Error = ERROR_NOT_SUPPORTED;
        Session->CompletionReason = RperfCompletionError;
        goto Cleanup;
    }
    NextCounter = StartCounter;
    if (!RperfMillisecondsToCounterTicks(Session->IntervalMs,
                                         Frequency.QuadPart,
                                         &IntervalTicks))
    {
        Error = ERROR_ARITHMETIC_OVERFLOW;
        Session->CompletionReason = RperfCompletionError;
        goto Cleanup;
    }
    if (IntervalTicks < 1)
        IntervalTicks = 1;
    if (Session->RequestedDurationMs != 0)
    {
        if (!RperfMillisecondsToCounterTicks(Session->RequestedDurationMs,
                                             Frequency.QuadPart,
                                             &DurationTicks) ||
            DurationTicks > 0x7fffffffffffffffLL - StartCounter.QuadPart)
        {
            Error = ERROR_ARITHMETIC_OVERFLOW;
            Session->CompletionReason = RperfCompletionError;
            goto Cleanup;
        }
        EndCounter = StartCounter.QuadPart + DurationTicks;
    }

    if (!RperfWriteHeader(Log, Session))
    {
        Error = ERROR_WRITE_FAULT;
        Session->CompletionReason = RperfCompletionError;
        goto Cleanup;
    }

    RperfGetProcessTimes(Process, &InitialUser, &InitialKernel);

    SymSetOptions(SymGetOptions() |
                  SYMOPT_UNDNAME |
                  SYMOPT_AUTO_PUBLICS |
                  SYMOPT_DEFERRED_LOADS);
    SymbolsInitialized = SymInitialize(Process, NULL, FALSE);
    if (!SymbolsInitialized)
    {
        Error = GetLastError();
        if (Error == ERROR_SUCCESS)
            Error = ERROR_DLL_INIT_FAILED;
        Session->CompletionReason = RperfCompletionError;
        goto Cleanup;
    }
    /*
     * A newly launched target can still be suspended in its loader while the
     * capture worker starts.  Module discovery is useful for unwinding and
     * symbols, but it must not make address sampling fail.  Resolution also
     * retries on demand and the periodic refresh below fills the catalog once
     * the loader becomes observable.
     */
    RperfSynchronizeModules(Session, Process);
    SetLastError(ERROR_SUCCESS);
    LastModuleRefreshTick = GetTickCount();
    WaitHandles[0] = Session->StopEvent;
    WaitHandles[1] = Process;

    for (;;)
    {
        DWORD WaitResult;
        DWORD WaitMilliseconds;
        LONGLONG Remaining;
        RPERF_SWEEP_RESULT SweepResult;

        if (WaitForSingleObject(Session->StopEvent, 0) == WAIT_OBJECT_0)
        {
            Session->CompletionReason = RperfCompletionUserStop;
            break;
        }
        if (WaitForSingleObject(Process, 0) == WAIT_OBJECT_0)
        {
            Session->CompletionReason = RperfCompletionTargetExit;
            break;
        }

        QueryPerformanceCounter(&Counter);
        Session->ElapsedUs = RperfCounterDeltaToUs(Counter.QuadPart -
                                                   StartCounter.QuadPart,
                                                   Frequency.QuadPart);
        if (EndCounter != 0 && Counter.QuadPart >= EndCounter)
        {
            Session->CompletionReason = RperfCompletionDuration;
            break;
        }

        if (GetTickCount() - LastModuleRefreshTick >=
            RPERF_MODULE_REFRESH_MS)
        {
            RperfSynchronizeModules(Session, Process);
            LastModuleRefreshTick = GetTickCount();
        }

        SweepResult = RperfCaptureAllThreads(Session,
                                             Process,
                                             StartCounter,
                                             Frequency,
                                             EndCounter,
                                             Log);
        if (SweepResult != RperfSweepContinue)
        {
            if (SweepResult == RperfSweepStopped)
                Session->CompletionReason = RperfCompletionUserStop;
            else if (SweepResult == RperfSweepTargetExit)
                Session->CompletionReason = RperfCompletionTargetExit;
            else if (SweepResult == RperfSweepDuration)
                Session->CompletionReason = RperfCompletionDuration;
            else
            {
                Error = GetLastError();
                if (Error == ERROR_SUCCESS)
                    Error = ERROR_GEN_FAILURE;
                Session->CompletionReason = RperfCompletionError;
            }
            break;
        }

        if (GetTickCount() - LastProgressTick >= 500)
        {
            PostMessageW(Session->NotifyWindow,
                         WM_RPERF_CAPTURE_PROGRESS,
                         (WPARAM)Session->SampleCount,
                         (LPARAM)Session->MissedThreads);
            LastProgressTick = GetTickCount();
            fflush(Log);
        }

        NextCounter.QuadPart += IntervalTicks;
        QueryPerformanceCounter(&Counter);
        Session->ElapsedUs = RperfCounterDeltaToUs(Counter.QuadPart -
                                                   StartCounter.QuadPart,
                                                   Frequency.QuadPart);
        if (EndCounter != 0 && Counter.QuadPart >= EndCounter)
        {
            Session->CompletionReason = RperfCompletionDuration;
            break;
        }
        while (NextCounter.QuadPart <= Counter.QuadPart)
        {
            ULONGLONG Missed = (ULONGLONG)
                ((Counter.QuadPart - NextCounter.QuadPart) / IntervalTicks) + 1;

            if (Missed > (ULONGLONG)-1 - Session->MissedTicks)
                Session->MissedTicks = (ULONGLONG)-1;
            else
                Session->MissedTicks += Missed;
            if (Missed > (ULONGLONG)
                ((0x7fffffffffffffffLL - NextCounter.QuadPart) /
                 IntervalTicks))
            {
                NextCounter.QuadPart = 0x7fffffffffffffffLL;
            }
            else
            {
                NextCounter.QuadPart += (LONGLONG)Missed * IntervalTicks;
            }
        }

        Remaining = NextCounter.QuadPart - Counter.QuadPart;
        WaitMilliseconds = (DWORD)
            ((Remaining * 1000 + Frequency.QuadPart - 1) /
             Frequency.QuadPart);
        if (WaitMilliseconds == 0)
            WaitMilliseconds = 1;

        if (EndCounter != 0)
        {
            LONGLONG UntilEnd = EndCounter - Counter.QuadPart;
            DWORD EndWait = (DWORD)
                ((UntilEnd * 1000 + Frequency.QuadPart - 1) /
                 Frequency.QuadPart);
            if (EndWait < WaitMilliseconds)
                WaitMilliseconds = EndWait;
        }

        WaitResult = WaitForMultipleObjects(ARRAYSIZE(WaitHandles),
                                            WaitHandles,
                                            FALSE,
                                            WaitMilliseconds);
        if (WaitResult == WAIT_OBJECT_0)
        {
            Session->CompletionReason = RperfCompletionUserStop;
            break;
        }
        if (WaitResult == WAIT_OBJECT_0 + 1)
        {
            Session->CompletionReason = RperfCompletionTargetExit;
            break;
        }
        if (WaitResult == WAIT_FAILED)
        {
            Error = GetLastError();
            if (Error == ERROR_SUCCESS)
                Error = ERROR_GEN_FAILURE;
            Session->CompletionReason = RperfCompletionError;
            break;
        }
    }

    QueryPerformanceCounter(&Counter);
    Session->ElapsedUs = RperfCounterDeltaToUs(Counter.QuadPart -
                                               StartCounter.QuadPart,
                                               Frequency.QuadPart);
    RperfGetProcessTimes(Process, &FinalUser, &FinalKernel);
    Session->UserTime100ns = (FinalUser >= InitialUser) ?
                             FinalUser - InitialUser : 0;
    Session->KernelTime100ns = (FinalKernel >= InitialKernel) ?
                               FinalKernel - InitialKernel : 0;

    if (Session->SampleCount == 0 && Error == ERROR_SUCCESS &&
        Session->MissedThreads != 0 &&
        Session->CompletionReason == RperfCompletionDuration)
    {
        Error = ERROR_ACCESS_DENIED;
        Session->CompletionReason = RperfCompletionError;
    }

Cleanup:
    Session->Counters.MissedCadenceTicks = Session->MissedTicks;
    Session->Counters.LostRecords = Session->LostSamples;
    if (Log != NULL)
    {
        if (Session->CompletionReason == RperfCompletionIncomplete)
            Session->CompletionReason = Error == ERROR_SUCCESS ?
                RperfCompletionUserStop : RperfCompletionError;
        if (fprintf(Log,
                    "e\t%I64u\t%I64u\t%I64u\t%I64u\t%I64u\t%I64u\t%I64u\t%lu\t%lu\n",
                    Session->ElapsedUs,
                    Session->UserTime100ns,
                    Session->KernelTime100ns,
                    Session->MissedThreads,
                    Session->MissedTicks,
                    Session->TruncatedStacks,
                    Session->LostSamples,
                    (DWORD)Session->CompletionReason,
                    Error) >= 0)
        {
            Session->LogComplete = TRUE;
        }
        else if (Error == ERROR_SUCCESS)
        {
            Error = ERROR_WRITE_FAULT;
        }
        if (fclose(Log) != 0)
        {
            Session->LogComplete = FALSE;
            if (Error == ERROR_SUCCESS)
                Error = ERROR_WRITE_FAULT;
        }
    }
    if (SymbolsInitialized)
        SymCleanup(Process);
    if (Process != NULL)
        CloseHandle(Process);

    if (Session->SampleCount != 0 && !RperfBuildAnalysis(Session) &&
        Error == ERROR_SUCCESS)
    {
        Error = GetLastError();
        if (Error == ERROR_SUCCESS)
            Error = ERROR_NOT_ENOUGH_MEMORY;
    }

    Session->CaptureError = Error;
    InterlockedExchange(&Session->Capturing, FALSE);
    PostMessageW(Session->NotifyWindow,
                 WM_RPERF_CAPTURE_DONE,
                 Error,
                 0);
    return Error;
}

BOOL
RperfCaptureStart(RPERF_SESSION *Session,
                  DWORD ProcessId,
                  PCWSTR ProcessName,
                  DWORD IntervalMs,
                  DWORD DurationMs,
                  PCWSTR LogPath,
                  HWND NotifyWindow)
{
    RPERF_CAPTURE_CONTEXT *Context;

    if (Session == NULL || ProcessId == 0 ||
        ProcessId == GetCurrentProcessId() ||
        IntervalMs == 0 || IntervalMs > 1000 ||
        DurationMs > 86400000 ||
        LogPath == NULL || *LogPath == UNICODE_NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    RperfSessionClear(Session);
    Session->ProcessId = ProcessId;
    Session->Backend = RperfBackendIntrusive;
    Session->IntervalMs = IntervalMs;
    Session->RequestedDurationMs = DurationMs;
    Session->NotifyWindow = NotifyWindow;
    lstrcpynW(Session->ProcessName,
              ProcessName != NULL ? ProcessName : L"<unknown>",
              ARRAYSIZE(Session->ProcessName));
    lstrcpynW(Session->SourcePath,
              LogPath,
              ARRAYSIZE(Session->SourcePath));

    Session->StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (Session->StopEvent == NULL)
        return FALSE;

    Context = HeapAlloc(GetProcessHeap(), 0, sizeof(*Context));
    if (Context == NULL)
    {
        CloseHandle(Session->StopEvent);
        Session->StopEvent = NULL;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    Context->Session = Session;
    InterlockedExchange(&Session->Capturing, TRUE);
    Session->WorkerThread = CreateThread(NULL,
                                         0,
                                         RperfCaptureThreadProc,
                                         Context,
                                         0,
                                         NULL);
    if (Session->WorkerThread == NULL)
    {
        DWORD Error = GetLastError();
        InterlockedExchange(&Session->Capturing, FALSE);
        HeapFree(GetProcessHeap(), 0, Context);
        CloseHandle(Session->StopEvent);
        Session->StopEvent = NULL;
        SetLastError(Error);
        return FALSE;
    }

    return TRUE;
}

static BOOL
RperfReserveNode(RPERF_SESSION *Session,
                 DWORD64 Address,
                 ULONG Parent,
                 USHORT Depth,
                 PULONG NodeIndex)
{
    RPERF_NODE *Node;

    if (Session->NodeCount >= RPERF_MAX_NODES ||
        Session->NodeCount >= (SIZE_T)(ULONG)-1)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }

    if (!RperfResizeArray((PVOID *)&Session->Nodes,
                          sizeof(*Session->Nodes),
                          &Session->NodeCapacity,
                          Session->NodeCount + 1))
    {
        return FALSE;
    }

    *NodeIndex = (ULONG)Session->NodeCount++;
    Node = &Session->Nodes[*NodeIndex];
    ZeroMemory(Node, sizeof(*Node));
    Node->Address = Address;
    Node->Parent = Parent;
    Node->FirstChild = RPERF_INVALID_NODE;
    Node->NextSibling = RPERF_INVALID_NODE;
    Node->Depth = Depth;
    return TRUE;
}

static SIZE_T
RperfHashNodeEdge(ULONG Parent,
                  DWORD64 Address)
{
    DWORD64 Key = Address ^ ((DWORD64)Parent * 0x9e3779b97f4a7c15ULL);
    return RperfHashAddress(Key);
}

static BOOL
RperfRebuildNodeHash(RPERF_SESSION *Session,
                     SIZE_T RequiredNodeCount)
{
    SIZE_T Capacity = 512;
    SIZE_T *Hash;
    SIZE_T Index;

    while (Capacity < RequiredNodeCount * 2)
    {
        if (Capacity > ((SIZE_T)-1) / 2)
        {
            SetLastError(ERROR_ARITHMETIC_OVERFLOW);
            return FALSE;
        }
        Capacity *= 2;
    }
    if (Capacity > ((SIZE_T)-1) / sizeof(*Hash))
    {
        SetLastError(ERROR_ARITHMETIC_OVERFLOW);
        return FALSE;
    }
    Hash = HeapAlloc(GetProcessHeap(),
                     HEAP_ZERO_MEMORY,
                     Capacity * sizeof(*Hash));
    if (Hash == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    for (Index = 1; Index < Session->NodeCount; ++Index)
    {
        const RPERF_NODE *Node = &Session->Nodes[Index];
        SIZE_T Slot = RperfHashNodeEdge(Node->Parent, Node->Address) &
                      (Capacity - 1);

        while (Hash[Slot] != 0)
            Slot = (Slot + 1) & (Capacity - 1);
        Hash[Slot] = Index + 1;
    }

    if (Session->NodeHash != NULL)
        HeapFree(GetProcessHeap(), 0, Session->NodeHash);
    Session->NodeHash = Hash;
    Session->NodeHashCapacity = Capacity;
    return TRUE;
}

static BOOL
RperfFindOrCreateChild(RPERF_SESSION *Session,
                       ULONG ParentIndex,
                       DWORD64 Address,
                       PULONG ChildIndex)
{
    RPERF_NODE *Parent = &Session->Nodes[ParentIndex];
    SIZE_T Slot;

    if (Session->NodeHashCapacity == 0 ||
        Session->NodeCount + 1 > Session->NodeHashCapacity / 2)
    {
        if (!RperfRebuildNodeHash(Session, Session->NodeCount + 1))
            return FALSE;
    }

    Slot = RperfHashNodeEdge(ParentIndex, Address) &
           (Session->NodeHashCapacity - 1);
    while (Session->NodeHash[Slot] != 0)
    {
        SIZE_T Index = Session->NodeHash[Slot] - 1;
        const RPERF_NODE *Node = &Session->Nodes[Index];

        if (Node->Parent == ParentIndex && Node->Address == Address)
        {
            *ChildIndex = (ULONG)Index;
            return TRUE;
        }
        Slot = (Slot + 1) & (Session->NodeHashCapacity - 1);
    }

    if (!RperfReserveNode(Session,
                          Address,
                          ParentIndex,
                          Parent->Depth + 1,
                          ChildIndex))
    {
        return FALSE;
    }

    Parent = &Session->Nodes[ParentIndex];
    Session->Nodes[*ChildIndex].NextSibling = Parent->FirstChild;
    Parent->FirstChild = *ChildIndex;
    Session->NodeHash[Slot] = (SIZE_T)*ChildIndex + 1;
    return TRUE;
}

static int __cdecl
RperfCompareSymbolsByAddress(const void *Left,
                             const void *Right)
{
    const RPERF_SYMBOL *LeftSymbol = Left;
    const RPERF_SYMBOL *RightSymbol = Right;

    if (LeftSymbol->Address < RightSymbol->Address)
        return -1;
    if (LeftSymbol->Address > RightSymbol->Address)
        return 1;
    return 0;
}

static BOOL
RperfSortSymbols(RPERF_SESSION *Session)
{
    if (Session->SymbolCount == 0)
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    if (!Session->SymbolsSorted)
    {
        qsort(Session->Symbols,
              Session->SymbolCount,
              sizeof(*Session->Symbols),
              RperfCompareSymbolsByAddress);
        Session->SymbolsSorted = TRUE;
        if (Session->SymbolHash != NULL)
        {
            HeapFree(GetProcessHeap(), 0, Session->SymbolHash);
            Session->SymbolHash = NULL;
            Session->SymbolHashCapacity = 0;
        }
        Session->FunctionIndexValid = FALSE;
    }
    return TRUE;
}

static int __cdecl
RperfCompareFunctionIndexEntries(const void *Left,
                                 const void *Right)
{
    const RPERF_FUNCTION_INDEX_ENTRY *LeftEntry = Left;
    const RPERF_FUNCTION_INDEX_ENTRY *RightEntry = Right;

    if (LeftEntry->FunctionAddress < RightEntry->FunctionAddress)
        return -1;
    if (LeftEntry->FunctionAddress > RightEntry->FunctionAddress)
        return 1;
    if (LeftEntry->RawAddress < RightEntry->RawAddress)
        return -1;
    if (LeftEntry->RawAddress > RightEntry->RawAddress)
        return 1;
    return 0;
}

static BOOL
RperfBuildFunctionIndex(RPERF_SESSION *Session)
{
    RPERF_FUNCTION_INDEX_ENTRY *Entries;
    SIZE_T Index, GroupStart;
    SIZE_T *FunctionSymbols;

    if (Session->FunctionIndexValid)
        return TRUE;
    if (Session->SymbolCount == 0 ||
        Session->SymbolCount > ((SIZE_T)-1) / sizeof(*Entries) ||
        Session->SymbolCount > ((SIZE_T)-1) / sizeof(*FunctionSymbols))
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }

    Entries = HeapAlloc(GetProcessHeap(),
                        0,
                        Session->SymbolCount * sizeof(*Entries));
    FunctionSymbols = HeapAlloc(GetProcessHeap(),
                                0,
                                Session->SymbolCount * sizeof(*FunctionSymbols));
    if (Entries == NULL || FunctionSymbols == NULL)
    {
        if (Entries != NULL)
            HeapFree(GetProcessHeap(), 0, Entries);
        if (FunctionSymbols != NULL)
            HeapFree(GetProcessHeap(), 0, FunctionSymbols);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    for (Index = 0; Index < Session->SymbolCount; ++Index)
    {
        Entries[Index].FunctionAddress =
            Session->Symbols[Index].FunctionAddress;
        Entries[Index].RawAddress = Session->Symbols[Index].Address;
        Entries[Index].SymbolIndex = Index;
    }
    qsort(Entries,
          Session->SymbolCount,
          sizeof(*Entries),
          RperfCompareFunctionIndexEntries);

    Session->FunctionCount = 0;
    GroupStart = 0;
    while (GroupStart < Session->SymbolCount)
    {
        SIZE_T GroupEnd = GroupStart + 1;
        SIZE_T Representative = Entries[GroupStart].SymbolIndex;

        while (GroupEnd < Session->SymbolCount &&
               Entries[GroupEnd].FunctionAddress ==
                   Entries[GroupStart].FunctionAddress)
        {
            GroupEnd++;
        }
        FunctionSymbols[Session->FunctionCount++] = Representative;
        for (Index = GroupStart; Index < GroupEnd; ++Index)
            Session->Symbols[Entries[Index].SymbolIndex].AggregateIndex =
                Representative;
        GroupStart = GroupEnd;
    }

    if (Session->FunctionSymbols != NULL)
        HeapFree(GetProcessHeap(), 0, Session->FunctionSymbols);
    Session->FunctionSymbols = FunctionSymbols;
    Session->FunctionIndexValid = TRUE;
    HeapFree(GetProcessHeap(), 0, Entries);
    return TRUE;
}

static BOOL
RperfAnalysisContinue(HANDLE CancelEvent)
{
    DWORD Wait;

    if (CancelEvent == NULL)
        return TRUE;
    Wait = WaitForSingleObject(CancelEvent, 0);
    if (Wait == WAIT_TIMEOUT)
        return TRUE;
    if (Wait == WAIT_OBJECT_0)
        SetLastError(ERROR_CANCELLED);
    else if (Wait != WAIT_FAILED)
        SetLastError(ERROR_GEN_FAILURE);
    return FALSE;
}

BOOL
RperfBuildFilteredAnalysisEx(RPERF_SESSION *Session,
                             DWORD ThreadId,
                             ULONGLONG StartUs,
                             ULONGLONG EndUs,
                             DWORD FilterFlags,
                             HANDLE CancelEvent,
                             RPERF_SESSION_PROGRESS Progress,
                             PVOID ProgressContext)
{
    SIZE_T SampleIndex, SymbolIndex;
    ULONG RootIndex;
    ULONGLONG ProgressTotal;

    if (Session == NULL || StartUs > Session->ElapsedUs ||
        (EndUs != (ULONGLONG)-1 &&
         (StartUs > EndUs || EndUs > Session->ElapsedUs)))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if ((FilterFlags & ~(DWORD)RPERF_FILTER_VALID) != 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (Session->SampleCount > ((ULONGLONG)-1) / 2)
    {
        SetLastError(ERROR_ARITHMETIC_OVERFLOW);
        return FALSE;
    }
    ProgressTotal = (ULONGLONG)Session->SampleCount * 2;
    if (!RperfAnalysisContinue(CancelEvent))
        return FALSE;
    if (Progress != NULL)
        Progress(ProgressContext, 0, ProgressTotal);

    for (SampleIndex = 0; SampleIndex < Session->SampleCount; ++SampleIndex)
    {
        const RPERF_SAMPLE *Sample = &Session->Samples[SampleIndex];
        USHORT FrameIndex;

        if ((SampleIndex & 0xff) == 0)
        {
            if (!RperfAnalysisContinue(CancelEvent))
                return FALSE;
            if (Progress != NULL && (SampleIndex & 0x3fff) == 0)
                Progress(ProgressContext, SampleIndex, ProgressTotal);
        }

        for (FrameIndex = 0; FrameIndex < Sample->Depth; ++FrameIndex)
        {
            if (!RperfEnsureSymbol(Session,
                                   NULL,
                                   Sample->Frames[FrameIndex],
                                   NULL))
            {
                return FALSE;
            }
        }
    }
    if (!RperfAnalysisContinue(CancelEvent))
        return FALSE;
    if (!RperfSortSymbols(Session))
        return FALSE;
    if (!RperfAnalysisContinue(CancelEvent))
        return FALSE;
    if (!RperfBuildFunctionIndex(Session))
        return FALSE;
    if (!RperfAnalysisContinue(CancelEvent))
        return FALSE;
    if (Progress != NULL)
        Progress(ProgressContext, Session->SampleCount, ProgressTotal);

    if (Session->Nodes != NULL)
    {
        HeapFree(GetProcessHeap(), 0, Session->Nodes);
        Session->Nodes = NULL;
    }
    if (Session->NodeHash != NULL)
    {
        HeapFree(GetProcessHeap(), 0, Session->NodeHash);
        Session->NodeHash = NULL;
    }
    Session->NodeCount = 0;
    Session->NodeCapacity = 0;
    Session->NodeHashCapacity = 0;
    Session->FilterThreadId = ThreadId;
    Session->FilterStartUs = StartUs;
    Session->FilterEndUs = (EndUs == (ULONGLONG)-1) ?
                           Session->ElapsedUs : EndUs;
    Session->FilterFlags = FilterFlags;
    Session->FilteredSampleCount = 0;

    for (SymbolIndex = 0; SymbolIndex < Session->SymbolCount; ++SymbolIndex)
    {
        Session->Symbols[SymbolIndex].Inclusive = 0;
        Session->Symbols[SymbolIndex].Exclusive = 0;
    }

    if (!RperfReserveNode(Session,
                          0,
                          RPERF_INVALID_NODE,
                          0,
                          &RootIndex))
    {
        return FALSE;
    }

    for (SampleIndex = 0; SampleIndex < Session->SampleCount; ++SampleIndex)
    {
        const RPERF_SAMPLE *Sample = &Session->Samples[SampleIndex];
        ULONG Parent = RootIndex;
        USHORT FrameIndex;
        DWORD64 CanonicalFrames[RPERF_MAX_FRAMES];

        if ((SampleIndex & 0xff) == 0)
        {
            if (!RperfAnalysisContinue(CancelEvent))
                return FALSE;
            if (Progress != NULL && (SampleIndex & 0x3fff) == 0)
            {
                Progress(ProgressContext,
                         (ULONGLONG)Session->SampleCount + SampleIndex,
                         ProgressTotal);
            }
        }

        if (Sample->Depth == 0 ||
            (ThreadId != 0 && Sample->ThreadId != ThreadId) ||
            Sample->TimeUs < StartUs ||
            (EndUs != (ULONGLONG)-1 && Sample->TimeUs > EndUs))
        {
            continue;
        }
        if ((FilterFlags & RPERF_FILTER_CPU_ONLY) != 0 && (Sample->Flags & RPERF_SAMPLE_WAITING) != 0)
            continue;

        Session->Nodes[RootIndex].Count++;
        Session->FilteredSampleCount++;
        for (FrameIndex = 0; FrameIndex < Sample->Depth; ++FrameIndex)
        {
            BOOL AlreadySeen = FALSE;
            USHORT Previous;
            BOOL Found;
            SIZE_T Index, AggregateIndex;

            Index = RperfSymbolLowerBound(Session,
                                           Sample->Frames[FrameIndex],
                                           &Found);
            if (!Found)
            {
                SetLastError(ERROR_INVALID_DATA);
                return FALSE;
            }
            CanonicalFrames[FrameIndex] =
                Session->Symbols[Index].FunctionAddress;
            AggregateIndex = Session->Symbols[Index].AggregateIndex;
            if (AggregateIndex >= Session->SymbolCount)
            {
                SetLastError(ERROR_INVALID_DATA);
                return FALSE;
            }

            for (Previous = 0; Previous < FrameIndex; ++Previous)
            {
                if (CanonicalFrames[Previous] == CanonicalFrames[FrameIndex])
                {
                    AlreadySeen = TRUE;
                    break;
                }
            }

            if (!AlreadySeen)
                Session->Symbols[AggregateIndex].Inclusive++;
            if (FrameIndex == 0)
                Session->Symbols[AggregateIndex].Exclusive++;
        }

        FrameIndex = Sample->Depth;
        while (FrameIndex-- != 0)
        {
            ULONG Child;
            if (!RperfFindOrCreateChild(Session,
                                        Parent,
                                        CanonicalFrames[FrameIndex],
                                        &Child))
            {
                return FALSE;
            }
            Session->Nodes[Child].Count++;
            Parent = Child;
        }
    }

    if (Session->NodeHash != NULL)
    {
        HeapFree(GetProcessHeap(), 0, Session->NodeHash);
        Session->NodeHash = NULL;
        Session->NodeHashCapacity = 0;
    }
    if (!RperfAnalysisContinue(CancelEvent))
        return FALSE;
    if (Progress != NULL)
        Progress(ProgressContext, ProgressTotal, ProgressTotal);
    return TRUE;
}

BOOL
RperfBuildFilteredAnalysis(RPERF_SESSION *Session,
                           DWORD ThreadId,
                           ULONGLONG StartUs,
                           ULONGLONG EndUs,
                           DWORD FilterFlags)
{
    return RperfBuildFilteredAnalysisEx(Session, ThreadId, StartUs, EndUs, FilterFlags, NULL, NULL, NULL);
}

BOOL
RperfBuildAnalysis(RPERF_SESSION *Session)
{
    return RperfBuildFilteredAnalysis(Session, 0, 0, (ULONGLONG)-1, 0);
}

static PCSTR
RperfBackendModeString(RPERF_BACKEND_KIND Backend)
{
    if (Backend == RperfBackendKernel) return "kernel-on-cpu-samples";
    if (Backend == RperfBackendEtw) return "etw-sampled-profile";
    if (Backend == RperfBackendFake) return "synthetic-contract-test";
    return "wall-clock-all-threads";
}

static VOID
RperfExportSymbolName(const RPERF_SESSION *Session, DWORD64 Address, PSTR Buffer, SIZE_T BufferCount)
{
    WCHAR Wide[192];
    SIZE_T Index;

    RperfFormatSymbol(Session, Address, Wide, ARRAYSIZE(Wide));
    for (Index = 0; Index + 1 < BufferCount && Wide[Index] != UNICODE_NULL; Index++)
    {
        WCHAR Value = Wide[Index];
        /* The folded format reserves the separator characters. */
        if (Value == L';') Value = L':';
        if (Value == L' ') Value = L'_';
        Buffer[Index] = (Value < 0x20 || Value > 0x7e) ? '?' : (CHAR)Value;
    }
    Buffer[Index] = ANSI_NULL;
}

static BOOL
RperfExportFoldedNodes(FILE *File, const RPERF_SESSION *Session, ULONG Parent, PSTR Path, SIZE_T Length, SIZE_T Capacity)
{
    ULONG Child;

    for (Child = Session->Nodes[Parent].FirstChild; Child != RPERF_INVALID_NODE; Child = Session->Nodes[Child].NextSibling)
    {
        CHAR Name[192];
        ULONGLONG Residual = Session->Nodes[Child].Count;
        SIZE_T NameLength, NewLength;
        ULONG Grandchild;

        for (Grandchild = Session->Nodes[Child].FirstChild; Grandchild != RPERF_INVALID_NODE; Grandchild = Session->Nodes[Grandchild].NextSibling)
        {
            Residual -= min(Residual, Session->Nodes[Grandchild].Count);
        }
        RperfExportSymbolName(Session, Session->Nodes[Child].Address, Name, ARRAYSIZE(Name));
        NameLength = strlen(Name);
        if (Length + NameLength + 2 > Capacity)
        {
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return FALSE;
        }
        NewLength = Length;
        if (NewLength != 0) Path[NewLength++] = ';';
        CopyMemory(Path + NewLength, Name, NameLength);
        NewLength += NameLength;
        Path[NewLength] = ANSI_NULL;
        if (Residual != 0 && fprintf(File, "%s %I64u\n", Path, Residual) < 0) return FALSE;
        if (!RperfExportFoldedNodes(File, Session, Child, Path, NewLength, Capacity)) return FALSE;
    }
    return TRUE;
}

BOOL
RperfExportSessionView(const RPERF_SESSION *Session, PCWSTR Path)
{
    FILE *File;
    PSTR Folded;
    const SIZE_T FoldedCapacity = (SIZE_T)RPERF_MAX_FRAMES * 200;
    BOOL Result = FALSE;

    if (Session == NULL || Path == NULL || Session->SampleCount == 0 || Session->Nodes == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    File = _wfopen(Path, L"wt");
    if (File == NULL)
    {
        SetLastError(ERROR_OPEN_FAILED);
        return FALSE;
    }
    Folded = HeapAlloc(GetProcessHeap(), 0, FoldedCapacity);
    if (Folded == NULL)
    {
        fclose(File);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    /* The header states the population and its caveats so a shared export
     * cannot be mistaken for the complete session. */
    if (fprintf(File, "# rosprofiler filtered-view export v1\n") < 0) goto Cleanup;
    if (fprintf(File, "# target: %S (pid %lu)\n", Session->ProcessName, Session->ProcessId) < 0) goto Cleanup;
    if (fprintf(File, "# mode: %s, interval %lu ms\n", RperfBackendModeString(Session->Backend), Session->IntervalMs) < 0) goto Cleanup;
    if (fprintf(File, "# completion: reason %d, complete-footer %s, error %lu\n", (int)Session->CompletionReason, Session->LogComplete ? "yes" : "no", Session->CaptureError) < 0) goto Cleanup;
    if (fprintf(File, "# view: thread %lu (0 means all), time %I64u..%I64u us, scope %s\n", Session->FilterThreadId, Session->FilterStartUs, Session->FilterEndUs, (Session->FilterFlags & RPERF_FILTER_CPU_ONLY) ? "cpu-only" : "wall-clock") < 0) goto Cleanup;
    if (fprintf(File, "# denominator: %Iu filtered samples of %Iu total\n", Session->FilteredSampleCount, Session->SampleCount) < 0) goto Cleanup;
    if (fprintf(File, "# scheduler-state: %I64u tagged, %I64u waiting\n", Session->StateTaggedSamples, Session->WaitingSamples) < 0) goto Cleanup;
    if (fprintf(File, "# loss: %I64u lost records (weight %I64u), %I64u truncated stacks, %I64u missed threads, %I64u missed ticks\n", Session->Counters.LostRecords, Session->Counters.LostWeight, Session->TruncatedStacks, Session->MissedThreads, Session->MissedTicks) < 0) goto Cleanup;
    Folded[0] = ANSI_NULL;
    if (!RperfExportFoldedNodes(File, Session, 0, Folded, 0, FoldedCapacity)) goto Cleanup;
    Result = TRUE;

Cleanup:
    HeapFree(GetProcessHeap(), 0, Folded);
    if (fclose(File) != 0) Result = FALSE;
    if (!Result && GetLastError() == ERROR_SUCCESS) SetLastError(ERROR_WRITE_FAULT);
    return Result;
}

static PSTR
RperfNextField(PSTR *Cursor)
{
    PSTR Field;
    PSTR End;

    if (Cursor == NULL || *Cursor == NULL || **Cursor == ANSI_NULL)
        return NULL;

    Field = *Cursor;
    End = Field;
    while (*End != ANSI_NULL && *End != '\t' && *End != '\r' && *End != '\n')
        ++End;

    if (*End == '\t')
    {
        *End = ANSI_NULL;
        *Cursor = End + 1;
    }
    else
    {
        *End = ANSI_NULL;
        *Cursor = NULL;
    }

    return Field;
}

static BOOL
RperfParseUnsigned64(PCSTR Field,
                     INT Base,
                     ULONGLONG Maximum,
                     ULONGLONG *Value)
{
    PSTR End;
    ULONGLONG Parsed;

    if (Field == NULL || *Field == ANSI_NULL || *Field == '-')
        return FALSE;

    errno = 0;
    Parsed = strtoull(Field, &End, Base);
    if (errno == ERANGE || End == Field || *End != ANSI_NULL || Parsed > Maximum)
        return FALSE;
    *Value = Parsed;
    return TRUE;
}

static BOOL
RperfNoMoreFields(PSTR Cursor)
{
    return Cursor == NULL || *Cursor == ANSI_NULL;
}

static BOOL
RperfParseSample(RPERF_SESSION *Session,
                 PSTR Cursor)
{
    RPERF_SAMPLE Sample;
    PSTR Field;
    USHORT Index;
    ULONGLONG Value;

    ZeroMemory(&Sample, sizeof(Sample));
    Field = RperfNextField(&Cursor);
    if (Field == NULL)
        return FALSE;
    if (!RperfParseUnsigned64(Field, 10, (ULONGLONG)-1, &Sample.TimeUs))
        return FALSE;
    Field = RperfNextField(&Cursor);
    if (Field == NULL)
        return FALSE;
    if (!RperfParseUnsigned64(Field, 10, MAXDWORD, &Value))
        return FALSE;
    Sample.ProcessId = (DWORD)Value;
    Field = RperfNextField(&Cursor);
    if (Field == NULL)
        return FALSE;
    if (!RperfParseUnsigned64(Field, 10, MAXDWORD, &Value))
        return FALSE;
    Sample.ThreadId = (DWORD)Value;
    Field = RperfNextField(&Cursor);
    if (Field == NULL)
        return FALSE;
    if (!RperfParseUnsigned64(Field, 10, RPERF_SAMPLE_FLAGS_VALID, &Value))
        return FALSE;
    if ((Value & ~(ULONGLONG)RPERF_SAMPLE_FLAGS_VALID) != 0)
        return FALSE;
    if ((Value & RPERF_SAMPLE_WAITING) != 0 && (Value & RPERF_SAMPLE_STATE_KNOWN) == 0)
        return FALSE;
    if ((Value & RPERF_SAMPLE_WAIT_REASON_MASK) != 0 && (Value & RPERF_SAMPLE_WAITING) == 0)
        return FALSE;
    Sample.Flags = (USHORT)Value;
    Field = RperfNextField(&Cursor);
    if (Field == NULL)
        return FALSE;
    if (!RperfParseUnsigned64(Field, 10, RPERF_MAX_FRAMES, &Value) ||
        Value == 0)
    {
        return FALSE;
    }
    Sample.Depth = (USHORT)Value;

    for (Index = 0; Index < Sample.Depth; ++Index)
    {
        Field = RperfNextField(&Cursor);
        if (Field == NULL)
            return FALSE;
        if (!RperfParseUnsigned64(Field,
                                  16,
                                  (ULONGLONG)-1,
                                  &Sample.Frames[Index]) ||
            Sample.Frames[Index] == 0)
        {
            return FALSE;
        }
    }

    if (!RperfNoMoreFields(Cursor) ||
        (Session->ProcessId != 0 && Sample.ProcessId != Session->ProcessId) ||
        (Session->SampleCount != 0 &&
         Sample.TimeUs < Session->Samples[Session->SampleCount - 1].TimeUs))
    {
        return FALSE;
    }
    if (!RperfAppendSample(Session, &Sample))
        return FALSE;
    RperfAccountSampleState(Session, Sample.Flags);
    return TRUE;
}

static BOOL
RperfParseSymbol(RPERF_SESSION *Session,
                 PSTR Cursor)
{
    RPERF_SYMBOL Symbol;
    PSTR Field;
    SIZE_T ExistingIndex;

    ZeroMemory(&Symbol, sizeof(Symbol));
    Field = RperfNextField(&Cursor);
    if (Field == NULL)
        return FALSE;
    if (!RperfParseUnsigned64(Field, 16, (ULONGLONG)-1, &Symbol.Address) ||
        Symbol.Address == 0)
    {
        return FALSE;
    }
    Field = RperfNextField(&Cursor);
    if (Field == NULL)
        return FALSE;
    if (!RperfParseUnsigned64(Field,
                              16,
                              (ULONGLONG)-1,
                              &Symbol.FunctionAddress) ||
        Symbol.FunctionAddress == 0)
    {
        return FALSE;
    }
    Field = RperfNextField(&Cursor);
    if (Field == NULL)
        return FALSE;
    if (!RperfParseUnsigned64(Field, 16, (ULONGLONG)-1, &Symbol.ModuleBase))
        return FALSE;
    Field = RperfNextField(&Cursor);
    if (Field == NULL)
        return FALSE;
    if (!RperfParseUnsigned64(Field, 16, (ULONGLONG)-1, &Symbol.Displacement))
        return FALSE;
    Field = RperfNextField(&Cursor);
    if (Field == NULL)
        return FALSE;
    RperfCopyLogField(Symbol.Module, ARRAYSIZE(Symbol.Module), Field);
    Field = RperfNextField(&Cursor);
    if (Field == NULL)
        return FALSE;
    RperfCopyLogField(Symbol.Name, ARRAYSIZE(Symbol.Name), Field);
    if (!RperfNoMoreFields(Cursor) ||
        Symbol.FunctionAddress > Symbol.Address ||
        (Symbol.ModuleBase != 0 && Symbol.ModuleBase > Symbol.Address))
        return FALSE;
    if (RperfFindRawSymbolIndex(Session, Symbol.Address, &ExistingIndex))
        return FALSE;
    return RperfInsertSymbol(Session, &Symbol, NULL);
}

static BOOL
RperfParseProcess(RPERF_SESSION *Session,
                  PSTR Cursor)
{
    PSTR Field = RperfNextField(&Cursor);
    PSTR Name;
    ULONGLONG ProcessId;

    if (Field == NULL)
        return FALSE;
    if (!RperfParseUnsigned64(Field, 10, MAXDWORD, &ProcessId) ||
        ProcessId == 0)
    {
        return FALSE;
    }
    Session->ProcessId = (DWORD)ProcessId;
    Name = RperfNextField(&Cursor);
    if (Name == NULL)
        return FALSE;

    if (!RperfNoMoreFields(Cursor) ||
        !MultiByteToWideChar(CP_UTF8,
                             0,
                             Name,
                             -1,
                             Session->ProcessName,
                             ARRAYSIZE(Session->ProcessName)))
    {
        return FALSE;
    }
    return TRUE;
}

static BOOL
RperfParseConfiguration(RPERF_SESSION *Session,
                        PSTR Cursor)
{
    PSTR Field = RperfNextField(&Cursor);
    PSTR Mode;
    ULONGLONG Value;
    if (Field == NULL)
        return FALSE;
    if (!RperfParseUnsigned64(Field, 10, 1000, &Value) || Value == 0)
        return FALSE;
    Session->IntervalMs = (DWORD)Value;
    Field = RperfNextField(&Cursor);
    if (Field == NULL)
        return FALSE;
    if (!RperfParseUnsigned64(Field, 10, 86400000, &Value))
        return FALSE;
    Session->RequestedDurationMs = (DWORD)Value;
    Mode = RperfNextField(&Cursor);
    if (Mode == NULL || !RperfNoMoreFields(Cursor))
        return FALSE;
    if (strcmp(Mode, "wall-clock-all-threads") == 0)
        Session->Backend = RperfBackendIntrusive;
    else if (strcmp(Mode, "kernel-on-cpu-samples") == 0)
        Session->Backend = RperfBackendKernel;
    else if (strcmp(Mode, "etw-sampled-profile") == 0)
        Session->Backend = RperfBackendEtw;
    else if (strcmp(Mode, "synthetic-contract-test") == 0)
        Session->Backend = RperfBackendFake;
    else
        return FALSE;
    return TRUE;
}

static BOOL
RperfParseEnd(RPERF_SESSION *Session,
              PSTR Cursor)
{
    ULONGLONG *Values[] =
    {
        &Session->ElapsedUs,
        &Session->UserTime100ns,
        &Session->KernelTime100ns,
        &Session->MissedThreads,
        &Session->MissedTicks,
        &Session->TruncatedStacks,
        &Session->LostSamples
    };
    SIZE_T Index;
    ULONGLONG Value;
    PSTR Field;

    for (Index = 0; Index < ARRAYSIZE(Values); ++Index)
    {
        Field = RperfNextField(&Cursor);
        if (Field == NULL)
            return FALSE;
        if (!RperfParseUnsigned64(Field, 10, (ULONGLONG)-1, Values[Index]))
            return FALSE;
    }
    Field = RperfNextField(&Cursor);
    if (Field == NULL ||
        !RperfParseUnsigned64(Field,
                              10,
                              RperfCompletionError,
                              &Value) ||
        Value == RperfCompletionIncomplete)
    {
        return FALSE;
    }
    Session->CompletionReason = (RPERF_COMPLETION_REASON)Value;
    Field = RperfNextField(&Cursor);
    if (Field == NULL || !RperfParseUnsigned64(Field, 10, MAXDWORD, &Value) ||
        !RperfNoMoreFields(Cursor))
    {
        return FALSE;
    }
    Session->CaptureError = (DWORD)Value;
    Session->LogComplete = TRUE;
    return TRUE;
}

BOOL
RperfLoadLog(RPERF_SESSION *Session,
             PCWSTR LogPath)
{
    RPERF_SESSION Loaded;
    FILE *Log;
    CHAR Line[4096];
    BOOL HeaderSeen = FALSE;
    BOOL ProcessSeen = FALSE;
    BOOL ConfigurationSeen = FALSE;
    BOOL FooterSeen = FALSE;
    BOOL TruncatedTail = FALSE;
    BOOL Result = FALSE;
    DWORD Error = ERROR_BAD_FORMAT;

    if (Session == NULL || LogPath == NULL || *LogPath == UNICODE_NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    RperfSessionInitialize(&Loaded);
    Log = _wfopen(LogPath, L"rt");
    if (Log == NULL)
    {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }

    while (fgets(Line, sizeof(Line), Log) != NULL)
    {
        PSTR Cursor = Line;
        PSTR Record;

        if (strchr(Line, '\n') == NULL)
        {
            if (feof(Log))
            {
                TruncatedTail = TRUE;
                break;
            }
            goto Cleanup;
        }

        Record = RperfNextField(&Cursor);

        if (Record == NULL || *Record == ANSI_NULL)
            continue;

        if (FooterSeen)
            goto Cleanup;

        if (!HeaderSeen)
        {
            PSTR Version;
            ULONGLONG ParsedVersion;
            if (strcmp(Record, "RPERF") != 0)
                goto Cleanup;
            Version = RperfNextField(&Cursor);
            if (Version == NULL ||
                !RperfParseUnsigned64(Version,
                                      10,
                                      RPERF_LOG_VERSION,
                                      &ParsedVersion) ||
                ParsedVersion != RPERF_LOG_VERSION ||
                !RperfNoMoreFields(Cursor))
            {
                goto Cleanup;
            }
            HeaderSeen = TRUE;
            continue;
        }

        if (strcmp(Record, "p") == 0)
        {
            if (ProcessSeen || ConfigurationSeen || Loaded.SampleCount != 0 ||
                Loaded.SymbolCount != 0 || !RperfParseProcess(&Loaded, Cursor))
            {
                goto Cleanup;
            }
            ProcessSeen = TRUE;
        }
        else if (strcmp(Record, "c") == 0)
        {
            if (!ProcessSeen || ConfigurationSeen || Loaded.SampleCount != 0 ||
                Loaded.SymbolCount != 0 ||
                !RperfParseConfiguration(&Loaded, Cursor))
            {
                goto Cleanup;
            }
            ConfigurationSeen = TRUE;
        }
        else if (strcmp(Record, "s") == 0)
        {
            if (!ProcessSeen || !ConfigurationSeen ||
                !RperfParseSample(&Loaded, Cursor))
            {
                goto Cleanup;
            }
        }
        else if (strcmp(Record, "y") == 0)
        {
            if (!ProcessSeen || !ConfigurationSeen ||
                !RperfParseSymbol(&Loaded, Cursor))
            {
                goto Cleanup;
            }
        }
        else if (strcmp(Record, "e") == 0)
        {
            if (!ProcessSeen || !ConfigurationSeen ||
                !RperfParseEnd(&Loaded, Cursor))
            {
                goto Cleanup;
            }
            FooterSeen = TRUE;
        }
        else
        {
            goto Cleanup;
        }
    }

    if (ferror(Log))
    {
        Error = ERROR_READ_FAULT;
        goto Cleanup;
    }
    if (!HeaderSeen || !ProcessSeen || !ConfigurationSeen ||
        Loaded.SampleCount == 0)
    {
        goto Cleanup;
    }
    if (!FooterSeen)
    {
        Loaded.LogComplete = FALSE;
        Loaded.CompletionReason = RperfCompletionIncomplete;
        Loaded.CaptureError = ERROR_HANDLE_EOF;
        Loaded.ElapsedUs = Loaded.Samples[Loaded.SampleCount - 1].TimeUs;
    }
    else if (TruncatedTail)
    {
        goto Cleanup;
    }
    if (Loaded.Samples[Loaded.SampleCount - 1].TimeUs > Loaded.ElapsedUs)
        goto Cleanup;
    if (Loaded.MissedThreads >
        (ULONGLONG)-1 - (ULONGLONG)Loaded.SampleCount)
    {
        goto Cleanup;
    }
    Loaded.Counters.AttemptedSamples =
        (ULONGLONG)Loaded.SampleCount + Loaded.MissedThreads;
    Loaded.Counters.SuccessfulSamples = Loaded.SampleCount;
    Loaded.Counters.FailedSamples = Loaded.MissedThreads;
    Loaded.Counters.TruncatedSamples = Loaded.TruncatedStacks;
    Loaded.Counters.MissedCadenceTicks = Loaded.MissedTicks;
    Loaded.Counters.LostRecords = Loaded.LostSamples;
    if (!RperfBuildAnalysis(&Loaded))
    {
        Error = GetLastError();
        if (Error == ERROR_SUCCESS)
            Error = ERROR_NOT_ENOUGH_MEMORY;
        goto Cleanup;
    }

    lstrcpynW(Loaded.SourcePath, LogPath, ARRAYSIZE(Loaded.SourcePath));
    RperfSessionClear(Session);
    *Session = Loaded;
    ZeroMemory(&Loaded, sizeof(Loaded));
    Result = TRUE;

Cleanup:
    fclose(Log);
    RperfSessionClear(&Loaded);
    if (!Result)
        SetLastError(Error);
    return Result;
}
