/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Windows 10 Push Lock compatibility functions
 * COPYRIGHT:       Copyright 2024 ReactOS Team
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS ****************************************************************/

/*
 * @implemented
 */
VOID
FASTCALL
KeAcquirePushLockExclusive(
    IN PEX_PUSH_LOCK PushLock)
{
    /* Windows 10 API - redirect to Ex function */
    ExAcquirePushLockExclusive(PushLock);
}

/*
 * @implemented
 */
VOID
FASTCALL
KeReleasePushLockExclusive(
    IN PEX_PUSH_LOCK PushLock)
{
    /* Windows 10 API - redirect to Ex function */
    ExReleasePushLockExclusive(PushLock);
}

/*
 * @implemented
 */
BOOLEAN
FASTCALL
ExIsAcquiredPushLockExclusive(
    IN PEX_PUSH_LOCK PushLock)
{
    /* Check if the push lock is exclusively owned */
    EX_PUSH_LOCK OldValue = *PushLock;
    
    /* In Windows 10, the lock is exclusive if the low bit is set and waiting bit is clear */
    return (OldValue.Locked && !OldValue.Waiting);
}

/* EOF */