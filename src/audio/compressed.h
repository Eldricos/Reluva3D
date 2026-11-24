// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/sound.h"
#include "common/types.h"

#include <cstdint>
#include <vector>

namespace reluva::audio {

/**
 * An Ogg Vorbis file, decoded with stb_vorbis (third_party/stb) to 16-bit
 * samples. Fails if it isn't one, or can't be decoded.
 */
Error decodeVorbis(const std::vector<uint8_t> &data, Sound &out);

/**
 * A FLAC file, decoded with dr_flac (third_party/dr_flac) to 16-bit
 * samples, deeper ones cut down. Fails if it isn't one, or can't be
 * decoded.
 */
Error decodeFlac(const std::vector<uint8_t> &data, Sound &out);

} // namespace reluva::audio
