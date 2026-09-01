/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Package detail parser self-tests
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#include "details.hpp"
#include "manifest.hpp"

namespace rosget
{

Status RunDetailsSelfTests()
{
    static constexpr std::string_view Fixture =
        "PackageIdentifier: VideoLAN.VLC\n"
        "PackageVersion: 3.0.21\n"
        "PackageLocale: en-US\n"
        "Publisher: VideoLAN\n"
        "PublisherUrl: https://www.videolan.org/\n"
        "Author: VideoLAN\n"
        "PackageName: VLC media player\n"
        "PackageUrl: https://www.videolan.org/vlc/\n"
        "License: GPL-2.0-only\n"
        "Copyright: Copyright (C) 1996-2024 VideoLAN\n"
        "ShortDescription: VLC is a free and open source cross-platform multimedia player\n"
        "Description: |-\n"
        "  VLC is a free and open source cross-platform multimedia player and framework\n"
        "  that plays most multimedia files.\n"
        "\n"
        "  It also plays DVDs, Audio CDs, VCDs, and various streaming protocols.\n"
        "Tags:\n"
        "- multimedia\n"
        "- player\n"
        "- video\n"
        "ManifestType: defaultLocale\n"
        "ManifestVersion: 1.6.0\n";

    PackageDetails details;
    Status status = ParseLocaleManifest(Fixture, details);
    if (!status) return status;
    if (details.publisher != "VideoLAN" || details.author != "VideoLAN" || details.license != "GPL-2.0-only")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "locale manifest scalar self-test failed");
    if (details.packageUrl != "https://www.videolan.org/vlc/" || details.publisherUrl != "https://www.videolan.org/")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "locale manifest URL self-test failed");
    if (details.description.find("streaming protocols.") == std::string::npos ||
        details.description.find("\n\n") == std::string::npos || details.description.back() == '\n')
        return Status::Fail(ERROR_ASSERTION_FAILURE, "locale manifest block scalar self-test failed");
    if (details.tags.size() != 3 || details.tags.front() != "multimedia" || details.tags.back() != "video")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "locale manifest tag self-test failed");

    static constexpr std::string_view FlowFixture =
        "Author: VideoLAN\n"
        "Description: \"Line one\\nLine two\n"
        "  continues here.\\n\\nNew paragraph.\"\n"
        "ManifestType: merged\n"
        "ShortDescription: VLC is a free and open source cross-platform multimedia player and\n"
        "  framework that plays most multimedia files.\n"
        "Tags:\n"
        "- alpha\n"
        "- beta\n";

    PackageDetails merged;
    status = ParseLocaleManifest(FlowFixture, merged);
    if (!status) return status;
    if (merged.shortDescription != "VLC is a free and open source cross-platform multimedia player and framework that plays most multimedia files.")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "merged manifest folded scalar self-test failed");
    if (merged.description != "Line one\nLine two continues here.\n\nNew paragraph.")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "merged manifest quoted scalar self-test failed");
    if (merged.tags.size() != 2 || merged.tags.front() != "alpha")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "merged manifest tag self-test failed");
    return Status::Ok();
}

} // namespace rosget
