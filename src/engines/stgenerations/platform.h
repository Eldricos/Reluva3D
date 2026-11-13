// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "graphics/cursor.h"
#include "graphics/draw.h"
#include "common/events.h"
#include "graphics/palette.h"
#include "common/stream.h"
#include "common/system.h"
#include "engines/stgenerations/canvas.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace reluva::stgenerations {

// What the game sees of the player's input, as the original's window
// procedure got it. Platform::pollEvent makes these from Reluva's events.
enum EventType {
    kEventInvalid = 0,
    kEventKeydown,
    kEventKeyup,
    kEventMousemove,
    kEventLbuttondown,
    kEventLbuttonup,
    kEventRbuttondown,
    kEventRbuttonup,
    kEventMbuttondown,
    kEventMbuttonup,
    kEventWheelup,
    kEventWheeldown,
    kEventQuit,             // the window closed
    kEventReturnToLauncher, // Ctrl+F5
    kEventFocusGained,
    kEventFocusLost,
};

// Modifier keys held (KeyState::flags).
enum { kKbdCtrl = 1 << 0, kKbdAlt = 1 << 1, kKbdShift = 1 << 2 };

struct KeyState {
    uint32_t keycode = key::Unknown; // common/events.h codes
    // The character typed, while text input is on (Platform::setTextInput),
    // for printable keys; Backspace, Tab, Return and Escape always.
    uint16_t ascii = 0;
    uint8_t flags = 0;
    KeyState() = default;
    KeyState(uint32_t k, uint16_t a = 0, uint8_t f = 0) : keycode(k), ascii(a), flags(f) {}
    bool hasFlags(uint8_t f) const { return (flags & f) == f; }
};

struct Event {
    EventType type = kEventInvalid;
    bool kbdRepeat = false;
    KeyState kbd;
    Point mouse; // in the game's 640x480
};

// The engine's side of Reluva's System, as the game uses it: one 640x480
// screen in 8-bit or true colour, its palette, the mouse pointer the game
// draws (shown by the System as its own pointer), the player's input and
// what's held, the time, the mixer and the folder for saves and settings.
// Tests make one over a System of their own.
class Platform {
public:
    // `mixer` may be null (no sound); `saveDir` is the game's own folder
    // (GameTarget::savePath).
    Platform(System &system, audio::Mixer *mixer, std::filesystem::path saveDir);

    System &system() { return _system; }

    // --- The screen -------------------------------------------------------
    // The screen's size and colours: 8-bit with a palette, or true colour
    // (0xAARRGGBB). Its pixels are kept until the next updateScreen().
    void initSize(int width, int height, Canvas::Format format = Canvas::kClut8);
    int getWidth() const { return _screen.w; }
    int getHeight() const { return _screen.h; }
    bool isTrueColour() const { return !_screen.isClut8(); }
    // `num` RGB triplets from `start`, 8 bits per channel.
    void setPalette(const uint8_t *colors, unsigned start, unsigned num);
    void grabPalette(uint8_t *colors, unsigned start, unsigned num) const;
    // Pixels in the screen's format onto it.
    void copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h);
    void updateScreen();
    void setWindowCaption(const std::string &caption);
    // The frames shown, for tests.
    const Canvas &screen() const { return _screen; }

    // --- The mouse pointer ------------------------------------------------
    // Returns whether it was shown.
    bool showMouse(bool visible);
    // The game's pointer: w x h pixels in the screen's format (8-bit:
    // coloured from the palette, as it is when it changes), `keycolor` see-
    // through, the hotspot at (hotspotX, hotspotY).
    void setMouseCursor(const void *buf, unsigned w, unsigned h, int hotspotX, int hotspotY, uint32_t keycolor);

    // --- Input ------------------------------------------------------------
    // The next event; false when none is pending.
    bool pollEvent(Event &event);
    void pushEvent(const Event &event) { _queue.push_back(event); }
    Point getMousePos() const { return _mouse; }
    // Bit 0: left, bit 1: right, bit 2: middle.
    int getButtonState() const { return _buttons; }
    int getModifierState() const { return _modifiers; }
    // Typed characters arrive in KeyState::ascii (for names typed in).
    void setTextInput(bool on);
    // The player closed the window or went back to the launcher.
    bool shouldQuit() const { return _quit; }
    void quitGame() { _quit = true; }

    // --- Time -------------------------------------------------------------
    uint32_t getMillis() const;
    void delayMillis(uint32_t ms);

    // --- Sound ------------------------------------------------------------
    audio::Mixer *getMixer() { return _mixer; }
    // Whether the mixer is heard: without a sound device nothing pulls it,
    // so the game plays nothing (as STG-Engine's mixer, not ready, did).
    void setSoundOutput(bool on) { _soundOutput = on; }
    bool hasSoundOutput() const { return _soundOutput; }

    // --- Saves and settings -----------------------------------------------
    // The game's own folder for its files (made when written to).
    const std::filesystem::path &saveDir() const { return _saveDir; }
    // A file of it read whole into a stream; nullptr if it isn't there.
    std::unique_ptr<SeekableReadStream> openForLoading(const std::string &name) const;
    // Written whole, made with the folder if needed; false if it can't be.
    bool saveFile(const std::string &name, const std::vector<uint8_t> &data);
    bool removeFile(const std::string &name);
    bool fileExists(const std::string &name) const;
    // Names of its files matching a pattern ('*' and '?'), sorted.
    std::vector<std::string> listFiles(const std::string &pattern) const;

private:
    void refreshCursor();
    Point clampToScreen(int x, int y) const;
    bool translate(const reluva::Event &in, Event &out);

    System &_system;
    audio::Mixer *_mixer;
    std::filesystem::path _saveDir;
    Canvas _screen;
    Surface _frame; // screen_ as the System takes it
    Palette _palette;
    bool _graphics = false;

    // The game's pointer, as it gave it, and shown or not.
    Canvas _cursorImage;
    int _cursorHotX = 0, _cursorHotY = 0;
    uint32_t _cursorKey = 0;
    bool _cursorSet = false;
    bool _mouseVisible = true;

    std::deque<Event> _queue;
    Point _mouse;
    int _buttons = 0;
    int _modifiers = 0;
    bool _textInput = false;
    bool _soundOutput = true;
    bool _quit = false;
};

} // namespace reluva::stgenerations
