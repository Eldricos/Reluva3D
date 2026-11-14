// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/ui/page.h"

#include "engines/awe/games.h"

#include "audio/mixer.h"
#include "common/settings.h"
#include "graphics/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace reluva::awe {

namespace {

bool startsWith(const std::string &name, const char *prefix) {
    const std::string lower = fs::toLower(name);
    return lower.compare(0, std::strlen(prefix), prefix) == 0;
}

} // namespace

Error runPages(System &sys, const GameTarget &target, const MakePages &make, const std::string &nothing) {
    const GameInfo *game = findGame(target.gameId);
    // As the game opens them (attwn's renamed and scrambled ones).
    std::unique_ptr<Archive> archive;
    if (Error err = openGameFiles(game, target.path, archive))
        return err;
    const Archive &files = *archive;
    Surface screen;
    screen.create(game ? game->width : kScreenW, game ? game->height : kScreenH, PixelFormat::kRGBA32);
    audio::Mixer mixer(sys.audioRate());
    mixer.setMasterVolume(settings::volume(target.settings, settings::kMasterVolume));
    const bool audioOut = !audio::startMixerOutput(sys, mixer);
    struct AudioStop { // stops the output before the mixer goes
        System &sys;
        bool on;
        ~AudioStop() {
            if (on)
                sys.stopAudio();
        }
    } audioStop{sys, audioOut};
    GameFonts fonts;
    const bool haveFonts = !fonts.load(files);
    ViewerContext ctx{
        sys, files, audioOut ? &mixer : nullptr, screen, target.savePath, haveFonts ? &fonts : nullptr, {}, -1, game};

    // Made after what they draw with, so they go first.
    const std::vector<std::unique_ptr<Page>> pages = make(ctx);
    const auto shows = [&](size_t i) { return pages[i]->count() > 0; };
    size_t page = 0;
    while (page < pages.size() && !shows(page))
        page++;
    if (page == pages.size())
        return Error::fail("No " + nothing + " found in " + target.path.string());
    pages[page]->activate();

    int shots = 0;
    // Frames shown in the last second, after the title.
    uint64_t second = sys.getMillis();
    int frames = 0;
    while (true) {
        if (const uint64_t now = sys.getMillis(); now - second >= 1000) {
            ctx.fps = int(frames * 1000 / (now - second));
            ctx.setTitle(ctx.title);
            second = now;
            frames = 0;
        }
        frames++;
        Event ev;
        while (sys.pollEvent(ev)) {
            Page &p = *pages[page];
            if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher ||
                (ev.type == EventType::kKeyDown && ev.key == key::kEscape && !p.wantsEscape())) {
                p.deactivate();
                return Error::success();
            }
            if (ev.type == EventType::kKeyDown && ev.key == key::kF1 + 11) {
                char name[48];
                std::snprintf(name, sizeof name, "screenshots/%s_%03d.tga", p.name(), ++shots);
                p.requestScreenshot(target.savePath / name);
                continue;
            }
            if (ev.type == EventType::kKeyDown && ev.key == key::kTab && pages.size() > 1) {
                p.deactivate();
                do
                    page = (page + 1) % pages.size();
                while (!shows(page));
                pages[page]->activate();
                continue;
            }
            p.handleEvent(ev);
            if (p.quitRequested()) {
                p.deactivate();
                return Error::success();
            }
        }
        pages[page]->update(sys.getMillis());
        // 3D pages are paced by presenting (vsync); the rest needn't spin.
        if (!pages[page]->uses3D())
            sys.delayMillis(10);
    }
}

std::string fileNameOf(const std::string &path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string folderOf(const std::string &path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

bool hasExtension(const std::string &name, std::initializer_list<const char *> exts) {
    const std::string lower = fs::toLower(name);
    for (const char *e : exts)
        if (lower.size() > std::strlen(e) && lower.compare(lower.size() - std::strlen(e), std::string::npos, e) == 0)
            return true;
    return false;
}

std::vector<std::string> listFiles(const Archive &files, std::initializer_list<const char *> folders,
                                   std::initializer_list<const char *> exts) {
    std::vector<std::string> out;
    for (const std::string &n : files.list()) {
        bool inFolder = folders.size() == 0;
        for (const char *f : folders)
            inFolder |= startsWith(n, f);
        if (inFolder && hasExtension(n, exts))
            out.push_back(n);
    }
    std::sort(out.begin(), out.end(),
              [](const std::string &a, const std::string &b) { return fs::toLower(a) < fs::toLower(b); });
    return out;
}

std::string shorten(const std::string &s, size_t max) {
    return s.size() <= max ? s : s.substr(0, max - 3) + "...";
}

// --- Page ---------------------------------------------------------------------

void Page::begin2D() {
    _ctx.sys.initGraphics(frameW(), frameH(), PixelFormat::kRGBA32);
    _ctx.sys.setMouseCursorVisible(true);
}

void Page::clear(uint8_t r, uint8_t g, uint8_t b) {
    // A pixel at a time, as one word (a byte at a time took milliseconds).
    const uint8_t px[4] = {r, g, b, 255};
    uint32_t word;
    std::memcpy(&word, px, 4);
    const size_t n = _ctx.screen.pixels.size() / 4;
    std::vector<uint32_t> row(size_t(std::max(_ctx.screen.w, 1)), word);
    for (size_t i = 0; i < n; i += row.size())
        std::memcpy(_ctx.screen.pixels.data() + i * 4, row.data(), std::min(row.size(), n - i) * 4);
}

void Page::fill(int x, int y, int w, int h, Color c, uint8_t alpha) {
    Surface &s = _ctx.screen;
    for (int yy = std::max(0, y); yy < std::min(s.h, y + h); yy++)
        for (int xx = std::max(0, x); xx < std::min(s.w, x + w); xx++) {
            // Over what's there, by its alpha too: on an overlay, transparent
            // where nothing is drawn, the colour isn't darkened by the
            // emptiness under it.
            uint8_t *p = s.row(yy) + xx * 4;
            const int under = p[3] * (255 - alpha) / 255;
            const int out = alpha + under;
            if (out == 0)
                continue;
            p[0] = uint8_t((c.r * alpha + p[0] * under) / out);
            p[1] = uint8_t((c.g * alpha + p[1] * under) / out);
            p[2] = uint8_t((c.b * alpha + p[2] * under) / out);
            p[3] = uint8_t(out);
        }
}

void Page::drawSubtitles(const std::vector<Subtitle> &subtitles, double seconds) {
    // The game plays them as a conversation in its movie mode (0x43a610):
    // no window, the line due wrapped at 944 and centred in the bottom 64
    // pixels (0x43cffc, 0x43d2a8), coloured as a conversation's are
    // (0x43c070: Antoinette 8, the others 9, then 10 when the speaker
    // changes).
    const TextStyle *style = _ctx.fonts ? _ctx.fonts->style("dialogue") : nullptr;
    if (!style || !style->font)
        return;
    const Subtitle *due = nullptr;
    int colour = 8;
    int dueColour = 8;
    std::string lastSpeaker;
    for (const Subtitle &sub : subtitles) {
        const std::string speaker = fs::toLower(sub.speaker);
        if (speaker.find("antoinette") != std::string::npos) {
            colour = 8;
        } else {
            if (!lastSpeaker.empty() && speaker != lastSpeaker)
                colour = colour == 9 ? 10 : 9;
            else if (colour != 9 && colour != 10)
                colour = 9;
            lastSpeaker = speaker;
        }
        if (seconds >= sub.start && seconds < sub.end) {
            due = &sub;
            dueColour = colour;
        }
    }
    if (!due)
        return;
    static const char *const kColours[] = {"dialogue for main character",
                                           "dialogue text for second character in conversation",
                                           "dialogue text for third character in conversation"};
    const Color c = _ctx.fonts->colour(kColours[dueColour - 8]);
    const std::vector<std::string> lines = gameRows(*style->font, style->pixels, due->line, 944);
    const int lh = style->font->lineHeight(style->pixels);
    int y = frameH() - 64 + (64 - int(lines.size()) * lh) / 2;
    for (const std::string &l : lines) {
        _ctx.fonts->draw(_ctx.screen, *style, (frameW() - style->font->width(l, style->pixels)) / 2, y, l, c);
        y += lh;
    }
}

void Page::frame(int x, int y, int w, int h, Color c) {
    fill(x, y, w, 1, c);
    fill(x, y + h - 1, w, 1, c);
    fill(x, y, 1, h, c);
    fill(x + w - 1, y, 1, h, c);
}

int Page::text(int x, int y, const std::string &s, Color c, int scale) {
    return font::drawText(_ctx.screen, x, y, s, c, scale);
}

void Page::label(int x, int y, const std::string &s, Color c) {
    fill(x - 2, y - 1, int(font::length(s)) * font::kAdvance + 4, font::kLineHeight + 1, {0, 0, 0}, 170);
    text(x, y, s, c);
}

void Page::help(const std::string &keys) {
    fill(0, frameH() - font::kLineHeight - 3, frameW(), font::kLineHeight + 3, {0, 0, 0}, 200);
    text(4, frameH() - font::kLineHeight - 1, keys + "   Tab: next page   Esc: launcher", {170, 190, 210});
}

void Page::finishScreenshot(gfx::Renderer3D *renderer, const Surface *overlay) {
    if (_shotFile.empty())
        return;
    image::Image img;
    img.w = frameW();
    img.h = frameH();
    if (renderer) {
        if (!renderer->takeCapture(img.rgba))
            return; // not kept yet: try the next frame
        if (overlay && overlay->pixels.size() == img.rgba.size())
            for (size_t i = 0; i < img.rgba.size(); i += 4) {
                const int a = overlay->pixels[i + 3];
                for (size_t c = 0; c < 3; c++)
                    img.rgba[i + c] = uint8_t((overlay->pixels[i + c] * a + img.rgba[i + c] * (255 - a)) / 255);
            }
    } else if (overlay) {
        img.rgba = overlay->pixels;
    }
    std::error_code ec;
    std::filesystem::create_directories(_shotFile.parent_path(), ec);
    const std::vector<uint8_t> tga = image::encodeTga(img);
    std::ofstream out(_shotFile, std::ios::binary);
    out.write(reinterpret_cast<const char *>(tga.data()), std::streamsize(tga.size()));
    _ctx.setTitle((out ? "Saved " : "Couldn't save ") + _shotFile.string());
    _shotFile.clear();
}

int Page::step(uint32_t key, int index, int count) {
    if (count <= 0)
        return 0;
    int to = index;
    if (key == key::kLeft)
        to = index - 1;
    else if (key == key::kRight)
        to = index + 1;
    else if (key == key::kPageUp)
        to = index - 10;
    else if (key == key::kPageDown)
        to = index + 10;
    else if (key == key::kHome)
        to = 0;
    else if (key == key::kEnd)
        to = count - 1;
    else
        return 0;
    to = ((to % count) + count) % count;
    return to - index == 0 ? 0 : to - index;
}

const image::Image &Page::picture(const std::string &name) {
    const std::string key = normalizeArchiveName(name);
    auto it = _pictures.find(key);
    if (it != _pictures.end())
        return it->second;
    if (_pictures.size() > 64)
        _pictures.clear();
    image::Image &img = _pictures[key];
    std::vector<uint8_t> data;
    if (_ctx.files.read(key, data) || image::decode(data, img))
        img = image::Image();
    return img;
}

} // namespace reluva::awe
