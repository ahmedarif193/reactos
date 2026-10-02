/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS system libraries
 * FILE:            dll/win32/kernel32/client/version.c
 * PURPOSE:         Version functions
 * PROGRAMMER:      Ariadne (ariadne@xs4all.nl)
                    Ged Murphy (gedmurphy@reactos.org)
 */

#include <k32.h>

#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

static UCHAR
BasepVersionUpdateCondition(PUCHAR LastCondition, UCHAR Condition)
{
    switch (*LastCondition)
    {
        case 0:
            *LastCondition = Condition;
            break;

        case VER_EQUAL:
            if (Condition >= VER_EQUAL && Condition <= VER_LESS_EQUAL)
            {
                *LastCondition = Condition;
                return Condition;
            }
            break;

        case VER_GREATER:
        case VER_GREATER_EQUAL:
            if (Condition >= VER_EQUAL && Condition <= VER_GREATER_EQUAL)
                return Condition;
            break;

        case VER_LESS:
        case VER_LESS_EQUAL:
            if (Condition == VER_EQUAL || (Condition >= VER_LESS && Condition <= VER_LESS_EQUAL))
                return Condition;
            break;
    }

    if (!Condition) *LastCondition |= 0x10;
    return *LastCondition & 0xf;
}

static BOOL
BasepVersionCompareValues(ULONG Left, ULONG Right, UCHAR Condition)
{
    switch (Condition)
    {
        case VER_EQUAL:
            return Left == Right;

        case VER_GREATER:
            return Left > Right;

        case VER_GREATER_EQUAL:
            return Left >= Right;

        case VER_LESS:
            return Left < Right;

        case VER_LESS_EQUAL:
            return Left <= Right;

        default:
            return FALSE;
    }
}

/*
 * @implemented
 */
BOOL
WINAPI
VerifyVersionInfoW(IN LPOSVERSIONINFOEXW lpVersionInformation,
                   IN DWORD dwTypeMask,
                   IN DWORDLONG dwlConditionMask)
{
    OSVERSIONINFOEXW Version;
    UCHAR Condition, LastCondition = 0;
    BOOL Succeeded = TRUE, DoNextCheck = TRUE;

    Version.dwOSVersionInfoSize = sizeof(Version);
    if (!GetVersionExW((LPOSVERSIONINFOW)&Version)) return FALSE;

    if (!dwTypeMask || !dwlConditionMask)
    {
        SetLastError(ERROR_BAD_ARGUMENTS);
        return FALSE;
    }

    if (dwTypeMask & VER_PRODUCT_TYPE)
    {
        if (!BasepVersionCompareValues(Version.wProductType,
                                       lpVersionInformation->wProductType,
                                       dwlConditionMask >> 7 * 3 & 0x07))
        {
            goto Mismatch;
        }
    }

    if (dwTypeMask & VER_SUITENAME)
    {
        switch (dwlConditionMask >> 6 * 3 & 0x07)
        {
            case VER_AND:
                if ((lpVersionInformation->wSuiteMask & Version.wSuiteMask) != lpVersionInformation->wSuiteMask)
                    goto Mismatch;
                break;

            case VER_OR:
                if (!(lpVersionInformation->wSuiteMask & Version.wSuiteMask) && lpVersionInformation->wSuiteMask)
                    goto Mismatch;
                break;

            default:
                SetLastError(ERROR_BAD_ARGUMENTS);
                return FALSE;
        }
    }

    if (dwTypeMask & VER_PLATFORMID)
    {
        if (!BasepVersionCompareValues(Version.dwPlatformId,
                                       lpVersionInformation->dwPlatformId,
                                       dwlConditionMask >> 3 * 3 & 0x07))
        {
            goto Mismatch;
        }
    }

    if (dwTypeMask & VER_BUILDNUMBER)
    {
        if (!BasepVersionCompareValues(Version.dwBuildNumber,
                                       lpVersionInformation->dwBuildNumber,
                                       dwlConditionMask >> 2 * 3 & 0x07))
        {
            goto Mismatch;
        }
    }

    if (dwTypeMask & VER_MAJORVERSION)
    {
        Condition = BasepVersionUpdateCondition(&LastCondition, dwlConditionMask >> 1 * 3 & 0x07);
        Succeeded = BasepVersionCompareValues(Version.dwMajorVersion,
                                              lpVersionInformation->dwMajorVersion,
                                              Condition);
        DoNextCheck = (Version.dwMajorVersion == lpVersionInformation->dwMajorVersion) &&
                      (Condition >= VER_EQUAL && Condition <= VER_LESS_EQUAL);
    }

    if ((dwTypeMask & VER_MINORVERSION) && DoNextCheck)
    {
        Condition = BasepVersionUpdateCondition(&LastCondition, dwlConditionMask >> 0 * 3 & 0x07);
        Succeeded = BasepVersionCompareValues(Version.dwMinorVersion,
                                              lpVersionInformation->dwMinorVersion,
                                              Condition);
        DoNextCheck = (Version.dwMinorVersion == lpVersionInformation->dwMinorVersion) &&
                      (Condition >= VER_EQUAL && Condition <= VER_LESS_EQUAL);
    }

    if ((dwTypeMask & VER_SERVICEPACKMAJOR) && DoNextCheck)
    {
        Condition = BasepVersionUpdateCondition(&LastCondition, dwlConditionMask >> 5 * 3 & 0x07);
        Succeeded = BasepVersionCompareValues(Version.wServicePackMajor,
                                              lpVersionInformation->wServicePackMajor,
                                              Condition);
        DoNextCheck = (Version.wServicePackMajor == lpVersionInformation->wServicePackMajor) &&
                      (Condition >= VER_EQUAL && Condition <= VER_LESS_EQUAL);
    }

    if ((dwTypeMask & VER_SERVICEPACKMINOR) && DoNextCheck)
    {
        Condition = BasepVersionUpdateCondition(&LastCondition, dwlConditionMask >> 4 * 3 & 0x07);
        Succeeded = BasepVersionCompareValues(Version.wServicePackMinor,
                                              lpVersionInformation->wServicePackMinor,
                                              Condition);
    }

    if (Succeeded) return TRUE;

Mismatch:
    SetLastError(ERROR_OLD_WIN_VERSION);
    return FALSE;
}
