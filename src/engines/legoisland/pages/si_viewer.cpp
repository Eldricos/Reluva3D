// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "audio/wav.h"
#include "common/stream.h"
#include "engines/legoisland/pages/viewers.h"
#include "graphics/font.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace reluva::legoisland {

namespace {

constexpr int kListW = 360;
constexpr int kRowH = font::kLineHeight + 1;
constexpr Color kBackground = {40, 40, 48};
constexpr Color kText = {230, 230, 230};
constexpr Color kDim = {150, 150, 160};
constexpr Color kSelected = {70, 90, 140};

void fillRect(Surface &s, int x, int y, int w, int h, Color c) {
    const int x0 = std::max(x, 0);
    const int y0 = std::max(y, 0);
    const int x1 = std::min(x + w, s.w);
    const int y1 = std::min(y + h, s.h);
    for (int py = y0; py < y1; ++py) {
        uint8_t *p = s.row(py) + size_t(x0) * 4;
        for (int px = x0; px < x1; ++px, p += 4) {
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
    }
}

const char *typeName(uint16_t type) {
    switch (SiType(type)) {
    case SiType::kAnimation:
        return "animation";
    case SiType::kSound:
        return "sound";
    case SiType::kSerial:
        return "in order";
    case SiType::kParallel:
        return "together";
    case SiType::kEvent:
        return "event";
    case SiType::kSelect:
        return "one of";
    case SiType::kStill:
        return "still";
    case SiType::kObject:
        return "3D object";
    }
    return "unknown type";
}

std::string number(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

std::string vec(const std::array<double, 3> &v) {
    return "(" + number(v[0]) + ", " + number(v[1]) + ", " + number(v[2]) + ")";
}

// An 8-bit picture in its palette's colours.
void toImage(const Surface &s, const uint8_t *palette, image::Image &out) {
    out.w = s.w;
    out.h = s.h;
    out.rgba.resize(size_t(s.w) * size_t(s.h) * 4);
    for (int y = 0; y < s.h; y++)
        for (int x = 0; x < s.w; x++) {
            const uint8_t *c = palette + 3 * s.row(y)[x];
            uint8_t *p = out.pixel(x, y);
            p[0] = c[0];
            p[1] = c[1];
            p[2] = c[2];
            p[3] = 255;
        }
}

// Text cut into lines of at most `chars` characters, at spaces where it can be.
std::vector<std::string> wrap(const std::string &text, size_t chars) {
    std::vector<std::string> lines;
    std::string rest = text;
    while (rest.size() > chars) {
        size_t cut = rest.rfind(' ', chars);
        if (cut == std::string::npos || cut == 0)
            cut = chars;
        lines.push_back(rest.substr(0, cut));
        rest = rest.substr(cut == chars ? cut : cut + 1);
    }
    lines.push_back(rest);
    return lines;
}

} // namespace

SiViewer::SiViewer(System &sys, Surface &screen, std::vector<std::filesystem::path> files, audio::Mixer *mixer)
    : _sys(sys), _screen(screen), _files(std::move(files)), _mixer(mixer) {
}

void SiViewer::activate() {
    if (_rows.empty() && _fileError.empty() && !_files.empty())
        openFile(_file);
    redraw();
}

void SiViewer::deactivate() {
    stop();
}

void SiViewer::openFile(size_t index) {
    stop();
    _file = index;
    _rows.clear();
    _fileError.clear();
    if (Error e = _si.open(_files[index])) {
        _fileError = e.message;
        _si = SiFile();
    }
    buildRows();
    select(0);
}

void SiViewer::buildRows() {
    _rows.clear();
    // Parents before their children, each a step further in.
    struct Walk {
        std::vector<Row> &rows;
        bool mediaOnly;
        void add(const SiObject &o, int depth) {
            if (!mediaOnly || o.isMedia())
                rows.push_back({&o, mediaOnly ? 0 : depth});
            for (const SiObject &c : o.children)
                add(c, depth + 1);
        }
    } walk{_rows, _mediaOnly};
    for (const SiObject &o : _si.objects())
        walk.add(o, 0);
}

void SiViewer::handleKey(uint32_t key) {
    if (_files.empty())
        return;
    const size_t n = _rows.size();
    if (key == key::kRight || key == key::kLeft) {
        const size_t f = _files.size();
        openFile(key == key::kRight ? (_file + 1) % f : (_file + f - 1) % f);
    } else if (key == 'm') {
        const SiObject *was = n ? _rows[_current].object : nullptr;
        _mediaOnly = !_mediaOnly;
        buildRows();
        size_t at = 0;
        for (size_t i = 0; i < _rows.size(); i++)
            if (_rows[i].object == was)
                at = i;
        select(at);
    } else if (key == 'a') {
        _autoPlay = !_autoPlay;
    } else if (key == key::kSpace || key == key::kReturn) {
        if (_animating || _playing != audio::kNoSound)
            stop();
        else
            play();
    } else if (n == 0) {
        return;
    } else if (key == key::kDown) {
        select(std::min(_current + 1, n - 1));
    } else if (key == key::kUp) {
        select(_current ? _current - 1 : 0);
    } else if (key == key::kPageDown) {
        select(std::min(_current + 20, n - 1));
    } else if (key == key::kPageUp) {
        select(_current >= 20 ? _current - 20 : 0);
    } else if (key == key::kHome) {
        select(0);
    } else if (key == key::kEnd) {
        select(n - 1);
    } else {
        return;
    }
    redraw();
}

void SiViewer::select(size_t row) {
    stop();
    _current = row;
    loadMedium();
    if (_autoPlay)
        play();
}

void SiViewer::loadMedium() {
    _mediumInfo.clear();
    _mediumError.clear();
    _pieces = 0;
    _still = image::Image();
    _frame = image::Image();
    _flic.reset();
    _smk.reset();
    _sound.reset();
    if (_rows.empty())
        return;
    const SiObject &o = *_rows[_current].object;
    if (!o.isMedia())
        return;
    std::vector<SiPiece> pieces;
    if (Error e = _si.readPieces(o.id, pieces)) {
        _mediumError = e.message;
        return;
    }
    _pieces = pieces.size();
    if (mediaExtension(o).empty())
        return;
    std::vector<uint8_t> file;
    if (Error e = rebuildMediaFile(o, pieces, file)) {
        _mediumError = e.message;
        return;
    }
    if (o.format == "STL") {
        if (Error e = image::decode(file, _still))
            _mediumError = e.message;
        else
            _mediumInfo = std::to_string(_still.w) + "x" + std::to_string(_still.h);
    } else if (o.format == "FLC") {
        _flic = std::make_unique<flic::Decoder>();
        if (!_flic->load(std::make_unique<MemoryReadStream>(std::move(file)))) {
            _mediumError = "not a FLIC";
            _flic.reset();
            return;
        }
        _mediumInfo = std::to_string(_flic->width()) + "x" + std::to_string(_flic->height()) + ", " +
                      std::to_string(_flic->frameCount()) + " frames";
        if (_flic->decodeNextFrame())
            toImage(_flic->surface(), _flic->palette(), _frame);
    } else if (o.format == "SMK") {
        _smk = std::make_unique<smacker::Decoder>();
        if (!_smk->load(std::make_unique<MemoryReadStream>(std::move(file)))) {
            _mediumError = "not a Smacker file";
            _smk.reset();
            return;
        }
        _mediumInfo = std::to_string(_smk->width()) + "x" + std::to_string(_smk->height()) + ", " +
                      std::to_string(_smk->frameCount()) + " frames";
        if (_smk->decodeNextFrame())
            toImage(_smk->surface(), _smk->palette(), _frame);
    } else if (o.format == "WAV") {
        auto sound = std::make_shared<audio::Sound>();
        if (Error e = audio::decodeWav(file, *sound)) {
            _mediumError = e.message;
            return;
        }
        char buf[64];
        std::snprintf(buf, sizeof buf, "%d Hz, %d-bit, %.2f s", sound->rate, sound->sourceBits,
                      sound->rate ? double(sound->frames()) / sound->rate : 0.0);
        _mediumInfo = buf;
        _sound = std::move(sound);
    }
}

void SiViewer::play() {
    stop();
    if (_sound && _mixer) {
        _playing = _mixer->play(_sound, audio::SoundType::kEffects);
        return;
    }
    if (_flic || _smk) {
        // From the start: decoders only go forward.
        loadMedium();
        _animating = true;
        _nextFrameMs = _sys.getMillis();
    }
}

void SiViewer::stop() {
    if (_mixer && _playing != audio::kNoSound)
        _mixer->stop(_playing);
    _playing = audio::kNoSound;
    _animating = false;
}

void SiViewer::update(uint64_t nowMs) {
    if (_animating && nowMs >= _nextFrameMs) {
        bool more = false;
        uint32_t micros = 100000;
        if (_flic) {
            more = _flic->decodeNextFrame();
            micros = _flic->frameMicros();
            if (more)
                toImage(_flic->surface(), _flic->palette(), _frame);
        } else if (_smk) {
            more = _smk->decodeNextFrame();
            micros = _smk->frameMicros();
            if (more)
                toImage(_smk->surface(), _smk->palette(), _frame);
        }
        _animating = more;
        _nextFrameMs += std::max<uint32_t>(micros / 1000, 1);
        redraw();
        return;
    }
    if (_playing != audio::kNoSound && nowMs - _lastDrawMs >= 200) {
        if (_mixer && !_mixer->isPlaying(_playing))
            _playing = audio::kNoSound;
        _lastDrawMs = nowMs;
        redraw();
    }
}

void SiViewer::redraw() {
    fillRect(_screen, 0, 0, _screen.w, _screen.h, kBackground);
    if (_files.empty())
        return;
    const std::string fileName = _files[_file].filename().string();
    font::drawText(_screen, 4, 2,
                   std::to_string(_file + 1) + "/" + std::to_string(_files.size()) + "  " + fileName +
                       (_fileError.empty() ? "  " + std::to_string(_rows.size()) + (_mediaOnly ? " media" : " objects")
                                           : "  " + _fileError),
                   kText);
    drawList(0, 16, kListW, _screen.h - 32);
    drawDetails(kListW + 8, 16, _screen.w - kListW - 12);
    fillRect(_screen, 0, _screen.h - 14, _screen.w, 14, {0, 0, 0});
    font::drawText(_screen, 4, _screen.h - 12,
                   std::string("Up/Down: object  Left/Right: SI file  M: ") +
                       (_mediaOnly ? "all objects" : "media only") + "  Space: play/stop  A: autoplay " +
                       (_autoPlay ? "on" : "off") + (_mixer ? "" : "  (no audio device)") + "  Esc: leave",
                   kDim);
    _sys.setWindowTitle("Reluva - LEGO Island - " + fileName);
}

void SiViewer::drawList(int x, int y, int w, int h) {
    const int visible = h / kRowH;
    if (_rows.empty() || visible <= 0)
        return;
    // The selected row stays in view, a third of the way down where it can.
    int first = int(_current) - visible / 3;
    first = std::max(0, std::min(first, int(_rows.size()) - visible));
    for (int i = 0; i < visible && first + i < int(_rows.size()); i++) {
        const Row &r = _rows[size_t(first + i)];
        const int ry = y + i * kRowH;
        if (size_t(first + i) == _current)
            fillRect(_screen, x, ry, w, kRowH, kSelected);
        const std::string label = r.object->name + (r.object->isMedia() ? "  " + r.object->format : "");
        font::drawText(_screen, x + 4 + r.depth * 10, ry + 1, label, r.object->isMedia() ? kText : kDim);
    }
}

void SiViewer::drawDetails(int x, int y, int w) {
    if (_rows.empty())
        return;
    const SiObject &o = *_rows[_current].object;
    std::vector<std::string> lines = {
        o.name + "  (id " + std::to_string(o.id) + ", " + typeName(o.type) + ")",
        "presenter: " + (o.presenter.empty() ? std::string("its type's own") : o.presenter),
        "start " + std::to_string(o.startTime) + " ms, " +
            (o.duration < 0 ? std::string("for as long as wanted") : "for " + std::to_string(o.duration) + " ms") +
            (o.loops != 1 ? ", " + std::to_string(o.loops) + " times" : "") + ", flags 0x" +
            [&] {
                char buf[16];
                std::snprintf(buf, sizeof buf, "%x", o.flags);
                return std::string(buf);
            }(),
        "location " + vec(o.location) + "  direction " + vec(o.direction) + "  up " + vec(o.up),
    };
    const size_t chars = size_t(w / 6);
    if (!o.extra.empty())
        for (const std::string &l : wrap("extra: " + o.extra, chars))
            lines.push_back(l);
    if (o.type == uint16_t(SiType::kSelect)) {
        std::string choices = "chosen by " + o.variable + ":";
        for (const std::string &c : o.choices)
            choices += " " + c;
        lines.push_back(choices);
    }
    if (!o.children.empty())
        lines.push_back(std::to_string(o.children.size()) + " children");
    if (o.isMedia()) {
        lines.push_back(o.format + " from " + o.fileName);
        std::string how = std::to_string(_pieces) + " pieces";
        if (o.format == "WAV")
            how += ", volume " + std::to_string(o.volume);
        if (o.format == "FLC" || o.format == "SMK")
            how += ", " + std::to_string(o.frameRate) + " frames a second";
        if (!_mediumInfo.empty())
            how += ": " + _mediumInfo;
        lines.push_back(how);
        if (!_mediumError.empty())
            lines.push_back("can't show it: " + _mediumError);
        if (_playing != audio::kNoSound && _sound)
            lines.push_back("playing " + number(double(_mixer->position(_playing)) / _sound->rate) + " s");
        else if (_flic || _smk)
            lines.push_back("frame " + std::to_string((_flic ? _flic->currentFrame() : _smk->currentFrame()) + 1));
    }
    int ly = y;
    for (const std::string &l : lines) {
        font::drawText(_screen, x, ly, l, kText);
        ly += kRowH;
    }
    drawPicture(x, ly + 8, w, _screen.h - 16 - (ly + 8));
}

void SiViewer::drawPicture(int x, int y, int w, int h) {
    const image::Image &img = _still.w ? _still : _frame;
    if (!img.w || w <= 0 || h <= 0)
        return;
    // Twice its size where it fits: the game's animations are small.
    const int scale = img.w * 2 <= w && img.h * 2 <= h ? 2 : 1;
    if (scale == 1) {
        image::blit(_screen, img, x, y);
        return;
    }
    image::Image big;
    big.w = img.w * 2;
    big.h = img.h * 2;
    big.rgba.resize(size_t(big.w) * size_t(big.h) * 4);
    for (int py = 0; py < big.h; py++)
        for (int px = 0; px < big.w; px++)
            std::copy_n(img.pixel(px / 2, py / 2), 4, big.pixel(px, py));
    image::blit(_screen, big, x, y);
}

} // namespace reluva::legoisland
