// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Island: the engine's MetaEngine (its name, the releases it recognises,
// making an Engine to run one) and its Engine, which so far shows an empty
// screen (docs/engines/legoisland/PLAN.md).

#include "engines/engine.h"

namespace reluva::legoisland {

namespace {

// The releases it recognises, by the game's code and its world database
// as the installer copies them (docs/engines/legoisland/systems/game-files.md).
const std::vector<GameVariant> kVariants = {
    {"legoisland",
     "LEGO Island (English 1.0)",
     "en",
     "windows",
     {{"LEGO1.DLL", "b7ca2f80366a427797cc3f820c48a0f1", 1134080},
      {"lego/data/WORLD.WDB", "a2de77ebfa5164ec949f19cd5c7bb58c", 9944860}}},
};

// The same game by its files' names alone, for releases not in kVariants
// (1.1, other languages): used only when nothing there matches.
const std::vector<GameVariant> kFallback = {
    {"legoisland",
     "LEGO Island (unknown release)",
     "",
     "windows",
     {{"LEGO1.DLL", "", -1}, {"lego/data/WORLD.WDB", "", -1}}},
};

/**
 * The game running: for now an empty screen until the player quits.
 */
class LegoislandEngine : public Engine {
public:
    LegoislandEngine(System &system, const GameTarget &target) : _sys(system), _target(target) {}

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
 * recognises, and making a LegoislandEngine to run one.
 */
class LegoislandMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "legoisland"; }
    const char *name() const override { return "LEGO Island"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        std::vector<DetectedGame> found = detectFromTable(id(), kVariants, dir);
        return found.empty() ? detectFromTable(id(), kFallback, dir) : found;
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<LegoislandEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::legoisland

namespace reluva {

const MetaEngine &legoislandMetaEngine() {
    static const legoisland::LegoislandMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
