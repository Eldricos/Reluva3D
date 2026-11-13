// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Need for Speed engine, starting with Need for Speed III: Hot Pursuit
// (docs/engines/nfs/games/nfs3/PLAN.md).
//
// Everything in this engine must come from the game's data files, observed
// behaviour of the original, public format documentation, or documented
// reverse engineering of a legally owned copy. Never from leaked source
// code. See CONTRIBUTING.md.
//
// For now the engine is a data viewer: what it found (the folders, the
// CD's files, tracks and cars), then the tracks and cars in 3D, the
// game's pictures, strings and fonts, its sounds, music and movies (see
// viewers.h for the keys). Tab switches between them; Esc goes
// back to the launcher. The Play page runs the game itself (play_page.h).

#include "audio/mixer.h"
#include "common/settings.h"
#include "engines/engine.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/game_files.h"
#include "engines/nfs/games.h"
#include "engines/nfs/pages/viewers.h"

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
    NfsEngine(System &system, GameTarget target, const GameInfo &game)
        : sys_(system), target_(std::move(target)), game_(game) {}

    Error run() override {
        const GameFolders folders = nfs::findFolders(target_.path, target_.settings, game_.executable);
        if (nfs::trackFolders(folders.install).empty())
            return Error::fail("No tracks (GameData/Tracks/trkNNN) found in " + folders.install.string());
        nfs::GameFiles files;
        files.scan(folders);

        if (Error err = sys_.initGraphics(kScreenW, kScreenH, PixelFormat::kRGBA32))
            return err;
        screen_.create(kScreenW, kScreenH, PixelFormat::kRGBA32);

        // One mixer for the pages' sounds; without audio output, none. The
        // pages go before it. surround= in reluva.ini sends it through the
        // Pro Logic decoder model, for the game's Dolby Surround mode.
        audio::Mixer mixer(sys_.audioRate());
        const auto surround = target_.settings.find("surround");
        const bool audioOut = !audio::startMixerOutput(
            sys_, mixer,
            audio::surroundFromSetting(surround == target_.settings.end() ? std::string() : surround->second));
        mixer.setMasterVolume(settings::volume(target_.settings, settings::kMasterVolume));
        struct AudioOff { // stops the output before the mixer goes
            System &sys;
            ~AudioOff() { sys.stopAudio(); }
        } audioOff{sys_};
        auto pages =
            nfs::makePages(game_.name, folders, files, audioOut ? &mixer : nullptr, target_.savePath, target_.settings);

        size_t page = 0;
        bool dirty = true;
        auto show = [&](size_t next) -> Error {
            // Leaving a 3D page: its renderer goes, the framebuffer comes back.
            const bool was3D = pages[page]->uses3D();
            pages[page]->deactivate();
            if (was3D)
                if (Error err = sys_.initGraphics(kScreenW, kScreenH, PixelFormat::kRGBA32))
                    return err;
            page = next;
            sys_.setWindowTitle(std::string("Reluva - ") + game_.name + " - " + pages[page]->name());
            pages[page]->activate(sys_);
            dirty = true;
            return Error::success();
        };
        sys_.setWindowTitle(std::string("Reluva - ") + game_.name + " - " + pages[page]->name());
        pages[page]->activate(sys_);

        while (true) {
            Event ev;
            while (sys_.pollEvent(ev)) {
                if (ev.type == EventType::Quit || ev.type == EventType::ReturnToLauncher) {
                    pages[page]->deactivate();
                    return Error::success();
                }
                if (ev.type == EventType::KeyDown && ev.key == key::Escape && !pages[page]->capturesEscape()) {
                    pages[page]->deactivate();
                    return Error::success();
                }
                if (ev.type == EventType::KeyDown && ev.key == key::Tab) {
                    const size_t step = (ev.mods & keymod::Shift) ? pages.size() - 1 : 1;
                    if (Error err = show((page + step) % pages.size()))
                        return err;
                    continue;
                }
                pages[page]->handleEvent(ev);
                dirty = true;
            }
            if (pages[page]->finished()) {
                pages[page]->deactivate();
                return Error::success();
            }
            if (pages[page]->uses3D()) {
                // Paced by presenting (vsync).
                pages[page]->update(sys_.getMillis());
                continue;
            }
            if (pages[page]->tick(sys_.getMillis()))
                dirty = true;
            if (dirty) {
                pages[page]->draw(screen_);
                dirty = false;
            }
            sys_.updateScreen(screen_);
            sys_.delayMillis(10);
        }
    }

private:
    System &sys_;
    GameTarget target_;
    const GameInfo &game_;
    Surface screen_;
};

class NfsMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "nfs"; }
    const char *name() const override { return "Need for Speed"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        auto found = detectFromTable(id(), nfs::detectionTable(), dir);
        return found.empty() ? detectFromTable(id(), nfs::fallbackTable(), dir) : found;
    }

    // The settings the game's pages read (pages/viewers.cpp, makePages),
    // for every NFS game and each: the first choice of each is what the
    // engine does when it's set nowhere. The others (cdpath, serial, modem,
    // wheelpedals, glidedll, glideboard) name things of the machine's, and
    // are set in reluva.ini.
    std::vector<GameOption> engineOptions() const override {
        auto choice = [](const char *key, const char *label, const char *tooltip,
                         std::vector<std::pair<std::string, std::string>> choices) {
            GameOption o;
            o.key = key;
            o.label = label;
            o.tooltip = tooltip;
            o.choices = std::move(choices);
            return o;
        };
        return {
            choice("renderer", "Renderer the game is told of",
                   "The game draws as for a Voodoo 2 (with fog) through Reluva's own THRASH; or as for its "
                   "Direct3D renderer; or its own Voodoo 2 renderer runs on the Glide stand-in.",
                   {{"voodoo2", "Voodoo 2"}, {"direct3d", "Direct3D"}, {"glide", "Voodoo 2 on Glide"}}),
            choice("renderscale", "Picture size", "The game's 640x480 drawn at this many times its size on the GPU.",
                   {{"2", "2x"}, {"1", "1x"}, {"3", "3x"}, {"4", "4x"}}),
            choice("texturefilter", "Textures", "Filtered as the game's renderers ask, or point-sampled.",
                   {{"bilinear", "Filtered"}, {"point", "Point-sampled"}}),
            choice("surround", "Dolby Surround",
                   "The game's Dolby Surround mode through a model of a Pro Logic decoder: to a 4.0 or 5.1 "
                   "device, the passive matrix alone, or folded back to stereo for headphones.",
                   {{"off", "Off (stereo as mixed)"},
                    {"prologic", "Pro Logic"},
                    {"passive", "Passive"},
                    {"headphones", "Headphones"}}),
            choice("joystick", "Game controller", "A controller connected when the game starts, offered to it.",
                   {{"on", "Offered to the game"}, {"off", "Left out"}}),
            choice("forcefeedback", "Force feedback", "The game's forces to a wheel's motor or a pad's rumble.",
                   {{"on", "On"}, {"off", "Off"}}),
            choice("network", "Network play", "The game's network play over the machine's network.",
                   {{"on", "On"}, {"off", "Off"}}),
            choice("positionalupdates", "Positional updates",
                   "Network races with the game's positional updates (for slow links); every player needs the "
                   "same.",
                   {{"off", "Off"}, {"on", "On"}}),
            choice("movielines", "Movies' black lines", "The lines the game leaves black between a movie's.",
                   {{"fill", "Filled in"}, {"keep", "Kept"}}),
        };
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        const GameInfo *game = nfs::findGame(target.gameId);
        if (!game)
            game = &nfs::games().front();
        return std::make_unique<NfsEngine>(system, target, *game);
    }
};

} // namespace

const MetaEngine &nfsMetaEngine() {
    static const NfsMetaEngine instance;
    return instance;
}

} // namespace reluva
