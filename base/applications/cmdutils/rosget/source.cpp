/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WinGet community source access
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#define NOMINMAX
#include <windows.h>
#include <wininet.h>

#include "source.hpp"
#include "hash.hpp"
#include "source_trust.hpp"
#include "util.hpp"

#include "minizip/ioapi.h"
#include "minizip/iowin32.h"
#include "minizip/unzip.h"
#include "zlib.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>

namespace rosget
{

namespace
{

inline constexpr std::wstring_view CommunitySourceBase = L"https://cdn.winget.microsoft.com/cache/";
inline constexpr unsigned long long MaximumSourcePackageSize = 512ull * 1024 * 1024;

class CacheLock
{
public:
    Status Acquire()
    {
        handle_ = CreateMutexW(nullptr, FALSE, L"Local\\rosget-winget-source-cache-v1");
        if (!handle_)
        {
            const DWORD error = GetLastError();
            return Status::Fail(error, "cannot create source cache lock: " + WindowsErrorMessage(error));
        }
        const DWORD result = WaitForSingleObject(handle_, 30000);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
        {
            const DWORD error = result == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
            CloseHandle(handle_);
            handle_ = nullptr;
            return Status::Fail(error, "timed out waiting for the source cache lock");
        }
        acquired_ = true;
        return Status::Ok();
    }

    ~CacheLock()
    {
        if (acquired_) ReleaseMutex(handle_);
        if (handle_) CloseHandle(handle_);
    }

private:
    HANDLE handle_ = nullptr;
    bool acquired_ = false;
};

class ZipArchive
{
public:
    explicit ZipArchive(unzFile archive) : archive_(archive) {}
    ~ZipArchive() { if (archive_) unzClose(archive_); }
    unzFile Get() const { return archive_; }

private:
    unzFile archive_ = nullptr;
};

std::wstring UniquePath(std::wstring_view base, std::wstring_view suffix)
{
    static LONG sequence = 0;
    std::wstring result(base);
    result += suffix;
    result.push_back(L'.');
    result += std::to_wstring(GetCurrentProcessId());
    result.push_back(L'.');
    result += std::to_wstring(static_cast<unsigned long>(InterlockedIncrement(&sequence)));
    return result;
}

Status ReadArchiveEntry(unzFile archive, const char *name, std::size_t maximumSize, std::vector<std::uint8_t> &bytes)
{
    if (unzLocateFile(archive, name, 1) != UNZ_OK)
        return Status::Fail(ERROR_FILE_NOT_FOUND, "source MSIX does not contain " + std::string(name));
    unz_file_info64 information{};
    if (unzGetCurrentFileInfo64(archive, &information, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK ||
        information.uncompressed_size > maximumSize)
        return Status::Fail(ERROR_BAD_FORMAT, "source MSIX entry has an invalid size: " + std::string(name));
    if (unzOpenCurrentFile(archive) != UNZ_OK)
        return Status::Fail(ERROR_BAD_COMPRESSION_BUFFER, "cannot decompress source MSIX entry: " + std::string(name));

    bytes.clear();
    bytes.reserve(static_cast<std::size_t>(information.uncompressed_size));
    std::array<std::uint8_t, 64 * 1024> buffer{};
    int result = UNZ_OK;
    for (;;)
    {
        result = unzReadCurrentFile(archive, buffer.data(), static_cast<unsigned>(buffer.size()));
        if (result < 0) break;
        if (!result) break;
        if (bytes.size() > maximumSize - static_cast<std::size_t>(result))
        {
            result = UNZ_BADZIPFILE;
            break;
        }
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + result);
    }
    const int closeResult = unzCloseCurrentFile(archive);
    if (result < 0 || closeResult != UNZ_OK || bytes.size() != information.uncompressed_size)
        return Status::Fail(ERROR_CRC, "source MSIX entry failed decompression or CRC validation: " + std::string(name));
    return Status::Ok();
}

Status LoadSourcePackage(std::wstring_view packagePath, SourcePackageContents &contents)
{
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(std::wstring(packagePath).c_str(), GetFileExInfoStandard, &attributes))
    {
        const DWORD error = GetLastError();
        return Status::Fail(error, "cannot inspect the WinGet source package: " + WindowsErrorMessage(error));
    }
    const unsigned long long packageSize = (static_cast<unsigned long long>(attributes.nFileSizeHigh) << 32) |
                                           attributes.nFileSizeLow;
    if (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY || packageSize > MaximumSourcePackageSize)
        return Status::Fail(ERROR_FILE_TOO_LARGE, "the WinGet source package exceeds the 512 MiB limit");
    zlib_filefunc64_def functions{};
    fill_win32_filefunc64W(&functions);
    ZipArchive archive(unzOpen2_64(std::wstring(packagePath).c_str(), &functions));
    if (!archive.Get())
        return Status::Fail(ERROR_BAD_FORMAT, "the WinGet source is not a readable MSIX package");
    Status status = ReadArchiveEntry(archive.Get(), "AppxSignature.p7x", 1024 * 1024, contents.signature);
    if (!status) return status;
    status = ReadArchiveEntry(archive.Get(), "AppxBlockMap.xml", 8 * 1024 * 1024, contents.blockMap);
    if (!status) return status;
    status = ReadArchiveEntry(archive.Get(), "AppxManifest.xml", 4 * 1024 * 1024, contents.manifest);
    if (!status) return status;
    return ReadArchiveEntry(archive.Get(), "Public/index.db", 256 * 1024 * 1024, contents.index);
}

Status WriteTemporaryBytes(std::wstring_view destination, const std::vector<std::uint8_t> &bytes, std::wstring &temporary)
{
    Status status = EnsureDirectory(ParentPath(destination));
    if (!status) return status;
    temporary = UniquePath(destination, L".partial");
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        return Status::Fail(error, "cannot create source cache file: " + WindowsErrorMessage(error));
    }
    std::size_t offset = 0;
    while (offset < bytes.size())
    {
        const DWORD amount = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1024 * 1024));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + offset, amount, &written, nullptr) || written != amount)
        {
            const DWORD error = GetLastError();
            CloseHandle(file);
            DeleteFileW(temporary.c_str());
            return Status::Fail(error, "cannot write source cache file: " + WindowsErrorMessage(error));
        }
        offset += written;
    }
    if (!FlushFileBuffers(file))
    {
        const DWORD error = GetLastError();
        CloseHandle(file);
        DeleteFileW(temporary.c_str());
        return Status::Fail(error, "cannot flush source cache file: " + WindowsErrorMessage(error));
    }
    CloseHandle(file);
    return Status::Ok();
}

Status FinalizeTemporaryFile(std::wstring_view temporary, std::wstring_view destination)
{
    if (!MoveFileExW(std::wstring(temporary).c_str(), std::wstring(destination).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const DWORD error = GetLastError();
        DeleteFileW(std::wstring(temporary).c_str());
        return Status::Fail(error, "cannot finalize source cache file: " + WindowsErrorMessage(error));
    }
    return Status::Ok();
}

Status AtomicWriteBytes(std::wstring_view destination, const std::vector<std::uint8_t> &bytes)
{
    std::wstring temporary;
    Status status = WriteTemporaryBytes(destination, bytes, temporary);
    return status ? FinalizeTemporaryFile(temporary, destination) : status;
}

std::uint32_t ReadLe32(const std::uint8_t *value)
{
    return static_cast<std::uint32_t>(value[0]) | (static_cast<std::uint32_t>(value[1]) << 8) |
           (static_cast<std::uint32_t>(value[2]) << 16) | (static_cast<std::uint32_t>(value[3]) << 24);
}

std::uint64_t ReadLe64(const std::uint8_t *value)
{
    return static_cast<std::uint64_t>(ReadLe32(value)) | (static_cast<std::uint64_t>(ReadLe32(value + 4)) << 32);
}

Status DecompressMsZip(const std::vector<std::uint8_t> &input, std::string &output)
{
    if (input.size() >= 3 && input[0] == 's' && input[1] == 'V' && input[2] == ':')
    {
        output.assign(reinterpret_cast<const char *>(input.data()), input.size());
        return Status::Ok();
    }
    if (input.size() < 30 || input[28] != 'C' || input[29] != 'K')
        return Status::Fail(ERROR_BAD_FORMAT, "WinGet version metadata is not MSZIP YAML");
    const std::uint64_t expectedSize = ReadLe64(input.data() + 8);
    if (!expectedSize || expectedSize != ReadLe64(input.data() + 16) || expectedSize > 16 * 1024 * 1024)
        return Status::Fail(ERROR_BAD_FORMAT, "WinGet version metadata has an invalid expanded size");

    std::vector<std::uint8_t> expanded;
    expanded.reserve(static_cast<std::size_t>(expectedSize));
    std::size_t offset = 24;
    while (offset < input.size())
    {
        if (input.size() - offset < 6)
            return Status::Fail(ERROR_BAD_FORMAT, "WinGet MSZIP chunk header is truncated");
        const std::uint32_t chunkSize = ReadLe32(input.data() + offset);
        if (chunkSize < 2 || chunkSize > input.size() - offset - 4 || input[offset + 4] != 'C' || input[offset + 5] != 'K')
            return Status::Fail(ERROR_BAD_FORMAT, "WinGet MSZIP chunk has an invalid CK header or size");

        z_stream stream{};
        stream.next_in = const_cast<Bytef *>(input.data() + offset + 6);
        stream.avail_in = chunkSize - 2;
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
            return Status::Fail(ERROR_BAD_COMPRESSION_BUFFER, "cannot initialize MSZIP decompression");
        if (!expanded.empty())
        {
            const uInt dictionarySize = static_cast<uInt>(std::min<std::size_t>(32768, expanded.size()));
            inflateSetDictionary(&stream, expanded.data() + expanded.size() - dictionarySize, dictionarySize);
        }
        int result = Z_OK;
        std::array<std::uint8_t, 64 * 1024> buffer{};
        while (result == Z_OK)
        {
            stream.next_out = buffer.data();
            stream.avail_out = static_cast<uInt>(buffer.size());
            result = inflate(&stream, Z_FINISH);
            const std::size_t produced = buffer.size() - stream.avail_out;
            if (expanded.size() > static_cast<std::size_t>(expectedSize) - std::min<std::size_t>(produced, expectedSize))
            {
                inflateEnd(&stream);
                return Status::Fail(ERROR_BAD_COMPRESSION_BUFFER, "WinGet MSZIP output exceeds its declared size");
            }
            expanded.insert(expanded.end(), buffer.begin(), buffer.begin() + produced);
        }
        const bool valid = result == Z_STREAM_END && stream.avail_in == 0;
        inflateEnd(&stream);
        if (!valid)
            return Status::Fail(ERROR_BAD_COMPRESSION_BUFFER, "WinGet MSZIP chunk decompression failed");
        offset += 4 + chunkSize;
    }
    if (expanded.size() != expectedSize)
        return Status::Fail(ERROR_BAD_COMPRESSION_BUFFER, "WinGet MSZIP expanded size does not match its header");
    output.assign(reinterpret_cast<const char *>(expanded.data()), expanded.size());
    return Status::Ok();
}

struct VersionDataRecord
{
    std::string version;
    std::string relativePath;
    std::string sha256;
};

Status ParseVersionData(std::string_view yaml, std::string_view wantedVersion, VersionDataRecord &wanted)
{
    bool inVersions = false;
    VersionDataRecord *current = nullptr;
    std::vector<VersionDataRecord> records;
    std::size_t offset = 0;
    while (offset <= yaml.size())
    {
        const std::size_t end = yaml.find('\n', offset);
        std::string line = Trim(yaml.substr(offset, end == std::string_view::npos ? yaml.size() - offset : end - offset));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        offset = end == std::string_view::npos ? yaml.size() + 1 : end + 1;
        if (line.empty()) continue;
        if (line == "vD:")
        {
            inVersions = true;
            continue;
        }
        if (!inVersions) continue;
        if (line.rfind("- ", 0) == 0)
        {
            records.emplace_back();
            current = &records.back();
            line.erase(0, 2);
        }
        if (!current) continue;
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = Trim(std::string_view(line).substr(0, colon));
        const std::string value = UnquoteYaml(std::string_view(line).substr(colon + 1));
        if (key == "v") current->version = value;
        else if (key == "rP") current->relativePath = value;
        else if (key == "s256H") current->sha256 = value;
    }
    const auto match = std::find_if(records.begin(), records.end(), [wantedVersion](const VersionDataRecord &record) {
        return record.version == wantedVersion;
    });
    if (match == records.end())
        return Status::Fail(ERROR_NOT_FOUND, "source version metadata does not contain package version " + std::string(wantedVersion));
    wanted = *match;
    std::array<std::uint8_t, 32> hash{};
    if (!ParseHex256(wanted.sha256, hash) || wanted.relativePath.rfind("manifests/", 0) != 0 ||
        wanted.relativePath.find("..") != std::string::npos || wanted.relativePath.find('\\') != std::string::npos ||
        wanted.relativePath.find(':') != std::string::npos || wanted.relativePath.find('?') != std::string::npos)
        return Status::Fail(ERROR_BAD_FORMAT, "source version metadata contains an invalid manifest path or hash");
    return Status::Ok();
}

bool ValidPackageIdentifier(std::string_view id)
{
    return !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char character) {
        return std::isalnum(character) || character == '.' || character == '-' || character == '_';
    });
}

} // namespace

SourceManager::SourceManager()
{
    cacheDirectory_ = LocalCacheDirectory();
    packagePath_ = JoinPath(cacheDirectory_, L"source2.msix");
    indexPath_ = JoinPath(cacheDirectory_, L"index.db");
}

Status SourceManager::ActivateSource(std::wstring_view sourcePackage, bool copyPackage)
{
    SourcePackageContents contents;
    Status status = LoadSourcePackage(sourcePackage, contents);
    if (!status) return status;
    status = VerifySourcePackageTrust(contents);
    if (!status) return status;

    std::wstring indexTemporary;
    status = WriteTemporaryBytes(indexPath_, contents.index, indexTemporary);
    if (!status) return status;
    SQLiteIndex validatedIndex;
    status = validatedIndex.Open(indexTemporary);
    if (!status)
    {
        DeleteFileW(indexTemporary.c_str());
        return status;
    }

    if (copyPackage)
    {
        std::vector<std::uint8_t> packageBytes;
        status = ReadFileBytes(sourcePackage, packageBytes, static_cast<std::size_t>(MaximumSourcePackageSize));
        if (!status)
        {
            DeleteFileW(indexTemporary.c_str());
            return status;
        }
        status = AtomicWriteBytes(packagePath_, packageBytes);
        if (!status)
        {
            DeleteFileW(indexTemporary.c_str());
            return status;
        }
    }
    status = FinalizeTemporaryFile(indexTemporary, indexPath_);
    if (!status) return status;
    index_ = std::move(validatedIndex);
    ready_ = true;
    return Status::Ok();
}

Status SourceManager::UpdateUnlocked()
{
    ready_ = false;
    Status status = EnsureDirectory(cacheDirectory_);
    if (!status) return status;
    const std::wstring downloaded = UniquePath(packagePath_, L".download");
    std::printf("Updating source: winget\n");
    status = http_.Download(CommunitySourceUrl, downloaded, {}, MaximumSourcePackageSize);
    if (!status) return status;
    status = ActivateSource(downloaded, true);
    DeleteFileW(downloaded.c_str());
    if (!status) return status;
    std::printf("Source signature, block map, and index verified.\n");
    std::printf("Source updated successfully.\n");
    return Status::Ok();
}

Status SourceManager::Update()
{
    CacheLock lock;
    Status status = lock.Acquire();
    return status ? UpdateUnlocked() : status;
}

Status SourceManager::Import(std::wstring_view packagePath)
{
    CacheLock lock;
    Status status = lock.Acquire();
    if (!status) return status;
    ready_ = false;
    const bool copy = _wcsicmp(std::wstring(packagePath).c_str(), packagePath_.c_str()) != 0;
    status = ActivateSource(packagePath, copy);
    if (!status) return status;
    std::printf("Source signature, block map, and index verified.\n");
    std::printf("Source imported successfully.\n");
    return Status::Ok();
}

Status SourceManager::EnsureReady()
{
    if (ready_) return Status::Ok();
    CacheLock lock;
    Status status = lock.Acquire();
    if (!status) return status;
    if (GetFileAttributesW(packagePath_.c_str()) == INVALID_FILE_ATTRIBUTES)
        return UpdateUnlocked();
    status = ActivateSource(packagePath_, false);
    if (!status)
    {
        std::fprintf(stderr, "rosget: cached source failed trust validation; refreshing it\n");
        return UpdateUnlocked();
    }
    return Status::Ok();
}

Status SourceManager::Search(const PackageQuery &query, std::vector<PackageRecord> &results)
{
    Status status = EnsureReady();
    return status ? index_.Search(query, results) : status;
}

Status SourceManager::Resolve(const PackageQuery &query, PackageRecord &package)
{
    Status status = EnsureReady();
    if (!status) return status;
    PackageQuery packageQuery = query;
    packageQuery.count = 2;
    std::vector<PackageRecord> results;
    status = index_.Search(packageQuery, results);
    if (!status) return status;
    if (results.empty())
        return Status::Fail(ERROR_NOT_FOUND, "no package found matching " + query.text);
    if (results.size() > 1 && query.field == SearchField::Any &&
        results[0].id != query.text && results[0].name != query.text && results[0].moniker != query.text)
        return Status::Fail(ERROR_MORE_DATA, "multiple packages match; use --id with an exact package identifier");
    package = std::move(results.front());
    return Status::Ok();
}

Status SourceManager::FetchInstallerManifest(const PackageRecord &package, std::string &yaml)
{
    Status status = EnsureReady();
    if (!status) return status;
    if (!ValidPackageIdentifier(package.id))
        return Status::Fail(ERROR_BAD_FORMAT, "source index contains an invalid package identifier");
    if (std::all_of(package.manifestHash.begin(), package.manifestHash.end(), [](std::uint8_t value) { return value == 0; }))
        return Status::Fail(ERROR_BAD_FORMAT, "source index package record has no version-data hash");

    const std::string versionDataHash = HexLower(package.manifestHash.data(), package.manifestHash.size());
    const std::wstring versionDataUrl = std::wstring(CommunitySourceBase) + L"packages/" + UrlEncodePath(package.id) +
                                        L"/" + WideFromUtf8(versionDataHash.substr(0, 8)) + L"/versionData.mszyml";
    HttpResponse response;
    status = http_.Get(versionDataUrl, {}, response);
    if (!status) return status;
    if (response.statusCode != HTTP_STATUS_OK)
        return Status::Fail(ERROR_INVALID_DATA, "version metadata service returned status " + std::to_string(response.statusCode));
    std::array<std::uint8_t, 32> actualVersionDataHash{};
    status = HashBytesSha256(response.body.data(), response.body.size(), actualVersionDataHash);
    if (!status) return status;
    if (actualVersionDataHash != package.manifestHash)
        return Status::Fail(ERROR_CRC, "downloaded version metadata does not match the signed source index hash");

    std::string versionDataYaml;
    status = DecompressMsZip(response.body, versionDataYaml);
    if (!status) return status;
    VersionDataRecord version;
    status = ParseVersionData(versionDataYaml, package.version, version);
    if (!status) return status;

    const std::wstring manifestUrl = std::wstring(CommunitySourceBase) + UrlEncodePath(version.relativePath);
    response = {};
    status = http_.Get(manifestUrl, {}, response);
    if (!status) return status;
    if (response.statusCode != HTTP_STATUS_OK)
        return Status::Fail(ERROR_INVALID_DATA, "manifest service returned status " + std::to_string(response.statusCode));
    std::array<std::uint8_t, 32> expectedManifestHash{};
    std::array<std::uint8_t, 32> actualManifestHash{};
    if (!ParseHex256(version.sha256, expectedManifestHash))
        return Status::Fail(ERROR_BAD_FORMAT, "version metadata manifest hash is invalid");
    status = HashBytesSha256(response.body.data(), response.body.size(), actualManifestHash);
    if (!status) return status;
    if (actualManifestHash != expectedManifestHash)
        return Status::Fail(ERROR_CRC, "downloaded manifest does not match the signed version metadata hash");
    yaml.assign(reinterpret_cast<const char *>(response.body.data()), response.body.size());
    return Status::Ok();
}

} // namespace rosget
