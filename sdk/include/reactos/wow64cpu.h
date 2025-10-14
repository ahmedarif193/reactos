#pragma once

#ifdef _NTOSKRNL_
#include <ntdef.h>
#include <xdk/winnt_old.h>
#else
#include <winnt.h>
#endif

#include <reactos/wow64apc.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _WOW64_CPU_AREA WOW64_CPU_AREA, *PWOW64_CPU_AREA;

#define WOW64_CPU_AREA_POINTER_TAG ((ULONG_PTR)0x1)
#define WOW64_CPU_AREA_ENCODE_POINTER(Area) \
    ((ULONG_PTR)((Area) ? ((ULONG_PTR)(Area) | WOW64_CPU_AREA_POINTER_TAG) : WOW64_CPU_AREA_POINTER_TAG))
#define WOW64_CPU_AREA_DECODE_POINTER(Value) \
    ((PWOW64_CPU_AREA)((ULONG_PTR)(Value) & ~(ULONG_PTR)WOW64_CPU_AREA_POINTER_TAG))
#define WOW64_CPU_AREA_IS_TAGGED(Value) \
    (((ULONG_PTR)(Value) & WOW64_CPU_AREA_POINTER_TAG) != 0)

typedef enum _WOW64_CPU_NOTIFY_TYPE
{
    Wow64CpuNotifyInitialize = 0,
    Wow64CpuNotifyThreadAttach,
    Wow64CpuNotifyThreadDetach,
    Wow64CpuNotifyShutdown
} WOW64_CPU_NOTIFY_TYPE;

NTSTATUS
NTAPI
CpuProcessInit(
    _Outptr_ PWOW64_CPU_AREA *CpuArea,
    _In_opt_ PVOID Reserved);

NTSTATUS
NTAPI
CpuThreadInit(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _In_opt_ PVOID ThreadContext);

VOID
NTAPI
CpuThreadTerm(
    _Inout_ PWOW64_CPU_AREA CpuArea);

NTSTATUS
NTAPI
CpuNotify(
    _In_ WOW64_CPU_NOTIFY_TYPE NotifyType,
    _Inout_opt_ PWOW64_CPU_AREA CpuArea,
    _In_opt_ PVOID Parameter);

VOID
NTAPI
CpupReturnFromSimulatedCode(VOID);

NTSTATUS
NTAPI
Wow64CpuGetContext(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _Out_writes_bytes_(ContextLength) PVOID Context,
    _In_ ULONG ContextLength);

NTSTATUS
NTAPI
Wow64CpuSetContext(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _In_reads_bytes_(ContextLength) const VOID *Context,
    _In_ ULONG ContextLength);

NTSTATUS
NTAPI
Wow64CpuSetPendingApc(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _In_ const WOW64_APC_CONTEXT *ApcContext,
    _In_ ULONG_PTR SystemArgument1,
    _In_ ULONG_PTR SystemArgument2);

NTSTATUS
NTAPI
Wow64CpuDispatchPendingApc(
    _Inout_ PWOW64_CPU_AREA CpuArea);

NTSTATUS
NTAPI
Wow64CpuTakePendingApc(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _Inout_ PWOW64_PENDING_APC PendingApc);

NTSTATUS
NTAPI
Wow64PrepareForException(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _In_reads_bytes_(sizeof(CONTEXT)) const CONTEXT *HostContext);

NTSTATUS
NTAPI
Wow64TransitionToNative(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _Inout_ WOW64_CONTEXT *CompatContext,
    _Out_writes_bytes_(sizeof(CONTEXT)) CONTEXT *NativeContext);

NTSTATUS
NTAPI
Wow64TransitionToCompat(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _Inout_ CONTEXT *NativeContext,
    _Out_writes_bytes_(sizeof(WOW64_CONTEXT)) WOW64_CONTEXT *CompatContext);

VOID
NTAPI
Wow64Transition(VOID);

#ifdef __cplusplus
}
#endif
