// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/movies.h"

#include "common/fs.h"
#include "video/movie_player.h"

#include <fstream>
#include <iterator>
#include <vector>

namespace reluva::stawayteam {

void clearScreen(Surface &screen) {
    for (size_t i = 0; i + 3 < screen.pixels.size(); i += 4) {
        screen.pixels[i] = screen.pixels[i + 1] = screen.pixels[i + 2] = 0;
        screen.pixels[i + 3] = 255;
    }
}

std::optional<std::filesystem::path> movieFile(const std::filesystem::path &install, const std::string &name) {
    return fs::findFile(install, "DATA/Movies/" + name + ".bik");
}

bool playMovie(System &sys, Surface &screen, audio::Mixer *mixer, const std::filesystem::path &file) {
    std::ifstream in(file, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    video::MoviePlayer player(mixer);
    // One that won't open is passed over, as the game passes over one it
    // can't open (0x4d4e14).
    if (player.open(std::move(data), sys.getMillis()))
        return true;
    clearScreen(screen);
    for (;;) {
        Event ev;
        while (sys.pollEvent(ev)) {
            if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                return false;
            // Any key pressed stops it: the game looks for a key newly down
            // in its table of keys (0x4d4e83, 0x4e8de0), which the mouse's
            // buttons don't go into (0x5fa9ea).
            if (ev.type == EventType::kKeyDown)
                return true;
        }
        const uint64_t now = sys.getMillis();
        if (player.update(now)) {
            const image::Image &frame = player.frame();
            clearScreen(screen);
            image::blit(screen, frame, (screen.w - frame.w) / 2, (screen.h - frame.h) / 2);
        }
        if (player.done(now))
            return true;
        sys.updateScreen(screen);
        sys.delayMillis(5);
    }
}

} // namespace reluva::stawayteam
