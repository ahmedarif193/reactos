/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Win8+ process, timer and overlapped helpers
 * COPYRIGHT:   Adapted from corresponding Wine kernelbase implementations
 */

#include "k32_vista.h"

BOOL WINAPI QueryUnbiasedInterruptTime(_Out_ PULONGLONG UnbiasedTime);

BOOL
WINAPI
SetWaitableTimerEx(
    _In_ HANDLE hTimer,
    _In_ const LARGE_INTEGER *lpDueTime,
    _In_ LONG lPeriod,
    _In_opt_ PTIMERAPCROUTINE pfnCompletionRoutine,
    _In_opt_ LPVOID lpArgToCompletionRoutine,
    _In_opt_ PVOID WakeContext,
    _In_ ULONG TolerableDelay)
{
    UNREFERENCED_PARAMETER(WakeContext);
    UNREFERENCED_PARAMETER(TolerableDelay);
    return SetWaitableTimer(hTimer, lpDueTime, lPeriod, pfnCompletionRoutine, lpArgToCompletionRoutine, FALSE);
}

LONG
WINAPI
GetCurrentPackageFullName(
    _Inout_ UINT32 *packageFullNameLength,
    _Out_writes_opt_(*packageFullNameLength) PWSTR packageFullName)
{
    UNREFERENCED_PARAMETER(packageFullName);

    if (!packageFullNameLength)
        return ERROR_INVALID_PARAMETER;

    return APPMODEL_ERROR_NO_PACKAGE;
}

LONG
WINAPI
GetCurrentApplicationUserModelId(
    _Inout_ UINT32 *applicationUserModelIdLength,
    _Out_writes_opt_(*applicationUserModelIdLength) PWSTR applicationUserModelId)
{
    UNREFERENCED_PARAMETER(applicationUserModelId);

    if (!applicationUserModelIdLength)
        return ERROR_INVALID_PARAMETER;

    return APPMODEL_ERROR_NO_APPLICATION;
}

VOID
WINAPI
QueryUnbiasedInterruptTimePrecise(
    _Out_ PULONGLONG lpUnbiasedInterruptTimePrecise)
{
    (VOID)QueryUnbiasedInterruptTime(lpUnbiasedInterruptTimePrecise);
}

typedef struct _K32_COPYFILE2_EXTENDED_PARAMETERS
{
    DWORD dwSize;
    DWORD dwCopyFlags;
    BOOL *pfCancel;
    PVOID pProgressRoutine;
    PVOID pvCallbackContext;
} K32_COPYFILE2_EXTENDED_PARAMETERS;

typedef struct _K32_CREATEFILE2_EXTENDED_PARAMETERS
{
    DWORD dwSize;
    DWORD dwFileAttributes;
    DWORD dwFileFlags;
    DWORD dwSecurityQosFlags;
    LPSECURITY_ATTRIBUTES lpSecurityAttributes;
    HANDLE hTemplateFile;
} K32_CREATEFILE2_EXTENDED_PARAMETERS;

HRESULT
WINAPI
CopyFile2(
    _In_ PCWSTR pwszExistingFileName,
    _In_ PCWSTR pwszNewFileName,
    _In_opt_ K32_COPYFILE2_EXTENDED_PARAMETERS *pExtendedParameters)
{
    BOOL *Cancel = NULL;
    DWORD Flags = 0;

    if (pExtendedParameters)
    {
        if (pExtendedParameters->dwSize != sizeof(*pExtendedParameters))
            return E_INVALIDARG;
        if (pExtendedParameters->pProgressRoutine != NULL)
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);

        Flags = pExtendedParameters->dwCopyFlags;
        Cancel = pExtendedParameters->pfCancel;
    }

    if (!CopyFileExW(pwszExistingFileName, pwszNewFileName, NULL, NULL, Cancel, Flags))
        return HRESULT_FROM_WIN32(GetLastError());

    return S_OK;
}

HANDLE
WINAPI
CreateFile2(
    _In_ PCWSTR lpFileName,
    _In_ DWORD dwDesiredAccess,
    _In_ DWORD dwShareMode,
    _In_ DWORD dwCreationDisposition,
    _In_opt_ K32_CREATEFILE2_EXTENDED_PARAMETERS *pCreateExParams)
{
    DWORD Attributes = FILE_ATTRIBUTE_NORMAL;
    LPSECURITY_ATTRIBUTES Security = NULL;
    HANDLE Template = NULL;

    if (pCreateExParams)
    {
        if (pCreateExParams->dwSize != sizeof(*pCreateExParams))
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return INVALID_HANDLE_VALUE;
        }

        Attributes = pCreateExParams->dwFileAttributes | pCreateExParams->dwFileFlags | pCreateExParams->dwSecurityQosFlags;
        Security = pCreateExParams->lpSecurityAttributes;
        Template = pCreateExParams->hTemplateFile;
    }

    return CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, Security, dwCreationDisposition, Attributes, Template);
}
