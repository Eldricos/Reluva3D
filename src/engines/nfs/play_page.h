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
    // With a GPU, frames are drawn `renderScale` times the game's 640x480.
    // Textures are bilinear-filtered, as the game's renderer DLLs ask for,
    // unless `smoothTextures` is false (point-sampled, GPU and software).
    // With `network`, the game's sockets are the machine's, for its
    // network play over TCP and IPX; `serial` and `modem` (as reluva.ini's
    // "listen:port" or "host:port") make its COM ports and its modem links
    // over TCP too. With `fillMovies`, the black lines the game
    // leaves between a movie's are filled in.
    // A game controller connected at the start is offered to the game as a
    // joystick unless `offerJoystick` is false: a wheel as a wheel, with
    // the forces the game asks for (force feedback, or a pad's rumble)
    // unless `forceFeedback` is false. `wheelPedals` ("accelerator,brake",
    // axis numbers) is for a wheel SDL has no layout for. `glide`: the
    // game's own voodoo2a.dll draws, on Reluva's Glide stand-in
    // (GameHost::Options::glide), rather than THRASH natively: on the GPU
    // when there is one, in software with `glideSoftware`; `glideDll` a
    // THRASH DLL file outside the install to run instead (the CD's Voodoo3
    // patch DLL, on Glide 3); `glideBoard` the board Glide stands in for
    // ("voodoo1" for the install's voodooa.dll; GameHost::Options::glideBoard).
    // `direct3d`: the game is told its renderer is d3da.dll and takes its
    // Direct3D path (no fog, the clouds' constant alpha, the dimming by the
    // effects intensity), drawn natively as d3da's device draws (bilinear,
    // undithered; docs/formats/nfs/hrz.md, The Direct3D renderer).
    PlayPage(GameFolders folders, std::filesystem::path saveFolder, audio::Mixer *mixer, int renderScale = 2,
             bool smoothTextures = true, bool offerJoystick = true, bool forceFeedback = true,
             std::string wheelPedals = {}, bool network = true, bool fillMovies = true, std::string serial = {},
             std::string modem = {}, bool glide = false, bool direct3d = false, bool glideSoftware = false,
             std::string glideDll = {}, std::string glideBoard = {});
    // Network races with positional updates (reluva.ini's
    // positionalupdates=on; GameHost::Options::positionalUpdates). Set
    // before the page is first shown; every player needs the same.
    void setPositionalUpdates(bool on) { positionalUpdates_ = on; }
    ~PlayPage() override;
    const char *name() const override { return "Play"; }
    bool capturesEscape() const override { return true; }
    bool finished() const override { return exited_; }
    void activate(System &sys) override;
    void deactivate() override;
    bool uses3D() const override { return gpu_; }
    void update(uint64_t nowMs) override;
    void handleEvent(const Event &ev) override;
    void draw(Surface &screen) override;
    bool tick(uint64_t nowMs) override;
    // Starts the game, if it isn't running (activate() does this).
    void start();

private:
    void say(const std::string &line);
    // The game controller's state passed on to the game, when it changes.
    void pollGamepad();
    // The forces the game's effects add up to, passed on when they change;
    // stopped when the page is left or the game ends.
    void passForces();
    void stopForces();
    // The title, how the run is going and the log's last lines (mutex_ held).
    void drawStatus(Surface &screen);

    GameFolders folders_;
    std::filesystem::path saveFolder_;
    audio::Mixer *mixer_;
    std::unique_ptr<GameHost> host_;
    std::thread thread_;
    audio::SoundHandle sound_ = audio::kNoSound;
    System *sys_ = nullptr;
    GamepadState pad_; // as last passed on
    bool offerJoystick_ = true;
    bool forceFeedback_ = true;
    bool smoothTextures_ = true;
    bool positionalUpdates_ = false;
    bool network_ = true;        // the game's sockets on the machine's (System::sockets)
    std::string serial_, modem_; // its COM ports' and modem's links (GameHost::Options)
    bool fillMovies_ = true;     // the black lines between a movie's filled in
    std::string wheelPedals_;
    GamepadForce force_; // as last passed on
    bool forceShown_ = false;
    bool padOffered_ = false;               // a joystick offered to the game at its start
    float mouseLeftX_ = 0, mouseLeftY_ = 0; // the mouse's moves not yet passed on (fractions)

    // Shared with the game's thread.
    std::mutex mutex_;
    Surface frame_; // the last frame the game flipped
    bool newFrame_ = false;
    std::deque<std::string> lines_; // the log's last lines, shown until the first frame
    std::atomic<bool> running_{false};
    std::atomic<bool> exited_{false}; // the game ended itself (not failed)
    int renderScale_ = 2;
    bool gpu_ = false;           // drawing with the GPU: decided when the game starts
    bool glide_ = false;         // voodoo2a.dll on the Glide stand-in (not THRASH's GPU drawing)
    bool glideSoftware_ = false; // ... in its software rasterizer rather than its GPU backend
    std::string glideDll_;       // ... with this THRASH DLL file instead of the install's
    std::string glideBoard_;     // ... on this board
    bool direct3d_ = false;      // the game told of d3da.dll (its Direct3D look)
    ThrashGpu gpuDraw_;          // on the engine's thread
    // THRASH's frames not drawn yet, in order: each is drawn (some add to
    // what the one before left), with the textures it brought, then kept.
    struct Pending {
        ThrashFrame frame;
        std::shared_ptr<const Surface> software; // comparing: the software rasterizer's of the same flip
    };
    std::deque<Pending> pending_;
    bool thrashLatest_ = false; // whether the last frame was THRASH's (else frame_)
    // The screen as DirectDraw last left it (a loading screen's picture),
    // shown over THRASH's frames until a page flip covers all of it: clear
    // (alpha 0) where flips have shown THRASH's frame since.
    Surface primary_;
    bool primaryShown_ = false;
    // Comparing the GPU's frames with the software rasterizer's (the
    // folder in RELUVA_NFS_COMPARE): the software frame of the last list.
    std::filesystem::path compareFolder_;
    int compared_ = 0, newFrames_ = 0;
    std::string result_; // how the run ended
    // How fast it goes (reportSpeed): frames the game made and the
    // instructions run (counted on its thread), frames drawn, not needed
    // (a later one covered them), dropped (too many waiting) and shown.
    void reportSpeed(uint64_t nowMs);
    std::atomic<uint64_t> statGameFrames_{0}, statDropped_{0}, statInstructions_{0};
    // Of the instructions, those on the game's main thread and on its timer
    // thread, as of the last frame.
    std::atomic<uint64_t> statOwn_{0}, statTimer_{0};
    uint64_t statLastOwn_ = 0, statLastTimer_ = 0;
    uint64_t statFrom_ = 0, statLastInstructions_ = 0, statDrawn_ = 0, statSkipped_ = 0, statShown_ = 0;
};

} // namespace reluva::nfs
