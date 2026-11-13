// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Deep Space Nine - Harbinger (Viacom New Media, 1996, DOS, two
// CDs): the engine's MetaEngine (its name, recognising the game and copying
// it from its CDs, its settings, making an Engine to run it) and its Engine.
// The game itself is Harbinger (harbinger.h) and Game (game/game.h).

#include "audio/mixer.h"
#include "common/fs.h"
#include "common/log.h"
#include "common/settings.h"
#include "engines/engine.h"
#include "engines/stharbinger/detection.h"
#include "engines/stharbinger/harbinger.h"
#include "engines/stharbinger/platform.h"

#include <algorithm>
#include <cstdlib>

namespace reluva::stharbinger {

namespace {

constexpr const char *kGameId = "ds9harbinger";
constexpr const char *kTitle = "Star Trek: Deep Space Nine - Harbinger";

// The releases it recognises by their files' signatures, as `reluva --hash
// <files...>` prints them. A release not listed is still recognised by the
// files it has (findGameParts), as the game with no release named.
//
// Each entry's files are relative to one CD (or an install) as it is, not
// to the folder added: recogniseRelease looks for them in each part of the
// game found, so a CD is recognised alone, beside the other, or in an
// import (disc1/, disc2/). The files to hash, from disc 1, for each
// release and language the game was sold in: SSE/SSE.ALL (the installer's
// archive: DEEP.EXE, the scripts and the dialogue text are inside it, so
// another build or language changes its size), and SSE/SSE.LST and
// SSE/SSE.CAT (small: where each file inside it starts, and their names).
// DS9/TORPEDOS/CD only says which disc a CD is, the same in every release:
// leave it out.
const std::vector<GameVariant> kVariants = {
    // The English release, from its disc 1.
    {"ds9harbinger",
     "Star Trek: Deep Space Nine - Harbinger",
     "en",
     "dos",
     {{"SSE/SSE.ALL", "0919fbe659e77bf6538441e6636039c6", 2865766},
      {"SSE/SSE.LST", "638945e4fb2e7a965da5b6c26a1b649d", 308},
      {"SSE/SSE.CAT", "73439fbcff8912977f44888eae834f38", 779}}},
};

// The settings (MetaEngine::engineOptions), as kept in reluva.ini.
constexpr const char *kOriginal = "original";
constexpr const char *kMusicVolume = "music_volume";
constexpr const char *kSpeechVolume = "speech_volume";
constexpr const char *kEffectsVolume = "effects_volume";
// Developer settings: not part of the game. A start scene (start_scene, a
// scene's name or CC.ALL slot) has no place in the launcher: it's set in
// reluva.ini.
constexpr const char *kStartScene = "start_scene";
constexpr const char *kDebug = "debug";
constexpr const char *kDebugLevel = "debug_level";

GameOption volumeOption(const char *key, const char *label, const char *tooltip) {
    GameOption o{key, label, tooltip, true, {{"100", "100%"}}};
    for (int p = 0; p <= 200; p += 10)
        if (p != 100)
            o.choices.push_back({std::to_string(p), std::to_string(p) + "%"});
    return o;
}

std::string setting(const GameTarget &target, const char *key) {
    const auto it = target.settings.find(key);
    return it == target.settings.end() ? std::string() : it->second;
}

bool flag(const GameTarget &target, const char *key) {
    const std::string v = setting(target, key);
    return !v.empty() && v != "0";
}

// Where each part goes in a folder Reluva copies the game into: a folder
// per CD, or "install", each found again by findGameParts.
std::string partFolder(const GamePart &part) {
    return part.disc >= 0 ? "disc" + std::to_string(part.disc + 1) : "install";
}

// Every file of a part, to be copied under its partFolder, in name order
// (the same on every system).
void addPart(ImportPlan &plan, const GamePart &part) {
    const std::string to = partFolder(part);
    const size_t first = plan.files.size();
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(part.folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec))
            continue;
        const uint64_t size = it->file_size(ec);
        const std::string rel = it->path().lexically_relative(part.folder).generic_string();
        plan.files.push_back({it->path(), to + "/" + std::string(fs::withoutIsoVersion(rel)), size, std::nullopt});
    }
    std::sort(plan.files.begin() + std::ptrdiff_t(first), plan.files.end(),
              [](const ImportPlan::File &a, const ImportPlan::File &b) { return fs::windowsNameLess(a.to, b.to); });
}

std::string describeParts(const std::vector<GamePart> &parts) {
    std::string out;
    for (const GamePart &p : parts) {
        if (!out.empty())
            out += ", ";
        out += p.disc >= 0 ? "disc " + std::to_string(p.disc + 1)
                           : "an installed copy (" + p.folder.filename().string() + ")";
    }
    return out;
}

void addNotes(ImportPlan &plan, const std::vector<GamePart> &parts) {
    plan.notes.clear();
    plan.notes.push_back("Found: " + describeParts(parts) + ".");
    const bool disc1 = std::any_of(parts.begin(), parts.end(), [](const GamePart &p) { return p.disc == 0; });
    const bool disc2 = std::any_of(parts.begin(), parts.end(), [](const GamePart &p) { return p.disc == 1; });
    if (!disc1 || !disc2)
        plan.notes.push_back(std::string("Add disc ") + (disc1 ? "2" : "1") +
                             " too (Add other disc...): the game needs both for all of its scenes.");
    if (!playable(parts))
        plan.notes.push_back("This isn't enough to play yet: the game needs disc 1 (its scene scripts) and the "
                             "data on the discs.");
}

class StharbingerEngine : public Engine {
public:
    StharbingerEngine(System &system, const GameTarget &target) : _sys(system), _target(target) {}

    Error run() override {
        Options options;
        for (const GamePart &part : findGameParts(_target.path))
            options.paths.push_back(part.folder.string());
        if (options.paths.empty())
            return Error::fail("Harbinger isn't found in " + _target.path.string() + " any more");
        options.original = flag(_target, kOriginal);
        options.debug = flag(_target, kDebug);
        options.startScene = setting(_target, kStartScene);
        log::setDebugLevel(options.debug ? std::atoi(setting(_target, kDebugLevel).c_str()) : 0);

        // The sound: the game's three kinds at the player's volumes.
        audio::Mixer mixer(_sys.audioRate());
        mixer.setMasterVolume(settings::volume(_target.settings, settings::kMasterVolume));
        mixer.setTypeVolume(audio::SoundType::kMusic, settings::volume(_target.settings, kMusicVolume));
        mixer.setTypeVolume(audio::SoundType::kSpeech, settings::volume(_target.settings, kSpeechVolume));
        mixer.setTypeVolume(audio::SoundType::kEffects, settings::volume(_target.settings, kEffectsVolume));
        const bool audioOut = !audio::startMixerOutput(_sys, mixer);
        struct AudioOff { // stops the output before the mixer goes
            System &sys;
            ~AudioOff() { sys.stopAudio(); }
        } audioOff{_sys};

        Platform platform(_sys, audioOut ? &mixer : nullptr, _target.savePath);
        try {
            Harbinger game(&platform, options);
            game.run();
        } catch (const FatalError &e) {
            log::setDebugLevel(0);
            return Error::fail(e.what());
        }
        log::setDebugLevel(0);
        return Error::success();
    }

private:
    System &_sys;
    GameTarget _target;
};

class StharbingerMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "stharbinger"; }
    const char *name() const override { return kTitle; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        // Enough to play from (disc 1, or an install beside the CDs'
        // data); then which release, by its parts' file signatures where
        // they're known, else by the files it has.
        const std::vector<GamePart> parts = findGameParts(dir);
        if (!playable(parts))
            return {};
        DetectedGame game =
            recogniseRelease(id(), parts, kVariants).value_or(DetectedGame{id(), kGameId, kTitle, "en", "dos"});
        const bool disc1 = std::any_of(parts.begin(), parts.end(), [](const GamePart &p) { return p.disc == 0; });
        const bool disc2 = std::any_of(parts.begin(), parts.end(), [](const GamePart &p) { return p.disc == 1; });
        if (disc1 != disc2)
            game.description += disc1 ? " (disc 1)" : " (disc 2)";
        return {game};
    }

    std::vector<ImportPlan> planImport(const std::filesystem::path &source) const override {
        const std::vector<GamePart> parts = findGameParts(source);
        if (parts.empty())
            return {};
        ImportPlan plan;
        plan.game = DetectedGame{id(), kGameId, kTitle, "en", "dos"};
        for (const GamePart &part : parts)
            addPart(plan, part);
        addNotes(plan, parts);
        plan.addPatchLabel.clear(); // the game had no patches
        plan.addFolderLabel = "Add other disc...";
        plan.addFolderTooltip = "Copy the game's other CD too (or a copy of it)";
        return {plan};
    }

    bool addToImport(ImportPlan &plan, const std::filesystem::path &file, std::string &message) const override {
        std::vector<GamePart> added = findGameParts(file);
        if (added.empty()) {
            message = "not one of Harbinger's CDs, nor a copy of one";
            return false;
        }
        // What the plan has already: the parts it copies, by their folders.
        std::vector<GamePart> parts;
        for (const ImportPlan::File &f : plan.files) {
            const std::string folder = f.to.substr(0, f.to.find('/'));
            if (std::none_of(parts.begin(), parts.end(), [&](const GamePart &p) { return partFolder(p) == folder; })) {
                GamePart p;
                p.disc = folder.rfind("disc", 0) == 0 ? std::atoi(folder.c_str() + 4) - 1 : -1;
                p.folder = folder;
                p.scripts = p.data = true; // as found when planned
                parts.push_back(p);
            }
        }
        bool any = false;
        for (const GamePart &part : added) {
            if (std::any_of(parts.begin(), parts.end(),
                            [&](const GamePart &p) { return partFolder(p) == partFolder(part); }))
                continue;
            addPart(plan, part);
            parts.push_back(part);
            any = true;
        }
        if (!any) {
            message = describeParts(added) + " is being copied already";
            return false;
        }
        addNotes(plan, parts);
        return true;
    }

    std::vector<GameOption> engineOptions() const override {
        return {
            {kOriginal,
             "Play exactly as the original",
             "Leaves out what Reluva adds: Esc for the game menu (and back), Y/N/Enter/Esc for its\n"
             "questions, M to close the deck map too, the menu on the very first start, and the\n"
             "menu's own colours behind a saved game's picture.",
             false,
             {}},
            volumeOption(kMusicVolume, "Music volume", "The action sequences' music and the videos' soundtracks"),
            volumeOption(kSpeechVolume, "Speech volume", "The conversations and the action sequences' voices"),
            volumeOption(kEffectsVolume, "Effects volume", "Every other sound"),
            // Developer settings (docs/engines/stharbinger/README.md): not
            // part of the game, for studying it.
            {kDebug,
             "Developer: debug keys",
             "F2 shows the hotspots, F10 prints the variables that are set, and the title bar\n"
             "names the scene, the hotspot under the mouse and anything skipped.",
             false,
             {}},
            {kDebugLevel,
             "Developer: console output",
             "How much the engine prints, with the debug keys on.",
             true,
             {{"0", "None"}, {"1", "Some"}, {"2", "More"}, {"3", "All"}}},
        };
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<StharbingerEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::stharbinger

namespace reluva {

const MetaEngine &stharbingerMetaEngine() {
    static const stharbinger::StharbingerMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
