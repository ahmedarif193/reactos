/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * PROGRAMMERS:     (c) 2025 Ahmed ARIF (arif.ing@outlook.com)
 */

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <ndk/ntndk.h>
#include <stdio.h>
#include <string.h>
#include <reactos/wow64apc.h>
#include <reactos/wow64cpu.h>
#include <reactos/wow64shared.h>

#if defined(__GNUC__)
#define WOW64_UNUSED __attribute__((unused))
#else
#define WOW64_UNUSED
#endif

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

static VOID
Wow64ReportStub(
    _In_z_ LPCSTR FunctionName)
{
#if DBG
    CHAR Buffer[128];

    if (FunctionName)
    {
        _snprintf(Buffer, sizeof(Buffer), "wow64.dll stub: %s\n", FunctionName);
        Buffer[sizeof(Buffer) - 1] = '\0';
        OutputDebugStringA(Buffer);
    }
    else
    {
        OutputDebugStringA("wow64.dll stub hit\n");
    }
#else
    UNREFERENCED_PARAMETER(FunctionName);
#endif
}

typedef struct _WOW64_INTERNAL_STATE
{
    PWOW64_CPU_AREA CpuArea;
    BOOLEAN CpuAreaReady;
    BOOLEAN ProcessReady;
    BOOLEAN ThreadReady;
    BOOLEAN CsrConnected;
    BOOLEAN ServerProcess;
    BOOLEAN ProcessInfoPublished;
    UCHAR CsrConnectInfo[64];
} WOW64_INTERNAL_STATE;

PVOID Wow64GetThreadContext(VOID);
NTSTATUS Wow64SetThreadContext(_In_ PVOID ThreadContext);
NTSTATUS Wow64ClearThreadContext(VOID);
NTSTATUS Wow64DeliverPendingApcEx(_In_ BOOLEAN CheckTls);

static WOW64_INTERNAL_STATE Wow64State;
static LONG Wow64TlsIndex = TLS_OUT_OF_INDEXES;
static LONG Wow64CallbackTlsIndex = TLS_OUT_OF_INDEXES;
static PVOID Wow64VectoredHandlerHandle = NULL;

typedef struct _WOW64_EXCEPTION_BRIDGE
{
    BOOLEAN Initialized;
    PVOID   Base64;
    ULONG   Size;
    ULONG   ExRecord32; /* 32-bit VA of EXCEPTION_RECORD32 within buffer */
    ULONG   Ctx32;      /* 32-bit VA of WOW64_CONTEXT within buffer */
} WOW64_EXCEPTION_BRIDGE, *PWOW64_EXCEPTION_BRIDGE;

static WOW64_EXCEPTION_BRIDGE Wow64ExceptionBridge;

/* wow64cpu low-level transition to 32-bit routine */
NTSTATUS NTAPI
CpupDoCallBack(
    _In_ ULONG_PTR CallbackRoutine,
    _In_ ULONG_PTR Stack32,
    _Inout_ WOW64_CONTEXT *CompatContext,
    _Inout_ CONTEXT *NativeContext);

#ifndef USER_SHARED_DATA
#define USER_SHARED_DATA 0x7FFE0000ULL
#endif

#ifndef _PEB_LDR_DATA32_DEFINED
#define _PEB_LDR_DATA32_DEFINED
typedef struct _PEB_LDR_DATA32
{
    ULONG Length;
    BOOLEAN Initialized;
    ULONG SsHandle;
    LIST_ENTRY32 InLoadOrderModuleList;
    LIST_ENTRY32 InMemoryOrderModuleList;
    LIST_ENTRY32 InInitializationOrderModuleList;
    ULONG EntryInProgress;
    ULONG ShutdownInProgress;
    ULONG ShutdownThreadId;
} PEB_LDR_DATA32, *PPEB_LDR_DATA32;
#endif

typedef struct _LDR_DATA_TABLE_ENTRY32
{
    LIST_ENTRY32 InLoadOrderLinks;
    LIST_ENTRY32 InMemoryOrderLinks;
    LIST_ENTRY32 InInitializationOrderLinks;
    ULONG DllBase;
    ULONG EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING32 FullDllName;
    UNICODE_STRING32 BaseDllName;
} LDR_DATA_TABLE_ENTRY32, *PLDR_DATA_TABLE_ENTRY32;

#define WIN_OBJ_DIR L"\\Windows"
#define BASESRV_SERVERDLL_INDEX 1

NTSTATUS
NTAPI
CsrClientConnectToServer(
    _In_ PCWSTR ObjectDirectory,
    _In_ ULONG ServerId,
    _In_ PVOID ConnectionInfo,
    _Inout_ PULONG ConnectionInfoSize,
    _Out_ PBOOLEAN ServerToServerCall);

NTSTATUS
NTAPI
CsrIdentifyAlertableThread(VOID);

NTSTATUS
NTAPI
CsrNewThread(VOID);

VOID
WINAPI
Wow64ApcRoutine(
    ULONG_PTR NormalContext,
    ULONG_PTR SystemArgument1,
    ULONG_PTR SystemArgument2,
    PCONTEXT SystemContext);

static WOW64_PROCESS_INFO Wow64ProcessInfo =
{
    WOW64_PROCESS_INFO_VERSION,
    sizeof(WOW64_PROCESS_INFO),
    Wow64ApcRoutine,
    NULL,
    {0, 0, 0, 0}
};

typedef struct _WOW64_CALLBACK_FRAME
{
    struct _WOW64_CALLBACK_FRAME *Previous;
    PWOW64_CPU_AREA CpuArea;
    PVOID *OutputBuffer;
    PULONG OutputLength;
    ULONG Flags;
} WOW64_CALLBACK_FRAME, *PWOW64_CALLBACK_FRAME;

#define WOW64_CALLBACK_FRAME_FLAG_CALLBACK 0x00000001
#define WOW64_CALLBACK_FRAME_FLAG_APC       0x00000002

static NTSTATUS
Wow64PublishProcessInfo(VOID)
{
    NTSTATUS Status;
    PVOID InfoPointer;

    if (Wow64State.ProcessInfoPublished)
    {
        return STATUS_SUCCESS;
    }

    Wow64ProcessInfo.Wow64ApcDispatcher = (PVOID)Wow64ApcRoutine;
    InfoPointer = &Wow64ProcessInfo;

    Status = NtSetInformationProcess(NtCurrentProcess(),
                                     ProcessWow64Information,
                                     &InfoPointer,
                                     sizeof(InfoPointer));
    if (NT_SUCCESS(Status))
    {
        Wow64State.ProcessInfoPublished = TRUE;
    }
    else
    {
        Wow64ReportStub("Wow64PublishProcessInfo failed");
    }

    return Status;
}

static BOOLEAN
Wow64EnsureCallbackTlsIndex(VOID)
{
    LONG CurrentIndex;

    CurrentIndex = Wow64CallbackTlsIndex;
    if (CurrentIndex != TLS_OUT_OF_INDEXES)
    {
        return TRUE;
    }

    CurrentIndex = (LONG)TlsAlloc();
    if (CurrentIndex == TLS_OUT_OF_INDEXES)
    {
        return FALSE;
    }

    if (InterlockedCompareExchange(&Wow64CallbackTlsIndex,
                                   CurrentIndex,
                                   TLS_OUT_OF_INDEXES) != TLS_OUT_OF_INDEXES)
    {
        TlsFree((DWORD)CurrentIndex);
    }

    return Wow64CallbackTlsIndex != TLS_OUT_OF_INDEXES;
}

static __inline PWOW64_CALLBACK_FRAME
Wow64GetCallbackFrame(VOID)
{
    if (Wow64CallbackTlsIndex == TLS_OUT_OF_INDEXES)
    {
        return NULL;
    }

    return (PWOW64_CALLBACK_FRAME)TlsGetValue((DWORD)Wow64CallbackTlsIndex);
}

static VOID
Wow64SetCallbackFrame(
    _In_opt_ PWOW64_CALLBACK_FRAME Frame)
{
    if (Wow64CallbackTlsIndex != TLS_OUT_OF_INDEXES)
    {
        TlsSetValue((DWORD)Wow64CallbackTlsIndex, Frame);
    }
}

/* Attempt to convert a preserved 64-bit host CONTEXT into a 32-bit WOW64 context
 * and publish it back into the CPU area so that user-mode dispatchers can use it.
 * This is opportunistic and safe to call when no host context is present. */
static VOID
Wow64SyncHostContextToCompat(VOID)
{
    PWOW64_CPU_AREA CpuArea = Wow64State.CpuArea;
    CONTEXT HostContext;
    WOW64_CONTEXT CompatContext;
    NTSTATUS Status;

    if (!CpuArea)
    {
        return;
    }

    RtlZeroMemory(&HostContext, sizeof(HostContext));
    /* Try to capture a native (host) context from the CPU area */
    Status = Wow64CpuGetContext(CpuArea, &HostContext, sizeof(HostContext));
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    /* Convert into a 32-bit WOW64 context and publish it back */
    RtlZeroMemory(&CompatContext, sizeof(CompatContext));
    Status = Wow64TransitionToCompat(CpuArea, &HostContext, &CompatContext);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    (void)Wow64CpuSetContext(CpuArea, &CompatContext, sizeof(CompatContext));
}

static NTSTATUS
Wow64EnsureExceptionBridgeBuffer(VOID)
{
    NTSTATUS Status;
    SIZE_T RegionSize;
    PVOID BaseAddress;

    if (Wow64ExceptionBridge.Initialized)
    {
        return STATUS_SUCCESS;
    }

    RegionSize = 0x2000; /* 8 KiB for EXCEPTION_RECORD32 + WOW64_CONTEXT */
    BaseAddress = (PVOID)(ULONG_PTR)0x100000; /* hint: low 32-bit */
    Status = NtAllocateVirtualMemory(NtCurrentProcess(),
                                     &BaseAddress,
                                     0,
                                     &RegionSize,
                                     MEM_RESERVE | MEM_COMMIT,
                                     PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if ((ULONG_PTR)BaseAddress > 0xFFFFFFFFULL)
    {
        /* Unexpected high address: free and fail for now */
        SIZE_T FreeSize = 0;
        NtFreeVirtualMemory(NtCurrentProcess(), &BaseAddress, &FreeSize, MEM_RELEASE);
        return STATUS_NOT_SUPPORTED;
    }

    Wow64ExceptionBridge.Base64 = BaseAddress;
    Wow64ExceptionBridge.Size = (ULONG)RegionSize;
    Wow64ExceptionBridge.ExRecord32 = (ULONG)(ULONG_PTR)BaseAddress;
    Wow64ExceptionBridge.Ctx32 = Wow64ExceptionBridge.ExRecord32 + 0x400; /* 1 KiB offset */
    Wow64ExceptionBridge.Initialized = TRUE;
    return STATUS_SUCCESS;
}

static VOID
Wow64ConvertExceptionRecord64To32(
    _In_ const EXCEPTION_RECORD *Record64,
    _Out_ PEXCEPTION_RECORD32 Record32)
{
    ULONG i;
    if (!Record32)
        return;
    if (!Record64)
    {
        RtlZeroMemory(Record32, sizeof(*Record32));
        return;
    }
    Record32->ExceptionCode = Record64->ExceptionCode;
    Record32->ExceptionFlags = Record64->ExceptionFlags;
    Record32->ExceptionRecord = (ULONG)(ULONG_PTR)Record64->ExceptionRecord;
    Record32->ExceptionAddress = (ULONG)(ULONG_PTR)Record64->ExceptionAddress;
    Record32->NumberParameters = Record64->NumberParameters;
    for (i = 0; i < EXCEPTION_MAXIMUM_PARAMETERS; i++)
    {
        Record32->ExceptionInformation[i] = (ULONG)(ULONG_PTR)Record64->ExceptionInformation[i];
    }
}

static ULONG
Wow64GetExceptionDispatcher32(VOID)
{
    PKUSER_SHARED_DATA SharedData = (PKUSER_SHARED_DATA)USER_SHARED_DATA;
    if (!SharedData)
        return 0;
    return SharedData->Wow64SharedInformation[Wow64SharedInformationUserExceptionDispatcher32];
}

/* Vectored Exception Handler: convert native CONTEXT to WOW64 context
 * when possible so that user-mode 32-bit dispatchers have access to it. */
static LONG WINAPI
Wow64VectoredExceptionHandler(
    _In_ struct _EXCEPTION_POINTERS *ExceptionInfo)
{
    PWOW64_CPU_AREA CpuArea = Wow64State.CpuArea;
    WOW64_CONTEXT CompatContext;
    NTSTATUS Status;

    if (!ExceptionInfo || !CpuArea)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    RtlZeroMemory(&CompatContext, sizeof(CompatContext));

    /* Prepare 32-bit pointers for exception record and context */
    if (NT_SUCCESS(Wow64EnsureExceptionBridgeBuffer()))
    {
        /* Convert native to WOW64 context */
        Status = Wow64TransitionToCompat(CpuArea,
                                         ExceptionInfo->ContextRecord,
                                         &CompatContext);
        if (NT_SUCCESS(Status))
        {
            /* Publish compat context into CPU area and 32-bit buffer */
            (void)Wow64CpuSetContext(CpuArea, &CompatContext, sizeof(CompatContext));
            RtlCopyMemory((PVOID)(ULONG_PTR)Wow64ExceptionBridge.Ctx32,
                          &CompatContext,
                          sizeof(CompatContext));

            /* Convert and store 32-bit exception record */
            Wow64ConvertExceptionRecord64To32(ExceptionInfo->ExceptionRecord,
                                              (PEXCEPTION_RECORD32)(ULONG_PTR)Wow64ExceptionBridge.ExRecord32);

#if DBG
            {
                ULONG Disp32 = Wow64GetExceptionDispatcher32();
                CHAR buf[128];
                _snprintf(buf, sizeof(buf),
                          "wow64: Prepared 32-bit exception payload (KiUserExceptionDispatcher32=0x%08lx, Ex=0x%08lx, Ctx=0x%08lx)\n",
                          Disp32,
                          Wow64ExceptionBridge.ExRecord32,
                          Wow64ExceptionBridge.Ctx32);
                buf[sizeof(buf)-1] = '\0';
                OutputDebugStringA(buf);
            }
#endif
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static PVOID
Wow64FindModuleBase32(
    _In_ PCWSTR ModuleName)
{
    ULONG_PTR Peb32Address = 0;
    NTSTATUS Status;
    PPEB32 Peb32;
    PPEB_LDR_DATA32 Ldr32;
    UNICODE_STRING TargetName;
    PLIST_ENTRY32 Head32;
    PLIST_ENTRY32 Current32;

    Status = NtQueryInformationProcess(NtCurrentProcess(),
                                       ProcessWow64Information,
                                       &Peb32Address,
                                       sizeof(Peb32Address),
                                       NULL);
    if (!NT_SUCCESS(Status) || Peb32Address == 0)
    {
        return NULL;
    }

    Peb32 = (PPEB32)(ULONG_PTR)Peb32Address;
    if (!Peb32->Ldr)
    {
        return NULL;
    }

    Ldr32 = (PPEB_LDR_DATA32)(ULONG_PTR)Peb32->Ldr;
    Head32 = &Ldr32->InLoadOrderModuleList;
    Current32 = (PLIST_ENTRY32)(ULONG_PTR)Head32->Flink;

    RtlInitUnicodeString(&TargetName, ModuleName);

    while (Current32 != Head32)
    {
        PLDR_DATA_TABLE_ENTRY32 Entry32;
        UNICODE_STRING BaseName;

        Entry32 = CONTAINING_RECORD(Current32, LDR_DATA_TABLE_ENTRY32, InLoadOrderLinks);

        BaseName.Length = Entry32->BaseDllName.Length;
        BaseName.MaximumLength = Entry32->BaseDllName.MaximumLength;
        BaseName.Buffer = (PWSTR)(ULONG_PTR)Entry32->BaseDllName.Buffer;

        if (BaseName.Buffer &&
            RtlEqualUnicodeString(&BaseName, &TargetName, TRUE))
        {
            return (PVOID)(ULONG_PTR)Entry32->DllBase;
        }

        Current32 = (PLIST_ENTRY32)(ULONG_PTR)Current32->Flink;
    }

    return NULL;
}

static ULONG_PTR
Wow64ResolveExportRva32(
    _In_ PVOID ModuleBase,
    _In_ PCSTR ExportName)
{
    ULONG ExportSize = 0;
    PIMAGE_EXPORT_DIRECTORY ExportDir;
    PULONG NameTable;
    PULONG FunctionTable;
    PUSHORT OrdinalTable;
    ULONG Index;

    ExportDir = (PIMAGE_EXPORT_DIRECTORY)RtlImageDirectoryEntryToData(ModuleBase,
                                                                      TRUE,
                                                                      IMAGE_DIRECTORY_ENTRY_EXPORT,
                                                                      &ExportSize);
    if (!ExportDir)
    {
        return 0;
    }

    NameTable = (PULONG)((PUCHAR)ModuleBase + ExportDir->AddressOfNames);
    FunctionTable = (PULONG)((PUCHAR)ModuleBase + ExportDir->AddressOfFunctions);
    OrdinalTable = (PUSHORT)((PUCHAR)ModuleBase + ExportDir->AddressOfNameOrdinals);

    for (Index = 0; Index < ExportDir->NumberOfNames; Index++)
    {
        PCSTR CurrentName = (PCSTR)((PUCHAR)ModuleBase + NameTable[Index]);
        if (CurrentName && _stricmp(CurrentName, ExportName) == 0)
        {
            USHORT Ordinal = OrdinalTable[Index];
            return (ULONG_PTR)FunctionTable[Ordinal];
        }
    }

    return 0;
}

static VOID
Wow64PublishSharedInformation(VOID)
{
    static const struct
    {
        const char *Name;
        WOW64_SHARED_INFORMATION_INDEX Index;
    } ExportTargets[] =
    {
        { "KiUserApcDispatcher", Wow64SharedInformationUserApcDispatcher32 },
        { "KiUserCallbackDispatcher", Wow64SharedInformationUserCallbackDispatcher32 },
        { "KiUserExceptionDispatcher", Wow64SharedInformationUserExceptionDispatcher32 },
        { "KiUserApcDispatcherContinue", Wow64SharedInformationUserApcReturn32 },
    };

    PVOID ModuleBase;
    PKUSER_SHARED_DATA SharedData;
    SIZE_T i;

    ModuleBase = Wow64FindModuleBase32(L"ntdll.dll");
    SharedData = (PKUSER_SHARED_DATA)USER_SHARED_DATA;

    if (!SharedData)
    {
        return;
    }

    /* Clear any stale values by default. */
    RtlZeroMemory(SharedData->Wow64SharedInformation,
                  sizeof(SharedData->Wow64SharedInformation));

    if (!ModuleBase)
    {
        Wow64ReportStub("Wow64PublishSharedInformation: ntdll.dll base not found");
        return;
    }

    for (i = 0; i < RTL_NUMBER_OF(ExportTargets); i++)
    {
        ULONG_PTR Rva;

        Rva = Wow64ResolveExportRva32(ModuleBase, ExportTargets[i].Name);
        if (Rva != 0)
        {
            ULONG_PTR Absolute = (ULONG_PTR)ModuleBase + Rva;
            SharedData->Wow64SharedInformation[ExportTargets[i].Index] = (ULONG)Absolute;
        }
    }
}

static NTSTATUS
Wow64EnsureCpuArea(VOID)
{
    NTSTATUS Status;

    if (Wow64State.CpuAreaReady)
    {
        return STATUS_SUCCESS;
    }

    Status = CpuProcessInit(&Wow64State.CpuArea, NULL);
    if (!NT_SUCCESS(Status))
    {
        Wow64ReportStub("CpuProcessInit failed");
        return Status;
    }

    Wow64State.CpuAreaReady = TRUE;
    (void)Wow64PublishProcessInfo();
    return STATUS_SUCCESS;
}

/* ===================================================================
 * Environment and KnownDLL Resolution
 * ================================================================ */

typedef struct _WOW64_ENVIRONMENT_CONTEXT
{
    BOOLEAN Initialized;
    WCHAR SystemRoot[MAX_PATH];
    WCHAR SysWOW64Path[MAX_PATH];
    WCHAR KnownDllsDirectory[MAX_PATH];
    HANDLE KnownDllsDirectoryHandle;
} WOW64_ENVIRONMENT_CONTEXT, *PWOW64_ENVIRONMENT_CONTEXT;

static WOW64_ENVIRONMENT_CONTEXT Wow64EnvironmentContext;

static NTSTATUS
Wow64InitializeEnvironmentPaths(VOID)
{
    UNICODE_STRING SystemRootString;
    UNICODE_STRING ValueString;
    NTSTATUS Status;

    if (Wow64EnvironmentContext.Initialized)
    {
        return STATUS_SUCCESS;
    }

    /* Get SystemRoot from environment */
    RtlInitUnicodeString(&SystemRootString, L"SystemRoot");
    ValueString.Length = 0;
    ValueString.MaximumLength = sizeof(Wow64EnvironmentContext.SystemRoot);
    ValueString.Buffer = Wow64EnvironmentContext.SystemRoot;

    Status = RtlQueryEnvironmentVariable_U(NULL, &SystemRootString, &ValueString);
    if (!NT_SUCCESS(Status))
    {
        /* Fallback to default */
        wcscpy(Wow64EnvironmentContext.SystemRoot, L"\\ReactOS");
    }

    /* Build SysWOW64 path: %SystemRoot%\SysWOW64 */
    _snwprintf(Wow64EnvironmentContext.SysWOW64Path,
               MAX_PATH,
               L"%s\\SysWOW64",
               Wow64EnvironmentContext.SystemRoot);
    Wow64EnvironmentContext.SysWOW64Path[MAX_PATH - 1] = L'\0';

    /* Build KnownDLLs directory path */
    wcscpy(Wow64EnvironmentContext.KnownDllsDirectory, L"\\KnownDlls32");

    Wow64EnvironmentContext.Initialized = TRUE;
    Wow64EnvironmentContext.KnownDllsDirectoryHandle = NULL;

#if DBG
    {
        CHAR Buffer[512];
        _snprintf(Buffer, sizeof(Buffer),
                 "wow64: Environment initialized\n"
                 "  SystemRoot: %S\n"
                 "  SysWOW64: %S\n"
                 "  KnownDlls: %S\n",
                 Wow64EnvironmentContext.SystemRoot,
                 Wow64EnvironmentContext.SysWOW64Path,
                 Wow64EnvironmentContext.KnownDllsDirectory);
        Buffer[sizeof(Buffer) - 1] = '\0';
        OutputDebugStringA(Buffer);
    }
#endif

    return STATUS_SUCCESS;
}

static NTSTATUS
Wow64OpenKnownDllsDirectory(VOID)
{
    UNICODE_STRING DirectoryName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    if (Wow64EnvironmentContext.KnownDllsDirectoryHandle)
    {
        return STATUS_SUCCESS;
    }

    Status = Wow64InitializeEnvironmentPaths();
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlInitUnicodeString(&DirectoryName, Wow64EnvironmentContext.KnownDllsDirectory);

    InitializeObjectAttributes(&ObjectAttributes,
                              &DirectoryName,
                              OBJ_CASE_INSENSITIVE,
                              NULL,
                              NULL);

    Status = NtOpenDirectoryObject(&Wow64EnvironmentContext.KnownDllsDirectoryHandle,
                                   DIRECTORY_QUERY | DIRECTORY_TRAVERSE,
                                   &ObjectAttributes);

    if (!NT_SUCCESS(Status))
    {
        Wow64ReportStub("Wow64OpenKnownDllsDirectory: Failed to open KnownDlls32");
        Wow64EnvironmentContext.KnownDllsDirectoryHandle = NULL;
    }
#if DBG
    else
    {
        OutputDebugStringA("wow64: Opened KnownDlls32 directory\n");
    }
#endif

    return Status;
}

static NTSTATUS WOW64_UNUSED
Wow64ResolveKnownDll(
    _In_ PCWSTR DllName,
    _Out_ PHANDLE SectionHandle)
{
    UNICODE_STRING DllNameString;
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    if (!SectionHandle)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *SectionHandle = NULL;

    Status = Wow64OpenKnownDllsDirectory();
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlInitUnicodeString(&DllNameString, DllName);

    InitializeObjectAttributes(&ObjectAttributes,
                              &DllNameString,
                              OBJ_CASE_INSENSITIVE,
                              Wow64EnvironmentContext.KnownDllsDirectoryHandle,
                              NULL);

    Status = NtOpenSection(SectionHandle,
                          SECTION_MAP_READ | SECTION_MAP_EXECUTE | SECTION_MAP_WRITE,
                          &ObjectAttributes);

#if DBG
    if (NT_SUCCESS(Status))
    {
        CHAR Buffer[256];
        _snprintf(Buffer, sizeof(Buffer),
                 "wow64: Resolved KnownDll: %S (handle 0x%p)\n",
                 DllName,
                 *SectionHandle);
        Buffer[sizeof(Buffer) - 1] = '\0';
        OutputDebugStringA(Buffer);
    }
#endif

    return Status;
}

static PCWSTR
Wow64ResolveObjectDirectory(VOID)
{
    /* TODO: Extend to session-aware paths when required. */
    return WIN_OBJ_DIR;
}

static NTSTATUS
Wow64SetEnvironmentVariable(
    _In_ PCWSTR VariableName,
    _In_ PCWSTR Value)
{
    UNICODE_STRING NameString;
    UNICODE_STRING ValueString;
    NTSTATUS Status;

    RtlInitUnicodeString(&NameString, VariableName);
    RtlInitUnicodeString(&ValueString, Value);

    Status = RtlSetEnvironmentVariable(NULL, &NameString, &ValueString);

#if DBG
    if (NT_SUCCESS(Status))
    {
        CHAR Buffer[512];
        _snprintf(Buffer, sizeof(Buffer),
                 "wow64: Set environment: %S=%S\n",
                 VariableName,
                 Value);
        Buffer[sizeof(Buffer) - 1] = '\0';
        OutputDebugStringA(Buffer);
    }
#endif

    return Status;
}

static NTSTATUS
Wow64ConfigureEnvironment(VOID)
{
    NTSTATUS Status;

    Status = Wow64InitializeEnvironmentPaths();
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /* Set ProgramFiles(x86) to point to Program Files (x86) */
    Wow64SetEnvironmentVariable(L"ProgramFiles(x86)", L"C:\\Program Files (x86)");

    /* Set ProgramW6432 to point to native 64-bit Program Files */
    Wow64SetEnvironmentVariable(L"ProgramW6432", L"C:\\Program Files");

    /* Set PROCESSOR_ARCHITECTURE to x86 for 32-bit apps */
    Wow64SetEnvironmentVariable(L"PROCESSOR_ARCHITECTURE", L"x86");

    /* Set PROCESSOR_ARCHITEW6432 to AMD64 to indicate underlying architecture */
    Wow64SetEnvironmentVariable(L"PROCESSOR_ARCHITEW6432", L"AMD64");

    return STATUS_SUCCESS;
}

BOOL
WINAPI
DllMain(
    HINSTANCE Instance,
    DWORD Reason,
    LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Reason);
    UNREFERENCED_PARAMETER(Reserved);
    return TRUE;
}

NTSTATUS
NTAPI
Wow64LdrpInitialize(VOID)
{
    NTSTATUS Status;

    Status = Wow64EnsureCpuArea();
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (!Wow64EnsureCallbackTlsIndex())
    {
        return STATUS_NO_MEMORY;
    }

    Wow64ReportStub("Wow64LdrpInitialize");
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
Wow64LdrpInitializeProcess(
    _In_opt_ PVOID Peb32,
    _In_opt_ PVOID LoaderParameterBlock)
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(LoaderParameterBlock);

    Status = Wow64EnsureCpuArea();
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = CpuNotify(Wow64CpuNotifyInitialize, Wow64State.CpuArea, Peb32);
    if (NT_SUCCESS(Status))
    {
        Wow64State.ProcessReady = TRUE;
    }
    else
    {
        Wow64ReportStub("CpuNotify initialize failed");
    }

    Wow64ReportStub("Wow64LdrpInitializeProcess");
    /* Register a vectored exception handler to assist 32-bit dispatch */
    if (!Wow64VectoredHandlerHandle)
    {
        Wow64VectoredHandlerHandle = RtlAddVectoredExceptionHandler(1, Wow64VectoredExceptionHandler);
    }

    return Status;
}

NTSTATUS
NTAPI
Wow64LdrpInitializeThread(
    _In_opt_ PVOID ThreadContext)
{
    NTSTATUS Status;

    Status = Wow64EnsureCpuArea();
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /* Store CPU area in TLS for this thread */
    Status = Wow64SetThreadContext(Wow64State.CpuArea);
    if (!NT_SUCCESS(Status))
    {
        Wow64ReportStub("Wow64LdrpInitializeThread: Failed to set TLS context");
        /* Non-fatal - continue */
    }

    Status = CpuThreadInit(Wow64State.CpuArea, ThreadContext);
    if (!NT_SUCCESS(Status))
    {
        Wow64ReportStub("CpuThreadInit failed");
        return Status;
    }

    Status = CpuNotify(Wow64CpuNotifyThreadAttach, Wow64State.CpuArea, ThreadContext);
    if (!NT_SUCCESS(Status))
    {
        Wow64ReportStub("CpuNotify thread attach failed");
    }
    else
    {
        Wow64State.ThreadReady = TRUE;
    }

    /* Mark this thread as alertable for CSR */
    (void)CsrIdentifyAlertableThread();

    /* Deliver pending APCs with TLS awareness */
    Status = Wow64DeliverPendingApcEx(TRUE);
    if (!NT_SUCCESS(Status) &&
        Status != STATUS_NOT_FOUND &&
        Status != STATUS_NOT_IMPLEMENTED)
    {
        Wow64ReportStub("Wow64LdrpInitializeThread: APC delivery failed");
    }

    /* Opportunistically sync any preserved host context to WOW64 compat */
    Wow64SyncHostContextToCompat();

    Wow64ReportStub("Wow64LdrpInitializeThread: Thread initialization complete");
    return Status;
}

NTSTATUS
NTAPI
Wow64ProcessInit(
    _In_opt_ PVOID CsrProcess)
{
    NTSTATUS Status;
    ULONG ConnectionInfoSize;
    BOOLEAN ServerToServer = FALSE;
    PCWSTR ObjectDirectory;

    UNREFERENCED_PARAMETER(CsrProcess);

    /* Initialize CPU area first */
    Status = Wow64EnsureCpuArea();
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (!Wow64EnsureCallbackTlsIndex())
    {
        return STATUS_NO_MEMORY;
    }

    /* Configure WOW64 environment variables */
    Status = Wow64ConfigureEnvironment();
    if (!NT_SUCCESS(Status))
    {
        Wow64ReportStub("Wow64ProcessInit: Environment configuration failed");
        /* Non-fatal - continue */
    }

    Wow64PublishSharedInformation();

    /* Attempt to open KnownDlls32 directory for later use */
    Status = Wow64OpenKnownDllsDirectory();
    if (!NT_SUCCESS(Status))
    {
        Wow64ReportStub("Wow64ProcessInit: KnownDlls32 not available");
        /* Non-fatal - continue without KnownDLL optimization */
    }

    /* Connect to CSR subsystem */
    ObjectDirectory = Wow64ResolveObjectDirectory();
    ConnectionInfoSize = sizeof(Wow64State.CsrConnectInfo);

    Status = CsrClientConnectToServer(ObjectDirectory,
                                      BASESRV_SERVERDLL_INDEX,
                                      &Wow64State.CsrConnectInfo,
                                      &ConnectionInfoSize,
                                      &ServerToServer);
    if (!NT_SUCCESS(Status))
    {
        Wow64ReportStub("Wow64ProcessInit: CSR connect failed");
        return Status;
    }

    Wow64State.CsrConnected = TRUE;
    Wow64State.ServerProcess = ServerToServer;

    /* Register this thread with CSR if not a server process */
    if (!ServerToServer)
    {
        NTSTATUS ThreadStatus = CsrNewThread();
        if (!NT_SUCCESS(ThreadStatus))
        {
            Wow64ReportStub("CsrNewThread failed");
            Status = ThreadStatus;
        }
    }

    /* Deliver any pending APCs that were queued during initialization */
    if (NT_SUCCESS(Status))
    {
        NTSTATUS ApcStatus = Wow64DeliverPendingApc();
        if (!NT_SUCCESS(ApcStatus) &&
            ApcStatus != STATUS_NOT_FOUND &&
            ApcStatus != STATUS_NOT_IMPLEMENTED)
        {
            Wow64ReportStub("Wow64ProcessInit: APC delivery failed");
        }
    }

    /* Opportunistically sync any preserved host context to WOW64 compat */
    Wow64SyncHostContextToCompat();

    Wow64ReportStub("Wow64ProcessInit: Initialization complete");
    return Status;
}

VOID
NTAPI
Wow64ProcessTerm(VOID)
{
    if (Wow64State.ThreadReady)
    {
        CpuNotify(Wow64CpuNotifyThreadDetach, Wow64State.CpuArea, NULL);
        CpuThreadTerm(Wow64State.CpuArea);
        Wow64State.ThreadReady = FALSE;
    }

    if (Wow64State.ProcessReady)
    {
        CpuNotify(Wow64CpuNotifyShutdown, Wow64State.CpuArea, NULL);
        Wow64State.ProcessReady = FALSE;
    }

    /* Clean up KnownDlls directory handle */
    if (Wow64EnvironmentContext.KnownDllsDirectoryHandle)
    {
        NtClose(Wow64EnvironmentContext.KnownDllsDirectoryHandle);
        Wow64EnvironmentContext.KnownDllsDirectoryHandle = NULL;
    }

    /* Clear TLS context */
    Wow64ClearThreadContext();

    /* Free TLS index if allocated */
    if (Wow64TlsIndex != TLS_OUT_OF_INDEXES)
    {
        TlsFree((DWORD)Wow64TlsIndex);
        Wow64TlsIndex = TLS_OUT_OF_INDEXES;
    }

    if (Wow64CallbackTlsIndex != TLS_OUT_OF_INDEXES)
    {
        TlsFree((DWORD)Wow64CallbackTlsIndex);
        Wow64CallbackTlsIndex = TLS_OUT_OF_INDEXES;
    }

    /* Unregister vectored exception handler if set */
    if (Wow64VectoredHandlerHandle)
    {
        RtlRemoveVectoredExceptionHandler(Wow64VectoredHandlerHandle);
        Wow64VectoredHandlerHandle = NULL;
    }

    Wow64State.CsrConnected = FALSE;
    Wow64State.ProcessInfoPublished = FALSE;
    Wow64ReportStub("Wow64ProcessTerm: Cleanup complete");
}

NTSTATUS
NTAPI
Wow64PopPendingApc(
    _Inout_ PWOW64_PENDING_APC PendingApc)
{
    PWOW64_CPU_AREA CpuArea;
    NTSTATUS Status;
    ULONG BufferSize;

    if (!PendingApc)
    {
        return STATUS_INVALID_PARAMETER;
    }

    BufferSize = PendingApc->Size;
    if (BufferSize < sizeof(*PendingApc))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlZeroMemory(PendingApc, BufferSize);
    PendingApc->Size = BufferSize;

    CpuArea = Wow64State.CpuArea;
    if (!CpuArea)
    {
        PendingApc->Version = WOW64_PENDING_APC_VERSION;
        PendingApc->Size = sizeof(*PendingApc);
        return STATUS_NOT_FOUND;
    }

    Status = Wow64CpuTakePendingApc(CpuArea, PendingApc);
    if (!NT_SUCCESS(Status) && Status != STATUS_NOT_FOUND)
    {
        Wow64ReportStub("Wow64PopPendingApc: consume failed");
    }

    return Status;
}

NTSTATUS
NTAPI
Wow64DeliverPendingApc(VOID)
{
    WOW64_PENDING_APC PendingApc;
    PWOW64_CPU_AREA CpuArea;
    WOW64_CONTEXT CompatContext;
    NTSTATUS Status;

    RtlZeroMemory(&PendingApc, sizeof(PendingApc));
    PendingApc.Size = sizeof(PendingApc);

    Status = Wow64PopPendingApc(&PendingApc);
    if (Status == STATUS_NOT_FOUND)
    {
        return STATUS_NOT_FOUND;
    }

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (!(PendingApc.Flags & WOW64_APC_CONTEXT_FLAG_HAS_USER_ROUTINE) ||
        PendingApc.UserRoutine == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    CpuArea = Wow64State.CpuArea;
    if (!CpuArea)
    {
        return STATUS_UNSUCCESSFUL;
    }

    Status = Wow64CpuGetContext(CpuArea, &CompatContext, sizeof(CompatContext));
    if (!NT_SUCCESS(Status))
    {
        RtlZeroMemory(&CompatContext, sizeof(CompatContext));
        CompatContext.ContextFlags = WOW64_CONTEXT_FULL;
        CompatContext.EFlags = 0x00000202;
        CompatContext.SegCs = 0x23;
        CompatContext.SegSs = 0x1B;
        CompatContext.SegDs = 0x23;
        CompatContext.SegEs = 0x23;
        CompatContext.SegFs = 0;
        CompatContext.SegGs = 0;
    }

    CompatContext.Eax = (DWORD)PendingApc.UserContext;
    CompatContext.Ebx = (DWORD)PendingApc.SystemArgument1;
    CompatContext.Ecx = (DWORD)PendingApc.SystemArgument2;
    CompatContext.Eip = (DWORD)PendingApc.UserRoutine;

    Status = Wow64CpuSetContext(CpuArea, &CompatContext, sizeof(CompatContext));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    {
        WOW64_APC_CONTEXT DispatchContext;

        RtlZeroMemory(&DispatchContext, sizeof(DispatchContext));
        DispatchContext.Version = WOW64_APC_CONTEXT_VERSION;
        DispatchContext.Size = sizeof(DispatchContext);
        DispatchContext.Flags = WOW64_APC_CONTEXT_FLAG_KERNEL_FILLED |
                                WOW64_APC_CONTEXT_FLAG_HAS_USER_ROUTINE;
        DispatchContext.UserContext = PendingApc.UserContext;
        DispatchContext.UserRoutine = PendingApc.UserRoutine;
        if (CpuArea)
        {
            DispatchContext.Flags |= WOW64_APC_CONTEXT_FLAG_HAS_CPU_AREA;
            DispatchContext.Wow64CpuArea = (ULONG_PTR)CpuArea;
        }

        Status = Wow64CpuSetPendingApc(CpuArea,
                                       &DispatchContext,
                                       PendingApc.SystemArgument1,
                                       PendingApc.SystemArgument2);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    Status = Wow64CpuDispatchPendingApc(CpuArea);
    if (Status == STATUS_PENDING)
    {
        return Status;
    }

    if (!NT_SUCCESS(Status))
    {
        if (Status != STATUS_NOT_IMPLEMENTED)
        {
            Wow64ReportStub("Wow64DeliverPendingApc: dispatcher failed");
        }

        return Status;
    }

    return Status;
}

NTSTATUS
NTAPI
Wow64KiUserCallbackDispatcher(
    _In_ ULONG ApiNumber,
    _In_reads_bytes_opt_(InputLength) PVOID InputBuffer,
    _In_ ULONG InputLength,
    _Outptr_result_maybenull_ PVOID *OutputBuffer,
    _Out_opt_ PULONG OutputLength)
{
    PWOW64_CPU_AREA CpuArea;
    WOW64_CONTEXT CompatContext;
    CONTEXT NativeContext;
    NTSTATUS Status;

    if (OutputBuffer)
    {
        *OutputBuffer = NULL;
    }

    if (OutputLength)
    {
        *OutputLength = 0;
    }

    CpuArea = (PWOW64_CPU_AREA)Wow64GetThreadContext();
    if (!CpuArea)
    {
        CpuArea = Wow64State.CpuArea;
    }

    if (!CpuArea)
    {
        Wow64ReportStub("Wow64KiUserCallbackDispatcher: no CPU area");
        return STATUS_NOT_SUPPORTED;
    }

    if (!Wow64EnsureCallbackTlsIndex())
    {
        return STATUS_NO_MEMORY;
    }

    Status = Wow64CpuGetContext(CpuArea, &CompatContext, sizeof(CompatContext));
    if (!NT_SUCCESS(Status))
    {
        RtlZeroMemory(&CompatContext, sizeof(CompatContext));
        CompatContext.ContextFlags = WOW64_CONTEXT_FULL;
        CompatContext.EFlags = 0x00000202;
        CompatContext.SegCs = 0x23;
        CompatContext.SegSs = 0x1B;
        CompatContext.SegDs = 0x23;
        CompatContext.SegEs = 0x23;
        CompatContext.SegFs = 0;
        CompatContext.SegGs = 0;
    }

    Status = Wow64CpuPrepareCallback(CpuArea,
                                     &CompatContext,
                                     ApiNumber,
                                     InputBuffer,
                                     InputLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = Wow64TransitionToNative(CpuArea, &CompatContext, &NativeContext);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    (void)Wow64SetThreadContext(CpuArea);

    {
        WOW64_CALLBACK_FRAME CallbackFrame;

        CallbackFrame.Previous = Wow64GetCallbackFrame();
        CallbackFrame.CpuArea = CpuArea;
        CallbackFrame.OutputBuffer = OutputBuffer;
        CallbackFrame.OutputLength = OutputLength;
        CallbackFrame.Flags = WOW64_CALLBACK_FRAME_FLAG_CALLBACK;

        NTSTATUS ContinueStatus;

        Status = STATUS_PENDING;
        Wow64SetCallbackFrame(&CallbackFrame);

        ContinueStatus = NtContinue(&NativeContext, FALSE);

        Wow64SetCallbackFrame(CallbackFrame.Previous);

        if (!NT_SUCCESS(ContinueStatus))
        {
            Status = ContinueStatus;
        }
    }

    return Status;
}

NTSTATUS
NTAPI
Wow64KiUserApcDispatcher(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _Inout_ WOW64_CONTEXT *CompatContext,
    _Inout_ CONTEXT *NativeContext)
{
    NTSTATUS Status;

    if (!CpuArea || !CompatContext || !NativeContext)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!Wow64EnsureCallbackTlsIndex())
    {
        return STATUS_NO_MEMORY;
    }

    (void)Wow64SetThreadContext(CpuArea);

    {
        WOW64_CALLBACK_FRAME Frame;

        Frame.Previous = Wow64GetCallbackFrame();
        Frame.CpuArea = CpuArea;
        Frame.OutputBuffer = NULL;
        Frame.OutputLength = NULL;
        Frame.Flags = WOW64_CALLBACK_FRAME_FLAG_APC;
        NTSTATUS ContinueStatus;

        Status = STATUS_PENDING;
        Wow64SetCallbackFrame(&Frame);

        ContinueStatus = NtContinue(NativeContext, FALSE);

        Wow64SetCallbackFrame(Frame.Previous);

        if (!NT_SUCCESS(ContinueStatus))
        {
            Status = ContinueStatus;
        }
    }

    return Status;
}

VOID
WINAPI
Wow64NotifyApcReturn(VOID)
{
    PWOW64_CALLBACK_FRAME Frame;

    Frame = Wow64GetCallbackFrame();
    if (!Frame)
    {
        return;
    }

    if (!(Frame->Flags & WOW64_CALLBACK_FRAME_FLAG_APC))
    {
        return;
    }

    Wow64SetCallbackFrame(Frame->Previous);
}

NTSTATUS
NTAPI
Wow64KiUserExceptionDispatcher(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_ WOW64_CONTEXT *CompatContext,
    _Inout_ CONTEXT *NativeContext)
{
    ULONG Dispatcher32;
    ULONG_PTR Stack32;
    NTSTATUS Status;

    if (!CpuArea || !ExceptionRecord || !CompatContext || !NativeContext)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!Wow64EnsureCallbackTlsIndex())
    {
        return STATUS_NO_MEMORY;
    }

    (void)Wow64SetThreadContext(CpuArea);

    /* Resolve 32-bit KiUserExceptionDispatcher address from KUSER_SHARED_DATA */
    Dispatcher32 = Wow64GetExceptionDispatcher32();
    if (!Dispatcher32)
    {
        /* Fallback: resume native execution */
        return NtContinue(NativeContext, FALSE);
    }

    /* Prepare a 32-bit stack with arguments: (PEXCEPTION_RECORD32, PCONTEXT32) */
    Stack32 = (ULONG_PTR)CompatContext->Esp;
    if (Stack32 < 8)
    {
        return STATUS_STACK_OVERFLOW;
    }

    {
        ULONG_PTR ArgPtr = Stack32 - 8;
        *(ULONG UNALIGNED *)(ULONG_PTR)ArgPtr = (ULONG)Wow64ExceptionBridge.ExRecord32;
        *(ULONG UNALIGNED *)(ULONG_PTR)(ArgPtr + 4) = (ULONG)Wow64ExceptionBridge.Ctx32;
        Stack32 = ArgPtr;
    }

    /* Update ESP in the compat context to reflect the newly pushed args */
    CompatContext->Esp = (DWORD)Stack32;

    /* Hand off to wow64cpu to transition and execute the 32-bit dispatcher */
    Status = CpupDoCallBack(Dispatcher32,
                            Stack32,
                            CompatContext,
                            NativeContext);

    if (Status == STATUS_PENDING)
    {
        return Status;
    }

    /* If the dispatcher returned, resume native execution as a fallback */
    return NtContinue(NativeContext, FALSE);
}

/* ===================================================================
 * Syscall Thunking Infrastructure
 * ================================================================ */

typedef NTSTATUS (NTAPI *PFN_WOW64_SYSCALL_THUNK)(ULONG_PTR *Args32);

typedef struct _WOW64_SYSCALL_ENTRY
{
    ULONG ServiceNumber;
    ULONG ArgumentCount;
    PFN_WOW64_SYSCALL_THUNK ThunkFunction;
    LPCSTR ServiceName;
} WOW64_SYSCALL_ENTRY, *PWOW64_SYSCALL_ENTRY;

static ULONGLONG
Wow64ReadUlong64Argument(
    _In_reads_(2) const ULONG_PTR *Args32)
{
    ULONGLONG LowPart;
    ULONGLONG HighPart;

    LowPart = (ULONGLONG)(ULONG)Args32[0];
    HighPart = (ULONGLONG)(ULONG)Args32[1];

    return (HighPart << 32) | LowPart;
}

static VOID
Wow64ConvertUnicodeString32To64(
    _In_ ULONG_PTR UnicodeString32,
    _Out_ PUNICODE_STRING UnicodeString64)
{
    UNICODE_STRING32 *String32;

    if (!UnicodeString64)
    {
        return;
    }

    if (!UnicodeString32)
    {
        RtlZeroMemory(UnicodeString64, sizeof(*UnicodeString64));
        return;
    }

    String32 = (UNICODE_STRING32 *)(ULONG_PTR)UnicodeString32;
    UnicodeString64->Length = String32->Length;
    UnicodeString64->MaximumLength = String32->MaximumLength;
    UnicodeString64->Buffer = (PWSTR)(ULONG_PTR)String32->Buffer;
}

/* Parameter conversion helpers */
static VOID
Wow64ConvertObjectAttributes32To64(
    _In_ ULONG_PTR ObjectAttributes32,
    _Out_ POBJECT_ATTRIBUTES ObjectAttributes64)
{
    typedef struct _OBJECT_ATTRIBUTES32
    {
        ULONG Length;
        ULONG RootDirectory;
        ULONG ObjectName;
        ULONG Attributes;
        ULONG SecurityDescriptor;
        ULONG SecurityQualityOfService;
    } OBJECT_ATTRIBUTES32;

    OBJECT_ATTRIBUTES32 *Oa32;

    if (!ObjectAttributes32 || !ObjectAttributes64)
    {
        if (ObjectAttributes64)
        {
            RtlZeroMemory(ObjectAttributes64, sizeof(*ObjectAttributes64));
        }
        return;
    }

    Oa32 = (OBJECT_ATTRIBUTES32 *)(ULONG_PTR)ObjectAttributes32;

    ObjectAttributes64->Length = sizeof(OBJECT_ATTRIBUTES);
    ObjectAttributes64->RootDirectory = (HANDLE)(ULONG_PTR)Oa32->RootDirectory;
    ObjectAttributes64->ObjectName = (PUNICODE_STRING)(ULONG_PTR)Oa32->ObjectName;
    ObjectAttributes64->Attributes = Oa32->Attributes;
    ObjectAttributes64->SecurityDescriptor = (PVOID)(ULONG_PTR)Oa32->SecurityDescriptor;
    ObjectAttributes64->SecurityQualityOfService = (PVOID)(ULONG_PTR)Oa32->SecurityQualityOfService;
}

static VOID
Wow64ConvertClientId32To64(
    _In_ ULONG_PTR ClientId32,
    _Out_ PCLIENT_ID ClientId64)
{
    typedef struct _CLIENT_ID32
    {
        ULONG UniqueProcess;
        ULONG UniqueThread;
    } CLIENT_ID32;

    CLIENT_ID32 *Cid32;

    if (!ClientId64)
    {
        return;
    }

    if (!ClientId32)
    {
        RtlZeroMemory(ClientId64, sizeof(*ClientId64));
        return;
    }

    Cid32 = (CLIENT_ID32 *)(ULONG_PTR)ClientId32;
    ClientId64->UniqueProcess = (HANDLE)(ULONG_PTR)Cid32->UniqueProcess;
    ClientId64->UniqueThread  = (HANDLE)(ULONG_PTR)Cid32->UniqueThread;
}

/* ================================================================
 * PS_ATTRIBUTE_LIST / PS_CREATE_INFO conversions (32 -> 64)
 * Minimal helpers to support future NtCreateUserProcess/NtCreateThreadEx
 * thunking. These functions copy scalar fields and widen pointers.
 * ================================================================ */

typedef struct _PS_ATTRIBUTE32
{
    ULONG Attribute;
    ULONG Size;
    union
    {
        ULONG Value;
        ULONG ValuePtr;
    } u;
    ULONG ReturnLength; /* pointer-sized in native, 32-bit here */
} PS_ATTRIBUTE32;

typedef struct _PS_ATTRIBUTE_LIST32
{
    ULONG TotalLength; /* total size in bytes of this struct + attributes */
    PS_ATTRIBUTE32 Attributes[1];
} PS_ATTRIBUTE_LIST32, *PPS_ATTRIBUTE_LIST32;
/* WOW64-local mirror for native PS_ATTRIBUTE to avoid pulling winternl.h */
typedef struct _WOW64_PS_ATTRIBUTE
{
    ULONG_PTR Attribute;
    SIZE_T    Size;
    ULONG_PTR Value;
    SIZE_T   *ReturnLength;
} WOW64_PS_ATTRIBUTE;

typedef struct _WOW64_PS_ATTRIBUTE_LIST
{
    SIZE_T TotalLength;
    WOW64_PS_ATTRIBUTE Attributes[1];
} WOW64_PS_ATTRIBUTE_LIST, *PWOW64_PS_ATTRIBUTE_LIST;

static SIZE_T WOW64_UNUSED
Wow64CountPsAttributes32(
    _In_ ULONG_PTR AttributeList32)
{
    PPS_ATTRIBUTE_LIST32 List32;
    ULONG total;

    if (!AttributeList32)
        return 0;

    List32 = (PPS_ATTRIBUTE_LIST32)(ULONG_PTR)AttributeList32;
    total = List32->TotalLength;
    if (total < sizeof(ULONG))
        return 0;

    /* Attributes area starts right after TotalLength */
    if (total < sizeof(ULONG) + sizeof(PS_ATTRIBUTE32))
        return 0;

    return (total - sizeof(ULONG)) / sizeof(PS_ATTRIBUTE32);
}

static SIZE_T WOW64_UNUSED
Wow64ConvertPsAttributeList32To64(
    _In_ ULONG_PTR AttributeList32,
    _Out_writes_(Capacity) PWOW64_PS_ATTRIBUTE_LIST AttributeList64,
    _In_ SIZE_T Capacity)
{
    PPS_ATTRIBUTE_LIST32 List32;
    SIZE_T Count, i;

    if (!AttributeList64 || Capacity == 0)
        return 0;

    if (!AttributeList32)
    {
        AttributeList64->TotalLength = sizeof(WOW64_PS_ATTRIBUTE_LIST);
        return 0;
    }

    List32 = (PPS_ATTRIBUTE_LIST32)(ULONG_PTR)AttributeList32;
    Count = Wow64CountPsAttributes32(AttributeList32);
    if (Count > Capacity)
        Count = Capacity;

    for (i = 0; i < Count; i++)
    {
        AttributeList64->Attributes[i].Attribute = (ULONG_PTR)List32->Attributes[i].Attribute;
        AttributeList64->Attributes[i].Size = (SIZE_T)List32->Attributes[i].Size;
        /* Heuristically treat value as pointer-sized; widen to 64-bit */
        AttributeList64->Attributes[i].Value = (ULONG_PTR)(ULONG)List32->Attributes[i].u.Value;
        AttributeList64->Attributes[i].ReturnLength = (SIZE_T *)(ULONG_PTR)List32->Attributes[i].ReturnLength;
    }

    AttributeList64->TotalLength = sizeof(WOW64_PS_ATTRIBUTE_LIST) + Count * sizeof(WOW64_PS_ATTRIBUTE);
    return Count;
}

/* Minimal mirror of PS_CREATE_STATE values used in conversion */
typedef enum _WOW64_PS_CREATE_STATE
{
    PsCreateInitialState,
    PsCreateFailOnFileOpen,
    PsCreateFailOnSectionCreate,
    PsCreateFailExeFormat,
    PsCreateFailMachineMismatch,
    PsCreateFailExeName,
    PsCreateSuccess,
    PsCreateMaximumStates
} WOW64_PS_CREATE_STATE;

typedef struct _PS_CREATE_INFO32
{
    ULONG Size;
    ULONG State;
    union
    {
        struct { ULONG InitFlags; ULONG AdditionalFileAccess; } InitState;
        struct { ULONG FileHandle; } FailSection;
        struct { USHORT DllCharacteristics; } ExeFormat;
        struct { ULONG IFEOKey; } ExeName;
        struct
        {
            ULONG OutputFlags;
            ULONG FileHandle;
            ULONG SectionHandle;
            ULONGLONG UserProcessParametersNative;
            ULONG UserProcessParametersWow64;
            ULONG CurrentParameterFlags;
            ULONGLONG PebAddressNative;
            ULONG PebAddressWow64;
            ULONGLONG ManifestAddress;
            ULONG ManifestSize;
        } SuccessState;
    } u;
} PS_CREATE_INFO32, *PPS_CREATE_INFO32;
/* WOW64-local mirror for native PS_CREATE_INFO to avoid pulling winternl.h */
typedef struct _WOW64_PS_CREATE_INFO
{
    SIZE_T Size;
    ULONG State;
    union
    {
        struct { ULONG InitFlags; ULONG AdditionalFileAccess; } InitState;
        struct { HANDLE FileHandle; } FailSection;
        struct { USHORT DllCharacteristics; } ExeFormat;
        struct { HANDLE IFEOKey; } ExeName;
        struct
        {
            ULONG OutputFlags;
            HANDLE FileHandle;
            HANDLE SectionHandle;
            ULONGLONG UserProcessParametersNative;
            ULONG UserProcessParametersWow64;
            ULONG CurrentParameterFlags;
            ULONGLONG PebAddressNative;
            ULONG PebAddressWow64;
            ULONGLONG ManifestAddress;
            ULONG ManifestSize;
        } SuccessState;
    } u;
} WOW64_PS_CREATE_INFO, *PWOW64_PS_CREATE_INFO;

static VOID WOW64_UNUSED
Wow64ConvertPsCreateInfo32To64(
    _In_ ULONG_PTR CreateInfo32,
    _Out_ PWOW64_PS_CREATE_INFO CreateInfo64)
{
    PPS_CREATE_INFO32 C32;

    if (!CreateInfo64)
        return;

    if (!CreateInfo32)
    {
        RtlZeroMemory(CreateInfo64, sizeof(*CreateInfo64));
        return;
    }

    C32 = (PPS_CREATE_INFO32)(ULONG_PTR)CreateInfo32;
    RtlZeroMemory(CreateInfo64, sizeof(*CreateInfo64));

    CreateInfo64->Size = sizeof(*CreateInfo64);
    CreateInfo64->State = C32->State;

    switch (CreateInfo64->State)
    {
    case PsCreateInitialState:
        CreateInfo64->u.InitState.InitFlags = C32->u.InitState.InitFlags;
        CreateInfo64->u.InitState.AdditionalFileAccess = C32->u.InitState.AdditionalFileAccess;
        break;
    case PsCreateFailOnSectionCreate:
        CreateInfo64->u.FailSection.FileHandle = (HANDLE)(ULONG_PTR)C32->u.FailSection.FileHandle;
        break;
    case PsCreateFailExeFormat:
        CreateInfo64->u.ExeFormat.DllCharacteristics = C32->u.ExeFormat.DllCharacteristics;
        break;
    case PsCreateFailExeName:
        CreateInfo64->u.ExeName.IFEOKey = (HANDLE)(ULONG_PTR)C32->u.ExeName.IFEOKey;
        break;
    case PsCreateSuccess:
        CreateInfo64->u.SuccessState.OutputFlags = C32->u.SuccessState.OutputFlags;
        CreateInfo64->u.SuccessState.FileHandle = (HANDLE)(ULONG_PTR)C32->u.SuccessState.FileHandle;
        CreateInfo64->u.SuccessState.SectionHandle = (HANDLE)(ULONG_PTR)C32->u.SuccessState.SectionHandle;
        CreateInfo64->u.SuccessState.UserProcessParametersNative = C32->u.SuccessState.UserProcessParametersNative;
        CreateInfo64->u.SuccessState.UserProcessParametersWow64 = C32->u.SuccessState.UserProcessParametersWow64;
        CreateInfo64->u.SuccessState.CurrentParameterFlags = C32->u.SuccessState.CurrentParameterFlags;
        CreateInfo64->u.SuccessState.PebAddressNative = C32->u.SuccessState.PebAddressNative;
        CreateInfo64->u.SuccessState.PebAddressWow64 = C32->u.SuccessState.PebAddressWow64;
        CreateInfo64->u.SuccessState.ManifestAddress = C32->u.SuccessState.ManifestAddress;
        CreateInfo64->u.SuccessState.ManifestSize = C32->u.SuccessState.ManifestSize;
        break;
    default:
        break;
    }
}

/* Minimal 32-bit process parameters mirrors for extracting a few strings */
typedef struct _WOW64_CURDIR32
{
    UNICODE_STRING32 DosPath;
    ULONG Handle; /* HANDLE (32-bit) */
} WOW64_CURDIR32, *PWOW64_CURDIR32;

typedef struct _WOW64_RTL_USER_PROCESS_PARAMETERS32
{
    ULONG MaximumLength;
    ULONG Length;
    ULONG Flags;
    ULONG DebugFlags;
    ULONG ConsoleHandle;
    ULONG ConsoleFlags;
    ULONG StdInput;
    ULONG StdOutput;
    ULONG StdError;
    WOW64_CURDIR32 CurrentDirectory;
    UNICODE_STRING32 DllPath;
    UNICODE_STRING32 ImagePathName;
    UNICODE_STRING32 CommandLine;
} WOW64_RTL_USER_PROCESS_PARAMETERS32, *PWOW64_RTL_USER_PROCESS_PARAMETERS32;

static VOID
Wow64ConvertIoStatusBlock32To64(
    _In_ ULONG_PTR IoStatusBlock32,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock64)
{
    if (!IoStatusBlock64)
    {
        return;
    }

    if (!IoStatusBlock32)
    {
        RtlZeroMemory(IoStatusBlock64, sizeof(*IoStatusBlock64));
        return;
    }

    /* IO_STATUS_BLOCK is same size on 32/64, but pointers differ */
    IoStatusBlock64->Status = STATUS_SUCCESS;
    IoStatusBlock64->Information = 0;
}

static VOID
Wow64ConvertIoStatusBlock64To32(
    _In_ PIO_STATUS_BLOCK IoStatusBlock64,
    _In_ ULONG_PTR IoStatusBlock32)
{
    typedef struct _IO_STATUS_BLOCK32
    {
        NTSTATUS Status;
        ULONG Information;
    } IO_STATUS_BLOCK32;

    IO_STATUS_BLOCK32 *Iosb32;

    if (!IoStatusBlock32 || !IoStatusBlock64)
    {
        return;
    }

    Iosb32 = (IO_STATUS_BLOCK32 *)(ULONG_PTR)IoStatusBlock32;
    Iosb32->Status = IoStatusBlock64->Status;
    Iosb32->Information = (ULONG)IoStatusBlock64->Information;
}

/* ===================================================================
 * File I/O Syscall Thunks
 * ================================================================ */

static NTSTATUS NTAPI
Wow64Thunk_NtCreateFile(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE FileHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    IO_STATUS_BLOCK IoStatusBlock64;
    PLARGE_INTEGER AllocationSize;
    ULONG FileAttributes;
    ULONG ShareAccess;
    ULONG CreateDisposition;
    ULONG CreateOptions;
    PVOID EaBuffer;
    ULONG EaLength;
    HANDLE FileHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    FileHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);
    Wow64ConvertIoStatusBlock32To64(Args32[3], &IoStatusBlock64);
    AllocationSize = (PLARGE_INTEGER)(ULONG_PTR)Args32[4];
    FileAttributes = (ULONG)Args32[5];
    ShareAccess = (ULONG)Args32[6];
    CreateDisposition = (ULONG)Args32[7];
    CreateOptions = (ULONG)Args32[8];
    EaBuffer = (PVOID)(ULONG_PTR)Args32[9];
    EaLength = (ULONG)Args32[10];

    Status = NtCreateFile(&FileHandle64,
                          DesiredAccess,
                          &ObjectAttributes64,
                          &IoStatusBlock64,
                          AllocationSize,
                          FileAttributes,
                          ShareAccess,
                          CreateDisposition,
                          CreateOptions,
                          EaBuffer,
                          EaLength);

    if (FileHandle32)
    {
        *(ULONG *)(ULONG_PTR)FileHandle32 = (ULONG)(ULONG_PTR)FileHandle64;
    }

    Wow64ConvertIoStatusBlock64To32(&IoStatusBlock64, Args32[3]);

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtOpenFile(
    _In_ ULONG_PTR *Args32)
{
    NTSTATUS Status;
    HANDLE FileHandle64 = NULL;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    IO_STATUS_BLOCK IoStatusBlock64;
    ACCESS_MASK DesiredAccess;
    ULONG ShareAccess;
    ULONG OpenOptions;

    /* Args32[0] = FileHandle* (32-bit pointer)
     * Args32[1] = DesiredAccess
     * Args32[2] = ObjectAttributes* (32-bit pointer)
     * Args32[3] = IoStatusBlock* (32-bit pointer)
     * Args32[4] = ShareAccess
     * Args32[5] = OpenOptions
     */

    DesiredAccess = (ACCESS_MASK)Args32[1];
    ShareAccess = (ULONG)Args32[4];
    OpenOptions = (ULONG)Args32[5];

    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);
    Wow64ConvertIoStatusBlock32To64(Args32[3], &IoStatusBlock64);

    Status = NtOpenFile(&FileHandle64,
                        DesiredAccess,
                        &ObjectAttributes64,
                        &IoStatusBlock64,
                        ShareAccess,
                        OpenOptions);

    if (Args32[0])
    {
        *(ULONG *)(ULONG_PTR)Args32[0] = (ULONG)(ULONG_PTR)FileHandle64;
    }

    Wow64ConvertIoStatusBlock64To32(&IoStatusBlock64, Args32[3]);

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtQueryInformationFile(
    _In_ ULONG_PTR *Args32)
{
    HANDLE FileHandle;
    IO_STATUS_BLOCK IoStatusBlock64;
    PVOID FileInformation;
    ULONG Length;
    FILE_INFORMATION_CLASS FileInformationClass;
    NTSTATUS Status;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    FileHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    Wow64ConvertIoStatusBlock32To64(Args32[1], &IoStatusBlock64);
    FileInformation = (PVOID)(ULONG_PTR)Args32[2];
    Length = (ULONG)Args32[3];
    FileInformationClass = (FILE_INFORMATION_CLASS)Args32[4];

    Status = NtQueryInformationFile(FileHandle,
                                    &IoStatusBlock64,
                                    FileInformation,
                                    Length,
                                    FileInformationClass);

    Wow64ConvertIoStatusBlock64To32(&IoStatusBlock64, Args32[1]);

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtSetInformationFile(
    _In_ ULONG_PTR *Args32)
{
    HANDLE FileHandle;
    IO_STATUS_BLOCK IoStatusBlock64;
    PVOID FileInformation;
    ULONG Length;
    FILE_INFORMATION_CLASS FileInformationClass;
    NTSTATUS Status;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    FileHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    Wow64ConvertIoStatusBlock32To64(Args32[1], &IoStatusBlock64);
    FileInformation = (PVOID)(ULONG_PTR)Args32[2];
    Length = (ULONG)Args32[3];
    FileInformationClass = (FILE_INFORMATION_CLASS)Args32[4];

    Status = NtSetInformationFile(FileHandle,
                                  &IoStatusBlock64,
                                  FileInformation,
                                  Length,
                                  FileInformationClass);

    Wow64ConvertIoStatusBlock64To32(&IoStatusBlock64, Args32[1]);

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtDeleteFile(
    _In_ ULONG_PTR *Args32)
{
    OBJECT_ATTRIBUTES ObjectAttributes64;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Wow64ConvertObjectAttributes32To64(Args32[0], &ObjectAttributes64);

    return NtDeleteFile(&ObjectAttributes64);
}

static NTSTATUS NTAPI
Wow64Thunk_NtFlushBuffersFile(
    _In_ ULONG_PTR *Args32)
{
    IO_STATUS_BLOCK IoStatusBlock64;
    HANDLE FileHandle;
    NTSTATUS Status;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    FileHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    Wow64ConvertIoStatusBlock32To64(Args32[1], &IoStatusBlock64);

    Status = NtFlushBuffersFile(FileHandle, &IoStatusBlock64);

    Wow64ConvertIoStatusBlock64To32(&IoStatusBlock64, Args32[1]);

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtQueryDirectoryFile(
    _In_ ULONG_PTR *Args32)
{
    HANDLE FileHandle;
    HANDLE Event;
    PIO_APC_ROUTINE ApcRoutine;
    PVOID ApcContext;
    IO_STATUS_BLOCK IoStatusBlock64;
    PVOID FileInformation;
    ULONG Length;
    FILE_INFORMATION_CLASS FileInformationClass;
    BOOLEAN ReturnSingleEntry;
    UNICODE_STRING FileName64;
    PUNICODE_STRING FileName;
    BOOLEAN RestartScan;
    NTSTATUS Status;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    FileHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    Event = (HANDLE)(ULONG_PTR)(ULONG)Args32[1];
    ApcRoutine = (PIO_APC_ROUTINE)(ULONG_PTR)Args32[2];
    ApcContext = (PVOID)(ULONG_PTR)Args32[3];
    Wow64ConvertIoStatusBlock32To64(Args32[4], &IoStatusBlock64);
    FileInformation = (PVOID)(ULONG_PTR)Args32[5];
    Length = (ULONG)Args32[6];
    FileInformationClass = (FILE_INFORMATION_CLASS)Args32[7];
    ReturnSingleEntry = (BOOLEAN)Args32[8];
    RestartScan = (BOOLEAN)Args32[10];

    if (Args32[9])
    {
        Wow64ConvertUnicodeString32To64(Args32[9], &FileName64);
        FileName = &FileName64;
    }
    else
    {
        FileName = NULL;
    }

    Status = NtQueryDirectoryFile(FileHandle,
                                  Event,
                                  ApcRoutine,
                                  ApcContext,
                                  &IoStatusBlock64,
                                  FileInformation,
                                  Length,
                                  FileInformationClass,
                                  ReturnSingleEntry,
                                  FileName,
                                  RestartScan);

    Wow64ConvertIoStatusBlock64To32(&IoStatusBlock64, Args32[4]);

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtQueryFullAttributesFile(
    _In_ ULONG_PTR *Args32)
{
    OBJECT_ATTRIBUTES ObjectAttributes64;
    PFILE_NETWORK_OPEN_INFORMATION FileInformation;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Wow64ConvertObjectAttributes32To64(Args32[0], &ObjectAttributes64);
    FileInformation = (PFILE_NETWORK_OPEN_INFORMATION)(ULONG_PTR)Args32[1];

    return NtQueryFullAttributesFile(&ObjectAttributes64,
                                     FileInformation);
}

static NTSTATUS NTAPI
Wow64Thunk_NtOpenProcess(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE ProcessHandle;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    CLIENT_ID ClientId64;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ProcessHandle = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);
    Wow64ConvertClientId32To64(Args32[3], &ClientId64);

    return NtOpenProcess(ProcessHandle,
                         DesiredAccess,
                         &ObjectAttributes64,
                         &ClientId64);
}

static NTSTATUS NTAPI
Wow64Thunk_NtOpenThread(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE ThreadHandle;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    CLIENT_ID ClientId64;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ThreadHandle = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);
    Wow64ConvertClientId32To64(Args32[3], &ClientId64);

    return NtOpenThread(ThreadHandle,
                        DesiredAccess,
                        &ObjectAttributes64,
                        &ClientId64);
}

static NTSTATUS NTAPI
Wow64Thunk_NtDuplicateObject(
    _In_ ULONG_PTR *Args32)
{
    HANDLE SourceProcessHandle;
    HANDLE SourceHandle;
    HANDLE TargetProcessHandle;
    PHANDLE TargetHandle;
    ACCESS_MASK DesiredAccess;
    ULONG Attributes;
    ULONG Options;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    SourceProcessHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    SourceHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[1];
    TargetProcessHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[2];
    TargetHandle = (PHANDLE)(ULONG_PTR)Args32[3];
    DesiredAccess = (ACCESS_MASK)Args32[4];
    Attributes = (ULONG)Args32[5];
    Options = (ULONG)Args32[6];

    return NtDuplicateObject(SourceProcessHandle,
                             SourceHandle,
                             TargetProcessHandle,
                             TargetHandle,
                             DesiredAccess,
                             Attributes,
                             Options);
}

static NTSTATUS NTAPI
Wow64Thunk_NtWaitForMultipleObjects(
    _In_ ULONG_PTR *Args32)
{
    ULONG Count;
    PHANDLE Handles;
    WAIT_TYPE WaitType;
    BOOLEAN Alertable;
    PLARGE_INTEGER Timeout;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Count = (ULONG)Args32[0];
    Handles = (PHANDLE)(ULONG_PTR)Args32[1];
    WaitType = (WAIT_TYPE)Args32[2];
    Alertable = (BOOLEAN)Args32[3];
    Timeout = (PLARGE_INTEGER)(ULONG_PTR)Args32[4];

    return NtWaitForMultipleObjects(Count,
                                    Handles,
                                    WaitType,
                                    Alertable,
                                    Timeout);
}

static NTSTATUS NTAPI
Wow64Thunk_NtSignalAndWaitForSingleObject(
    _In_ ULONG_PTR *Args32)
{
    HANDLE SignalHandle;
    HANDLE WaitHandle;
    BOOLEAN Alertable;
    PLARGE_INTEGER Timeout;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    SignalHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    WaitHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[1];
    Alertable = (BOOLEAN)Args32[2];
    Timeout = (PLARGE_INTEGER)(ULONG_PTR)Args32[3];

    return NtSignalAndWaitForSingleObject(SignalHandle,
                                          WaitHandle,
                                          Alertable,
                                          Timeout);
}

static NTSTATUS NTAPI
Wow64Thunk_NtQueryInformationThread(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ThreadHandle;
    THREADINFOCLASS ThreadInformationClass;
    PVOID ThreadInformation;
    ULONG ThreadInformationLength;
    PULONG ReturnLength;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ThreadHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    ThreadInformationClass = (THREADINFOCLASS)Args32[1];
    ThreadInformation = (PVOID)(ULONG_PTR)Args32[2];
    ThreadInformationLength = (ULONG)Args32[3];
    ReturnLength = (PULONG)(ULONG_PTR)Args32[4];

    return NtQueryInformationThread(ThreadHandle,
                                    ThreadInformationClass,
                                    ThreadInformation,
                                    ThreadInformationLength,
                                    ReturnLength);
}

static NTSTATUS NTAPI
Wow64Thunk_NtSetInformationThread(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ThreadHandle;
    THREADINFOCLASS ThreadInformationClass;
    PVOID ThreadInformation;
    ULONG ThreadInformationLength;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ThreadHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    ThreadInformationClass = (THREADINFOCLASS)Args32[1];
    ThreadInformation = (PVOID)(ULONG_PTR)Args32[2];
    ThreadInformationLength = (ULONG)Args32[3];

    return NtSetInformationThread(ThreadHandle,
                                  ThreadInformationClass,
                                  ThreadInformation,
                                  ThreadInformationLength);
}

static NTSTATUS NTAPI
Wow64Thunk_NtQueryObject(
    _In_ ULONG_PTR *Args32)
{
    HANDLE Handle;
    OBJECT_INFORMATION_CLASS ObjectInformationClass;
    PVOID ObjectInformation;
    ULONG Length;
    PULONG ReturnLength;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Handle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    ObjectInformationClass = (OBJECT_INFORMATION_CLASS)Args32[1];
    ObjectInformation = (PVOID)(ULONG_PTR)Args32[2];
    Length = (ULONG)Args32[3];
    ReturnLength = (PULONG)(ULONG_PTR)Args32[4];

    return NtQueryObject(Handle,
                         ObjectInformationClass,
                         ObjectInformation,
                         Length,
                         ReturnLength);
}

static NTSTATUS NTAPI
Wow64Thunk_NtDeviceIoControlFile(
    _In_ ULONG_PTR *Args32)
{
    HANDLE FileHandle;
    HANDLE Event;
    PIO_APC_ROUTINE ApcRoutine;
    PVOID ApcContext;
    IO_STATUS_BLOCK IoStatusBlock64;
    ULONG IoControlCode;
    PVOID InputBuffer;
    ULONG InputBufferLength;
    PVOID OutputBuffer;
    ULONG OutputBufferLength;
    NTSTATUS Status;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    FileHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    Event = (HANDLE)(ULONG_PTR)(ULONG)Args32[1];
    ApcRoutine = (PIO_APC_ROUTINE)(ULONG_PTR)Args32[2];
    ApcContext = (PVOID)(ULONG_PTR)Args32[3];
    Wow64ConvertIoStatusBlock32To64(Args32[4], &IoStatusBlock64);
    IoControlCode = (ULONG)Args32[5];
    InputBuffer = (PVOID)(ULONG_PTR)Args32[6];
    InputBufferLength = (ULONG)Args32[7];
    OutputBuffer = (PVOID)(ULONG_PTR)Args32[8];
    OutputBufferLength = (ULONG)Args32[9];

    Status = NtDeviceIoControlFile(FileHandle,
                                   Event,
                                   ApcRoutine,
                                   ApcContext,
                                   &IoStatusBlock64,
                                   IoControlCode,
                                   InputBuffer,
                                   InputBufferLength,
                                   OutputBuffer,
                                   OutputBufferLength);

    Wow64ConvertIoStatusBlock64To32(&IoStatusBlock64, Args32[4]);

    return Status;
}

/* Placeholders for advanced process/thread creation (partial implementation).
 * These are wired in user-mode via ntdll exports rather than direct syscalls
 * in this ReactOS configuration; we provide stubs to aid future conversion work.
 */
static NTSTATUS WOW64_UNUSED NTAPI
Wow64Thunk_NtCreateUserProcess(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE ProcessHandle32;
    PHANDLE ThreadHandle32;
    ACCESS_MASK ProcessDesiredAccess;
    ACCESS_MASK ThreadDesiredAccess;
    OBJECT_ATTRIBUTES ProcessObjectAttributes64;
    OBJECT_ATTRIBUTES ThreadObjectAttributes64;
    ULONG ProcessFlags;
    ULONG ThreadFlags;
    ULONG_PTR ProcessParameters32;
    ULONG_PTR CreateInfo32;
    ULONG_PTR AttributeList32;
    WOW64_PS_CREATE_INFO CreateInfo;
    HANDLE ParentHandle = NtCurrentProcess();
    BOOLEAN InheritHandles = FALSE;
    UNICODE_STRING ImagePath;
    UNICODE_STRING DllPath = (UNICODE_STRING){0};
    UNICODE_STRING CommandLine = (UNICODE_STRING){0};
    UNICODE_STRING CurrentDirectoryPath = (UNICODE_STRING){0};
    BOOLEAN HaveImage = FALSE;
    /* Standard handle propagation from 32-bit params */
    HANDLE StdInputHandle = NULL;
    HANDLE StdOutputHandle = NULL;
    HANDLE StdErrorHandle = NULL;
    BOOLEAN HaveStdHandles = FALSE;
    RTL_USER_PROCESS_INFORMATION ProcInfo;
    PRTL_USER_PROCESS_PARAMETERS ProcParams = NULL;
    NTSTATUS Status;
    /* Local attribute list buffer for up to 8 entries */
    struct { WOW64_PS_ATTRIBUTE_LIST List; WOW64_PS_ATTRIBUTE Extra[7]; } AttrBuf;
    WOW64_PS_ATTRIBUTE_LIST *AttrList64 = &AttrBuf.List;
    SIZE_T AttrCapacity = 8; /* reasonable default */

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ProcessHandle32        = (PHANDLE)(ULONG_PTR)Args32[0];
    ThreadHandle32         = (PHANDLE)(ULONG_PTR)Args32[1];
    ProcessDesiredAccess   = (ACCESS_MASK)Args32[2];
    ThreadDesiredAccess    = (ACCESS_MASK)Args32[3];
    Wow64ConvertObjectAttributes32To64(Args32[4], &ProcessObjectAttributes64);
    Wow64ConvertObjectAttributes32To64(Args32[5], &ThreadObjectAttributes64);
    ProcessFlags           = (ULONG)Args32[6];
    ThreadFlags            = (ULONG)Args32[7];
    ProcessParameters32    = Args32[8];
    CreateInfo32           = Args32[9];
    AttributeList32        = Args32[10];

    UNREFERENCED_PARAMETER(ProcessDesiredAccess);
    UNREFERENCED_PARAMETER(ThreadDesiredAccess);
    UNREFERENCED_PARAMETER(ProcessFlags);
    UNREFERENCED_PARAMETER(ThreadFlags);
    /* If a 32-bit ProcessParameters is provided, extract strings */
    if (ProcessParameters32)
    {
        PWOW64_RTL_USER_PROCESS_PARAMETERS32 Pp32;
        Pp32 = (PWOW64_RTL_USER_PROCESS_PARAMETERS32)(ULONG_PTR)ProcessParameters32;
        /* Prefer ImagePathName from parameters if present */
        if (Pp32->ImagePathName.Buffer && Pp32->ImagePathName.Length)
        {
            Wow64ConvertUnicodeString32To64((ULONG_PTR)&Pp32->ImagePathName, &ImagePath);
            HaveImage = (ImagePath.Buffer != NULL && ImagePath.Length > 0);
        }
        if (Pp32->DllPath.Buffer && Pp32->DllPath.Length)
        {
            Wow64ConvertUnicodeString32To64((ULONG_PTR)&Pp32->DllPath, &DllPath);
        }
        if (Pp32->CommandLine.Buffer && Pp32->CommandLine.Length)
        {
            Wow64ConvertUnicodeString32To64((ULONG_PTR)&Pp32->CommandLine, &CommandLine);
        }
        if (Pp32->CurrentDirectory.DosPath.Buffer && Pp32->CurrentDirectory.DosPath.Length)
        {
            Wow64ConvertUnicodeString32To64((ULONG_PTR)&Pp32->CurrentDirectory.DosPath, &CurrentDirectoryPath);
        }

        /* Capture explicit standard handles if provided */
        if (Pp32->StdInput && Pp32->StdInput != (ULONG)-1)
        {
            StdInputHandle = (HANDLE)(ULONG_PTR)Pp32->StdInput;
            HaveStdHandles = TRUE;
        }
        if (Pp32->StdOutput && Pp32->StdOutput != (ULONG)-1)
        {
            StdOutputHandle = (HANDLE)(ULONG_PTR)Pp32->StdOutput;
            HaveStdHandles = TRUE;
        }
        if (Pp32->StdError && Pp32->StdError != (ULONG)-1)
        {
            StdErrorHandle = (HANDLE)(ULONG_PTR)Pp32->StdError;
            HaveStdHandles = TRUE;
        }
    }

    /* Convert optional PS_CREATE_INFO and PS_ATTRIBUTE_LIST */
    RtlZeroMemory(&CreateInfo, sizeof(CreateInfo));
    Wow64ConvertPsCreateInfo32To64(CreateInfo32, &CreateInfo);

    RtlZeroMemory(&AttrBuf, sizeof(AttrBuf));
    {
        SIZE_T Count = Wow64CountPsAttributes32(AttributeList32);
        if (Count > AttrCapacity) Count = AttrCapacity;
        (void)Wow64ConvertPsAttributeList32To64(AttributeList32, AttrList64, Count);

        for (SIZE_T i = 0; i < Count; i++)
        {
            ULONG_PTR Attr = AttrList64->Attributes[i].Attribute;
            ULONG Number = (ULONG)(Attr & 0xFFFF);
            switch (Number)
            {
                case 0: /* PsAttributeParentProcess */
                    ParentHandle = (HANDLE)(ULONG_PTR)AttrList64->Attributes[i].Value;
                    break;
                case 5: /* PsAttributeImageName */
                {
                    UNICODE_STRING Name64;
                    Wow64ConvertUnicodeString32To64(AttrList64->Attributes[i].Value, &Name64);
                    ImagePath = Name64;
                    HaveImage = (ImagePath.Buffer != NULL && ImagePath.Length > 0);
                    break;
                }
                case 10: /* PsAttributeStdHandleInfo */
                    InheritHandles = TRUE;
                    break;
                case 11: /* PsAttributeHandleList */
                    InheritHandles = TRUE;
                    break;
                default:
                    break;
            }
        }
    }

    if (!HaveImage)
    {
        if (ProcessHandle32) *(ULONG *)(ULONG_PTR)ProcessHandle32 = 0;
        if (ThreadHandle32)  *(ULONG *)(ULONG_PTR)ThreadHandle32  = 0;
        return STATUS_INVALID_PARAMETER_MIX;
    }

    /* Build minimal process parameters */
    Status = RtlCreateProcessParameters(&ProcParams,
                                        &ImagePath,
                                        (DllPath.Buffer ? &DllPath : NULL),
                                        (CurrentDirectoryPath.Buffer ? &CurrentDirectoryPath : NULL),
                                        (CommandLine.Buffer ? &CommandLine : &ImagePath),
                                        NULL,
                                        NULL, NULL, NULL, NULL);
    /* Apply explicit std handle mapping if provided by caller */
    if (!NT_SUCCESS(Status))
    {
        if (ProcessHandle32) *(ULONG *)(ULONG_PTR)ProcessHandle32 = 0;
        if (ThreadHandle32)  *(ULONG *)(ULONG_PTR)ThreadHandle32  = 0;
        return Status;
    }

    if (HaveStdHandles && ProcParams)
    {
        if (StdInputHandle)
            ProcParams->StandardInput = StdInputHandle;
        if (StdOutputHandle)
            ProcParams->StandardOutput = StdOutputHandle;
        if (StdErrorHandle)
            ProcParams->StandardError = StdErrorHandle;

        /* Ensure we inherit handles so the child gets them if inheritable */
        InheritHandles = TRUE;
    }

    RtlZeroMemory(&ProcInfo, sizeof(ProcInfo));
    ProcInfo.Size = sizeof(ProcInfo);

    Status = RtlCreateUserProcess(&ImagePath,
                                  0,
                                  ProcParams,
                                  ProcessObjectAttributes64.SecurityDescriptor,
                                  ThreadObjectAttributes64.SecurityDescriptor,
                                  ParentHandle,
                                  InheritHandles,
                                  NULL,
                                  NULL,
                                  &ProcInfo);

    RtlDestroyProcessParameters(ProcParams);

    if (!NT_SUCCESS(Status))
    {
        if (ProcessHandle32) *(ULONG *)(ULONG_PTR)ProcessHandle32 = 0;
        if (ThreadHandle32)  *(ULONG *)(ULONG_PTR)ThreadHandle32  = 0;
        return Status;
    }

    if (ProcessHandle32)
        *(ULONG *)(ULONG_PTR)ProcessHandle32 = (ULONG)(ULONG_PTR)ProcInfo.ProcessHandle;
    if (ThreadHandle32)
        *(ULONG *)(ULONG_PTR)ThreadHandle32 = (ULONG)(ULONG_PTR)ProcInfo.ThreadHandle;

    return STATUS_SUCCESS;
}

static NTSTATUS WOW64_UNUSED NTAPI
Wow64Thunk_NtCreateThreadEx(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE ThreadHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    HANDLE ProcessHandle;
    PTHREAD_START_ROUTINE StartRoutine;
    PVOID Argument;
    ULONG CreateFlags;
    ULONG_PTR ZeroBits;
    SIZE_T StackSize;
    SIZE_T MaximumStackSize;
    PVOID AttributeList32; /* Optional PS_ATTRIBUTE_LIST32 */
    HANDLE ThreadHandle64 = NULL;
    CLIENT_ID ClientId;
    BOOLEAN CreateSuspended;
    NTSTATUS Status;
    /* Mapped attributes (minimal subset) */
    ULONG_PTR ClientIdOut32 = 0;        /* points to CLIENT_ID32 */
    ULONG_PTR TebAddrOut32 = 0;         /* points to PVOID (32-bit) */
    /* Optional inputs */
    BOOL HaveGroupAffinity = FALSE;
    GROUP_AFFINITY GroupAffinityIn = {0};
    BOOL HaveIdealProcessor = FALSE;
    PROCESSOR_NUMBER IdealProcessorIn = {0};
    struct { WOW64_PS_ATTRIBUTE_LIST List; WOW64_PS_ATTRIBUTE Extra[7]; } AttrBuf;
    WOW64_PS_ATTRIBUTE_LIST *AttrList64 = &AttrBuf.List;
    SIZE_T AttrCount = 0;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ThreadHandle32   = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess    = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);
    ProcessHandle    = (HANDLE)(ULONG_PTR)(ULONG)Args32[3];
    StartRoutine     = (PTHREAD_START_ROUTINE)(ULONG_PTR)Args32[4];
    Argument         = (PVOID)(ULONG_PTR)Args32[5];
    CreateFlags      = (ULONG)Args32[6];
    ZeroBits         = (ULONG_PTR)Args32[7];
    StackSize        = (SIZE_T)Args32[8];
    MaximumStackSize = (SIZE_T)Args32[9];
    AttributeList32  = (PVOID)(ULONG_PTR)Args32[10];

    UNREFERENCED_PARAMETER(DesiredAccess);

    /* Convert and scan optional attribute list for outputs we can fill */
    RtlZeroMemory(&AttrBuf, sizeof(AttrBuf));
    if (AttributeList32)
    {
        AttrCount = Wow64CountPsAttributes32((ULONG_PTR)AttributeList32);
        if (AttrCount > 8) AttrCount = 8;
        (void)Wow64ConvertPsAttributeList32To64((ULONG_PTR)AttributeList32, AttrList64, AttrCount);

        for (SIZE_T i = 0; i < AttrCount; i++)
        {
            ULONG_PTR Attr = AttrList64->Attributes[i].Attribute;
            ULONG Number = (ULONG)(Attr & 0xFFFF); /* PS_ATTRIBUTE_NUM */
            switch (Number)
            {
                case 3: /* PsAttributeClientId (thread attr) */
                    ClientIdOut32 = AttrList64->Attributes[i].Value;
                    break;
                case 4: /* PsAttributeTebAddress (thread attr) */
                    TebAddrOut32 = AttrList64->Attributes[i].Value;
                    break;
                case 12: /* PsAttributeGroupAffinity (thread input) */
                {
                    /* 32-bit mirror of GROUP_AFFINITY: KAFFINITY is 32-bit */
                    typedef struct _GROUP_AFFINITY32 { ULONG Mask; USHORT Group; USHORT Reserved[3]; } GROUP_AFFINITY32;
                    GROUP_AFFINITY32 *Ga32 = (GROUP_AFFINITY32 *)(ULONG_PTR)AttrList64->Attributes[i].Value;
                    if (Ga32)
                    {
                        RtlZeroMemory(&GroupAffinityIn, sizeof(GroupAffinityIn));
                        GroupAffinityIn.Group = Ga32->Group;
                        GroupAffinityIn.Mask = (KAFFINITY)Ga32->Mask;
                        HaveGroupAffinity = TRUE;
                    }
                    break;
                }
                case 14: /* PsAttributeIdealProcessor (thread input) */
                {
                    PROCESSOR_NUMBER *Ip32 = (PROCESSOR_NUMBER *)(ULONG_PTR)AttrList64->Attributes[i].Value;
                    if (Ip32)
                    {
                        IdealProcessorIn = *Ip32;
                        HaveIdealProcessor = TRUE;
                    }
                    break;
                }
                default:
                    break; /* Other attributes not yet handled */
            }
        }
    }

    /* Map CreateFlags to RtlCreateUserThread semantics */
    CreateSuspended = (CreateFlags & 0x1) ? TRUE : FALSE; /* bit0: CREATE_SUSPENDED */

    /* Use ObjectAttributes64.SecurityDescriptor if provided */
    Status = RtlCreateUserThread(ProcessHandle,
                                 (PSECURITY_DESCRIPTOR)ObjectAttributes64.SecurityDescriptor,
                                 CreateSuspended,
                                 (ULONG)ZeroBits,
                                 MaximumStackSize ? MaximumStackSize : 0,
                                 StackSize ? StackSize : 0,
                                 StartRoutine,
                                 Argument,
                                 &ThreadHandle64,
                                 &ClientId);

    if (NT_SUCCESS(Status))
    {
        /* Return 32-bit thread handle if requested */
        if (ThreadHandle32)
            *(ULONG *)(ULONG_PTR)ThreadHandle32 = (ULONG)(ULONG_PTR)ThreadHandle64;

        /* Fill CLIENT_ID if requested via attributes */
        if (ClientIdOut32)
        {
            typedef struct _CLIENT_ID32 { ULONG UniqueProcess; ULONG UniqueThread; } CLIENT_ID32;
            CLIENT_ID32 *Cid32 = (CLIENT_ID32 *)(ULONG_PTR)ClientIdOut32;
            Cid32->UniqueProcess = (ULONG)(ULONG_PTR)ClientId.UniqueProcess;
            Cid32->UniqueThread  = (ULONG)(ULONG_PTR)ClientId.UniqueThread;
        }

        /* Fill TEB address if requested via attributes */
        if (TebAddrOut32)
        {
            THREAD_BASIC_INFORMATION Tbi;
            ULONG RetLen = 0;
            if (NT_SUCCESS(NtQueryInformationThread(ThreadHandle64,
                                                    ThreadBasicInformation,
                                                    &Tbi,
                                                    sizeof(Tbi),
                                                    &RetLen)))
            {
                *(ULONG *)(ULONG_PTR)TebAddrOut32 = (ULONG)(ULONG_PTR)Tbi.TebBaseAddress;
            }
            else
            {
                *(ULONG *)(ULONG_PTR)TebAddrOut32 = 0;
            }
        }

        /* Apply optional inputs after creation */
        if (HaveGroupAffinity)
        {
            /* Some headers may not expose extended info classes; define if needed */
            #ifndef ThreadGroupInformation
            #define ThreadGroupInformation 30
            #endif
            NtSetInformationThread(ThreadHandle64,
                                   (THREADINFOCLASS)ThreadGroupInformation,
                                   &GroupAffinityIn,
                                   sizeof(GroupAffinityIn));
        }
        if (HaveIdealProcessor)
        {
            #ifndef ThreadIdealProcessorEx
            #define ThreadIdealProcessorEx 33
            #endif
            NtSetInformationThread(ThreadHandle64,
                                   (THREADINFOCLASS)ThreadIdealProcessorEx,
                                   &IdealProcessorIn,
                                   sizeof(IdealProcessorIn));
        }
    }


    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtCreateThread(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE ThreadHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    HANDLE ProcessHandle;
    PCLIENT_ID ClientId;
    PCONTEXT ThreadContext;
    PINITIAL_TEB InitialTeb;
    BOOLEAN CreateSuspended;
    HANDLE ThreadHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    ThreadHandle32   = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess    = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);
    ProcessHandle    = (HANDLE)(ULONG_PTR)(ULONG)Args32[3];
    ClientId         = (PCLIENT_ID)(ULONG_PTR)Args32[4];
    ThreadContext    = (PCONTEXT)(ULONG_PTR)Args32[5];
    InitialTeb       = (PINITIAL_TEB)(ULONG_PTR)Args32[6];
    CreateSuspended  = (BOOLEAN)Args32[7];

    Status = NtCreateThread(&ThreadHandle64,
                            DesiredAccess,
                            (Args32[2] ? &ObjectAttributes64 : NULL),
                            ProcessHandle,
                            ClientId,
                            ThreadContext,
                            InitialTeb,
                            CreateSuspended);

    if (NT_SUCCESS(Status) && ThreadHandle32)
        *(ULONG *)(ULONG_PTR)ThreadHandle32 = (ULONG)(ULONG_PTR)ThreadHandle64;

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtLockFile(
    _In_ ULONG_PTR *Args32)
{
    HANDLE FileHandle;
    HANDLE EventHandle;
    PIO_APC_ROUTINE ApcRoutine;
    PVOID ApcContext;
    IO_STATUS_BLOCK IoStatusBlock64;
    PLARGE_INTEGER ByteOffset;
    PLARGE_INTEGER Length;
    ULONG Key;
    BOOLEAN FailImmediately;
    BOOLEAN ExclusiveLock;
    NTSTATUS Status;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    FileHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    EventHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[1];
    ApcRoutine = (PIO_APC_ROUTINE)(ULONG_PTR)Args32[2];
    ApcContext = (PVOID)(ULONG_PTR)Args32[3];
    Wow64ConvertIoStatusBlock32To64(Args32[4], &IoStatusBlock64);
    ByteOffset = (PLARGE_INTEGER)(ULONG_PTR)Args32[5];
    Length = (PLARGE_INTEGER)(ULONG_PTR)Args32[6];
    Key = (ULONG)Args32[7];
    FailImmediately = (BOOLEAN)Args32[8];
    ExclusiveLock = (BOOLEAN)Args32[9];

    Status = NtLockFile(FileHandle,
                        EventHandle,
                        ApcRoutine,
                        ApcContext,
                        &IoStatusBlock64,
                        ByteOffset,
                        Length,
                        Key,
                        FailImmediately,
                        ExclusiveLock);

    Wow64ConvertIoStatusBlock64To32(&IoStatusBlock64, Args32[4]);

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtUnlockFile(
    _In_ ULONG_PTR *Args32)
{
    HANDLE FileHandle;
    IO_STATUS_BLOCK IoStatusBlock64;
    PLARGE_INTEGER ByteOffset;
    PLARGE_INTEGER Length;
    ULONG Key;
    NTSTATUS Status;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    FileHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    Wow64ConvertIoStatusBlock32To64(Args32[1], &IoStatusBlock64);
    ByteOffset = (PLARGE_INTEGER)(ULONG_PTR)Args32[2];
    Length = (PLARGE_INTEGER)(ULONG_PTR)Args32[3];
    Key = (ULONG)Args32[4];

    Status = NtUnlockFile(FileHandle,
                          &IoStatusBlock64,
                          ByteOffset,
                          Length,
                          Key);

    Wow64ConvertIoStatusBlock64To32(&IoStatusBlock64, Args32[1]);

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtFlushVirtualMemory(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ProcessHandle;
    PVOID *BaseAddress;
    PSIZE_T RegionSize;
    IO_STATUS_BLOCK IoStatusBlock64;
    NTSTATUS Status;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ProcessHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    BaseAddress = (PVOID *)(ULONG_PTR)Args32[1];
    RegionSize = (PSIZE_T)(ULONG_PTR)Args32[2];
    Wow64ConvertIoStatusBlock32To64(Args32[3], &IoStatusBlock64);

    Status = NtFlushVirtualMemory(ProcessHandle,
                                  BaseAddress,
                                  RegionSize,
                                  &IoStatusBlock64);

    Wow64ConvertIoStatusBlock64To32(&IoStatusBlock64, Args32[3]);

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtFlushInstructionCache(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ProcessHandle;
    PVOID BaseAddress;
    ULONG Length;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ProcessHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    BaseAddress = (PVOID)(ULONG_PTR)Args32[1];
    Length = (ULONG)Args32[2];

    return NtFlushInstructionCache(ProcessHandle,
                                   BaseAddress,
                                   Length);
}

static NTSTATUS NTAPI
Wow64Thunk_NtProtectVirtualMemory(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ProcessHandle;
    PVOID *BaseAddress;
    PSIZE_T NumberOfBytes;
    ULONG NewProtect;
    PULONG OldProtect;
    NTSTATUS Status;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ProcessHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    BaseAddress = (PVOID *)(ULONG_PTR)Args32[1];
    NumberOfBytes = (PSIZE_T)(ULONG_PTR)Args32[2];
    NewProtect = (ULONG)Args32[3];
    OldProtect = (PULONG)(ULONG_PTR)Args32[4];

    Status = NtProtectVirtualMemory(ProcessHandle,
                                    BaseAddress,
                                    NumberOfBytes,
                                    NewProtect,
                                    OldProtect);

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtQueryVirtualMemory(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ProcessHandle;
    PVOID BaseAddress;
    MEMORY_INFORMATION_CLASS MemoryInformationClass;
    PVOID MemoryInformation;
    SIZE_T MemoryInformationLength;
    PSIZE_T ReturnLength;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ProcessHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    BaseAddress = (PVOID)(ULONG_PTR)Args32[1];
    MemoryInformationClass = (MEMORY_INFORMATION_CLASS)Args32[2];
    MemoryInformation = (PVOID)(ULONG_PTR)Args32[3];
    MemoryInformationLength = (SIZE_T)Args32[4];
    ReturnLength = (PSIZE_T)(ULONG_PTR)Args32[5];

    return NtQueryVirtualMemory(ProcessHandle,
                                BaseAddress,
                                MemoryInformationClass,
                                MemoryInformation,
                                MemoryInformationLength,
                                ReturnLength);
}

/* ===================================================================
 * Registry Thunks
 * ================================================================ */

static NTSTATUS NTAPI
Wow64Thunk_NtCreateKey(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE KeyHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    ULONG TitleIndex;
    UNICODE_STRING ClassString64;
    PUNICODE_STRING ClassString;
    ULONG CreateOptions;
    PULONG Disposition;
    HANDLE KeyHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeyHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);
    TitleIndex = (ULONG)Args32[3];
    if (Args32[4])
    {
        Wow64ConvertUnicodeString32To64(Args32[4], &ClassString64);
        ClassString = &ClassString64;
    }
    else
    {
        ClassString = NULL;
    }
    CreateOptions = (ULONG)Args32[5];
    Disposition = (PULONG)(ULONG_PTR)Args32[6];

    Status = NtCreateKey(&KeyHandle64,
                         DesiredAccess,
                         Args32[2] ? &ObjectAttributes64 : NULL,
                         TitleIndex,
                         ClassString,
                         CreateOptions,
                         Disposition);

    if (KeyHandle32)
    {
        *(ULONG *)(ULONG_PTR)KeyHandle32 = (ULONG)(ULONG_PTR)KeyHandle64;
    }

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtOpenKey(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE KeyHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    HANDLE KeyHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeyHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);

    Status = NtOpenKey(&KeyHandle64,
                       DesiredAccess,
                       Args32[2] ? &ObjectAttributes64 : NULL);

    if (KeyHandle32)
    {
        *(ULONG *)(ULONG_PTR)KeyHandle32 = (ULONG)(ULONG_PTR)KeyHandle64;
    }

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtQueryValueKey(
    _In_ ULONG_PTR *Args32)
{
    HANDLE KeyHandle;
    UNICODE_STRING ValueName64;
    PUNICODE_STRING ValueName;
    KEY_VALUE_INFORMATION_CLASS InformationClass;
    PVOID ValueInformation;
    ULONG Length;
    PULONG ResultLength;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeyHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    if (Args32[1])
    {
        Wow64ConvertUnicodeString32To64(Args32[1], &ValueName64);
        ValueName = &ValueName64;
    }
    else
    {
        ValueName = NULL;
    }
    InformationClass = (KEY_VALUE_INFORMATION_CLASS)Args32[2];
    ValueInformation = (PVOID)(ULONG_PTR)Args32[3];
    Length = (ULONG)Args32[4];
    ResultLength = (PULONG)(ULONG_PTR)Args32[5];

    return NtQueryValueKey(KeyHandle,
                           ValueName,
                           InformationClass,
                           ValueInformation,
                           Length,
                           ResultLength);
}

static NTSTATUS NTAPI
Wow64Thunk_NtSetValueKey(
    _In_ ULONG_PTR *Args32)
{
    HANDLE KeyHandle;
    UNICODE_STRING ValueName64;
    PUNICODE_STRING ValueName;
    ULONG TitleIndex;
    ULONG Type;
    PVOID Data;
    ULONG DataSize;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeyHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    if (Args32[1])
    {
        Wow64ConvertUnicodeString32To64(Args32[1], &ValueName64);
        ValueName = &ValueName64;
    }
    else
    {
        ValueName = NULL;
    }
    TitleIndex = (ULONG)Args32[2];
    Type = (ULONG)Args32[3];
    Data = (PVOID)(ULONG_PTR)Args32[4];
    DataSize = (ULONG)Args32[5];

    return NtSetValueKey(KeyHandle,
                         ValueName,
                         TitleIndex,
                         Type,
                         Data,
                         DataSize);
}

 static NTSTATUS NTAPI
 Wow64Thunk_NtReadFile(
     _In_ ULONG_PTR *Args32)
{
    NTSTATUS Status;
    HANDLE FileHandle;
    HANDLE Event;
    PIO_APC_ROUTINE ApcRoutine;
    PVOID ApcContext;
    IO_STATUS_BLOCK IoStatusBlock64;
    PVOID Buffer;
    ULONG Length;
    PLARGE_INTEGER ByteOffset;
    PULONG Key;

    /* Args32[0] = FileHandle
     * Args32[1] = Event
     * Args32[2] = ApcRoutine (32-bit pointer)
     * Args32[3] = ApcContext (32-bit pointer)
     * Args32[4] = IoStatusBlock* (32-bit pointer)
     * Args32[5] = Buffer* (32-bit pointer)
     * Args32[6] = Length
     * Args32[7] = ByteOffset* (32-bit pointer)
     * Args32[8] = Key* (32-bit pointer)
     */

    FileHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    Event = (HANDLE)(ULONG_PTR)(ULONG)Args32[1];
    ApcRoutine = (PIO_APC_ROUTINE)(ULONG_PTR)Args32[2];
    ApcContext = (PVOID)(ULONG_PTR)Args32[3];
    Buffer = (PVOID)(ULONG_PTR)Args32[5];
    Length = (ULONG)Args32[6];
    ByteOffset = (PLARGE_INTEGER)(ULONG_PTR)Args32[7];
    Key = (PULONG)(ULONG_PTR)Args32[8];

    Wow64ConvertIoStatusBlock32To64(Args32[4], &IoStatusBlock64);

    Status = NtReadFile(FileHandle,
                       Event,
                       ApcRoutine,
                       ApcContext,
                       &IoStatusBlock64,
                       Buffer,
                       Length,
                       ByteOffset,
                       Key);

    Wow64ConvertIoStatusBlock64To32(&IoStatusBlock64, Args32[4]);

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtWriteFile(
    _In_ ULONG_PTR *Args32)
{
    NTSTATUS Status;
    HANDLE FileHandle;
    HANDLE Event;
    PIO_APC_ROUTINE ApcRoutine;
    PVOID ApcContext;
    IO_STATUS_BLOCK IoStatusBlock64;
    PVOID Buffer;
    ULONG Length;
    PLARGE_INTEGER ByteOffset;
    PULONG Key;

    /* Args32[0] = FileHandle
     * Args32[1] = Event
     * Args32[2] = ApcRoutine (32-bit pointer)
     * Args32[3] = ApcContext (32-bit pointer)
     * Args32[4] = IoStatusBlock* (32-bit pointer)
     * Args32[5] = Buffer* (32-bit pointer)
     * Args32[6] = Length
     * Args32[7] = ByteOffset* (32-bit pointer)
     * Args32[8] = Key* (32-bit pointer)
     */

    FileHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    Event = (HANDLE)(ULONG_PTR)(ULONG)Args32[1];
    ApcRoutine = (PIO_APC_ROUTINE)(ULONG_PTR)Args32[2];
    ApcContext = (PVOID)(ULONG_PTR)Args32[3];
    Buffer = (PVOID)(ULONG_PTR)Args32[5];
    Length = (ULONG)Args32[6];
    ByteOffset = (PLARGE_INTEGER)(ULONG_PTR)Args32[7];
    Key = (PULONG)(ULONG_PTR)Args32[8];

    Wow64ConvertIoStatusBlock32To64(Args32[4], &IoStatusBlock64);

    Status = NtWriteFile(FileHandle,
                        Event,
                        ApcRoutine,
                        ApcContext,
                        &IoStatusBlock64,
                        Buffer,
                        Length,
                        ByteOffset,
                        Key);

    Wow64ConvertIoStatusBlock64To32(&IoStatusBlock64, Args32[4]);

    return Status;
}

/* ===================================================================
 * Process/System Query Thunks
 * ================================================================ */

static NTSTATUS NTAPI
Wow64Thunk_NtQueryInformationProcess(
    _In_ ULONG_PTR *Args32)
{
    NTSTATUS Status;
    HANDLE ProcessHandle;
    PROCESSINFOCLASS ProcessInformationClass;
    PVOID ProcessInformation;
    ULONG ProcessInformationLength;
    PULONG ReturnLength;

    /* Args32[0] = ProcessHandle
     * Args32[1] = ProcessInformationClass
     * Args32[2] = ProcessInformation* (32-bit pointer)
     * Args32[3] = ProcessInformationLength
     * Args32[4] = ReturnLength* (32-bit pointer)
     */

    ProcessHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    ProcessInformationClass = (PROCESSINFOCLASS)Args32[1];
    ProcessInformation = (PVOID)(ULONG_PTR)Args32[2];
    ProcessInformationLength = (ULONG)Args32[3];
    ReturnLength = (PULONG)(ULONG_PTR)Args32[4];

    Status = NtQueryInformationProcess(ProcessHandle,
                                      ProcessInformationClass,
                                      ProcessInformation,
                                      ProcessInformationLength,
                                      ReturnLength);

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtSetInformationProcess(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ProcessHandle;
    PROCESSINFOCLASS ProcessInformationClass;
    PVOID ProcessInformation;
    ULONG ProcessInformationLength;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ProcessHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    ProcessInformationClass = (PROCESSINFOCLASS)Args32[1];
    ProcessInformation = (PVOID)(ULONG_PTR)Args32[2];
    ProcessInformationLength = (ULONG)Args32[3];

    return NtSetInformationProcess(ProcessHandle,
                                   ProcessInformationClass,
                                   ProcessInformation,
                                   ProcessInformationLength);
}

static NTSTATUS NTAPI
Wow64Thunk_NtQuerySystemInformation(
    _In_ ULONG_PTR *Args32)
{
    NTSTATUS Status;
    ULONG SystemInformationClass;
    PVOID SystemInformation;
    ULONG SystemInformationLength;
    PULONG ReturnLength;

    /* Args32[0] = SystemInformationClass
     * Args32[1] = SystemInformation* (32-bit pointer)
     * Args32[2] = SystemInformationLength
     * Args32[3] = ReturnLength* (32-bit pointer)
     */

    SystemInformationClass = (ULONG)Args32[0];
    SystemInformation = (PVOID)(ULONG_PTR)Args32[1];
    SystemInformationLength = (ULONG)Args32[2];
    ReturnLength = (PULONG)(ULONG_PTR)Args32[3];

    Status = NtQuerySystemInformation(SystemInformationClass,
                                     SystemInformation,
                                     SystemInformationLength,
                                     ReturnLength);

    return Status;
}

/* ===================================================================
 * Additional Essential Console Syscalls
 * ================================================================ */

static NTSTATUS NTAPI
Wow64Thunk_NtCreateMutant(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE MutantHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    BOOLEAN InitialOwner;
    HANDLE MutantHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    MutantHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);
    InitialOwner = (BOOLEAN)Args32[3];

    Status = NtCreateMutant(&MutantHandle64,
                            DesiredAccess,
                            (Args32[2] ? &ObjectAttributes64 : NULL),
                            InitialOwner);

    if (NT_SUCCESS(Status) && MutantHandle32)
        *(ULONG *)(ULONG_PTR)MutantHandle32 = (ULONG)(ULONG_PTR)MutantHandle64;

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtOpenMutant(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE MutantHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    HANDLE MutantHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    MutantHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);

    Status = NtOpenMutant(&MutantHandle64,
                          DesiredAccess,
                          (Args32[2] ? &ObjectAttributes64 : NULL));

    if (NT_SUCCESS(Status) && MutantHandle32)
        *(ULONG *)(ULONG_PTR)MutantHandle32 = (ULONG)(ULONG_PTR)MutantHandle64;

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtReleaseMutant(
    _In_ ULONG_PTR *Args32)
{
    HANDLE MutantHandle;
    PLONG PreviousCount;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    MutantHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    PreviousCount = (PLONG)(ULONG_PTR)Args32[1];
    return NtReleaseMutant(MutantHandle, PreviousCount);
}

static NTSTATUS NTAPI
Wow64Thunk_NtCreateSemaphore(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE SemaphoreHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    LONG InitialCount;
    LONG MaximumCount;
    HANDLE SemaphoreHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    SemaphoreHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);
    InitialCount = (LONG)Args32[3];
    MaximumCount = (LONG)Args32[4];

    Status = NtCreateSemaphore(&SemaphoreHandle64,
                               DesiredAccess,
                               (Args32[2] ? &ObjectAttributes64 : NULL),
                               InitialCount,
                               MaximumCount);

    if (NT_SUCCESS(Status) && SemaphoreHandle32)
        *(ULONG *)(ULONG_PTR)SemaphoreHandle32 = (ULONG)(ULONG_PTR)SemaphoreHandle64;

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtReleaseSemaphore(
    _In_ ULONG_PTR *Args32)
{
    HANDLE SemaphoreHandle;
    LONG ReleaseCount;
    PLONG PreviousCount;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    SemaphoreHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    ReleaseCount = (LONG)Args32[1];
    PreviousCount = (PLONG)(ULONG_PTR)Args32[2];

    return NtReleaseSemaphore(SemaphoreHandle, ReleaseCount, PreviousCount);
}

static NTSTATUS NTAPI
Wow64Thunk_NtCreateSymbolicLinkObject(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE LinkHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    UNICODE_STRING TargetName64;
    HANDLE LinkHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    LinkHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);
    Wow64ConvertUnicodeString32To64(Args32[3], &TargetName64);

    Status = NtCreateSymbolicLinkObject(&LinkHandle64,
                                        DesiredAccess,
                                        (Args32[2] ? &ObjectAttributes64 : NULL),
                                        &TargetName64);

    if (NT_SUCCESS(Status) && LinkHandle32)
        *(ULONG *)(ULONG_PTR)LinkHandle32 = (ULONG)(ULONG_PTR)LinkHandle64;

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtOpenSymbolicLinkObject(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE LinkHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    HANDLE LinkHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    LinkHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);

    Status = NtOpenSymbolicLinkObject(&LinkHandle64,
                                      DesiredAccess,
                                      (Args32[2] ? &ObjectAttributes64 : NULL));

    if (NT_SUCCESS(Status) && LinkHandle32)
        *(ULONG *)(ULONG_PTR)LinkHandle32 = (ULONG)(ULONG_PTR)LinkHandle64;

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtQuerySymbolicLinkObject(
    _In_ ULONG_PTR *Args32)
{
    HANDLE LinkHandle;
    UNICODE_STRING Name64;
    PUNICODE_STRING32 Name32;
    PULONG ReturnLength;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    LinkHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    Name32 = (PUNICODE_STRING32)(ULONG_PTR)Args32[1];
    ReturnLength = (PULONG)(ULONG_PTR)Args32[2];

    /* Convert the provided 32-bit UNICODE_STRING to native */
    Wow64ConvertUnicodeString32To64(Args32[1], &Name64);

    Status = NtQuerySymbolicLinkObject(LinkHandle,
                                       &Name64,
                                       ReturnLength);

    /* Propagate updated Length back to 32-bit structure */
    if (Name32)
    {
        Name32->Length = Name64.Length;
    }

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtCreateDirectoryObject(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE DirectoryHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    HANDLE DirectoryHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    DirectoryHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);

    Status = NtCreateDirectoryObject(&DirectoryHandle64,
                                     DesiredAccess,
                                     (Args32[2] ? &ObjectAttributes64 : NULL));

    if (NT_SUCCESS(Status) && DirectoryHandle32)
        *(ULONG *)(ULONG_PTR)DirectoryHandle32 = (ULONG)(ULONG_PTR)DirectoryHandle64;

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtOpenDirectoryObject(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE DirectoryHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    HANDLE DirectoryHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    DirectoryHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);

    Status = NtOpenDirectoryObject(&DirectoryHandle64,
                                   DesiredAccess,
                                   (Args32[2] ? &ObjectAttributes64 : NULL));

    if (NT_SUCCESS(Status) && DirectoryHandle32)
        *(ULONG *)(ULONG_PTR)DirectoryHandle32 = (ULONG)(ULONG_PTR)DirectoryHandle64;

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtQueryDirectoryObject(
    _In_ ULONG_PTR *Args32)
{
    HANDLE DirectoryHandle;
    PVOID Buffer;
    ULONG Length;
    BOOLEAN ReturnSingleEntry;
    BOOLEAN RestartScan;
    PULONG Context;
    PULONG ReturnLength;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    DirectoryHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    Buffer = (PVOID)(ULONG_PTR)Args32[1];
    Length = (ULONG)Args32[2];
    ReturnSingleEntry = (BOOLEAN)Args32[3];
    RestartScan = (BOOLEAN)Args32[4];
    Context = (PULONG)(ULONG_PTR)Args32[5];
    ReturnLength = (PULONG)(ULONG_PTR)Args32[6];

    return NtQueryDirectoryObject(DirectoryHandle,
                                  Buffer,
                                  Length,
                                  ReturnSingleEntry,
                                  RestartScan,
                                  Context,
                                  ReturnLength);
}

static NTSTATUS NTAPI
Wow64Thunk_NtQueryKey(
    _In_ ULONG_PTR *Args32)
{
    HANDLE KeyHandle;
    KEY_INFORMATION_CLASS KeyInformationClass;
    PVOID KeyInformation;
    ULONG Length;
    PULONG ResultLength;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    KeyHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    KeyInformationClass = (KEY_INFORMATION_CLASS)Args32[1];
    KeyInformation = (PVOID)(ULONG_PTR)Args32[2];
    Length = (ULONG)Args32[3];
    ResultLength = (PULONG)(ULONG_PTR)Args32[4];

    return NtQueryKey(KeyHandle,
                      KeyInformationClass,
                      KeyInformation,
                      Length,
                      ResultLength);
}

static NTSTATUS NTAPI
Wow64Thunk_NtEnumerateKey(
    _In_ ULONG_PTR *Args32)
{
    HANDLE KeyHandle;
    ULONG Index;
    KEY_INFORMATION_CLASS KeyInformationClass;
    PVOID KeyInformation;
    ULONG Length;
    PULONG ResultLength;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    KeyHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    Index = (ULONG)Args32[1];
    KeyInformationClass = (KEY_INFORMATION_CLASS)Args32[2];
    KeyInformation = (PVOID)(ULONG_PTR)Args32[3];
    Length = (ULONG)Args32[4];
    ResultLength = (PULONG)(ULONG_PTR)Args32[5];

    return NtEnumerateKey(KeyHandle,
                          Index,
                          KeyInformationClass,
                          KeyInformation,
                          Length,
                          ResultLength);
}

static NTSTATUS NTAPI
Wow64Thunk_NtEnumerateValueKey(
    _In_ ULONG_PTR *Args32)
{
    HANDLE KeyHandle;
    ULONG Index;
    KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass;
    PVOID KeyValueInformation;
    ULONG Length;
    PULONG ResultLength;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    KeyHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    Index = (ULONG)Args32[1];
    KeyValueInformationClass = (KEY_VALUE_INFORMATION_CLASS)Args32[2];
    KeyValueInformation = (PVOID)(ULONG_PTR)Args32[3];
    Length = (ULONG)Args32[4];
    ResultLength = (PULONG)(ULONG_PTR)Args32[5];

    return NtEnumerateValueKey(KeyHandle,
                               Index,
                               KeyValueInformationClass,
                               KeyValueInformation,
                               Length,
                               ResultLength);
}

static NTSTATUS NTAPI
Wow64Thunk_NtOpenSemaphore(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE SemaphoreHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    HANDLE SemaphoreHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    SemaphoreHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);

    Status = NtOpenSemaphore(&SemaphoreHandle64,
                             DesiredAccess,
                             (Args32[2] ? &ObjectAttributes64 : NULL));

    if (NT_SUCCESS(Status) && SemaphoreHandle32)
        *(ULONG *)(ULONG_PTR)SemaphoreHandle32 = (ULONG)(ULONG_PTR)SemaphoreHandle64;

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtCreateEventPair(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE EventPairHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    HANDLE EventPairHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    EventPairHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);

    Status = NtCreateEventPair(&EventPairHandle64,
                               DesiredAccess,
                               (Args32[2] ? &ObjectAttributes64 : NULL));

    if (NT_SUCCESS(Status) && EventPairHandle32)
        *(ULONG *)(ULONG_PTR)EventPairHandle32 = (ULONG)(ULONG_PTR)EventPairHandle64;

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtOpenEventPair(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE EventPairHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    HANDLE EventPairHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    EventPairHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);

    Status = NtOpenEventPair(&EventPairHandle64,
                             DesiredAccess,
                             (Args32[2] ? &ObjectAttributes64 : NULL));

    if (NT_SUCCESS(Status) && EventPairHandle32)
        *(ULONG *)(ULONG_PTR)EventPairHandle32 = (ULONG)(ULONG_PTR)EventPairHandle64;

    return Status;
}

/* ================================================================
 * Timers
 * ================================================================ */

static NTSTATUS NTAPI
Wow64Thunk_NtCreateTimer(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE TimerHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    TIMER_TYPE TimerType;
    HANDLE TimerHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    TimerHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);
    TimerType = (TIMER_TYPE)Args32[3];

    Status = NtCreateTimer(&TimerHandle64,
                           DesiredAccess,
                           (Args32[2] ? &ObjectAttributes64 : NULL),
                           TimerType);

    if (NT_SUCCESS(Status) && TimerHandle32)
        *(ULONG *)(ULONG_PTR)TimerHandle32 = (ULONG)(ULONG_PTR)TimerHandle64;

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtOpenTimer(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE TimerHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    HANDLE TimerHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    TimerHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);

    Status = NtOpenTimer(&TimerHandle64,
                         DesiredAccess,
                         (Args32[2] ? &ObjectAttributes64 : NULL));

    if (NT_SUCCESS(Status) && TimerHandle32)
        *(ULONG *)(ULONG_PTR)TimerHandle32 = (ULONG)(ULONG_PTR)TimerHandle64;

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtSetTimer(
    _In_ ULONG_PTR *Args32)
{
    HANDLE TimerHandle;
    PLARGE_INTEGER DueTime;
    PTIMER_APC_ROUTINE TimerApcRoutine;
    PVOID TimerContext;
    BOOLEAN Resume;
    LONG Period;
    PBOOLEAN PreviousState;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    TimerHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    DueTime = (PLARGE_INTEGER)(ULONG_PTR)Args32[1];
    TimerApcRoutine = (PTIMER_APC_ROUTINE)(ULONG_PTR)Args32[2];
    TimerContext = (PVOID)(ULONG_PTR)Args32[3];
    Resume = (BOOLEAN)Args32[4];
    Period = (LONG)Args32[5];
    PreviousState = (PBOOLEAN)(ULONG_PTR)Args32[6];

    return NtSetTimer(TimerHandle,
                      DueTime,
                      TimerApcRoutine,
                      TimerContext,
                      Resume,
                      Period,
                      PreviousState);
}

static NTSTATUS NTAPI
Wow64Thunk_NtCancelTimer(
    _In_ ULONG_PTR *Args32)
{
    HANDLE TimerHandle;
    PBOOLEAN CurrentState;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    TimerHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    CurrentState = (PBOOLEAN)(ULONG_PTR)Args32[1];
    return NtCancelTimer(TimerHandle, CurrentState);
}

static NTSTATUS NTAPI
Wow64Thunk_NtQueryTimer(
    _In_ ULONG_PTR *Args32)
{
    HANDLE TimerHandle;
    TIMER_INFORMATION_CLASS TimerInformationClass;
    PVOID TimerInformation;
    ULONG TimerInformationLength;
    PULONG ReturnLength;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    TimerHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    TimerInformationClass = (TIMER_INFORMATION_CLASS)Args32[1];
    TimerInformation = (PVOID)(ULONG_PTR)Args32[2];
    TimerInformationLength = (ULONG)Args32[3];
    ReturnLength = (PULONG)(ULONG_PTR)Args32[4];

    return NtQueryTimer(TimerHandle,
                        TimerInformationClass,
                        TimerInformation,
                        TimerInformationLength,
                        ReturnLength);
}

static NTSTATUS NTAPI
Wow64Thunk_NtQueryTimerResolution(
    _In_ ULONG_PTR *Args32)
{
    PULONG MaximumResolution;
    PULONG MinimumResolution;
    PULONG CurrentResolution;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    MaximumResolution = (PULONG)(ULONG_PTR)Args32[0];
    MinimumResolution = (PULONG)(ULONG_PTR)Args32[1];
    CurrentResolution = (PULONG)(ULONG_PTR)Args32[2];

    return NtQueryTimerResolution(MaximumResolution,
                                  MinimumResolution,
                                  CurrentResolution);
}

static NTSTATUS NTAPI
Wow64Thunk_NtSetTimerResolution(
    _In_ ULONG_PTR *Args32)
{
    ULONG DesiredResolution;
    BOOLEAN SetResolution;
    PULONG CurrentResolution;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    DesiredResolution = (ULONG)Args32[0];
    SetResolution = (BOOLEAN)Args32[1];
    CurrentResolution = (PULONG)(ULONG_PTR)Args32[2];

    return NtSetTimerResolution(DesiredResolution,
                                SetResolution,
                                CurrentResolution);
}

static NTSTATUS NTAPI
Wow64Thunk_NtDelayExecution(
    _In_ ULONG_PTR *Args32)
{
    BOOLEAN Alertable;
    PLARGE_INTEGER DelayInterval;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    Alertable = (BOOLEAN)Args32[0];
    DelayInterval = (PLARGE_INTEGER)(ULONG_PTR)Args32[1];

    return NtDelayExecution(Alertable, DelayInterval);
}

static NTSTATUS NTAPI
Wow64Thunk_NtFsControlFile(
    _In_ ULONG_PTR *Args32)
{
    HANDLE FileHandle;
    HANDLE Event;
    PIO_APC_ROUTINE ApcRoutine;
    PVOID ApcContext;
    IO_STATUS_BLOCK IoStatusBlock64;
    ULONG FsControlCode;
    PVOID InputBuffer;
    ULONG InputBufferLength;
    PVOID OutputBuffer;
    ULONG OutputBufferLength;
    NTSTATUS Status;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    FileHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    Event = (HANDLE)(ULONG_PTR)(ULONG)Args32[1];
    ApcRoutine = (PIO_APC_ROUTINE)(ULONG_PTR)Args32[2];
    ApcContext = (PVOID)(ULONG_PTR)Args32[3];
    Wow64ConvertIoStatusBlock32To64(Args32[4], &IoStatusBlock64);
    FsControlCode = (ULONG)Args32[5];
    InputBuffer = (PVOID)(ULONG_PTR)Args32[6];
    InputBufferLength = (ULONG)Args32[7];
    OutputBuffer = (PVOID)(ULONG_PTR)Args32[8];
    OutputBufferLength = (ULONG)Args32[9];

    Status = NtFsControlFile(FileHandle,
                             Event,
                             ApcRoutine,
                             ApcContext,
                             &IoStatusBlock64,
                             FsControlCode,
                             InputBuffer,
                             InputBufferLength,
                             OutputBuffer,
                             OutputBufferLength);

    Wow64ConvertIoStatusBlock64To32(&IoStatusBlock64, Args32[4]);

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtQueryVolumeInformationFile(
    _In_ ULONG_PTR *Args32)
{
    HANDLE FileHandle;
    IO_STATUS_BLOCK IoStatusBlock64;
    PVOID FsInformation;
    ULONG Length;
    FS_INFORMATION_CLASS FsInformationClass;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    FileHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    Wow64ConvertIoStatusBlock32To64(Args32[1], &IoStatusBlock64);
    FsInformation = (PVOID)(ULONG_PTR)Args32[2];
    Length = (ULONG)Args32[3];
    FsInformationClass = (FS_INFORMATION_CLASS)Args32[4];

    {
        NTSTATUS Status = NtQueryVolumeInformationFile(FileHandle,
                                                       &IoStatusBlock64,
                                                       FsInformation,
                                                       Length,
                                                       FsInformationClass);
        Wow64ConvertIoStatusBlock64To32(&IoStatusBlock64, Args32[1]);
        return Status;
    }
}

static NTSTATUS NTAPI
Wow64Thunk_NtQueryAttributesFile(
    _In_ ULONG_PTR *Args32)
{
    OBJECT_ATTRIBUTES ObjectAttributes64;
    PVOID FileInformation;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    Wow64ConvertObjectAttributes32To64(Args32[0], &ObjectAttributes64);
    FileInformation = (PVOID)(ULONG_PTR)Args32[1];

    return NtQueryAttributesFile(&ObjectAttributes64,
                                 FileInformation);
}

static NTSTATUS NTAPI
Wow64Thunk_NtSetVolumeInformationFile(
    _In_ ULONG_PTR *Args32)
{
    HANDLE FileHandle;
    IO_STATUS_BLOCK IoStatusBlock64;
    PVOID FsInformation;
    ULONG Length;
    FS_INFORMATION_CLASS FsInformationClass;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    FileHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    Wow64ConvertIoStatusBlock32To64(Args32[1], &IoStatusBlock64);
    FsInformation = (PVOID)(ULONG_PTR)Args32[2];
    Length = (ULONG)Args32[3];
    FsInformationClass = (FS_INFORMATION_CLASS)Args32[4];

    {
        NTSTATUS Status = NtSetVolumeInformationFile(FileHandle,
                                                     &IoStatusBlock64,
                                                     FsInformation,
                                                     Length,
                                                     FsInformationClass);
        Wow64ConvertIoStatusBlock64To32(&IoStatusBlock64, Args32[1]);
        return Status;
    }
}

static NTSTATUS NTAPI
Wow64Thunk_NtQueryQuotaInformationFile(
    _In_ ULONG_PTR *Args32)
{
    HANDLE FileHandle;
    IO_STATUS_BLOCK IoStatusBlock64;
    PVOID Buffer;
    ULONG Length;
    BOOLEAN ReturnSingleEntry;
    PVOID SidList;
    ULONG SidListLength;
    PSID StartSid;
    BOOLEAN RestartScan;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    FileHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    Wow64ConvertIoStatusBlock32To64(Args32[1], &IoStatusBlock64);
    Buffer = (PVOID)(ULONG_PTR)Args32[2];
    Length = (ULONG)Args32[3];
    ReturnSingleEntry = (BOOLEAN)Args32[4];
    SidList = (PVOID)(ULONG_PTR)Args32[5];
    SidListLength = (ULONG)Args32[6];
    StartSid = (PSID)(ULONG_PTR)Args32[7];
    RestartScan = (BOOLEAN)Args32[8];

    {
        NTSTATUS Status = NtQueryQuotaInformationFile(FileHandle,
                                                      &IoStatusBlock64,
                                                      Buffer,
                                                      Length,
                                                      ReturnSingleEntry,
                                                      SidList,
                                                      SidListLength,
                                                      StartSid,
                                                      RestartScan);
        Wow64ConvertIoStatusBlock64To32(&IoStatusBlock64, Args32[1]);
        return Status;
    }
}

static NTSTATUS NTAPI
Wow64Thunk_NtOpenEvent(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE EventHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    HANDLE EventHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    EventHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);

    Status = NtOpenEvent(&EventHandle64,
                         DesiredAccess,
                         (Args32[2] ? &ObjectAttributes64 : NULL));

    if (NT_SUCCESS(Status) && EventHandle32)
        *(ULONG *)(ULONG_PTR)EventHandle32 = (ULONG)(ULONG_PTR)EventHandle64;

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtSetEvent(
    _In_ ULONG_PTR *Args32)
{
    HANDLE EventHandle;
    PLONG PreviousState;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    EventHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    PreviousState = (PLONG)(ULONG_PTR)Args32[1];
    return NtSetEvent(EventHandle, PreviousState);
}

static NTSTATUS NTAPI
Wow64Thunk_NtResetEvent(
    _In_ ULONG_PTR *Args32)
{
    HANDLE EventHandle;
    PLONG PreviousState;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    EventHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    PreviousState = (PLONG)(ULONG_PTR)Args32[1];
    return NtResetEvent(EventHandle, PreviousState);
}

static NTSTATUS NTAPI
Wow64Thunk_NtCreateIoCompletion(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE IoCompletionHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    ULONG NumberOfConcurrentThreads;
    HANDLE IoCompletionHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    IoCompletionHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);
    NumberOfConcurrentThreads = (ULONG)Args32[3];

    Status = NtCreateIoCompletion(&IoCompletionHandle64,
                                  DesiredAccess,
                                  (Args32[2] ? &ObjectAttributes64 : NULL),
                                  NumberOfConcurrentThreads);

    if (NT_SUCCESS(Status) && IoCompletionHandle32)
        *(ULONG *)(ULONG_PTR)IoCompletionHandle32 = (ULONG)(ULONG_PTR)IoCompletionHandle64;

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtSetIoCompletion(
    _In_ ULONG_PTR *Args32)
{
    HANDLE IoCompletionPortHandle;
    PVOID CompletionKey;
    PVOID CompletionContext;
    NTSTATUS CompletionStatus;
    ULONG_PTR CompletionInformation;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    IoCompletionPortHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    CompletionKey = (PVOID)(ULONG_PTR)Args32[1];
    CompletionContext = (PVOID)(ULONG_PTR)Args32[2];
    CompletionStatus = (NTSTATUS)Args32[3];
    CompletionInformation = (ULONG_PTR)Args32[4];

    return NtSetIoCompletion(IoCompletionPortHandle,
                             CompletionKey,
                             CompletionContext,
                             CompletionStatus,
                             CompletionInformation);
}

static NTSTATUS NTAPI
Wow64Thunk_NtRemoveIoCompletion(
    _In_ ULONG_PTR *Args32)
{
    HANDLE IoCompletionHandle;
    PVOID *CompletionKey;
    PVOID *CompletionContext;
    IO_STATUS_BLOCK IoStatusBlock64;
    PLARGE_INTEGER Timeout;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    IoCompletionHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    CompletionKey = (PVOID *)(ULONG_PTR)Args32[1];
    CompletionContext = (PVOID *)(ULONG_PTR)Args32[2];
    Wow64ConvertIoStatusBlock32To64(Args32[3], &IoStatusBlock64);
    Timeout = (PLARGE_INTEGER)(ULONG_PTR)Args32[4];

    {
        NTSTATUS Status = NtRemoveIoCompletion(IoCompletionHandle,
                                               CompletionKey,
                                               CompletionContext,
                                               &IoStatusBlock64,
                                               Timeout);
        Wow64ConvertIoStatusBlock64To32(&IoStatusBlock64, Args32[3]);
        return Status;
    }
}

static NTSTATUS NTAPI
Wow64Thunk_NtReadFileScatter(
    _In_ ULONG_PTR *Args32)
{
    HANDLE FileHandle;
    HANDLE Event;
    PIO_APC_ROUTINE ApcRoutine;
    PVOID ApcContext;
    IO_STATUS_BLOCK IoStatusBlock64;
    PVOID BufferDescription;
    ULONG BufferLength;
    PLARGE_INTEGER ByteOffset;
    PULONG Key;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    FileHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    Event = (HANDLE)(ULONG_PTR)(ULONG)Args32[1];
    ApcRoutine = (PIO_APC_ROUTINE)(ULONG_PTR)Args32[2];
    ApcContext = (PVOID)(ULONG_PTR)Args32[3];
    Wow64ConvertIoStatusBlock32To64(Args32[4], &IoStatusBlock64);
    BufferDescription = (PVOID)(ULONG_PTR)Args32[5];
    BufferLength = (ULONG)Args32[6];
    ByteOffset = (PLARGE_INTEGER)(ULONG_PTR)Args32[7];
    Key = (PULONG)(ULONG_PTR)Args32[8];

    Status = NtReadFileScatter(FileHandle,
                               Event,
                               ApcRoutine,
                               ApcContext,
                               &IoStatusBlock64,
                               BufferDescription,
                               BufferLength,
                               ByteOffset,
                               Key);

    Wow64ConvertIoStatusBlock64To32(&IoStatusBlock64, Args32[4]);
    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtWriteFileGather(
    _In_ ULONG_PTR *Args32)
{
    HANDLE FileHandle;
    HANDLE Event;
    PIO_APC_ROUTINE ApcRoutine;
    PVOID ApcContext;
    IO_STATUS_BLOCK IoStatusBlock64;
    PVOID BufferDescription;
    ULONG BufferLength;
    PLARGE_INTEGER ByteOffset;
    PULONG Key;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    FileHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    Event = (HANDLE)(ULONG_PTR)(ULONG)Args32[1];
    ApcRoutine = (PIO_APC_ROUTINE)(ULONG_PTR)Args32[2];
    ApcContext = (PVOID)(ULONG_PTR)Args32[3];
    Wow64ConvertIoStatusBlock32To64(Args32[4], &IoStatusBlock64);
    BufferDescription = (PVOID)(ULONG_PTR)Args32[5];
    BufferLength = (ULONG)Args32[6];
    ByteOffset = (PLARGE_INTEGER)(ULONG_PTR)Args32[7];
    Key = (PULONG)(ULONG_PTR)Args32[8];

    Status = NtWriteFileGather(FileHandle,
                               Event,
                               ApcRoutine,
                               ApcContext,
                               &IoStatusBlock64,
                               BufferDescription,
                               BufferLength,
                               ByteOffset,
                               Key);

    Wow64ConvertIoStatusBlock64To32(&IoStatusBlock64, Args32[4]);
    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtOpenThreadToken(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ThreadHandle;
    ACCESS_MASK DesiredAccess;
    BOOLEAN OpenAsSelf;
    PHANDLE TokenHandle32;
    HANDLE TokenHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    ThreadHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    OpenAsSelf = (BOOLEAN)Args32[2];
    TokenHandle32 = (PHANDLE)(ULONG_PTR)Args32[3];

    Status = NtOpenThreadToken(ThreadHandle,
                               DesiredAccess,
                               OpenAsSelf,
                               &TokenHandle64);
    if (NT_SUCCESS(Status) && TokenHandle32)
        *(ULONG *)(ULONG_PTR)TokenHandle32 = (ULONG)(ULONG_PTR)TokenHandle64;

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtOpenThreadTokenEx(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ThreadHandle;
    ACCESS_MASK DesiredAccess;
    BOOLEAN OpenAsSelf;
    ULONG HandleAttributes;
    PHANDLE TokenHandle32;
    HANDLE TokenHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    ThreadHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    OpenAsSelf = (BOOLEAN)Args32[2];
    HandleAttributes = (ULONG)Args32[3];
    TokenHandle32 = (PHANDLE)(ULONG_PTR)Args32[4];

    Status = NtOpenThreadTokenEx(ThreadHandle,
                                 DesiredAccess,
                                 OpenAsSelf,
                                 HandleAttributes,
                                 &TokenHandle64);
    if (NT_SUCCESS(Status) && TokenHandle32)
        *(ULONG *)(ULONG_PTR)TokenHandle32 = (ULONG)(ULONG_PTR)TokenHandle64;

    return Status;
}

/* ================================================================
 * Object and Impersonation Thunks
 * ================================================================ */

static NTSTATUS NTAPI
Wow64Thunk_NtSetInformationObject(
    _In_ ULONG_PTR *Args32)
{
    HANDLE Handle;
    OBJECT_INFORMATION_CLASS ObjectInformationClass;
    PVOID ObjectInformation;
    ULONG ObjectInformationLength;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    Handle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    ObjectInformationClass = (OBJECT_INFORMATION_CLASS)Args32[1];
    ObjectInformation = (PVOID)(ULONG_PTR)Args32[2];
    ObjectInformationLength = (ULONG)Args32[3];

    return NtSetInformationObject(Handle,
                                  ObjectInformationClass,
                                  ObjectInformation,
                                  ObjectInformationLength);
}

static NTSTATUS NTAPI
Wow64Thunk_NtImpersonateAnonymousToken(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ThreadHandle;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    ThreadHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    return NtImpersonateAnonymousToken(ThreadHandle);
}

static NTSTATUS NTAPI
Wow64Thunk_NtImpersonateThread(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ThreadHandle;
    HANDLE ThreadToImpersonate;
    PSECURITY_QUALITY_OF_SERVICE SecurityQos;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    ThreadHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    ThreadToImpersonate = (HANDLE)(ULONG_PTR)(ULONG)Args32[1];
    SecurityQos = (PSECURITY_QUALITY_OF_SERVICE)(ULONG_PTR)Args32[2];

    return NtImpersonateThread(ThreadHandle,
                               ThreadToImpersonate,
                               SecurityQos);
}

/* ================================================================
 * LPC/ALPC (minimal mapping; 32->64 pass-through where safe)
 * ================================================================ */

static NTSTATUS NTAPI
Wow64Thunk_NtConnectPort(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE PortHandle32;
    UNICODE_STRING PortName64;
    PSECURITY_QUALITY_OF_SERVICE SecurityQos;
    PPORT_VIEW ClientView;
    PREMOTE_PORT_VIEW ServerView;
    PULONG MaxMessageLength;
    PVOID ConnectionInformation;
    PULONG ConnectionInformationLength;
    HANDLE PortHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    PortHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    Wow64ConvertUnicodeString32To64(Args32[1], &PortName64);
    SecurityQos = (PSECURITY_QUALITY_OF_SERVICE)(ULONG_PTR)Args32[2];
    ClientView = (PPORT_VIEW)(ULONG_PTR)Args32[3];
    ServerView = (PREMOTE_PORT_VIEW)(ULONG_PTR)Args32[4];
    MaxMessageLength = (PULONG)(ULONG_PTR)Args32[5];
    ConnectionInformation = (PVOID)(ULONG_PTR)Args32[6];
    ConnectionInformationLength = (PULONG)(ULONG_PTR)Args32[7];

    Status = NtConnectPort(&PortHandle64,
                           &PortName64,
                           SecurityQos,
                           ClientView,
                           ServerView,
                           MaxMessageLength,
                           ConnectionInformation,
                           ConnectionInformationLength);

    if (NT_SUCCESS(Status) && PortHandle32)
        *(ULONG *)(ULONG_PTR)PortHandle32 = (ULONG)(ULONG_PTR)PortHandle64;

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtSecureConnectPort(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE PortHandle32;
    UNICODE_STRING PortName64;
    PSECURITY_QUALITY_OF_SERVICE SecurityQos;
    PPORT_VIEW ClientView;
    PSID RequiredServerSid;
    PREMOTE_PORT_VIEW ServerView;
    PULONG MaxMessageLength;
    PVOID ConnectionInformation;
    PULONG ConnectionInformationLength;
    HANDLE PortHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    PortHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    Wow64ConvertUnicodeString32To64(Args32[1], &PortName64);
    SecurityQos = (PSECURITY_QUALITY_OF_SERVICE)(ULONG_PTR)Args32[2];
    ClientView = (PPORT_VIEW)(ULONG_PTR)Args32[3];
    RequiredServerSid = (PSID)(ULONG_PTR)Args32[4];
    ServerView = (PREMOTE_PORT_VIEW)(ULONG_PTR)Args32[5];
    MaxMessageLength = (PULONG)(ULONG_PTR)Args32[6];
    ConnectionInformation = (PVOID)(ULONG_PTR)Args32[7];
    ConnectionInformationLength = (PULONG)(ULONG_PTR)Args32[8];

    Status = NtSecureConnectPort(&PortHandle64,
                                 &PortName64,
                                 SecurityQos,
                                 ClientView,
                                 RequiredServerSid,
                                 ServerView,
                                 MaxMessageLength,
                                 ConnectionInformation,
                                 ConnectionInformationLength);

    if (NT_SUCCESS(Status) && PortHandle32)
        *(ULONG *)(ULONG_PTR)PortHandle32 = (ULONG)(ULONG_PTR)PortHandle64;

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtCreatePort(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE PortHandle32;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    ULONG MaxConnections;
    ULONG MaxMessageLength;
    ULONG MaxPoolUsage;
    HANDLE PortHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    PortHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    Wow64ConvertObjectAttributes32To64(Args32[1], &ObjectAttributes64);
    MaxConnections = (ULONG)Args32[2];
    MaxMessageLength = (ULONG)Args32[3];
    MaxPoolUsage = (ULONG)Args32[4];

    Status = NtCreatePort(&PortHandle64,
                          &ObjectAttributes64,
                          MaxConnections,
                          MaxMessageLength,
                          MaxPoolUsage);

    if (NT_SUCCESS(Status) && PortHandle32)
        *(ULONG *)(ULONG_PTR)PortHandle32 = (ULONG)(ULONG_PTR)PortHandle64;

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtCompleteConnectPort(
    _In_ ULONG_PTR *Args32)
{
    HANDLE PortHandle;
    if (!Args32)
        return STATUS_INVALID_PARAMETER;
    PortHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    return NtCompleteConnectPort(PortHandle);
}

static NTSTATUS NTAPI
Wow64Thunk_NtListenPort(
    _In_ ULONG_PTR *Args32)
{
    HANDLE PortHandle;
    PPORT_MESSAGE ConnectionRequest;
    if (!Args32)
        return STATUS_INVALID_PARAMETER;
    PortHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    ConnectionRequest = (PPORT_MESSAGE)(ULONG_PTR)Args32[1];
    return NtListenPort(PortHandle, ConnectionRequest);
}

static NTSTATUS NTAPI
Wow64Thunk_NtReplyPort(
    _In_ ULONG_PTR *Args32)
{
    HANDLE PortHandle;
    PPORT_MESSAGE ReplyMessage;
    if (!Args32)
        return STATUS_INVALID_PARAMETER;
    PortHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    ReplyMessage = (PPORT_MESSAGE)(ULONG_PTR)Args32[1];
    return NtReplyPort(PortHandle, ReplyMessage);
}

static NTSTATUS NTAPI
Wow64Thunk_NtReplyWaitReplyPort(
    _In_ ULONG_PTR *Args32)
{
    HANDLE PortHandle;
    PPORT_MESSAGE ReplyMessage;
    if (!Args32)
        return STATUS_INVALID_PARAMETER;
    PortHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    ReplyMessage = (PPORT_MESSAGE)(ULONG_PTR)Args32[1];
    return NtReplyWaitReplyPort(PortHandle, ReplyMessage);
}

static NTSTATUS NTAPI
Wow64Thunk_NtReplyWaitReceivePort(
    _In_ ULONG_PTR *Args32)
{
    HANDLE PortHandle;
    PVOID *PortContext;
    PPORT_MESSAGE ReplyMessage;
    PPORT_MESSAGE ReceiveMessage;
    if (!Args32)
        return STATUS_INVALID_PARAMETER;
    PortHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    PortContext = (PVOID *)(ULONG_PTR)Args32[1];
    ReplyMessage = (PPORT_MESSAGE)(ULONG_PTR)Args32[2];
    ReceiveMessage = (PPORT_MESSAGE)(ULONG_PTR)Args32[3];
    return NtReplyWaitReceivePort(PortHandle, PortContext, ReplyMessage, ReceiveMessage);
}

static NTSTATUS NTAPI
Wow64Thunk_NtRequestPort(
    _In_ ULONG_PTR *Args32)
{
    HANDLE PortHandle;
    PPORT_MESSAGE RequestMessage;
    if (!Args32)
        return STATUS_INVALID_PARAMETER;
    PortHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    RequestMessage = (PPORT_MESSAGE)(ULONG_PTR)Args32[1];
    return NtRequestPort(PortHandle, RequestMessage);
}

static NTSTATUS NTAPI
Wow64Thunk_NtRequestWaitReplyPort(
    _In_ ULONG_PTR *Args32)
{
    HANDLE PortHandle;
    PPORT_MESSAGE ReplyMessage;
    PPORT_MESSAGE RequestMessage;
    if (!Args32)
        return STATUS_INVALID_PARAMETER;
    PortHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    ReplyMessage = (PPORT_MESSAGE)(ULONG_PTR)Args32[1];
    RequestMessage = (PPORT_MESSAGE)(ULONG_PTR)Args32[2];
    return NtRequestWaitReplyPort(PortHandle, ReplyMessage, RequestMessage);
}

static NTSTATUS NTAPI
Wow64Thunk_NtReplyWaitReceivePortEx(
    _In_ ULONG_PTR *Args32)
{
    HANDLE PortHandle;
    PVOID *PortContext;
    PPORT_MESSAGE ReplyMessage;
    PPORT_MESSAGE ReceiveMessage;
    PLARGE_INTEGER Timeout;
    if (!Args32)
        return STATUS_INVALID_PARAMETER;
    PortHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    PortContext = (PVOID *)(ULONG_PTR)Args32[1];
    ReplyMessage = (PPORT_MESSAGE)(ULONG_PTR)Args32[2];
    ReceiveMessage = (PPORT_MESSAGE)(ULONG_PTR)Args32[3];
    Timeout = (PLARGE_INTEGER)(ULONG_PTR)Args32[4];
    return NtReplyWaitReceivePortEx(PortHandle, PortContext, ReplyMessage, ReceiveMessage, Timeout);
}

static NTSTATUS NTAPI
Wow64Thunk_NtReadRequestData(
    _In_ ULONG_PTR *Args32)
{
    HANDLE PortHandle;
    PPORT_MESSAGE Message;
    ULONG Index;
    PVOID Buffer;
    ULONG BufferLength;
    PULONG ReturnLength;
    if (!Args32)
        return STATUS_INVALID_PARAMETER;
    PortHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    Message = (PPORT_MESSAGE)(ULONG_PTR)Args32[1];
    Index = (ULONG)Args32[2];
    Buffer = (PVOID)(ULONG_PTR)Args32[3];
    BufferLength = (ULONG)Args32[4];
    ReturnLength = (PULONG)(ULONG_PTR)Args32[5];
    return NtReadRequestData(PortHandle, Message, Index, Buffer, BufferLength, ReturnLength);
}

static NTSTATUS NTAPI
Wow64Thunk_NtWriteRequestData(
    _In_ ULONG_PTR *Args32)
{
    HANDLE PortHandle;
    PPORT_MESSAGE Message;
    ULONG Index;
    PVOID Buffer;
    ULONG BufferLength;
    PULONG ReturnLength;
    if (!Args32)
        return STATUS_INVALID_PARAMETER;
    PortHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    Message = (PPORT_MESSAGE)(ULONG_PTR)Args32[1];
    Index = (ULONG)Args32[2];
    Buffer = (PVOID)(ULONG_PTR)Args32[3];
    BufferLength = (ULONG)Args32[4];
    ReturnLength = (PULONG)(ULONG_PTR)Args32[5];
    return NtWriteRequestData(PortHandle, Message, Index, Buffer, BufferLength, ReturnLength);
}

static NTSTATUS NTAPI
Wow64Thunk_NtImpersonateClientOfPort(
    _In_ ULONG_PTR *Args32)
{
    HANDLE PortHandle;
    PPORT_MESSAGE ClientMessage;
    if (!Args32)
        return STATUS_INVALID_PARAMETER;
    PortHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    ClientMessage = (PPORT_MESSAGE)(ULONG_PTR)Args32[1];
    return NtImpersonateClientOfPort(PortHandle, ClientMessage);
}

static NTSTATUS NTAPI
Wow64Thunk_NtQueryInformationPort(
    _In_ ULONG_PTR *Args32)
{
    HANDLE PortHandle;
    PORT_INFORMATION_CLASS PortInformationClass;
    PVOID PortInformation;
    ULONG PortInformationLength;
    PULONG ReturnLength;
    if (!Args32)
        return STATUS_INVALID_PARAMETER;
    PortHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    PortInformationClass = (PORT_INFORMATION_CLASS)Args32[1];
    PortInformation = (PVOID)(ULONG_PTR)Args32[2];
    PortInformationLength = (ULONG)Args32[3];
    ReturnLength = (PULONG)(ULONG_PTR)Args32[4];
    return NtQueryInformationPort(PortHandle,
                                  PortInformationClass,
                                  PortInformation,
                                  PortInformationLength,
                                  ReturnLength);
}

/* ================================================================
 * Token/Security Thunks
 * ================================================================ */

static NTSTATUS NTAPI
Wow64Thunk_NtOpenProcessToken(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ProcessHandle;
    ACCESS_MASK DesiredAccess;
    PHANDLE TokenHandle32;
    HANDLE TokenHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    ProcessHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    TokenHandle32 = (PHANDLE)(ULONG_PTR)Args32[2];

    Status = NtOpenProcessToken(ProcessHandle, DesiredAccess, &TokenHandle64);
    if (NT_SUCCESS(Status) && TokenHandle32)
    {
        *(ULONG *)(ULONG_PTR)TokenHandle32 = (ULONG)(ULONG_PTR)TokenHandle64;
    }

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtOpenProcessTokenEx(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ProcessHandle;
    ACCESS_MASK DesiredAccess;
    ULONG HandleAttributes;
    PHANDLE TokenHandle32;
    HANDLE TokenHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    ProcessHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    HandleAttributes = (ULONG)Args32[2];
    TokenHandle32 = (PHANDLE)(ULONG_PTR)Args32[3];

    Status = NtOpenProcessTokenEx(ProcessHandle, DesiredAccess, HandleAttributes, &TokenHandle64);
    if (NT_SUCCESS(Status) && TokenHandle32)
    {
        *(ULONG *)(ULONG_PTR)TokenHandle32 = (ULONG)(ULONG_PTR)TokenHandle64;
    }

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtQueryInformationToken(
    _In_ ULONG_PTR *Args32)
{
    HANDLE TokenHandle;
    TOKEN_INFORMATION_CLASS TokenInformationClass;
    PVOID TokenInformation;
    ULONG TokenInformationLength;
    PULONG ReturnLength;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    TokenHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    TokenInformationClass = (TOKEN_INFORMATION_CLASS)Args32[1];
    TokenInformation = (PVOID)(ULONG_PTR)Args32[2];
    TokenInformationLength = (ULONG)Args32[3];
    ReturnLength = (PULONG)(ULONG_PTR)Args32[4];

    return NtQueryInformationToken(TokenHandle,
                                   TokenInformationClass,
                                   TokenInformation,
                                   TokenInformationLength,
                                   ReturnLength);
}

static NTSTATUS NTAPI
Wow64Thunk_NtSetInformationToken(
    _In_ ULONG_PTR *Args32)
{
    HANDLE TokenHandle;
    TOKEN_INFORMATION_CLASS TokenInformationClass;
    PVOID TokenInformation;
    ULONG TokenInformationLength;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    TokenHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    TokenInformationClass = (TOKEN_INFORMATION_CLASS)Args32[1];
    TokenInformation = (PVOID)(ULONG_PTR)Args32[2];
    TokenInformationLength = (ULONG)Args32[3];

    return NtSetInformationToken(TokenHandle,
                                 TokenInformationClass,
                                 TokenInformation,
                                 TokenInformationLength);
}

static NTSTATUS NTAPI
Wow64Thunk_NtDuplicateToken(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ExistingTokenHandle;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    BOOLEAN EffectiveOnly;
    TOKEN_TYPE TokenType;
    PHANDLE NewTokenHandle32;
    HANDLE NewTokenHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    ExistingTokenHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);
    EffectiveOnly = (BOOLEAN)Args32[3];
    TokenType = (TOKEN_TYPE)Args32[4];
    NewTokenHandle32 = (PHANDLE)(ULONG_PTR)Args32[5];

    Status = NtDuplicateToken(ExistingTokenHandle,
                              DesiredAccess,
                              (Args32[2] ? &ObjectAttributes64 : NULL),
                              EffectiveOnly,
                              TokenType,
                              &NewTokenHandle64);

    if (NT_SUCCESS(Status) && NewTokenHandle32)
    {
        *(ULONG *)(ULONG_PTR)NewTokenHandle32 = (ULONG)(ULONG_PTR)NewTokenHandle64;
    }

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtAdjustPrivilegesToken(
    _In_ ULONG_PTR *Args32)
{
    HANDLE TokenHandle;
    BOOLEAN DisableAllPrivileges;
    PTOKEN_PRIVILEGES NewState;
    ULONG BufferLength;
    PTOKEN_PRIVILEGES PreviousState;
    PULONG ReturnLength;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    TokenHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    DisableAllPrivileges = (BOOLEAN)Args32[1];
    NewState = (PTOKEN_PRIVILEGES)(ULONG_PTR)Args32[2];
    BufferLength = (ULONG)Args32[3];
    PreviousState = (PTOKEN_PRIVILEGES)(ULONG_PTR)Args32[4];
    ReturnLength = (PULONG)(ULONG_PTR)Args32[5];

    return NtAdjustPrivilegesToken(TokenHandle,
                                   DisableAllPrivileges,
                                   NewState,
                                   BufferLength,
                                   PreviousState,
                                   ReturnLength);
}

static NTSTATUS NTAPI
Wow64Thunk_NtCreateMailslotFile(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE MailSlotFileHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    IO_STATUS_BLOCK IoStatusBlock64;
    ULONG FileAttributes;
    ULONG ShareAccess;
    ULONG MaxMessageSize;
    PLARGE_INTEGER TimeOut;
    HANDLE MailSlotFileHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    MailSlotFileHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);
    Wow64ConvertIoStatusBlock32To64(Args32[3], &IoStatusBlock64);
    FileAttributes = (ULONG)Args32[4];
    ShareAccess = (ULONG)Args32[5];
    MaxMessageSize = (ULONG)Args32[6];
    TimeOut = (PLARGE_INTEGER)(ULONG_PTR)Args32[7];

    Status = NtCreateMailslotFile(&MailSlotFileHandle64,
                                  DesiredAccess,
                                  (Args32[2] ? &ObjectAttributes64 : NULL),
                                  &IoStatusBlock64,
                                  FileAttributes,
                                  ShareAccess,
                                  MaxMessageSize,
                                  TimeOut);

    if (MailSlotFileHandle32)
    {
        *(ULONG *)(ULONG_PTR)MailSlotFileHandle32 = (ULONG)(ULONG_PTR)MailSlotFileHandle64;
    }

    Wow64ConvertIoStatusBlock64To32(&IoStatusBlock64, Args32[3]);

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtCreateNamedPipeFile(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE NamedPipeFileHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    IO_STATUS_BLOCK IoStatusBlock64;
    ULONG ShareAccess;
    ULONG CreateDisposition;
    ULONG CreateOptions;
    ULONG WriteModeMessage;
    ULONG ReadModeMessage;
    ULONG NonBlocking;
    ULONG MaxInstances;
    ULONG InBufferSize;
    ULONG OutBufferSize;
    PLARGE_INTEGER DefaultTimeOut;
    HANDLE NamedPipeFileHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    NamedPipeFileHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);
    Wow64ConvertIoStatusBlock32To64(Args32[3], &IoStatusBlock64);
    ShareAccess = (ULONG)Args32[4];
    CreateDisposition = (ULONG)Args32[5];
    CreateOptions = (ULONG)Args32[6];
    WriteModeMessage = (ULONG)Args32[7];
    ReadModeMessage = (ULONG)Args32[8];
    NonBlocking = (ULONG)Args32[9];
    MaxInstances = (ULONG)Args32[10];
    InBufferSize = (ULONG)Args32[11];
    OutBufferSize = (ULONG)Args32[12];
    DefaultTimeOut = (PLARGE_INTEGER)(ULONG_PTR)Args32[13];

    Status = NtCreateNamedPipeFile(&NamedPipeFileHandle64,
                                   DesiredAccess,
                                   (Args32[2] ? &ObjectAttributes64 : NULL),
                                   &IoStatusBlock64,
                                   ShareAccess,
                                   CreateDisposition,
                                   CreateOptions,
                                   WriteModeMessage,
                                   ReadModeMessage,
                                   NonBlocking,
                                   MaxInstances,
                                   InBufferSize,
                                   OutBufferSize,
                                   DefaultTimeOut);

    if (NamedPipeFileHandle32)
    {
        *(ULONG *)(ULONG_PTR)NamedPipeFileHandle32 = (ULONG)(ULONG_PTR)NamedPipeFileHandle64;
    }

    Wow64ConvertIoStatusBlock64To32(&IoStatusBlock64, Args32[3]);

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtClose(
    _In_ ULONG_PTR *Args32)
{
    HANDLE Handle;

    Handle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    return NtClose(Handle);
}

static NTSTATUS NTAPI
Wow64Thunk_NtCreateEvent(
    _In_ ULONG_PTR *Args32)
{
    NTSTATUS Status;
    HANDLE EventHandle64 = NULL;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    EVENT_TYPE EventType;
    BOOLEAN InitialState;

    /* Args32[0] = EventHandle*
     * Args32[1] = DesiredAccess
     * Args32[2] = ObjectAttributes*
     * Args32[3] = EventType
     * Args32[4] = InitialState
     */

    EventType = (EVENT_TYPE)Args32[3];
    InitialState = (BOOLEAN)Args32[4];

    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);

    Status = NtCreateEvent(&EventHandle64,
                          (ACCESS_MASK)Args32[1],
                          &ObjectAttributes64,
                          EventType,
                          InitialState);

    if (Args32[0])
    {
        *(ULONG *)(ULONG_PTR)Args32[0] = (ULONG)(ULONG_PTR)EventHandle64;
    }

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtWaitForSingleObject(
    _In_ ULONG_PTR *Args32)
{
    HANDLE Handle;
    BOOLEAN Alertable;
    PLARGE_INTEGER Timeout;

    /* Args32[0] = Handle
     * Args32[1] = Alertable
     * Args32[2] = Timeout*
     */

    Handle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    Alertable = (BOOLEAN)Args32[1];
    Timeout = (PLARGE_INTEGER)(ULONG_PTR)Args32[2];

    return NtWaitForSingleObject(Handle, Alertable, Timeout);
}

static NTSTATUS NTAPI
Wow64Thunk_NtResumeThread(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ThreadHandle;
    PULONG SuspendCount;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ThreadHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    SuspendCount = (PULONG)(ULONG_PTR)Args32[1];

    return NtResumeThread(ThreadHandle, SuspendCount);
}

static NTSTATUS NTAPI
Wow64Thunk_NtSuspendThread(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ThreadHandle;
    PULONG PreviousSuspendCount;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ThreadHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    PreviousSuspendCount = (PULONG)(ULONG_PTR)Args32[1];

    return NtSuspendThread(ThreadHandle, PreviousSuspendCount);
}

static NTSTATUS NTAPI
Wow64Thunk_NtAllocateVirtualMemory(
    _In_ ULONG_PTR *Args32)
{
    NTSTATUS Status;
    HANDLE ProcessHandle;
    PVOID *BaseAddress;
    ULONG_PTR ZeroBits;
    PSIZE_T RegionSize;
    ULONG AllocationType;
    ULONG Protect;

    /* Args32[0] = ProcessHandle
     * Args32[1] = BaseAddress**
     * Args32[2] = ZeroBits
     * Args32[3] = RegionSize*
     * Args32[4] = AllocationType
     * Args32[5] = Protect
     */

    ProcessHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    BaseAddress = (PVOID *)(ULONG_PTR)Args32[1];
    ZeroBits = (ULONG_PTR)Args32[2];
    RegionSize = (PSIZE_T)(ULONG_PTR)Args32[3];
    AllocationType = (ULONG)Args32[4];
    Protect = (ULONG)Args32[5];

    Status = NtAllocateVirtualMemory(ProcessHandle,
                                    BaseAddress,
                                    ZeroBits,
                                    RegionSize,
                                    AllocationType,
                                    Protect);

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtFreeVirtualMemory(
    _In_ ULONG_PTR *Args32)
{
    NTSTATUS Status;
    HANDLE ProcessHandle;
    PVOID *BaseAddress;
    PSIZE_T RegionSize;
    ULONG FreeType;

    /* Args32[0] = ProcessHandle
     * Args32[1] = BaseAddress**
     * Args32[2] = RegionSize*
     * Args32[3] = FreeType
     */

    ProcessHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    BaseAddress = (PVOID *)(ULONG_PTR)Args32[1];
    RegionSize = (PSIZE_T)(ULONG_PTR)Args32[2];
    FreeType = (ULONG)Args32[3];

    Status = NtFreeVirtualMemory(ProcessHandle,
                                BaseAddress,
                                RegionSize,
                                FreeType);

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtCreateProcess(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE ProcessHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    HANDLE ParentProcess;
    BOOLEAN InheritObjectTable;
    HANDLE SectionHandle;
    HANDLE DebugPort;
    HANDLE ExceptionPort;
    HANDLE ProcessHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    ProcessHandle32     = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess       = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);
    ParentProcess       = (HANDLE)(ULONG_PTR)(ULONG)Args32[3];
    InheritObjectTable  = (BOOLEAN)Args32[4];
    SectionHandle       = (HANDLE)(ULONG_PTR)(ULONG)Args32[5];
    DebugPort           = (HANDLE)(ULONG_PTR)(ULONG)Args32[6];
    ExceptionPort       = (HANDLE)(ULONG_PTR)(ULONG)Args32[7];

    Status = NtCreateProcess(&ProcessHandle64,
                             DesiredAccess,
                             (Args32[2] ? &ObjectAttributes64 : NULL),
                             ParentProcess,
                             InheritObjectTable,
                             SectionHandle,
                             DebugPort,
                             ExceptionPort);

    if (NT_SUCCESS(Status) && ProcessHandle32)
        *(ULONG *)(ULONG_PTR)ProcessHandle32 = (ULONG)(ULONG_PTR)ProcessHandle64;

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtCreateProcessEx(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE ProcessHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    HANDLE ParentProcess;
    ULONG Flags;
    HANDLE SectionHandle;
    HANDLE DebugPort;
    HANDLE ExceptionPort;
    BOOLEAN InJob;
    HANDLE ProcessHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
        return STATUS_INVALID_PARAMETER;

    ProcessHandle32     = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess       = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);
    ParentProcess       = (HANDLE)(ULONG_PTR)(ULONG)Args32[3];
    Flags               = (ULONG)Args32[4];
    SectionHandle       = (HANDLE)(ULONG_PTR)(ULONG)Args32[5];
    DebugPort           = (HANDLE)(ULONG_PTR)(ULONG)Args32[6];
    ExceptionPort       = (HANDLE)(ULONG_PTR)(ULONG)Args32[7];
    InJob               = (BOOLEAN)Args32[8];

    Status = NtCreateProcessEx(&ProcessHandle64,
                               DesiredAccess,
                               (Args32[2] ? &ObjectAttributes64 : NULL),
                               ParentProcess,
                               Flags,
                               SectionHandle,
                               DebugPort,
                               ExceptionPort,
                               InJob);

    if (NT_SUCCESS(Status) && ProcessHandle32)
        *(ULONG *)(ULONG_PTR)ProcessHandle32 = (ULONG)(ULONG_PTR)ProcessHandle64;

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtCreateSection(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE SectionHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    PLARGE_INTEGER MaximumSize;
    ULONG SectionPageProtection;
    ULONG AllocationAttributes;
    HANDLE FileHandle;
    HANDLE SectionHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    SectionHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);
    MaximumSize = (PLARGE_INTEGER)(ULONG_PTR)Args32[3];
    SectionPageProtection = (ULONG)Args32[4];
    AllocationAttributes = (ULONG)Args32[5];
    FileHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[6];

    Status = NtCreateSection(&SectionHandle64,
                             DesiredAccess,
                             Args32[2] ? &ObjectAttributes64 : NULL,
                             MaximumSize,
                             SectionPageProtection,
                             AllocationAttributes,
                             FileHandle);

    if (SectionHandle32)
    {
        *(ULONG *)(ULONG_PTR)SectionHandle32 = (ULONG)(ULONG_PTR)SectionHandle64;
    }

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtOpenSection(
    _In_ ULONG_PTR *Args32)
{
    PHANDLE SectionHandle32;
    ACCESS_MASK DesiredAccess;
    OBJECT_ATTRIBUTES ObjectAttributes64;
    HANDLE SectionHandle64 = NULL;
    NTSTATUS Status;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    SectionHandle32 = (PHANDLE)(ULONG_PTR)Args32[0];
    DesiredAccess = (ACCESS_MASK)Args32[1];
    Wow64ConvertObjectAttributes32To64(Args32[2], &ObjectAttributes64);

    Status = NtOpenSection(&SectionHandle64,
                           DesiredAccess,
                           Args32[2] ? &ObjectAttributes64 : NULL);

    if (SectionHandle32)
    {
        *(ULONG *)(ULONG_PTR)SectionHandle32 = (ULONG)(ULONG_PTR)SectionHandle64;
    }

    return Status;
}

static NTSTATUS NTAPI
Wow64Thunk_NtMapViewOfSection(
    _In_ ULONG_PTR *Args32)
{
    HANDLE SectionHandle;
    HANDLE ProcessHandle;
    PVOID *BaseAddress;
    ULONG_PTR ZeroBits;
    SIZE_T CommitSize;
    PLARGE_INTEGER SectionOffset;
    PSIZE_T ViewSize;
    SECTION_INHERIT InheritDisposition;
    ULONG AllocationType;
    ULONG Win32Protect;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    SectionHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    ProcessHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[1];
    BaseAddress = (PVOID *)(ULONG_PTR)Args32[2];
    ZeroBits = (ULONG_PTR)Args32[3];
    CommitSize = (SIZE_T)Args32[4];
    SectionOffset = (PLARGE_INTEGER)(ULONG_PTR)Args32[5];
    ViewSize = (PSIZE_T)(ULONG_PTR)Args32[6];
    InheritDisposition = (SECTION_INHERIT)Args32[7];
    AllocationType = (ULONG)Args32[8];
    Win32Protect = (ULONG)Args32[9];

    return NtMapViewOfSection(SectionHandle,
                              ProcessHandle,
                              BaseAddress,
                              ZeroBits,
                              CommitSize,
                              SectionOffset,
                              ViewSize,
                              InheritDisposition,
                              AllocationType,
                              Win32Protect);
}

static NTSTATUS NTAPI
Wow64Thunk_NtUnmapViewOfSection(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ProcessHandle;
    PVOID BaseAddress;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ProcessHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    BaseAddress = (PVOID)(ULONG_PTR)Args32[1];

    return NtUnmapViewOfSection(ProcessHandle, BaseAddress);
}

static NTSTATUS NTAPI
Wow64Thunk_NtWow64AllocateVirtualMemory64(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ProcessHandle;
    PULONG64 BaseAddress;
    ULONGLONG ZeroBits;
    PULONG64 RegionSize;
    ULONG AllocationType;
    ULONG Protect;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ProcessHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    BaseAddress = (PULONG64)(ULONG_PTR)Args32[1];
    ZeroBits = Wow64ReadUlong64Argument(&Args32[2]);
    RegionSize = (PULONG64)(ULONG_PTR)Args32[4];
    AllocationType = (ULONG)Args32[5];
    Protect = (ULONG)Args32[6];

    return NtWow64AllocateVirtualMemory64(ProcessHandle,
                                          BaseAddress,
                                          ZeroBits,
                                          RegionSize,
                                          AllocationType,
                                          Protect);
}

static NTSTATUS NTAPI
Wow64Thunk_NtWow64ReadVirtualMemory64(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ProcessHandle;
    ULONGLONG BaseAddress;
    PVOID Buffer;
    ULONGLONG NumberOfBytesToRead;
    PULONG64 NumberOfBytesRead;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ProcessHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    BaseAddress = Wow64ReadUlong64Argument(&Args32[1]);
    Buffer = (PVOID)(ULONG_PTR)Args32[3];
    NumberOfBytesToRead = Wow64ReadUlong64Argument(&Args32[4]);
    NumberOfBytesRead = (PULONG64)(ULONG_PTR)Args32[6];

    return NtWow64ReadVirtualMemory64(ProcessHandle,
                                      BaseAddress,
                                      Buffer,
                                      NumberOfBytesToRead,
                                      NumberOfBytesRead);
}

static NTSTATUS NTAPI
Wow64Thunk_NtWow64WriteVirtualMemory64(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ProcessHandle;
    ULONGLONG BaseAddress;
    PVOID Buffer;
    ULONGLONG NumberOfBytesToWrite;
    PULONG64 NumberOfBytesWritten;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ProcessHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    BaseAddress = Wow64ReadUlong64Argument(&Args32[1]);
    Buffer = (PVOID)(ULONG_PTR)Args32[3];
    NumberOfBytesToWrite = Wow64ReadUlong64Argument(&Args32[4]);
    NumberOfBytesWritten = (PULONG64)(ULONG_PTR)Args32[6];

    return NtWow64WriteVirtualMemory64(ProcessHandle,
                                       BaseAddress,
                                       Buffer,
                                       NumberOfBytesToWrite,
                                       NumberOfBytesWritten);
}

static NTSTATUS NTAPI
Wow64Thunk_NtWow64QueryInformationProcess64(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ProcessHandle;
    PROCESSINFOCLASS ProcessInformationClass;
    PVOID ProcessInformation;
    ULONG ProcessInformationLength;
    PULONG ReturnLength;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ProcessHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    ProcessInformationClass = (PROCESSINFOCLASS)Args32[1];
    ProcessInformation = (PVOID)(ULONG_PTR)Args32[2];
    ProcessInformationLength = (ULONG)Args32[3];
    ReturnLength = (PULONG)(ULONG_PTR)Args32[4];

    return NtWow64QueryInformationProcess64(ProcessHandle,
                                            ProcessInformationClass,
                                            ProcessInformation,
                                            ProcessInformationLength,
                                            ReturnLength);
}

static NTSTATUS NTAPI
Wow64Thunk_NtWow64GetNativeSystemInformation(
    _In_ ULONG_PTR *Args32)
{
    SYSTEM_INFORMATION_CLASS SystemInformationClass;
    PVOID SystemInformation;
    ULONG SystemInformationLength;
    PULONG ReturnLength;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    SystemInformationClass = (SYSTEM_INFORMATION_CLASS)Args32[0];
    SystemInformation = (PVOID)(ULONG_PTR)Args32[1];
    SystemInformationLength = (ULONG)Args32[2];
    ReturnLength = (PULONG)(ULONG_PTR)Args32[3];

    return NtWow64GetNativeSystemInformation(SystemInformationClass,
                                             SystemInformation,
                                             SystemInformationLength,
                                             ReturnLength);
}

static NTSTATUS NTAPI
Wow64Thunk_NtWow64IsProcessorFeaturePresent(
    _In_ ULONG_PTR *Args32)
{
    ULONG ProcessorFeature;

    if (!Args32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ProcessorFeature = (ULONG)Args32[0];

    return NtWow64IsProcessorFeaturePresent(ProcessorFeature);
}

static NTSTATUS NTAPI
Wow64Thunk_NtTerminateProcess(
    _In_ ULONG_PTR *Args32)
{
    HANDLE ProcessHandle;
    NTSTATUS ExitStatus;

    /* Args32[0] = ProcessHandle
     * Args32[1] = ExitStatus
     */

    ProcessHandle = (HANDLE)(ULONG_PTR)(ULONG)Args32[0];
    ExitStatus = (NTSTATUS)Args32[1];

    return NtTerminateProcess(ProcessHandle, ExitStatus);
}

static NTSTATUS NTAPI
Wow64Thunk_NtCallbackReturn(
    _In_ ULONG_PTR *Args32)
{
    PVOID ResultBuffer32;
    ULONG ResultLength32;
    NTSTATUS CallbackStatus;
    PWOW64_CALLBACK_FRAME Frame;
    PVOID ResultPointer64;

    /* Args32[0] = Result buffer pointer
     * Args32[1] = Result length
     * Args32[2] = Callback status
     */

    ResultBuffer32 = (PVOID)(ULONG_PTR)(ULONG)Args32[0];
    ResultLength32 = (ULONG)Args32[1];
    CallbackStatus = (NTSTATUS)Args32[2];
    ResultPointer64 = (PVOID)(ULONG_PTR)ResultBuffer32;

    Frame = Wow64GetCallbackFrame();
    if (!Frame)
    {
        Wow64ReportStub("NtCallbackReturn invoked without active callback frame");
        return STATUS_INVALID_SYSTEM_SERVICE;
    }

    if (!(Frame->Flags & WOW64_CALLBACK_FRAME_FLAG_CALLBACK))
    {
        Wow64ReportStub("NtCallbackReturn: frame type mismatch");
        return STATUS_INVALID_SYSTEM_SERVICE;
    }

    if (Frame->OutputBuffer)
    {
        *Frame->OutputBuffer = ResultPointer64;
    }

    if (Frame->OutputLength)
    {
        *Frame->OutputLength = ResultLength32;
    }

    Wow64SetCallbackFrame(Frame->Previous);

    return NtCallbackReturn(ResultPointer64,
                             ResultLength32,
                             CallbackStatus);
}

/* ===================================================================
 * Fallback Stub Handler
 * ================================================================ */

static NTSTATUS NTAPI
Wow64Thunk_Unimplemented(
    _In_ ULONG_PTR *Args32)
{
    UNREFERENCED_PARAMETER(Args32);

#if DBG
    OutputDebugStringA("wow64.dll: Unimplemented syscall thunk called\n");
#endif

    return STATUS_NOT_IMPLEMENTED;
}

/* ===================================================================
 * Syscall Dispatch Table
 * ================================================================ */

/*
 * Service numbers map directly to indices in ntoskrnl/sysfuncs.lst (0-based).
 * ArgumentCount reflects the number of 32-bit stack slots consumed by the thunk
 * (64-bit values therefore count as two). Keep this table in sync with the
 * kernel list whenever new thunks are added.
*/
static WOW64_SYSCALL_ENTRY Wow64SyscallTable[] =
{
    { 56, 8, Wow64Thunk_NtCreateThread, "NtCreateThread" },
    { 50, 8, Wow64Thunk_NtCreateProcess, "NtCreateProcess" },
    { 51, 9, Wow64Thunk_NtCreateProcessEx, "NtCreateProcessEx" },
    { 18, 6, Wow64Thunk_NtAllocateVirtualMemory, "NtAllocateVirtualMemory" },
    { 22, 3, Wow64Thunk_NtCallbackReturn, "NtCallbackReturn" },
    { 27, 1, Wow64Thunk_NtClose, "NtClose" },
    { 37, 5, Wow64Thunk_NtCreateEvent, "NtCreateEvent" },
    { 39, 11, Wow64Thunk_NtCreateFile, "NtCreateFile" },
    { 43, 7, Wow64Thunk_NtCreateKey, "NtCreateKey" },
    { 52, 7, Wow64Thunk_NtCreateSection, "NtCreateSection" },
    { 45, 8, Wow64Thunk_NtCreateMailslotFile, "NtCreateMailslotFile" },
    { 50, 5, Wow64Thunk_NtCreatePort, "NtCreatePort" },
    { 47, 14, Wow64Thunk_NtCreateNamedPipeFile, "NtCreateNamedPipeFile" },
    { 54, 5, Wow64Thunk_NtCreateSemaphore, "NtCreateSemaphore" },
    { 55, 4, Wow64Thunk_NtCreateSymbolicLinkObject, "NtCreateSymbolicLinkObject" },
    { 46, 4, Wow64Thunk_NtCreateMutant, "NtCreateMutant" },
    { 37, 3, Wow64Thunk_NtCreateDirectoryObject, "NtCreateDirectoryObject" },
    { 39, 3, Wow64Thunk_NtCreateEventPair, "NtCreateEventPair" },
    { 65, 1, Wow64Thunk_NtDeleteFile, "NtDeleteFile" },
    { 81, 2, Wow64Thunk_NtFlushBuffersFile, "NtFlushBuffersFile" },
    { 82, 3, Wow64Thunk_NtFlushInstructionCache, "NtFlushInstructionCache" },
    { 84, 4, Wow64Thunk_NtFlushVirtualMemory, "NtFlushVirtualMemory" },
    { 87, 4, Wow64Thunk_NtFreeVirtualMemory, "NtFreeVirtualMemory" },
    { 91, 7, Wow64Thunk_NtWow64AllocateVirtualMemory64, "NtWow64AllocateVirtualMemory64" },
    { 92, 4, Wow64Thunk_NtWow64GetNativeSystemInformation, "NtWow64GetNativeSystemInformation" },
    { 93, 1, Wow64Thunk_NtWow64IsProcessorFeaturePresent, "NtWow64IsProcessorFeaturePresent" },
    { 94, 5, Wow64Thunk_NtWow64QueryInformationProcess64, "NtWow64QueryInformationProcess64" },
    { 95, 7, Wow64Thunk_NtWow64ReadVirtualMemory64, "NtWow64ReadVirtualMemory64" },
    { 97, 7, Wow64Thunk_NtWow64WriteVirtualMemory64, "NtWow64WriteVirtualMemory64" },
    { 113, 10, Wow64Thunk_NtLockFile, "NtLockFile" },
    { 121, 10, Wow64Thunk_NtMapViewOfSection, "NtMapViewOfSection" },
    { 130, 6, Wow64Thunk_NtOpenFile, "NtOpenFile" },
    { 133, 3, Wow64Thunk_NtOpenKey, "NtOpenKey" },
    { 139, 3, Wow64Thunk_NtOpenSection, "NtOpenSection" },
    { 128, 3, Wow64Thunk_NtOpenDirectoryObject, "NtOpenDirectoryObject" },
    { 130, 3, Wow64Thunk_NtOpenEvent, "NtOpenEvent" },
    { 141, 3, Wow64Thunk_NtOpenSemaphore, "NtOpenSemaphore" },
    { 130, 3, Wow64Thunk_NtOpenEventPair, "NtOpenEventPair" },
    { 151, 5, Wow64Thunk_NtProtectVirtualMemory, "NtProtectVirtualMemory" },
    { 159, 11, Wow64Thunk_NtQueryDirectoryFile, "NtQueryDirectoryFile" },
    { 161, 7, Wow64Thunk_NtQueryDirectoryObject, "NtQueryDirectoryObject" },
    { 164, 2, Wow64Thunk_NtQueryFullAttributesFile, "NtQueryFullAttributesFile" },
    { 166, 5, Wow64Thunk_NtQueryInformationFile, "NtQueryInformationFile" },
    { 154, 2, Wow64Thunk_NtQueryAttributesFile, "NtQueryAttributesFile" },
    { 196, 5, Wow64Thunk_NtQueryVolumeInformationFile, "NtQueryVolumeInformationFile" },
    { 184, 9, Wow64Thunk_NtQueryQuotaInformationFile, "NtQueryQuotaInformationFile" },
    { 169, 5, Wow64Thunk_NtQueryInformationProcess, "NtQueryInformationProcess" },
    { 176, 5, Wow64Thunk_NtQueryKey, "NtQueryKey" },
    { 173, 5, Wow64Thunk_NtQueryInformationToken, "NtQueryInformationToken" },
    { 189, 4, Wow64Thunk_NtQuerySystemInformation, "NtQuerySystemInformation" },
    { 76, 6, Wow64Thunk_NtEnumerateKey, "NtEnumerateKey" },
    { 78, 6, Wow64Thunk_NtEnumerateValueKey, "NtEnumerateValueKey" },
    { 194, 6, Wow64Thunk_NtQueryValueKey, "NtQueryValueKey" },
    { 195, 6, Wow64Thunk_NtQueryVirtualMemory, "NtQueryVirtualMemory" },
    { 200, 9, Wow64Thunk_NtReadFile, "NtReadFile" },
    { 203, 9, Wow64Thunk_NtReadFileScatter, "NtReadFileScatter" },
    { 223, 2, Wow64Thunk_NtResumeThread, "NtResumeThread" },
    { 242, 5, Wow64Thunk_NtSetInformationFile, "NtSetInformationFile" },
    { 246, 4, Wow64Thunk_NtSetInformationProcess, "NtSetInformationProcess" },
    { 250, 4, Wow64Thunk_NtSetInformationToken, "NtSetInformationToken" },
    { 247, 4, Wow64Thunk_NtSetInformationObject, "NtSetInformationObject" },
    { 268, 5, Wow64Thunk_NtSetVolumeInformationFile, "NtSetVolumeInformationFile" },
    { 265, 6, Wow64Thunk_NtSetValueKey, "NtSetValueKey" },
    { 272, 2, Wow64Thunk_NtSuspendThread, "NtSuspendThread" },
    { 275, 2, Wow64Thunk_NtTerminateProcess, "NtTerminateProcess" },
    { 284, 5, Wow64Thunk_NtUnlockFile, "NtUnlockFile" },
    { 286, 2, Wow64Thunk_NtUnmapViewOfSection, "NtUnmapViewOfSection" },
    { 290, 3, Wow64Thunk_NtWaitForSingleObject, "NtWaitForSingleObject" },
    { 293, 9, Wow64Thunk_NtWriteFile, "NtWriteFile" },
    { 296, 9, Wow64Thunk_NtWriteFileGather, "NtWriteFileGather" },

    /* New process/thread/object thunks to support console apps */
    { 13, 6,  Wow64Thunk_NtAdjustPrivilegesToken, "NtAdjustPrivilegesToken" },
    { 33, 1, Wow64Thunk_NtCompleteConnectPort, "NtCompleteConnectPort" },
    { 35, 8, Wow64Thunk_NtConnectPort, "NtConnectPort" },
    { 42, 4, Wow64Thunk_NtCreateIoCompletion, "NtCreateIoCompletion" },
    { 88, 10, Wow64Thunk_NtFsControlFile, "NtFsControlFile" },
    { 110, 2, Wow64Thunk_NtListenPort, "NtListenPort" },
    { 70, 10, Wow64Thunk_NtDeviceIoControlFile, "NtDeviceIoControlFile" },
    { 72, 7,  Wow64Thunk_NtDuplicateObject, "NtDuplicateObject" },
    { 74, 6,  Wow64Thunk_NtDuplicateToken, "NtDuplicateToken" },
    { 104, 2, Wow64Thunk_NtImpersonateClientOfPort, "NtImpersonateClientOfPort" },
    { 170, 5, Wow64Thunk_NtQueryInformationPort, "NtQueryInformationPort" },
    { 213, 2, Wow64Thunk_NtReplyPort, "NtReplyPort" },
    { 214, 4, Wow64Thunk_NtReplyWaitReceivePort, "NtReplyWaitReceivePort" },
    { 215, 5, Wow64Thunk_NtReplyWaitReceivePortEx, "NtReplyWaitReceivePortEx" },
    { 216, 2, Wow64Thunk_NtReplyWaitReplyPort, "NtReplyWaitReplyPort" },
    { 218, 2, Wow64Thunk_NtRequestPort, "NtRequestPort" },
    { 219, 3, Wow64Thunk_NtRequestWaitReplyPort, "NtRequestWaitReplyPort" },
    { 204, 6, Wow64Thunk_NtReadRequestData, "NtReadRequestData" },
    { 229, 9, Wow64Thunk_NtSecureConnectPort, "NtSecureConnectPort" },
    { 297, 6, Wow64Thunk_NtWriteRequestData, "NtWriteRequestData" },
    { 137, 4, Wow64Thunk_NtOpenProcess, "NtOpenProcess" },
    { 130, 3, Wow64Thunk_NtOpenEvent, "NtOpenEvent" },
    { 139, 3, Wow64Thunk_NtOpenProcessToken, "NtOpenProcessToken" },
    { 140, 4, Wow64Thunk_NtOpenProcessTokenEx, "NtOpenProcessTokenEx" },
    { 145, 4, Wow64Thunk_NtOpenThreadToken, "NtOpenThreadToken" },
    { 146, 5, Wow64Thunk_NtOpenThreadTokenEx, "NtOpenThreadTokenEx" },
    { 135, 3, Wow64Thunk_NtOpenMutant, "NtOpenMutant" },
    { 143, 4, Wow64Thunk_NtOpenThread, "NtOpenThread" },
    { 142, 3, Wow64Thunk_NtOpenSymbolicLinkObject, "NtOpenSymbolicLinkObject" },
    { 171, 5, Wow64Thunk_NtQueryInformationThread, "NtQueryInformationThread" },
    { 179, 5, Wow64Thunk_NtQueryObject, "NtQueryObject" },
    { 209, 5, Wow64Thunk_NtRemoveIoCompletion, "NtRemoveIoCompletion" },
    { 248, 4, Wow64Thunk_NtSetInformationThread, "NtSetInformationThread" },
    { 239, 2, Wow64Thunk_NtSetEvent, "NtSetEvent" },
    { 221, 2, Wow64Thunk_NtResetEvent, "NtResetEvent" },
    { 252, 5, Wow64Thunk_NtSetIoCompletion, "NtSetIoCompletion" },
    { 269, 4, Wow64Thunk_NtSignalAndWaitForSingleObject, "NtSignalAndWaitForSingleObject" },
    { 290, 5, Wow64Thunk_NtWaitForMultipleObjects, "NtWaitForMultipleObjects" },
    { 187, 3, Wow64Thunk_NtQuerySymbolicLinkObject, "NtQuerySymbolicLinkObject" },
    { 206, 2, Wow64Thunk_NtReleaseMutant, "NtReleaseMutant" },
    { 207, 3, Wow64Thunk_NtReleaseSemaphore, "NtReleaseSemaphore" },
    { 103, 1, Wow64Thunk_NtImpersonateAnonymousToken, "NtImpersonateAnonymousToken" },
    { 105, 3, Wow64Thunk_NtImpersonateThread, "NtImpersonateThread" },
};

#define WOW64_SYSCALL_TABLE_SIZE (sizeof(Wow64SyscallTable) / sizeof(Wow64SyscallTable[0]))

static NTSTATUS
Wow64DispatchSyscall(
    _In_ ULONG ServiceNumber,
    _In_ ULONG_PTR *Arguments32)
{
    ULONG Index;

    if (!Arguments32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Linear search through syscall table */
    for (Index = 0; Index < WOW64_SYSCALL_TABLE_SIZE; Index++)
    {
        if (Wow64SyscallTable[Index].ServiceNumber == ServiceNumber)
        {
#if DBG
            if (Wow64SyscallTable[Index].ServiceName)
            {
                CHAR Buffer[256];
                _snprintf(Buffer, sizeof(Buffer),
                         "wow64: Dispatching syscall %s (service %lu)\n",
                         Wow64SyscallTable[Index].ServiceName,
                         ServiceNumber);
                Buffer[sizeof(Buffer) - 1] = '\0';
                OutputDebugStringA(Buffer);
            }
#endif
            return Wow64SyscallTable[Index].ThunkFunction(Arguments32);
        }
    }

    /* Unimplemented syscall */
#if DBG
    {
        CHAR Buffer[128];
        _snprintf(Buffer, sizeof(Buffer),
                 "wow64: Unimplemented syscall %lu\n",
                 ServiceNumber);
        Buffer[sizeof(Buffer) - 1] = '\0';
        OutputDebugStringA(Buffer);
    }
#endif

    return Wow64Thunk_Unimplemented(Arguments32);
}

/* Export for wow64cpu to invoke */
NTSTATUS
NTAPI
Wow64SystemServiceEx(
    _In_ ULONG ServiceNumber,
    _In_ ULONG_PTR *Arguments32)
{
    return Wow64DispatchSyscall(ServiceNumber, Arguments32);
}

/* ===================================================================
 * TLS and Thread Context Helpers
 * ================================================================ */

static BOOLEAN
Wow64EnsureTlsIndex(VOID)
{
    LONG CurrentIndex;

    CurrentIndex = Wow64TlsIndex;
    if (CurrentIndex != TLS_OUT_OF_INDEXES)
    {
        return TRUE;
    }

    CurrentIndex = (LONG)TlsAlloc();
    if (CurrentIndex == TLS_OUT_OF_INDEXES)
    {
        return FALSE;
    }

    if (InterlockedCompareExchange(&Wow64TlsIndex,
                                   CurrentIndex,
                                   TLS_OUT_OF_INDEXES) != TLS_OUT_OF_INDEXES)
    {
        TlsFree((DWORD)CurrentIndex);
    }

    return Wow64TlsIndex != TLS_OUT_OF_INDEXES;
}

NTSTATUS
NTAPI
Wow64SetThreadContext(
    _In_ PVOID ThreadContext)
{
    if (!Wow64EnsureTlsIndex())
    {
        return STATUS_NO_MEMORY;
    }

    if (!TlsSetValue((DWORD)Wow64TlsIndex, ThreadContext))
    {
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

PVOID
NTAPI
Wow64GetThreadContext(VOID)
{
    if (Wow64TlsIndex == TLS_OUT_OF_INDEXES)
    {
        return NULL;
    }

    return TlsGetValue((DWORD)Wow64TlsIndex);
}

NTSTATUS
NTAPI
Wow64ClearThreadContext(VOID)
{
    if (Wow64TlsIndex != TLS_OUT_OF_INDEXES)
    {
        TlsSetValue((DWORD)Wow64TlsIndex, NULL);
    }

    return STATUS_SUCCESS;
}

/* Helper to check if current thread has WOW64 context */
BOOLEAN
NTAPI
Wow64IsCurrentThreadWow64(VOID)
{
    return (Wow64GetThreadContext() != NULL) ||
           (Wow64State.CpuArea != NULL);
}

/* Deliver APCs with TLS context awareness */
NTSTATUS
NTAPI
Wow64DeliverPendingApcEx(
    _In_ BOOLEAN CheckTls)
{
    PWOW64_CPU_AREA CpuArea;

    if (CheckTls)
    {
        PVOID ThreadContext = Wow64GetThreadContext();
        if (ThreadContext)
        {
            CpuArea = (PWOW64_CPU_AREA)ThreadContext;
        }
        else
        {
            CpuArea = Wow64State.CpuArea;
        }
    }
    else
    {
        CpuArea = Wow64State.CpuArea;
    }

    if (!CpuArea)
    {
        return STATUS_NOT_FOUND;
    }

    /* Save the CPU area in TLS for this call */
    if (CheckTls)
    {
        Wow64SetThreadContext(CpuArea);
    }

    return Wow64DeliverPendingApc();
}

/* ===================================================================
 * APC Delivery
 * ================================================================ */

VOID
WINAPI
Wow64ApcRoutine(
    ULONG_PTR NormalContext,
    ULONG_PTR SystemArgument1,
    ULONG_PTR SystemArgument2,
    PCONTEXT SystemContext)
{
    PWOW64_APC_CONTEXT ApcContext;
    WOW64_APC_CONTEXT LocalContext;
    PWOW64_CPU_AREA CpuArea;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(SystemContext);

    ApcContext = (PWOW64_APC_CONTEXT)(ULONG_PTR)NormalContext;
    if (!ApcContext)
    {
        Wow64ReportStub("Wow64ApcRoutine: missing context");
        return;
    }

    RtlCopyMemory(&LocalContext, ApcContext, sizeof(LocalContext));

    if ((LocalContext.Version != WOW64_APC_CONTEXT_VERSION) ||
        (LocalContext.Size < sizeof(WOW64_APC_CONTEXT)))
    {
        Wow64ReportStub("Wow64ApcRoutine: unsupported APC context");
        return;
    }

    if (!(LocalContext.Flags & WOW64_APC_CONTEXT_FLAG_HAS_USER_ROUTINE) ||
        (LocalContext.UserRoutine == 0))
    {
        Wow64ReportStub("Wow64ApcRoutine: no user routine");
        return;
    }

    if ((LocalContext.Flags & WOW64_APC_CONTEXT_FLAG_HAS_CPU_AREA) &&
        LocalContext.Wow64CpuArea != 0)
    {
        CpuArea = (PWOW64_CPU_AREA)(ULONG_PTR)LocalContext.Wow64CpuArea;
    }
    else
    {
        CpuArea = Wow64State.CpuArea;
    }

    if (!CpuArea)
    {
        Wow64ReportStub("Wow64ApcRoutine: no CPU area");
        return;
    }

    Status = Wow64CpuSetPendingApc(CpuArea, &LocalContext, SystemArgument1, SystemArgument2);
    if (!NT_SUCCESS(Status))
    {
        Wow64ReportStub("Wow64ApcRoutine: failed to queue APC");
        return;
    }

    Wow64ReportStub("Wow64ApcRoutine: pending APC queued");
}
