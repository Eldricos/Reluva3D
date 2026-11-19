// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Clue commands that run the game and its data viewer: each
// run as reluva and reluva-viewer run it, on a System with no window whose
// clock moves only as it waits, so a run is the same on every machine. The
// game is played by clicking its own buttons where dialogInfo.res puts
// them. Used by the game-files test (tests/engines/clue/gamedata.cmake).

#include "common/headless_system.h"
#include "image/image.h"
#include "engines/clue/ui/frontend.h"
#include "engines/clue/game_data.h"
#include "engines/clue/formats/resi.h"
#include "engines/clue/game/rules.h"
#include "engines/clue/ui/ui.h"
#include "engines/registry.h"
#include "engines/viewer_registry.h"
#include "devtools/tool.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::tool;

// FNV-1a, 64 bits: the same on every machine.
struct Hash {
    uint64_t h = 0xcbf29ce484222325ull;
    void bytes(const uint8_t *p, size_t n) {
        for (size_t i = 0; i < n; ++i)
            h = (h ^ p[i]) * 0x100000001b3ull;
    }
    std::string hex() const {
        char s[17];
        std::snprintf(s, sizeof s, "%016llx", static_cast<unsigned long long>(h));
        return s;
    }
};

// The headless System with a player: events at given times. It keeps a
// checksum of every screen shown, and with a folder for them, writes one
// a second as a PNG.
class PlayingSystem : public HeadlessSystem {
public:
    struct Input {
        uint64_t at;
        Event event;
    };
    std::vector<Input> inputs;
    Hash screens;
    int shown = 0;
    std::vector<std::string> titles;
    std::filesystem::path shots;

    void updateScreen(const Surface &s) override {
        HeadlessSystem::updateScreen(s);
        screens.bytes(s.pixels.data(), s.pixels.size());
        shown++;
        if (!shots.empty() && getMillis() / 1000 != _lastShot) {
            _lastShot = getMillis() / 1000;
            image::Image img;
            img.w = s.w;
            img.h = s.h;
            img.rgba = s.pixels;
            const std::vector<uint8_t> png = image::encodePng(img);
            char name[32];
            std::snprintf(name, sizeof name, "%06llu.png", static_cast<unsigned long long>(getMillis()));
            std::ofstream(shots / name, std::ios::binary)
                .write(reinterpret_cast<const char *>(png.data()), std::streamsize(png.size()));
        }
    }
    void setWindowTitle(const std::string &t) override {
        if (titles.empty() || titles.back() != t)
            titles.push_back(t);
    }
    bool pollEvent(Event &ev) override {
        if (_next < inputs.size() && inputs[_next].at <= getMillis()) {
            ev = inputs[_next++].event;
            return true;
        }
        return false;
    }

private:
    size_t _next = 0;
    uint64_t _lastShot = ~uint64_t(0);
};

Event keyDown(uint32_t k) {
    Event e;
    e.type = EventType::kKeyDown;
    e.key = k;
    return e;
}

Event mouse(EventType type, int x, int y) {
    Event e;
    e.type = type;
    e.button = MouseButton::kLeft;
    e.mouseX = x;
    e.mouseY = y;
    e.clicks = type == EventType::kMouseDown ? 1 : 0;
    return e;
}

// The saved games written: each one's name, and a checksum of the game.
std::string savesIn(const std::filesystem::path &dir) {
    std::string out;
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && it->path().extension() == ".sav")
            files.push_back(it->path());
    std::sort(files.begin(), files.end());
    for (const auto &f : files) {
        std::ifstream in(f, std::ios::binary);
        std::string tag;
        std::string name;
        std::getline(in, tag);
        clue::saveio::getString(in, name);
        const std::vector<uint8_t> rest((std::istreambuf_iterator<char>(in)), {});
        Hash h;
        h.bytes(rest.data(), rest.size());
        out += (out.empty() ? "" : ", ") + f.filename().string() + " \"" + name + "\" " + h.hex();
    }
    return out.empty() ? "(none)" : out;
}

// The game in the folder: the US's, or with uk the UK's (the CD has both).
// Prints it, or says there's none.
bool findGame(const std::filesystem::path &gameDir, bool uk, std::vector<DetectedGame> &found) {
    const MetaEngine *engine = findEngine("clue");
    const std::vector<DetectedGame> games = engine ? engine->detect(gameDir) : std::vector<DetectedGame>{};
    for (const DetectedGame &g : games)
        if ((g.gameId == "clue-uk") == uk)
            found.push_back(g);
    if (found.empty()) {
        std::fprintf(stderr, "No %s in %s\n", uk ? "Cluedo (the UK's)" : "Clue", gameDir.string().c_str());
        return false;
    }
    std::printf("game: %s %s\n", found[0].gameId.c_str(), found[0].description.c_str());
    return true;
}

// The game folder and options both commands take.
struct RunArgs {
    std::filesystem::path gameDir;
    std::filesystem::path shots;
    bool uk = false;
};

RunArgs runArgs(int argc, char **argv) {
    RunArgs a;
    a.gameDir = argv[2];
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "-shots") == 0 && i + 1 < argc)
            a.shots = argv[++i];
        else if (std::strcmp(argv[i], "-uk") == 0)
            a.uk = true;
    }
    return a;
}

// What a run did: how it ended, every screen's checksum, the window's
// titles.
void printRun(const PlayingSystem &sys, const Error &err) {
    std::printf("result: %s\n", err ? ("error: " + err.message).c_str() : "ok");
    std::printf("screens: %d %s, %.1f s\n", sys.shown, sys.screens.hex().c_str(), double(sys.getMillis()) / 1000);
    std::string titles;
    for (const std::string &title : sys.titles)
        titles += (titles.empty() ? "" : " / ") + title;
    std::printf("titles: %s\n", titles.c_str());
}

int cmdGameRun(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool clue game-run <game folder> [-uk] [-shots <folder>]\n");
        return 2;
    }
    const RunArgs args = runArgs(argc, argv);
    const std::filesystem::path &gameDir = args.gameDir;

    const MetaEngine *engine = findEngine("clue");
    std::vector<DetectedGame> found;
    if (!engine || !findGame(gameDir, args.uk, found))
        return 1;

    // Where the game's buttons are: its dialogs as the game shows them.
    clue::GameData data;
    if (Error err = data.open(gameDir, found[0].gameId == "clue-uk" ? clue::Language::kUK : clue::Language::kUS)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    clue::Resi resi;
    const std::filesystem::path *resiFile = data.find("dialogInfo.res");
    std::vector<uint8_t> resiBytes;
    if (!resiFile || !readFile(resiFile->string().c_str(), resiBytes) || clue::parseResi(resiBytes, resi)) {
        std::fprintf(stderr, "No dialogInfo.res to read in %s\n", gameDir.string().c_str());
        return 1;
    }
    const clue::Fonts fonts(data, resi);

    // Played: the intro skipped; New Game, a game here; Miss Scarlet a
    // person, Colonel Mustard and Mrs. White computer players (normal);
    // Start; the deal skipped; ten seconds of the game; saved as "Reluva"
    // in the first slot from the Esc menu; left; the saved game loaded
    // from the main menu, and ten seconds more; back to the launcher.
    PlayingSystem sys;
    sys.shots = args.shots;
    if (!args.shots.empty())
        std::filesystem::create_directories(args.shots);
    uint64_t t = 1000;
    bool missing = false;
    const auto at = [&](const Event &e, uint64_t wait) {
        sys.inputs.push_back({t, e});
        t += wait;
    };
    const auto click = [&](int dialog, int id, bool overGame = false) {
        const clue::ResiDialog *d = resi.dialog(dialog);
        clue::UiRect r;
        if (d)
            r = clue::DialogView(data, fonts, *d, overGame ? clue::kOverGameX : 0, overGame ? clue::kOverGameY : 0)
                    .rect(id);
        if (r.w <= 0 || r.h <= 0) {
            std::fprintf(stderr, "No button %d in dialog %d\n", id, dialog);
            missing = true;
            return;
        }
        const int x = r.x + r.w / 2;
        const int y = r.y + r.h / 2;
        at(mouse(EventType::kMouseMove, x, y), 20);
        at(mouse(EventType::kMouseDown, x, y), 20);
        at(mouse(EventType::kMouseUp, x, y), 2500);
    };
    at(keyDown(key::kEscape), 2000); // the intro
    click(1, 1);                     // New Game
    click(2, 0);                     // a game here
    click(17, 5);                    // Miss Scarlet: a person
    click(18, 0);
    click(18, 10001);
    click(17, 6); // Colonel Mustard: a computer player
    click(19, 1);
    click(19, 10001);
    click(17, 7); // Mrs. White: a computer player
    click(20, 1);
    click(20, 10001);
    click(17, 29); // Start
    t += 2000;
    at(keyDown(key::kEscape), 10000); // the deal
    at(keyDown(key::kEscape), 1500);  // the Esc menu
    click(35, 0, true);               // Save
    click(36, 2, true);               // the first slot
    Event typed;
    typed.type = EventType::kTextInput;
    std::snprintf(typed.text, sizeof typed.text, "Reluva");
    at(typed, 500);
    click(36, 1, true); // Save
    at(keyDown(key::kEscape), 1500);
    click(35, 3, true); // Exit
    click(44, 10001, true);
    click(1, 3); // Load Game
    click(3, 0); // the first slot
    t += 10000;
    at(Event{EventType::kReturnToLauncher}, 0);
    if (missing)
        return 1;

    const std::filesystem::path saves = std::filesystem::temp_directory_path() / "reluva-clue-game-run";
    std::error_code ec;
    std::filesystem::remove_all(saves, ec);
    GameTarget target{"clue", "clue", found[0].gameId, found[0].description, gameDir, saves, {}};
    std::unique_ptr<Engine> game = engine->create(sys, target);
    const Error err = game->run();
    printRun(sys, err);
    std::printf("saved: %s\n", savesIn(saves).c_str());
    std::filesystem::remove_all(saves, ec);
    return err ? 1 : 0;
}

int cmdViewerRun(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool clue viewer-run <game folder> [-uk] [-shots <folder>]\n");
        return 2;
    }
    const RunArgs args = runArgs(argc, argv);
    const MetaViewer *viewer = findViewer("clue");
    std::vector<DetectedGame> found;
    if (!viewer || !findGame(args.gameDir, args.uk, found))
        return 1;

    // Every page, as reluva-viewer shows it: the mansion scrolled, its whole
    // house and back, its walls; Tab to the videos (the intro playing, then
    // the next), the animations (the next, the next file, a frame on), the
    // images (the next, zoomed, on black), the sounds (the next, played),
    // the text (the next file, scrolled); Tab round to the mansion, and Esc
    // leaves, ending the run.
    PlayingSystem sys;
    sys.shots = args.shots;
    if (!args.shots.empty())
        std::filesystem::create_directories(args.shots);
    uint64_t t = 1000;
    const auto press = [&](uint32_t key, uint64_t wait) {
        sys.inputs.push_back({t, keyDown(key)});
        t += wait;
    };
    press(key::kRight, 300);
    press(key::kDown, 300);
    press('m', 1500);
    press(key::kEscape, 500);
    press('w', 500);
    press(key::kTab, 3000);
    press(key::kRight, 2000);
    press(key::kTab, 1000);
    press(key::kRight, 500);
    press(key::kDown, 500);
    press('.', 500);
    press(key::kTab, 500);
    press(key::kRight, 500);
    press('z', 500);
    press('b', 500);
    press(key::kTab, 500);
    press(key::kRight, 500);
    press(key::kSpace, 1000);
    press(key::kTab, 500);
    press(key::kRight, 500);
    press(key::kDown, 500);
    press(key::kTab, 1000);
    press(key::kEscape, 1000);
    // Esc should have ended it: this only stops a run that didn't.
    sys.inputs.push_back({t + 5000, Event{EventType::kReturnToLauncher}});

    GameTarget target{"clue", "clue", found[0].gameId, found[0].description, args.gameDir, {}, {}};
    std::unique_ptr<Engine> run = viewer->create(sys, target);
    const Error err = run->run();
    printRun(sys, err);
    std::printf("left by: %s\n", sys.getMillis() < t + 5000 ? "Esc" : "nothing: still running");
    return err ? 1 : 0;
}

const Commands kCommands(
    "clue",
    {
        {"game-run", "<game folder> [-uk] [-shots <folder>]",
         "play the game headless (the UK's with -uk): a new game of a person and two computer players, saved, left and loaded again; "
         "a checksum of every screen shown and of the saved game (-shots writes a screen a second)",
         cmdGameRun},
        {"viewer-run", "<game folder> [-uk] [-shots <folder>]",
         "run the data viewer headless (the UK's game with -uk): every page, a few keys on each, then Esc; "
         "a checksum of every screen shown and the window's titles (-shots writes a screen a second)",
         cmdViewerRun},
    });

} // namespace
