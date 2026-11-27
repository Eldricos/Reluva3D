// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/cut_scene.h"

#include "common/fs.h"
#include "common/stream.h"

namespace reluva::sith {

bool CutSceneVideo::open(const std::filesystem::path &file, std::string &error) {
    _smacker.reset();
    _smush.reset();
    std::vector<uint8_t> bytes;
    if (!readFile(file, bytes)) {
        error = "can't read " + file.string();
        return false;
    }
    if (fs::toLower(file.extension().string()) == ".san") {
        auto smush = std::make_unique<smush::Decoder>();
        if (!smush->load(std::move(bytes)) || !smush->decodeNextFrame()) {
            error = "not a SMUSH video, or its first frame doesn't decode";
            return false;
        }
        _smush = std::move(smush);
        return true;
    }
    auto smacker = std::make_unique<smacker::Decoder>();
    if (!smacker->load(std::make_unique<MemoryReadStream>(std::move(bytes))) || !smacker->decodeNextFrame()) {
        error = "not a Smacker video, or its first frame doesn't decode";
        return false;
    }
    _smacker = std::move(smacker);
    return true;
}

int CutSceneVideo::width() const {
    return _smush ? _smush->width() : _smacker->width();
}
int CutSceneVideo::height() const {
    return _smush ? _smush->height() : _smacker->height();
}
int CutSceneVideo::frameCount() const {
    return _smush ? _smush->frameCount() : _smacker->frameCount();
}
int CutSceneVideo::currentFrame() const {
    return _smush ? _smush->currentFrame() : _smacker->currentFrame();
}
uint32_t CutSceneVideo::frameMicros() const {
    return _smush ? _smush->frameMicros() : _smacker->frameMicros();
}
bool CutSceneVideo::endOfVideo() const {
    return _smush ? _smush->endOfVideo() : _smacker->endOfVideo();
}
bool CutSceneVideo::decodeNextFrame() {
    return _smush ? _smush->decodeNextFrame() : _smacker->decodeNextFrame();
}
const Surface &CutSceneVideo::surface() const {
    return _smush ? _smush->surface() : _smacker->surface();
}
const uint8_t *CutSceneVideo::palette() const {
    return _smush ? _smush->palette() : _smacker->palette();
}
bool CutSceneVideo::hasAudio() const {
    return _smush ? _smush->hasAudio() : _smacker->hasAudio();
}
int CutSceneVideo::audioRate() const {
    return _smush ? _smush->audioRate() : _smacker->audioRate();
}
bool CutSceneVideo::audioStereo() const {
    return _smush ? _smush->audioStereo() : _smacker->audioStereo();
}
std::vector<int16_t> CutSceneVideo::takeAudio() {
    return _smush ? _smush->takeAudio() : _smacker->takeAudio();
}

std::vector<uint8_t> CutSceneVideo::subtitleData() const {
    if (!_smush)
        return _smacker->trackData(1);
    const int n = _smush->subtitle();
    if (n < 0)
        return {};
    return {uint8_t(n), uint8_t(n >> 8), uint8_t(n >> 16), uint8_t(n >> 24)};
}

} // namespace reluva::sith
