// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Generations: the engine's MetaEngine (its name, the releases it recognises,
// making an Engine to run one) and its Engine. Started from the template
// (cmake/NewEngine.cmake): so far it recognises nothing and shows an empty
// screen.

#include "engines/engine.h"

namespace reluva::stgenerations {

namespace {

// The releases it recognises: files that must be there, with the MD5 of
// their start and their size, as `reluva --hash <files...>` prints them.
// A folder matching every file of a variant is that release.
const std::vector<GameVariant> kVariants = {
    // {"<game id>", "<Game name> (<release>)", "en", "windows",
    //  {{"<file>", "<md5>", <size>}, {"<other file>", "", -1}}},
};

class StgenerationsEngine : public Engine {
public:
    StgenerationsEngine(System& system, const GameTarget& target) : sys_(system), target_(target) {}

    Error run() override {
        if (Error err = sys_.initGraphics(kWidth, kHeight, PixelFormat::CLUT8))
            return err;
        sys_.setWindowTitle(("Reluva - " + target_.description).c_str());
        Surface screen;
        screen.create(kWidth, kHeight, PixelFormat::CLUT8);
        for (bool running = true; running;) {
            Event ev;
            while (sys_.pollEvent(ev))
                if (ev.type == EventType::Quit || ev.type == EventType::ReturnToLauncher ||
                    (ev.type == EventType::KeyDown && ev.key == key::Escape))
                    running = false;
            sys_.updateScreen(screen);
            sys_.delayMillis(1);
        }
        return Error::success();
    }

private:
    static constexpr int kWidth = 640, kHeight = 480;
    System& sys_;
    GameTarget target_;
};

class StgenerationsMetaEngine : public MetaEngine {
public:
    const char* id() const override { return "stgenerations"; }
    const char* name() const override { return "Star Trek: Generations"; }

    std::vector<DetectedGame> detect(const std::filesystem::path& dir) const override {
        return detectFromTable(id(), kVariants, dir);
    }

    std::unique_ptr<Engine> create(System& system, const GameTarget& target) const override {
        return std::make_unique<StgenerationsEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::stgenerations

namespace reluva {

const MetaEngine& stgenerationsMetaEngine() {
    static const stgenerations::StgenerationsMetaEngine instance;
    return instance;
}

} // namespace reluva
