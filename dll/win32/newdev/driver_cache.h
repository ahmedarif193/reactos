/*
 * PROJECT:     ReactOS New Device Installer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Batch driver-discovery cache and same-key serialization
 */

#pragma once

#include <windef.h>
#include <winbase.h>

typedef struct _NEWDEV_DRIVER_CACHE_ENTRY NEWDEV_DRIVER_CACHE_ENTRY, *PNEWDEV_DRIVER_CACHE_ENTRY;

typedef enum _NEWDEV_DRIVER_CACHE_RESULT
{
    NewDevDriverCacheUnknown,
    NewDevDriverCacheFound,
    NewDevDriverCacheNotFoundPending,
    NewDevDriverCacheNotFound
} NEWDEV_DRIVER_CACHE_RESULT;

typedef enum _NEWDEV_DRIVER_CACHE_ACQUIRE
{
    NewDevDriverCacheInactive,
    NewDevDriverCacheAcquired,
    NewDevDriverCacheBusy,
    NewDevDriverCacheError
} NEWDEV_DRIVER_CACHE_ACQUIRE;

VOID
NewDevDriverCacheInitialize(VOID);

VOID
NewDevDriverCacheUninitialize(VOID);

VOID
NewDevDriverCacheBeginBatch(VOID);

VOID
NewDevDriverCacheEndBatch(VOID);

NEWDEV_DRIVER_CACHE_ACQUIRE
NewDevDriverCacheTryAcquire(
    IN const GUID *ClassGuid,
    IN LPCWSTR HardwareIds OPTIONAL,
    IN LPCWSTR CompatibleIds OPTIONAL,
    OUT PNEWDEV_DRIVER_CACHE_ENTRY *CacheEntry);

BOOL
NewDevDriverCacheWait(
    IN PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry);

NEWDEV_DRIVER_CACHE_RESULT
NewDevDriverCacheGetResult(
    IN PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry,
    OUT LPWSTR InfFileName,
    IN DWORD InfFileNameCch,
    OUT PBOOL NullDriver);

VOID
NewDevDriverCacheRememberFound(
    IN PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry,
    IN LPCWSTR InfFileName,
    IN BOOL NullDriver);

VOID
NewDevDriverCacheRememberNotFoundPending(
    IN PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry);

VOID
NewDevDriverCacheRememberNotFound(
    IN PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry);

VOID
NewDevDriverCacheInvalidate(
    IN PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry);

VOID
NewDevDriverCacheRelease(
    IN PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry);
