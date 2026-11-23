// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace reluva::net {
class Sockets;
}

namespace reluva::audio {
class Mixer;
}

namespace reluva::sfc {

class GameProcess;

/**
 * Starfleet Command II played: its StarFleet2.exe (GameProcess) on a
 * thread of its own, as its main loop never hands back control. The
 * engine's thread passes it events and takes its frames; every call here
 * is for the engine's thread.
 */
class Player {
public:
    /**
     * What the program writes in its folder goes to `saveDir`; its sound
     * goes to `mixer`, if there's one, which must outlive the player.
     */
    Player(std::filesystem::path gameDir, std::filesystem::path saveDir, audio::Mixer *mixer = nullptr);
    /** Stops the game and waits for its thread. */
    ~Player();

    /**
     * The network its multiplayer games are played over (DirectPlay's
     * TCP/IP), and the local networks' broadcast addresses; before start().
     */
    void setNetwork(std::shared_ptr<net::Sockets> sockets, std::vector<uint32_t> broadcast) {
        _sockets = std::move(sockets);
        _broadcast = std::move(broadcast);
    }
    /** Starts the game's thread, which loads and runs the program; call once. */
    void start();
    /**
     * Queues an input event for the game: the pointer where the mouse is
     * on the game's screen, its buttons and wheel, and keys. The game takes
     * them at its next frame, or when it looks for a message and finds none.
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
    std::shared_ptr<net::Sockets> _sockets;
    std::vector<uint32_t> _broadcast;
    GameProcess *_game = nullptr; // the game thread's, while it runs
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

} // namespace reluva::sfc
