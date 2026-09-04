/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Read-only WinGet SQLite v2 package index reader
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#include "sqlite_index.hpp"
#include "util.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace rosget
{

struct SQLiteIndex::Value
{
    enum class Kind { Null, Integer, Text, Blob } kind = Kind::Null;
    std::int64_t integer = 0;
    std::string text;
    std::vector<std::uint8_t> blob;
};

namespace
{

std::uint16_t ReadBe16(const std::uint8_t *value)
{
    return static_cast<std::uint16_t>((value[0] << 8) | value[1]);
}

std::uint32_t ReadBe32(const std::uint8_t *value)
{
    return (static_cast<std::uint32_t>(value[0]) << 24) | (static_cast<std::uint32_t>(value[1]) << 16) |
           (static_cast<std::uint32_t>(value[2]) << 8) | value[3];
}

bool ReadVarint(const std::uint8_t *data, std::size_t size, std::size_t &offset, std::uint64_t &value)
{
    value = 0;
    for (unsigned index = 0; index < 9; ++index)
    {
        if (offset >= size)
            return false;
        const std::uint8_t byte = data[offset++];
        if (index == 8)
        {
            value = (value << 8) | byte;
            return true;
        }
        value = (value << 7) | (byte & 0x7f);
        if (!(byte & 0x80))
            return true;
    }
    return false;
}

std::int64_t ReadSignedBigEndian(const std::uint8_t *data, std::size_t size)
{
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < size; ++index)
        value = (value << 8) | data[index];
    if (size < sizeof(value) && size && (data[0] & 0x80))
        value |= (~std::uint64_t{0}) << (size * 8);
    return static_cast<std::int64_t>(value);
}

std::size_t SerialSize(std::uint64_t serial)
{
    switch (serial)
    {
        case 0: return 0;
        case 1: return 1;
        case 2: return 2;
        case 3: return 3;
        case 4: return 4;
        case 5: return 6;
        case 6: return 8;
        case 7: return 8;
        case 8: return 0;
        case 9: return 0;
        case 10:
        case 11: return std::numeric_limits<std::size_t>::max();
        default: return static_cast<std::size_t>((serial - (serial & 1 ? 13 : 12)) / 2);
    }
}

int MatchRank(const PackageRecord &package, const PackageQuery &query)
{
    const auto exactMatch = [&query](std::string_view value) { return value == query.text; };
    const auto equals = [&query](std::string_view value) { return AsciiEquals(value, query.text); };
    const auto startsWith = [&query](std::string_view value) { return AsciiStartsWith(value, query.text); };
    const auto contains = [&query](std::string_view value) { return AsciiContains(value, query.text); };
    const std::string_view selected = query.field == SearchField::Id ? std::string_view(package.id) :
                                      query.field == SearchField::Name ? std::string_view(package.name) :
                                      query.field == SearchField::Moniker ? std::string_view(package.moniker) :
                                      std::string_view();
    if (query.field != SearchField::Any)
    {
        if (query.exact)
            return exactMatch(selected) ? 0 : -1;
        if (equals(selected)) return 0;
        if (startsWith(selected)) return 1;
        return contains(selected) ? 2 : -1;
    }

    if (query.exact)
        return exactMatch(package.id) || exactMatch(package.name) || exactMatch(package.moniker) ? 0 : -1;
    if (query.text.empty()) return 5;
    if (AsciiEquals(package.id, query.text)) return 0;
    if (AsciiEquals(package.name, query.text) || AsciiEquals(package.moniker, query.text)) return 1;
    if (AsciiStartsWith(package.id, query.text)) return 2;
    if (AsciiStartsWith(package.name, query.text) || AsciiStartsWith(package.moniker, query.text)) return 3;
    if (AsciiContains(package.id, query.text)) return 4;
    return AsciiContains(package.name, query.text) || AsciiContains(package.moniker, query.text) ? 5 : -1;
}

int TextMatchRank(std::string_view value, const PackageQuery &query)
{
    if (query.exact) return value == query.text ? 0 : -1;
    if (AsciiEquals(value, query.text)) return 0;
    if (AsciiStartsWith(value, query.text)) return 1;
    return AsciiContains(value, query.text) ? 2 : -1;
}

} // namespace

Status SQLiteIndex::Open(std::wstring_view path)
{
    roots_.clear();
    schemas_.clear();
    Status status = ReadFileBytes(path, data_);
    if (!status)
        return status;
    static constexpr char Magic[] = "SQLite format 3\0";
    if (data_.size() < 100 || std::memcmp(data_.data(), Magic, sizeof(Magic) - 1) != 0)
        return Status::Fail(ERROR_BAD_FORMAT, "WinGet index is not a SQLite 3 database");

    pageSize_ = ReadBe16(data_.data() + 16);
    if (pageSize_ == 1)
        pageSize_ = 65536;
    if (pageSize_ < 512 || (pageSize_ & (pageSize_ - 1)) || data_.size() < pageSize_ || data_.size() % pageSize_ ||
        data_[18] != 1 || data_[19] != 1 || data_[20] >= pageSize_ ||
        data_[21] != 64 || data_[22] != 32 || data_[23] != 32 || ReadBe32(data_.data() + 56) != 1)
        return Status::Fail(ERROR_BAD_FORMAT, "WinGet index has an invalid page size");
    usableSize_ = pageSize_ - data_[20];
    pageCount_ = ReadBe32(data_.data() + 28);
    const std::uint32_t physicalPages = static_cast<std::uint32_t>(data_.size() / pageSize_);
    if (!pageCount_) pageCount_ = physicalPages;
    if (pageCount_ != physicalPages)
        return Status::Fail(ERROR_BAD_FORMAT, "WinGet index page count does not match its file size");
    return FindRoots();
}

const std::uint8_t *SQLiteIndex::Page(std::uint32_t pageNumber) const
{
    if (!pageNumber || pageNumber > pageCount_)
        return nullptr;
    const std::size_t offset = static_cast<std::size_t>(pageNumber - 1) * pageSize_;
    return offset + pageSize_ <= data_.size() ? data_.data() + offset : nullptr;
}

Status SQLiteIndex::FindRoots()
{
    Status status = ScanTable(1, [this](std::int64_t, const std::vector<Value> &values) {
        if (values.size() >= 5 && values[0].kind == Value::Kind::Text && values[0].text == "table" &&
            values[1].kind == Value::Kind::Text && values[3].kind == Value::Kind::Integer)
        {
            roots_[values[1].text] = static_cast<std::uint32_t>(values[3].integer);
            if (values[4].kind == Value::Kind::Text) schemas_[values[1].text] = values[4].text;
        }
        return Status::Ok();
    });
    if (!status)
        return status;
    if (!Root("packages"))
        return Status::Fail(ERROR_BAD_FORMAT, "WinGet index does not contain the packages table");
    return ValidateSchema();
}

Status SQLiteIndex::ValidateSchema()
{
    const auto packages = schemas_.find("packages");
    if (packages == schemas_.end())
        return Status::Fail(ERROR_BAD_FORMAT, "WinGet index packages table has no schema");
    const std::string schema = AsciiLower(packages->second);
    std::size_t position = 0;
    for (const std::string_view column : {std::string_view("rowid integer primary key"), std::string_view("[id] text not null"),
                                          std::string_view("[name] text not null"), std::string_view("[moniker] text"),
                                          std::string_view("[latest_version] text not null"), std::string_view("[arp_min_version] text"),
                                          std::string_view("[arp_max_version] text"), std::string_view("[hash] blob")})
    {
        position = schema.find(column, position);
        if (position == std::string::npos)
            return Status::Fail(ERROR_BAD_FORMAT, "WinGet packages table schema is not version 2 compatible");
        position += column.size();
    }

    const std::uint32_t metadataRoot = Root("metadata");
    if (!metadataRoot)
        return Status::Fail(ERROR_BAD_FORMAT, "WinGet index has no metadata table");
    std::map<std::string, std::string> metadata;
    Status status = ScanIndex(metadataRoot, [&metadata](std::int64_t, const std::vector<Value> &values) {
        if (values.size() >= 2 && values[0].kind == Value::Kind::Text && values[1].kind == Value::Kind::Text)
            metadata[values[0].text] = values[1].text;
        return Status::Ok();
    });
    if (metadata["majorVersion"] != "2" || metadata["minorVersion"] != "0" || metadata["databaseIdentifier"].empty())
        return Status::Fail(ERROR_NOT_SUPPORTED, "WinGet index metadata is not schema version 2.0");
    return Status::Ok();
}

std::uint32_t SQLiteIndex::Root(std::string_view table) const
{
    const auto entry = roots_.find(std::string(table));
    return entry == roots_.end() ? 0 : entry->second;
}

Status SQLiteIndex::ScanTable(std::uint32_t rootPage, const RowCallback &callback) const
{
    std::vector<bool> visited(static_cast<std::size_t>(pageCount_) + 1, false);
    return ScanTablePage(rootPage, callback, visited);
}

Status SQLiteIndex::ScanTablePage(std::uint32_t pageNumber, const RowCallback &callback, std::vector<bool> &visited) const
{
    const std::uint8_t *page = Page(pageNumber);
    if (!page || visited[pageNumber])
        return Status::Fail(ERROR_BAD_FORMAT, "WinGet index contains an invalid B-tree page reference");
    visited[pageNumber] = true;

    const std::size_t headerOffset = pageNumber == 1 ? 100 : 0;
    if (headerOffset + 12 > pageSize_)
        return Status::Fail(ERROR_BAD_FORMAT, "WinGet index B-tree header is truncated");
    const std::uint8_t pageType = page[headerOffset];
    const bool interior = pageType == 0x05;
    if (!interior && pageType != 0x0d)
        return Status::Fail(ERROR_BAD_FORMAT, "WinGet index table uses an unsupported B-tree page type");
    const std::uint16_t cellCount = ReadBe16(page + headerOffset + 3);
    const std::size_t headerSize = interior ? 12 : 8;
    if (headerOffset + headerSize + static_cast<std::size_t>(cellCount) * 2 > pageSize_)
        return Status::Fail(ERROR_BAD_FORMAT, "WinGet index B-tree cell array is truncated");

    for (std::uint16_t index = 0; index < cellCount; ++index)
    {
        const std::uint16_t cellOffset = ReadBe16(page + headerOffset + headerSize + index * 2);
        if (!cellOffset || cellOffset >= pageSize_)
            return Status::Fail(ERROR_BAD_FORMAT, "WinGet index B-tree cell points outside its page");
        if (interior)
        {
            if (static_cast<std::uint32_t>(cellOffset) + 4 > pageSize_)
                return Status::Fail(ERROR_BAD_FORMAT, "WinGet index interior cell is truncated");
            Status status = ScanTablePage(ReadBe32(page + cellOffset), callback, visited);
            if (!status)
                return status;
            continue;
        }

        std::size_t cursor = cellOffset;
        std::uint64_t payloadSize = 0;
        std::uint64_t rowid = 0;
        if (!ReadVarint(page, pageSize_, cursor, payloadSize) || !ReadVarint(page, pageSize_, cursor, rowid))
            return Status::Fail(ERROR_BAD_FORMAT, "WinGet index leaf cell has invalid varints");
        const std::uint32_t maximumLocal = usableSize_ - 35;
        const std::uint32_t minimumLocal = ((usableSize_ - 12) * 32 / 255) - 23;
        std::uint64_t localSize = payloadSize;
        if (payloadSize > maximumLocal)
        {
            const std::uint64_t candidate = minimumLocal + ((payloadSize - minimumLocal) % (usableSize_ - 4));
            localSize = candidate <= maximumLocal ? candidate : minimumLocal;
        }
        std::vector<std::uint8_t> payload;
        Status status = ReadPayload(page, cursor, payloadSize, localSize, payload);
        if (!status) return status;
        std::vector<Value> values;
        status = ParseRecord(payload.data(), payload.size(), values);
        if (!status)
            return status;
        status = callback(static_cast<std::int64_t>(rowid), values);
        if (!status)
            return status;
    }

    if (interior)
        return ScanTablePage(ReadBe32(page + headerOffset + 8), callback, visited);
    return Status::Ok();
}

Status SQLiteIndex::ScanIndex(std::uint32_t rootPage, const RowCallback &callback) const
{
    std::vector<bool> visited(static_cast<std::size_t>(pageCount_) + 1, false);
    return ScanIndexPage(rootPage, callback, visited);
}

Status SQLiteIndex::ScanIndexPage(std::uint32_t pageNumber, const RowCallback &callback, std::vector<bool> &visited) const
{
    const std::uint8_t *page = Page(pageNumber);
    if (!page || visited[pageNumber])
        return Status::Fail(ERROR_BAD_FORMAT, "WinGet index contains an invalid B-tree page reference");
    visited[pageNumber] = true;

    const std::size_t headerOffset = pageNumber == 1 ? 100 : 0;
    if (headerOffset + 12 > pageSize_)
        return Status::Fail(ERROR_BAD_FORMAT, "WinGet index B-tree header is truncated");
    const std::uint8_t pageType = page[headerOffset];
    const bool interior = pageType == 0x02;
    if (!interior && pageType != 0x0a)
        return Status::Fail(ERROR_BAD_FORMAT, "WinGet index table uses an unsupported B-tree page type");
    const std::uint16_t cellCount = ReadBe16(page + headerOffset + 3);
    const std::size_t headerSize = interior ? 12 : 8;
    if (headerOffset + headerSize + static_cast<std::size_t>(cellCount) * 2 > pageSize_)
        return Status::Fail(ERROR_BAD_FORMAT, "WinGet index B-tree cell array is truncated");

    const std::uint32_t maximumLocal = ((usableSize_ - 12) * 64 / 255) - 23;
    const std::uint32_t minimumLocal = ((usableSize_ - 12) * 32 / 255) - 23;
    for (std::uint16_t index = 0; index < cellCount; ++index)
    {
        const std::uint16_t cellOffset = ReadBe16(page + headerOffset + headerSize + index * 2);
        if (!cellOffset || cellOffset >= pageSize_)
            return Status::Fail(ERROR_BAD_FORMAT, "WinGet index B-tree cell points outside its page");
        std::size_t cursor = cellOffset;
        if (interior)
        {
            if (static_cast<std::uint32_t>(cellOffset) + 4 > pageSize_)
                return Status::Fail(ERROR_BAD_FORMAT, "WinGet index interior cell is truncated");
            Status status = ScanIndexPage(ReadBe32(page + cursor), callback, visited);
            if (!status)
                return status;
            cursor += 4;
        }

        std::uint64_t payloadSize = 0;
        if (!ReadVarint(page, pageSize_, cursor, payloadSize))
            return Status::Fail(ERROR_BAD_FORMAT, "WinGet index cell has an invalid payload size");
        std::uint64_t localSize = payloadSize;
        if (payloadSize > maximumLocal)
        {
            const std::uint64_t candidate = minimumLocal + ((payloadSize - minimumLocal) % (usableSize_ - 4));
            localSize = candidate <= maximumLocal ? candidate : minimumLocal;
        }
        std::vector<std::uint8_t> payload;
        Status status = ReadPayload(page, cursor, payloadSize, localSize, payload);
        if (!status) return status;
        std::vector<Value> values;
        status = ParseRecord(payload.data(), payload.size(), values);
        if (!status)
            return status;
        status = callback(0, values);
        if (!status)
            return status;
    }

    if (interior)
        return ScanIndexPage(ReadBe32(page + headerOffset + 8), callback, visited);
    return Status::Ok();
}

Status SQLiteIndex::ReadPayload(const std::uint8_t *page, std::size_t cursor, std::uint64_t payloadSize,
                                std::uint64_t localSize, std::vector<std::uint8_t> &payload) const
{
    if (payloadSize > std::numeric_limits<std::size_t>::max() || localSize > payloadSize ||
        cursor > usableSize_ || localSize > usableSize_ - cursor)
        return Status::Fail(ERROR_BAD_FORMAT, "WinGet index cell payload is truncated");
    payload.assign(page + cursor, page + cursor + static_cast<std::size_t>(localSize));
    if (localSize == payloadSize) return Status::Ok();
    if (usableSize_ - cursor - static_cast<std::size_t>(localSize) < 4)
        return Status::Fail(ERROR_BAD_FORMAT, "WinGet index overflow pointer is truncated");
    std::uint32_t overflowPage = ReadBe32(page + cursor + static_cast<std::size_t>(localSize));
    std::vector<bool> visited(static_cast<std::size_t>(pageCount_) + 1, false);
    std::size_t remaining = static_cast<std::size_t>(payloadSize - localSize);
    while (remaining)
    {
        const std::uint8_t *overflow = Page(overflowPage);
        if (!overflow || visited[overflowPage] || usableSize_ < 5)
            return Status::Fail(ERROR_BAD_FORMAT, "WinGet index has an invalid overflow page chain");
        visited[overflowPage] = true;
        const std::uint32_t next = ReadBe32(overflow);
        const std::size_t amount = std::min<std::size_t>(remaining, usableSize_ - 4);
        payload.insert(payload.end(), overflow + 4, overflow + 4 + amount);
        remaining -= amount;
        overflowPage = next;
    }
    if (overflowPage)
        return Status::Fail(ERROR_BAD_FORMAT, "WinGet index overflow page chain is longer than its payload");
    return Status::Ok();
}

Status SQLiteIndex::ParseRecord(const std::uint8_t *payload, std::size_t payloadSize, std::vector<Value> &values) const
{
    std::size_t cursor = 0;
    std::uint64_t headerSizeValue = 0;
    if (!ReadVarint(payload, payloadSize, cursor, headerSizeValue) || headerSizeValue > payloadSize || headerSizeValue < cursor)
        return Status::Fail(ERROR_BAD_FORMAT, "WinGet index record has an invalid header");
    const std::size_t headerSize = static_cast<std::size_t>(headerSizeValue);
    std::vector<std::uint64_t> serials;
    while (cursor < headerSize)
    {
        std::uint64_t serial = 0;
        if (!ReadVarint(payload, headerSize, cursor, serial))
            return Status::Fail(ERROR_BAD_FORMAT, "WinGet index record has an invalid serial type");
        serials.push_back(serial);
    }

    cursor = headerSize;
    values.clear();
    values.reserve(serials.size());
    for (const std::uint64_t serial : serials)
    {
        const std::size_t size = SerialSize(serial);
        if (size == std::numeric_limits<std::size_t>::max() || cursor + size > payloadSize)
            return Status::Fail(ERROR_BAD_FORMAT, "WinGet index record value is truncated");
        Value value;
        if (serial == 0)
        {
            value.kind = Value::Kind::Null;
        }
        else if (serial >= 1 && serial <= 6)
        {
            value.kind = Value::Kind::Integer;
            value.integer = ReadSignedBigEndian(payload + cursor, size);
        }
        else if (serial == 8 || serial == 9)
        {
            value.kind = Value::Kind::Integer;
            value.integer = serial - 8;
        }
        else if (serial >= 12 && !(serial & 1))
        {
            value.kind = Value::Kind::Blob;
            value.blob.assign(payload + cursor, payload + cursor + size);
        }
        else if (serial >= 13 && (serial & 1))
        {
            value.kind = Value::Kind::Text;
            value.text.assign(reinterpret_cast<const char *>(payload + cursor), size);
        }
        else
        {
            return Status::Fail(ERROR_NOT_SUPPORTED, "WinGet index contains an unsupported SQLite value");
        }
        cursor += size;
        values.push_back(std::move(value));
    }
    return Status::Ok();
}

Status SQLiteIndex::ForEachPackage(const std::function<Status(std::int64_t, PackageRecord &)> &callback) const
{
    const std::uint32_t root = Root("packages");
    if (!root)
        return Status::Fail(ERROR_INVALID_STATE, "WinGet index is not open");
    return ScanTable(root, [&callback](std::int64_t rowid, const std::vector<Value> &values) {
        if (values.size() < 8 || values[1].kind != Value::Kind::Text || values[2].kind != Value::Kind::Text || values[4].kind != Value::Kind::Text)
            return Status::Fail(ERROR_BAD_FORMAT, "WinGet packages row has an invalid schema");
        PackageRecord package;
        package.id = values[1].text;
        package.name = values[2].text;
        if (values[3].kind == Value::Kind::Text)
            package.moniker = values[3].text;
        package.version = values[4].text;
        if (values[7].kind != Value::Kind::Blob || values[7].blob.size() != package.manifestHash.size())
            return Status::Fail(ERROR_BAD_FORMAT, "WinGet package row has an invalid version-data hash");
        std::copy(values[7].blob.begin(), values[7].blob.end(), package.manifestHash.begin());
        return callback(rowid, package);
    });
}

Status SQLiteIndex::Search(const PackageQuery &query, std::vector<PackageRecord> &results) const
{
    struct RankedPackage { int rank; PackageRecord package; };
    std::vector<RankedPackage> ranked;
    if (query.field == SearchField::Tag || query.field == SearchField::Command)
    {
        Catalog catalog;
        Status status = LoadCatalog(catalog);
        if (!status) return status;
        const std::vector<std::string> &values = query.field == SearchField::Tag ? catalog.tags : catalog.commands;
        for (CatalogEntry &entry : catalog.entries)
        {
            const std::vector<std::uint32_t> &indexes = query.field == SearchField::Tag ? entry.tags : entry.commands;
            int rank = -1;
            for (const std::uint32_t index : indexes)
            {
                if (index >= values.size()) continue;
                const int candidate = TextMatchRank(values[index], query);
                if (candidate >= 0 && (rank < 0 || candidate < rank)) rank = candidate;
            }
            if (rank >= 0) ranked.push_back({rank, std::move(entry.package)});
        }
    }
    else
    {
        Status status = ForEachPackage([&ranked, &query](std::int64_t, PackageRecord &package) {
        const int rank = MatchRank(package, query);
        if (rank >= 0)
            ranked.push_back({rank, std::move(package)});
        return Status::Ok();
    });
        if (!status) return status;
    }
    std::sort(ranked.begin(), ranked.end(), [](const RankedPackage &left, const RankedPackage &right) {
        if (left.rank != right.rank) return left.rank < right.rank;
        return AsciiLower(left.package.id) < AsciiLower(right.package.id);
    });
    results.clear();
    const std::size_t count = std::min(query.count, ranked.size());
    results.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
        results.push_back(std::move(ranked[index].package));
    return Status::Ok();
}

Status SQLiteIndex::FindExactId(std::string_view id, PackageRecord &package) const
{
    PackageQuery query;
    query.text = std::string(id);
    query.field = SearchField::Id;
    query.exact = true;
    query.count = 2;
    std::vector<PackageRecord> results;
    Status status = Search(query, results);
    if (!status)
        return status;
    if (results.empty())
        return Status::Fail(ERROR_NOT_FOUND, "no package found matching " + std::string(id));
    package = std::move(results.front());
    return Status::Ok();
}

Status RunSQLiteIndexSelfTests()
{
    PackageRecord package;
    package.id = "7zip.7zip";
    package.name = "Seven Zip";
    package.moniker = "7z";
    package.version = "26.02";
    PackageQuery query;
    query.text = "7ZIP.7ZIP";
    query.field = SearchField::Id;
    query.exact = true;
    if (MatchRank(package, query) >= 0)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "exact search must be case-sensitive");
    query.exact = false;
    if (MatchRank(package, query) < 0)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "non-exact search must be case-insensitive");
    query.text = "Seven Zip";
    query.field = SearchField::Moniker;
    if (MatchRank(package, query) >= 0)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "moniker search escaped its selected field");
    query.field = SearchField::Name;
    if (MatchRank(package, query) < 0)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "name search did not inspect the name field");
    return Status::Ok();
}

Status SQLiteIndex::LoadCatalog(Catalog &catalog) const
{
    catalog.entries.clear();
    catalog.tags.clear();
    catalog.commands.clear();

    std::vector<std::uint32_t> packageSlots;
    Status status = ForEachPackage([&catalog, &packageSlots](std::int64_t rowid, PackageRecord &package) {
        if (rowid > 0 && rowid < 0x00100000)
        {
            if (packageSlots.size() <= static_cast<std::size_t>(rowid))
                packageSlots.resize(static_cast<std::size_t>(rowid) + 1, 0xffffffffu);
            packageSlots[static_cast<std::size_t>(rowid)] = static_cast<std::uint32_t>(catalog.entries.size());
        }
        CatalogEntry entry;
        entry.package = std::move(package);
        catalog.entries.push_back(std::move(entry));
        return Status::Ok();
    });
    if (!status)
        return status;

    const std::uint32_t tagsRoot = Root("tags2");
    const std::uint32_t mapRoot = Root("tags2_map");
    if (!tagsRoot || !mapRoot)
        return Status::Ok();

    std::vector<std::uint32_t> tagSlots;
    status = ScanTable(tagsRoot, [&catalog, &tagSlots](std::int64_t rowid, const std::vector<Value> &values) {
        if (values.size() < 2 || values[1].kind != Value::Kind::Text)
            return Status::Ok();
        if (rowid > 0 && rowid < 0x00100000)
        {
            if (tagSlots.size() <= static_cast<std::size_t>(rowid))
                tagSlots.resize(static_cast<std::size_t>(rowid) + 1, 0xffffffffu);
            tagSlots[static_cast<std::size_t>(rowid)] = static_cast<std::uint32_t>(catalog.tags.size());
        }
        catalog.tags.push_back(values[1].text);
        return Status::Ok();
    });
    if (!status)
        return status;

    status = ScanIndex(mapRoot, [&catalog, &tagSlots, &packageSlots](std::int64_t, const std::vector<Value> &values) {
        if (values.size() < 2 || values[0].kind != Value::Kind::Integer || values[1].kind != Value::Kind::Integer)
            return Status::Ok();
        const std::int64_t tagRow = values[0].integer;
        const std::int64_t packageRow = values[1].integer;
        if (tagRow < 0 || static_cast<std::size_t>(tagRow) >= tagSlots.size())
            return Status::Ok();
        if (packageRow < 0 || static_cast<std::size_t>(packageRow) >= packageSlots.size())
            return Status::Ok();
        const std::uint32_t tag = tagSlots[static_cast<std::size_t>(tagRow)];
        const std::uint32_t package = packageSlots[static_cast<std::size_t>(packageRow)];
        if (tag != 0xffffffffu && package != 0xffffffffu)
            catalog.entries[package].tags.push_back(tag);
        return Status::Ok();
    });
    if (!status) return status;

    const std::uint32_t commandsRoot = Root("commands2");
    const std::uint32_t commandsMapRoot = Root("commands2_map");
    if (!commandsRoot || !commandsMapRoot) return Status::Ok();
    std::vector<std::uint32_t> commandSlots;
    status = ScanTable(commandsRoot, [&catalog, &commandSlots](std::int64_t rowid, const std::vector<Value> &values) {
        if (values.size() < 2 || values[1].kind != Value::Kind::Text) return Status::Ok();
        if (rowid > 0 && rowid < 0x00100000)
        {
            if (commandSlots.size() <= static_cast<std::size_t>(rowid)) commandSlots.resize(static_cast<std::size_t>(rowid) + 1, 0xffffffffu);
            commandSlots[static_cast<std::size_t>(rowid)] = static_cast<std::uint32_t>(catalog.commands.size());
        }
        catalog.commands.push_back(values[1].text);
        return Status::Ok();
    });
    if (!status) return status;
    return ScanIndex(commandsMapRoot, [&catalog, &commandSlots, &packageSlots](std::int64_t, const std::vector<Value> &values) {
        if (values.size() < 2 || values[0].kind != Value::Kind::Integer || values[1].kind != Value::Kind::Integer) return Status::Ok();
        const std::int64_t commandRow = values[0].integer;
        const std::int64_t packageRow = values[1].integer;
        if (commandRow < 0 || static_cast<std::size_t>(commandRow) >= commandSlots.size() ||
            packageRow < 0 || static_cast<std::size_t>(packageRow) >= packageSlots.size()) return Status::Ok();
        const std::uint32_t command = commandSlots[static_cast<std::size_t>(commandRow)];
        const std::uint32_t package = packageSlots[static_cast<std::size_t>(packageRow)];
        if (command != 0xffffffffu && package != 0xffffffffu) catalog.entries[package].commands.push_back(command);
        return Status::Ok();
    });
}

} // namespace rosget
