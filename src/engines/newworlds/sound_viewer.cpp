// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/viewers.h"

#include "audio/decode.h"
#include "common/fs.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>

namespace reluva::newworlds {

SoundViewer::SoundViewer(System &sys, Surface &screen, const Archives &archives, audio::Mixer *mixer,
                         const std::filesystem::path &gameDir)
    : _sys(sys), _screen(screen), _archives(archives), _mixer(mixer) {
    for (size_t a = 0; a < _archives.size(); ++a) {
        const auto &entries = _archives[a]->entries();
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].tag == cattag::kWave)
                _sounds.push_back({{a, i}, {}, entries[i].name});
    }
    // Find the archives' copies by their size and a hash of their bytes (FNV-1a).
    std::map<std::pair<size_t, uint64_t>, size_t> seen;
    for (size_t s = 0; s < _sounds.size(); ++s) {
        std::vector<uint8_t> data;
        if (read(s, data))
            _firstCopy.push_back(s); // unreadable: shown on its own, with the error
        else {
            uint64_t hash = 14695981039346656037ull;
            for (uint8_t b : data)
                hash = (hash ^ b) * 1099511628211ull;
            _firstCopy.push_back(seen.emplace(std::make_pair(data.size(), hash), s).first->second);
        }
    }
    // Then the game folder's loose sounds (New Worlds' Media/: voice, music),
    // by path; these aren't read until shown.
    std::vector<std::pair<std::string, std::filesystem::path>> loose;
    std::error_code ec;
    if (!gameDir.empty())
        for (std::filesystem::recursive_directory_iterator it(gameDir, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string ext = fs::toLower(it->path().extension().string());
            if (it->is_regular_file(ec) && (ext == ".mp3" || ext == ".wav"))
                loose.push_back({it->path().lexically_relative(gameDir).generic_string(), it->path()});
        }
    std::sort(loose.begin(), loose.end(),
              [](const auto &x, const auto &y) { return fs::toLower(x.first) < fs::toLower(y.first); });
    for (auto &[name, path] : loose) {
        _firstCopy.push_back(_sounds.size());
        _sounds.push_back({{0, 0}, std::move(path), std::move(name)});
    }
    for (size_t s = 0; s < _sounds.size(); ++s)
        if (_showCopies || _firstCopy[s] == s)
            _shownSounds.push_back(s);
}

Error SoundViewer::read(size_t sound, std::vector<uint8_t> &data) const {
    const Item &item = _sounds[sound];
    if (item.file.empty()) {
        CatArchive &cat = *_archives[item.ref.archive];
        return cat.read(cat.entries()[item.ref.entry], data);
    }
    std::ifstream in(item.file, std::ios::binary);
    if (!in)
        return Error::fail("cannot open " + item.file.string());
    data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return Error::success();
}

const std::string &SoundViewer::nameOf(size_t sound) const {
    return _sounds[sound].name;
}

std::string SoundViewer::archiveOf(size_t sound) const {
    const Item &item = _sounds[sound];
    return item.file.empty() ? _archives[item.ref.archive]->path().filename().string() : "the game folder";
}

void SoundViewer::activate() {
    if (!_shown) {
        _shown = true;
        show(0);
        return;
    }
    draw();
    updateTitle();
}

void SoundViewer::deactivate() {
    stop();
}

void SoundViewer::handleKey(uint32_t k) {
    const int n = int(_shownSounds.size());
    if (n == 0)
        return;
    switch (k) {
    case 'f': {
        // List every copy, or only the first of each.
        const size_t sound = _shownSounds[size_t(_current)];
        _showCopies = !_showCopies;
        _shownSounds.clear();
        for (size_t s = 0; s < _sounds.size(); ++s)
            if (_showCopies || _firstCopy[s] == s)
                _shownSounds.push_back(s);
        const auto it = std::find(_shownSounds.begin(), _shownSounds.end(), _firstCopy[sound]);
        const auto at = std::find(_shownSounds.begin(), _shownSounds.end(), sound);
        _current = int((at != _shownSounds.end() ? at : it) - _shownSounds.begin());
        updateTitle();
        break;
    }
    case key::kRight:
        show((_current + 1) % n);
        break;
    case key::kLeft:
        show((_current + n - 1) % n);
        break;
    case key::kPageDown:
        show(std::min(_current + 10, n - 1));
        break;
    case key::kPageUp:
        show(std::max(_current - 10, 0));
        break;
    case key::kHome:
        show(0);
        break;
    case key::kEnd:
        show(n - 1);
        break;
    case key::kSpace:
    case key::kReturn:
        if (playing())
            stop();
        else
            play();
        break;
    case 'a':
        _autoPlay = !_autoPlay;
        updateTitle();
        break;
    default:
        break;
    }
}

void SoundViewer::update(uint64_t) {
    // Redraw while playing, to move the play position, and once after.
    const bool now = playing();
    if (now || _wasPlaying) {
        draw();
        updateTitle();
    }
    _wasPlaying = now;
}

void SoundViewer::show(int index) {
    stop();
    _current = index;
    _status.clear();
    std::vector<uint8_t> data;
    auto sound = std::make_shared<audio::Sound>();
    if (Error err = read(_shownSounds[size_t(index)], data))
        _status = err.message;
    else if (Error err2 = audio::decodeSound(data, *sound))
        _status = err2.message;
    _sound = _status.empty() ? sound : nullptr;
    draw();
    updateTitle();
    if (_autoPlay)
        play();
}

bool SoundViewer::playing() const {
    return _mixer && _mixer->isPlaying(_playing);
}

void SoundViewer::play() {
    if (!_mixer) {
        _status = "no audio output";
        updateTitle();
        return;
    }
    _mixer->stop(_playing);
    _playing = _mixer->play(_sound, audio::SoundType::kEffects);
}

void SoundViewer::stop() {
    if (_mixer)
        _mixer->stop(_playing);
    _playing = audio::kNoSound;
}

void SoundViewer::draw() {
    const int w = _screen.w;
    const int h = _screen.h;
    for (int y = 0; y < h; ++y) {
        uint8_t *row = _screen.row(y);
        for (int x = 0; x < w; ++x, row += 4)
            row[0] = 22, row[1] = 26, row[2] = 38, row[3] = 255;
    }
    std::shared_ptr<const audio::Sound> sound;
    double position = 0;
    bool playing = false;
    sound = _sound;
    playing = this->playing();
    if (playing)
        position = _mixer->position(_playing);
    auto plot = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        if (x < 0 || y < 0 || x >= w || y >= h)
            return;
        uint8_t *p = _screen.row(y) + x * 4;
        p[0] = r, p[1] = g, p[2] = b;
    };
    const int mid = h / 2;
    for (int x = 0; x < w; ++x)
        plot(x, mid, 60, 66, 84);
    if (!sound || !sound->frames())
        return;
    // The waveform: each column's lowest and highest sample (first channel).
    const size_t n = sound->frames();
    const size_t ch = size_t(sound->channels);
    const size_t playFrame = playing ? size_t(position) : n;
    for (int x = 0; x < w; ++x) {
        const size_t from = n * size_t(x) / size_t(w);
        const size_t to = std::max(from + 1, n * size_t(x + 1) / size_t(w));
        int lo = 32767;
        int hi = -32768;
        for (size_t f = from; f < to && f < n; ++f) {
            lo = std::min(lo, int(sound->samples[f * ch]));
            hi = std::max(hi, int(sound->samples[f * ch]));
        }
        const int y0 = mid - hi * (h / 2 - 20) / 32768;
        const int y1 = mid - lo * (h / 2 - 20) / 32768;
        const bool played = from < playFrame;
        for (int y = y0; y <= y1; ++y)
            plot(x, y, played ? 255 : 120, played ? 200 : 170, played ? 80 : 230);
    }
    if (playing) {
        const int x = int(double(w) * position / double(n));
        for (int y = 0; y < h; ++y)
            plot(x, y, 255, 255, 255);
    }
}

void SoundViewer::updateTitle() {
    if (_shownSounds.empty())
        return;
    const size_t current = _shownSounds[size_t(_current)];
    std::string title = std::string("New Worlds sounds ") + (_showCopies ? "[all] " : "") +
                        std::to_string(_current + 1) + "/" + std::to_string(_shownSounds.size()) + ": " +
                        nameOf(current) + " in " + archiveOf(current) + " (";
    std::shared_ptr<const audio::Sound> sound;
    sound = _sound;
    if (!_status.empty() || !sound) {
        title += _status + ")";
    } else {
        char info[96];
        std::snprintf(info, sizeof info, "%d Hz, %s, %.2f s", sound->rate, sound->channels == 1 ? "mono" : "stereo",
                      double(sound->frames()) / sound->rate);
        title += std::string(info) + ", " + audio::formatName(sound->format) + ")";
    }
    // Its copies: how many, and any other names or archives they're in.
    size_t copies = 0;
    std::vector<std::string> names;
    for (size_t s = 0; s < _sounds.size(); ++s) {
        if (_firstCopy[s] != _firstCopy[current])
            continue;
        ++copies;
        std::string where;
        if (nameOf(s) != nameOf(current))
            where = nameOf(s);
        if (archiveOf(s) != archiveOf(current))
            where += (where.empty() ? "in " : " in ") + archiveOf(s);
        if (!where.empty() && std::find(names.begin(), names.end(), where) == names.end())
            names.push_back(where);
    }
    if (copies > 1) {
        title += " [" + std::to_string(copies) + " copies";
        for (size_t i = 0; i < names.size(); ++i)
            title += (i ? ", " : "; also ") + names[i];
        title += "]";
    }
    title += playing() ? " [playing]" : " (Space: play)";
    if (_autoPlay)
        title += " [auto-play]";
    _sys.setWindowTitle(title);
}

} // namespace reluva::newworlds
