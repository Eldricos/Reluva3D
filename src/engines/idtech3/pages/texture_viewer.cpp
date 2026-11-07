// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/pages/texture_viewer.h"

#include "common/font.h"

#include <algorithm>
#include <filesystem>

namespace reluva::idtech3 {

namespace {

bool hasExtension(const std::string& name, std::initializer_list<const char*> exts) {
    const std::string key = normalizeArchiveName(name);
    for (const char* e : exts) {
        const std::string ext = e;
        if (key.size() > ext.size() && key.compare(key.size() - ext.size(), ext.size(), ext) == 0)
            return true;
    }
    return false;
}

} // namespace

// --- TextureViewer ----------------------------------------------------------------------

TextureViewer::TextureViewer(ViewerContext& ctx) : Page(ctx) {
    for (const std::string& n : ctx.files.list())
        if (hasExtension(n, {".tga", ".jpg", ".jpeg", ".png"}))
            names_.push_back(n);
}

void TextureViewer::handleEvent(const Event& ev) {
    if (ev.type != EventType::KeyDown || names_.empty())
        return;
    int i = index_;
    if (ev.key == key::Left)
        --i;
    else if (ev.key == key::Right)
        ++i;
    else if (ev.key == key::PageUp)
        i -= 10;
    else if (ev.key == key::PageDown)
        i += 10;
    else if (ev.key == key::Home)
        i = 0;
    else if (ev.key == key::End)
        i = static_cast<int>(names_.size()) - 1;
    else
        return;
    show(std::clamp(i, 0, static_cast<int>(names_.size()) - 1));
}

void TextureViewer::show(int index) {
    index_ = index;
    Surface& s = ctx_.screen;
    // Checkerboard, so transparency shows.
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) {
            const uint8_t v = ((x / 16 + y / 16) & 1) ? 70 : 50;
            uint8_t* p = s.row(y) + x * 4;
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    if (names_.empty())
        return;
    const std::string& name = names_[size_t(index)];
    const std::string where = std::to_string(index + 1) + "/" + std::to_string(names_.size());
    setTitle(name + " (" + where + ")");

    const int lh = font::kLineHeight;
    std::vector<uint8_t> data;
    image::Image img;
    std::string info;
    if (Error err = ctx_.files.read(name, data)) {
        info = err.message;
    } else if (Error derr = image::decode(data, img)) {
        info = derr.message;
    } else {
        // Fit the space under the caption: whole multiples up to 4x when
        // small, nearest-neighbour shrinking when large.
        const int areaW = s.w, areaH = s.h - 3 * lh;
        float scale = std::min(float(areaW) / float(img.w), float(areaH) / float(img.h));
        scale = scale >= 1 ? std::min(4.0f, float(int(scale))) : scale;
        image::Image shown;
        shown.w = std::max(1, int(float(img.w) * scale));
        shown.h = std::max(1, int(float(img.h) * scale));
        shown.rgba.resize(size_t(shown.w) * shown.h * 4);
        for (int y = 0; y < shown.h; ++y)
            for (int x = 0; x < shown.w; ++x) {
                const int sx = std::min(img.w - 1, int(float(x) / scale));
                const int sy = std::min(img.h - 1, int(float(y) / scale));
                std::copy_n(img.pixel(sx, sy), 4, shown.pixel(x, y));
            }
        image::blit(s, shown, (areaW - shown.w) / 2, 3 * lh + (areaH - shown.h) / 2);
        info = std::to_string(img.w) + "x" + std::to_string(img.h);
        if (scale != 1)
            info += " shown at " + std::to_string(int(scale * 100 + 0.5f)) + "%";
    }
    fill(0, 0, s.w, 3 * lh, 16, 18, 24);
    text(4, 4, name + "  " + where);
    const Archive* from = ctx_.files.find(name);
    text(4, 4 + lh, info + (from ? "  from " + std::filesystem::path(from->describe()).filename().string() : ""),
         {150, 200, 255});
}

} // namespace reluva::idtech3
