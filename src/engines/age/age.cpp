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
// mounts the game's archives, runs its races, drives its cars round its
// cities and shows its models, textures and files.

#include "audio/mixer.h"
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
    AgeEngine(System &system, GameTarget target, const GameInfo &game)
        : _sys(system), _target(std::move(target)), _game(game) {}

    Error run() override {
        std::vector<std::string> warnings;
        // Damaged archives don't stop the viewer; the Files page lists what
        // went wrong.
        if (Error err = mountGame(_files, _target.path, warnings))
            return err;

        if (Error err = _sys.initGraphics(kScreenW, kScreenH, PixelFormat::kRgbA32))
            return err;
        _screen.create(kScreenW, kScreenH, PixelFormat::kRgbA32);

        // One mixer for every page's sounds; without audio output, none.
        audio::Mixer mixer(_sys.audioRate());
        const bool audioOut = !audio::startMixerOutput(_sys, mixer);
        struct AudioOff { // stops the output before the mixer goes
            System &sys;
            ~AudioOff() { sys.stopAudio(); }
        } audioOff{_sys};

        ViewerContext ctx{_sys, _screen, _files, _game, warnings, audioOut ? &mixer : nullptr, _target.savePath};
        std::vector<std::unique_ptr<Page>> pages;
        pages.push_back(std::make_unique<RaceViewer>(ctx));
        pages.push_back(std::make_unique<DriveViewer>(ctx));
        pages.push_back(std::make_unique<WorldViewer>(ctx));
        pages.push_back(std::make_unique<ModelViewer>(ctx));
        pages.push_back(std::make_unique<TextureViewer>(ctx));
        pages.push_back(std::make_unique<FileViewer>(ctx));
        size_t page = 0;
        while (page < pages.size() && pages[page]->count() == 0)
            ++page;
        if (page == pages.size())
            return Error::fail("No files found in " + _target.path.string());
        pages[page]->activate();
        // Leaving a 3D page: its renderer goes, the framebuffer comes back.
        auto switchTo = [&](size_t next) -> Error {
            const bool was3D = pages[page]->uses3D();
            pages[page]->deactivate();
            if (was3D)
                if (Error err = _sys.initGraphics(kScreenW, kScreenH, PixelFormat::kRgbA32))
                    return err;
            page = next;
            pages[page]->activate();
            return Error::success();
        };
        struct Leave { // a 3D page's GPU objects go before the system's renderer
            std::vector<std::unique_ptr<Page>> &pages;
            ~Leave() {
                for (auto &p : pages)
                    p->deactivate();
            }
        } leave{pages};

        while (true) {
            Event ev;
            while (_sys.pollEvent(ev)) {
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                    return Error::success();
                const bool pageKey = pages[page]->takesKey(ev);
                if (ev.type == EventType::kKeyDown && ev.key == key::kEscape && !pageKey)
                    return Error::success();
                if (ev.type == EventType::kKeyDown && ev.key == key::kTab && !pageKey) {
                    size_t next = page;
                    do
                        next = (next + 1) % pages.size();
                    while (pages[next]->count() == 0);
                    if (Error err = switchTo(next))
                        return err;
                    continue;
                }
                pages[page]->handleEvent(ev);
                if (pages[page]->wantsToQuit())
                    return Error::success();
            }
            pages[page]->update(_sys.getMillis());
            _sys.updateScreen(_screen);
            // 3D pages are paced by presenting (vsync); the rest needn't spin.
            if (!pages[page]->uses3D())
                _sys.delayMillis(10);
        }
    }

private:
    System &_sys;
    GameTarget _target;
    const GameInfo &_game;
    SearchSet _files;
    Surface _screen;
};

class AgeMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "age"; }
    const char *name() const override { return "Angel Game Engine"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        auto found = detectFromTable(id(), detectionTable(), dir);
        return found.empty() ? detectFromTable(id(), fallbackTable(), dir) : found;
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        const GameInfo *game = findGame(target.gameId);
        if (!game)
            game = &games().front();
        return std::make_unique<AgeEngine>(system, target, *game);
    }
};

} // namespace

const MetaEngine &ageMetaEngine() {
    static const AgeMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
