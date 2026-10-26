// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/netimmerse/viewers.h"
#include "engines/netimmerse/data.h"

#include "common/font.h"
#include "common/fs.h"
#include "common/image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::netimmerse {

namespace {

// Windows-1252 (the game's text) as UTF-8, for the built-in font.
std::string fromWindows1252(std::string_view s) {
    // 0x80..0x9f, where Windows-1252 differs from Latin-1; 0 where unused.
    static const uint16_t kHigh[32] = {0x20ac, 0,      0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
                                       0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017d, 0,
                                       0,      0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
                                       0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0,      0x017e, 0x0178};
    std::string out;
    for (unsigned char c : s) {
        uint32_t cp = c;
        if (c >= 0x80 && c < 0xa0)
            cp = kHigh[c - 0x80] ? kHigh[c - 0x80] : '?';
        if (cp < 0x80) {
            out.push_back(char(cp));
        } else if (cp < 0x800) {
            out.push_back(char(0xc0 | cp >> 6));
            out.push_back(char(0x80 | (cp & 0x3f)));
        } else {
            out.push_back(char(0xe0 | cp >> 12));
            out.push_back(char(0x80 | (cp >> 6 & 0x3f)));
            out.push_back(char(0x80 | (cp & 0x3f)));
        }
    }
    return out;
}

std::string extensionOf(const std::string& name) {
    const size_t dot = name.rfind('.');
    return dot == std::string::npos ? std::string() : fs::toLower(name.substr(dot + 1));
}

// Every file with one of `extensions` in the archives, archive by archive
// (in name order), each archive's files in its own order; not the empty
// placeholders some archives list.
template <typename Entry>
std::vector<Entry> collect(const ViewerContext& ctx, std::initializer_list<const char*> extensions) {
    std::vector<Entry> out;
    for (const auto& [archiveName, archive] : ctx.archives)
        for (const PacArchive::Entry& e : archive->entries())
            for (const char* ext : extensions)
                if (e.size && extensionOf(e.name) == ext) {
                    out.push_back({archiveName, e.name});
                    break;
                }
    return out;
}

} // namespace

// --- Page ---------------------------------------------------------------------

void Page::setTitle(const std::string& what) { ctx_.sys.setWindowTitle("Reluva - " + ctx_.title + " - " + what); }

void Page::use2D() { ctx_.sys.initGraphics(kScreenW, kScreenH, PixelFormat::RGBA32); }

void Page::clear(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t* p = ctx_.screen.pixels.data();
    for (size_t i = 0; i + 3 < ctx_.screen.pixels.size(); i += 4) {
        p[i] = r;
        p[i + 1] = g;
        p[i + 2] = b;
        p[i + 3] = 255;
    }
}

void Page::text(int x, int y, const std::string& s, Color c, int scale) {
    font::drawText(ctx_.screen, x, y, s, c, scale);
}

// --- Archives -----------------------------------------------------------------

ArchivesPage::ArchivesPage(ViewerContext& ctx, std::vector<std::pair<std::string, std::string>> lines,
                           std::vector<std::string> errors)
    : Page(ctx), lines_(std::move(lines)), errors_(std::move(errors)) {}

void ArchivesPage::activate() {
    use2D();
    setTitle("Archives (Tab: next page, Esc: back)");
}

void ArchivesPage::update(uint64_t) {
    clear(0, 0, 0);
    const Color grey{170, 170, 170}, green{120, 220, 120}, red{240, 110, 110};
    int y = 24;
    text(24, y, ctx_.title, {255, 255, 255}, 2);
    y += font::kLineHeight * 2 + 8;
    text(24, y, "Not playable yet. Tab: models, textures, sounds, text.", grey);
    y += font::kLineHeight * 2;
    for (const auto& [name, holds] : lines_) {
        text(24, y, name, green);
        text(180, y, holds);
        y += font::kLineHeight + 9;
    }
    for (const std::string& e : errors_) {
        // Long messages wrap.
        const size_t perLine = size_t((kScreenW - 48) / font::kAdvance);
        for (size_t at = 0; at < e.size(); at += perLine, y += font::kLineHeight + 3)
            text(24, y, e.substr(at, perLine), red);
        y += 6;
    }
    text(24, kScreenH - 32, "Tab: next page   Esc: back to the launcher", grey);
    ctx_.sys.updateScreen(ctx_.screen);
}

// --- Textures -----------------------------------------------------------------

TexturePage::TexturePage(ViewerContext& ctx) : Page(ctx), entries_(collect<Entry>(ctx, {"tga"})) {}

void TexturePage::show(int index) {
    index_ = std::clamp(index, 0, int(entries_.size()) - 1);
    use2D();
    setTitle("Textures");
}

void TexturePage::handleEvent(const Event& ev) {
    if (ev.type != EventType::KeyDown)
        return;
    const int n = int(entries_.size());
    switch (ev.key) {
    case key::Left: show((index_ + n - 1) % n); break;
    case key::Right: show((index_ + 1) % n); break;
    case key::PageUp: show(index_ - 10); break;
    case key::PageDown: show(index_ + 10); break;
    case key::Home: show(0); break;
    case key::End: show(n - 1); break;
    case 'f': {
        // The next archive's first image.
        int i = index_;
        while (i < n && entries_[size_t(i)].archive == entries_[size_t(index_)].archive)
            ++i;
        show(i % n);
        break;
    }
    default: break;
    }
}

void TexturePage::update(uint64_t) {
    clear(24, 26, 32);
    const Entry& e = entries_[size_t(index_)];
    std::string line = e.archive + "/" + e.name + "  (" + std::to_string(index_ + 1) + "/" +
                       std::to_string(entries_.size()) + ")";
    std::vector<uint8_t> data;
    image::Image img;
    Error err = ctx_.archives.at(e.archive)->read(e.name, data);
    if (!err)
        err = image::decode(data, img);
    if (err) {
        text(8, 8, line);
        text(8, 8 + font::kLineHeight, err.message, {255, 120, 120});
    } else {
        bool alpha = false;
        for (size_t i = 3; i < img.rgba.size() && !alpha; i += 4)
            alpha = img.rgba[i] != 255;
        line += "  " + std::to_string(img.w) + "x" + std::to_string(img.h) + (alpha ? ", with alpha" : "");
        text(8, 8, line);
        // Scaled to fit below the line: up by whole steps, down as needed;
        // nearest pixel, over a checkerboard so transparency shows.
        const int top = 8 + font::kLineHeight * 2, availW = kScreenW - 16, availH = kScreenH - top - 8;
        float scale = std::min(float(availW) / float(img.w), float(availH) / float(img.h));
        if (scale >= 1)
            scale = std::floor(scale);
        const int w = std::max(1, int(float(img.w) * scale)), h = std::max(1, int(float(img.h) * scale));
        const int x0 = (kScreenW - w) / 2, y0 = top + (availH - h) / 2;
        for (int y = 0; y < h; ++y) {
            uint8_t* row = ctx_.screen.row(y0 + y);
            const int sy = std::min(img.h - 1, int(float(y) / scale));
            for (int x = 0; x < w; ++x) {
                const uint8_t* s = img.pixel(std::min(img.w - 1, int(float(x) / scale)), sy);
                const int check = ((x / 8 + y / 8) & 1) ? 90 : 60;
                uint8_t* d = row + (x0 + x) * 4;
                for (int c = 0; c < 3; ++c)
                    d[c] = uint8_t((s[c] * s[3] + check * (255 - s[3])) / 255);
            }
        }
    }
    text(8, kScreenH - 14, "Left/Right, PageUp/PageDown, Home/End: step   F: next archive", {150, 150, 150});
    ctx_.sys.updateScreen(ctx_.screen);
}

// --- Sounds -------------------------------------------------------------------

SoundPage::SoundPage(ViewerContext& ctx) : Page(ctx), entries_(collect<Entry>(ctx, {"dds"})) {}

void SoundPage::activate() {
    use2D();
    setTitle("Sounds");
    select(index_);
}

void SoundPage::deactivate() {
    if (ctx_.mixer && playing_ != audio::kNoSound)
        ctx_.mixer->stop(playing_);
    playing_ = audio::kNoSound;
}

void SoundPage::select(int index) {
    deactivate();
    index_ = std::clamp(index, 0, int(entries_.size()) - 1);
    const Entry& e = entries_[size_t(index_)];
    auto sound = std::make_shared<audio::Sound>();
    std::vector<uint8_t> data;
    Error err = ctx_.archives.at(e.archive)->read(e.name, data);
    if (!err)
        err = decodeDds(data, *sound);
    if (err) {
        sound_.reset();
        info_ = err.message;
        return;
    }
    sound_ = sound;
    char buf[96];
    std::snprintf(buf, sizeof buf, "%d Hz, %s, %.2f s", sound->rate, sound->channels == 2 ? "stereo" : "mono",
                  double(sound->frames()) / sound->rate);
    info_ = buf;
    if (autoPlay_)
        play();
}

void SoundPage::play() {
    if (!ctx_.mixer || !sound_)
        return;
    if (playing_ != audio::kNoSound && ctx_.mixer->isPlaying(playing_)) {
        ctx_.mixer->stop(playing_);
        playing_ = audio::kNoSound;
        return;
    }
    playing_ = ctx_.mixer->play(sound_, audio::SoundType::Effects);
}

void SoundPage::handleEvent(const Event& ev) {
    if (ev.type != EventType::KeyDown)
        return;
    const int n = int(entries_.size());
    switch (ev.key) {
    case key::Left:
    case key::Up: select((index_ + n - 1) % n); break;
    case key::Right:
    case key::Down: select((index_ + 1) % n); break;
    case key::PageUp: select(index_ - 10); break;
    case key::PageDown: select(index_ + 10); break;
    case key::Home: select(0); break;
    case key::End: select(n - 1); break;
    case key::Space:
    case key::Return: play(); break;
    case 'a':
        autoPlay_ = !autoPlay_;
        if (autoPlay_)
            play();
        break;
    case 'f': {
        int i = index_;
        while (i < n && entries_[size_t(i)].archive == entries_[size_t(index_)].archive)
            ++i;
        select(i % n);
        break;
    }
    default: break;
    }
}

void SoundPage::update(uint64_t) {
    clear(20, 22, 30);
    const int lh = font::kLineHeight + 2, rows = (kScreenH - 80) / lh;
    const int first = std::clamp(index_ - rows / 2, 0, std::max(0, int(entries_.size()) - rows));
    for (int r = 0; r < rows && first + r < int(entries_.size()); ++r) {
        const Entry& e = entries_[size_t(first + r)];
        const bool sel = first + r == index_;
        if (sel)
            for (int y = 0; y < lh; ++y)
                for (int x = 4; x < kScreenW - 4; ++x) {
                    uint8_t* p = ctx_.screen.row(8 + r * lh + y) + x * 4;
                    p[0] = 50, p[1] = 70, p[2] = 110;
                }
        text(12, 9 + r * lh, e.archive + "/" + e.name, sel ? Color{255, 255, 255} : Color{180, 180, 180});
    }
    const bool playing = ctx_.mixer && playing_ != audio::kNoSound && ctx_.mixer->isPlaying(playing_);
    text(8, kScreenH - 58,
         std::to_string(index_ + 1) + "/" + std::to_string(entries_.size()) + "  " + info_ +
             (playing ? "  (playing)" : ""),
         {230, 230, 140});
    if (!ctx_.mixer)
        text(8, kScreenH - 44, "No audio device: sounds can't be played.", {255, 120, 120});
    text(8, kScreenH - 30, "Up/Down, PageUp/PageDown: step   Space/Enter: play or stop", {150, 150, 150});
    text(8, kScreenH - 16,
         std::string("A: play each when selected (") + (autoPlay_ ? "on" : "off") + ")   F: next archive",
         {150, 150, 150});
    ctx_.sys.updateScreen(ctx_.screen);
}

// --- Text ---------------------------------------------------------------------

TextPage::TextPage(ViewerContext& ctx) : Page(ctx), files_(collect<File>(ctx, {"txt", "puz"})) {}

void TextPage::show(int index) {
    index_ = (index + int(files_.size())) % int(files_.size());
    scroll_ = 0;
    lines_.clear();
    use2D();
    const File& f = files_[size_t(index_)];
    setTitle("Text: " + f.name);
    std::vector<uint8_t> data;
    if (Error err = ctx_.archives.at(f.archive)->read(f.name, data)) {
        lines_.push_back(err.message);
        return;
    }
    // Lines wrapped to the screen's width.
    const size_t width = size_t((kScreenW - 16) / font::kAdvance);
    const std::string text = fromWindows1252(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        // Wrapped by characters (the font's count, not bytes).
        while (font::length(line) > width) {
            size_t cut = 0;
            for (size_t chars = 0; cut < line.size() && chars < width; ++chars) {
                ++cut;
                while (cut < line.size() && (uint8_t(line[cut]) & 0xc0) == 0x80)
                    ++cut;
            }
            lines_.push_back(line.substr(0, cut));
            line.erase(0, cut);
        }
        lines_.push_back(line);
        if (end == text.size())
            break;
        pos = end + 1;
    }
}

void TextPage::handleEvent(const Event& ev) {
    const int page = (kScreenH - 40) / (font::kLineHeight + 1);
    if (ev.type == EventType::MouseWheel) {
        scroll_ -= ev.wheelY * 3;
    } else if (ev.type == EventType::KeyDown) {
        switch (ev.key) {
        case key::Left: show(index_ - 1); return;
        case key::Right: show(index_ + 1); return;
        case key::Up: --scroll_; break;
        case key::Down: ++scroll_; break;
        case key::PageUp: scroll_ -= page; break;
        case key::PageDown: scroll_ += page; break;
        case key::Home: scroll_ = 0; break;
        case key::End: scroll_ = int(lines_.size()); break;
        default: break;
        }
    }
    scroll_ = std::clamp(scroll_, 0, std::max(0, int(lines_.size()) - page));
}

void TextPage::update(uint64_t) {
    clear(18, 20, 26);
    const File& f = files_[size_t(index_)];
    text(8, 6, f.archive + "/" + f.name + "  (" + std::to_string(index_ + 1) + "/" + std::to_string(files_.size()) +
                   ", " + std::to_string(lines_.size()) + " lines)",
         {230, 230, 140});
    const int lh = font::kLineHeight + 1, rows = (kScreenH - 40) / lh;
    for (int r = 0; r < rows && scroll_ + r < int(lines_.size()); ++r)
        text(8, 22 + r * lh, lines_[size_t(scroll_ + r)]);
    text(8, kScreenH - 14, "Left/Right: file   Up/Down, PageUp/PageDown, Home/End: scroll", {150, 150, 150});
    ctx_.sys.updateScreen(ctx_.screen);
}

} // namespace reluva::netimmerse
