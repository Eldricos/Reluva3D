// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Need for Speed engine's data viewer, as reluva-viewer runs it: what
// the engine found (the folders, the CD's files, tracks and cars), the
// game itself (the Play page), then the tracks and cars in 3D, the game's
// pictures, strings and fonts, its sounds, music and movies (see
// viewers.h for the keys). Tab switches between them; Esc goes back to
// reluva-viewer's list.

#include "engines/nfs/game_files.h"
#include "engines/nfs/games.h"
#include "engines/nfs/pages/viewers.h"
#include "engines/nfs/ui/page.h"
#include "engines/viewer.h"

#include <memory>

namespace reluva {

namespace {

class NfsViewer : public Engine {
public:
    NfsViewer(System &system, GameTarget target, const nfs::GameInfo &game)
        : _sys(system), _target(std::move(target)), _game(game) {}

    Error run() override {
        auto makePages = [this](const nfs::GameFolders &folders, audio::Mixer *mixer) {
            _files.scan(folders);
            return nfs::makePages(_game.name, folders, _files, mixer, _target.savePath, _target.settings);
        };
        return nfs::runPages(_sys, _target, _game, makePages, true);
    }

private:
    System &_sys;
    GameTarget _target;
    const nfs::GameInfo &_game;
    nfs::GameFiles _files; // the pages read it while they're shown
};

class NfsMetaViewer : public MetaViewer {
public:
    const char *engineId() const override { return "nfs"; }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        const nfs::GameInfo *game = nfs::findGame(target.gameId);
        if (!game)
            game = &nfs::games().front();
        return std::make_unique<NfsViewer>(system, target, *game);
    }
};

} // namespace

const MetaViewer &nfsMetaViewer() {
    static const NfsMetaViewer kInstance;
    return kInstance;
}

} // namespace reluva
