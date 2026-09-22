/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests querying TokenBnoIsolation on a process token
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(TokenBnoIsolation)
{
    UCHAR Buffer[sizeof(TOKEN_BNO_ISOLATION_INFORMATION) + 64];
    PTOKEN_BNO_ISOLATION_INFORMATION Isolation = (PTOKEN_BNO_ISOLATION_INFORMATION)Buffer;
    HANDLE Token;
    DWORD Length;
    BOOL Ret;

    ok(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token), "OpenProcessToken failed: %lu\n", GetLastError());

    Length = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    Ret = GetTokenInformation(Token, TokenBnoIsolation, NULL, 0, &Length);
    ok(!Ret, "GetTokenInformation succeeded without a buffer\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Unexpected error %lu\n", GetLastError());
    ok(Length == sizeof(TOKEN_BNO_ISOLATION_INFORMATION), "Unexpected length %lu\n", Length);

    FillMemory(Buffer, sizeof(Buffer), 0xCC);
    Length = 0xdeadbeef;
    Ret = GetTokenInformation(Token, TokenBnoIsolation, Buffer, sizeof(Buffer), &Length);
    ok(Ret, "GetTokenInformation failed: %lu\n", GetLastError());
    ok(Length == sizeof(TOKEN_BNO_ISOLATION_INFORMATION), "Unexpected length %lu\n", Length);
    ok(Isolation->IsolationPrefix == NULL, "Unexpected prefix %p\n", Isolation->IsolationPrefix);
    ok(Isolation->IsolationEnabled == FALSE, "Isolation is enabled\n");

    CloseHandle(Token);
}
