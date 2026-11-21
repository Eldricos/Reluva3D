// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Away Team: the engine's MetaEngine (its name, the releases it
// recognises, making an Engine to run one) and its Engine, which so far
// reads the game's rules and shows an empty screen.

#include "engines/engine.h"

#include "common/fs.h"
#include "common/zip.h"
#include "engines/stawayteam/formats/text_object.h"

#include <system_error>

namespace reluva::stawayteam {

namespace {

// The releases it recognises: files that must be there, with the MD5 of
// their start and their size, as `reluva --hash <files...>` prints them.
// A folder matching every file of a variant is that release.
const std::vector<GameVariant> kVariants = {
    {"stawayteam",
     "Star Trek: Away Team",
     "en",
     "windows",
     {{"Star Trek.exe", "fb8c91668fc25623c2a094f9adab12f2", 2437120},
      {"DATA.dat", "d50706fe22ae0ed084e162ba088eb8e0", 325841399}}},
};

// The same game by its files' names alone, for releases not in kVariants
// (another language, a patch): used only when nothing there matches.
const std::vector<GameVariant> kFallback = {
    {"stawayteam",
     "Star Trek: Away Team (unknown release)",
     "",
     "windows",
     {{"Star Trek.exe", "", -1}, {"DATA.dat", "", -1}}},
};

// The files of the game Reluva uses, in the folder it runs from. The CD
// and the install have more (Bink's, Ogg's and the registration's DLLs,
// installers, help pages) that it doesn't.
const char *const kGameFiles[] = {"Star Trek.exe", "DATA.dat", "texture.dat"};
// The movies' folder: loose files, beside DATA.dat.
constexpr const char *kMoviesFolder = "DATA/Movies";

// The folder in `source` holding the game: the source itself (an install,
// or a copy of the CD's Setup folder), or its Setup folder (the CD, which
// has the install there uncompressed as well as in data1.cab).
std::optional<std::filesystem::path> findGameFolder(const std::filesystem::path &source) {
    for (const char *sub : {"", "Setup"}) {
        const auto dir = *sub ? fs::findDirectory(source, sub) : std::optional(source);
        if (dir && fs::findFile(*dir, "Star Trek.exe") && fs::findFile(*dir, "DATA.dat"))
            return dir;
    }
    return std::nullopt;
}

/**
 * The game running: for now its archive opened and its rules read, then an
 * empty screen until the player quits.
 */
class StawayteamEngine : public Engine {
public:
    StawayteamEngine(System &system, const GameTarget &target) : _sys(system), _target(target) {}

    Error run() override {
        const auto data = fs::findFile(_target.path, "DATA.dat");
        if (!data)
            return Error::fail("No DATA.dat in " + _target.path.string());
        if (Error err = _data.open(*data))
            return err;
        if (!_data.has("Rules.txt"))
            return Error::fail("DATA.dat has no Rules.txt");
        std::vector<uint8_t> rules;
        if (Error err = _data.read("Rules.txt", rules))
            return Error::fail("DATA.dat: Rules.txt: " + err.message);
        if (Error err = readTextObject(std::string(rules.begin(), rules.end()), _rules))
            return Error::fail("DATA.dat: Rules.txt: " + err.message);
        if (Error err = _sys.initGraphics(kWidth, kHeight, PixelFormat::kCLUT8))
            return err;
        _sys.setWindowTitle(("Reluva - " + _target.description).c_str());
        Surface screen;
        screen.create(kWidth, kHeight, PixelFormat::kCLUT8);
        for (bool running = true; running;) {
            Event ev;
            while (_sys.pollEvent(ev))
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher ||
                    (ev.type == EventType::kKeyDown && ev.key == key::kEscape))
                    running = false;
            _sys.updateScreen(screen);
            _sys.delayMillis(1);
        }
        return Error::success();
    }

private:
    static constexpr int kWidth = 640;
    static constexpr int kHeight = 480;
    System &_sys;
    GameTarget _target;
    ZipArchive _data;  // DATA.dat: nearly all the game's files
    TextObject _rules; // Rules.txt: the first map, the menus, the screen's layout
};

/**
 * The engine as the launcher sees it: its name, the releases it
 * recognises, and making a StawayteamEngine to run one.
 */
class StawayteamMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "stawayteam"; }
    const char *name() const override { return "Star Trek: Away Team"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        auto found = detectFromTable(id(), kVariants, dir);
        if (found.empty())
            found = detectFromTable(id(), kFallback, dir);
        return found;
    }

    std::vector<ImportPlan> planImport(const std::filesystem::path &source) const override {
        const auto dir = findGameFolder(source);
        if (!dir)
            return {};
        const auto found = detect(*dir);
        if (found.empty())
            return {};
        ImportPlan plan;
        plan.game = found[0];
        std::error_code ec;
        auto add = [&](const std::filesystem::path &from, std::string to) {
            ImportPlan::File file;
            file.from = from;
            file.to = std::move(to);
            file.size = std::filesystem::file_size(from, ec);
            plan.files.push_back(std::move(file));
        };
        for (const char *name : kGameFiles)
            if (const auto file = fs::findFile(*dir, name))
                add(*file, name);
        // The movies keep their own names and folders ("Mission 01/...").
        if (const auto movies = fs::findDirectory(*dir, kMoviesFolder)) {
            for (const auto &entry : std::filesystem::recursive_directory_iterator(*movies, ec)) {
                if (!entry.is_regular_file(ec))
                    continue;
                std::string to = std::string(kMoviesFolder);
                for (const auto &part : entry.path().lexically_relative(*movies))
                    to += "/" + std::string(fs::withoutIsoVersion(part.string()));
                add(entry.path(), to);
            }
        } else {
            plan.notes.push_back("No movies were found (DATA/Movies): the game plays without them.");
        }
        // No patch or expansion is known for the game.
        plan.addPatchLabel.clear();
        plan.addFolderLabel.clear();
        return {plan};
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<StawayteamEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::stawayteam

namespace reluva {

const MetaEngine &stawayteamMetaEngine() {
    static const stawayteam::StawayteamMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
