// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"

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

namespace reluva::netimmerse::alphateam {

class GameProcess;

/**
 * LEGO Alpha Team played: its program (GameProcess) run on a thread of its
 * own, since its main loop never hands back control. The engine's thread
 * passes it events and takes its frames; every call here is for the
 * engine's thread.
 */
class Player {
public:
    /**
     * What it writes (its settings and progress) goes to `saveDir`; its
     * sound goes to `mixer`, if there's one, which must outlive the player.
     */
    Player(std::filesystem::path gameDir, std::filesystem::path saveDir, audio::Mixer *mixer = nullptr);
    /** Stops the game and waits for its thread. */
    ~Player();

    /** Starts the game's thread, which loads and runs the program; call once. */
    void start();
    /**
     * Queues an input event for the game (mouse movement as relative
     * movement, buttons, the wheel, keys); the game takes it at its next
     * frame or when it looks for a message and finds none.
     */
    void input(const Event &event);
    /**
     * The newest frame shown, as 0xAARRGGBB.
     * @return False, leaving the arguments alone, if none was shown since the last asked for.
     */
    bool frame(std::vector<uint32_t> &pixels, int &width, int &height);
    /** How many frames the game has shown (flipped) so far. */
    uint64_t framesShown();
    /**
     * Whether the program has stopped.
     * @param ended Set to whether it stopped by itself, exiting.
     * @param error Set to why it stopped otherwise; empty if it was stopped by the player.
     */
    bool finished(bool &ended, std::string &error);
    /** What the program has written (its debug messages) since last asked, up to 1 MB. */
    std::string takeOutput();

private:
    void work();
    void drain(); // on the game's thread: applies the queued input

    std::filesystem::path _dir;
    std::filesystem::path _saveDir;
    audio::Mixer *_mixer;
    GameProcess *_game = nullptr; // the game thread's, while it runs
    std::thread _thread;
    std::mutex _mutex;
    std::deque<Event> _events;
    std::vector<uint32_t> _frame;
    int _width = 0;
    int _height = 0;
    uint64_t _frameNumber = 0;
    uint64_t _frameTaken = 0;
    std::chrono::steady_clock::time_point _nextFlip{}; // the next vertical blank a flip may show at
    bool _stopping = false;
    bool _finished = false;
    bool _ended = false;
    std::string _error;
    std::string _output;
    // fractions of a pixel of movement not passed on yet
    float _pendingX = 0;
    float _pendingY = 0;
};

} // namespace reluva::netimmerse::alphateam
