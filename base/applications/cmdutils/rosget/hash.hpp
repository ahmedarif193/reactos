/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SHA-256 verification for downloaded installers
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#include "model.hpp"

namespace rosget
{

Status HashBytesSha256(const std::uint8_t *bytes, std::size_t size, std::array<std::uint8_t, 32> &digest);
Status HashFileSha256(std::wstring_view path, std::array<std::uint8_t, 32> &digest);
Status VerifyFileSha256(std::wstring_view path, std::string_view expectedHex);
Status RunHashSelfTests();

} // namespace rosget
