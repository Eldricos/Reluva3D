// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/ui/main_menu.h"

#include "engines/tpw/ui/front_end.h"
#include "engines/tpw/ui/front_end_audio.h"
#include "engines/tpw/ui/front_end_effects.h"
#include "engines/tpw/ui/front_end_overlay.h"
#include "engines/tpw/ui/globe.h"
#include "engines/tpw/ui/personnel_file.h"

#include <algorithm>
#include <cstring>

namespace reluva::tpw {

namespace {

constexpr uint32_t kFrameMillis = 40;

// The globe turning, the advisor speaking over it, until the play button
// is pressed or the Personnel File's time comes. `start` is when the
// camera's path ended, as GlobeTurning takes it, moved back if the skip
// button is pressed while the camera is still coming down; `pathMillis`
// is how long the path is, the overlay's clock running on from it; the
// audio kept up with it. False if the window was closed meanwhile.
bool watchGlobe(System &sys, const Globe &globe, uint64_t &start, uint32_t pathMillis, FrontEndOverlay &overlay,
                FrontEndAudio &sound) {
    Surface screen;
    screen.create(640, 480, PixelFormat::kRGBA32);
    soft3d::Rasterizer rasterizer;
    for (;;) {
        const uint64_t now = sys.getMillis();
        auto clock = [&] { return pathMillis + uint32_t(now - std::min(now, start)); };
        Event ev;
        while (sys.pollEvent(ev)) {
            if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                return false;
            if (!overlay.handle(ev))
                continue;
            if (FrontEndOverlay::buttonAt(clock()) == FrontEndOverlay::Button::kPlay)
                return true;
            start = now - std::min<uint64_t>(now, FrontEndOverlay::kGlobeMillis - pathMillis);
        }
        const uint32_t ms = clock();
        if (ms >= FrontEndOverlay::kPersonnelFileMillis)
            return true;
        sound.update(ms);
        GlobeTurning(globe, start).draw(screen, rasterizer, now);
        overlay.draw(screen, ms);
        sys.updateScreen(screen);
        sys.delayMillis(kFrameMillis);
    }
}

} // namespace

int runFrontEnd(System &sys, const SearchSet &files, const std::filesystem::path &saveFolder, audio::Mixer *mixer,
                PlayerFile *chosen) {
    FrontEndScene scene;
    Globe globe;
    const bool drawn = !scene.load(files) && !globe.load(scene);
    FrontEndEffects effects;
    const bool withEffects = drawn && !effects.load(files, scene);
    effects.setMixer(mixer);
    FrontEndOverlay overlay;
    const bool withOverlay = drawn && !overlay.load(files);
    // Without its files, silence; the music and speech go with the
    // overlay, which times them.
    FrontEndAudio sound;
    if (withOverlay && !sound.load(files))
        sound.setMixer(mixer);
    struct AudioOff { // stops them however the front end ends
        FrontEndAudio &sound;
        ~AudioOff() {
            sound.stop();
            sound.setMixer(nullptr);
        }
    } audioOff{sound};
    const auto keepUp = [&](uint32_t ms) {
        if (withOverlay)
            sound.update(ms);
    };
    const PathEnd end =
        drawn ? playFrontEnd(sys, scene, withEffects ? &effects : nullptr, withOverlay ? &overlay : nullptr, keepUp)
              : PathEnd::kSkipped;
    effects.setMixer(nullptr);
    if (mixer && end == PathEnd::kSkipped)
        mixer->stopType(audio::SoundType::kEffects);
    if (end == PathEnd::kQuit)
        return -1;
    // The recording's music turns to the globe's as the path ends.
    sound.setMusicValue(FrontEndAudio::kGlobeValue, true);
    if (end == PathEnd::kSkipped)
        sound.stopSpeech(false);

    // Skipped, the camera is already down over the globe.
    const uint32_t pathMillis = uint32_t(scene.frames() * 1000 / Animation::kFramesPerSecond);
    const uint32_t ahead = end == PathEnd::kSkipped ? FrontEndOverlay::kGlobeMillis - pathMillis : 0;
    uint64_t start = sys.getMillis() - std::min<uint64_t>(sys.getMillis(), ahead);
    if (withOverlay && !watchGlobe(sys, globe, start, pathMillis, overlay, sound))
        return -1;
    // The portrait goes as the Personnel File opens, and his words with it.
    sound.stopSpeech(true);
    const GlobeTurning turning(globe, start);
    soft3d::Rasterizer rasterizer;
    const Backdrop backdrop = [&](Surface &screen) {
        sound.update(FrontEndOverlay::kPersonnelFileMillis);
        if (drawn)
            turning.draw(screen, rasterizer, sys.getMillis());
        else
            std::fill(screen.pixels.begin(), screen.pixels.end(), uint8_t(0));
    };
    PlayerFile player;
    if (!choosePlayer(sys, files, saveFolder, backdrop, player))
        return -1;
    if (chosen)
        *chosen = player;

    std::array<bool, Globe::kWorlds> open{};
    for (size_t i = 0; i < open.size(); ++i)
        open[i] = player.worlds[i].open;
    if (std::none_of(open.begin(), open.end(), [](bool o) { return o; }))
        open[0] = true;
    if (!drawn)
        return int(std::find(open.begin(), open.end(), true) - open.begin());
    return chooseWorld(sys, files, turning, open, [&] { sound.update(FrontEndOverlay::kPersonnelFileMillis); });
}

} // namespace reluva::tpw
