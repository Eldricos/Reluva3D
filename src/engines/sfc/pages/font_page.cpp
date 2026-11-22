// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Fonts page: each .bft font's characters, and sample
// text written with it.

#include "common/stream.h"
#include "engines/sfc/pages/viewers.h"
#include "graphics/font.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace reluva::sfc {

FontPage::FontPage(ViewerContext &ctx) : Page(ctx) {
    _files = filesWithExtension(ctx.root, ".bft");
}

void FontPage::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown || _files.empty())
        return;
    const size_t n = _files.size();
    if (ev.key == key::kLeft)
        _current = (_current + n - 1) % n;
    else if (ev.key == key::kRight)
        _current = (_current + 1) % n;
    else if (ev.key == 'b')
        _boxes = !_boxes;
    else
        return;
    show();
}

void FontPage::show() {
    clear({20, 22, 28});
    if (_files.empty())
        return;
    const auto &rel = _files[_current];
    std::vector<uint8_t> data;
    BftFont font;
    Error err = readFile(_ctx.root / rel, data) ? font.read(data) : Error::fail("Can't read it");
    char buf[200];
    std::snprintf(buf, sizeof buf, "%s  (%zu of %zu): line height %d, %zu glyphs", rel.generic_string().c_str(),
                  _current + 1, _files.size(), font.lineHeight(), font.glyphs().size());
    text(4, 2, buf, {150, 200, 255});
    text(4, 2 + font::kLineHeight, "Left/Right: fonts  B: each glyph's box", {170, 170, 170});
    setTitle(rel.filename().string());
    if (err) {
        text(4, 40, err.message, {255, 120, 120});
        return;
    }
    // The characters from 32 up, sixteen to a row, then sample lines.
    int y = 2 * font::kLineHeight + 10;
    const int cell = std::max(font.lineHeight(), 8) + 6;
    for (size_t c = 32; c < font.glyphs().size(); c += 16) {
        if (y + cell > kScreenH)
            break;
        for (size_t i = 0; i < 16 && c + i < font.glyphs().size(); ++i) {
            const int x = 40 + int(i) * (cell + 8);
            const auto &g = font.glyphs()[c + i];
            if (_boxes && g.width > 0)
                fill(x + g.x, y + g.y, g.width, g.height, {60, 50, 40});
            font.draw(_ctx.screen, x, y, std::string(1, char(c + i)), {255, 255, 255});
        }
        std::snprintf(buf, sizeof buf, "%3zu", c);
        text(4, y, buf, {120, 120, 120});
        y += cell;
    }
    y += 6;
    for (const char *sample :
         {"Starfleet Command: Empires at War", "Phasers 1-4, Photon Torpedoes, Disruptors", "0123456789 +-.,:;!?"}) {
        if (y + font.lineHeight() > kScreenH)
            break;
        font.draw(_ctx.screen, 8, y, sample, {230, 200, 120});
        y += font.lineHeight();
    }
}

} // namespace reluva::sfc
