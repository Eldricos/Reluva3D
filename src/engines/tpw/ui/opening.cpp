// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/ui/opening.h"

#include "common/fs.h"
#include "engines/tpw/game_files.h"
#include "image/image.h"
#include "video/movie.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace reluva::tpw {

namespace {

// How long the splash stays without a key or a click: the recording shows
// it for 0.6 s, probably cut short by a click (guessed).
constexpr uint64_t kSplashMillis = 3000;

// The EA GAMES logo: bf.tgq and bfSim.tgq are the same file, so which the
// game plays doesn't matter.
bool playLogo(System &sys, audio::Mixer *mixer, const std::filesystem::path &install) {
    auto movie = fs::findFile(install, "Data/movies/bfSim.tgq");
    if (!movie)
        movie = fs::findFile(install, "Data/movies/bf.tgq");
    if (!movie)
        return true;
    // Its 320x480 frames are shown at 640x480, as the recording's ring is
    // round (inferred).
    video::MovieOptions options;
    options.screenWidth = kOpeningWidth;
    options.screenHeight = kOpeningHeight;
    options.pictureWidth = kOpeningWidth;
    options.pictureHeight = kOpeningHeight;
    bool quit = false;
    video::playMovie(sys, mixer, *movie, quit, options);
    return !quit;
}

// The Bullfrog splash: Init/640/Splash_<language>.png, the screen's own
// size, shown as it is.
bool showSplash(System &sys, const SearchSet &files) {
    const std::string language = languageOf(files);
    std::vector<uint8_t> data;
    image::Image picture;
    if (language.empty() || files.read("Init/640/Splash_" + language + ".png", data) || image::decode(data, picture))
        return true;
    Surface screen;
    screen.create(kOpeningWidth, kOpeningHeight, PixelFormat::kRGBA32);
    const int w = std::min(picture.w, kOpeningWidth);
    for (int y = 0; y < std::min(picture.h, kOpeningHeight); ++y)
        std::memcpy(screen.row(y), picture.pixel(0, y), size_t(w) * 4);
    sys.updateScreen(screen);
    const uint64_t start = sys.getMillis();
    while (sys.getMillis() - start < kSplashMillis) {
        Event ev;
        while (sys.pollEvent(ev)) {
            if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                return false;
            if ((ev.type == EventType::kKeyDown && !ev.repeat) || ev.type == EventType::kMouseDown)
                return true;
        }
        sys.delayMillis(10);
    }
    return true;
}

} // namespace

bool playOpening(System &sys, audio::Mixer *mixer, const std::filesystem::path &install, const SearchSet &files) {
    return playLogo(sys, mixer, install) && showSplash(sys, files);
}

} // namespace reluva::tpw
