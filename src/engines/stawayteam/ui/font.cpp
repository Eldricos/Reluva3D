// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/font.h"

#include "engines/stawayteam/formats/binary_object.h"
#include "engines/stawayteam/ui/widgets.h"

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
    for (const auto &frame : font.sequence->frames)
        _glyphs.push_back(frame ? frameImage(*frame) : image::Image{});
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
            w += g->w;
    return w;
}

void Font::draw(Surface &screen, int x, int y, std::string_view text, int opacity, float brightness) const {
    for (char c : text) {
        const image::Image *g = glyph(static_cast<unsigned char>(c));
        if (!g)
            continue;
        if (brightness < 1.0f)
            image::blit(screen, dimmed(*g, brightness), x, y, opacity);
        else
            image::blit(screen, *g, x, y, opacity);
        x += g->w;
    }
}

void Font::drawWrapped(Surface &screen, int x, int y, int maxWidth, std::string_view text, Align align,
                       int spacing) const {
    for (const std::string &line : wrap(text, maxWidth)) {
        const int w = width(line);
        draw(screen, align == Align::kLeft ? x : align == Align::kCenter ? x - w / 2 : x - w, y, line);
        y += _height + spacing;
    }
}

std::vector<std::string> Font::wrap(std::string_view text, int maxWidth, int indent, int narrowLines) const {
    std::vector<std::string> out;
    // A line break starts a line, an empty one for a blank line (inferred:
    // the game's drawing of them isn't traced).
    for (std::string_view rest = text;;) {
        const size_t newline = rest.find('\n');
        std::string_view paragraph = rest.substr(0, newline);
        do {
            const int room = maxWidth > 0 && int(out.size()) < narrowLines ? maxWidth - indent : maxWidth;
            // As many words as fit; a word wider than the line has it to itself.
            size_t end = paragraph.size();
            if (room > 0 && width(paragraph) > room) {
                end = 0;
                for (size_t space = paragraph.find(' '); space != std::string_view::npos;
                     space = paragraph.find(' ', space + 1)) {
                    if (end && width(paragraph.substr(0, space)) > room)
                        break;
                    end = space;
                }
                if (!end)
                    end = paragraph.size();
            }
            out.emplace_back(paragraph.substr(0, end));
            paragraph.remove_prefix(std::min(paragraph.size(), end + 1));
        } while (!paragraph.empty());
        if (newline == std::string_view::npos)
            break;
        rest.remove_prefix(newline + 1);
    }
    return out;
}

} // namespace reluva::stawayteam
