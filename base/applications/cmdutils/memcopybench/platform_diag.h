/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 * Optional Raspberry Pi firmware queries. No clocks/registers are changed.
 * Unsupported boards/drivers simply report an error and keep benchmarking.
 */
#include <winioctl.h>

typedef struct { DWORD Value, Error; } PI_VALUE;
typedef struct { PI_VALUE Arm, Core, Sdram, Temperature; } PI_SAMPLE;
static HANDLE PiMailbox = INVALID_HANDLE_VALUE;
static DWORD PiOpenError;
static BOOL PiRequested;
static PI_VALUE PiConfigured[4], PiMaximum[4];

static PI_VALUE PiRead(DWORD Tag, DWORD Id)
{
    DWORD Packet[8] = {32, 0, Tag, 8, 0, Id, 0, 0}, Returned = 0;
    PI_VALUE Result = {0, ERROR_NOT_SUPPORTED};
    if (PiMailbox == INVALID_HANDLE_VALUE)
        return Result;
    if (!DeviceIoControl(PiMailbox, CTL_CODE(2836, 2008, METHOD_BUFFERED, FILE_ANY_ACCESS),
                         Packet, sizeof(Packet), Packet, sizeof(Packet), &Returned, NULL))
        Result.Error = GetLastError();
    else if (Returned < sizeof(Packet) || Packet[0] != sizeof(Packet) ||
             Packet[1] != 0x80000000 || Packet[2] != Tag ||
             !(Packet[4] & 0x80000000) || (Packet[4] & 0x7fffffff) < 8 || Packet[5] != Id)
        Result.Error = ERROR_INVALID_DATA;
    else
    {
        Result.Value = Packet[6];
        Result.Error = ERROR_SUCCESS;
    }
    return Result;
}

static void PiOpen(void)
{
    static const DWORD Ids[] = {3, 4, 5, 8};
    ULONG I;
    if (!PiRequested) return;
    PiMailbox = CreateFileW(L"\\\\.\\RPIQ", GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (PiMailbox == INVALID_HANDLE_VALUE) PiOpenError = GetLastError();
    for (I = 0; I < ARRAYSIZE(Ids); ++I)
    {
        PiConfigured[I] = PiRead(0x00030002, Ids[I]);
        PiMaximum[I] = PiRead(0x00030004, Ids[I]);
    }
}

static void PiSample(PI_SAMPLE *Sample)
{
    if (!PiRequested) return;
    Sample->Arm = PiRead(0x00030047, 3);
    Sample->Core = PiRead(0x00030047, 4);
    Sample->Sdram = PiRead(0x00030047, 8);
    Sample->Temperature = PiRead(0x00030006, 0);
    /* Some firmware accepts a measured-clock tag but returns zero for clocks
     * it cannot measure (observed for SDRAM). Do not report that as 0 Hz. */
    if (!Sample->Arm.Error && !Sample->Arm.Value) Sample->Arm.Error = ERROR_NOT_SUPPORTED;
    if (!Sample->Core.Error && !Sample->Core.Value) Sample->Core.Error = ERROR_NOT_SUPPORTED;
    if (!Sample->Sdram.Error && !Sample->Sdram.Value) Sample->Sdram.Error = ERROR_NOT_SUPPORTED;
}
