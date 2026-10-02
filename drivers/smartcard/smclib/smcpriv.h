/*
 * PROJECT:     LiberNT Smart Card Driver Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _SMCLIB_PRIVATE_H_
#define _SMCLIB_PRIVATE_H_

#define _SMCLIBSYSTEM_

#include <ntddk.h>
#include <ntstrsafe.h>
#include <smclib.h>

#define SMCLIB_TAG 'bilS'

extern CLOCK_RATE_CONVERSION SmcClockRateConversion[16];
extern BIT_RATE_ADJUSTMENT SmcBitRateAdjustment[16];

BOOLEAN
SmcIsNegotiable(
    _In_ PSMARTCARD_EXTENSION SmartcardExtension,
    _Out_ PULONG Selected);

VOID
SmcSelectTransmission(
    _In_ PSMARTCARD_EXTENSION SmartcardExtension,
    _In_ BOOLEAN Optimal);

#endif
