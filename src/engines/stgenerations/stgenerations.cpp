// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Generations (MicroProse, 1997, Windows, two CDs): the engine's
// MetaEngine (its name, recognising the game and copying it with its CDs,
// its settings, making an Engine to run it) and its Engine. The game itself
// is Generations (game/generations.h), ported from STG-Engine.

#include "audio/mixer.h"
#include "common/fs.h"
#include "common/log.h"
#include "common/settings.h"
#include "common/text.h"
#include "engines/engine.h"
#include "engines/stgenerations/debug.h"
#include "engines/stgenerations/detection.h"
#include "engines/stgenerations/game/generations.h"
#include "engines/stgenerations/platform.h"

#include <algorithm>
#include <cstdlib>

namespace reluva::stgenerations {

namespace {

constexpr const char *kGameId = "stgenerations";

// The releases it recognises by their files' signatures, as `reluva --hash
// <files...>` prints them, relative to the installed game. A release not
// listed is still recognised by its executable (detectInstall), as one
// Reluva doesn't know.
const std::vector<GameVariant> kVariants = {
    // The 2 CD Windows release, installed. English, as its README is; no
    // other language's copy has been seen.
    {kGameId,
     "Star Trek: Generations",
     "en",
     "windows",
     {{"sgens.exe", "5044cd4e8cd2ef067dc79d86b38cc44d", 1571328},
      {"DATA/COMMON.DAT", "fc40c9b9bf0dad97830037a4d7a7483f", 11410717}}},
};

// The settings (MetaEngine::engineOptions), as kept in reluva.ini. The
// game's own menu changes the first five; what it sets is kept in the
// game's folder and wins over these from then on.
constexpr const char *kMusicVolume = "music_volume";
constexpr const char *kEffectsVolume = "effects_volume";
constexpr const char *kSpeechVolume = "speech_volume";
constexpr const char *kDifficulty = "difficulty";
constexpr const char *kSubtitles = "subtitles";
// Developer settings: not part of the game. The story's section to start
// at (story=..., story=list names them), a space combat to start in
// (combat=1..), and the CDs' folders when they aren't in the game's
// (cd1=..., cd2=...) have no place in the launcher: they're set in
// reluva.ini.
constexpr const char *kStartMode = "start_mode";
constexpr const char *kStory = "story";
constexpr const char *kCombat = "combat";
constexpr const char *kCd1 = "cd1";
constexpr const char *kCd2 = "cd2";
constexpr const char *kDebugLevel = "debug_level";
constexpr const char *kDebugChannels = "debug_channels";
// For tests: saved games dated this, not by the clock.
constexpr const char *kFixedSaveDate = "fixed_save_date";

GameOption volumeOption(const char *key, const char *label, const char *tooltip) {
    // The original's default is three quarters of the way up.
    GameOption o{key, label, tooltip, true, {{"75", "75%"}}};
    for (int p = 0; p <= 100; p += 5)
        if (p != 75)
            o.choices.push_back({std::to_string(p), std::to_string(p) + "%"});
    return o;
}

std::string setting(const GameTarget &target, const char *key) {
    const auto it = target.settings.find(key);
    return it == target.settings.end() ? std::string() : it->second;
}

int percent(const GameTarget &target, const char *key, int fallback) {
    const std::string v = setting(target, key);
    if (v.empty())
        return fallback;
    return std::clamp(std::atoi(v.c_str()), 0, 100);
}

// The game, as the launcher's settings set it up.
Options optionsFor(const GameTarget &target) {
    Options o;
    o.gameDir = target.path;
    o.description = target.description;
    bool unknown = false;
    detectInstall("stgenerations", kVariants, target.path, &unknown);
    o.unknownVariant = unknown;
    const std::string cd1 = setting(target, kCd1);
    const std::string cd2 = setting(target, kCd2);
    o.cd1 = cd1;
    o.cd2 = cd2;
    o.viewer = setting(target, kStartMode) == "viewer";
    o.story = setting(target, kStory);
    o.combat = std::atoi(setting(target, kCombat).c_str());
    o.fixedSaveDate = setting(target, kFixedSaveDate);
    // Percent to the game's 0..256 volumes.
    o.musicVolume = percent(target, kMusicVolume, 75) * 256 / 100;
    o.effectsVolume = percent(target, kEffectsVolume, 75) * 256 / 100;
    o.speechVolume = percent(target, kSpeechVolume, 75) * 256 / 100;
    const std::string difficulty = setting(target, kDifficulty);
    if (!difficulty.empty())
        o.difficulty = std::clamp(std::atoi(difficulty.c_str()), 0, 2);
    const std::string subtitles = setting(target, kSubtitles);
    if (!subtitles.empty())
        o.subtitles = subtitles != "0";
    return o;
}

class StgenerationsEngine : public Engine {
public:
    StgenerationsEngine(System &system, const GameTarget &target) : sys_(system), target_(target) {}

    Error run() override {
        log::setDebugLevel(std::atoi(setting(target_, kDebugLevel).c_str()));
        for (const std::string &channel : text::split(setting(target_, kDebugChannels), ','))
            if (!text::trim(channel).empty())
                enableDebugChannel(text::trim(channel));

        audio::Mixer mixer(sys_.audioRate());
        mixer.setMasterVolume(settings::volume(target_.settings, settings::kMasterVolume));
        // No sound card shouldn't stop anyone playing.
        const Error audioErr = audio::startMixerOutput(sys_, mixer);
        if (audioErr)
            log::warning("No sound: %s", audioErr.message.c_str());
        Platform platform(sys_, &mixer, target_.savePath);
        platform.setSoundOutput(!audioErr);
        platform.setWindowCaption("Reluva - " + target_.description);
        Error result;
        {
            Generations game(&platform, optionsFor(target_));
            result = game.run();
        }
        sys_.stopAudio();
        sys_.setMouseCursor(nullptr);
        return result;
    }

private:
    System &sys_;
    GameTarget target_;
};

class StgenerationsMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "stgenerations"; }
    const char *name() const override { return "Star Trek: Generations"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        std::vector<DetectedGame> found = detectInstall(id(), kVariants, dir);
        // Without its CDs the game has no story to play: say so.
        if (!found.empty()) {
            const std::vector<GamePart> parts = findGameParts(dir);
            const bool cds =
                std::any_of(parts.begin(), parts.end(), [](const GamePart &p) { return p.kind != GamePart::kInstall; });
            if (!cds)
                found.front().description += " (without its CDs: import it with them to play)";
        }
        return found;
    }

    std::vector<ImportPlan> planImport(const std::filesystem::path &source) const override {
        const std::vector<GamePart> parts = findGameParts(source);
        if (parts.empty())
            return {};
        ImportPlan plan;
        plan.game = DetectedGame{id(), kGameId, "Star Trek: Generations", "en", "windows"};
        for (const GamePart &part : parts)
            addPartToImport(plan, part);
        noteImport(plan);
        plan.addPatchLabel.clear(); // the game had no patches
        plan.addFolderLabel = "Add CD or installed game...";
        plan.addFolderTooltip =
            "Copy one of the game's CDs too (or a copy of it), or the game as its setup installed it";
        return {plan};
    }

    bool addToImport(ImportPlan &plan, const std::filesystem::path &file, std::string &message) const override {
        const std::vector<GamePart> parts = findGameParts(file);
        if (parts.empty()) {
            message = "not one of Star Trek: Generations' CDs, nor the game as its setup installed it";
            return false;
        }
        const size_t before = plan.files.size();
        for (const GamePart &part : parts)
            addPartToImport(plan, part);
        if (plan.files.size() == before) {
            message = "its files are being copied already";
            return false;
        }
        noteImport(plan);
        return true;
    }

    std::vector<GameOption> engineOptions() const override {
        return {
            volumeOption(kMusicVolume, "Music volume", "The music, and the movies' soundtracks"),
            volumeOption(kEffectsVolume, "Effects volume", "Every other sound"),
            volumeOption(kSpeechVolume, "Speech volume",
                         "The crew's voices (the game's menu sets it with the effects)"),
            {kDifficulty,
             "Difficulty",
             "As the game's Settings set it, until they do",
             true,
             {{"1", "Lieutenant"}, {"0", "Captain"}, {"2", "Ensign"}}},
            {kSubtitles, "Subtitles", "The crew's words written under the picture", true, {}},
            // Developer settings (docs/engines/stgenerations/README.md):
            // not part of the game, for studying it.
            {kStartMode,
             "Developer: start with",
             "The resource viewer shows the game's files: images, sounds, levels, overlays and sprites.\n"
             "A section of the story to start at is story in reluva.ini (story=list names them).",
             true,
             {{"game", "The game"}, {"viewer", "The resource viewer"}}},
            {kDebugLevel,
             "Developer: console output",
             "Messages on the console as the game runs",
             true,
             {{"0", "None"}, {"1", "Some"}, {"2", "More"}, {"3", "Everything"}}},
            {kDebugChannels,
             "Developer: about",
             "Which topics the console output covers (with it on)",
             true,
             {{"", "Warnings only"},
              {"resources,scripts,sound", "Everything"},
              {"resources", "Files and archives"},
              {"scripts", "The missions' scripts"},
              {"sound", "Sound"}}},
        };
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<StgenerationsEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::stgenerations

namespace reluva {

const MetaEngine &stgenerationsMetaEngine() {
    static const stgenerations::StgenerationsMetaEngine instance;
    return instance;
}

} // namespace reluva
