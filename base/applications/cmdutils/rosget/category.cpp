/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Maps WinGet tags onto browsable catalog categories
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#define NOMINMAX
#include <windows.h>

#include "category.hpp"
#include "util.hpp"

#include <functional>

namespace rosget
{

namespace
{

const CategoryDefinition Definitions[] = {
    {L"Audio", RGB(0x2E, 0x9E, 0x6B),
     "audio music sound mp3 flac wav player media-player daw dj podcast radio equalizer midi mixer voice speaker "
     "soundboard audio-player music-player audiobook karaoke recording audio-editor tagger streaming-audio",
     "audio music sound podcast"},
    {L"Video", RGB(0x2B, 0x7C, 0xD6),
     "video movie movies streaming youtube ffmpeg codec mkv subtitle subtitles screen-recorder recorder transcode "
     "dvd webcam video-player video-editor screencast livestream obs media player multimedia",
     "video movie subtitle stream"},
    {L"Graphics", RGB(0xB4, 0x53, 0xC9),
     "graphics image images photo photography design drawing paint svg vector render rendering animation icon icons "
     "screenshot gif raw art texture 3d modeling illustration image-editor photo-editor imaging viewer",
     "graphic image photo draw paint render"},
    {L"Games & Fun", RGB(0xC9, 0x5A, 0x2E),
     "game games gaming emulator emulation minecraft steam rpg retro chess puzzle arcade roguelike modding "
     "speedrun game-engine gamedev controller",
     "game emulat"},
    {L"Internet & Network", RGB(0x1F, 0x8A, 0xB0),
     "internet browser web-browser webbrowser browsers network networking download downloader torrent ftp ssh sftp "
     "vpn proxy chat email mail messenger irc rss dns http remote-desktop rdp social web webpage wifi firewall p2p "
     "telnet curl xmpp matrix voip webserver",
     "browser network internet download torrent chat mail remote"},
    {L"Office", RGB(0x35, 0x6C, 0xB8),
     "office document documents pdf text word spreadsheet presentation notes note-taking markdown ebook epub "
     "dictionary calendar todo writing latex organizer productivity outliner mindmap diagram wiki knowledge-base "
     "ocr translation reader",
     "office document pdf note writ spreadsheet"},
    {L"Development", RGB(0x50, 0x6A, 0xD4),
     "development developer-tools programming ide editor code coding compiler sdk git version-control debugger "
     "python java dotnet javascript nodejs node rust golang php ruby perl database sql api devops docker kubernetes "
     "terminal cli tui shell testing regex json xml yaml diff merge lsp text-editor develop toolchain build",
     "develop program coding compiler debug git sdk database"},
    {L"AI & LLM", RGB(0x8A, 0x4F, 0xD8),
     "ai llm large-language-model artificial-intelligence machine-learning chatbot gpt openai stable-diffusion "
     "agent rag transformers neural-network deep-learning copilot inference embeddings",
     "llm chatbot artificial-intelligence"},
    {L"Edutainment", RGB(0xC0, 0x8A, 0x1E),
     "education learning teaching kids language typing school university courses flashcards e-learning study "
     "children reference tutorial",
     "educat learn teach study"},
    {L"Engineering", RGB(0x6E, 0x7A, 0x8A),
     "engineering cad electronics pcb simulation robotics plc gis mechanical architecture 3d-printing eda "
     "hardware-design surveying bim cnc",
     "engineer electronic simulat robot"},
    {L"Finance", RGB(0x2F, 0x8F, 0x4E),
     "finance accounting money banking cryptocurrency bitcoin ethereum invoice invoicing trading budget tax "
     "wallet stocks payments erp point-of-sale",
     "financ account money cryptocurrency bank invoic budget"},
    {L"Science", RGB(0x1B, 0x92, 0x9B),
     "science math mathematics statistics chemistry physics biology astronomy research data-analysis plotting "
     "medical bioinformatics numerical geospatial neuroscience dataset visualization",
     "scien math statist chemi physic biolog astronom research"},
    {L"Tools", RGB(0x77, 0x74, 0x6E),
     "utility utilities tools tool backup compression archive archiver file file-manager filemanager system "
     "monitoring benchmark cleaner disk antivirus security encryption password clipboard automation search sync "
     "synchronization virtualization hardware performance uninstaller task-manager launcher keyboard mouse "
     "window-manager partition recovery iso usb serial logging scheduler system-information",
     "utilit tool backup archiv compress file system monitor secur password encrypt"},
    {L"Drivers", RGB(0x8C, 0x6D, 0x3F),
     "driver drivers firmware printer printing gpu chipset device bios peripheral",
     "driver firmware printer"},
    {L"Libraries", RGB(0x5F, 0x6B, 0x7A),
     "library libraries runtime framework redistributable dependencies vcredist jre jdk openjdk toolkit component "
     "dotnet-runtime java-runtime interpreter",
     "runtime redistributable framework librar"},
    {L"Themes", RGB(0xC2, 0x4F, 0x86),
     "theme themes icons wallpaper wallpapers customization skin rainmeter desktop-customization cursor font fonts "
     "appearance shell-customization ricing",
     "theme wallpaper customiz font cursor"},
    {L"Other", RGB(0x88, 0x88, 0x88), "", ""},
};

void SplitWords(const char *text, const std::function<void(std::string)> &sink)
{
    std::string word;
    for (const char *cursor = text; ; ++cursor)
    {
        if (*cursor && *cursor != ' ')
        {
            word.push_back(*cursor);
            continue;
        }
        if (!word.empty())
        {
            sink(word);
            word.clear();
        }
        if (!*cursor)
            break;
    }
}

} // namespace

const CategoryDefinition *CategoryTable()
{
    return Definitions;
}

std::size_t CategoryCount()
{
    return sizeof(Definitions) / sizeof(Definitions[0]);
}

std::size_t OtherCategoryIndex()
{
    return CategoryCount() - 1;
}

CategoryClassifier::CategoryClassifier()
{
    for (std::size_t index = 0; index < CategoryCount(); ++index)
    {
        const std::uint32_t bit = 1u << index;
        SplitWords(Definitions[index].tags, [this, bit](std::string word) { exact_[word] |= bit; });
        SplitWords(Definitions[index].roots, [this, bit](std::string word) { roots_.emplace_back(std::move(word), bit); });
    }
}

std::uint32_t CategoryClassifier::Classify(const std::vector<std::uint32_t> &tags, const std::vector<std::string> &names) const
{
    std::uint32_t mask = 0;
    for (const std::uint32_t tag : tags)
    {
        if (tag >= names.size())
            continue;
        const auto match = exact_.find(names[tag]);
        if (match != exact_.end())
            mask |= match->second;
    }
    if (mask)
        return mask;

    for (const std::uint32_t tag : tags)
    {
        if (tag >= names.size())
            continue;
        for (const auto &root : roots_)
        {
            if (names[tag].find(root.first) != std::string::npos)
                mask |= root.second;
        }
    }
    return mask ? mask : (1u << OtherCategoryIndex());
}

std::uint32_t CategoryClassifier::ClassifyText(std::string_view loweredText) const
{
    std::uint32_t mask = 0;
    for (const auto &root : roots_)
    {
        if (loweredText.find(root.first) != std::string_view::npos)
            mask |= root.second;
    }
    return mask ? mask : (1u << OtherCategoryIndex());
}

} // namespace rosget
