// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/truetype.h"
#include "common/types.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// The game's text as it shows it: its fonts and their uses
// (Text/fonts.txt, the fonts in Interface/Fonts), and the subtitles of
// its movies (Text/Movies/<movie>.txt). See docs/formats/awe/text.md.

namespace reluva::awe {

// A use of text, as fonts.txt lists them: "dialogue", "location
// descriptions"...
struct TextStyle {
    std::string use;
    std::string fontName;
    float points = 12;
    bool bold = false, shadow = false;
    ttf::Font* font = nullptr; // null if the game doesn't ship it (and Arial doesn't load)
    float pixels = 16;         // the size for font's draw calls
};

class GameFonts {
public:
    // Reads fonts.txt and loads the fonts it names that the game has.
    Error load(const Archive& files);

    // The style whose use starts with `use` ("dialogue"), or null.
    const TextStyle* style(std::string_view use) const;
    // A colour by its use ("dialogue for main character"), or `fallback`.
    Color colour(std::string_view use, Color fallback = {255, 255, 255}) const;
    const std::vector<TextStyle>& styles() const { return styles_; }

    // Draws text in a style: its shadow (one pixel down and right, the
    // shadow colour) first if it has one.
    int draw(Surface& dst, const TextStyle& style, int x, int y, std::string_view utf8, Color colour) const;

private:
    std::map<std::string, std::unique_ptr<ttf::Font>> fonts_; // by file name
    std::vector<TextStyle> styles_;
    std::vector<std::pair<std::string, Color>> colours_;
    Color shadow_{0, 0, 0};
};

// A movie's subtitles: who speaks, what, and when (seconds from its start).
struct Subtitle {
    std::string speaker, line;
    double start = 0, end = 0;
};
// Reads a movie's subtitle table (its Speaker, Line and Duration columns;
// Duration is "start, length").
std::vector<Subtitle> parseSubtitles(std::string_view text);

} // namespace reluva::awe
