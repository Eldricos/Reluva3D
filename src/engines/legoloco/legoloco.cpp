// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Loco: the engine's MetaEngine (its name, the releases it recognises,
// making an Engine to run one) and its Engine, which so far shows an empty
// screen.

#include "engines/engine.h"

namespace reluva::legoloco {

namespace {

// The releases it recognises, as `reluva --hash <files...>` prints them.
// The CD and an install have the same layout, and the installer copies the
// same files whichever of the CD's nine languages is chosen (it writes the
// choice into Exe/LEGO.INI), so the one entry stands for all of them.
const std::vector<GameVariant> kVariants = {
    {"legoloco",
     "LEGO Loco (1.0.18.2)",
     "en",
     "windows",
     {{"Exe/Loco.exe", "695dd274531337b1b600fd55f8f51482", 1117696},
      {"art-res/resource.RFH", "e7f73852368fbd30ca42b6d527b25242", 85647}}},
};

/**
 * The game running: for now an empty screen until the player quits.
 */
class LegolocoEngine : public Engine {
public:
    LegolocoEngine(System &system, const GameTarget &target) : _sys(system), _target(target) {}

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
 * recognises, and making a LegolocoEngine to run one.
 */
class LegolocoMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "legoloco"; }
    const char *name() const override { return "LEGO Loco"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        return detectFromTable(id(), kVariants, dir);
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<LegolocoEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::legoloco

namespace reluva {

const MetaEngine &legolocoMetaEngine() {
    static const legoloco::LegolocoMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
