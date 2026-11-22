// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Sounds page: the WAV files in Assets/Sound's zips.

#include "audio/wav.h"
#include "common/fs.h"
#include "engines/sfc/pages/viewers.h"
#include "graphics/font.h"

#include <algorithm>
#include <cstdio>

namespace reluva::sfc {

namespace {

constexpr int kListTop = 2 * font::kLineHeight + 12;

// Effects first: the zips as the install has them.
const char *const kZips[] = {"Assets/Sound/SFCSounds.zip", "Assets/Sound/SFCVoice.zip", "Assets/Sound/sfcmusic.zip"};

std::string seconds(double s) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f s", s);
    return buf;
}

} // namespace

SoundPage::SoundPage(ViewerContext &ctx) : Page(ctx) {
    for (const char *name : kZips) {
        const auto path = fs::findFile(ctx.root, name);
        if (!path)
            continue;
        Zip zip;
        zip.name = path->filename().string();
        zip.music = fs::toLower(zip.name) == "sfcmusic.zip";
        zip.archive = std::make_unique<ZipArchive>();
        if (zip.archive->open(*path))
            continue;
        for (const std::string &n : zip.archive->list())
            if (std::filesystem::path(fs::toLower(n)).extension() == ".wav")
                zip.sounds.push_back(n);
        if (!zip.sounds.empty())
            _zips.push_back(std::move(zip));
    }
}

size_t SoundPage::count() const {
    size_t n = 0;
    for (const Zip &z : _zips)
        n += z.sounds.size();
    return n;
}

void SoundPage::select(size_t index) {
    stop();
    _current = index;
    _sound.reset();
    _error.clear();
    if (_zips.empty())
        return;
    std::vector<uint8_t> data;
    auto sound = std::make_shared<audio::Sound>();
    const Zip &zip = _zips[_zip];
    if (Error err = zip.archive->read(zip.sounds[_current], data))
        _error = err.message;
    else if (Error e = audio::decodeWav(data, *sound))
        _error = e.message;
    else
        _sound = std::move(sound);
    if (_autoPlay)
        play();
    redraw();
}

void SoundPage::play() {
    stop();
    if (_ctx.mixer && _sound && !_sound->samples.empty())
        _playing = _ctx.mixer->play(_sound, _zips[_zip].music ? audio::SoundType::kMusic : audio::SoundType::kEffects);
}

void SoundPage::stop() {
    if (_ctx.mixer && _playing != audio::kNoSound)
        _ctx.mixer->stop(_playing);
    _playing = audio::kNoSound;
}

void SoundPage::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown || _zips.empty())
        return;
    const size_t n = _zips[_zip].sounds.size();
    switch (ev.key) {
    case key::kUp:
        select(_current ? _current - 1 : 0);
        break;
    case key::kDown:
        select(std::min(n - 1, _current + 1));
        break;
    case key::kPageUp:
        select(_current > 20 ? _current - 20 : 0);
        break;
    case key::kPageDown:
        select(std::min(n - 1, _current + 20));
        break;
    case key::kHome:
        select(0);
        break;
    case key::kEnd:
        select(n - 1);
        break;
    case key::kLeft:
    case key::kRight:
        _zip = (_zip + (ev.key == key::kRight ? 1 : _zips.size() - 1)) % _zips.size();
        select(0);
        break;
    case key::kSpace:
    case key::kReturn:
        if (_ctx.mixer && _playing != audio::kNoSound && _ctx.mixer->isPlaying(_playing))
            stop();
        else
            play();
        redraw();
        break;
    case 'a':
        _autoPlay = !_autoPlay;
        redraw();
        break;
    default:
        break;
    }
}

void SoundPage::update(uint64_t nowMs) {
    // While a sound plays, its progress a few times a second; once it ends,
    // once more to show it stopped.
    if (_playing == audio::kNoSound || nowMs - _lastDrawMs < 100)
        return;
    _lastDrawMs = nowMs;
    if (_ctx.mixer && !_ctx.mixer->isPlaying(_playing))
        _playing = audio::kNoSound;
    redraw();
}

void SoundPage::redraw() {
    clear({20, 22, 28});
    if (_zips.empty())
        return;
    const Zip &zip = _zips[_zip];
    std::string info = zip.name + ": " + zip.sounds[_current] + "  (" + std::to_string(_current + 1) + " of " +
                       std::to_string(zip.sounds.size()) + ")  ";
    if (!_error.empty())
        info += _error;
    else if (_sound) {
        info += std::to_string(_sound->rate) + " Hz, " + (_sound->channels == 2 ? "stereo" : "mono") + ", " +
                seconds(_sound->rate ? double(_sound->frames()) / _sound->rate : 0);
        if (_ctx.mixer && _playing != audio::kNoSound && _ctx.mixer->isPlaying(_playing))
            info += "  playing " + seconds(_ctx.mixer->position(_playing) / _sound->rate);
    }
    text(4, 2, info, {150, 200, 255});
    text(4, 2 + font::kLineHeight,
         std::string("Up/Down, PgUp/PgDn: sounds  Left/Right: zip  Space: play/stop  A: autoplay ") +
             (_autoPlay ? "on" : "off") + (_ctx.mixer ? "" : "  (no audio device)"),
         {170, 170, 170});
    setTitle(zip.sounds[_current]);
    const int rows = (kScreenH - kListTop) / font::kLineHeight;
    size_t first = _current > size_t(rows / 2) ? _current - size_t(rows / 2) : 0;
    first = std::min(first, zip.sounds.size() > size_t(rows) ? zip.sounds.size() - size_t(rows) : 0);
    for (int r = 0; r < rows && first + size_t(r) < zip.sounds.size(); ++r) {
        const size_t i = first + size_t(r);
        const int y = kListTop + r * font::kLineHeight;
        if (i == _current)
            fill(2, y - 1, kScreenW - 4, font::kLineHeight, {50, 60, 90});
        text(8, y, zip.sounds[i], i == _current ? Color{255, 220, 120} : Color{220, 220, 220});
    }
}

} // namespace reluva::sfc
