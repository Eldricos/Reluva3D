// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Unreal engine's data viewer, in reluva-viewer: the maps (drawn, and
// played with Enter), meshes, textures, sounds, movies and packages (see
// viewers.h for each page's keys). Tab moves between the pages, Esc leaves.
//
// RELUVA_BENCH="<map> <seconds>" goes straight into that map, played, and
// prints how long its frames took.

#include "audio/mixer.h"
#include "common/settings.h"
#include "engines/unreal/formats/package_set.h"
#include "engines/unreal/games.h"
#include "engines/unreal/pages/viewers.h"
#include "engines/viewer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>

namespace reluva {

namespace {

using namespace unreal;

constexpr int kScreenW = 640;
constexpr int kScreenH = 480;

class UnrealViewer : public Engine {
public:
    UnrealViewer(System &system, GameTarget target, const GameInfo &game)
        : sys_(system), target_(std::move(target)), game_(game) {}

    Error run() override {
        std::vector<std::string> warnings;
        if (Error err = packages_.open(target_.path, game_.iniName, &warnings))
            return err;

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
        std::vector<std::unique_ptr<Page>> pages;
        pages.push_back(std::make_unique<LevelView>(ctx));
        pages.push_back(std::make_unique<MeshViewer>(ctx));
        pages.push_back(std::make_unique<TextureViewer>(ctx));
        pages.push_back(std::make_unique<SoundViewer>(ctx));
        auto movies = std::make_unique<MoviePlayer>(ctx);
        MoviePlayer *moviePage = movies.get();
        const size_t moviePageIndex = pages.size();
        pages.push_back(std::move(movies));
        pages.push_back(std::make_unique<PackageViewer>(ctx));
        size_t first = 0;
        while (first < pages.size() && pages[first]->count() == 0)
            ++first;
        if (first == pages.size())
            return Error::fail("No packages found in " + target_.path.string());
        // The game's intro plays first, as the original's does; then the
        // first page.
        size_t page = first;
        bool intro = false;
        if (moviePage->count() > 0) {
            page = moviePageIndex;
            pages[page]->activate();
            intro = moviePage->play("intro.avi");
            if (!intro)
                page = first;
        }
        // RELUVA_BENCH="<map> <seconds>": straight into that map, played
        // (no intro), its frames timed, then out: each frame's own work
        // (the page's update: the level's ticks, posing, the draws handed
        // to the renderer) and the presenting apart.
        std::string benchMap;
        double benchSeconds = 0;
        if (const char *bench = std::getenv("RELUVA_BENCH")) {
            std::istringstream in(bench);
            in >> benchMap >> benchSeconds;
            if (benchMap.empty() || benchSeconds <= 0)
                benchMap.clear();
        }
        if (!benchMap.empty()) {
            intro = false;
            page = 0;
        }
        if (!intro)
            pages[page]->activate();
        if (!benchMap.empty())
            static_cast<LevelView *>(pages[0].get())->travel(benchMap);
        std::vector<double> benchUpdate, benchPresent, benchPlay, benchEnd;
        const auto benchStart = std::chrono::steady_clock::now();
        auto switchTo = [&](size_t next) -> Error {
            const bool was3D = pages[page]->uses3D();
            pages[page]->deactivate();
            if (was3D)
                if (Error err = sys_.initGraphics(kScreenW, kScreenH, PixelFormat::kRGBA32))
                    return err;
            page = next;
            pages[page]->activate();
            return Error::success();
        };

        while (true) {
            Event ev;
            while (sys_.pollEvent(ev)) {
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                    return Error::success();
                if (ev.type == EventType::kKeyDown && ev.key == key::kEscape && !pages[page]->ownsKey(ev.key))
                    return Error::success();
                if (ev.type == EventType::kKeyDown && ev.key == key::kTab) {
                    size_t next = page;
                    do
                        next = (next + 1) % pages.size();
                    while (pages[next]->count() == 0);
                    if (Error err = switchTo(next))
                        return err;
                    continue;
                }
                pages[page]->handleEvent(ev);
            }
            const auto t0 = std::chrono::steady_clock::now();
            pages[page]->update(sys_.getMillis());
            const auto t1 = std::chrono::steady_clock::now();
            if (intro && !moviePage->playing()) {
                intro = false;
                if (page == moviePageIndex && page != first)
                    if (Error err = switchTo(first))
                        return err;
            }
            sys_.updateScreen(screen_);
            if (!benchMap.empty()) {
                const auto t2 = std::chrono::steady_clock::now();
                benchUpdate.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
                benchPresent.push_back(std::chrono::duration<double, std::milli>(t2 - t1).count());
                const LevelView::FrameTimes &ft = static_cast<LevelView *>(pages[0].get())->frameTimes();
                benchPlay.push_back(ft.play);
                benchEnd.push_back(ft.endFrame);
                if (std::chrono::duration<double>(t2 - benchStart).count() >= benchSeconds) {
                    auto report = [](const char *what, std::vector<double> v) {
                        std::sort(v.begin(), v.end());
                        double sum = 0;
                        for (double x : v)
                            sum += x;
                        std::printf("%s: mean %.2f ms, median %.2f, 95%% %.2f, max %.2f\n", what,
                                    v.empty() ? 0.0 : sum / double(v.size()), v.empty() ? 0.0 : v[v.size() / 2],
                                    v.empty() ? 0.0 : v[v.size() * 95 / 100], v.empty() ? 0.0 : v.back());
                    };
                    std::printf("bench %s: %zu frames in %.1f s\n", benchMap.c_str(), benchUpdate.size(),
                                std::chrono::duration<double>(t2 - benchStart).count());
                    report("frame's own work", benchUpdate);
                    report("  the level's ticks", benchPlay);
                    report("  renderer's endFrame", benchEnd);
                    report("presenting", benchPresent);
                    std::fflush(stdout);
                    return Error::success();
                }
            }
            // 3D pages are paced by presenting (vsync); the rest needn't spin.
            if (!pages[page]->uses3D())
                sys_.delayMillis(10);
        }
    }

private:
    System &sys_;
    GameTarget target_;
    const GameInfo &game_;
    PackageSet packages_;
    Surface screen_;
};

class UnrealMetaViewer : public MetaViewer {
public:
    const char *engineId() const override { return "unreal"; }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        const GameInfo *game = findGame(target.gameId);
        if (!game)
            game = &games().back(); // the unidentified game
        return std::make_unique<UnrealViewer>(system, target, *game);
    }
};

} // namespace

const MetaViewer &unrealMetaViewer() {
    static const UnrealMetaViewer kInstance;
    return kInstance;
}

} // namespace reluva
