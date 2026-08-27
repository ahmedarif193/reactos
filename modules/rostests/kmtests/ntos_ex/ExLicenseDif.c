/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel license and absent DIF provider parity tests
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>

NTSTATUS
NTAPI
ZwQueryLicenseValue(
    _In_ PCUNICODE_STRING ValueName,
    _Out_opt_ PULONG Type,
    _Out_writes_bytes_to_opt_(DataSize, *ResultDataSize) PVOID Data,
    _In_ ULONG DataSize,
    _Out_ PULONG ResultDataSize);

NTSTATUS
NTAPI
DifRegisterClassDriverPlugin(
    _In_ ULONG Version,
    _In_opt_ PVOID Plugin,
    _In_ ULONG Flags,
    _In_opt_ PDRIVER_OBJECT DriverObject);

START_TEST(ExLicenseDif)
{
    static const WCHAR ExpectedLicenseData[] = L"EMPTY";
    static const UNICODE_STRING LicenseName =
        RTL_CONSTANT_STRING(L"Kernel-MUI-Language-Allowed");
    static const UNICODE_STRING MissingLicenseName =
        RTL_CONSTANT_STRING(L"ReactOS-Missing-License-Value");
    WCHAR LicenseData[16];
    ULONG ResultDataSize;
    ULONG Type;
    NTSTATUS Status;

    Type = 0xA5A5A5A5;
    ResultDataSize = 0xA5A5A5A5;
    Status = ZwQueryLicenseValue(&LicenseName, &Type, LicenseData, 0,
                                 &ResultDataSize);
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok_eq_ulong(Type, REG_SZ);
    ok_eq_ulong(ResultDataSize, sizeof(ExpectedLicenseData));

    RtlFillMemory(LicenseData, sizeof(LicenseData), 0xA5);
    Status = ZwQueryLicenseValue(&LicenseName, &Type, LicenseData,
                                 sizeof(LicenseData), &ResultDataSize);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Type, REG_SZ);
    ok_eq_ulong(ResultDataSize, sizeof(ExpectedLicenseData));
    ok(RtlEqualMemory(LicenseData, ExpectedLicenseData,
                      sizeof(ExpectedLicenseData)),
       "unexpected MUI license data\n");

    Type = 0xA5A5A5A5;
    ResultDataSize = 0xA5A5A5A5;
    Status = ZwQueryLicenseValue(&MissingLicenseName, &Type, LicenseData,
                                 sizeof(LicenseData), &ResultDataSize);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    ok_eq_ulong(Type, 0xA5A5A5A5);
    ok_eq_ulong(ResultDataSize, 0xA5A5A5A5);

    Status = DifRegisterClassDriverPlugin(0, NULL, 0, KmtDriverObject);
    ok_eq_hex(Status, STATUS_DIF_DRIVER_PLUGIN_MISMATCH);
}
