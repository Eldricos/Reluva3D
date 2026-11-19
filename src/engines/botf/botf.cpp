// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Birth of the Federation: the engine's MetaEngine (its name, the
// releases it recognises, making an Engine to run one) and its Engine,
// which so far shows an empty screen.

#include "engines/engine.h"

#include <memory>
#include <vector>

namespace reluva::botf {

namespace {

// Detection keys on the data files: trek.exe is only SafeDisc's loader
// (docs/engines/botf/README.md), and a patched or replaced one shouldn't
// stop a copy being recognised.
const std::vector<GameVariant> kVariants = {
    {"botf",
     "Star Trek: Birth of the Federation",
     "en",
     "windows",
     {{"stbof.res", "2e888c1eaefa901da9e2dbd6229798a5", 79901501},
      {"english.snd", "806bf768303d60745c792fae58ec506f", 39509316}}},
    // stbof.res as on the CD's GERMAN folder; german.snd isn't hashed yet.
    {"botf",
     "Star Trek: Birth of the Federation",
     "de",
     "windows",
     {{"stbof.res", "21df64c3479690c70f50bfc0bed3792b", 79385299}, {"german.snd", "", -1}}},
};

// The same game by its files' names alone, for releases not in kVariants:
// used only when nothing there matches.
const std::vector<GameVariant> kFallback = {
    {"botf",
     "Star Trek: Birth of the Federation (unknown release)",
     "",
     "windows",
     {{"stbof.res", "", -1}, {"sfx.snd", "", -1}}},
};

/**
 * The game running: for now an empty screen until the player quits.
 */
class BotfEngine : public Engine {
public:
    BotfEngine(System &system, GameTarget target) : _sys(system), _target(std::move(target)) {}

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
    static constexpr int kWidth = 800; // the game's own resolution
    static constexpr int kHeight = 600;
    System &_sys;
    GameTarget _target;
};

/**
 * The engine as the launcher sees it: its name, the releases it
 * recognises, and making a BotfEngine to run one.
 */
class BotfMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "botf"; }
    const char *name() const override { return "Star Trek: Birth of the Federation"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        auto found = detectFromTable(id(), kVariants, dir);
        if (found.empty())
            found = detectFromTable(id(), kFallback, dir);
        return found;
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<BotfEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::botf

namespace reluva {

const MetaEngine &botfMetaEngine() {
    static const botf::BotfMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
