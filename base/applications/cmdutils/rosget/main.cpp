/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     rosget command-line interface
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#define NOMINMAX
#include <windows.h>

#include "hash.hpp"
#include "installer.hpp"
#include "manifest.hpp"
#include "details.hpp"
#include "gui.hpp"
#include "source.hpp"
#include "util.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace rosget
{

namespace
{

struct CommandOptions
{
    std::string query;
    SearchField field = SearchField::Any;
    bool exact = false;
    std::size_t count = 20;
    SelectionOptions selection;
    std::optional<std::wstring> downloadDirectory;
};

void PrintHelp()
{
    std::printf(
        "rosget %.*s - ReactOS package manager for the WinGet community repository\n\n"
        "Usage:\n"
        "  rosget search [[-q] <query>] [--id|--name|--moniker|--tag|--command <value>] [-e] [-n <count>]\n"
        "  rosget show <query> [--id <id>] [-e]\n"
        "  rosget download <query> [--id <id>] [--architecture <arch>]\n"
        "  rosget install <query> [--id <id>] [--architecture <arch>] [--silent|--interactive]\n"
        "  rosget source list|update|import <source2.msix>\n"
        "  rosget gui [[-q] <query>]\n"
        "  rosget selftest\n\n"
        "Common WinGet-compatible options:\n"
        "  -q, --query <query>        Search text\n"
        "  --id <id>                 Match a package identifier\n"
        "  --name <name>             Match only package names\n"
        "  --moniker <moniker>       Match only package monikers\n"
        "  --tag <tag>               Match only package tags\n"
        "  --command <command>       Match only provided commands\n"
        "  -e, --exact               Use an exact match\n"
        "  -n, --count <count>       Limit search results (1-1000)\n"
        "  -s, --source winget       Select the community source\n"
        "  --architecture <arch>     x86, x64, arm, or arm64\n"
        "  --installer-type <type>   Require an installer type\n"
        "  --scope <scope>           user or machine\n"
        "  --locale <locale>         Require an installer locale\n"
        "  --download-directory <p>  Store the verified installer in p\n"
        "  --silent                  Run with silent installer switches\n"
        "  --interactive             Run interactively\n"
        "  --disable-interactivity   Never prompt for user input\n"
        "  --accept-package-agreements\n"
        "                            Accept package license agreements\n",
        static_cast<int>(Version.size()), Version.data());
}

Status ParseCount(std::string_view value, std::size_t &count)
{
    const std::string owned(value);
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(owned.c_str(), &end, 10);
    if (!end || *end || parsed < 1 || parsed > 1000)
        return Status::Fail(ERROR_INVALID_PARAMETER, "--count must be between 1 and 1000");
    count = parsed;
    return Status::Ok();
}

Status ParseOptions(const std::vector<std::string> &arguments, std::size_t first, CommandOptions &options)
{
    bool modeSpecified = false;
    const auto selectField = [&options](SearchField field) -> Status {
        if (options.field != SearchField::Any && options.field != field)
            return Status::Fail(ERROR_INVALID_PARAMETER, "only one search field option may be specified");
        options.field = field;
        return Status::Ok();
    };
    for (std::size_t index = first; index < arguments.size(); ++index)
    {
        const std::string &argument = arguments[index];
        const auto next = [&]() -> const std::string * {
            if (index + 1 >= arguments.size())
                return nullptr;
            return &arguments[++index];
        };
        if (argument == "-q" || argument == "--query")
        {
            const std::string *value = next();
            if (!value) return Status::Fail(ERROR_INVALID_PARAMETER, argument + " requires a value");
            options.query = *value;
        }
        else if (argument == "--id")
        {
            const std::string *value = next();
            if (!value) return Status::Fail(ERROR_INVALID_PARAMETER, "--id requires a value");
            Status status = selectField(SearchField::Id);
            if (!status) return status;
            options.query = *value;
        }
        else if (argument == "--name" || argument == "--moniker" || argument == "--tag" || argument == "--command")
        {
            const std::string *value = next();
            if (!value) return Status::Fail(ERROR_INVALID_PARAMETER, argument + " requires a value");
            const SearchField field = argument == "--name" ? SearchField::Name :
                                      argument == "--moniker" ? SearchField::Moniker :
                                      argument == "--tag" ? SearchField::Tag : SearchField::Command;
            Status status = selectField(field);
            if (!status) return status;
            options.query = *value;
        }
        else if (argument == "-e" || argument == "--exact")
        {
            options.exact = true;
        }
        else if (argument == "-n" || argument == "--count")
        {
            const std::string *value = next();
            if (!value) return Status::Fail(ERROR_INVALID_PARAMETER, argument + " requires a value");
            Status status = ParseCount(*value, options.count);
            if (!status) return status;
        }
        else if (argument == "--architecture")
        {
            const std::string *value = next();
            if (!value) return Status::Fail(ERROR_INVALID_PARAMETER, "--architecture requires a value");
            const std::string architecture = AsciiLower(*value);
            if (architecture != "x86" && architecture != "x64" && architecture != "arm" && architecture != "arm64")
                return Status::Fail(ERROR_INVALID_PARAMETER, "unsupported architecture: " + *value);
            options.selection.architecture = architecture;
        }
        else if (argument == "--installer-type")
        {
            const std::string *value = next();
            if (!value || value->empty()) return Status::Fail(ERROR_INVALID_PARAMETER, "--installer-type requires a value");
            options.selection.installerType = AsciiLower(*value);
        }
        else if (argument == "--scope")
        {
            const std::string *value = next();
            if (!value) return Status::Fail(ERROR_INVALID_PARAMETER, "--scope requires a value");
            const std::string scope = AsciiLower(*value);
            if (scope != "user" && scope != "machine") return Status::Fail(ERROR_INVALID_PARAMETER, "--scope must be user or machine");
            options.selection.scope = scope;
        }
        else if (argument == "--locale")
        {
            const std::string *value = next();
            if (!value || value->empty()) return Status::Fail(ERROR_INVALID_PARAMETER, "--locale requires a value");
            options.selection.locale = *value;
        }
        else if (argument == "--download-directory")
        {
            const std::string *value = next();
            if (!value) return Status::Fail(ERROR_INVALID_PARAMETER, "--download-directory requires a value");
            options.downloadDirectory = WideFromUtf8(*value);
        }
        else if (argument == "--silent")
        {
            if (modeSpecified) return Status::Fail(ERROR_INVALID_PARAMETER, "--silent and --interactive cannot be combined");
            options.selection.mode = InstallMode::Silent;
            modeSpecified = true;
        }
        else if (argument == "--interactive")
        {
            if (modeSpecified) return Status::Fail(ERROR_INVALID_PARAMETER, "--silent and --interactive cannot be combined");
            options.selection.mode = InstallMode::Interactive;
            modeSpecified = true;
        }
        else if (argument == "-s" || argument == "--source")
        {
            const std::string *value = next();
            if (!value) return Status::Fail(ERROR_INVALID_PARAMETER, argument + " requires a value");
            if (!AsciiEquals(*value, "winget")) return Status::Fail(ERROR_NOT_SUPPORTED, "only the winget community source is currently supported");
        }
        else if (argument == "--disable-interactivity")
        {
            options.selection.disableInteractivity = true;
        }
        else if (argument == "--accept-package-agreements")
        {
            options.selection.acceptPackageAgreements = true;
        }
        else if (argument == "--accept-source-agreements")
        {
            return Status::Fail(ERROR_NOT_SUPPORTED, argument + " is not supported until rosget can retrieve and display the corresponding agreement text");
        }
        else if (!argument.empty() && argument.front() == '-')
        {
            return Status::Fail(ERROR_INVALID_PARAMETER, "unknown option: " + argument);
        }
        else if (options.query.empty())
        {
            options.query = argument;
        }
        else
        {
            return Status::Fail(ERROR_INVALID_PARAMETER, "unexpected argument: " + argument);
        }
    }
    return Status::Ok();
}

int Report(const Status &status)
{
    if (status)
        return 0;
    std::fprintf(stderr, "rosget: %s (0x%08lx)\n", status.message.c_str(), status.code);
    return status.code ? static_cast<int>(status.code) : 1;
}

Status ResolveManifest(SourceManager &source, const CommandOptions &options, PackageRecord &package, Manifest &manifest)
{
    if (options.query.empty())
        return Status::Fail(ERROR_INVALID_PARAMETER, "a package query or --id is required");
    PackageQuery query;
    query.text = options.query;
    query.field = options.field;
    query.exact = options.exact;
    Status status = source.Resolve(query, package);
    if (!status)
        return status;
    std::string yaml;
    status = source.FetchInstallerManifest(package, yaml);
    if (!status)
        return status;
    status = ParseInstallerManifest(yaml, manifest);
    if (!status)
        return status;
    if (!AsciiEquals(package.id, manifest.id) || package.version != manifest.version)
        return Status::Fail(ERROR_BAD_FORMAT, "manifest identity does not match the source index");
    return Status::Ok();
}

Status SearchCommand(SourceManager &source, const CommandOptions &options)
{
    PackageQuery query;
    query.text = options.query;
    query.field = options.field;
    query.exact = options.exact;
    query.count = options.count;
    std::vector<PackageRecord> packages;
    Status status = source.Search(query, packages);
    if (!status)
        return status;
    if (packages.empty())
        return Status::Fail(ERROR_NOT_FOUND, "no package found matching input criteria");

    std::size_t nameWidth = 4;
    std::size_t idWidth = 2;
    std::size_t versionWidth = 7;
    for (const PackageRecord &package : packages)
    {
        nameWidth = std::min<std::size_t>(40, std::max(nameWidth, package.name.size()));
        idWidth = std::min<std::size_t>(48, std::max(idWidth, package.id.size()));
        versionWidth = std::min<std::size_t>(20, std::max(versionWidth, package.version.size()));
    }
    std::printf("%-*s  %-*s  %-*s  Source\n", static_cast<int>(nameWidth), "Name", static_cast<int>(idWidth), "Id", static_cast<int>(versionWidth), "Version");
    std::printf("%-*s  %-*s  %-*s  ------\n", static_cast<int>(nameWidth), std::string(nameWidth, '-').c_str(), static_cast<int>(idWidth), std::string(idWidth, '-').c_str(), static_cast<int>(versionWidth), std::string(versionWidth, '-').c_str());
    for (const PackageRecord &package : packages)
        std::printf("%-*.*s  %-*.*s  %-*.*s  winget\n", static_cast<int>(nameWidth), static_cast<int>(nameWidth), package.name.c_str(), static_cast<int>(idWidth), static_cast<int>(idWidth), package.id.c_str(), static_cast<int>(versionWidth), static_cast<int>(versionWidth), package.version.c_str());
    std::printf("ROSGET_SEARCH_DONE count=%zu\n", packages.size());
    return Status::Ok();
}

void PrintPackage(const PackageRecord &package, const Manifest &manifest, const InstallerEntry &installer)
{
    std::printf("Found %s [%s]\n", package.name.c_str(), package.id.c_str());
    std::printf("Version:        %s\n", package.version.c_str());
    std::printf("Source:         winget\n");
    std::printf("Architecture:   %s\n", installer.architecture.c_str());
    std::printf("Installer type: %s\n", installer.type.c_str());
    std::printf("Installer URL:  %s\n", installer.url.c_str());
    std::printf("Installer SHA:  %s\n", installer.sha256.c_str());
    std::printf("Manifest:       %s %s (%zu installer%s)\n", manifest.id.c_str(), manifest.version.c_str(), manifest.installers.size(), manifest.installers.size() == 1 ? "" : "s");
}

void PrintPackageAgreements(const Manifest &manifest)
{
    if (manifest.agreements.empty()) return;
    std::printf("Package agreements:\n");
    for (const PackageAgreement &agreement : manifest.agreements)
    {
        std::printf("  %s\n", agreement.label.empty() ? "Agreement" : agreement.label.c_str());
        if (!agreement.text.empty()) std::printf("    %s\n", agreement.text.c_str());
        if (!agreement.url.empty()) std::printf("    %s\n", agreement.url.c_str());
    }
}

Status ShowCommand(SourceManager &source, const CommandOptions &options)
{
    PackageRecord package;
    Manifest manifest;
    Status status = ResolveManifest(source, options, package, manifest);
    if (!status)
        return status;
    InstallerEntry installer;
    status = SelectInstaller(manifest, options.selection, installer);
    if (!status)
        return status;
    PrintPackage(package, manifest, installer);
    PrintPackageAgreements(manifest);
    std::printf("ROSGET_SHOW_DONE id=%s version=%s\n", package.id.c_str(), package.version.c_str());
    return Status::Ok();
}

Status DownloadOrInstallCommand(SourceManager &source, const CommandOptions &options, bool install)
{
    PackageRecord package;
    Manifest manifest;
    Status status = ResolveManifest(source, options, package, manifest);
    if (!status)
        return status;
    if (manifest.hasAgreements)
    {
        PrintPackageAgreements(manifest);
        if (!options.selection.acceptPackageAgreements)
            return Status::Fail(ERROR_CANCELLED, "package agreements must be accepted with --accept-package-agreements");
    }
    InstallerEntry installer;
    SelectionOptions selection = options.selection;
    selection.downloadOnly = !install;
    status = SelectInstaller(manifest, selection, installer);
    if (!status)
        return status;
    PrintPackage(package, manifest, installer);

    InstallerService service;
    std::wstring path;
    status = service.Download(package, installer, options.downloadDirectory, path);
    if (!status)
        return status;
    std::printf("Installer downloaded to: %s\n", Utf8FromWide(path).c_str());
    if (!install)
    {
        std::printf("ROSGET_DOWNLOAD_DONE id=%s path=%s\n", package.id.c_str(), Utf8FromWide(path).c_str());
        return Status::Ok();
    }

    DWORD exitCode = 0;
    status = service.Install(installer, path, selection.mode, exitCode);
    if (!status)
        return status;
    std::printf("Successfully installed %s.\n", package.name.c_str());
    std::printf("ROSGET_INSTALL_DONE id=%s exit=%lu\n", package.id.c_str(), exitCode);
    return Status::Ok();
}

Status SelfTestCommand()
{
    Status status = RunManifestSelfTests();
    if (!status)
        return status;
    status = RunHashSelfTests();
    if (!status)
        return status;
    status = RunDetailsSelfTests();
    if (!status)
        return status;
    status = RunSQLiteIndexSelfTests();
    if (!status)
        return status;
    status = RunInstallerSelfTests();
    if (!status)
        return status;
    std::size_t count = 0;
    status = ParseCount("1000", count);
    if (!status || count != 1000 || ParseCount("1001", count))
        return Status::Fail(ERROR_ASSERTION_FAILURE, "command count parser self-test failed");
    CommandOptions options;
    status = ParseOptions({"rosget", "search", "--name", "Seven Zip", "--moniker", "7z"}, 2, options);
    if (status)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "command field conflict self-test failed");
    options = {};
    status = ParseOptions({"rosget", "install", "--silent", "--interactive", "Example.Tool"}, 2, options);
    if (status)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "command mode conflict self-test failed");
    std::printf("ROSGET_SELFTEST_DONE architecture=%s\n", ArchitectureName(CurrentMachineArchitecture()).c_str());
    return Status::Ok();
}

} // namespace

int Main(const std::vector<std::string> &arguments)
{
    if (arguments.size() < 2 || arguments[1] == "--help" || arguments[1] == "-?" || arguments[1] == "help")
    {
        PrintHelp();
        return 0;
    }
    if (arguments[1] == "--version" || arguments[1] == "-v")
    {
        std::printf(
            "rosget v%.*s\n"
            "Copyright (C) 2026 Ahmed Arif\n"
            "License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n",
            static_cast<int>(Version.size()), Version.data());
        return 0;
    }
    if (arguments[1] == "selftest")
        return Report(SelfTestCommand());

    SourceManager source;
    if (arguments[1] == "source")
    {
        if (arguments.size() >= 3 && arguments[2] == "list" && arguments.size() == 3)
        {
            std::printf("Name    Argument                                      Type\n");
            std::printf("------- --------------------------------------------- ----------------------------\n");
            std::printf("winget  https://cdn.winget.microsoft.com/cache       Microsoft.PreIndexed.Package\n");
            std::printf("ROSGET_SOURCE_LIST_DONE count=1\n");
            return 0;
        }
        if (arguments.size() >= 3 && arguments[2] == "update" && arguments.size() == 3)
        {
            const Status status = source.Update();
            if (status) std::printf("ROSGET_SOURCE_UPDATE_DONE\n");
            return Report(status);
        }
        if (arguments.size() == 4 && arguments[2] == "import")
        {
            const Status status = source.Import(WideFromUtf8(arguments[3]));
            if (status) std::printf("ROSGET_SOURCE_IMPORT_DONE\n");
            return Report(status);
        }
        return Report(Status::Fail(ERROR_INVALID_PARAMETER, "usage: rosget source list|update|import <source2.msix>"));
    }

    CommandOptions options;
    Status status = ParseOptions(arguments, 2, options);
    if (!status)
        return Report(status);
    if (arguments[1] == "gui" || arguments[1] == "--gui") return Report(RunCatalogGui(source, options.query));
    if (arguments[1] == "search") return Report(SearchCommand(source, options));
    if (arguments[1] == "show") return Report(ShowCommand(source, options));
    if (arguments[1] == "download") return Report(DownloadOrInstallCommand(source, options, false));
    if (arguments[1] == "install") return Report(DownloadOrInstallCommand(source, options, true));
    return Report(Status::Fail(ERROR_INVALID_PARAMETER, "unknown command: " + arguments[1]));
}

} // namespace rosget

int wmain(int argc, wchar_t **argv)
{
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index)
        arguments.push_back(rosget::Utf8FromWide(argv[index]));
    return rosget::Main(arguments);
}
