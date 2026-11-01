// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/game_text.h"

#include "common/fs.h"
#include "engines/awe/tables.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace reluva::awe {

namespace {

// The fonts fonts.txt names, and the files of Interface/Fonts that hold
// them; Times New Roman and Courier New aren't shipped (Windows has them:
// systemFontFiles).
const char* fontFile(const std::string& name) {
    const std::string n = fs::toLower(name);
    if (n == "calisto mt")
        return "interface/fonts/calist.ttf";
    if (n == "arial narrow")
        return "interface/fonts/arialn.ttf";
    return "interface/fonts/arial.ttf";
}

// Where a font the game names but doesn't ship may be on this computer:
// Windows' own file (the game takes it from Windows), the same file on
// macOS or from Linux's core fonts packages, then Liberation's, made to the
// same measurements, so text sets and wraps the same.
std::vector<std::filesystem::path> systemFontFiles(const std::string& name, bool bold) {
    const std::string n = fs::toLower(name);
    struct Names {
        const char *win, *mac, *core, *coreLower, *liberation;
    };
    Names f;
    if (n == "times new roman")
        f = bold ? Names{"timesbd.ttf", "Times New Roman Bold.ttf", "Times_New_Roman_Bold.ttf", "timesbd.ttf",
                         "LiberationSerif-Bold.ttf"}
                 : Names{"times.ttf", "Times New Roman.ttf", "Times_New_Roman.ttf", "times.ttf",
                         "LiberationSerif-Regular.ttf"};
    else if (n == "courier new")
        f = bold ? Names{"courbd.ttf", "Courier New Bold.ttf", "Courier_New_Bold.ttf", "courbd.ttf",
                         "LiberationMono-Bold.ttf"}
                 : Names{"cour.ttf", "Courier New.ttf", "Courier_New.ttf", "cour.ttf", "LiberationMono-Regular.ttf"};
    else
        return {};
    std::vector<std::filesystem::path> out;
    for (const char* var : {"WINDIR", "SystemRoot"})
        if (const char* dir = std::getenv(var))
            out.push_back(std::filesystem::path(dir) / "Fonts" / f.win);
    for (const char* dir : {"/System/Library/Fonts/Supplemental", "/Library/Fonts"})
        out.push_back(std::filesystem::path(dir) / f.mac);
    if (const char* home = std::getenv("HOME"))
        out.push_back(std::filesystem::path(home) / "Library/Fonts" / f.mac);
    for (const char* dir : {"/usr/share/fonts/truetype/msttcorefonts", "/usr/share/fonts/msttcore",
                            "/usr/share/fonts/msttcorefonts"})
        out.push_back(std::filesystem::path(dir) / f.core);
    for (const char* dir : {"/usr/share/fonts/TTF", "/usr/share/fonts/truetype/msttcorefonts"})
        out.push_back(std::filesystem::path(dir) / f.coreLower);
    const bool serif = n == "times new roman";
    for (const char* dir : {"/usr/share/fonts/truetype/liberation", "/usr/share/fonts/truetype/liberation2",
                            serif ? "/usr/share/fonts/liberation-serif" : "/usr/share/fonts/liberation-mono",
                            "/usr/share/fonts/liberation", "/usr/share/fonts/TTF", "/usr/local/share/fonts"})
        out.push_back(std::filesystem::path(dir) / f.liberation);
    return out;
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
        // Not shipped (the newspaper's Times New Roman, the fingerprint
        // cards' Courier New): this computer's, else Arial.
        if (const auto candidates = systemFontFiles(s.fontName, s.bold); !candidates.empty()) {
            s.font = nullptr;
            for (const auto& path : candidates) {
                const std::string key = path.string();
                auto it = fonts_.find(key);
                if (it == fonts_.end()) {
                    std::unique_ptr<ttf::Font> font;
                    std::ifstream in(path, std::ios::binary);
                    if (in) {
                        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                        font = std::make_unique<ttf::Font>();
                        if (font->load(std::move(bytes)))
                            font.reset();
                    }
                    it = fonts_.emplace(key, std::move(font)).first;
                }
                if (it->second) {
                    s.font = it->second.get();
                    s.boldFace = s.bold;
                    break;
                }
            }
        }
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

Color GameFonts::colourAt(size_t index, Color fallback) const {
    return index < colours_.size() ? colours_[index].second : fallback;
}

int GameFonts::draw(Surface& dst, const TextStyle& style, int x, int y, std::string_view utf8, Color colour,
                    uint8_t alpha) const {
    if (!style.font)
        return x;
    if (style.shadow)
        style.font->draw(dst, x + 1, y + 1, utf8, style.pixels, shadow_, alpha);
    // No bold font is shipped: drawn twice, a pixel apart (unless the font
    // is a bold face itself).
    if (style.bold && !style.boldFace)
        style.font->draw(dst, x + 1, y, utf8, style.pixels, colour, alpha);
    return style.font->draw(dst, x, y, utf8, style.pixels, colour, alpha);
}

std::vector<Subtitle> parseSubtitles(std::string_view text) {
    std::vector<Subtitle> out;
    const Table t = parseTable(text);
    const int name = t.column("Name"), speaker = t.column("Speaker"), line = t.column("Line"),
              duration = t.column("Duration");
    if (line < 0 || duration < 0)
        return out;
    for (size_t r = 0; r < t.rows.size(); r++) {
        Subtitle s;
        s.name = name < 0 ? std::string() : t.cell(r, name);
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
