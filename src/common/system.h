// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "graphics/cursor.h"
#include "common/events.h"
#include "graphics/gfx3d.h"
#include "common/net.h"
#include "common/types.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace reluva {

// How a game's picture is shown in the window. The defaults are Reluva's
// own: the window kept as it is, the picture letterboxed into it at any
// size with sharp pixels, waiting for the display's refresh. Set from the
// display settings (settings.h) by the frontend before a game starts.
struct DisplayOptions {
    static constexpr int kAutoScale = -1;
    bool fullscreen = false;     // start in full screen (Alt+Enter still switches)
    int windowScale = 0;         // the window as a multiple of the game's size; 0: as it is;
                                 // kAutoScale: 2 where it fits, else 1
    bool integerScaling = false; // whole multiples only, with borders
    bool smooth = false;         // smoothed (linear) instead of sharp pixels, when scaled
    bool vsync = true;           // wait for the display's refresh

    bool operator==(const DisplayOptions &) const = default;
};

// Everything an engine may ask of the platform. Engines must never include
// SDL (or any other backend) headers; they go through this interface so the
// backend can be swapped or extended (GPU rendering, consoles, web...).
class System {
public:
    virtual ~System() = default;

    // --- Graphics -----------------------------------------------------------
    // Set up a game framebuffer of the given size. It is scaled to the window.
    virtual Error initGraphics(int width, int height, PixelFormat format) = 0;
    virtual void setPalette(const Color *colors, int start, int count) = 0;
    // Copy the surface to the screen and present it.
    virtual void updateScreen(const Surface &surface) = 0;
    virtual void setWindowTitle(const std::string &title) = 0;

    // --- 3D graphics (optional) ----------------------------------------------
    // Switch to GPU rendering of width x height frames, scaled to the window
    // like the framebuffer. `out` stays valid, and its textures and meshes
    // alive, until the next initGraphics() or init3D() or the engine ends;
    // updateScreen() does nothing meanwhile. Fails (out = nullptr) when the
    // backend or the machine can't render 3D.
    virtual Error init3D(int width, int height, gfx::Renderer3D *&out) {
        (void)width;
        (void)height;
        out = nullptr;
        return Error::fail("3D rendering is not available");
    }

    // --- Input --------------------------------------------------------------
    // Returns false when no more events are pending.
    virtual bool pollEvent(Event &event) = 0;
    // Whether typed text arrives as TextInput events (off by default).
    virtual void setTextInput(bool on) { (void)on; }
    // Whether the system's mouse pointer shows over the game (for games
    // that draw their own).
    virtual void setMouseCursorVisible(bool visible) { (void)visible; }
    // The game's own mouse pointer (graphics/cursor.h) in place of the
    // system's; nullptr goes back to the system's. Shown while the pointer
    // is (setMouseCursorVisible), over the game's 2D or 3D picture, scaled
    // as the picture is; the next initGraphics() or init3D() keeps it.
    // Backends without a pointer ignore it.
    virtual void setMouseCursor(const Cursor *cursor) { (void)cursor; }
    // Whether the mouse is held by the window, hidden, for looking around:
    // MouseMove events then carry only how far it moved (moveX, moveY).
    virtual void setRelativeMouse(bool on) { (void)on; }
    // The window for a game's video mode: the whole screen, or a window of
    // width x height (when it fits on the screen; the frame is letterboxed
    // in whatever size the window is).
    virtual void setVideoMode(int width, int height, bool fullscreen) { (void)width, (void)height, (void)fullscreen; }
    // After a Quit event (or, with `toLauncher`, a ReturnToLauncher one):
    // asks the player whether to leave after all, showing `text`. True to
    // leave; false keeps the game going and the event is forgotten. Without
    // a way to ask, leaves.
    virtual bool confirmQuit(const std::string &text, bool toLauncher = false) {
        (void)text, (void)toLauncher;
        return true;
    }
    // How the picture is shown (the player's display settings): taken up by
    // the next initGraphics() or init3D(); vsync at once. Backends without a
    // window ignore it.
    virtual void setDisplayOptions(const DisplayOptions &options) { (void)options; }

    // --- Time ---------------------------------------------------------------
    virtual uint64_t getMillis() const = 0;
    virtual void delayMillis(uint32_t ms) = 0;

    // --- Audio --------------------------------------------------------------
    // The callback runs on the audio thread and must fill `frames` stereo
    // frames of interleaved signed 16-bit samples at audioRate().
    using AudioCallback = std::function<void(int16_t *out, int frames)>;
    virtual Error startAudio(AudioCallback callback) = 0;
    virtual void stopAudio() = 0;
    virtual int audioRate() const = 0;

    // --- Network (optional) --------------------------------------------------
    // A UDP socket on `port` (0: any), for games played over a network;
    // fails when the backend has none.
    virtual Error openUdp(uint16_t port, std::unique_ptr<net::UdpSocket> &out) {
        (void)port;
        out = nullptr;
        return Error::fail("The network is not available");
    }
    // A host's name (or a.b.c.d) as its IPv4 address; false if not found.
    virtual bool resolveHost(const std::string &host, uint32_t &ip) {
        (void)host;
        (void)ip;
        return false;
    }
    // The broadcast address of each network the machine is on (255.255.255.255
    // reaches only one of them when there are several).
    virtual std::vector<uint32_t> broadcastAddresses() { return {}; }
};

} // namespace reluva
