// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/sound.h"
#include "common/types.h"

#include <cstdint>
#include <vector>

namespace reluva::audio {

/**
 * An Ogg Vorbis file, decoded whole with Xiph's libvorbis. Old files from
 * before Vorbis 1.0 (floor 0) are decoded too.
 * Fails if the data isn't Ogg Vorbis, or has more than two channels.
 */
Error decodeVorbis(const std::vector<uint8_t> &data, Sound &out);

} // namespace reluva::audio
