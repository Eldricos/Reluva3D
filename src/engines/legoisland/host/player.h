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
#include <utility>
#include <vector>

namespace reluva::audio {
class Mixer;
}

namespace reluva::legoisland {

class GameProcess;

/**
 * LEGO Island played: its program (GameProcess) run on a thread of its own,
 * since its main loop never hands back control. The engine's thread passes
 * it events and takes its frames; every call here is for the engine's
 * thread.
 */
class Player {
public:
    /**
     * What it writes (its saved games) goes to `saveDir`; its sound goes to
     * `mixer`, if there's one, which must outlive the player.
     */
    Player(std::filesystem::path gameDir, std::filesystem::path saveDir, audio::Mixer *mixer = nullptr);
    /** Stops the game and waits for its thread. */
    ~Player();

    /** One of the program's registry settings (GameProcess::setSetting); before start. */
    void setSetting(const std::string &name, const std::string &value) { _settings.emplace_back(name, value); }
    /** Whether the game is told of a joystick, which the input's joystick events move; before start. */
    void setJoystick(bool present) { _joystick = present; }
    /** At most so many frames a second (0: as many as it draws); before start. */
    void setFrameRate(int framesPerSecond) { _frameRate = framesPerSecond; }
    /** Starts the game's thread, which loads and runs the program; call once. */
    void start();
    /**
     * Queues an input event for the game (the pointer where it is in the
     * game's picture, buttons, the wheel, keys); the game takes it at its
     * next frame or when it looks for a message and finds none.
     */
    void input(const Event &event);
    /**
     * The newest frame shown, as 0xAARRGGBB.
     * @return False, leaving the arguments alone, if none was shown since the last asked for.
     */
    bool frame(std::vector<uint32_t> &pixels, int &width, int &height);
    /** How many frames the game has shown so far. */
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
    int _frameRate = 0;
    bool _joystick = false;
    std::vector<std::pair<std::string, std::string>> _settings;
    std::chrono::steady_clock::time_point _nextFrame; // the game thread's
    GameProcess *_game = nullptr;                     // the game thread's, while it runs
    std::thread _thread;
    std::mutex _mutex;
    std::deque<Event> _events;
    std::vector<uint32_t> _frame;
    int _width = 0;
    int _height = 0;
    uint64_t _frameNumber = 0;
    uint64_t _frameTaken = 0;
    bool _stopping = false;
    bool _finished = false;
    bool _ended = false;
    std::string _error;
    std::string _output;
};

} // namespace reluva::legoisland
