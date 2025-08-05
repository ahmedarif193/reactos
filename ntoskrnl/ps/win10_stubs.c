/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Windows 10 Process Manager compatibility stubs
 * COPYRIGHT:       Copyright 2024 ReactOS Team
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS ****************************************************************/

/*
 * @unimplemented
 */
BOOLEAN
NTAPI
PsIsDiskCountersEnabled(VOID)
{
    /* Windows 10 disk counters check */
    UNIMPLEMENTED;
    return FALSE;
}

/* EOF */