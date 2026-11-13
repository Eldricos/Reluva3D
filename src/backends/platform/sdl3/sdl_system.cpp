// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "backends/platform/sdl3/sdl_system.h"
#include "backends/platform/sdl3/sdl_controller.h"
#include "backends/graphics/sdl3/sdl_gpu.h"
#include "backends/networking/sockets/socket_net.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

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
    if (k == SDLK_KP_PLUS)
        return key::kKpPlus;
    if (k == SDLK_KP_MINUS)
        return key::kKpMinus;
    if (k == SDLK_KP_MULTIPLY)
        return key::kKpMultiply;
    if (k == SDLK_KP_DIVIDE)
        return key::kKpDivide;
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
    closeController();
    stopAudio();
    release3D();
    releaseCursor();
    if (_gameTexture)
        SDL_DestroyTexture(_gameTexture);
    if (_joystick)
        SDL_CloseJoystick(_joystick);
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
    _renderer3d->setBeforePresent([this] { drawSoftwareCursor(); });
    applyPresentation(width, height);
    out = _renderer3d.get();
    return Error::success();
}

void SdlSystem::resetForLauncher() {
    stopAudio();
    SDL_StopTextInput(_window);
    _cursor.reset();
    releaseCursor();
    _cursorVisible = true;
    SDL_ShowCursor();
    _relativeMouse = false;
    SDL_SetWindowRelativeMouseMode(_window, false);
    SDL_SetWindowFullscreen(_window, false);
    // The display settings were the game's: the launcher has Reluva's own,
    // and its window back as it was.
    if (!(_display == DisplayOptions{}))
        SDL_SetRenderVSync(_renderer, 1);
    _display = DisplayOptions{};
    if (_launcherW > 0) {
        SDL_SetWindowSize(_window, _launcherW, _launcherH);
        _launcherW = _launcherH = 0;
    }
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
    SDL_SetTextureScaleMode(_gameTexture, _display.smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);

    _gameW = width;
    _gameH = height;
    _gameFormat = format;
    _convertBuffer.assign(static_cast<size_t>(width) * height, 0);
    for (int i = 0; i < 256; ++i)
        _palette[i] = packRgba(0, 0, 0, 255);

    applyPresentation(width, height);
    return Error::success();
}

void SdlSystem::applyPresentation(int width, int height) {
    // The window as a multiple of the game's size, when asked and it fits
    // on the screen (not in full screen, nor when the player maximised it).
    const Uint64 flags = SDL_GetWindowFlags(_window);
    if (_display.windowScale != 0 && !(flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED))) {
        SDL_Rect usable{};
        if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(_window), &usable)) {
            int scale = _display.windowScale;
            if (scale == DisplayOptions::kAutoScale)
                scale = 2 * width <= usable.w && 2 * height <= usable.h ? 2 : 1;
            while (scale > 1 && (scale * width > usable.w || scale * height > usable.h))
                --scale;
            int w = 0;
            int h = 0;
            SDL_GetWindowSize(_window, &w, &h);
            if (w != scale * width || h != scale * height) {
                if (_launcherW == 0) {
                    _launcherW = w;
                    _launcherH = h;
                }
                SDL_SetWindowSize(_window, scale * width, scale * height);
                SDL_SyncWindow(_window);
            }
        }
    }
    // Scale the frame into the window at any size (letterboxed), or by whole
    // multiples only, and map mouse coordinates back into its pixels.
    SDL_SetRenderLogicalPresentation(_renderer, width, height,
                                     _display.integerScaling ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE
                                                             : SDL_LOGICAL_PRESENTATION_LETTERBOX);
    refreshCursor();
}

void SdlSystem::setDisplayOptions(const DisplayOptions &options) {
    _display = options;
    SDL_SetRenderVSync(_renderer, options.vsync ? 1 : 0);
    if (options.fullscreen && !(SDL_GetWindowFlags(_window) & SDL_WINDOW_FULLSCREEN)) {
        SDL_SetWindowFullscreenMode(_window, nullptr);
        SDL_SetWindowFullscreen(_window, true);
        SDL_SyncWindow(_window);
    }
}

void SdlSystem::setPalette(const Color *colors, int start, int count) {
    for (int i = 0; i < count && start + i < 256; ++i)
        _palette[start + i] = packRgba(colors[i].r, colors[i].g, colors[i].b, 255);
}

void SdlSystem::updateScreen(const Surface &surface) {
    if (!_gameTexture || surface.w != _gameW || surface.h != _gameH || surface.format != _gameFormat)
        return;

    if (surface.format == PixelFormat::kCLUT8) {
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
    drawSoftwareCursor();
    SDL_RenderPresent(_renderer);
    _lastPresent = SDL_GetTicks();
}

void SdlSystem::setWindowTitle(const std::string &title) {
    SDL_SetWindowTitle(_window, title.c_str());
}

bool SdlSystem::pollEvent(Event &out) {
    // The first joystick connected, opened when there's none: the launcher
    // takes SDL's events itself, connection notices included.
    if (!_joystick && SDL_HasJoystick()) {
        int count = 0;
        if (SDL_JoystickID *ids = SDL_GetJoysticks(&count)) {
            if (count > 0 && (_joystick = SDL_OpenJoystick(ids[0])))
                _joystickId = ids[0];
            SDL_free(ids);
        }
    }
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
        case SDL_EVENT_MOUSE_MOTION:
            _mouseX = ev.motion.x;
            _mouseY = ev.motion.y;
            _mouseInside = true;
            // A cursor we draw moves only when the picture is shown again:
            // show the last one again, not more often than the display can.
            if (_softwareCursor && _cursorVisible && !_relativeMouse && _gameTexture && !_renderer3d &&
                SDL_GetTicks() - _lastPresent >= 8) {
                SDL_SetRenderDrawColor(_renderer, 0, 0, 0, 255);
                SDL_RenderClear(_renderer);
                SDL_RenderTexture(_renderer, _gameTexture, nullptr, nullptr);
                drawSoftwareCursor();
                SDL_RenderPresent(_renderer);
                _lastPresent = SDL_GetTicks();
            }
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
        case SDL_EVENT_JOYSTICK_ADDED:
            // The first joystick connected is the one used, as the
            // original's engine read joystick 1.
            if (!_joystick)
                if ((_joystick = SDL_OpenJoystick(ev.jdevice.which)))
                    _joystickId = ev.jdevice.which;
            break;
        case SDL_EVENT_JOYSTICK_REMOVED:
            if (_joystick && ev.jdevice.which == _joystickId) {
                SDL_CloseJoystick(_joystick);
                _joystick = nullptr;
                out.type = EventType::kJoyRemoved;
                return true;
            }
            break;
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
        case SDL_EVENT_JOYSTICK_BUTTON_UP:
            if (!_joystick || ev.jbutton.which != _joystickId)
                break;
            out.type = ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN ? EventType::kJoyButtonDown : EventType::kJoyButtonUp;
            out.joyButton = ev.jbutton.button;
            return true;
        case SDL_EVENT_JOYSTICK_AXIS_MOTION:
            if (!_joystick || ev.jaxis.which != _joystickId)
                break;
            out.type = EventType::kJoyAxis;
            out.joyAxis = ev.jaxis.axis;
            out.joyValue = ev.jaxis.value;
            return true;
        case SDL_EVENT_JOYSTICK_HAT_MOTION:
            if (!_joystick || ev.jhat.which != _joystickId || ev.jhat.hat != 0)
                break;
            out.type = EventType::kJoyHat;
            out.joyHat = uint8_t((ev.jhat.value & SDL_HAT_UP ? joyhat::kUp : 0) |
                                 (ev.jhat.value & SDL_HAT_RIGHT ? joyhat::kRight : 0) |
                                 (ev.jhat.value & SDL_HAT_DOWN ? joyhat::kDown : 0) |
                                 (ev.jhat.value & SDL_HAT_LEFT ? joyhat::kLeft : 0));
            return true;
        case SDL_EVENT_TEXT_INPUT:
            if (!ev.text.text)
                break;
            out.type = EventType::kTextInput;
            SDL_strlcpy(out.text, ev.text.text, sizeof out.text);
            return true;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            out.type = EventType::kFocusLost;
            return true;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            if (_relativeMouse) {
                SDL_SetWindowRelativeMouseMode(_window, false);
                SDL_SetWindowRelativeMouseMode(_window, true);
            }
            out.type = EventType::kFocusGained;
            return true;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            // The window changed under a held mouse: hold it again, as some
            // systems let it go.
            if (_relativeMouse) {
                SDL_SetWindowRelativeMouseMode(_window, false);
                SDL_SetWindowRelativeMouseMode(_window, true);
            }
            refreshCursor(); // the picture's scale may have changed
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            refreshCursor();
            break;
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
            _mouseInside = true;
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            _mouseInside = false;
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
    _cursorVisible = visible;
    // A cursor we draw keeps the system's pointer hidden.
    if (visible && !_softwareCursor)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
}

void SdlSystem::setMouseCursor(const Cursor *cursor) {
    if (!cursor || cursor->empty()) {
        if (!_cursor)
            return;
        _cursor.reset();
        releaseCursor();
        setMouseCursorVisible(_cursorVisible);
        return;
    }
    if (_cursor && *_cursor == *cursor)
        return; // games often set the same one again
    _cursor = *cursor;
    // A new picture: made again, whatever the scale.
    _sdlCursorScale = 0;
    if (_cursorTexture) {
        SDL_DestroyTexture(_cursorTexture);
        _cursorTexture = nullptr;
    }
    refreshCursor();
}

void SdlSystem::releaseCursor() {
    if (_sdlCursor) {
        SDL_SetCursor(SDL_GetDefaultCursor());
        SDL_DestroyCursor(_sdlCursor);
        _sdlCursor = nullptr;
    }
    _sdlCursorScale = 0;
    if (_cursorTexture) {
        SDL_DestroyTexture(_cursorTexture);
        _cursorTexture = nullptr;
    }
    _softwareCursor = false;
}

void SdlSystem::refreshCursor() {
    if (!_cursor || !_window || !_renderer)
        return;
    // How many of the window's pixels a game pixel takes now, and how many
    // of its points (the system's pointer is sized in points; a second
    // picture gives it the pixels on high-density screens).
    int lw = 0;
    int lh = 0;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    SDL_GetRenderLogicalPresentation(_renderer, &lw, &lh, &mode);
    float pixelScale = 1;
    SDL_FRect rect{};
    if (lw > 0 && SDL_GetRenderLogicalPresentationRect(_renderer, &rect) && rect.w > 0)
        pixelScale = rect.w / float(lw);
    float density = SDL_GetWindowPixelDensity(_window);
    if (!(density > 0))
        density = 1;
    const float pointScale = pixelScale / density;

    // RELUVA_SOFTWARE_CURSOR=1 draws it ourselves, to try that path.
    const char *forced = SDL_getenv("RELUVA_SOFTWARE_CURSOR");
    const bool software = forced && *forced && std::strcmp(forced, "0") != 0;
    if (!software) {
        if (_sdlCursor && _sdlCursorScale == pixelScale)
            return;
        const Cursor points = cursor::scaled(*_cursor, pointScale, _display.smooth);
        SDL_Surface *surface = SDL_CreateSurfaceFrom(points.w, points.h, SDL_PIXELFORMAT_RGBA32,
                                                     const_cast<uint8_t *>(points.rgba.data()), points.w * 4);
        Cursor pixels;
        SDL_Surface *dense = nullptr;
        if (surface && density != 1) {
            pixels = cursor::scaled(*_cursor, pixelScale, _display.smooth);
            dense = SDL_CreateSurfaceFrom(pixels.w, pixels.h, SDL_PIXELFORMAT_RGBA32, pixels.rgba.data(), pixels.w * 4);
            if (dense)
                SDL_AddSurfaceAlternateImage(surface, dense);
        }
        SDL_Cursor *made = surface ? SDL_CreateColorCursor(surface, points.hotX, points.hotY) : nullptr;
        if (dense)
            SDL_DestroySurface(dense);
        if (surface)
            SDL_DestroySurface(surface);
        if (made) {
            SDL_SetCursor(made);
            if (_sdlCursor)
                SDL_DestroyCursor(_sdlCursor);
            _sdlCursor = made;
            _sdlCursorScale = pixelScale;
            if (_cursorTexture) {
                SDL_DestroyTexture(_cursorTexture);
                _cursorTexture = nullptr;
            }
            _softwareCursor = false;
            setMouseCursorVisible(_cursorVisible);
            return;
        }
        SDL_Log("The game's cursor can't be the system's pointer (%s): drawing it", SDL_GetError());
    }
    // Drawn by us, over the picture, scaled with it.
    if (_sdlCursor) {
        SDL_SetCursor(SDL_GetDefaultCursor());
        SDL_DestroyCursor(_sdlCursor);
        _sdlCursor = nullptr;
        _sdlCursorScale = 0;
    }
    if (!_cursorTexture) {
        _cursorTexture =
            SDL_CreateTexture(_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, _cursor->w, _cursor->h);
        if (_cursorTexture) {
            SDL_UpdateTexture(_cursorTexture, nullptr, _cursor->rgba.data(), _cursor->w * 4);
            SDL_SetTextureBlendMode(_cursorTexture, SDL_BLENDMODE_BLEND);
        }
    }
    if (_cursorTexture)
        SDL_SetTextureScaleMode(_cursorTexture, _display.smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    _softwareCursor = true;
    if (!_mouseInside)
        _mouseInside = SDL_GetMouseFocus() == _window;
    setMouseCursorVisible(_cursorVisible);
}

void SdlSystem::drawSoftwareCursor() {
    if (!_softwareCursor || !_cursorTexture || !_cursor || !_cursorVisible || !_mouseInside || _relativeMouse)
        return;
    const SDL_FRect dst{std::floor(_mouseX) - float(_cursor->hotX), std::floor(_mouseY) - float(_cursor->hotY),
                        float(_cursor->w), float(_cursor->h)};
    SDL_RenderTexture(_renderer, _cursorTexture, nullptr, &dst);
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
    int w = 0;
    int h = 0;
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

bool SdlSystem::desktopMode(int &width, int &height, int &bits) {
    SDL_DisplayID display = _window ? SDL_GetDisplayForWindow(_window) : 0;
    if (!display)
        display = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode *mode = display ? SDL_GetDesktopDisplayMode(display) : nullptr;
    if (!mode || mode->w <= 0 || mode->h <= 0)
        return false;
    width = mode->w;
    height = mode->h;
    // In memory: 32 for the usual XRGB8888 (SDL_BITSPERPIXEL's 24 bits of
    // colour in 4 bytes), 16 for RGB565, 15 for XRGB1555.
    bits = SDL_BYTESPERPIXEL(mode->format) == 4 ? 32 : int(SDL_BITSPERPIXEL(mode->format));
    return bits > 0;
}

void SdlSystem::setRelativeMouse(bool on) {
    _relativeMouse = on;
    if (_window)
        SDL_SetWindowRelativeMouseMode(_window, on);
}

// A message box of the window's: the mouse is let go while it shows.
bool SdlSystem::confirmQuit(const std::string &text, bool toLauncher) {
    const bool held = _relativeMouse;
    if (_window && held)
        SDL_SetWindowRelativeMouseMode(_window, false);
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Keep playing"},
        {0, 1, toLauncher ? "Return to launcher" : "Quit"},
    };
    const SDL_MessageBoxData box{SDL_MESSAGEBOX_WARNING,
                                 _window,
                                 toLauncher ? "Return to the launcher?" : "Quit Reluva?",
                                 text.c_str(),
                                 SDL_arraysize(buttons),
                                 buttons,
                                 nullptr};
    int chosen = 0;
    if (!SDL_ShowMessageBox(&box, &chosen))
        chosen = 1; // no box to show: quit, as without asking
    const bool quit = chosen == 1;
    if (!quit) {
        _quitRequested = false;
        if (_window && held)
            SDL_SetWindowRelativeMouseMode(_window, true);
    }
    return quit;
}

uint64_t SdlSystem::getMillis() const {
    return SDL_GetTicks();
}

void SdlSystem::delayMillis(uint32_t ms) {
    SDL_Delay(ms);
}

void SDLCALL SdlSystem::audioThunk(void *userdata, SDL_AudioStream *stream, int additional, int) {
    auto *self = static_cast<SdlSystem *>(userdata);
    const int frameBytes = self->_audioChannels * static_cast<int>(sizeof(int16_t));
    const int frames = additional / frameBytes;
    if (frames <= 0)
        return;
    self->_audioBuffer.resize(static_cast<size_t>(frames) * size_t(self->_audioChannels));
    self->_audioCallback(self->_audioBuffer.data(), frames);
    SDL_PutAudioStreamData(stream, self->_audioBuffer.data(), frames * frameBytes);
}

Error SdlSystem::startAudio(AudioCallback callback) {
    return startAudioChannels(2, std::move(callback));
}

int SdlSystem::audioDeviceChannels() const {
    SDL_AudioSpec spec{};
    if (!SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr) || spec.channels <= 0)
        return 2;
    return spec.channels;
}

Error SdlSystem::startAudioChannels(int channels, AudioCallback callback) {
    stopAudio();
    if (channels < 1 || channels > 8)
        return Error::fail("Cannot open audio with " + std::to_string(channels) + " channels");
    _audioCallback = std::move(callback);
    _audioChannels = channels;
    const SDL_AudioSpec spec{SDL_AUDIO_S16, channels, kAudioRate};
    _audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audioThunk, this);
    if (!_audioStream) {
        _audioCallback = nullptr;
        return Error::fail(std::string("Cannot open audio: ") + SDL_GetError());
    }
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

std::shared_ptr<net::Sockets> SdlSystem::sockets() {
    static const std::shared_ptr<net::Sockets> kShared = net::systemSockets();
    return kShared;
}

} // namespace reluva
