/* DbgHelp-backed PDB, embedded rossym, DWARF and export resolver. */

#include "profiler_symbolizer_dbghelp.h"
#include "profiler_pe.h"

#include <dbghelp.h>
#include <reactos/rperf.h>
#include <stdio.h>
#include <string.h>

#define RPERF_DBGHELP_SYNTHETIC_FIRST 0x0000010000000000ULL
#define RPERF_DBGHELP_SYNTHETIC_STRIDE 0x0000001000000000ULL

typedef enum _RPERF_DBGHELP_MODULE_STATE
{
    RperfDbgHelpModuleNew = 0,
    RperfDbgHelpModuleLoaded,
    RperfDbgHelpModuleImageMissing,
    RperfDbgHelpModuleIdentityMismatch,
    RperfDbgHelpModuleLoadError
} RPERF_DBGHELP_MODULE_STATE;

typedef struct _RPERF_DBGHELP_MODULE
{
    ULONGLONG Id;
    ULONGLONG LoadBase;
    RPERF_DBGHELP_MODULE_STATE State;
    DWORD Error;
    RPERF_PE_IDENTITY Identity;
    PWSTR ImagePath;
} RPERF_DBGHELP_MODULE;

typedef struct _RPERF_DBGHELP_CONTEXT
{
    HANDLE Process;
    CRITICAL_SECTION Lock;
    BOOL LockInitialized;
    PWSTR ImageSearchPath;
    BOOL AllowNetwork;
    RPERF_DBGHELP_MODULE *Modules;
    SIZE_T ModuleCount;
    SIZE_T ModuleCapacity;
    RPERF_SYMBOLIZATION_SUMMARY Summary;
} RPERF_DBGHELP_CONTEXT;

static PWSTR
RperfDbgHelpDuplicate(PCWSTR Text)
{
    SIZE_T Bytes;
    PWSTR Copy;

    if (Text == NULL)
        return NULL;
    if (wcslen(Text) > (((SIZE_T)-1) / sizeof(WCHAR)) - 1)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return NULL;
    }
    Bytes = (wcslen(Text) + 1) * sizeof(WCHAR);
    Copy = HeapAlloc(GetProcessHeap(), 0, Bytes);
    if (Copy != NULL)
        CopyMemory(Copy, Text, Bytes);
    return Copy;
}

static BOOL
RperfDbgHelpMayUseNetwork(PCWSTR Path)
{
    if (Path == NULL)
        return FALSE;
    while (*Path == L' ' || *Path == L'\t' || *Path == L'\"')
        Path++;
    return (Path[0] == L'\\' && Path[1] == L'\\') ||
           (Path[0] == L'/' && Path[1] == L'/') ||
           _wcsnicmp(Path, L"http:", 5) == 0 ||
           _wcsnicmp(Path, L"https:", 6) == 0 ||
           _wcsnicmp(Path, L"srv*", 4) == 0 ||
           _wcsnicmp(Path, L"symsrv*", 7) == 0;
}

static BOOL
RperfDbgHelpSearchPathAllowed(PCWSTR SearchPath,
                              BOOL AllowNetwork)
{
    PCWSTR Cursor, End;

    if (AllowNetwork || SearchPath == NULL)
        return TRUE;
    Cursor = SearchPath;
    while (*Cursor != UNICODE_NULL)
    {
        WCHAR Segment[MAX_PATH * 4];
        SIZE_T Length;

        End = wcschr(Cursor, L';');
        Length = End != NULL ? (SIZE_T)(End - Cursor) : wcslen(Cursor);
        if (Length >= ARRAYSIZE(Segment))
            return FALSE;
        CopyMemory(Segment, Cursor, Length * sizeof(WCHAR));
        Segment[Length] = UNICODE_NULL;
        if (RperfDbgHelpMayUseNetwork(Segment))
            return FALSE;
        if (End == NULL)
            break;
        Cursor = End + 1;
    }
    return TRUE;
}

static BOOL
RperfDbgHelpIsFile(PCWSTR Path,
                   BOOL AllowNetwork)
{
    DWORD Attributes;

    if (Path == NULL || *Path == UNICODE_NULL ||
        (!AllowNetwork && RperfDbgHelpMayUseNetwork(Path)))
        return FALSE;
    Attributes = GetFileAttributesW(Path);
    return Attributes != INVALID_FILE_ATTRIBUTES &&
           !(Attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static PCWSTR
RperfDbgHelpBaseName(PCWSTR Path)
{
    PCWSTR Slash, Backslash;

    if (Path == NULL)
        return L"module";
    Slash = wcsrchr(Path, L'/');
    Backslash = wcsrchr(Path, L'\\');
    if (Slash == NULL || (Backslash != NULL && Backslash > Slash))
        Slash = Backslash;
    return Slash != NULL ? Slash + 1 : Path;
}

static PWSTR
RperfDbgHelpJoin(PCWSTR Directory,
                 SIZE_T DirectoryLength,
                 PCWSTR Name)
{
    SIZE_T NameLength = wcslen(Name);
    BOOL Separator = DirectoryLength != 0 &&
                     Directory[DirectoryLength - 1] != L'\\' &&
                     Directory[DirectoryLength - 1] != L'/';
    SIZE_T Characters;
    PWSTR Path;

    if (DirectoryLength > ((SIZE_T)-1) - NameLength - 2)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return NULL;
    }
    Characters = DirectoryLength + NameLength + (Separator ? 2 : 1);
    Path = HeapAlloc(GetProcessHeap(), 0, Characters * sizeof(WCHAR));
    if (Path == NULL)
        return NULL;
    CopyMemory(Path, Directory, DirectoryLength * sizeof(WCHAR));
    if (Separator)
        Path[DirectoryLength++] = L'\\';
    CopyMemory(Path + DirectoryLength, Name,
               (NameLength + 1) * sizeof(WCHAR));
    return Path;
}

static PWSTR
RperfDbgHelpSystemRootPath(PCWSTR Path)
{
    WCHAR WindowsDirectory[MAX_PATH];
    UINT Length;

    if (Path == NULL || _wcsnicmp(Path, L"\\SystemRoot\\", 12) != 0)
        return NULL;
    Length = GetWindowsDirectoryW(WindowsDirectory,
                                  ARRAYSIZE(WindowsDirectory));
    if (Length == 0 || Length >= ARRAYSIZE(WindowsDirectory))
        return NULL;
    return RperfDbgHelpJoin(WindowsDirectory, Length, Path + 12);
}

static PWSTR
RperfDbgHelpFindImage(const RPERF_DBGHELP_CONTEXT *Context,
                      const RPERF_MODULE *Module)
{
    PWSTR Candidate;
    PCWSTR Cursor, End, Name;

    if (Module->Path != NULL)
    {
        PCWSTR Recorded = Module->Path;
        if (_wcsnicmp(Recorded, L"\\??\\", 4) == 0)
            Recorded += 4;
        if (RperfDbgHelpIsFile(Recorded, Context->AllowNetwork))
            return RperfDbgHelpDuplicate(Recorded);
        Candidate = RperfDbgHelpSystemRootPath(Module->Path);
        if (Candidate != NULL)
        {
            if (RperfDbgHelpIsFile(Candidate, Context->AllowNetwork))
                return Candidate;
            HeapFree(GetProcessHeap(), 0, Candidate);
        }
    }
    if (Context->ImageSearchPath == NULL)
        return NULL;
    Name = RperfDbgHelpBaseName(Module->Path);
    Cursor = Context->ImageSearchPath;
    while (*Cursor != UNICODE_NULL)
    {
        SIZE_T Length;
        End = wcschr(Cursor, L';');
        Length = End != NULL ? (SIZE_T)(End - Cursor) : wcslen(Cursor);
        while (Length != 0 && (*Cursor == L' ' || *Cursor == L'\t'))
        {
            Cursor++;
            Length--;
        }
        while (Length != 0 &&
               (Cursor[Length - 1] == L' ' ||
                Cursor[Length - 1] == L'\t'))
            Length--;
        if (Length != 0)
        {
            Candidate = RperfDbgHelpJoin(Cursor, Length, Name);
            if (Candidate == NULL)
                return NULL;
            if (RperfDbgHelpIsFile(Candidate, Context->AllowNetwork))
                return Candidate;
            HeapFree(GetProcessHeap(), 0, Candidate);
        }
        if (End == NULL)
            break;
        Cursor = End + 1;
    }
    return NULL;
}

static BOOL
RperfDbgHelpDebugIdPresent(const UCHAR DebugId[16])
{
    ULONG Index;
    for (Index = 0; Index < 16; ++Index)
    {
        if (DebugId[Index] != 0)
            return TRUE;
    }
    return FALSE;
}

static BOOL
RperfDbgHelpIdentityMatches(const RPERF_MODULE *Recorded,
                            const RPERF_PE_IDENTITY *Image)
{
    if (Recorded->Architecture != RPERF_ARCH_UNKNOWN &&
        Image->Architecture != RPERF_ARCH_UNKNOWN &&
        Recorded->Architecture != Image->Architecture)
        return FALSE;
    if (Recorded->TimeDateStamp != 0 && Image->TimeDateStamp != 0 &&
        Recorded->TimeDateStamp != Image->TimeDateStamp)
        return FALSE;
    if (Recorded->Checksum != 0 && Image->Checksum != 0 &&
        Recorded->Checksum != Image->Checksum)
        return FALSE;
    if (Recorded->Size != 0 && Image->ImageSize != 0 &&
        Recorded->Size != Image->ImageSize)
        return FALSE;
    if (RperfDbgHelpDebugIdPresent(Recorded->DebugId) &&
        RperfDbgHelpDebugIdPresent(Image->DebugId) &&
        memcmp(Recorded->DebugId, Image->DebugId, 16) != 0)
        return FALSE;
    if (Recorded->DebugAge != 0 && Image->DebugAge != 0 &&
        Recorded->DebugAge != Image->DebugAge)
        return FALSE;
    return TRUE;
}

static BOOL
RperfDbgHelpModuleInfoMatches(const RPERF_MODULE *Recorded,
                              const IMAGEHLP_MODULE64 *Loaded)
{
    if (Loaded->PdbUnmatched || Loaded->DbgUnmatched)
        return FALSE;
    if (Recorded->TimeDateStamp != 0 && Loaded->TimeDateStamp != 0 &&
        Recorded->TimeDateStamp != Loaded->TimeDateStamp)
        return FALSE;
    if (Recorded->Checksum != 0 && Loaded->CheckSum != 0 &&
        Recorded->Checksum != Loaded->CheckSum)
        return FALSE;
    if (RperfDbgHelpDebugIdPresent(Recorded->DebugId) &&
        RperfDbgHelpDebugIdPresent((const UCHAR *)&Loaded->PdbSig70) &&
        memcmp(Recorded->DebugId, &Loaded->PdbSig70, 16) != 0)
        return FALSE;
    if (Recorded->DebugAge != 0 && Loaded->PdbAge != 0 &&
        Recorded->DebugAge != Loaded->PdbAge)
        return FALSE;
    return TRUE;
}

static RPERF_SYMBOL_SOURCE_KIND
RperfDbgHelpSource(const RPERF_DBGHELP_MODULE *Module,
                   const IMAGEHLP_MODULE64 *Info)
{
    if (Module->Identity.HasRosSym && Info->SymType == SymDia)
        return RperfSymbolSourceRosSym;
    switch (Info->SymType)
    {
        case SymPdb:
            return RperfSymbolSourcePdb;
        case SymDia:
            return RperfSymbolSourceDwarf;
        case SymCv:
        case SymCoff:
            return RperfSymbolSourceCoff;
        case SymExport:
            return RperfSymbolSourceExport;
        default:
            return RperfSymbolSourceUnknown;
    }
}

static RPERF_DBGHELP_MODULE *
RperfDbgHelpGetModule(RPERF_DBGHELP_CONTEXT *Context,
                      const RPERF_MODULE *Module)
{
    RPERF_DBGHELP_MODULE *State;
    SIZE_T Index, NewCapacity;
    PVOID NewModules;
    BYTE SymbolBytes[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
    SYMBOL_INFO *Symbol = (SYMBOL_INFO *)SymbolBytes;
    DWORD64 Displacement;
    IMAGEHLP_MODULE64 Info;

    for (Index = 0; Index < Context->ModuleCount; ++Index)
    {
        if (Context->Modules[Index].Id == Module->Id)
            return &Context->Modules[Index];
    }
    if (Context->ModuleCount == Context->ModuleCapacity)
    {
        NewCapacity = Context->ModuleCapacity != 0 ?
                      Context->ModuleCapacity * 2 : 64;
        if (NewCapacity > ((SIZE_T)-1) / sizeof(*Context->Modules))
        {
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return NULL;
        }
        if (Context->Modules != NULL)
        {
            NewModules = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                     Context->Modules,
                                     NewCapacity * sizeof(*Context->Modules));
        }
        else
        {
            NewModules = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                   NewCapacity * sizeof(*Context->Modules));
        }
        if (NewModules == NULL)
            return NULL;
        Context->Modules = NewModules;
        Context->ModuleCapacity = NewCapacity;
    }
    State = &Context->Modules[Context->ModuleCount];
    ZeroMemory(State, sizeof(*State));
    State->Id = Module->Id;
    if (Context->ModuleCount >
        ((ULONGLONG)-1 - RPERF_DBGHELP_SYNTHETIC_FIRST) /
        RPERF_DBGHELP_SYNTHETIC_STRIDE)
    {
        State->State = RperfDbgHelpModuleLoadError;
        State->Error = ERROR_BUFFER_OVERFLOW;
        Context->ModuleCount++;
        return State;
    }
    State->LoadBase = RPERF_DBGHELP_SYNTHETIC_FIRST +
                      Context->ModuleCount *
                      RPERF_DBGHELP_SYNTHETIC_STRIDE;
    Context->ModuleCount++;
    State->ImagePath = RperfDbgHelpFindImage(Context, Module);
    if (State->ImagePath == NULL)
    {
        State->State = RperfDbgHelpModuleImageMissing;
        State->Error = ERROR_FILE_NOT_FOUND;
        return State;
    }
    if (!RperfReadPeIdentity(State->ImagePath, &State->Identity))
    {
        State->State = RperfDbgHelpModuleLoadError;
        State->Error = GetLastError();
        return State;
    }
    if (!RperfDbgHelpIdentityMatches(Module, &State->Identity))
    {
        State->State = RperfDbgHelpModuleIdentityMismatch;
        State->Error = ERROR_REVISION_MISMATCH;
        return State;
    }
    if (SymLoadModuleExW(Context->Process, NULL,
                         State->ImagePath,
                         RperfDbgHelpBaseName(State->ImagePath),
                         State->LoadBase,
                         Module->Size <= MAXDWORD ?
                         (DWORD)Module->Size : 0,
                         NULL, 0) == 0)
    {
        State->State = RperfDbgHelpModuleLoadError;
        State->Error = GetLastError();
        if (State->Error == ERROR_SUCCESS)
            State->Error = ERROR_MOD_NOT_FOUND;
        return State;
    }
    ZeroMemory(SymbolBytes, sizeof(SymbolBytes));
    Symbol->SizeOfStruct = sizeof(*Symbol);
    Symbol->MaxNameLen = MAX_SYM_NAME - 1;
    SymFromAddr(Context->Process, State->LoadBase,
                &Displacement, Symbol); /* force deferred debug loading */
    ZeroMemory(&Info, sizeof(Info));
    Info.SizeOfStruct = sizeof(Info);
    if (!SymGetModuleInfo64(Context->Process, State->LoadBase, &Info))
    {
        State->State = RperfDbgHelpModuleLoadError;
        State->Error = GetLastError();
        SymUnloadModule64(Context->Process, State->LoadBase);
        return State;
    }
    if (!RperfDbgHelpModuleInfoMatches(Module, &Info))
    {
        State->State = RperfDbgHelpModuleIdentityMismatch;
        State->Error = ERROR_REVISION_MISMATCH;
        SymUnloadModule64(Context->Process, State->LoadBase);
        return State;
    }
    State->State = RperfDbgHelpModuleLoaded;
    State->Error = ERROR_SUCCESS;
    return State;
}

static VOID
RperfDbgHelpCopyAnsi(PSTR Destination,
                     SIZE_T DestinationCount,
                     PCSTR Source)
{
    SIZE_T Length;

    if (DestinationCount == 0)
        return;
    if (Source == NULL)
    {
        Destination[0] = ANSI_NULL;
        return;
    }
    Length = strlen(Source);
    if (Length >= DestinationCount)
        Length = DestinationCount - 1;
    CopyMemory(Destination, Source, Length);
    Destination[Length] = ANSI_NULL;
}

static VOID
RperfDbgHelpModuleName(PSTR Destination,
                       SIZE_T DestinationCount,
                       PCWSTR Path)
{
    PCWSTR Name = RperfDbgHelpBaseName(Path);
    INT Result;

    if (DestinationCount == 0)
        return;
    Result = WideCharToMultiByte(CP_UTF8, 0, Name, -1,
                                 Destination, (INT)DestinationCount,
                                 NULL, NULL);
    if (Result == 0)
    {
        Result = WideCharToMultiByte(CP_ACP, 0, Name, -1,
                                     Destination, (INT)DestinationCount,
                                     NULL, NULL);
    }
    Destination[DestinationCount - 1] = ANSI_NULL;
}

static VOID
RperfDbgHelpCount(RPERF_DBGHELP_CONTEXT *Context,
                  const RPERF_SYMBOL_RESULT *Result)
{
    switch (Result->Source)
    {
        case RperfSymbolSourcePdb: Context->Summary.Pdb++; break;
        case RperfSymbolSourceRosSym: Context->Summary.RosSym++; break;
        case RperfSymbolSourceDwarf: Context->Summary.Dwarf++; break;
        case RperfSymbolSourceCoff: Context->Summary.Coff++; break;
        case RperfSymbolSourceExport: Context->Summary.Export++; break;
        case RperfSymbolSourceModuleOffset:
            Context->Summary.ModuleOffset++;
            break;
        default: break;
    }
    switch (Result->Status)
    {
        case RperfSymbolStatusImageMissing:
            Context->Summary.ImageMissing++;
            break;
        case RperfSymbolStatusIdentityMismatch:
            Context->Summary.IdentityMismatch++;
            break;
        case RperfSymbolStatusSymbolsMissing:
            Context->Summary.SymbolsMissing++;
            break;
        case RperfSymbolStatusLoadError:
            Context->Summary.LoadErrors++;
            break;
        default: break;
    }
}

static BOOL
RperfDbgHelpFallback(RPERF_DBGHELP_CONTEXT *Context,
                     const RPERF_MODULE *Module,
                     ULONGLONG Address,
                     RPERF_SYMBOL_STATUS_KIND Status,
                     RPERF_SYMBOL_RESULT *Result)
{
    ULONGLONG Relative = Module != NULL && Address >= Module->Base ?
                         Address - Module->Base : Address;

    ZeroMemory(Result, sizeof(*Result));
    Result->FunctionAddress = Address;
    Result->Resolution = RperfResolutionAddress;
    Result->Source = RperfSymbolSourceModuleOffset;
    Result->Status = Status;
    if (Module != NULL)
        RperfDbgHelpModuleName(Result->ModuleName,
                               ARRAYSIZE(Result->ModuleName),
                               Module->Path);
    _snprintf(Result->Name, ARRAYSIZE(Result->Name),
              "+0x%I64x", Relative);
    Result->Name[ARRAYSIZE(Result->Name) - 1] = ANSI_NULL;
    RperfDbgHelpCount(Context, Result);
    return TRUE;
}

static BOOL
RperfDbgHelpResolve(PVOID Opaque,
                    const RPERF_MODULE *Module,
                    ULONGLONG Address,
                    RPERF_SYMBOL_RESULT *Result)
{
    RPERF_DBGHELP_CONTEXT *Context = Opaque;
    RPERF_DBGHELP_MODULE *State;
    BYTE SymbolBytes[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
    SYMBOL_INFO *Symbol = (SYMBOL_INFO *)SymbolBytes;
    IMAGEHLP_MODULE64 Info;
    IMAGEHLP_LINE64 Line;
    DWORD64 Displacement;
    DWORD LineDisplacement;
    ULONGLONG QueryAddress;

    if (Context == NULL || Result == NULL || Address == 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    EnterCriticalSection(&Context->Lock);
    Context->Summary.Attempted++;
    if (Module == NULL || Address < Module->Base ||
        (Module->Size != 0 && Address - Module->Base >= Module->Size))
    {
        RperfDbgHelpFallback(Context, Module, Address,
                             RperfSymbolStatusImageMissing, Result);
        LeaveCriticalSection(&Context->Lock);
        return TRUE;
    }
    State = RperfDbgHelpGetModule(Context, Module);
    if (State == NULL)
    {
        RperfDbgHelpFallback(Context, Module, Address,
                             RperfSymbolStatusLoadError, Result);
        LeaveCriticalSection(&Context->Lock);
        return TRUE;
    }
    if (State->State != RperfDbgHelpModuleLoaded)
    {
        RPERF_SYMBOL_STATUS_KIND Status = RperfSymbolStatusLoadError;
        if (State->State == RperfDbgHelpModuleImageMissing)
            Status = RperfSymbolStatusImageMissing;
        else if (State->State == RperfDbgHelpModuleIdentityMismatch)
            Status = RperfSymbolStatusIdentityMismatch;
        RperfDbgHelpFallback(Context, Module, Address, Status, Result);
        LeaveCriticalSection(&Context->Lock);
        return TRUE;
    }
    QueryAddress = State->LoadBase + (Address - Module->Base);
    ZeroMemory(Result, sizeof(*Result));
    RperfDbgHelpModuleName(Result->ModuleName,
                           ARRAYSIZE(Result->ModuleName),
                           State->ImagePath);
    ZeroMemory(SymbolBytes, sizeof(SymbolBytes));
    Symbol->SizeOfStruct = sizeof(*Symbol);
    Symbol->MaxNameLen = MAX_SYM_NAME - 1;
    if (!SymFromAddr(Context->Process, QueryAddress,
                     &Displacement, Symbol) ||
        Symbol->Address < State->LoadBase)
    {
        RperfDbgHelpFallback(Context, Module, Address,
                             RperfSymbolStatusSymbolsMissing, Result);
        LeaveCriticalSection(&Context->Lock);
        return TRUE;
    }
    Result->FunctionAddress = Module->Base +
                              (Symbol->Address - State->LoadBase);
    Result->Resolution = RperfResolutionFunction;
    Result->Status = RperfSymbolStatusResolved;
    RperfDbgHelpCopyAnsi(Result->Name, ARRAYSIZE(Result->Name),
                         Symbol->Name);
    ZeroMemory(&Info, sizeof(Info));
    Info.SizeOfStruct = sizeof(Info);
    if (SymGetModuleInfo64(Context->Process, QueryAddress, &Info))
        Result->Source = RperfDbgHelpSource(State, &Info);
    ZeroMemory(&Line, sizeof(Line));
    Line.SizeOfStruct = sizeof(Line);
    if (SymGetLineFromAddr64(Context->Process, QueryAddress,
                             &LineDisplacement, &Line))
    {
        Result->Resolution = RperfResolutionSource;
        Result->SourceLine = Line.LineNumber;
        RperfDbgHelpCopyAnsi(Result->SourceFile,
                             ARRAYSIZE(Result->SourceFile),
                             Line.FileName);
    }
    if (Result->Source == RperfSymbolSourceUnknown)
        Result->Source = State->Identity.HasRosSym ?
                         RperfSymbolSourceRosSym :
                         RperfSymbolSourceCoff;
    RperfDbgHelpCount(Context, Result);
    LeaveCriticalSection(&Context->Lock);
    return TRUE;
}

static BOOL
RperfDbgHelpQuerySummary(PVOID Opaque,
                         RPERF_SYMBOLIZATION_SUMMARY *Summary)
{
    RPERF_DBGHELP_CONTEXT *Context = Opaque;
    if (Context == NULL || Summary == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    EnterCriticalSection(&Context->Lock);
    *Summary = Context->Summary;
    LeaveCriticalSection(&Context->Lock);
    return TRUE;
}

static VOID
RperfDbgHelpDestroy(PVOID Opaque)
{
    RPERF_DBGHELP_CONTEXT *Context = Opaque;
    SIZE_T Index;

    if (Context == NULL)
        return;
    if (Context->Process != NULL)
    {
        SymCleanup(Context->Process);
        CloseHandle(Context->Process);
    }
    for (Index = 0; Index < Context->ModuleCount; ++Index)
    {
        if (Context->Modules[Index].ImagePath != NULL)
            HeapFree(GetProcessHeap(), 0,
                     Context->Modules[Index].ImagePath);
    }
    if (Context->Modules != NULL)
        HeapFree(GetProcessHeap(), 0, Context->Modules);
    if (Context->ImageSearchPath != NULL)
        HeapFree(GetProcessHeap(), 0, Context->ImageSearchPath);
    if (Context->LockInitialized)
        DeleteCriticalSection(&Context->Lock);
    HeapFree(GetProcessHeap(), 0, Context);
}

static const RPERF_SYMBOL_PROVIDER_OPS RperfDbgHelpOps =
{
    RperfDbgHelpResolve,
    RperfDbgHelpQuerySummary,
    RperfDbgHelpDestroy
};

RPERF_SYMBOL_PROVIDER *
RperfCreateDbgHelpSymbolProvider(
    const RPERF_DBGHELP_CONFIGURATION *Configuration)
{
    RPERF_DBGHELP_CONTEXT *Context;
    RPERF_SYMBOL_PROVIDER *Provider;
    PCWSTR ImageSearchPath = Configuration != NULL ?
                            Configuration->ImageSearchPath : NULL;
    PCWSTR SymbolSearchPath = Configuration != NULL ?
                             Configuration->SymbolSearchPath : NULL;
    BOOL AllowNetwork = Configuration != NULL &&
                        Configuration->AllowNetwork;
    SIZE_T MaximumEntries = Configuration != NULL &&
                            Configuration->MaximumCacheEntries != 0 ?
                            Configuration->MaximumCacheEntries : 262144;
    DWORD Options;

    if (!RperfDbgHelpSearchPathAllowed(ImageSearchPath, AllowNetwork) ||
        !RperfDbgHelpSearchPathAllowed(SymbolSearchPath, AllowNetwork))
    {
        SetLastError(ERROR_ACCESS_DISABLED_BY_POLICY);
        return NULL;
    }
    Context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                        sizeof(*Context));
    if (Context == NULL)
        return NULL;
    InitializeCriticalSection(&Context->Lock);
    Context->LockInitialized = TRUE;
    Context->AllowNetwork = AllowNetwork;
    Context->ImageSearchPath = RperfDbgHelpDuplicate(ImageSearchPath);
    if (ImageSearchPath != NULL && Context->ImageSearchPath == NULL)
        goto Failure;
    Context->Process = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (Context->Process == NULL)
        goto Failure;
    Options = SymGetOptions() | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES |
              SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS;
#ifdef SYMOPT_DISABLE_SYMSRV_AUTODETECT
    if (!AllowNetwork)
        Options |= SYMOPT_DISABLE_SYMSRV_AUTODETECT;
#endif
#ifdef SYMOPT_DISABLE_SRVSTAR_ON_STARTUP
    if (!AllowNetwork)
        Options |= SYMOPT_DISABLE_SRVSTAR_ON_STARTUP;
#endif
    SymSetOptions(Options);
    if (!SymInitializeW(Context->Process,
                        SymbolSearchPath != NULL ? SymbolSearchPath : L"",
                        FALSE))
        goto Failure;
    Provider = RperfCreateCachedSymbolProvider(&RperfDbgHelpOps,
                                               Context,
                                               MaximumEntries);
    if (Provider == NULL)
        goto Failure;
    return Provider;

Failure:
    RperfDbgHelpDestroy(Context);
    return NULL;
}
