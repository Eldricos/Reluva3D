// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "backends/platform/sdl3/sdl_system.h"
#include "backends/graphics/sdl3/sdl_gpu.h"
#include "backends/networking/sockets/socket_net.h"

#include <algorithm>
#include <cstring>

namespace reluva {

namespace {

uint32_t packRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    const uint8_t bytes[4] = {r, g, b, a};
    uint32_t v;
    std::memcpy(&v, bytes, 4);
    return v;
}

uint32_t translateKey(SDL_Keycode k) {
    if (k >= 'A' && k <= 'Z')
        return k - 'A' + 'a';
    if (k < 128)
        return k; // SDL keycodes match ASCII for printable keys
    switch (k) {
    case SDLK_UP:
        return key::kUp;
    case SDLK_DOWN:
        return key::kDown;
    case SDLK_LEFT:
        return key::kLeft;
    case SDLK_RIGHT:
        return key::kRight;
    case SDLK_INSERT:
        return key::kInsert;
    case SDLK_HOME:
        return key::kHome;
    case SDLK_END:
        return key::kEnd;
    case SDLK_PAGEUP:
        return key::kPageUp;
    case SDLK_PAGEDOWN:
        return key::kPageDown;
    case SDLK_LSHIFT:
        return key::kLShift;
    case SDLK_RSHIFT:
        return key::kRShift;
    case SDLK_LCTRL:
        return key::kLCtrl;
    case SDLK_RCTRL:
        return key::kRCtrl;
    case SDLK_LALT:
        return key::kLAlt;
    case SDLK_RALT:
        return key::kRAlt;
    default:
        break;
    }
    if (k >= SDLK_F1 && k <= SDLK_F12)
        return key::kF1 + (k - SDLK_F1);
    // The keypad's keys keep their own codes with NumLock off too (SDL3
    // reports KP_8, not Up), so engines can use them as keys of their own.
    if (k >= SDLK_KP_1 && k <= SDLK_KP_9)
        return key::kKp0 + 1 + (k - SDLK_KP_1);
    if (k == SDLK_KP_0)
        return key::kKp0;
    if (k == SDLK_KP_PERIOD)
        return key::kKpPeriod;
    if (k == SDLK_KP_ENTER)
        return key::kKpEnter;
    return key::kUnknown;
}

uint16_t translateMods(SDL_Keymod m) {
    uint16_t out = 0;
    if (m & SDL_KMOD_SHIFT)
        out |= keymod::kShift;
    if (m & SDL_KMOD_CTRL)
        out |= keymod::kCtrl;
    if (m & SDL_KMOD_ALT)
        out |= keymod::kAlt;
    return out;
}

MouseButton translateButton(uint8_t b) {
    switch (b) {
    case SDL_BUTTON_LEFT:
        return MouseButton::kLeft;
    case SDL_BUTTON_RIGHT:
        return MouseButton::kRight;
    case SDL_BUTTON_MIDDLE:
        return MouseButton::kMiddle;
    default:
        return MouseButton::kNone;
    }
}

} // namespace

SdlSystem::SdlSystem() = default;

SdlSystem::~SdlSystem() {
    stopAudio();
    for (SDL_Gamepad *p : _pads)
        SDL_CloseGamepad(p);
    release3D();
    if (_gameTexture)
        SDL_DestroyTexture(_gameTexture);
    if (_renderer)
        SDL_DestroyRenderer(_renderer);
    if (_gpu)
        SDL_DestroyGPUDevice(_gpu);
    if (_window)
        SDL_DestroyWindow(_window);
    SDL_Quit();
}

Error SdlSystem::init() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
        return Error::fail(std::string("SDL_Init failed: ") + SDL_GetError());
    // No sound card shouldn't stop anyone playing; startAudio() reports it.
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        SDL_Log("Audio unavailable: %s", SDL_GetError());

    _window = SDL_CreateWindow("Reluva", 1280, 800, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!_window)
        return Error::fail(std::string("SDL_CreateWindow failed: ") + SDL_GetError());

    // The renderer runs on a GPU device of our own when it can, so engines
    // can draw 3D on the same device (init3D). Asking SDL for a particular
    // renderer (SDL_RENDER_DRIVER) other than "gpu" skips this.
    const char *driver = SDL_GetHint(SDL_HINT_RENDER_DRIVER);
    if (!driver || std::strcmp(driver, "gpu") == 0) {
        _gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
        if (_gpu) {
            _renderer = SDL_CreateGPURenderer(_gpu, _window);
            if (!_renderer) {
                SDL_DestroyGPUDevice(_gpu);
                _gpu = nullptr;
            }
        }
        if (!_gpu)
            SDL_Log("No GPU device, so no 3D: %s", SDL_GetError());
    }
    if (!_renderer)
        _renderer = SDL_CreateRenderer(_window, nullptr);
    if (!_renderer)
        return Error::fail(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
    SDL_SetRenderVSync(_renderer, 1);

    SDL_Log("Video driver: %s, renderer: %s%s%s%s%s", SDL_GetCurrentVideoDriver(), SDL_GetRendererName(_renderer),
            _gpu ? ", GPU driver: " : "", _gpu ? SDL_GetGPUDeviceDriver(_gpu) : "", _gpu ? ", device: " : "",
            _gpu ? SDL_GetStringProperty(SDL_GetGPUDeviceProperties(_gpu), SDL_PROP_GPU_DEVICE_NAME_STRING, "?") : "");
    return Error::success();
}

void SdlSystem::release3D() {
    _renderer3d.reset();
}

Error SdlSystem::init3D(int width, int height, gfx::Renderer3D *&out) {
    out = nullptr;
    release3D();
    if (!_gpu)
        return Error::fail("3D rendering needs a GPU with Vulkan or Metal support");
    if (_gameTexture) {
        SDL_DestroyTexture(_gameTexture);
        _gameTexture = nullptr;
    }
    _gameW = _gameH = 0;
    auto r = std::make_unique<SdlGpuRenderer>(_gpu, _renderer);
    if (Error err = r->init(width, height))
        return err;
    _renderer3d = std::move(r);
    SDL_SetRenderLogicalPresentation(_renderer, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    out = _renderer3d.get();
    return Error::success();
}

void SdlSystem::resetForLauncher() {
    stopAudio();
    SDL_StopTextInput(_window);
    SDL_ShowCursor();
    _relativeMouse = false;
    SDL_SetWindowRelativeMouseMode(_window, false);
    SDL_SetWindowFullscreen(_window, false);
    release3D();
    if (_gameTexture) {
        SDL_DestroyTexture(_gameTexture);
        _gameTexture = nullptr;
    }
    _gameW = _gameH = 0;
    SDL_SetRenderLogicalPresentation(_renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetWindowTitle(_window, "Reluva");
}

Error SdlSystem::initGraphics(int width, int height, PixelFormat format) {
    release3D();
    if (_gameTexture)
        SDL_DestroyTexture(_gameTexture);
    _gameTexture = SDL_CreateTexture(_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!_gameTexture)
        return Error::fail(std::string("SDL_CreateTexture failed: ") + SDL_GetError());
    SDL_SetTextureScaleMode(_gameTexture, SDL_SCALEMODE_NEAREST);

    _gameW = width;
    _gameH = height;
    _gameFormat = format;
    _convertBuffer.assign(static_cast<size_t>(width) * height, 0);
    for (int i = 0; i < 256; ++i)
        _palette[i] = packRgba(0, 0, 0, 255);

    // Letterbox the game framebuffer into the window at any size, and map
    // mouse coordinates back into framebuffer pixels.
    SDL_SetRenderLogicalPresentation(_renderer, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    return Error::success();
}

void SdlSystem::setPalette(const Color *colors, int start, int count) {
    for (int i = 0; i < count && start + i < 256; ++i)
        _palette[start + i] = packRgba(colors[i].r, colors[i].g, colors[i].b, 255);
}

void SdlSystem::updateScreen(const Surface &surface) {
    if (!_gameTexture || surface.w != _gameW || surface.h != _gameH || surface.format != _gameFormat)
        return;

    if (surface.format == PixelFormat::kCluT8) {
        for (int y = 0; y < surface.h; ++y) {
            const uint8_t *src = surface.row(y);
            uint32_t *dst = _convertBuffer.data() + static_cast<size_t>(y) * _gameW;
            for (int x = 0; x < surface.w; ++x)
                dst[x] = _palette[src[x]];
        }
        SDL_UpdateTexture(_gameTexture, nullptr, _convertBuffer.data(), _gameW * 4);
    } else {
        SDL_UpdateTexture(_gameTexture, nullptr, surface.pixels.data(), surface.pitch);
    }

    SDL_SetRenderDrawColor(_renderer, 0, 0, 0, 255);
    SDL_RenderClear(_renderer);
    SDL_RenderTexture(_renderer, _gameTexture, nullptr, nullptr);
    SDL_RenderPresent(_renderer);
}

void SdlSystem::setWindowTitle(const std::string &title) {
    SDL_SetWindowTitle(_window, title.c_str());
}

bool SdlSystem::pollEvent(Event &out) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        SDL_ConvertEventToRenderCoordinates(_renderer, &ev);
        out = Event{};
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            _quitRequested = true;
            out.type = EventType::kQuit;
            return true;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            // Ctrl+F5 is the global "return to launcher" hotkey.
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_F5 && (ev.key.mod & SDL_KMOD_CTRL)) {
                out.type = EventType::kReturnToLauncher;
                return true;
            }
            // Alt+Enter toggles fullscreen.
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_RETURN && (ev.key.mod & SDL_KMOD_ALT)) {
                bool fs = SDL_GetWindowFlags(_window) & SDL_WINDOW_FULLSCREEN;
                SDL_SetWindowFullscreen(_window, !fs);
                continue;
            }
            out.type = ev.type == SDL_EVENT_KEY_DOWN ? EventType::kKeyDown : EventType::kKeyUp;
            out.key = translateKey(ev.key.key);
            out.mods = translateMods(ev.key.mod);
            out.repeat = ev.key.repeat;
            return true;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (SDL_Gamepad *p = SDL_OpenGamepad(ev.gdevice.which))
                _pads.push_back(p);
            continue;
        case SDL_EVENT_GAMEPAD_REMOVED:
            for (size_t i = 0; i < _pads.size(); ++i)
                if (SDL_GetGamepadID(_pads[i]) == ev.gdevice.which) {
                    SDL_CloseGamepad(_pads[i]);
                    _pads.erase(_pads.begin() + std::ptrdiff_t(i));
                    break;
                }
            continue;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            // SDL's buttons up to the d-pad are in pad::Button's order.
            if (ev.gbutton.button >= pad::kButtons)
                continue;
            out.type = ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? EventType::kPadDown : EventType::kPadUp;
            out.pad = ev.gbutton.button;
            return true;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (ev.gaxis.axis >= pad::kAxes)
                continue;
            out.type = EventType::kPadAxis;
            out.pad = ev.gaxis.axis;
            out.value = std::max(-1.0f, float(ev.gaxis.value) / 32767.0f);
            return true;
        case SDL_EVENT_MOUSE_MOTION:
            out.type = EventType::kMouseMove;
            out.mouseX = static_cast<int>(ev.motion.x);
            out.mouseY = static_cast<int>(ev.motion.y);
            out.moveX = ev.motion.xrel;
            out.moveY = ev.motion.yrel;
            out.mods = translateMods(SDL_GetModState());
            return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            out.type = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? EventType::kMouseDown : EventType::kMouseUp;
            out.button = translateButton(ev.button.button);
            out.mouseX = static_cast<int>(ev.button.x);
            out.mouseY = static_cast<int>(ev.button.y);
            out.clicks = ev.button.clicks;
            out.mods = translateMods(SDL_GetModState());
            return true;
        case SDL_EVENT_MOUSE_WHEEL:
            out.type = EventType::kMouseWheel;
            out.wheelY = ev.wheel.integer_y;
            return true;
        case SDL_EVENT_TEXT_INPUT:
            if (!ev.text.text)
                break;
            out.type = EventType::kTextInput;
            SDL_strlcpy(out.text, ev.text.text, sizeof out.text);
            return true;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            // The window changed under a held mouse: hold it again, as some
            // systems let it go.
            if (_relativeMouse) {
                SDL_SetWindowRelativeMouseMode(_window, false);
                SDL_SetWindowRelativeMouseMode(_window, true);
            }
            break;
        default:
            break; // ignore everything else
        }
    }
    return false;
}

void SdlSystem::setTextInput(bool on) {
    if (on)
        SDL_StartTextInput(_window);
    else
        SDL_StopTextInput(_window);
}

void SdlSystem::setMouseCursorVisible(bool visible) {
    if (visible)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
}

void SdlSystem::rumble(float low, float high, uint32_t ms) {
    auto motor = [](float v) { return Uint16(std::clamp(v, 0.0f, 1.0f) * 65535); };
    for (SDL_Gamepad *p : _pads)
        SDL_RumbleGamepad(p, motor(low), motor(high), ms);
}

void SdlSystem::minimizeWindow() {
    if (_window)
        SDL_MinimizeWindow(_window);
}

void SdlSystem::setVideoMode(int width, int height, bool fullscreen) {
    if (!_window)
        return;
    // Fullscreen at the desktop's own mode (null), which needs no mode switch.
    const bool wasFullscreen = (SDL_GetWindowFlags(_window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (fullscreen != wasFullscreen) {
        SDL_SetWindowFullscreenMode(_window, nullptr);
        SDL_SetWindowFullscreen(_window, fullscreen);
    }
    SDL_Rect usable{};
    int w = 0, h = 0;
    SDL_GetWindowSize(_window, &w, &h);
    if (!fullscreen && (w != width || h != height) &&
        SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(_window), &usable) && width <= usable.w &&
        height <= usable.h && !(SDL_GetWindowFlags(_window) & SDL_WINDOW_MAXIMIZED))
        SDL_SetWindowSize(_window, width, height);
    // The changes happen in their own time on some systems: wait, so the
    // mouse is held by the window as it ends up.
    SDL_SyncWindow(_window);
    if (_relativeMouse)
        SDL_SetWindowRelativeMouseMode(_window, true);
}

void SdlSystem::setRelativeMouse(bool on) {
    _relativeMouse = on;
    if (_window)
        SDL_SetWindowRelativeMouseMode(_window, on);
}

uint64_t SdlSystem::getMillis() const {
    return SDL_GetTicks();
}

void SdlSystem::delayMillis(uint32_t ms) {
    SDL_Delay(ms);
}

void SDLCALL SdlSystem::audioThunk(void *userdata, SDL_AudioStream *stream, int additional, int) {
    auto *self = static_cast<SdlSystem *>(userdata);
    const int frames = additional / (2 * static_cast<int>(sizeof(int16_t)));
    if (frames <= 0)
        return;
    self->_audioBuffer.resize(static_cast<size_t>(frames) * 2);
    self->_audioCallback(self->_audioBuffer.data(), frames);
    SDL_PutAudioStreamData(stream, self->_audioBuffer.data(), frames * 4);
}

Error SdlSystem::startAudio(AudioCallback callback) {
    stopAudio();
    _audioCallback = std::move(callback);
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, kAudioRate};
    _audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audioThunk, this);
    if (!_audioStream)
        return Error::fail(std::string("Cannot open audio: ") + SDL_GetError());
    SDL_ResumeAudioStreamDevice(_audioStream);
    return Error::success();
}

void SdlSystem::stopAudio() {
    if (_audioStream) {
        SDL_DestroyAudioStream(_audioStream); // stops the callback before returning
        _audioStream = nullptr;
    }
    _audioCallback = nullptr;
}

Error SdlSystem::openUdp(uint16_t port, std::unique_ptr<net::UdpSocket> &out) {
    std::string error;
    out = net::openSystemUdp(port, error);
    return out ? Error::success() : Error::fail(error);
}

bool SdlSystem::resolveHost(const std::string &host, uint32_t &ip) {
    return net::resolveSystemHost(host, ip);
}

std::vector<uint32_t> SdlSystem::broadcastAddresses() {
    return net::systemBroadcastAddresses();
}

bool SdlSystem::httpExchange(const net::Address &to, const std::string &request, std::string &response,
                             uint32_t &localIp, int timeoutMs) {
    return net::systemHttpExchange(to, request, response, localIp, timeoutMs);
}

std::vector<uint32_t> SdlSystem::gateways() {
    return net::systemGateways();
}

} // namespace reluva
