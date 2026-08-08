/*
 * PROJECT:     ReactOS Kernel - Vista+ APIs
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Po functions of Vista+
 * COPYRIGHT:   2020 Victor Perevertkin (victor.perevertkin@reactos.org)
 */

#include <ntdef.h>
#include <ntifs.h>

_IRQL_requires_max_(DISPATCH_LEVEL)
NTKRNLVISTAAPI
BOOLEAN
NTAPI
PoQueryWatchdogTime(
    _In_ PDEVICE_OBJECT Pdo,
    _Out_ PULONG SecondsRemaining)
{
    return FALSE;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTKRNLVISTAAPI
VOID
NTAPI
PoSetSystemWake(
    _Inout_ struct _IRP *Irp)
{

}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTKRNLVISTAAPI
BOOLEAN
NTAPI
PoGetSystemWake(
    _In_ struct _IRP *Irp)
{
    return FALSE;
}
