// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Textures page.

#include "engines/age/viewers.h"

#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cstdio>

namespace reluva::age {

namespace {

bool endsWith(const std::string& s, const char* ext) {
    const std::string e = ext;
    return s.size() > e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
}

} // namespace

TextureViewer::TextureViewer(ViewerContext& ctx) : Page(ctx) {
    for (const std::string& n : ctx.files.list()) {
        const std::string key = normalizeArchiveName(n);
        if ((key.rfind("texture/", 0) == 0 && (endsWith(key, ".tex") || endsWith(key, ".tga"))) ||
            (key.rfind("jpg/", 0) == 0 && endsWith(key, ".jpg")))
            names_.push_back(n);
    }
}

void TextureViewer::activate() {
    if (!names_.empty())
        show(index_);
}

void TextureViewer::show(int index) {
    index_ = index;
    const std::string& name = names_[size_t(index)];
    tex_ = Tex{};
    error_.clear();
    std::vector<uint8_t> data;
    if (Error err = ctx_.files.read(name, data))
        error_ = err.message;
    else if (endsWith(normalizeArchiveName(name), ".tex"))
        error_ = decodeTex(data, tex_).message;
    else if (Error e = image::decode(data, tex_.image))
        error_ = e.message;
    else
        tex_.levels = 1;
    setTitle(name + " (" + std::to_string(index + 1) + "/" + std::to_string(names_.size()) + ")");
    draw();
}

void TextureViewer::handleEvent(const Event& ev) {
    if (ev.type != EventType::KeyDown || names_.empty())
        return;
    const int n = static_cast<int>(names_.size());
    int i = index_;
    if (ev.key == key::Left)
        i = (i + n - 1) % n;
    else if (ev.key == key::Right)
        i = (i + 1) % n;
    else if (ev.key == key::PageUp)
        i = std::max(0, i - 10);
    else if (ev.key == key::PageDown)
        i = std::min(n - 1, i + 10);
    else if (ev.key == key::Home)
        i = 0;
    else if (ev.key == key::End)
        i = n - 1;
    else if (ev.key == key::Up || ev.key == key::Down) {
        // The first texture of the next (or previous) kind.
        const int step = ev.key == key::Down ? 1 : n - 1;
        const std::string kind = kindOf(names_[size_t(i)]);
        do
            i = (i + step) % n;
        while (i != index_ && kindOf(names_[size_t(i)]) == kind);
        if (ev.key == key::Up)
            while (i > 0 && kindOf(names_[size_t(i - 1)]) == kindOf(names_[size_t(i)]))
                --i;
    } else if (ev.key == '+' || ev.key == '=') {
        zoom_ = std::min(zoom_ + 1, 8);
    } else if (ev.key == '-') {
        zoom_ = std::max(zoom_ - 1, 0);
    } else if (ev.key == 'a') {
        alpha_ = !alpha_;
    } else {
        return;
    }
    if (i != index_)
        show(i);
    else
        draw();
}

void TextureViewer::draw() {
    clear(16, 18, 24);
    const int lh = font::kLineHeight;
    const std::string& name = names_[size_t(index_)];
    text(4, 4, name + "  (" + std::to_string(index_ + 1) + "/" + std::to_string(names_.size()) + ")",
         {150, 200, 255});
    if (!error_.empty()) {
        text(4, 4 + lh, error_, {255, 120, 120});
        return;
    }
    const image::Image& img = tex_.image;
    char info[160];
    // Not a .tex: a TGA or JPEG, as its name says.
    const std::string format =
        tex_.format ? formatName(tex_.format) : fs::toLower(name.substr(name.rfind('.') + 1)) + " file";
    std::snprintf(info, sizeof info, "%d x %d, %s, %u level%s, flags 0x%x%s", img.w, img.h, format.c_str(),
                  tex_.levels, tex_.levels == 1 ? "" : "s", tex_.flags, alpha_ ? "" : "; alpha ignored (A)");
    text(4, 4 + lh, info, {180, 180, 180});

    // Fit the space below the text (zoom 0), or that many times the size.
    const int top = 4 + 3 * lh, areaW = ctx_.screen.w - 8, areaH = ctx_.screen.h - top - 4;
    int w = img.w, h = img.h;
    if (zoom_ > 0) {
        w *= zoom_;
        h *= zoom_;
    } else {
        const float s = std::min({float(areaW) / float(w), float(areaH) / float(h), 4.0f});
        w = std::max(1, int(float(w) * s));
        h = std::max(1, int(float(h) * s));
    }
    const int x0 = 4 + std::max(0, (areaW - w) / 2), y0 = top + std::max(0, (areaH - h) / 2);
    Surface& s = ctx_.screen;
    for (int y = 0; y < h && y0 + y < s.h; ++y) {
        uint8_t* row = s.row(y0 + y);
        const int sy = int(int64_t(y) * img.h / h);
        for (int x = 0; x < w && x0 + x < s.w; ++x) {
            const uint8_t* p = img.pixel(int(int64_t(x) * img.w / w), sy);
            const int a = alpha_ ? p[3] : 255;
            const uint8_t check = ((x / 8 + y / 8) & 1) ? 100 : 60; // shows through where transparent
            uint8_t* d = row + (x0 + x) * 4;
            for (int c = 0; c < 3; ++c)
                d[c] = uint8_t((p[c] * a + check * (255 - a)) / 255);
            d[3] = 255;
        }
    }
}

} // namespace reluva::age
