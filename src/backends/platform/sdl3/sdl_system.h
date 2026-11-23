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

/**
 * The System on SDL 3: one window, drawn with SDL's renderer (on a GPU
 * device of its own where there is one, which 3D needs), its sound, input,
 * game controller and the operating system's sockets.
 */
class SdlSystem : public System {
public:
    SdlSystem();
    ~SdlSystem() override;
    SdlSystem(const SdlSystem &) = delete;
    SdlSystem &operator=(const SdlSystem &) = delete;

    /**
     * Starts SDL and opens the window and its renderer. Without a sound
     * device it still succeeds (startAudio says so later); without a GPU
     * device, with no 3D.
     */
    Error init();

    /** The window and its renderer, for the launcher, which draws with ImGui directly on SDL. */
    ///@{
    SDL_Window *window() const { return _window; }
    SDL_Renderer *renderer() const { return _renderer; }
    ///@}
    /**
     * Drops the game's picture, sound and display settings, and gives the
     * window back to the launcher as it was.
     */
    void resetForLauncher();
    /**
     * True once the user closed the window during a game (and didn't take
     * it back when confirmQuit asked).
     */
    bool quitRequested() const { return _quitRequested; }

    // System's.
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
    void minimizeWindow() override;
    bool desktopMode(int &width, int &height, int &bits) override;
    void setRelativeMouse(bool on) override;
    bool readGamepad(GamepadState &out) override;
    bool readGamepad(int index, GamepadState &out) override;
    void setGamepadForce(const GamepadForce &force) override;
    void setGamepadForce(int index, const GamepadForce &force) override;
    void setWheelPedals(int accelerator, int brake) override;
    bool confirmQuit(const std::string &text, bool toLauncher) override;
    void setDisplayOptions(const DisplayOptions &options) override;
    uint64_t getMillis() const override;
    void delayMillis(uint32_t ms) override;
    Error startAudio(AudioCallback callback) override;
    int audioDeviceChannels() const override;
    Error startAudioChannels(int channels, AudioCallback callback) override;
    void stopAudio() override;
    int audioRate() const override { return kAudioRate; }
    Error openUdp(uint16_t port, std::unique_ptr<net::UdpSocket> &out) override;
    bool resolveHost(const std::string &host, uint32_t &ip) override;
    std::vector<uint32_t> broadcastAddresses() override;
    bool httpExchange(const net::Address &to, const std::string &request, std::string &response, uint32_t &localIp,
                      int timeoutMs) override;
    std::vector<uint32_t> gateways() override;
    std::shared_ptr<net::Sockets> sockets() override;

private:
    static constexpr int kAudioRate = 44100;
    static void SDLCALL audioThunk(void *userdata, SDL_AudioStream *stream, int additional, int total);
    void release3D();
    // The game's picture, width x height, shown as _display says: scaled
    // into the window (and the window sized, if asked).
    void applyPresentation(int width, int height);
    // The game's cursor (setMouseCursor) made again for how the picture is
    // scaled now: as the system's pointer, or drawn by us (software).
    void refreshCursor();
    void releaseCursor();
    // The software cursor drawn at the mouse, in the picture's coordinates.
    void drawSoftwareCursor();

    SDL_Window *_window = nullptr;
    // The controller (sdl_controller.cpp): the first pad connected, or else
    // the first wheel, opened when asked for; its forces.
    struct Controller;
    std::unique_ptr<Controller> _controller;
    void closeController();
    static void readState(Controller &c, GamepadState &out); // a pad's, or a wheel's by its axes
    // A controller's forces: its haptics or rumble opened, a force given,
    // the rumble sent again before it runs out; and its devices closed.
    static void openForces(Controller &c);
    static void applyForce(Controller &c, const GamepadForce &f);
    static void resendRumble(Controller &c);
    static void closeDevices(Controller &c);
    // The second (readGamepad(1)): the next pad connected, or else a wheel,
    // with forces of its own (setGamepadForce(1, ...)).
    std::unique_ptr<Controller> _second;
    void closeSecond();
    SDL_Joystick *_joystick = nullptr; // the first connected, whose events are passed on
    SDL_JoystickID _joystickId = 0;
    bool _relativeMouse = false; // held by setRelativeMouse, held again when the window changes
    SDL_Renderer *_renderer = nullptr;
    SDL_GPUDevice *_gpu = nullptr; // set when _renderer runs on SDL's GPU API, which 3D needs
    std::unique_ptr<SdlGpuRenderer> _renderer3d;
    SDL_Texture *_gameTexture = nullptr;
    int _gameW = 0;
    int _gameH = 0;
    PixelFormat _gameFormat = PixelFormat::kCLUT8;
    uint32_t _palette[256] = {};          // RGBA32, byte order R,G,B,A
    std::vector<uint32_t> _convertBuffer; // CLUT8 -> RGBA32 staging
    bool _quitRequested = false;

    std::optional<Cursor> _cursor;    // the game's, as it gave it
    SDL_Cursor *_sdlCursor = nullptr; // it as the system's pointer
    float _sdlCursorScale = 0;        // the scale _sdlCursor was made at
    bool _softwareCursor = false;     // drawn by us instead (_cursorTexture)
    SDL_Texture *_cursorTexture = nullptr;
    bool _cursorVisible = true; // setMouseCursorVisible
    bool _mouseInside = false;  // the mouse is over the window
    // in the picture's coordinates
    float _mouseX = 0;
    float _mouseY = 0;
    uint64_t _lastPresent = 0; // when the 2D picture was last shown (ms)
    DisplayOptions _display;   // the game's; the launcher's are the defaults
    // the window's size before a game resized it
    int _launcherW = 0;
    int _launcherH = 0;

    SDL_AudioStream *_audioStream = nullptr;
    AudioCallback _audioCallback;
    int _audioChannels = 2;
    std::vector<int16_t> _audioBuffer; // audio thread only
};

} // namespace reluva
