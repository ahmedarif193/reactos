/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WinGet community source access
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#include "http.hpp"
#include "sqlite_index.hpp"

namespace rosget
{

class SourceManager
{
public:
    SourceManager();

    Status Update();
    Status Import(std::wstring_view packagePath);
    Status EnsureReady();
    Status Search(const PackageQuery &query, std::vector<PackageRecord> &results);
    Status Resolve(const PackageQuery &query, PackageRecord &package);
    Status FetchInstallerManifest(const PackageRecord &package, std::string &yaml);
    const std::wstring &IndexPath() const { return indexPath_; }

private:
    Status ActivateSource(std::wstring_view sourcePackage, bool copyPackage);
    Status UpdateUnlocked();

    HttpClient http_;
    SQLiteIndex index_;
    std::wstring cacheDirectory_;
    std::wstring packagePath_;
    std::wstring indexPath_;
    bool ready_ = false;
};

} // namespace rosget
