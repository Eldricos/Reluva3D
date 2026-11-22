// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/nfs2se/status_page.h"

#include "engines/nfs/ui/page_draw.h"

#include <algorithm>

namespace reluva::nfs::nfs2se {

namespace {

using namespace page;

std::string join(const std::vector<std::string> &names) {
    std::string out;
    for (const std::string &n : names)
        out += (out.empty() ? "" : " ") + n;
    return out;
}

} // namespace

StatusPage::StatusPage(GameFolders folders) : _folders(std::move(folders)) {
    _programs = programs(_folders);
    _music = filesWithExtension(_folders.cd, "GameData/Audio/pc", ".mus");
    _movies = filesWithExtension(_folders.cd, "FeData/pc/movies", ".dct");
    _tracks = tracks(_folders);
    _cars = carModels(_folders);
}

void StatusPage::draw(Surface &s) {
    clear(s);
    text(s, kMargin, 8, "Need for Speed II SE", kTitleColor);
    constexpr int kValueX = kMargin + 12 * font::kAdvance;
    const size_t width = static_cast<size_t>((s.w - kValueX - kMargin) / font::kAdvance);
    int y = 8 + font::kLineHeight * 2;
    // A long value goes on over as many lines as it takes, broken at a
    // space where there's one.
    auto line = [&](const std::string &label, const std::string &value, Color color) {
        text(s, kMargin, y, label, kLabelColor);
        size_t at = 0;
        do {
            size_t take = std::min(width, value.size() - at);
            if (at + take < value.size()) {
                const size_t space = value.rfind(' ', at + take);
                if (space != std::string::npos && space > at)
                    take = space - at;
            }
            text(s, kValueX, y, value.substr(at, take), color);
            y += font::kLineHeight;
            at += take;
            while (at < value.size() && value[at] == ' ')
                ++at;
        } while (at < value.size());
    };
    auto where = [&](const std::filesystem::path &file) {
        if (file.empty())
            return std::string("not found");
        const std::filesystem::path fromCd = file.lexically_relative(_folders.cd);
        const bool onCd =
            !_folders.cd.empty() && _folders.cd != _folders.install && !fromCd.empty() && *fromCd.begin() != "..";
        return file.filename().string() + (onCd ? " (on the CD)" : "");
    };

    line("Install", _folders.install.string(), kTextColor);
    if (_folders.cd.empty()) {
        line("CD files", "not found: no tracks, music or movies", kWarnColor);
        line("", "Set cdpath=<folder> in this game's section of reluva.ini,", kLabelColor);
        line("", "or put a copy of the CD beside the install.", kLabelColor);
    } else {
        line("CD files", _folders.cd.string(), kTextColor);
        line("", "(" + _folders.cdSource + ")", kLabelColor);
        line("Music", std::to_string(_music.size()) + " streams", kTextColor);
        line("Movies", std::to_string(_movies.size()) + " files", kTextColor);
    }
    line("Software", where(_programs.software), _programs.software.empty() ? kWarnColor : kTextColor);
    line("3dfx", where(_programs.glide), kTextColor);
    y += font::kLineHeight;
    line("Tracks", std::to_string(_tracks.size()) + ": " + join(_tracks), kTextColor);
    line("Car models", std::to_string(_cars.size()) + ": " + join(_cars), kTextColor);
    y += font::kLineHeight;
    line("", "Reluva can't play Need for Speed II SE yet.", kWarnColor);
    text(s, kMargin, s.h - font::kLineHeight - 4, "Esc: back", kLabelColor);
}

} // namespace reluva::nfs::nfs2se
