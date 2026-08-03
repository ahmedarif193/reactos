/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     API definitions for api-ms-win-core-sysinfo-l1
 * COPYRIGHT:   Copyright 2024 Timo Kreuzer (timo.kreuzer@reactos.org)
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

WINBASEAPI
BOOL
WINAPI
GetProductInfo(
    _In_ DWORD dwOSMajorVersion,
    _In_ DWORD dwOSMinorVersion,
    _In_ DWORD dwSpMajorVersion,
    _In_ DWORD dwSpMinorVersion,
    _Out_ PDWORD pdwReturnedProductType);

WINBASEAPI
VOID
WINAPI
GetSystemTimePreciseAsFileTime(
    _Out_ LPFILETIME lpSystemTimeAsFileTime);

#ifdef __cplusplus
} // extern "C"
#endif
