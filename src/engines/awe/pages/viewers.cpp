// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/pages/viewers.h"

#include "audio/decode.h"
#include "engines/viewer.h"
#include "engines/awe/game/scene_page.h"
#include "graphics/font.h"
#include "common/fs.h"
#include "common/text.h"
#include "engines/awe/formats/tables.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace reluva::awe {

// --- Pictures ---------------------------------------------------------------------

PicturePage::PicturePage(ViewerContext &ctx) : Page(ctx) {
    _names = listFiles(ctx.files, {"locations/", "interface/", "inventory/", "elements/", "models/", "cutscenes/"},
                       {".jpg", ".png", ".tga", ".bmp"});
}

void PicturePage::activate() {
    begin2D();
    show();
}

void PicturePage::show() {
    clear(24, 24, 28);
    if (_names.empty())
        return;
    const std::string &name = _names[size_t(_index)];
    const image::Image &img = picture(name);
    if (img.w > 0) {
        // Scaled down (nearest) to fit, over a checkerboard that shows alpha.
        const int areaH = frameH() - 2 * font::kLineHeight - 8;
        const float scale = std::min({1.0f, float(frameW()) / float(img.w), float(areaH) / float(img.h)});
        const int w = std::max(1, int(float(img.w) * scale));
        const int h = std::max(1, int(float(img.h) * scale));
        const int x0 = (frameW() - w) / 2;
        const int y0 = font::kLineHeight + 4 + (areaH - h) / 2;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                const uint8_t *src =
                    img.pixel(std::min(img.w - 1, int(float(x) / scale)), std::min(img.h - 1, int(float(y) / scale)));
                const uint8_t check = ((x / 8 + y / 8) & 1) ? 90 : 60;
                uint8_t *dst = _ctx.screen.row(y0 + y) + (x0 + x) * 4;
                for (int c = 0; c < 3; c++)
                    dst[c] = uint8_t((src[c] * src[3] + check * (255 - src[3])) / 255);
            }
        label(4, 2,
              name + "  " + std::to_string(img.w) + "x" + std::to_string(img.h) + "  (" + std::to_string(_index + 1) +
                  "/" + std::to_string(_names.size()) + ")");
    } else {
        label(4, 2, name + ": can't be decoded", {255, 120, 120});
    }
    help("Left/Right: picture  PgUp/PgDn: 10  Home/End");
    present();
    _ctx.setTitle("Pictures: " + name);
}

void PicturePage::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown)
        return;
    if (const int d = step(ev.key, _index, int(_names.size()))) {
        _index += d;
        show();
    }
}
// --- Movies ---------------------------------------------------------------------

MoviePage::MoviePage(ViewerContext &ctx) : Page(ctx), _player(ctx.mixer) {
    _names = listFiles(ctx.files, {"movies/", "elements/", "locations/", "fmv/", "inventory/"}, {".bik"});
}

MoviePage::~MoviePage() {
    _player.stop();
}

void MoviePage::activate() {
    begin2D();
    open();
}

void MoviePage::deactivate() {
    _player.stop();
}

void MoviePage::open() {
    _player.stop();
    _subtitles.clear();
    _info.clear();
    clear(0, 0, 0);
    if (_names.empty())
        return;
    const std::string &name = _names[size_t(_index)];
    {
        // Its subtitles, if it has some: Text/Movies/<its name>.txt.
        std::string stem = fileNameOf(name);
        stem = stem.substr(0, stem.rfind('.'));
        std::vector<uint8_t> text;
        if (!_ctx.files.read("text/movies/" + fs::toLower(stem) + ".txt", text))
            _subtitles = moviesOwn(parseSubtitles(decodeText(text)), fs::toLower(stem));
    }
    std::vector<uint8_t> data;
    Error e = _ctx.files.read(name, data);
    if (!e)
        e = _player.open(std::move(data), _ctx.sys.getMillis());
    if (e) {
        _info = e.message;
        return;
    }
    const bink::Movie &movie = *_player.movie();
    char buf[160];
    std::snprintf(buf, sizeof buf, "%dx%d, %u frames at %.2f fps, revision %c", movie.width(), movie.height(),
                  movie.frameCount(), _player.fps(), movie.revision());
    _info = buf;
    if (_player.hasSound())
        _info += ", sound " + std::to_string(movie.audioRate()) + " Hz";
    if (!_player.error().empty())
        _info += ", " + _player.error();
    _ctx.setTitle("Movies: " + name);
}

void MoviePage::update(uint64_t nowMs) {
    _player.update(nowMs);
    // Video loops (no sound) start over.
    if (_player.loaded() && !_player.hasSound() && !_player.paused() && _player.done(nowMs)) {
        open();
        return;
    }
    clear(0, 0, 0);
    const image::Image &frame = _player.frame();
    if (frame.w)
        image::blit(_ctx.screen, frame, (frameW() - frame.w) / 2, (frameH() - frame.h) / 2);
    drawSubtitles(_subtitles, _player.seconds());
    label(4, 2,
          (_names.empty() ? std::string("No movies") : _names[size_t(_index)]) + "  (" + std::to_string(_index + 1) +
              "/" + std::to_string(_names.size()) + ")  " + _info +
              (_player.loaded()
                   ? "  frame " + std::to_string(_player.next()) + "/" + std::to_string(_player.movie()->frameCount())
                   : "") +
              (_subtitles.empty() ? "" : ", " + std::to_string(_subtitles.size()) + " subtitles"),
          {255, 230, 150});
    help("Left/Right: movie  Space: pause  Enter: restart");
    Page::update(nowMs);
}

void MoviePage::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown)
        return;
    if (const int d = step(ev.key, _index, int(_names.size()))) {
        _index += d;
        open();
    } else if (ev.key == key::kReturn) {
        open();
    } else if (ev.key == key::kSpace && _player.loaded()) {
        _player.pause(!_player.paused(), _ctx.sys.getMillis());
    }
}

// --- Text ---------------------------------------------------------------------

TextPage::TextPage(ViewerContext &ctx) : Page(ctx) {
    _names = listFiles(ctx.files, {"text/", "dialogue/"}, {".txt"});
}

void TextPage::activate() {
    begin2D();
    load();
}

void TextPage::load() {
    _scroll = 0;
    _lines.clear();
    if (_names.empty())
        return draw();
    std::vector<uint8_t> data;
    if (Error e = _ctx.files.read(_names[size_t(_index)], data)) {
        _lines.push_back(e.message);
        return draw();
    }
    const std::string text = decodeText(data);
    const size_t width = size_t(frameW() - 8) / font::kAdvance;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        // A table's empty rows are only tabs.
        if (line.find_first_not_of("\t ") == std::string::npos && !line.empty()) {
            start = end + 1;
            continue;
        }
        for (size_t i; (i = line.find('\t')) != std::string::npos;)
            line.replace(i, 1, " | ");
        // Wrapped to the screen's width.
        do {
            size_t cut = line.size();
            if (font::length(line) > width) {
                cut = 0;
                for (size_t chars = 0, i = 0; i < line.size() && chars < width; chars++)
                    text::nextUtf8(line, i), cut = i;
            }
            _lines.push_back(line.substr(0, cut));
            line = cut < line.size() ? "    " + line.substr(cut) : std::string();
        } while (!line.empty());
        start = end + 1;
    }
    draw();
}

void TextPage::draw() {
    clear(20, 22, 30);
    const int rows = (frameH() - 2 * font::kLineHeight - 8) / font::kLineHeight;
    label(4, 2,
          (_names.empty() ? std::string("No text files") : _names[size_t(_index)]) + "  (" +
              std::to_string(_index + 1) + "/" + std::to_string(_names.size()) + "), lines " +
              std::to_string(_scroll + 1) + "-" + std::to_string(std::min(int(_lines.size()), _scroll + rows)) +
              " of " + std::to_string(_lines.size()),
          {255, 230, 150});
    for (int i = 0; i < rows && size_t(_scroll + i) < _lines.size(); i++)
        text(4, font::kLineHeight + 6 + i * font::kLineHeight, _lines[size_t(_scroll + i)]);
    help("Left/Right: file  Up/Down, PgUp/PgDn (with Shift: file by 10): scroll");
    present();
    _ctx.setTitle("Text: " + (_names.empty() ? std::string() : _names[size_t(_index)]));
}

void TextPage::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown)
        return;
    const int rows = (frameH() - 2 * font::kLineHeight - 8) / font::kLineHeight;
    const int maxScroll = std::max(0, int(_lines.size()) - rows);
    const bool shift = ev.mods & keymod::kShift;
    if (ev.key == key::kLeft || ev.key == key::kRight ||
        ((ev.key == key::kPageUp || ev.key == key::kPageDown) && shift)) {
        if (const int d = step(ev.key, _index, int(_names.size()))) {
            _index += d;
            load();
        }
    } else if (ev.key == key::kUp || ev.key == key::kDown || ev.key == key::kPageUp || ev.key == key::kPageDown ||
               ev.key == key::kHome || ev.key == key::kEnd) {
        const int by = ev.key == key::kUp         ? -1
                       : ev.key == key::kDown     ? 1
                       : ev.key == key::kPageUp   ? -rows
                       : ev.key == key::kPageDown ? rows
                       : ev.key == key::kHome     ? -int(_lines.size())
                                                  : int(_lines.size());
        _scroll = std::clamp(_scroll + by, 0, maxScroll);
        draw();
    }
}

// --- Sounds ---------------------------------------------------------------------

SoundPage::SoundPage(ViewerContext &ctx) : Page(ctx) {
    _names = listFiles(ctx.files, {"sound/", "models/", "people/"}, {".wav", ".mp3"});
}

void SoundPage::activate() {
    begin2D();
    draw();
}

void SoundPage::deactivate() {
    if (_ctx.mixer)
        _ctx.mixer->stopAll();
    _playing = audio::kNoSound;
}

void SoundPage::play() {
    _info.clear();
    std::vector<uint8_t> data;
    auto sound = std::make_shared<audio::Sound>();
    if (Error e = _ctx.files.read(_names[size_t(_index)], data)) {
        _info = e.message;
    } else if (Error e2 = audio::decodeSound(data, *sound)) {
        _info = e2.message;
    } else {
        char buf[128];
        std::snprintf(buf, sizeof buf, "%s, %d Hz, %d channel%s, %.2f s", audio::formatName(sound->format), sound->rate,
                      sound->channels, sound->channels == 1 ? "" : "s",
                      sound->rate ? double(sound->frames()) / sound->rate : 0.0);
        _info = buf;
        if (_ctx.mixer) {
            _ctx.mixer->stopAll();
            _playing = _ctx.mixer->play(sound, audio::SoundType::kEffects);
        } else {
            _info += " (no audio device)";
        }
    }
    draw();
}

void SoundPage::draw() {
    clear(18, 24, 22);
    const int rows = (frameH() - 4 * font::kLineHeight - 8) / font::kLineHeight;
    const int first = std::clamp(_index - rows / 2, 0, std::max(0, int(_names.size()) - rows));
    label(4, 2, std::to_string(_names.size()) + " sounds" + (_info.empty() ? "" : ":  " + _info), {255, 230, 150});
    for (int i = 0; i < rows && first + i < int(_names.size()); i++) {
        const bool current = first + i == _index;
        const int y = 2 * font::kLineHeight + 4 + i * font::kLineHeight;
        if (current)
            fill(0, y - 1, frameW(), font::kLineHeight, {60, 80, 70});
        text(8, y, _names[size_t(first + i)], current ? Color{255, 255, 255} : Color{190, 200, 195});
    }
    help("Up/Down (Left/Right): sound  PgUp/PgDn: 10  Space/Enter: play  S: stop");
    present();
    _ctx.setTitle("Sounds");
}

void SoundPage::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown || _names.empty())
        return;
    const uint32_t k = ev.key == key::kUp ? key::kLeft : ev.key == key::kDown ? key::kRight : ev.key;
    if (const int d = step(k, _index, int(_names.size()))) {
        _index += d;
        _info.clear();
        draw();
    } else if (ev.key == key::kSpace || ev.key == key::kReturn) {
        play();
    } else if (ev.key == 's' && _ctx.mixer) {
        _ctx.mixer->stopAll();
    }
}

} // namespace reluva::awe

namespace reluva {

namespace {

// The viewer's pages, the game's Scenes first (its browser on): Tab moves
// between them, Esc leaves.
class AweViewer : public Engine {
public:
    AweViewer(System &system, GameTarget target) : _sys(system), _target(std::move(target)) {}

    Error run() override {
        return awe::runPages(
            _sys, _target,
            [](awe::ViewerContext &ctx) {
                std::vector<std::unique_ptr<awe::Page>> pages;
                pages.push_back(std::make_unique<awe::ScenePage>(ctx, awe::ScenePage::Browser::kOn));
                pages.push_back(std::make_unique<awe::ModelPage>(ctx));
                pages.push_back(std::make_unique<awe::MoviePage>(ctx));
                pages.push_back(std::make_unique<awe::PicturePage>(ctx));
                pages.push_back(std::make_unique<awe::TextPage>(ctx));
                pages.push_back(std::make_unique<awe::SoundPage>(ctx));
                return pages;
            },
            "scenes, models, pictures, text or sounds");
    }

private:
    System &_sys;
    GameTarget _target;
};

class AweMetaViewer : public MetaViewer {
public:
    const char *engineId() const override { return "awe"; }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<AweViewer>(system, target);
    }
};

} // namespace

const MetaViewer &aweMetaViewer() {
    static const AweMetaViewer kInstance;
    return kInstance;
}

} // namespace reluva
