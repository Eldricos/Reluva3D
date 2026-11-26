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
    _first.clear();
    _nearby.clear();
    _frame = ~0u;
    for (const TrackBlock &b : track.blocks) {
        _first.push_back(b.sounds.empty() ? -1 : static_cast<int>(_placed.size()));
        _placed.insert(_placed.end(), b.sounds.begin(), b.sounds.end());
        std::vector<uint16_t> near;
        for (const ExtraBlock &e : b.extra)
            if (e.id == 4)
                for (size_t i = 0; i + 1 < e.data.size() && near.size() < e.count; i += 2)
                    near.push_back(uint16_t(e.data[i] | e.data[i + 1] << 8));
        _nearby.push_back(std::move(near));
    }
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

// The placed sounds as the game plays them (0x434e7d, 0x434030): the
// listener's block's neighbourhood in its order, each block's first sound;
// the volume 2^18 / the distance squared, at most 127; the pan by how far
// to the side. The first loop met plays on its own voice, the rest not;
// none met, that voice stops (0x434f26). A one-shot plays when the
// program's frame count, in eighths of 64, comes round to a number picked
// at random 0 to 7, picked again after each that plays (0x4340d1).
void TrackSounds::update(uint32_t frame, size_t block, const std::array<float, 3> &eye,
                         const std::array<float, 3> &right) {
    if (!_mixer || _data.empty() || frame == _frame || block >= _nearby.size())
        return;
    _frame = frame;
    int loop = -1;
    int loopVolume = 0;
    int loopPan = 0;
    for (uint16_t b : _nearby[block]) {
        if (b >= _first.size() || _first[b] < 0)
            continue;
        const AmbientSound &s = _placed[size_t(_first[b])];
        const float dx = s.position[0] - eye[0];
        const float dy = s.position[1] - eye[1];
        const float dz = s.position[2] - eye[2];
        const float d = roughDistance(dx, dy, dz);
        const int volume = d > 0 ? static_cast<int>(std::min(127.0f, 262144.0f / (d * d))) : 127;
        const float side = d > 0 ? std::clamp((dx * right[0] + dz * right[2]) / d, -1.0f, 1.0f) : 0;
        const int pan = static_cast<int>(side * audio::kMaxPan);
        if (s.random == 1) {
            if (((frame & 63) >> 3) == static_cast<uint32_t>(_oneShotPhase)) {
                if (auto sound = decoded(s.sound))
                    _mixer->play(sound, audio::SoundType::kEffects, volume * audio::kMaxVolume / 127, pan, false);
                _random = _random * 1103515245u + 12345u;
                _oneShotPhase = static_cast<int>(_random >> 16 & 7);
            }
        } else if (loop < 0) {
            loop = s.sound;
            loopVolume = volume * audio::kMaxVolume / 127;
            loopPan = pan;
        }
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
