// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace reluva::netimmerse::alphateam {

class GameProcess;

// LEGO Alpha Team played: its program (GameProcess) run on a thread of its
// own, since its main loop never hands back control. The engine's thread
// passes it events and takes its frames; every call here is for the
// engine's thread.
class Player {
public:
    explicit Player(std::filesystem::path gameDir);
    ~Player(); // stops the game and waits for it

    void start();
    // An input event for the game (mouse movement as relative movement,
    // buttons, the wheel, keys).
    void input(const Event& event);
    // The newest frame shown, as 0xAARRGGBB, if there's one since the last
    // asked for.
    bool frame(std::vector<uint32_t>& pixels, int& width, int& height);
    // Holds the game at its next frame (or lets it go on).
    void pause(bool paused);
    // Whether the program has stopped: by itself (`ended`) or with an error
    // (`error`).
    bool finished(bool& ended, std::string& error);
    // What the program has written (its debug messages), since last asked.
    std::string takeOutput();

private:
    void work();
    void drain(); // on the game's thread: applies the queued input

    std::filesystem::path dir_;
    GameProcess* game_ = nullptr; // the game thread's, while it runs
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable resume_;
    std::deque<Event> events_;
    std::vector<uint32_t> frame_;
    int width_ = 0, height_ = 0;
    uint64_t frameNumber_ = 0, frameTaken_ = 0;
    bool paused_ = false, stopping_ = false, finished_ = false, ended_ = false;
    std::string error_, output_;
    float pendingX_ = 0, pendingY_ = 0; // fractions of a pixel of movement not passed on yet
};

} // namespace reluva::netimmerse::alphateam
