// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The viewer's pages of the game's sequence: the episodes' steps, and the
// cut scenes on the CDs.

#include "audio/stream.h"
#include "common/events.h"
#include "common/fs.h"
#include "common/stream.h"
#include "engines/sith/formats/episode.h"
#include "engines/sith/pages/pages.h"
#include "graphics/font.h"
#include "image/image.h"
#include "video/smacker.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

namespace reluva::sith {

namespace {

constexpr Color kBackground = {40, 40, 48};
constexpr int kGap = 8;

// Every episode in the archives, by the archive it's in.
std::vector<std::pair<std::string, Episode>> readEpisodes(const GameFiles &files) {
    std::vector<std::pair<std::string, Episode>> out;
    for (const auto &gob : files.archives()) {
        std::vector<uint8_t> bytes;
        Episode episode;
        if (gob->has("episode.jk") && !gob->read("episode.jk", bytes) && !parseEpisode(bytes, episode))
            out.emplace_back(gob->describe(), std::move(episode));
    }
    return out;
}

/** The episodes: each one's steps, a line each. Up/Down scroll. */
class EpisodePage : public Page {
public:
    explicit EpisodePage(const ViewerData &data) : _episodes(readEpisodes(data.files)) {}

    const char *name() const override { return "Episodes"; }
    size_t count() const override { return _episodes.size(); }
    const char *keys() const override { return "Left/Right: episode, Up/Down: scroll"; }

    bool handleKey(uint32_t pressed) override {
        if (pressed == key::kLeft || pressed == key::kRight) {
            stepIndex(pressed, _episodes.size(), _index);
            _top = 0;
        } else if (pressed == key::kDown)
            ++_top;
        else if (pressed == key::kUp)
            _top = _top > 0 ? _top - 1 : 0;
        else
            return false;
        return true;
    }

    void draw(Surface &screen) override {
        fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
        const auto &[archive, episode] = _episodes[_index];
        _top = std::min(_top, episode.steps.empty() ? 0 : episode.steps.size() - 1);
        font::drawText(screen, kGap, kGap, "step  CD level  kind    file                  light dark   goto",
                       {150, 200, 255});
        for (size_t i = 0; i < kLines && _top + i < episode.steps.size(); ++i) {
            const Episode::Step &s = episode.steps[_top + i];
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%4d  %2d %5d  %-7s %-21s %5d %4d  %4d %4d", s.line, s.cd, s.level,
                          s.kind.c_str(), s.file.c_str(), s.lightPower, s.darkPower, s.gotoA, s.gotoB);
            const Color c = s.kind == "LEVEL" ? Color{230, 230, 230} : Color{230, 200, 120};
            font::drawText(screen, kGap, kGap + 14 + int(i) * 12, buf, c);
        }
        drawStatus(screen, std::to_string(_index + 1) + "/" + std::to_string(_episodes.size()) + "  " + archive +
                               ": \"" + episode.title + "\", type " + std::to_string(episode.type) + ", " +
                               std::to_string(episode.steps.size()) + " steps");
    }

private:
    static constexpr size_t kLines = (kViewerH - 40) / 12;

    std::vector<std::pair<std::string, Episode>> _episodes;
    size_t _index = 0;
    size_t _top = 0;
};

/**
 * The cut scenes on the CDs found: the picture, played with its sound.
 * Space plays or pauses, `.` steps a frame while paused.
 */
class CutScenePage : public Page {
public:
    explicit CutScenePage(const ViewerData &data)
        : _data(data), _cuts(findCutScenes(data.folders)), _episodes(readEpisodes(data.files)) {}

    const char *name() const override { return "Cut scenes"; }
    size_t count() const override { return _cuts.size(); }
    const char *keys() const override { return "Left/Right, Space: play or pause, .: next frame"; }

    bool handleKey(uint32_t pressed) override {
        if (stepIndex(pressed, _cuts.size(), _index)) {
            stop();
            _decoder.reset();
            return true;
        }
        if (pressed == ' ') {
            if (_playing)
                stop();
            else
                _startPending = true;
            return true;
        }
        if (pressed == '.' && !_decoder && !open(_cuts[_index]))
            return true;
        if (pressed == '.' && !_playing && !_decoder->endOfVideo()) {
            nextFrame();
            return true;
        }
        return false;
    }

    bool update(uint64_t millis) override {
        _now = millis;
        if (_startPending && _decoder) {
            _startPending = false;
            start();
        }
        if (!_playing || !_decoder)
            return false;
        // The picture follows the clock: every frame due by now is decoded.
        const uint64_t due = (millis - _startMillis) * 1000 / _decoder->frameMicros();
        bool changed = false;
        while (_decoder->currentFrame() < int(due) + _startFrame - 1 && !_decoder->endOfVideo()) {
            nextFrame();
            changed = true;
        }
        if (_decoder->endOfVideo()) {
            stop();
            changed = true;
        }
        return changed;
    }

    void leave() override { stop(); }

    void draw(Surface &screen) override {
        fillRect(screen, 0, 0, screen.w, screen.h, {0, 0, 0});
        const CutScene &cut = _cuts[_index];
        std::string line = std::to_string(_index + 1) + "/" + std::to_string(_cuts.size()) + "  CD " +
                           std::to_string(cut.cd) + ": " + cut.name;
        if (!_decoder && !open(cut)) {
            drawStatus(screen, line + "  " + _error);
            return;
        }
        const Surface &frame = _decoder->surface();
        const uint8_t *pal = _decoder->palette();
        image::Image img;
        img.w = frame.w;
        img.h = frame.h;
        img.rgba.resize(size_t(img.w) * img.h * 4);
        for (int y = 0; y < frame.h; ++y)
            for (int x = 0; x < frame.w; ++x) {
                const uint8_t i = frame.row(y)[x];
                uint8_t *p = img.pixel(x, y);
                p[0] = pal[i * 3];
                p[1] = pal[i * 3 + 1];
                p[2] = pal[i * 3 + 2];
                p[3] = 255;
            }
        image::blit(screen, img, (kViewerW - img.w) / 2, (kViewerH - 16 - img.h) / 2);

        char info[160];
        std::snprintf(info, sizeof(info), "  %dx%d, frame %d/%d, %.1f/%.1f s%s", _decoder->width(), _decoder->height(),
                      _decoder->currentFrame() + 1, _decoder->frameCount(),
                      (_decoder->currentFrame() + 1) * double(_decoder->frameMicros()) / 1e6,
                      _decoder->frameCount() * double(_decoder->frameMicros()) / 1e6,
                      _playing      ? ", playing"
                      : _data.mixer ? ""
                                    : ", no sound output");
        line += info;
        // Where the episodes play it.
        for (const auto &[archive, episode] : _episodes)
            for (const Episode::Step &s : episode.steps)
                if (fs::toLower(s.file) == fs::toLower(cut.name))
                    line += ", step " + std::to_string(s.line) + " (level " + std::to_string(s.level) + ")";
        drawStatus(screen, line);
    }

private:
    bool open(const CutScene &cut) {
        std::vector<uint8_t> bytes;
        if (!readFile(cut.path, bytes)) {
            _error = "can't read " + cut.path.string();
            return false;
        }
        auto decoder = std::make_unique<smacker::Decoder>();
        if (!decoder->load(std::make_unique<MemoryReadStream>(std::move(bytes))) || !decoder->decodeNextFrame()) {
            _error = "not a Smacker video, or its first frame doesn't decode";
            return false;
        }
        decoder->takeAudio(); // played only once it plays
        _decoder = std::move(decoder);
        _error.clear();
        return true;
    }

    void nextFrame() {
        if (!_decoder->decodeNextFrame()) {
            stop();
            return;
        }
        std::vector<int16_t> samples = _decoder->takeAudio();
        if (_audio)
            _audio->queue(samples);
    }

    void start() {
        if (_decoder->endOfVideo())
            return;
        _playing = true;
        _startMillis = _now;
        _startFrame = _decoder->currentFrame() + 1;
        if (_data.mixer && _decoder->hasAudio()) {
            _audio = std::make_shared<audio::QueuingStream>(_decoder->audioRate(), _decoder->audioStereo());
            _audioHandle = _data.mixer->play(_audio, audio::SoundType::kMusic);
        }
    }

    void stop() {
        _playing = false;
        _startPending = false;
        if (_data.mixer && _audioHandle != audio::kNoSound)
            _data.mixer->stop(_audioHandle);
        _audioHandle = audio::kNoSound;
        _audio.reset();
    }

    const ViewerData &_data;
    std::vector<CutScene> _cuts;
    std::vector<std::pair<std::string, Episode>> _episodes;
    size_t _index = 0;
    std::unique_ptr<smacker::Decoder> _decoder;
    std::string _error;
    bool _playing = false;
    bool _startPending = false;
    uint64_t _now = 0;
    uint64_t _startMillis = 0;
    int _startFrame = 0;
    std::shared_ptr<audio::QueuingStream> _audio;
    audio::SoundHandle _audioHandle = audio::kNoSound;
};

} // namespace

std::unique_ptr<Page> makeEpisodePage(const ViewerData &data) {
    return std::make_unique<EpisodePage>(data);
}

std::unique_ptr<Page> makeCutScenePage(const ViewerData &data) {
    return std::make_unique<CutScenePage>(data);
}

} // namespace reluva::sith
