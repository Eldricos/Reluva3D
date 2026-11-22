// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The viewer's pages of sound: the .wav sounds, and the .snd sound classes
// with the sound each event plays.

#include "audio/wav.h"
#include "common/events.h"
#include "common/fs.h"
#include "engines/sith/formats/sound_class.h"
#include "engines/sith/pages/pages.h"
#include "graphics/font.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

namespace reluva::sith {

namespace {

constexpr Color kBackground = {40, 40, 48};
constexpr Color kWave = {120, 200, 140};
constexpr Color kPlayhead = {255, 220, 90};
constexpr int kGap = 8;

// The sound's waveform in a box, its channels mixed, and where it's
// playing (a fraction of its length; below 0 when it isn't).
void drawWave(Surface &screen, const audio::Sound &sound, int x, int y, int w, int h, double playing) {
    fillRect(screen, x, y, w, h, {24, 24, 30});
    fillRect(screen, x, y + h / 2, w, 1, {70, 70, 80});
    const size_t frames = sound.frames();
    if (frames == 0)
        return;
    for (int col = 0; col < w; ++col) {
        const size_t from = frames * size_t(col) / size_t(w);
        const size_t to = std::max(from + 1, frames * size_t(col + 1) / size_t(w));
        int lo = 0;
        int hi = 0;
        for (size_t f = from; f < to && f < frames; ++f) {
            int v = 0;
            for (int c = 0; c < sound.channels; ++c)
                v += sound.samples[f * size_t(sound.channels) + size_t(c)];
            v /= sound.channels;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        const int top = y + h / 2 - hi * (h / 2) / 32768;
        const int bottom = y + h / 2 - lo * (h / 2) / 32768;
        fillRect(screen, x + col, top, 1, std::max(1, bottom - top), kWave);
    }
    if (playing >= 0)
        fillRect(screen, x + int(playing * w), y, 2, h, kPlayhead);
}

// Finds a .wav by its file name in the archives' sound and voice folders.
const GobArchive *findWav(const GameFiles &files, const std::string &name, std::string &path) {
    for (const char *folder : {"sound/", "voice/"}) {
        path = folder + name;
        if (const GobArchive *gob = files.find(path))
            return gob;
    }
    return nullptr;
}

/** What both pages share: a sound decoded, played and shown. */
class SoundPlayer {
public:
    explicit SoundPlayer(const ViewerData &data) : _data(data) {}

    /** Decodes a sound to show and play; false, with why, if it can't. */
    bool load(const GobArchive &gob, const std::string &name, std::string &why) {
        stop();
        _sound.reset();
        std::vector<uint8_t> bytes;
        auto sound = std::make_shared<audio::Sound>();
        Error err = gob.read(name, bytes);
        if (!err)
            err = audio::decodeWav(bytes, *sound);
        if (err) {
            why = err.message;
            return false;
        }
        _sound = std::move(sound);
        return true;
    }

    void play() {
        stop();
        if (_data.mixer && _sound)
            _handle = _data.mixer->play(_sound, audio::SoundType::kEffects);
    }

    void stop() {
        if (_data.mixer && _handle != audio::kNoSound)
            _data.mixer->stop(_handle);
        _handle = audio::kNoSound;
    }

    bool playing() const { return _data.mixer && _handle != audio::kNoSound && _data.mixer->isPlaying(_handle); }

    /** How far it has played, 0 to 1, or -1 when it isn't playing. */
    double position() const {
        if (!playing() || !_sound || _sound->frames() == 0)
            return -1;
        return std::min(1.0, _data.mixer->position(_handle) * _sound->rate / double(_sound->frames()));
    }

    const audio::Sound *sound() const { return _sound.get(); }

    /** The sound's kind and length, for the status line. */
    std::string describe() const {
        if (!_sound)
            return "";
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%d Hz %s, %.2f s", _sound->rate, _sound->channels == 1 ? "mono" : "stereo",
                      double(_sound->frames()) / _sound->rate);
        return buf;
    }

private:
    const ViewerData &_data;
    std::shared_ptr<audio::Sound> _sound;
    audio::SoundHandle _handle = audio::kNoSound;
};

/** The .wav sounds: each one's waveform; Space plays it, A plays each as it's picked. */
class SoundPage : public Page {
public:
    explicit SoundPage(const ViewerData &data) : _data(data), _files(filesEnding(data.files, ".wav")), _player(data) {}

    const char *name() const override { return "Sounds"; }
    size_t count() const override { return _files.size(); }
    const char *keys() const override { return "Left/Right, Space: play or stop, A: play each as it's picked"; }

    bool handleKey(uint32_t pressed) override {
        if (stepIndex(pressed, _files.size(), _index)) {
            _loaded = false;
            if (_auto)
                _playWhenLoaded = true;
            return true;
        }
        if (pressed == ' ') {
            if (_player.playing())
                _player.stop();
            else
                _player.play();
            return true;
        }
        if (pressed == 'a') {
            _auto = !_auto;
            return true;
        }
        return false;
    }

    bool update(uint64_t /*millis*/) override {
        // While it plays, the playhead moves; once more when it stops.
        const bool now = _player.playing();
        const bool changed = now || now != _wasPlaying;
        _wasPlaying = now;
        return changed;
    }

    void leave() override { _player.stop(); }

    void draw(Surface &screen) override {
        fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
        const FileRef &file = _files[_index];
        std::string why;
        if (!_loaded) {
            _loaded = true;
            _error = _player.load(*file.first, file.second, why) ? "" : why;
            if (_playWhenLoaded && _error.empty())
                _player.play();
            _playWhenLoaded = false;
        }
        std::string line = std::to_string(_index + 1) + "/" + std::to_string(_files.size()) + "  " +
                           file.first->describe() + ": " + file.second;
        if (!_error.empty()) {
            drawStatus(screen, line + "  " + _error);
            return;
        }
        drawWave(screen, *_player.sound(), kGap, kGap, kViewerW - 2 * kGap, 300, _player.position());
        line += "  " + _player.describe();
        if (!_data.mixer)
            line += ", no sound output";
        else if (_auto)
            line += ", playing each";
        drawStatus(screen, line);
    }

private:
    const ViewerData &_data;
    std::vector<FileRef> _files;
    SoundPlayer _player;
    size_t _index = 0;
    bool _loaded = false;
    bool _auto = false;
    bool _playWhenLoaded = false;
    bool _wasPlaying = false;
    std::string _error;
};

/**
 * The .snd sound classes: each event, its sound and its numbers; Up/Down
 * pick an event, Space plays its sound.
 */
class SoundClassPage : public Page {
public:
    explicit SoundClassPage(const ViewerData &data)
        : _data(data), _files(filesEnding(data.files, ".snd")), _player(data) {}

    const char *name() const override { return "Sound classes"; }
    size_t count() const override { return _files.size(); }
    const char *keys() const override { return "Left/Right: class, Up/Down: event, Space: play or stop"; }

    bool handleKey(uint32_t pressed) override {
        if (pressed == key::kLeft || pressed == key::kRight || pressed == key::kHome || pressed == key::kEnd ||
            pressed == key::kPageUp || pressed == key::kPageDown) {
            stepIndex(pressed, _files.size(), _index);
            _loaded = false;
            _entry = 0;
            _player.stop();
            return true;
        }
        if (pressed == key::kDown || pressed == key::kUp) {
            loadClass();
            if (!_snd.entries.empty())
                _entry = (_entry + (pressed == key::kDown ? 1 : _snd.entries.size() - 1)) % _snd.entries.size();
            _player.stop();
            return true;
        }
        if (pressed == ' ') {
            if (_player.playing())
                _player.stop();
            else
                play();
            return true;
        }
        return false;
    }

    bool update(uint64_t /*millis*/) override {
        const bool now = _player.playing();
        const bool changed = now || now != _wasPlaying;
        _wasPlaying = now;
        return changed;
    }

    void leave() override { _player.stop(); }

    void draw(Surface &screen) override {
        fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
        const FileRef &file = _files[_index];
        std::string line = std::to_string(_index + 1) + "/" + std::to_string(_files.size()) + "  " +
                           file.first->describe() + ": " + file.second;
        loadClass();
        if (!_error.empty()) {
            drawStatus(screen, line + "  " + _error);
            return;
        }
        font::drawText(screen, kGap, kGap, "event              sound                      flags     radii       volume",
                       {150, 200, 255});
        for (size_t i = 0; i < _snd.entries.size() && kGap + 14 + int(i) * 12 < 380; ++i) {
            const SoundClass::Entry &e = _snd.entries[i];
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%-18s %-26s %-9s %-11s %s", e.event.c_str(), e.wav.c_str(),
                          e.flags ? ("0x" + hex(*e.flags)).c_str() : "-",
                          e.minRadius && e.maxRadius ? (fixed(*e.minRadius) + "-" + fixed(*e.maxRadius)).c_str() : "-",
                          e.maxVolume ? fixed(*e.maxVolume).c_str() : "-");
            const int y = kGap + 14 + int(i) * 12;
            if (i == _entry)
                fillRect(screen, kGap - 2, y - 1, kViewerW - 2 * kGap + 4, 11, {70, 70, 100});
            font::drawText(screen, kGap, y, buf, {230, 230, 230});
        }
        line += "  " + std::to_string(_snd.entries.size()) + " events";
        if (!_snd.entries.empty()) {
            const SoundClass::Entry &e = _snd.entries[_entry];
            std::string path;
            const GobArchive *gob = findWav(_data.files, e.wav, path);
            if (gob && _shown != path) {
                std::string why;
                _player.load(*gob, path, why);
                _shown = path;
            }
            if (gob && _player.sound()) {
                drawWave(screen, *_player.sound(), kGap, 390, kViewerW - 2 * kGap, 180, _player.position());
                line += "; " + e.event + ": " + path + ", " + _player.describe();
            } else
                line += "; " + e.event + ": " + e.wav + " isn't in the archives";
        }
        drawStatus(screen, line);
    }

private:
    static std::string hex(uint32_t v) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%x", v);
        return buf;
    }

    static std::string fixed(float v) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.2f", double(v));
        return buf;
    }

    void loadClass() {
        if (_loaded)
            return;
        _loaded = true;
        const FileRef &file = _files[_index];
        std::vector<uint8_t> bytes;
        Error err = file.first->read(file.second, bytes);
        if (!err)
            err = parseSoundClass(bytes, _snd);
        _error = err ? err.message : "";
    }

    void play() {
        loadClass();
        if (_snd.entries.empty())
            return;
        std::string path;
        if (const GobArchive *gob = findWav(_data.files, _snd.entries[_entry].wav, path)) {
            std::string why;
            if (_shown != path && _player.load(*gob, path, why))
                _shown = path;
            _player.play();
        }
    }

    const ViewerData &_data;
    std::vector<FileRef> _files;
    SoundPlayer _player;
    SoundClass _snd;
    size_t _index = 0;
    size_t _entry = 0;
    bool _loaded = false;
    bool _wasPlaying = false;
    std::string _error;
    std::string _shown;
};

} // namespace

std::unique_ptr<Page> makeSoundPage(const ViewerData &data) {
    return std::make_unique<SoundPage>(data);
}

std::unique_ptr<Page> makeSoundClassPage(const ViewerData &data) {
    return std::make_unique<SoundClassPage>(data);
}

} // namespace reluva::sith
