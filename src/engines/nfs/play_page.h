// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/nfs/folders.h"
#include "engines/nfs/thrash_gpu.h"
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
// to the game too: Ctrl+F5 goes back to the launcher. With a GPU, what the
// game draws through THRASH is drawn with it (thrash_gpu.h); without, in
// software into the frames shown.
class PlayPage : public Page {
public:
    // With a GPU, frames are drawn `renderScale` times the game's 640x480,
    // with bilinear filtering if `smoothTextures` (else point-sampled, as
    // the software renderer).
    PlayPage(GameFolders folders, std::filesystem::path saveFolder, audio::Mixer* mixer, int renderScale = 2,
             bool smoothTextures = false);
    ~PlayPage() override;
    const char* name() const override { return "Play"; }
    bool capturesEscape() const override { return true; }
    void activate(System& sys) override;
    void deactivate() override;
    bool uses3D() const override { return gpu_; }
    void update(uint64_t nowMs) override;
    void handleEvent(const Event& ev) override;
    void draw(Surface& screen) override;
    bool tick(uint64_t nowMs) override;
    // Starts the game, if it isn't running (activate() does this).
    void start();

private:
    void say(const std::string& line);
    // The title, how the run is going and the log's last lines (mutex_ held).
    void drawStatus(Surface& screen);

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
    int renderScale_ = 2;
    bool gpu_ = false; // drawing with the GPU: decided when the game starts
    ThrashGpu gpuDraw_; // on the engine's thread
    std::shared_ptr<const ThrashFrame> thrashFrame_; // the last THRASH frame, its textures taken out
    ThrashFrame textureUpdates_;                     // textures it and those before brought, not applied yet
    bool thrashLatest_ = false;                      // whether the last frame was THRASH's (else frame_)
    std::string result_; // how the run ended
};

} // namespace reluva::nfs
