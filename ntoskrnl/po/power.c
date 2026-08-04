/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/po/power.c
 * PURPOSE:         Power Manager
 * PROGRAMMERS:     Casper S. Hornstrup (chorns@users.sourceforge.net)
 *                  Hervé Poussineau (hpoussin@reactos.com)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include <poclass.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

typedef struct _POWER_STATE_TRAVERSE_CONTEXT
{
    SYSTEM_POWER_STATE SystemPowerState;
    POWER_ACTION PowerAction;
    PDEVICE_OBJECT PowerDevice;
} POWER_STATE_TRAVERSE_CONTEXT, *PPOWER_STATE_TRAVERSE_CONTEXT;

PDEVICE_NODE PopSystemPowerDeviceNode = NULL;
BOOLEAN PopAcpiPresent = FALSE;
POP_POWER_ACTION PopAction;
WORK_QUEUE_ITEM PopShutdownWorkItem;
SYSTEM_POWER_CAPABILITIES PopCapabilities;

#define POP_PROCESSOR_POWER_TAG 'pPoP'
#define POP_PROCESSOR_PERF_PERIOD_MS 100
#define POP_MAXIMUM_PERF_STATES 32

/* PRIVATE FUNCTIONS *********************************************************/

static WORKER_THREAD_ROUTINE PopPassivePowerCall;

_Use_decl_annotations_
static
VOID
NTAPI
PopPassivePowerCall(
    PVOID Parameter)
{
    PIRP Irp = Parameter;
    PIO_STACK_LOCATION IoStack;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    _Analysis_assume_(Irp != NULL);
    IoStack = IoGetNextIrpStackLocation(Irp);

    (VOID)IoCallDriver(IoStack->DeviceObject, Irp);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
static
NTSTATUS
PopPresentIrp(
    _In_ PIO_STACK_LOCATION NextStack,
    _In_ PIRP Irp)
{
    NTSTATUS Status;
    BOOLEAN CallAtPassiveLevel;
    PDEVICE_OBJECT DeviceObject;
    PWORK_QUEUE_ITEM WorkQueueItem;

    ASSERT(NextStack->MajorFunction == IRP_MJ_POWER);

    DeviceObject = NextStack->DeviceObject;

    /* Determine whether the IRP must be handled at PASSIVE_LEVEL.
     * Only SET_POWER to working state can happen at raised IRQL. */
    CallAtPassiveLevel = TRUE;
    if ((NextStack->MinorFunction == IRP_MN_SET_POWER) &&
        !(DeviceObject->Flags & DO_POWER_PAGABLE))
    {
        if (NextStack->Parameters.Power.Type == DevicePowerState &&
            NextStack->Parameters.Power.State.DeviceState == PowerDeviceD0)
        {
            CallAtPassiveLevel = FALSE;
        }
        if (NextStack->Parameters.Power.Type == SystemPowerState &&
            NextStack->Parameters.Power.State.SystemState == PowerSystemWorking)
        {
            CallAtPassiveLevel = FALSE;
        }
    }

    if (CallAtPassiveLevel)
    {
        /* We need to fit a work item into the DriverContext below */
        C_ASSERT(sizeof(Irp->Tail.Overlay.DriverContext) >= sizeof(WORK_QUEUE_ITEM));

        if (KeGetCurrentIrql() == PASSIVE_LEVEL)
        {
            /* Already at passive, call next driver directly */
            return IoCallDriver(DeviceObject, Irp);
        }

        /* Need to schedule a work item and return pending */
        NextStack->Control |= SL_PENDING_RETURNED;

        WorkQueueItem = (PWORK_QUEUE_ITEM)&Irp->Tail.Overlay.DriverContext;
        ExInitializeWorkItem(WorkQueueItem,
                             PopPassivePowerCall,
                             Irp);
        ExQueueWorkItem(WorkQueueItem, DelayedWorkQueue);

        return STATUS_PENDING;
    }

    /* Direct call. Raise IRQL in debug to catch invalid paged memory access. */
#if DBG
    {
    KIRQL OldIrql;
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
#endif

    Status = IoCallDriver(DeviceObject, Irp);

#if DBG
    KeLowerIrql(OldIrql);
    }
#endif

    return Status;
}

static IO_COMPLETION_ROUTINE PopRequestPowerIrpCompletion;

static
NTSTATUS
NTAPI
PopRequestPowerIrpCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PIO_STACK_LOCATION Stack;
    PREQUEST_POWER_COMPLETE CompletionRoutine;
    POWER_STATE PowerState;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    CompletionRoutine = Context;

    PowerState.DeviceState = (ULONG_PTR)Stack->Parameters.Others.Argument3;

    if (CompletionRoutine)
    {
        CompletionRoutine(Stack->Parameters.Others.Argument1,
                          (UCHAR)(ULONG_PTR)Stack->Parameters.Others.Argument2,
                          PowerState,
                          Stack->Parameters.Others.Argument4,
                          &Irp->IoStatus);
    }

    IoSkipCurrentIrpStackLocation(Irp);
    IoFreeIrp(Irp);
    ObDereferenceObject(DeviceObject);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

VOID
NTAPI
PopCleanupPowerState(IN PPOWER_STATE PowerState)
{
    //UNIMPLEMENTED;
}

NTSTATUS
PopSendQuerySystemPowerState(PDEVICE_OBJECT DeviceObject, SYSTEM_POWER_STATE SystemState, POWER_ACTION PowerAction)
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    PIO_STACK_LOCATION IrpSp;
    PIRP Irp;
    NTSTATUS Status;

    KeInitializeEvent(&Event,
                      NotificationEvent,
                      FALSE);

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_POWER,
                                       DeviceObject,
                                       NULL,
                                       0,
                                       NULL,
                                       &Event,
                                       &IoStatusBlock);
    if (!Irp) return STATUS_INSUFFICIENT_RESOURCES;

    IrpSp = IoGetNextIrpStackLocation(Irp);
    IrpSp->MinorFunction = IRP_MN_QUERY_POWER;
    IrpSp->Parameters.Power.Type = SystemPowerState;
    IrpSp->Parameters.Power.State.SystemState = SystemState;
    IrpSp->Parameters.Power.ShutdownType = PowerAction;

    Status = PoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = IoStatusBlock.Status;
    }

    return Status;
}

NTSTATUS
PopSendSetSystemPowerState(PDEVICE_OBJECT DeviceObject, SYSTEM_POWER_STATE SystemState, POWER_ACTION PowerAction)
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    PIO_STACK_LOCATION IrpSp;
    PIRP Irp;
    NTSTATUS Status;

    KeInitializeEvent(&Event,
                      NotificationEvent,
                      FALSE);

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_POWER,
                                       DeviceObject,
                                       NULL,
                                       0,
                                       NULL,
                                       &Event,
                                       &IoStatusBlock);
    if (!Irp) return STATUS_INSUFFICIENT_RESOURCES;

    IrpSp = IoGetNextIrpStackLocation(Irp);
    IrpSp->MinorFunction = IRP_MN_SET_POWER;
    IrpSp->Parameters.Power.Type = SystemPowerState;
    IrpSp->Parameters.Power.State.SystemState = SystemState;
    IrpSp->Parameters.Power.ShutdownType = PowerAction;

    Status = PoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = IoStatusBlock.Status;
    }

    return Status;
}

NTSTATUS
PopQuerySystemPowerStateTraverse(PDEVICE_NODE DeviceNode,
                                 PVOID Context)
{
    PPOWER_STATE_TRAVERSE_CONTEXT PowerStateContext = Context;
    PDEVICE_OBJECT TopDeviceObject;
    NTSTATUS Status;

    DPRINT("PopQuerySystemPowerStateTraverse(%p, %p)\n", DeviceNode, Context);

    if (DeviceNode == IopRootDeviceNode)
        return STATUS_SUCCESS;

    if (DeviceNode->Flags & DNF_LEGACY_DRIVER)
        return STATUS_SUCCESS;

    TopDeviceObject = IoGetAttachedDeviceReference(DeviceNode->PhysicalDeviceObject);

    Status = PopSendQuerySystemPowerState(TopDeviceObject,
                                          PowerStateContext->SystemPowerState,
                                          PowerStateContext->PowerAction);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Device '%wZ' failed IRP_MN_QUERY_POWER\n", &DeviceNode->InstancePath);
    }
    ObDereferenceObject(TopDeviceObject);

#if 0
    return Status;
#else
    return STATUS_SUCCESS;
#endif
}

NTSTATUS
PopSetSystemPowerStateTraverse(PDEVICE_NODE DeviceNode,
                               PVOID Context)
{
    PPOWER_STATE_TRAVERSE_CONTEXT PowerStateContext = Context;
    PDEVICE_OBJECT TopDeviceObject;
    NTSTATUS Status;

    DPRINT("PopSetSystemPowerStateTraverse(%p, %p)\n", DeviceNode, Context);

    if (DeviceNode == IopRootDeviceNode)
        return STATUS_SUCCESS;

    if (DeviceNode->PhysicalDeviceObject == PowerStateContext->PowerDevice)
        return STATUS_SUCCESS;

    if (DeviceNode->Flags & DNF_LEGACY_DRIVER)
        return STATUS_SUCCESS;

    TopDeviceObject = IoGetAttachedDeviceReference(DeviceNode->PhysicalDeviceObject);
    if (TopDeviceObject == PowerStateContext->PowerDevice)
    {
        ObDereferenceObject(TopDeviceObject);
        return STATUS_SUCCESS;
    }

    Status = PopSendSetSystemPowerState(TopDeviceObject,
                                        PowerStateContext->SystemPowerState,
                                        PowerStateContext->PowerAction);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Device '%wZ' failed IRP_MN_SET_POWER\n", &DeviceNode->InstancePath);
    }

    ObDereferenceObject(TopDeviceObject);

#if 0
    return Status;
#else
    return STATUS_SUCCESS;
#endif
}

NTSTATUS
NTAPI
PopSetSystemPowerState(SYSTEM_POWER_STATE PowerState, POWER_ACTION PowerAction)
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;
    DEVICETREE_TRAVERSE_CONTEXT Context;
    POWER_STATE_TRAVERSE_CONTEXT PowerContext;

    Status = IopGetSystemPowerDeviceObject(&DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("No system power driver available\n");
        Fdo = NULL;
    }
    else
    {
        Fdo = IoGetAttachedDeviceReference(DeviceObject);
        if (Fdo == DeviceObject)
        {
            DPRINT("An FDO was not attached\n");
            return STATUS_UNSUCCESSFUL;
        }
    }

    /* Set up context */
    PowerContext.PowerAction = PowerAction;
    PowerContext.SystemPowerState = PowerState;
    PowerContext.PowerDevice = Fdo;

    /* Query for system power change */
    IopInitDeviceTreeTraverseContext(&Context,
                                     IopRootDeviceNode,
                                     PopQuerySystemPowerStateTraverse,
                                     &PowerContext);

    Status = IopTraverseDeviceTree(&Context);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Query system power state failed; changing state anyway\n");
    }

    /* Set system power change */
    IopInitDeviceTreeTraverseContext(&Context,
                                     IopRootDeviceNode,
                                     PopSetSystemPowerStateTraverse,
                                     &PowerContext);

    IopTraverseDeviceTree(&Context);

    if (!PopAcpiPresent) return STATUS_NOT_IMPLEMENTED;

    if (Fdo != NULL)
    {
        if (PowerAction != PowerActionShutdownReset)
            PopSendSetSystemPowerState(Fdo, PowerState, PowerAction);

        ObDereferenceObject(Fdo);
    }

    return Status;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
PoInitSystem(IN ULONG BootPhase)
{
    PVOID NotificationEntry;
    PCHAR CommandLine;
    BOOLEAN ForceAcpiDisable = FALSE;

    /* Check if this is phase 1 init */
    if (BootPhase == 1)
    {
        NTSTATUS Status;
        /* Register power button notification */
        Status = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                                                PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                                                (PVOID)&GUID_DEVICE_SYS_BUTTON,
                                                IopRootDeviceNode->PhysicalDeviceObject->DriverObject,
                                                PopAddRemoveSysCapsCallback,
                                                (PVOID)(ULONG_PTR)PolicyDeviceSystemButton,
                                                &NotificationEntry);
        if (!NT_SUCCESS(Status))
            return FALSE;

        /* Register lid notification */
        Status = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                                                PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                                                (PVOID)&GUID_DEVICE_LID,
                                                IopRootDeviceNode->PhysicalDeviceObject->DriverObject,
                                                PopAddRemoveSysCapsCallback,
                                                (PVOID)(ULONG_PTR)PolicyDeviceSystemButton,
                                                &NotificationEntry);
        if (!NT_SUCCESS(Status))
            return FALSE;

        /* Register battery notification */
        Status = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                                                PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                                                (PVOID)&GUID_DEVICE_BATTERY,
                                                IopRootDeviceNode->PhysicalDeviceObject->DriverObject,
                                                PopAddRemoveSysCapsCallback,
                                                (PVOID)(ULONG_PTR)PolicyDeviceBattery,
                                                &NotificationEntry);

        return NT_SUCCESS(Status);
    }

    /* Initialize the power capabilities */
    RtlZeroMemory(&PopCapabilities, sizeof(SYSTEM_POWER_CAPABILITIES));
    PopInitializeEventSupport();
    PopInitializeThermalRequests();

    /* Get the Command Line */
    CommandLine = KeLoaderBlock->LoadOptions;

    /* Upcase it */
    _strupr(CommandLine);

    /* Check for ACPI disable */
    if (strstr(CommandLine, "NOACPI")) ForceAcpiDisable = TRUE;

    if (ForceAcpiDisable)
    {
        /* Set the ACPI State to False if it's been forced that way */
        PopAcpiPresent = FALSE;
    }
    else
    {
        /* Otherwise check if the LoaderBlock has a ACPI Table  */
        PopAcpiPresent = KeLoaderBlock->Extension->AcpiTable != NULL ? TRUE : FALSE;
    }

    /* Enable shutdown by power button */
    if (PopAcpiPresent)
        PopCapabilities.SystemS5 = TRUE;

    /* Initialize volume support */
    InitializeListHead(&PopVolumeDevices);
    KeInitializeGuardedMutex(&PopVolumeLock);

    /* Initialize support for dope */
    KeInitializeSpinLock(&PopDopeGlobalLock);

    /* Initialize support for shutdown waits and work-items */
    PopInitShutdownList();

    return TRUE;
}

VOID
NTAPI
PopPerfIdle(PPROCESSOR_POWER_STATE PowerState)
{
    PKPRCB Prcb = CONTAINING_RECORD(PowerState, KPRCB, PowerState);
    PSET_PROCESSOR_THROTTLE2 SetPerfLevel = PowerState->PerfSetThrottle;
    ULONG TotalTime;
    ULONG IdleTime;
    ULONG TotalDelta;
    ULONG IdleDelta;
    ULONG Busy;
    ULONG Target;
    NTSTATUS Status;

    if (!SetPerfLevel || PowerState->PerfStatesCount == 0)
        return;

    TotalTime = Prcb->KernelTime + Prcb->UserTime;
    IdleTime = Prcb->IdleThread ? Prcb->IdleThread->KernelTime : 0;
    TotalDelta = TotalTime - PowerState->LastKernelUserTime;
    IdleDelta = IdleTime - PowerState->PerfIdleTime;
    PowerState->LastKernelUserTime = TotalTime;
    PowerState->PerfIdleTime = IdleTime;
    if (TotalDelta == 0)
        return;
    if (IdleDelta > TotalDelta)
        IdleDelta = TotalDelta;

    Busy = (ULONG)(((ULONGLONG)(TotalDelta - IdleDelta) * 100) / TotalDelta);
    Target = PowerState->CurrentThrottle;
    if (Busy >= 80)
        Target = min((ULONG)PowerState->ProcessorMaxThrottle, Target + 20);
    else if (Busy <= 20)
        Target = max((ULONG)PowerState->ProcessorMinThrottle, Target > 10 ? Target - 10 : 0);
    else
        Target = Busy + 10;
    Target = max(Target, (ULONG)PowerState->ProcessorMinThrottle);
    Target = min(Target, (ULONG)PowerState->ProcessorMaxThrottle);
    Target = min(Target, (ULONG)PowerState->ThermalThrottleLimit);
    PowerState->LastBusyPercentage = (UCHAR)Busy;
    PowerState->LastAdjustedBusyPercentage = (UCHAR)Target;
    if (Target == PowerState->CurrentThrottle)
        return;

    Status = SetPerfLevel((UCHAR)Target);
    if (NT_SUCCESS(Status))
    {
        PowerState->CurrentThrottle = (UCHAR)Target;
    }
    else
    {
        PowerState->ErrorCount++;
        DPRINT1("POP: CPU %u performance request %lu%% failed, status 0x%08lx\n", Prcb->Number, Target, Status);
    }
}

VOID
NTAPI
PopPerfIdleDpc(IN PKDPC Dpc,
               IN PVOID DeferredContext,
               IN PVOID SystemArgument1,
               IN PVOID SystemArgument2)
{
    /* Call the Perf Idle function */
    PopPerfIdle(&((PKPRCB)DeferredContext)->PowerState);
}

VOID
FASTCALL
PopIdle0(IN PPROCESSOR_POWER_STATE PowerState)
{
    /* FIXME: Extremly naive implementation */
    HalProcessorIdle();
}

static
VOID
FASTCALL
PopIdleRegistered(
    _In_ PPROCESSOR_POWER_STATE PowerState)
{
    PPROCESSOR_IDLE_HANDLER_INFO Handlers = PowerState->IdleHandlers;
    PROCESSOR_IDLE_TIMES IdleTimes;
    ULONG Index = (ULONG)(ULONG_PTR)PowerState->IdleState;
    ULONGLONG Duration;
    NTSTATUS Status;

    if (!Handlers || PowerState->IdleHandlersCount == 0)
    {
        HalProcessorIdle();
        return;
    }
    if (Index >= PowerState->IdleHandlersCount)
        Index = 0;

    RtlZeroMemory(&IdleTimes, sizeof(IdleTimes));
    IdleTimes.StartTime = KeQueryInterruptTime();
    Status = Handlers[Index].Handler(Index, &IdleTimes);
    IdleTimes.EndTime = KeQueryInterruptTime();
    if (!NT_SUCCESS(Status))
    {
        PowerState->ErrorCount++;
        HalProcessorIdle();
        return;
    }

    Duration = IdleTimes.EndTime - IdleTimes.StartTime;
    if (Index < RTL_NUMBER_OF(PowerState->TotalIdleStateTime))
    {
        PowerState->TotalIdleStateTime[Index] += Duration;
        PowerState->TotalIdleTransitions[Index]++;
    }
    if (Index + 1 < PowerState->IdleHandlersCount && Duration >= (ULONGLONG)Handlers[Index + 1].HardwareLatency * 40)
    {
        Index++;
        PowerState->PromotionCount++;
    }
    else if (Index > 0 && Duration < (ULONGLONG)Handlers[Index].HardwareLatency * 20)
    {
        Index--;
        PowerState->DemotionCount++;
    }
    PowerState->IdleState = (PVOID)(ULONG_PTR)Index;
}

static
NTSTATUS
PopRegisterProcessorStateHandler2(
    _In_reads_bytes_(InputBufferLength) PPROCESSOR_STATE_HANDLER2 Handler,
    _In_ ULONG InputBufferLength)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PPROCESSOR_POWER_STATE PowerState = &Prcb->PowerState;
    PPROCESSOR_IDLE_HANDLER_INFO IdleHandlers = NULL;
    PPROCESSOR_IDLE_HANDLER_INFO OldIdleHandlers;
    PPROCESSOR_PERF_STATE PerfStates = NULL;
    PPROCESSOR_PERF_STATE OldPerfStates;
    ULONG RequiredLength;
    ULONG Index;
    KIRQL OldIrql;
    LARGE_INTEGER DueTime;

    if (InputBufferLength < FIELD_OFFSET(PROCESSOR_STATE_HANDLER2, PerfLevel))
        return STATUS_INFO_LENGTH_MISMATCH;
    if (Handler->NumIdleHandlers > MAX_IDLE_HANDLERS || Handler->NumPerfStates > POP_MAXIMUM_PERF_STATES)
        return STATUS_INVALID_PARAMETER;
    RequiredLength = FIELD_OFFSET(PROCESSOR_STATE_HANDLER2, PerfLevel) + Handler->NumPerfStates * sizeof(Handler->PerfLevel[0]);
    if (RequiredLength > InputBufferLength)
        return STATUS_INFO_LENGTH_MISMATCH;

    if (Handler->NumIdleHandlers != 0)
    {
        IdleHandlers = ExAllocatePoolWithTag(NonPagedPool, Handler->NumIdleHandlers * sizeof(*IdleHandlers), POP_PROCESSOR_POWER_TAG);
        if (!IdleHandlers)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlCopyMemory(IdleHandlers, Handler->IdleHandler, Handler->NumIdleHandlers * sizeof(*IdleHandlers));
        for (Index = 0; Index < Handler->NumIdleHandlers; Index++)
        {
            if (!IdleHandlers[Index].Handler)
            {
                ExFreePoolWithTag(IdleHandlers, POP_PROCESSOR_POWER_TAG);
                return STATUS_INVALID_PARAMETER;
            }
        }
    }
    if (Handler->NumPerfStates != 0)
    {
        if (!Handler->SetPerfLevel)
        {
            if (IdleHandlers)
                ExFreePoolWithTag(IdleHandlers, POP_PROCESSOR_POWER_TAG);
            return STATUS_INVALID_PARAMETER;
        }
        PerfStates = ExAllocatePoolWithTag(NonPagedPool, Handler->NumPerfStates * sizeof(*PerfStates), POP_PROCESSOR_POWER_TAG);
        if (!PerfStates)
        {
            if (IdleHandlers)
                ExFreePoolWithTag(IdleHandlers, POP_PROCESSOR_POWER_TAG);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(PerfStates, Handler->NumPerfStates * sizeof(*PerfStates));
        for (Index = 0; Index < Handler->NumPerfStates; Index++)
        {
            PerfStates[Index].PercentFrequency = Handler->PerfLevel[Index].PercentFrequency;
            PerfStates[Index].MinCapacity = Handler->PerfLevel[Index].PercentFrequency;
            PerfStates[Index].Flags = Handler->PerfLevel[Index].Flags;
        }
    }

    OldIrql = KeRaiseIrqlToDpcLevel();
    OldIdleHandlers = PowerState->IdleHandlers;
    OldPerfStates = PowerState->PerfStates;
    PowerState->IdleHandlers = IdleHandlers;
    PowerState->IdleHandlersCount = Handler->NumIdleHandlers;
    PowerState->IdleState = NULL;
    PowerState->IdleFunction = Handler->NumIdleHandlers ? PopIdleRegistered : PopIdle0;
    PowerState->PerfStates = PerfStates;
    PowerState->PerfStatesCount = Handler->NumPerfStates;
    PowerState->PerfSetThrottle = Handler->SetPerfLevel;
    PowerState->ProcessorMaxThrottle = Handler->NumPerfStates ? Handler->PerfLevel[0].PercentFrequency : 100;
    PowerState->ProcessorMinThrottle = Handler->NumPerfStates ? Handler->PerfLevel[Handler->NumPerfStates - 1].PercentFrequency : 100;
    PowerState->ThermalThrottleLimit = 100;
    PowerState->CurrentThrottle = PowerState->ProcessorMaxThrottle;
    PowerState->CurrentThrottleIndex = 0;
    PowerState->LastKernelUserTime = Prcb->KernelTime + Prcb->UserTime;
    PowerState->PerfIdleTime = Prcb->IdleThread ? Prcb->IdleThread->KernelTime : 0;
    KeLowerIrql(OldIrql);

    KeCancelTimer(&PowerState->PerfTimer);
    KeRemoveQueueDpc(&PowerState->PerfDpc);
    if (Handler->NumPerfStates != 0)
    {
        DueTime.QuadPart = -(LONGLONG)POP_PROCESSOR_PERF_PERIOD_MS * 10000;
        KeSetTimerEx(&PowerState->PerfTimer, DueTime, POP_PROCESSOR_PERF_PERIOD_MS, &PowerState->PerfDpc);
        if (!NT_SUCCESS(Handler->SetPerfLevel(PowerState->ProcessorMaxThrottle)))
            PowerState->ErrorCount++;
    }
    if (OldIdleHandlers)
        ExFreePoolWithTag(OldIdleHandlers, POP_PROCESSOR_POWER_TAG);
    if (OldPerfStates)
        ExFreePoolWithTag(OldPerfStates, POP_PROCESSOR_POWER_TAG);

    DPRINT1("POP: CPU %u registered %lu idle and %u performance states\n", Prcb->Number, Handler->NumIdleHandlers, Handler->NumPerfStates);
    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
VOID
NTAPI
PoInitializePrcb(IN PKPRCB Prcb)
{
    /* Initialize the Power State */
    RtlZeroMemory(&Prcb->PowerState, sizeof(Prcb->PowerState));
    Prcb->PowerState.Idle0KernelTimeLimit = 0xFFFFFFFF;
    Prcb->PowerState.CurrentThrottle = 100;
    Prcb->PowerState.ThermalThrottleLimit = 100;
    Prcb->PowerState.ProcessorMinThrottle = 100;
    Prcb->PowerState.ProcessorMaxThrottle = 100;
    Prcb->PowerState.CurrentThrottleIndex = 0;
    Prcb->PowerState.IdleFunction = PopIdle0;

    /* Initialize the Perf DPC and Timer */
    KeInitializeDpc(&Prcb->PowerState.PerfDpc, PopPerfIdleDpc, Prcb);
    KeSetTargetProcessorDpc(&Prcb->PowerState.PerfDpc, Prcb->Number);
    KeInitializeTimerEx(&Prcb->PowerState.PerfTimer, SynchronizationTimer);
}

/* PUBLIC FUNCTIONS **********************************************************/

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
PoCancelDeviceNotify(IN PVOID NotifyBlock)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
PoRegisterDeviceNotify(OUT PVOID Unknown0,
                       IN ULONG Unknown1,
                       IN ULONG Unknown2,
                       IN ULONG Unknown3,
                       IN PVOID Unknown4,
                       IN PVOID Unknown5)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
VOID
NTAPI
PoShutdownBugCheck(IN BOOLEAN LogError,
                   IN ULONG BugCheckCode,
                   IN ULONG_PTR BugCheckParameter1,
                   IN ULONG_PTR BugCheckParameter2,
                   IN ULONG_PTR BugCheckParameter3,
                   IN ULONG_PTR BugCheckParameter4)
{
    DPRINT1("PoShutdownBugCheck called\n");

    /* FIXME: Log error if requested */
    /* FIXME: Initiate a shutdown */

    /* Bugcheck the system */
    KeBugCheckEx(BugCheckCode,
                 BugCheckParameter1,
                 BugCheckParameter2,
                 BugCheckParameter3,
                 BugCheckParameter4);
}

/*
 * @unimplemented
 */
VOID
NTAPI
PoSetHiberRange(IN PVOID HiberContext,
                IN ULONG Flags,
                IN OUT PVOID StartPage,
                IN ULONG Length,
                IN ULONG PageTag)
{
    UNIMPLEMENTED;
    return;
}

/*
 * @implemented
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
NTAPI
PoCallDriver(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ __drv_aliasesMem PIRP Irp)
{
    PIO_STACK_LOCATION NextStack;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    ASSERT(DeviceObject);
    ASSERT(Irp);

    NextStack = IoGetNextIrpStackLocation(Irp);
    ASSERT(NextStack->MajorFunction == IRP_MJ_POWER);

    /* Set DeviceObject for PopPresentIrp */
    NextStack->DeviceObject = DeviceObject;

    /* Only QUERY_POWER and SET_POWER use special handling */
    if (NextStack->MinorFunction != IRP_MN_SET_POWER &&
        NextStack->MinorFunction != IRP_MN_QUERY_POWER)
    {
        return IoCallDriver(DeviceObject, Irp);
    }

    /* Call the next driver, either directly or at PASSIVE_LEVEL */
    return PopPresentIrp(NextStack, Irp);
}

/*
 * @unimplemented
 */
PULONG
NTAPI
PoRegisterDeviceForIdleDetection(IN PDEVICE_OBJECT DeviceObject,
                                 IN ULONG ConservationIdleTime,
                                 IN ULONG PerformanceIdleTime,
                                 IN DEVICE_POWER_STATE State)
{
    UNIMPLEMENTED;
    return NULL;
}

/*
 * @unimplemented
 */
PVOID
NTAPI
PoRegisterSystemState(IN PVOID StateHandle,
                      IN EXECUTION_STATE Flags)
{
    UNIMPLEMENTED;
    return NULL;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
PoRequestPowerIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ UCHAR MinorFunction,
    _In_ POWER_STATE PowerState,
    _In_opt_ PREQUEST_POWER_COMPLETE CompletionFunction,
    _In_opt_ __drv_aliasesMem PVOID Context,
    _Outptr_opt_ PIRP *pIrp)
{
    PDEVICE_OBJECT TopDeviceObject;
    PIO_STACK_LOCATION Stack;
    PIRP Irp;

    if (MinorFunction != IRP_MN_QUERY_POWER
        && MinorFunction != IRP_MN_SET_POWER
        && MinorFunction != IRP_MN_WAIT_WAKE)
        return STATUS_INVALID_PARAMETER_2;

    /* Always call the top of the device stack */
    TopDeviceObject = IoGetAttachedDeviceReference(DeviceObject);

    Irp = IoAllocateIrp(TopDeviceObject->StackSize + 2, FALSE);
    if (!Irp)
    {
        ObDereferenceObject(TopDeviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;

    IoSetNextIrpStackLocation(Irp);

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->Parameters.Others.Argument1 = DeviceObject;
    Stack->Parameters.Others.Argument2 = (PVOID)(ULONG_PTR)MinorFunction;
    Stack->Parameters.Others.Argument3 = (PVOID)(ULONG_PTR)PowerState.DeviceState;
    Stack->Parameters.Others.Argument4 = Context;
    Stack->DeviceObject = TopDeviceObject;
    IoSetNextIrpStackLocation(Irp);

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_POWER;
    Stack->MinorFunction = MinorFunction;
    if (MinorFunction == IRP_MN_WAIT_WAKE)
    {
        Stack->Parameters.WaitWake.PowerState = PowerState.SystemState;
    }
    else
    {
        Stack->Parameters.Power.Type = DevicePowerState;
        Stack->Parameters.Power.State = PowerState;
    }

    if (pIrp != NULL)
        *pIrp = Irp;

    IoSetCompletionRoutine(Irp, PopRequestPowerIrpCompletion, CompletionFunction, TRUE, TRUE, TRUE);
    PoCallDriver(TopDeviceObject, Irp);

    /* Always return STATUS_PENDING. The completion routine
     * will call CompletionFunction and complete the Irp.
     */
    return STATUS_PENDING;
}

/*
 * @unimplemented
 */
POWER_STATE
NTAPI
PoSetPowerState(IN PDEVICE_OBJECT DeviceObject,
                IN POWER_STATE_TYPE Type,
                IN POWER_STATE State)
{
    POWER_STATE ps;

    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    ps.SystemState = PowerSystemWorking;  // Fully on
    ps.DeviceState = PowerDeviceD0;       // Fully on

    return ps;
}

/*
 * @unimplemented
 */
VOID
NTAPI
PoSetSystemState(IN EXECUTION_STATE Flags)
{
    UNIMPLEMENTED;
}

/*
 * @unimplemented
 */
VOID
NTAPI
PoStartNextPowerIrp(IN PIRP Irp)
{
    UNIMPLEMENTED_ONCE;
}

/*
 * @unimplemented
 */
VOID
NTAPI
PoUnregisterSystemState(IN PVOID StateHandle)
{
    UNIMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
NtInitiatePowerAction(IN POWER_ACTION SystemAction,
                      IN SYSTEM_POWER_STATE MinSystemState,
                      IN ULONG Flags,
                      IN BOOLEAN Asynchronous)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

static
NTSTATUS
PopQueryBatteryState(
    _Out_ PSYSTEM_BATTERY_STATE BatteryState)
{
    static const UNICODE_STRING CompBattDeviceName =
        RTL_CONSTANT_STRING(L"\\Device\\CompositeBattery");
    OBJECT_ATTRIBUTES ObjectAttributes = RTL_CONSTANT_OBJECT_ATTRIBUTES(
        &CompBattDeviceName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE);
    HANDLE CompBattHandle;
    IO_STATUS_BLOCK IoStatusBlock;
    BATTERY_QUERY_INFORMATION BatteryQueryInfo = {0};
    BATTERY_INFORMATION BatteryInfo = { 0 };
    BATTERY_STATUS BatteryStatus;
    NTSTATUS Status;

    /* Open the CompositeBattery device */
    Status = ZwOpenFile(&CompBattHandle,
                        GENERIC_READ | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to open CompositeBattery device: Status: 0x%08lX\n", Status);
        return Status;
    }

    /* Query the battery tag */
    ULONG Wait = 0;
    Status = ZwDeviceIoControlFile(CompBattHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_BATTERY_QUERY_TAG,
                                   &Wait,
                                   sizeof(Wait),
                                   &BatteryQueryInfo.BatteryTag,
                                   sizeof(BatteryQueryInfo.BatteryTag));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to query battery tag: Status: 0x%08lX\n", Status);
        goto Exit;
    }

    /* Query the battery information */
    BatteryQueryInfo.InformationLevel = BatteryInformation;
    Status = ZwDeviceIoControlFile(CompBattHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_BATTERY_QUERY_INFORMATION,
                                   &BatteryQueryInfo,
                                   sizeof(BatteryQueryInfo),
                                   &BatteryInfo,
                                   sizeof(BatteryInfo));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to query battery information: Status: 0x%08lX\n", Status);
        goto Exit;
    }

    /* Query the battery status */
    BATTERY_WAIT_STATUS BatteryWait = {0};
    BatteryWait.BatteryTag = BatteryQueryInfo.BatteryTag;
    BatteryWait.HighCapacity = MAXULONG;
    Status = ZwDeviceIoControlFile(CompBattHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_BATTERY_QUERY_STATUS,
                                   &BatteryWait,
                                   sizeof(BatteryWait),
                                   &BatteryStatus,
                                   sizeof(BatteryStatus));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to query battery status: Status: 0x%08lX\n", Status);
        goto Exit;
    }

    RtlZeroMemory(BatteryState, sizeof(*BatteryState));
    BatteryState->BatteryPresent = TRUE;
    BatteryState->AcOnLine = BooleanFlagOn(BatteryStatus.PowerState, BATTERY_POWER_ON_LINE);
    BatteryState->Charging = BooleanFlagOn(BatteryStatus.PowerState, BATTERY_CHARGING);
    BatteryState->Discharging = BooleanFlagOn(BatteryStatus.PowerState, BATTERY_DISCHARGING);
    BatteryState->Tag = (BYTE)BatteryQueryInfo.BatteryTag;

    if (BatteryInfo.FullChargedCapacity != BATTERY_UNKNOWN_CAPACITY)
        BatteryState->MaxCapacity = BatteryInfo.FullChargedCapacity;

    if (BatteryStatus.Capacity != BATTERY_UNKNOWN_CAPACITY)
        BatteryState->RemainingCapacity = BatteryStatus.Capacity;

    if (BatteryStatus.Rate != (LONG)BATTERY_UNKNOWN_RATE)
        BatteryState->Rate = BatteryStatus.Rate;

    BatteryState->DefaultAlert1 = BatteryInfo.DefaultAlert1;
    BatteryState->DefaultAlert2 = BatteryInfo.DefaultAlert2;

    if (BatteryState->Discharging &&
        BatteryStatus.Rate != (LONG)BATTERY_UNKNOWN_RATE &&
        BatteryStatus.Rate < 0 &&
        BatteryStatus.Capacity != BATTERY_UNKNOWN_CAPACITY)
    {
        BatteryState->EstimatedTime =
            (ULONG)((3600ULL * BatteryStatus.Capacity) / (ULONG)(-BatteryStatus.Rate));
    }
    else
    {
        BatteryState->EstimatedTime = MAXULONG;
    }

Exit:
    ZwClose(CompBattHandle);
    return Status;
}
    

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
NtPowerInformation(IN POWER_INFORMATION_LEVEL PowerInformationLevel,
                   IN PVOID InputBuffer  OPTIONAL,
                   IN ULONG InputBufferLength,
                   OUT PVOID OutputBuffer  OPTIONAL,
                   IN ULONG OutputBufferLength)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();

    PAGED_CODE();

    DPRINT("NtPowerInformation(PowerInformationLevel 0x%x, InputBuffer 0x%p, "
           "InputBufferLength 0x%x, OutputBuffer 0x%p, OutputBufferLength 0x%x)\n",
           PowerInformationLevel,
           InputBuffer, InputBufferLength,
           OutputBuffer, OutputBufferLength);

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(InputBuffer, InputBufferLength, 1);
            ProbeForWrite(OutputBuffer, OutputBufferLength, sizeof(ULONG));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    switch (PowerInformationLevel)
    {
        case ProcessorStateHandler2:
        {
            if (PreviousMode != KernelMode)
                return STATUS_ACCESS_DENIED;
            if (!InputBuffer || OutputBuffer || OutputBufferLength != 0)
                return STATUS_INVALID_PARAMETER;
            Status = PopRegisterProcessorStateHandler2(InputBuffer, InputBufferLength);
            break;
        }

        case SystemBatteryState:
        {
            SYSTEM_BATTERY_STATE BatteryState;

            if (InputBuffer != NULL)
                return STATUS_INVALID_PARAMETER;
            if (OutputBufferLength < sizeof(SYSTEM_BATTERY_STATE))
                return STATUS_BUFFER_TOO_SMALL;

            if (PopCapabilities.SystemBatteriesPresent)
            {
                /* Open the battery driver and query the battery state */
                Status = PopQueryBatteryState(&BatteryState);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("Failed to query battery state: Status: 0x%08X\n", Status);
                    return Status;
                }
            }
            else
            {
                /* No batteries present, set defaults */
                RtlZeroMemory(&BatteryState, sizeof(BatteryState));
                BatteryState.AcOnLine = TRUE;
            }

            _SEH2_TRY
            {
                /* Copy the battery state to the output buffer */
                RtlCopyMemory(OutputBuffer, &BatteryState, sizeof(BatteryState));
                Status = STATUS_SUCCESS;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            break;
        }

        case SystemPowerCapabilities:
        {
            PSYSTEM_POWER_CAPABILITIES PowerCapabilities = (PSYSTEM_POWER_CAPABILITIES)OutputBuffer;

            if (InputBuffer != NULL)
                return STATUS_INVALID_PARAMETER;
            if (OutputBufferLength < sizeof(SYSTEM_POWER_CAPABILITIES))
                return STATUS_BUFFER_TOO_SMALL;

            _SEH2_TRY
            {
                RtlCopyMemory(PowerCapabilities,
                              &PopCapabilities,
                              sizeof(SYSTEM_POWER_CAPABILITIES));

                Status = STATUS_SUCCESS;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            break;
        }

        case ProcessorInformation:
        {
            PPROCESSOR_POWER_INFORMATION PowerInformation = (PPROCESSOR_POWER_INFORMATION)OutputBuffer;
            ULONG ProcessorCount, i;
            ULONG CurrentSpeed[MAXIMUM_PROCESSORS];
            ULONG MaxSpeed[MAXIMUM_PROCESSORS];

            if (InputBuffer != NULL)
                return STATUS_INVALID_PARAMETER;
            if (OutputBufferLength < sizeof(PROCESSOR_POWER_INFORMATION))
                return STATUS_BUFFER_TOO_SMALL;

            /* Return an entry for each processor the output buffer can hold */
            ProcessorCount = min((ULONG)KeNumberProcessors,
                                 OutputBufferLength / sizeof(PROCESSOR_POWER_INFORMATION));

            for (i = 0; i < ProcessorCount; i++)
            {
#if defined(_M_ARM64)
                KeSetSystemAffinityThread(AFFINITY_MASK(i));
                CurrentSpeed[i] = KiArm64QueryEffectiveClockMHz(i);
                MaxSpeed[i] = KiArm64GetProcessorClockMHz(i);
#elif defined(_M_AMD64)
                KeSetSystemAffinityThread(AFFINITY_MASK(i));
                CurrentSpeed[i] = KiAmd64QueryEffectiveMHz(i);
                MaxSpeed[i] = KiProcessorBlock[i]->MHz;
#else
                CurrentSpeed[i] = KiProcessorBlock[i]->MHz;
                MaxSpeed[i] = CurrentSpeed[i];
#endif
            }
#if defined(_M_ARM64) || defined(_M_AMD64)
            if (ProcessorCount != 0)
            {
                KeRevertToUserAffinityThread();
            }
#endif

            _SEH2_TRY
            {
                for (i = 0; i < ProcessorCount; i++)
                {
                    PPROCESSOR_POWER_STATE ProcessorState = &KiProcessorBlock[i]->PowerState;

                    PowerInformation[i].Number = i;
                    PowerInformation[i].MaxMhz = MaxSpeed[i];
                    PowerInformation[i].CurrentMhz = CurrentSpeed[i];
                    PowerInformation[i].MhzLimit = (ULONG)(((ULONGLONG)MaxSpeed[i] * min(ProcessorState->ProcessorMaxThrottle, ProcessorState->ThermalThrottleLimit)) / 100);
                    PowerInformation[i].MaxIdleState = ProcessorState->IdleHandlersCount;
                    PowerInformation[i].CurrentIdleState = ProcessorState->IdleHandlersCount ? (ULONG)(ULONG_PTR)ProcessorState->IdleState + 1 : 0;
                }

                Status = STATUS_SUCCESS;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            break;
        }

        case PlatformInformation:
        {
            PPOWER_PLATFORM_INFORMATION PlatformInfo = (PPOWER_PLATFORM_INFORMATION)OutputBuffer;

            if (InputBuffer != NULL)
                return STATUS_INVALID_PARAMETER;
            if (OutputBufferLength < sizeof(POWER_PLATFORM_INFORMATION))
                return STATUS_BUFFER_TOO_SMALL;

            _SEH2_TRY
            {
                /* No Modern Standby (AoAc) support */
                PlatformInfo->AoAc = FALSE;
                Status = STATUS_SUCCESS;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            break;
        }

        default:
            Status = STATUS_NOT_IMPLEMENTED;
            DPRINT1("PowerInformationLevel 0x%x is UNIMPLEMENTED! Have a nice day.\n",
                    PowerInformationLevel);
            break;
    }

    return Status;
}

NTSTATUS
NTAPI
NtGetDevicePowerState(IN HANDLE Device,
                      IN PDEVICE_POWER_STATE PowerState)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

BOOLEAN
NTAPI
NtIsSystemResumeAutomatic(VOID)
{
    UNIMPLEMENTED;
    return FALSE;
}

NTSTATUS
NTAPI
NtRequestWakeupLatency(IN LATENCY_TIME Latency)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtSetThreadExecutionState(IN EXECUTION_STATE esFlags,
                          OUT EXECUTION_STATE *PreviousFlags)
{
    PKTHREAD Thread = KeGetCurrentThread();
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    EXECUTION_STATE PreviousState;
    PAGED_CODE();

    /* Validate flags */
    if (esFlags & ~(ES_CONTINUOUS | ES_USER_PRESENT))
    {
        /* Fail the request */
        return STATUS_INVALID_PARAMETER;
    }

    /* Check for user parameters */
    if (PreviousMode != KernelMode)
    {
        /* Protect the probes */
        _SEH2_TRY
        {
            /* Check if the pointer is valid */
            ProbeForWriteUlong(PreviousFlags);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* It isn't -- fail */
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    /* Save the previous state, always masking in the continous flag */
#if (NTDDI_VERSION >= NTDDI_WIN7) || defined(_M_ARM64)
    /* PowerState was renamed to LargeStack at Win7 (same byte overlay) */
    PreviousState = Thread->LargeStack | ES_CONTINUOUS;
    if (esFlags & ES_CONTINUOUS) Thread->LargeStack = (UCHAR)esFlags;
#else
    PreviousState = Thread->PowerState | ES_CONTINUOUS;

    /* Check if we need to update the power state */
    if (esFlags & ES_CONTINUOUS) Thread->PowerState = (UCHAR)esFlags;
#endif

    /* Protect the write back to user mode */
    _SEH2_TRY
    {
        /* Return the previous flags */
        *PreviousFlags = PreviousState;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        /* Something's wrong, fail */
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    /* All is good */
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtSetSystemPowerState(IN POWER_ACTION SystemAction,
                      IN SYSTEM_POWER_STATE MinSystemState,
                      IN ULONG Flags)
{
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    POP_POWER_ACTION Action = {0};
    NTSTATUS Status;
    ULONG Dummy;

    /* Check for invalid parameter combinations */
    if ((MinSystemState >= PowerSystemMaximum) ||
        (MinSystemState <= PowerSystemUnspecified) ||
        (SystemAction > PowerActionWarmEject) ||
        (SystemAction < PowerActionReserved) ||
        (Flags & ~(POWER_ACTION_QUERY_ALLOWED  |
                   POWER_ACTION_UI_ALLOWED     |
                   POWER_ACTION_OVERRIDE_APPS  |
                   POWER_ACTION_LIGHTEST_FIRST |
                   POWER_ACTION_LOCK_CONSOLE   |
                   POWER_ACTION_DISABLE_WAKES  |
                   POWER_ACTION_CRITICAL)))
    {
        DPRINT1("NtSetSystemPowerState: Bad parameters!\n");
        DPRINT1("                       SystemAction: 0x%x\n", SystemAction);
        DPRINT1("                       MinSystemState: 0x%x\n", MinSystemState);
        DPRINT1("                       Flags: 0x%x\n", Flags);
        return STATUS_INVALID_PARAMETER;
    }

    /* Check for user caller */
    if (PreviousMode != KernelMode)
    {
        /* Check for shutdown permission */
        if (!SeSinglePrivilegeCheck(SeShutdownPrivilege, PreviousMode))
        {
            /* Not granted */
            DPRINT1("ERROR: Privilege not held for shutdown\n");
            return STATUS_PRIVILEGE_NOT_HELD;
        }

        /* Do it as a kernel-mode caller for consistency with system state */
        return ZwSetSystemPowerState(SystemAction, MinSystemState, Flags);
    }

    /* Read policy settings (partial shutdown vs. full shutdown) */
    if (SystemAction == PowerActionShutdown) PopReadShutdownPolicy();

    /* Disable lazy flushing of registry */
    DPRINT("Stopping lazy flush\n");
    CmSetLazyFlushState(FALSE);

    /* Setup the power action */
    Action.Action = SystemAction;
    Action.Flags = Flags;

    /* Notify callbacks */
    DPRINT("Notifying callbacks\n");
    ExNotifyCallback(PowerStateCallback, (PVOID)3, NULL);

    /* Swap in any worker thread stacks */
    DPRINT("Swapping worker threads\n");
    ExSwapinWorkerThreads(FALSE);

    /* Make our action global */
    PopAction = Action;

    /* Start power loop */
    Status = STATUS_CANCELLED;
    while (TRUE)
    {
        /* Break out if there's nothing to do */
        if (Action.Action == PowerActionNone) break;

        /* Check for first-pass or restart */
        if (Status == STATUS_CANCELLED)
        {
            /* Check for shutdown action */
            if ((PopAction.Action == PowerActionShutdown) ||
                (PopAction.Action == PowerActionShutdownReset) ||
                (PopAction.Action == PowerActionShutdownOff))
            {
                /* Set the action */
                PopAction.Shutdown = TRUE;
            }

            /* Now we are good to go */
            Status = STATUS_SUCCESS;
        }

        /* Check if we're still in an invalid status */
        if (!NT_SUCCESS(Status)) break;

        /* Flush all volumes and the registry */
        DPRINT("Flushing volumes\n");
        PopFlushVolumes(PopAction.Shutdown);

#ifndef NEWCC
        /* Flush dirty cache pages */
        /* XXX: Is that still mandatory? As now we'll wait on lazy writer to complete? */
        CcRosFlushDirtyPages(MAXULONG, &Dummy, TRUE, FALSE);
        DPRINT("Cache flushed %lu pages\n", Dummy);
#else
        Dummy = 0;
#endif

        /* Set IRP for drivers */
        PopAction.IrpMinor = IRP_MN_SET_POWER;
        if (PopAction.Shutdown)
        {
            DPRINT("Queueing shutdown thread\n");
            /* Check if we are running in the system context */
            if (PsGetCurrentProcess() != PsInitialSystemProcess)
            {
                /* We're not, so use a worker thread for shutdown */
                ExInitializeWorkItem(&PopShutdownWorkItem,
                                     &PopGracefulShutdown,
                                     NULL);

                ExQueueWorkItem(&PopShutdownWorkItem, CriticalWorkQueue);

                /* Spend us -- when we wake up, the system is good to go down */
                KeSuspendThread(KeGetCurrentThread());
                Status = STATUS_SYSTEM_SHUTDOWN;
                goto Exit;

            }
            else
            {
                /* Do the shutdown inline */
                PopGracefulShutdown(NULL);
            }
        }

        /* You should not have made it this far */
        // ASSERTMSG("System is still up and running?!\n", FALSE);
        DPRINT1("System is still up and running, you may not have chosen a yet supported power option: %u\n", PopAction.Action);
        break;
    }

Exit:
    /* We're done, return */
    return Status;
}
