// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Theme Park World: the engine's MetaEngine (its name, the releases it recognises,
// making an Engine to run one) and its Engine. So far it recognises
// SimCoaster and shows an empty screen.

#include "engines/engine.h"
#include "engines/tpw/games.h"

namespace reluva::tpw {

namespace {

/**
 * The game running: for now an empty screen until the player quits.
 */
class TpwEngine : public Engine {
public:
    TpwEngine(System &system, const GameTarget &target) : _sys(system), _target(target) {}

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
 * recognises, and making a TpwEngine to run one.
 */
class TpwMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "tpw"; }
    const char *name() const override { return "Theme Park World"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        auto found = detectFromTable(id(), detectionTable(), dir);
        return found.empty() ? detectFromTable(id(), fallbackTable(), dir) : found;
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<TpwEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::tpw

namespace reluva {

const MetaEngine &tpwMetaEngine() {
    static const tpw::TpwMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
