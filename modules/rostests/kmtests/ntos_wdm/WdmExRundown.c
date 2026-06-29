/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     WDM executive rundown protection behavior tests
 */

#include <kmt_test.h>

#define TAG_RUNDOWN 'dRmK'

static
VOID
TestRundownRef(VOID)
{
    EX_RUNDOWN_REF RunRef;
    BOOLEAN Ret;

    ExInitializeRundownProtection(&RunRef);

    Ret = ExAcquireRundownProtection(&RunRef);
    ok_bool_true(Ret, "ExAcquireRundownProtection returned");
    ExReleaseRundownProtection(&RunRef);

    Ret = ExAcquireRundownProtectionEx(&RunRef, 3);
    ok_bool_true(Ret, "ExAcquireRundownProtectionEx returned");
    ExReleaseRundownProtectionEx(&RunRef, 3);

    ExWaitForRundownProtectionRelease(&RunRef);
    Ret = ExAcquireRundownProtection(&RunRef);
    ok_bool_false(Ret, "ExAcquireRundownProtection returned after rundown started");

    ExRundownCompleted(&RunRef);
    ExReInitializeRundownProtection(&RunRef);

    Ret = ExAcquireRundownProtection(&RunRef);
    ok_bool_true(Ret, "ExAcquireRundownProtection returned after reinitialize");
    ExReleaseRundownProtection(&RunRef);
}

static
VOID
TestAllocatedCacheAwareRundown(VOID)
{
    PEX_RUNDOWN_REF_CACHE_AWARE RunRef;
    BOOLEAN Ret;
    SIZE_T Size;

    Size = ExSizeOfRundownProtectionCacheAware();
    ok(Size >= sizeof(EX_RUNDOWN_REF_CACHE_AWARE) + sizeof(EX_RUNDOWN_REF),
       "ExSizeOfRundownProtectionCacheAware returned %Iu\n", Size);

    RunRef = ExAllocateCacheAwareRundownProtection(NonPagedPool, TAG_RUNDOWN);
    ok(RunRef != NULL, "ExAllocateCacheAwareRundownProtection returned NULL\n");
    if (!RunRef)
        return;

    Ret = ExAcquireRundownProtectionCacheAware(RunRef);
    ok_bool_true(Ret, "ExAcquireRundownProtectionCacheAware returned");
    ExReleaseRundownProtectionCacheAware(RunRef);

    Ret = ExAcquireRundownProtectionCacheAwareEx(RunRef, 2);
    ok_bool_true(Ret, "ExAcquireRundownProtectionCacheAwareEx returned");
    ExReleaseRundownProtectionCacheAwareEx(RunRef, 2);

    ExWaitForRundownProtectionReleaseCacheAware(RunRef);
    Ret = ExAcquireRundownProtectionCacheAware(RunRef);
    ok_bool_false(Ret, "ExAcquireRundownProtectionCacheAware returned after rundown started");

    ExRundownCompletedCacheAware(RunRef);
    ExFreeCacheAwareRundownProtection(RunRef);
}

static
VOID
TestCallerAllocatedCacheAwareRundown(VOID)
{
    PEX_RUNDOWN_REF_CACHE_AWARE RunRef;
    BOOLEAN Ret;
    SIZE_T Size;

    Size = ExSizeOfRundownProtectionCacheAware();
    RunRef = ExAllocatePoolWithTag(NonPagedPool, Size, TAG_RUNDOWN);
    ok(RunRef != NULL, "ExAllocatePoolWithTag returned NULL for %Iu-byte cache-aware rundown\n", Size);
    if (!RunRef)
        return;

    RtlFillMemory(RunRef, Size, 0x55);
    ExInitializeRundownProtectionCacheAware(RunRef, Size);

    Ret = ExAcquireRundownProtectionCacheAware(RunRef);
    ok_bool_true(Ret, "ExAcquireRundownProtectionCacheAware returned");
    ExReleaseRundownProtectionCacheAware(RunRef);

    ExWaitForRundownProtectionReleaseCacheAware(RunRef);
    Ret = ExAcquireRundownProtectionCacheAware(RunRef);
    ok_bool_false(Ret, "ExAcquireRundownProtectionCacheAware returned after caller-allocated rundown started");

    ExRundownCompletedCacheAware(RunRef);
    ExReInitializeRundownProtectionCacheAware(RunRef);

    Ret = ExAcquireRundownProtectionCacheAware(RunRef);
    ok_bool_true(Ret, "ExAcquireRundownProtectionCacheAware returned after cache-aware reinitialize");
    ExReleaseRundownProtectionCacheAware(RunRef);

    ExFreePoolWithTag(RunRef, TAG_RUNDOWN);
}

START_TEST(WdmExRundown)
{
    TestRundownRef();
    TestAllocatedCacheAwareRundown();
    TestCallerAllocatedCacheAwareRundown();
}
