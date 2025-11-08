/*
 * PROJECT:     ReactOS Mini-HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Initialize the x86 HAL
 * COPYRIGHT:   Copyright 1998 David Welch <welch@cwcom.net>
 */

/* INCLUDES *****************************************************************/

#include <hal.h>
#define NDEBUG
#include <debug.h>

/* Mini-HALs never bring up more than the bootstrap processor.  Default to CPU0. */
KAFFINITY HalpDefaultInterruptAffinity = 1;

/* FUNCTIONS ***************************************************************/

VOID
NTAPI
HalpInitProcessor(
    IN ULONG ProcessorNumber,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
}

VOID
HalpInitPhase0(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
}

VOID
HalpInitPhase1(VOID)
{
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpSetupAcpiPhase0(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    return STATUS_SUCCESS;
}

VOID
NTAPI
HalpInitializePICs(IN BOOLEAN EnableInterrupts)
{
}

PDMA_ADAPTER
NTAPI
HalpGetDmaAdapter(
    IN PVOID Context,
    IN PDEVICE_DESCRIPTION DeviceDescription,
    OUT PULONG NumberOfMapRegisters)
{
    return NULL;
}

BOOLEAN
NTAPI
HalpBiosDisplayReset(VOID)
{
    return FALSE;
}

ULONG
NTAPI
HalGetBusData(
    IN BUS_DATA_TYPE BusDataType,
    IN ULONG SystemIoBusNumber,
    IN ULONG SlotNumber,
    OUT PVOID Buffer,
    IN ULONG Length)
{
    UNREFERENCED_PARAMETER(BusDataType);
    UNREFERENCED_PARAMETER(SystemIoBusNumber);
    UNREFERENCED_PARAMETER(SlotNumber);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
    return 0;
}

#if !defined(MINIHAL_HAS_BUS_SUPPORT)
ULONG
NTAPI
HalGetBusDataByOffset(
    IN BUS_DATA_TYPE BusDataType,
    IN ULONG SystemIoBusNumber,
    IN ULONG SlotNumber,
    OUT PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length)
{
    UNREFERENCED_PARAMETER(BusDataType);
    UNREFERENCED_PARAMETER(SystemIoBusNumber);
    UNREFERENCED_PARAMETER(SlotNumber);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Offset);
    UNREFERENCED_PARAMETER(Length);
    return 0;
}
#endif

/* EOF */
