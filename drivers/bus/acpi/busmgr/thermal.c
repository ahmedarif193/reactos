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
#define ACPI_THERMAL_PASSIVE_HYSTERESIS 20
#define ACPI_THERMAL_MAX_COOLING_DEVICES 64

typedef struct _ACPI_THERMAL_TRIP
{
    BOOLEAN Valid;
    UINT64 Temperature;
} ACPI_THERMAL_TRIP, *PACPI_THERMAL_TRIP;

typedef struct _ACPI_THERMAL_PROCESSOR_REQUEST
{
    ACPI_HANDLE Handle;
    PVOID Request;
} ACPI_THERMAL_PROCESSOR_REQUEST, *PACPI_THERMAL_PROCESSOR_REQUEST;

typedef struct _ACPI_THERMAL_ZONE
{
    struct acpi_device *Device;
    ACPI_HANDLE Handle;
    EX_RUNDOWN_REF Rundown;
    FAST_MUTEX PolicyLock;
    KEVENT WorkIdleEvent;
    volatile LONG WorkCount;
    volatile LONG Removing;
    ACPI_THERMAL_TRIP Critical;
    ACPI_THERMAL_TRIP Hot;
    ACPI_THERMAL_TRIP Passive;
    ACPI_THERMAL_TRIP Active[ACPI_THERMAL_ACTIVE_LEVELS];
    ULONG ActiveMask;
    BOOLEAN PassiveEngaged;
    BOOLEAN PassiveCoolingFailed;
    BOOLEAN CriticalEngaged;
    BOOLEAN HotEngaged;
    ACPI_THERMAL_PROCESSOR_REQUEST ProcessorRequests[ACPI_THERMAL_MAX_COOLING_DEVICES];
    ULONG ProcessorRequestCount;
    ACPI_HANDLE CoolingDevices[ACPI_THERMAL_MAX_COOLING_DEVICES];
    ULONG CoolingDeviceCount;
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
acpi_thermal_set_active_cooling_policy(
    PACPI_THERMAL_ZONE zone)
{
    ACPI_OBJECT arguments[3];
    ACPI_OBJECT_LIST argument_list;
    ACPI_STATUS status;

    RtlZeroMemory(arguments, sizeof(arguments));
    arguments[0].Type = ACPI_TYPE_INTEGER;
    arguments[0].Integer.Value = ACPI_THERMAL_MODE_ACTIVE;
    arguments[1].Type = ACPI_TYPE_INTEGER;
    arguments[1].Integer.Value = 5;
    arguments[2].Type = ACPI_TYPE_INTEGER;
    arguments[2].Integer.Value = 5;
    argument_list.Count = 3;
    argument_list.Pointer = arguments;
    status = AcpiEvaluateObject(zone->Handle, "_SCP", &argument_list, NULL);
    if (status == AE_NOT_FOUND)
        return;
    if (ACPI_FAILURE(status)) {
        DPRINT1("ACPI: Thermal [%s] could not select active cooling policy: %s\n",
                acpi_device_bid(zone->Device), AcpiFormatException(status));
        return;
    }
    DPRINT1("ACPI: Thermal [%s] selected active cooling policy\n", acpi_device_bid(zone->Device));
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
    ULONG i;

    acpi_thermal_query_trip(zone, "_CRT", &zone->Critical);
    acpi_thermal_query_trip(zone, "_HOT", &zone->Hot);
    acpi_thermal_query_trip(zone, "_PSV", &zone->Passive);
    for (i = 0; i < ACPI_THERMAL_ACTIVE_LEVELS; i++)
        acpi_thermal_query_trip(zone, active_methods[i], &zone->Active[i]);

    DPRINT1("ACPI: Thermal [%s] trips CRT=%s%ld.%luC HOT=%s%ld.%luC PSV=%s%ld.%luC\n",
            acpi_device_bid(zone->Device),
            zone->Critical.Valid ? "" : "?", acpi_thermal_celsius_tenths(zone->Critical.Temperature) / 10, acpi_thermal_fraction(acpi_thermal_celsius_tenths(zone->Critical.Temperature)),
            zone->Hot.Valid ? "" : "?", acpi_thermal_celsius_tenths(zone->Hot.Temperature) / 10, acpi_thermal_fraction(acpi_thermal_celsius_tenths(zone->Hot.Temperature)),
            zone->Passive.Valid ? "" : "?", acpi_thermal_celsius_tenths(zone->Passive.Temperature) / 10, acpi_thermal_fraction(acpi_thermal_celsius_tenths(zone->Passive.Temperature)));
}

#if (NTDDI_VERSION >= NTDDI_WINTHRESHOLD)

static LONG
acpi_thermal_find_processor_request(
    PACPI_THERMAL_PROCESSOR_REQUEST requests,
    ULONG count,
    ACPI_HANDLE handle)
{
    ULONG i;

    for (i = 0; i < count; i++) {
        if (requests[i].Handle == handle)
            return (LONG)i;
    }
    return -1;
}

static VOID
acpi_thermal_release_processor_requests(
    PACPI_THERMAL_ZONE zone)
{
    ULONG i;

    for (i = 0; i < zone->ProcessorRequestCount; i++) {
        if (zone->ProcessorRequests[i].Request)
            PoDeleteThermalRequest(zone->ProcessorRequests[i].Request);
    }
    RtlZeroMemory(zone->ProcessorRequests, sizeof(zone->ProcessorRequests));
    zone->ProcessorRequestCount = 0;
}

static ULONG
acpi_thermal_apply_processor_list(
    PACPI_THERMAL_ZONE zone,
    int limit,
    PULONG targets)
{
    ACPI_BUFFER buffer = {ACPI_ALLOCATE_BUFFER, NULL};
    ACPI_THERMAL_PROCESSOR_REQUEST new_requests[ACPI_THERMAL_MAX_COOLING_DEVICES];
    COUNTED_REASON_CONTEXT reason_context;
    UNICODE_STRING reason_string;
    struct acpi_device *target_device;
    ACPI_OBJECT *package;
    ACPI_OBJECT *object;
    ACPI_STATUS status;
    NTSTATUS request_status;
    LONG old_index;
    ULONG applied = 0;
    ULONG count = 0;
    ULONG i;
    UCHAR throttle = limit == ACPI_PROCESSOR_LIMIT_NONE ? 100 : 0;

    if (targets)
        *targets = 0;

    RtlZeroMemory(new_requests, sizeof(new_requests));
    RtlZeroMemory(&reason_context, sizeof(reason_context));
    RtlInitUnicodeString(&reason_string, L"ACPI thermal zone passive cooling");
    reason_context.Version = POWER_REQUEST_CONTEXT_VERSION;
    reason_context.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    reason_context.SimpleString = reason_string;

    status = AcpiEvaluateObject(zone->Handle, "_PSL", NULL, &buffer);
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
        if (acpi_thermal_find_processor_request(new_requests, count, object->Reference.Handle) >= 0)
            continue;
        if (count >= ACPI_THERMAL_MAX_COOLING_DEVICES)
            break;
        if (targets)
            (*targets)++;
        new_requests[count].Handle = object->Reference.Handle;
        old_index = acpi_thermal_find_processor_request(zone->ProcessorRequests, zone->ProcessorRequestCount, object->Reference.Handle);
        if (old_index >= 0)
        {
            new_requests[count].Request = zone->ProcessorRequests[old_index].Request;
            zone->ProcessorRequests[old_index].Request = NULL;
        }
        else if (!acpi_bus_get_device(object->Reference.Handle, &target_device) && target_device->pdo && zone->Device->pdo)
        {
            request_status = PoCreateThermalRequest(&new_requests[count].Request, target_device->pdo, zone->Device->pdo, &reason_context, 0);
            if (NT_SUCCESS(request_status) && !PoGetThermalRequestSupport(new_requests[count].Request, PoThermalRequestPassive))
            {
                PoDeleteThermalRequest(new_requests[count].Request);
                new_requests[count].Request = NULL;
            }
        }
        if (new_requests[count].Request && NT_SUCCESS(PoSetThermalPassiveCooling(new_requests[count].Request, throttle)))
            applied++;
        count++;
    }

    for (i = 0; i < zone->ProcessorRequestCount; i++) {
        if (zone->ProcessorRequests[i].Request)
            PoDeleteThermalRequest(zone->ProcessorRequests[i].Request);
    }
    RtlCopyMemory(zone->ProcessorRequests, new_requests, count * sizeof(new_requests[0]));
    zone->ProcessorRequestCount = count;

Exit:
    if (buffer.Pointer)
        AcpiOsFree(buffer.Pointer);
    return applied;
}

#else

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

static int
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
        return -1;
    status = acpi_processor_get_ptc_register(handle, &control_register);
    if (ACPI_FAILURE(status))
        return -1;
    status = AcpiWrite(control, &control_register);
    if (ACPI_FAILURE(status))
    {
        DPRINT1("ACPI: Processor thermal throttle write failed: %s space=%u address=0x%I64x\n", AcpiFormatException(status), control_register.SpaceId, control_register.Address);
        return -1;
    }
    DPRINT1("ACPI: Processor thermal limit set to %lu%% control=0x%I64x\n", percentage, control);
    return 0;
}

static ULONG
acpi_thermal_apply_processor_list(
    PACPI_THERMAL_ZONE zone,
    int limit,
    PULONG targets)
{
    ACPI_BUFFER buffer = {ACPI_ALLOCATE_BUFFER, NULL};
    ACPI_OBJECT *package;
    ACPI_OBJECT *object;
    ACPI_STATUS status;
    ULONG applied = 0;
    ULONG i;

    if (targets)
        *targets = 0;

    status = AcpiEvaluateObject(zone->Handle, "_PSL", NULL, &buffer);
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
        if (targets)
            (*targets)++;
        if (acpi_processor_set_thermal_limit(object->Reference.Handle, limit) == 0)
            applied++;
    }

Exit:
    if (buffer.Pointer)
        AcpiOsFree(buffer.Pointer);
    return applied;
}

#endif /* NTDDI_VERSION >= NTDDI_WINTHRESHOLD */

static LONG
acpi_thermal_find_cooling_device(
    ACPI_HANDLE *handles,
    ULONG count,
    ACPI_HANDLE handle)
{
    ULONG i;

    for (i = 0; i < count; i++) {
        if (handles[i] == handle)
            return (LONG)i;
    }
    return -1;
}

static VOID
acpi_thermal_collect_level(
    PACPI_THERMAL_ZONE zone,
    ULONG level,
    BOOLEAN active,
    ACPI_HANDLE *handles,
    LONG *levels,
    PULONG count)
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
    LONG index;
    ULONG i;

    status = AcpiEvaluateObject(zone->Handle, (ACPI_STRING)active_lists[level], NULL, &buffer);
    if (ACPI_FAILURE(status))
        goto Exit;
    package = buffer.Pointer;
    if (!package || package->Type != ACPI_TYPE_PACKAGE)
        goto Exit;
    for (i = 0; i < package->Package.Count; i++) {
        object = &package->Package.Elements[i];
        if (object->Type != ACPI_TYPE_LOCAL_REFERENCE || !object->Reference.Handle)
            continue;
        index = acpi_thermal_find_cooling_device(handles, *count, object->Reference.Handle);
        if (index < 0) {
            if (*count >= ACPI_THERMAL_MAX_COOLING_DEVICES) {
                DPRINT1("ACPI: Thermal [%s] has more than %u cooling devices\n",
                        acpi_device_bid(zone->Device),
                        ACPI_THERMAL_MAX_COOLING_DEVICES);
                break;
            }
            index = (LONG)(*count);
            handles[index] = object->Reference.Handle;
            levels[index] = ACPI_FAN_THERMAL_LEVEL_OFF;
            (*count)++;
        }
        if (active && (levels[index] == ACPI_FAN_THERMAL_LEVEL_OFF || (LONG)level < levels[index]))
            levels[index] = (LONG)level;
    }

Exit:
    if (buffer.Pointer)
        AcpiOsFree(buffer.Pointer);
}

static ULONG
acpi_thermal_update_active_policy(
    PACPI_THERMAL_ZONE zone,
    ULONG active_mask,
    PBOOLEAN policy_failed)
{
    ACPI_HANDLE handles[ACPI_THERMAL_MAX_COOLING_DEVICES];
    LONG levels[ACPI_THERMAL_MAX_COOLING_DEVICES];
    LONG index;
    ULONG count = 0;
    ULONG activated = 0;
    ULONG i;
    int result;

    *policy_failed = FALSE;
    RtlZeroMemory(handles, sizeof(handles));
    for (i = 0; i < ACPI_THERMAL_MAX_COOLING_DEVICES; i++)
        levels[i] = ACPI_FAN_THERMAL_LEVEL_OFF;
    for (i = 0; i < ACPI_THERMAL_ACTIVE_LEVELS; i++) {
        if (zone->Active[i].Valid)
            acpi_thermal_collect_level(zone, i, (active_mask & (1u << i)) != 0, handles, levels, &count);
    }

    for (i = 0; i < zone->CoolingDeviceCount; i++) {
        index = acpi_thermal_find_cooling_device(handles, count, zone->CoolingDevices[i]);
        if (index >= 0)
            continue;
        result = acpi_fan_set_thermal_level(zone->CoolingDevices[i], zone, ACPI_FAN_THERMAL_LEVEL_OFF);
        if (result)
            acpi_bus_set_power(zone->CoolingDevices[i], ACPI_STATE_D3);
    }

    for (i = 0; i < count; i++) {
        result = acpi_fan_set_thermal_level(handles[i], zone, levels[i]);
        if (result)
            result = acpi_bus_set_power(handles[i], levels[i] == ACPI_FAN_THERMAL_LEVEL_OFF ? ACPI_STATE_D3 : ACPI_STATE_D0);
        if (levels[i] != ACPI_FAN_THERMAL_LEVEL_OFF) {
            if (!result)
                activated++;
            else
                *policy_failed = TRUE;
        }
    }

    RtlCopyMemory(zone->CoolingDevices, handles, count * sizeof(handles[0]));
    zone->CoolingDeviceCount = count;
    return activated;
}

static VOID
acpi_thermal_release_cooling_policy(
    PACPI_THERMAL_ZONE zone)
{
    ULONG i;

    for (i = 0; i < zone->CoolingDeviceCount; i++) {
        if (acpi_fan_set_thermal_level(zone->CoolingDevices[i], zone, ACPI_FAN_THERMAL_LEVEL_OFF))
            acpi_bus_set_power(zone->CoolingDevices[i], ACPI_STATE_D3);
    }
    acpi_fan_set_all_thermal_levels(&zone->CriticalEngaged, ACPI_FAN_THERMAL_LEVEL_OFF);
    zone->CoolingDeviceCount = 0;
    zone->ActiveMask = 0;
}

static VOID
acpi_thermal_check(
    PACPI_THERMAL_ZONE zone,
    ULONG reason)
{
    UINT64 temperature;
    ULONG new_active_mask = 0;
    ULONG changed_active_mask;
    ULONG activated;
    ULONG targets;
    ULONG level;
    BOOLEAN emergency;
    BOOLEAN active_policy_failed;

    if (!acpi_thermal_evaluate_integer(zone->Handle, "_TMP", &temperature))
    {
        DPRINT1("ACPI: Thermal [%s] _TMP evaluation failed; retaining previous cooling state\n", acpi_device_bid(zone->Device));
        return;
    }
    if (zone->Critical.Valid && temperature >= zone->Critical.Temperature)
    {
        if (!zone->CriticalEngaged)
            DPRINT1("ACPI: Thermal [%s] CRITICAL trip reached; forcing maximum cooling\n", acpi_device_bid(zone->Device));
        zone->CriticalEngaged = TRUE;
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
    }
    changed_active_mask = zone->ActiveMask ^ new_active_mask;
    activated = acpi_thermal_update_active_policy(zone, new_active_mask, &active_policy_failed);
    for (level = 0; level < ACPI_THERMAL_ACTIVE_LEVELS; level++) {
        if (!(changed_active_mask & (1u << level)))
            continue;
        DPRINT1("ACPI: Thermal [%s] active trip AC%lu %s\n",
                acpi_device_bid(zone->Device), level,
                (new_active_mask & (1u << level)) ? "reached" : "cleared");
    }
    zone->ActiveMask = new_active_mask;

    if (zone->Passive.Valid && temperature >= zone->Passive.Temperature && (!zone->PassiveEngaged || zone->PassiveCoolingFailed || reason == ACPI_THERMAL_NOTIFY_DEVICES))
    {
        ULONG throttled = acpi_thermal_apply_processor_list(zone, ACPI_PROCESSOR_LIMIT_DECREMENT, &targets);
        zone->PassiveEngaged = TRUE;
        zone->PassiveCoolingFailed = targets == 0 || throttled != targets;
        DPRINT1("ACPI: Thermal [%s] passive trip reached; %lu/%lu processors throttled\n", acpi_device_bid(zone->Device), throttled, targets);
    }
    else if (zone->PassiveEngaged && (!zone->Passive.Valid || (temperature < zone->Passive.Temperature && zone->Passive.Temperature - temperature > ACPI_THERMAL_PASSIVE_HYSTERESIS)))
    {
        ULONG restored = acpi_thermal_apply_processor_list(zone, ACPI_PROCESSOR_LIMIT_NONE, &targets);
        zone->PassiveEngaged = FALSE;
        zone->PassiveCoolingFailed = FALSE;
        DPRINT1("ACPI: Thermal [%s] passive trip cleared; %lu/%lu processors restored\n", acpi_device_bid(zone->Device), restored, targets);
    }

    emergency = zone->CriticalEngaged || zone->HotEngaged || (new_active_mask != 0 && (activated == 0 || active_policy_failed)) || zone->PassiveCoolingFailed;
    acpi_fan_set_all_thermal_levels(&zone->CriticalEngaged, emergency ? ACPI_FAN_THERMAL_LEVEL_MAXIMUM : ACPI_FAN_THERMAL_LEVEL_OFF);
}

static VOID NTAPI
acpi_thermal_worker(
    PVOID context)
{
    PACPI_THERMAL_WORK work = context;
    PACPI_THERMAL_ZONE zone = work->Zone;

    ExAcquireFastMutex(&zone->PolicyLock);
    if (!InterlockedCompareExchange(&zone->Removing, 0, 0)) {
        if (work->Reason == ACPI_THERMAL_NOTIFY_THRESHOLDS || work->Reason == ACPI_THERMAL_NOTIFY_DEVICES)
            acpi_thermal_refresh_trips(zone);
        acpi_thermal_check(zone, work->Reason);
    }
    ExReleaseFastMutex(&zone->PolicyLock);
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
        if (KeGetCurrentIrql() == PASSIVE_LEVEL)
            acpi_fan_set_all_thermal_levels(&zone->CriticalEngaged, ACPI_FAN_THERMAL_LEVEL_MAXIMUM);
        else
            DPRINT1("ACPI: Thermal [%s] could not queue a policy check at IRQL %lu\n", acpi_device_bid(zone->Device), KeGetCurrentIrql());
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
    ExInitializeFastMutex(&zone->PolicyLock);
    KeInitializeEvent(&zone->WorkIdleEvent, NotificationEvent, TRUE);
    acpi_thermal_set_active_cooling_policy(zone);
    acpi_thermal_refresh_trips(zone);

    status = AcpiInstallNotifyHandler(zone->Handle, ACPI_DEVICE_NOTIFY, acpi_thermal_notify, zone);
    if (ACPI_FAILURE(status))
    {
        device->driver_data = NULL;
        ExFreePoolWithTag(zone, ACPI_THERMAL_TAG);
        DPRINT1("ACPI: Thermal [%s] notify registration failed: %s\n", acpi_device_bid(device), AcpiFormatException(status));
        return_VALUE(-15);
    }

    acpi_thermal_queue_check(zone, ACPI_THERMAL_NOTIFY_TEMPERATURE);
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
    AcpiRemoveNotifyHandler(zone->Handle, ACPI_DEVICE_NOTIFY, acpi_thermal_notify);
    ExWaitForRundownProtectionRelease(&zone->Rundown);
    KeWaitForSingleObject(&zone->WorkIdleEvent, Executive, KernelMode, FALSE, NULL);
    ExAcquireFastMutex(&zone->PolicyLock);
    acpi_thermal_release_cooling_policy(zone);
#if (NTDDI_VERSION >= NTDDI_WINTHRESHOLD)
    acpi_thermal_release_processor_requests(zone);
#else
    if (zone->PassiveEngaged)
        acpi_thermal_apply_processor_list(zone, ACPI_PROCESSOR_LIMIT_NONE, NULL);
#endif
    ExReleaseFastMutex(&zone->PolicyLock);
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
