// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Sith: the engine's MetaEngine (its name, the releases it recognises,
// making an Engine to run one) and its Engine, which so far shows an empty
// screen.

#include "engines/engine.h"
#include "engines/sith/games.h"
#include "engines/sith/import.h"

namespace reluva::sith {

namespace {

/**
 * The game running: for now an empty screen until the player quits.
 */
class SithEngine : public Engine {
public:
    SithEngine(System &system, const GameTarget &target) : _sys(system), _target(target) {}

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
 * recognises, importing it from its CDs, and making a SithEngine to
 * run one.
 */
class SithMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "sith"; }
    const char *name() const override { return "Sith"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override { return detectGames(dir); }

    std::vector<ImportPlan> planImport(const std::filesystem::path &source) const override {
        return sith::planImport(source);
    }

    bool addToImport(ImportPlan &plan, const std::filesystem::path &folder, std::string &message) const override {
        return sith::addToImport(plan, folder, message);
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<SithEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::sith

namespace reluva {

const MetaEngine &sithMetaEngine() {
    static const sith::SithMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
