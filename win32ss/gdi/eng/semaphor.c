#include <win32k.h>

#define NDEBUG
#include <debug.h>

static HSEMAPHORE ghsemSafeSemaphoreLock;

CODE_SEG("INIT")
NTSTATUS
NTAPI
InitSafeSemaphoreImpl(VOID)
{
    ghsemSafeSemaphoreLock = EngCreateSemaphore();
    if (!ghsemSafeSemaphoreLock)
        return STATUS_INSUFFICIENT_RESOURCES;

    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
__drv_allocatesMem(Mem)
_Post_writable_byte_size_(sizeof(ERESOURCE))
HSEMAPHORE
APIENTRY
EngCreateSemaphore(
    VOID)
{
    // www.osr.com/ddk/graphics/gdifncs_95lz.htm
    PERESOURCE psem = ExAllocatePoolWithTag(NonPagedPool,
                                            sizeof(ERESOURCE),
                                            GDITAG_SEMAPHORE);
    if (!psem)
        return NULL;

    if (!NT_SUCCESS(ExInitializeResourceLite(psem)))
    {
        ExFreePoolWithTag(psem, GDITAG_SEMAPHORE );
        return NULL;
    }

    return (HSEMAPHORE)psem;
}

/*
 * @implemented
 */
_Requires_lock_not_held_(*hsem)
_Acquires_exclusive_lock_(*hsem)
_Acquires_lock_(_Global_critical_region_)
VOID
APIENTRY
EngAcquireSemaphore(
    _Inout_ HSEMAPHORE hsem)
{
    // www.osr.com/ddk/graphics/gdifncs_14br.htm
    PTHREADINFO W32Thread;

    /* On Windows a NULL hsem is ignored */
    if (hsem == NULL)
    {
        DPRINT1("EngAcquireSemaphore called with hsem == NULL!\n");
        return;
    }

    ExEnterCriticalRegionAndAcquireResourceExclusive((PERESOURCE)hsem);
    W32Thread = PsGetThreadWin32Thread(PsGetCurrentThread());
    if (W32Thread) W32Thread->dwEngAcquireCount++;
}

/*
 * @implemented
 */
_Requires_lock_held_(*hsem)
_Releases_lock_(*hsem)
_Releases_lock_(_Global_critical_region_)
VOID
APIENTRY
EngReleaseSemaphore(
    _Inout_ HSEMAPHORE hsem)
{
    // www.osr.com/ddk/graphics/gdifncs_5u3r.htm
    PTHREADINFO W32Thread;

    /* On Windows a NULL hsem is ignored */
    if (hsem == NULL)
    {
        DPRINT1("EngReleaseSemaphore called with hsem == NULL!\n");
        return;
    }

    W32Thread = PsGetThreadWin32Thread(PsGetCurrentThread());
    if (W32Thread) --W32Thread->dwEngAcquireCount;
    ExReleaseResourceAndLeaveCriticalRegion((PERESOURCE)hsem);
}

_Acquires_lock_(_Global_critical_region_)
_Requires_lock_not_held_(*hsem)
_Acquires_shared_lock_(*hsem)
VOID
NTAPI
EngAcquireSemaphoreShared(
     _Inout_ HSEMAPHORE hsem)
{
    PTHREADINFO pti;

    ASSERT(hsem);
    ExEnterCriticalRegionAndAcquireResourceShared((PERESOURCE)hsem);
    pti = PsGetThreadWin32Thread(PsGetCurrentThread());
    if (pti) ++pti->dwEngAcquireCount;
}

/*
 * @implemented
 */
_Requires_lock_not_held_(*hsem)
VOID
APIENTRY
EngDeleteSemaphore(
    _Inout_ __drv_freesMem(Mem) HSEMAPHORE hsem)
{
    // www.osr.com/ddk/graphics/gdifncs_13c7.htm
    ASSERT(hsem);

    ExDeleteResourceLite((PERESOURCE)hsem);
    ExFreePoolWithTag((PVOID)hsem, GDITAG_SEMAPHORE);
}

/*
 * @implemented
 */
BOOL
APIENTRY
EngIsSemaphoreOwned(
    _In_ HSEMAPHORE hsem)
{
    // www.osr.com/ddk/graphics/gdifncs_6wmf.htm
    ASSERT(hsem);
    return (((PERESOURCE)hsem)->ActiveCount > 0);
}

/*
 * @implemented
 */
BOOL
APIENTRY
EngIsSemaphoreOwnedByCurrentThread(
    _In_ HSEMAPHORE hsem)
{
    // www.osr.com/ddk/graphics/gdifncs_9yxz.htm
    ASSERT(hsem);
    return ExIsResourceAcquiredExclusiveLite((PERESOURCE)hsem);
}

/*
 * @implemented
 */
BOOL
APIENTRY
EngInitializeSafeSemaphore(
    _Out_ ENGSAFESEMAPHORE *Semaphore)
{
    HSEMAPHORE hSem;
    BOOL Result = TRUE;

    ASSERT(ghsemSafeSemaphoreLock != NULL);
    EngAcquireSemaphore(ghsemSafeSemaphoreLock);

    if (Semaphore->lCount == 0)
    {
        /* Create the semaphore */
        hSem = EngCreateSemaphore();
        if (hSem == NULL)
        {
            Result = FALSE;
        }
        else
        {
            Semaphore->hsem = hSem;
        }
    }

    if (Result)
        ++Semaphore->lCount;

    EngReleaseSemaphore(ghsemSafeSemaphoreLock);
    return Result;
}

/*
 * @implemented
 */
VOID
APIENTRY
EngDeleteSafeSemaphore(
    _Inout_ _Post_invalid_ ENGSAFESEMAPHORE *pssem)
{
    ASSERT(ghsemSafeSemaphoreLock != NULL);
    EngAcquireSemaphore(ghsemSafeSemaphoreLock);

    ASSERT(pssem->lCount > 0);
    if (pssem->lCount == 1)
    {
        EngDeleteSemaphore(pssem->hsem);
        pssem->hsem = NULL;
    }

    --pssem->lCount;
    EngReleaseSemaphore(ghsemSafeSemaphoreLock);
}

/* EOF */
