/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     SystemPowerInformation buffer contract and returned state
 */

#include <apitest.h>
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <ndk/potypes.h>
#include <powrprof.h>

START_TEST(SystemPowerInformation)
{
    struct
    {
        SYSTEM_POWER_INFORMATION Information;
        ULONG Guard;
    } Buffer;
    ULONG Input = 0;
    NTSTATUS Status;

    memset(&Buffer, 0xcc, sizeof(Buffer));
    Status = CallNtPowerInformation(SystemPowerInformation, NULL, 0,
                                   &Buffer, sizeof(Buffer));
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok(Buffer.Information.MaxIdlenessAllowed <= 100, "Idle threshold %lu\n", Buffer.Information.MaxIdlenessAllowed);
        ok(Buffer.Information.Idleness <= 100, "Idleness %lu\n", Buffer.Information.Idleness);
        ok(Buffer.Information.CoolingMode <= 2, "Cooling mode %u\n", Buffer.Information.CoolingMode);
    }
    ok_hex(Buffer.Guard, 0xcccccccc);

    memset(&Buffer, 0xcc, sizeof(Buffer));
    Status = CallNtPowerInformation(SystemPowerInformation, NULL, 0,
                                   &Buffer, sizeof(Buffer.Information) - 1);
    ok_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok_hex(Buffer.Information.MaxIdlenessAllowed, 0xcccccccc);
    ok_hex(Buffer.Guard, 0xcccccccc);

    /* A zero input length selects a query even with a non-NULL input pointer. */
    Status = CallNtPowerInformation(SystemPowerInformation, &Input, 0,
                                   &Buffer, sizeof(Buffer.Information));
    ok_hex(Status, STATUS_SUCCESS);
    Status = CallNtPowerInformation(SystemPowerInformation, NULL, 0, NULL, sizeof(Buffer.Information));
    ok_hex(Status, STATUS_INVALID_PARAMETER);
    Status = CallNtPowerInformation(SystemPowerInformation, NULL, 0, &Buffer, 0);
    ok_hex(Status, STATUS_INVALID_PARAMETER);
}
