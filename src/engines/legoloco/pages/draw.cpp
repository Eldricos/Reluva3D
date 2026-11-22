// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The viewer's shared drawing: pictures by their frames, rectangles, and
// the status lines.

#include "engines/legoloco/pages/viewers.h"

#include "common/archive.h"
#include "graphics/font.h"

#include <algorithm>

namespace reluva::legoloco {

int Picture::footprintRows() const {
    if (hasDefinition && definition.physical && definition.physical->rows > 0)
        return definition.physical->rows;
    return 1;
}

Error loadPicture(const GameFiles &files, size_t entry, Picture &out) {
    out = Picture();
    const ResourceFile &rf = files.resources();
    std::vector<uint8_t> data;
    if (Error err = rf.readEntry(entry, data))
        return err;
    if (Error err = image::decodeBmp(data, out.bitmap))
        return Error::fail(rf.entries()[entry].name + ": " + err.message);
    const std::string &name = rf.entries()[entry].name;
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos && normalizeArchiveName(name.substr(dot)) == ".bmp") {
        std::vector<uint8_t> text;
        if (!rf.read(name.substr(0, dot) + ".dat", text) &&
            !parseObjectDefinition(std::string_view(reinterpret_cast<const char *>(text.data()), text.size()),
                                   out.definition))
            out.hasDefinition = true;
    }
    out.frames = out.hasDefinition ? std::max(1, out.definition.totalFrames) : 1;
    out.frameW = frameWidth(out.bitmap.surface.w, out.frames);
    return Error::success();
}

void drawFrame(Surface &dst, const Picture &picture, int frame, int x, int y, int scale, bool transparent, bool flip) {
    const Surface &src = picture.bitmap.surface;
    if (frame < 0 || frame >= picture.frames || picture.frameW <= 0)
        return;
    const int x0 = frame * picture.frameW;
    for (int sy = 0; sy < src.h; ++sy) {
        const uint8_t *row = src.row(sy);
        for (int sx = 0; sx < picture.frameW; ++sx) {
            const int px = x0 + (flip ? picture.frameW - 1 - sx : sx);
            Color c;
            if (picture.bitmap.indexed()) {
                const uint8_t index = row[px];
                if (transparent && index == 0)
                    continue;
                picture.bitmap.palette.getColor(index, c.r, c.g, c.b);
            } else {
                const uint8_t *p = row + px * 4;
                c = {p[0], p[1], p[2]};
            }
            fillRect(dst, x + sx * scale, y + sy * scale, scale, scale, c);
        }
    }
}

void fillRect(Surface &s, int x, int y, int w, int h, Color c) {
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(s.w, x + w);
    const int y1 = std::min(s.h, y + h);
    for (int yy = y0; yy < y1; ++yy) {
        uint8_t *p = s.row(yy) + x0 * 4;
        for (int xx = x0; xx < x1; ++xx, p += 4) {
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
    }
}

void drawStatus(Surface &s, const std::string &line1, const std::string &line2, const std::string &line3) {
    const int lines = line3.empty() ? (line2.empty() ? 1 : 2) : 3;
    const int top = s.h - lines * 12 - 4;
    fillRect(s, 0, top, s.w, s.h - top, {16, 16, 24});
    font::drawText(s, 4, top + 2, line1, {230, 230, 230});
    if (lines > 1)
        font::drawText(s, 4, top + 14, line2, {170, 170, 190});
    if (lines > 2)
        font::drawText(s, 4, top + 26, line3, {170, 170, 190});
}

} // namespace reluva::legoloco
