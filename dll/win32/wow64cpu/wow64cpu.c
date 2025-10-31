/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * PROGRAMMERS:     (c) 2025 Ahmed ARIF (arif.ing@outlook.com)
 */


#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <reactos/wow64apc.h>
#include <reactos/wow64cpu.h>
#include <reactos/wow64shared.h>
#include <ntstatus.h>
#include <ndk/ntndk.h>
#include <pseh/pseh2.h>
#include <string.h>
#include <strsafe.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#define WOW64_CPU_AREA_FLAG_COMPAT_CONTEXT   0x00000001
#define WOW64_CPU_AREA_FLAG_NATIVE_CONTEXT   0x00000002
#define WOW64_CPU_AREA_FLAG_PENDING_APC      0x00000004
#define WOW64_CPU_AREA_FLAG_APC_IN_PROGRESS  0x00000008
#define WOW64_CPU_AREA_FLAG_HOST_CONTEXT     0x00000010

#define WOW64CPU_MIN(a,b) (((a) < (b)) ? (a) : (b))

#ifndef WOW64_CPU_AREA_POINTER_TAG
#define WOW64_CPU_AREA_POINTER_TAG ((ULONG_PTR)0x1)
#define WOW64_CPU_AREA_ENCODE_POINTER(Area) \
    ((ULONG_PTR)((Area) ? ((ULONG_PTR)(Area) | WOW64_CPU_AREA_POINTER_TAG) : WOW64_CPU_AREA_POINTER_TAG))
#define WOW64_CPU_AREA_DECODE_POINTER(Value) \
    ((PWOW64_CPU_AREA)((ULONG_PTR)(Value) & ~(ULONG_PTR)WOW64_CPU_AREA_POINTER_TAG))
#define WOW64_CPU_AREA_IS_TAGGED(Value) \
    (((ULONG_PTR)(Value) & WOW64_CPU_AREA_POINTER_TAG) != 0)
#endif

#define WOW64_UCALLOUT_BUFFER_OFFSET   0x20
#define WOW64_UCALLOUT_LENGTH_OFFSET   0x28
#define WOW64_UCALLOUT_APINUM_OFFSET   0x2C

#define WOW64_ALIGNED_STACK_DOWN(Value, Alignment) \
    ((ULONG_PTR)((Value) & ~((Alignment) - 1)))

#ifndef WOW64_CONTEXT_i386
#define WOW64_CONTEXT_i386            0x00010000L
#define WOW64_CONTEXT_CONTROL         (WOW64_CONTEXT_i386 | 0x00000001L)
#define WOW64_CONTEXT_INTEGER         (WOW64_CONTEXT_i386 | 0x00000002L)
#define WOW64_CONTEXT_SEGMENTS        (WOW64_CONTEXT_i386 | 0x00000004L)
#define WOW64_CONTEXT_FULL            (WOW64_CONTEXT_CONTROL | WOW64_CONTEXT_INTEGER | WOW64_CONTEXT_SEGMENTS)
#endif

typedef NTSTATUS (NTAPI *PFN_NTSETINFORMATIONPROCESS)(HANDLE, ULONG, PVOID, ULONG);

typedef NTSTATUS (NTAPI *PFN_WOW64_KI_USER_CALLBACK_DISPATCHER)(
    _In_ ULONG ApiNumber,
    _In_reads_bytes_opt_(InputLength) PVOID InputBuffer,
    _In_ ULONG InputLength,
    _Outptr_result_maybenull_ PVOID *OutputBuffer,
    _Out_opt_ PULONG OutputLength);

typedef NTSTATUS (NTAPI *PFN_WOW64_KI_USER_APC_DISPATCHER)(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _Inout_ WOW64_CONTEXT *CompatContext,
    _Inout_ CONTEXT *NativeContext);

typedef NTSTATUS (NTAPI *PFN_WOW64_KI_USER_EXCEPTION_DISPATCHER)(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_ WOW64_CONTEXT *CompatContext,
    _Inout_ CONTEXT *NativeContext);

typedef NTSTATUS (NTAPI *PFN_WOW64_SET_THREAD_CONTEXT)(
    _In_ PVOID ThreadContext);

#define WOW64CPU_PROCESS_WOW64_INFORMATION 26

static NTSTATUS
Wow64cpuExecuteCompatApc(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _Inout_ WOW64_CONTEXT *CompatContext,
    _Inout_ CONTEXT *NativeContext);

NTSTATUS
NTAPI
Wow64cpuRunCallback(
    _In_ ULONG_PTR CallbackRoutine,
    _In_ ULONG_PTR Stack32,
    _Inout_ WOW64_CONTEXT *CompatContext,
    _Inout_ CONTEXT *NativeContext);

NTSTATUS
NTAPI
Wow64cpuDispatchWow64Exception(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_ WOW64_CONTEXT *CompatContext,
    _Inout_ CONTEXT *NativeContext);

NTSTATUS
NTAPI
Wow64cpuApcTrampoline(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _Inout_ WOW64_CONTEXT *CompatContext,
    _Inout_ CONTEXT *HostContext);

static PWOW64_CPU_AREA
Wow64cpuQueryThreadArea(VOID);

static NTSTATUS
Wow64cpuStoreContext(
    _Inout_ PWOW64_CPU_AREA Area,
    _In_reads_bytes_(Length) const VOID *Context,
    _In_ ULONG Length,
    _In_ BOOLEAN Native);

static HMODULE
Wow64cpuGetWow64Module(VOID)
{
    static HMODULE Wow64Module = NULL;

    if (!Wow64Module)
    {
        Wow64Module = GetModuleHandleW(L"wow64.dll");
    }

    return Wow64Module;
}

static PFN_WOW64_SET_THREAD_CONTEXT
Wow64cpuGetWow64SetThreadContext(VOID)
{
    static PFN_WOW64_SET_THREAD_CONTEXT SetThreadContext = NULL;
    static BOOL Resolved = FALSE;

    if (!Resolved)
    {
        HMODULE Wow64Module = Wow64cpuGetWow64Module();
        if (Wow64Module)
        {
            SetThreadContext =
                (PFN_WOW64_SET_THREAD_CONTEXT)GetProcAddress(Wow64Module,
                                                             "Wow64SetThreadContext");
        }
        Resolved = TRUE;
    }

    return SetThreadContext;
}

static PFN_WOW64_KI_USER_CALLBACK_DISPATCHER
Wow64cpuGetCallbackDispatcher(VOID)
{
    static PFN_WOW64_KI_USER_CALLBACK_DISPATCHER Dispatcher = NULL;
    static BOOL Resolved = FALSE;

    if (!Resolved)
    {
        HMODULE Wow64Module = Wow64cpuGetWow64Module();
        if (Wow64Module)
        {
            Dispatcher =
                (PFN_WOW64_KI_USER_CALLBACK_DISPATCHER)GetProcAddress(Wow64Module,
                                                                      "Wow64KiUserCallbackDispatcher");
        }
        Resolved = TRUE;
    }

    return Dispatcher;
}

static PFN_WOW64_KI_USER_APC_DISPATCHER
Wow64cpuGetApcDispatcher(VOID)
{
    static PFN_WOW64_KI_USER_APC_DISPATCHER Dispatcher = NULL;
    static BOOL Resolved = FALSE;

    if (!Resolved)
    {
        HMODULE Wow64Module = Wow64cpuGetWow64Module();
        if (Wow64Module)
        {
            Dispatcher =
                (PFN_WOW64_KI_USER_APC_DISPATCHER)GetProcAddress(Wow64Module,
                                                                 "Wow64KiUserApcDispatcher");
        }
        Resolved = TRUE;
    }

    return Dispatcher;
}

static PFN_WOW64_KI_USER_EXCEPTION_DISPATCHER
Wow64cpuGetExceptionDispatcher(VOID)
{
    static PFN_WOW64_KI_USER_EXCEPTION_DISPATCHER Dispatcher = NULL;
    static BOOL Resolved = FALSE;

    if (!Resolved)
    {
        HMODULE Wow64Module = Wow64cpuGetWow64Module();
        if (Wow64Module)
        {
            Dispatcher =
                (PFN_WOW64_KI_USER_EXCEPTION_DISPATCHER)GetProcAddress(Wow64Module,
                                                                       "Wow64KiUserExceptionDispatcher");
        }
        Resolved = TRUE;
    }

    return Dispatcher;
}

static ULONG
Wow64cpuLookupSharedDispatcher(
    _In_ WOW64_SHARED_INFORMATION_INDEX Index)
{
    PKUSER_SHARED_DATA SharedData;

    if (Index <= Wow64SharedInformationInvalid || Index >= WOW64_SHARED_INFORMATION_COUNT)
    {
        return 0;
    }

    SharedData = (PKUSER_SHARED_DATA)USER_SHARED_DATA;
    if (!SharedData)
    {
        return 0;
    }

    return SharedData->Wow64SharedInformation[Index];
}

NTSTATUS
NTAPI
Wow64cpuRunCallback(
    _In_ ULONG_PTR CallbackRoutine,
    _In_ ULONG_PTR Stack32,
    _Inout_ WOW64_CONTEXT *CompatContext,
    _Inout_ CONTEXT *NativeContext)
{
    PWOW64_CPU_AREA CpuArea;
    PFN_WOW64_SET_THREAD_CONTEXT SetThreadContext;
    PFN_WOW64_KI_USER_CALLBACK_DISPATCHER CallbackDispatcher;
    PVOID OutputBuffer = NULL;
    ULONG OutputLength = 0;
    PUCHAR CalloutFrame;
    PVOID CallBuffer;
    ULONG CallLength;
    ULONG ApiNumber;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(CallbackRoutine);

    if (!CompatContext || !NativeContext)
    {
        return STATUS_INVALID_PARAMETER;
    }

    CpuArea = Wow64cpuQueryThreadArea();
    if (!CpuArea)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    Status = Wow64cpuStoreContext(CpuArea,
                                  CompatContext,
                                  sizeof(*CompatContext),
                                  FALSE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = Wow64cpuStoreContext(CpuArea,
                                  NativeContext,
                                  sizeof(*NativeContext),
                                  TRUE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    CalloutFrame = (PUCHAR)(ULONG_PTR)Stack32;
    if (!CalloutFrame)
    {
        return STATUS_INVALID_PARAMETER;
    }

    CallBuffer = (PVOID)(ULONG_PTR)(*(ULONG_PTR UNALIGNED *)(CalloutFrame + WOW64_UCALLOUT_BUFFER_OFFSET));
    CallLength = *(ULONG UNALIGNED *)(CalloutFrame + WOW64_UCALLOUT_LENGTH_OFFSET);
    ApiNumber = *(ULONG UNALIGNED *)(CalloutFrame + WOW64_UCALLOUT_APINUM_OFFSET);

    CallbackDispatcher = Wow64cpuGetCallbackDispatcher();
    if (!CallbackDispatcher)
    {
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    SetThreadContext = Wow64cpuGetWow64SetThreadContext();
    if (SetThreadContext)
    {
        (void)SetThreadContext(CpuArea);
    }

    Status = CallbackDispatcher(ApiNumber,
                                CallBuffer,
                                CallLength,
                                &OutputBuffer,
                                &OutputLength);

    if (Status == STATUS_PENDING)
    {
        return Status;
    }

    if (NT_SUCCESS(Status))
    {
        /* Unexpected success code other than STATUS_PENDING. */
        Status = STATUS_UNSUCCESSFUL;
    }

    return Status;
}

NTSTATUS
NTAPI
Wow64cpuDispatchWow64Exception(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_ WOW64_CONTEXT *CompatContext,
    _Inout_ CONTEXT *NativeContext)
{
    PFN_WOW64_SET_THREAD_CONTEXT SetThreadContext;
    PFN_WOW64_KI_USER_EXCEPTION_DISPATCHER ExceptionDispatcher;
    NTSTATUS Status;

    if (!CpuArea || !ExceptionRecord || !CompatContext || !NativeContext)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = Wow64cpuStoreContext(CpuArea,
                                  CompatContext,
                                  sizeof(*CompatContext),
                                  FALSE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = Wow64cpuStoreContext(CpuArea,
                                  NativeContext,
                                  sizeof(*NativeContext),
                                  TRUE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    ExceptionDispatcher = Wow64cpuGetExceptionDispatcher();
    if (!ExceptionDispatcher)
    {
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    SetThreadContext = Wow64cpuGetWow64SetThreadContext();
    if (SetThreadContext)
    {
        (void)SetThreadContext(CpuArea);
    }

    Status = ExceptionDispatcher(CpuArea,
                                  ExceptionRecord,
                                  CompatContext,
                                  NativeContext);

    if (Status == STATUS_PENDING)
    {
        return Status;
    }

    if (NT_SUCCESS(Status))
    {
        Status = STATUS_UNSUCCESSFUL;
    }

    return Status;
}

static NTSTATUS
Wow64cpuStoreHostContext(
    _Inout_ PWOW64_CPU_AREA Area,
    _In_reads_bytes_(sizeof(CONTEXT)) const CONTEXT *Context);

static NTSTATUS
Wow64cpuCaptureHostContext(
    _Inout_ PWOW64_CPU_AREA Area,
    _Out_writes_bytes_(sizeof(CONTEXT)) CONTEXT *Context);

static PFN_NTSETINFORMATIONPROCESS Wow64cpuGetNtSetInformationProcess(VOID)
{
    static PFN_NTSETINFORMATIONPROCESS NtSetInformationProcess = NULL;
    static BOOL Resolved = FALSE;

    if (!Resolved)
    {
        HMODULE NtDll = GetModuleHandleW(L"ntdll.dll");
        if (NtDll)
        {
            NtSetInformationProcess =
                (PFN_NTSETINFORMATIONPROCESS)GetProcAddress(NtDll, "NtSetInformationProcess");
        }
        Resolved = TRUE;
    }

    return NtSetInformationProcess;
}

static VOID
Wow64cpuPublishCpuArea(
    _In_opt_ PWOW64_CPU_AREA Area)
{
    PFN_NTSETINFORMATIONPROCESS NtSetInformationProcess;
    ULONG_PTR Value;

    NtSetInformationProcess = Wow64cpuGetNtSetInformationProcess();
    if (!NtSetInformationProcess)
    {
        return;
    }

    Value = WOW64_CPU_AREA_ENCODE_POINTER(Area);
    NtSetInformationProcess(GetCurrentProcess(),
                            WOW64CPU_PROCESS_WOW64_INFORMATION,
                            &Value,
                            sizeof(Value));
}

static volatile LONG Wow64cpuTlsIndex = (LONG)TLS_OUT_OF_INDEXES;

static BOOL
Wow64cpuEnsureTlsIndex(VOID)
{
    LONG CurrentIndex;

    CurrentIndex = Wow64cpuTlsIndex;
    if (CurrentIndex != (LONG)TLS_OUT_OF_INDEXES)
    {
        return TRUE;
    }

    CurrentIndex = (LONG)TlsAlloc();
    if (CurrentIndex == (LONG)TLS_OUT_OF_INDEXES)
    {
        return FALSE;
    }

    if (InterlockedCompareExchange(&Wow64cpuTlsIndex,
                                   CurrentIndex,
                                   (LONG)TLS_OUT_OF_INDEXES) != (LONG)TLS_OUT_OF_INDEXES)
    {
        TlsFree((DWORD)CurrentIndex);
    }

    return Wow64cpuTlsIndex != (LONG)TLS_OUT_OF_INDEXES;
}

static NTSTATUS
Wow64cpuAssignThreadArea(
    _In_opt_ PWOW64_CPU_AREA CpuArea)
{
    if (!Wow64cpuEnsureTlsIndex())
    {
        return STATUS_NO_MEMORY;
    }

    if (!TlsSetValue((DWORD)Wow64cpuTlsIndex, CpuArea))
    {
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

static PWOW64_CPU_AREA
Wow64cpuQueryThreadArea(VOID)
{
    if (Wow64cpuTlsIndex == (LONG)TLS_OUT_OF_INDEXES)
    {
        return NULL;
    }

    return (PWOW64_CPU_AREA)TlsGetValue((DWORD)Wow64cpuTlsIndex);
}

static VOID
Wow64cpuClearThreadArea(VOID)
{
    if (Wow64cpuTlsIndex != (LONG)TLS_OUT_OF_INDEXES)
    {
        TlsSetValue((DWORD)Wow64cpuTlsIndex, NULL);
    }
}

static VOID
Wow64cpuReleaseContextBuffer(
    _Inout_ PVOID *Buffer,
    _Inout_ PULONG Length,
    _Inout_ PULONG Flags,
    _In_ ULONG FlagBit)
{
    if (*Buffer)
    {
        HeapFree(GetProcessHeap(), 0, *Buffer);
        *Buffer = NULL;
    }

    if (Length)
    {
        *Length = 0;
    }

    if (Flags)
    {
        *Flags &= ~FlagBit;
    }
}

typedef enum _WOW64_CPU_CONTEXT_SLOT
{
    Wow64CpuContextCompat = 0,
    Wow64CpuContextNative,
    Wow64CpuContextHost
} WOW64_CPU_CONTEXT_SLOT;

typedef struct _WOW64_CPU_CONTEXT_ENTRY
{
    PVOID *Buffer;
    PULONG Length;
    ULONG FlagBit;
} WOW64_CPU_CONTEXT_ENTRY, *PWOW64_CPU_CONTEXT_ENTRY;

static BOOLEAN
Wow64cpuResolveContextSlot(
    _Inout_ PWOW64_CPU_AREA Area,
    _In_ WOW64_CPU_CONTEXT_SLOT Slot,
    _Out_ PWOW64_CPU_CONTEXT_ENTRY Entry)
{
    if (!Area || !Entry)
    {
        return FALSE;
    }

    switch (Slot)
    {
        case Wow64CpuContextCompat:
            Entry->Buffer = &Area->CompatContext;
            Entry->Length = &Area->CompatContextLength;
            Entry->FlagBit = WOW64_CPU_AREA_FLAG_COMPAT_CONTEXT;
            return TRUE;

        case Wow64CpuContextNative:
            Entry->Buffer = &Area->NativeContext;
            Entry->Length = &Area->NativeContextLength;
            Entry->FlagBit = WOW64_CPU_AREA_FLAG_NATIVE_CONTEXT;
            return TRUE;

        case Wow64CpuContextHost:
            Entry->Buffer = &Area->HostContext;
            Entry->Length = &Area->HostContextLength;
            Entry->FlagBit = WOW64_CPU_AREA_FLAG_HOST_CONTEXT;
            return TRUE;

        default:
            return FALSE;
    }
}

static NTSTATUS
Wow64cpuStoreTypedContext(
    _Inout_ PWOW64_CPU_AREA Area,
    _In_ WOW64_CPU_CONTEXT_SLOT Slot,
    _In_reads_bytes_(Length) const VOID *Context,
    _In_ ULONG Length)
{
    WOW64_CPU_CONTEXT_ENTRY Entry;

    if (!Context || Length == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!Wow64cpuResolveContextSlot(Area, Slot, &Entry))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (*(Entry.Buffer) && *(Entry.Length) != Length)
    {
        Wow64cpuReleaseContextBuffer(Entry.Buffer,
                                     Entry.Length,
                                     &Area->Flags,
                                     Entry.FlagBit);
    }

    if (!*(Entry.Buffer))
    {
        *(Entry.Buffer) = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Length);
        if (!*(Entry.Buffer))
        {
            return STATUS_NO_MEMORY;
        }
    }

    memcpy(*(Entry.Buffer), Context, Length);
    *(Entry.Length) = Length;
    Area->Flags |= Entry.FlagBit;

    return STATUS_SUCCESS;
}

static NTSTATUS
Wow64cpuCaptureTypedContext(
    _Inout_ PWOW64_CPU_AREA Area,
    _In_ WOW64_CPU_CONTEXT_SLOT Slot,
    _Out_writes_bytes_(Length) PVOID Context,
    _In_ ULONG Length)
{
    WOW64_CPU_CONTEXT_ENTRY Entry;

    if (!Context || Length == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!Wow64cpuResolveContextSlot(Area, Slot, &Entry))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!(Area->Flags & Entry.FlagBit) ||
        !*(Entry.Buffer) ||
        *(Entry.Length) == 0)
    {
        return STATUS_NOT_FOUND;
    }

    memcpy(Context, *(Entry.Buffer), WOW64CPU_MIN(Length, *(Entry.Length)));
    return STATUS_SUCCESS;
}

static NTSTATUS
Wow64cpuStoreContext(
    _Inout_ PWOW64_CPU_AREA Area,
    _In_reads_bytes_(Length) const VOID *Context,
    _In_ ULONG Length,
    _In_ BOOLEAN Native);

static NTSTATUS
Wow64cpuCaptureContext(
    _Inout_ PWOW64_CPU_AREA Area,
    _Out_writes_bytes_(Length) PVOID Context,
    _In_ ULONG Length,
    _In_ BOOLEAN Native);

static VOID
Wow64cpuResetArea(
    _Inout_ WOW64_CPU_AREA *Area)
{
    if (!Area)
    {
        return;
    }

    Wow64cpuReleaseContextBuffer(&Area->CompatContext,
                                 &Area->CompatContextLength,
                                 &Area->Flags,
                                 WOW64_CPU_AREA_FLAG_COMPAT_CONTEXT);

    Wow64cpuReleaseContextBuffer(&Area->NativeContext,
                                 &Area->NativeContextLength,
                                 &Area->Flags,
                                 WOW64_CPU_AREA_FLAG_NATIVE_CONTEXT);

    Wow64cpuReleaseContextBuffer(&Area->HostContext,
                                 &Area->HostContextLength,
                                 &Area->Flags,
                                 WOW64_CPU_AREA_FLAG_HOST_CONTEXT);

    Area->PendingUserContext = 0;
    Area->PendingUserRoutine = 0;
    Area->PendingSystemArgument1 = 0;
    Area->PendingSystemArgument2 = 0;
    Area->Flags &= ~(WOW64_CPU_AREA_FLAG_PENDING_APC |
                     WOW64_CPU_AREA_FLAG_APC_IN_PROGRESS);
}

static VOID
Wow64cpuDebugTrace(
    _In_z_ LPCSTR Message)
{
#if DBG
    OutputDebugStringA("wow64cpu: ");
    OutputDebugStringA(Message);
#endif
    UNREFERENCED_PARAMETER(Message);
}

VOID
NTAPI
Wow64cpuDebugTraceTransition(
    _In_ ULONG TransitionType,
    _In_ ULONG_PTR Parameter)
{
#if DBG
    CHAR Buffer[256];
    const CHAR *TypeString;

    switch (TransitionType)
    {
        case 1:
            TypeString = "Callback Entry";
            break;
        case 2:
            TypeString = "Callback Return";
            break;
        case 3:
            TypeString = "Exception Dispatch Entry";
            break;
        case 4:
            TypeString = "Exception Context Converted";
            break;
        default:
            TypeString = "Unknown Transition";
            break;
    }
    if (SUCCEEDED(StringCchPrintfA(Buffer,
                                   ARRAYSIZE(Buffer),
                                   "wow64cpu: CPU Transition - Type: %s, Param: 0x%p\r\n",
                                   TypeString,
                                   (PVOID)Parameter)))
    {
        OutputDebugStringA(Buffer);
    }
#else
    UNREFERENCED_PARAMETER(TransitionType);
    UNREFERENCED_PARAMETER(Parameter);
#endif
}

PWOW64_CPU_AREA
NTAPI
Wow64cpuGetThreadArea(VOID)
{
    return Wow64cpuQueryThreadArea();
}

static NTSTATUS
Wow64cpuSimulatePendingApc(
    _Inout_ PWOW64_CPU_AREA Area)
{
    NTSTATUS Status;
    CONTEXT HostContext;
    WOW64_CONTEXT CompatContext;

    if (!Area)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!(Area->Flags & WOW64_CPU_AREA_FLAG_PENDING_APC))
    {
        return STATUS_NOT_FOUND;
    }

    Status = Wow64cpuCaptureHostContext(Area, &HostContext);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlZeroMemory(&CompatContext, sizeof(CompatContext));
    Status = Wow64cpuCaptureContext(Area,
                                    &CompatContext,
                                    sizeof(CompatContext),
                                    FALSE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return Wow64cpuApcTrampoline(Area, &CompatContext, &HostContext);
}

static NTSTATUS
Wow64cpuStoreContext(
    _Inout_ PWOW64_CPU_AREA Area,
    _In_reads_bytes_(Length) const VOID *Context,
    _In_ ULONG Length,
    _In_ BOOLEAN Native)
{
    WOW64_CPU_CONTEXT_SLOT Slot;

    if (!Area)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Slot = Native ? Wow64CpuContextNative : Wow64CpuContextCompat;
    return Wow64cpuStoreTypedContext(Area, Slot, Context, Length);
}

static NTSTATUS
Wow64cpuCaptureContext(
    _Inout_ PWOW64_CPU_AREA Area,
    _Out_writes_bytes_(Length) PVOID Context,
    _In_ ULONG Length,
    _In_ BOOLEAN Native)
{
    WOW64_CPU_CONTEXT_SLOT Slot;

    if (!Area)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Slot = Native ? Wow64CpuContextNative : Wow64CpuContextCompat;
    return Wow64cpuCaptureTypedContext(Area, Slot, Context, Length);
}

static NTSTATUS
Wow64cpuStoreHostContext(
    _Inout_ PWOW64_CPU_AREA Area,
    _In_reads_bytes_(sizeof(CONTEXT)) const CONTEXT *Context)
{
    return Wow64cpuStoreTypedContext(Area,
                                     Wow64CpuContextHost,
                                     Context,
                                     sizeof(*Context));
}

static NTSTATUS
Wow64cpuCaptureHostContext(
    _Inout_ PWOW64_CPU_AREA Area,
    _Out_writes_bytes_(sizeof(CONTEXT)) CONTEXT *Context)
{
    return Wow64cpuCaptureTypedContext(Area,
                                       Wow64CpuContextHost,
                                       Context,
                                       sizeof(*Context));
}

NTSTATUS
NTAPI
Wow64cpuApcTrampoline(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _Inout_ WOW64_CONTEXT *CompatContext,
    _Inout_ CONTEXT *HostContext)
{
    CONTEXT NativeContext;
    NTSTATUS Status;

    if (!CpuArea || !CompatContext || !HostContext)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = Wow64TransitionToNative(CpuArea, CompatContext, &NativeContext);
    if (!NT_SUCCESS(Status))
    {
        Wow64cpuDebugTrace("Wow64cpuApcTrampoline: native conversion failed");
        return Status;
    }

    Status = Wow64cpuStoreContext(CpuArea,
                                  CompatContext,
                                  sizeof(*CompatContext),
                                  FALSE);
    if (!NT_SUCCESS(Status))
    {
        Wow64cpuDebugTrace("Wow64cpuApcTrampoline: failed to store compat context");
        return Status;
    }

    Status = Wow64cpuStoreContext(CpuArea,
                                  &NativeContext,
                                  sizeof(NativeContext),
                                  TRUE);
    if (!NT_SUCCESS(Status))
    {
        Wow64cpuDebugTrace("Wow64cpuApcTrampoline: failed to store native context");
        return Status;
    }

    Status = Wow64cpuStoreHostContext(CpuArea, HostContext);
    if (!NT_SUCCESS(Status))
    {
        Wow64cpuDebugTrace("Wow64cpuApcTrampoline: failed to stash host context");
        return Status;
    }

    Status = Wow64cpuExecuteCompatApc(CpuArea, CompatContext, &NativeContext);
    if (Status == STATUS_NOT_IMPLEMENTED)
    {
        Wow64cpuDebugTrace("Wow64cpuApcTrampoline: compat executor not implemented");
    }

    return Status;
}

static NTSTATUS
Wow64cpuExecuteCompatApc(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _Inout_ WOW64_CONTEXT *CompatContext,
    _Inout_ CONTEXT *NativeContext)
{
    PFN_WOW64_SET_THREAD_CONTEXT SetThreadContext;
    PFN_WOW64_KI_USER_APC_DISPATCHER ApcDispatcher;
    NTSTATUS Status;

    if (!CpuArea || !CompatContext || !NativeContext)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!(CpuArea->Flags & WOW64_CPU_AREA_FLAG_APC_IN_PROGRESS))
    {
        Wow64cpuDebugTrace("Wow64cpuExecuteCompatApc: APC flag missing");
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (!(CpuArea->Flags & WOW64_CPU_AREA_FLAG_PENDING_APC))
    {
        Wow64cpuDebugTrace("Wow64cpuExecuteCompatApc: no pending APC");
        return STATUS_NOT_FOUND;
    }

    if (!(CompatContext->ContextFlags & WOW64_CONTEXT_FULL))
    {
        Wow64cpuDebugTrace("Wow64cpuExecuteCompatApc: compat context incomplete");
        return STATUS_INVALID_PARAMETER;
    }

    if (!(NativeContext->ContextFlags & CONTEXT_CONTROL) ||
        !(NativeContext->ContextFlags & CONTEXT_INTEGER))
    {
        Wow64cpuDebugTrace("Wow64cpuExecuteCompatApc: native context incomplete");
        return STATUS_INVALID_PARAMETER;
    }

    Status = Wow64cpuStoreContext(CpuArea,
                                  CompatContext,
                                  sizeof(*CompatContext),
                                  FALSE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = Wow64cpuStoreContext(CpuArea,
                                  NativeContext,
                                  sizeof(*NativeContext),
                                  TRUE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    ApcDispatcher = Wow64cpuGetApcDispatcher();
    if (!ApcDispatcher)
    {
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    SetThreadContext = Wow64cpuGetWow64SetThreadContext();
    if (SetThreadContext)
    {
        (void)SetThreadContext(CpuArea);
    }

    Status = ApcDispatcher(CpuArea, CompatContext, NativeContext);
    if (Status == STATUS_PENDING)
    {
        return Status;
    }

    if (NT_SUCCESS(Status))
    {
        /* Unexpected success code other than STATUS_PENDING. */
        Status = STATUS_UNSUCCESSFUL;
    }

    return Status;
}

BOOL
WINAPI
DllMain(
    HINSTANCE Instance,
    DWORD Reason,
    LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Reserved);
    UNREFERENCED_PARAMETER(Reason);

    return TRUE;
}

NTSTATUS
NTAPI
CpuProcessInit(
    PWOW64_CPU_AREA *CpuArea,
    PVOID Reserved)
{
    WOW64_CPU_AREA *Area;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Reserved);

    if (!CpuArea)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Area = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Area));
    if (!Area)
    {
        *CpuArea = NULL;
        return STATUS_NO_MEMORY;
    }

    Area->Size = sizeof(*Area);
    Area->Flags = 0;
    Area->CompatContext = NULL;
    Area->CompatContextLength = 0;
    Area->NativeContext = NULL;
    Area->NativeContextLength = 0;
    Area->HostContext = NULL;
    Area->HostContextLength = 0;

    Status = Wow64cpuAssignThreadArea(Area);
    if (!NT_SUCCESS(Status))
    {
        Wow64cpuResetArea(Area);
        HeapFree(GetProcessHeap(), 0, Area);
        *CpuArea = NULL;
        return Status;
    }

    *CpuArea = Area;

    Wow64cpuDebugTrace("CpuProcessInit");
    Wow64cpuPublishCpuArea(Area);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CpuThreadInit(
    PWOW64_CPU_AREA CpuArea,
    PVOID ThreadContext)
{
    UNREFERENCED_PARAMETER(ThreadContext);

    if (!CpuArea)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!NT_SUCCESS(Wow64cpuAssignThreadArea(CpuArea)))
    {
        return STATUS_UNSUCCESSFUL;
    }

    Wow64cpuDebugTrace("CpuThreadInit");

    return STATUS_SUCCESS;
}

VOID
NTAPI
CpuThreadTerm(
    PWOW64_CPU_AREA CpuArea)
{
    Wow64cpuClearThreadArea();

    if (!CpuArea)
    {
        return;
    }

    Wow64cpuResetArea(CpuArea);
    Wow64cpuDebugTrace("CpuThreadTerm");
}

NTSTATUS
NTAPI
CpuNotify(
    WOW64_CPU_NOTIFY_TYPE NotifyType,
    PWOW64_CPU_AREA CpuArea,
    PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);

    switch (NotifyType)
    {
        case Wow64CpuNotifyInitialize:
            Wow64cpuDebugTrace("CpuNotify: initialize");
            return STATUS_SUCCESS;

        case Wow64CpuNotifyThreadAttach:
            if (CpuArea)
            {
                Wow64cpuResetArea(CpuArea);
                if (!NT_SUCCESS(Wow64cpuAssignThreadArea(CpuArea)))
                {
                    return STATUS_UNSUCCESSFUL;
                }
            }
            else
            {
                Wow64cpuClearThreadArea();
            }
            Wow64cpuDebugTrace("CpuNotify: thread attach");
            return STATUS_SUCCESS;

        case Wow64CpuNotifyThreadDetach:
            if (CpuArea)
            {
                Wow64cpuResetArea(CpuArea);
            }
            Wow64cpuClearThreadArea();
            Wow64cpuDebugTrace("CpuNotify: thread detach");
            return STATUS_SUCCESS;

        case Wow64CpuNotifyShutdown:
            if (CpuArea)
            {
                Wow64cpuResetArea(CpuArea);
                HeapFree(GetProcessHeap(), 0, CpuArea);
            }
            Wow64cpuClearThreadArea();
            Wow64cpuPublishCpuArea(NULL);
            Wow64cpuDebugTrace("CpuNotify: shutdown");
            return STATUS_SUCCESS;

        default:
            Wow64cpuDebugTrace("CpuNotify: unsupported notification");
            return STATUS_NOT_IMPLEMENTED;
    }
}

VOID
NTAPI
CpupReturnFromSimulatedCode(VOID)
{
    PWOW64_CPU_AREA CpuArea;
    CONTEXT NativeContext;
    WOW64_CONTEXT CompatContext;
    NTSTATUS Status;

    CpuArea = Wow64cpuQueryThreadArea();
    if (!CpuArea)
    {
        Wow64cpuDebugTrace("CpupReturnFromSimulatedCode: no CPU area");
        return;
    }

    if (CpuArea->Flags & WOW64_CPU_AREA_FLAG_APC_IN_PROGRESS)
    {
        Wow64cpuReleaseContextBuffer(&CpuArea->HostContext,
                                     &CpuArea->HostContextLength,
                                     &CpuArea->Flags,
                                     WOW64_CPU_AREA_FLAG_HOST_CONTEXT);
        CpuArea->Flags &= ~WOW64_CPU_AREA_FLAG_APC_IN_PROGRESS;
    }

    RtlCaptureContext(&NativeContext);
    Status = Wow64TransitionToCompat(CpuArea, &NativeContext, &CompatContext);
    if (!NT_SUCCESS(Status))
    {
        Wow64cpuDebugTrace("CpupReturnFromSimulatedCode: context sync failed");
    }
}

NTSTATUS
NTAPI
Wow64CpuGetContext(
    PWOW64_CPU_AREA CpuArea,
    PVOID Context,
    ULONG ContextLength)
{
    NTSTATUS Status;
    BOOLEAN Native;

    if (ContextLength >= sizeof(CONTEXT))
    {
        Native = TRUE;
    }
    else
    {
        Native = FALSE;
    }

    Status = Wow64cpuCaptureContext(CpuArea, Context, ContextLength, Native);
    if (!NT_SUCCESS(Status) && Native)
    {
        /* Retry as compatibility context if native lookup failed. */
        Status = Wow64cpuCaptureContext(CpuArea, Context, ContextLength, FALSE);
    }

    return Status;
}

NTSTATUS
NTAPI
Wow64CpuSetContext(
    PWOW64_CPU_AREA CpuArea,
    const VOID *Context,
    ULONG ContextLength)
{
    NTSTATUS Status;
    BOOLEAN Native;

    if (ContextLength >= sizeof(CONTEXT))
    {
        Native = TRUE;
    }
    else
    {
        Native = FALSE;
    }

    Status = Wow64cpuStoreContext(CpuArea, Context, ContextLength, Native);
    if (!NT_SUCCESS(Status) && Native)
    {
        /* Retry storing as compatibility context if native write failed. */
        Status = Wow64cpuStoreContext(CpuArea, Context, ContextLength, FALSE);
    }

    return Status;
}

NTSTATUS
NTAPI
Wow64CpuSetPendingApc(
    PWOW64_CPU_AREA CpuArea,
    const WOW64_APC_CONTEXT *ApcContext,
    ULONG_PTR SystemArgument1,
    ULONG_PTR SystemArgument2)
{
    if (!CpuArea || !ApcContext)
    {
        return STATUS_INVALID_PARAMETER;
    }

    CpuArea->PendingUserContext = ApcContext->UserContext;
    CpuArea->PendingUserRoutine = ApcContext->UserRoutine;
    CpuArea->PendingSystemArgument1 = SystemArgument1;
    CpuArea->PendingSystemArgument2 = SystemArgument2;
    CpuArea->Flags |= WOW64_CPU_AREA_FLAG_PENDING_APC;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
Wow64CpuDispatchPendingApc(
    PWOW64_CPU_AREA CpuArea)
{
    NTSTATUS Status;
    CONTEXT HostContext;

    if (!CpuArea)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!(CpuArea->Flags & WOW64_CPU_AREA_FLAG_PENDING_APC))
    {
        return STATUS_NOT_FOUND;
    }

    if (CpuArea->Flags & WOW64_CPU_AREA_FLAG_APC_IN_PROGRESS)
    {
        return STATUS_ALREADY_COMMITTED;
    }

    CpuArea->Flags |= WOW64_CPU_AREA_FLAG_APC_IN_PROGRESS;

    RtlCaptureContext(&HostContext);
    Status = Wow64cpuStoreHostContext(CpuArea, &HostContext);
    if (!NT_SUCCESS(Status))
    {
        CpuArea->Flags &= ~WOW64_CPU_AREA_FLAG_APC_IN_PROGRESS;
        return Status;
    }

    Status = Wow64cpuSimulatePendingApc(CpuArea);
    if (!NT_SUCCESS(Status) && Status != STATUS_PENDING)
    {
        CpuArea->Flags &= ~WOW64_CPU_AREA_FLAG_APC_IN_PROGRESS;
    }

    return Status;
}

NTSTATUS
NTAPI
Wow64CpuPrepareCallback(
    PWOW64_CPU_AREA CpuArea,
    WOW64_CONTEXT *CompatContext,
    ULONG ApiNumber,
    _In_reads_bytes_opt_(InputLength) const VOID *InputBuffer,
    ULONG InputLength)
{
    ULONG Dispatcher;
    ULONG ReturnAddress;
    ULONG_PTR BufferPointer;
    ULONG_PTR OldEsp;
    ULONG_PTR NewEsp;
    PULONG Stack32;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!CpuArea || !CompatContext)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Dispatcher = Wow64cpuLookupSharedDispatcher(Wow64SharedInformationUserCallbackDispatcher32);
    if (Dispatcher == 0)
    {
        return STATUS_NOT_SUPPORTED;
    }

    ReturnAddress = Wow64cpuLookupSharedDispatcher(Wow64SharedInformationUserApcReturn32);
    if (ReturnAddress == 0)
    {
        ReturnAddress = CompatContext->Eip;
    }

    BufferPointer = (ULONG_PTR)InputBuffer;
    if (BufferPointer > MAXULONG)
    {
        return STATUS_INVALID_PARAMETER;
    }

    OldEsp = CompatContext->Esp;
    if (OldEsp <= (sizeof(ULONG) * 4))
    {
        return STATUS_STACK_OVERFLOW;
    }

    NewEsp = WOW64_ALIGNED_STACK_DOWN(OldEsp - (sizeof(ULONG) * 4), 4);
    if (NewEsp == 0)
    {
        return STATUS_STACK_OVERFLOW;
    }

    Stack32 = (PULONG)(ULONG_PTR)NewEsp;

    {
        NTSTATUS WriteStatus = STATUS_SUCCESS;

        _SEH2_TRY
        {
            Stack32[0] = ReturnAddress;
            Stack32[1] = ApiNumber;
            Stack32[2] = (ULONG)BufferPointer;
            Stack32[3] = InputLength;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            WriteStatus = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(WriteStatus))
        {
            return WriteStatus;
        }
    }

    CompatContext->Esp = (ULONG)NewEsp;
    CompatContext->Eip = Dispatcher;
    CompatContext->ContextFlags |= WOW64_CONTEXT_FULL;

    CompatContext->EFlags |= 0x00000200; /* Ensure IF remains set */

    if (CompatContext->SegCs == 0)
    {
        CompatContext->SegCs = 0x23;
    }
    if (CompatContext->SegSs == 0)
    {
        CompatContext->SegSs = 0x1B;
    }
    if (CompatContext->SegDs == 0)
    {
        CompatContext->SegDs = 0x23;
    }
    if (CompatContext->SegEs == 0)
    {
        CompatContext->SegEs = 0x23;
    }
    if (CompatContext->SegFs == 0)
    {
        CompatContext->SegFs = 0;
    }
    if (CompatContext->SegGs == 0)
    {
        CompatContext->SegGs = 0;
    }

    Status = Wow64cpuStoreContext(CpuArea,
                                  CompatContext,
                                  sizeof(*CompatContext),
                                  FALSE);

    return Status;
}

NTSTATUS
NTAPI
Wow64CpuTakePendingApc(
    PWOW64_CPU_AREA CpuArea,
    PWOW64_PENDING_APC PendingApc)
{
    ULONG BufferSize;

    if (!CpuArea || !PendingApc)
    {
        return STATUS_INVALID_PARAMETER;
    }

    BufferSize = PendingApc->Size;
    if (BufferSize < sizeof(*PendingApc))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (!(CpuArea->Flags & WOW64_CPU_AREA_FLAG_PENDING_APC))
    {
        PendingApc->Version = WOW64_PENDING_APC_VERSION;
        PendingApc->Size = sizeof(*PendingApc);
        PendingApc->Flags = 0;
        PendingApc->Reserved = 0;
        PendingApc->UserContext = 0;
        PendingApc->UserRoutine = 0;
        PendingApc->SystemArgument1 = 0;
        PendingApc->SystemArgument2 = 0;
        return STATUS_NOT_FOUND;
    }

    PendingApc->Version = WOW64_PENDING_APC_VERSION;
    PendingApc->Size = sizeof(*PendingApc);
    PendingApc->Flags = WOW64_APC_CONTEXT_FLAG_HAS_USER_ROUTINE;
    PendingApc->Reserved = 0;
    PendingApc->UserContext = CpuArea->PendingUserContext;
    PendingApc->UserRoutine = CpuArea->PendingUserRoutine;
    PendingApc->SystemArgument1 = CpuArea->PendingSystemArgument1;
    PendingApc->SystemArgument2 = CpuArea->PendingSystemArgument2;

    CpuArea->PendingUserContext = 0;
    CpuArea->PendingUserRoutine = 0;
    CpuArea->PendingSystemArgument1 = 0;
    CpuArea->PendingSystemArgument2 = 0;
    CpuArea->Flags &= ~(WOW64_CPU_AREA_FLAG_PENDING_APC |
                        WOW64_CPU_AREA_FLAG_APC_IN_PROGRESS);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
Wow64PrepareForException(
    PWOW64_CPU_AREA CpuArea,
    const CONTEXT *HostContext)
{
    if (!CpuArea || !HostContext)
    {
        return STATUS_INVALID_PARAMETER;
    }

    return Wow64cpuStoreContext(CpuArea,
                                HostContext,
                                sizeof(*HostContext),
                                TRUE);
}

NTSTATUS
NTAPI
Wow64TransitionToNative(
    PWOW64_CPU_AREA CpuArea,
    WOW64_CONTEXT *CompatContext,
    CONTEXT *NativeContext)
{
    NTSTATUS Status;

    if (!CpuArea || !CompatContext || !NativeContext)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ZeroMemory(NativeContext, sizeof(*NativeContext));
    NativeContext->ContextFlags = CONTEXT_CONTROL |
                                  CONTEXT_INTEGER |
                                  CONTEXT_SEGMENTS |
                                  CONTEXT_FLOATING_POINT |
                                  CONTEXT_DEBUG_REGISTERS;

    NativeContext->Dr0 = CompatContext->Dr0;
    NativeContext->Dr1 = CompatContext->Dr1;
    NativeContext->Dr2 = CompatContext->Dr2;
    NativeContext->Dr3 = CompatContext->Dr3;
    NativeContext->Dr6 = CompatContext->Dr6;
    NativeContext->Dr7 = CompatContext->Dr7;

    NativeContext->Rax = CompatContext->Eax;
    NativeContext->Rbx = CompatContext->Ebx;
    NativeContext->Rcx = CompatContext->Ecx;
    NativeContext->Rdx = CompatContext->Edx;
    NativeContext->Rsi = CompatContext->Esi;
    NativeContext->Rdi = CompatContext->Edi;
    NativeContext->Rbp = CompatContext->Ebp;
    NativeContext->Rsp = CompatContext->Esp;
    NativeContext->Rip = CompatContext->Eip;

    NativeContext->EFlags = CompatContext->EFlags;

    NativeContext->SegCs = (WORD)CompatContext->SegCs;
    NativeContext->SegSs = (WORD)CompatContext->SegSs;
    NativeContext->SegDs = (WORD)CompatContext->SegDs;
    NativeContext->SegEs = (WORD)CompatContext->SegEs;
    NativeContext->SegFs = (WORD)CompatContext->SegFs;
    NativeContext->SegGs = (WORD)CompatContext->SegGs;

    RtlZeroMemory(&NativeContext->FltSave, sizeof(NativeContext->FltSave));
    RtlCopyMemory(&NativeContext->FltSave,
                  &CompatContext->FloatSave,
                  sizeof(CompatContext->FloatSave));

    Status = Wow64cpuStoreContext(CpuArea,
                                  CompatContext,
                                  sizeof(*CompatContext),
                                  FALSE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = Wow64cpuStoreContext(CpuArea,
                                  NativeContext,
                                  sizeof(*NativeContext),
                                  TRUE);
    return Status;
}

NTSTATUS
NTAPI
Wow64TransitionToCompat(
    PWOW64_CPU_AREA CpuArea,
    CONTEXT *NativeContext,
    WOW64_CONTEXT *CompatContext)
{
    NTSTATUS Status;

    if (!CpuArea || !NativeContext || !CompatContext)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ZeroMemory(CompatContext, sizeof(*CompatContext));
    CompatContext->ContextFlags = WOW64_CONTEXT_FULL;

    CompatContext->Dr0 = (DWORD)NativeContext->Dr0;
    CompatContext->Dr1 = (DWORD)NativeContext->Dr1;
    CompatContext->Dr2 = (DWORD)NativeContext->Dr2;
    CompatContext->Dr3 = (DWORD)NativeContext->Dr3;
    CompatContext->Dr6 = (DWORD)NativeContext->Dr6;
    CompatContext->Dr7 = (DWORD)NativeContext->Dr7;

    CompatContext->Eax = (DWORD)NativeContext->Rax;
    CompatContext->Ebx = (DWORD)NativeContext->Rbx;
    CompatContext->Ecx = (DWORD)NativeContext->Rcx;
    CompatContext->Edx = (DWORD)NativeContext->Rdx;
    CompatContext->Esi = (DWORD)NativeContext->Rsi;
    CompatContext->Edi = (DWORD)NativeContext->Rdi;
    CompatContext->Ebp = (DWORD)NativeContext->Rbp;
    CompatContext->Esp = (DWORD)NativeContext->Rsp;
    CompatContext->Eip = (DWORD)NativeContext->Rip;

    CompatContext->EFlags = NativeContext->EFlags;

    CompatContext->SegCs = NativeContext->SegCs;
    CompatContext->SegSs = NativeContext->SegSs;
    CompatContext->SegDs = NativeContext->SegDs;
    CompatContext->SegEs = NativeContext->SegEs;
    CompatContext->SegFs = NativeContext->SegFs;
    CompatContext->SegGs = NativeContext->SegGs;

    if (NativeContext->ContextFlags & CONTEXT_FLOATING_POINT)
    {
        RtlCopyMemory(&CompatContext->FloatSave,
                      &NativeContext->FltSave,
                      sizeof(CompatContext->FloatSave));
    }
    else
    {
        RtlZeroMemory(&CompatContext->FloatSave, sizeof(CompatContext->FloatSave));
    }

    RtlZeroMemory(CompatContext->ExtendedRegisters, sizeof(CompatContext->ExtendedRegisters));

    Status = Wow64cpuStoreContext(CpuArea,
                                  NativeContext,
                                  sizeof(*NativeContext),
                                  TRUE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = Wow64cpuStoreContext(CpuArea,
                                  CompatContext,
                                  sizeof(*CompatContext),
                                  FALSE);
    return Status;
}

VOID
NTAPI
Wow64Transition(VOID)
{
    PWOW64_CPU_AREA CpuArea;
    WOW64_CONTEXT CompatContext;
    CONTEXT NativeContext;
    NTSTATUS Status;

    CpuArea = Wow64cpuQueryThreadArea();
    if (!CpuArea)
    {
        Wow64cpuDebugTrace("Wow64Transition: no CPU area");
        return;
    }

    Status = Wow64cpuCaptureContext(CpuArea,
                                    &CompatContext,
                                    sizeof(CompatContext),
                                    FALSE);
    if (!NT_SUCCESS(Status))
    {
        Wow64cpuDebugTrace("Wow64Transition: no compat context available");
        return;
    }

    Status = Wow64TransitionToNative(CpuArea, &CompatContext, &NativeContext);
    if (!NT_SUCCESS(Status))
    {
        Wow64cpuDebugTrace("Wow64Transition: native conversion failed");
    }
}
