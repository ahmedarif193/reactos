#pragma once

#ifdef _NTOSKRNL_
#include <ntdef.h>
#else
#include <windef.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define WOW64_APC_CONTEXT_VERSION 1

#define WOW64_APC_CONTEXT_FLAG_KERNEL_FILLED  0x00000001
#define WOW64_APC_CONTEXT_FLAG_HAS_CPU_AREA   0x00000002
#define WOW64_APC_CONTEXT_FLAG_HAS_USER_ROUTINE 0x00000004

typedef struct _WOW64_APC_CONTEXT
{
    ULONG Version;
    ULONG Size;
    ULONG Flags;
    ULONG Reserved;
    ULONG_PTR UserContext;
    ULONG_PTR UserRoutine;
    ULONG_PTR Wow64CpuArea;
    ULONG_PTR Reserved1;
} WOW64_APC_CONTEXT, *PWOW64_APC_CONTEXT;

#define WOW64_PENDING_APC_VERSION 1

typedef struct _WOW64_PENDING_APC
{
    ULONG Version;
    ULONG Size;
    ULONG Flags;
    ULONG Reserved;
    ULONG_PTR UserContext;
    ULONG_PTR UserRoutine;
    ULONG_PTR SystemArgument1;
    ULONG_PTR SystemArgument2;
} WOW64_PENDING_APC, *PWOW64_PENDING_APC;

#define WOW64_PROCESS_INFO_VERSION 1

typedef struct _WOW64_PROCESS_INFO
{
    ULONG Version;
    ULONG Size;
    PVOID Wow64ApcDispatcher;
    PVOID Reserved0;
    ULONG_PTR Reserved[4];
} WOW64_PROCESS_INFO, *PWOW64_PROCESS_INFO;

NTSTATUS
NTAPI
Wow64PopPendingApc(
    _Inout_ PWOW64_PENDING_APC PendingApc);

NTSTATUS
NTAPI
Wow64DeliverPendingApc(VOID);

#ifdef __cplusplus
}
#endif
