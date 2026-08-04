/*
 * PROJECT:     ReactOS ACPI bus driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Minimal fail-safe ACPI fan driver
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#include <precomp.h>

#define NDEBUG
#include <debug.h>

#define _COMPONENT              ACPI_FAN_COMPONENT
ACPI_MODULE_NAME               ("acpi_fan")

#define ACPI_FAN_FIF_COUNT      4
#define ACPI_FAN_FPS_COUNT      5
#define ACPI_FAN_MAX_PERCENT    100

#define ACPI_FAN_FPS_CONTROL    0
#define ACPI_FAN_FPS_TRIP_POINT 1
#define ACPI_FAN_FPS_SPEED      2
#define ACPI_FAN_MAX_DEVICES    32
#define ACPI_FAN_MAX_REQUESTS   32
#define ACPI_FAN_LEVEL_OFF      (-1)
#define ACPI_FAN_LEVEL_MAXIMUM  10

typedef struct _ACPI_FAN_REQUEST
{
    PVOID Source;
    LONG Level;
} ACPI_FAN_REQUEST, *PACPI_FAN_REQUEST;

typedef struct _ACPI_FAN_CONTEXT
{
    struct acpi_device *Device;
    FAST_MUTEX PolicyLock;
    ACPI_FAN_REQUEST Requests[ACPI_FAN_MAX_REQUESTS];
    ULONG RequestCount;
    LONG AppliedLevel;
} ACPI_FAN_CONTEXT, *PACPI_FAN_CONTEXT;

static int acpi_fan_add(struct acpi_device *device);
static int acpi_fan_remove(struct acpi_device *device, int type);

static struct acpi_driver acpi_fan_driver = {
    {0,0},
    ACPI_FAN_DRIVER_NAME,
    ACPI_FAN_CLASS,
    0,
    0,
    ACPI_FAN_HID,
    {acpi_fan_add, acpi_fan_remove}
};

static FAST_MUTEX acpi_fan_list_lock;
static PACPI_FAN_CONTEXT acpi_fan_contexts[ACPI_FAN_MAX_DEVICES];
static ULONG acpi_fan_context_count;

static BOOLEAN
acpi_fan_has_advanced_control(
    ACPI_HANDLE handle)
{
    static const char *methods[] = {"_FIF", "_FPS", "_FSL", "_FST"};
    ACPI_HANDLE method;
    UINT32 i;

    for (i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
        if (ACPI_FAILURE(AcpiGetHandle(handle, (ACPI_STRING)methods[i], &method))) {
            return FALSE;
        }
    }

    return TRUE;
}

static ACPI_STATUS
acpi_fan_get_fine_grain(
    ACPI_HANDLE handle,
    BOOLEAN *fine_grain)
{
    ACPI_BUFFER buffer = {ACPI_ALLOCATE_BUFFER, NULL};
    ACPI_OBJECT *fif;
    ACPI_STATUS status;
    UINT32 i;

    status = AcpiEvaluateObject(handle, "_FIF", NULL, &buffer);
    if (ACPI_FAILURE(status))
        goto end;

    fif = (ACPI_OBJECT *)buffer.Pointer;
    if (!fif ||
        fif->Type != ACPI_TYPE_PACKAGE ||
        fif->Package.Count < ACPI_FAN_FIF_COUNT) {
        status = AE_BAD_DATA;
        goto end;
    }

    for (i = 0; i < ACPI_FAN_FIF_COUNT; i++) {
        if (fif->Package.Elements[i].Type != ACPI_TYPE_INTEGER) {
            status = AE_BAD_DATA;
            goto end;
        }
    }

    if (fif->Package.Elements[0].Integer.Value != 0) {
        status = AE_SUPPORT;
        goto end;
    }

    *fine_grain = fif->Package.Elements[1].Integer.Value != 0;

end:
    if (buffer.Pointer)
        AcpiOsFree(buffer.Pointer);
    return status;
}

static ACPI_STATUS
acpi_fan_get_max_control(
    ACPI_HANDLE handle,
    UINT64 *control)
{
    ACPI_BUFFER buffer = {ACPI_ALLOCATE_BUFFER, NULL};
    ACPI_OBJECT *fps;
    ACPI_OBJECT *state;
    ACPI_OBJECT *elements;
    ACPI_STATUS status;
    UINT64 max_speed = 0;
    UINT64 max_speed_control = 0;
    UINT64 ac0_control = 0;
    BOOLEAN have_speed = FALSE;
    BOOLEAN have_ac0 = FALSE;
    UINT32 i;
    UINT32 j;

    status = AcpiEvaluateObject(handle, "_FPS", NULL, &buffer);
    if (ACPI_FAILURE(status))
        goto end;

    fps = (ACPI_OBJECT *)buffer.Pointer;
    if (!fps ||
        fps->Type != ACPI_TYPE_PACKAGE ||
        fps->Package.Count < 2 ||
        fps->Package.Elements[0].Type != ACPI_TYPE_INTEGER ||
        fps->Package.Elements[0].Integer.Value != 0) {
        status = AE_BAD_DATA;
        goto end;
    }

    for (i = 1; i < fps->Package.Count; i++) {
        state = &fps->Package.Elements[i];
        if (state->Type != ACPI_TYPE_PACKAGE ||
            state->Package.Count < ACPI_FAN_FPS_COUNT) {
            status = AE_BAD_DATA;
            goto end;
        }

        elements = state->Package.Elements;
        for (j = 0; j < ACPI_FAN_FPS_COUNT; j++) {
            if (elements[j].Type != ACPI_TYPE_INTEGER ||
                elements[j].Integer.Value > ACPI_UINT32_MAX) {
                status = AE_BAD_DATA;
                goto end;
            }
        }

        if (elements[ACPI_FAN_FPS_TRIP_POINT].Integer.Value == 0 &&
            !have_ac0) {
            ac0_control =
                elements[ACPI_FAN_FPS_CONTROL].Integer.Value;
            have_ac0 = TRUE;
        }

        if (elements[ACPI_FAN_FPS_SPEED].Integer.Value !=
                ACPI_UINT32_MAX &&
            elements[ACPI_FAN_FPS_SPEED].Integer.Value > max_speed) {
            max_speed =
                elements[ACPI_FAN_FPS_SPEED].Integer.Value;
            max_speed_control =
                elements[ACPI_FAN_FPS_CONTROL].Integer.Value;
            have_speed = TRUE;
        }
    }

    if (have_speed) {
        *control = max_speed_control;
    } else if (have_ac0) {
        *control = ac0_control;
    } else {
        status = AE_NOT_FOUND;
    }

end:
    if (buffer.Pointer)
        AcpiOsFree(buffer.Pointer);
    return status;
}

static ACPI_STATUS
acpi_fan_get_trip_control(
    ACPI_HANDLE handle,
    ULONG level,
    UINT64 *control)
{
    ACPI_BUFFER buffer = {ACPI_ALLOCATE_BUFFER, NULL};
    ACPI_OBJECT *fps;
    ACPI_OBJECT *state;
    ACPI_OBJECT *elements;
    ACPI_STATUS status;
    UINT32 i;
    UINT32 j;

    if (level >= ACPI_FAN_LEVEL_MAXIMUM)
        return AE_BAD_PARAMETER;

    status = AcpiEvaluateObject(handle, "_FPS", NULL, &buffer);
    if (ACPI_FAILURE(status))
        goto end;

    fps = (ACPI_OBJECT *)buffer.Pointer;
    if (!fps || fps->Type != ACPI_TYPE_PACKAGE || fps->Package.Count < 2 || fps->Package.Elements[0].Type != ACPI_TYPE_INTEGER || fps->Package.Elements[0].Integer.Value != 0) {
        status = AE_BAD_DATA;
        goto end;
    }

    status = AE_NOT_FOUND;
    for (i = 1; i < fps->Package.Count; i++) {
        state = &fps->Package.Elements[i];
        if (state->Type != ACPI_TYPE_PACKAGE || state->Package.Count < ACPI_FAN_FPS_COUNT) {
            status = AE_BAD_DATA;
            break;
        }

        elements = state->Package.Elements;
        for (j = 0; j < ACPI_FAN_FPS_COUNT; j++) {
            if (elements[j].Type != ACPI_TYPE_INTEGER || elements[j].Integer.Value > ACPI_UINT32_MAX) {
                status = AE_BAD_DATA;
                break;
            }
        }
        if (ACPI_FAILURE(status) && status != AE_NOT_FOUND)
            break;
        if (elements[ACPI_FAN_FPS_TRIP_POINT].Integer.Value != level)
            continue;
        *control = elements[ACPI_FAN_FPS_CONTROL].Integer.Value;
        status = AE_OK;
        break;
    }

end:
    if (buffer.Pointer)
        AcpiOsFree(buffer.Pointer);
    return status;
}

static ACPI_STATUS
acpi_fan_set_level(
    ACPI_HANDLE handle,
    UINT64 control)
{
    ACPI_OBJECT argument;
    ACPI_OBJECT_LIST arguments;

    argument.Type = ACPI_TYPE_INTEGER;
    argument.Integer.Value = control;
    arguments.Count = 1;
    arguments.Pointer = &argument;

    return AcpiEvaluateObject(handle, "_FSL", &arguments, NULL);
}

static int
acpi_fan_apply_level_locked(
    PACPI_FAN_CONTEXT context,
    LONG level)
{
    struct acpi_device *device = context->Device;
    ACPI_STATUS status = AE_NOT_FOUND;
    BOOLEAN fine_grain = FALSE;
    UINT64 control = 0;
    LONG applied_level = level;
    int result;

    if (level < ACPI_FAN_LEVEL_OFF || level > ACPI_FAN_LEVEL_MAXIMUM)
        return_VALUE(-1);
    if (context->AppliedLevel == level)
        return_VALUE(0);

    if (acpi_fan_has_advanced_control(device->handle)) {
        if (level == ACPI_FAN_LEVEL_OFF) {
            status = acpi_fan_set_level(device->handle, 0);
        } else if (level == ACPI_FAN_LEVEL_MAXIMUM) {
            status = acpi_fan_get_fine_grain(device->handle, &fine_grain);
            if (ACPI_SUCCESS(status)) {
                if (fine_grain)
                    control = ACPI_FAN_MAX_PERCENT;
                else
                    status = acpi_fan_get_max_control(device->handle, &control);
            }
            if (ACPI_SUCCESS(status))
                status = acpi_fan_set_level(device->handle, control);
        } else {
            status = acpi_fan_get_trip_control(device->handle, (ULONG)level, &control);
            if (ACPI_SUCCESS(status)) {
                status = acpi_fan_set_level(device->handle, control);
            } else {
                status = acpi_fan_get_max_control(device->handle, &control);
                if (ACPI_SUCCESS(status))
                    status = acpi_fan_set_level(device->handle, control);
                applied_level = ACPI_FAN_LEVEL_MAXIMUM;
            }
        }

        if (ACPI_SUCCESS(status)) {
            context->AppliedLevel = applied_level;
            DPRINT1("ACPI: Fan [%s] set to thermal level %ld with _FSL(%lu)\n",
                    acpi_device_bid(device), level, (ULONG)control);
            return_VALUE(0);
        }

        DPRINT1("ACPI: Fan [%s] advanced control failed: %s; falling back to D%u\n",
                acpi_device_bid(device), AcpiFormatException(status),
                level == ACPI_FAN_LEVEL_OFF ? ACPI_STATE_D3 : ACPI_STATE_D0);
    }

    result = acpi_bus_set_power(device->handle, level == ACPI_FAN_LEVEL_OFF ? ACPI_STATE_D3 : ACPI_STATE_D0);
    if (result) {
        DPRINT1("ACPI: Fan [%s] could not be set to D%u: 0x%x\n",
                acpi_device_bid(device),
                level == ACPI_FAN_LEVEL_OFF ? ACPI_STATE_D3 : ACPI_STATE_D0,
                result);
        return_VALUE(-15);
    }

    context->AppliedLevel = level;
    DPRINT1("ACPI: Fan [%s] set to D%u for thermal level %ld\n",
            acpi_device_bid(device),
            level == ACPI_FAN_LEVEL_OFF ? ACPI_STATE_D3 : ACPI_STATE_D0,
            level);
    return_VALUE(0);
}

static int
acpi_fan_force_device_maximum(
    PACPI_FAN_CONTEXT context)
{
    if (!context || !context->Device)
        return_VALUE(-1);
    context->AppliedLevel = ACPI_FAN_LEVEL_OFF;
    return_VALUE(acpi_fan_apply_level_locked(context, ACPI_FAN_LEVEL_MAXIMUM));
}

static PACPI_FAN_CONTEXT
acpi_fan_acquire_context(
    ACPI_HANDLE handle)
{
    PACPI_FAN_CONTEXT context = NULL;
    ULONG i;

    ExAcquireFastMutex(&acpi_fan_list_lock);
    for (i = 0; i < acpi_fan_context_count; i++) {
        if (acpi_fan_contexts[i]->Device->handle != handle)
            continue;
        context = acpi_fan_contexts[i];
        ExAcquireFastMutex(&context->PolicyLock);
        break;
    }
    ExReleaseFastMutex(&acpi_fan_list_lock);
    return context;
}

static LONG
acpi_fan_effective_level(
    PACPI_FAN_CONTEXT context)
{
    LONG level = ACPI_FAN_LEVEL_OFF;
    ULONG i;

    for (i = 0; i < context->RequestCount; i++) {
        if (context->Requests[i].Level == ACPI_FAN_LEVEL_MAXIMUM)
            return ACPI_FAN_LEVEL_MAXIMUM;
        if (level == ACPI_FAN_LEVEL_OFF || context->Requests[i].Level < level)
            level = context->Requests[i].Level;
    }
    return level;
}

static int
acpi_fan_update_request_locked(
    PACPI_FAN_CONTEXT context,
    PVOID source,
    LONG level)
{
    ULONG index;

    if (!source || level < ACPI_FAN_LEVEL_OFF || level > ACPI_FAN_LEVEL_MAXIMUM)
        return_VALUE(-1);
    for (index = 0; index < context->RequestCount; index++) {
        if (context->Requests[index].Source == source)
            break;
    }
    if (level == ACPI_FAN_LEVEL_OFF) {
        if (index < context->RequestCount) {
            context->RequestCount--;
            context->Requests[index] = context->Requests[context->RequestCount];
            RtlZeroMemory(&context->Requests[context->RequestCount], sizeof(context->Requests[0]));
        }
    } else if (index < context->RequestCount) {
        context->Requests[index].Level = level;
    } else {
        if (context->RequestCount >= ACPI_FAN_MAX_REQUESTS)
            return_VALUE(-12);
        context->Requests[context->RequestCount].Source = source;
        context->Requests[context->RequestCount].Level = level;
        context->RequestCount++;
    }
    return_VALUE(acpi_fan_apply_level_locked(context, acpi_fan_effective_level(context)));
}

int
acpi_fan_force_maximum(
    ACPI_HANDLE handle)
{
    PACPI_FAN_CONTEXT context;
    int result;

    if (!handle)
        return_VALUE(-1);
    context = acpi_fan_acquire_context(handle);
    if (!context)
        return_VALUE(-1);
    result = acpi_fan_force_device_maximum(context);
    ExReleaseFastMutex(&context->PolicyLock);
    return_VALUE(result);
}

void
acpi_fan_force_all_maximum(void)
{
    ULONG i;

    ExAcquireFastMutex(&acpi_fan_list_lock);
    for (i = 0; i < acpi_fan_context_count; i++) {
        ExAcquireFastMutex(&acpi_fan_contexts[i]->PolicyLock);
        acpi_fan_force_device_maximum(acpi_fan_contexts[i]);
        ExReleaseFastMutex(&acpi_fan_contexts[i]->PolicyLock);
    }
    ExReleaseFastMutex(&acpi_fan_list_lock);
}

int
acpi_fan_set_thermal_level(
    ACPI_HANDLE handle,
    PVOID source,
    LONG level)
{
    PACPI_FAN_CONTEXT context;
    int result;

    context = acpi_fan_acquire_context(handle);
    if (!context)
        return_VALUE(-1);
    result = acpi_fan_update_request_locked(context, source, level);
    ExReleaseFastMutex(&context->PolicyLock);
    return_VALUE(result);
}

void
acpi_fan_set_all_thermal_levels(
    PVOID source,
    LONG level)
{
    ULONG i;

    ExAcquireFastMutex(&acpi_fan_list_lock);
    for (i = 0; i < acpi_fan_context_count; i++) {
        ExAcquireFastMutex(&acpi_fan_contexts[i]->PolicyLock);
        acpi_fan_update_request_locked(acpi_fan_contexts[i], source, level);
        ExReleaseFastMutex(&acpi_fan_contexts[i]->PolicyLock);
    }
    ExReleaseFastMutex(&acpi_fan_list_lock);
}

static int
acpi_fan_add(
    struct acpi_device *device)
{
    PACPI_FAN_CONTEXT context;
    int result;

    if (!device)
        return_VALUE(-1);

    context = ExAllocatePoolWithTag(NonPagedPool, sizeof(*context), 'naFA');
    if (!context)
        return_VALUE(-12);
    RtlZeroMemory(context, sizeof(*context));
    context->Device = device;
    context->AppliedLevel = ACPI_FAN_LEVEL_OFF;
    ExInitializeFastMutex(&context->PolicyLock);

    sprintf(acpi_device_name(device), "%s", ACPI_FAN_DEVICE_NAME);
    sprintf(acpi_device_class(device), "%s", ACPI_FAN_CLASS);
    ExAcquireFastMutex(&context->PolicyLock);
    result = acpi_fan_force_device_maximum(context);
    ExReleaseFastMutex(&context->PolicyLock);
    if (result) {
        ExFreePoolWithTag(context, 'naFA');
        return_VALUE(result);
    }

    ExAcquireFastMutex(&acpi_fan_list_lock);
    if (acpi_fan_context_count >= ACPI_FAN_MAX_DEVICES) {
        ExReleaseFastMutex(&acpi_fan_list_lock);
        ExFreePoolWithTag(context, 'naFA');
        return_VALUE(-12);
    }
    acpi_fan_contexts[acpi_fan_context_count++] = context;
    device->driver_data = context;
    ExReleaseFastMutex(&acpi_fan_list_lock);
    return_VALUE(0);
}

static int
acpi_fan_remove(
    struct acpi_device *device,
    int type)
{
    PACPI_FAN_CONTEXT context;
    BOOLEAN found = FALSE;
    ULONG i;

    UNREFERENCED_PARAMETER(type);

    if (!device || !device->driver_data)
        return_VALUE(-1);
    context = device->driver_data;

    ExAcquireFastMutex(&acpi_fan_list_lock);
    for (i = 0; i < acpi_fan_context_count; i++) {
        if (acpi_fan_contexts[i] != context)
            continue;
        ExAcquireFastMutex(&context->PolicyLock);
        acpi_fan_context_count--;
        acpi_fan_contexts[i] = acpi_fan_contexts[acpi_fan_context_count];
        acpi_fan_contexts[acpi_fan_context_count] = NULL;
        found = TRUE;
        break;
    }
    ExReleaseFastMutex(&acpi_fan_list_lock);

    if (!found) {
        DPRINT1("ACPI: Fan [%s] removal context was not registered\n", acpi_device_bid(device));
        return_VALUE(-1);
    }

    /*
     * This is a fail-safe driver. Do not lower or disable cooling when the
     * driver is detached.
     */
    acpi_fan_force_device_maximum(context);
    ExReleaseFastMutex(&context->PolicyLock);
    device->driver_data = NULL;
    ExFreePoolWithTag(context, 'naFA');
    return_VALUE(0);
}

int
acpi_fan_init(void)
{
    int result;

    ExInitializeFastMutex(&acpi_fan_list_lock);
    acpi_fan_context_count = 0;
    RtlZeroMemory(acpi_fan_contexts, sizeof(acpi_fan_contexts));
    result = acpi_bus_register_driver(&acpi_fan_driver);
    if (result < 0)
        return_VALUE(-15);

    return_VALUE(0);
}

void
acpi_fan_exit(void)
{
    acpi_bus_unregister_driver(&acpi_fan_driver);
}
