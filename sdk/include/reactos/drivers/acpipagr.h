/*
 * ReactOS processor-aggregator kernel interface
 * Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

NTKERNELAPI
NTSTATUS
NTAPI
PoSetProcessorAggregatorParking(
    _In_ ULONG RequestedParkedProcessors,
    _Out_ PULONG ParkedProcessors);
