/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <windows.h>
#include <wincrypt.h>
#include <mscat.h>
#include <wine/test.h>

static BOOL (WINAPI *acquire_context)(HCATADMIN *, const GUID *, const WCHAR *, const CERT_STRONG_SIGN_PARA *, DWORD);
static BOOL (WINAPI *hash_file)(HCATADMIN, HANDLE, DWORD *, BYTE *, DWORD);

static void check_hash(HANDLE file, const WCHAR *algorithm, CERT_STRONG_SIGN_PARA *policy,
                       const BYTE *expected, DWORD length)
{
    HCATADMIN context = NULL;
    BYTE hash[64];
    DWORD size = 0;
    BOOL ret;

    ret = acquire_context(&context, NULL, algorithm, policy, 0);
    ok(ret, "Acquire %s: %lu\n", wine_dbgstr_w(algorithm), GetLastError());
    if (!ret) return;
    ret = hash_file(context, file, &size, NULL, 0);
    ok(ret && size == length, "Hash size %lu, expected %lu, error %lu\n", size, length, GetLastError());
    SetFilePointer(file, 0, NULL, FILE_BEGIN);
    size = sizeof(hash);
    ret = hash_file(context, file, &size, hash, 0);
    ok(ret && size == length, "Hash failed: %lu, size %lu\n", GetLastError(), size);
    if (ret && size == length) ok(!memcmp(hash, expected, length), "Wrong catalog digest\n");
    ret = CryptCATAdminReleaseContext(context, 0);
    ok(ret, "Release failed: %lu\n", GetLastError());
}

START_TEST(catalog_policy)
{
    static const BYTE sha1[] = {0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d};
    static const BYTE sha256[] = {0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    static const BYTE sha384[] = {0xcb,0x00,0x75,0x3f,0x45,0xa3,0x5e,0x8b,0xb5,0xa0,0x3d,0x69,0x9a,0xc6,0x50,0x07,0x27,0x2c,0x32,0xab,0x0e,0xde,0xd1,0x63,0x1a,0x8b,0x60,0x5a,0x43,0xff,0x5b,0xed,0x80,0x86,0x07,0x2b,0xa1,0xe7,0xcc,0x23,0x58,0xba,0xec,0xa1,0x34,0xc8,0x25,0xa7};
    static const BYTE sha512[] = {0xdd,0xaf,0x35,0xa1,0x93,0x61,0x7a,0xba,0xcc,0x41,0x73,0x49,0xae,0x20,0x41,0x31,0x12,0xe6,0xfa,0x4e,0x89,0xa9,0x7e,0xa2,0x0a,0x9e,0xee,0xe6,0x4b,0x55,0xd3,0x9a,0x21,0x92,0x99,0x2a,0x27,0x4f,0xc1,0xa8,0x36,0xba,0x3c,0x23,0xa3,0xfe,0xeb,0xbd,0x45,0x4d,0x44,0x23,0x64,0x3c,0xe8,0x0e,0x2a,0x9a,0xc9,0x4f,0xa5,0x4c,0xa4,0x9f};
    CERT_STRONG_SIGN_PARA policy = {sizeof(policy)};
    CERT_STRONG_SIGN_SERIALIZED_INFO serialized = {0};
    HCATADMIN context;
    WCHAR path[MAX_PATH], name[MAX_PATH];
    HMODULE module = LoadLibraryW(L"wintrust.dll");
    HANDLE file;
    DWORD written;
    BOOL ret;

    acquire_context = (void *)GetProcAddress(module, "CryptCATAdminAcquireContext2");
    hash_file = (void *)GetProcAddress(module, "CryptCATAdminCalcHashFromFileHandle2");
    if (!acquire_context || !hash_file)
    {
        win_skip("Catalog policy API unavailable\n");
        FreeLibrary(module);
        return;
    }
    GetTempPathW(ARRAY_SIZE(path), path);
    GetTempFileNameW(path, L"csh", 0, name);
    file = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                      FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    ok(file != INVALID_HANDLE_VALUE, "CreateFile: %lu\n", GetLastError());
    if (file == INVALID_HANDLE_VALUE)
    {
        DeleteFileW(name);
        FreeLibrary(module);
        return;
    }
    ret = WriteFile(file, "abc", 3, &written, NULL);
    ok(ret && written == 3, "WriteFile: %lu\n", GetLastError());
    check_hash(file, NULL, NULL, sha1, sizeof(sha1));
    policy.dwInfoChoice = CERT_STRONG_SIGN_OID_INFO_CHOICE;
    policy.pszOID = (char *)szOID_CERT_STRONG_SIGN_OS_CURRENT;
    check_hash(file, NULL, &policy, sha256, sizeof(sha256));
    check_hash(file, L"SHA256", &policy, sha256, sizeof(sha256));
    check_hash(file, L"SHA384", &policy, sha384, sizeof(sha384));
    policy.pszOID = (char *)szOID_CERT_STRONG_KEY_OS_CURRENT;
    check_hash(file, NULL, &policy, sha1, sizeof(sha1));
    policy.dwInfoChoice = CERT_STRONG_SIGN_SERIALIZED_INFO_CHOICE;
    serialized.pwszCNGSignHashAlgids = L"RSA/SHA512;ECDSA/SHA256";
    serialized.pwszCNGPubKeyMinBitLengths = L"RSA/2048;ECDSA/256";
    policy.pSerializedInfo = &serialized;
    check_hash(file, NULL, &policy, sha256, sizeof(sha256));
    serialized.pwszCNGSignHashAlgids = L"RSA/SHA512";
    check_hash(file, NULL, &policy, sha512, sizeof(sha512));

    serialized.pwszCNGSignHashAlgids = L"RSA/UNKNOWN";
    context = (HCATADMIN)(ULONG_PTR)0x1234;
    ret = acquire_context(&context, NULL, NULL, &policy, 0);
    ok(!ret && GetLastError() == NTE_BAD_ALGID, "Disallowed hashes: %d, %lu\n", ret, GetLastError());
    ok(context == (HCATADMIN)(ULONG_PTR)0x1234, "Failure overwrote context\n");
    serialized.pwszCNGSignHashAlgids = L"SHA256";
    ret = acquire_context(&context, NULL, NULL, &policy, 0);
    ok(!ret, "Malformed serialized policy accepted\n");
    policy.dwInfoChoice = CERT_STRONG_SIGN_OID_INFO_CHOICE;
    policy.pszOID = "1.2.3.4.5";
    ret = acquire_context(&context, NULL, NULL, &policy, 0);
    ok(!ret, "Unknown policy accepted\n");
    policy.pszOID = (char *)szOID_CERT_STRONG_SIGN_OS_CURRENT;
    policy.cbSize--;
    ret = acquire_context(&context, NULL, NULL, &policy, 0);
    ok(!ret, "Short policy accepted\n");
    CloseHandle(file);
    FreeLibrary(module);
}
