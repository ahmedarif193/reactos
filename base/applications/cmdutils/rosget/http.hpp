/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     HTTPS client used by rosget
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#include "model.hpp"

#include <functional>

namespace rosget
{

struct HttpResponse
{
    DWORD statusCode = 0;
    std::vector<std::uint8_t> body;
};

using DownloadProgressCallback = std::function<void(unsigned long long received, unsigned long long total)>;

class HttpClient
{
public:
    HttpClient();
    ~HttpClient();
    HttpClient(const HttpClient &) = delete;
    HttpClient &operator=(const HttpClient &) = delete;

    Status Get(std::wstring_view url, std::wstring_view headers, HttpResponse &response) const;
    Status Download(std::wstring_view url, std::wstring_view destination, std::wstring_view headers = {},
                    unsigned long long maximumBytes = ~0ull, DownloadProgressCallback progress = {}) const;

private:
    void *session_ = nullptr;
};

} // namespace rosget
