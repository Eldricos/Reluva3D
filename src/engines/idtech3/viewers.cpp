// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's shared drawing, and its Files and Textures pages.

#include "engines/idtech3/viewers.h"

#include "common/font.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

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

// --- Page ------------------------------------------------------------------------------

void Page::clear(uint8_t r, uint8_t g, uint8_t b) { fill(0, 0, ctx_.screen.w, ctx_.screen.h, r, g, b); }

void Page::fill(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    Surface& s = ctx_.screen;
    const int x0 = std::max(x, 0), y0 = std::max(y, 0);
    const int x1 = std::min(x + w, s.w), y1 = std::min(y + h, s.h);
    for (int yy = y0; yy < y1; ++yy) {
        uint8_t* p = s.row(yy) + x0 * 4;
        for (int xx = x0; xx < x1; ++xx, p += 4) {
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 255;
        }
    }
}

void Page::text(int x, int y, const std::string& s, Color c) { font::drawText(ctx_.screen, x, y, s, c); }

void Page::setTitle(const std::string& detail) {
    ctx_.sys.setWindowTitle(std::string("Reluva - ") + ctx_.game.name + " - " + name() + ": " + detail);
}

// --- FileViewer -------------------------------------------------------------------------

FileViewer::FileViewer(ViewerContext& ctx) : Page(ctx) {
    for (const std::string& n : ctx.files.list()) {
        const Archive* a = ctx.files.find(n);
        files_.push_back({n, a ? std::filesystem::path(a->describe()).filename().string() : std::string()});
    }
}

void FileViewer::activate() {
    setTitle(std::to_string(files_.size()) + " files in " + std::to_string(ctx_.files.size()) + " archives");
    draw();
}

void FileViewer::handleEvent(const Event& ev) {
    const int rows = (ctx_.screen.h - 4 * font::kLineHeight) / font::kLineHeight;
    if (ev.type == EventType::MouseWheel)
        top_ -= ev.wheelY * 3;
    else if (ev.type != EventType::KeyDown)
        return;
    else if (ev.key == key::Up)
        --top_;
    else if (ev.key == key::Down)
        ++top_;
    else if (ev.key == key::PageUp)
        top_ -= rows;
    else if (ev.key == key::PageDown)
        top_ += rows;
    else if (ev.key == key::Home)
        top_ = 0;
    else if (ev.key == key::End)
        top_ = static_cast<int>(files_.size());
    else
        return;
    top_ = std::clamp(top_, 0, std::max(0, static_cast<int>(files_.size()) - rows));
    draw();
}

void FileViewer::draw() {
    clear(16, 18, 24);
    const int lh = font::kLineHeight;
    text(4, 4, "Search path, first match wins:", {150, 200, 255});
    std::string order;
    for (const Archive* a : ctx_.files.archives()) {
        if (!order.empty())
            order += ", ";
        order += std::filesystem::path(a->describe()).filename().string();
    }
    text(4, 4 + lh, order, {150, 200, 255});
    if (!ctx_.warnings.empty())
        text(4, 4 + 2 * lh,
             std::to_string(ctx_.warnings.size()) + " problems, the first: " + ctx_.warnings.front(),
             {255, 120, 120});
    const int rows = (ctx_.screen.h - 4 * lh) / lh;
    const int sourceX = ctx_.screen.w - 20 * font::kAdvance;
    for (int i = 0; i < rows && top_ + i < static_cast<int>(files_.size()); ++i) {
        const Row& r = files_[size_t(top_ + i)];
        const int y = 4 * lh + i * lh;
        text(4, y, r.name);
        text(sourceX, y, r.source, {140, 140, 140});
    }
}

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

namespace reluva::idtech3 {

namespace {
constexpr const char* kConfigName = "reluva.cfg";
}

void loadConfig(ViewerContext& ctx) {
    auto readFile = [&](const std::string& name, std::string& out) {
        // The player's own files first, then the game's.
        if (!ctx.savePath.empty()) {
            std::ifstream in(ctx.savePath / name, std::ios::binary);
            if (in) {
                out.assign(std::istreambuf_iterator<char>(in), {});
                return true;
            }
        }
        std::vector<uint8_t> data;
        if (ctx.files.read(name, data))
            return false;
        out.assign(data.begin(), data.end());
        return true;
    };
    std::string text;
    std::vector<uint8_t> data;
    if (!ctx.files.read("default.cfg", data))
        ctx.bindings.execute(std::string(data.begin(), data.end()), ctx.cvars, nullptr, readFile);
    if (!ctx.savePath.empty()) {
        std::ifstream in(ctx.savePath / kConfigName, std::ios::binary);
        if (in) {
            text.assign(std::istreambuf_iterator<char>(in), {});
            ctx.bindings.execute(text, ctx.cvars, nullptr, readFile);
        }
    }
    if (ctx.bindings.empty()) {
        // No default.cfg: the usual keys, so there's something to play with.
        ctx.bindings.execute("bind w +forward; bind s +back; bind a +moveleft; bind d +moveright;"
                             "bind SPACE +moveup; bind c +movedown; bind MOUSE1 +attack; bind MOUSE2 +altattack;"
                             "bind MWHEELUP weapprev; bind MWHEELDOWN weapnext; bind SHIFT +speed;"
                             "bind 1 \"weapon 1\"; bind 2 \"weapon 2\"; bind 3 \"weapon 3\"; bind 4 \"weapon 4\";"
                             "bind 5 \"weapon 5\"; bind 6 \"weapon 6\"; bind 7 \"weapon 7\"; bind 8 \"weapon 8\";"
                             "bind 9 \"weapon 9\"",
                             ctx.cvars);
    }
    ctx.bindings.changed = false;
}

float brightness(const ViewerContext& ctx) {
    const CvarStore::Cvar* gamma = ctx.cvars.find("r_gamma");
    return gamma ? std::clamp(ctx.cvars.value("r_gamma"), 0.5f, 3.0f) : 1.0f;
}

void saveConfig(ViewerContext& ctx) {
    if (ctx.savePath.empty())
        return;
    std::string text = "// Reluva's settings for this game: key bindings and kept settings\n";
    text += ctx.bindings.write();
    for (const CvarStore::Cvar& cv : ctx.cvars.all())
        if (cv.flags & CvarStore::Archive)
            text += "seta " + cv.name + " \"" + cv.value + "\"\n";
    std::error_code ec;
    std::filesystem::create_directories(ctx.savePath, ec);
    std::ofstream out(ctx.savePath / kConfigName, std::ios::binary);
    out << text;
    ctx.bindings.changed = false;
}

std::vector<uint32_t> decodeUtf8(const char* s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; s[i];) {
        const auto c = static_cast<unsigned char>(s[i]);
        const int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (extra < 0) {
            ++i;
            continue;
        }
        uint32_t cp = extra == 0 ? c : c & (0x3f >> extra);
        size_t j = 1;
        for (; j <= size_t(extra) && (static_cast<unsigned char>(s[i + j]) >> 6) == 2; ++j)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + j]) & 0x3f);
        if (j == size_t(extra) + 1)
            out.push_back(cp);
        i += j;
    }
    return out;
}

} // namespace reluva::idtech3
