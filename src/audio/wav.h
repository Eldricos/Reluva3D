// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/sound.h"
#include "common/types.h"

#include <cstdint>
#include <vector>

namespace reluva::audio {

// A RIFF WAVE file: 8- and 16-bit PCM and IMA ADPCM (format 0x11, as in
// Star Trek: New Worlds; see docs/engines/newworlds/wav.md).
Error decodeWav(const std::vector<uint8_t> &data, Sound &out);

/**
 * Microsoft IMA ADPCM blocks (WAV format 0x11) without a WAV file around
 * them, as Birth of the Federation's .snd archives keep them.
 * @param blockAlign Bytes a block, for every channel.
 * @param frameLimit Frames to keep at most; 0 keeps them all.
 */
Error decodeImaAdpcm(const uint8_t *data, size_t size, int channels, int rate, size_t blockAlign, size_t frameLimit,
                     Sound &out);

// A 16-bit PCM WAV file of the sound, for checking it elsewhere.
std::vector<uint8_t> encodePcmWav(const Sound &sound);

} // namespace reluva::audio
