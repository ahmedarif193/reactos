/*
 * PROJECT:     LiberNT PowerPC HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Processor management (uniprocessor PReP)
 */

#include <ntifs.h>
#include <arc/arc.h>
#include "halp.h"

BOOLEAN
NTAPI
HalStartNextProcessor(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PKPROCESSOR_STATE ProcessorState)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
    UNREFERENCED_PARAMETER(ProcessorState);
    return FALSE;
}

VOID
NTAPI
HalInitializeProcessor(
    _In_ ULONG ProcessorNumber,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(ProcessorNumber);
    UNREFERENCED_PARAMETER(LoaderBlock);
}

VOID
NTAPI
HalRequestIpi(_In_ KAFFINITY TargetProcessors)
{
    UNREFERENCED_PARAMETER(TargetProcessors);
}

BOOLEAN
NTAPI
HalAllProcessorsStarted(VOID)
{
    return TRUE;
}
