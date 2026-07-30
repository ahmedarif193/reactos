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

static BOOLEAN
acpi_fan_has_advanced_control(
    ACPI_HANDLE handle)
{
    static const char *methods[] = {"_FIF", "_FPS", "_FSL", "_FST"};
    ACPI_HANDLE method;
    UINT32 i;

    for (i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
        if (ACPI_FAILURE(AcpiGetHandle(handle,
                                      (ACPI_STRING)methods[i],
                                      &method))) {
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
acpi_fan_force_d0(
    struct acpi_device *device)
{
    UINT32 force_power_state;
    int result;

    force_power_state = device->flags.force_power_state;
    device->flags.force_power_state = 1;
    result = acpi_bus_set_power(device->handle, ACPI_STATE_D0);
    device->flags.force_power_state = force_power_state;

    return result;
}

static int
acpi_fan_add(
    struct acpi_device *device)
{
    ACPI_STATUS status = AE_NOT_FOUND;
    BOOLEAN fine_grain = FALSE;
    UINT64 control = 0;
    int result;

    if (!device)
        return_VALUE(-1);

    sprintf(acpi_device_name(device), "%s", ACPI_FAN_DEVICE_NAME);
    sprintf(acpi_device_class(device), "%s", ACPI_FAN_CLASS);

    if (acpi_fan_has_advanced_control(device->handle)) {
        status = acpi_fan_get_fine_grain(device->handle, &fine_grain);
        if (ACPI_SUCCESS(status)) {
            if (fine_grain) {
                control = ACPI_FAN_MAX_PERCENT;
            } else {
                status = acpi_fan_get_max_control(device->handle,
                                                  &control);
            }
        }

        if (ACPI_SUCCESS(status))
            status = acpi_fan_set_level(device->handle, control);

        if (ACPI_SUCCESS(status)) {
            DPRINT1("ACPI: Fan [%s] forced to maximum with _FSL(%lu)\n",
                    acpi_device_bid(device), (ULONG)control);
            return_VALUE(0);
        }

        DPRINT1("ACPI: Fan [%s] advanced control failed: %s; "
                "falling back to D0\n",
                acpi_device_bid(device), AcpiFormatException(status));
    }

    result = acpi_fan_force_d0(device);
    if (result) {
        DPRINT1("ACPI: Fan [%s] could not be forced to D0: 0x%x\n",
                acpi_device_bid(device), result);
        return_VALUE(-15);
    }

    DPRINT1("ACPI: Fan [%s] forced to D0\n", acpi_device_bid(device));
    return_VALUE(0);
}

static int
acpi_fan_remove(
    struct acpi_device *device,
    int type)
{
    UNREFERENCED_PARAMETER(type);

    if (!device)
        return_VALUE(-1);

    /*
     * This is a fail-safe driver. Do not lower or disable cooling when the
     * driver is detached.
     */
    return_VALUE(0);
}

int
acpi_fan_init(void)
{
    int result;

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
