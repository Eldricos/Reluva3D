// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "emulation/x86/dplay.h"
#include "emulation/x86/joystick.h"
#include "engines/sith/folders.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace reluva::audio {
class Mixer;
}

namespace reluva::sith {

class GameProcess;

/**
 * Jedi Knight played: JK.EXE (GameProcess) run on a thread of its own,
 * since its main loop never hands back control. The engine's thread passes
 * it events and takes its frames; every call here is for the engine's
 * thread.
 */
class Player {
public:
    /**
     * What the game writes (its players, saves and settings) goes to
     * `saveFolder`; its sound card plays through `mixer`, if there's one,
     * which must outlive the player; its multiplayer sessions over
     * `network`, if it has one.
     */
    Player(GameFolders folders, std::filesystem::path saveFolder, audio::Mixer *mixer = nullptr,
           x86::DirectPlay::Network network = {});
    /** Stops the game and waits for its thread. */
    ~Player();

    /** Starts the game's thread, which loads and runs the program; call once. */
    void start();
    /**
     * Queues an input event for the game: keys; the mouse's place in the
     * frame while the game shows its pointer, its movement while it hides
     * it (in play, where it turns the view); buttons. The game takes them
     * at its next frame or when it looks for a message and finds none.
     */
    void input(const Event &event);
    /**
     * Game controller `index` (0 or 1) now, offered to the game as that
     * joystick (x86::Joysticks::fromGamepad); nullptr when none is
     * connected. Any thread; the game sees it at its next frame.
     */
    void gamepad(int index, const GamepadState *pad);
    /**
     * The newest frame shown, as 0xAARRGGBB.
     * @return False, leaving the arguments alone, if none was shown since the last asked for.
     */
    bool frame(std::vector<uint32_t> &pixels, int &width, int &height);
    /** How many frames the game has shown so far. */
    uint64_t framesShown();
    /** Whether the game shows the mouse pointer: the menus do, play doesn't. */
    bool pointerShown();
    /**
     * Whether the program has stopped.
     * @param ended Set to whether it stopped by itself, exiting.
     * @param error Set to why it stopped otherwise; empty if it was stopped by the player.
     */
    bool finished(bool &ended, std::string &error);
    /** What the program has written for a debugger since last asked, up to 1 MB. */
    std::string takeOutput();

private:
    void work();
    void drain();                      // on the game's thread: applies the queued input
    void offerPads(GameProcess &game); // on the game's thread: the controllers as they are now

    GameFolders _folders;
    std::filesystem::path _saveFolder;
    audio::Mixer *_mixer;
    x86::DirectPlay::Network _network;
    GameProcess *_game = nullptr; // the game thread's, while it runs
    std::thread _thread;
    std::mutex _mutex;
    std::deque<Event> _events;
    std::vector<uint32_t> _frame;
    int _width = 0;
    int _height = 0;
    uint64_t _frameNumber = 0;
    uint64_t _frameTaken = 0;
    bool _pointerShown = true;
    std::chrono::steady_clock::time_point _nextFlip{}; // the next vertical blank a flip may show at
    bool _stopping = false;
    bool _finished = false;
    bool _ended = false;
    std::string _error;
    std::string _output;
    // fractions of a pixel of movement not passed on yet
    float _pendingX = 0;
    float _pendingY = 0;
    // Each controller to offer, and what the game was last given.
    struct Pad {
        bool connected = false;
        GamepadState state;
        bool given = false;
        std::string givenName;
    };
    std::array<Pad, x86::Joysticks::kConnectable> _pads;
};

} // namespace reluva::sith
