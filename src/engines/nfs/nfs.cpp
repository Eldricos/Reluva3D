// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Need for Speed engine, starting with Need for Speed III: Hot Pursuit
// (docs/engines/nfs.md).
//
// Everything in this engine must come from the game's data files, observed
// behaviour of the original, public format documentation, or documented
// reverse engineering of a legally owned copy. Never from leaked source
// code. See CONTRIBUTING.md.
//
// For now the engine is a data viewer: what it found (the folders, the
// CD's files, tracks and cars), then the game's pictures, strings and
// fonts (see viewers.h for the keys). Tab switches between them; Esc goes
// back to the launcher.

#include "engines/engine.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/game_files.h"
#include "engines/nfs/games.h"
#include "engines/nfs/viewers.h"

#include <string>
#include <vector>

namespace reluva {

namespace {

using nfs::GameFolders;
using nfs::GameInfo;

constexpr int kScreenW = 640;
constexpr int kScreenH = 480;

class NfsEngine : public Engine {
public:
    NfsEngine(System& system, GameTarget target, const GameInfo& game)
        : sys_(system), target_(std::move(target)), game_(game) {}

    Error run() override {
        const GameFolders folders = nfs::findFolders(target_.path, target_.settings, game_.executable);
        if (nfs::trackFolders(folders.install).empty())
            return Error::fail("No tracks (GameData/Tracks/trkNNN) found in " + folders.install.string());
        nfs::GameFiles files;
        files.scan(folders);
        auto pages = nfs::makePages(game_.name, folders, files);

        if (Error err = sys_.initGraphics(kScreenW, kScreenH, PixelFormat::RGBA32))
            return err;
        screen_.create(kScreenW, kScreenH, PixelFormat::RGBA32);

        size_t page = 0;
        bool dirty = true;
        while (true) {
            Event ev;
            while (sys_.pollEvent(ev)) {
                if (ev.type == EventType::Quit || ev.type == EventType::ReturnToLauncher)
                    return Error::success();
                if (ev.type != EventType::KeyDown)
                    continue;
                if (ev.key == key::Escape)
                    return Error::success();
                if (ev.key == key::Tab)
                    page = (page + ((ev.mods & keymod::Shift) ? pages.size() - 1 : 1)) % pages.size();
                else
                    pages[page]->handleKey(ev.key, ev.mods);
                dirty = true;
            }
            if (dirty) {
                sys_.setWindowTitle(std::string("Reluva - ") + game_.name + " - " + pages[page]->name());
                pages[page]->draw(screen_);
                dirty = false;
            }
            sys_.updateScreen(screen_);
            sys_.delayMillis(10);
        }
    }

private:
    System& sys_;
    GameTarget target_;
    const GameInfo& game_;
    Surface screen_;
};

class NfsMetaEngine : public MetaEngine {
public:
    const char* id() const override { return "nfs"; }
    const char* name() const override { return "Need for Speed"; }

    std::vector<DetectedGame> detect(const std::filesystem::path& dir) const override {
        auto found = detectFromTable(id(), nfs::detectionTable(), dir);
        return found.empty() ? detectFromTable(id(), nfs::fallbackTable(), dir) : found;
    }

    std::unique_ptr<Engine> create(System& system, const GameTarget& target) const override {
        const GameInfo* game = nfs::findGame(target.gameId);
        if (!game)
            game = &nfs::games().front();
        return std::make_unique<NfsEngine>(system, target, *game);
    }
};

} // namespace

const MetaEngine& nfsMetaEngine() {
    static const NfsMetaEngine instance;
    return instance;
}

} // namespace reluva
