// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/system.h"

#include <SDL3/SDL.h>

#include <memory>
#include <vector>

namespace reluva {

class SdlGpuRenderer;

class SdlSystem : public System {
public:
    SdlSystem();
    ~SdlSystem() override;
    SdlSystem(const SdlSystem&) = delete;
    SdlSystem& operator=(const SdlSystem&) = delete;

    Error init();

    // Used by the launcher, which draws with ImGui directly on SDL.
    SDL_Window* window() const { return _window; }
    SDL_Renderer* renderer() const { return _renderer; }
    // Drop the game framebuffer and return the window to launcher mode.
    void resetForLauncher();
    // True once the user closed the window during a game.
    bool quitRequested() const { return _quitRequested; }

    // System
    Error initGraphics(int width, int height, PixelFormat format) override;
    void setPalette(const Color* colors, int start, int count) override;
    void updateScreen(const Surface& surface) override;
    void setWindowTitle(const std::string& title) override;
    Error init3D(int width, int height, gfx::Renderer3D*& out) override;
    bool pollEvent(Event& event) override;
    void setTextInput(bool on) override;
    void setMouseCursorVisible(bool visible) override;
    void setVideoMode(int width, int height, bool fullscreen) override;
    void minimizeWindow() override;
    void rumble(float low, float high, uint32_t ms) override;
    void setRelativeMouse(bool on) override;
    uint64_t getMillis() const override;
    void delayMillis(uint32_t ms) override;
    Error startAudio(AudioCallback callback) override;
    void stopAudio() override;
    int audioRate() const override { return kAudioRate; }
    Error openUdp(uint16_t port, std::unique_ptr<net::UdpSocket>& out) override;
    bool resolveHost(const std::string& host, uint32_t& ip) override;
    std::vector<uint32_t> broadcastAddresses() override;
    bool httpExchange(const net::Address& to, const std::string& request, std::string& response, uint32_t& localIp,
                      int timeoutMs) override;
    std::vector<uint32_t> gateways() override;

private:
    static constexpr int kAudioRate = 44100;
    static void SDLCALL audioThunk(void* userdata, SDL_AudioStream* stream, int additional, int total);
    void release3D();

    SDL_Window* _window = nullptr;
    bool _relativeMouse = false; // held by setRelativeMouse, held again when the window changes
    SDL_Renderer* _renderer = nullptr;
    SDL_GPUDevice* _gpu = nullptr; // set when renderer_ runs on SDL's GPU API, which 3D needs
    std::unique_ptr<SdlGpuRenderer> _renderer3d;
    SDL_Texture* _gameTexture = nullptr;
    int _gameW = 0, _gameH = 0;
    PixelFormat _gameFormat = PixelFormat::kCluT8;
    uint32_t _palette[256] = {};          // RGBA32, byte order R,G,B,A
    std::vector<uint32_t> _convertBuffer; // CLUT8 -> RGBA32 staging
    bool _quitRequested = false;
    std::vector<SDL_Gamepad*> _pads; // opened as they're plugged in

    SDL_AudioStream* _audioStream = nullptr;
    AudioCallback _audioCallback;
    std::vector<int16_t> _audioBuffer; // audio thread only
};

} // namespace reluva
