/*
 * PROJECT:     ReactOS certutil
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     CertUtil hashfile implementation
 * COPYRIGHT:   Copyright 2020 Mark Jansen (mark.jansen@reactos.org)
 */

#include "precomp.h"
#include <wincrypt.h>
#include <stdlib.h>

#define MAX_HASH_SIZE 64

typedef struct
{
    LPCWSTR Name;
    ALG_ID Algorithm;
    DWORD ProviderType;
} HashAlgorithm;

static const HashAlgorithm HashAlgorithms[] = {
    { L"MD2",    CALG_MD2,     PROV_RSA_FULL },
    { L"MD4",    CALG_MD4,     PROV_RSA_FULL },
    { L"MD5",    CALG_MD5,     PROV_RSA_FULL },
    { L"SHA1",   CALG_SHA1,    PROV_RSA_FULL },
    { L"SHA256", CALG_SHA_256, PROV_RSA_AES  },
    { L"SHA384", CALG_SHA_384, PROV_RSA_AES  },
    { L"SHA512", CALG_SHA_512, PROV_RSA_AES  },
};

static const HashAlgorithm* MatchAlgorithm(LPCWSTR Name)
{
    size_t n;

    if (!Name)
        return HashAlgorithms + 3;

    for (n = 0; n < RTL_NUMBER_OF(HashAlgorithms); ++n)
    {
        if (!_wcsicmp(HashAlgorithms[n].Name, Name))
            return HashAlgorithms + n;
    }

    return NULL;
}

BOOL hash_file(LPCWSTR Filename, LPCWSTR AlgorithmName)
{
    HCRYPTPROV hProv;
    BOOL bSuccess = FALSE;
    const HashAlgorithm* Algorithm = MatchAlgorithm(AlgorithmName);
    HANDLE hFile;

    if (!Algorithm)
    {
        ConPrintf(StdOut, L"CertUtil: -hashfile unknown algorithm: %s\n", AlgorithmName);
        return FALSE;
    }

    hFile = CreateFileW(Filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        ConPrintf(StdOut, L"CertUtil: -hashfile command failed: %d\n", GetLastError());
        return bSuccess;
    }

    if (CryptAcquireContextW(&hProv, NULL, NULL, Algorithm->ProviderType, CRYPT_VERIFYCONTEXT))
    {
        HCRYPTHASH hHash;

        if (CryptCreateHash(hProv, Algorithm->Algorithm, 0, 0, &hHash))
        {
            BYTE Buffer[2048];
            DWORD cbRead;

            while ((bSuccess = ReadFile(hFile, Buffer, sizeof(Buffer), &cbRead, NULL)))
            {
                if (cbRead == 0)
                    break;

                if (!CryptHashData(hHash, Buffer, cbRead, 0))
                {
                    bSuccess = FALSE;
                    ConPrintf(StdOut, L"CertUtil: -hashfile command failed to hash: %d\n", GetLastError());
                    break;
                }
            }

            if (bSuccess)
            {
                BYTE rgbHash[MAX_HASH_SIZE];
                /* CryptGetHashParam reads this as the size of the buffer it is
                 * given, so it has to say how big that buffer is. */
                DWORD cbHash = sizeof(rgbHash), n;

                if (CryptGetHashParam(hHash, HP_HASHVAL, rgbHash, &cbHash, 0))
                {
                    ConPrintf(StdOut, L"%s hash of %s:\n", Algorithm->Name, Filename);
                    for (n = 0; n < cbHash; ++n)
                    {
                        ConPrintf(StdOut, L"%02x", rgbHash[n]);
                    }
                    ConPuts(StdOut, L"\n");
                }
                else
                {
                    ConPrintf(StdOut, L"CertUtil: -hashfile command failed to extract hash: %d\n", GetLastError());
                    bSuccess = FALSE;
                }
            }

            CryptDestroyHash(hHash);
        }
        else
        {
            ConPrintf(StdOut, L"CertUtil: -hashfile command no algorithm: %d\n", GetLastError());
        }

        CryptReleaseContext(hProv, 0);
    }
    else
    {
        ConPrintf(StdOut, L"CertUtil: -hashfile command no context: %d\n", GetLastError());
    }

    CloseHandle(hFile);
    return bSuccess;
}
