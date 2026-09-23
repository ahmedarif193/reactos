/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests LookupAccountNameW with well-known account names
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <sddl.h>

typedef struct
{
    PCWSTR Name;
    PCWSTR Sid;
    PCWSTR Domain;
    SID_NAME_USE Use;
} ACCOUNT_TEST;

static const ACCOUNT_TEST Tests[] =
{
    { L"INTERACTIVE", L"S-1-5-4", L"NT AUTHORITY", SidTypeWellKnownGroup },
    { L"NT AUTHORITY\\INTERACTIVE", L"S-1-5-4", L"NT AUTHORITY", SidTypeWellKnownGroup },
    { L"SYSTEM", L"S-1-5-18", L"NT AUTHORITY", SidTypeWellKnownGroup },
    { L"NETWORK SERVICE", L"S-1-5-20", L"NT AUTHORITY", SidTypeWellKnownGroup },
    { L"Everyone", L"S-1-1-0", L"", SidTypeWellKnownGroup },
    { L"Administrators", L"S-1-5-32-544", L"BUILTIN", SidTypeAlias },
};

static void
TestAccount(const ACCOUNT_TEST *Test)
{
    BYTE SidBuffer[SECURITY_MAX_SID_SIZE];
    WCHAR Domain[256];
    DWORD cbSid, cchDomain;
    SID_NAME_USE Use;
    PWSTR SidString = NULL;
    BOOL Ret;

    cbSid = 0;
    cchDomain = 0;
    SetLastError(0xdeadbeef);
    Ret = LookupAccountNameW(NULL, Test->Name, NULL, &cbSid, NULL, &cchDomain, &Use);
    ok(!Ret, "%s: size query succeeded\n", wine_dbgstr_w(Test->Name));
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "%s: size query error %lu\n", wine_dbgstr_w(Test->Name), GetLastError());
    ok(cbSid > 0 && cbSid <= sizeof(SidBuffer), "%s: SID size %lu\n", wine_dbgstr_w(Test->Name), cbSid);
    ok(cchDomain == wcslen(Test->Domain) + 1, "%s: domain size %lu\n", wine_dbgstr_w(Test->Name), cchDomain);

    cbSid = sizeof(SidBuffer);
    cchDomain = ARRAYSIZE(Domain);
    SetLastError(0xdeadbeef);
    Ret = LookupAccountNameW(NULL, Test->Name, SidBuffer, &cbSid, Domain, &cchDomain, &Use);
    ok(Ret, "%s: lookup failed with error %lu\n", wine_dbgstr_w(Test->Name), GetLastError());
    if (!Ret)
        return;

    ok(ConvertSidToStringSidW((PSID)SidBuffer, &SidString), "ConvertSidToStringSidW failed: %lu\n", GetLastError());
    ok(SidString && !wcscmp(SidString, Test->Sid), "%s: SID is %s\n", wine_dbgstr_w(Test->Name), wine_dbgstr_w(SidString));
    ok(!wcscmp(Domain, Test->Domain), "%s: domain is %s\n", wine_dbgstr_w(Test->Name), wine_dbgstr_w(Domain));
    ok(cchDomain == wcslen(Test->Domain), "%s: returned domain length %lu\n", wine_dbgstr_w(Test->Name), cchDomain);
    ok(Use == Test->Use, "%s: use is %d\n", wine_dbgstr_w(Test->Name), Use);
    if (SidString)
        LocalFree(SidString);
}

START_TEST(LookupAccountName)
{
    ULONG i;

    for (i = 0; i < ARRAYSIZE(Tests); i++)
        TestAccount(&Tests[i]);
}
