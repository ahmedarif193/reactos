/*
 * New device installer (newdev.dll)
 *
 * Copyright 2005-2006 Hervé Poussineau (hpoussin@reactos.org)
 *           2005 Christoph von Wittich (Christoph@ActiveVB.de)
 *           2009 Colin Finck (colin@reactos.org)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "newdev_private.h"

#include <cfgmgr32.h>
#include <stdio.h>
#include <stdlib.h>
#include <winnls.h>

#define NEWDEV_BATCH_MAX_DEVICES 65536
#define NEWDEV_BATCH_MAX_WORKERS 1
#define NEWDEV_BATCH_MAX_DEPTH   64
#define NEWDEV_BATCH_DEPTH_UNKNOWN ((ULONG)-1)

/* Global variables */
HINSTANCE hDllInstance;

typedef struct _NO_DRIVER_ID_ENTRY
{
    struct _NO_DRIVER_ID_ENTRY *Next;
    WCHAR Id[1];
} NO_DRIVER_ID_ENTRY, *PNO_DRIVER_ID_ENTRY;

typedef struct _DRIVER_MATCH_CACHE_ENTRY
{
    struct _DRIVER_MATCH_CACHE_ENTRY *Next;
    SIZE_T HardwareIdsSize;
    SIZE_T CompatibleIdsSize;
    BOOL NullDriver;
    WCHAR InfFileName[MAX_PATH];
    BYTE Ids[1];
} DRIVER_MATCH_CACHE_ENTRY, *PDRIVER_MATCH_CACHE_ENTRY;

static PNO_DRIVER_ID_ENTRY NoDriverIdCache;
static PDRIVER_MATCH_CACHE_ENTRY DriverMatchCache;
static CRITICAL_SECTION NoDriverIdCacheLock;
static BOOL NoDriverIdCacheReady;
static LONG NoDriverIdCacheBatchDepth;

typedef struct _DEVICE_INSTALL_PHASE_LOCK
{
    CRITICAL_SECTION StateLock;
    CONDITION_VARIABLE StateChanged;
    DWORD ActiveReaders;
    DWORD WaitingWriters;
    BOOL WriterActive;
} DEVICE_INSTALL_PHASE_LOCK;

/* Keep discovery parallel, but block waiters while a long commit is active. */
static DEVICE_INSTALL_PHASE_LOCK DeviceInstallPhaseLock;

typedef struct _BATCH_DEVICE
{
    PWSTR InstanceId;
    ULONG Depth;
} BATCH_DEVICE, *PBATCH_DEVICE;

typedef struct _BATCH_WORK_CONTEXT
{
    PBATCH_DEVICE Devices;
    volatile LONG NextIndex;
    LONG EndIndex;
} BATCH_WORK_CONTEXT, *PBATCH_WORK_CONTEXT;

static BOOL
SearchDriver(
    IN PDEVINSTDATA DevInstData,
    IN LPCWSTR Directory OPTIONAL,
    IN LPCWSTR InfFile OPTIONAL);

static BOOL
InstallNullDriver(
    IN PDEVINSTDATA DevInstData);

static VOID
InitializeDeviceInstallPhaseLock(VOID)
{
    InitializeCriticalSection(&DeviceInstallPhaseLock.StateLock);
    InitializeConditionVariable(&DeviceInstallPhaseLock.StateChanged);
    DeviceInstallPhaseLock.ActiveReaders = 0;
    DeviceInstallPhaseLock.WaitingWriters = 0;
    DeviceInstallPhaseLock.WriterActive = FALSE;
}

static VOID
DeleteDeviceInstallPhaseLock(VOID)
{
    DeleteCriticalSection(&DeviceInstallPhaseLock.StateLock);
}

static VOID
AcquireDeviceInstallPhaseShared(VOID)
{
    EnterCriticalSection(&DeviceInstallPhaseLock.StateLock);
    while (DeviceInstallPhaseLock.WriterActive || DeviceInstallPhaseLock.WaitingWriters != 0)
        SleepConditionVariableCS(&DeviceInstallPhaseLock.StateChanged, &DeviceInstallPhaseLock.StateLock, INFINITE);
    DeviceInstallPhaseLock.ActiveReaders++;
    LeaveCriticalSection(&DeviceInstallPhaseLock.StateLock);
}

static VOID
ReleaseDeviceInstallPhaseShared(VOID)
{
    EnterCriticalSection(&DeviceInstallPhaseLock.StateLock);
    if (DeviceInstallPhaseLock.ActiveReaders == 0)
    {
        ERR("Device install phase reader count underflow\n");
        LeaveCriticalSection(&DeviceInstallPhaseLock.StateLock);
        return;
    }
    DeviceInstallPhaseLock.ActiveReaders--;
    if (DeviceInstallPhaseLock.ActiveReaders == 0)
        WakeAllConditionVariable(&DeviceInstallPhaseLock.StateChanged);
    LeaveCriticalSection(&DeviceInstallPhaseLock.StateLock);
}

static VOID
AcquireDeviceInstallPhaseExclusive(VOID)
{
    EnterCriticalSection(&DeviceInstallPhaseLock.StateLock);
    DeviceInstallPhaseLock.WaitingWriters++;
    while (DeviceInstallPhaseLock.WriterActive || DeviceInstallPhaseLock.ActiveReaders != 0)
        SleepConditionVariableCS(&DeviceInstallPhaseLock.StateChanged, &DeviceInstallPhaseLock.StateLock, INFINITE);
    DeviceInstallPhaseLock.WaitingWriters--;
    DeviceInstallPhaseLock.WriterActive = TRUE;
    LeaveCriticalSection(&DeviceInstallPhaseLock.StateLock);
}

static VOID
ReleaseDeviceInstallPhaseExclusive(VOID)
{
    EnterCriticalSection(&DeviceInstallPhaseLock.StateLock);
    if (!DeviceInstallPhaseLock.WriterActive)
    {
        ERR("Device install phase writer released while inactive\n");
        LeaveCriticalSection(&DeviceInstallPhaseLock.StateLock);
        return;
    }
    DeviceInstallPhaseLock.WriterActive = FALSE;
    WakeAllConditionVariable(&DeviceInstallPhaseLock.StateChanged);
    LeaveCriticalSection(&DeviceInstallPhaseLock.StateLock);
}

static BOOL
DeviceInstallPhasePromote(
    IN OUT PBOOL SharedPhaseLockHeld)
{
    BOOL WasShared = *SharedPhaseLockHeld;

    if (WasShared)
    {
        ReleaseDeviceInstallPhaseShared();
        *SharedPhaseLockHeld = FALSE;
    }

    AcquireDeviceInstallPhaseExclusive();
    return WasShared;
}

static VOID
DeviceInstallPhaseDemote(
    IN BOOL WasShared,
    IN OUT PBOOL SharedPhaseLockHeld)
{
    ReleaseDeviceInstallPhaseExclusive();

    if (WasShared)
    {
        AcquireDeviceInstallPhaseShared();
        *SharedPhaseLockHeld = TRUE;
    }
}

static BOOL
InstallSelectedDriver(
    IN PDEVINSTDATA DevInstData,
    IN BOOL NullDriver,
    IN OUT PBOOL SharedPhaseLockHeld)
{
    BOOL WasShared = DeviceInstallPhasePromote(SharedPhaseLockHeld);
    BOOL Ret;

    Ret = NullDriver ? InstallNullDriver(DevInstData) : InstallCurrentDriver(DevInstData);

    DeviceInstallPhaseDemote(WasShared, SharedPhaseLockHeld);
    return Ret;
}

static BOOL
SetFailedInstallSerialized(
    IN HDEVINFO DeviceInfoSet,
    IN PSP_DEVINFO_DATA DeviceInfoData,
    IN BOOLEAN Set,
    IN OUT PBOOL SharedPhaseLockHeld)
{
    BOOL WasShared = DeviceInstallPhasePromote(SharedPhaseLockHeld);
    BOOL Ret;

    Ret = NewDevSetFailedInstall(DeviceInfoSet, DeviceInfoData, Set);

    DeviceInstallPhaseDemote(WasShared, SharedPhaseLockHeld);
    return Ret;
}

static BOOL
NoDriverCacheContainsLocked(
    IN LPCWSTR Id)
{
    PNO_DRIVER_ID_ENTRY Entry;

    for (Entry = NoDriverIdCache; Entry; Entry = Entry->Next)
    {
        if (!lstrcmpiW(Entry->Id, Id))
            return TRUE;
    }

    return FALSE;
}

static SIZE_T
MultiSzSize(
    IN LPCWSTR IdList OPTIONAL)
{
    LPCWSTR Id;

    if (!IdList)
        return 0;

    for (Id = IdList; *Id; Id += lstrlenW(Id) + 1)
    {
    }

    return (Id - IdList + 1) * sizeof(WCHAR);
}

static BOOL
DriverMatchCacheEntryMatches(
    IN PDRIVER_MATCH_CACHE_ENTRY Entry,
    IN LPCWSTR HardwareIds OPTIONAL,
    IN SIZE_T HardwareIdsSize,
    IN LPCWSTR CompatibleIds OPTIONAL,
    IN SIZE_T CompatibleIdsSize)
{
    if (Entry->HardwareIdsSize != HardwareIdsSize ||
        Entry->CompatibleIdsSize != CompatibleIdsSize)
    {
        return FALSE;
    }

    if (HardwareIdsSize &&
        memcmp(Entry->Ids, HardwareIds, HardwareIdsSize))
    {
        return FALSE;
    }

    if (CompatibleIdsSize &&
        memcmp(Entry->Ids + HardwareIdsSize, CompatibleIds, CompatibleIdsSize))
    {
        return FALSE;
    }

    return TRUE;
}

static BOOL
DriverMatchCacheLookup(
    IN LPCWSTR HardwareIds OPTIONAL,
    IN LPCWSTR CompatibleIds OPTIONAL,
    OUT LPWSTR InfFileName,
    IN DWORD InfFileNameCch,
    OUT BOOL *NullDriver)
{
    PDRIVER_MATCH_CACHE_ENTRY Entry;
    SIZE_T HardwareIdsSize, CompatibleIdsSize;
    BOOL Found = FALSE;

    if (!NoDriverIdCacheReady || NoDriverIdCacheBatchDepth == 0)
        return FALSE;

    HardwareIdsSize = MultiSzSize(HardwareIds);
    CompatibleIdsSize = MultiSzSize(CompatibleIds);
    if (HardwareIdsSize == 0 && CompatibleIdsSize == 0)
        return FALSE;

    EnterCriticalSection(&NoDriverIdCacheLock);

    for (Entry = DriverMatchCache; Entry; Entry = Entry->Next)
    {
        if (DriverMatchCacheEntryMatches(Entry,
                                         HardwareIds,
                                         HardwareIdsSize,
                                         CompatibleIds,
                                         CompatibleIdsSize))
        {
            lstrcpynW(InfFileName, Entry->InfFileName, InfFileNameCch);
            *NullDriver = Entry->NullDriver;
            Found = TRUE;
            break;
        }
    }

    LeaveCriticalSection(&NoDriverIdCacheLock);
    return Found;
}

static BOOL
DriverMatchCacheRemember(
    IN LPCWSTR HardwareIds OPTIONAL,
    IN LPCWSTR CompatibleIds OPTIONAL,
    IN LPCWSTR InfFileName,
    IN BOOL NullDriver)
{
    PDRIVER_MATCH_CACHE_ENTRY Entry;
    SIZE_T HardwareIdsSize, CompatibleIdsSize, Size;
    BOOL Ret = TRUE;

    if (!NoDriverIdCacheReady || NoDriverIdCacheBatchDepth == 0 ||
        !InfFileName || !*InfFileName)
    {
        return TRUE;
    }

    HardwareIdsSize = MultiSzSize(HardwareIds);
    CompatibleIdsSize = MultiSzSize(CompatibleIds);
    if (HardwareIdsSize == 0 && CompatibleIdsSize == 0)
        return TRUE;

    EnterCriticalSection(&NoDriverIdCacheLock);

    for (Entry = DriverMatchCache; Entry; Entry = Entry->Next)
    {
        if (DriverMatchCacheEntryMatches(Entry,
                                         HardwareIds,
                                         HardwareIdsSize,
                                         CompatibleIds,
                                         CompatibleIdsSize))
        {
            LeaveCriticalSection(&NoDriverIdCacheLock);
            return TRUE;
        }
    }

    Size = FIELD_OFFSET(DRIVER_MATCH_CACHE_ENTRY, Ids) +
           HardwareIdsSize + CompatibleIdsSize;
    Entry = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Entry)
    {
        Ret = FALSE;
    }
    else
    {
        Entry->HardwareIdsSize = HardwareIdsSize;
        Entry->CompatibleIdsSize = CompatibleIdsSize;
        Entry->NullDriver = NullDriver;
        lstrcpynW(Entry->InfFileName,
                  InfFileName,
                  sizeof(Entry->InfFileName) / sizeof(Entry->InfFileName[0]));
        if (HardwareIdsSize)
            memcpy(Entry->Ids, HardwareIds, HardwareIdsSize);
        if (CompatibleIdsSize)
            memcpy(Entry->Ids + HardwareIdsSize, CompatibleIds, CompatibleIdsSize);
        Entry->Next = DriverMatchCache;
        DriverMatchCache = Entry;
    }

    LeaveCriticalSection(&NoDriverIdCacheLock);
    return Ret;
}

static BOOL
NoDriverCacheAdd(
    IN LPCWSTR Id)
{
    PNO_DRIVER_ID_ENTRY Entry;
    SIZE_T Size;
    BOOL Ret = TRUE;

    if (!NoDriverIdCacheReady || !Id || !*Id)
        return TRUE;

    EnterCriticalSection(&NoDriverIdCacheLock);

    if (!NoDriverCacheContainsLocked(Id))
    {
        Size = FIELD_OFFSET(NO_DRIVER_ID_ENTRY, Id) + (lstrlenW(Id) + 1) * sizeof(WCHAR);
        Entry = HeapAlloc(GetProcessHeap(), 0, Size);
        if (!Entry)
        {
            Ret = FALSE;
        }
        else
        {
            Entry->Next = NoDriverIdCache;
            lstrcpyW(Entry->Id, Id);
            NoDriverIdCache = Entry;
        }
    }

    LeaveCriticalSection(&NoDriverIdCacheLock);
    return Ret;
}

static VOID
NoDriverCacheAddList(
    IN LPCWSTR IdList OPTIONAL)
{
    LPCWSTR Id;

    if (!IdList)
        return;

    for (Id = IdList; *Id; Id += lstrlenW(Id) + 1)
        NoDriverCacheAdd(Id);
}

static VOID
NoDriverCacheRemember(
    IN LPCWSTR HardwareIds OPTIONAL,
    IN LPCWSTR CompatibleIds OPTIONAL)
{
    if (NoDriverIdCacheBatchDepth == 0)
        return;

    NoDriverCacheAddList(HardwareIds);
    NoDriverCacheAddList(CompatibleIds);
}

static VOID
NoDriverCacheCheckListLocked(
    IN LPCWSTR IdList OPTIONAL,
    IN OUT BOOL *SawId,
    IN OUT BOOL *AllKnown)
{
    LPCWSTR Id;

    if (!IdList)
        return;

    for (Id = IdList; *Id && *AllKnown; Id += lstrlenW(Id) + 1)
    {
        *SawId = TRUE;
        if (!NoDriverCacheContainsLocked(Id))
            *AllKnown = FALSE;
    }
}

static BOOL
NoDriverCacheAllIdsKnown(
    IN LPCWSTR HardwareIds OPTIONAL,
    IN LPCWSTR CompatibleIds OPTIONAL)
{
    BOOL SawId = FALSE;
    BOOL AllKnown = TRUE;

    if (!NoDriverIdCacheReady || NoDriverIdCacheBatchDepth == 0)
        return FALSE;

    EnterCriticalSection(&NoDriverIdCacheLock);
    NoDriverCacheCheckListLocked(HardwareIds, &SawId, &AllKnown);
    NoDriverCacheCheckListLocked(CompatibleIds, &SawId, &AllKnown);
    LeaveCriticalSection(&NoDriverIdCacheLock);

    return SawId && AllKnown;
}

static PWSTR
GetDeviceMultiSzProperty(
    IN PDEVINSTDATA DevInstData,
    IN DWORD Property)
{
    PWSTR Buffer;
    DWORD DataType;
    DWORD RequiredSize = 0;

    if (SetupDiGetDeviceRegistryPropertyW(DevInstData->hDevInfo,
                                          &DevInstData->devInfoData,
                                          Property,
                                          &DataType,
                                          NULL,
                                          0,
                                          &RequiredSize))
    {
        return NULL;
    }

    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || RequiredSize == 0)
        return NULL;

    Buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, RequiredSize + sizeof(WCHAR));
    if (!Buffer)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

    if (!SetupDiGetDeviceRegistryPropertyW(DevInstData->hDevInfo,
                                           &DevInstData->devInfoData,
                                           Property,
                                           &DataType,
                                           (PBYTE)Buffer,
                                           RequiredSize,
                                           &RequiredSize) ||
        DataType != REG_MULTI_SZ)
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        return NULL;
    }

    return Buffer;
}

static VOID
NoDriverCacheClear(VOID)
{
    PNO_DRIVER_ID_ENTRY Entry, Next;
    PDRIVER_MATCH_CACHE_ENTRY MatchEntry, MatchNext;

    if (!NoDriverIdCacheReady)
        return;

    EnterCriticalSection(&NoDriverIdCacheLock);
    Entry = NoDriverIdCache;
    NoDriverIdCache = NULL;
    MatchEntry = DriverMatchCache;
    DriverMatchCache = NULL;
    LeaveCriticalSection(&NoDriverIdCacheLock);

    while (Entry)
    {
        Next = Entry->Next;
        HeapFree(GetProcessHeap(), 0, Entry);
        Entry = Next;
    }

    while (MatchEntry)
    {
        MatchNext = MatchEntry->Next;
        HeapFree(GetProcessHeap(), 0, MatchEntry);
        MatchEntry = MatchNext;
    }
}

static VOID
NoDriverCacheBeginBatch(VOID)
{
    if (NoDriverIdCacheReady)
        InterlockedIncrement(&NoDriverIdCacheBatchDepth);
}

static VOID
NoDriverCacheEndBatch(VOID)
{
    if (!NoDriverIdCacheReady)
        return;

    if (InterlockedDecrement(&NoDriverIdCacheBatchDepth) == 0)
        NoDriverCacheClear();
}

static VOID
FreeNoDriverCache(VOID)
{
    if (!NoDriverIdCacheReady)
        return;

    NoDriverCacheClear();

    DeleteCriticalSection(&NoDriverIdCacheLock);
    NoDriverIdCacheReady = FALSE;
}

/*
* @implemented
*/
BOOL WINAPI
UpdateDriverForPlugAndPlayDevicesW(
    IN HWND hwndParent,
    IN LPCWSTR HardwareId,
    IN LPCWSTR FullInfPath,
    IN DWORD InstallFlags,
    OUT PBOOL bRebootRequired OPTIONAL)
{
    DEVINSTDATA DevInstData;
    DWORD i;
    LPWSTR Buffer = NULL;
    DWORD BufferSize;
    LPCWSTR CurrentHardwareId; /* Pointer into Buffer */
    DWORD Property;
    BOOL FoundHardwareId, FoundAtLeastOneDevice = FALSE;
    BOOL ret = FALSE;

    DevInstData.hDevInfo = INVALID_HANDLE_VALUE;

    TRACE("UpdateDriverForPlugAndPlayDevicesW(%p %s %s 0x%x %p)\n",
        hwndParent, debugstr_w(HardwareId), debugstr_w(FullInfPath), InstallFlags, bRebootRequired);

    /* FIXME: InstallFlags bRebootRequired ignored! */

    /* Check flags */
    if (InstallFlags & ~(INSTALLFLAG_FORCE | INSTALLFLAG_READONLY | INSTALLFLAG_NONINTERACTIVE))
    {
        TRACE("Unknown flags: 0x%08lx\n", InstallFlags & ~(INSTALLFLAG_FORCE | INSTALLFLAG_READONLY | INSTALLFLAG_NONINTERACTIVE));
        SetLastError(ERROR_INVALID_FLAGS);
        goto cleanup;
    }

    /* Enumerate all devices of the system */
    DevInstData.hDevInfo = SetupDiGetClassDevsW(NULL, NULL, hwndParent, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (DevInstData.hDevInfo == INVALID_HANDLE_VALUE)
        goto cleanup;
    DevInstData.devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    for (i = 0; ; i++)
    {
        if (!SetupDiEnumDeviceInfo(DevInstData.hDevInfo, i, &DevInstData.devInfoData))
        {
            if (GetLastError() != ERROR_NO_MORE_ITEMS)
            {
                TRACE("SetupDiEnumDeviceInfo() failed with error 0x%x\n", GetLastError());
                goto cleanup;
            }
            /* This error was expected */
            break;
        }

        /* Match Hardware ID */
        FoundHardwareId = FALSE;
        Property = SPDRP_HARDWAREID;
        while (TRUE)
        {
            /* Get IDs data */
            Buffer = NULL;
            BufferSize = 0;
            while (!SetupDiGetDeviceRegistryPropertyW(DevInstData.hDevInfo,
                                                      &DevInstData.devInfoData,
                                                      Property,
                                                      NULL,
                                                      (PBYTE)Buffer,
                                                      BufferSize,
                                                      &BufferSize))
            {
                if (GetLastError() == ERROR_FILE_NOT_FOUND)
                {
                    break;
                }
                else if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
                {
                    TRACE("SetupDiGetDeviceRegistryPropertyW() failed with error 0x%x\n", GetLastError());
                    goto cleanup;
                }
                /* This error was expected */
                HeapFree(GetProcessHeap(), 0, Buffer);
                Buffer = HeapAlloc(GetProcessHeap(), 0, BufferSize);
                if (!Buffer)
                {
                    TRACE("HeapAlloc() failed\n", GetLastError());
                    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                    goto cleanup;
                }
            }
            if (Buffer)
            {
                /* Check if we match the given hardware ID */
                for (CurrentHardwareId = Buffer; *CurrentHardwareId != UNICODE_NULL; CurrentHardwareId += wcslen(CurrentHardwareId) + 1)
                {
                    if (_wcsicmp(CurrentHardwareId, HardwareId) == 0)
                    {
                        FoundHardwareId = TRUE;
                        break;
                    }
                }
            }
            if (FoundHardwareId || Property == SPDRP_COMPATIBLEIDS)
            {
                break;
            }
            Property = SPDRP_COMPATIBLEIDS;
        }
        if (!FoundHardwareId)
            continue;

        /* We need to try to update the driver of this device */

        /* Get Instance ID */
        HeapFree(GetProcessHeap(), 0, Buffer);
        Buffer = NULL;
        if (SetupDiGetDeviceInstanceIdW(DevInstData.hDevInfo, &DevInstData.devInfoData, NULL, 0, &BufferSize))
        {
            /* Error, as the output buffer should be too small */
            SetLastError(ERROR_GEN_FAILURE);
            goto cleanup;
        }
        else if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
            TRACE("SetupDiGetDeviceInstanceIdW() failed with error 0x%x\n", GetLastError());
            goto cleanup;
        }
        else if ((Buffer = HeapAlloc(GetProcessHeap(), 0, BufferSize * sizeof(WCHAR))) == NULL)
        {
            TRACE("HeapAlloc() failed\n", GetLastError());
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            goto cleanup;
        }
        else if (!SetupDiGetDeviceInstanceIdW(DevInstData.hDevInfo, &DevInstData.devInfoData, Buffer, BufferSize, NULL))
        {
            TRACE("SetupDiGetDeviceInstanceIdW() failed with error 0x%x\n", GetLastError());
            goto cleanup;
        }
        TRACE("Trying to update the driver of %s\n", debugstr_w(Buffer));

        /* Search driver in the specified .inf file */
        if (!SearchDriver(&DevInstData, NULL, FullInfPath))
        {
            TRACE("SearchDriver() failed with error 0x%x\n", GetLastError());
            continue;
        }

        /* FIXME: HACK! We shouldn't check of ERROR_PRIVILEGE_NOT_HELD */
        //if (!InstallCurrentDriver(&DevInstData))
        if (!InstallCurrentDriver(&DevInstData) && GetLastError() != ERROR_PRIVILEGE_NOT_HELD)
        {
            TRACE("InstallCurrentDriver() failed with error 0x%x\n", GetLastError());
            continue;
        }

        FoundAtLeastOneDevice = TRUE;
    }

    if (FoundAtLeastOneDevice)
    {
        SetLastError(NO_ERROR);
        ret = TRUE;
    }
    else
    {
        TRACE("No device found with HardwareID %s\n", debugstr_w(HardwareId));
        SetLastError(ERROR_NO_SUCH_DEVINST);
    }

cleanup:
    if (DevInstData.hDevInfo != INVALID_HANDLE_VALUE)
        SetupDiDestroyDeviceInfoList(DevInstData.hDevInfo);
    HeapFree(GetProcessHeap(), 0, Buffer);
    return ret;
}

/*
* @implemented
*/
BOOL WINAPI
UpdateDriverForPlugAndPlayDevicesA(
    IN HWND hwndParent,
    IN LPCSTR HardwareId,
    IN LPCSTR FullInfPath,
    IN DWORD InstallFlags,
    OUT PBOOL bRebootRequired OPTIONAL)
{
    BOOL Result;
    LPWSTR HardwareIdW = NULL;
    LPWSTR FullInfPathW = NULL;

    int len = MultiByteToWideChar(CP_ACP, 0, HardwareId, -1, NULL, 0);
    HardwareIdW = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
    if (!HardwareIdW)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    MultiByteToWideChar(CP_ACP, 0, HardwareId, -1, HardwareIdW, len);

    len = MultiByteToWideChar(CP_ACP, 0, FullInfPath, -1, NULL, 0);
    FullInfPathW = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
    if (!FullInfPathW)
    {
        HeapFree(GetProcessHeap(), 0, HardwareIdW);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    MultiByteToWideChar(CP_ACP, 0, FullInfPath, -1, FullInfPathW, len);

    Result = UpdateDriverForPlugAndPlayDevicesW(
        hwndParent,
        HardwareIdW,
        FullInfPathW,
        InstallFlags,
        bRebootRequired);

    HeapFree(GetProcessHeap(), 0, HardwareIdW);
    HeapFree(GetProcessHeap(), 0, FullInfPathW);

    return Result;
}

/* Directory and InfFile MUST NOT be specified simultaneously */
static BOOL
SearchDriver(
    IN PDEVINSTDATA DevInstData,
    IN LPCWSTR Directory OPTIONAL,
    IN LPCWSTR InfFile OPTIONAL)
{
    SP_DEVINSTALL_PARAMS_W DevInstallParams = {0,};
    BOOL ret;

    DevInstallParams.cbSize = sizeof(SP_DEVINSTALL_PARAMS_W);
    if (!SetupDiGetDeviceInstallParamsW(DevInstData->hDevInfo, &DevInstData->devInfoData, &DevInstallParams))
    {
        TRACE("SetupDiGetDeviceInstallParams() failed with error 0x%x\n", GetLastError());
        return FALSE;
    }
    DevInstallParams.FlagsEx |= DI_FLAGSEX_ALLOWEXCLUDEDDRVS;

    if (InfFile)
    {
        DevInstallParams.Flags |= DI_ENUMSINGLEINF;
        wcsncpy(DevInstallParams.DriverPath, InfFile, MAX_PATH);
    }
    else if (Directory)
    {
        DevInstallParams.Flags &= ~DI_ENUMSINGLEINF;
        wcsncpy(DevInstallParams.DriverPath, Directory, MAX_PATH);
    }
    else
    {
        DevInstallParams.Flags &= ~DI_ENUMSINGLEINF;
        *DevInstallParams.DriverPath = '\0';
    }

    ret = SetupDiSetDeviceInstallParamsW(
        DevInstData->hDevInfo,
        &DevInstData->devInfoData,
        &DevInstallParams);
    if (!ret)
    {
        TRACE("SetupDiSetDeviceInstallParams() failed with error 0x%x\n", GetLastError());
        return FALSE;
    }

    ret = SetupDiBuildDriverInfoList(
        DevInstData->hDevInfo,
        &DevInstData->devInfoData,
        SPDIT_COMPATDRIVER);
    if (!ret)
    {
        TRACE("SetupDiBuildDriverInfoList() failed with error 0x%x\n", GetLastError());
        return FALSE;
    }

    DevInstData->drvInfoData.cbSize = sizeof(SP_DRVINFO_DATA);
    ret = SetupDiEnumDriverInfoW(
        DevInstData->hDevInfo,
        &DevInstData->devInfoData,
        SPDIT_COMPATDRIVER,
        0,
        &DevInstData->drvInfoData);
    if (!ret)
    {
        if (GetLastError() == ERROR_NO_MORE_ITEMS)
            return FALSE;
        TRACE("SetupDiEnumDriverInfo() failed with error 0x%x\n", GetLastError());
        return FALSE;
    }

    return TRUE;
}

static BOOL
GetCurrentDriverInfoDetail(
    IN PDEVINSTDATA DevInstData,
    OUT PSP_DRVINFO_DETAIL_DATA_W *DriverInfoDetail)
{
    PSP_DRVINFO_DETAIL_DATA_W Details;
    DWORD RequiredSize = 0;
    BOOL Ret;

    *DriverInfoDetail = NULL;

    SetupDiGetDriverInfoDetailW(DevInstData->hDevInfo,
                                &DevInstData->devInfoData,
                                &DevInstData->drvInfoData,
                                NULL,
                                0,
                                &RequiredSize);
    if (RequiredSize < sizeof(SP_DRVINFO_DETAIL_DATA_W))
        return FALSE;

    Details = HeapAlloc(GetProcessHeap(), 0, RequiredSize);
    if (!Details)
        return FALSE;

    Details->cbSize = sizeof(SP_DRVINFO_DETAIL_DATA_W);
    Ret = SetupDiGetDriverInfoDetailW(DevInstData->hDevInfo,
                                      &DevInstData->devInfoData,
                                      &DevInstData->drvInfoData,
                                      Details,
                                      RequiredSize,
                                      NULL);
    if (!Ret)
    {
        HeapFree(GetProcessHeap(), 0, Details);
        return FALSE;
    }

    *DriverInfoDetail = Details;
    return TRUE;
}

static BOOL
GetCurrentDriverInfFileName(
    IN PDEVINSTDATA DevInstData,
    OUT LPWSTR InfFileName,
    IN DWORD InfFileNameCch)
{
    PSP_DRVINFO_DETAIL_DATA_W Details;
    BOOL Ret = FALSE;

    if (InfFileNameCch == 0)
        return FALSE;

    InfFileName[0] = UNICODE_NULL;

    if (GetCurrentDriverInfoDetail(DevInstData, &Details))
    {
        if (Details->InfFileName[0])
        {
            lstrcpynW(InfFileName, Details->InfFileName, InfFileNameCch);
            Ret = TRUE;
        }

        HeapFree(GetProcessHeap(), 0, Details);
    }

    return Ret;
}

static BOOL
InfSectionIsEmpty(
    IN HINF InfHandle,
    IN LPCWSTR SectionName)
{
    INFCONTEXT Context;

    return !SetupFindFirstLineW(InfHandle, SectionName, NULL, &Context);
}

static BOOL
AppendSectionSuffix(
    IN OUT LPWSTR SectionName,
    IN DWORD SectionNameCch,
    IN LPCWSTR Suffix)
{
    DWORD Length = lstrlenW(SectionName);
    DWORD SuffixLength = lstrlenW(Suffix);

    if (Length + SuffixLength + 1 > SectionNameCch)
        return FALSE;

    lstrcatW(SectionName, Suffix);
    return TRUE;
}

static BOOL
InfServicesSectionIsNull(
    IN HINF InfHandle,
    IN LPCWSTR SectionName)
{
    static const WCHAR AddService[] = L"AddService";
    INFCONTEXT Context;
    WCHAR Key[LINE_LEN];
    WCHAR ServiceName[MAX_PATH];

    if (!SetupFindFirstLineW(InfHandle, SectionName, NULL, &Context))
        return TRUE;

    do
    {
        Key[0] = UNICODE_NULL;
        if (!SetupGetStringFieldW(&Context,
                                  0,
                                  Key,
                                  sizeof(Key) / sizeof(Key[0]),
                                  NULL) ||
            lstrcmpiW(Key, AddService))
        {
            return FALSE;
        }

        ServiceName[0] = UNICODE_NULL;
        SetupGetStringFieldW(&Context,
                             1,
                             ServiceName,
                             sizeof(ServiceName) / sizeof(ServiceName[0]),
                             NULL);
        if (ServiceName[0] != UNICODE_NULL)
            return FALSE;
    } while (SetupFindNextLine(&Context, &Context));

    return TRUE;
}

static BOOL
DeviceHasAssociatedService(
    IN PDEVINSTDATA DevInstData)
{
    WCHAR ServiceName[MAX_PATH];
    DWORD RegType, RequiredSize;

    if (SetupDiGetDeviceRegistryPropertyW(DevInstData->hDevInfo,
                                          &DevInstData->devInfoData,
                                          SPDRP_SERVICE,
                                          &RegType,
                                          (PBYTE)ServiceName,
                                          sizeof(ServiceName),
                                          &RequiredSize))
    {
        return RegType == REG_SZ && ServiceName[0] != UNICODE_NULL;
    }

    return GetLastError() == ERROR_INSUFFICIENT_BUFFER;
}

static BOOL
IsCurrentDriverNullInstall(
    IN PDEVINSTDATA DevInstData)
{
    static const WCHAR DotHW[] = L".HW";
    static const WCHAR DotServices[] = L".Services";
    static const WCHAR DotCoInstallers[] = L".CoInstallers";
    static const WCHAR DotInterfaces[] = L".Interfaces";
    PSP_DRVINFO_DETAIL_DATA_W Details;
    WCHAR SectionName[MAX_PATH];
    WCHAR TestSection[MAX_PATH];
    DWORD SectionNameLength;
    HINF InfHandle;
    BOOL Ret = FALSE;

    if (DeviceHasAssociatedService(DevInstData))
        return FALSE;

    if (!GetCurrentDriverInfoDetail(DevInstData, &Details))
        return FALSE;

    InfHandle = SetupOpenInfFileW(Details->InfFileName, NULL, INF_STYLE_WIN4, NULL);
    if (InfHandle == INVALID_HANDLE_VALUE)
        goto cleanup_details;

    if (!SetupDiGetActualSectionToInstallW(InfHandle,
                                           Details->SectionName,
                                           SectionName,
                                           sizeof(SectionName) / sizeof(SectionName[0]),
                                           &SectionNameLength,
                                           NULL))
    {
        goto cleanup_inf;
    }

    if (!InfSectionIsEmpty(InfHandle, SectionName))
        goto cleanup_inf;

    lstrcpyW(TestSection, SectionName);
    if (!AppendSectionSuffix(TestSection,
                             sizeof(TestSection) / sizeof(TestSection[0]),
                             DotHW) ||
        !InfSectionIsEmpty(InfHandle, TestSection))
    {
        goto cleanup_inf;
    }

    lstrcpyW(TestSection, SectionName);
    if (!AppendSectionSuffix(TestSection,
                             sizeof(TestSection) / sizeof(TestSection[0]),
                             DotCoInstallers) ||
        !InfSectionIsEmpty(InfHandle, TestSection))
    {
        goto cleanup_inf;
    }

    lstrcpyW(TestSection, SectionName);
    if (!AppendSectionSuffix(TestSection,
                             sizeof(TestSection) / sizeof(TestSection[0]),
                             DotInterfaces) ||
        !InfSectionIsEmpty(InfHandle, TestSection))
    {
        goto cleanup_inf;
    }

    lstrcpyW(TestSection, SectionName);
    if (!AppendSectionSuffix(TestSection,
                             sizeof(TestSection) / sizeof(TestSection[0]),
                             DotServices) ||
        !InfServicesSectionIsNull(InfHandle, TestSection))
    {
        goto cleanup_inf;
    }

    Ret = TRUE;

cleanup_inf:
    SetupCloseInfFile(InfHandle);
cleanup_details:
    HeapFree(GetProcessHeap(), 0, Details);
    return Ret;
}

static BOOL
InstallNullDriver(
    IN PDEVINSTDATA DevInstData)
{
    BOOL Ret;

    Ret = SetupDiCallClassInstaller(DIF_SELECTBESTCOMPATDRV,
                                    DevInstData->hDevInfo,
                                    &DevInstData->devInfoData);
    if (!Ret)
        return FALSE;

    return SetupDiInstallDevice(DevInstData->hDevInfo,
                                &DevInstData->devInfoData);
}

static BOOL
IsDots(IN LPCWSTR str)
{
    if(wcscmp(str, L".") && wcscmp(str, L"..")) return FALSE;
    return TRUE;
}

static LPCWSTR
GetFileExt(IN LPWSTR FileName)
{
    LPCWSTR Dot;

    Dot = wcsrchr(FileName, '.');
    if (!Dot)
        return L"";

    return Dot;
}

static BOOL
SearchDriverRecursive(
    IN PDEVINSTDATA DevInstData,
    IN LPCWSTR Path)
{
    WIN32_FIND_DATAW wfd;
    WCHAR DirPath[MAX_PATH];
    WCHAR FileName[MAX_PATH];
    WCHAR FullPath[MAX_PATH];
    WCHAR LastDirPath[MAX_PATH] = L"";
    WCHAR PathWithPattern[MAX_PATH];
    BOOL ok = TRUE;
    BOOL retval = FALSE;
    HANDLE hFindFile = INVALID_HANDLE_VALUE;

    wcscpy(DirPath, Path);

    if (DirPath[wcslen(DirPath) - 1] != '\\')
        wcscat(DirPath, L"\\");

    wcscpy(PathWithPattern, DirPath);
    wcscat(PathWithPattern, L"*");

    for (hFindFile = FindFirstFileW(PathWithPattern, &wfd);
        ok && hFindFile != INVALID_HANDLE_VALUE;
        ok = FindNextFileW(hFindFile, &wfd))
    {

        wcscpy(FileName, wfd.cFileName);
        if (IsDots(FileName))
            continue;

        if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            /* Recursive search */
            wcscpy(FullPath, DirPath);
            wcscat(FullPath, FileName);
            if (SearchDriverRecursive(DevInstData, FullPath))
            {
                retval = TRUE;
                /* We continue the search for a better driver */
            }
        }
        else
        {
            LPCWSTR pszExtension = GetFileExt(FileName);

            if ((_wcsicmp(pszExtension, L".inf") == 0) && (wcscmp(LastDirPath, DirPath) != 0))
            {
                wcscpy(LastDirPath, DirPath);

                if (wcslen(DirPath) > MAX_PATH)
                    /* Path is too long to be searched */
                    continue;

                if (SearchDriver(DevInstData, DirPath, NULL))
                {
                    retval = TRUE;
                    /* We continue the search for a better driver */
                }

            }
        }
    }

    if (hFindFile != INVALID_HANDLE_VALUE)
        FindClose(hFindFile);
    return retval;
}

BOOL
CheckBestDriver(
    _In_ PDEVINSTDATA DevInstData,
    _In_ PCWSTR pszDir)
{
    return SearchDriverRecursive(DevInstData, pszDir);
}

BOOL
ScanFoldersForDriver(
    IN PDEVINSTDATA DevInstData)
{
    BOOL result;

    /* Search in default location */
    result = SearchDriver(DevInstData, NULL, NULL);

    if (DevInstData->CustomSearchPath)
    {
        /* Search only in specified paths */
        /* We need to check all specified directories to be
         * sure to find the best driver for the device.
         */
        LPCWSTR Path;
        for (Path = DevInstData->CustomSearchPath; *Path != '\0'; Path += wcslen(Path) + 1)
        {
            TRACE("Search driver in %s\n", debugstr_w(Path));
            if (wcslen(Path) == 2 && Path[1] == ':')
            {
                if (SearchDriverRecursive(DevInstData, Path))
                    result = TRUE;
            }
            else
            {
                if (SearchDriver(DevInstData, Path, NULL))
                    result = TRUE;
            }
        }
    }

    return result;
}

BOOL
PrepareFoldersToScan(
    IN PDEVINSTDATA DevInstData,
    IN BOOL IncludeRemovableDevices,
    IN BOOL IncludeCustomPath,
    IN HWND hwndCombo OPTIONAL)
{
    WCHAR drive[] = {'?',':',0};
    DWORD dwDrives = 0;
    DWORD i;
    UINT nType;
    DWORD CustomTextLength = 0;
    DWORD LengthNeeded = 0;
    LPWSTR Buffer;
    INT idx = (INT)SendMessageW(hwndCombo, CB_GETCURSEL, 0, 0);

    /* Calculate length needed to store the search paths */
    if (IncludeRemovableDevices)
    {
        dwDrives = GetLogicalDrives();
        for (drive[0] = 'A', i = 1; drive[0] <= 'Z'; drive[0]++, i <<= 1)
        {
            if (dwDrives & i)
            {
                nType = GetDriveTypeW(drive);
                if (nType == DRIVE_REMOVABLE || nType == DRIVE_CDROM)
                {
                    LengthNeeded += 3;
                }
            }
        }
    }
    if (IncludeCustomPath)
    {
        CustomTextLength = 1 + ((idx != CB_ERR) ?
        (INT)SendMessageW(hwndCombo, CB_GETLBTEXTLEN, idx, 0) : ComboBox_GetTextLength(hwndCombo));
        LengthNeeded += CustomTextLength;
    }

    /* Allocate space for search paths */
    HeapFree(GetProcessHeap(), 0, DevInstData->CustomSearchPath);
    DevInstData->CustomSearchPath = Buffer = HeapAlloc(
        GetProcessHeap(),
        0,
        (LengthNeeded + 1) * sizeof(WCHAR));
    if (!Buffer)
    {
        TRACE("HeapAlloc() failed\n");
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    /* Fill search paths */
    if (IncludeRemovableDevices)
    {
        for (drive[0] = 'A', i = 1; drive[0] <= 'Z'; drive[0]++, i <<= 1)
        {
            if (dwDrives & i)
            {
                nType = GetDriveTypeW(drive);
                if (nType == DRIVE_REMOVABLE || nType == DRIVE_CDROM)
                {
                    Buffer += 1 + _swprintf(Buffer, drive);
                }
            }
        }
    }
    if (IncludeCustomPath)
    {
        Buffer += 1 + ((idx != CB_ERR) ?
        SendMessageW(hwndCombo, CB_GETLBTEXT, idx, (LPARAM)Buffer) :
        GetWindowTextW(hwndCombo, Buffer, CustomTextLength));
    }
    *Buffer = '\0';

    return TRUE;
}

BOOL
InstallCurrentDriver(
    IN PDEVINSTDATA DevInstData)
{
    BOOL ret;

    TRACE("Installing driver %s: %s\n",
        debugstr_w(DevInstData->drvInfoData.MfgName),
        debugstr_w(DevInstData->drvInfoData.Description));

    ret = SetupDiCallClassInstaller(
        DIF_SELECTBESTCOMPATDRV,
        DevInstData->hDevInfo,
        &DevInstData->devInfoData);
    if (!ret)
    {
        TRACE("SetupDiCallClassInstaller(DIF_SELECTBESTCOMPATDRV) failed with error 0x%x\n", GetLastError());
        return FALSE;
    }

    ret = SetupDiCallClassInstaller(
        DIF_ALLOW_INSTALL,
        DevInstData->hDevInfo,
        &DevInstData->devInfoData);
    if (!ret)
    {
        TRACE("SetupDiCallClassInstaller(DIF_ALLOW_INSTALL) failed with error 0x%x\n", GetLastError());
        return FALSE;
    }

    ret = SetupDiCallClassInstaller(
        DIF_NEWDEVICEWIZARD_PREANALYZE,
        DevInstData->hDevInfo,
        &DevInstData->devInfoData);
    if (!ret)
    {
        TRACE("SetupDiCallClassInstaller(DIF_NEWDEVICEWIZARD_PREANALYZE) failed with error 0x%x\n", GetLastError());
        return FALSE;
    }

    ret = SetupDiCallClassInstaller(
        DIF_NEWDEVICEWIZARD_POSTANALYZE,
        DevInstData->hDevInfo,
        &DevInstData->devInfoData);
    if (!ret)
    {
        TRACE("SetupDiCallClassInstaller(DIF_NEWDEVICEWIZARD_POSTANALYZE) failed with error 0x%x\n", GetLastError());
        return FALSE;
    }

    ret = SetupDiCallClassInstaller(
        DIF_INSTALLDEVICEFILES,
        DevInstData->hDevInfo,
        &DevInstData->devInfoData);
    if (!ret)
    {
        TRACE("SetupDiCallClassInstaller(DIF_INSTALLDEVICEFILES) failed with error 0x%x\n", GetLastError());
        return FALSE;
    }

    ret = SetupDiCallClassInstaller(
        DIF_REGISTER_COINSTALLERS,
        DevInstData->hDevInfo,
        &DevInstData->devInfoData);
    if (!ret)
    {
        TRACE("SetupDiCallClassInstaller(DIF_REGISTER_COINSTALLERS) failed with error 0x%x\n", GetLastError());
        return FALSE;
    }

    ret = SetupDiCallClassInstaller(
        DIF_INSTALLINTERFACES,
        DevInstData->hDevInfo,
        &DevInstData->devInfoData);
    if (!ret)
    {
        TRACE("SetupDiCallClassInstaller(DIF_INSTALLINTERFACES) failed with error 0x%x\n", GetLastError());
        return FALSE;
    }

    ret = SetupDiCallClassInstaller(
        DIF_INSTALLDEVICE,
        DevInstData->hDevInfo,
        &DevInstData->devInfoData);
    if (!ret)
    {
        TRACE("SetupDiCallClassInstaller(DIF_INSTALLDEVICE) failed with error 0x%x\n", GetLastError());
        return FALSE;
    }

    ret = SetupDiCallClassInstaller(
        DIF_NEWDEVICEWIZARD_FINISHINSTALL,
        DevInstData->hDevInfo,
        &DevInstData->devInfoData);
    if (!ret)
    {
        TRACE("SetupDiCallClassInstaller(DIF_NEWDEVICEWIZARD_FINISHINSTALL) failed with error 0x%x\n", GetLastError());
        return FALSE;
    }

    ret = SetupDiCallClassInstaller(
        DIF_DESTROYPRIVATEDATA,
        DevInstData->hDevInfo,
        &DevInstData->devInfoData);
    if (!ret)
    {
        TRACE("SetupDiCallClassInstaller(DIF_DESTROYPRIVATEDATA) failed with error 0x%x\n", GetLastError());
        return FALSE;
    }

    return TRUE;
}

static BOOL
DevInstallInternal(
    IN HWND hWndParent,
    IN HINSTANCE hInstance,
    IN LPCWSTR InstanceId,
    IN INT Show,
    IN BOOL InBatch)
{
    PDEVINSTDATA DevInstData = NULL;
    PWSTR HardwareIds = NULL;
    PWSTR CompatibleIds = NULL;
    WCHAR CachedInfFile[MAX_PATH];
    WCHAR InstalledInfFile[MAX_PATH];
    BOOL ret;
    DWORD config_flags;
    BOOL CachedNullDriver = FALSE;
    BOOL UseDriverMatchCache = FALSE;
    BOOL SharedPhaseLockHeld = FALSE;
    BOOL retval = FALSE;

    TRACE("(%p, %p, %s, %d)\n", hWndParent, hInstance, debugstr_w(InstanceId), Show);

    if (!IsUserAdmin())
    {
        /* XP kills the process... */
        ExitProcess(ERROR_ACCESS_DENIED);
    }

    if (InBatch)
    {
        AcquireDeviceInstallPhaseShared();
        SharedPhaseLockHeld = TRUE;
    }

    DevInstData = HeapAlloc(GetProcessHeap(), 0, sizeof(DEVINSTDATA));
    if (!DevInstData)
    {
        TRACE("HeapAlloc() failed\n");
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        goto cleanup;
    }

    /* Clear devinst data */
    ZeroMemory(DevInstData, sizeof(DEVINSTDATA));
    DevInstData->devInfoData.cbSize = 0; /* Tell if the devInfoData is valid */

    /* Fill devinst data */
    DevInstData->hDevInfo = SetupDiCreateDeviceInfoListExW(NULL, NULL, NULL, NULL);
    if (DevInstData->hDevInfo == INVALID_HANDLE_VALUE)
    {
        TRACE("SetupDiCreateDeviceInfoListExW() failed with error 0x%x\n", GetLastError());
        goto cleanup;
    }

    DevInstData->devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    ret = SetupDiOpenDeviceInfoW(
        DevInstData->hDevInfo,
        InstanceId,
        NULL,
        0, /* Open flags */
        &DevInstData->devInfoData);
    if (!ret)
    {
        TRACE("SetupDiOpenDeviceInfoW() failed with error 0x%x (InstanceId %s)\n",
            GetLastError(), debugstr_w(InstanceId));
        DevInstData->devInfoData.cbSize = 0;
        goto cleanup;
    }

    SetLastError(ERROR_GEN_FAILURE);
    ret = SetupDiGetDeviceRegistryProperty(
        DevInstData->hDevInfo,
        &DevInstData->devInfoData,
        SPDRP_DEVICEDESC,
        &DevInstData->regDataType,
        NULL, 0,
        &DevInstData->requiredSize);

    if (!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER && DevInstData->regDataType == REG_SZ)
    {
        DevInstData->buffer = HeapAlloc(GetProcessHeap(), 0, DevInstData->requiredSize);
        if (!DevInstData->buffer)
        {
            TRACE("HeapAlloc() failed\n");
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        }
        else
        {
            ret = SetupDiGetDeviceRegistryPropertyW(
                DevInstData->hDevInfo,
                &DevInstData->devInfoData,
                SPDRP_DEVICEDESC,
                &DevInstData->regDataType,
                DevInstData->buffer, DevInstData->requiredSize,
                &DevInstData->requiredSize);
        }
    }
    if (!ret)
    {
        TRACE("SetupDiGetDeviceRegistryProperty() failed with error 0x%x (InstanceId %s)\n",
            GetLastError(), debugstr_w(InstanceId));
        goto cleanup;
    }

    if (SetupDiGetDeviceRegistryPropertyW(
        DevInstData->hDevInfo,
        &DevInstData->devInfoData,
        SPDRP_CONFIGFLAGS,
        NULL,
        (BYTE *)&config_flags,
        sizeof(config_flags),
        NULL))
    {
        if (config_flags & CONFIGFLAG_FAILEDINSTALL)
        {
            /* The device is disabled */
            TRACE("Device is disabled\n");
            retval = TRUE;
            goto cleanup;
        }
    }

    TRACE("Installing %s (%s)\n", debugstr_w((PCWSTR)DevInstData->buffer), debugstr_w(InstanceId));

    if (Show == SW_HIDE && !DevInstData->bUpdate)
    {
        UseDriverMatchCache = TRUE;
        HardwareIds = GetDeviceMultiSzProperty(DevInstData, SPDRP_HARDWAREID);
        CompatibleIds = GetDeviceMultiSzProperty(DevInstData, SPDRP_COMPATIBLEIDS);

        if (NoDriverCacheAllIdsKnown(HardwareIds, CompatibleIds))
        {
            TRACE("No driver cached for %s\n", debugstr_w(InstanceId));
            SetFailedInstallSerialized(DevInstData->hDevInfo, &DevInstData->devInfoData, TRUE, &SharedPhaseLockHeld);
            SetLastError(ERROR_FILE_NOT_FOUND);
            goto cleanup;
        }

        if (DriverMatchCacheLookup(HardwareIds,
                                   CompatibleIds,
                                   CachedInfFile,
                                   sizeof(CachedInfFile) / sizeof(CachedInfFile[0]),
                                   &CachedNullDriver))
        {
            if (SearchDriver(DevInstData, NULL, CachedInfFile))
            {
                retval = InstallSelectedDriver(DevInstData, CachedNullDriver, &SharedPhaseLockHeld);
                TRACE("Cached driver install returned %d\n", retval);
                goto cleanup;
            }

            TRACE("Cached driver INF %ls no longer matches %s\n",
                  CachedInfFile, debugstr_w(InstanceId));
        }
    }

    /* Search driver in default location and removable devices */
    if (!PrepareFoldersToScan(DevInstData, FALSE, FALSE, NULL))
    {
        TRACE("PrepareFoldersToScan() failed with error 0x%lx\n", GetLastError());
        goto cleanup;
    }
    if (ScanFoldersForDriver(DevInstData))
    {
        /* Driver found ; install it */
        retval = InstallSelectedDriver(DevInstData, FALSE, &SharedPhaseLockHeld);
        TRACE("InstallCurrentDriver() returned %d\n", retval);
        if (retval && UseDriverMatchCache &&
            GetCurrentDriverInfFileName(DevInstData,
                                        InstalledInfFile,
                                        sizeof(InstalledInfFile) / sizeof(InstalledInfFile[0])))
        {
            DriverMatchCacheRemember(HardwareIds,
                                     CompatibleIds,
                                     InstalledInfFile,
                                     IsCurrentDriverNullInstall(DevInstData));
        }

        if (retval && Show != SW_HIDE)
        {
            /* Should we display the 'Need to reboot' page? */
            SP_DEVINSTALL_PARAMS installParams;
            installParams.cbSize = sizeof(SP_DEVINSTALL_PARAMS);
            if (SetupDiGetDeviceInstallParams(
                DevInstData->hDevInfo,
                &DevInstData->devInfoData,
                &installParams))
            {
                if (installParams.Flags & (DI_NEEDRESTART | DI_NEEDREBOOT))
                {
                    TRACE("Displaying 'Reboot' wizard page\n");
                    retval = DisplayWizard(DevInstData, hWndParent, IDD_NEEDREBOOT);
                }
            }
        }
        goto cleanup;
    }
    else if (Show == SW_HIDE)
    {
        /* We can't show the wizard. Fail the install */
        TRACE("No wizard\n");
        if (!DevInstData->bUpdate)
        {
            if (!SetFailedInstallSerialized(DevInstData->hDevInfo, &DevInstData->devInfoData, TRUE, &SharedPhaseLockHeld))
            {
                TRACE("NewDevSetFailedInstall() failed with error 0x%lx\n", GetLastError());
            }
            NoDriverCacheRemember(HardwareIds, CompatibleIds);
        }
        SetLastError(ERROR_FILE_NOT_FOUND);
        goto cleanup;
    }

    /* Prepare the wizard, and display it */
    TRACE("Need to show install wizard\n");
    retval = DisplayWizard(DevInstData, hWndParent, IDD_WELCOMEPAGE);

cleanup:
    if (DevInstData)
    {
        if (DevInstData->devInfoData.cbSize != 0)
        {
            if (!SetupDiDestroyDriverInfoList(DevInstData->hDevInfo, &DevInstData->devInfoData, SPDIT_COMPATDRIVER))
                TRACE("SetupDiDestroyDriverInfoList() failed with error 0x%lx\n", GetLastError());
        }
        if (DevInstData->hDevInfo != INVALID_HANDLE_VALUE)
        {
            if (!SetupDiDestroyDeviceInfoList(DevInstData->hDevInfo))
                TRACE("SetupDiDestroyDeviceInfoList() failed with error 0x%lx\n", GetLastError());
        }
        HeapFree(GetProcessHeap(), 0, HardwareIds);
        HeapFree(GetProcessHeap(), 0, CompatibleIds);
        HeapFree(GetProcessHeap(), 0, DevInstData->buffer);
        HeapFree(GetProcessHeap(), 0, DevInstData);
    }

    if (SharedPhaseLockHeld)
        ReleaseDeviceInstallPhaseShared();

    return retval;
}

/*
* @implemented
*/
BOOL WINAPI
DevInstallW(
    IN HWND hWndParent,
    IN HINSTANCE hInstance,
    IN LPCWSTR InstanceId,
    IN INT Show)
{
    return DevInstallInternal(hWndParent, hInstance, InstanceId, Show, FALSE);
}


BOOL
WINAPI
InstallDevInstEx(
    IN HWND hWndParent,
    IN LPCWSTR InstanceId,
    IN BOOL bUpdate,
    OUT LPDWORD lpReboot,
    IN DWORD Unknown)
{
    PDEVINSTDATA DevInstData = NULL;
    BOOL ret;
    BOOL retval = FALSE;

    TRACE("InstllDevInstEx(%p, %s, %d, %p, %lx)\n",
          hWndParent, debugstr_w(InstanceId), bUpdate, lpReboot, Unknown);

    DevInstData = HeapAlloc(GetProcessHeap(), 0, sizeof(DEVINSTDATA));
    if (!DevInstData)
    {
        TRACE("HeapAlloc() failed\n");
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        goto cleanup;
    }

    /* Clear devinst data */
    ZeroMemory(DevInstData, sizeof(DEVINSTDATA));
    DevInstData->devInfoData.cbSize = 0; /* Tell if the devInfoData is valid */
    DevInstData->bUpdate = bUpdate;

    /* Fill devinst data */
    DevInstData->hDevInfo = SetupDiCreateDeviceInfoListExW(NULL, NULL, NULL, NULL);
    if (DevInstData->hDevInfo == INVALID_HANDLE_VALUE)
    {
        TRACE("SetupDiCreateDeviceInfoListExW() failed with error 0x%x\n", GetLastError());
        goto cleanup;
    }

    DevInstData->devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    ret = SetupDiOpenDeviceInfoW(
        DevInstData->hDevInfo,
        InstanceId,
        NULL,
        0, /* Open flags */
        &DevInstData->devInfoData);
    if (!ret)
    {
        TRACE("SetupDiOpenDeviceInfoW() failed with error 0x%x (InstanceId %s)\n",
            GetLastError(), debugstr_w(InstanceId));
        DevInstData->devInfoData.cbSize = 0;
        goto cleanup;
    }

    SetLastError(ERROR_GEN_FAILURE);
    ret = SetupDiGetDeviceRegistryProperty(
        DevInstData->hDevInfo,
        &DevInstData->devInfoData,
        SPDRP_DEVICEDESC,
        &DevInstData->regDataType,
        NULL, 0,
        &DevInstData->requiredSize);

    if (!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER && DevInstData->regDataType == REG_SZ)
    {
        DevInstData->buffer = HeapAlloc(GetProcessHeap(), 0, DevInstData->requiredSize);
        if (!DevInstData->buffer)
        {
            TRACE("HeapAlloc() failed\n");
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        }
        else
        {
            ret = SetupDiGetDeviceRegistryPropertyW(
                DevInstData->hDevInfo,
                &DevInstData->devInfoData,
                SPDRP_DEVICEDESC,
                &DevInstData->regDataType,
                DevInstData->buffer, DevInstData->requiredSize,
                &DevInstData->requiredSize);
        }
    }

    if (!ret)
    {
        TRACE("SetupDiGetDeviceRegistryProperty() failed with error 0x%x (InstanceId %s)\n",
            GetLastError(), debugstr_w(InstanceId));
        goto cleanup;
    }

    /* Prepare the wizard, and display it */
    TRACE("Need to show install wizard\n");
    retval = DisplayWizard(DevInstData, hWndParent, IDD_WELCOMEPAGE);

cleanup:
    if (DevInstData)
    {
        if (DevInstData->devInfoData.cbSize != 0)
        {
            if (!SetupDiDestroyDriverInfoList(DevInstData->hDevInfo, &DevInstData->devInfoData, SPDIT_COMPATDRIVER))
                TRACE("SetupDiDestroyDriverInfoList() failed with error 0x%lx\n", GetLastError());
        }
        if (DevInstData->hDevInfo != INVALID_HANDLE_VALUE)
        {
            if (!SetupDiDestroyDeviceInfoList(DevInstData->hDevInfo))
                TRACE("SetupDiDestroyDeviceInfoList() failed with error 0x%lx\n", GetLastError());
        }
        HeapFree(GetProcessHeap(), 0, DevInstData->buffer);
        HeapFree(GetProcessHeap(), 0, DevInstData);
    }

    return retval;
}


/*
 * @implemented
 */
BOOL
WINAPI
InstallDevInst(
    IN HWND hWndParent,
    IN LPCWSTR InstanceId,
    IN BOOL bUpdate,
    OUT LPDWORD lpReboot)
{
    return InstallDevInstEx(hWndParent, InstanceId, bUpdate, lpReboot, 0);
}


/*
* @implemented
*/
BOOL WINAPI
ClientSideInstallW(
    IN HWND hWndOwner,
    IN HINSTANCE hInstance,
    IN LPWSTR lpNamedPipeName,
    IN INT Show)
{
    BOOL ReturnValue = FALSE;
    BOOL ShowWizard;
    DWORD BytesRead;
    DWORD Value;
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    PWSTR DeviceInstance = NULL;
    PWSTR InstallEventName = NULL;
    HANDLE hInstallEvent;

    /* Open the pipe */
    hPipe = CreateFileW(lpNamedPipeName, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if(hPipe == INVALID_HANDLE_VALUE)
    {
        ERR("CreateFileW failed with error %u\n", GetLastError());
        goto cleanup;
    }

    /* Read the data. Some is just included for compatibility with Windows right now and not yet used by ReactOS.
       See umpnpmgr for more details. */
    if(!ReadFile(hPipe, &Value, sizeof(Value), &BytesRead, NULL))
    {
        ERR("ReadFile failed with error %u\n", GetLastError());
        goto cleanup;
    }

    InstallEventName = (PWSTR)HeapAlloc(GetProcessHeap(), 0, Value);

    if(!ReadFile(hPipe, InstallEventName, Value, &BytesRead, NULL))
    {
        ERR("ReadFile failed with error %u\n", GetLastError());
        goto cleanup;
    }

    /* I couldn't figure out what the following value means under Windows XP.
       Therefore I used it in umpnpmgr to pass the ShowWizard variable. */
    if(!ReadFile(hPipe, &ShowWizard, sizeof(ShowWizard), &BytesRead, NULL))
    {
        ERR("ReadFile failed with error %u\n", GetLastError());
        goto cleanup;
    }

    /* Next one is again size in bytes of the following string */
    if(!ReadFile(hPipe, &Value, sizeof(Value), &BytesRead, NULL))
    {
        ERR("ReadFile failed with error %u\n", GetLastError());
        goto cleanup;
    }

    DeviceInstance = (PWSTR)HeapAlloc(GetProcessHeap(), 0, Value);

    if(!ReadFile(hPipe, DeviceInstance, Value, &BytesRead, NULL))
    {
        ERR("ReadFile failed with error %u\n", GetLastError());
        goto cleanup;
    }

    ReturnValue = DevInstallW(NULL, NULL, DeviceInstance, ShowWizard ? SW_SHOWNOACTIVATE : SW_HIDE);
    if(!ReturnValue)
    {
        ERR("DevInstallW failed with error %lu\n", GetLastError());
        goto cleanup;
    }

    hInstallEvent = CreateEventW(NULL, TRUE, FALSE, InstallEventName);
    if(!hInstallEvent)
    {
        TRACE("CreateEventW('%ls') failed with error %lu\n", InstallEventName, GetLastError());
        goto cleanup;
    }

    SetEvent(hInstallEvent);
    CloseHandle(hInstallEvent);

cleanup:
    if(hPipe != INVALID_HANDLE_VALUE)
        CloseHandle(hPipe);

    if(InstallEventName)
        HeapFree(GetProcessHeap(), 0, InstallEventName);

    if(DeviceInstance)
        HeapFree(GetProcessHeap(), 0, DeviceInstance);

    return ReturnValue;
}


static ULONG
GetBatchDeviceDepth(
    IN PCWSTR DeviceInstance)
{
    CONFIGRET ConfigRet;
    DEVINST DeviceNode;
    DEVINST ParentNode;
    ULONG Depth = 0;

    ConfigRet = CM_Locate_DevNodeW(&DeviceNode, (DEVINSTID_W)DeviceInstance, CM_LOCATE_DEVNODE_NORMAL);
    if (ConfigRet != CR_SUCCESS)
        return NEWDEV_BATCH_DEPTH_UNKNOWN;

    while (Depth < NEWDEV_BATCH_MAX_DEPTH)
    {
        ConfigRet = CM_Get_Parent(&ParentNode, DeviceNode, 0);
        if (ConfigRet == CR_NO_SUCH_DEVNODE)
            return Depth;
        if (ConfigRet != CR_SUCCESS)
            return NEWDEV_BATCH_DEPTH_UNKNOWN;

        DeviceNode = ParentNode;
        Depth++;
    }

    return NEWDEV_BATCH_DEPTH_UNKNOWN;
}


static DWORD
RemoveDuplicateBatchDevices(
    IN OUT PBATCH_DEVICE Devices,
    IN DWORD DeviceCount)
{
    DWORD UniqueCount = 0;
    DWORD i;
    DWORD j;

    for (i = 0; i < DeviceCount; i++)
    {
        for (j = 0; j < UniqueCount; j++)
        {
            if (!lstrcmpiW(Devices[j].InstanceId, Devices[i].InstanceId))
                break;
        }

        if (j != UniqueCount)
        {
            HeapFree(GetProcessHeap(), 0, Devices[i].InstanceId);
            continue;
        }

        if (UniqueCount != i)
            Devices[UniqueCount] = Devices[i];
        UniqueCount++;
    }

    return UniqueCount;
}


static int __cdecl
CompareBatchDeviceDepths(
    const void *First,
    const void *Second)
{
    ULONG DepthA = ((const BATCH_DEVICE *)First)->Depth;
    ULONG DepthB = ((const BATCH_DEVICE *)Second)->Depth;

    return (DepthA > DepthB) - (DepthA < DepthB);
}

static VOID
SortBatchDevicesByDepth(
    IN OUT PBATCH_DEVICE Devices,
    IN DWORD DeviceCount)
{
    qsort(Devices, DeviceCount, sizeof(*Devices), CompareBatchDeviceDepths);
}


static DWORD
WINAPI
BatchInstallWorker(
    IN PVOID Parameter)
{
    PBATCH_WORK_CONTEXT Context = Parameter;
    LONG Index;

    for (;;)
    {
        Index = InterlockedIncrement(&Context->NextIndex);
        if (Index >= Context->EndIndex)
            break;

        TRACE("ClientSideInstallBatchW: installing %ls at depth %lu\n", Context->Devices[Index].InstanceId, Context->Devices[Index].Depth);
        if (!DevInstallInternal(NULL, NULL, Context->Devices[Index].InstanceId, SW_HIDE, TRUE))
            TRACE("DevInstallW failed for %ls (error %lu)\n", Context->Devices[Index].InstanceId, GetLastError());
    }

    return ERROR_SUCCESS;
}


static VOID
InstallBatchDepthRange(
    IN PBATCH_DEVICE Devices,
    IN DWORD FirstIndex,
    IN DWORD EndIndex)
{
    BATCH_WORK_CONTEXT Context;
    SYSTEM_INFO SystemInfo;
    HANDLE Threads[NEWDEV_BATCH_MAX_WORKERS - 1];
    DWORD DeviceCount = EndIndex - FirstIndex;
    DWORD ThreadCount = 0;
    DWORD WorkerCount;
    DWORD i;

    GetSystemInfo(&SystemInfo);
    WorkerCount = min(SystemInfo.dwNumberOfProcessors, NEWDEV_BATCH_MAX_WORKERS);
    WorkerCount = min(WorkerCount, DeviceCount);

    Context.Devices = Devices;
    Context.NextIndex = (LONG)FirstIndex - 1;
    Context.EndIndex = (LONG)EndIndex;

    TRACE("ClientSideInstallBatchW: depth %lu, %lu device(s), %lu worker(s)\n", Devices[FirstIndex].Depth, DeviceCount, WorkerCount);

    for (i = 1; i < WorkerCount; i++)
    {
        Threads[ThreadCount] = CreateThread(NULL, 0, BatchInstallWorker, &Context, 0, NULL);
        if (Threads[ThreadCount] != NULL)
            ThreadCount++;
    }

    BatchInstallWorker(&Context);

    for (i = 0; i < ThreadCount; i++)
    {
        WaitForSingleObject(Threads[i], INFINITE);
        CloseHandle(Threads[i]);
    }
}


/*
 * @implemented
 *
 * Batch variant of ClientSideInstallW. Reads the same prologue (event name
 * and ShowWizard) but then a DeviceCount followed by Count (size, instance)
 * pairs. Devices are grouped by devnode depth so parents finish before their
 * children. Driver discovery within one depth is parallel, while the final
 * class-installer/registry/file commit is serialized. If any hierarchy lookup
 * fails, the original input order is retained and the whole batch is serial.
 * The install event is signalled exactly once after the whole batch completes
 * so umpnpmgr's handshake behaves the same as for a single-device install.
 *
 * Used by umpnpmgr's DeviceInstallThread Step 1 (boot device list) to
 * avoid launching rundll32 once per device.
 */
BOOL WINAPI
ClientSideInstallBatchW(
    IN HWND hWndOwner,
    IN HINSTANCE hInstance,
    IN LPWSTR lpNamedPipeName,
    IN INT Show)
{
    BOOL ReturnValue = FALSE;
    BOOL ShowWizard;
    DWORD BytesRead;
    DWORD Value;
    DWORD DeviceCount;
    DWORD FirstIndex;
    DWORD EndIndex;
    DWORD i;
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    PWSTR DeviceInstance = NULL;
    PWSTR InstallEventName = NULL;
    HANDLE hInstallEvent;
    BOOL CacheBatchActive = FALSE;
    BOOL BatchDepthsKnown = TRUE;
    PBATCH_DEVICE Devices = NULL;

    /* Open the pipe */
    hPipe = CreateFileW(lpNamedPipeName, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hPipe == INVALID_HANDLE_VALUE)
    {
        ERR("CreateFileW failed with error %u\n", GetLastError());
        goto cleanup;
    }

    /* Prologue — same as ClientSideInstallW: event name size + event name */
    if (!ReadFile(hPipe, &Value, sizeof(Value), &BytesRead, NULL))
    {
        ERR("ReadFile(cbEventName) failed with error %u\n", GetLastError());
        goto cleanup;
    }

    InstallEventName = HeapAlloc(GetProcessHeap(), 0, Value);
    if (!InstallEventName)
    {
        ERR("HeapAlloc(InstallEventName) failed\n");
        goto cleanup;
    }

    if (!ReadFile(hPipe, InstallEventName, Value, &BytesRead, NULL))
    {
        ERR("ReadFile(EventName) failed with error %u\n", GetLastError());
        goto cleanup;
    }

    /* Consumed for symmetry with the single-install protocol. Batch mode is
     * always driven with SW_HIDE so this is effectively informational. */
    if (!ReadFile(hPipe, &ShowWizard, sizeof(ShowWizard), &BytesRead, NULL))
    {
        ERR("ReadFile(ShowWizard) failed with error %u\n", GetLastError());
        goto cleanup;
    }

    /* Batch-specific: device count, followed by N (size, instance) pairs */
    if (!ReadFile(hPipe, &DeviceCount, sizeof(DeviceCount), &BytesRead, NULL))
    {
        ERR("ReadFile(DeviceCount) failed with error %u\n", GetLastError());
        goto cleanup;
    }

    TRACE("ClientSideInstallBatchW: processing %lu device(s)\n", DeviceCount);

    if (DeviceCount > NEWDEV_BATCH_MAX_DEVICES)
    {
        ERR("ClientSideInstallBatchW: invalid device count %lu\n", DeviceCount);
        goto cleanup;
    }

    if (DeviceCount != 0)
    {
        Devices = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, DeviceCount * sizeof(*Devices));
        if (Devices == NULL)
        {
            ERR("HeapAlloc(Devices[%lu]) failed\n", DeviceCount);
            goto cleanup;
        }
    }

    for (i = 0; i < DeviceCount; i++)
    {
        if (!ReadFile(hPipe, &Value, sizeof(Value), &BytesRead, NULL))
        {
            ERR("ReadFile(cbDeviceInstance[%lu]) failed with error %u\n", i, GetLastError());
            goto cleanup;
        }

        if (Value < sizeof(WCHAR) || Value > (MAX_DEVICE_ID_LEN + 1) * sizeof(WCHAR) || (Value % sizeof(WCHAR)) != 0)
        {
            ERR("Invalid DeviceInstance[%lu] size %lu\n", i, Value);
            goto cleanup;
        }

        DeviceInstance = HeapAlloc(GetProcessHeap(), 0, Value);
        if (!DeviceInstance)
        {
            ERR("HeapAlloc(DeviceInstance[%lu]) failed\n", i);
            goto cleanup;
        }

        if (!ReadFile(hPipe, DeviceInstance, Value, &BytesRead, NULL))
        {
            ERR("ReadFile(DeviceInstance[%lu]) failed with error %u\n", i, GetLastError());
            goto cleanup;
        }

        if (DeviceInstance[Value / sizeof(WCHAR) - 1] != UNICODE_NULL)
        {
            ERR("DeviceInstance[%lu] is not terminated\n", i);
            goto cleanup;
        }

        Devices[i].InstanceId = DeviceInstance;
        DeviceInstance = NULL;
    }

    DeviceCount = RemoveDuplicateBatchDevices(Devices, DeviceCount);

    for (i = 0; i < DeviceCount; i++)
    {
        Devices[i].Depth = GetBatchDeviceDepth(Devices[i].InstanceId);
        if (Devices[i].Depth == NEWDEV_BATCH_DEPTH_UNKNOWN)
            BatchDepthsKnown = FALSE;
    }

    if (BatchDepthsKnown)
        SortBatchDevicesByDepth(Devices, DeviceCount);

    NoDriverCacheBeginBatch();
    CacheBatchActive = TRUE;

    /* Best-effort install: individual failures are logged but don't abort
     * the batch, matching the pre-existing boot loop behaviour. Per-device
    * progress for the serial log is emitted by umpnpmgr's batch caller. */
    FirstIndex = 0;
    while (FirstIndex < DeviceCount)
    {
        EndIndex = FirstIndex + 1;
        if (BatchDepthsKnown)
        {
            while (EndIndex < DeviceCount && Devices[EndIndex].Depth == Devices[FirstIndex].Depth)
                EndIndex++;
        }

        InstallBatchDepthRange(Devices, FirstIndex, EndIndex);
        FirstIndex = EndIndex;
    }

    /* Signal completion of the whole batch exactly once. */
    hInstallEvent = CreateEventW(NULL, TRUE, FALSE, InstallEventName);
    if (!hInstallEvent)
    {
        TRACE("CreateEventW('%ls') failed with error %lu\n", InstallEventName, GetLastError());
        goto cleanup;
    }

    SetEvent(hInstallEvent);
    CloseHandle(hInstallEvent);

    ReturnValue = TRUE;

cleanup:
    if (hPipe != INVALID_HANDLE_VALUE)
        CloseHandle(hPipe);

    if (InstallEventName)
        HeapFree(GetProcessHeap(), 0, InstallEventName);

    if (DeviceInstance)
        HeapFree(GetProcessHeap(), 0, DeviceInstance);

    if (Devices)
    {
        for (i = 0; i < DeviceCount; i++)
            HeapFree(GetProcessHeap(), 0, Devices[i].InstanceId);
        HeapFree(GetProcessHeap(), 0, Devices);
    }

    if (CacheBatchActive)
        NoDriverCacheEndBatch();

    return ReturnValue;
}


BOOL WINAPI
DllMain(
    IN HINSTANCE hInstance,
    IN DWORD dwReason,
    IN LPVOID lpReserved)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        INITCOMMONCONTROLSEX InitControls;

        DisableThreadLibraryCalls(hInstance);
        InitializeCriticalSection(&NoDriverIdCacheLock);
        InitializeDeviceInstallPhaseLock();
        NoDriverIdCacheReady = TRUE;

        InitControls.dwSize = sizeof(INITCOMMONCONTROLSEX);
        InitControls.dwICC = ICC_PROGRESS_CLASS;
        InitCommonControlsEx(&InitControls);
        hDllInstance = hInstance;
    }
    else if (dwReason == DLL_PROCESS_DETACH)
    {
        FreeNoDriverCache();
        DeleteDeviceInstallPhaseLock();
    }

    return TRUE;
}
