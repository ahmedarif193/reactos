/*
 * PROJECT:     LiberNT Windows Error Reporting
 * FILE:        dll/win32/wer/wer.c
 * PURPOSE:     Windows Error Reporting client API
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <windef.h>
#include <winbase.h>
#include <werapi.h>

/* Reports are never collected, so the exclusion list has nothing to act on. */
HRESULT WINAPI WerAddExcludedApplication(PCWSTR ExeName, BOOL AllUsers)
{
    return (ExeName && *ExeName) ? S_OK : E_INVALIDARG;
}

HRESULT WINAPI WerRemoveExcludedApplication(PCWSTR ExeName, BOOL AllUsers)
{
    return (ExeName && *ExeName) ? S_OK : E_INVALIDARG;
}

HRESULT WINAPI WerReportCreate(PCWSTR EventType, WER_REPORT_TYPE Type,
                               PWER_REPORT_INFORMATION Information, HREPORT *Report)
{
    if (Report)
        *Report = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI WerReportCloseHandle(HREPORT Report)
{
    return Report ? E_NOTIMPL : E_INVALIDARG;
}

HRESULT WINAPI WerReportSetParameter(HREPORT Report, DWORD Id, PCWSTR Name, PCWSTR Value)
{
    return E_NOTIMPL;
}

HRESULT WINAPI WerReportSetUIOption(HREPORT Report, DWORD Type, PCWSTR Value)
{
    return E_NOTIMPL;
}

HRESULT WINAPI WerReportAddFile(HREPORT Report, PCWSTR Path, DWORD Type, DWORD Flags)
{
    return E_NOTIMPL;
}

HRESULT WINAPI WerReportAddDump(HREPORT Report, HANDLE Process, HANDLE Thread, DWORD Type,
                                PVOID ExceptionParam, PVOID DumpCustomOptions, DWORD Flags)
{
    return E_NOTIMPL;
}

HRESULT WINAPI WerReportSubmit(HREPORT Report, WER_CONSENT Consent, DWORD Flags,
                               PWER_SUBMIT_RESULT Result)
{
    if (Result)
        *Result = WerReportFailed;
    return E_NOTIMPL;
}
