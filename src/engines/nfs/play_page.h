// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/nfs/folders.h"
#include "engines/nfs/viewers.h"

#include <atomic>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace reluva::nfs {

class GameHost;

// The game itself: the retail nfs3.exe in the x86 interpreter
// (host/game_host.h), started when the page is first shown and running
// on a thread of its own from then on. Its frames are shown here, its
// sound goes to the mixer, and the keyboard and mouse go to it. Esc goes
// to the game too: Ctrl+F5 goes back to the launcher.
class PlayPage : public Page {
public:
    PlayPage(GameFolders folders, std::filesystem::path saveFolder, audio::Mixer* mixer);
    ~PlayPage() override;
    const char* name() const override { return "Play"; }
    bool capturesEscape() const override { return true; }
    void activate(System& sys) override;
    void handleEvent(const Event& ev) override;
    void draw(Surface& screen) override;
    bool tick(uint64_t nowMs) override;
    // Starts the game, if it isn't running (activate() does this).
    void start();

private:
    void say(const std::string& line);

    GameFolders folders_;
    std::filesystem::path saveFolder_;
    audio::Mixer* mixer_;
    std::unique_ptr<GameHost> host_;
    std::thread thread_;
    audio::SoundHandle sound_ = audio::kNoSound;
    bool mouseKnown_ = false;
    int mouseX_ = 0, mouseY_ = 0;

    // Shared with the game's thread.
    std::mutex mutex_;
    Surface frame_; // the last frame the game flipped
    bool newFrame_ = false;
    std::deque<std::string> lines_; // the log's last lines, shown until the first frame
    std::atomic<bool> running_{false};
    std::string result_; // how the run ended
};

} // namespace reluva::nfs
