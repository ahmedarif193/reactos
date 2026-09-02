/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     rosget utility helpers
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#define NOMINMAX
#include <windows.h>

#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace rosget
{

Status Status::Ok()
{
    return {};
}

Status Status::Fail(DWORD error, std::string text)
{
    Status status;
    status.success = false;
    status.code = error;
    status.message = std::move(text);
    return status;
}

std::string Utf8FromWide(std::wstring_view value)
{
    if (value.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring WideFromUtf8(std::string_view value)
{
    if (value.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0)
        return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string Trim(std::string_view value)
{
    std::size_t first = 0;
    while (first < value.size() && (value[first] == ' ' || value[first] == '\t' || value[first] == '\r' || value[first] == '\n'))
        ++first;
    std::size_t last = value.size();
    while (last > first && (value[last - 1] == ' ' || value[last - 1] == '\t' || value[last - 1] == '\r' || value[last - 1] == '\n'))
        --last;
    return std::string(value.substr(first, last - first));
}

std::string UnquoteYaml(std::string_view value)
{
    std::string result = Trim(value);
    if (result.size() >= 2 && ((result.front() == '"' && result.back() == '"') || (result.front() == '\'' && result.back() == '\'')))
        result = result.substr(1, result.size() - 2);
    return result;
}

std::string AsciiLower(std::string_view value)
{
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

bool AsciiEquals(std::string_view left, std::string_view right)
{
    return left.size() == right.size() && AsciiLower(left) == AsciiLower(right);
}

bool AsciiContains(std::string_view value, std::string_view needle)
{
    return AsciiLower(value).find(AsciiLower(needle)) != std::string::npos;
}

bool AsciiStartsWith(std::string_view value, std::string_view prefix)
{
    return value.size() >= prefix.size() && AsciiEquals(value.substr(0, prefix.size()), prefix);
}

static bool IsUnreserved(unsigned char character)
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '-' || character == '_' ||
           character == '.' || character == '~' || character == '/';
}

std::wstring UrlEncodePath(std::string_view path)
{
    static constexpr char Hex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(path.size() * 3);
    for (const unsigned char character : path)
    {
        if (IsUnreserved(character))
        {
            encoded.push_back(static_cast<char>(character));
        }
        else
        {
            encoded.push_back('%');
            encoded.push_back(Hex[character >> 4]);
            encoded.push_back(Hex[character & 0xf]);
        }
    }
    return WideFromUtf8(encoded);
}

std::wstring QuoteCommandArgument(std::wstring_view value)
{
    if (value.find_first_of(L" \t\"") == std::wstring_view::npos)
        return std::wstring(value);

    std::wstring result(1, L'"');
    std::size_t slashes = 0;
    for (const wchar_t character : value)
    {
        if (character == L'\\')
        {
            ++slashes;
            continue;
        }
        if (character == L'"')
        {
            result.append(slashes * 2 + 1, L'\\');
            result.push_back(L'"');
            slashes = 0;
            continue;
        }
        result.append(slashes, L'\\');
        slashes = 0;
        result.push_back(character);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

std::wstring JoinPath(std::wstring_view left, std::wstring_view right)
{
    std::wstring result(left);
    if (!result.empty() && result.back() != L'\\' && result.back() != L'/')
        result.push_back(L'\\');
    while (!right.empty() && (right.front() == L'\\' || right.front() == L'/'))
        right.remove_prefix(1);
    result.append(right);
    return result;
}

std::wstring ParentPath(std::wstring_view path)
{
    const auto position = path.find_last_of(L"\\/");
    return position == std::wstring_view::npos ? std::wstring() : std::wstring(path.substr(0, position));
}

std::wstring FileNameFromUrl(std::string_view url)
{
    const auto query = url.find_first_of("?#");
    url = url.substr(0, query);
    const auto slash = url.find_last_of('/');
    std::string name = std::string(slash == std::string_view::npos ? url : url.substr(slash + 1));
    if (name.empty())
        name = "installer.bin";
    for (char &character : name)
    {
        if (character == '<' || character == '>' || character == ':' || character == '"' || character == '/' ||
            character == '\\' || character == '|' || character == '?' || character == '*')
            character = '_';
    }
    return WideFromUtf8(name);
}

static std::wstring EnvironmentPath(const wchar_t *name)
{
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (!needed)
        return {};
    std::wstring value(needed, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), needed);
    if (!written || written >= needed)
        return {};
    value.resize(written);
    return value;
}

std::wstring LocalCacheDirectory()
{
    std::wstring base = EnvironmentPath(L"LOCALAPPDATA");
    if (base.empty())
        base = TemporaryDirectory();
    return JoinPath(base, L"rosget\\cache");
}

std::wstring TemporaryDirectory()
{
    const DWORD needed = GetTempPathW(0, nullptr);
    if (!needed)
        return L".";
    std::wstring value(needed, L'\0');
    const DWORD written = GetTempPathW(needed, value.data());
    if (!written || written >= needed)
        return L".";
    value.resize(written);
    return value;
}

Status EnsureDirectory(std::wstring_view path)
{
    if (path.empty())
        return Status::Ok();
    const DWORD attributes = GetFileAttributesW(std::wstring(path).c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES)
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? Status::Ok() : Status::Fail(ERROR_ALREADY_EXISTS, "path exists but is not a directory");

    const std::wstring parent = ParentPath(path);
    if (!parent.empty() && parent != path)
    {
        Status status = EnsureDirectory(parent);
        if (!status)
            return status;
    }
    if (CreateDirectoryW(std::wstring(path).c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS)
        return Status::Ok();
    const DWORD error = GetLastError();
    return Status::Fail(error, "cannot create directory: " + WindowsErrorMessage(error));
}

namespace
{

void TraceFileOperation(const std::string &message)
{
    const std::string line = "[ROSGET:FILE] " + message + "\n";
    OutputDebugStringA(line.c_str());
}

bool IsMoveCompatibilityError(DWORD error)
{
    return error == ERROR_INVALID_FUNCTION || error == ERROR_INVALID_PARAMETER || error == ERROR_INVALID_NAME ||
           error == ERROR_NOT_SUPPORTED || error == ERROR_CALL_NOT_IMPLEMENTED;
}

} // namespace

Status AtomicReplaceFile(std::wstring_view source, std::wstring_view destination, std::string_view description)
{
    const std::wstring sourcePath(source);
    const std::wstring destinationPath(destination);
    const DWORD sourceAttributes = GetFileAttributesW(sourcePath.c_str());
    const DWORD destinationAttributes = GetFileAttributesW(destinationPath.c_str());
    const DWORD durableFlags = MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH;
    TraceFileOperation("finalize " + std::string(description) + " source=\"" + Utf8FromWide(sourcePath) + "\" source-length=" + std::to_string(sourcePath.size()) + " source-attributes=" + std::to_string(sourceAttributes) + " destination=\"" + Utf8FromWide(destinationPath) + "\" destination-length=" + std::to_string(destinationPath.size()) + " destination-attributes=" + std::to_string(destinationAttributes) + " flags=" + std::to_string(durableFlags));

    if (MoveFileExW(sourcePath.c_str(), destinationPath.c_str(), durableFlags))
    {
        TraceFileOperation("MoveFileExW durable rename succeeded");
        return Status::Ok();
    }

    DWORD error = GetLastError();
    TraceFileOperation("MoveFileExW durable rename failed error=" + std::to_string(error) + " " + WindowsErrorMessage(error));
    if (IsMoveCompatibilityError(error))
    {
        if (MoveFileExW(sourcePath.c_str(), destinationPath.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            TraceFileOperation("MoveFileExW compatibility rename succeeded without WRITE_THROUGH");
            return Status::Ok();
        }

        error = GetLastError();
        TraceFileOperation("MoveFileExW compatibility rename failed error=" + std::to_string(error) + " " + WindowsErrorMessage(error));
        if (IsMoveCompatibilityError(error))
        {
            if (CopyFileW(sourcePath.c_str(), destinationPath.c_str(), FALSE))
            {
                HANDLE destinationFile = CreateFileW(destinationPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (destinationFile != INVALID_HANDLE_VALUE)
                {
                    if (!FlushFileBuffers(destinationFile))
                        TraceFileOperation("FlushFileBuffers after compatibility copy failed error=" + std::to_string(GetLastError()));
                    CloseHandle(destinationFile);
                }
                else
                {
                    TraceFileOperation("opening compatibility copy for flush failed error=" + std::to_string(GetLastError()));
                }
                if (!DeleteFileW(sourcePath.c_str()))
                    TraceFileOperation("compatibility copy succeeded but temporary cleanup failed error=" + std::to_string(GetLastError()));
                else
                    TraceFileOperation("compatibility copy and temporary cleanup succeeded");
                return Status::Ok();
            }

            error = GetLastError();
            TraceFileOperation("CopyFileW compatibility fallback failed error=" + std::to_string(error) + " " + WindowsErrorMessage(error));
        }
    }

    if (!DeleteFileW(sourcePath.c_str()))
        TraceFileOperation("failed-finalize temporary cleanup failed error=" + std::to_string(GetLastError()));
    return Status::Fail(error, "cannot finalize " + std::string(description) + ": " + WindowsErrorMessage(error));
}

Status ReadFileBytes(std::wstring_view path, std::vector<std::uint8_t> &bytes, std::size_t maximumSize)
{
    bytes.clear();
    HANDLE file = CreateFileW(std::wstring(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        return Status::Fail(error, "cannot open file: " + WindowsErrorMessage(error));
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0)
    {
        const DWORD error = GetLastError();
        CloseHandle(file);
        return Status::Fail(error ? error : ERROR_INVALID_DATA, "cannot determine file size");
    }
    if (static_cast<unsigned long long>(size.QuadPart) > SIZE_MAX ||
        static_cast<unsigned long long>(size.QuadPart) > maximumSize)
    {
        CloseHandle(file);
        return Status::Fail(ERROR_FILE_TOO_LARGE, "file exceeds its permitted size");
    }
    bytes.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t offset = 0;
    while (offset < bytes.size())
    {
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1024 * 1024));
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + offset, request, &read, nullptr) || !read)
        {
            const DWORD error = GetLastError();
            CloseHandle(file);
            return Status::Fail(error ? error : ERROR_HANDLE_EOF, "cannot read file");
        }
        offset += read;
    }
    CloseHandle(file);
    return Status::Ok();
}

Status WriteFileBytes(std::wstring_view path, const std::vector<std::uint8_t> &bytes)
{
    Status status = EnsureDirectory(ParentPath(path));
    if (!status)
        return status;
    HANDLE file = CreateFileW(std::wstring(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        return Status::Fail(error, "cannot create file: " + WindowsErrorMessage(error));
    }
    std::size_t offset = 0;
    while (offset < bytes.size())
    {
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1024 * 1024));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + offset, request, &written, nullptr) || written != request)
        {
            const DWORD error = GetLastError();
            CloseHandle(file);
            DeleteFileW(std::wstring(path).c_str());
            return Status::Fail(error, "cannot write file: " + WindowsErrorMessage(error));
        }
        offset += written;
    }
    CloseHandle(file);
    return Status::Ok();
}

std::string WindowsErrorMessage(DWORD error)
{
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    std::string result = length && buffer ? Trim(Utf8FromWide(std::wstring_view(buffer, length))) : "Windows error " + std::to_string(error);
    if (buffer)
        LocalFree(buffer);
    return result;
}

std::string HexLower(const std::uint8_t *data, std::size_t size)
{
    static constexpr char Hex[] = "0123456789abcdef";
    std::string result(size * 2, '0');
    for (std::size_t index = 0; index < size; ++index)
    {
        result[index * 2] = Hex[data[index] >> 4];
        result[index * 2 + 1] = Hex[data[index] & 0xf];
    }
    return result;
}

bool ParseHex256(std::string_view text, std::array<std::uint8_t, 32> &value)
{
    if (text.size() != value.size() * 2)
        return false;
    const auto digit = [](char character) -> int {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        if (character >= 'A' && character <= 'F') return character - 'A' + 10;
        return -1;
    };
    for (std::size_t index = 0; index < value.size(); ++index)
    {
        const int high = digit(text[index * 2]);
        const int low = digit(text[index * 2 + 1]);
        if (high < 0 || low < 0)
            return false;
        value[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

} // namespace rosget
