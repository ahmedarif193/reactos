/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Authenticode and MSIX block-map validation for the WinGet source
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#include "model.hpp"

namespace rosget
{

struct SourcePackageContents
{
    std::vector<std::uint8_t> signature;
    std::vector<std::uint8_t> blockMap;
    std::vector<std::uint8_t> manifest;
    std::vector<std::uint8_t> index;
};

Status VerifySourcePackageTrust(const SourcePackageContents &contents);

} // namespace rosget
