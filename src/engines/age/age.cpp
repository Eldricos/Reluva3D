// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Angel Game Engine: Angel Studios' games, starting with Midtown Madness 2.
//
// Sources: the file formats are documented in docs/formats/age/, worked
// out from the game's own data (see docs/engines/age.md). The program is
// encrypted by its copy protection, so nothing comes from it. See
// CONTRIBUTING.md: never use leaked or unlicensed code.
//
// For now the engine is a data viewer (see viewers.h for the keys): it
// mounts the game's archives and shows its files.

#include "engines/age/filesystem.h"
#include "engines/age/games.h"
#include "engines/age/viewers.h"
#include "engines/engine.h"

#include <memory>

namespace reluva {

namespace {

using namespace age;

constexpr int kScreenW = 640;
constexpr int kScreenH = 480;

class AgeEngine : public Engine {
public:
    AgeEngine(System& system, GameTarget target, const GameInfo& game)
        : sys_(system), target_(std::move(target)), game_(game) {}

    Error run() override {
        std::vector<std::string> warnings;
        // Damaged archives don't stop the viewer; the Files page lists what
        // went wrong.
        if (Error err = mountGame(files_, target_.path, warnings))
            return err;

        if (Error err = sys_.initGraphics(kScreenW, kScreenH, PixelFormat::RGBA32))
            return err;
        screen_.create(kScreenW, kScreenH, PixelFormat::RGBA32);

        ViewerContext ctx{sys_, screen_, files_, game_, warnings};
        std::vector<std::unique_ptr<Page>> pages;
        pages.push_back(std::make_unique<FileViewer>(ctx));
        size_t page = 0;
        while (page < pages.size() && pages[page]->count() == 0)
            ++page;
        if (page == pages.size())
            return Error::fail("No files found in " + target_.path.string());
        pages[page]->activate();

        while (true) {
            Event ev;
            while (sys_.pollEvent(ev)) {
                if (ev.type == EventType::Quit || ev.type == EventType::ReturnToLauncher)
                    return Error::success();
                const bool pageKey = pages[page]->takesKey(ev);
                if (ev.type == EventType::KeyDown && ev.key == key::Escape && !pageKey)
                    return Error::success();
                if (ev.type == EventType::KeyDown && ev.key == key::Tab && !pageKey) {
                    size_t next = page;
                    do
                        next = (next + 1) % pages.size();
                    while (pages[next]->count() == 0);
                    page = next;
                    pages[page]->activate();
                    continue;
                }
                pages[page]->handleEvent(ev);
            }
            sys_.updateScreen(screen_);
            sys_.delayMillis(10);
        }
    }

private:
    System& sys_;
    GameTarget target_;
    const GameInfo& game_;
    SearchSet files_;
    Surface screen_;
};

class AgeMetaEngine : public MetaEngine {
public:
    const char* id() const override { return "age"; }
    const char* name() const override { return "Angel Game Engine"; }

    std::vector<DetectedGame> detect(const std::filesystem::path& dir) const override {
        auto found = detectFromTable(id(), detectionTable(), dir);
        return found.empty() ? detectFromTable(id(), fallbackTable(), dir) : found;
    }

    std::unique_ptr<Engine> create(System& system, const GameTarget& target) const override {
        const GameInfo* game = findGame(target.gameId);
        if (!game)
            game = &games().front();
        return std::make_unique<AgeEngine>(system, target, *game);
    }
};

} // namespace

const MetaEngine& ageMetaEngine() {
    static const AgeMetaEngine instance;
    return instance;
}

} // namespace reluva
