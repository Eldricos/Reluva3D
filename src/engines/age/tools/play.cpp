// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's age commands that run the game and its data viewer: each
// as reluva and reluva-viewer run it, on a System with no window whose
// clock moves only as it waits, so a run is the same on every machine.
// Without a window there is no 3D renderer, so the race screen and the 3D
// pages say they need one; the rest is drawn. Used by the game-files test
// (tests/engines/age/gamedata.cmake).

#include "common/headless_system.h"
#include "devtools/tool.h"
#include "engines/registry.h"
#include "engines/viewer_registry.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
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

// The headless System with a player: keys at given times. It keeps a
// checksum of every screen shown and the window's titles in turn.
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

    void updateScreen(const Surface &s) override {
        HeadlessSystem::updateScreen(s);
        screens.bytes(s.pixels.data(), s.pixels.size());
        shown++;
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
};

Event keyDown(uint32_t k) {
    Event e;
    e.type = EventType::kKeyDown;
    e.key = k;
    return e;
}

// The game in the folder, printed; false, said, if there's none.
bool findGame(const std::filesystem::path &dir, std::vector<DetectedGame> &found) {
    const MetaEngine *engine = findEngine("age");
    if (engine)
        found = engine->detect(dir);
    if (found.empty()) {
        std::fprintf(stderr, "No Midtown Madness 2 in %s\n", dir.string().c_str());
        return false;
    }
    std::printf("game: %s %s\n", found[0].gameId.c_str(), found[0].description.c_str());
    return true;
}

// How a run ended, every screen's checksum and the window's titles.
void printRun(const PlayingSystem &sys, const Error &err) {
    std::printf("result: %s\n", err ? ("error: " + err.message).c_str() : "ok");
    std::printf("screens: %d %s, %.1f s\n", sys.shown, sys.screens.hex().c_str(), double(sys.getMillis()) / 1000);
    std::string titles;
    for (const std::string &title : sys.titles)
        titles += (titles.empty() ? "" : " / ") + title;
    std::printf("titles: %s\n", titles.c_str());
}

// Runs what create() makes with the keys given, half a second apart, then
// says whether Esc ended it.
int playKeys(const std::filesystem::path &gameDir, bool viewer, const std::vector<uint32_t> &keys) {
    std::vector<DetectedGame> found;
    if (!findGame(gameDir, found))
        return 1;
    PlayingSystem sys;
    uint64_t t = 1000;
    for (const uint32_t k : keys) {
        sys.inputs.push_back({t, keyDown(k)});
        t += 500;
    }
    // Esc should have ended it: this only stops a run that didn't.
    sys.inputs.push_back({t + 5000, Event{EventType::kReturnToLauncher}});
    const std::filesystem::path save = std::filesystem::temp_directory_path() / "reluva-age-run";
    std::filesystem::remove_all(save);
    GameTarget target{"mm2", "age", found[0].gameId, found[0].description, gameDir, save, {}};
    std::unique_ptr<Engine> run;
    if (viewer) {
        const MetaViewer *v = findViewer("age");
        if (!v) {
            std::fprintf(stderr, "age's viewer isn't in this build\n");
            return 1;
        }
        run = v->create(sys, target);
    } else {
        run = findEngine("age")->create(sys, target);
    }
    const Error err = run->run();
    std::filesystem::remove_all(save);
    printRun(sys, err);
    std::printf("left by: %s\n", sys.getMillis() < t + 5000 ? "Esc" : "nothing: still running");
    return err ? 1 : 0;
}

int cmdGameRun(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool age game-run <game folder>\n");
        return 2;
    }
    // The opening movie skipped, the race screen (which needs 3D) left.
    return playKeys(argv[2], false, {key::kEscape, key::kEscape});
}

int cmdViewerRun(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool age viewer-run <game folder>\n");
        return 2;
    }
    // Past the 3D pages to the textures, three of them; the files, three
    // down, one opened and closed; round, and Esc.
    return playKeys(argv[2], true,
                    {key::kTab, key::kTab, key::kTab, key::kTab, key::kRight, key::kRight, key::kRight, key::kTab,
                     key::kDown, key::kDown, key::kDown, key::kReturn, key::kEscape, key::kTab, key::kEscape});
}

const Commands kCommands("age",
                         {
                             {"game-run", "<game folder>",
                              "run the game headless: the opening skipped and the race screen left; a checksum of "
                              "every screen shown and the window's titles",
                              cmdGameRun},
                             {"viewer-run", "<game folder>",
                              "run the data viewer headless: its pages in turn, a few keys on the textures and the "
                              "files, then Esc; a checksum of every screen shown and the window's titles",
                              cmdViewerRun},
                         });

} // namespace
