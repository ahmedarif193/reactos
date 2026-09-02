/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     rosget package catalog window
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include "category.hpp"
#include "details.hpp"
#include "gui.hpp"
#include "http.hpp"
#include "installed.hpp"
#include "installer.hpp"
#include "manifest.hpp"
#include "resource.h"
#include "util.hpp"

#include <algorithm>
#include <memory>

namespace rosget
{

namespace
{

enum : UINT
{
    MessageCatalogReady = WM_APP + 1,
    MessageDetailsReady = WM_APP + 2,
    MessageOperationUpdate = WM_APP + 3,
};

enum : int
{
    IdSearch = 1001,
    IdList = 1002,
    IdTree = 1003,
    IdInfo = 1004,
    IdStatus = 1005,
    IdInstall = 1006,
    IdDownload = 1007,
    IdRefresh = 1008,
    IdAppTitle = 1009,
    IdAppSubtitle = 1010,
    IdOperationTitle = 1011,
    IdOperationDetail = 1012,
    IdOperationProgress = 1013,
    IdOperationPercent = 1014,
    IdInstalled = 1015,
};

enum : UINT_PTR
{
    TimerSearch = 1,
    TimerSelect = 2,
    TimerSpinner = 3,
    TimerSize = 4,
};

enum class BlockKind
{
    Title,
    Subtitle,
    Chips,
    Section,
    Body,
    Field,
    Link,
    Status,
};

struct InfoBlock
{
    BlockKind kind = BlockKind::Body;
    std::wstring label;
    std::wstring text;
    std::vector<std::wstring> chips;
    std::vector<COLORREF> chipAccents;
    COLORREF accent = 0;
    bool useAccent = false;
    bool spinner = false;
};

struct GuiPackage
{
    std::wstring name;
    std::wstring id;
    std::wstring version;
    std::wstring tagText;
    std::string utf8Id;
    std::string utf8Name;
    std::string utf8Version;
    std::string searchKey;
    std::string sortKey;
    std::array<std::uint8_t, 32> manifestHash{};
    std::uint32_t categories = 0;
};

struct PreparedCatalog
{
    std::vector<GuiPackage> packages;
    std::vector<std::uint32_t> counts;
};

struct CatalogResult
{
    bool ok = false;
    std::string error;
    PreparedCatalog catalog;
};

struct DetailResult
{
    std::uint32_t generation = 0;
    int stage = 0;
    bool ok = false;
    std::string error;
    PackageDetails details;
    std::vector<PackageAgreement> agreements;
    std::vector<std::string> installers;
    std::string selected;
    std::string selectedArchitecture;
    std::string selectedError;
    std::string installerUrl;
    std::string installerHash;
    bool sizePending = false;
    bool sizeKnown = false;
    unsigned long long installerSize = 0;
    std::string sizeError;
};

enum class OperationStage
{
    Preparing,
    Downloading,
    Verifying,
    Installing,
    Complete,
    Failed,
};

struct OperationUpdate
{
    std::uint32_t generation = 0;
    OperationStage stage = OperationStage::Preparing;
    bool install = false;
    unsigned long long received = 0;
    unsigned long long total = 0;
    DWORD exitCode = 0;
    std::wstring packageName;
    std::wstring path;
    std::string error;
};

COLORREF Blend(COLORREF front, COLORREF back, int weight)
{
    const int red = (GetRValue(front) * weight + GetRValue(back) * (255 - weight)) / 255;
    const int green = (GetGValue(front) * weight + GetGValue(back) * (255 - weight)) / 255;
    const int blue = (GetBValue(front) * weight + GetBValue(back) * (255 - weight)) / 255;
    return RGB(red, green, blue);
}

std::wstring FormatCount(unsigned long long value)
{
    std::wstring digits = std::to_wstring(value);
    std::wstring grouped;
    for (std::size_t index = 0; index < digits.size(); ++index)
    {
        if (index && !((digits.size() - index) % 3))
            grouped.push_back(L',');
        grouped.push_back(digits[index]);
    }
    return grouped;
}

std::wstring FormatByteCount(unsigned long long value)
{
    static const wchar_t *Units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    unsigned unit = 0;
    unsigned long long divisor = 1;
    while (unit + 1 < sizeof(Units) / sizeof(Units[0]) && value / divisor >= 1024)
    {
        divisor *= 1024;
        ++unit;
    }
    if (!unit) return std::to_wstring(value) + L" B";
    const unsigned long long whole = value / divisor;
    const unsigned long long tenth = (value % divisor) * 10 / divisor;
    return std::to_wstring(whole) + L"." + std::to_wstring(tenth) + L" " + Units[unit];
}

unsigned ScaleProgress(unsigned long long received, unsigned long long total, unsigned scale)
{
    if (!total)
        return 0;
    if (received >= total)
        return scale;
    return static_cast<unsigned>((static_cast<long double>(received) * scale) / total);
}

std::wstring JoinTags(const std::vector<std::uint32_t> &tags, const std::vector<std::wstring> &wide,
                      const std::vector<std::string> &lower, std::string &searchKey)
{
    std::wstring text;
    for (const std::uint32_t tag : tags)
    {
        if (tag >= wide.size())
            continue;
        if (!text.empty())
            text += L", ";
        text += wide[tag];
        searchKey.push_back('\n');
        searchKey += lower[tag];
    }
    return text;
}

PreparedCatalog PrepareCatalog(const Catalog &catalog)
{
    PreparedCatalog prepared;
    prepared.counts.assign(CategoryCount(), 0);
    prepared.packages.reserve(catalog.entries.size());
    const CategoryClassifier classifier;

    std::vector<std::wstring> wideTags;
    std::vector<std::string> loweredTags;
    wideTags.reserve(catalog.tags.size());
    loweredTags.reserve(catalog.tags.size());
    for (const std::string &tag : catalog.tags)
    {
        wideTags.push_back(WideFromUtf8(tag));
        loweredTags.push_back(AsciiLower(tag));
    }
    for (const CatalogEntry &entry : catalog.entries)
    {
        GuiPackage package;
        package.utf8Id = entry.package.id;
        package.utf8Name = entry.package.name;
        package.utf8Version = entry.package.version;
        package.manifestHash = entry.package.manifestHash;
        package.name = WideFromUtf8(entry.package.name);
        package.id = WideFromUtf8(entry.package.id);
        package.version = WideFromUtf8(entry.package.version);
        package.searchKey = AsciiLower(entry.package.id);
        package.searchKey.push_back('\n');
        package.searchKey += AsciiLower(entry.package.name);
        if (!entry.package.moniker.empty())
        {
            package.searchKey.push_back('\n');
            package.searchKey += AsciiLower(entry.package.moniker);
        }
        package.tagText = JoinTags(entry.tags, wideTags, loweredTags, package.searchKey);
        package.sortKey = AsciiLower(entry.package.name);
        package.categories = entry.tags.empty() ? classifier.ClassifyText(package.searchKey)
                                                : classifier.Classify(entry.tags, loweredTags);
        for (std::size_t index = 0; index < prepared.counts.size(); ++index)
        {
            if (package.categories & (1u << index))
                ++prepared.counts[index];
        }
        prepared.packages.push_back(std::move(package));
    }
    return prepared;
}

int RankPackage(const GuiPackage &package, const std::string &query)
{
    if (query.empty())
        return 0;
    const std::size_t split = package.searchKey.find('\n');
    const std::string id = package.searchKey.substr(0, split);
    const std::size_t nameEnd = package.searchKey.find('\n', split + 1);
    const std::string name = package.searchKey.substr(split + 1, nameEnd == std::string::npos ? std::string::npos : nameEnd - split - 1);
    if (id == query || name == query)
        return 0;
    if (name.rfind(query, 0) == 0)
        return 1;
    if (id.rfind(query, 0) == 0)
        return 2;
    if (name.find(query) != std::string::npos)
        return 3;
    if (id.find(query) != std::string::npos)
        return 4;
    return 5;
}

class Worker
{
public:
    ~Worker() { Stop(); }

    bool Start(HWND window, SourceManager *source)
    {
        window_ = window;
        source_ = source;
        InitializeCriticalSection(&lock_);
        signal_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        probeStop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!signal_ || !probeStop_)
        {
            if (signal_)
                CloseHandle(signal_);
            if (probeStop_)
                CloseHandle(probeStop_);
            signal_ = nullptr;
            probeStop_ = nullptr;
            DeleteCriticalSection(&lock_);
            return false;
        }
        thread_ = CreateThread(nullptr, 0, ThreadMain, this, 0, nullptr);
        if (!thread_)
        {
            CloseHandle(signal_);
            CloseHandle(probeStop_);
            signal_ = nullptr;
            probeStop_ = nullptr;
            DeleteCriticalSection(&lock_);
            return false;
        }
        return true;
    }

    void Stop()
    {
        if (!signal_)
            return;
        EnterCriticalSection(&lock_);
        quit_ = true;
        LeaveCriticalSection(&lock_);
        SetEvent(probeStop_);
        SetEvent(signal_);
        if (thread_)
        {
            WaitForSingleObject(thread_, INFINITE);
            CloseHandle(thread_);
            thread_ = nullptr;
        }
        CloseHandle(signal_);
        signal_ = nullptr;
        CloseHandle(probeStop_);
        probeStop_ = nullptr;
        DeleteCriticalSection(&lock_);
    }

    void RequestCatalog(bool refresh)
    {
        EnterCriticalSection(&lock_);
        catalogPending_ = true;
        catalogRefresh_ = catalogRefresh_ || refresh;
        LeaveCriticalSection(&lock_);
        SetEvent(signal_);
    }

    std::uint32_t RequestDetails(const PackageRecord &package)
    {
        const std::uint32_t generation = static_cast<std::uint32_t>(InterlockedIncrement(&generation_));
        EnterCriticalSection(&lock_);
        detailsPending_ = true;
        pending_ = package;
        pendingGeneration_ = generation;
        LeaveCriticalSection(&lock_);
        SetEvent(signal_);
        return generation;
    }

    void CancelDetails() { InterlockedIncrement(&generation_); }

    std::uint32_t RequestOperation(const PackageRecord &package, bool install)
    {
        const std::uint32_t generation = static_cast<std::uint32_t>(InterlockedIncrement(&operationGeneration_));
        EnterCriticalSection(&lock_);
        operationPending_ = true;
        operationPackage_ = package;
        operationInstall_ = install;
        pendingOperationGeneration_ = generation;
        LeaveCriticalSection(&lock_);
        SetEvent(signal_);
        return generation;
    }

private:
    struct SizeProbeContext
    {
        HWND window = nullptr;
        HANDLE stop = nullptr;
        std::unique_ptr<DetailResult> result;
    };

    static DWORD WINAPI ThreadMain(LPVOID parameter)
    {
        static_cast<Worker *>(parameter)->Run();
        return 0;
    }

    static DWORD WINAPI SizeProbeMain(LPVOID parameter)
    {
        std::unique_ptr<SizeProbeContext> context(static_cast<SizeProbeContext *>(parameter));
        unsigned long long installerSize = 0;
        HttpClient http;
        const Status status = http.ProbeContentLength(WideFromUtf8(context->result->installerUrl), installerSize);
        context->result->stage = 3;
        context->result->sizePending = false;
        if (status)
        {
            context->result->sizeKnown = true;
            context->result->installerSize = installerSize;
        }
        else
        {
            context->result->sizeError = status.message;
        }

        if (WaitForSingleObject(context->stop, 0) != WAIT_OBJECT_0 &&
            PostMessageW(context->window, MessageDetailsReady, context->result->generation,
                         reinterpret_cast<LPARAM>(context->result.get())))
        {
            context->result.release();
        }
        CloseHandle(context->stop);
        return 0;
    }

    void StartSizeProbe(std::unique_ptr<DetailResult> result)
    {
        HANDLE stop = nullptr;
        if (!DuplicateHandle(GetCurrentProcess(), probeStop_, GetCurrentProcess(), &stop, 0, FALSE,
                             DUPLICATE_SAME_ACCESS))
        {
            result->stage = 3;
            result->sizePending = false;
            result->sizeError = "cannot start installer size query";
            PostDetails(result);
            return;
        }

        std::unique_ptr<SizeProbeContext> context(new SizeProbeContext());
        context->window = window_;
        context->stop = stop;
        context->result = std::move(result);
        HANDLE thread = CreateThread(nullptr, 0, SizeProbeMain, context.get(), 0, nullptr);
        if (!thread)
        {
            CloseHandle(stop);
            context->result->stage = 3;
            context->result->sizePending = false;
            context->result->sizeError = "cannot start installer size query";
            PostDetails(context->result);
            return;
        }
        context.release();
        CloseHandle(thread);
    }

    std::uint32_t Generation() { return static_cast<std::uint32_t>(InterlockedCompareExchange(&generation_, 0, 0)); }

    void Run()
    {
        for (;;)
        {
            WaitForSingleObject(signal_, INFINITE);
            for (;;)
            {
                bool catalog = false;
                bool refresh = false;
                bool details = false;
                bool operation = false;
                bool install = false;
                PackageRecord package;
                std::uint32_t generation = 0;
                EnterCriticalSection(&lock_);
                const bool quit = quit_;
                if (operationPending_)
                {
                    operationPending_ = false;
                    package = operationPackage_;
                    install = operationInstall_;
                    generation = pendingOperationGeneration_;
                    operation = true;
                }
                else if (catalogPending_)
                {
                    catalogPending_ = false;
                    refresh = catalogRefresh_;
                    catalogRefresh_ = false;
                    catalog = true;
                }
                else if (detailsPending_)
                {
                    detailsPending_ = false;
                    package = pending_;
                    generation = pendingGeneration_;
                    details = true;
                }
                LeaveCriticalSection(&lock_);
                if (quit)
                    return;
                if (operation)
                    RunOperationJob(package, install, generation);
                else if (catalog)
                    RunCatalogJob(refresh);
                else if (details)
                    RunDetailsJob(package, generation);
                else
                    break;
            }
        }
    }

    void RunCatalogJob(bool refresh)
    {
        std::unique_ptr<CatalogResult> result(new CatalogResult());
        Catalog catalog;
        Status status = refresh ? source_->Update() : source_->EnsureReady();
        if (status)
        {
            SQLiteIndex index;
            status = index.Open(source_->IndexPath());
            if (status)
                status = index.LoadCatalog(catalog);
        }
        if (!status)
        {
            result->error = status.message;
        }
        else
        {
            result->catalog = PrepareCatalog(catalog);
            result->ok = true;
        }
        if (PostMessageW(window_, MessageCatalogReady, 0, reinterpret_cast<LPARAM>(result.get())))
            result.release();
    }

    void PostDetails(std::unique_ptr<DetailResult> &result)
    {
        std::unique_ptr<DetailResult> copy(new DetailResult(*result));
        if (PostMessageW(window_, MessageDetailsReady, copy->generation, reinterpret_cast<LPARAM>(copy.get())))
            copy.release();
    }

    void PostOperation(const OperationUpdate &update)
    {
        std::unique_ptr<OperationUpdate> copy(new OperationUpdate(update));
        if (PostMessageW(window_, MessageOperationUpdate, copy->generation, reinterpret_cast<LPARAM>(copy.get())))
            copy.release();
    }

    void RunOperationJob(const PackageRecord &package, bool install, std::uint32_t generation)
    {
        OperationUpdate update;
        update.generation = generation;
        update.install = install;
        update.packageName = WideFromUtf8(package.name);
        update.stage = OperationStage::Preparing;
        PostOperation(update);

        std::string yaml;
        Status status = source_->FetchInstallerManifest(package, yaml);
        Manifest manifest;
        if (status) status = ParseInstallerManifest(yaml, manifest);
        if (status && (!AsciiEquals(package.id, manifest.id) || package.version != manifest.version))
            status = Status::Fail(ERROR_BAD_FORMAT, "manifest identity does not match the source index");
        if (status && manifest.hasAgreements)
            status = Status::Fail(ERROR_CANCELLED, "review this package's agreements in the details pane, then use the command line with --accept-package-agreements");

        SelectionOptions options;
        options.downloadOnly = !install;
        InstallerEntry installer;
        if (status) status = SelectInstaller(manifest, options, installer);
        if (!status)
        {
            update.stage = OperationStage::Failed;
            update.error = status.message;
            PostOperation(update);
            return;
        }

        unsigned lastPercent = 101;
        unsigned long long lastReceived = 0;
        const auto progress = [&](unsigned long long received, unsigned long long total) {
            if (total)
            {
                const unsigned percent = ScaleProgress(received, total, 100);
                if (received != total && percent == lastPercent) return;
                lastPercent = percent;
            }
            else if (received && received != lastReceived && received - lastReceived < 256 * 1024)
            {
                return;
            }
            lastReceived = received;
            OperationUpdate current = update;
            current.stage = OperationStage::Downloading;
            current.received = received;
            current.total = total;
            PostOperation(current);
        };
        const auto stage = [&](InstallerDownloadStage value) {
            if (value == InstallerDownloadStage::Verified) return;
            OperationUpdate current = update;
            current.stage = value == InstallerDownloadStage::Verifying ? OperationStage::Verifying : OperationStage::Downloading;
            PostOperation(current);
        };

        InstallerService service;
        std::wstring path;
        status = service.Download(package, installer, std::nullopt, path, progress, stage);
        if (!status)
        {
            update.stage = OperationStage::Failed;
            update.error = status.message;
            PostOperation(update);
            return;
        }
        update.path = path;
        if (install)
        {
            update.stage = OperationStage::Installing;
            PostOperation(update);
            DWORD exitCode = 0;
            status = service.Install(installer, path, InstallMode::Default, exitCode);
            update.exitCode = exitCode;
            if (!status)
            {
                update.stage = OperationStage::Failed;
                update.error = status.message;
                PostOperation(update);
                return;
            }
        }

        update.stage = OperationStage::Complete;
        update.received = update.total = 1;
        PostOperation(update);
        std::printf("ROSGET_GUI_%s_DONE id=%s\n", install ? "INSTALL" : "DOWNLOAD", package.id.c_str());
    }

    void RunDetailsJob(const PackageRecord &package, std::uint32_t generation)
    {
        std::unique_ptr<DetailResult> result(new DetailResult());
        result->generation = generation;
        std::string yaml;
        Status status = source_->FetchInstallerManifest(package, yaml);
        if (status)
            status = ParseLocaleManifest(yaml, result->details);
        if (!status)
        {
            result->error = status.message;
            result->stage = 3;
            PostDetails(result);
            return;
        }
        result->ok = true;
        result->stage = 1;
        PostDetails(result);
        if (result->generation != Generation())
            return;

        Manifest manifest;
        if (status)
            status = ParseInstallerManifest(yaml, manifest);
        if (!status)
        {
            result->stage = 3;
            result->selectedError = status.message;
            PostDetails(result);
            return;
        }
        result->agreements = manifest.agreements;
        for (const InstallerEntry &installer : manifest.installers)
        {
            std::string entry = installer.architecture.empty() ? "unknown" : installer.architecture;
            if (!installer.type.empty())
                entry += " (" + installer.type + ")";
            if (std::find(result->installers.begin(), result->installers.end(), entry) == result->installers.end())
                result->installers.push_back(std::move(entry));
        }
        InstallerEntry selected;
        const Status selection = SelectInstaller(manifest, SelectionOptions(), selected);
        if (selection)
        {
            result->selectedArchitecture = selected.architecture.empty() ? "unknown" : selected.architecture;
            result->selected = selected.architecture + " \xc2\xb7 " + selected.type;
            result->installerUrl = selected.url;
            result->installerHash = selected.sha256;
            result->stage = 2;
            result->sizePending = true;
            PostDetails(result);
            if (result->generation != Generation())
                return;
            StartSizeProbe(std::move(result));
            return;
        }
        else
        {
            result->stage = 3;
            result->selectedError = selection.message;
        }
        PostDetails(result);
    }

    HWND window_ = nullptr;
    SourceManager *source_ = nullptr;
    HANDLE thread_ = nullptr;
    HANDLE signal_ = nullptr;
    HANDLE probeStop_ = nullptr;
    CRITICAL_SECTION lock_{};
    PackageRecord pending_;
    PackageRecord operationPackage_;
    std::uint32_t pendingGeneration_ = 0;
    std::uint32_t pendingOperationGeneration_ = 0;
    volatile LONG generation_ = 0;
    volatile LONG operationGeneration_ = 0;
    bool quit_ = false;
    bool catalogPending_ = false;
    bool catalogRefresh_ = false;
    bool detailsPending_ = false;
    bool operationPending_ = false;
    bool operationInstall_ = false;
};

struct Theme
{
    HFONT ui = nullptr;
    HFONT bold = nullptr;
    HFONT title = nullptr;
    HFONT mini = nullptr;
    HBRUSH windowBrush = nullptr;
    HBRUSH faceBrush = nullptr;
    HBRUSH operationBrush = nullptr;
    HBRUSH lineBrush = nullptr;
    HBRUSH accentBrush = nullptr;
    HICON mark = nullptr;
    int uiHeight = 16;
    int titleHeight = 22;
    int miniHeight = 14;
    int scale = 100;
    COLORREF window = 0;
    COLORREF text = 0;
    COLORREF dim = 0;
    COLORREF face = 0;
    COLORREF accent = 0;
    COLORREF chip = 0;
    COLORREF line = 0;
    COLORREF operation = 0;
    COLORREF alternateRow = 0;
    COLORREF sidebar = 0;

    int Px(int value) const { return MulDiv(value, scale, 100); }

    void Create()
    {
        HDC screen = GetDC(nullptr);
        scale = MulDiv(GetDeviceCaps(screen, LOGPIXELSY), 100, 96);
        LoadIconWithScaleDown(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_ROSGET), Px(36), Px(36), &mark);

        LOGFONTW base{};
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
            base = metrics.lfMessageFont;
        else
            GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(base), &base);
        if (!base.lfHeight)
            base.lfHeight = -MulDiv(9, GetDeviceCaps(screen, LOGPIXELSY), 72);

        ui = CreateFontIndirectW(&base);
        LOGFONTW variant = base;
        variant.lfWeight = FW_BOLD;
        bold = CreateFontIndirectW(&variant);
        variant = base;
        variant.lfHeight = base.lfHeight * 15 / 10;
        variant.lfWeight = FW_SEMIBOLD;
        title = CreateFontIndirectW(&variant);
        variant = base;
        variant.lfHeight = base.lfHeight * 9 / 10;
        mini = CreateFontIndirectW(&variant);

        TEXTMETRICW metric{};
        HGDIOBJ previous = SelectObject(screen, ui);
        GetTextMetricsW(screen, &metric);
        uiHeight = metric.tmHeight;
        SelectObject(screen, title);
        GetTextMetricsW(screen, &metric);
        titleHeight = metric.tmHeight;
        SelectObject(screen, mini);
        GetTextMetricsW(screen, &metric);
        miniHeight = metric.tmHeight;
        SelectObject(screen, previous);
        ReleaseDC(nullptr, screen);

        window = GetSysColor(COLOR_WINDOW);
        text = GetSysColor(COLOR_WINDOWTEXT);
        face = GetSysColor(COLOR_BTNFACE);
        accent = GetSysColor(COLOR_HOTLIGHT);
        dim = Blend(text, window, 150);
        chip = Blend(text, window, 30);
        line = Blend(text, window, 55);
        operation = Blend(accent, window, 20);
        alternateRow = Blend(text, window, 8);
        sidebar = Blend(accent, window, 10);
        windowBrush = CreateSolidBrush(window);
        faceBrush = CreateSolidBrush(face);
        operationBrush = CreateSolidBrush(operation);
        lineBrush = CreateSolidBrush(line);
        accentBrush = CreateSolidBrush(accent);
    }

    void Destroy()
    {
        for (HFONT *font : {&ui, &bold, &title, &mini})
        {
            if (*font)
                DeleteObject(*font);
            *font = nullptr;
        }
        for (HBRUSH *brush : {&windowBrush, &faceBrush, &operationBrush, &lineBrush, &accentBrush})
        {
            if (*brush)
                DeleteObject(*brush);
            *brush = nullptr;
        }
        if (mark)
            DestroyIcon(mark);
        mark = nullptr;
    }
};

struct InfoPane
{
    HWND window = nullptr;
    Theme *theme = nullptr;
    std::vector<InfoBlock> blocks;
    std::vector<std::pair<RECT, std::wstring>> links;
    int scroll = 0;
    int content = 0;
    int spinner = 0;
};

int MeasureText(HDC dc, const std::wstring &text, int width, UINT flags)
{
    if (text.empty())
        return 0;
    RECT rect{0, 0, width, 0};
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, flags | DT_CALCRECT);
    return rect.bottom - rect.top;
}

void DrawSpinner(HDC dc, const RECT &bounds, const Theme &theme, int phase)
{
    static const int SineTable[8] = {0, 707, 1000, 707, 0, -707, -1000, -707};
    const int size = bounds.right - bounds.left;
    const int radius = size / 2 - theme.Px(2);
    const int centreX = bounds.left + size / 2;
    const int centreY = bounds.top + size / 2;
    const int dot = std::max(2, theme.Px(2));
    HGDIOBJ pen = SelectObject(dc, GetStockObject(NULL_PEN));
    for (int index = 0; index < 8; ++index)
    {
        const int x = centreX + radius * SineTable[index] / 1000;
        const int y = centreY - radius * SineTable[(index + 2) % 8] / 1000;
        const int age = (index - phase + 8) % 8;
        const int weight = 40 + (7 - age) * 26;
        HBRUSH brush = CreateSolidBrush(Blend(theme.accent, theme.window, weight));
        HGDIOBJ previous = SelectObject(dc, brush);
        Ellipse(dc, x - dot, y - dot, x + dot, y + dot);
        SelectObject(dc, previous);
        DeleteObject(brush);
    }
    SelectObject(dc, pen);
}

int RenderInfo(HDC dc, InfoPane &pane, int width, int originY, bool draw)
{
    const Theme &theme = *pane.theme;
    const int pad = theme.Px(14);
    const int inner = std::max(theme.Px(60), width - pad * 2);
    const int labelWidth = theme.Px(94);
    int y = originY + pad;
    if (draw)
        pane.links.clear();

    for (const InfoBlock &block : pane.blocks)
    {
        switch (block.kind)
        {
            case BlockKind::Title:
            {
                SelectObject(dc, theme.title);
                RECT rect{pad, y, pad + inner, y + theme.titleHeight};
                if (draw)
                {
                    SetTextColor(dc, theme.text);
                    DrawTextW(dc, block.text.c_str(), -1, &rect, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                }
                y += theme.titleHeight + theme.Px(3);
                break;
            }
            case BlockKind::Subtitle:
            {
                SelectObject(dc, theme.mini);
                RECT rect{pad, y, pad + inner, y + theme.miniHeight};
                if (draw)
                {
                    SetTextColor(dc, theme.dim);
                    DrawTextW(dc, block.text.c_str(), -1, &rect, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                }
                y += theme.miniHeight + theme.Px(10);
                break;
            }
            case BlockKind::Chips:
            {
                SelectObject(dc, theme.mini);
                const int chipHeight = theme.miniHeight + theme.Px(7);
                const int gap = theme.Px(6);
                int x = pad;
                int rows = block.chips.empty() ? 0 : 1;
                for (std::size_t chipIndex = 0; chipIndex < block.chips.size(); ++chipIndex)
                {
                    const std::wstring &chip = block.chips[chipIndex];
                    SIZE extent{};
                    GetTextExtentPoint32W(dc, chip.c_str(), static_cast<int>(chip.size()), &extent);
                    int chipWidth = extent.cx + theme.Px(16);
                    if (chipWidth > inner)
                        chipWidth = inner;
                    if (x + chipWidth > pad + inner && x > pad)
                    {
                        x = pad;
                        y += chipHeight + gap;
                        ++rows;
                    }
                    if (draw)
                    {
                        const bool coloured = chipIndex < block.chipAccents.size();
                        const COLORREF tint = coloured ? block.chipAccents[chipIndex] : block.accent;
                        const COLORREF fill = coloured || block.useAccent ? Blend(tint, theme.window, 55) : theme.chip;
                        HBRUSH brush = CreateSolidBrush(fill);
                        HGDIOBJ oldBrush = SelectObject(dc, brush);
                        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
                        RoundRect(dc, x, y, x + chipWidth + 1, y + chipHeight + 1, theme.Px(8), theme.Px(8));
                        SelectObject(dc, oldPen);
                        SelectObject(dc, oldBrush);
                        DeleteObject(brush);
                        RECT rect{x + theme.Px(8), y, x + chipWidth - theme.Px(8), y + chipHeight};
                        SetTextColor(dc, coloured || block.useAccent ? Blend(tint, theme.text, 150) : Blend(theme.text, theme.window, 210));
                        DrawTextW(dc, chip.c_str(), -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
                    }
                    x += chipWidth + gap;
                }
                if (rows)
                    y += chipHeight + theme.Px(8);
                break;
            }
            case BlockKind::Section:
            {
                SelectObject(dc, theme.bold);
                y += theme.Px(10);
                RECT rect{pad, y, pad + inner, y + theme.uiHeight};
                if (draw)
                {
                    SetTextColor(dc, theme.dim);
                    DrawTextW(dc, block.text.c_str(), -1, &rect, DT_SINGLELINE | DT_NOPREFIX);
                }
                y += theme.uiHeight + theme.Px(4);
                if (draw)
                {
                    HBRUSH brush = CreateSolidBrush(theme.line);
                    RECT separator{pad, y, pad + inner, y + 1};
                    FillRect(dc, &separator, brush);
                    DeleteObject(brush);
                }
                y += theme.Px(8);
                break;
            }
            case BlockKind::Body:
            {
                SelectObject(dc, theme.ui);
                const int height = MeasureText(dc, block.text, inner, DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);
                RECT rect{pad, y, pad + inner, y + height};
                if (draw)
                {
                    SetTextColor(dc, block.useAccent ? block.accent : theme.text);
                    DrawTextW(dc, block.text.c_str(), -1, &rect, DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);
                }
                y += height + theme.Px(7);
                break;
            }
            case BlockKind::Field:
            case BlockKind::Link:
            {
                SelectObject(dc, theme.ui);
                const int valueWidth = std::max(theme.Px(40), inner - labelWidth);
                const int height = std::max(theme.uiHeight, MeasureText(dc, block.text, valueWidth, DT_WORDBREAK | DT_NOPREFIX));
                if (draw)
                {
                    SelectObject(dc, theme.mini);
                    SetTextColor(dc, theme.dim);
                    RECT label{pad, y, pad + labelWidth - theme.Px(8), y + theme.uiHeight};
                    DrawTextW(dc, block.label.c_str(), -1, &label, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                    SelectObject(dc, theme.ui);
                    SetTextColor(dc, block.kind == BlockKind::Link ? theme.accent : theme.text);
                    RECT value{pad + labelWidth, y, pad + labelWidth + valueWidth, y + height};
                    DrawTextW(dc, block.text.c_str(), -1, &value, DT_WORDBREAK | DT_NOPREFIX);
                    if (block.kind == BlockKind::Link)
                        pane.links.emplace_back(value, block.text);
                }
                y += height + theme.Px(5);
                break;
            }
            case BlockKind::Status:
            {
                SelectObject(dc, theme.ui);
                const int box = theme.Px(18);
                const int height = std::max(theme.uiHeight, box);
                if (draw)
                {
                    int textLeft = pad;
                    if (block.spinner)
                    {
                        RECT bounds{pad, y + (height - box) / 2, pad + box, y + (height - box) / 2 + box};
                        DrawSpinner(dc, bounds, theme, pane.spinner);
                        textLeft = pad + box + theme.Px(9);
                    }
                    SetTextColor(dc, block.useAccent ? block.accent : theme.dim);
                    RECT rect{textLeft, y, pad + inner, y + height};
                    DrawTextW(dc, block.text.c_str(), -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
                }
                y += height + theme.Px(6);
                break;
            }
        }
    }
    return y - originY + pad;
}

void UpdateInfoScroll(InfoPane &pane)
{
    RECT client{};
    GetClientRect(pane.window, &client);
    const int page = client.bottom - client.top;
    if (pane.scroll > pane.content - page)
        pane.scroll = pane.content - page;
    if (pane.scroll < 0)
        pane.scroll = 0;
    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = pane.content > 0 ? pane.content - 1 : 0;
    info.nPage = static_cast<UINT>(page > 0 ? page : 1);
    info.nPos = pane.scroll;
    SetScrollInfo(pane.window, SB_VERT, &info, TRUE);
}

void PaintInfo(InfoPane &pane)
{
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(pane.window, &paint);
    RECT client{};
    GetClientRect(pane.window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0)
    {
        EndPaint(pane.window, &paint);
        return;
    }

    HDC memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, width, height);
    HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
    FillRect(memory, &client, pane.theme->windowBrush);
    SetBkMode(memory, TRANSPARENT);

    if (pane.blocks.empty())
    {
        SelectObject(memory, pane.theme->ui);
        SetTextColor(memory, pane.theme->dim);
        RECT rect = client;
        DrawTextW(memory, L"Select a package to read its description.", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    else
    {
        RenderInfo(memory, pane, width, -pane.scroll, true);
    }

    BitBlt(dc, 0, 0, width, height, memory, 0, 0, SRCCOPY);
    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    EndPaint(pane.window, &paint);
}

void RelayoutInfo(InfoPane &pane)
{
    RECT client{};
    GetClientRect(pane.window, &client);
    HDC dc = GetDC(pane.window);
    HGDIOBJ previous = SelectObject(dc, pane.theme->ui);
    pane.content = RenderInfo(dc, pane, client.right - client.left, 0, false);
    SelectObject(dc, previous);
    ReleaseDC(pane.window, dc);
    UpdateInfoScroll(pane);
    InvalidateRect(pane.window, nullptr, FALSE);
}

LRESULT CALLBACK InfoPaneProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    InfoPane *pane = reinterpret_cast<InfoPane *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!pane)
        return DefWindowProcW(window, message, wParam, lParam);

    switch (message)
    {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            PaintInfo(*pane);
            return 0;
        case WM_SIZE:
            RelayoutInfo(*pane);
            return 0;
        case WM_MOUSEWHEEL:
            pane->scroll -= GET_WHEEL_DELTA_WPARAM(wParam) * pane->theme->Px(48) / WHEEL_DELTA;
            UpdateInfoScroll(*pane);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_VSCROLL:
        {
            RECT client{};
            GetClientRect(window, &client);
            const int page = client.bottom - client.top;
            switch (LOWORD(wParam))
            {
                case SB_LINEUP: pane->scroll -= pane->theme->Px(24); break;
                case SB_LINEDOWN: pane->scroll += pane->theme->Px(24); break;
                case SB_PAGEUP: pane->scroll -= page; break;
                case SB_PAGEDOWN: pane->scroll += page; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: pane->scroll = HIWORD(wParam); break;
                case SB_TOP: pane->scroll = 0; break;
                case SB_BOTTOM: pane->scroll = pane->content; break;
                default: return 0;
            }
            UpdateInfoScroll(*pane);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        case WM_SETCURSOR:
        {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(window, &point);
            for (const auto &link : pane->links)
            {
                if (PtInRect(&link.first, point))
                {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
            }
            break;
        }
        case WM_LBUTTONUP:
        {
            const POINT point{static_cast<LONG>(static_cast<short>(LOWORD(lParam))), static_cast<LONG>(static_cast<short>(HIWORD(lParam)))};
            for (const auto &link : pane->links)
            {
                if (PtInRect(&link.first, point))
                {
                    if (link.second.size() >= 8 && _wcsnicmp(link.second.c_str(), L"https://", 8) == 0)
                        ShellExecuteW(window, L"open", link.second.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                }
            }
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

struct CatalogWindow
{
    HWND window = nullptr;
    HWND search = nullptr;
    HWND list = nullptr;
    HWND tree = nullptr;
    HWND status = nullptr;
    HWND install = nullptr;
    HWND download = nullptr;
    HWND refresh = nullptr;
    HWND installed = nullptr;
    HWND appTitle = nullptr;
    HWND appSubtitle = nullptr;
    HWND operationTitle = nullptr;
    HWND operationDetail = nullptr;
    HWND operationProgress = nullptr;
    HWND operationPercent = nullptr;
    HIMAGELIST treeImages = nullptr;
    Theme theme;
    InfoPane info;
    Worker worker;
    SourceManager *source = nullptr;
    PreparedCatalog catalog;
    std::vector<std::uint32_t> filtered;
    std::map<std::string, DetailResult> cache;
    std::string query;
    std::string failure;
    std::wstring placeholder;
    int category = -1;
    int sortColumn = -1;
    bool sortDescending = false;
    int selected = -1;
    std::uint32_t generation = 0;
    std::uint32_t operationGeneration = 0;
    bool loading = true;
    bool operationVisible = false;
    bool operationBusy = false;
    bool backgroundProgress = false;
    bool spinning = false;
    bool sizeAnimating = false;
    unsigned sizeDotPhase = 1;
    bool selectFirst = false;
    bool buildingTree = false;
    int treeWidth = 0;
    int infoHeight = 0;
    int drag = 0;
};

WNDPROC OriginalSearchProc = nullptr;

void UpdateActionButtons(CatalogWindow &state);
void SetOperationMarquee(CatalogWindow &state, bool marquee);
void ShowBackgroundProgress(CatalogWindow &state, const wchar_t *title, const wchar_t *detail);
void HideBackgroundProgress(CatalogWindow &state);
void StopSpinner(CatalogWindow &state);
void StopSizeAnimation(CatalogWindow &state);

int CompareAscii(const std::string &left, const std::string &right)
{
    const std::size_t count = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < count; ++index)
    {
        const unsigned char first = static_cast<unsigned char>(left[index]);
        const unsigned char second = static_cast<unsigned char>(right[index]);
        const int lowerFirst = first >= 'A' && first <= 'Z' ? first + 32 : first;
        const int lowerSecond = second >= 'A' && second <= 'Z' ? second + 32 : second;
        if (lowerFirst != lowerSecond)
            return lowerFirst < lowerSecond ? -1 : 1;
    }
    if (left.size() == right.size())
        return 0;
    return left.size() < right.size() ? -1 : 1;
}

std::vector<std::wstring> SplitChips(const std::wstring &text)
{
    std::vector<std::wstring> chips;
    std::wstring current;
    for (const wchar_t character : text)
    {
        if (character == L',')
        {
            if (!current.empty())
                chips.push_back(current);
            current.clear();
            continue;
        }
        if (character == L' ' && current.empty())
            continue;
        current.push_back(character);
    }
    if (!current.empty())
        chips.push_back(current);
    if (chips.size() > 18)
        chips.resize(18);
    return chips;
}

HIMAGELIST BuildCategoryImages(const Theme &theme)
{
    const int size = theme.Px(12);
    HIMAGELIST images = ImageList_Create(size, size, ILC_COLOR24 | ILC_MASK, static_cast<int>(CategoryCount()) + 1, 1);
    if (!images)
        return nullptr;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    for (std::size_t index = 0; index <= CategoryCount(); ++index)
    {
        HBITMAP bitmap = CreateCompatibleBitmap(screen, size, size);
        HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
        RECT rect{0, 0, size, size};
        HBRUSH mask = CreateSolidBrush(RGB(255, 0, 255));
        FillRect(memory, &rect, mask);
        DeleteObject(mask);
        HBRUSH brush = CreateSolidBrush(index ? CategoryTable()[index - 1].accent : theme.accent);
        HGDIOBJ oldBrush = SelectObject(memory, brush);
        HGDIOBJ oldPen = SelectObject(memory, GetStockObject(NULL_PEN));
        Ellipse(memory, 1, 1, size - 1, size - 1);
        SelectObject(memory, oldPen);
        SelectObject(memory, oldBrush);
        DeleteObject(brush);
        SelectObject(memory, oldBitmap);
        ImageList_AddMasked(images, bitmap, RGB(255, 0, 255));
        DeleteObject(bitmap);
    }
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return images;
}

void LayoutWindow(CatalogWindow &state)
{
    RECT client{};
    GetClientRect(state.window, &client);
    const int width = client.right - client.left;
    int height = client.bottom - client.top;
    if (width <= 0 || height <= 0)
        return;

    RECT statusRect{};
    SendMessageW(state.status, WM_SIZE, 0, 0);
    GetWindowRect(state.status, &statusRect);
    const int statusHeight = statusRect.bottom - statusRect.top;
    height -= statusHeight;

    const int header = state.theme.Px(96);
    const int splitter = state.theme.Px(6);
    const int margin = state.theme.Px(12);
    const int actionWidth = state.theme.Px(82);
    const int installedWidth = state.theme.Px(112);
    const int buttonHeight = state.theme.Px(30);
    const int gap = state.theme.Px(8);

    MoveWindow(state.appTitle, margin + state.theme.Px(46), state.theme.Px(9),
               std::max(state.theme.Px(120), width - installedWidth - margin * 3 - state.theme.Px(46)), state.theme.Px(26), TRUE);
    MoveWindow(state.appSubtitle, margin + state.theme.Px(46), state.theme.Px(34),
               std::max(state.theme.Px(120), width - installedWidth - margin * 3 - state.theme.Px(46)), state.theme.Px(18), TRUE);
    MoveWindow(state.installed, width - margin - installedWidth, state.theme.Px(15), installedWidth, buttonHeight, TRUE);

    int right = width - margin;
    MoveWindow(state.install, right - actionWidth, state.theme.Px(57), actionWidth, buttonHeight, TRUE);
    right -= actionWidth + gap;
    MoveWindow(state.download, right - actionWidth, state.theme.Px(57), actionWidth, buttonHeight, TRUE);
    right -= actionWidth + gap;
    MoveWindow(state.refresh, right - actionWidth, state.theme.Px(57), actionWidth, buttonHeight, TRUE);
    right -= actionWidth + state.theme.Px(14);
    const int searchWidth = std::max(state.theme.Px(160), right - margin);
    MoveWindow(state.search, margin, state.theme.Px(57), searchWidth, buttonHeight, TRUE);

    const int operationHeight = state.operationVisible ? state.theme.Px(86) : 0;
    const int operationTop = height - operationHeight;
    const int operationMargin = state.theme.Px(16);
    const int percentWidth = state.theme.Px(74);
    const int operationTextWidth = std::max(state.theme.Px(80), width - operationMargin * 2 - percentWidth);
    for (HWND child : {state.operationTitle, state.operationDetail, state.operationProgress, state.operationPercent})
        ShowWindow(child, state.operationVisible ? SW_SHOW : SW_HIDE);
    if (state.operationVisible)
    {
        MoveWindow(state.operationTitle, operationMargin, operationTop + state.theme.Px(10), operationTextWidth,
                   state.theme.Px(22), TRUE);
        MoveWindow(state.operationDetail, operationMargin, operationTop + state.theme.Px(33), operationTextWidth,
                   state.theme.Px(20), TRUE);
        MoveWindow(state.operationPercent, width - operationMargin - percentWidth, operationTop + state.theme.Px(33),
                   percentWidth, state.theme.Px(20), TRUE);
        MoveWindow(state.operationProgress, operationMargin, operationTop + state.theme.Px(59),
                   width - operationMargin * 2, state.theme.Px(13), TRUE);
    }

    const int minimumTree = state.theme.Px(150);
    const int maximumTree = std::max(minimumTree, width - state.theme.Px(340));
    state.treeWidth = std::min(std::max(state.treeWidth, minimumTree), maximumTree);

    const int bodyTop = header + margin;
    const int bodyHeight = std::max(state.theme.Px(120), operationTop - bodyTop - margin);
    const int minimumInfo = state.theme.Px(110);
    const int maximumInfo = std::max(minimumInfo, bodyHeight - state.theme.Px(140));
    state.infoHeight = std::min(std::max(state.infoHeight, minimumInfo), maximumInfo);

    MoveWindow(state.tree, margin, bodyTop, std::max(state.theme.Px(80), state.treeWidth - margin), bodyHeight, TRUE);
    const int contentLeft = state.treeWidth + splitter;
    const int contentWidth = std::max(state.theme.Px(120), width - contentLeft - margin);
    const int listHeight = std::max(state.theme.Px(60), bodyHeight - state.infoHeight - splitter);
    MoveWindow(state.list, contentLeft, bodyTop, contentWidth, listHeight, TRUE);
    MoveWindow(state.info.window, contentLeft, bodyTop + listHeight + splitter, contentWidth, state.infoHeight, TRUE);
}

void UpdateStatusBar(CatalogWindow &state)
{
    const std::wstring category = state.category < 0 ? L"All packages" : CategoryTable()[state.category].name;
    std::wstring counts = FormatCount(state.filtered.size()) + L" of " + FormatCount(state.catalog.packages.size()) + L" packages";
    std::wstring message = L"Ready";
    if (state.loading)
        message = L"Loading the winget catalog…";
    else if (state.operationBusy)
        message = L"Package operation in progress…";
    else if (!state.failure.empty())
        message = WideFromUtf8(state.failure);
    SendMessageW(state.status, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(message.c_str()));
    SendMessageW(state.status, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(category.c_str()));
    SendMessageW(state.status, SB_SETTEXTW, 2, reinterpret_cast<LPARAM>(counts.c_str()));
}

void ApplyFilter(CatalogWindow &state)
{
    std::vector<std::string> terms;
    std::string term;
    for (const char character : state.query)
    {
        if (character == ' ')
        {
            if (!term.empty())
                terms.push_back(term);
            term.clear();
            continue;
        }
        term.push_back(character);
    }
    if (!term.empty())
        terms.push_back(term);

    const std::uint32_t mask = state.category >= 0 ? (1u << state.category) : 0;
    std::vector<std::pair<int, std::uint32_t>> scored;
    scored.reserve(state.catalog.packages.size());
    for (std::size_t index = 0; index < state.catalog.packages.size(); ++index)
    {
        const GuiPackage &package = state.catalog.packages[index];
        if (mask && !(package.categories & mask))
            continue;
        bool matched = true;
        for (const std::string &needle : terms)
        {
            if (package.searchKey.find(needle) == std::string::npos)
            {
                matched = false;
                break;
            }
        }
        if (!matched)
            continue;
        scored.emplace_back(RankPackage(package, state.query), static_cast<std::uint32_t>(index));
    }

    const PreparedCatalog *catalog = &state.catalog;
    const int column = state.sortColumn;
    const bool descending = state.sortDescending;
    std::sort(scored.begin(), scored.end(), [catalog, column, descending](const std::pair<int, std::uint32_t> &left, const std::pair<int, std::uint32_t> &right) {
        const GuiPackage &first = catalog->packages[left.second];
        const GuiPackage &second = catalog->packages[right.second];
        int order = 0;
        switch (column)
        {
            case 0: order = CompareAscii(first.utf8Name, second.utf8Name); break;
            case 1: order = CompareAscii(first.utf8Id, second.utf8Id); break;
            case 2: order = CompareAscii(first.utf8Version, second.utf8Version); break;
            case 3: order = lstrcmpiW(first.tagText.c_str(), second.tagText.c_str()); break;
            default:
                if (left.first != right.first)
                    return left.first < right.first;
                order = CompareAscii(first.utf8Name, second.utf8Name);
                break;
        }
        if (!order)
            order = CompareAscii(first.utf8Id, second.utf8Id);
        return descending ? order > 0 : order < 0;
    });

    state.filtered.clear();
    state.filtered.reserve(scored.size());
    for (const auto &entry : scored)
        state.filtered.push_back(entry.second);

    state.selected = -1;
    state.worker.CancelDetails();
    state.generation = 0;
    StopSpinner(state);
    StopSizeAnimation(state);
    HideBackgroundProgress(state);
    UpdateActionButtons(state);
    SendMessageW(state.list, LVM_SETITEMCOUNT, static_cast<WPARAM>(state.filtered.size()), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
    InvalidateRect(state.list, nullptr, FALSE);
    UpdateStatusBar(state);
}

void BuildTree(CatalogWindow &state)
{
    state.buildingTree = true;
    TreeView_DeleteAllItems(state.tree);
    TVINSERTSTRUCTW insert{};
    insert.hParent = TVI_ROOT;
    insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;

    std::wstring label = L"All packages (" + FormatCount(state.catalog.packages.size()) + L")";
    insert.item.pszText = const_cast<LPWSTR>(label.c_str());
    insert.item.lParam = -1;
    insert.item.iImage = 0;
    insert.item.iSelectedImage = 0;
    const HTREEITEM root = TreeView_InsertItem(state.tree, &insert);

    for (std::size_t index = 0; index < CategoryCount(); ++index)
    {
        if (!state.catalog.counts[index])
            continue;
        std::wstring text = std::wstring(CategoryTable()[index].name) + L" (" + FormatCount(state.catalog.counts[index]) + L")";
        insert.item.pszText = const_cast<LPWSTR>(text.c_str());
        insert.item.lParam = static_cast<LPARAM>(index);
        insert.item.iImage = static_cast<int>(index) + 1;
        insert.item.iSelectedImage = static_cast<int>(index) + 1;
        TreeView_InsertItem(state.tree, &insert);
    }
    state.buildingTree = false;
    TreeView_SelectItem(state.tree, root);
}

void StopSpinner(CatalogWindow &state)
{
    if (!state.spinning)
        return;
    KillTimer(state.window, TimerSpinner);
    state.spinning = false;
}

void StartSpinner(CatalogWindow &state)
{
    if (state.spinning)
        return;
    SetTimer(state.window, TimerSpinner, 90, nullptr);
    state.spinning = true;
}

void StopSizeAnimation(CatalogWindow &state)
{
    if (!state.sizeAnimating)
        return;
    KillTimer(state.window, TimerSize);
    state.sizeAnimating = false;
    state.sizeDotPhase = 1;
}

void StartSizeAnimation(CatalogWindow &state)
{
    StopSizeAnimation(state);
    state.sizeAnimating = true;
    state.sizeDotPhase = 1;
    SetTimer(state.window, TimerSize, 350, nullptr);
}

void AdvanceSizeAnimation(CatalogWindow &state)
{
    if (!state.sizeAnimating)
        return;
    state.sizeDotPhase = state.sizeDotPhase == 3 ? 1 : state.sizeDotPhase + 1;
    for (InfoBlock &block : state.info.blocks)
    {
        if (block.kind == BlockKind::Field && block.label == L"Size")
        {
            block.text.assign(state.sizeDotPhase, L'.');
            InvalidateRect(state.info.window, nullptr, FALSE);
            break;
        }
    }
}

void SetInfoBlocks(CatalogWindow &state, const GuiPackage &package, const DetailResult *result)
{
    std::vector<InfoBlock> blocks;

    InfoBlock title;
    title.kind = BlockKind::Title;
    title.text = package.name;
    blocks.push_back(std::move(title));

    InfoBlock subtitle;
    subtitle.kind = BlockKind::Subtitle;
    subtitle.text = package.id + L"   ·   version " + package.version;
    blocks.push_back(std::move(subtitle));

    InfoBlock categories;
    categories.kind = BlockKind::Chips;
    for (std::size_t index = 0; index < CategoryCount(); ++index)
    {
        if (!(package.categories & (1u << index)))
            continue;
        categories.chips.push_back(CategoryTable()[index].name);
        categories.chipAccents.push_back(CategoryTable()[index].accent);
    }
    if (!categories.chips.empty())
        blocks.push_back(std::move(categories));

    if (!result)
    {
        InfoBlock loading;
        loading.kind = BlockKind::Status;
        loading.spinner = true;
        loading.text = L"Fetching the description from winget-pkgs…";
        blocks.push_back(std::move(loading));
    }
    else if (!result->ok)
    {
        InfoBlock failed;
        failed.kind = BlockKind::Status;
        failed.text = L"No description available: " + WideFromUtf8(result->error);
        blocks.push_back(std::move(failed));
    }
    else
    {
        const PackageDetails &details = result->details;
        if (!details.shortDescription.empty())
        {
            InfoBlock summary;
            summary.kind = BlockKind::Body;
            summary.text = WideFromUtf8(details.shortDescription);
            blocks.push_back(std::move(summary));
        }
        if (!details.description.empty() && details.description != details.shortDescription)
        {
            InfoBlock body;
            body.kind = BlockKind::Body;
            body.text = WideFromUtf8(details.description);
            blocks.push_back(std::move(body));
        }

        InfoBlock section;
        section.kind = BlockKind::Section;
        section.text = L"About";
        blocks.push_back(std::move(section));

        const std::pair<const wchar_t *, const std::string *> fields[] = {
            {L"Publisher", &details.publisher},
            {L"Author", &details.author},
            {L"License", &details.license},
            {L"Copyright", &details.copyright},
        };
        for (const auto &field : fields)
        {
            if (field.second->empty())
                continue;
            InfoBlock entry;
            entry.kind = BlockKind::Field;
            entry.label = field.first;
            entry.text = WideFromUtf8(*field.second);
            blocks.push_back(std::move(entry));
        }
        const std::pair<const wchar_t *, const std::string *> links[] = {
            {L"Homepage", &details.packageUrl},
            {L"Publisher site", &details.publisherUrl},
            {L"Support", &details.supportUrl},
            {L"License terms", &details.licenseUrl},
            {L"Release notes", &details.releaseNotesUrl},
        };
        for (const auto &field : links)
        {
            if (field.second->empty())
                continue;
            InfoBlock entry;
            entry.kind = AsciiStartsWith(*field.second, "https://") ? BlockKind::Link : BlockKind::Field;
            entry.label = field.first;
            entry.text = WideFromUtf8(*field.second);
            blocks.push_back(std::move(entry));
        }
        if (!details.tags.empty())
        {
            InfoBlock section2;
            section2.kind = BlockKind::Section;
            section2.text = L"Tags";
            blocks.push_back(std::move(section2));
            InfoBlock chips;
            chips.kind = BlockKind::Chips;
            for (const std::string &tag : details.tags)
                chips.chips.push_back(WideFromUtf8(tag));
            blocks.push_back(std::move(chips));
        }
        else if (!package.tagText.empty())
        {
            InfoBlock section2;
            section2.kind = BlockKind::Section;
            section2.text = L"Tags";
            blocks.push_back(std::move(section2));
            InfoBlock chips;
            chips.kind = BlockKind::Chips;
            chips.chips = SplitChips(package.tagText);
            blocks.push_back(std::move(chips));
        }

        if (!result->agreements.empty())
        {
            InfoBlock agreementsSection;
            agreementsSection.kind = BlockKind::Section;
            agreementsSection.text = L"Package agreements";
            blocks.push_back(std::move(agreementsSection));
            for (const PackageAgreement &agreement : result->agreements)
            {
                if (!agreement.label.empty())
                {
                    InfoBlock label;
                    label.kind = BlockKind::Field;
                    label.label = L"Agreement";
                    label.text = WideFromUtf8(agreement.label);
                    blocks.push_back(std::move(label));
                }
                if (!agreement.text.empty())
                {
                    InfoBlock text;
                    text.kind = BlockKind::Body;
                    text.text = WideFromUtf8(agreement.text);
                    blocks.push_back(std::move(text));
                }
                if (!agreement.url.empty())
                {
                    InfoBlock url;
                    url.kind = AsciiStartsWith(agreement.url, "https://") ? BlockKind::Link : BlockKind::Field;
                    url.label = L"Terms";
                    url.text = WideFromUtf8(agreement.url);
                    blocks.push_back(std::move(url));
                }
            }
        }

        InfoBlock section3;
        section3.kind = BlockKind::Section;
        section3.text = L"Installer";
        blocks.push_back(std::move(section3));

        if (result->stage < 2)
        {
            InfoBlock loading;
            loading.kind = BlockKind::Status;
            loading.spinner = true;
            loading.text = L"Reading the installer manifest…";
            blocks.push_back(std::move(loading));
        }
        else
        {
            if (!result->installers.empty())
            {
                std::string joined;
                for (const std::string &entry : result->installers)
                {
                    if (!joined.empty())
                        joined += ", ";
                    joined += entry;
                }
                InfoBlock available;
                available.kind = BlockKind::Field;
                available.label = L"Available";
                available.text = WideFromUtf8(joined);
                blocks.push_back(std::move(available));
            }
            InfoBlock selected;
            selected.kind = BlockKind::Field;
            selected.label = L"Selected";
            if (!result->selected.empty())
            {
                selected.text = WideFromUtf8(result->selected);
            }
            else
            {
                selected.text = result->selectedError.empty() ? L"no supported installer" : WideFromUtf8(result->selectedError);
                selected.useAccent = true;
                selected.accent = state.theme.dim;
            }
            blocks.push_back(std::move(selected));
            if (!result->selectedArchitecture.empty())
            {
                InfoBlock architecture;
                architecture.kind = BlockKind::Field;
                architecture.label = L"Architecture";
                architecture.text = WideFromUtf8(result->selectedArchitecture);
                blocks.push_back(std::move(architecture));
            }
            if (!result->installerUrl.empty())
            {
                InfoBlock size;
                size.kind = BlockKind::Field;
                size.label = L"Size";
                if (result->sizePending)
                    size.text = L".";
                else if (result->sizeKnown)
                    size.text = FormatByteCount(result->installerSize) + L" (" +
                                FormatCount(result->installerSize) + L" bytes)";
                else
                {
                    size.text = result->sizeError.empty() ? L"not reported" : L"not reported by server";
                    size.useAccent = true;
                    size.accent = state.theme.dim;
                }
                blocks.push_back(std::move(size));
            }
            if (!result->installerUrl.empty())
            {
                InfoBlock url;
                url.kind = BlockKind::Field;
                url.label = L"Installer URL";
                url.text = WideFromUtf8(result->installerUrl);
                blocks.push_back(std::move(url));
            }
            if (!result->installerHash.empty())
            {
                InfoBlock hash;
                hash.kind = BlockKind::Field;
                hash.label = L"SHA-256";
                hash.text = WideFromUtf8(result->installerHash);
                blocks.push_back(std::move(hash));
            }
        }
    }

    state.info.blocks = std::move(blocks);
    state.info.scroll = 0;
    RelayoutInfo(state.info);
}

void ShowDetails(CatalogWindow &state)
{
    StopSizeAnimation(state);
    if (state.selected < 0 || static_cast<std::size_t>(state.selected) >= state.filtered.size())
    {
        state.worker.CancelDetails();
        StopSpinner(state);
        HideBackgroundProgress(state);
        state.info.blocks.clear();
        RelayoutInfo(state.info);
        return;
    }

    const GuiPackage &package = state.catalog.packages[state.filtered[state.selected]];
    const std::string key = package.utf8Id + "/" + package.utf8Version;
    const auto cached = state.cache.find(key);
    if (cached != state.cache.end())
    {
        state.worker.CancelDetails();
        state.generation = 0;
        StopSpinner(state);
        HideBackgroundProgress(state);
        SetInfoBlocks(state, package, &cached->second);
        return;
    }

    PackageRecord record;
    record.id = package.utf8Id;
    record.name = package.utf8Name;
    record.version = package.utf8Version;
    record.manifestHash = package.manifestHash;
    state.generation = state.worker.RequestDetails(record);
    SetInfoBlocks(state, package, nullptr);
    StartSpinner(state);
    ShowBackgroundProgress(state, L"Loading package details", L"Fetching the signed installer manifest…");
}

void UpdateActionButtons(CatalogWindow &state)
{
    const bool selected = state.selected >= 0 && static_cast<std::size_t>(state.selected) < state.filtered.size();
    EnableWindow(state.install, selected && !state.loading && !state.operationBusy);
    EnableWindow(state.download, selected && !state.loading && !state.operationBusy);
    EnableWindow(state.refresh, !state.loading && !state.operationBusy);
    EnableWindow(state.installed, !state.operationBusy);
}

void SetOperationMarquee(CatalogWindow &state, bool marquee)
{
    LONG_PTR style = GetWindowLongPtrW(state.operationProgress, GWL_STYLE);
    if (marquee)
    {
        if (!(style & PBS_MARQUEE))
        {
            SetWindowLongPtrW(state.operationProgress, GWL_STYLE, style | PBS_MARQUEE);
            SetWindowPos(state.operationProgress, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        SendMessageW(state.operationProgress, PBM_SETPOS, 0, 0);
        SendMessageW(state.operationProgress, PBM_SETMARQUEE, TRUE, 35);
    }
    else
    {
        SendMessageW(state.operationProgress, PBM_SETMARQUEE, FALSE, 0);
        if (style & PBS_MARQUEE)
        {
            SetWindowLongPtrW(state.operationProgress, GWL_STYLE, style & ~static_cast<LONG_PTR>(PBS_MARQUEE));
            SetWindowPos(state.operationProgress, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
    }
}

void ShowBackgroundProgress(CatalogWindow &state, const wchar_t *title, const wchar_t *detail)
{
    if (state.operationBusy)
        return;
    state.backgroundProgress = true;
    state.operationVisible = true;
    SetWindowTextW(state.operationTitle, title);
    SetWindowTextW(state.operationDetail, detail);
    SetWindowTextW(state.operationPercent, L"");
    SendMessageW(state.operationProgress, PBM_SETBARCOLOR, 0, state.theme.accent);
    SetOperationMarquee(state, true);
    LayoutWindow(state);
    InvalidateRect(state.window, nullptr, TRUE);
}

void HideBackgroundProgress(CatalogWindow &state)
{
    if (!state.backgroundProgress || state.operationBusy)
        return;
    state.backgroundProgress = false;
    state.operationVisible = false;
    SetOperationMarquee(state, false);
    LayoutWindow(state);
    InvalidateRect(state.window, nullptr, TRUE);
}

void BeginOperation(CatalogWindow &state, bool install)
{
    if (state.operationBusy || state.selected < 0 || static_cast<std::size_t>(state.selected) >= state.filtered.size())
        return;
    const GuiPackage &package = state.catalog.packages[state.filtered[state.selected]];
    PackageRecord record;
    record.id = package.utf8Id;
    record.name = package.utf8Name;
    record.version = package.utf8Version;
    record.manifestHash = package.manifestHash;

    state.backgroundProgress = false;
    state.operationVisible = true;
    state.operationBusy = true;
    SetWindowTextW(state.operationTitle, (std::wstring(L"Preparing ") + package.name).c_str());
    SetWindowTextW(state.operationDetail, L"Checking signed package metadata…");
    SetWindowTextW(state.operationPercent, L"");
    SendMessageW(state.operationProgress, PBM_SETBARCOLOR, 0, state.theme.accent);
    SetOperationMarquee(state, true);
    state.operationGeneration = state.worker.RequestOperation(record, install);
    UpdateActionButtons(state);
    UpdateStatusBar(state);
    LayoutWindow(state);
    InvalidateRect(state.window, nullptr, TRUE);
}

void ApplyOperationUpdate(CatalogWindow &state, const OperationUpdate &update)
{
    const std::wstring name = update.packageName.empty() ? L"package" : update.packageName;
    switch (update.stage)
    {
        case OperationStage::Preparing:
            SetWindowTextW(state.operationTitle, (std::wstring(L"Preparing ") + name).c_str());
            SetWindowTextW(state.operationDetail, L"Checking signed package metadata…");
            SetWindowTextW(state.operationPercent, L"");
            SetOperationMarquee(state, true);
            break;
        case OperationStage::Downloading:
        {
            SetWindowTextW(state.operationTitle, (std::wstring(L"Downloading ") + name).c_str());
            if (update.total)
            {
                const unsigned percent = ScaleProgress(update.received, update.total, 100);
                const int position = static_cast<int>(ScaleProgress(update.received, update.total, 1000));
                const std::wstring detail = FormatByteCount(update.received) + L" of " + FormatByteCount(update.total);
                const std::wstring percentText = std::to_wstring(percent) + L"%";
                SetWindowTextW(state.operationDetail, detail.c_str());
                SetWindowTextW(state.operationPercent, percentText.c_str());
                SetOperationMarquee(state, false);
                SendMessageW(state.operationProgress, PBM_SETRANGE32, 0, 1000);
                SendMessageW(state.operationProgress, PBM_SETPOS, position, 0);
            }
            else
            {
                const std::wstring detail = update.received ? FormatByteCount(update.received) + L" downloaded" : L"Waiting for the server…";
                SetWindowTextW(state.operationDetail, detail.c_str());
                SetWindowTextW(state.operationPercent, L"");
                SetOperationMarquee(state, true);
            }
            break;
        }
        case OperationStage::Verifying:
            SetWindowTextW(state.operationTitle, L"Verifying download");
            SetWindowTextW(state.operationDetail, L"Checking the installer SHA-256 against the signed manifest…");
            SetWindowTextW(state.operationPercent, L"");
            SetOperationMarquee(state, true);
            break;
        case OperationStage::Installing:
            SetWindowTextW(state.operationTitle, (std::wstring(L"Installing ") + name).c_str());
            SetWindowTextW(state.operationDetail, L"The verified installer is running silently. This may take a moment.");
            SetWindowTextW(state.operationPercent, L"");
            SetOperationMarquee(state, true);
            break;
        case OperationStage::Complete:
        {
            state.operationBusy = false;
            SetWindowTextW(state.operationTitle, update.install ? L"Installation complete" : L"Download complete");
            const std::wstring detail = update.install ? name + L" is ready to use." : L"Saved to " + update.path;
            SetWindowTextW(state.operationDetail, detail.c_str());
            SetWindowTextW(state.operationPercent, L"Done");
            SetOperationMarquee(state, false);
            SendMessageW(state.operationProgress, PBM_SETRANGE32, 0, 1000);
            SendMessageW(state.operationProgress, PBM_SETPOS, 1000, 0);
            UpdateActionButtons(state);
            UpdateStatusBar(state);
            break;
        }
        case OperationStage::Failed:
            state.operationBusy = false;
            SetWindowTextW(state.operationTitle, update.install ? L"Installation failed" : L"Download failed");
            SetWindowTextW(state.operationDetail, WideFromUtf8(update.error).c_str());
            SetWindowTextW(state.operationPercent, L"Failed");
            SetOperationMarquee(state, false);
            SendMessageW(state.operationProgress, PBM_SETPOS, 0, 0);
            SendMessageW(state.operationProgress, PBM_SETBARCOLOR, 0, RGB(0xC4, 0x2B, 0x1C));
            UpdateActionButtons(state);
            UpdateStatusBar(state);
            break;
    }
}

LRESULT CALLBACK SearchProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    CatalogWindow *state = reinterpret_cast<CatalogWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_PAINT && state && !GetWindowTextLengthW(window) && GetFocus() != window && !state->placeholder.empty())
    {
        const LRESULT result = CallWindowProcW(OriginalSearchProc, window, message, wParam, lParam);
        HDC dc = GetDC(window);
        RECT rect{};
        GetClientRect(window, &rect);
        rect.left += state->theme.Px(4);
        HGDIOBJ previous = SelectObject(dc, state->theme.ui);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, state->theme.dim);
        DrawTextW(dc, state->placeholder.c_str(), -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, previous);
        ReleaseDC(window, dc);
        return result;
    }
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS)
        InvalidateRect(window, nullptr, TRUE);
    return CallWindowProcW(OriginalSearchProc, window, message, wParam, lParam);
}

void CreateChildren(CatalogWindow &state)
{
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(state.window, GWLP_HINSTANCE));
    state.treeWidth = state.theme.Px(210);
    state.infoHeight = state.theme.Px(230);

    state.appTitle = CreateWindowExW(0, L"STATIC", L"rosget", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                                     0, 0, 10, 10, state.window, reinterpret_cast<HMENU>(IdAppTitle), instance, nullptr);
    state.appSubtitle = CreateWindowExW(0, L"STATIC", L"Signed software catalog", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                                        0, 0, 10, 10, state.window, reinterpret_cast<HMENU>(IdAppSubtitle), instance, nullptr);

    state.search = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                   0, 0, 10, 10, state.window, reinterpret_cast<HMENU>(IdSearch), instance, nullptr);
    SetWindowLongPtrW(state.search, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    OriginalSearchProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(state.search, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(SearchProc)));

    state.install = CreateWindowExW(0, L"BUTTON", L"Install", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_DISABLED | BS_DEFPUSHBUTTON,
                                    0, 0, 10, 10, state.window, reinterpret_cast<HMENU>(IdInstall), instance, nullptr);
    state.download = CreateWindowExW(0, L"BUTTON", L"Download", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_DISABLED | BS_PUSHBUTTON,
                                     0, 0, 10, 10, state.window, reinterpret_cast<HMENU>(IdDownload), instance, nullptr);
    state.refresh = CreateWindowExW(0, L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                    0, 0, 10, 10, state.window, reinterpret_cast<HMENU>(IdRefresh), instance, nullptr);
    state.installed = CreateWindowExW(0, L"BUTTON", L"Installed apps", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                      0, 0, 10, 10, state.window, reinterpret_cast<HMENU>(IdInstalled), instance, nullptr);

    state.tree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_FULLROWSELECT | TVS_SHOWSELALWAYS | TVS_TRACKSELECT | TVS_NOHSCROLL,
                                 0, 0, 10, 10, state.window, reinterpret_cast<HMENU>(IdTree), instance, nullptr);
    state.treeImages = BuildCategoryImages(state.theme);
    if (state.treeImages)
        TreeView_SetImageList(state.tree, state.treeImages, TVSIL_NORMAL);
    TreeView_SetBkColor(state.tree, state.theme.sidebar);
    TreeView_SetTextColor(state.tree, state.theme.text);

    state.list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                 0, 0, 10, 10, state.window, reinterpret_cast<HMENU>(IdList), instance, nullptr);
    ListView_SetExtendedListViewStyle(state.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_LABELTIP);

    static const struct { const wchar_t *title; int width; } Columns[] = {
        {L"Name", 220}, {L"Package identifier", 250}, {L"Version", 100}, {L"Tags", 360},
    };
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    for (int index = 0; index < 4; ++index)
    {
        column.iSubItem = index;
        column.pszText = const_cast<LPWSTR>(Columns[index].title);
        column.cx = state.theme.Px(Columns[index].width);
        ListView_InsertColumn(state.list, index, &column);
    }

    state.info.theme = &state.theme;
    state.info.window = CreateWindowExW(WS_EX_CLIENTEDGE, L"RosGetInfoPane", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                                        0, 0, 10, 10, state.window, reinterpret_cast<HMENU>(IdInfo), instance, nullptr);
    SetWindowLongPtrW(state.info.window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state.info));

    state.status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                   0, 0, 10, 10, state.window, reinterpret_cast<HMENU>(IdStatus), instance, nullptr);
    int parts[3] = {state.theme.Px(240), state.theme.Px(420), -1};
    SendMessageW(state.status, SB_SETPARTS, 3, reinterpret_cast<LPARAM>(parts));

    state.operationTitle = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFT | SS_NOPREFIX,
                                            0, 0, 10, 10, state.window, reinterpret_cast<HMENU>(IdOperationTitle), instance, nullptr);
    state.operationDetail = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFT | SS_NOPREFIX | SS_PATHELLIPSIS,
                                             0, 0, 10, 10, state.window, reinterpret_cast<HMENU>(IdOperationDetail), instance, nullptr);
    state.operationPercent = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_RIGHT | SS_NOPREFIX,
                                              0, 0, 10, 10, state.window, reinterpret_cast<HMENU>(IdOperationPercent), instance, nullptr);
    state.operationProgress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | PBS_SMOOTH | PBS_MARQUEE,
                                               0, 0, 10, 10, state.window, reinterpret_cast<HMENU>(IdOperationProgress), instance, nullptr);
    SendMessageW(state.operationProgress, PBM_SETRANGE32, 0, 1000);
    SendMessageW(state.operationProgress, PBM_SETBARCOLOR, 0, state.theme.accent);
    SendMessageW(state.operationProgress, PBM_SETBKCOLOR, 0, state.theme.window);

    for (HWND child : {state.search, state.install, state.download, state.refresh, state.installed, state.tree, state.list, state.status,
                       state.operationDetail})
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(state.theme.ui), TRUE);
    SendMessageW(state.appTitle, WM_SETFONT, reinterpret_cast<WPARAM>(state.theme.title), TRUE);
    SendMessageW(state.appSubtitle, WM_SETFONT, reinterpret_cast<WPARAM>(state.theme.mini), TRUE);
    SendMessageW(state.operationTitle, WM_SETFONT, reinterpret_cast<WPARAM>(state.theme.bold), TRUE);
    SendMessageW(state.operationPercent, WM_SETFONT, reinterpret_cast<WPARAM>(state.theme.bold), TRUE);
}

LRESULT HandleNotify(CatalogWindow &state, LPARAM lParam)
{
    const NMHDR *header = reinterpret_cast<const NMHDR *>(lParam);
    if (header->idFrom == IdList)
    {
        if (header->code == NM_CUSTOMDRAW)
        {
            NMLVCUSTOMDRAW *draw = reinterpret_cast<NMLVCUSTOMDRAW *>(lParam);
            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
                return CDRF_NOTIFYITEMDRAW;
            if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && !(draw->nmcd.uItemState & CDIS_SELECTED))
            {
                draw->clrTextBk = (draw->nmcd.dwItemSpec & 1) ? state.theme.alternateRow : state.theme.window;
                return CDRF_DODEFAULT;
            }
        }
        if (header->code == LVN_GETDISPINFOW)
        {
            NMLVDISPINFOW *info = reinterpret_cast<NMLVDISPINFOW *>(lParam);
            if (!(info->item.mask & LVIF_TEXT))
                return 0;
            const int index = info->item.iItem;
            if (index < 0 || static_cast<std::size_t>(index) >= state.filtered.size())
                return 0;
            const GuiPackage &package = state.catalog.packages[state.filtered[index]];
            const std::wstring *text = &package.name;
            if (info->item.iSubItem == 1)
                text = &package.id;
            else if (info->item.iSubItem == 2)
                text = &package.version;
            else if (info->item.iSubItem == 3)
                text = &package.tagText;
            info->item.pszText = const_cast<LPWSTR>(text->c_str());
            return 0;
        }
        if (header->code == LVN_ITEMCHANGED)
        {
            const NMLISTVIEW *view = reinterpret_cast<const NMLISTVIEW *>(lParam);
            if ((view->uChanged & LVIF_STATE) && (view->uNewState & LVIS_SELECTED))
            {
                state.worker.CancelDetails();
                state.generation = 0;
                StopSpinner(state);
                StopSizeAnimation(state);
                state.selected = view->iItem;
                UpdateActionButtons(state);
                SetTimer(state.window, TimerSelect, 160, nullptr);
            }
            return 0;
        }
        if (header->code == LVN_COLUMNCLICK)
        {
            const NMLISTVIEW *view = reinterpret_cast<const NMLISTVIEW *>(lParam);
            if (state.sortColumn == view->iSubItem)
                state.sortDescending = !state.sortDescending;
            else
                state.sortDescending = false;
            state.sortColumn = view->iSubItem;
            ApplyFilter(state);
            return 0;
        }
        if (header->code == NM_DBLCLK)
        {
            BeginOperation(state, true);
            return 0;
        }
    }
    else if (header->idFrom == IdTree && header->code == TVN_SELCHANGEDW && !state.buildingTree)
    {
        const NMTREEVIEWW *view = reinterpret_cast<const NMTREEVIEWW *>(lParam);
        state.category = view->itemNew.hItem ? static_cast<int>(view->itemNew.lParam) : -1;
        ApplyFilter(state);
    }
    return 0;
}

LRESULT CALLBACK CatalogProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    CatalogWindow *state = reinterpret_cast<CatalogWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const CREATESTRUCTW *create = reinterpret_cast<const CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return DefWindowProcW(window, message, wParam, lParam);
    }
    if (!state)
        return DefWindowProcW(window, message, wParam, lParam);

    switch (message)
    {
        case WM_CREATE:
            state->window = window;
            CreateChildren(*state);
            return 0;
        case WM_SIZE:
            LayoutWindow(*state);
            InvalidateRect(window, nullptr, TRUE);
            return 0;
        case WM_GETMINMAXINFO:
        {
            MINMAXINFO *info = reinterpret_cast<MINMAXINFO *>(lParam);
            info->ptMinTrackSize.x = 760;
            info->ptMinTrackSize.y = 520;
            return 0;
        }
        case WM_ERASEBKGND:
        {
            RECT client{};
            GetClientRect(window, &client);
            HDC dc = reinterpret_cast<HDC>(wParam);
            FillRect(dc, &client, state->theme.faceBrush);
            RECT header{0, 0, client.right, state->theme.Px(96)};
            FillRect(dc, &header, state->theme.windowBrush);
            RECT headerLine{0, header.bottom - 1, client.right, header.bottom};
            FillRect(dc, &headerLine, state->theme.lineBrush);
            const int markSize = state->theme.Px(36);
            if (state->theme.mark)
                DrawIconEx(dc, state->theme.Px(11), state->theme.Px(9), state->theme.mark, markSize, markSize, 0, nullptr, DI_NORMAL);
            if (state->operationVisible)
            {
                RECT statusRect{};
                GetWindowRect(state->status, &statusRect);
                const int statusHeight = statusRect.bottom - statusRect.top;
                RECT operation{0, client.bottom - statusHeight - state->theme.Px(86), client.right, client.bottom - statusHeight};
                FillRect(dc, &operation, state->theme.operationBrush);
                RECT operationLine{0, operation.top, client.right, operation.top + 1};
                FillRect(dc, &operationLine, state->theme.lineBrush);
                RECT accent{0, operation.top, state->theme.Px(4), operation.bottom};
                FillRect(dc, &accent, state->theme.accentBrush);
            }
            return 1;
        }
        case WM_CTLCOLORSTATIC:
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            HWND child = reinterpret_cast<HWND>(lParam);
            SetBkMode(dc, TRANSPARENT);
            if (child == state->operationTitle || child == state->operationDetail || child == state->operationPercent)
            {
                SetTextColor(dc, child == state->operationDetail ? state->theme.dim : state->theme.text);
                return reinterpret_cast<LRESULT>(state->theme.operationBrush);
            }
            if (child == state->appTitle || child == state->appSubtitle)
            {
                SetTextColor(dc, child == state->appSubtitle ? state->theme.dim : state->theme.text);
                return reinterpret_cast<LRESULT>(state->theme.windowBrush);
            }
            break;
        }
        case WM_SETFOCUS:
            SetFocus(state->list);
            return 0;
        case WM_NOTIFY:
            return HandleNotify(*state, lParam);
        case WM_TIMER:
            if (wParam == TimerSearch)
            {
                KillTimer(window, TimerSearch);
                wchar_t buffer[256]{};
                GetWindowTextW(state->search, buffer, 255);
                const std::string text = AsciiLower(Trim(Utf8FromWide(buffer)));
                if (text != state->query)
                {
                    state->query = text;
                    ApplyFilter(*state);
                }
                return 0;
            }
            if (wParam == TimerSelect)
            {
                KillTimer(window, TimerSelect);
                ShowDetails(*state);
                return 0;
            }
            if (wParam == TimerSpinner)
            {
                state->info.spinner = (state->info.spinner + 1) % 8;
                InvalidateRect(state->info.window, nullptr, FALSE);
                return 0;
            }
            if (wParam == TimerSize)
            {
                AdvanceSizeAnimation(*state);
                return 0;
            }
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IdSearch:
                    if (HIWORD(wParam) == EN_CHANGE)
                        SetTimer(window, TimerSearch, 140, nullptr);
                    return 0;
                case IdInstall:
                    BeginOperation(*state, true);
                    return 0;
                case IdDownload:
                    BeginOperation(*state, false);
                    return 0;
                case IdRefresh:
                    if (!state->loading)
                    {
                        state->loading = true;
                        state->cache.clear();
                        state->worker.CancelDetails();
                        state->generation = 0;
                        StopSpinner(*state);
                        StopSizeAnimation(*state);
                        UpdateStatusBar(*state);
                        UpdateActionButtons(*state);
                        ShowBackgroundProgress(*state, L"Loading winget catalog", L"Refreshing the signed package index…");
                        state->worker.RequestCatalog(true);
                    }
                    return 0;
                case IdInstalled:
                {
                    const Status status = ShowInstalledAppsManager(window);
                    if (!status)
                    {
                        const std::wstring message = WideFromUtf8(status.message);
                        MessageBoxW(window, message.c_str(), L"Installed apps", MB_OK | MB_ICONERROR);
                    }
                    return 0;
                }
                case IDOK:
                    if (GetFocus() == state->search)
                        SetFocus(state->list);
                    else
                        BeginOperation(*state, true);
                    return 0;
                case IDCANCEL:
                    if (GetWindowTextLengthW(state->search))
                    {
                        SetWindowTextW(state->search, L"");
                        SetFocus(state->search);
                    }
                    else
                    {
                        DestroyWindow(window);
                    }
                    return 0;
                default:
                    return 0;
            }
        case WM_SETCURSOR:
        {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(window, &point);
            const int splitter = state->theme.Px(6);
            RECT treeRect{};
            GetWindowRect(state->tree, &treeRect);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&treeRect), 2);
            if (point.y >= treeRect.top && point.y < treeRect.bottom && point.x >= treeRect.right && point.x < treeRect.right + splitter)
            {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
            RECT listRect{};
            GetWindowRect(state->list, &listRect);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&listRect), 2);
            if (point.x > state->treeWidth && point.y >= listRect.bottom && point.y < listRect.bottom + splitter)
            {
                SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
                return TRUE;
            }
            break;
        }
        case WM_LBUTTONDOWN:
        {
            const POINT point{static_cast<LONG>(static_cast<short>(LOWORD(lParam))), static_cast<LONG>(static_cast<short>(HIWORD(lParam)))};
            const int splitter = state->theme.Px(6);
            RECT treeRect{};
            GetWindowRect(state->tree, &treeRect);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&treeRect), 2);
            if (point.y >= treeRect.top && point.y < treeRect.bottom && point.x >= treeRect.right && point.x < treeRect.right + splitter)
            {
                state->drag = 1;
                SetCapture(window);
                return 0;
            }
            RECT listRect{};
            GetWindowRect(state->list, &listRect);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&listRect), 2);
            if (point.x > state->treeWidth && point.y >= listRect.bottom && point.y < listRect.bottom + splitter)
            {
                state->drag = 2;
                SetCapture(window);
            }
            return 0;
        }
        case WM_MOUSEMOVE:
        {
            if (!state->drag)
                break;
            const POINT point{static_cast<LONG>(static_cast<short>(LOWORD(lParam))), static_cast<LONG>(static_cast<short>(HIWORD(lParam)))};
            RECT client{};
            GetClientRect(window, &client);
            if (state->drag == 1)
            {
                state->treeWidth = point.x;
            }
            else
            {
                RECT statusRect{};
                GetWindowRect(state->status, &statusRect);
                const int operationHeight = state->operationVisible ? state->theme.Px(86) : 0;
                state->infoHeight = client.bottom - point.y - (statusRect.bottom - statusRect.top) - operationHeight;
            }
            LayoutWindow(*state);
            return 0;
        }
        case WM_LBUTTONUP:
            if (state->drag)
            {
                state->drag = 0;
                ReleaseCapture();
            }
            return 0;
        case MessageCatalogReady:
        {
            std::unique_ptr<CatalogResult> result(reinterpret_cast<CatalogResult *>(lParam));
            state->loading = false;
            HideBackgroundProgress(*state);
            UpdateActionButtons(*state);
            if (!result->ok)
            {
                state->failure = result->error;
                UpdateStatusBar(*state);
                InfoBlock title;
                title.kind = BlockKind::Title;
                title.text = L"The winget catalog is not available";
                InfoBlock reason;
                reason.kind = BlockKind::Body;
                reason.text = WideFromUtf8(result->error);
                InfoBlock hint;
                hint.kind = BlockKind::Status;
                hint.text = L"Run \u201crosget source update\u201d, or import a source2.msix, then press Refresh.";
                state->info.blocks.clear();
                state->info.blocks.push_back(std::move(title));
                state->info.blocks.push_back(std::move(reason));
                state->info.blocks.push_back(std::move(hint));
                RelayoutInfo(state->info);
                return 0;
            }
            state->failure.clear();
            state->catalog = std::move(result->catalog);
            state->placeholder = L"Search " + FormatCount(state->catalog.packages.size()) + L" packages…";
            InvalidateRect(state->search, nullptr, TRUE);
            BuildTree(*state);
            ApplyFilter(*state);
            if (state->selectFirst && !state->filtered.empty())
            {
                state->selectFirst = false;
                SetFocus(state->list);
                ListView_SetItemState(state->list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(state->list, 0, FALSE);
            }
            std::wstring caption = L"rosget — winget catalog (" + FormatCount(state->catalog.packages.size()) + L" packages)";
            SetWindowTextW(window, caption.c_str());
            return 0;
        }
        case MessageDetailsReady:
        {
            std::unique_ptr<DetailResult> result(reinterpret_cast<DetailResult *>(lParam));
            if (static_cast<std::uint32_t>(wParam) != state->generation)
                return 0;
            if (state->selected < 0 || static_cast<std::size_t>(state->selected) >= state->filtered.size())
                return 0;
            const GuiPackage &package = state->catalog.packages[state->filtered[state->selected]];
            if (result->stage >= 2)
            {
                StopSpinner(*state);
                HideBackgroundProgress(*state);
            }
            if (result->stage >= 3)
            {
                state->cache[package.utf8Id + "/" + package.utf8Version] = *result;
            }
            SetInfoBlocks(*state, package, result.get());
            if (result->sizePending)
                StartSizeAnimation(*state);
            else
                StopSizeAnimation(*state);
            return 0;
        }
        case MessageOperationUpdate:
        {
            std::unique_ptr<OperationUpdate> update(reinterpret_cast<OperationUpdate *>(lParam));
            if (static_cast<std::uint32_t>(wParam) != state->operationGeneration)
                return 0;
            ApplyOperationUpdate(*state, *update);
            InvalidateRect(window, nullptr, TRUE);
            return 0;
        }
        case WM_DESTROY:
            StopSpinner(*state);
            StopSizeAnimation(*state);
            state->worker.Stop();
            if (state->treeImages)
                ImageList_Destroy(state->treeImages);
            state->theme.Destroy();
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

Status RunCatalogGui(SourceManager &source, const std::string &initialQuery)
{
    FreeConsole();

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    CatalogWindow state;
    state.source = &source;
    state.query = AsciiLower(Trim(initialQuery));
    state.selectFirst = !state.query.empty();
    state.theme.Create();

    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW pane{};
    pane.cbSize = sizeof(pane);
    pane.lpfnWndProc = InfoPaneProc;
    pane.hInstance = instance;
    pane.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    pane.lpszClassName = L"RosGetInfoPane";
    RegisterClassExW(&pane);

    WNDCLASSEXW catalog{};
    catalog.cbSize = sizeof(catalog);
    catalog.lpfnWndProc = CatalogProc;
    catalog.hInstance = instance;
    catalog.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    catalog.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_ROSGET));
    catalog.hIconSm = reinterpret_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_ROSGET), IMAGE_ICON,
                                                         GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    catalog.lpszClassName = L"RosGetCatalog";
    if (!RegisterClassExW(&catalog))
    {
        const DWORD error = GetLastError();
        state.theme.Destroy();
        return Status::Fail(error, "cannot register the catalog window class: " + WindowsErrorMessage(error));
    }

    RECT work{0, 0, 1024, 720};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = std::min<int>(state.theme.Px(1040), work.right - work.left - 40);
    const int height = std::min<int>(state.theme.Px(700), work.bottom - work.top - 40);
    HWND window = CreateWindowExW(0, L"RosGetCatalog", L"rosget — winget catalog",
                                  WS_OVERLAPPEDWINDOW,
                                  work.left + (work.right - work.left - width) / 2,
                                  work.top + (work.bottom - work.top - height) / 2,
                                  width, height, nullptr, nullptr, instance, &state);
    if (!window)
    {
        const DWORD error = GetLastError();
        state.theme.Destroy();
        return Status::Fail(error, "cannot create the catalog window: " + WindowsErrorMessage(error));
    }

    if (!state.query.empty())
        SetWindowTextW(state.search, WideFromUtf8(state.query).c_str());
    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);
    UpdateStatusBar(state);

    MessageBoxW(window,
                L"The winget catalog contains applications published for Windows. Some packages may not install or run correctly on ReactOS yet.",
                L"rosget compatibility notice", MB_OK | MB_ICONWARNING);

    if (!state.worker.Start(window, &source))
    {
        DestroyWindow(window);
        return Status::Fail(ERROR_NOT_ENOUGH_MEMORY, "cannot start the catalog worker thread");
    }
    ShowBackgroundProgress(state, L"Loading winget catalog", L"Opening the signed package index…");
    state.worker.RequestCatalog(false);
    UpdateActionButtons(state);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (message.message == WM_KEYDOWN && message.wParam == 'F' && (GetKeyState(VK_CONTROL) & 0x8000))
        {
            SetFocus(state.search);
            SendMessageW(state.search, EM_SETSEL, 0, -1);
            continue;
        }
        if (IsDialogMessageW(window, &message))
            continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return Status::Ok();
}

} // namespace rosget
