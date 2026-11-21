// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/font.h"

#include "engines/stawayteam/formats/binary_object.h"

#include <algorithm>
#include <cctype>

namespace reluva::stawayteam {

Error Font::load(const Archive &data, const std::string &name) {
    std::vector<uint8_t> bytes;
    const std::string path = "Cache/Fonts/" + name + ".seq16";
    if (Error err = data.read(path, bytes))
        return err;
    BinaryObject font;
    if (Error err = readBinaryObject(bytes.data(), bytes.size(), "", font))
        return Error::fail(path + ": " + err.message);
    if (font.object.className != "CFont" || !font.sequence || font.sequence->frames.empty())
        return Error::fail(path + ": not a font");
    _first = int(font.object.integer("First ASCII").value_or(32));
    _last = int(font.object.integer("Last ASCII").value_or(125));
    _fixedWidth = font.object.integer("Fixed Width").value_or(0) != 0;
    const auto &box = font.sequence->box;
    _height = box[3] - box[1];
    // CFont's own number before First ASCII has no name; the button's
    // underline goes a row below it (0x5cd191), so it's the baseline.
    _baseline = _height;
    const auto &fields = font.object.fields;
    for (size_t i = 1; i < fields.size(); ++i)
        if (fields[i].key == "First ASCII" && fields[i - 1].key.empty())
            _baseline = std::atoi(fields[i - 1].value.c_str());
    _glyphs.clear();
    _cellWidth = 0;
    for (const auto &frame : font.sequence->frames) {
        _glyphs.push_back(frame ? frameImage(*frame) : image::Image{});
        _cellWidth = std::max(_cellWidth, _glyphs.back().w);
    }
    return Error::success();
}

const image::Image *Font::glyph(unsigned char c) const {
    int code = c;
    // A font without lower case has them drawn as capitals (0x4b15bc).
    if (_last + 1 < 0x60)
        code = std::toupper(code);
    // Control characters are left out (0x4b15ce); any other the font
    // hasn't got is drawn as its first.
    if (code < 0x14)
        return nullptr;
    size_t i = size_t(code - _first);
    if (code < _first || i >= _glyphs.size())
        i = 0;
    return &_glyphs[i];
}

int Font::width(std::string_view text) const {
    int w = 0;
    for (char c : text)
        if (const image::Image *g = glyph(static_cast<unsigned char>(c)))
            w += _fixedWidth ? _cellWidth : g->w;
    return w;
}

void Font::draw(Surface &screen, int x, int y, std::string_view text) const {
    for (char c : text) {
        const image::Image *g = glyph(static_cast<unsigned char>(c));
        if (!g)
            continue;
        if (_fixedWidth) {
            // Each character in the middle of its cell (0x4b1600).
            image::blit(screen, *g, x + (_cellWidth - g->w) / 2, y);
            x += _cellWidth;
        } else {
            image::blit(screen, *g, x, y);
            x += g->w;
        }
    }
}

void Font::drawWrapped(Surface &screen, int centerX, int y, int maxWidth, std::string_view text) const {
    while (!text.empty()) {
        // As many words as fit; a word wider than the line has it to itself.
        size_t end = text.size();
        if (width(text) > maxWidth) {
            end = 0;
            for (size_t space = text.find(' '); space != std::string_view::npos; space = text.find(' ', space + 1)) {
                if (end && width(text.substr(0, space)) > maxWidth)
                    break;
                end = space;
            }
            if (!end)
                end = text.size();
        }
        const std::string_view line = text.substr(0, end);
        draw(screen, centerX - width(line) / 2, y, line);
        y += _height;
        text.remove_prefix(std::min(text.size(), end + 1));
    }
}

} // namespace reluva::stawayteam
