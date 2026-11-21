// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Island: the engine's MetaEngine (its name, the releases it recognises,
// making an Engine to run one) and its Engine, which so far shows an empty
// screen (docs/engines/legoisland/PLAN.md).

#include "engines/engine.h"
#include "engines/legoisland/detection.h"

namespace reluva::legoisland {

namespace {

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

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override { return detectGame(id(), dir); }

    std::vector<ImportPlan> planImport(const std::filesystem::path &source) const override {
        const std::vector<GamePart> parts = findGameParts(source);
        if (parts.empty())
            return {};
        ImportPlan plan;
        plan.game = DetectedGame{id(), kGameId, "LEGO Island", "en", "windows"};
        for (const GamePart &part : parts) {
            addPartToImport(plan, part);
            // Which release, from the game's own files where they are.
            if (part.kind == GamePart::kInstall)
                if (const std::vector<DetectedGame> found = detectGame(id(), part.dir); !found.empty())
                    plan.game = found[0];
        }
        noteImport(plan);
        plan.addPatchLabel.clear(); // the game had no patches
        plan.addFolderLabel = "Add CD or installed game...";
        plan.addFolderTooltip = "Copy the game's CD too (or a copy of it), or the game as its installer left it";
        return {plan};
    }

    bool addToImport(ImportPlan &plan, const std::filesystem::path &file, std::string &message) const override {
        const std::vector<GamePart> parts = findGameParts(file);
        if (parts.empty()) {
            message = "not LEGO Island's CD, nor the game as its installer left it";
            return false;
        }
        const size_t before = plan.files.size();
        for (const GamePart &part : parts)
            addPartToImport(plan, part);
        if (plan.files.size() == before) {
            message = "its files are being copied already";
            return false;
        }
        noteImport(plan);
        return true;
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
