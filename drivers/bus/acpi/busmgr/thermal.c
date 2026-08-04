/*
 * PROJECT:     ReactOS ACPI bus driver
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ACPI thermal zones, active cooling, and processor throttling
 */

#include <precomp.h>

#define NDEBUG
#include <debug.h>

#define _COMPONENT ACPI_THERMAL_COMPONENT
ACPI_MODULE_NAME("acpi_thermal")

#define ACPI_THERMAL_TAG 'hTcA'
#define ACPI_THERMAL_ACTIVE_LEVELS 10
#define ACPI_THERMAL_DEFAULT_POLL_MS 5000
#define ACPI_THERMAL_MIN_POLL_MS 1000
#define ACPI_THERMAL_MAX_POLL_MS 30000
#define ACPI_THERMAL_PASSIVE_HYSTERESIS 20

typedef struct _ACPI_THERMAL_TRIP
{
    BOOLEAN Valid;
    UINT64 Temperature;
} ACPI_THERMAL_TRIP, *PACPI_THERMAL_TRIP;

typedef struct _ACPI_THERMAL_ZONE
{
    struct acpi_device *Device;
    ACPI_HANDLE Handle;
    KTIMER Timer;
    KDPC TimerDpc;
    EX_RUNDOWN_REF Rundown;
    KEVENT WorkIdleEvent;
    volatile LONG WorkCount;
    volatile LONG Removing;
    ULONG PollMilliseconds;
    ACPI_THERMAL_TRIP Critical;
    ACPI_THERMAL_TRIP Hot;
    ACPI_THERMAL_TRIP Passive;
    ACPI_THERMAL_TRIP Active[ACPI_THERMAL_ACTIVE_LEVELS];
    UINT64 LastTemperature;
    ULONG ActiveMask;
    BOOLEAN PassiveEngaged;
    BOOLEAN CriticalEngaged;
    BOOLEAN HotEngaged;
} ACPI_THERMAL_ZONE, *PACPI_THERMAL_ZONE;

typedef struct _ACPI_THERMAL_WORK
{
    WORK_QUEUE_ITEM WorkItem;
    PACPI_THERMAL_ZONE Zone;
    ULONG Reason;
} ACPI_THERMAL_WORK, *PACPI_THERMAL_WORK;

static int acpi_thermal_add(struct acpi_device *device);
static int acpi_thermal_remove(struct acpi_device *device, int type);

static struct acpi_driver acpi_thermal_driver = {
    {0, 0},
    ACPI_THERMAL_DRIVER_NAME,
    ACPI_THERMAL_CLASS,
    0,
    0,
    ACPI_THERMAL_HID,
    {acpi_thermal_add, acpi_thermal_remove}
};

static BOOLEAN
acpi_thermal_evaluate_integer(
    ACPI_HANDLE handle,
    const char *method,
    UINT64 *value)
{
    ACPI_BUFFER buffer = {ACPI_ALLOCATE_BUFFER, NULL};
    ACPI_OBJECT *object;
    ACPI_STATUS status;
    BOOLEAN valid = FALSE;

    status = AcpiEvaluateObject(handle, (ACPI_STRING)method, NULL, &buffer);
    if (ACPI_FAILURE(status))
    {
        if (buffer.Pointer)
            AcpiOsFree(buffer.Pointer);
        return FALSE;
    }
    object = buffer.Pointer;
    if (object && object->Type == ACPI_TYPE_INTEGER && object->Integer.Value != ACPI_UINT64_MAX)
    {
        *value = object->Integer.Value;
        valid = TRUE;
    }
    if (buffer.Pointer)
        AcpiOsFree(buffer.Pointer);
    return valid;
}

static VOID
acpi_thermal_query_trip(
    PACPI_THERMAL_ZONE zone,
    const char *method,
    PACPI_THERMAL_TRIP trip)
{
    trip->Valid = acpi_thermal_evaluate_integer(zone->Handle, method, &trip->Temperature);
}

static LONG
acpi_thermal_celsius_tenths(
    UINT64 temperature)
{
    if (temperature > LONG_MAX + 2732ULL)
        return LONG_MAX;
    return (LONG)temperature - 2732;
}

static ULONG
acpi_thermal_fraction(
    LONG temperature)
{
    LONG fraction = temperature % 10;

    return (ULONG)(fraction < 0 ? -fraction : fraction);
}

static VOID
acpi_thermal_refresh_trips(
    PACPI_THERMAL_ZONE zone)
{
    static const char *active_methods[ACPI_THERMAL_ACTIVE_LEVELS] =
    {
        "_AC0", "_AC1", "_AC2", "_AC3", "_AC4",
        "_AC5", "_AC6", "_AC7", "_AC8", "_AC9"
    };
    UINT64 polling;
    ULONG poll_milliseconds = ACPI_THERMAL_DEFAULT_POLL_MS;
    ULONG i;

    acpi_thermal_query_trip(zone, "_CRT", &zone->Critical);
    acpi_thermal_query_trip(zone, "_HOT", &zone->Hot);
    acpi_thermal_query_trip(zone, "_PSV", &zone->Passive);
    for (i = 0; i < ACPI_THERMAL_ACTIVE_LEVELS; i++)
        acpi_thermal_query_trip(zone, active_methods[i], &zone->Active[i]);

    if (acpi_thermal_evaluate_integer(zone->Handle, "_TZP", &polling) && polling != 0 && polling <= MAXULONG / 100)
        poll_milliseconds = (ULONG)polling * 100;
    if (zone->Passive.Valid && acpi_thermal_evaluate_integer(zone->Handle, "_TSP", &polling) && polling != 0 && polling <= MAXULONG / 100 && polling * 100 < poll_milliseconds)
        poll_milliseconds = (ULONG)polling * 100;
    if (poll_milliseconds < ACPI_THERMAL_MIN_POLL_MS)
        poll_milliseconds = ACPI_THERMAL_MIN_POLL_MS;
    if (poll_milliseconds > ACPI_THERMAL_MAX_POLL_MS)
        poll_milliseconds = ACPI_THERMAL_MAX_POLL_MS;
    zone->PollMilliseconds = poll_milliseconds;

    DPRINT1("ACPI: Thermal [%s] trips CRT=%s%ld.%luC HOT=%s%ld.%luC PSV=%s%ld.%luC poll=%lums\n",
            acpi_device_bid(zone->Device),
            zone->Critical.Valid ? "" : "?", acpi_thermal_celsius_tenths(zone->Critical.Temperature) / 10, acpi_thermal_fraction(acpi_thermal_celsius_tenths(zone->Critical.Temperature)),
            zone->Hot.Valid ? "" : "?", acpi_thermal_celsius_tenths(zone->Hot.Temperature) / 10, acpi_thermal_fraction(acpi_thermal_celsius_tenths(zone->Hot.Temperature)),
            zone->Passive.Valid ? "" : "?", acpi_thermal_celsius_tenths(zone->Passive.Temperature) / 10, acpi_thermal_fraction(acpi_thermal_celsius_tenths(zone->Passive.Temperature)),
            zone->PollMilliseconds);
}

static ACPI_STATUS
acpi_processor_get_throttle_control(
    ACPI_HANDLE handle,
    BOOLEAN minimum,
    UINT64 *control,
    ULONG *percentage)
{
    ACPI_BUFFER buffer = {ACPI_ALLOCATE_BUFFER, NULL};
    ACPI_OBJECT *package;
    ACPI_OBJECT *state;
    ACPI_STATUS status;
    UINT64 selected_control = 0;
    UINT64 selected_percentage = minimum ? ACPI_UINT64_MAX : 0;
    BOOLEAN found = FALSE;
    ULONG i;

    status = AcpiEvaluateObject(handle, "_TSS", NULL, &buffer);
    if (ACPI_FAILURE(status))
        goto Exit;
    package = buffer.Pointer;
    if (!package || package->Type != ACPI_TYPE_PACKAGE)
    {
        status = AE_BAD_DATA;
        goto Exit;
    }

    for (i = 0; i < package->Package.Count; i++)
    {
        state = &package->Package.Elements[i];
        if (state->Type != ACPI_TYPE_PACKAGE || state->Package.Count < 5 || state->Package.Elements[0].Type != ACPI_TYPE_INTEGER || state->Package.Elements[3].Type != ACPI_TYPE_INTEGER)
            continue;
        if (!found || (minimum ? state->Package.Elements[0].Integer.Value < selected_percentage : state->Package.Elements[0].Integer.Value > selected_percentage))
        {
            selected_percentage = state->Package.Elements[0].Integer.Value;
            selected_control = state->Package.Elements[3].Integer.Value;
            found = TRUE;
        }
    }
    if (!found || selected_percentage > 100)
    {
        status = AE_BAD_DATA;
        goto Exit;
    }
    *control = selected_control;
    *percentage = (ULONG)selected_percentage;
    status = AE_OK;

Exit:
    if (buffer.Pointer)
        AcpiOsFree(buffer.Pointer);
    return status;
}

static ACPI_STATUS
acpi_processor_get_ptc_register(
    ACPI_HANDLE handle,
    ACPI_GENERIC_ADDRESS *control_register)
{
    ACPI_BUFFER buffer = {ACPI_ALLOCATE_BUFFER, NULL};
    ACPI_RESOURCE *resource = NULL;
    ACPI_OBJECT *package;
    ACPI_OBJECT *object;
    ACPI_STATUS status;

    status = AcpiEvaluateObject(handle, "_PTC", NULL, &buffer);
    if (ACPI_FAILURE(status))
        goto Exit;
    package = buffer.Pointer;
    if (!package || package->Type != ACPI_TYPE_PACKAGE || package->Package.Count < 2)
    {
        status = AE_BAD_DATA;
        goto Exit;
    }
    object = &package->Package.Elements[0];
    if (object->Type != ACPI_TYPE_BUFFER || object->Buffer.Length > MAXUSHORT)
    {
        status = AE_BAD_DATA;
        goto Exit;
    }
    status = AcpiBufferToResource(object->Buffer.Pointer, (UINT16)object->Buffer.Length, &resource);
    if (ACPI_FAILURE(status))
        goto Exit;
    if (!resource || resource->Type != ACPI_RESOURCE_TYPE_GENERIC_REGISTER)
    {
        status = AE_BAD_DATA;
        goto Exit;
    }

    control_register->SpaceId = resource->Data.GenericReg.SpaceId;
    control_register->BitWidth = resource->Data.GenericReg.BitWidth;
    control_register->BitOffset = resource->Data.GenericReg.BitOffset;
    control_register->AccessWidth = resource->Data.GenericReg.AccessSize;
    control_register->Address = resource->Data.GenericReg.Address;
    status = AE_OK;

Exit:
    if (resource)
        AcpiOsFree(resource);
    if (buffer.Pointer)
        AcpiOsFree(buffer.Pointer);
    return status;
}

int
acpi_processor_set_thermal_limit(
    ACPI_HANDLE handle,
    int type)
{
    ACPI_GENERIC_ADDRESS control_register;
    ACPI_STATUS status;
    UINT64 control;
    ULONG percentage;
    BOOLEAN minimum = type != ACPI_PROCESSOR_LIMIT_NONE;

    RtlZeroMemory(&control_register, sizeof(control_register));
    status = acpi_processor_get_throttle_control(handle, minimum, &control, &percentage);
    if (ACPI_FAILURE(status))
        return_VALUE(-1);
    status = acpi_processor_get_ptc_register(handle, &control_register);
    if (ACPI_FAILURE(status))
        return_VALUE(-1);
    status = AcpiWrite(control, &control_register);
    if (ACPI_FAILURE(status))
    {
        DPRINT1("ACPI: Processor thermal throttle write failed: %s space=%u address=0x%I64x\n", AcpiFormatException(status), control_register.SpaceId, control_register.Address);
        return_VALUE(-1);
    }
    DPRINT1("ACPI: Processor thermal limit set to %lu%% control=0x%I64x\n", percentage, control);
    return_VALUE(0);
}

static ULONG
acpi_thermal_apply_processor_list(
    PACPI_THERMAL_ZONE zone,
    int limit)
{
    ACPI_BUFFER buffer = {ACPI_ALLOCATE_BUFFER, NULL};
    ACPI_OBJECT *package;
    ACPI_OBJECT *object;
    ACPI_STATUS status;
    ULONG applied = 0;
    ULONG i;

    status = AcpiEvaluateObject(zone->Handle, "_PSL", NULL, &buffer);
    if (ACPI_FAILURE(status))
        goto Exit;
    package = buffer.Pointer;
    if (!package || package->Type != ACPI_TYPE_PACKAGE)
        goto Exit;
    for (i = 0; i < package->Package.Count; i++)
    {
        object = &package->Package.Elements[i];
        if (object->Type == ACPI_TYPE_LOCAL_REFERENCE && object->Reference.Handle && acpi_processor_set_thermal_limit(object->Reference.Handle, limit) == 0)
            applied++;
    }

Exit:
    if (buffer.Pointer)
        AcpiOsFree(buffer.Pointer);
    return applied;
}

static ULONG
acpi_thermal_activate_level(
    PACPI_THERMAL_ZONE zone,
    ULONG level)
{
    static const char *active_lists[ACPI_THERMAL_ACTIVE_LEVELS] =
    {
        "_AL0", "_AL1", "_AL2", "_AL3", "_AL4",
        "_AL5", "_AL6", "_AL7", "_AL8", "_AL9"
    };
    ACPI_BUFFER buffer = {ACPI_ALLOCATE_BUFFER, NULL};
    ACPI_OBJECT *package;
    ACPI_OBJECT *object;
    ACPI_STATUS status;
    ULONG activated = 0;
    ULONG i;

    if (level >= ACPI_THERMAL_ACTIVE_LEVELS)
        return 0;
    status = AcpiEvaluateObject(zone->Handle, (ACPI_STRING)active_lists[level], NULL, &buffer);
    if (ACPI_FAILURE(status))
        goto Exit;
    package = buffer.Pointer;
    if (!package || package->Type != ACPI_TYPE_PACKAGE)
        goto Exit;
    for (i = 0; i < package->Package.Count; i++)
    {
        object = &package->Package.Elements[i];
        if (object->Type != ACPI_TYPE_LOCAL_REFERENCE || !object->Reference.Handle)
            continue;
        if (acpi_fan_force_maximum(object->Reference.Handle) == 0 || acpi_bus_set_power(object->Reference.Handle, ACPI_STATE_D0) == 0)
            activated++;
    }

Exit:
    if (buffer.Pointer)
        AcpiOsFree(buffer.Pointer);
    return activated;
}

static VOID
acpi_thermal_check(
    PACPI_THERMAL_ZONE zone,
    ULONG reason)
{
    UINT64 temperature;
    ULONG new_active_mask = 0;
    ULONG level;
    LONG celsius_tenths;

    if (!acpi_thermal_evaluate_integer(zone->Handle, "_TMP", &temperature))
    {
        DPRINT1("ACPI: Thermal [%s] _TMP evaluation failed\n", acpi_device_bid(zone->Device));
        acpi_fan_force_all_maximum();
        return;
    }
    celsius_tenths = acpi_thermal_celsius_tenths(temperature);
    if (reason != 0 || zone->LastTemperature == 0 || (temperature >= zone->LastTemperature && temperature - zone->LastTemperature >= 10) || (temperature < zone->LastTemperature && zone->LastTemperature - temperature >= 10))
        DPRINT1("ACPI: Thermal [%s] temperature %ld.%luC reason=0x%02lx\n", acpi_device_bid(zone->Device), celsius_tenths / 10, acpi_thermal_fraction(celsius_tenths), reason);
    zone->LastTemperature = temperature;

    if (zone->Critical.Valid && temperature >= zone->Critical.Temperature)
    {
        if (!zone->CriticalEngaged)
            DPRINT1("ACPI: Thermal [%s] CRITICAL trip reached; forcing maximum cooling\n", acpi_device_bid(zone->Device));
        zone->CriticalEngaged = TRUE;
        acpi_fan_force_all_maximum();
    }
    else
    {
        zone->CriticalEngaged = FALSE;
    }
    if (zone->Hot.Valid && temperature >= zone->Hot.Temperature)
    {
        if (!zone->HotEngaged)
            DPRINT1("ACPI: Thermal [%s] HOT trip reached; forcing maximum cooling\n", acpi_device_bid(zone->Device));
        zone->HotEngaged = TRUE;
        acpi_fan_force_all_maximum();
    }
    else
    {
        zone->HotEngaged = FALSE;
    }

    for (level = 0; level < ACPI_THERMAL_ACTIVE_LEVELS; level++)
    {
        if (!zone->Active[level].Valid || temperature < zone->Active[level].Temperature)
            continue;
        new_active_mask |= 1u << level;
        if (!(zone->ActiveMask & (1u << level)))
        {
            ULONG activated = acpi_thermal_activate_level(zone, level);
            DPRINT1("ACPI: Thermal [%s] active trip AC%lu reached; %lu cooling devices activated\n", acpi_device_bid(zone->Device), level, activated);
            if (activated == 0)
                acpi_fan_force_all_maximum();
        }
    }
    zone->ActiveMask = new_active_mask;

    if (zone->Passive.Valid && temperature >= zone->Passive.Temperature && !zone->PassiveEngaged)
    {
        ULONG throttled = acpi_thermal_apply_processor_list(zone, ACPI_PROCESSOR_LIMIT_DECREMENT);
        zone->PassiveEngaged = TRUE;
        DPRINT1("ACPI: Thermal [%s] passive trip reached; %lu processors throttled\n", acpi_device_bid(zone->Device), throttled);
        if (throttled == 0)
            acpi_fan_force_all_maximum();
    }
    else if (zone->PassiveEngaged && zone->Passive.Valid && temperature < zone->Passive.Temperature && zone->Passive.Temperature - temperature > ACPI_THERMAL_PASSIVE_HYSTERESIS)
    {
        ULONG restored = acpi_thermal_apply_processor_list(zone, ACPI_PROCESSOR_LIMIT_NONE);
        zone->PassiveEngaged = FALSE;
        DPRINT1("ACPI: Thermal [%s] passive trip cleared; %lu processors restored\n", acpi_device_bid(zone->Device), restored);
    }
}

static VOID
acpi_thermal_worker(
    PVOID context)
{
    PACPI_THERMAL_WORK work = context;
    PACPI_THERMAL_ZONE zone = work->Zone;

    if (!InterlockedCompareExchange(&zone->Removing, 0, 0))
    {
        if (work->Reason == ACPI_THERMAL_NOTIFY_THRESHOLDS || work->Reason == ACPI_THERMAL_NOTIFY_DEVICES)
            acpi_thermal_refresh_trips(zone);
        acpi_thermal_check(zone, work->Reason);
    }
    if (InterlockedDecrement(&zone->WorkCount) == 0)
        KeSetEvent(&zone->WorkIdleEvent, IO_NO_INCREMENT, FALSE);
    ExReleaseRundownProtection(&zone->Rundown);
    ExFreePoolWithTag(work, ACPI_THERMAL_TAG);
}

static VOID
acpi_thermal_queue_check(
    PACPI_THERMAL_ZONE zone,
    ULONG reason)
{
    PACPI_THERMAL_WORK work;

    if (!zone || !ExAcquireRundownProtection(&zone->Rundown))
        return;
    if (InterlockedCompareExchange(&zone->Removing, 0, 0))
    {
        ExReleaseRundownProtection(&zone->Rundown);
        return;
    }
    work = ExAllocatePoolWithTag(NonPagedPool, sizeof(*work), ACPI_THERMAL_TAG);
    if (!work)
    {
        acpi_fan_force_all_maximum();
        ExReleaseRundownProtection(&zone->Rundown);
        return;
    }
    work->Zone = zone;
    work->Reason = reason;
    ExInitializeWorkItem(&work->WorkItem, acpi_thermal_worker, work);
    if (InterlockedIncrement(&zone->WorkCount) == 1)
        KeClearEvent(&zone->WorkIdleEvent);
    ExQueueWorkItem(&work->WorkItem, DelayedWorkQueue);
}

static VOID
NTAPI
acpi_thermal_timer_dpc(
    PKDPC dpc,
    PVOID deferred_context,
    PVOID system_argument1,
    PVOID system_argument2)
{
    UNREFERENCED_PARAMETER(dpc);
    UNREFERENCED_PARAMETER(system_argument1);
    UNREFERENCED_PARAMETER(system_argument2);
    acpi_thermal_queue_check((PACPI_THERMAL_ZONE)deferred_context, 0);
}

static VOID
acpi_thermal_notify(
    ACPI_HANDLE handle,
    UINT32 event,
    PVOID context)
{
    PACPI_THERMAL_ZONE zone = context;

    UNREFERENCED_PARAMETER(handle);
    switch (event)
    {
        case ACPI_THERMAL_NOTIFY_TEMPERATURE:
        case ACPI_THERMAL_NOTIFY_THRESHOLDS:
        case ACPI_THERMAL_NOTIFY_DEVICES:
        case ACPI_THERMAL_NOTIFY_CRITICAL:
        case ACPI_THERMAL_NOTIFY_HOT:
            acpi_thermal_queue_check(zone, event);
            break;
        default:
            DPRINT1("ACPI: Thermal [%s] unsupported notification 0x%02x\n", acpi_device_bid(zone->Device), event);
            break;
    }
}

static int
acpi_thermal_add(
    struct acpi_device *device)
{
    PACPI_THERMAL_ZONE zone;
    LARGE_INTEGER due_time;
    ACPI_STATUS status;

    if (!device)
        return_VALUE(-1);
    zone = ExAllocatePoolWithTag(NonPagedPool, sizeof(*zone), ACPI_THERMAL_TAG);
    if (!zone)
        return_VALUE(-12);
    RtlZeroMemory(zone, sizeof(*zone));
    zone->Device = device;
    zone->Handle = device->handle;
    device->driver_data = zone;
    sprintf(acpi_device_name(device), "%s", ACPI_THERMAL_DEVICE_NAME);
    sprintf(acpi_device_class(device), "%s", ACPI_THERMAL_CLASS);
    ExInitializeRundownProtection(&zone->Rundown);
    KeInitializeEvent(&zone->WorkIdleEvent, NotificationEvent, TRUE);
    KeInitializeTimerEx(&zone->Timer, NotificationTimer);
    KeInitializeDpc(&zone->TimerDpc, acpi_thermal_timer_dpc, zone);
    acpi_thermal_refresh_trips(zone);

    status = AcpiInstallNotifyHandler(zone->Handle, ACPI_DEVICE_NOTIFY, acpi_thermal_notify, zone);
    if (ACPI_FAILURE(status))
    {
        device->driver_data = NULL;
        ExFreePoolWithTag(zone, ACPI_THERMAL_TAG);
        DPRINT1("ACPI: Thermal [%s] notify registration failed: %s\n", acpi_device_bid(device), AcpiFormatException(status));
        return_VALUE(-15);
    }

    due_time.QuadPart = -10000000LL;
    KeSetTimerEx(&zone->Timer, due_time, zone->PollMilliseconds, &zone->TimerDpc);
    return_VALUE(0);
}

static int
acpi_thermal_remove(
    struct acpi_device *device,
    int type)
{
    PACPI_THERMAL_ZONE zone;

    UNREFERENCED_PARAMETER(type);
    if (!device || !device->driver_data)
        return_VALUE(-1);
    zone = device->driver_data;
    InterlockedExchange(&zone->Removing, 1);
    KeCancelTimer(&zone->Timer);
    KeRemoveQueueDpc(&zone->TimerDpc);
    KeFlushQueuedDpcs();
    AcpiRemoveNotifyHandler(zone->Handle, ACPI_DEVICE_NOTIFY, acpi_thermal_notify);
    ExWaitForRundownProtectionRelease(&zone->Rundown);
    KeWaitForSingleObject(&zone->WorkIdleEvent, Executive, KernelMode, FALSE, NULL);
    device->driver_data = NULL;
    ExFreePoolWithTag(zone, ACPI_THERMAL_TAG);
    return_VALUE(0);
}

int
acpi_thermal_init(void)
{
    int result = acpi_bus_register_driver(&acpi_thermal_driver);

    return_VALUE(result < 0 ? -15 : 0);
}

void
acpi_thermal_exit(void)
{
    acpi_bus_unregister_driver(&acpi_thermal_driver);
}
