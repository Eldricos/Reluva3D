// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/system.h"

#include <cstdint>
#include <deque>

// reluva-tool's AWE engine commands: the command's exit code, or -1 if
// argv[1] isn't one of them.
int aweToolCommand(int argc, char** argv);
// reluva-tool awe-check: every Granny file, scene and text file of an
// install read, failures reported (awe_check.cpp).
int aweCheckCommand(int argc, char** argv);
// Their usage lines.
const char* aweToolUsage();

namespace reluva::awe {

// A System with no window: the last frame shown is kept, events come from
// a queue.
class HeadlessSystem : public System {
public:
    Surface last;
    std::deque<Event> events;
    Error initGraphics(int, int, PixelFormat) override { return Error::success(); }
    void setPalette(const Color*, int, int) override {}
    void updateScreen(const Surface& s) override { last = s; }
    void setWindowTitle(const std::string&) override {}
    bool pollEvent(Event& ev) override {
        if (events.empty())
            return false;
        ev = events.front();
        events.pop_front();
        return true;
    }
    uint64_t getMillis() const override { return now; }
    uint64_t now = 0;
    void delayMillis(uint32_t) override {}
    Error startAudio(AudioCallback) override { return Error::fail("no audio"); }
    void stopAudio() override {}
    int audioRate() const override { return 44100; }
    // 3D from another System (a window's), if given.
    System* threeD = nullptr;
    Error init3D(int width, int height, gfx::Renderer3D*& out) override {
        if (!threeD) {
            out = nullptr;
            return Error::fail("no 3D");
        }
        return threeD->init3D(width, height, out);
    }
};

// reluva-tool awe-path: the game's path between two cells beside Reluva's
// (awe_path.cpp).
int aweCommandPath(int argc, char** argv);
// reluva-tool awe-scripts: every script command of an install, by verb
// (awe_scripts.cpp).
int aweCommandScripts(int argc, char** argv);

// reluva-tool awe-acts -solve and -play: a playthrough searched for, and
// one played back (awe_play.cpp).
int aweCommandSolve(int argc, char** argv);
} // namespace reluva::awe
