/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Maps WinGet tags onto browsable catalog categories
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#include "model.hpp"

namespace rosget
{

struct CategoryDefinition
{
    const wchar_t *name;
    COLORREF accent;
    const char *tags;
    const char *roots;
};

const CategoryDefinition *CategoryTable();
std::size_t CategoryCount();
std::size_t OtherCategoryIndex();

class CategoryClassifier
{
public:
    CategoryClassifier();
    std::uint32_t Classify(const std::vector<std::uint32_t> &tags, const std::vector<std::string> &loweredNames) const;
    std::uint32_t ClassifyText(std::string_view loweredText) const;

private:
    std::map<std::string, std::uint32_t> exact_;
    std::vector<std::pair<std::string, std::uint32_t>> roots_;
};

} // namespace rosget
