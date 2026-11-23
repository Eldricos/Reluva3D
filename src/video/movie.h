// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/system.h"
#include "common/types.h"

#include <filesystem>
#include <memory>

namespace reluva::video {

/** How a movie is shown, beyond the defaults. */
struct MovieOptions {
    // The screen it plays on, the picture in its middle on black; 0 for the
    // picture's own size.
    int screenWidth = 0;
    int screenHeight = 0;
    // Its sound, when the game plays it beside the movie rather than in it:
    // used when the movie has none.
    std::shared_ptr<const audio::Sound> soundtrack;
    // The picture stretched to this size (each pixel repeated); 0 for its
    // own. SimCoaster shows its 320x480 logo at 640x480, say.
    int pictureWidth = 0;
    int pictureHeight = 0;
};

/**
 * Plays a movie until it ends or a key or click skips it, the picture
 * following the sound: an AVI (Indeo 5 or Cinepak picture, PCM sound) or an
 * EA movie (`.tgq`: TQI picture, the sound of its EA stream). `quit` is set
 * if the window was closed (or the launcher asked for) meanwhile. `mixer`
 * may be nullptr: then it plays silently, by the clock, as it does a
 * soundtrack that isn't 16-bit mono or stereo.
 * @return An error if the file can't be read or is neither.
 */
Error playMovie(System &sys, audio::Mixer *mixer, const std::filesystem::path &file, bool &quit,
                const MovieOptions &options = {});

} // namespace reluva::video
