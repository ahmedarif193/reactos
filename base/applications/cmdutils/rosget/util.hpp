/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     rosget utility declarations
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#include "model.hpp"

namespace rosget
{

std::string Utf8FromWide(std::wstring_view value);
std::wstring WideFromUtf8(std::string_view value);
std::string Trim(std::string_view value);
std::string UnquoteYaml(std::string_view value);
std::string AsciiLower(std::string_view value);
bool AsciiEquals(std::string_view left, std::string_view right);
bool AsciiContains(std::string_view value, std::string_view needle);
bool AsciiStartsWith(std::string_view value, std::string_view prefix);
std::wstring UrlEncodePath(std::string_view path);
std::wstring QuoteCommandArgument(std::wstring_view value);
std::wstring JoinPath(std::wstring_view left, std::wstring_view right);
std::wstring ParentPath(std::wstring_view path);
std::wstring FileNameFromUrl(std::string_view url);
std::wstring LocalCacheDirectory();
std::wstring TemporaryDirectory();
Status EnsureDirectory(std::wstring_view path);
Status AtomicReplaceFile(std::wstring_view source, std::wstring_view destination, std::string_view description);
Status ReadFileBytes(std::wstring_view path, std::vector<std::uint8_t> &bytes,
                     std::size_t maximumSize = static_cast<std::size_t>(-1));
Status WriteFileBytes(std::wstring_view path, const std::vector<std::uint8_t> &bytes);
std::string WindowsErrorMessage(DWORD error);
std::string HexLower(const std::uint8_t *data, std::size_t size);
bool ParseHex256(std::string_view text, std::array<std::uint8_t, 32> &value);

} // namespace rosget
