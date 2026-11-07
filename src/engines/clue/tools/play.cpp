// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Clue command that plays the game itself: the engine run as
// the launcher runs it, on a System with no window whose clock moves only
// as the game waits, so a run is the same on every machine. It clicks the
// game's own buttons where dialogInfo.res puts them. Used by the
// game-files test (tests/engines/clue/gamedata.cmake).

#include "common/headless_system.h"
#include "common/image.h"
#include "engines/clue/frontend.h"
#include "engines/clue/game_data.h"
#include "engines/clue/resi.h"
#include "engines/clue/rules.h"
#include "engines/clue/ui.h"
#include "engines/registry.h"
#include "tools/tool.h"

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
    void bytes(const uint8_t* p, size_t n) {
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

    void updateScreen(const Surface& s) override {
        HeadlessSystem::updateScreen(s);
        screens.bytes(s.pixels.data(), s.pixels.size());
        shown++;
        if (!shots.empty() && getMillis() / 1000 != lastShot_) {
            lastShot_ = getMillis() / 1000;
            image::Image img;
            img.w = s.w;
            img.h = s.h;
            img.rgba = s.pixels;
            const std::vector<uint8_t> png = image::encodePng(img);
            char name[32];
            std::snprintf(name, sizeof name, "%06llu.png", static_cast<unsigned long long>(getMillis()));
            std::ofstream(shots / name, std::ios::binary)
                .write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
        }
    }
    void setWindowTitle(const std::string& t) override {
        if (titles.empty() || titles.back() != t)
            titles.push_back(t);
    }
    bool pollEvent(Event& ev) override {
        if (next_ < inputs.size() && inputs[next_].at <= getMillis()) {
            ev = inputs[next_++].event;
            return true;
        }
        return false;
    }

private:
    size_t next_ = 0;
    uint64_t lastShot_ = ~uint64_t(0);
};

Event keyDown(uint32_t k) {
    Event e;
    e.type = EventType::KeyDown;
    e.key = k;
    return e;
}

Event mouse(EventType type, int x, int y) {
    Event e;
    e.type = type;
    e.button = MouseButton::Left;
    e.mouseX = x;
    e.mouseY = y;
    e.clicks = type == EventType::MouseDown ? 1 : 0;
    return e;
}

// The saved games written: each one's name, and a checksum of the game.
std::string savesIn(const std::filesystem::path& dir) {
    std::string out;
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && it->path().extension() == ".sav")
            files.push_back(it->path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        std::ifstream in(f, std::ios::binary);
        std::string tag, name;
        std::getline(in, tag);
        clue::saveio::getString(in, name);
        const std::vector<uint8_t> rest((std::istreambuf_iterator<char>(in)), {});
        Hash h;
        h.bytes(rest.data(), rest.size());
        out += (out.empty() ? "" : ", ") + f.filename().string() + " \"" + name + "\" " + h.hex();
    }
    return out.empty() ? "(none)" : out;
}

int cmdGameRun(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool clue game-run <game folder> [-shots <folder>]\n");
        return 2;
    }
    const std::filesystem::path gameDir = argv[2];
    std::filesystem::path shots;
    for (int i = 3; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "-shots") == 0)
            shots = argv[++i];

    const MetaEngine* engine = findEngine("clue");
    const std::vector<DetectedGame> found = engine ? engine->detect(gameDir) : std::vector<DetectedGame>{};
    if (found.empty()) {
        std::fprintf(stderr, "No Clue in %s\n", gameDir.string().c_str());
        return 1;
    }
    std::printf("game: %s %s\n", found[0].gameId.c_str(), found[0].description.c_str());

    // Where the game's buttons are: its dialogs as the game shows them.
    clue::GameData data;
    if (Error err = data.open(gameDir, found[0].gameId == "clue-uk" ? clue::Language::UK : clue::Language::US)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    clue::Resi resi;
    const std::filesystem::path* resiFile = data.find("dialogInfo.res");
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
    sys.shots = shots;
    if (!shots.empty())
        std::filesystem::create_directories(shots);
    uint64_t t = 1000;
    bool missing = false;
    const auto at = [&](const Event& e, uint64_t wait) {
        sys.inputs.push_back({t, e});
        t += wait;
    };
    const auto click = [&](int dialog, int id, bool overGame = false) {
        const clue::ResiDialog* d = resi.dialog(dialog);
        clue::UiRect r;
        if (d)
            r = clue::DialogView(data, fonts, *d, overGame ? clue::kOverGameX : 0, overGame ? clue::kOverGameY : 0)
                    .rect(id);
        if (r.w <= 0 || r.h <= 0) {
            std::fprintf(stderr, "No button %d in dialog %d\n", id, dialog);
            missing = true;
            return;
        }
        const int x = r.x + r.w / 2, y = r.y + r.h / 2;
        at(mouse(EventType::MouseMove, x, y), 20);
        at(mouse(EventType::MouseDown, x, y), 20);
        at(mouse(EventType::MouseUp, x, y), 2500);
    };
    at(keyDown(key::Escape), 2000); // the intro
    click(1, 1);                    // New Game
    click(2, 0);                    // a game here
    click(17, 5);                   // Miss Scarlet: a person
    click(18, 0);
    click(18, 10001);
    click(17, 6);                   // Colonel Mustard: a computer player
    click(19, 1);
    click(19, 10001);
    click(17, 7);                   // Mrs. White: a computer player
    click(20, 1);
    click(20, 10001);
    click(17, 29);                  // Start
    t += 2000;
    at(keyDown(key::Escape), 10000); // the deal
    at(keyDown(key::Escape), 1500);  // the Esc menu
    click(35, 0, true);              // Save
    click(36, 2, true);              // the first slot
    Event typed;
    typed.type = EventType::TextInput;
    std::snprintf(typed.text, sizeof typed.text, "Reluva");
    at(typed, 500);
    click(36, 1, true); // Save
    at(keyDown(key::Escape), 1500);
    click(35, 3, true); // Exit
    click(44, 10001, true);
    click(1, 3); // Load Game
    click(3, 0); // the first slot
    t += 10000;
    at(Event{EventType::ReturnToLauncher}, 0);
    if (missing)
        return 1;

    const std::filesystem::path saves = std::filesystem::temp_directory_path() / "reluva-clue-game-run";
    std::error_code ec;
    std::filesystem::remove_all(saves, ec);
    GameTarget target{"clue", "clue", found[0].gameId, found[0].description, gameDir, saves, {}};
    std::unique_ptr<Engine> game = engine->create(sys, target);
    const Error err = game->run();
    std::printf("result: %s\n", err ? ("error: " + err.message).c_str() : "ok");
    std::printf("screens: %d %s, %.1f s\n", sys.shown, sys.screens.hex().c_str(), double(sys.getMillis()) / 1000);
    std::string titles;
    for (const std::string& title : sys.titles)
        titles += (titles.empty() ? "" : " / ") + title;
    std::printf("titles: %s\n", titles.c_str());
    std::printf("saved: %s\n", savesIn(saves).c_str());
    std::filesystem::remove_all(saves, ec);
    return err ? 1 : 0;
}

const Commands commands("clue", {
    {"game-run", "<game folder> [-shots <folder>]",
     "play the game headless: a new game of a person and two computer players, saved, left and loaded again; "
     "a checksum of every screen shown and of the saved game (-shots writes a screen a second)",
     cmdGameRun},
});

} // namespace
