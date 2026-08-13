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
#include "driver_cache.h"

#include <cfgmgr32.h>
#include <stdio.h>
#include <winnls.h>

/* Global variables */
HINSTANCE hDllInstance;

typedef enum _DRIVER_SEARCH_RESULT
{
    DriverSearchError,
    DriverSearchNotFound,
    DriverSearchFound
} DRIVER_SEARCH_RESULT;

static BOOL
SearchDriver(
    IN PDEVINSTDATA DevInstData,
    IN LPCWSTR Directory OPTIONAL,
    IN LPCWSTR InfFile OPTIONAL);

static DRIVER_SEARCH_RESULT
SearchDriverResult(
    IN PDEVINSTDATA DevInstData,
    IN LPCWSTR Directory OPTIONAL,
    IN LPCWSTR InfFile OPTIONAL);

static BOOL
InstallNullDriver(
    IN PDEVINSTDATA DevInstData);

static BOOL
InstallDevicesFromBatchPipe(
    IN HANDLE hPipe);

static BOOL
ReadPipeData(
    IN HANDLE hPipe,
    OUT PVOID Buffer,
    IN DWORD Size)
{
    PBYTE Cursor = Buffer;
    DWORD BytesRead;

    while (Size != 0)
    {
        if (!ReadFile(hPipe, Cursor, Size, &BytesRead, NULL) || BytesRead == 0)
            return FALSE;

        Cursor += BytesRead;
        Size -= BytesRead;
    }

    return TRUE;
}

static BOOL
AcquireBatchDriverCacheEntry(
    IN PDEVINSTDATA DevInstData,
    IN LPCWSTR HardwareIds OPTIONAL,
    IN LPCWSTR CompatibleIds OPTIONAL,
    OUT PNEWDEV_DRIVER_CACHE_ENTRY *CacheEntry)
{
    NEWDEV_DRIVER_CACHE_ACQUIRE AcquireResult;
    DWORD Error = ERROR_SUCCESS;

    for (;;)
    {
        AcquireResult = NewDevDriverCacheTryAcquire(&DevInstData->devInfoData.ClassGuid, HardwareIds, CompatibleIds, CacheEntry);
        if (AcquireResult != NewDevDriverCacheBusy)
            break;

        if (!NewDevDriverCacheWait(*CacheEntry))
        {
            Error = GetLastError();
            AcquireResult = NewDevDriverCacheError;
            break;
        }
    }

    if (AcquireResult == NewDevDriverCacheError && Error == ERROR_SUCCESS)
        Error = GetLastError();

    if (AcquireResult == NewDevDriverCacheError)
    {
        *CacheEntry = NULL;
        SetLastError(Error);
    }

    return AcquireResult != NewDevDriverCacheError;
}

static BOOL
GetDeviceMultiSzProperty(
    IN PDEVINSTDATA DevInstData,
    IN DWORD Property,
    OUT PWSTR *PropertyValue)
{
    PWSTR Buffer = NULL;
    DWORD DataType;
    DWORD RequiredSize = 0;
    DWORD Error;
    BOOL Result;

    *PropertyValue = NULL;
    Result = SetupDiGetDeviceRegistryPropertyW(DevInstData->hDevInfo, &DevInstData->devInfoData, Property, &DataType, NULL, 0, &RequiredSize);
    if (Result)
    {
        if (DataType == REG_MULTI_SZ)
            return TRUE;

        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }

    Error = GetLastError();
    if (Error == ERROR_FILE_NOT_FOUND)
        return TRUE;
    if (Error != ERROR_INSUFFICIENT_BUFFER || RequiredSize == 0)
    {
        SetLastError(Error != ERROR_INSUFFICIENT_BUFFER ? Error : ERROR_INVALID_DATA);
        return FALSE;
    }

    Buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, RequiredSize + sizeof(WCHAR));
    if (!Buffer)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    Result = SetupDiGetDeviceRegistryPropertyW(DevInstData->hDevInfo, &DevInstData->devInfoData, Property, &DataType, (PBYTE)Buffer, RequiredSize, &RequiredSize);
    if (!Result)
    {
        Error = GetLastError();
        HeapFree(GetProcessHeap(), 0, Buffer);
        SetLastError(Error);
        return FALSE;
    }
    if (DataType != REG_MULTI_SZ)
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }

    *PropertyValue = Buffer;
    return TRUE;
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
static DRIVER_SEARCH_RESULT
SearchDriverResult(
    IN PDEVINSTDATA DevInstData,
    IN LPCWSTR Directory OPTIONAL,
    IN LPCWSTR InfFile OPTIONAL)
{
    SP_DEVINSTALL_PARAMS_W DevInstallParams = {0,};
    BOOL ret;
    DWORD Error;

    DevInstallParams.cbSize = sizeof(SP_DEVINSTALL_PARAMS_W);
    if (!SetupDiGetDeviceInstallParamsW(DevInstData->hDevInfo, &DevInstData->devInfoData, &DevInstallParams))
    {
        Error = GetLastError();
        TRACE("SetupDiGetDeviceInstallParams() failed with error 0x%x\n", Error);
        SetLastError(Error);
        return DriverSearchError;
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
        Error = GetLastError();
        TRACE("SetupDiSetDeviceInstallParams() failed with error 0x%x\n", Error);
        SetLastError(Error);
        return DriverSearchError;
    }

    ret = SetupDiBuildDriverInfoList(
        DevInstData->hDevInfo,
        &DevInstData->devInfoData,
        SPDIT_COMPATDRIVER);
    if (!ret)
    {
        Error = GetLastError();

        TRACE("SetupDiBuildDriverInfoList() failed with error 0x%x\n", Error);
        if (Error == ERROR_FILE_NOT_FOUND)
        {
            SetLastError(Error);
            return DriverSearchNotFound;
        }
        SetLastError(Error);
        return DriverSearchError;
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
        Error = GetLastError();
        if (Error == ERROR_NO_MORE_ITEMS)
        {
            SetLastError(Error);
            return DriverSearchNotFound;
        }
        TRACE("SetupDiEnumDriverInfo() failed with error 0x%x\n", Error);
        SetLastError(Error);
        return DriverSearchError;
    }

    return DriverSearchFound;
}

static BOOL
SearchDriver(
    IN PDEVINSTDATA DevInstData,
    IN LPCWSTR Directory OPTIONAL,
    IN LPCWSTR InfFile OPTIONAL)
{
    return SearchDriverResult(DevInstData, Directory, InfFile) == DriverSearchFound;
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

static DRIVER_SEARCH_RESULT
ScanFoldersForDriverResult(
    IN PDEVINSTDATA DevInstData)
{
    DRIVER_SEARCH_RESULT Result;
    DRIVER_SEARCH_RESULT PathResult;
    DWORD SearchError = ERROR_SUCCESS;

    /* Search in default location */
    Result = SearchDriverResult(DevInstData, NULL, NULL);
    if (Result == DriverSearchError)
        SearchError = GetLastError();

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
                    Result = DriverSearchFound;
            }
            else
            {
                PathResult = SearchDriverResult(DevInstData, Path, NULL);
                if (PathResult == DriverSearchFound)
                    Result = DriverSearchFound;
                else if (PathResult == DriverSearchError && Result != DriverSearchFound && SearchError == ERROR_SUCCESS)
                    SearchError = GetLastError();
            }
        }
    }

    if (Result != DriverSearchFound && SearchError != ERROR_SUCCESS)
    {
        SetLastError(SearchError);
        return DriverSearchError;
    }

    return Result;
}

BOOL
ScanFoldersForDriver(
    IN PDEVINSTDATA DevInstData)
{
    return ScanFoldersForDriverResult(DevInstData) == DriverSearchFound;
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
    IN INT Show)
{
    PDEVINSTDATA DevInstData = NULL;
    PWSTR HardwareIds = NULL;
    PWSTR CompatibleIds = NULL;
    WCHAR CachedInfFile[MAX_PATH];
    WCHAR InstalledInfFile[MAX_PATH];
    PNEWDEV_DRIVER_CACHE_ENTRY DriverCacheEntry = NULL;
    NEWDEV_DRIVER_CACHE_RESULT DriverCacheResult;
    DRIVER_SEARCH_RESULT SearchResult;
    BOOL ret;
    DWORD LastError;
    DWORD config_flags;
    BOOL CachedNullDriver = FALSE;
    BOOL retval = FALSE;

    TRACE("(%p, %p, %s, %d)\n", hWndParent, hInstance, debugstr_w(InstanceId), Show);

    if (!IsUserAdmin())
    {
        /* XP kills the process... */
        ExitProcess(ERROR_ACCESS_DENIED);
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
        if (!GetDeviceMultiSzProperty(DevInstData, SPDRP_HARDWAREID, &HardwareIds) || !GetDeviceMultiSzProperty(DevInstData, SPDRP_COMPATIBLEIDS, &CompatibleIds))
        {
            LastError = GetLastError();
            SetLastError(LastError);
            goto cleanup;
        }

        if (!AcquireBatchDriverCacheEntry(DevInstData, HardwareIds, CompatibleIds, &DriverCacheEntry))
        {
            goto cleanup;
        }

        if (DriverCacheEntry)
        {
            DriverCacheResult = NewDevDriverCacheGetResult(DriverCacheEntry, CachedInfFile, sizeof(CachedInfFile) / sizeof(CachedInfFile[0]), &CachedNullDriver);
            if (DriverCacheResult == NewDevDriverCacheFound)
            {
                SearchResult = SearchDriverResult(DevInstData, NULL, CachedInfFile);
                if (SearchResult == DriverSearchFound)
                {
                    retval = CachedNullDriver ? InstallNullDriver(DevInstData) : InstallCurrentDriver(DevInstData);
                    TRACE("Cached driver install returned %d\n", retval);
                    goto cleanup;
                }

                LastError = GetLastError();
                TRACE("Cached driver INF %ls could not be selected for %s (result %u, error %lu)\n", CachedInfFile, debugstr_w(InstanceId), SearchResult, LastError);
                if (SearchResult == DriverSearchError)
                {
                    SetLastError(LastError);
                    goto cleanup;
                }
                NewDevDriverCacheInvalidate(DriverCacheEntry);
            }
            else if (DriverCacheResult == NewDevDriverCacheNotFoundPending ||
                     DriverCacheResult == NewDevDriverCacheNotFound)
            {
                SearchResult = DriverSearchNotFound;
                goto handle_search_result;
            }
        }
    }

    /* Search driver in default location and removable devices */
    if (!PrepareFoldersToScan(DevInstData, FALSE, FALSE, NULL))
    {
        LastError = GetLastError();
        TRACE("PrepareFoldersToScan() failed with error 0x%lx\n", LastError);
        SetLastError(LastError);
        goto cleanup;
    }
    SearchResult = ScanFoldersForDriverResult(DevInstData);
handle_search_result:
    if (SearchResult == DriverSearchFound)
    {
        if (DriverCacheEntry && GetCurrentDriverInfFileName(DevInstData, InstalledInfFile, sizeof(InstalledInfFile) / sizeof(InstalledInfFile[0])))
            NewDevDriverCacheRememberFound(DriverCacheEntry, InstalledInfFile, IsCurrentDriverNullInstall(DevInstData));

        /* Driver found; install it. */
        retval = InstallCurrentDriver(DevInstData);
        TRACE("InstallCurrentDriver() returned %d\n", retval);

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
        if (SearchResult == DriverSearchError)
        {
            LastError = GetLastError();
            TRACE("Driver discovery failed for %s with error %lu\n", debugstr_w(InstanceId), LastError);
            SetLastError(LastError);
            goto cleanup;
        }

        if (DriverCacheEntry)
            NewDevDriverCacheRememberNotFound(DriverCacheEntry);

        /* We can't show the wizard. Fail the install */
        TRACE("No wizard\n");
        if (!DevInstData->bUpdate)
        {
            if (!NewDevSetFailedInstall(DevInstData->hDevInfo, &DevInstData->devInfoData, TRUE))
            {
                TRACE("NewDevSetFailedInstall() failed with error 0x%lx\n", GetLastError());
            }
        }
        SetLastError(ERROR_FILE_NOT_FOUND);
        goto cleanup;
    }

    /* Prepare the wizard, and display it */
    TRACE("Need to show install wizard\n");
    retval = DisplayWizard(DevInstData, hWndParent, IDD_WELCOMEPAGE);

cleanup:
    LastError = GetLastError();

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

    if (DriverCacheEntry)
        NewDevDriverCacheRelease(DriverCacheEntry);

    SetLastError(LastError);
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
    return DevInstallInternal(hWndParent, hInstance, InstanceId, Show);
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
    if (!ReadPipeData(hPipe, &Value, sizeof(Value)))
    {
        ERR("ReadFile failed with error %u\n", GetLastError());
        goto cleanup;
    }

    if (Value < sizeof(WCHAR) || Value > 512 * sizeof(WCHAR) || (Value % sizeof(WCHAR)) != 0)
    {
        ERR("Invalid install event name size %lu\n", Value);
        goto cleanup;
    }

    InstallEventName = (PWSTR)HeapAlloc(GetProcessHeap(), 0, Value);
    if (!InstallEventName)
        goto cleanup;

    if (!ReadPipeData(hPipe, InstallEventName, Value))
    {
        ERR("ReadFile failed with error %u\n", GetLastError());
        goto cleanup;
    }
    if (InstallEventName[Value / sizeof(WCHAR) - 1] != UNICODE_NULL)
    {
        ERR("Install event name is not terminated\n");
        goto cleanup;
    }

    /* I couldn't figure out what the following value means under Windows XP.
       Therefore I used it in umpnpmgr to pass the ShowWizard variable. */
    if (!ReadPipeData(hPipe, &ShowWizard, sizeof(ShowWizard)))
    {
        ERR("ReadFile failed with error %u\n", GetLastError());
        goto cleanup;
    }

    /* A marker selects the private ReactOS batch extension. Otherwise the
     * value is the native protocol's device-instance size in bytes. */
    if (!ReadPipeData(hPipe, &Value, sizeof(Value)))
    {
        ERR("ReadFile failed with error %u\n", GetLastError());
        goto cleanup;
    }

    if (Value == NEWDEV_INSTALL_BATCH_MARKER)
    {
        ReturnValue = InstallDevicesFromBatchPipe(hPipe);
    }
    else
    {
        if (Value < sizeof(WCHAR) || Value > (MAX_DEVICE_ID_LEN + 1) * sizeof(WCHAR) || (Value % sizeof(WCHAR)) != 0)
        {
            ERR("Invalid device instance size %lu\n", Value);
            goto cleanup;
        }

        DeviceInstance = (PWSTR)HeapAlloc(GetProcessHeap(), 0, Value);
        if (!DeviceInstance)
            goto cleanup;

        if (!ReadPipeData(hPipe, DeviceInstance, Value))
        {
            ERR("ReadFile failed with error %u\n", GetLastError());
            goto cleanup;
        }
        if (DeviceInstance[Value / sizeof(WCHAR) - 1] != UNICODE_NULL)
        {
            ERR("Device instance is not terminated\n");
            goto cleanup;
        }

        ReturnValue = DevInstallW(NULL, NULL, DeviceInstance, ShowWizard ? SW_SHOWNOACTIVATE : SW_HIDE);
    }

    if (!ReturnValue)
    {
        ERR("DevInstallW failed with error %lu\n", GetLastError());
        goto cleanup;
    }

    hInstallEvent = CreateEventW(NULL, TRUE, FALSE, InstallEventName);
    if (!hInstallEvent)
    {
        TRACE("CreateEventW('%ls') failed with error %lu\n", InstallEventName, GetLastError());
        goto cleanup;
    }

    SetEvent(hInstallEvent);
    CloseHandle(hInstallEvent);

cleanup:
    if (hPipe != INVALID_HANDLE_VALUE)
        CloseHandle(hPipe);

    if (InstallEventName)
        HeapFree(GetProcessHeap(), 0, InstallEventName);

    if (DeviceInstance)
        HeapFree(GetProcessHeap(), 0, DeviceInstance);

    return ReturnValue;
}


/*
 * Read and install a batch carried by the private ClientSideInstallW pipe
 * extension. Keep all SetupAPI/device commits on one thread; these operations
 * are not independent and native newdev.dll exposes no parallel batch entry.
 */
static BOOL
InstallDevicesFromBatchPipe(
    IN HANDLE hPipe)
{
    BOOL ReturnValue = FALSE;
    BOOL CacheBatchActive = FALSE;
    DWORD DeviceCount;
    DWORD Value;
    DWORD i;
    PWSTR DeviceInstance = NULL;

    if (!ReadPipeData(hPipe, &DeviceCount, sizeof(DeviceCount)))
    {
        ERR("ReadFile(DeviceCount) failed with error %u\n", GetLastError());
        goto cleanup;
    }

    if (DeviceCount > NEWDEV_INSTALL_BATCH_MAX_DEVICES)
    {
        ERR("Invalid batch device count %lu\n", DeviceCount);
        goto cleanup;
    }

    TRACE("ClientSideInstallW: processing batch of %lu device(s)\n", DeviceCount);
    NewDevDriverCacheBeginBatch();
    CacheBatchActive = TRUE;

    for (i = 0; i < DeviceCount; i++)
    {
        if (!ReadPipeData(hPipe, &Value, sizeof(Value)))
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

        if (!ReadPipeData(hPipe, DeviceInstance, Value))
        {
            ERR("ReadFile(DeviceInstance[%lu]) failed with error %u\n", i, GetLastError());
            goto cleanup;
        }

        if (DeviceInstance[Value / sizeof(WCHAR) - 1] != UNICODE_NULL)
        {
            ERR("DeviceInstance[%lu] is not terminated\n", i);
            goto cleanup;
        }

        TRACE("ClientSideInstallW: installing [%lu/%lu] %ls\n", i + 1, DeviceCount, DeviceInstance);
        if (!DevInstallW(NULL, NULL, DeviceInstance, SW_HIDE))
            TRACE("DevInstallW failed for %ls (error %lu)\n", DeviceInstance, GetLastError());

        HeapFree(GetProcessHeap(), 0, DeviceInstance);
        DeviceInstance = NULL;
    }

    ReturnValue = TRUE;

cleanup:
    if (DeviceInstance)
        HeapFree(GetProcessHeap(), 0, DeviceInstance);

    if (CacheBatchActive)
        NewDevDriverCacheEndBatch();

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
        NewDevDriverCacheInitialize();

        InitControls.dwSize = sizeof(INITCOMMONCONTROLSEX);
        InitControls.dwICC = ICC_PROGRESS_CLASS;
        InitCommonControlsEx(&InitControls);
        hDllInstance = hInstance;
    }
    else if (dwReason == DLL_PROCESS_DETACH)
    {
        NewDevDriverCacheUninitialize();
    }

    return TRUE;
}
