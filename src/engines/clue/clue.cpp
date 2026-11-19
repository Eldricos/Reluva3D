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
// (frontend.h, play.h). Its data viewer is pages/viewer.cpp's, in
// reluva-viewer.

#include "common/fs.h"
#include "common/settings.h"
#include "engines/clue/ui/frontend.h"
#include "engines/engine.h"

namespace reluva {

namespace {

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
    ClueEngine(System &system, GameTarget target) : _sys(system), _target(std::move(target)) {}

    Error run() override {
        const auto language = _target.gameId == "clue-uk" ? clue::Language::kUK : clue::Language::kUS;
        if (Error err = _data.open(_target.path, language))
            return err;
        if (Error err = _sys.initGraphics(clue::kScreenW, clue::kScreenH, PixelFormat::kRGBA32))
            return err;
        _screen.create(clue::kScreenW, clue::kScreenH, PixelFormat::kRGBA32);
        audio::Mixer mixer(_sys.audioRate());
        // The launcher's master volume, over the game's own sliders.
        mixer.setMasterVolume(settings::volume(_target.settings, settings::kMasterVolume));
        const bool audioOut = !audio::startMixerOutput(_sys, mixer);
        struct AudioStop {
            System &sys;
            bool on;
            ~AudioStop() {
                if (on)
                    sys.stopAudio();
            }
        } audioStop{_sys, audioOut};

        clue::FrontEnd game(_sys, _screen, _data, audioOut ? &mixer : nullptr,
                            _target.savePath.empty() ? std::filesystem::path() : _target.savePath / "preferences.ini");
        if (Error err = game.open())
            return err;

        while (true) {
            Event ev;
            while (_sys.pollEvent(ev)) {
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                    return Error::success();
                game.handleEvent(ev);
            }
            game.update(_sys.getMillis());
            if (game.quit())
                return Error::success();
            _sys.updateScreen(_screen);
            _sys.delayMillis(10);
        }
    }

private:
    System &_sys;
    GameTarget _target;
    Surface _screen;
    clue::GameData _data;
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
            const clue::Language language = uk ? clue::Language::kUK : clue::Language::kUS;
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
    static const ClueMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
