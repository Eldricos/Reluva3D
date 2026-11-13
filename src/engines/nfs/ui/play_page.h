// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/nfs/folders.h"
#include "engines/nfs/render/thrash_gpu.h"
#include "engines/nfs/ui/page.h"

#include <atomic>
#include <deque>
#include <filesystem>
#include <map>
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
    // undithered; docs/engines/nfs/games/nfs3/formats/hrz.md, The Direct3D renderer).
    PlayPage(GameFolders folders, std::filesystem::path saveFolder, audio::Mixer *mixer, int renderScale = 2,
             bool smoothTextures = true, bool offerJoystick = true, bool forceFeedback = true,
             std::string wheelPedals = {}, bool network = true, bool fillMovies = true, std::string serial = {},
             std::string modem = {}, bool glide = false, bool direct3d = false, bool glideSoftware = false,
             std::string glideDll = {}, std::string glideBoard = {});
    // Network races with positional updates (reluva.ini's
    // positionalupdates=on; GameHost::Options::positionalUpdates). Set
    // before the page is first shown; every player needs the same.
    void setPositionalUpdates(bool on) { _positionalUpdates = on; }
    // The keys named at the foot of the screen shown until the game's
    // first frame.
    void setKeysLine(std::string line) { _keysLine = std::move(line); }
    ~PlayPage() override;
    const char *name() const override { return "Play"; }
    bool capturesEscape() const override { return true; }
    bool finished() const override { return _exited; }
    Error failure() const override;
    void activate(System &sys) override;
    void deactivate() override;
    bool uses3D() const override { return _gpu; }
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

    GameFolders _folders;
    std::filesystem::path _saveFolder;
    audio::Mixer *_mixer;
    std::unique_ptr<GameHost> _host;
    std::thread _thread;
    audio::SoundHandle _sound = audio::kNoSound;
    System *_sys = nullptr;
    GamepadState _pad; // as last passed on
    bool _offerJoystick = true;
    bool _forceFeedback = true;
    bool _smoothTextures = true;
    bool _positionalUpdates = false;
    bool _network = true; // the game's sockets on the machine's (System::sockets)
    // its COM ports' and modem's links (GameHost::Options)
    std::string _serial;
    std::string _modem;
    bool _fillMovies = true; // the black lines between a movie's filled in
    std::string _wheelPedals;
    std::string _keysLine = "Keys and mouse go to the game   Ctrl+F5: launcher";
    GamepadForce _force; // as last passed on
    bool _forceShown = false;
    bool _padOffered = false; // a joystick offered to the game at its start
    // the mouse's moves not yet passed on (fractions)
    float _mouseLeftX = 0;
    float _mouseLeftY = 0;

    // Shared with the game's thread.
    mutable std::mutex _mutex;
    Surface _frame; // the last frame the game flipped
    bool _newFrame = false;
    std::deque<std::string> _lines; // the log's last lines, shown until the first frame
    std::atomic<bool> _running{false};
    std::atomic<bool> _exited{false}; // the game ended itself (not failed)
    int _renderScale = 2;
    bool _gpu = false;           // drawing with the GPU: decided when the game starts
    bool _glide = false;         // voodoo2a.dll on the Glide stand-in (not THRASH's GPU drawing)
    bool _glideSoftware = false; // ... in its software rasterizer rather than its GPU backend
    std::string _glideDll;       // ... with this THRASH DLL file instead of the install's
    std::string _glideBoard;     // ... on this board
    bool _direct3d = false;      // the game told of d3da.dll (its Direct3D look)
    ThrashGpu _gpuDraw;          // on the engine's thread
    // THRASH's frames not drawn yet, in order: each is drawn (some add to
    // what the one before left), with the textures it brought, then kept.
    struct Pending {
        ThrashFrame frame;
        std::shared_ptr<const Surface> software; // comparing: the software rasterizer's of the same flip
    };
    std::deque<Pending> _pending;
    bool _thrashLatest = false; // whether the last frame was THRASH's (else frame_)
    // The screen as DirectDraw last left it (a loading screen's picture),
    // shown over THRASH's frames until a page flip covers all of it: clear
    // (alpha 0) where flips have shown THRASH's frame since.
    Surface _primary;
    bool _primaryShown = false;
    // Comparing the GPU's frames with the software rasterizer's (the
    // folder in RELUVA_NFS_COMPARE): the software frame of the last list.
    std::filesystem::path _compareFolder;
    int _compared = 0;
    int _newFrames = 0;
    std::string _result; // how the run ended
    // How fast it goes (reportSpeed): frames the game made and the
    // instructions run (counted on its thread), frames drawn, not needed
    // (a later one covered them), dropped (too many waiting) and shown.
    void reportSpeed(uint64_t nowMs);
    std::atomic<uint64_t> _statGameFrames{0};
    std::atomic<uint64_t> _statDropped{0};
    std::atomic<uint64_t> _statInstructions{0};
    // Of the instructions, those on the game's main thread and on its timer
    // thread, as of the last frame.
    std::atomic<uint64_t> _statOwn{0};
    std::atomic<uint64_t> _statTimer{0};
    uint64_t _statLastOwn = 0;
    uint64_t _statLastTimer = 0;
    uint64_t _statFrom = 0;
    uint64_t _statLastInstructions = 0;
    uint64_t _statDrawn = 0;
    uint64_t _statSkipped = 0;
    uint64_t _statShown = 0;
};

/**
 * The game's page, set up from `settings`, the game's section of
 * reluva.ini: renderscale (1-4, the frame against the game's 640x480; 2
 * if not set), texturefilter (point: point-sampled textures; bilinear
 * otherwise), renderer (glide, direct3d; THRASH drawn natively
 * otherwise), glide (software), glidedll, glideboard, joystick (off: a
 * game controller isn't offered to the game), forcefeedback (off),
 * wheelpedals, network (off), serial, modem, movielines (keep) and
 * positionalupdates (on: network races with positional updates).
 * @param saveFolder Where the game writes (its "game" folder);
 *        the install's reluva-save if empty.
 * @param mixer Plays the game's sound; null without audio output.
 */
std::unique_ptr<PlayPage> makePlayPage(const GameFolders &folders, const std::filesystem::path &saveFolder,
                                       audio::Mixer *mixer, const std::map<std::string, std::string> &settings);

} // namespace reluva::nfs
