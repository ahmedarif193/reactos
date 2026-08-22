/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Winlogon
 * FILE:            base/system/winlogon/environment.c
 * PURPOSE:         User environment routines
 * PROGRAMMERS:     Thomas Weidenmueller (w3seek@users.sourceforge.net)
 *                  Hervé Poussineau (hpoussin@reactos.org)
 *                  Eric Kohl
 */

/* INCLUDES *****************************************************************/

#include "winlogon.h"

/* FUNCTIONS ****************************************************************/

static
VOID
SetShellFolderEnvironmentVariable(
    IN HKEY hKeyVolatileEnv,
    IN HKEY hKeyShellFolders,
    IN LPCWSTR ShellFolderName,
    IN LPCWSTR EnvironmentName)
{
    WCHAR szPath[MAX_PATH + 1];
    DWORD dwType;
    DWORD dwSize = sizeof(szPath);
    LONG lError;

    lError = RegQueryValueExW(hKeyShellFolders, ShellFolderName, NULL, &dwType, (LPBYTE)szPath, &dwSize);
    if (lError != ERROR_SUCCESS || (dwType != REG_SZ && dwType != REG_EXPAND_SZ))
        return;

    TRACE("%S path: %S\n", EnvironmentName, szPath);
    RegSetValueExW(hKeyVolatileEnv, EnvironmentName, 0, REG_SZ, (LPBYTE)szPath, (wcslen(szPath) + 1) * sizeof(WCHAR));
}

static
VOID
BuildVolatileEnvironment(
    IN PWLSESSION Session,
    IN HKEY hKeyCurrentUser)
{
    LPCWSTR wstr;
    SIZE_T size;
    WCHAR szEnvKey[MAX_PATH];
    WCHAR szEnvValue[1024];
    SIZE_T length;
    LPWSTR eqptr, endptr;
    DWORD dwDisp;
    LONG lError;
    HKEY hKeyVolatileEnv;
    HKEY hKeyShellFolders;

    /* Create the 'Volatile Environment' key */
    lError = RegCreateKeyExW(hKeyCurrentUser,
                             L"Volatile Environment",
                             0,
                             NULL,
                             REG_OPTION_VOLATILE,
                             KEY_WRITE,
                             NULL,
                             &hKeyVolatileEnv,
                             &dwDisp);
    if (lError != ERROR_SUCCESS)
    {
        WARN("WL: RegCreateKeyExW() failed to create the volatile environment key (Error: %ld)\n", lError);
        return;
    }

    /* Parse the environment variables and add them to the volatile environment key */
    if (Session->Profile->dwType == WLX_PROFILE_TYPE_V2_0 &&
        Session->Profile->pszEnvironment != NULL)
    {
        wstr = Session->Profile->pszEnvironment;
        while (*wstr != UNICODE_NULL)
        {
            size = wcslen(wstr) + 1;

            eqptr = wcschr(wstr, L'=');

            if (eqptr != NULL)
            {
                endptr = eqptr;

                endptr--;
                while (iswspace(*endptr))
                    endptr--;

                length = (SIZE_T)(endptr - wstr + 1);

                wcsncpy(szEnvKey, wstr, length);
                szEnvKey[length] = 0;

                eqptr++;
                while (iswspace(*eqptr))
                    eqptr++;
                wcscpy(szEnvValue, eqptr);

                RegSetValueExW(hKeyVolatileEnv,
                               szEnvKey,
                               0,
                               REG_SZ,
                               (LPBYTE)szEnvValue,
                               (wcslen(szEnvValue) + 1) * sizeof(WCHAR));
            }

            wstr += size;
        }
    }

    /* Set the per-user application data environment variables */
    lError = RegOpenKeyExW(hKeyCurrentUser, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders", 0, KEY_READ, &hKeyShellFolders);
    if (lError == ERROR_SUCCESS)
    {
        SetShellFolderEnvironmentVariable(hKeyVolatileEnv, hKeyShellFolders, L"AppData", L"APPDATA");
        SetShellFolderEnvironmentVariable(hKeyVolatileEnv, hKeyShellFolders, L"Local AppData", L"LOCALAPPDATA");
        RegCloseKey(hKeyShellFolders);
    }

    RegCloseKey(hKeyVolatileEnv);
}


BOOL
CreateUserEnvironment(
    IN PWLSESSION Session)
{
    HKEY hKeyCurrentUser;
    LONG lError;

    TRACE("WL: CreateUserEnvironment called\n");

    /* Impersonate the new user */
    if (!ImpersonateLoggedOnUser(Session->UserToken))
    {
        ERR("ImpersonateLoggedOnUser() failed with error %lu\n", GetLastError());
        return FALSE;
    }

    /* Open the new user HKCU key */
    lError = RegOpenCurrentUser(KEY_CREATE_SUB_KEY,
                                &hKeyCurrentUser);
    if (lError == ERROR_SUCCESS)
    {
        BuildVolatileEnvironment(Session,
                                 hKeyCurrentUser);
        RegCloseKey(hKeyCurrentUser);
    }

    /* Revert the impersonation */
    RevertToSelf();

    TRACE("WL: CreateUserEnvironment done\n");

    return TRUE;
}
