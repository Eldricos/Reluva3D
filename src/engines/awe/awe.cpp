// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The AWE Productions engine (clean-room reimplementation): Agatha
// Christie: Murder on the Orient Express. See docs/engines/awe.md.
//
// Everything in this engine must come from the game's data files, observed
// behaviour of the original, or documented reverse engineering of a legally
// owned copy. Never from leaked source code. See CONTRIBUTING.md.

#include "engines/engine.h"

namespace reluva {

namespace {

// Detection table. Built with `reluva --hash`: each entry needs the size and
// the MD5 of the first kDetectionHashBytes bytes. Detection keys on the
// item list: a patch is likelier to replace the executable than the text.
const std::vector<GameVariant> kVariants = {
    {"motoe", "Agatha Christie: Murder on the Orient Express", "en", "windows",
     {{"motoe.exe", "cdfcba4e6c8c7cee637959bc78bd46e7", 684032},
      {"Text/itemlist.txt", "a2a0d2cd331a6fb36ffa79dced219aa5", 37328}}},
};
// Any other release: the same files, whatever their contents.
const std::vector<GameVariant> kFallback = {
    {"motoe", "Agatha Christie: Murder on the Orient Express (unknown release)", "", "windows",
     {{"motoe.exe", "", -1}, {"Text/itemlist.txt", "", -1}, {"granny2.dll", "", -1}}},
};

class AweEngine : public Engine {
public:
    AweEngine(System& system, GameTarget target) : sys_(system), target_(std::move(target)) {}

    Error run() override { return Error::fail("The Orient Express engine has nothing to show yet"); }

private:
    System& sys_;
    GameTarget target_;
};

class AweMetaEngine : public MetaEngine {
public:
    const char* id() const override { return "awe"; }
    const char* name() const override { return "AWE (Agatha Christie)"; }

    std::vector<DetectedGame> detect(const std::filesystem::path& dir) const override {
        std::vector<DetectedGame> found = detectFromTable(id(), kVariants, dir);
        if (found.empty())
            found = detectFromTable(id(), kFallback, dir);
        return found;
    }

    std::unique_ptr<Engine> create(System& system, const GameTarget& target) const override {
        return std::make_unique<AweEngine>(system, target);
    }
};

} // namespace

const MetaEngine& aweMetaEngine() {
    static const AweMetaEngine instance;
    return instance;
}

} // namespace reluva
