/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WinGet YAML manifest reader and installer selection
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#define NOMINMAX
#include <windows.h>

#include "manifest.hpp"
#include "util.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <limits>

namespace rosget
{

namespace
{

enum class NestedSection
{
    None,
    Switches,
    UnsupportedArchitectures,
    InstallModes,
    SuccessCodes,
    ExpectedReturnCodes,
    Dependencies,
    Authentication,
    Markets,
};

std::string RemoveYamlComment(std::string_view line)
{
    bool singleQuoted = false;
    bool doubleQuoted = false;
    for (std::size_t index = 0; index < line.size(); ++index)
    {
        if (line[index] == '\'' && !doubleQuoted)
            singleQuoted = !singleQuoted;
        else if (line[index] == '"' && !singleQuoted && (index == 0 || line[index - 1] != '\\'))
            doubleQuoted = !doubleQuoted;
        else if (line[index] == '#' && !singleQuoted && !doubleQuoted &&
                 (index == 0 || line[index - 1] == ' ' || line[index - 1] == '\t'))
            return std::string(line.substr(0, index));
    }
    return std::string(line);
}

bool SplitProperty(std::string_view line, std::string &key, std::string &value)
{
    const auto colon = line.find(':');
    if (colon == std::string_view::npos)
        return false;
    key = Trim(line.substr(0, colon));
    value = UnquoteYaml(line.substr(colon + 1));
    return !key.empty();
}

std::vector<std::string> ParseInlineList(std::string_view value)
{
    std::string text = Trim(value);
    if (text.size() < 2 || text.front() != '[' || text.back() != ']')
        return {};
    text = text.substr(1, text.size() - 2);
    std::vector<std::string> result;
    std::size_t offset = 0;
    while (offset <= text.size())
    {
        const std::size_t comma = text.find(',', offset);
        std::string item = UnquoteYaml(std::string_view(text).substr(offset, comma == std::string::npos ? text.size() - offset : comma - offset));
        if (!item.empty())
            result.push_back(std::move(item));
        if (comma == std::string::npos)
            break;
        offset = comma + 1;
    }
    return result;
}

bool ParseCode(std::string_view value, DWORD &code)
{
    const std::string owned = Trim(value);
    if (owned.empty() || owned.front() == '-')
        return false;
    errno = 0;
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(owned.c_str(), &end, 0);
    if (errno == ERANGE || !end || *end || parsed > std::numeric_limits<DWORD>::max())
        return false;
    code = static_cast<DWORD>(parsed);
    return true;
}

bool ParseBoolean(std::string_view value, bool &result)
{
    if (AsciiEquals(value, "true"))
    {
        result = true;
        return true;
    }
    if (AsciiEquals(value, "false"))
    {
        result = false;
        return true;
    }
    return false;
}

std::string FoldBlock(const std::vector<std::string> &lines, bool folded, bool keepTrailing)
{
    std::string text;
    for (const std::string &line : lines)
    {
        if (!folded)
        {
            text += line;
            text.push_back('\n');
            continue;
        }
        if (line.empty())
        {
            text.push_back('\n');
            continue;
        }
        if (!text.empty() && text.back() != '\n')
            text.push_back(' ');
        text += line;
    }
    if (!keepTrailing)
    {
        while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
            text.pop_back();
    }
    return text;
}

void SetSwitch(InstallerSwitches &switches, std::string_view key, std::string value)
{
    if (key == "Silent") switches.silent = std::move(value);
    else if (key == "SilentWithProgress") switches.silentWithProgress = std::move(value);
    else if (key == "Interactive") switches.interactive = std::move(value);
    else if (key == "Custom") switches.custom = std::move(value);
}

NestedSection SetInstallerProperty(InstallerEntry &installer, std::string_view key, std::string value)
{
    if (key == "Architecture") installer.architecture = std::move(value);
    else if (key == "InstallerType") installer.type = std::move(value);
    else if (key == "InstallerUrl") installer.url = std::move(value);
    else if (key == "InstallerSha256") installer.sha256 = std::move(value);
    else if (key == "Scope") installer.scope = std::move(value);
    else if (key == "InstallerLocale") installer.locale = std::move(value);
    else if (key == "MinimumOSVersion") installer.minimumOsVersion = std::move(value);
    else if (key == "InstallerSwitches") return NestedSection::Switches;
    else if (key == "UnsupportedOSArchitectures")
    {
        installer.unsupportedOsArchitectures = ParseInlineList(value);
        return NestedSection::UnsupportedArchitectures;
    }
    else if (key == "InstallModes")
    {
        installer.installModes = ParseInlineList(value);
        return NestedSection::InstallModes;
    }
    else if (key == "InstallerSuccessCodes")
    {
        for (const std::string &item : ParseInlineList(value))
        {
            DWORD code = 0;
            if (ParseCode(item, code)) installer.successCodes.push_back(code);
        }
        return NestedSection::SuccessCodes;
    }
    else if (key == "ExpectedReturnCodes") return NestedSection::ExpectedReturnCodes;
    else if (key == "Dependencies")
    {
        installer.hasDependencies = !value.empty();
        return NestedSection::Dependencies;
    }
    else if (key == "Authentication")
    {
        installer.hasAuthentication = true;
        return NestedSection::Authentication;
    }
    else if (key == "Markets")
    {
        installer.hasMarketRestrictions = true;
        return NestedSection::Markets;
    }
    else if (key == "DownloadCommandProhibited")
    {
        bool prohibited = false;
        if (ParseBoolean(value, prohibited)) installer.downloadCommandProhibited = prohibited;
    }
    return NestedSection::None;
}

void InheritString(std::string &value, const std::string &fallback)
{
    if (value.empty()) value = fallback;
}

void InheritInstaller(InstallerEntry &installer, const InstallerEntry &defaults)
{
    InheritString(installer.architecture, defaults.architecture);
    InheritString(installer.type, defaults.type);
    InheritString(installer.url, defaults.url);
    InheritString(installer.sha256, defaults.sha256);
    InheritString(installer.scope, defaults.scope);
    InheritString(installer.locale, defaults.locale);
    InheritString(installer.minimumOsVersion, defaults.minimumOsVersion);
    InheritString(installer.switches.silent, defaults.switches.silent);
    InheritString(installer.switches.silentWithProgress, defaults.switches.silentWithProgress);
    InheritString(installer.switches.interactive, defaults.switches.interactive);
    InheritString(installer.switches.custom, defaults.switches.custom);
    if (installer.unsupportedOsArchitectures.empty()) installer.unsupportedOsArchitectures = defaults.unsupportedOsArchitectures;
    if (installer.installModes.empty()) installer.installModes = defaults.installModes;
    if (installer.successCodes.empty()) installer.successCodes = defaults.successCodes;
    if (installer.expectedReturnCodes.empty()) installer.expectedReturnCodes = defaults.expectedReturnCodes;
    installer.hasDependencies = installer.hasDependencies || defaults.hasDependencies;
    installer.hasAuthentication = installer.hasAuthentication || defaults.hasAuthentication;
    installer.hasMarketRestrictions = installer.hasMarketRestrictions || defaults.hasMarketRestrictions;
    installer.downloadCommandProhibited = installer.downloadCommandProhibited || defaults.downloadCommandProhibited;
}

int ArchitectureRank(std::string_view installerArchitecture, MachineArchitecture machine)
{
    const std::string architecture = AsciiLower(installerArchitecture);
    if (architecture == "neutral") return 3;
    switch (machine)
    {
        case MachineArchitecture::Arm64:
            if (architecture == "arm64") return 0;
            if (architecture == "x64") return 1;
            if (architecture == "x86") return 2;
            if (architecture == "arm") return 4;
            break;
        case MachineArchitecture::X64:
            if (architecture == "x64") return 0;
            if (architecture == "x86") return 1;
            break;
        case MachineArchitecture::X86:
            if (architecture == "x86") return 0;
            break;
        case MachineArchitecture::Arm:
            if (architecture == "arm") return 0;
            break;
        case MachineArchitecture::Unknown:
            break;
    }
    return -1;
}

int TypeRank(std::string_view type)
{
    if (AsciiEquals(type, "wix") || AsciiEquals(type, "msi")) return 0;
    if (AsciiEquals(type, "inno") || AsciiEquals(type, "nullsoft") || AsciiEquals(type, "burn")) return 1;
    if (AsciiEquals(type, "exe")) return 2;
    return -1;
}

bool ContainsCaseInsensitive(const std::vector<std::string> &values, std::string_view wanted)
{
    return std::any_of(values.begin(), values.end(), [wanted](const std::string &value) { return AsciiEquals(value, wanted); });
}

bool HasNonInteractiveArguments(const InstallerEntry &installer, InstallMode mode)
{
    if (AsciiEquals(installer.type, "msi") || AsciiEquals(installer.type, "wix") ||
        AsciiEquals(installer.type, "inno") || AsciiEquals(installer.type, "nullsoft") || AsciiEquals(installer.type, "burn"))
        return true;
    if (mode == InstallMode::Silent)
        return !installer.switches.silent.empty();
    return !installer.switches.silentWithProgress.empty() || !installer.switches.silent.empty();
}

bool SupportsMode(const InstallerEntry &installer, InstallMode mode)
{
    if (installer.installModes.empty())
        return true;
    if (mode == InstallMode::Interactive)
        return ContainsCaseInsensitive(installer.installModes, "interactive");
    if (mode == InstallMode::Silent)
        return ContainsCaseInsensitive(installer.installModes, "silent");
    return ContainsCaseInsensitive(installer.installModes, "silentWithProgress") ||
           ContainsCaseInsensitive(installer.installModes, "silent");
}

bool ParseVersion(std::string_view text, std::array<DWORD, 4> &parts)
{
    parts = {};
    for (std::size_t index = 0; index < parts.size(); ++index)
    {
        const std::size_t dot = text.find('.');
        const std::string_view component = dot == std::string_view::npos ? text : text.substr(0, dot);
        DWORD value = 0;
        if (!ParseCode(component, value))
            return false;
        parts[index] = value;
        if (dot == std::string_view::npos)
            return true;
        text.remove_prefix(dot + 1);
    }
    return text.empty();
}

std::array<DWORD, 4> CurrentOsVersion()
{
    struct RtlVersionInfo
    {
        ULONG size;
        ULONG major;
        ULONG minor;
        ULONG build;
        ULONG platform;
        WCHAR servicePack[128];
    } information{};
    information.size = sizeof(information);
    using RtlGetVersionFunction = LONG (WINAPI *)(RtlVersionInfo *);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto rtlGetVersion = ntdll ? reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    if (rtlGetVersion && rtlGetVersion(&information) >= 0)
        return {information.major, information.minor, information.build, 0};
    return {};
}

bool MeetsMinimumOsVersion(std::string_view minimum)
{
    if (minimum.empty())
        return true;
    std::array<DWORD, 4> required{};
    if (!ParseVersion(minimum, required))
        return false;
    return CurrentOsVersion() >= required;
}

bool IsSha256(std::string_view value)
{
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char character) {
        return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
               (character >= 'A' && character <= 'F');
    });
}

Status ParsePackageAgreements(std::string_view yaml, std::vector<PackageAgreement> &agreements)
{
    agreements.clear();
    std::vector<std::string> lines;
    std::size_t offset = 0;
    while (offset <= yaml.size())
    {
        const std::size_t end = yaml.find('\n', offset);
        std::string line(yaml.substr(offset, end == std::string_view::npos ? yaml.size() - offset : end - offset));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        if (end == std::string_view::npos) break;
        offset = end + 1;
    }

    bool inAgreements = false;
    std::size_t agreementsIndent = 0;
    std::size_t itemIndent = 0;
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        std::string line = RemoveYamlComment(lines[index]);
        if (Trim(line).empty()) continue;
        std::size_t indent = 0;
        while (indent < line.size() && line[indent] == ' ') ++indent;
        if (indent < line.size() && line[indent] == '\t')
            return Status::Fail(ERROR_BAD_FORMAT, "package agreements use a tab for YAML indentation");
        std::string_view content(line.data() + indent, line.size() - indent);
        if (!inAgreements)
        {
            if (Trim(content) == "Agreements:")
            {
                inAgreements = true;
                agreementsIndent = indent;
            }
            continue;
        }
        if (indent <= agreementsIndent && content.rfind("- ", 0) != 0)
            break;

        std::string_view property = content;
        if (content.rfind("- ", 0) == 0 && (agreements.empty() || indent <= itemIndent))
        {
            if (agreements.size() >= 32)
                return Status::Fail(ERROR_BAD_FORMAT, "package manifest contains too many agreements");
            agreements.emplace_back();
            itemIndent = indent;
            property.remove_prefix(2);
        }
        else if (agreements.empty() || indent <= itemIndent)
        {
            return Status::Fail(ERROR_BAD_FORMAT, "package agreement list is malformed");
        }

        std::string key;
        std::string value;
        if (!SplitProperty(property, key, value)) continue;
        const bool block = value == "|" || value == "|-" || value == "|+" ||
                           value == ">" || value == ">-" || value == ">+";
        if (block)
        {
            const bool folded = value.front() == '>';
            const bool keep = value.size() > 1 && value[1] == '+';
            std::vector<std::string> blockLines;
            std::size_t blockIndent = 0;
            while (index + 1 < lines.size())
            {
                const std::string &next = lines[index + 1];
                std::size_t nextIndent = 0;
                while (nextIndent < next.size() && next[nextIndent] == ' ') ++nextIndent;
                if (nextIndent == next.size())
                {
                    blockLines.emplace_back();
                    ++index;
                    continue;
                }
                if (nextIndent <= indent) break;
                if (!blockIndent) blockIndent = nextIndent;
                if (nextIndent < blockIndent) break;
                blockLines.push_back(next.substr(blockIndent));
                ++index;
            }
            value = FoldBlock(blockLines, folded, keep);
        }

        PackageAgreement &agreement = agreements.back();
        if (key == "AgreementLabel") agreement.label = std::move(value);
        else if (key == "Agreement") agreement.text = std::move(value);
        else if (key == "AgreementUrl") agreement.url = std::move(value);
    }
    for (const PackageAgreement &agreement : agreements)
    {
        if (agreement.label.size() > 100 || agreement.text.size() > 10000 || agreement.url.size() > 2048 ||
            (agreement.text.empty() && agreement.url.empty()))
            return Status::Fail(ERROR_BAD_FORMAT, "package manifest contains an invalid agreement");
    }
    return Status::Ok();
}

Status SelectInstallerForMachine(const Manifest &manifest, const SelectionOptions &options, MachineArchitecture machine, InstallerEntry &installer)
{
    if (options.architecture && ArchitectureRank(*options.architecture, machine) < 0)
        return Status::Fail(ERROR_NOT_SUPPORTED, "requested architecture " + *options.architecture +
                            " cannot run on " + ArchitectureName(machine));

    int bestRank = 1000;
    const InstallerEntry *best = nullptr;
    bool constrained = false;
    bool unsupportedType = false;
    for (const InstallerEntry &candidate : manifest.installers)
    {
        int architectureRank = ArchitectureRank(candidate.architecture, machine);
        if (options.architecture)
        {
            if (AsciiEquals(candidate.architecture, *options.architecture)) architectureRank = 0;
            else if (AsciiEquals(candidate.architecture, "neutral")) architectureRank = 1;
            else architectureRank = -1;
        }
        const int typeRank = TypeRank(candidate.type);
        unsupportedType = unsupportedType || typeRank < 0;
        if (architectureRank < 0 || typeRank < 0 || candidate.url.empty() || !IsSha256(candidate.sha256) ||
            (options.installerType && !AsciiEquals(candidate.type, *options.installerType)))
            continue;
        if (ContainsCaseInsensitive(candidate.unsupportedOsArchitectures, ArchitectureName(machine)) ||
            !MeetsMinimumOsVersion(candidate.minimumOsVersion) || candidate.hasDependencies ||
            candidate.hasAuthentication || candidate.hasMarketRestrictions ||
            (options.scope && !AsciiEquals(candidate.scope, *options.scope)) ||
            (options.locale && !AsciiEquals(candidate.locale, *options.locale)) ||
            (options.downloadOnly && candidate.downloadCommandProhibited) || !SupportsMode(candidate, options.mode) ||
            (options.mode != InstallMode::Interactive && !HasNonInteractiveArguments(candidate, options.mode)))
        {
            constrained = true;
            continue;
        }
        const int rank = architectureRank * 10 + typeRank;
        if (rank < bestRank)
        {
            bestRank = rank;
            best = &candidate;
        }
    }
    if (!best)
    {
        if (constrained)
            return Status::Fail(ERROR_NOT_SUPPORTED, "matching installers require an unsupported OS, dependency, authentication, market, download, or install-mode capability");
        if (unsupportedType)
            return Status::Fail(ERROR_NOT_SUPPORTED, "matching manifests only contain installer types rosget does not implement");
        return Status::Fail(ERROR_NOT_SUPPORTED, "no supported installer applies to architecture " +
                            (options.architecture ? *options.architecture : ArchitectureName(machine)));
    }
    installer = *best;
    return Status::Ok();
}

} // namespace

Status ParseInstallerManifest(std::string_view yaml, Manifest &manifest)
{
    manifest = {};
    InstallerEntry defaults;
    InstallerEntry *current = nullptr;
    bool inInstallers = false;
    std::size_t installerItemIndent = 0;
    NestedSection section = NestedSection::None;
    std::size_t sectionIndent = 0;
    InstallerEntry *sectionTarget = nullptr;
    std::optional<DWORD> expectedCode;

    std::size_t offset = 0;
    while (offset <= yaml.size())
    {
        const std::size_t end = yaml.find('\n', offset);
        std::string line = RemoveYamlComment(yaml.substr(offset, end == std::string_view::npos ? yaml.size() - offset : end - offset));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        offset = end == std::string_view::npos ? yaml.size() + 1 : end + 1;
        if (Trim(line).empty()) continue;

        if (Trim(line) == "Agreements:") manifest.hasAgreements = true;

        std::size_t indent = 0;
        while (indent < line.size() && line[indent] == ' ') ++indent;
        if (indent < line.size() && line[indent] == '\t')
            return Status::Fail(ERROR_BAD_FORMAT, "manifest uses a tab for YAML indentation");
        std::string_view content(line.data() + indent, line.size() - indent);

        if (inInstallers && content.rfind("- ", 0) == 0 &&
            (!current || indent <= installerItemIndent))
        {
            manifest.installers.emplace_back();
            current = &manifest.installers.back();
            installerItemIndent = indent;
            section = NestedSection::None;
            sectionTarget = nullptr;
            expectedCode.reset();
            std::string key;
            std::string value;
            if (SplitProperty(content.substr(2), key, value))
            {
                section = SetInstallerProperty(*current, key, std::move(value));
                if (section != NestedSection::None)
                {
                    sectionIndent = indent;
                    sectionTarget = current;
                }
            }
            continue;
        }

        if (section != NestedSection::None && indent >= sectionIndent && content.rfind("- ", 0) == 0 && sectionTarget)
        {
            const std::string item = UnquoteYaml(content.substr(2));
            if (section == NestedSection::UnsupportedArchitectures) sectionTarget->unsupportedOsArchitectures.push_back(item);
            else if (section == NestedSection::InstallModes) sectionTarget->installModes.push_back(item);
            else if (section == NestedSection::SuccessCodes)
            {
                DWORD code = 0;
                if (!ParseCode(item, code)) return Status::Fail(ERROR_BAD_FORMAT, "InstallerSuccessCodes contains a non-numeric value");
                sectionTarget->successCodes.push_back(code);
            }
            else if (section == NestedSection::ExpectedReturnCodes)
            {
                std::string key;
                std::string value;
                if (SplitProperty(content.substr(2), key, value) && key == "InstallerReturnCode")
                {
                    DWORD code = 0;
                    if (!ParseCode(value, code)) return Status::Fail(ERROR_BAD_FORMAT, "ExpectedReturnCodes contains a non-numeric code");
                    expectedCode = code;
                    sectionTarget->expectedReturnCodes[code] = "expected installer response";
                }
            }
            else if (section == NestedSection::Dependencies) sectionTarget->hasDependencies = true;
            else if (section == NestedSection::Authentication) sectionTarget->hasAuthentication = true;
            else if (section == NestedSection::Markets) sectionTarget->hasMarketRestrictions = true;
            continue;
        }

        std::string key;
        std::string value;
        if (!SplitProperty(content, key, value))
            continue;

        if (section != NestedSection::None && indent > sectionIndent && sectionTarget)
        {
            if (section == NestedSection::Switches) SetSwitch(sectionTarget->switches, key, std::move(value));
            else if (section == NestedSection::ExpectedReturnCodes && key == "ReturnResponse" && expectedCode)
                sectionTarget->expectedReturnCodes[*expectedCode] = std::move(value);
            else if (section == NestedSection::Dependencies) sectionTarget->hasDependencies = true;
            else if (section == NestedSection::Authentication) sectionTarget->hasAuthentication = true;
            else if (section == NestedSection::Markets) sectionTarget->hasMarketRestrictions = true;
            continue;
        }

        section = NestedSection::None;
        sectionTarget = nullptr;
        expectedCode.reset();
        if (indent == 0 && key == "Installers")
        {
            inInstallers = true;
            current = nullptr;
            continue;
        }
        if (indent == 0)
        {
            inInstallers = false;
            if (key == "PackageIdentifier") manifest.id = std::move(value);
            else if (key == "PackageVersion") manifest.version = std::move(value);
            else
            {
                section = SetInstallerProperty(defaults, key, std::move(value));
                if (section != NestedSection::None)
                {
                    sectionIndent = indent;
                    sectionTarget = &defaults;
                }
            }
            continue;
        }
        if (inInstallers && current && indent > installerItemIndent)
        {
            section = SetInstallerProperty(*current, key, std::move(value));
            if (section != NestedSection::None)
            {
                sectionIndent = indent;
                sectionTarget = current;
            }
        }
    }

    if (manifest.id.empty() || manifest.version.empty())
        return Status::Fail(ERROR_BAD_FORMAT, "manifest is missing PackageIdentifier or PackageVersion");
    if (manifest.installers.empty() && (!defaults.architecture.empty() || !defaults.url.empty()))
        manifest.installers.push_back(defaults);
    for (InstallerEntry &installer : manifest.installers)
        InheritInstaller(installer, defaults);

    manifest.defaultArchitecture = defaults.architecture;
    manifest.defaultType = defaults.type;
    manifest.defaultScope = defaults.scope;
    manifest.defaultLocale = defaults.locale;
    manifest.defaultMinimumOsVersion = defaults.minimumOsVersion;
    manifest.defaultUrl = defaults.url;
    manifest.defaultSha256 = defaults.sha256;
    manifest.defaultSwitches = defaults.switches;
    manifest.defaultUnsupportedOsArchitectures = defaults.unsupportedOsArchitectures;
    manifest.defaultInstallModes = defaults.installModes;
    manifest.defaultSuccessCodes = defaults.successCodes;
    manifest.defaultExpectedReturnCodes = defaults.expectedReturnCodes;
    manifest.defaultHasDependencies = defaults.hasDependencies;
    manifest.defaultHasAuthentication = defaults.hasAuthentication;
    manifest.defaultHasMarketRestrictions = defaults.hasMarketRestrictions;
    manifest.defaultDownloadCommandProhibited = defaults.downloadCommandProhibited;
    Status agreementStatus = ParsePackageAgreements(yaml, manifest.agreements);
    if (!agreementStatus) return agreementStatus;
    if (manifest.hasAgreements && manifest.agreements.empty())
        return Status::Fail(ERROR_BAD_FORMAT, "manifest declares package agreements without displayable terms");
    manifest.hasAgreements = !manifest.agreements.empty();
    return Status::Ok();
}

namespace
{

void SetDetailProperty(PackageDetails &details, std::string_view key, std::string value)
{
    if (key == "Publisher") details.publisher = std::move(value);
    else if (key == "PublisherUrl") details.publisherUrl = std::move(value);
    else if (key == "PublisherSupportUrl") details.supportUrl = std::move(value);
    else if (key == "Author") details.author = std::move(value);
    else if (key == "PackageName") details.packageName = std::move(value);
    else if (key == "PackageUrl") details.packageUrl = std::move(value);
    else if (key == "License") details.license = std::move(value);
    else if (key == "LicenseUrl") details.licenseUrl = std::move(value);
    else if (key == "Copyright") details.copyright = std::move(value);
    else if (key == "ShortDescription") details.shortDescription = std::move(value);
    else if (key == "Description") details.description = std::move(value);
    else if (key == "ReleaseNotesUrl") details.releaseNotesUrl = std::move(value);
    else if (key == "Documentations" || key == "DocumentUrl") details.documentationUrl = std::move(value);
}

bool FlowScalarComplete(std::string_view text, char quote)
{
    if (text.size() < 2)
        return false;
    std::size_t index = 1;
    while (index < text.size())
    {
        if (quote == '\'')
        {
            if (text[index] != '\'')
            {
                ++index;
                continue;
            }
            if (index + 1 < text.size() && text[index + 1] == '\'')
            {
                index += 2;
                continue;
            }
            return true;
        }
        if (text[index] == '\\')
        {
            index += 2;
            continue;
        }
        if (text[index] == '"')
            return true;
        ++index;
    }
    return false;
}

void AppendUtf8(std::string &text, unsigned long code)
{
    if (code < 0x80)
    {
        text.push_back(static_cast<char>(code));
    }
    else if (code < 0x800)
    {
        text.push_back(static_cast<char>(0xc0 | (code >> 6)));
        text.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    }
    else
    {
        text.push_back(static_cast<char>(0xe0 | (code >> 12)));
        text.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
        text.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    }
}

std::string DecodeFlowScalar(std::string_view text, char quote)
{
    std::string value;
    std::size_t index = 1;
    while (index < text.size())
    {
        const char character = text[index];
        if (character == quote)
        {
            if (quote == '\'' && index + 1 < text.size() && text[index + 1] == '\'')
            {
                value.push_back('\'');
                index += 2;
                continue;
            }
            break;
        }
        if (character == '\n')
        {
            std::size_t breaks = 0;
            while (index < text.size() && (text[index] == '\n' || text[index] == ' '))
            {
                if (text[index] == '\n')
                    ++breaks;
                ++index;
            }
            if (breaks > 1)
                value.append(breaks - 1, '\n');
            else if (!value.empty())
                value.push_back(' ');
            continue;
        }
        if (quote == '"' && character == '\\' && index + 1 < text.size())
        {
            const char escape = text[index + 1];
            index += 2;
            switch (escape)
            {
                case 'n': value.push_back('\n'); break;
                case 't': value.push_back('\t'); break;
                case 'r': value.push_back('\r'); break;
                case '0': value.push_back('\0'); break;
                case 'u':
                {
                    unsigned long code = 0;
                    std::size_t digits = 0;
                    while (digits < 4 && index < text.size() && std::isxdigit(static_cast<unsigned char>(text[index])))
                    {
                        const char digit = text[index];
                        code = code * 16 + static_cast<unsigned long>(digit <= '9' ? digit - '0' : (digit | 0x20) - 'a' + 10);
                        ++index;
                        ++digits;
                    }
                    AppendUtf8(value, code);
                    break;
                }
                default: value.push_back(escape); break;
            }
            continue;
        }
        value.push_back(character);
        ++index;
    }
    return value;
}

} // namespace

Status ParseLocaleManifest(std::string_view yaml, PackageDetails &details)
{
    details = {};
    std::vector<std::string> lines;
    std::size_t offset = 0;
    while (offset <= yaml.size())
    {
        const std::size_t end = yaml.find('\n', offset);
        std::string line(yaml.substr(offset, end == std::string_view::npos ? yaml.size() - offset : end - offset));
        offset = end == std::string_view::npos ? yaml.size() + 1 : end + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(std::move(line));
    }

    const auto indentOf = [](const std::string &line) {
        std::size_t indent = 0;
        while (indent < line.size() && line[indent] == ' ')
            ++indent;
        return indent;
    };
    const auto isBlank = [&indentOf](const std::string &line) { return indentOf(line) == line.size(); };
    const auto isSequence = [](const std::string &line) {
        const std::string trimmed = Trim(line);
        return trimmed.size() > 1 && trimmed[0] == '-' && trimmed[1] == ' ';
    };

    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        const std::string line = lines[index];
        if (isBlank(line) || indentOf(line))
            continue;
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        const std::string key = Trim(line.substr(0, colon));
        const std::string raw = Trim(line.substr(colon + 1));
        if (key.empty())
            continue;

        if (key == "Tags")
        {
            while (index + 1 < lines.size() && isSequence(lines[index + 1]))
                details.tags.push_back(UnquoteYaml(Trim(lines[++index]).substr(2)));
            continue;
        }
        if (raw.empty())
            continue;

        if (raw == "|" || raw == "|-" || raw == "|+" || raw == ">" || raw == ">-" || raw == ">+")
        {
            std::vector<std::string> block;
            std::size_t blockIndent = 0;
            while (index + 1 < lines.size())
            {
                if (isBlank(lines[index + 1]))
                {
                    block.emplace_back();
                    ++index;
                    continue;
                }
                const std::size_t indent = indentOf(lines[index + 1]);
                if (!blockIndent)
                    blockIndent = indent;
                if (indent < blockIndent)
                    break;
                block.push_back(lines[++index].substr(blockIndent));
            }
            SetDetailProperty(details, key, FoldBlock(block, raw.front() == '>', raw.size() > 1 && raw[1] == '+'));
            continue;
        }

        if (raw.front() == '"' || raw.front() == '\'')
        {
            std::string text = raw;
            while (!FlowScalarComplete(text, raw.front()) && index + 1 < lines.size())
            {
                text.push_back('\n');
                text += Trim(lines[++index]);
            }
            SetDetailProperty(details, key, DecodeFlowScalar(text, raw.front()));
            continue;
        }

        std::string plain = raw;
        while (index + 1 < lines.size() && !isBlank(lines[index + 1]) && indentOf(lines[index + 1]) && !isSequence(lines[index + 1]))
        {
            plain.push_back(' ');
            plain += Trim(lines[++index]);
        }
        SetDetailProperty(details, key, plain);
    }
    return Status::Ok();
}

MachineArchitecture CurrentMachineArchitecture()
{
    SYSTEM_INFO information{};
    GetNativeSystemInfo(&information);
    switch (information.wProcessorArchitecture)
    {
        case PROCESSOR_ARCHITECTURE_INTEL: return MachineArchitecture::X86;
        case PROCESSOR_ARCHITECTURE_AMD64: return MachineArchitecture::X64;
        case PROCESSOR_ARCHITECTURE_ARM: return MachineArchitecture::Arm;
        case PROCESSOR_ARCHITECTURE_ARM64: return MachineArchitecture::Arm64;
        default: return MachineArchitecture::Unknown;
    }
}

std::string ArchitectureName(MachineArchitecture architecture)
{
    switch (architecture)
    {
        case MachineArchitecture::X86: return "x86";
        case MachineArchitecture::X64: return "x64";
        case MachineArchitecture::Arm: return "arm";
        case MachineArchitecture::Arm64: return "arm64";
        default: return "unknown";
    }
}

Status SelectInstaller(const Manifest &manifest, const SelectionOptions &options, InstallerEntry &installer)
{
    return SelectInstallerForMachine(manifest, options, CurrentMachineArchitecture(), installer);
}

Status RunManifestSelfTests()
{
    static constexpr std::string_view Fixture =
        "PackageIdentifier: Example.Tool\n"
        "PackageVersion: 1.2.3\n"
        "InstallerType: exe\n"
        "InstallerSwitches:\n"
        "  Silent: /S\n"
        "Installers:\n"
        "- InstallerUrl: https://example.test/tool-x86.exe\n"
        "  InstallerSuccessCodes: [42]\n"
        "  ExpectedReturnCodes:\n"
        "  - InstallerReturnCode: 1602\n"
        "    ReturnResponse: cancelledByUser\n"
        "  Architecture: x86\n"
        "  InstallerSha256: 0000000000000000000000000000000000000000000000000000000000000000\n"
        "- InstallerSha256: 1111111111111111111111111111111111111111111111111111111111111111\n"
        "  UnsupportedOSArchitectures: [x86]\n"
        "  InstallerUrl: https://example.test/tool-arm64.exe\n"
        "  Architecture: arm64\n";
    Manifest manifest;
    Status status = ParseInstallerManifest(Fixture, manifest);
    if (!status) return status;
    if (manifest.id != "Example.Tool" || manifest.version != "1.2.3" || manifest.installers.size() != 2)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "manifest parser self-test failed");
    if (manifest.installers[1].type != "exe" || manifest.installers[1].switches.silent != "/S")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "manifest inheritance self-test failed");
    if (manifest.installers[0].architecture != "x86" || manifest.installers[0].successCodes != std::vector<DWORD>{42})
        return Status::Fail(ERROR_ASSERTION_FAILURE, "order-independent installer parser self-test failed");
    if (manifest.installers[0].expectedReturnCodes[1602] != "cancelledByUser")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "expected return code parser self-test failed");

    static constexpr std::string_view NestedTailFixture =
        "PackageIdentifier: Example.NestedTail\n"
        "PackageVersion: 1.0\n"
        "InstallerType: exe\n"
        "InstallerSwitches:\n"
        "  Silent: /S\n"
        "Installers:\n"
        "- Architecture: x86\n"
        "  InstallerUrl: https://example.test/x86.exe\n"
        "  InstallerSha256: 2222222222222222222222222222222222222222222222222222222222222222\n"
        "  InstallModes:\n"
        "  - silent\n"
        "- Architecture: x64\n"
        "  InstallerUrl: https://example.test/x64.exe\n"
        "  InstallerSha256: 3333333333333333333333333333333333333333333333333333333333333333\n";
    Manifest nestedTail;
    status = ParseInstallerManifest(NestedTailFixture, nestedTail);
    if (!status || nestedTail.installers.size() != 2 || nestedTail.installers[0].architecture != "x86" ||
        nestedTail.installers[1].architecture != "x64" || nestedTail.installers[1].url != "https://example.test/x64.exe")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "nested-list installer boundary self-test failed");

    SelectionOptions options;
    options.architecture = "arm64";
    InstallerEntry installer;
    status = SelectInstallerForMachine(manifest, options, MachineArchitecture::X64, installer);
    if (status)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "incompatible explicit architecture self-test failed");
    options.architecture = "x86";
    status = SelectInstallerForMachine(manifest, options, MachineArchitecture::X64, installer);
    if (!status || installer.architecture != "x86")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "compatible architecture selection self-test failed");
    options.scope = "machine";
    status = SelectInstallerForMachine(manifest, options, MachineArchitecture::X64, installer);
    if (status)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "unspecified installer scope self-test failed");
    options.scope.reset();
    options.locale = "en-US";
    status = SelectInstallerForMachine(manifest, options, MachineArchitecture::X64, installer);
    if (status)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "unspecified installer locale self-test failed");
    options.locale.reset();
    static constexpr std::string_view AgreementFixture =
        "PackageIdentifier: Example.Agreement\n"
        "PackageVersion: 1.0\n"
        "Agreements:\n"
        "- AgreementLabel: Terms of use\n"
        "  Agreement: |-\n"
        "    Read these terms before installing.\n"
        "  AgreementUrl: https://example.test/terms\n"
        "InstallerType: exe\n"
        "Architecture: x86\n"
        "InstallerUrl: https://example.test/agreement.exe\n"
        "InstallerSha256: 4444444444444444444444444444444444444444444444444444444444444444\n";
    Manifest agreements;
    status = ParseInstallerManifest(AgreementFixture, agreements);
    if (!status || !agreements.hasAgreements || agreements.agreements.size() != 1 ||
        agreements.agreements[0].label != "Terms of use" ||
        agreements.agreements[0].text != "Read these terms before installing." ||
        agreements.agreements[0].url != "https://example.test/terms")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "package agreement parser self-test failed");
    if (UrlEncodePath("manifests/a/A+B/1.0/A+B.yaml") != L"manifests/a/A%2BB/1.0/A%2BB.yaml")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "URL encoder self-test failed");
    return Status::Ok();
}

} // namespace rosget
