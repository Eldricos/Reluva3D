// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/game_text.h"

#include "common/fs.h"
#include "engines/awe/tables.h"

#include <cstdlib>

namespace reluva::awe {

namespace {

// The fonts fonts.txt names, and the files of Interface/Fonts that hold
// them; Times New Roman and Courier New aren't shipped (Windows has them).
const char* fontFile(const std::string& name) {
    const std::string n = fs::toLower(name);
    if (n == "calisto mt")
        return "interface/fonts/calist.ttf";
    if (n == "arial narrow")
        return "interface/fonts/arialn.ttf";
    return "interface/fonts/arial.ttf";
}

} // namespace

Error GameFonts::load(const Archive& files) {
    std::vector<uint8_t> data;
    if (Error e = files.read("text/fonts.txt", data))
        return e;
    const Table t = parseTable(decodeText(data));
    auto loadFont = [&](const std::string& file) -> ttf::Font* {
        auto it = fonts_.find(file);
        if (it == fonts_.end()) {
            std::vector<uint8_t> bytes;
            auto font = std::make_unique<ttf::Font>();
            if (files.read(file, bytes) || font->load(std::move(bytes)))
                font.reset();
            it = fonts_.emplace(file, std::move(font)).first;
        }
        return it->second.get();
    };
    // Two kinds of row under the same headings: a font (name, size, "bold"
    // or not, "shadow" or not, its use), then colours (A, R, G, B, use).
    for (const auto& row : t.rows) {
        if (row.size() < 5)
            continue;
        char* end = nullptr;
        const long first = std::strtol(row[0].c_str(), &end, 10);
        if (end && *end == 0 && !row[0].empty()) {
            const Color c{uint8_t(std::atoi(row[1].c_str())), uint8_t(std::atoi(row[2].c_str())),
                          uint8_t(std::atoi(row[3].c_str()))};
            (void)first; // alpha
            colours_.emplace_back(fs::toLower(row[4]), c);
            if (fs::toLower(row[4]) == "text shadow")
                shadow_ = c;
            continue;
        }
        TextStyle s;
        s.fontName = row[0];
        s.points = float(std::atof(row[1].c_str()));
        s.bold = fs::toLower(row[2]) == "bold";
        s.shadow = fs::toLower(row[3]) == "shadow";
        s.use = fs::toLower(row[4]);
        s.font = loadFont(fontFile(s.fontName));
        if (!s.font)
            s.font = loadFont("interface/fonts/arial.ttf");
        s.pixels = s.font ? s.font->sizeForPoints(s.points) : s.points;
        styles_.push_back(std::move(s));
    }
    return Error::success();
}

const TextStyle* GameFonts::style(std::string_view use) const {
    const std::string want = fs::toLower(use);
    for (const TextStyle& s : styles_)
        if (s.use.rfind(want, 0) == 0)
            return &s;
    return nullptr;
}

Color GameFonts::colour(std::string_view use, Color fallback) const {
    const std::string want = fs::toLower(use);
    for (const auto& [u, c] : colours_)
        if (u == want)
            return c;
    return fallback;
}

int GameFonts::draw(Surface& dst, const TextStyle& style, int x, int y, std::string_view utf8, Color colour) const {
    if (!style.font)
        return x;
    if (style.shadow)
        style.font->draw(dst, x + 1, y + 1, utf8, style.pixels, shadow_);
    // No bold font is shipped: drawn twice, a pixel apart.
    if (style.bold)
        style.font->draw(dst, x + 1, y, utf8, style.pixels, colour);
    return style.font->draw(dst, x, y, utf8, style.pixels, colour);
}

std::vector<Subtitle> parseSubtitles(std::string_view text) {
    std::vector<Subtitle> out;
    const Table t = parseTable(text);
    const int speaker = t.column("Speaker"), line = t.column("Line"), duration = t.column("Duration");
    if (line < 0 || duration < 0)
        return out;
    for (size_t r = 0; r < t.rows.size(); r++) {
        Subtitle s;
        s.speaker = t.cell(r, speaker);
        // The speaker cells have stray spaces ("Moinsieur Bouc ").
        while (!s.speaker.empty() && s.speaker.back() == ' ')
            s.speaker.pop_back();
        s.line = t.cell(r, line);
        const std::string& d = t.cell(r, duration);
        char* end = nullptr;
        s.start = std::strtod(d.c_str(), &end);
        if (!end || *end != ',' || s.line.empty())
            continue;
        s.end = s.start + std::strtod(end + 1, nullptr);
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace reluva::awe
