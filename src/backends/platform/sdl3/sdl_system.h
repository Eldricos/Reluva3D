// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/system.h"

#include <SDL3/SDL.h>

#include <memory>
#include <optional>
#include <vector>

namespace reluva {

class SdlGpuRenderer;

class SdlSystem : public System {
public:
    SdlSystem();
    ~SdlSystem() override;
    SdlSystem(const SdlSystem &) = delete;
    SdlSystem &operator=(const SdlSystem &) = delete;

    Error init();

    // Used by the launcher, which draws with ImGui directly on SDL.
    SDL_Window *window() const { return window_; }
    SDL_Renderer *renderer() const { return renderer_; }
    // Drop the game framebuffer and return the window to launcher mode.
    void resetForLauncher();
    // True once the user closed the window during a game.
    bool quitRequested() const { return quitRequested_; }

    // System
    Error initGraphics(int width, int height, PixelFormat format) override;
    void setPalette(const Color *colors, int start, int count) override;
    void updateScreen(const Surface &surface) override;
    void setWindowTitle(const std::string &title) override;
    Error init3D(int width, int height, gfx::Renderer3D *&out) override;
    bool pollEvent(Event &event) override;
    void setTextInput(bool on) override;
    void setMouseCursorVisible(bool visible) override;
    void setMouseCursor(const Cursor *cursor) override;
    void setVideoMode(int width, int height, bool fullscreen) override;
    void setRelativeMouse(bool on) override;
    bool confirmQuit(const std::string &text, bool toLauncher) override;
    void setDisplayOptions(const DisplayOptions &options) override;
    uint64_t getMillis() const override;
    void delayMillis(uint32_t ms) override;
    Error startAudio(AudioCallback callback) override;
    void stopAudio() override;
    int audioRate() const override { return kAudioRate; }
    Error openUdp(uint16_t port, std::unique_ptr<net::UdpSocket> &out) override;
    bool resolveHost(const std::string &host, uint32_t &ip) override;
    std::vector<uint32_t> broadcastAddresses() override;

private:
    static constexpr int kAudioRate = 44100;
    static void SDLCALL audioThunk(void *userdata, SDL_AudioStream *stream, int additional, int total);
    void release3D();
    // The game's picture, width x height, shown as display_ says: scaled
    // into the window (and the window sized, if asked).
    void applyPresentation(int width, int height);
    // The game's cursor (setMouseCursor) made again for how the picture is
    // scaled now: as the system's pointer, or drawn by us (software).
    void refreshCursor();
    void releaseCursor();
    // The software cursor drawn at the mouse, in the picture's coordinates.
    void drawSoftwareCursor();

    SDL_Window *window_ = nullptr;
    SDL_Joystick *joystick_ = nullptr; // the first connected, whose events are passed on
    SDL_JoystickID joystickId_ = 0;
    bool relativeMouse_ = false; // held by setRelativeMouse, held again when the window changes
    SDL_Renderer *renderer_ = nullptr;
    SDL_GPUDevice *gpu_ = nullptr; // set when renderer_ runs on SDL's GPU API, which 3D needs
    std::unique_ptr<SdlGpuRenderer> renderer3d_;
    SDL_Texture *gameTexture_ = nullptr;
    int gameW_ = 0, gameH_ = 0;
    PixelFormat gameFormat_ = PixelFormat::CLUT8;
    uint32_t palette_[256] = {};          // RGBA32, byte order R,G,B,A
    std::vector<uint32_t> convertBuffer_; // CLUT8 -> RGBA32 staging
    bool quitRequested_ = false;

    std::optional<Cursor> cursor_;    // the game's, as it gave it
    SDL_Cursor *sdlCursor_ = nullptr; // it as the system's pointer
    float sdlCursorScale_ = 0;        // the scale sdlCursor_ was made at
    bool softwareCursor_ = false;     // drawn by us instead (cursorTexture_)
    SDL_Texture *cursorTexture_ = nullptr;
    bool cursorVisible_ = true;         // setMouseCursorVisible
    bool mouseInside_ = false;          // the mouse is over the window
    float mouseX_ = 0, mouseY_ = 0;     // in the picture's coordinates
    uint64_t lastPresent_ = 0;          // when the 2D picture was last shown (ms)
    DisplayOptions display_;            // the game's; the launcher's are the defaults
    int launcherW_ = 0, launcherH_ = 0; // the window's size before a game resized it

    SDL_AudioStream *audioStream_ = nullptr;
    AudioCallback audioCallback_;
    std::vector<int16_t> audioBuffer_; // audio thread only
};

} // namespace reluva
