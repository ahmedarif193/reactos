/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SHA-256 verification for downloaded installers
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include "hash.hpp"
#include "util.hpp"

#include <algorithm>

namespace rosget
{

Status HashBytesSha256(const std::uint8_t *bytes, std::size_t size, std::array<std::uint8_t, 32> &digest)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<std::uint8_t> object;
    NTSTATUS result = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(result))
        return Status::Fail(static_cast<DWORD>(result), "BCryptOpenAlgorithmProvider(SHA256) failed");
    DWORD objectSize = 0;
    DWORD returned = 0;
    result = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &returned, 0);
    if (BCRYPT_SUCCESS(result))
    {
        object.resize(objectSize);
        result = BCryptCreateHash(algorithm, &hash, object.data(), static_cast<ULONG>(object.size()), nullptr, 0, 0);
    }
    if (BCRYPT_SUCCESS(result) && size)
        result = BCryptHashData(hash, const_cast<PUCHAR>(bytes), static_cast<ULONG>(size), 0);
    if (BCRYPT_SUCCESS(result))
        result = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    if (hash)
        BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return BCRYPT_SUCCESS(result) ? Status::Ok() : Status::Fail(static_cast<DWORD>(result), "SHA-256 operation failed");
}

Status HashFileSha256(std::wstring_view path, std::array<std::uint8_t, 32> &digest)
{
    HANDLE file = CreateFileW(std::wstring(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        return Status::Fail(error, "cannot open installer for hashing: " + WindowsErrorMessage(error));
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<std::uint8_t> object;
    NTSTATUS result = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    DWORD objectSize = 0;
    DWORD returned = 0;
    if (BCRYPT_SUCCESS(result))
        result = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &returned, 0);
    if (BCRYPT_SUCCESS(result))
    {
        object.resize(objectSize);
        result = BCryptCreateHash(algorithm, &hash, object.data(), static_cast<ULONG>(object.size()), nullptr, 0, 0);
    }

    std::array<std::uint8_t, 128 * 1024> buffer{};
    while (BCRYPT_SUCCESS(result))
    {
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
        {
            result = static_cast<NTSTATUS>(0xc0000001);
            break;
        }
        if (!read)
            break;
        result = BCryptHashData(hash, buffer.data(), read, 0);
    }
    if (BCRYPT_SUCCESS(result))
        result = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    if (hash)
        BCryptDestroyHash(hash);
    if (algorithm)
        BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return BCRYPT_SUCCESS(result) ? Status::Ok() : Status::Fail(static_cast<DWORD>(result), "cannot calculate installer SHA-256");
}

Status VerifyFileSha256(std::wstring_view path, std::string_view expectedHex)
{
    std::array<std::uint8_t, 32> expected{};
    if (!ParseHex256(expectedHex, expected))
        return Status::Fail(ERROR_BAD_FORMAT, "manifest InstallerSha256 is invalid");
    std::array<std::uint8_t, 32> actual{};
    Status status = HashFileSha256(path, actual);
    if (!status)
        return status;
    if (!std::equal(actual.begin(), actual.end(), expected.begin()))
        return Status::Fail(ERROR_CRC, "installer SHA-256 mismatch: expected " + HexLower(expected.data(), expected.size()) + ", got " + HexLower(actual.data(), actual.size()));
    return Status::Ok();
}

Status RunHashSelfTests()
{
    static constexpr std::uint8_t Input[] = {'a', 'b', 'c'};
    static constexpr std::string_view Expected = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    std::array<std::uint8_t, 32> digest{};
    Status status = HashBytesSha256(Input, sizeof(Input), digest);
    if (!status)
        return status;
    return HexLower(digest.data(), digest.size()) == Expected ? Status::Ok() : Status::Fail(ERROR_ASSERTION_FAILURE, "SHA-256 self-test failed");
}

} // namespace rosget
