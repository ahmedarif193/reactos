/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Read-only WinGet SQLite v2 package index reader
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#include "model.hpp"

#include <functional>

namespace rosget
{

enum class SearchField
{
    Any,
    Id,
    Name,
    Moniker,
    Tag,
    Command,
};

struct PackageQuery
{
    std::string text;
    SearchField field = SearchField::Any;
    bool exact = false;
    std::size_t count = 20;
};

class SQLiteIndex
{
public:
    Status Open(std::wstring_view path);
    Status Search(const PackageQuery &query, std::vector<PackageRecord> &results) const;
    Status FindExactId(std::string_view id, PackageRecord &package) const;
    Status LoadCatalog(Catalog &catalog) const;

private:
    struct Value;
    using RowCallback = std::function<Status(std::int64_t, const std::vector<Value> &)>;

    Status FindRoots();
    Status ValidateSchema();
    std::uint32_t Root(std::string_view table) const;
    Status ScanTable(std::uint32_t rootPage, const RowCallback &callback) const;
    Status ScanTablePage(std::uint32_t pageNumber, const RowCallback &callback, std::vector<bool> &visited) const;
    Status ScanIndex(std::uint32_t rootPage, const RowCallback &callback) const;
    Status ScanIndexPage(std::uint32_t pageNumber, const RowCallback &callback, std::vector<bool> &visited) const;
    Status ParseRecord(const std::uint8_t *payload, std::size_t payloadSize, std::vector<Value> &values) const;
    Status ReadPayload(const std::uint8_t *page, std::size_t cursor, std::uint64_t payloadSize,
                       std::uint64_t localSize, std::vector<std::uint8_t> &payload) const;
    Status ForEachPackage(const std::function<Status(std::int64_t, PackageRecord &)> &callback) const;
    const std::uint8_t *Page(std::uint32_t pageNumber) const;

    std::vector<std::uint8_t> data_;
    std::map<std::string, std::uint32_t> roots_;
    std::map<std::string, std::string> schemas_;
    std::uint32_t pageSize_ = 0;
    std::uint32_t usableSize_ = 0;
    std::uint32_t pageCount_ = 0;
};

Status RunSQLiteIndexSelfTests();

} // namespace rosget
