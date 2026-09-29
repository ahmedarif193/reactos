/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/config/cmmapvw.c
 * PURPOSE:         Configuration Manager - Map-Viewed Hive Support
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include "ntoskrnl.h"
#define NDEBUG
#include "debug.h"

/* GLOBALS *******************************************************************/

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
CmpInitHiveViewList(IN PCMHIVE Hive)
{
    RtlZeroMemory(&Hive->Hive.ViewMap, sizeof(Hive->Hive.ViewMap));
}

VOID
NTAPI
CmpDestroyHiveViewList(IN PCMHIVE Hive)
{
    ASSERT(Hive->Hive.ViewMap.ViewTree.Root == NULL);
}

/* EOF */
