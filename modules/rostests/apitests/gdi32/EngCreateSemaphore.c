/*
 * PROJECT:         ReactOS api tests
 * LICENSE:         GPL - See COPYING in the top level directory
 * PURPOSE:         Test for EngCreateSemaphore
 * PROGRAMMERS:     Magnus Olsen
 */

#include "precomp.h"

void Test_EngCreateSemaphore()
{
    HSEMAPHORE hsem;
    PRTL_CRITICAL_SECTION lpcrit;

    hsem = EngCreateSemaphore();
    ok(hsem != NULL, "EngCreateSemaphore failed\n");
    if (!hsem) return;
    lpcrit = (PRTL_CRITICAL_SECTION)hsem;

    ok(lpcrit->LockCount == -1, "lpcrit->LockCount=%ld\n", lpcrit->LockCount);
    ok(lpcrit->RecursionCount == 0, "lpcrit->RecursionCount=%ld\n", lpcrit->RecursionCount);
    ok(lpcrit->OwningThread == 0, "lpcrit->OwningThread=%p\n", lpcrit->OwningThread);
    ok(lpcrit->LockSemaphore == 0, "lpcrit->LockSemaphore=%p\n", lpcrit->LockSemaphore);
    ok((lpcrit->SpinCount & RTL_CRITICAL_SECTION_ALL_FLAG_BITS) == RTL_CRITICAL_SECTION_FLAG_DYNAMIC_SPIN,
       "lpcrit->SpinCount=%Iu\n", lpcrit->SpinCount);
    ok(lpcrit->DebugInfo == (PRTL_CRITICAL_SECTION_DEBUG)-1, "lpcrit->DebugInfo=%p\n", lpcrit->DebugInfo);

    EngDeleteSemaphore(hsem);
}

START_TEST(EngCreateSemaphore)
{
    Test_EngCreateSemaphore();
}
