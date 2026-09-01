/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     rosget package catalog window
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#include "source.hpp"

namespace rosget
{

Status RunCatalogGui(SourceManager &source, const std::string &initialQuery);

} // namespace rosget
