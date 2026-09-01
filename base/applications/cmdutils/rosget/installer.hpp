/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Verified installer download and execution
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#include "http.hpp"

namespace rosget
{

enum class InstallerDownloadStage
{
    Downloading,
    Verifying,
    Verified,
};

using InstallerStageCallback = std::function<void(InstallerDownloadStage)>;

class InstallerService
{
public:
    Status Download(const PackageRecord &package, const InstallerEntry &installer, std::optional<std::wstring> directory,
                    std::wstring &path, DownloadProgressCallback progress = {}, InstallerStageCallback stage = {});
    Status Install(const InstallerEntry &installer, std::wstring_view path, InstallMode mode, DWORD &exitCode);

private:
    std::wstring DefaultPackageDirectory(const PackageRecord &package) const;
    Status Execute(std::wstring commandLine, std::wstring_view workingDirectory, DWORD &exitCode) const;

    HttpClient http_;
};

Status RunInstallerSelfTests();

} // namespace rosget
