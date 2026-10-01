/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Platform extension plug-in registration
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <acpiioct.h>
#include <debug.h>

#define PEP_INFORMATION_V3                0x00000003
#define PEP_KERNEL_INFORMATION_V3         0x00000003
#define PEP_FLAG_WORKER_CONCURRENCY       0x00000001
#define PEP_DPM_WORK                      0x0D
#define POP_PEP_TAG                       'pePP'

DECLARE_HANDLE(PEPHANDLE);

typedef VOID POFXCALLBACKREQUESTWORKER(_In_ POHANDLE PluginHandle);
typedef POFXCALLBACKREQUESTWORKER *PPOFXCALLBACKREQUESTWORKER;

typedef VOID POFXCALLBACKCRITICALRESOURCE(
    _In_ POHANDLE DeviceHandle,
    _In_ ULONG Component,
    _In_ BOOLEAN Active);
typedef POFXCALLBACKCRITICALRESOURCE *PPOFXCALLBACKCRITICALRESOURCE;

typedef NTSTATUS (NTAPI *PPOP_PEP_HALT_ROUTINE)(_Inout_opt_ PVOID Context);

typedef NTSTATUS POFXCALLBACKPROCESSORHALT(
    _In_ ULONG Flags,
    _Inout_opt_ PVOID Context,
    _In_ PPOP_PEP_HALT_ROUTINE Halt);
typedef POFXCALLBACKPROCESSORHALT *PPOFXCALLBACKPROCESSORHALT;

typedef NTSTATUS POFXCALLBACKREQUESTINTERRUPT(_In_ ULONG Gsiv);
typedef POFXCALLBACKREQUESTINTERRUPT *PPOFXCALLBACKREQUESTINTERRUPT;

typedef union _PEP_UNMASKED_INTERRUPT_FLAGS
{
    struct
    {
        USHORT SecondaryInterrupt:1;
        USHORT Reserved:15;
    };
    USHORT AsUSHORT;
} PEP_UNMASKED_INTERRUPT_FLAGS, *PPEP_UNMASKED_INTERRUPT_FLAGS;

typedef struct _PEP_UNMASKED_INTERRUPT_INFORMATION
{
    USHORT Version;
    USHORT Size;
    PEP_UNMASKED_INTERRUPT_FLAGS Flags;
    KINTERRUPT_MODE Mode;
    KINTERRUPT_POLARITY Polarity;
    ULONG Gsiv;
    USHORT PinNumber;
    PEPHANDLE DeviceHandle;
} PEP_UNMASKED_INTERRUPT_INFORMATION, *PPEP_UNMASKED_INTERRUPT_INFORMATION;

typedef BOOLEAN (*PPO_ENUMERATE_INTERRUPT_SOURCE_CALLBACK)(
    _In_ PVOID CallbackContext,
    _In_ PPEP_UNMASKED_INTERRUPT_INFORMATION InterruptInformation);

typedef NTSTATUS POFXCALLBACKENUMERATEUNMASKEDINTERRUPTS(
    _In_opt_ POHANDLE PluginHandle,
    _In_opt_ ULONG EnumerateFlags,
    _In_ PPO_ENUMERATE_INTERRUPT_SOURCE_CALLBACK Callback,
    _In_ PVOID CallbackContext,
    _Inout_ PPEP_UNMASKED_INTERRUPT_INFORMATION InterruptInformation);
typedef POFXCALLBACKENUMERATEUNMASKEDINTERRUPTS *PPOFXCALLBACKENUMERATEUNMASKEDINTERRUPTS;

typedef NTSTATUS POFXCALLBACKPROCESSORIDLEVETO(
    _In_ POHANDLE ProcessorHandle,
    _In_ ULONG ProcessorState,
    _In_ ULONG VetoReason,
    _In_ BOOLEAN Increment);
typedef POFXCALLBACKPROCESSORIDLEVETO *PPOFXCALLBACKPROCESSORIDLEVETO;

typedef NTSTATUS POFXCALLBACKPLATFORMIDLEVETO(
    _In_ POHANDLE ProcessorHandle,
    _In_ ULONG PlatformState,
    _In_ ULONG VetoReason,
    _In_ BOOLEAN Increment);
typedef POFXCALLBACKPLATFORMIDLEVETO *PPOFXCALLBACKPLATFORMIDLEVETO;

typedef struct _PEP_PROCESSOR_IDLE_STATE_UPDATE
{
    ULONG Version;
    ULONG Latency;
    ULONG BreakEvenDuration;
} PEP_PROCESSOR_IDLE_STATE_UPDATE, *PPEP_PROCESSOR_IDLE_STATE_UPDATE;

typedef NTSTATUS POFXCALLBACKUPDATEPROCESSORIDLESTATE(
    _In_ POHANDLE ProcessorHandle,
    _In_ ULONG ProcessorState,
    _In_ PPEP_PROCESSOR_IDLE_STATE_UPDATE Update);
typedef POFXCALLBACKUPDATEPROCESSORIDLESTATE *PPOFXCALLBACKUPDATEPROCESSORIDLESTATE;

typedef struct _PEP_PLATFORM_IDLE_STATE_UPDATE
{
    ULONG Version;
    ULONG Latency;
    ULONG BreakEvenDuration;
} PEP_PLATFORM_IDLE_STATE_UPDATE, *PPEP_PLATFORM_IDLE_STATE_UPDATE;

typedef NTSTATUS POFXCALLBACKUPDATEPLATFORMIDLESTATE(
    _In_ POHANDLE ProcessorHandle,
    _In_ ULONG PlatformState,
    _In_ PPEP_PLATFORM_IDLE_STATE_UPDATE Update);
typedef POFXCALLBACKUPDATEPLATFORMIDLESTATE *PPOFXCALLBACKUPDATEPLATFORMIDLESTATE;

typedef NTSTATUS POFXCALLBACKREQUESTCOMMON(
    _In_ ULONG RequestId,
    _Inout_opt_ PVOID Data);
typedef POFXCALLBACKREQUESTCOMMON *PPOFXCALLBACKREQUESTCOMMON;

typedef struct _PEP_KERNEL_INFORMATION_STRUCT_V3
{
    USHORT Version;
    USHORT Size;
    POHANDLE Plugin;
    PPOFXCALLBACKREQUESTWORKER RequestWorker;
    PPOFXCALLBACKENUMERATEUNMASKEDINTERRUPTS EnumerateUnmaskedInterrupts;
    PPOFXCALLBACKPROCESSORHALT ProcessorHalt;
    PPOFXCALLBACKREQUESTINTERRUPT RequestInterrupt;
    PPOFXCALLBACKCRITICALRESOURCE TransitionCriticalResource;
    PPOFXCALLBACKPROCESSORIDLEVETO ProcessorIdleVeto;
    PPOFXCALLBACKPLATFORMIDLEVETO PlatformIdleVeto;
    PPOFXCALLBACKUPDATEPROCESSORIDLESTATE UpdateProcessorIdleState;
    PPOFXCALLBACKUPDATEPLATFORMIDLESTATE UpdatePlatformIdleState;
    PPOFXCALLBACKREQUESTCOMMON RequestCommon;
} PEP_KERNEL_INFORMATION, *PPEP_KERNEL_INFORMATION;

typedef BOOLEAN PEPCALLBACKNOTIFYDPM(
    _In_ ULONG Notification,
    _In_ PVOID Data);
typedef PEPCALLBACKNOTIFYDPM *PPEPCALLBACKNOTIFYDPM;

typedef BOOLEAN PEPCALLBACKNOTIFYPPM(
    _In_ PEPHANDLE Handle,
    _In_ ULONG Notification,
    _Inout_opt_ PVOID Data);
typedef PEPCALLBACKNOTIFYPPM *PPEPCALLBACKNOTIFYPPM;

typedef BOOLEAN PEPCALLBACKNOTIFYACPI(
    _In_ ULONG Notification,
    _Inout_opt_ PVOID Data);
typedef PEPCALLBACKNOTIFYACPI *PPEPCALLBACKNOTIFYACPI;

typedef struct _PEP_INFORMATION
{
    USHORT Version;
    USHORT Size;
    PPEPCALLBACKNOTIFYDPM AcceptDeviceNotification;
    PPEPCALLBACKNOTIFYPPM AcceptProcessorNotification;
    PPEPCALLBACKNOTIFYACPI AcceptAcpiNotification;
} PEP_INFORMATION, *PPEP_INFORMATION;

typedef enum _PEP_WORK_TYPE
{
    PepWorkRequestPowerControl = 3,
    PepWorkCompleteIdleState = 5,
    PepWorkCompletePerfState,
    PepWorkAcpiNotify,
    PepWorkAcpiEvaluateControlMethodComplete,
    PepWorkMax
} PEP_WORK_TYPE, *PPEP_WORK_TYPE;

typedef struct _PEP_WORK_POWER_CONTROL
{
    POHANDLE DeviceHandle;
    LPCGUID PowerControlCode;
    PVOID RequestContext;
    PVOID InBuffer;
    SIZE_T InBufferSize;
    PVOID OutBuffer;
    SIZE_T OutBufferSize;
} PEP_WORK_POWER_CONTROL, *PPEP_WORK_POWER_CONTROL;

typedef struct _PEP_WORK_COMPLETE_IDLE_STATE
{
    POHANDLE DeviceHandle;
    ULONG Component;
} PEP_WORK_COMPLETE_IDLE_STATE, *PPEP_WORK_COMPLETE_IDLE_STATE;

typedef struct _PEP_WORK_COMPLETE_PERF_STATE
{
    POHANDLE DeviceHandle;
    ULONG Component;
    BOOLEAN Succeeded;
} PEP_WORK_COMPLETE_PERF_STATE, *PPEP_WORK_COMPLETE_PERF_STATE;

typedef struct _PEP_WORK_ACPI_NOTIFY
{
    POHANDLE DeviceHandle;
    ULONG NotifyCode;
} PEP_WORK_ACPI_NOTIFY, *PPEP_WORK_ACPI_NOTIFY;

typedef struct _PEP_WORK_ACPI_EVALUATE_CONTROL_METHOD_COMPLETE
{
    POHANDLE DeviceHandle;
    ULONG CompletionFlags;
    NTSTATUS MethodStatus;
    PVOID CompletionContext;
    SIZE_T OutputArgumentSize;
    PACPI_METHOD_ARGUMENT OutputArguments;
} PEP_WORK_ACPI_EVALUATE_CONTROL_METHOD_COMPLETE, *PPEP_WORK_ACPI_EVALUATE_CONTROL_METHOD_COMPLETE;

typedef struct _PEP_WORK_INFORMATION
{
    PEP_WORK_TYPE WorkType;
    union
    {
        PEP_WORK_POWER_CONTROL PowerControl;
        PEP_WORK_COMPLETE_IDLE_STATE CompleteIdleState;
        PEP_WORK_COMPLETE_PERF_STATE CompletePerfState;
        PEP_WORK_ACPI_NOTIFY AcpiNotify;
        PEP_WORK_ACPI_EVALUATE_CONTROL_METHOD_COMPLETE ControlMethodComplete;
    };
} PEP_WORK_INFORMATION, *PPEP_WORK_INFORMATION;

typedef struct _PEP_WORK
{
    PPEP_WORK_INFORMATION WorkInformation;
    BOOLEAN NeedWork;
} PEP_WORK, *PPEP_WORK;

typedef struct _POP_PEP_PLUGIN
{
    LIST_ENTRY ListEntry;
    PEP_INFORMATION Information;
    ULONGLONG Flags;
    WORK_QUEUE_ITEM WorkItem;
    LONG WorkRequests;
} POP_PEP_PLUGIN, *PPOP_PEP_PLUGIN;

static LIST_ENTRY PopPepPluginList = {&PopPepPluginList, &PopPepPluginList};
static KSPIN_LOCK PopPepPluginLock;

static
VOID
NTAPI
PopPepWorker(
    _In_ PVOID Parameter)
{
    PPOP_PEP_PLUGIN Plugin = Parameter;
    PEP_WORK_INFORMATION WorkInformation;
    PEP_WORK Work;

    do
    {
        RtlZeroMemory(&WorkInformation, sizeof(WorkInformation));
        Work.WorkInformation = &WorkInformation;
        Work.NeedWork = FALSE;
        if (Plugin->Information.AcceptDeviceNotification != NULL)
            Plugin->Information.AcceptDeviceNotification(PEP_DPM_WORK, &Work);
    } while (InterlockedDecrement(&Plugin->WorkRequests) != 0);
}

static
VOID
PopPepRequestWorker(
    _In_ POHANDLE PluginHandle)
{
    PPOP_PEP_PLUGIN Plugin = (PPOP_PEP_PLUGIN)PluginHandle;

    if (InterlockedIncrement(&Plugin->WorkRequests) == 1)
        ExQueueWorkItem(&Plugin->WorkItem, DelayedWorkQueue);
}

static
NTSTATUS
PopPepEnumerateUnmaskedInterrupts(
    _In_opt_ POHANDLE PluginHandle,
    _In_opt_ ULONG EnumerateFlags,
    _In_ PPO_ENUMERATE_INTERRUPT_SOURCE_CALLBACK Callback,
    _In_ PVOID CallbackContext,
    _Inout_ PPEP_UNMASKED_INTERRUPT_INFORMATION InterruptInformation)
{
    UNREFERENCED_PARAMETER(PluginHandle);
    UNREFERENCED_PARAMETER(EnumerateFlags);
    UNREFERENCED_PARAMETER(Callback);
    UNREFERENCED_PARAMETER(CallbackContext);
    UNREFERENCED_PARAMETER(InterruptInformation);
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
PopPepProcessorHalt(
    _In_ ULONG Flags,
    _Inout_opt_ PVOID Context,
    _In_ PPOP_PEP_HALT_ROUTINE Halt)
{
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Halt);
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
PopPepRequestInterrupt(
    _In_ ULONG Gsiv)
{
    UNREFERENCED_PARAMETER(Gsiv);
    return STATUS_NOT_SUPPORTED;
}

static
VOID
PopPepTransitionCriticalResource(
    _In_ POHANDLE DeviceHandle,
    _In_ ULONG Component,
    _In_ BOOLEAN Active)
{
    UNREFERENCED_PARAMETER(DeviceHandle);
    UNREFERENCED_PARAMETER(Component);
    UNREFERENCED_PARAMETER(Active);
}

static
NTSTATUS
PopPepIdleVeto(
    _In_ POHANDLE ProcessorHandle,
    _In_ ULONG State,
    _In_ ULONG VetoReason,
    _In_ BOOLEAN Increment)
{
    UNREFERENCED_PARAMETER(ProcessorHandle);
    UNREFERENCED_PARAMETER(State);
    UNREFERENCED_PARAMETER(VetoReason);
    UNREFERENCED_PARAMETER(Increment);
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
PopPepUpdateProcessorIdleState(
    _In_ POHANDLE ProcessorHandle,
    _In_ ULONG ProcessorState,
    _In_ PPEP_PROCESSOR_IDLE_STATE_UPDATE Update)
{
    UNREFERENCED_PARAMETER(ProcessorHandle);
    UNREFERENCED_PARAMETER(ProcessorState);
    UNREFERENCED_PARAMETER(Update);
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
PopPepUpdatePlatformIdleState(
    _In_ POHANDLE ProcessorHandle,
    _In_ ULONG PlatformState,
    _In_ PPEP_PLATFORM_IDLE_STATE_UPDATE Update)
{
    UNREFERENCED_PARAMETER(ProcessorHandle);
    UNREFERENCED_PARAMETER(PlatformState);
    UNREFERENCED_PARAMETER(Update);
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
PopPepRequestCommon(
    _In_ ULONG RequestId,
    _Inout_opt_ PVOID Data)
{
    UNREFERENCED_PARAMETER(RequestId);
    UNREFERENCED_PARAMETER(Data);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
PoFxRegisterPluginEx(
    _In_ PPEP_INFORMATION PepInformation,
    _In_ ULONGLONG Flags,
    _Inout_ PPEP_KERNEL_INFORMATION KernelInformation)
{
    PPOP_PEP_PLUGIN Plugin;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    PAGED_CODE();

    if ((PepInformation == NULL) || (KernelInformation == NULL))
        return STATUS_INVALID_PARAMETER;
    if ((PepInformation->Version != PEP_INFORMATION_V3) ||
        (PepInformation->Size < sizeof(PEP_INFORMATION)))
    {
        return STATUS_INVALID_PEP_INFO_VERSION;
    }
    if ((KernelInformation->Version != PEP_KERNEL_INFORMATION_V3) ||
        (KernelInformation->Size < sizeof(PEP_KERNEL_INFORMATION)) ||
        ((PepInformation->AcceptDeviceNotification == NULL) &&
         (PepInformation->AcceptProcessorNotification == NULL) &&
         (PepInformation->AcceptAcpiNotification == NULL)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Plugin = ExAllocatePoolZero(NonPagedPool, sizeof(*Plugin), POP_PEP_TAG);
    if (Plugin == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Plugin->Information = *PepInformation;
    Plugin->Flags = Flags;
    ExInitializeWorkItem(&Plugin->WorkItem, PopPepWorker, Plugin);

    KeAcquireSpinLock(&PopPepPluginLock, &OldIrql);
    for (Entry = PopPepPluginList.Flink; Entry != &PopPepPluginList; Entry = Entry->Flink)
    {
        PPOP_PEP_PLUGIN Other = CONTAINING_RECORD(Entry, POP_PEP_PLUGIN, ListEntry);

        if ((Other->Information.AcceptDeviceNotification == PepInformation->AcceptDeviceNotification) &&
            (Other->Information.AcceptProcessorNotification == PepInformation->AcceptProcessorNotification) &&
            (Other->Information.AcceptAcpiNotification == PepInformation->AcceptAcpiNotification))
        {
            KeReleaseSpinLock(&PopPepPluginLock, OldIrql);
            ExFreePoolWithTag(Plugin, POP_PEP_TAG);
            return STATUS_INVALID_PARAMETER;
        }
    }
    InsertTailList(&PopPepPluginList, &Plugin->ListEntry);
    KeReleaseSpinLock(&PopPepPluginLock, OldIrql);

    KernelInformation->Plugin = (POHANDLE)Plugin;
    KernelInformation->RequestWorker = PopPepRequestWorker;
    KernelInformation->EnumerateUnmaskedInterrupts = PopPepEnumerateUnmaskedInterrupts;
    KernelInformation->ProcessorHalt = PopPepProcessorHalt;
    KernelInformation->RequestInterrupt = PopPepRequestInterrupt;
    KernelInformation->TransitionCriticalResource = PopPepTransitionCriticalResource;
    KernelInformation->ProcessorIdleVeto = PopPepIdleVeto;
    KernelInformation->PlatformIdleVeto = PopPepIdleVeto;
    KernelInformation->UpdateProcessorIdleState = PopPepUpdateProcessorIdleState;
    KernelInformation->UpdatePlatformIdleState = PopPepUpdatePlatformIdleState;
    KernelInformation->RequestCommon = PopPepRequestCommon;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PoFxRegisterPlugin(
    _In_ PPEP_INFORMATION PepInformation,
    _Inout_ PPEP_KERNEL_INFORMATION KernelInformation)
{
    return PoFxRegisterPluginEx(PepInformation, 0, KernelInformation);
}
