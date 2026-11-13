// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Clue: Murder at Boddy Mansion (Hasbro Interactive, 1998), a clean-room
// reimplementation.
//
// Everything in this engine must come from the game's data files, observed
// behaviour of the original, or documented reverse engineering of a legally
// owned copy. Never from leaked source code. See CONTRIBUTING.md and
// docs/engines/clue/REVERSE_ENGINEERING.md.
//
// The game starts as the original does: its intro, then its menus, the
// players' set-up and the game in the mansion, alone or over a network
// (frontend.h, play.h). F9 switches to a viewer of the
// game's data (the mansion, videos, animations, images, sounds and text;
// see viewers.h for the keys), where Tab moves between its pages and F9 or
// Esc goes back.

#include "common/fs.h"
#include "common/settings.h"
#include "engines/clue/ui/frontend.h"
#include "engines/clue/pages/viewers.h"
#include "engines/engine.h"

namespace reluva {

namespace {

using clue::AnimationViewer;
using clue::ImageViewer;
using clue::MansionViewer;
using clue::VideoViewer;
using clue::SoundViewer;
using clue::TextViewer;

// Detection. Clue.exe tells the version (the 2.2 patch replaces it): its
// signature (the size and the MD5 of the first kDetectionHashBytes bytes,
// as `reluva --hash` prints them). The language's own files tell US from
// UK: the UK's have one file the US's don't, Sounds/end turn.wav (a spoken
// "end turn": Clue.exe 2.0 and 2.2 name it in their table of voices,
// 0x45ECE0, but nothing plays it). An install copies the chosen language's
// files into Data/Classic; the CD has both, in Data/US and Data/UK. The UK
// installs are inferred from the CD's UK files: no UK install has been seen
// yet. The UK's is named as it was sold: Cluedo: Murder at Blackwell Grange.
const std::vector<GameVariant> kExecutables = {
    {"clue", "2.2", "", "windows", {{"Clue.exe", "cb43d1d845b786193353ac221e5f0ac1", 391168}}},
    {"clue", "2.0", "", "windows", {{"Clue.exe", "7ddb4f359cfd8827b4f7b054e95b1cba", 386048}}},
};
constexpr const char *kUkOnlyFile = "Sounds/end turn.wav";

class ClueEngine : public Engine {
public:
    ClueEngine(System &system, GameTarget target) : sys_(system), target_(std::move(target)) {}

    Error run() override {
        const auto language = target_.gameId == "clue-uk" ? clue::Language::UK : clue::Language::US;
        if (Error err = data_.open(target_.path, language))
            return err;
        if (Error err = sys_.initGraphics(clue::kScreenW, clue::kScreenH, PixelFormat::RGBA32))
            return err;
        screen_.create(clue::kScreenW, clue::kScreenH, PixelFormat::RGBA32);
        audio::Mixer mixer(sys_.audioRate());
        // The launcher's master volume, over the game's own sliders.
        mixer.setMasterVolume(settings::volume(target_.settings, settings::kMasterVolume));
        const bool audioOut = !audio::startMixerOutput(sys_, mixer);
        struct AudioStop {
            System &sys;
            bool on;
            ~AudioStop() {
                if (on)
                    sys.stopAudio();
            }
        } audioStop{sys_, audioOut};

        clue::FrontEnd game(sys_, screen_, data_, audioOut ? &mixer : nullptr,
                            target_.savePath.empty() ? std::filesystem::path() : target_.savePath / "preferences.ini");
        if (Error err = game.open())
            return err;
        bool viewing = false; // the data viewer instead of the game

        MansionViewer mansion(sys_, screen_, data_);
        VideoViewer videos(sys_, screen_, data_, audioOut ? &mixer : nullptr);
        AnimationViewer animations(sys_, screen_, data_);
        ImageViewer images(sys_, screen_, data_);
        SoundViewer sounds(sys_, screen_, data_, audioOut ? &mixer : nullptr);
        TextViewer texts(sys_, screen_, data_);
        // Viewer 0 shows the mansion (or why it can't), 1 videos, 2
        // animations, 3 images, 4 sounds, 5 text; Tab moves to the next one
        // that has something to show.
        constexpr int kViewers = 6;
        const size_t counts[kViewers] = {
            1, videos.count(), animations.count(), images.count(), sounds.count(), texts.count()};
        int viewer = 0;
        const auto activate = [&] {
            if (viewer == 0)
                mansion.activate();
            else if (viewer == 1)
                videos.activate();
            else if (viewer == 2)
                animations.activate();
            else if (viewer == 3)
                images.activate();
            else if (viewer == 4)
                sounds.activate();
            else
                texts.activate();
        };
        const auto leaveViewer = [&] {
            if (viewer == 1)
                videos.deactivate();
            else if (viewer == 4)
                sounds.deactivate();
            viewing = false;
            game.resume();
        };

        while (true) {
            Event ev;
            while (sys_.pollEvent(ev)) {
                if (ev.type == EventType::Quit || ev.type == EventType::ReturnToLauncher)
                    return Error::success();
                if (ev.type == EventType::KeyDown && ev.key == key::F1 + 8) { // F9
                    if (viewing) {
                        leaveViewer();
                    } else {
                        game.leave();
                        viewing = true;
                        activate();
                    }
                    continue;
                }
                if (!viewing) {
                    game.handleEvent(ev);
                    continue;
                }
                if (viewer == 0) {
                    // Tab, and Esc outside the overview, are the engine's;
                    // the rest is the mansion's (the mouse too).
                    const bool keyDown = ev.type == EventType::KeyDown;
                    if (!(keyDown && (ev.key == key::Tab || (ev.key == key::Escape && !mansion.overview())))) {
                        mansion.handleEvent(ev);
                        continue;
                    }
                }
                if (ev.type != EventType::KeyDown)
                    continue;
                if (ev.key == key::Escape) {
                    leaveViewer();
                    continue;
                }
                if (ev.key == key::Tab) {
                    if (viewer == 1)
                        videos.deactivate();
                    else if (viewer == 4)
                        sounds.deactivate();
                    do
                        viewer = (viewer + 1) % kViewers;
                    while (counts[viewer] == 0);
                    activate();
                    continue;
                }
                if (viewer == 1)
                    videos.handleKey(ev.key);
                else if (viewer == 2)
                    animations.handleKey(ev.key);
                else if (viewer == 3)
                    images.handleKey(ev.key);
                else if (viewer == 4)
                    sounds.handleKey(ev.key);
                else if (viewer == 5)
                    texts.handleKey(ev.key);
            }
            if (!viewing) {
                game.update(sys_.getMillis());
                if (game.quit())
                    return Error::success();
            } else if (viewer == 0)
                mansion.update(sys_.getMillis());
            else if (viewer == 1)
                videos.update(sys_.getMillis());
            else if (viewer == 2)
                animations.update(sys_.getMillis());
            else if (viewer == 4)
                sounds.update(sys_.getMillis());
            sys_.updateScreen(screen_);
            sys_.delayMillis(10);
        }
    }

private:
    System &sys_;
    GameTarget target_;
    Surface screen_;
    clue::GameData data_;
};

class ClueMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "clue"; }
    const char *name() const override { return "Clue"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        std::vector<DetectedGame> found;
        if (!fs::findFile(dir, "Clue.exe"))
            return found;
        // The version, by Clue.exe; one not in the table is still Clue.
        const std::vector<DetectedGame> exe = detectFromTable(id(), kExecutables, dir);
        const std::string version = exe.empty() ? "unknown release" : exe[0].description;
        const auto add = [&](bool uk, bool cd) {
            std::string what = version;
            if (cd && !exe.empty())
                what += ", CD";
            const clue::Language language = uk ? clue::Language::UK : clue::Language::US;
            found.push_back({id(), uk ? "clue-uk" : "clue", std::string(clue::gameTitle(language)) + " (" + what + ")",
                             uk ? "en-gb" : "en", "windows"});
        };
        if (fs::findDirectory(dir, "Data/Classic")) {
            // An install: the language its files are.
            add(bool(fs::findFile(dir, std::string("Data/Classic/") + kUkOnlyFile)), false);
            return found;
        }
        // The CD, run from the disc or a copy of it: each language it has.
        if (fs::findDirectory(dir, "Data/US"))
            add(false, true);
        if (fs::findFile(dir, std::string("Data/UK/") + kUkOnlyFile))
            add(true, true);
        return found;
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<ClueEngine>(system, target);
    }
};

} // namespace

const MetaEngine &clueMetaEngine() {
    static const ClueMetaEngine instance;
    return instance;
}

} // namespace reluva
