/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS system libraries
 * FILE:            dll/win32/userenv/setup.c
 * PURPOSE:         Profile setup functions
 * PROGRAMMERS:     Eric Kohl
 *                  Hermes Belusca-Maito
 */

#include "precomp.h"

#define NDEBUG
#include <debug.h>

#include "resources.h"

typedef struct _FOLDERDATA
{
    LPWSTR lpValueName;
    LPWSTR lpPath;
    BOOL bHidden;
    BOOL bShellFolder;
    BOOL bUserShellFolder;
} FOLDERDATA, *PFOLDERDATA;


static FOLDERDATA
UserShellFolders[] =
{
    {L"AppData", L"AppData\\Roaming", TRUE, TRUE, TRUE},
    {L"Desktop", L"Desktop", FALSE, TRUE, TRUE},
    {L"Favorites", L"Favorites", FALSE, TRUE, TRUE},
    {L"Personal", L"Documents", FALSE, TRUE, TRUE},
    {L"My Music", L"Music", FALSE, TRUE, TRUE},
    {L"My Pictures", L"Pictures", FALSE, TRUE, TRUE},
    {L"My Video", L"Videos", FALSE, TRUE, TRUE},
    {L"NetHood", L"AppData\\Roaming\\Microsoft\\Windows\\Network Shortcuts", TRUE, TRUE, TRUE},
    {L"PrintHood", L"AppData\\Roaming\\Microsoft\\Windows\\Printer Shortcuts", TRUE, TRUE, TRUE},
    {L"Recent", L"AppData\\Roaming\\Microsoft\\Windows\\Recent", TRUE, TRUE, TRUE},
    {L"SendTo", L"AppData\\Roaming\\Microsoft\\Windows\\SendTo", FALSE, TRUE, TRUE},
    {L"Templates", L"AppData\\Roaming\\Microsoft\\Windows\\Templates", FALSE, TRUE, TRUE},
    {L"Start Menu", L"AppData\\Roaming\\Microsoft\\Windows\\Start Menu", FALSE, TRUE, TRUE},
    {L"Programs", L"AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs", FALSE, TRUE, TRUE},
    {L"Startup", L"AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\Startup", FALSE, TRUE, TRUE},
    {L"Local Settings", L"AppData\\Local", TRUE, TRUE, TRUE},
    {L"Local AppData", L"AppData\\Local", TRUE, TRUE, TRUE},
    {L"Temp", L"AppData\\Local\\Temp", FALSE, FALSE, FALSE},
    {L"Cache", L"AppData\\Local\\Microsoft\\Windows\\INetCache", FALSE, TRUE, TRUE},
    {L"History", L"AppData\\Local\\Microsoft\\Windows\\History", FALSE, TRUE, TRUE},
    {L"Cookies", L"AppData\\Local\\Microsoft\\Windows\\INetCookies", FALSE, TRUE, TRUE},
    {NULL, NULL, FALSE, FALSE, FALSE}
};


static FOLDERDATA
CommonShellFolders[] =
{
    {L"Common AppData", L"", TRUE, TRUE, TRUE},
    {L"Common Templates", L"Microsoft\\Windows\\Templates", TRUE, TRUE, TRUE},
    {L"Common Start Menu", L"Microsoft\\Windows\\Start Menu", FALSE, TRUE, TRUE},
    {L"Common Programs", L"Microsoft\\Windows\\Start Menu\\Programs", FALSE, TRUE, TRUE},
    {L"Common Startup", L"Microsoft\\Windows\\Start Menu\\Programs\\Startup", FALSE, TRUE, TRUE},
    {NULL, NULL, FALSE, FALSE, FALSE}
};

static FOLDERDATA
PublicShellFolders[] =
{
    {L"Common Desktop", L"Desktop", FALSE, TRUE, TRUE},
    {L"Common Documents", L"Documents", FALSE, TRUE, TRUE},
    {L"CommonMusic", L"Music", FALSE, TRUE, TRUE},
    {L"CommonPictures", L"Pictures", FALSE, TRUE, TRUE},
    {L"CommonVideo", L"Videos", FALSE, TRUE, TRUE},
    {NULL, NULL, FALSE, FALSE, FALSE}
};

typedef struct _PROFILEPARAMS
{
    LPCWSTR pszProfileName;
    LPCWSTR pszProfileRegValue;
    LPCWSTR pszEnvVar;
    LPCWSTR pszEnvVarProfilePath;
    PFOLDERDATA pFolderList;
    HKEY hRootKey;
    LPCWSTR pszShellFoldersKey;
    LPCWSTR pszUserShellFoldersKey;
} PROFILEPARAMS, *PPROFILEPARAMS;


static PROFILEPARAMS
StandardProfiles[] =
{
    {
        L"%SystemDrive%\\Users\\Default", L"Default",
        L"USERPROFILE", L"%USERPROFILE%",
        UserShellFolders,
        HKEY_USERS,
        L".Default\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders",
        L".Default\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders"
    },
    {
        L"%SystemDrive%\\ProgramData", L"ProgramData",
        L"ProgramData", L"%ProgramData%",
        CommonShellFolders,
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders",
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders"
    },
    {
        L"%SystemDrive%\\Users\\Public", L"Public",
        L"PUBLIC", L"%PUBLIC%",
        PublicShellFolders,
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders",
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders"
    },
};


static
BOOL
CreateStandardProfile(IN HKEY hProfileListKey,
                      IN PPROFILEPARAMS pProfileParams)
{
    LONG Error;
    PFOLDERDATA lpFolderData;
    HKEY hKey;
    DWORD dwLength;
    WCHAR szProfilePath[MAX_PATH];
    WCHAR szBuffer[MAX_PATH];

    /*
     * Create the standard profile main directory
     */

    StringCbCopyW(szBuffer, sizeof(szBuffer), pProfileParams->pszProfileName);

    if (!ExpandEnvironmentStringsW(szBuffer, szProfilePath, ARRAYSIZE(szProfilePath)))
        return FALSE;
    if (!CreateDirectoryPath(szProfilePath, NULL))
        return FALSE;

    /* Set 'DefaultUserProfile' / 'AllUsersProfile' value */
    /* Store the default user / all users profile path in the registry */
    dwLength = (wcslen(szBuffer) + 1) * sizeof(WCHAR);
    Error = RegSetValueExW(hProfileListKey,
                           pProfileParams->pszProfileRegValue,
                           0,
                           REG_EXPAND_SZ,
                           (LPBYTE)szBuffer,
                           dwLength);
    if (Error != ERROR_SUCCESS)
    {
        DPRINT1("Error: %lu\n", Error);
        SetLastError((DWORD)Error);
        return FALSE;
    }

    /* Set 'Default User' / 'All Users' profile */
    SetEnvironmentVariableW(pProfileParams->pszEnvVar, szProfilePath);


    /*
     * Create the standard profile sub-directories and associated registry keys
     */

    /* Create 'Default User' / 'All Users' subdirectories */
    /* FIXME: Take these paths from the registry */

    lpFolderData = pProfileParams->pFolderList;
    while (lpFolderData->lpValueName != NULL)
    {
        StringCbCopyW(szBuffer, sizeof(szBuffer), szProfilePath);
        if (*lpFolderData->lpPath)
            StringCbCatW(szBuffer, sizeof(szBuffer), L"\\");

        /* Append the folder name */
        StringCbCatW(szBuffer, sizeof(szBuffer), lpFolderData->lpPath);

        // FIXME: Security!
        if (!CreateDirectoryPath(szBuffer, NULL))
        {
            if (GetLastError() != ERROR_ALREADY_EXISTS)
            {
                DPRINT1("Error: %lu\n", GetLastError());
                return FALSE;
            }
        }

        if (lpFolderData->bHidden)
            SetFileAttributesW(szBuffer, FILE_ATTRIBUTE_HIDDEN);

        lpFolderData++;
    }

    /* Set 'Shell Folders' values */
    Error = RegOpenKeyExW(pProfileParams->hRootKey,
                          pProfileParams->pszShellFoldersKey,
                          0,
                          KEY_SET_VALUE,
                          &hKey);
    if (Error != ERROR_SUCCESS)
    {
        DPRINT1("Error: %lu\n", Error);
        SetLastError((DWORD)Error);
        return FALSE;
    }

    /*
     * NOTE: This is identical to UpdateUsersShellFolderSettings().
     */
    lpFolderData = pProfileParams->pFolderList;
    while (lpFolderData->lpValueName != NULL)
    {
        if (lpFolderData->bShellFolder)
        {
            StringCbCopyW(szBuffer, sizeof(szBuffer), szProfilePath);
            if (*lpFolderData->lpPath)
                StringCbCatW(szBuffer, sizeof(szBuffer), L"\\");

            /* Append the folder name */
            StringCbCatW(szBuffer, sizeof(szBuffer), lpFolderData->lpPath);

            dwLength = (wcslen(szBuffer) + 1) * sizeof(WCHAR);
            Error = RegSetValueExW(hKey,
                                   lpFolderData->lpValueName,
                                   0,
                                   REG_SZ,
                                   (LPBYTE)szBuffer,
                                   dwLength);
            if (Error != ERROR_SUCCESS)
            {
                DPRINT1("Error: %lu\n", Error);
                RegCloseKey(hKey);
                SetLastError((DWORD)Error);
                return FALSE;
            }
        }

        lpFolderData++;
    }

    RegCloseKey(hKey);

    /* Set 'User Shell Folders' values */
    Error = RegOpenKeyExW(pProfileParams->hRootKey,
                          pProfileParams->pszUserShellFoldersKey,
                          0,
                          KEY_SET_VALUE,
                          &hKey);
    if (Error != ERROR_SUCCESS)
    {
        DPRINT1("Error: %lu\n", Error);
        SetLastError((DWORD)Error);
        return FALSE;
    }

    lpFolderData = pProfileParams->pFolderList;
    while (lpFolderData->lpValueName != NULL)
    {
        if (lpFolderData->bUserShellFolder)
        {
            StringCbCopyW(szBuffer, sizeof(szBuffer), pProfileParams->pszEnvVarProfilePath);
            if (*lpFolderData->lpPath)
                StringCbCatW(szBuffer, sizeof(szBuffer), L"\\");

            /* Append the folder name */
            StringCbCatW(szBuffer, sizeof(szBuffer), lpFolderData->lpPath);

            dwLength = (wcslen(szBuffer) + 1) * sizeof(WCHAR);
            Error = RegSetValueExW(hKey,
                                   lpFolderData->lpValueName,
                                   0,
                                   REG_EXPAND_SZ,
                                   (LPBYTE)szBuffer,
                                   dwLength);
            if (Error != ERROR_SUCCESS)
            {
                DPRINT1("Error: %lu\n", Error);
                RegCloseKey(hKey);
                SetLastError((DWORD)Error);
                return FALSE;
            }
        }

        lpFolderData++;
    }

    RegCloseKey(hKey);

    return TRUE;
}


BOOL
WINAPI
InitializeProfiles(VOID)
{
    LONG Error;
    HKEY hKey;
    DWORD dwLength;
    WCHAR szProfilesPath[MAX_PATH];
    WCHAR szBuffer[MAX_PATH];

    DPRINT("InitializeProfiles()\n");

    /* Load profiles directory path */
    StringCbCopyW(szBuffer, sizeof(szBuffer), L"%SystemDrive%\\Users");

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList",
                          0,
                          KEY_SET_VALUE,
                          &hKey);
    if (Error != ERROR_SUCCESS)
    {
        DPRINT1("Error: %lu\n", Error);
        SetLastError((DWORD)Error);
        return FALSE;
    }

    /* Expand it */
    if (!ExpandEnvironmentStringsW(szBuffer,
                                   szProfilesPath,
                                   ARRAYSIZE(szProfilesPath)))
    {
        DPRINT1("Error: %lu\n", GetLastError());
        RegCloseKey(hKey);
        return FALSE;
    }

    /* Create profiles directory */
    // FIXME: Security!
    if (!CreateDirectoryW(szProfilesPath, NULL))
    {
        if (GetLastError() != ERROR_ALREADY_EXISTS)
        {
            DPRINT1("Error: %lu\n", GetLastError());
            RegCloseKey(hKey);
            return FALSE;
        }
    }

    /* Store the profiles directory path (unexpanded) in the registry */
    dwLength = (wcslen(szBuffer) + 1) * sizeof(WCHAR);
    Error = RegSetValueExW(hKey,
                           L"ProfilesDirectory",
                           0,
                           REG_EXPAND_SZ,
                           (LPBYTE)szBuffer,
                           dwLength);
    if (Error != ERROR_SUCCESS)
    {
        DPRINT1("Error: %lu\n", Error);
        RegCloseKey(hKey);
        SetLastError((DWORD)Error);
        return FALSE;
    }

    /* Create 'Default User' profile directory path */
    if (!CreateStandardProfile(hKey, &StandardProfiles[0]))
    {
        DPRINT1("CreateStandardProfile(L\"%S\") failed.\n", StandardProfiles[0].pszProfileName);
        RegCloseKey(hKey);
        return FALSE;
    }

    /* Create 'All Users' profile directory path */
    if (!CreateStandardProfile(hKey, &StandardProfiles[1]))
    {
        DPRINT1("CreateStandardProfile(L\"%S\") failed.\n", StandardProfiles[1].pszProfileName);
        RegCloseKey(hKey);
        return FALSE;
    }

    if (!CreateStandardProfile(hKey, &StandardProfiles[2]))
    {
        RegCloseKey(hKey);
        return FALSE;
    }

    RegCloseKey(hKey);

    DPRINT("Success\n");

    return TRUE;
}


/*
 * NOTE: See CreateStandardProfile() too.
 * Used by registry.c!CreateUserHive()
 */
BOOL
UpdateUsersShellFolderSettings(LPCWSTR lpUserProfilePath,
                               HKEY hUserKey)
{
    WCHAR szBuffer[MAX_PATH];
    DWORD dwLength;
    PFOLDERDATA lpFolderData;
    HKEY hFoldersKey;
    LONG Error;

    DPRINT("UpdateUsersShellFolderSettings() called\n");

    DPRINT("User profile path: %S\n", lpUserProfilePath);

    Error = RegOpenKeyExW(hUserKey,
                          L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders",
                          0,
                          KEY_SET_VALUE,
                          &hFoldersKey);
    if (Error != ERROR_SUCCESS)
    {
        DPRINT1("Error: %lu\n", Error);
        SetLastError((DWORD)Error);
        return FALSE;
    }

    lpFolderData = &UserShellFolders[0];
    while (lpFolderData->lpValueName != NULL)
    {
        if (lpFolderData->bShellFolder)
        {
            StringCbCopyW(szBuffer, sizeof(szBuffer), lpUserProfilePath);
            if (*lpFolderData->lpPath)
                StringCbCatW(szBuffer, sizeof(szBuffer), L"\\");

            /* Append the folder name */
            StringCbCatW(szBuffer, sizeof(szBuffer), lpFolderData->lpPath);

            DPRINT("%S: %S\n", lpFolderData->lpValueName, szBuffer);

            dwLength = (wcslen(szBuffer) + 1) * sizeof(WCHAR);
            Error = RegSetValueExW(hFoldersKey,
                                   lpFolderData->lpValueName,
                                   0,
                                   REG_SZ,
                                   (LPBYTE)szBuffer,
                                   dwLength);
            if (Error != ERROR_SUCCESS)
            {
                DPRINT1("Error: %lu\n", Error);
                RegCloseKey(hFoldersKey);
                SetLastError((DWORD)Error);
                return FALSE;
            }
        }

        lpFolderData++;
    }

    RegCloseKey(hFoldersKey);

    DPRINT("UpdateUsersShellFolderSettings() done\n");

    return TRUE;
}

/* EOF */
