// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/sound.h"
#include "common/types.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace reluva::netimmerse {

// A .DDS sound (Audio.pac, Ambient.pac, Locale_*.pac): an 18-byte Windows
// WAVEFORMATEX, then 16-bit PCM samples. See docs/engines/netimmerse/formats/pac.md.
Error decodeDds(const std::vector<uint8_t>& data, audio::Sound& out);

// A language text file (Locale_*.pac's .TXT): each "==key==" line is
// followed by its text, up to the next key; '#' lines are comments. The
// text's lines are joined with '\n', blank lines at its end left out.
std::vector<std::pair<std::string, std::string>> parseLocaleText(std::string_view text);

} // namespace reluva::netimmerse
