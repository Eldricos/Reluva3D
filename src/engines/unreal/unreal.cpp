// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Unreal Engine 1: games built on Epic's first Unreal engine, starting
// with Star Trek: Deep Space Nine - The Fallen (docs/engines/unreal/).
//
// The game starts as the original does: its intro movie, then the level
// its .ini names to start in ([URL] LocalMap), played. The data viewer,
// with every level and the game's files, is in pages/viewer.cpp.

#include "audio/mixer.h"
#include "common/settings.h"
#include "engines/engine.h"
#include "engines/unreal/formats/package_set.h"
#include "engines/unreal/games.h"
#include "engines/unreal/render/level_view.h"
#include "engines/unreal/ui/movie.h"

#include <fstream>
#include <memory>
#include <sstream>

namespace reluva {

namespace {

using namespace unreal;

constexpr int kScreenW = 640;
constexpr int kScreenH = 480;

// The level the game starts in: its own .ini's LocalMap, else the one the
// install shipped with (Default.ini, which the original copies to make it).
std::string startMap(const PackageSet &packages) {
    for (const std::filesystem::path &file : {packages.iniFile(), packages.iniFile().parent_path() / "Default.ini"}) {
        std::ifstream in(file, std::ios::binary);
        if (!in)
            continue;
        std::stringstream text;
        text << in.rdbuf();
        std::string map = iniValue(text.str(), "URL", "LocalMap");
        if (!map.empty())
            return map;
    }
    return {};
}

class UnrealEngine : public Engine {
public:
    UnrealEngine(System &system, GameTarget target, const GameInfo &game)
        : sys_(system), target_(std::move(target)), game_(game) {}

    Error run() override {
        std::vector<std::string> warnings;
        if (Error err = packages_.open(target_.path, game_.iniName, &warnings))
            return err;
        const std::string map = startMap(packages_);
        if (map.empty())
            return Error::fail("The game's .ini names no level to start in ([URL] LocalMap)");

        if (Error err = sys_.initGraphics(kScreenW, kScreenH, PixelFormat::kRGBA32))
            return err;
        screen_.create(kScreenW, kScreenH, PixelFormat::kRGBA32);

        audio::Mixer mixer(sys_.audioRate());
        mixer.setMasterVolume(settings::volume(target_.settings, settings::kMasterVolume));
        const bool audioOut = !audio::startMixerOutput(sys_, mixer);
        struct AudioOff { // stops the output before the mixer goes
            System &sys;
            ~AudioOff() { sys.stopAudio(); }
        } audioOff{sys_};

        ViewContext ctx{sys_, screen_, packages_, game_, warnings, audioOut ? &mixer : nullptr};
        if (!target_.savePath.empty())
            ctx.saveFolder = target_.savePath / "Saves";
        ctx.gameFolder = target_.path;

        // The intro first, while there's one; Esc, Space or Enter skip it.
        MoviePlayer movies(ctx);
        if (movies.count() > 0) {
            movies.activate();
            if (movies.play("intro.avi"))
                while (movies.playing()) {
                    Event ev;
                    while (sys_.pollEvent(ev)) {
                        if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                            return Error::success();
                        movies.handleEvent(ev);
                    }
                    movies.update(sys_.getMillis());
                    sys_.updateScreen(screen_);
                    sys_.delayMillis(5);
                }
            movies.deactivate();
            if (Error err = sys_.initGraphics(kScreenW, kScreenH, PixelFormat::kRGBA32))
                return err;
        }

        LevelView level(ctx);
        level.setGame(true);
        level.activate();
        level.travel(map);
        while (true) {
            Event ev;
            while (sys_.pollEvent(ev)) {
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                    return Error::success();
                if (ev.type == EventType::kKeyDown && ev.key == key::kEscape && !level.ownsKey(ev.key))
                    return Error::success();
                level.handleEvent(ev);
            }
            level.update(sys_.getMillis());
            sys_.updateScreen(screen_);
        }
    }

private:
    System &sys_;
    GameTarget target_;
    const GameInfo &game_;
    PackageSet packages_;
    Surface screen_;
};

class UnrealMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "unreal"; }
    const char *name() const override { return "Unreal Engine 1"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        return unreal::detect(id(), dir);
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        const GameInfo *game = findGame(target.gameId);
        if (!game)
            game = &games().back(); // the unidentified game
        return std::make_unique<UnrealEngine>(system, target, *game);
    }
};

} // namespace

const MetaEngine &unrealMetaEngine() {
    static const UnrealMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
