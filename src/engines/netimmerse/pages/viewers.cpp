// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/netimmerse/pages/viewers.h"
#include "engines/netimmerse/formats/data.h"

#include "graphics/font.h"
#include "common/fs.h"
#include "image/image.h"

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

std::string extensionOf(const std::string &name) {
    const size_t dot = name.rfind('.');
    return dot == std::string::npos ? std::string() : fs::toLower(name.substr(dot + 1));
}

// Every file with one of `extensions` in the archives, archive by archive
// (in name order), each archive's files in its own order; not the empty
// placeholders some archives list.
template<typename Entry>
std::vector<Entry> collect(const ViewerContext &ctx, std::initializer_list<const char *> extensions) {
    std::vector<Entry> out;
    for (const auto &[archiveName, archive] : ctx.archives)
        for (const PacArchive::Entry &e : archive->entries())
            for (const char *ext : extensions)
                if (e.size && extensionOf(e.name) == ext) {
                    out.push_back({archiveName, e.name});
                    break;
                }
    return out;
}

} // namespace

// --- Page ---------------------------------------------------------------------

void Page::setTitle(const std::string &what) {
    _ctx.sys.setWindowTitle("Reluva - " + _ctx.title + " - " + what);
}

void Page::use2D() {
    _ctx.sys.initGraphics(kScreenW, kScreenH, PixelFormat::kRGBA32);
}

void Page::clear(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t *p = _ctx.screen.pixels.data();
    for (size_t i = 0; i + 3 < _ctx.screen.pixels.size(); i += 4) {
        p[i] = r;
        p[i + 1] = g;
        p[i + 2] = b;
        p[i + 3] = 255;
    }
}

void Page::text(int x, int y, const std::string &s, Color c, int scale) {
    font::drawText(_ctx.screen, x, y, s, c, scale);
}

// --- Archives -----------------------------------------------------------------

ArchivesPage::ArchivesPage(ViewerContext &ctx, std::vector<std::pair<std::string, std::string>> lines,
                           std::vector<std::string> errors)
    : Page(ctx), _lines(std::move(lines)), _errors(std::move(errors)) {
}

void ArchivesPage::activate() {
    use2D();
    setTitle("Archives (Tab: next page, Esc: back)");
}

void ArchivesPage::update(uint64_t) {
    clear(0, 0, 0);
    const Color grey{170, 170, 170};
    const Color green{120, 220, 120};
    const Color red{240, 110, 110};
    int y = 24;
    text(24, y, _ctx.title, {255, 255, 255}, 2);
    y += font::kLineHeight * 2 + 8;
    text(24, y, "Not playable yet. Tab: models, textures, sounds, text.", grey);
    y += font::kLineHeight * 2;
    for (const auto &[name, holds] : _lines) {
        text(24, y, name, green);
        text(180, y, holds);
        y += font::kLineHeight + 9;
    }
    for (const std::string &e : _errors) {
        // Long messages wrap.
        const size_t perLine = size_t((kScreenW - 48) / font::kAdvance);
        for (size_t at = 0; at < e.size(); at += perLine, y += font::kLineHeight + 3)
            text(24, y, e.substr(at, perLine), red);
        y += 6;
    }
    text(24, kScreenH - 32, "Tab: next page   Esc: back to the launcher", grey);
    _ctx.sys.updateScreen(_ctx.screen);
}

// --- Textures -----------------------------------------------------------------

TexturePage::TexturePage(ViewerContext &ctx) : Page(ctx), _entries(collect<Entry>(ctx, {"tga"})) {
}

void TexturePage::show(int index) {
    _index = std::clamp(index, 0, int(_entries.size()) - 1);
    use2D();
    setTitle("Textures");
}

void TexturePage::handleEvent(const Event &ev) {
    if (ev.type != EventType::KeyDown)
        return;
    const int n = int(_entries.size());
    switch (ev.key) {
    case key::Left:
        show((_index + n - 1) % n);
        break;
    case key::Right:
        show((_index + 1) % n);
        break;
    case key::PageUp:
        show(_index - 10);
        break;
    case key::PageDown:
        show(_index + 10);
        break;
    case key::Home:
        show(0);
        break;
    case key::End:
        show(n - 1);
        break;
    case 'f': {
        // The next archive's first image.
        int i = _index;
        while (i < n && _entries[size_t(i)].archive == _entries[size_t(_index)].archive)
            ++i;
        show(i % n);
        break;
    }
    default:
        break;
    }
}

void TexturePage::update(uint64_t) {
    clear(24, 26, 32);
    const Entry &e = _entries[size_t(_index)];
    std::string line =
        e.archive + "/" + e.name + "  (" + std::to_string(_index + 1) + "/" + std::to_string(_entries.size()) + ")";
    std::vector<uint8_t> data;
    image::Image img;
    Error err = _ctx.archives.at(e.archive)->read(e.name, data);
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
        const int top = 8 + font::kLineHeight * 2;
        const int availW = kScreenW - 16;
        const int availH = kScreenH - top - 8;
        float scale = std::min(float(availW) / float(img.w), float(availH) / float(img.h));
        if (scale >= 1)
            scale = std::floor(scale);
        const int w = std::max(1, int(float(img.w) * scale));
        const int h = std::max(1, int(float(img.h) * scale));
        const int x0 = (kScreenW - w) / 2;
        const int y0 = top + (availH - h) / 2;
        for (int y = 0; y < h; ++y) {
            uint8_t *row = _ctx.screen.row(y0 + y);
            const int sy = std::min(img.h - 1, int(float(y) / scale));
            for (int x = 0; x < w; ++x) {
                const uint8_t *s = img.pixel(std::min(img.w - 1, int(float(x) / scale)), sy);
                const int check = ((x / 8 + y / 8) & 1) ? 90 : 60;
                uint8_t *d = row + (x0 + x) * 4;
                for (int c = 0; c < 3; ++c)
                    d[c] = uint8_t((s[c] * s[3] + check * (255 - s[3])) / 255);
            }
        }
    }
    text(8, kScreenH - 14, "Left/Right, PageUp/PageDown, Home/End: step   F: next archive", {150, 150, 150});
    _ctx.sys.updateScreen(_ctx.screen);
}

// --- Fonts --------------------------------------------------------------------

FontPage::FontPage(ViewerContext &ctx) : Page(ctx), _entries(collect<Entry>(ctx, {"ddf"})) {
}

void FontPage::show(int index) {
    _index = std::clamp(index, 0, int(_entries.size()) - 1);
    use2D();
    setTitle("Fonts");
    const Entry &e = _entries[size_t(_index)];
    const PacArchive &pac = *_ctx.archives.at(e.archive);
    _pages.clear();
    std::vector<uint8_t> data;
    Error err = pac.read(e.name, data);
    if (!err)
        err = _font.parse(data);
    for (int i = 0; !err && i < _font.pages; ++i) {
        _pages.emplace_back();
        err = pac.read(DdfFont::pageName(e.name, i), data);
        if (!err)
            err = image::decode(data, _pages.back());
    }
    _error = err ? err.message : std::string();
}

void FontPage::handleEvent(const Event &ev) {
    if (ev.type != EventType::KeyDown)
        return;
    const int n = int(_entries.size());
    switch (ev.key) {
    case key::Left:
        show((_index + n - 1) % n);
        break;
    case key::Right:
        show((_index + 1) % n);
        break;
    case 'p':
        _showPages = !_showPages;
        break;
    default:
        break;
    }
}

void FontPage::update(uint64_t) {
    clear(24, 26, 32);
    const Entry &e = _entries[size_t(_index)];
    text(8, 8,
         e.archive + "/" + e.name + "  (" + std::to_string(_index + 1) + "/" + std::to_string(_entries.size()) + ")");
    if (!_error.empty()) {
        text(8, 8 + font::kLineHeight, _error, {255, 120, 120});
    } else {
        text(8, 8 + font::kLineHeight,
             "\"" + _font.name + "\"  line " + std::to_string(_font.lineHeight) + ", descent " +
                 std::to_string(_font.descent) + ", space " + std::to_string(_font.spaceAdvance) + ", chars " +
                 std::to_string(_font.first) + "-" + std::to_string(_font.last) + ", " + std::to_string(_font.pages) +
                 " pages of " + std::to_string(_font.pageWidth) + "x" + std::to_string(_font.pageHeight),
             {170, 170, 170});
        const int top = 8 + font::kLineHeight * 3;
        if (_showPages) {
            // The pages side by side, over a checkerboard.
            int x0 = 8;
            for (const image::Image &img : _pages) {
                for (int y = 0; y < img.h && top + y < kScreenH - 20; ++y)
                    for (int x = 0; x < img.w && x0 + x < kScreenW; ++x) {
                        const uint8_t *s = img.pixel(x, y);
                        const int check = ((x / 8 + y / 8) & 1) ? 90 : 60;
                        uint8_t *d = _ctx.screen.row(top + y) + (x0 + x) * 4;
                        for (int c = 0; c < 3; ++c)
                            d[c] = uint8_t((s[c] * s[3] + check * (255 - s[3])) / 255);
                    }
                x0 += img.w + 8;
            }
        } else {
            // A sample, then every character the font draws, a line at a time.
            int y = top;
            const Color white{255, 255, 255};
            const Color gold{250, 210, 90};
            _font.draw(_ctx.screen, _pages, 8, y, "The quick brown fox jumps over the lazy dog.", white);
            y += _font.lineHeight;
            _font.draw(_ctx.screen, _pages, 8, y, "LEGO ALPHA TEAM 0123456789 !?&", gold);
            y += _font.lineHeight + 4;
            std::string line;
            for (int c = _font.first; c <= _font.last && y + _font.lineHeight < kScreenH - 20; ++c) {
                std::string next = line + char(c);
                if (_font.measure(next) > kScreenW - 16) {
                    _font.draw(_ctx.screen, _pages, 8, y, line, white);
                    y += _font.lineHeight;
                    next = std::string(1, char(c));
                }
                line = next;
            }
            if (y + _font.lineHeight < kScreenH - 20)
                _font.draw(_ctx.screen, _pages, 8, y, line, white);
        }
    }
    text(8, kScreenH - 14, "Left/Right: step   P: pages or sample", {150, 150, 150});
    _ctx.sys.updateScreen(_ctx.screen);
}

// --- Sounds -------------------------------------------------------------------

SoundPage::SoundPage(ViewerContext &ctx) : Page(ctx), _entries(collect<Entry>(ctx, {"dds"})) {
}

void SoundPage::activate() {
    use2D();
    setTitle("Sounds");
    select(_index);
}

void SoundPage::deactivate() {
    if (_ctx.mixer && _playing != audio::kNoSound)
        _ctx.mixer->stop(_playing);
    _playing = audio::kNoSound;
}

void SoundPage::select(int index) {
    deactivate();
    _index = std::clamp(index, 0, int(_entries.size()) - 1);
    const Entry &e = _entries[size_t(_index)];
    auto sound = std::make_shared<audio::Sound>();
    std::vector<uint8_t> data;
    Error err = _ctx.archives.at(e.archive)->read(e.name, data);
    if (!err)
        err = decodeDds(data, *sound);
    if (err) {
        _sound.reset();
        _info = err.message;
        return;
    }
    _sound = sound;
    char buf[96];
    std::snprintf(buf, sizeof buf, "%d Hz, %s, %.2f s", sound->rate, sound->channels == 2 ? "stereo" : "mono",
                  double(sound->frames()) / sound->rate);
    _info = buf;
    if (_autoPlay)
        play();
}

void SoundPage::play() {
    if (!_ctx.mixer || !_sound)
        return;
    if (_playing != audio::kNoSound && _ctx.mixer->isPlaying(_playing)) {
        _ctx.mixer->stop(_playing);
        _playing = audio::kNoSound;
        return;
    }
    _playing = _ctx.mixer->play(_sound, audio::SoundType::Effects);
}

void SoundPage::handleEvent(const Event &ev) {
    if (ev.type != EventType::KeyDown)
        return;
    const int n = int(_entries.size());
    switch (ev.key) {
    case key::Left:
    case key::Up:
        select((_index + n - 1) % n);
        break;
    case key::Right:
    case key::Down:
        select((_index + 1) % n);
        break;
    case key::PageUp:
        select(_index - 10);
        break;
    case key::PageDown:
        select(_index + 10);
        break;
    case key::Home:
        select(0);
        break;
    case key::End:
        select(n - 1);
        break;
    case key::Space:
    case key::Return:
        play();
        break;
    case 'a':
        _autoPlay = !_autoPlay;
        if (_autoPlay)
            play();
        break;
    case 'f': {
        int i = _index;
        while (i < n && _entries[size_t(i)].archive == _entries[size_t(_index)].archive)
            ++i;
        select(i % n);
        break;
    }
    default:
        break;
    }
}

void SoundPage::update(uint64_t) {
    clear(20, 22, 30);
    const int lh = font::kLineHeight + 2;
    const int rows = (kScreenH - 80) / lh;
    const int first = std::clamp(_index - rows / 2, 0, std::max(0, int(_entries.size()) - rows));
    for (int r = 0; r < rows && first + r < int(_entries.size()); ++r) {
        const Entry &e = _entries[size_t(first + r)];
        const bool sel = first + r == _index;
        if (sel)
            for (int y = 0; y < lh; ++y)
                for (int x = 4; x < kScreenW - 4; ++x) {
                    uint8_t *p = _ctx.screen.row(8 + r * lh + y) + x * 4;
                    p[0] = 50, p[1] = 70, p[2] = 110;
                }
        text(12, 9 + r * lh, e.archive + "/" + e.name, sel ? Color{255, 255, 255} : Color{180, 180, 180});
    }
    const bool playing = _ctx.mixer && _playing != audio::kNoSound && _ctx.mixer->isPlaying(_playing);
    text(8, kScreenH - 58,
         std::to_string(_index + 1) + "/" + std::to_string(_entries.size()) + "  " + _info +
             (playing ? "  (playing)" : ""),
         {230, 230, 140});
    if (!_ctx.mixer)
        text(8, kScreenH - 44, "No audio device: sounds can't be played.", {255, 120, 120});
    text(8, kScreenH - 30, "Up/Down, PageUp/PageDown: step   Space/Enter: play or stop", {150, 150, 150});
    text(8, kScreenH - 16,
         std::string("A: play each when selected (") + (_autoPlay ? "on" : "off") + ")   F: next archive",
         {150, 150, 150});
    _ctx.sys.updateScreen(_ctx.screen);
}

// --- Text ---------------------------------------------------------------------

TextPage::TextPage(ViewerContext &ctx) : Page(ctx), _files(collect<File>(ctx, {"txt", "puz"})) {
}

void TextPage::show(int index) {
    _index = (index + int(_files.size())) % int(_files.size());
    _scroll = 0;
    _lines.clear();
    use2D();
    const File &f = _files[size_t(_index)];
    setTitle("Text: " + f.name);
    std::vector<uint8_t> data;
    if (Error err = _ctx.archives.at(f.archive)->read(f.name, data)) {
        _lines.push_back(err.message);
        return;
    }
    // Lines wrapped to the screen's width.
    const size_t width = size_t((kScreenW - 16) / font::kAdvance);
    const std::string text =
        fromWindows1252(std::string_view(reinterpret_cast<const char *>(data.data()), data.size()));
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
            _lines.push_back(line.substr(0, cut));
            line.erase(0, cut);
        }
        _lines.push_back(line);
        if (end == text.size())
            break;
        pos = end + 1;
    }
}

void TextPage::handleEvent(const Event &ev) {
    const int page = (kScreenH - 40) / (font::kLineHeight + 1);
    if (ev.type == EventType::MouseWheel) {
        _scroll -= ev.wheelY * 3;
    } else if (ev.type == EventType::KeyDown) {
        switch (ev.key) {
        case key::Left:
            show(_index - 1);
            return;
        case key::Right:
            show(_index + 1);
            return;
        case key::Up:
            --_scroll;
            break;
        case key::Down:
            ++_scroll;
            break;
        case key::PageUp:
            _scroll -= page;
            break;
        case key::PageDown:
            _scroll += page;
            break;
        case key::Home:
            _scroll = 0;
            break;
        case key::End:
            _scroll = int(_lines.size());
            break;
        default:
            break;
        }
    }
    _scroll = std::clamp(_scroll, 0, std::max(0, int(_lines.size()) - page));
}

void TextPage::update(uint64_t) {
    clear(18, 20, 26);
    const File &f = _files[size_t(_index)];
    text(8, 6,
         f.archive + "/" + f.name + "  (" + std::to_string(_index + 1) + "/" + std::to_string(_files.size()) + ", " +
             std::to_string(_lines.size()) + " lines)",
         {230, 230, 140});
    const int lh = font::kLineHeight + 1;
    const int rows = (kScreenH - 40) / lh;
    for (int r = 0; r < rows && _scroll + r < int(_lines.size()); ++r)
        text(8, 22 + r * lh, _lines[size_t(_scroll + r)]);
    text(8, kScreenH - 14, "Left/Right: file   Up/Down, PageUp/PageDown, Home/End: scroll", {150, 150, 150});
    _ctx.sys.updateScreen(_ctx.screen);
}

} // namespace reluva::netimmerse
