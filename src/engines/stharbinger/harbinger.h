// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/stharbinger/formats/resources.h"
#include "engines/stharbinger/platform.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace reluva::stharbinger {

const int kScreenWidth = 640;
const int kScreenHeight = 480;

// A fault in the game's data that the game can't go on from: thrown by
// fatal(), and returned by the Engine's run() as its Error.
class FatalError : public std::runtime_error {
public:
    explicit FatalError(const std::string &message) : std::runtime_error(message) {}
};

[[noreturn]] void fatal(const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

// How the game runs: the launcher's settings for it (stharbinger.cpp lists
// them, under "Developer" for those that aren't part of the game), or
// reluva-tool's options.
struct Options {
    std::vector<std::string> paths; // the game's folders: an install, a CD or both CDs
    // Developer: start in this scene (a name or CC.ALL slot), with every
    // variable at 0, so later scenes may not behave as in a playthrough.
    // The viewer's one-video mode takes a video in DS9 here instead
    // (INTRO/VNM.SMK).
    std::string startScene;
    // Developer: keys and options that aren't part of the game (F2 hotspots,
    // F10 variables, the scene in the title bar).
    bool debug = false;
    bool original = false; // none of the added conveniences: exactly as the original plays
};

// Star Trek: Deep Space Nine - Harbinger: the game's data, and the game
// (Game, the scene scripts) run on it.
class Harbinger {
public:
    Harbinger(Platform *system, Options options);

    // Runs the game until the player quits; FatalError on a fault in its data.
    void run();
    // Writes what the game data holds (scenes, dialogues, credits) to stdout.
    void printInfo();

    Platform *getSystem() const { return _system; }
    const Options &getOptions() const { return _options; }
    Resources &getResources() { return _resources; }
    const std::vector<Scene> &getScenes() const { return _scenes; }
    bool shouldQuit() const { return _system && _system->quitRequested(); }

    // Index into getScenes() of the scene with this name, SCENE number (as used
    // by NEWSCENE) or, for findScene(), CC.ALL slot number; -1 if none.
    int findScene(const std::string &nameOrSlot) const;
    int findSceneByNumber(int number) const;

    // Opens the game folders and loads the scene scripts (run() and
    // printInfo() call it; tests may call it directly).
    void initResources();

private:
    Platform *_system;
    Options _options;
    Resources _resources;
    std::vector<Scene> _scenes;
};

} // namespace reluva::stharbinger
