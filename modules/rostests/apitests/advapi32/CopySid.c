/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests CopySid return values and last error
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(CopySid)
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    BYTE Buffer[SECURITY_MAX_SID_SIZE];
    PSID Sid = NULL;
    DWORD Length;
    BOOL Ret;

    Ret = AllocateAndInitializeSid(&NtAuthority, 1, SECURITY_INTERACTIVE_RID,
                                   0, 0, 0, 0, 0, 0, 0, &Sid);
    ok(Ret, "AllocateAndInitializeSid failed: %lu\n", GetLastError());
    if (!Ret)
        return;

    Length = GetLengthSid(Sid);

    ZeroMemory(Buffer, sizeof(Buffer));
    SetLastError(0xdeadbeef);
    Ret = CopySid(sizeof(Buffer), Buffer, Sid);
    ok(Ret == TRUE, "CopySid returned %d\n", Ret);
    ok(GetLastError() == 0xdeadbeef, "Last error is %lu\n", GetLastError());
    ok(EqualSid((PSID)Buffer, Sid), "Copied SID differs\n");

    ZeroMemory(Buffer, sizeof(Buffer));
    SetLastError(0xdeadbeef);
    Ret = CopySid(Length, Buffer, Sid);
    ok(Ret == TRUE, "CopySid with exact length returned %d\n", Ret);
    ok(GetLastError() == 0xdeadbeef, "Last error is %lu\n", GetLastError());
    ok(EqualSid((PSID)Buffer, Sid), "Copied SID differs\n");

    SetLastError(0xdeadbeef);
    Ret = CopySid(Length - 1, Buffer, Sid);
    ok(Ret == FALSE, "CopySid with short buffer returned %d\n", Ret);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Last error is %lu\n", GetLastError());

    FreeSid(Sid);
}
