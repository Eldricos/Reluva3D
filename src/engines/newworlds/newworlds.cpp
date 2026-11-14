// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: New Worlds engine (clean-room reimplementation).
//
// Everything in this engine must come from the game's data files, observed
// behaviour of the original, or documented reverse engineering of a legally
// owned copy. Never from leaked source code. See CONTRIBUTING.md.
//
// The game can't be played yet: what the engine reads is shown by its data
// viewer (pages/), in reluva-viewer.

#include "engines/engine.h"

#include <memory>

namespace reluva {

namespace {

// Detection table. Built with `reluva --hash`: each entry needs the size and
// the MD5 of the first kDetectionHashBytes bytes.
//
// Detection keys on glblgame.cat: patches tend to replace the executable but
// not the large archives, so the executable is only required to exist.
const std::vector<GameVariant> kVariants = {
    {"newworlds",
     "Star Trek: New Worlds",
     "en",
     "windows",
     {{"glblgame.cat", "9b5c322aef388f8e58e72b81bd9a91ad", 6883984}, {"STNW.exe", "", -1}}},
};

class NewWorldsEngine : public Engine {
public:
    explicit NewWorldsEngine(GameTarget target) : _target(std::move(target)) {}

    Error run() override {
        return Error::fail(
            "Star Trek: New Worlds can't be played yet, only looked at in reluva-viewer (reluva-viewer " +
            _target.path.string() + ")");
    }

private:
    GameTarget _target;
};

class NewWorldsMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "newworlds"; }
    const char *name() const override { return "New Worlds"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        return detectFromTable(id(), kVariants, dir);
    }

    std::unique_ptr<Engine> create(System & /*system*/, const GameTarget &target) const override {
        return std::make_unique<NewWorldsEngine>(target);
    }
};

} // namespace

const MetaEngine &newWorldsMetaEngine() {
    static const NewWorldsMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
