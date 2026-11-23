// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "emulation/x86/dplay.h"
#include "emulation/x86/gdi.h"
#include "emulation/x86/win32.h"
#include "graphics/cursor.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace reluva::audio {
class Mixer;
}

namespace reluva::legoloco {

class GameProcess;

/**
 * LEGO Loco played: its program (GameProcess) run on a thread of its own,
 * as its main loop never hands back control. The engine's thread passes it
 * events and takes its frames; every call here is for the engine's thread
 * (docs/engines/legoloco/systems/running.md).
 */
class Player {
public:
    /**
     * What it writes (its settings, its layouts) goes to `saveDir`; its
     * sound to `mixer`, if there's one, which must outlive the player; its
     * network games are played on `network`, if there's one.
     */
    Player(std::filesystem::path gameDir, std::filesystem::path saveDir, audio::Mixer *mixer = nullptr,
           std::shared_ptr<x86::DirectPlay::Network> network = nullptr);
    /** Stops the game and waits for its thread. */
    ~Player();

    /** The desktop the game is given, in pixels: it fills it. Before start(). */
    void setDesktop(int width, int height) { _desktopWidth = width, _desktopHeight = height; }
    /**
     * How Windows looks to it: its system colours, and whether its text is
     * drawn in the player's own Arial where it's installed (else Liberation
     * Sans). Before start().
     */
    void setLook(x86::Gdi::ColourScheme colours, bool ownArial) { _colours = colours, _ownArial = ownArial; }
    /**
     * Shows the player a dialog for the game's Open or Save (a postcard's
     * attachment), on the engine's thread, in serve(); `done` is called
     * once, with the file picked or nothing, from any thread. The game
     * waits meanwhile. Unset, its dialogs are cancelled. Before start().
     */
    std::function<void(const x86::Win32Process::FileRequest &request,
                       std::function<void(std::optional<std::filesystem::path>)> done)>
        chooseFile;
    /**
     * Asks the player whether to open a web page the game wants opened,
     * and opens it if so, on the engine's thread, in serve(). Unset, none
     * is. Before start().
     */
    std::function<bool(const std::string &url)> openUrl;
    /** Does what the game has asked of the engine's thread (its dialogs): call it often. */
    void serve();
    /** Starts the game's thread, which loads and runs the program; call once. */
    void start();
    /**
     * Queues an input event for the game: the pointer's place on its
     * screen, buttons, the wheel and keys. The game takes it when it looks
     * for a message and finds none, or at its next frame.
     */
    void input(const Event &event);
    /**
     * The newest frame shown, as 0xAARRGGBB.
     * @return False, leaving the arguments alone, if none was shown since the last asked for.
     */
    bool frame(std::vector<uint32_t> &pixels, int &width, int &height);
    /**
     * Windows' pointer, which the game shows over its own at times (the busy
     * pointer while it saves).
     * @param cursor Set to its picture, when it's shown.
     * @param shown Set to whether it's shown.
     * @return False, leaving the arguments alone, if it hasn't changed since last asked.
     */
    bool pointer(Cursor &cursor, bool &shown);
    /** How many frames the game has shown so far. */
    uint64_t framesShown();
    /**
     * Whether the program has stopped.
     * @param ended Set to whether it stopped by itself, exiting.
     * @param error Set to why it stopped otherwise; empty if it was stopped by the player.
     */
    bool finished(bool &ended, std::string &error);
    /** What the program has written (the stand-ins' reports) since last asked, up to 1 MB. */
    std::string takeOutput();

private:
    void work();
    void drain();       // on the game's thread: passes on the input queued
    void takePointer(); // on the game's thread: keeps Windows' pointer for the engine
    template<typename T>
    std::optional<T> ask(std::function<void(std::function<void(T)>)> job); // on the game's thread: waits for serve()

    std::filesystem::path _dir;
    std::filesystem::path _saveDir;
    audio::Mixer *_mixer;
    std::shared_ptr<x86::DirectPlay::Network> _network;
    int _desktopWidth = 1024; // as the game was played: its boards fit it
    int _desktopHeight = 768;
    x86::Gdi::ColourScheme _colours = x86::Gdi::ColourScheme::kWindows98;
    bool _ownArial = false;
    GameProcess *_game = nullptr; // the game thread's, while it runs
    std::thread _thread;
    std::mutex _mutex;
    std::deque<Event> _events;
    std::deque<std::function<void()>> _jobs; // for serve()
    std::vector<uint32_t> _frame;
    int _width = 0;
    int _height = 0;
    // Where the frame's corner is on the game's screen: the engine's mouse
    // is in the frame's pixels. The game thread's.
    int _frameX = 0;
    int _frameY = 0;
    const Cursor *_pointerNow = nullptr; // the game thread's: what it last kept
    Cursor _pointer;
    bool _pointerShown = false;
    uint64_t _pointerNumber = 0;
    uint64_t _pointerTaken = 0;
    uint64_t _frameNumber = 0;
    uint64_t _frameTaken = 0;
    bool _stopping = false;
    bool _finished = false;
    bool _ended = false;
    std::string _error;
    std::string _output;
};

} // namespace reluva::legoloco
