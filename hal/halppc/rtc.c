/*
 * PROJECT:     LiberNT PowerPC HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     PReP MC146818 real-time clock
 */

#include <ntifs.h>
#include <arc/arc.h>
#include "halp.h"

#define RTC_INDEX 0x70
#define RTC_DATA  0x71

static
UCHAR
HalpPpcReadCmos(_In_ UCHAR Register)
{
    WRITE_PORT_UCHAR((PUCHAR)RTC_INDEX, Register);
    return READ_PORT_UCHAR((PUCHAR)RTC_DATA);
}

static
VOID
HalpPpcWriteCmos(_In_ UCHAR Register, _In_ UCHAR Value)
{
    WRITE_PORT_UCHAR((PUCHAR)RTC_INDEX, Register);
    WRITE_PORT_UCHAR((PUCHAR)RTC_DATA, Value);
}

static UCHAR HalpPpcFromBcd(UCHAR Value) { return (Value >> 4) * 10 + (Value & 0xF); }
static UCHAR HalpPpcToBcd(UCHAR Value) { return (UCHAR)(((Value / 10) << 4) | (Value % 10)); }

BOOLEAN
NTAPI
HalQueryRealTimeClock(_Out_ PTIME_FIELDS Time)
{
    BOOLEAN Interrupts = KeDisableInterrupts();
    UCHAR Status;
    ULONG Spin;

    /* Wait out an update in progress. */
    for (Spin = 0; (Spin < 100000) && (HalpPpcReadCmos(0x0A) & 0x80); Spin++)
        YieldProcessor();
    Status = HalpPpcReadCmos(0x0B);
    Time->Second = HalpPpcReadCmos(0x00);
    Time->Minute = HalpPpcReadCmos(0x02);
    Time->Hour = HalpPpcReadCmos(0x04);
    Time->Day = HalpPpcReadCmos(0x07);
    Time->Month = HalpPpcReadCmos(0x08);
    Time->Year = HalpPpcReadCmos(0x09);
    KeRestoreInterrupts(Interrupts);

    if (!(Status & 0x04))
    {
        Time->Second = HalpPpcFromBcd((UCHAR)Time->Second);
        Time->Minute = HalpPpcFromBcd((UCHAR)Time->Minute);
        Time->Hour = HalpPpcFromBcd((UCHAR)Time->Hour);
        Time->Day = HalpPpcFromBcd((UCHAR)Time->Day);
        Time->Month = HalpPpcFromBcd((UCHAR)Time->Month);
        Time->Year = HalpPpcFromBcd((UCHAR)Time->Year);
    }
    Time->Year += (Time->Year < 80) ? 2000 : 1900;
    Time->Milliseconds = 0;
    Time->Weekday = 0;
    return TRUE;
}

BOOLEAN
NTAPI
HalSetRealTimeClock(_In_ PTIME_FIELDS Time)
{
    BOOLEAN Interrupts = KeDisableInterrupts();
    UCHAR Status = HalpPpcReadCmos(0x0B);
    BOOLEAN Bcd = !(Status & 0x04);
    UCHAR Year = (UCHAR)(Time->Year % 100);

    HalpPpcWriteCmos(0x0B, Status | 0x80);
    HalpPpcWriteCmos(0x00, Bcd ? HalpPpcToBcd((UCHAR)Time->Second) : (UCHAR)Time->Second);
    HalpPpcWriteCmos(0x02, Bcd ? HalpPpcToBcd((UCHAR)Time->Minute) : (UCHAR)Time->Minute);
    HalpPpcWriteCmos(0x04, Bcd ? HalpPpcToBcd((UCHAR)Time->Hour) : (UCHAR)Time->Hour);
    HalpPpcWriteCmos(0x07, Bcd ? HalpPpcToBcd((UCHAR)Time->Day) : (UCHAR)Time->Day);
    HalpPpcWriteCmos(0x08, Bcd ? HalpPpcToBcd((UCHAR)Time->Month) : (UCHAR)Time->Month);
    HalpPpcWriteCmos(0x09, Bcd ? HalpPpcToBcd(Year) : Year);
    HalpPpcWriteCmos(0x0B, Status & ~0x80);
    KeRestoreInterrupts(Interrupts);
    return TRUE;
}
