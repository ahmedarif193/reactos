/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     HTTPS client used by rosget
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#define NOMINMAX
#include <windows.h>
#include <wininet.h>

#include "http.hpp"
#include "util.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>

namespace rosget
{

namespace
{

constexpr unsigned MaximumAttempts = 4;
constexpr std::size_t MaximumMemoryResponse = 4 * 1024 * 1024;

class InternetRequest
{
public:
    explicit InternetRequest(HINTERNET handle) : handle_(handle) {}
    ~InternetRequest() { if (handle_) InternetCloseHandle(handle_); }
    InternetRequest(const InternetRequest &) = delete;
    InternetRequest &operator=(const InternetRequest &) = delete;
    HINTERNET Get() const { return handle_; }

private:
    HINTERNET handle_ = nullptr;
};

Status ValidateHttpsUrl(std::wstring_view url)
{
    if (url.size() < 8 || _wcsnicmp(url.data(), L"https://", 8) != 0)
        return Status::Fail(ERROR_INTERNET_INVALID_URL, "rosget only downloads HTTPS URLs");
    return Status::Ok();
}

Status OpenRequest(HINTERNET session, std::wstring_view url, std::wstring_view headers, std::unique_ptr<InternetRequest> &request)
{
    Status status = ValidateHttpsUrl(url);
    if (!status) return status;
    const DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_COOKIES |
                        INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_UI;
    HINTERNET handle = InternetOpenUrlW(session, std::wstring(url).c_str(), headers.empty() ? nullptr : std::wstring(headers).c_str(),
                                        headers.empty() ? 0 : static_cast<DWORD>(headers.size()), flags, 0);
    if (!handle)
    {
        const DWORD error = GetLastError();
        return Status::Fail(error, "HTTPS request failed: " + WindowsErrorMessage(error));
    }
    request = std::make_unique<InternetRequest>(handle);
    return Status::Ok();
}

Status QueryStatusCode(HINTERNET request, DWORD &statusCode)
{
    DWORD size = sizeof(statusCode);
    if (!HttpQueryInfoW(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &size, nullptr))
    {
        const DWORD error = GetLastError();
        return Status::Fail(error, "cannot read HTTP status: " + WindowsErrorMessage(error));
    }
    return Status::Ok();
}

unsigned long long QueryContentLength(HINTERNET request)
{
    wchar_t text[64]{};
    DWORD size = sizeof(text) - sizeof(wchar_t);
    if (!HttpQueryInfoW(request, HTTP_QUERY_CONTENT_LENGTH, text, &size, nullptr))
        return 0;
    text[std::min<std::size_t>(size / sizeof(wchar_t), (sizeof(text) / sizeof(text[0])) - 1)] = L'\0';
    wchar_t *end = nullptr;
    const unsigned long long length = _wcstoui64(text, &end, 10);
    return end && end != text && !*end ? length : 0;
}

bool IsTransientHttpStatus(DWORD status)
{
    return status == 408 || status == 425 || status == 429 || status == 500 || status == 502 || status == 503 || status == 504;
}

bool IsTransientInternetError(DWORD error)
{
    return error == ERROR_INTERNET_TIMEOUT || error == ERROR_INTERNET_CANNOT_CONNECT ||
           error == ERROR_INTERNET_CONNECTION_ABORTED || error == ERROR_INTERNET_CONNECTION_RESET ||
           error == ERROR_INTERNET_FORCE_RETRY || error == ERROR_INTERNET_INTERNAL_ERROR ||
           error == ERROR_INTERNET_NAME_NOT_RESOLVED;
}

void WaitBeforeRetry(unsigned attempt, HINTERNET request = nullptr)
{
    DWORD delay = attempt == 0 ? 250 : attempt == 1 ? 1000 : 2000;
    if (request)
    {
        DWORD seconds = 0;
        DWORD size = sizeof(seconds);
        if (HttpQueryInfoW(request, HTTP_QUERY_RETRY_AFTER | HTTP_QUERY_FLAG_NUMBER, &seconds, &size, nullptr))
            delay = std::min<DWORD>(seconds, 5) * 1000;
    }
    Sleep(delay);
}

std::wstring UniquePartialPath(std::wstring_view destination)
{
    static LONG sequence = 0;
    const LONG number = InterlockedIncrement(&sequence);
    std::wstring path(destination);
    path += L".partial.";
    path += std::to_wstring(GetCurrentProcessId());
    path.push_back(L'.');
    path += std::to_wstring(GetCurrentThreadId());
    path.push_back(L'.');
    path += std::to_wstring(static_cast<unsigned long>(number));
    return path;
}

Status AtomicReplace(std::wstring_view source, std::wstring_view destination, std::string_view description)
{
    if (!MoveFileExW(std::wstring(source).c_str(), std::wstring(destination).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const DWORD error = GetLastError();
        DeleteFileW(std::wstring(source).c_str());
        return Status::Fail(error, "cannot finalize " + std::string(description) + ": " + WindowsErrorMessage(error));
    }
    return Status::Ok();
}

} // namespace

HttpClient::HttpClient()
{
    session_ = InternetOpenW(L"rosget/0.1", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (session_)
    {
        DWORD timeout = 30000;
        InternetSetOptionW(static_cast<HINTERNET>(session_), INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionW(static_cast<HINTERNET>(session_), INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionW(static_cast<HINTERNET>(session_), INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    }
}

HttpClient::~HttpClient()
{
    if (session_) InternetCloseHandle(static_cast<HINTERNET>(session_));
}

Status HttpClient::Get(std::wstring_view url, std::wstring_view headers, HttpResponse &response) const
{
    response = {};
    if (!session_)
    {
        const DWORD error = GetLastError();
        return Status::Fail(error, "cannot initialize WinINet: " + WindowsErrorMessage(error));
    }

    Status last = Status::Fail(ERROR_RETRY, "HTTPS request retries exhausted");
    for (unsigned attempt = 0; attempt < MaximumAttempts; ++attempt)
    {
        response = {};
        std::unique_ptr<InternetRequest> request;
        last = OpenRequest(static_cast<HINTERNET>(session_), url, headers, request);
        if (!last)
        {
            if (attempt + 1 < MaximumAttempts && IsTransientInternetError(last.code))
            {
                WaitBeforeRetry(attempt);
                continue;
            }
            return last;
        }
        last = QueryStatusCode(request->Get(), response.statusCode);
        if (!last) return last;
        if (IsTransientHttpStatus(response.statusCode) && attempt + 1 < MaximumAttempts)
        {
            WaitBeforeRetry(attempt, request->Get());
            continue;
        }

        std::array<std::uint8_t, 64 * 1024> buffer{};
        for (;;)
        {
            DWORD read = 0;
            if (!InternetReadFile(request->Get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read))
            {
                const DWORD error = GetLastError();
                last = Status::Fail(error, "HTTPS response read failed: " + WindowsErrorMessage(error));
                break;
            }
            if (!read) return Status::Ok();
            if (response.body.size() > MaximumMemoryResponse - std::min<std::size_t>(read, MaximumMemoryResponse))
                return Status::Fail(ERROR_INSUFFICIENT_BUFFER, "HTTPS response exceeds the 4 MiB manifest limit");
            response.body.insert(response.body.end(), buffer.begin(), buffer.begin() + read);
        }
        if (attempt + 1 < MaximumAttempts && IsTransientInternetError(last.code))
        {
            WaitBeforeRetry(attempt);
            continue;
        }
        return last;
    }
    return last;
}

Status HttpClient::Download(std::wstring_view url, std::wstring_view destination, std::wstring_view headers,
                            unsigned long long maximumBytes, DownloadProgressCallback progress) const
{
    if (!session_)
    {
        const DWORD error = GetLastError();
        return Status::Fail(error, "cannot initialize WinINet: " + WindowsErrorMessage(error));
    }
    Status status = EnsureDirectory(ParentPath(destination));
    if (!status) return status;

    Status last = Status::Fail(ERROR_RETRY, "download retries exhausted");
    for (unsigned attempt = 0; attempt < MaximumAttempts; ++attempt)
    {
        std::unique_ptr<InternetRequest> request;
        last = OpenRequest(static_cast<HINTERNET>(session_), url, headers, request);
        if (!last)
        {
            if (attempt + 1 < MaximumAttempts && IsTransientInternetError(last.code))
            {
                WaitBeforeRetry(attempt);
                continue;
            }
            return last;
        }
        DWORD statusCode = 0;
        last = QueryStatusCode(request->Get(), statusCode);
        if (!last) return last;
        if (IsTransientHttpStatus(statusCode) && attempt + 1 < MaximumAttempts)
        {
            WaitBeforeRetry(attempt, request->Get());
            continue;
        }
        if (statusCode < 200 || statusCode >= 300)
            return Status::Fail(ERROR_INVALID_DATA, "HTTP server returned status " + std::to_string(statusCode));

        const unsigned long long expected = QueryContentLength(request->Get());
        if (expected && expected > maximumBytes)
            return Status::Fail(ERROR_FILE_TOO_LARGE, "download exceeds its permitted size");
        if (progress) progress(0, expected);

        const std::wstring partial = UniquePartialPath(destination);
        HANDLE file = CreateFileW(partial.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            const DWORD error = GetLastError();
            return Status::Fail(error, "cannot create download file: " + WindowsErrorMessage(error));
        }

        std::array<std::uint8_t, 64 * 1024> buffer{};
        unsigned long long total = 0;
        bool complete = false;
        for (;;)
        {
            DWORD read = 0;
            if (!InternetReadFile(request->Get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read))
            {
                const DWORD error = GetLastError();
                last = Status::Fail(error, "download read failed: " + WindowsErrorMessage(error));
                break;
            }
            if (!read)
            {
                complete = true;
                break;
            }
            if (total > maximumBytes || static_cast<unsigned long long>(read) > maximumBytes - total)
            {
                last = Status::Fail(ERROR_FILE_TOO_LARGE, "download exceeds its permitted size");
                break;
            }
            DWORD written = 0;
            if (!WriteFile(file, buffer.data(), read, &written, nullptr) || written != read)
            {
                const DWORD error = GetLastError();
                last = Status::Fail(error, "download write failed: " + WindowsErrorMessage(error));
                break;
            }
            total += read;
            if (progress) progress(total, expected);
        }
        if (complete && !FlushFileBuffers(file))
        {
            const DWORD error = GetLastError();
            last = Status::Fail(error, "cannot flush download file: " + WindowsErrorMessage(error));
            complete = false;
        }
        CloseHandle(file);
        if (!complete)
        {
            DeleteFileW(partial.c_str());
            if (attempt + 1 < MaximumAttempts && IsTransientInternetError(last.code))
            {
                WaitBeforeRetry(attempt);
                continue;
            }
            return last;
        }
        status = AtomicReplace(partial, destination, "download");
        if (!status) return status;
        if (progress) progress(total, expected ? expected : total);
        std::printf("Downloaded %llu bytes.\n", total);
        return Status::Ok();
    }
    return last;
}

} // namespace rosget
