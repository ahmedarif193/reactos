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
#include <cstring>
#include <limits>

namespace rosget
{

namespace
{

Status HashFileHandleSha256Streamed(HANDLE file,
                                    std::array<std::uint8_t, 32> &digest,
                                    const std::uint8_t *expectedBytes = nullptr,
                                    std::size_t expectedSize = 0)
{
    LARGE_INTEGER beginning{};
    if (!SetFilePointerEx(file, beginning, nullptr, FILE_BEGIN))
        return Status::Fail(GetLastError(), "cannot rewind installer for streamed hashing");

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

    DWORD readError = ERROR_SUCCESS;
    bool contentMismatch = false;
    std::size_t offset = 0;
    std::array<std::uint8_t, 128 * 1024> buffer{};
    while (BCRYPT_SUCCESS(result))
    {
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
        {
            readError = GetLastError();
            break;
        }
        if (!read)
            break;
        if (expectedBytes &&
            (offset > expectedSize || read > expectedSize - offset ||
             std::memcmp(buffer.data(), expectedBytes + offset, read)))
        {
            contentMismatch = true;
            break;
        }
        offset += read;
        result = BCryptHashData(hash, buffer.data(), read, 0);
    }
    if (expectedBytes && !contentMismatch && offset != expectedSize)
        contentMismatch = true;
    if (BCRYPT_SUCCESS(result) && !readError && !contentMismatch)
        result = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    if (hash)
        BCryptDestroyHash(hash);
    if (algorithm)
        BCryptCloseAlgorithmProvider(algorithm, 0);
    if (readError)
        return Status::Fail(readError, "cannot read installer while calculating SHA-256");
    if (contentMismatch)
        return Status::Fail(ERROR_CRC, "streamed file bytes differ from the bytes written");
    return BCRYPT_SUCCESS(result) ? Status::Ok() : Status::Fail(static_cast<DWORD>(result), "cannot calculate streamed installer SHA-256");
}

Status WriteHashSelfTestFile(std::wstring_view path,
                             const std::vector<std::uint8_t> &bytes)
{
    HANDLE file = CreateFileW(std::wstring(path).c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return Status::Fail(GetLastError(), "cannot create SHA-256 file self-test input");

    std::size_t offset = 0;
    while (offset < bytes.size())
    {
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(64 * 1024, bytes.size() - offset));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + offset, request, &written, nullptr) || written != request)
        {
            const DWORD error = GetLastError();
            CloseHandle(file);
            return Status::Fail(error, "cannot write SHA-256 file self-test input");
        }
        offset += written;
    }
    if (!FlushFileBuffers(file))
    {
        const DWORD error = GetLastError();
        CloseHandle(file);
        return Status::Fail(error, "cannot flush SHA-256 file self-test input");
    }
    CloseHandle(file);
    return Status::Ok();
}

} // namespace

Status HashBytesSha256(const std::uint8_t *bytes, std::size_t size, std::array<std::uint8_t, 32> &digest)
{
    if (size > std::numeric_limits<ULONG>::max())
        return Status::Fail(ERROR_FILE_TOO_LARGE, "SHA-256 input exceeds the BCrypt one-shot limit");
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

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file, &fileSize))
    {
        const DWORD error = GetLastError();
        CloseHandle(file);
        return Status::Fail(error, "cannot inspect installer for hashing: " + WindowsErrorMessage(error));
    }
    if (!fileSize.QuadPart)
    {
        CloseHandle(file);
        return HashBytesSha256(nullptr, 0, digest);
    }
    if (static_cast<unsigned long long>(fileSize.QuadPart) <= std::numeric_limits<ULONG>::max())
    {
        HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (mapping)
        {
            const std::uint8_t *view = static_cast<const std::uint8_t *>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
            if (view)
            {
                Status status = HashBytesSha256(view, static_cast<std::size_t>(fileSize.QuadPart), digest);
                UnmapViewOfFile(view);
                CloseHandle(mapping);
                CloseHandle(file);
                return status;
            }
            CloseHandle(mapping);
        }
    }

    Status status = HashFileHandleSha256Streamed(file, digest);
    CloseHandle(file);
    return status;
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
    if (HexLower(digest.data(), digest.size()) != Expected)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "SHA-256 short-vector self-test failed");

    /* Match the byte count of the installer that exposed the original failure. */
    std::vector<std::uint8_t> longInput(23684672);
    for (std::size_t index = 0; index < longInput.size(); ++index)
        longInput[index] = static_cast<std::uint8_t>(index * 37 + 11);
    static constexpr std::string_view LongExpected = "6817bf691b42ce0f9ea59fa05b5dc9e9c8331a4554b23594fcdcd9c6c40cc751";
    status = HashBytesSha256(longInput.data(), longInput.size(), digest);
    if (!status || HexLower(digest.data(), digest.size()) != LongExpected)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "SHA-256 long-vector self-test failed");

    wchar_t temporary[MAX_PATH]{};
    if (!GetTempFileNameW(TemporaryDirectory().c_str(), L"rgh", 0, temporary))
        return Status::Fail(GetLastError(), "cannot create SHA-256 file self-test input");
    status = WriteHashSelfTestFile(temporary, longInput);
    if (status)
        status = HashFileSha256(temporary, digest);
    if (!status || HexLower(digest.data(), digest.size()) != LongExpected)
    {
        DeleteFileW(temporary);
        return Status::Fail(ERROR_ASSERTION_FAILURE, "SHA-256 mapped-file self-test failed");
    }

    HANDLE file = CreateFileW(temporary, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        DeleteFileW(temporary);
        return Status::Fail(error, "cannot reopen SHA-256 file self-test input");
    }
    status = HashFileHandleSha256Streamed(file, digest, longInput.data(), longInput.size());
    CloseHandle(file);
    DeleteFileW(temporary);
    if (!status)
        return status;
    if (HexLower(digest.data(), digest.size()) != LongExpected)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "SHA-256 streamed-file self-test failed");
    return Status::Ok();
}

} // namespace rosget
