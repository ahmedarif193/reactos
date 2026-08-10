/*
 * PROJECT:     ReactOS New Device Installer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Batch driver-discovery cache and same-key serialization
 */

#include <windef.h>
#include <winbase.h>
#include <string.h>
#include <wchar.h>

#include "driver_cache.h"

struct _NEWDEV_DRIVER_CACHE_ENTRY
{
    struct _NEWDEV_DRIVER_CACHE_ENTRY *Next;
    CONDITION_VARIABLE Idle;
    BOOL Busy;
    NEWDEV_DRIVER_CACHE_RESULT Result;
    GUID ClassGuid;
    SIZE_T HardwareIdsSize;
    SIZE_T CompatibleIdsSize;
    BOOL NullDriver;
    WCHAR InfFileName[MAX_PATH];
    BYTE Ids[1];
};

static PNEWDEV_DRIVER_CACHE_ENTRY DriverCache;
static NEWDEV_DRIVER_CACHE_ENTRY DriverCacheFallback;
static CRITICAL_SECTION DriverCacheLock;
static BOOL DriverCacheReady;
static LONG DriverCacheBatchDepth;

static SIZE_T
MultiSzSize(
    IN LPCWSTR IdList OPTIONAL)
{
    LPCWSTR Id;

    if (!IdList)
        return 0;

    for (Id = IdList; *Id; Id += wcslen(Id) + 1)
    {
    }

    return (Id - IdList + 1) * sizeof(WCHAR);
}

static BOOL
MultiSzEqual(
    IN LPCWSTR First OPTIONAL,
    IN SIZE_T FirstSize,
    IN LPCWSTR Second OPTIONAL,
    IN SIZE_T SecondSize)
{
    LPCWSTR FirstId = First;
    LPCWSTR SecondId = Second;

    if (FirstSize != SecondSize)
        return FALSE;

    if (FirstSize == 0)
        return TRUE;

    while (*FirstId && *SecondId)
    {
        if (_wcsicmp(FirstId, SecondId))
            return FALSE;

        FirstId += wcslen(FirstId) + 1;
        SecondId += wcslen(SecondId) + 1;
    }

    return !*FirstId && !*SecondId;
}

static BOOL
DriverCacheEntryMatches(
    IN PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry,
    IN const GUID *ClassGuid,
    IN LPCWSTR HardwareIds OPTIONAL,
    IN SIZE_T HardwareIdsSize,
    IN LPCWSTR CompatibleIds OPTIONAL,
    IN SIZE_T CompatibleIdsSize)
{
    if (memcmp(&CacheEntry->ClassGuid, ClassGuid, sizeof(*ClassGuid)))
        return FALSE;

    if (!MultiSzEqual((LPCWSTR)CacheEntry->Ids, CacheEntry->HardwareIdsSize, HardwareIds, HardwareIdsSize))
        return FALSE;

    return MultiSzEqual((LPCWSTR)(CacheEntry->Ids + CacheEntry->HardwareIdsSize), CacheEntry->CompatibleIdsSize, CompatibleIds, CompatibleIdsSize);
}

static VOID
DriverCacheFreeList(
    IN PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry)
{
    PNEWDEV_DRIVER_CACHE_ENTRY NextEntry;

    while (CacheEntry)
    {
        NextEntry = CacheEntry->Next;
        HeapFree(GetProcessHeap(), 0, CacheEntry);
        CacheEntry = NextEntry;
    }
}

VOID
NewDevDriverCacheInitialize(VOID)
{
    InitializeCriticalSection(&DriverCacheLock);
    InitializeConditionVariable(&DriverCacheFallback.Idle);
    DriverCacheReady = TRUE;
}

VOID
NewDevDriverCacheUninitialize(VOID)
{
    PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry;

    if (!DriverCacheReady)
        return;

    EnterCriticalSection(&DriverCacheLock);
    CacheEntry = DriverCache;
    DriverCache = NULL;
    DriverCacheBatchDepth = 0;
    DriverCacheFallback.Busy = FALSE;
    WakeAllConditionVariable(&DriverCacheFallback.Idle);
    DriverCacheReady = FALSE;
    LeaveCriticalSection(&DriverCacheLock);

    DriverCacheFreeList(CacheEntry);
    DeleteCriticalSection(&DriverCacheLock);
}

VOID
NewDevDriverCacheBeginBatch(VOID)
{
    if (!DriverCacheReady)
        return;

    EnterCriticalSection(&DriverCacheLock);
    DriverCacheBatchDepth++;
    LeaveCriticalSection(&DriverCacheLock);
}

VOID
NewDevDriverCacheEndBatch(VOID)
{
    PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry = NULL;

    if (!DriverCacheReady)
        return;

    EnterCriticalSection(&DriverCacheLock);
    if (DriverCacheBatchDepth != 0 && --DriverCacheBatchDepth == 0)
    {
        CacheEntry = DriverCache;
        DriverCache = NULL;
    }
    LeaveCriticalSection(&DriverCacheLock);

    DriverCacheFreeList(CacheEntry);
}

NEWDEV_DRIVER_CACHE_ACQUIRE
NewDevDriverCacheTryAcquire(
    IN const GUID *ClassGuid,
    IN LPCWSTR HardwareIds OPTIONAL,
    IN LPCWSTR CompatibleIds OPTIONAL,
    OUT PNEWDEV_DRIVER_CACHE_ENTRY *CacheEntry)
{
    PNEWDEV_DRIVER_CACHE_ENTRY CurrentEntry;
    SIZE_T HardwareIdsSize;
    SIZE_T CompatibleIdsSize;
    SIZE_T EntrySize;

    *CacheEntry = NULL;

    if (!DriverCacheReady)
        return NewDevDriverCacheInactive;

    HardwareIdsSize = MultiSzSize(HardwareIds);
    CompatibleIdsSize = MultiSzSize(CompatibleIds);
    if (HardwareIdsSize == 0 && CompatibleIdsSize == 0)
        return NewDevDriverCacheInactive;

    EnterCriticalSection(&DriverCacheLock);

    if (DriverCacheBatchDepth == 0)
    {
        LeaveCriticalSection(&DriverCacheLock);
        return NewDevDriverCacheInactive;
    }

    if (DriverCacheFallback.Busy)
    {
        *CacheEntry = &DriverCacheFallback;
        LeaveCriticalSection(&DriverCacheLock);
        return NewDevDriverCacheBusy;
    }

    for (CurrentEntry = DriverCache; CurrentEntry; CurrentEntry = CurrentEntry->Next)
    {
        if (DriverCacheEntryMatches(CurrentEntry, ClassGuid, HardwareIds, HardwareIdsSize, CompatibleIds, CompatibleIdsSize))
            break;
    }

    if (!CurrentEntry)
    {
        EntrySize = FIELD_OFFSET(NEWDEV_DRIVER_CACHE_ENTRY, Ids) + HardwareIdsSize + CompatibleIdsSize;
        CurrentEntry = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, EntrySize);
        if (!CurrentEntry)
        {
            DriverCacheFallback.Busy = TRUE;
            *CacheEntry = &DriverCacheFallback;
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            LeaveCriticalSection(&DriverCacheLock);
            return NewDevDriverCacheAcquired;
        }

        InitializeConditionVariable(&CurrentEntry->Idle);
        memcpy(&CurrentEntry->ClassGuid, ClassGuid, sizeof(*ClassGuid));
        CurrentEntry->HardwareIdsSize = HardwareIdsSize;
        CurrentEntry->CompatibleIdsSize = CompatibleIdsSize;
        if (HardwareIdsSize)
            memcpy(CurrentEntry->Ids, HardwareIds, HardwareIdsSize);
        if (CompatibleIdsSize)
            memcpy(CurrentEntry->Ids + HardwareIdsSize, CompatibleIds, CompatibleIdsSize);
        CurrentEntry->Next = DriverCache;
        DriverCache = CurrentEntry;
    }

    *CacheEntry = CurrentEntry;
    if (CurrentEntry->Busy)
    {
        LeaveCriticalSection(&DriverCacheLock);
        return NewDevDriverCacheBusy;
    }

    CurrentEntry->Busy = TRUE;
    LeaveCriticalSection(&DriverCacheLock);
    return NewDevDriverCacheAcquired;
}

BOOL
NewDevDriverCacheWait(
    IN PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry)
{
    DWORD Error;

    EnterCriticalSection(&DriverCacheLock);
    while (CacheEntry->Busy)
    {
        if (!SleepConditionVariableCS(&CacheEntry->Idle, &DriverCacheLock, INFINITE))
        {
            Error = GetLastError();
            LeaveCriticalSection(&DriverCacheLock);
            SetLastError(Error);
            return FALSE;
        }
    }
    LeaveCriticalSection(&DriverCacheLock);
    return TRUE;
}

NEWDEV_DRIVER_CACHE_RESULT
NewDevDriverCacheGetResult(
    IN PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry,
    OUT LPWSTR InfFileName,
    IN DWORD InfFileNameCch,
    OUT PBOOL NullDriver)
{
    if (CacheEntry == &DriverCacheFallback)
        return NewDevDriverCacheUnknown;

    if (CacheEntry->Result == NewDevDriverCacheFound && InfFileName && InfFileNameCch != 0 && NullDriver)
    {
        lstrcpynW(InfFileName, CacheEntry->InfFileName, InfFileNameCch);
        *NullDriver = CacheEntry->NullDriver;
    }

    return CacheEntry->Result;
}

VOID
NewDevDriverCacheRememberFound(
    IN PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry,
    IN LPCWSTR InfFileName,
    IN BOOL NullDriver)
{
    if (CacheEntry == &DriverCacheFallback || !InfFileName || !*InfFileName)
        return;

    lstrcpynW(CacheEntry->InfFileName, InfFileName, sizeof(CacheEntry->InfFileName) / sizeof(CacheEntry->InfFileName[0]));
    CacheEntry->NullDriver = NullDriver;
    CacheEntry->Result = NewDevDriverCacheFound;
}

VOID
NewDevDriverCacheRememberNotFoundPending(
    IN PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry)
{
    if (CacheEntry != &DriverCacheFallback)
        CacheEntry->Result = NewDevDriverCacheNotFoundPending;
}

VOID
NewDevDriverCacheRememberNotFound(
    IN PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry)
{
    if (CacheEntry != &DriverCacheFallback)
        CacheEntry->Result = NewDevDriverCacheNotFound;
}

VOID
NewDevDriverCacheInvalidate(
    IN PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry)
{
    if (CacheEntry == &DriverCacheFallback)
        return;

    CacheEntry->Result = NewDevDriverCacheUnknown;
    CacheEntry->InfFileName[0] = UNICODE_NULL;
    CacheEntry->NullDriver = FALSE;
}

VOID
NewDevDriverCacheRelease(
    IN PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry)
{
    EnterCriticalSection(&DriverCacheLock);
    CacheEntry->Busy = FALSE;
    WakeAllConditionVariable(&CacheEntry->Idle);
    LeaveCriticalSection(&DriverCacheLock);
}
