/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/po/events.c
 * PURPOSE:         Power Manager
 * PROGRAMMERS:     Hervé Poussineau (hpoussin@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

typedef struct _SYS_BUTTON_CONTEXT
{
    PDEVICE_OBJECT DeviceObject;
    PIO_WORKITEM WorkItem;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    ULONG SysButton;
} SYS_BUTTON_CONTEXT, *PSYS_BUTTON_CONTEXT;

typedef struct _POP_POWER_SETTING_CACHE_ENTRY
{
    LIST_ENTRY Link;
    GUID Guid;
    SYSTEM_POWER_CONDITION PowerCondition;
    ULONG ValueLength;
    UCHAR Value[ANYSIZE_ARRAY];
} POP_POWER_SETTING_CACHE_ENTRY, *PPOP_POWER_SETTING_CACHE_ENTRY;

typedef struct _POP_POWER_SETTING_SNAPSHOT
{
    GUID Guid;
    ULONG ValueLength;
    PUCHAR Value;
} POP_POWER_SETTING_SNAPSHOT, *PPOP_POWER_SETTING_SNAPSHOT;

typedef struct _POP_POWER_SETTING_REGISTRATION
{
    LIST_ENTRY Link;
    GUID Guid;
    PPOWER_SETTING_CALLBACK Callback;
    PVOID Context;
    volatile LONG References;
    volatile LONG Removed;
    KEVENT CallbackDrainEvent;
} POP_POWER_SETTING_REGISTRATION, *PPOP_POWER_SETTING_REGISTRATION;

#define POP_POWER_SETTING_TAG 'sPoP'
#define POP_POWER_CALLBACK_TAG 'cPoP'

static volatile LONG PopBatteryInterfaceCount;
static KSPIN_LOCK PopBatteryInterfaceLock;
static LIST_ENTRY PopPowerSettingCache;
static LIST_ENTRY PopPowerSettingCallbacks;
static KGUARDED_MUTEX PopPowerSettingLock;
static KMUTEX PopPowerSettingCallbackLock;
static PETHREAD PopPowerSettingCallbackThread;
static ULONG PopPowerSettingCallbackDepth;
static ULONG PopPowerSettingCacheCount;
static SYSTEM_POWER_CONDITION PopCurrentPowerCondition;

static VOID
NTAPI
PopGetSysButton(
    IN PDEVICE_OBJECT DeviceObject,
    IN PVOID Context);

PKWIN32_POWEREVENT_CALLOUT PopEventCallout;
extern PCALLBACK_OBJECT SetSystemTimeCallback;

/* FUNCTIONS *****************************************************************/

static PPOP_POWER_SETTING_CACHE_ENTRY
PopFindPowerSettingCacheEntry(
    _In_ LPCGUID SettingGuid,
    _In_ SYSTEM_POWER_CONDITION PowerCondition)
{
    PLIST_ENTRY Entry;

    for (Entry = PopPowerSettingCache.Flink; Entry != &PopPowerSettingCache; Entry = Entry->Flink)
    {
        PPOP_POWER_SETTING_CACHE_ENTRY CacheEntry = CONTAINING_RECORD(Entry, POP_POWER_SETTING_CACHE_ENTRY, Link);

        if (CacheEntry->PowerCondition == PowerCondition && IsEqualGUID(&CacheEntry->Guid, SettingGuid))
            return CacheEntry;
    }

    return NULL;
}

static PPOP_POWER_SETTING_CACHE_ENTRY
PopAllocatePowerSettingCacheEntry(
    _In_ LPCGUID SettingGuid,
    _In_ SYSTEM_POWER_CONDITION PowerCondition,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength)
{
    PPOP_POWER_SETTING_CACHE_ENTRY CacheEntry;
    SIZE_T AllocationSize;

    if (ValueLength > POP_MAX_POWER_SETTING_VALUE_LENGTH)
        return NULL;
    AllocationSize = FIELD_OFFSET(POP_POWER_SETTING_CACHE_ENTRY, Value) + ValueLength;
    CacheEntry = ExAllocatePoolWithTag(PagedPool, AllocationSize, POP_POWER_SETTING_TAG);
    if (!CacheEntry)
        return NULL;

    CacheEntry->Guid = *SettingGuid;
    CacheEntry->PowerCondition = PowerCondition;
    CacheEntry->ValueLength = ValueLength;
    if (ValueLength != 0)
        RtlCopyMemory(CacheEntry->Value, Value, ValueLength);
    return CacheEntry;
}

static VOID
PopDereferencePowerSettingRegistration(
    _In_ PPOP_POWER_SETTING_REGISTRATION Registration)
{
    LONG References;

    References = InterlockedDecrement(&Registration->References);
    if (Registration->Removed && References == 1)
        KeSetEvent(&Registration->CallbackDrainEvent, IO_NO_INCREMENT, FALSE);
    if (References == 0)
        ExFreePoolWithTag(Registration, POP_POWER_CALLBACK_TAG);
}

static VOID
PopInvokePowerSettingCallback(
    _In_ PPOP_POWER_SETTING_REGISTRATION Registration,
    _In_reads_bytes_opt_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength)
{
    if (!Registration->Removed)
    {
        if (PopPowerSettingCallbackDepth == 0)
            PopPowerSettingCallbackThread = PsGetCurrentThread();
        else
            ASSERT(PopPowerSettingCallbackThread == PsGetCurrentThread());
        PopPowerSettingCallbackDepth++;
        (VOID)Registration->Callback(&Registration->Guid, Value, ValueLength, Registration->Context);
        PopPowerSettingCallbackDepth--;
        if (PopPowerSettingCallbackDepth == 0)
            PopPowerSettingCallbackThread = NULL;
    }
}

static VOID
PopSeedPowerSetting(
    _In_ LPCGUID SettingGuid,
    _In_ SYSTEM_POWER_CONDITION PowerCondition,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength)
{
    PPOP_POWER_SETTING_CACHE_ENTRY CacheEntry;

    CacheEntry = PopAllocatePowerSettingCacheEntry(SettingGuid, PowerCondition, Value, ValueLength);
    if (CacheEntry)
    {
        InsertTailList(&PopPowerSettingCache, &CacheEntry->Link);
        PopPowerSettingCacheCount++;
    }
}

VOID
NTAPI
PopInitializeEventSupport(VOID)
{
    ULONG MinimumThrottle = 5;
    ULONG MaximumThrottle = 100;
    ULONG AutonomousMode = PROCESSOR_PERF_AUTONOMOUS_MODE_DISABLED;
    ULONG AcEnergyPreference = 33;
    ULONG DcEnergyPreference = 50;
    ULONG DisplayState = PowerMonitorOn;
    ULONG DiskIdleTimeout = MAXULONG;
    ULONG AcPowerSource = PoAc;
    ULONG DcPowerSource = PoDc;

    PopBatteryInterfaceCount = 0;
    KeInitializeSpinLock(&PopBatteryInterfaceLock);
    InitializeListHead(&PopPowerSettingCache);
    InitializeListHead(&PopPowerSettingCallbacks);
    KeInitializeGuardedMutex(&PopPowerSettingLock);
    KeInitializeMutex(&PopPowerSettingCallbackLock, 0);
    PopPowerSettingCallbackThread = NULL;
    PopPowerSettingCallbackDepth = 0;
    PopPowerSettingCacheCount = 0;
    PopCurrentPowerCondition = PoAc;
    PopSeedPowerSetting(&GUID_PROCESSOR_THROTTLE_MINIMUM, PoAc, &MinimumThrottle, sizeof(MinimumThrottle));
    PopSeedPowerSetting(&GUID_PROCESSOR_THROTTLE_MINIMUM, PoDc, &MinimumThrottle, sizeof(MinimumThrottle));
    PopSeedPowerSetting(&GUID_PROCESSOR_THROTTLE_MAXIMUM, PoAc, &MaximumThrottle, sizeof(MaximumThrottle));
    PopSeedPowerSetting(&GUID_PROCESSOR_THROTTLE_MAXIMUM, PoDc, &MaximumThrottle, sizeof(MaximumThrottle));
    PopSeedPowerSetting(&GUID_PROCESSOR_PERF_AUTONOMOUS_MODE, PoAc, &AutonomousMode, sizeof(AutonomousMode));
    PopSeedPowerSetting(&GUID_PROCESSOR_PERF_AUTONOMOUS_MODE, PoDc, &AutonomousMode, sizeof(AutonomousMode));
    PopSeedPowerSetting(&GUID_PROCESSOR_PERF_ENERGY_PERFORMANCE_PREFERENCE, PoAc, &AcEnergyPreference, sizeof(AcEnergyPreference));
    PopSeedPowerSetting(&GUID_PROCESSOR_PERF_ENERGY_PERFORMANCE_PREFERENCE, PoDc, &DcEnergyPreference, sizeof(DcEnergyPreference));
    PopSeedPowerSetting(&GUID_CONSOLE_DISPLAY_STATE, PoAc, &DisplayState, sizeof(DisplayState));
    PopSeedPowerSetting(&GUID_CONSOLE_DISPLAY_STATE, PoDc, &DisplayState, sizeof(DisplayState));
    PopSeedPowerSetting(&GUID_DISK_IDLE_TIMEOUT, PoAc, &DiskIdleTimeout, sizeof(DiskIdleTimeout));
    PopSeedPowerSetting(&GUID_DISK_IDLE_TIMEOUT, PoDc, &DiskIdleTimeout, sizeof(DiskIdleTimeout));
    PopSeedPowerSetting(&GUID_ACDC_POWER_SOURCE, PoAc, &AcPowerSource, sizeof(AcPowerSource));
    PopSeedPowerSetting(&GUID_ACDC_POWER_SOURCE, PoDc, &DcPowerSource, sizeof(DcPowerSource));
}

NTSTATUS
NTAPI
PoRegisterPowerSettingCallback(
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _In_ LPCGUID SettingGuid,
    _In_ PPOWER_SETTING_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Outptr_opt_ PVOID *Handle)
{
    PPOP_POWER_SETTING_REGISTRATION Registration;
    PPOP_POWER_SETTING_CACHE_ENTRY CacheEntry;
    PVOID InitialValue = NULL;
    ULONG InitialValueLength = 0;
    BOOLEAN InvokeInitialCallback = FALSE;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceObject);

    if (KeGetCurrentIrql() > APC_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    if (!SettingGuid || !Callback || !Handle)
        return STATUS_INVALID_PARAMETER;
    *Handle = NULL;

    Registration = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Registration), POP_POWER_CALLBACK_TAG);
    if (!Registration)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Registration, sizeof(*Registration));
    Registration->Guid = *SettingGuid;
    Registration->Callback = Callback;
    Registration->Context = Context;
    Registration->References = 1;
    KeInitializeEvent(&Registration->CallbackDrainEvent, NotificationEvent, FALSE);

    KeWaitForSingleObject(&PopPowerSettingCallbackLock, Executive, KernelMode, FALSE, NULL);
    KeAcquireGuardedMutex(&PopPowerSettingLock);
    CacheEntry = PopFindPowerSettingCacheEntry(SettingGuid, PopCurrentPowerCondition);
    if (CacheEntry)
    {
        InvokeInitialCallback = TRUE;
        if (CacheEntry->ValueLength != 0)
        {
            InitialValue = ExAllocatePoolWithTag(PagedPool, CacheEntry->ValueLength, POP_POWER_SETTING_TAG);
            if (!InitialValue)
            {
                KeReleaseGuardedMutex(&PopPowerSettingLock);
                KeReleaseMutex(&PopPowerSettingCallbackLock, FALSE);
                ExFreePoolWithTag(Registration, POP_POWER_CALLBACK_TAG);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            InitialValueLength = CacheEntry->ValueLength;
            RtlCopyMemory(InitialValue, CacheEntry->Value, InitialValueLength);
        }
    }
    InsertTailList(&PopPowerSettingCallbacks, &Registration->Link);
    *Handle = Registration;
    KeReleaseGuardedMutex(&PopPowerSettingLock);

    if (InvokeInitialCallback)
    {
        InterlockedIncrement(&Registration->References);
        PopInvokePowerSettingCallback(Registration, InitialValue, InitialValueLength);
        PopDereferencePowerSettingRegistration(Registration);
    }
    KeReleaseMutex(&PopPowerSettingCallbackLock, FALSE);

    if (InitialValue)
        ExFreePoolWithTag(InitialValue, POP_POWER_SETTING_TAG);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PoUnregisterPowerSettingCallback(
    _Inout_ PVOID Handle)
{
    PPOP_POWER_SETTING_REGISTRATION Registration = NULL;
    PPOP_POWER_SETTING_REGISTRATION Candidate;
    PLIST_ENTRY Entry;
    BOOLEAN CalledFromCallback;

    PAGED_CODE();

    if (KeGetCurrentIrql() > APC_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    if (!Handle)
        return STATUS_INVALID_PARAMETER;

    KeAcquireGuardedMutex(&PopPowerSettingLock);
    for (Entry = PopPowerSettingCallbacks.Flink; Entry != &PopPowerSettingCallbacks; Entry = Entry->Flink)
    {
        Candidate = CONTAINING_RECORD(Entry, POP_POWER_SETTING_REGISTRATION, Link);
        if (Candidate == Handle)
        {
            Registration = Candidate;
            break;
        }
    }
    if (!Registration)
    {
        KeReleaseGuardedMutex(&PopPowerSettingLock);
        return STATUS_INVALID_PARAMETER;
    }

    InterlockedIncrement(&Registration->References);
    RemoveEntryList(&Registration->Link);
    InterlockedExchange(&Registration->Removed, TRUE);
    CalledFromCallback = PopPowerSettingCallbackThread == PsGetCurrentThread();
    PopDereferencePowerSettingRegistration(Registration);
    KeReleaseGuardedMutex(&PopPowerSettingLock);

    if (!CalledFromCallback)
        KeWaitForSingleObject(&Registration->CallbackDrainEvent, Executive, KernelMode, FALSE, NULL);
    PopDereferencePowerSettingRegistration(Registration);
    return STATUS_SUCCESS;
}

static NTSTATUS
PopNotifyPowerSettingCallbacks(
    _In_ LPCGUID SettingGuid,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength)
{
    PPOP_POWER_SETTING_REGISTRATION Registration;
    PPOP_POWER_SETTING_REGISTRATION *Callbacks = NULL;
    PLIST_ENTRY Entry;
    ULONG CallbackCount = 0;
    ULONG CallbackIndex = 0;

    KeAcquireGuardedMutex(&PopPowerSettingLock);
    for (Entry = PopPowerSettingCallbacks.Flink; Entry != &PopPowerSettingCallbacks; Entry = Entry->Flink)
    {
        Registration = CONTAINING_RECORD(Entry, POP_POWER_SETTING_REGISTRATION, Link);
        if (IsEqualGUID(&Registration->Guid, SettingGuid))
            CallbackCount++;
    }
    KeReleaseGuardedMutex(&PopPowerSettingLock);

    if (CallbackCount != 0)
    {
        if ((SIZE_T)CallbackCount > MAXULONG_PTR / sizeof(*Callbacks))
            return STATUS_INSUFFICIENT_RESOURCES;
        Callbacks = ExAllocatePoolWithTag(PagedPool, CallbackCount * sizeof(*Callbacks), POP_POWER_CALLBACK_TAG);
        if (!Callbacks)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (Callbacks)
    {
        KeAcquireGuardedMutex(&PopPowerSettingLock);
        for (Entry = PopPowerSettingCallbacks.Flink; Entry != &PopPowerSettingCallbacks && CallbackIndex < CallbackCount; Entry = Entry->Flink)
        {
            Registration = CONTAINING_RECORD(Entry, POP_POWER_SETTING_REGISTRATION, Link);
            if (IsEqualGUID(&Registration->Guid, SettingGuid))
            {
                InterlockedIncrement(&Registration->References);
                Callbacks[CallbackIndex++] = Registration;
            }
        }
        KeReleaseGuardedMutex(&PopPowerSettingLock);
    }

    for (CallbackCount = 0; CallbackCount < CallbackIndex; CallbackCount++)
    {
        PopInvokePowerSettingCallback(Callbacks[CallbackCount], Value, ValueLength);
        PopDereferencePowerSettingRegistration(Callbacks[CallbackCount]);
    }

    if (Callbacks)
        ExFreePoolWithTag(Callbacks, POP_POWER_CALLBACK_TAG);
    return STATUS_SUCCESS;
}

static NTSTATUS
PopReapplyCurrentPowerSettings(VOID)
{
    PPOP_POWER_SETTING_SNAPSHOT Snapshots;
    PPOP_POWER_SETTING_CACHE_ENTRY CacheEntry;
    SYSTEM_POWER_CONDITION PowerCondition;
    PLIST_ENTRY Entry;
    SIZE_T AllocationSize = 0;
    PUCHAR ValueBuffer;
    ULONG SnapshotCount = 0;
    ULONG Index;
    NTSTATUS FirstFailure = STATUS_SUCCESS;
    NTSTATUS Status;

    KeAcquireGuardedMutex(&PopPowerSettingLock);
    PowerCondition = PopCurrentPowerCondition;
    for (Entry = PopPowerSettingCache.Flink; Entry != &PopPowerSettingCache; Entry = Entry->Flink)
    {
        CacheEntry = CONTAINING_RECORD(Entry, POP_POWER_SETTING_CACHE_ENTRY, Link);
        if (CacheEntry->PowerCondition != PowerCondition || IsEqualGUID(&CacheEntry->Guid, &GUID_ACDC_POWER_SOURCE))
            continue;
        AllocationSize += sizeof(*Snapshots) + ALIGN_UP_BY(CacheEntry->ValueLength, sizeof(PVOID));
        SnapshotCount++;
    }
    if (SnapshotCount == 0)
    {
        KeReleaseGuardedMutex(&PopPowerSettingLock);
        return STATUS_SUCCESS;
    }

    Snapshots = ExAllocatePoolWithTag(PagedPool, AllocationSize, POP_POWER_SETTING_TAG);
    if (!Snapshots)
    {
        KeReleaseGuardedMutex(&PopPowerSettingLock);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    ValueBuffer = (PUCHAR)&Snapshots[SnapshotCount];
    Index = 0;
    for (Entry = PopPowerSettingCache.Flink; Entry != &PopPowerSettingCache; Entry = Entry->Flink)
    {
        CacheEntry = CONTAINING_RECORD(Entry, POP_POWER_SETTING_CACHE_ENTRY, Link);
        if (CacheEntry->PowerCondition != PowerCondition || IsEqualGUID(&CacheEntry->Guid, &GUID_ACDC_POWER_SOURCE))
            continue;
        Snapshots[Index].Guid = CacheEntry->Guid;
        Snapshots[Index].ValueLength = CacheEntry->ValueLength;
        Snapshots[Index].Value = ValueBuffer;
        if (CacheEntry->ValueLength)
            RtlCopyMemory(ValueBuffer, CacheEntry->Value, CacheEntry->ValueLength);
        ValueBuffer += ALIGN_UP_BY(CacheEntry->ValueLength, sizeof(PVOID));
        Index++;
    }
    KeReleaseGuardedMutex(&PopPowerSettingLock);

    for (Index = 0; Index < SnapshotCount; Index++)
    {
        Status = PopApplyProcessorPowerSetting(&Snapshots[Index].Guid, Snapshots[Index].Value, Snapshots[Index].ValueLength);
        if (NT_SUCCESS(Status))
            Status = PopNotifyPowerSettingCallbacks(&Snapshots[Index].Guid, Snapshots[Index].Value, Snapshots[Index].ValueLength);
        if (!NT_SUCCESS(Status) && NT_SUCCESS(FirstFailure))
            FirstFailure = Status;
    }

    ExFreePoolWithTag(Snapshots, POP_POWER_SETTING_TAG);
    return FirstFailure;
}

NTSTATUS
NTAPI
PopSetPowerSettingValue(
    _In_ LPCGUID SettingGuid,
    _In_ SYSTEM_POWER_CONDITION PowerCondition,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength)
{
    PPOP_POWER_SETTING_CACHE_ENTRY NewCacheEntry;
    PPOP_POWER_SETTING_CACHE_ENTRY OldCacheEntry;
    SYSTEM_POWER_CONDITION CurrentPowerCondition;
    BOOLEAN ActiveSetting;
    BOOLEAN PowerSourceChanged;
    BOOLEAN PowerSourceSetting;
    NTSTATUS ReplayStatus;
    NTSTATUS Status;

    PAGED_CODE();

    if (!SettingGuid || (!Value && ValueLength != 0) || PowerCondition < PoAc || PowerCondition >= PoConditionMaximum)
        return STATUS_INVALID_PARAMETER;
    if (ValueLength > POP_MAX_POWER_SETTING_VALUE_LENGTH)
        return STATUS_INVALID_BUFFER_SIZE;
    if (IsEqualGUID(SettingGuid, &GUID_PROCESSOR_PERF_AUTONOMOUS_MODE))
    {
        if (ValueLength != sizeof(ULONG) || *(PULONG)Value > PROCESSOR_PERF_AUTONOMOUS_MODE_ENABLED)
            return STATUS_INVALID_PARAMETER;
    }
    else if (IsEqualGUID(SettingGuid, &GUID_PROCESSOR_PERF_ENERGY_PERFORMANCE_PREFERENCE))
    {
        if (ValueLength != sizeof(ULONG) || *(PULONG)Value > 100)
            return STATUS_INVALID_PARAMETER;
    }

    PowerSourceSetting = IsEqualGUID(SettingGuid, &GUID_ACDC_POWER_SOURCE);
    if (PowerSourceSetting && (ValueLength != sizeof(ULONG) || *(PULONG)Value >= PoConditionMaximum || PowerCondition != (SYSTEM_POWER_CONDITION)*(PULONG)Value))
        return STATUS_INVALID_PARAMETER;
    if (PowerSourceSetting && PopCurrentPowerCondition == PowerCondition)
        return STATUS_SUCCESS;

    NewCacheEntry = PopAllocatePowerSettingCacheEntry(SettingGuid, PowerCondition, Value, ValueLength);
    if (!NewCacheEntry)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeWaitForSingleObject(&PopPowerSettingCallbackLock, Executive, KernelMode, FALSE, NULL);
    KeAcquireGuardedMutex(&PopPowerSettingLock);
    OldCacheEntry = PopFindPowerSettingCacheEntry(SettingGuid, PowerCondition);
    if (!OldCacheEntry && PopPowerSettingCacheCount >= POP_MAX_CACHED_POWER_SETTINGS)
    {
        KeReleaseGuardedMutex(&PopPowerSettingLock);
        Status = STATUS_QUOTA_EXCEEDED;
        goto Failure;
    }
    CurrentPowerCondition = PowerSourceSetting ? (SYSTEM_POWER_CONDITION)*(PULONG)Value : PopCurrentPowerCondition;
    PowerSourceChanged = PowerSourceSetting && PopCurrentPowerCondition != CurrentPowerCondition;
    ActiveSetting = PowerSourceSetting ? PowerSourceChanged : PowerCondition == CurrentPowerCondition;
    KeReleaseGuardedMutex(&PopPowerSettingLock);

    if (ActiveSetting)
    {
        Status = PopApplyProcessorPowerSetting(SettingGuid, Value, ValueLength);
        if (!NT_SUCCESS(Status))
            goto Failure;
    }

    KeAcquireGuardedMutex(&PopPowerSettingLock);
    if (OldCacheEntry)
        RemoveEntryList(&OldCacheEntry->Link);
    else
        PopPowerSettingCacheCount++;
    InsertTailList(&PopPowerSettingCache, &NewCacheEntry->Link);
    if (PowerSourceSetting)
        PopCurrentPowerCondition = CurrentPowerCondition;
    KeReleaseGuardedMutex(&PopPowerSettingLock);

    Status = ActiveSetting ? PopNotifyPowerSettingCallbacks(SettingGuid, Value, ValueLength) : STATUS_SUCCESS;
    if (PowerSourceChanged)
    {
        ReplayStatus = PopReapplyCurrentPowerSettings();
        if (NT_SUCCESS(Status))
            Status = ReplayStatus;
    }
    KeReleaseMutex(&PopPowerSettingCallbackLock, FALSE);

    if (OldCacheEntry)
        ExFreePoolWithTag(OldCacheEntry, POP_POWER_SETTING_TAG);
    return Status;

Failure:
    KeReleaseMutex(&PopPowerSettingCallbackLock, FALSE);
    ExFreePoolWithTag(NewCacheEntry, POP_POWER_SETTING_TAG);
    return Status;
}

VOID
NTAPI
PoNotifySystemTimeSet(VOID)
{
    KIRQL OldIrql;

    /* Check if Win32k registered a notification callback */
    if (PopEventCallout)
    {
        /* Raise to dispatch */
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);

        /* Notify the callback */
        ExNotifyCallback(SetSystemTimeCallback, NULL, NULL);

        /* Lower IRQL back */
        KeLowerIrql(OldIrql);
    }
}

static NTSTATUS
NTAPI
PopGetSysButtonCompletion(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context)
{
    PSYS_BUTTON_CONTEXT SysButtonContext = Context;
    ULONG SysButton;

    /* The DeviceObject can be NULL, so use the one we stored */
    DeviceObject = SysButtonContext->DeviceObject;

    /* FIXME: What do do with the sys button event? */
    SysButton = *(PULONG)Irp->AssociatedIrp.SystemBuffer;
    {
        DPRINT1("A device reported the event 0x%x (", SysButton);
        if (SysButton & SYS_BUTTON_POWER) DbgPrint(" POWER");
        if (SysButton & SYS_BUTTON_SLEEP) DbgPrint(" SLEEP");
        if (SysButton & SYS_BUTTON_LID) DbgPrint(" LID");
        if (SysButton == 0) DbgPrint(" WAKE");
        DbgPrint(" )\n");

        if (SysButton & SYS_BUTTON_POWER)
        {
            /* FIXME: Read registry for the action we should perform here */
            DPRINT1("Initiating shutdown after power button event\n");

            ZwShutdownSystem(ShutdownNoReboot);
        }
    }

    /* Allocate a new workitem to send the next IOCTL_GET_SYS_BUTTON_EVENT */
    SysButtonContext->WorkItem = IoAllocateWorkItem(DeviceObject);
    if (!SysButtonContext->WorkItem)
    {
        DPRINT("IoAllocateWorkItem() failed\n");
        ExFreePoolWithTag(SysButtonContext, 'IWOP');
        return STATUS_SUCCESS;
    }
    IoQueueWorkItem(SysButtonContext->WorkItem,
                    PopGetSysButton,
                    DelayedWorkQueue,
                    SysButtonContext);

    return STATUS_SUCCESS /* STATUS_CONTINUE_COMPLETION */;
}

static VOID
NTAPI
PopGetSysButton(
    IN PDEVICE_OBJECT DeviceObject,
    IN PVOID Context)
{
    PSYS_BUTTON_CONTEXT SysButtonContext = Context;
    PIO_WORKITEM CurrentWorkItem = SysButtonContext->WorkItem;
    PIRP Irp;

    /* Get button pressed (IOCTL_GET_SYS_BUTTON_EVENT) */
    KeInitializeEvent(&SysButtonContext->Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_GET_SYS_BUTTON_EVENT,
                                        DeviceObject,
                                        NULL,
                                        0,
                                        &SysButtonContext->SysButton,
                                        sizeof(SysButtonContext->SysButton),
                                        FALSE,
                                        &SysButtonContext->Event,
                                        &SysButtonContext->IoStatusBlock);
    if (Irp)
    {
        IoSetCompletionRoutine(Irp,
                               PopGetSysButtonCompletion,
                               SysButtonContext,
                               TRUE,
                               FALSE,
                               FALSE);
        IoCallDriver(DeviceObject, Irp);
    }
    else
    {
        DPRINT1("IoBuildDeviceIoControlRequest() failed\n");
        ExFreePoolWithTag(SysButtonContext, 'IWOP');
    }

    IoFreeWorkItem(CurrentWorkItem);
}

NTSTATUS
NTAPI
PopAddRemoveSysCapsCallback(IN PVOID NotificationStructure,
                            IN PVOID Context)
{
    PDEVICE_INTERFACE_CHANGE_NOTIFICATION Notification;
    PSYS_BUTTON_CONTEXT SysButtonContext;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE FileHandle;
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    PIRP Irp;
    IO_STATUS_BLOCK IoStatusBlock;
    KEVENT Event;
    BOOLEAN Arrival;
    ULONG Caps;
    NTSTATUS Status;
    KIRQL OldIrql;
    POP_POLICY_DEVICE_TYPE DeviceType = (POP_POLICY_DEVICE_TYPE)(ULONG_PTR)Context;

    DPRINT("PopAddRemoveSysCapsCallback(%p %p)\n",
        NotificationStructure, Context);

    Notification = (PDEVICE_INTERFACE_CHANGE_NOTIFICATION)NotificationStructure;
    if (Notification->Version != 1)
        return STATUS_REVISION_MISMATCH;
    if (Notification->Size != sizeof(DEVICE_INTERFACE_CHANGE_NOTIFICATION))
        return STATUS_INVALID_PARAMETER;
    if (RtlCompareMemory(&Notification->Event, &GUID_DEVICE_INTERFACE_ARRIVAL, sizeof(GUID)) == sizeof(GUID))
        Arrival = TRUE;
    else if (RtlCompareMemory(&Notification->Event, &GUID_DEVICE_INTERFACE_REMOVAL, sizeof(GUID)) == sizeof(GUID))
        Arrival = FALSE;
    else
        return STATUS_INVALID_PARAMETER;

    if (DeviceType == PolicyDeviceBattery)
    {
        KeAcquireSpinLock(&PopBatteryInterfaceLock, &OldIrql);

        if (Arrival)
        {
            PopBatteryInterfaceCount++;
        }
        else if (PopBatteryInterfaceCount > 0)
        {
            PopBatteryInterfaceCount--;
        }

        PopCapabilities.SystemBatteriesPresent =
            (PopBatteryInterfaceCount != 0);

        KeReleaseSpinLock(&PopBatteryInterfaceLock, OldIrql);
        return STATUS_SUCCESS;
    }

    if (Arrival)
    {
        DPRINT("Arrival of %wZ\n", Notification->SymbolicLinkName);

        /* Open the device */
        InitializeObjectAttributes(&ObjectAttributes,
                                   Notification->SymbolicLinkName,
                                   OBJ_KERNEL_HANDLE,
                                   NULL,
                                   NULL);
        Status = ZwOpenFile(&FileHandle,
                            FILE_READ_DATA,
                            &ObjectAttributes,
                            &IoStatusBlock,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            0);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ZwOpenFile() failed with status 0x%08lx\n", Status);
            return Status;
        }
        Status = ObReferenceObjectByHandle(FileHandle,
                                           FILE_READ_DATA,
                                           IoFileObjectType,
                                           KernelMode,
                                           (PVOID*)&FileObject,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ObReferenceObjectByHandle() failed with status 0x%08lx\n", Status);
            ZwClose(FileHandle);
            return Status;
        }
        DeviceObject = IoGetRelatedDeviceObject(FileObject);
        ObDereferenceObject(FileObject);

        /* Get capabilities (IOCTL_GET_SYS_BUTTON_CAPS) */
        KeInitializeEvent(&Event, NotificationEvent, FALSE);
        Irp = IoBuildDeviceIoControlRequest(IOCTL_GET_SYS_BUTTON_CAPS,
                                            DeviceObject,
                                            NULL,
                                            0,
                                            &Caps,
                                            sizeof(Caps),
                                            FALSE,
                                            &Event,
                                            &IoStatusBlock);
        if (!Irp)
        {
            DPRINT1("IoBuildDeviceIoControlRequest() failed\n");
            ZwClose(FileHandle);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        Status = IoCallDriver(DeviceObject, Irp);
        if (Status == STATUS_PENDING)
        {
            DPRINT("IOCTL_GET_SYS_BUTTON_CAPS pending\n");
            KeWaitForSingleObject(&Event, Suspended, KernelMode, FALSE, NULL);
            Status = IoStatusBlock.Status;
        }
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Sending IOCTL_GET_SYS_BUTTON_CAPS failed with status 0x%08x\n", Status);
            ZwClose(FileHandle);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        DPRINT("Device capabilities: 0x%x\n", Caps);
        if (Caps & SYS_BUTTON_POWER)
        {
            DPRINT("POWER button present\n");
            PopCapabilities.PowerButtonPresent = TRUE;
        }

        if (Caps & SYS_BUTTON_SLEEP)
        {
            DPRINT("SLEEP button present\n");
            PopCapabilities.SleepButtonPresent = TRUE;
        }

        if (Caps & SYS_BUTTON_LID)
        {
            DPRINT("LID present\n");
            PopCapabilities.LidPresent = TRUE;
        }

        SysButtonContext = ExAllocatePoolWithTag(NonPagedPool,
                                                 sizeof(SYS_BUTTON_CONTEXT),
                                                 'IWOP');
        if (!SysButtonContext)
        {
            DPRINT1("ExAllocatePoolWithTag() failed\n");
            ZwClose(FileHandle);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        /* Queue a work item to get sys button event */
        SysButtonContext->WorkItem = IoAllocateWorkItem(DeviceObject);
        SysButtonContext->DeviceObject = DeviceObject;
        if (!SysButtonContext->WorkItem)
        {
            DPRINT1("IoAllocateWorkItem() failed\n");
            ZwClose(FileHandle);
            ExFreePoolWithTag(SysButtonContext, 'IWOP');
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        IoQueueWorkItem(SysButtonContext->WorkItem,
                        PopGetSysButton,
                        DelayedWorkQueue,
                        SysButtonContext);

        ZwClose(FileHandle);
        return STATUS_SUCCESS;
    }
    else
    {
        DPRINT1("Removal of a power capable device not implemented\n");
        return STATUS_NOT_IMPLEMENTED;
    }
}
