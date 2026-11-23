// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/ui/text.h"

#include <map>

namespace reluva::tpw {

namespace {

// Calls visit(glyph, pen) for each character of the text the font has,
// the pen moving by each glyph's advance; returns where it ends.
template<typename Visit>
int forEachGlyph(const Font &font, const CharacterSet &characters, const std::string &utf8, Visit visit) {
    std::map<char32_t, uint8_t> codes;
    for (size_t code = 1; code <= characters.size(); ++code)
        codes.emplace(characters.unicode(uint8_t(code)), uint8_t(code));
    int pen = 0;
    for (size_t i = 0; i < utf8.size();) {
        char32_t ch = uint8_t(utf8[i]);
        int extra = ch >= 0xf0 ? 3 : ch >= 0xe0 ? 2 : ch >= 0xc0 ? 1 : 0;
        if (extra)
            ch &= 0x3f >> extra;
        ++i;
        for (; extra > 0 && i < utf8.size(); --extra, ++i)
            ch = ch << 6 | (uint8_t(utf8[i]) & 0x3f);
        const auto it = codes.find(ch);
        const Font::Glyph *g = it == codes.end() ? nullptr : font.glyph(it->second);
        if (!g)
            continue;
        visit(*g, pen);
        pen += g->advance;
    }
    return pen;
}

} // namespace

int drawGameText(Surface &s, const Font &font, const CharacterSet &characters, int x, int y, const std::string &utf8,
                 Color c) {
    return x + forEachGlyph(font, characters, utf8, [&](const Font::Glyph &g, int pen) {
               for (int py = 0; py < g.height && !g.coverage.empty(); ++py)
                   for (int px = 0; px < g.width; ++px) {
                       const int sx = x + pen + g.left + px;
                       const int sy = y + g.top + py;
                       const int cover = g.coverage[size_t(py * g.width + px)];
                       if (cover == 0 || sx < 0 || sy < 0 || sx >= s.w || sy >= s.h)
                           continue;
                       uint8_t *dst = s.row(sy) + sx * 4;
                       dst[0] = uint8_t((c.r * cover + dst[0] * (15 - cover)) / 15);
                       dst[1] = uint8_t((c.g * cover + dst[1] * (15 - cover)) / 15);
                       dst[2] = uint8_t((c.b * cover + dst[2] * (15 - cover)) / 15);
                   }
           });
}

int gameTextWidth(const Font &font, const CharacterSet &characters, const std::string &utf8) {
    return forEachGlyph(font, characters, utf8, [](const Font::Glyph &, int) {});
}

Error readGameFont(const SearchSet &files, const std::string &folder, const char *name, Font &out) {
    std::vector<uint8_t> data;
    if (Error err = files.read(folder + name + ".bf4", data))
        return err;
    return out.parse(data);
}

Error readStringTable(const SearchSet &files, const std::string &folder, const char *name,
                      const CharacterSet &characters, StringTable &out) {
    std::vector<uint8_t> data;
    if (Error err = files.read(folder + name + ".str", data))
        return err;
    return out.parse(data, characters);
}

} // namespace reluva::tpw
