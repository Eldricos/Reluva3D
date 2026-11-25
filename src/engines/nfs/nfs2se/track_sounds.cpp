// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/nfs2se/track_sounds.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>

namespace reluva::nfs::nfs2se {

namespace {

// About the distance, as the game reckons it for its placed sounds
// (0x42df30): the largest of each pair plus a quarter of the other.
float roughDistance(float dx, float dy, float dz) {
    const float ax = std::abs(dx);
    const float az = std::abs(dz);
    const float across = std::max(ax, az) + std::min(ax, az) / 4;
    const float ay = std::abs(dy);
    return std::max(across, ay) + std::min(across, ay) / 4;
}

} // namespace

// The track's own bank of placed sounds: TRAMBnn.BNK for trNN.
void TrackSounds::load(const std::string &stem, const Track &track) {
    stop();
    _data.clear();
    _bank = {};
    _sounds.clear();
    _placed.clear();
    _frame = ~0u;
    for (const TrackBlock &b : track.blocks)
        _placed.insert(_placed.end(), b.sounds.begin(), b.sounds.end());
    if (stem.size() < 4)
        return;
    const std::string name = "audio/sfx/pc/tramb" + stem.substr(2) + ".bnk";
    for (const FileRef &f : _files.withExtensions({".bnk"})) {
        if (fs::toLower(f.name).find(name) == std::string::npos)
            continue;
        if (_files.read(f, _data) || parseSoundBank(_data, _bank)) {
            _data.clear();
            _bank = {};
        }
        break;
    }
}

void TrackSounds::stop() {
    if (_mixer && _loop != audio::kNoSound)
        _mixer->stop(_loop);
    _loop = audio::kNoSound;
    _loopSound = -1;
}

std::shared_ptr<audio::Sound> TrackSounds::decoded(int slot) {
    if (slot < 0 || slot >= static_cast<int>(_bank.slots.size()) || !_bank.slots[size_t(slot)])
        return nullptr;
    auto &sound = _sounds[slot];
    if (!sound) {
        sound = std::make_shared<audio::Sound>();
        if (decodeBankSound(_data, *_bank.slots[size_t(slot)], *sound))
            sound = std::make_shared<audio::Sound>();
    }
    return sound->samples.empty() ? nullptr : sound;
}

// The placed sounds as the game plays them (0x434030): the volume 2^18 /
// the distance squared, at most 127; the pan by how far to the side. One
// loop at a time, on its own voice, the game's the first it comes to, this
// class's the nearest; a one-shot when the program's frame count, in
// eighths of 64, comes round to a number picked at random 0 to 7, picked
// again after it plays.
void TrackSounds::update(uint32_t frame, const std::array<float, 3> &eye, const std::array<float, 3> &right) {
    if (!_mixer || _data.empty() || frame == _frame)
        return;
    _frame = frame;
    const bool oneShotDue = ((frame & 63) >> 3) == static_cast<uint32_t>(_oneShotPhase);
    bool oneShotPlayed = false;
    int loop = -1;
    float loopDistance = 0;
    int loopVolume = 0;
    int loopPan = 0;
    for (const AmbientSound &s : _placed) {
        const float dx = s.position[0] - eye[0];
        const float dy = s.position[1] - eye[1];
        const float dz = s.position[2] - eye[2];
        const float d = roughDistance(dx, dy, dz);
        const int volume = d > 0 ? static_cast<int>(std::min(127.0f, 262144.0f / (d * d))) : 127;
        if (volume < 1)
            continue;
        const float side = d > 0 ? std::clamp((dx * right[0] + dz * right[2]) / d, -1.0f, 1.0f) : 0;
        const int pan = static_cast<int>(side * audio::kMaxPan);
        if (s.random == 1) {
            if (oneShotDue && !oneShotPlayed)
                if (auto sound = decoded(s.sound)) {
                    _mixer->play(sound, audio::SoundType::kEffects, volume * audio::kMaxVolume / 127, pan, false);
                    oneShotPlayed = true;
                }
        } else if (loop < 0 || d < loopDistance) {
            loop = s.sound;
            loopDistance = d;
            loopVolume = volume * audio::kMaxVolume / 127;
            loopPan = pan;
        }
    }
    if (oneShotPlayed) {
        _random = _random * 1103515245u + 12345u;
        _oneShotPhase = static_cast<int>(_random >> 16 & 7);
    }
    if (loop != _loopSound) {
        stop();
        if (auto sound = decoded(loop)) {
            _loop = _mixer->play(sound, audio::SoundType::kEffects, loopVolume, loopPan, true);
            _loopSound = loop;
        }
    } else if (_loop != audio::kNoSound) {
        _mixer->setVolume(_loop, loopVolume);
        _mixer->setPan(_loop, loopPan);
    }
}

} // namespace reluva::nfs::nfs2se
