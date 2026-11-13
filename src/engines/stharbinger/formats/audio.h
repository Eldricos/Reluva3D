// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "audio/stream.h"

#include <memory>
#include <vector>

namespace reluva::stharbinger {

// Every .RAW file and dialogue clip plays at 22050 Hz, mono.
const int kSampleRate = 22050;

// The game's sample volumes, on the sound library's 0..127 scale. Everything
// plays at the default 127 except arcade music (0x26784) and speech (0x2B24A).
const int kVolumeFull = 127;
const int kVolumeArcadeMusic = 64;
const int kVolumeSpeech = 75;

// A game volume (0..127) on the mixer's scale (0..audio::kMaxVolume).
inline int mixerVolume(int gameVolume) { return gameVolume * audio::kMaxVolume / kVolumeFull; }

enum RawFormat { kRawU8, kRawS8, kRawS16LE, kRawS16BE };

// The files have no header and mix formats, so the format is worked out from the
// data: the high byte of 16-bit samples is far more predictable than the low byte.
RawFormat detectRawFormat(const std::vector<uint8_t>& data);

// Makes a stream for raw sound data (`loops` 0 = forever).
std::unique_ptr<audio::Stream> makeRawStream(std::vector<uint8_t> data, int loops = 1);

} // namespace reluva::stharbinger
