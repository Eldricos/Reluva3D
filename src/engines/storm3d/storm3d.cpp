// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D: the engine's MetaEngine (its name, the releases it recognises,
// making an Engine to run one) and its Engine, which shows an empty screen
// so far. Star Trek: Armada is its first game.

#include "engines/engine.h"

namespace reluva::storm3d {

namespace {

// The releases it recognises, as `reluva --hash <files...>` prints them.
// Which files a patch changes isn't known yet, so a release is told by all
// three. See docs/engines/storm3d/games/armada/formats/install.md.
const std::vector<GameVariant> kVariants = {
    // English retail CD, Armada.exe of 10 March 2000 (November 2026).
    {"armada",
     "Star Trek: Armada",
     "en",
     "windows",
     {{"Armada.exe", "ecce0bedbc8159b7fa550319fc866432", 2383917},
      {"assets.zfs", "586ad0c23181ab5ca55dde64b807d8c3", 832202},
      {"objtxt.zfs", "9cf8666631c341618b1db662e0105a7a", 19216}}},
};

// The same game by its files' names alone, for releases not in kVariants
// (patches, other languages). Used only when nothing there matches.
const std::vector<GameVariant> kFallback = {
    {"armada",
     "Star Trek: Armada (unknown release)",
     "",
     "windows",
     {{"Armada.exe", "", -1}, {"assets.zfs", "", -1}, {"objtxt.zfs", "", -1}}},
};

/**
 * The game running: for now an empty screen until the player quits.
 */
class Storm3dEngine : public Engine {
public:
    Storm3dEngine(System &system, const GameTarget &target) : _sys(system), _target(target) {}

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
 * recognises, and making a Storm3dEngine to run one.
 */
class Storm3dMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "storm3d"; }
    const char *name() const override { return "Storm3D"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        std::vector<DetectedGame> found = detectFromTable(id(), kVariants, dir);
        return found.empty() ? detectFromTable(id(), kFallback, dir) : found;
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<Storm3dEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::storm3d

namespace reluva {

const MetaEngine &storm3dMetaEngine() {
    static const storm3d::Storm3dMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
