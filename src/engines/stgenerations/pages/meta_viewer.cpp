// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Generations' data viewer for reluva-viewer: ResourceViewer, run
// with the game's settings as the game is.

#include "engines/stgenerations/pages/resource_viewer.h"
#include "engines/stgenerations/stgenerations.h"
#include "engines/viewer.h"

namespace reluva::stgenerations {

namespace {

class StgenerationsViewerEngine : public Engine {
public:
    StgenerationsViewerEngine(System &system, const GameTarget &target) : _sys(system), _target(target) {}

    Error run() override {
        return runGame(_sys, _target, [](Platform *platform, Options options) {
            return std::make_unique<ResourceViewer>(platform, std::move(options));
        });
    }

private:
    System &_sys;
    GameTarget _target;
};

class StgenerationsMetaViewer : public MetaViewer {
public:
    const char *engineId() const override { return "stgenerations"; }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<StgenerationsViewerEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::stgenerations

namespace reluva {

const MetaViewer &stgenerationsMetaViewer() {
    static const stgenerations::StgenerationsMetaViewer kInstance;
    return kInstance;
}

} // namespace reluva
