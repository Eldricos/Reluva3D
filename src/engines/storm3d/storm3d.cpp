// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D: the engine's MetaEngine (its name, the releases it recognises,
// making an Engine to run one) and its Engine. Started from the template
// (cmake/NewEngine.cmake): so far it recognises nothing and shows an empty
// screen.

#include "engines/engine.h"

namespace reluva::storm3d {

namespace {

// The releases it recognises: files that must be there, with the MD5 of
// their start and their size, as `reluva --hash <files...>` prints them.
// A folder matching every file of a variant is that release.
const std::vector<GameVariant> kVariants = {
    // {"<game id>", "<Game name> (<release>)", "en", "windows",
    //  {{"<file>", "<md5>", <size>}, {"<other file>", "", -1}}},
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
        return detectFromTable(id(), kVariants, dir);
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
