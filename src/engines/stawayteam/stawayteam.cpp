// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Away Team: the engine's MetaEngine (its name, the releases it
// recognises, making an Engine to run one) and its Engine, which so far
// shows an empty screen.

#include "engines/engine.h"

namespace reluva::stawayteam {

namespace {

// The releases it recognises: files that must be there, with the MD5 of
// their start and their size, as `reluva --hash <files...>` prints them.
// A folder matching every file of a variant is that release.
const std::vector<GameVariant> kVariants = {
    {"stawayteam",
     "Star Trek: Away Team",
     "en",
     "windows",
     {{"Star Trek.exe", "fb8c91668fc25623c2a094f9adab12f2", 2437120},
      {"DATA.dat", "d50706fe22ae0ed084e162ba088eb8e0", 325841399}}},
};

// The same game by its files' names alone, for releases not in kVariants
// (another language, a patch): used only when nothing there matches.
const std::vector<GameVariant> kFallback = {
    {"stawayteam",
     "Star Trek: Away Team (unknown release)",
     "",
     "windows",
     {{"Star Trek.exe", "", -1}, {"DATA.dat", "", -1}}},
};

/**
 * The game running: for now an empty screen until the player quits.
 */
class StawayteamEngine : public Engine {
public:
    StawayteamEngine(System &system, const GameTarget &target) : _sys(system), _target(target) {}

    Error run() override {
        if (Error err = _sys.initGraphics(kWidth, kHeight, PixelFormat::kCLUT8))
            return err;
        _sys.setWindowTitle(("Reluva - " + _target.description).c_str());
        Surface screen;
        screen.create(kWidth, kHeight, PixelFormat::kCLUT8);
        for (bool running = true; running;) {
            Event ev;
            while (_sys.pollEvent(ev))
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher ||
                    (ev.type == EventType::kKeyDown && ev.key == key::kEscape))
                    running = false;
            _sys.updateScreen(screen);
            _sys.delayMillis(1);
        }
        return Error::success();
    }

private:
    static constexpr int kWidth = 640;
    static constexpr int kHeight = 480;
    System &_sys;
    GameTarget _target;
};

/**
 * The engine as the launcher sees it: its name, the releases it
 * recognises, and making a StawayteamEngine to run one.
 */
class StawayteamMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "stawayteam"; }
    const char *name() const override { return "Star Trek: Away Team"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        auto found = detectFromTable(id(), kVariants, dir);
        if (found.empty())
            found = detectFromTable(id(), kFallback, dir);
        return found;
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<StawayteamEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::stawayteam

namespace reluva {

const MetaEngine &stawayteamMetaEngine() {
    static const stawayteam::StawayteamMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
