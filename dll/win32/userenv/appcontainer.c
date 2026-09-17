/*
 * PROJECT:     ReactOS system libraries
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     AppContainer profile functions
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <sddl.h>

#define NDEBUG
#include <debug.h>

#define APPCONTAINER_MAPPINGS_KEY L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppContainer\\Mappings"

typedef struct _SHA256_CTX
{
    ULONG State[8];
    ULONGLONG BitCount;
    UCHAR Buffer[64];
    ULONG BufferLength;
} SHA256_CTX;

static const ULONG Sha256K[64] =
{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static VOID
Sha256Transform(SHA256_CTX *Ctx, const UCHAR *Block)
{
    ULONG W[64], a, b, c, d, e, f, g, h, T1, T2;
    ULONG i;

    for (i = 0; i < 16; i++)
        W[i] = ((ULONG)Block[i * 4] << 24) | ((ULONG)Block[i * 4 + 1] << 16) |
               ((ULONG)Block[i * 4 + 2] << 8) | (ULONG)Block[i * 4 + 3];
    for (; i < 64; i++)
    {
        ULONG s0 = ROTR(W[i - 15], 7) ^ ROTR(W[i - 15], 18) ^ (W[i - 15] >> 3);
        ULONG s1 = ROTR(W[i - 2], 17) ^ ROTR(W[i - 2], 19) ^ (W[i - 2] >> 10);
        W[i] = W[i - 16] + s0 + W[i - 7] + s1;
    }

    a = Ctx->State[0]; b = Ctx->State[1]; c = Ctx->State[2]; d = Ctx->State[3];
    e = Ctx->State[4]; f = Ctx->State[5]; g = Ctx->State[6]; h = Ctx->State[7];

    for (i = 0; i < 64; i++)
    {
        T1 = h + (ROTR(e, 6) ^ ROTR(e, 11) ^ ROTR(e, 25)) + ((e & f) ^ (~e & g)) + Sha256K[i] + W[i];
        T2 = (ROTR(a, 2) ^ ROTR(a, 13) ^ ROTR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + T1; d = c; c = b; b = a; a = T1 + T2;
    }

    Ctx->State[0] += a; Ctx->State[1] += b; Ctx->State[2] += c; Ctx->State[3] += d;
    Ctx->State[4] += e; Ctx->State[5] += f; Ctx->State[6] += g; Ctx->State[7] += h;
}

static VOID
Sha256Init(SHA256_CTX *Ctx)
{
    static const ULONG Init[8] =
    {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    RtlCopyMemory(Ctx->State, Init, sizeof(Init));
    Ctx->BitCount = 0;
    Ctx->BufferLength = 0;
}

static VOID
Sha256Update(SHA256_CTX *Ctx, const UCHAR *Data, ULONG Length)
{
    while (Length)
    {
        ULONG Take = min(Length, 64 - Ctx->BufferLength);
        RtlCopyMemory(Ctx->Buffer + Ctx->BufferLength, Data, Take);
        Ctx->BufferLength += Take;
        Ctx->BitCount += (ULONGLONG)Take * 8;
        Data += Take;
        Length -= Take;
        if (Ctx->BufferLength == 64)
        {
            Sha256Transform(Ctx, Ctx->Buffer);
            Ctx->BufferLength = 0;
        }
    }
}

static VOID
Sha256Final(SHA256_CTX *Ctx, UCHAR Digest[32])
{
    UCHAR Pad = 0x80;
    UCHAR Zero = 0;
    UCHAR LengthBytes[8];
    ULONGLONG Bits = Ctx->BitCount;
    ULONG i;

    Sha256Update(Ctx, &Pad, 1);
    while (Ctx->BufferLength != 56)
        Sha256Update(Ctx, &Zero, 1);
    for (i = 0; i < 8; i++)
        LengthBytes[i] = (UCHAR)(Bits >> (56 - i * 8));
    Sha256Update(Ctx, LengthBytes, 8);
    for (i = 0; i < 8; i++)
    {
        Digest[i * 4] = (UCHAR)(Ctx->State[i] >> 24);
        Digest[i * 4 + 1] = (UCHAR)(Ctx->State[i] >> 16);
        Digest[i * 4 + 2] = (UCHAR)(Ctx->State[i] >> 8);
        Digest[i * 4 + 3] = (UCHAR)(Ctx->State[i]);
    }
}

HRESULT
WINAPI
DeriveAppContainerSidFromAppContainerName(PCWSTR pszAppContainerName,
                                          PSID *ppsidAppContainerSid)
{
    SID_IDENTIFIER_AUTHORITY AppAuthority = {SECURITY_APP_PACKAGE_AUTHORITY};
    SHA256_CTX Ctx;
    UCHAR Digest[32];
    PWSTR Lower;
    SIZE_T Length, i;
    PISID Sid;
    ULONG Rids[SECURITY_APP_PACKAGE_RID_COUNT - 1];

    if (!pszAppContainerName || !ppsidAppContainerSid)
        return E_INVALIDARG;
    *ppsidAppContainerSid = NULL;

    Length = wcslen(pszAppContainerName);
    if (Length == 0 || Length > 64)
        return E_INVALIDARG;

    Lower = HeapAlloc(GetProcessHeap(), 0, (Length + 1) * sizeof(WCHAR));
    if (!Lower) return E_OUTOFMEMORY;
    for (i = 0; i <= Length; i++)
        Lower[i] = towlower(pszAppContainerName[i]);

    Sha256Init(&Ctx);
    Sha256Update(&Ctx, (const UCHAR *)Lower, (ULONG)(Length * sizeof(WCHAR)));
    Sha256Final(&Ctx, Digest);
    HeapFree(GetProcessHeap(), 0, Lower);

    for (i = 0; i < SECURITY_APP_PACKAGE_RID_COUNT - 1; i++)
    {
        Rids[i] = ((ULONG)Digest[i * 4]) |
                  ((ULONG)Digest[i * 4 + 1] << 8) |
                  ((ULONG)Digest[i * 4 + 2] << 16) |
                  ((ULONG)Digest[i * 4 + 3] << 24);
    }
    if (!AllocateAndInitializeSid(&AppAuthority, SECURITY_APP_PACKAGE_RID_COUNT,
                                  SECURITY_APP_PACKAGE_BASE_RID,
                                  Rids[0], Rids[1], Rids[2], Rids[3], Rids[4], Rids[5], Rids[6],
                                  (PSID *)&Sid))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    *ppsidAppContainerSid = Sid;
    return S_OK;
}

static HRESULT
OpenMappingKey(PSID Sid, BOOL Create, PHKEY Key, PBOOL Created)
{
    LPWSTR SidString = NULL;
    WCHAR Path[512];
    DWORD Disposition = 0;
    LONG Error;

    if (!ConvertSidToStringSidW(Sid, &SidString))
        return HRESULT_FROM_WIN32(GetLastError());

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%s", APPCONTAINER_MAPPINGS_KEY, SidString);
    LocalFree(SidString);

    if (Create)
        Error = RegCreateKeyExW(HKEY_CURRENT_USER, Path, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, Key, &Disposition);
    else
        Error = RegOpenKeyExW(HKEY_CURRENT_USER, Path, 0, KEY_READ | KEY_WRITE, Key);
    if (Error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(Error);
    if (Created)
        *Created = (Disposition == REG_CREATED_NEW_KEY);
    return S_OK;
}

HRESULT
WINAPI
CreateAppContainerProfile(PCWSTR pszAppContainerName,
                          PCWSTR pszDisplayName,
                          PCWSTR pszDescription,
                          PSID_AND_ATTRIBUTES pCapabilities,
                          DWORD dwCapabilityCount,
                          PSID *ppSidAppContainerSid)
{
    PSID Sid = NULL;
    HKEY Key = NULL;
    BOOL Created = FALSE;
    HRESULT hr;

    if (!pszAppContainerName || !pszDisplayName || (dwCapabilityCount && !pCapabilities))
        return E_INVALIDARG;
    if (ppSidAppContainerSid)
        *ppSidAppContainerSid = NULL;

    hr = DeriveAppContainerSidFromAppContainerName(pszAppContainerName, &Sid);
    if (FAILED(hr)) return hr;

    hr = OpenMappingKey(Sid, TRUE, &Key, &Created);
    if (FAILED(hr))
    {
        FreeSid(Sid);
        return hr;
    }

    if (!Created)
    {
        RegCloseKey(Key);
        FreeSid(Sid);
        return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    }

    RegSetValueExW(Key, L"Moniker", 0, REG_SZ, (const BYTE *)pszAppContainerName,
                   (DWORD)((wcslen(pszAppContainerName) + 1) * sizeof(WCHAR)));
    RegSetValueExW(Key, L"DisplayName", 0, REG_SZ, (const BYTE *)pszDisplayName,
                   (DWORD)((wcslen(pszDisplayName) + 1) * sizeof(WCHAR)));
    if (pszDescription)
    {
        RegSetValueExW(Key, L"Description", 0, REG_SZ, (const BYTE *)pszDescription,
                       (DWORD)((wcslen(pszDescription) + 1) * sizeof(WCHAR)));
    }
    RegCloseKey(Key);

    if (ppSidAppContainerSid)
        *ppSidAppContainerSid = Sid;
    else
        FreeSid(Sid);
    return S_OK;
}

HRESULT
WINAPI
DeleteAppContainerProfile(PCWSTR pszAppContainerName)
{
    PSID Sid = NULL;
    LPWSTR SidString = NULL;
    WCHAR Path[512];
    LONG Error;
    HRESULT hr;

    if (!pszAppContainerName)
        return E_INVALIDARG;

    hr = DeriveAppContainerSidFromAppContainerName(pszAppContainerName, &Sid);
    if (FAILED(hr)) return hr;

    if (!ConvertSidToStringSidW(Sid, &SidString))
    {
        FreeSid(Sid);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%s", APPCONTAINER_MAPPINGS_KEY, SidString);
    LocalFree(SidString);
    FreeSid(Sid);

    Error = RegDeleteKeyW(HKEY_CURRENT_USER, Path);
    if (Error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(Error);
    return S_OK;
}

HRESULT
WINAPI
GetAppContainerRegistryLocation(REGSAM desiredAccess, PHKEY phAppContainerKey)
{
    HANDLE Token;
    UCHAR Buffer[sizeof(PSID) + SECURITY_MAX_SID_SIZE];
    DWORD Length;
    PSID Sid;
    LPWSTR SidString = NULL;
    WCHAR Path[512];
    LONG Error;
    BOOL Impersonating = FALSE;

    if (!phAppContainerKey)
        return E_INVALIDARG;
    *phAppContainerKey = NULL;

    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_IMPERSONATE, TRUE, &Token))
    {
        Impersonating = TRUE;
    }
    else if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (!GetTokenInformation(Token, TokenAppContainerSid, Buffer, sizeof(Buffer), &Length))
    {
        CloseHandle(Token);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    Sid = ((PTOKEN_APPCONTAINER_INFORMATION)Buffer)->TokenAppContainer;
    if (!Sid)
    {
        CloseHandle(Token);
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    if (!ConvertSidToStringSidW(Sid, &SidString))
    {
        CloseHandle(Token);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%s", APPCONTAINER_MAPPINGS_KEY, SidString);
    LocalFree(SidString);

    if (Impersonating && !SetThreadToken(NULL, NULL))
    {
        Error = GetLastError();
        CloseHandle(Token);
        return HRESULT_FROM_WIN32(Error);
    }
    Error = RegOpenKeyExW(HKEY_CURRENT_USER, Path, 0, desiredAccess, phAppContainerKey);
    if (Impersonating && !SetThreadToken(NULL, Token))
    {
        if (*phAppContainerKey)
        {
            RegCloseKey(*phAppContainerKey);
            *phAppContainerKey = NULL;
        }
        Error = GetLastError();
    }
    CloseHandle(Token);
    if (Error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(Error);
    return S_OK;
}

HRESULT
WINAPI
GetAppContainerFolderPath(PCWSTR pszAppContainerSid, PWSTR *ppszPath)
{
    WCHAR Path[MAX_PATH], Profile[MAX_PATH], Moniker[256];
    HANDLE Token = NULL;
    DWORD Length, Type, Index;
    HKEY Key;
    LONG Error;
    PWSTR Result;
    SIZE_T Size;

    if (!pszAppContainerSid || !ppszPath)
        return E_INVALIDARG;
    *ppszPath = NULL;

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%s", APPCONTAINER_MAPPINGS_KEY, pszAppContainerSid);
    Error = RegOpenKeyExW(HKEY_CURRENT_USER, Path, 0, KEY_QUERY_VALUE, &Key);
    if (Error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(Error);

    Length = sizeof(Moniker);
    Error = RegQueryValueExW(Key, L"Moniker", NULL, &Type, (LPBYTE)Moniker, &Length);
    RegCloseKey(Key);
    if (Error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(Error);
    if (Type != REG_SZ)
        return E_INVALIDARG;
    Moniker[ARRAYSIZE(Moniker) - 1] = UNICODE_NULL;
    for (Index = 0; Moniker[Index]; Index++)
        Moniker[Index] = towlower(Moniker[Index]);

    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &Token) &&
        !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    Length = ARRAYSIZE(Profile);
    if (!GetUserProfileDirectoryW(Token, Profile, &Length))
    {
        if (Token) CloseHandle(Token);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (Token) CloseHandle(Token);

    if (FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path),
                                L"%s\\AppData\\Local\\Packages\\%s\\AC", Profile, Moniker)))
    {
        return E_INVALIDARG;
    }

    Size = (wcslen(Path) + 1) * sizeof(WCHAR);
    Result = CoTaskMemAlloc(Size);
    if (!Result)
        return E_OUTOFMEMORY;
    RtlCopyMemory(Result, Path, Size);
    *ppszPath = Result;
    return S_OK;
}
