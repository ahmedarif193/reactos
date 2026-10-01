/*
 * PROJECT:     LiberNT PnP Utility
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Manage driver packages and devices from the command line
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <winuser.h>
#include <objbase.h>
#include <reason.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <cfg.h>
#include <newdev.h>
#include <regstr.h>

#include <conutils.h>

#define PROBLEM_NAME(x) { x, L"" #x }

typedef struct _ADD_CONTEXT
{
    BOOL Install;
    BOOL SubDirs;
    DWORD Total;
    DWORD Added;
    BOOL NeedReboot;
    DWORD Error;
} ADD_CONTEXT, *PADD_CONTEXT;

typedef struct _ENUM_FILTER
{
    BOOL Connected;
    BOOL Disconnected;
    BOOL Problem;
    BOOL HasProblemCode;
    ULONG ProblemCode;
    BOOL Ids;
    PCWSTR InstanceId;
    PCWSTR Class;
} ENUM_FILTER, *PENUM_FILTER;

static const struct
{
    ULONG Code;
    PCWSTR Name;
} ProblemNames[] =
{
    PROBLEM_NAME(CM_PROB_NOT_CONFIGURED),
    PROBLEM_NAME(CM_PROB_DEVLOADER_FAILED),
    PROBLEM_NAME(CM_PROB_OUT_OF_MEMORY),
    PROBLEM_NAME(CM_PROB_ENTRY_IS_WRONG_TYPE),
    PROBLEM_NAME(CM_PROB_LACKED_ARBITRATOR),
    PROBLEM_NAME(CM_PROB_BOOT_CONFIG_CONFLICT),
    PROBLEM_NAME(CM_PROB_FAILED_FILTER),
    PROBLEM_NAME(CM_PROB_DEVLOADER_NOT_FOUND),
    PROBLEM_NAME(CM_PROB_INVALID_DATA),
    PROBLEM_NAME(CM_PROB_FAILED_START),
    PROBLEM_NAME(CM_PROB_LIAR),
    PROBLEM_NAME(CM_PROB_NORMAL_CONFLICT),
    PROBLEM_NAME(CM_PROB_NOT_VERIFIED),
    PROBLEM_NAME(CM_PROB_NEED_RESTART),
    PROBLEM_NAME(CM_PROB_REENUMERATION),
    PROBLEM_NAME(CM_PROB_PARTIAL_LOG_CONF),
    PROBLEM_NAME(CM_PROB_UNKNOWN_RESOURCE),
    PROBLEM_NAME(CM_PROB_REINSTALL),
    PROBLEM_NAME(CM_PROB_REGISTRY),
    PROBLEM_NAME(CM_PROB_VXDLDR),
    PROBLEM_NAME(CM_PROB_WILL_BE_REMOVED),
    PROBLEM_NAME(CM_PROB_DISABLED),
    PROBLEM_NAME(CM_PROB_DEVLOADER_NOT_READY),
    PROBLEM_NAME(CM_PROB_DEVICE_NOT_THERE),
    PROBLEM_NAME(CM_PROB_MOVED),
    PROBLEM_NAME(CM_PROB_TOO_EARLY),
    PROBLEM_NAME(CM_PROB_NO_VALID_LOG_CONF),
    PROBLEM_NAME(CM_PROB_FAILED_INSTALL),
    PROBLEM_NAME(CM_PROB_HARDWARE_DISABLED),
    PROBLEM_NAME(CM_PROB_CANT_SHARE_IRQ),
    PROBLEM_NAME(CM_PROB_FAILED_ADD),
    PROBLEM_NAME(CM_PROB_DISABLED_SERVICE),
    PROBLEM_NAME(CM_PROB_TRANSLATION_FAILED),
    PROBLEM_NAME(CM_PROB_NO_SOFTCONFIG),
    PROBLEM_NAME(CM_PROB_BIOS_TABLE),
    PROBLEM_NAME(CM_PROB_IRQ_TRANSLATION_FAILED),
    PROBLEM_NAME(CM_PROB_FAILED_DRIVER_ENTRY),
    PROBLEM_NAME(CM_PROB_DRIVER_FAILED_PRIOR_UNLOAD),
    PROBLEM_NAME(CM_PROB_DRIVER_FAILED_LOAD),
    PROBLEM_NAME(CM_PROB_DRIVER_SERVICE_KEY_INVALID),
    PROBLEM_NAME(CM_PROB_LEGACY_SERVICE_NO_DEVICES),
    PROBLEM_NAME(CM_PROB_DUPLICATE_DEVICE),
    PROBLEM_NAME(CM_PROB_FAILED_POST_START),
    PROBLEM_NAME(CM_PROB_HALTED),
    PROBLEM_NAME(CM_PROB_PHANTOM),
    PROBLEM_NAME(CM_PROB_SYSTEM_SHUTDOWN),
    PROBLEM_NAME(CM_PROB_HELD_FOR_EJECT),
    PROBLEM_NAME(CM_PROB_DRIVER_BLOCKED),
    PROBLEM_NAME(CM_PROB_REGISTRY_TOO_LARGE),
    PROBLEM_NAME(CM_PROB_SETPROPERTIES_FAILED),
    PROBLEM_NAME(CM_PROB_WAITING_ON_DEPENDENCY),
    PROBLEM_NAME(CM_PROB_UNSIGNED_DRIVER),
};

static const WCHAR UsageText[] =
    L"PNPUTIL [/add-driver <...> | /delete-driver <...> |\n"
    L"         /enum-drivers | /enum-devices [<...>] |\n"
    L"         /disable-device <...> | /enable-device <...> |\n"
    L"         /restart-device <...> | /remove-device <...> |\n"
    L"         /scan-devices | /?]\n"
    L"\n"
    L"Commands:\n"
    L"\n"
    L"  /add-driver <filename.inf | *.inf> [/subdirs] [/install] [/reboot]\n"
    L"\n"
    L"    Add driver package(s) into the driver store.\n"
    L"      /subdirs - traverse sub directories for driver packages.\n"
    L"      /install - install/update drivers on any matching devices.\n"
    L"      /reboot - reboot system if needed to complete the operation.\n"
    L"\n"
    L"  /delete-driver <oem#.inf> [/uninstall] [/force] [/reboot]\n"
    L"\n"
    L"    Delete driver package from the driver store.\n"
    L"      /uninstall - uninstall driver package from any devices using it.\n"
    L"      /force - delete driver package even when it is in use by devices.\n"
    L"      /reboot - reboot system if needed to complete the operation.\n"
    L"\n"
    L"  /enum-drivers\n"
    L"\n"
    L"    Enumerate all 3rd party driver packages in the driver store.\n"
    L"\n"
    L"  /enum-devices [/connected | /disconnected] [/instanceid <instance ID>]\n"
    L"                [/class <name | GUID>] [/problem [<code>]] [/ids]\n"
    L"\n"
    L"    Enumerate all devices on the system.\n"
    L"      /connected | /disconnected - filter by connected or disconnected devices.\n"
    L"      /instanceid <instance ID> - filter by device instance ID.\n"
    L"      /class <name | GUID> - filter by device class name or GUID.\n"
    L"      /problem [<code>] - filter by devices with problems or specific problem code.\n"
    L"      /ids - display hardware IDs and compatible IDs.\n"
    L"\n"
    L"  /disable-device <instance ID> [/reboot]\n"
    L"  /enable-device <instance ID> [/reboot]\n"
    L"  /restart-device <instance ID> [/reboot]\n"
    L"  /remove-device <instance ID> [/reboot]\n"
    L"\n"
    L"    Disable, enable, restart or remove a device.\n"
    L"      /reboot - reboot system if needed to complete the operation.\n"
    L"\n"
    L"  /scan-devices\n"
    L"\n"
    L"    Scan the system for any device hardware changes.\n"
    L"\n"
    L"Legacy Commands:\n"
    L"\n"
    L"  [-i] -a <filename.inf>  ==>  /add-driver <filename.inf> [/install]\n"
    L"  [-f] -d <oem#.inf>      ==>  /delete-driver <oem#.inf> [/force]\n"
    L"  -e                      ==>  /enum-drivers\n";

static BOOL
IsOption(
    _In_ PCWSTR Argument,
    _In_ PCWSTR Name)
{
    return (Argument[0] == L'/' || Argument[0] == L'-') && _wcsicmp(Argument + 1, Name) == 0;
}

static VOID
PrintError(
    _In_ PCWSTR Prefix,
    _In_ DWORD Error)
{
    WCHAR Message[512];
    DWORD Length;

    Length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                            NULL,
                            Error,
                            0,
                            Message,
                            _countof(Message),
                            NULL);
    while (Length > 0 && (Message[Length - 1] == L'\r' || Message[Length - 1] == L'\n' || Message[Length - 1] == L' '))
        Message[--Length] = UNICODE_NULL;

    if (Length > 0)
        ConPrintf(StdOut, L"%ls: %ls\n", Prefix, Message);
    else
        ConPrintf(StdOut, L"%ls: error 0x%08lx\n", Prefix, Error);
}

static BOOL
RebootSystem(VOID)
{
    TOKEN_PRIVILEGES Privileges;
    HANDLE hToken;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return FALSE;

    Privileges.PrivilegeCount = 1;
    Privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &Privileges.Privileges[0].Luid))
        AdjustTokenPrivileges(hToken, FALSE, &Privileges, 0, NULL, NULL);
    CloseHandle(hToken);

    return ExitWindowsEx(EWX_REBOOT, SHTDN_REASON_MAJOR_HARDWARE | SHTDN_REASON_MINOR_INSTALLATION | SHTDN_REASON_FLAG_PLANNED);
}

static INT
FinishOperation(
    _In_ DWORD Error,
    _In_ BOOL NeedReboot,
    _In_ BOOL Reboot)
{
    if (Error != ERROR_SUCCESS)
        return (INT)Error;

    if (!NeedReboot)
        return ERROR_SUCCESS;

    if (Reboot)
    {
        ConPuts(StdOut, L"System reboot is required to complete the operation. Rebooting.\n");
        if (RebootSystem())
            return ERROR_SUCCESS_REBOOT_INITIATED;
    }
    else
    {
        ConPuts(StdOut, L"System reboot is needed to complete the operation!\n");
    }

    return ERROR_SUCCESS_REBOOT_REQUIRED;
}

static BOOL
GetDriverKeyString(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ PCWSTR ValueName,
    _Out_writes_(BufferSize) PWSTR Buffer,
    _In_ DWORD BufferSize)
{
    DWORD Size = (BufferSize - 1) * sizeof(WCHAR);
    DWORD Type;
    HKEY hKey;
    LONG Result;

    Buffer[0] = UNICODE_NULL;
    hKey = SetupDiOpenDevRegKey(DeviceInfoSet, DeviceInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_QUERY_VALUE);
    if (hKey == INVALID_HANDLE_VALUE)
        return FALSE;

    Result = RegQueryValueExW(hKey, ValueName, NULL, &Type, (LPBYTE)Buffer, &Size);
    RegCloseKey(hKey);
    if (Result != ERROR_SUCCESS || Type != REG_SZ)
    {
        Buffer[0] = UNICODE_NULL;
        return FALSE;
    }

    Buffer[Size / sizeof(WCHAR)] = UNICODE_NULL;
    return TRUE;
}

static DWORD
CountDevicesUsingInf(
    _In_ PCWSTR PublishedName,
    _In_ BOOL Remove)
{
    SP_DEVINFO_DATA DeviceInfoData;
    HDEVINFO DeviceInfoSet;
    DWORD Count = 0;
    DWORD Index;

    DeviceInfoSet = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES | (Remove ? 0 : DIGCF_PRESENT));
    if (DeviceInfoSet == INVALID_HANDLE_VALUE)
        return 0;

    DeviceInfoData.cbSize = sizeof(DeviceInfoData);
    for (Index = 0; SetupDiEnumDeviceInfo(DeviceInfoSet, Index, &DeviceInfoData); Index++)
    {
        WCHAR InfPath[MAX_PATH];

        if (!GetDriverKeyString(DeviceInfoSet, &DeviceInfoData, REGSTR_VAL_INFPATH, InfPath, _countof(InfPath)) ||
            _wcsicmp(InfPath, PublishedName) != 0)
        {
            continue;
        }

        if (Remove && !SetupDiCallClassInstaller(DIF_REMOVE, DeviceInfoSet, &DeviceInfoData))
            continue;

        Count++;
    }

    SetupDiDestroyDeviceInfoList(DeviceInfoSet);
    return Count;
}

static VOID
ScanDevices(VOID)
{
    DEVINST Root;

    if (CM_Locate_DevNodeW(&Root, NULL, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS)
        CM_Reenumerate_DevNode(Root, CM_REENUMERATE_SYNCHRONOUS);
}

static VOID
AddDriverPackage(
    _In_ PCWSTR InfPath,
    _Inout_ PADD_CONTEXT Context)
{
    WCHAR Published[MAX_PATH];
    PWSTR Component = NULL;
    PCWSTR Name = wcsrchr(InfPath, L'\\');
    BOOL NeedReboot = FALSE;

    Name = Name ? Name + 1 : InfPath;
    Context->Total++;
    ConPrintf(StdOut, L"Adding driver package:  %ls\n", Name);

    if (!SetupCopyOEMInfW(InfPath, NULL, SPOST_NONE, 0, Published, _countof(Published), NULL, &Component))
    {
        Context->Error = GetLastError();
        PrintError(L"Failed to add driver package", Context->Error);
        ConPuts(StdOut, L"\n");
        return;
    }

    if (!Component)
        Component = Published;

    Context->Added++;
    ConPuts(StdOut, L"Driver package added successfully.\n");
    ConPrintf(StdOut, L"Published Name:         %ls\n", Component);

    if (Context->Install)
    {
        if (!DiInstallDriverW(NULL, InfPath, 0, &NeedReboot))
        {
            Context->Error = GetLastError();
            PrintError(L"Failed to install driver package", Context->Error);
        }
        else if (CountDevicesUsingInf(Component, FALSE) != 0)
        {
            ConPuts(StdOut, L"Driver package installed on matching devices.\n");
        }
        else
        {
            ConPuts(StdOut, L"No matching devices are present.\n");
        }

        if (NeedReboot)
            Context->NeedReboot = TRUE;
    }

    ConPuts(StdOut, L"\n");
}

static VOID
AddDriverPackages(
    _In_ PCWSTR Specification,
    _Inout_ PADD_CONTEXT Context)
{
    WCHAR Pattern[MAX_PATH], Directory[MAX_PATH], Path[MAX_PATH], Name[MAX_PATH];
    WIN32_FIND_DATAW FindData;
    PWSTR FilePart = NULL;
    HANDLE hFind;
    DWORD Length;
    BOOL Found = FALSE;

    Length = GetFullPathNameW(Specification, _countof(Pattern), Pattern, &FilePart);
    if (Length == 0 || Length >= _countof(Pattern) || !FilePart)
    {
        Context->Total++;
        Context->Error = ERROR_FILE_NOT_FOUND;
        PrintError(L"Failed to add driver package", Context->Error);
        return;
    }

    wcscpy(Name, FilePart);
    wcsncpy(Directory, Pattern, FilePart - Pattern);
    Directory[FilePart - Pattern] = UNICODE_NULL;

    hFind = FindFirstFileW(Pattern, &FindData);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            if (wcslen(Directory) + wcslen(FindData.cFileName) >= _countof(Path))
                continue;

            wcscpy(Path, Directory);
            wcscat(Path, FindData.cFileName);
            Found = TRUE;
            AddDriverPackage(Path, Context);
        } while (FindNextFileW(hFind, &FindData));
        FindClose(hFind);
    }

    if (Context->SubDirs)
    {
        if (wcslen(Directory) + 1 >= _countof(Path))
            return;

        wcscpy(Path, Directory);
        wcscat(Path, L"*");
        hFind = FindFirstFileW(Path, &FindData);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!(FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    !wcscmp(FindData.cFileName, L".") ||
                    !wcscmp(FindData.cFileName, L".."))
                {
                    continue;
                }
                if (wcslen(Directory) + wcslen(FindData.cFileName) + 1 + wcslen(Name) >= _countof(Path))
                    continue;

                wcscpy(Path, Directory);
                wcscat(Path, FindData.cFileName);
                wcscat(Path, L"\\");
                wcscat(Path, Name);
                AddDriverPackages(Path, Context);
            } while (FindNextFileW(hFind, &FindData));
            FindClose(hFind);
        }
    }
    else if (!Found)
    {
        Context->Total++;
        Context->Error = ERROR_FILE_NOT_FOUND;
        ConPrintf(StdOut, L"Adding driver package:  %ls\n", Name);
        PrintError(L"Failed to add driver package", Context->Error);
        ConPuts(StdOut, L"\n");
    }
}

static INT
CommandAddDriver(
    _In_ INT argc,
    _In_ WCHAR **argv,
    _In_ BOOL LegacyInstall)
{
    ADD_CONTEXT Context = {0};
    PCWSTR Specification = NULL;
    BOOL Reboot = FALSE;
    INT i;

    Context.Install = LegacyInstall;
    for (i = 0; i < argc; i++)
    {
        if (IsOption(argv[i], L"subdirs"))
            Context.SubDirs = TRUE;
        else if (IsOption(argv[i], L"install") || IsOption(argv[i], L"i"))
            Context.Install = TRUE;
        else if (IsOption(argv[i], L"reboot"))
            Reboot = TRUE;
        else if (!Specification)
            Specification = argv[i];
        else
            return ERROR_INVALID_PARAMETER;
    }

    if (!Specification)
        return ERROR_INVALID_PARAMETER;

    AddDriverPackages(Specification, &Context);

    ConPrintf(StdOut, L"Total driver packages:  %lu\n", Context.Total);
    ConPrintf(StdOut, L"Added driver packages:  %lu\n", Context.Added);

    return FinishOperation(Context.Error, Context.NeedReboot, Reboot);
}

static INT
CommandDeleteDriver(
    _In_ INT argc,
    _In_ WCHAR **argv,
    _In_ BOOL LegacyForce)
{
    PCWSTR Name = NULL;
    BOOL Uninstall = FALSE;
    BOOL Force = LegacyForce;
    BOOL Reboot = FALSE;
    DWORD Error = ERROR_SUCCESS;
    INT i;

    for (i = 0; i < argc; i++)
    {
        if (IsOption(argv[i], L"uninstall"))
            Uninstall = TRUE;
        else if (IsOption(argv[i], L"force") || IsOption(argv[i], L"f"))
            Force = TRUE;
        else if (IsOption(argv[i], L"reboot"))
            Reboot = TRUE;
        else if (!Name)
            Name = argv[i];
        else
            return ERROR_INVALID_PARAMETER;
    }

    if (!Name)
        return ERROR_INVALID_PARAMETER;

    if (Uninstall)
        CountDevicesUsingInf(Name, TRUE);

    if (SetupUninstallOEMInfW(Name, Force ? SUOI_FORCEDELETE : 0, NULL))
    {
        ConPuts(StdOut, L"Driver package deleted successfully.\n");
    }
    else
    {
        Error = GetLastError();
        if (Error == ERROR_INF_IN_USE_BY_DEVICES)
            ConPuts(StdOut, L"Failed to delete driver package: One or more devices are presently installed using the specified INF.\n");
        else if (Error == ERROR_NOT_AN_INSTALLED_OEM_INF)
            ConPuts(StdOut, L"Failed to delete driver package: The specified file is not an installed OEM INF.\n");
        else
            PrintError(L"Failed to delete driver package", Error);
    }

    if (Uninstall)
        ScanDevices();

    return FinishOperation(Error, FALSE, Reboot);
}

static VOID
GetInfVersionValue(
    _In_ HINF hInf,
    _In_ PCWSTR Key,
    _Out_writes_(BufferSize) PWSTR Buffer,
    _In_ DWORD BufferSize)
{
    if (!SetupGetLineTextW(NULL, hInf, L"Version", Key, Buffer, BufferSize, NULL))
        Buffer[0] = UNICODE_NULL;
}

static INT
CommandEnumDrivers(VOID)
{
    WCHAR Pattern[MAX_PATH], Path[MAX_PATH], Store[MAX_PATH], Value[MAX_PATH], Description[LINE_LEN];
    WIN32_FIND_DATAW FindData;
    HANDLE hFind;
    UINT Length;
    DWORD Count = 0;

    Length = GetSystemWindowsDirectoryW(Pattern, _countof(Pattern));
    if (Length == 0 || Length + 16 >= _countof(Pattern))
        return ERROR_GEN_FAILURE;
    wcscat(Pattern, L"\\inf\\");
    Length = (UINT)wcslen(Pattern);
    wcscat(Pattern, L"oem*.inf");

    hFind = FindFirstFileW(Pattern, &FindData);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            PCWSTR Original = FindData.cFileName;
            GUID ClassGuid;
            HINF hInf;

            if (Length + wcslen(FindData.cFileName) >= _countof(Path))
                continue;

            wcsncpy(Path, Pattern, Length);
            Path[Length] = UNICODE_NULL;
            wcscat(Path, FindData.cFileName);

            hInf = SetupOpenInfFileW(Path, NULL, INF_STYLE_WIN4, NULL);
            if (hInf == INVALID_HANDLE_VALUE)
                continue;

            if (SetupGetInfDriverStoreLocationW(Path, NULL, NULL, Store, _countof(Store), NULL) &&
                wcsrchr(Store, L'\\'))
            {
                Original = wcsrchr(Store, L'\\') + 1;
            }

            ConPrintf(StdOut, L"Published Name:     %ls\n", FindData.cFileName);
            ConPrintf(StdOut, L"Original Name:      %ls\n", Original);

            GetInfVersionValue(hInf, L"Provider", Value, _countof(Value));
            ConPrintf(StdOut, L"Provider Name:      %ls\n", Value);

            GetInfVersionValue(hInf, L"ClassGUID", Value, _countof(Value));
            Description[0] = UNICODE_NULL;
            if (Value[0] == L'{' && wcslen(Value) == 38)
            {
                WCHAR Guid[40];

                wcscpy(Guid, Value);
                if (CLSIDFromString(Guid, &ClassGuid) == S_OK)
                    SetupDiGetClassDescriptionW(&ClassGuid, Description, _countof(Description), NULL);
            }
            if (!Description[0])
                GetInfVersionValue(hInf, L"Class", Description, _countof(Description));
            ConPrintf(StdOut, L"Class Name:         %ls\n", Description);
            ConPrintf(StdOut, L"Class GUID:         %ls\n", Value);

            GetInfVersionValue(hInf, L"DriverVer", Value, _countof(Value));
            if (wcschr(Value, L','))
                *wcschr(Value, L',') = L' ';
            ConPrintf(StdOut, L"Driver Version:     %ls\n\n", Value);

            SetupCloseInfFile(hInf);
            Count++;
        } while (FindNextFileW(hFind, &FindData));
        FindClose(hFind);
    }

    if (Count == 0)
        ConPuts(StdOut, L"No published driver packages were found on the system.\n");

    return ERROR_SUCCESS;
}

static VOID
PrintDeviceProperty(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ DWORD Property,
    _In_ PCWSTR Label)
{
    WCHAR Buffer[1024];
    DWORD Type;
    PCWSTR Value;

    if (!SetupDiGetDeviceRegistryPropertyW(DeviceInfoSet,
                                           DeviceInfoData,
                                           Property,
                                           &Type,
                                           (PBYTE)Buffer,
                                           sizeof(Buffer) - 2 * sizeof(WCHAR),
                                           NULL))
    {
        return;
    }

    if (Type == REG_SZ)
    {
        ConPrintf(StdOut, L"%-28ls%ls\n", Label, Buffer);
    }
    else if (Type == REG_MULTI_SZ)
    {
        for (Value = Buffer; *Value; Value += wcslen(Value) + 1)
        {
            ConPrintf(StdOut, L"%-28ls%ls\n", Label, Value);
            Label = L"";
        }
    }
}

static BOOL
MatchesClass(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ PCWSTR Class)
{
    WCHAR Buffer[MAX_PATH];

    if (SetupDiGetDeviceRegistryPropertyW(DeviceInfoSet, DeviceInfoData, SPDRP_CLASS, NULL,
                                          (PBYTE)Buffer, sizeof(Buffer), NULL) &&
        _wcsicmp(Buffer, Class) == 0)
    {
        return TRUE;
    }

    return SetupDiGetDeviceRegistryPropertyW(DeviceInfoSet, DeviceInfoData, SPDRP_CLASSGUID, NULL,
                                             (PBYTE)Buffer, sizeof(Buffer), NULL) &&
           _wcsicmp(Buffer, Class) == 0;
}

static INT
CommandEnumDevices(
    _In_ INT argc,
    _In_ WCHAR **argv)
{
    ENUM_FILTER Filter = {0};
    SP_DEVINFO_DATA DeviceInfoData;
    HDEVINFO DeviceInfoSet;
    DWORD Index, Count = 0;
    INT i;

    for (i = 0; i < argc; i++)
    {
        if (IsOption(argv[i], L"connected"))
        {
            Filter.Connected = TRUE;
        }
        else if (IsOption(argv[i], L"disconnected"))
        {
            Filter.Disconnected = TRUE;
        }
        else if (IsOption(argv[i], L"ids"))
        {
            Filter.Ids = TRUE;
        }
        else if (IsOption(argv[i], L"instanceid") && i + 1 < argc)
        {
            Filter.InstanceId = argv[++i];
        }
        else if (IsOption(argv[i], L"class") && i + 1 < argc)
        {
            Filter.Class = argv[++i];
        }
        else if (IsOption(argv[i], L"problem"))
        {
            Filter.Problem = TRUE;
            if (i + 1 < argc && argv[i + 1][0] != L'/' && argv[i + 1][0] != L'-')
            {
                Filter.HasProblemCode = TRUE;
                Filter.ProblemCode = wcstoul(argv[++i], NULL, 0);
            }
        }
        else
        {
            return ERROR_INVALID_PARAMETER;
        }
    }

    DeviceInfoSet = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES);
    if (DeviceInfoSet == INVALID_HANDLE_VALUE)
        return (INT)GetLastError();

    DeviceInfoData.cbSize = sizeof(DeviceInfoData);
    for (Index = 0; SetupDiEnumDeviceInfo(DeviceInfoSet, Index, &DeviceInfoData); Index++)
    {
        WCHAR InstanceId[MAX_DEVICE_ID_LEN + 1], InfPath[MAX_PATH];
        ULONG Status = 0, Problem = 0;
        BOOL Present;
        PCWSTR State;
        UINT n;

        if (!SetupDiGetDeviceInstanceIdW(DeviceInfoSet, &DeviceInfoData, InstanceId, _countof(InstanceId), NULL))
            continue;

        Present = CM_Get_DevNode_Status(&Status, &Problem, DeviceInfoData.DevInst, 0) == CR_SUCCESS;

        if ((Filter.Connected && !Present) ||
            (Filter.Disconnected && Present) ||
            (Filter.InstanceId && _wcsicmp(Filter.InstanceId, InstanceId) != 0) ||
            (Filter.Class && !MatchesClass(DeviceInfoSet, &DeviceInfoData, Filter.Class)) ||
            (Filter.Problem && (!Present || !(Status & DN_HAS_PROBLEM))) ||
            (Filter.HasProblemCode && Problem != Filter.ProblemCode))
        {
            continue;
        }

        if (!Present)
            State = L"Disconnected";
        else if (Status & DN_HAS_PROBLEM)
            State = Problem == CM_PROB_DISABLED ? L"Disabled" : L"Problem";
        else if (Status & DN_STARTED)
            State = L"Started";
        else
            State = L"Stopped";

        ConPrintf(StdOut, L"%-28ls%ls\n", L"Instance ID:", InstanceId);
        PrintDeviceProperty(DeviceInfoSet, &DeviceInfoData, SPDRP_DEVICEDESC, L"Device Description:");
        PrintDeviceProperty(DeviceInfoSet, &DeviceInfoData, SPDRP_CLASS, L"Class Name:");
        PrintDeviceProperty(DeviceInfoSet, &DeviceInfoData, SPDRP_CLASSGUID, L"Class GUID:");
        PrintDeviceProperty(DeviceInfoSet, &DeviceInfoData, SPDRP_MFG, L"Manufacturer Name:");
        ConPrintf(StdOut, L"%-28ls%ls\n", L"Status:", State);

        if (Present && (Status & DN_HAS_PROBLEM))
        {
            PCWSTR Name = L"";

            for (n = 0; n < _countof(ProblemNames); n++)
            {
                if (ProblemNames[n].Code == Problem)
                    Name = ProblemNames[n].Name;
            }
            ConPrintf(StdOut, L"%-28ls%lu (0x%02lX) [%ls]\n", L"Problem Code:", Problem, Problem, Name);
        }

        if (GetDriverKeyString(DeviceInfoSet, &DeviceInfoData, REGSTR_VAL_INFPATH, InfPath, _countof(InfPath)))
            ConPrintf(StdOut, L"%-28ls%ls\n", L"Driver Name:", InfPath);

        if (Filter.Ids)
        {
            PrintDeviceProperty(DeviceInfoSet, &DeviceInfoData, SPDRP_HARDWAREID, L"Hardware IDs:");
            PrintDeviceProperty(DeviceInfoSet, &DeviceInfoData, SPDRP_COMPATIBLEIDS, L"Compatible IDs:");
        }

        ConPuts(StdOut, L"\n");
        Count++;
    }

    SetupDiDestroyDeviceInfoList(DeviceInfoSet);

    if (Count == 0)
        ConPuts(StdOut, L"No devices were found on the system.\n");

    return ERROR_SUCCESS;
}

static INT
CommandDevice(
    _In_ DI_FUNCTION Function,
    _In_ DWORD StateChange,
    _In_ PCWSTR Progress,
    _In_ PCWSTR Success,
    _In_ PCWSTR Failure,
    _In_ INT argc,
    _In_ WCHAR **argv)
{
    SP_DEVINFO_DATA DeviceInfoData;
    SP_DEVINSTALL_PARAMS_W InstallParams;
    HDEVINFO DeviceInfoSet;
    PCWSTR InstanceId = NULL;
    BOOL Reboot = FALSE;
    BOOL NeedReboot = FALSE;
    DWORD Error = ERROR_SUCCESS;
    BOOL Result;
    INT i;

    for (i = 0; i < argc; i++)
    {
        if (IsOption(argv[i], L"reboot"))
            Reboot = TRUE;
        else if (IsOption(argv[i], L"subtree"))
            continue;
        else if (!InstanceId)
            InstanceId = argv[i];
        else
            return ERROR_INVALID_PARAMETER;
    }

    if (!InstanceId)
        return ERROR_INVALID_PARAMETER;

    ConPrintf(StdOut, L"%-22ls%ls\n", Progress, InstanceId);

    DeviceInfoSet = SetupDiCreateDeviceInfoList(NULL, NULL);
    if (DeviceInfoSet == INVALID_HANDLE_VALUE)
        return (INT)GetLastError();

    DeviceInfoData.cbSize = sizeof(DeviceInfoData);
    Result = SetupDiOpenDeviceInfoW(DeviceInfoSet, InstanceId, NULL, 0, &DeviceInfoData);
    if (Result && Function == DIF_PROPERTYCHANGE)
    {
        SP_PROPCHANGE_PARAMS PropChange;

        PropChange.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
        PropChange.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
        PropChange.StateChange = StateChange;
        PropChange.Scope = StateChange == DICS_PROPCHANGE ? DICS_FLAG_CONFIGSPECIFIC : DICS_FLAG_GLOBAL;
        PropChange.HwProfile = 0;
        Result = SetupDiSetClassInstallParamsW(DeviceInfoSet,
                                               &DeviceInfoData,
                                               &PropChange.ClassInstallHeader,
                                               sizeof(PropChange));
    }
    if (Result)
        Result = SetupDiCallClassInstaller(Function, DeviceInfoSet, &DeviceInfoData);

    if (Result)
    {
        InstallParams.cbSize = sizeof(InstallParams);
        if (Function != DIF_REMOVE &&
            SetupDiGetDeviceInstallParamsW(DeviceInfoSet, &DeviceInfoData, &InstallParams))
        {
            NeedReboot = (InstallParams.Flags & (DI_NEEDRESTART | DI_NEEDREBOOT)) != 0;
        }
        if (!NeedReboot && Function == DIF_PROPERTYCHANGE && StateChange != DICS_DISABLE)
        {
            ULONG Status = 0, Problem = 0;

            if (CM_Get_DevNode_Status(&Status, &Problem, DeviceInfoData.DevInst, 0) != CR_SUCCESS ||
                !(Status & DN_STARTED))
            {
                Result = FALSE;
                SetLastError(ERROR_GEN_FAILURE);
            }
        }
    }

    if (!Result)
    {
        Error = GetLastError();
        PrintError(Failure, Error);
    }
    else if (!NeedReboot)
    {
        ConPrintf(StdOut, L"%ls\n", Success);
    }

    SetupDiDestroyDeviceInfoList(DeviceInfoSet);
    return FinishOperation(Error, NeedReboot, Reboot);
}

static INT
CommandScanDevices(VOID)
{
    ConPuts(StdOut, L"Scanning for device hardware changes.\n");
    ScanDevices();
    ConPuts(StdOut, L"Scan complete.\n");
    return ERROR_SUCCESS;
}

int
wmain(
    int argc,
    WCHAR **argv)
{
    PCWSTR Command;
    INT Result;

    ConInitStdStreams();
    ConPuts(StdOut, L"LiberNT PnP Utility\n\n");

    if (argc < 2 || IsOption(argv[1], L"?") || IsOption(argv[1], L"h") || IsOption(argv[1], L"help"))
    {
        ConPuts(StdOut, UsageText);
        return ERROR_SUCCESS;
    }

    Command = argv[1];
    argc -= 2;
    argv += 2;

    if (IsOption(Command, L"add-driver") || IsOption(Command, L"a"))
    {
        Result = CommandAddDriver(argc, argv, FALSE);
    }
    else if (IsOption(Command, L"i") && argc >= 1 && IsOption(argv[0], L"a"))
    {
        Result = CommandAddDriver(argc - 1, argv + 1, TRUE);
    }
    else if (IsOption(Command, L"delete-driver") || IsOption(Command, L"d"))
    {
        Result = CommandDeleteDriver(argc, argv, FALSE);
    }
    else if (IsOption(Command, L"f") && argc >= 1 && IsOption(argv[0], L"d"))
    {
        Result = CommandDeleteDriver(argc - 1, argv + 1, TRUE);
    }
    else if (IsOption(Command, L"enum-drivers") || IsOption(Command, L"e"))
    {
        Result = CommandEnumDrivers();
    }
    else if (IsOption(Command, L"enum-devices"))
    {
        Result = CommandEnumDevices(argc, argv);
    }
    else if (IsOption(Command, L"scan-devices"))
    {
        Result = CommandScanDevices();
    }
    else if (IsOption(Command, L"restart-device"))
    {
        Result = CommandDevice(DIF_PROPERTYCHANGE, DICS_PROPCHANGE,
                               L"Restarting device:", L"Device restarted successfully.",
                               L"Failed to restart device", argc, argv);
    }
    else if (IsOption(Command, L"disable-device"))
    {
        Result = CommandDevice(DIF_PROPERTYCHANGE, DICS_DISABLE,
                               L"Disabling device:", L"Device disabled successfully.",
                               L"Failed to disable device", argc, argv);
    }
    else if (IsOption(Command, L"enable-device"))
    {
        Result = CommandDevice(DIF_PROPERTYCHANGE, DICS_ENABLE,
                               L"Enabling device:", L"Device enabled successfully.",
                               L"Failed to enable device", argc, argv);
    }
    else if (IsOption(Command, L"remove-device"))
    {
        Result = CommandDevice(DIF_REMOVE, 0,
                               L"Removing device:", L"Device removed successfully.",
                               L"Failed to remove device", argc, argv);
    }
    else
    {
        Result = ERROR_INVALID_PARAMETER;
    }

    if (Result == ERROR_INVALID_PARAMETER)
    {
        ConPuts(StdOut, L"Invalid command line.\n\n");
        ConPuts(StdOut, UsageText);
    }

    return Result;
}
