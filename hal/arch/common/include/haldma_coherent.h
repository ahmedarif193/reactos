/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Coherent, direct-mapped bus-master DMA helpers
 */

#pragma once

NTSTATUS NTAPI
HalpCoherentGetScatterGatherList(PDMA_ADAPTER Adapter, PDEVICE_OBJECT DeviceObject,
                                PMDL Mdl, PVOID CurrentVa, ULONG Length,
                                PDRIVER_LIST_CONTROL ExecutionRoutine, PVOID Context,
                                BOOLEAN WriteToDevice);
VOID NTAPI
HalpCoherentPutScatterGatherList(PDMA_ADAPTER Adapter,
                                PSCATTER_GATHER_LIST List, BOOLEAN WriteToDevice);
