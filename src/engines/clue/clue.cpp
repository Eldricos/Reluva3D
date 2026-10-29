// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Clue: Murder at Boddy Mansion (Hasbro Interactive, 1998), a clean-room
// reimplementation.
//
// Everything in this engine must come from the game's data files, observed
// behaviour of the original, or documented reverse engineering of a legally
// owned copy. Never from leaked source code. See CONTRIBUTING.md and
// docs/engines/clue.md.
//
// For now the engine is a data viewer: the mansion, videos, animations,
// images, sounds and text (see viewers.h for the keys). Tab switches between them; Esc goes back to
// the launcher.

#include "engines/engine.h"
#include "engines/clue/viewers.h"

namespace reluva {

namespace {

using clue::AnimationViewer;
using clue::ImageViewer;
using clue::MansionViewer;
using clue::VideoViewer;
using clue::SoundViewer;
using clue::TextViewer;

// Detection table. Built with `reluva --hash`: each entry needs the size and
// the MD5 of the first kDetectionHashBytes bytes.
//
// Clue.exe tells the version (the 2.2 patch replaces it) and the ticker's
// sentences the language ("Miss Scarlet" or "Miss Scarlett"). An install
// copies the chosen language's files into Data/Classic; the CD has both.
// The UK installs are inferred from the CD's UK files: no UK install has
// been seen yet.
constexpr const char* kExe20 = "7ddb4f359cfd8827b4f7b054e95b1cba";
constexpr int64_t kExe20Size = 386048;
constexpr const char* kExe22 = "cb43d1d845b786193353ac221e5f0ac1";
constexpr int64_t kExe22Size = 391168;
constexpr const char* kTickerUs = "3c205884038261fa092d50b65d5f51da";
constexpr int64_t kTickerUsSize = 272574;
constexpr const char* kTickerUk = "b5048de76701fe2041ffa7f0a6a6b7e9";
constexpr int64_t kTickerUkSize = 281028;

const std::vector<GameVariant> kVariants = {
    {"clue", "Clue: Murder at Boddy Mansion (2.2)", "en", "windows",
     {{"Clue.exe", kExe22, kExe22Size}, {"Data/Classic/Text/TickerStrings.txt", kTickerUs, kTickerUsSize}}},
    {"clue", "Clue: Murder at Boddy Mansion (2.0)", "en", "windows",
     {{"Clue.exe", kExe20, kExe20Size}, {"Data/Classic/Text/TickerStrings.txt", kTickerUs, kTickerUsSize}}},
    {"clue-uk", "Clue: Murder at Boddy Mansion (UK English, 2.2)", "en-gb", "windows",
     {{"Clue.exe", kExe22, kExe22Size}, {"Data/Classic/Text/TickerStrings.txt", kTickerUk, kTickerUkSize}}},
    {"clue-uk", "Clue: Murder at Boddy Mansion (UK English, 2.0)", "en-gb", "windows",
     {{"Clue.exe", kExe20, kExe20Size}, {"Data/Classic/Text/TickerStrings.txt", kTickerUk, kTickerUkSize}}},
    // The CD, run from the disc or a copy of it.
    {"clue", "Clue: Murder at Boddy Mansion (2.0, CD)", "en", "windows",
     {{"Clue.exe", kExe20, kExe20Size}, {"Data/US/Text/TickerStrings.txt", kTickerUs, kTickerUsSize}}},
    {"clue-uk", "Clue: Murder at Boddy Mansion (UK English, 2.0, CD)", "en-gb", "windows",
     {{"Clue.exe", kExe20, kExe20Size}, {"Data/UK/Text/TickerStrings.txt", kTickerUk, kTickerUkSize}}},
};

class ClueEngine : public Engine {
public:
    ClueEngine(System& system, GameTarget target) : sys_(system), target_(std::move(target)) {}

    Error run() override {
        const auto language = target_.gameId == "clue-uk" ? clue::Language::UK : clue::Language::US;
        if (Error err = data_.open(target_.path, language))
            return err;
        if (Error err = sys_.initGraphics(clue::kScreenW, clue::kScreenH, PixelFormat::RGBA32))
            return err;
        screen_.create(clue::kScreenW, clue::kScreenH, PixelFormat::RGBA32);
        audio::Mixer mixer(sys_.audioRate());
        const bool audioOut = !audio::startMixerOutput(sys_, mixer);
        struct AudioStop {
            System& sys;
            bool on;
            ~AudioStop() {
                if (on)
                    sys.stopAudio();
            }
        } audioStop{sys_, audioOut};

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
        const size_t counts[kViewers] = {1, videos.count(), animations.count(), images.count(), sounds.count(),
                                         texts.count()};
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
        activate();

        while (true) {
            Event ev;
            while (sys_.pollEvent(ev)) {
                if (ev.type == EventType::Quit || ev.type == EventType::ReturnToLauncher)
                    return Error::success();
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
                if (ev.key == key::Escape)
                    return Error::success();
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
            if (viewer == 0)
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
    System& sys_;
    GameTarget target_;
    Surface screen_;
    clue::GameData data_;
};

class ClueMetaEngine : public MetaEngine {
public:
    const char* id() const override { return "clue"; }
    const char* name() const override { return "Clue"; }

    std::vector<DetectedGame> detect(const std::filesystem::path& dir) const override {
        return detectFromTable(id(), kVariants, dir);
    }

    std::unique_ptr<Engine> create(System& system, const GameTarget& target) const override {
        return std::make_unique<ClueEngine>(system, target);
    }
};

} // namespace

const MetaEngine& clueMetaEngine() {
    static const ClueMetaEngine instance;
    return instance;
}

} // namespace reluva
