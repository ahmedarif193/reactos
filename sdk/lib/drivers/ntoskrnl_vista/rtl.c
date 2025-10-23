/*
 * PROJECT:     ReactOS drivers import library for Windows Vista ntoskrnl
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Minimal runtime helpers required by the CRT when linked
 *              into kernel modules.
 */

#include <ntifs.h>

NTKRNLVISTAAPI
VOID
NTAPI
RtlRaiseStatus(
    _In_ NTSTATUS Status)
{
    ExRaiseStatus(Status);
}
