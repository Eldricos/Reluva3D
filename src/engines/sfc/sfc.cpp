// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Starfleet Command: the engine's MetaEngine (its name, the releases it recognises,
// making an Engine to run one) and its Engine, which so far shows an empty
// screen.

#include "engines/engine.h"

namespace reluva::sfc {

namespace {

// The releases it recognises: files that must be there, with the MD5 of
// their start and their size, as `reluva --hash <files...>` prints them.
// A folder matching every file of a variant is that release.
// The 2.0.3.6 patch rewrites both files, so a patched install is an unknown
// release until its own are hashed.
const std::vector<GameVariant> kVariants = {
    {"sfc2",
     "Star Trek: Starfleet Command II (1.0)",
     "en",
     "windows",
     {{"StarFleet2.exe", "e78f2d2c9c9c51f2077bb5c10cbaf617", 4149248},
      {"Assets/Sprites/sprites.q3", "7eaeae9272f5d82ac6523a3d6191a45a", 78253026}}},
};

// The same game by its files' names alone, for releases not in kVariants:
// used only when nothing there matches.
const std::vector<GameVariant> kFallback = {
    {"sfc2",
     "Star Trek: Starfleet Command II (unknown release)",
     "",
     "windows",
     {{"StarFleet2.exe", "", -1}, {"Assets/Sprites/sprites.q3", "", -1}}},
};

/**
 * The game running: for now an empty screen until the player quits.
 */
class SfcEngine : public Engine {
public:
    SfcEngine(System &system, const GameTarget &target) : _sys(system), _target(target) {}

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
 * recognises, and making a SfcEngine to run one.
 */
class SfcMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "sfc"; }
    const char *name() const override { return "Starfleet Command"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        auto found = detectFromTable(id(), kVariants, dir);
        if (found.empty())
            found = detectFromTable(id(), kFallback, dir);
        return found;
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<SfcEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::sfc

namespace reluva {

const MetaEngine &sfcMetaEngine() {
    static const sfc::SfcMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
