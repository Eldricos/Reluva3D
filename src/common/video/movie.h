// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/mixer.h"
#include "common/system.h"
#include "common/types.h"

#include <filesystem>

namespace reluva::video {

// Plays an AVI movie (Indeo 5 picture, PCM sound) on the screen at its own
// size, the picture following the sound, until it ends or a key or click
// skips it. `quit` is set if the window was closed (or the launcher asked
// for) meanwhile. `mixer` may be nullptr: then it plays silently, by the
// clock.
Error playMovie(System& sys, audio::Mixer* mixer, const std::filesystem::path& file, bool& quit);

} // namespace reluva::video
