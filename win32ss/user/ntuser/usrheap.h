#pragma once

typedef struct _WIN32HEAP WIN32HEAP, *PWIN32HEAP;

/*
typedef struct _W32HEAP_USER_MAPPING
{
    struct _W32HEAP_USER_MAPPING* Next;
    PVOID KernelMapping;
    PVOID UserMapping;
    ULONG_PTR Limit;
    ULONG Count;
} W32HEAP_USER_MAPPING, *PW32HEAP_USER_MAPPING;
*/

/* User heap */
extern HANDLE GlobalUserHeap;
extern PVOID GlobalUserHeapSection;
/* Serializes the HEAP_NO_SERIALIZE global user heap so heap-allocating callers
 * only need the global USER lock entered (shared or exclusive), not exclusive
 * (per-desktop split). Innermost lock: never held across a USER/desktop-lock
 * acquire or a callout. */
extern ERESOURCE GlobalUserHeapLock;

PWIN32HEAP
UserCreateHeap(OUT PVOID *SectionObject,
               IN OUT PVOID *SystemBase,
               IN SIZE_T HeapSize);

NTSTATUS
UnmapGlobalUserHeap(IN PEPROCESS Process);

NTSTATUS
MapGlobalUserHeap(IN  PEPROCESS Process,
                  OUT PVOID* KernelMapping,
                  OUT PVOID* UserMapping);

static __inline PVOID
UserHeapAlloc(SIZE_T Bytes)
{
    PVOID Ret;
    ASSERT(UserIsEntered());
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&GlobalUserHeapLock, TRUE);
    Ret = RtlAllocateHeap(GlobalUserHeap,
                          HEAP_NO_SERIALIZE,
                          Bytes);
    ExReleaseResourceLite(&GlobalUserHeapLock);
    KeLeaveCriticalRegion();
    return Ret;
}

static __inline BOOL
UserHeapFree(PVOID lpMem)
{
    BOOL Ret;
    ASSERT(UserIsEntered());
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&GlobalUserHeapLock, TRUE);
    Ret = RtlFreeHeap(GlobalUserHeap,
                      HEAP_NO_SERIALIZE,
                      lpMem);
    ExReleaseResourceLite(&GlobalUserHeapLock);
    KeLeaveCriticalRegion();
    return Ret;
}

static __inline PVOID
UserHeapReAlloc(PVOID lpMem,
                SIZE_T Bytes)
{
    /* NOTE: ntoskrnl doesn't export RtlReAllocateHeap, so do it by hand. */
    SIZE_T PrevSize;
    PVOID pNew;

    ASSERT(UserIsEntered());
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&GlobalUserHeapLock, TRUE);

    PrevSize = RtlSizeHeap(GlobalUserHeap,
                           HEAP_NO_SERIALIZE,
                           lpMem);

    if (PrevSize == Bytes)
    {
        ExReleaseResourceLite(&GlobalUserHeapLock);
        KeLeaveCriticalRegion();
        return lpMem;
    }

    pNew = RtlAllocateHeap(GlobalUserHeap,
                           HEAP_NO_SERIALIZE,
                           Bytes);
    if (pNew != NULL)
    {
        if (PrevSize < Bytes)
            Bytes = PrevSize;

        RtlCopyMemory(pNew,
                      lpMem,
                      Bytes);

        RtlFreeHeap(GlobalUserHeap,
                    HEAP_NO_SERIALIZE,
                    lpMem);
    }

    ExReleaseResourceLite(&GlobalUserHeapLock);
    KeLeaveCriticalRegion();
    return pNew;
}

static __inline PVOID
UserHeapAddressToUser(PVOID lpMem)
{
    PPROCESSINFO W32Process = PsGetCurrentProcessWin32Process();

    /* The first mapping entry is the global user heap mapping */
    return (PVOID)(((ULONG_PTR)lpMem - (ULONG_PTR)GlobalUserHeap) +
                   (ULONG_PTR)W32Process->HeapMappings.UserMapping);
}

/* EOF */
