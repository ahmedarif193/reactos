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
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(CompatContext);

    if (!CpuArea || !NativeContext)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!Wow64EnsureCallbackTlsIndex())
    {
        return STATUS_NO_MEMORY;
    }

    (void)Wow64SetThreadContext(CpuArea);

    Status = STATUS_PENDING;

    {
        NTSTATUS ContinueStatus;

        ContinueStatus = NtContinue(NativeContext, FALSE);
        if (!NT_SUCCESS(ContinueStatus))
        {
            Status = ContinueStatus;
        }
    }

    return Status;
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

static WOW64_SYSCALL_ENTRY Wow64SyscallTable[] =
{
    /* Essential file I/O */
    { 0, 6, Wow64Thunk_NtOpenFile, "NtOpenFile" },
    { 0, 9, Wow64Thunk_NtReadFile, "NtReadFile" },
    { 0, 9, Wow64Thunk_NtWriteFile, "NtWriteFile" },
    { 0, 1, Wow64Thunk_NtClose, "NtClose" },

    /* Process/System queries */
    { 0, 5, Wow64Thunk_NtQueryInformationProcess, "NtQueryInformationProcess" },
    { 0, 4, Wow64Thunk_NtQuerySystemInformation, "NtQuerySystemInformation" },

    /* Synchronization */
    { 0, 5, Wow64Thunk_NtCreateEvent, "NtCreateEvent" },
    { 0, 3, Wow64Thunk_NtWaitForSingleObject, "NtWaitForSingleObject" },

    /* Memory management */
    { 0, 6, Wow64Thunk_NtAllocateVirtualMemory, "NtAllocateVirtualMemory" },
    { 0, 4, Wow64Thunk_NtFreeVirtualMemory, "NtFreeVirtualMemory" },

    /* Process management */
    { 0, 2, Wow64Thunk_NtTerminateProcess, "NtTerminateProcess" },
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
        if (Wow64SyscallTable[Index].ServiceNumber == ServiceNumber ||
            ServiceNumber < WOW64_SYSCALL_TABLE_SIZE)
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
