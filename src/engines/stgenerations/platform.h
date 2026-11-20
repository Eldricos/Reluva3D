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

/**
 * What the game sees of the player's input, as the original's window
 * procedure got it. Platform::pollEvent makes these from Reluva's events.
 */
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

/** Modifier keys held (KeyState::flags). */
enum { kKbdCtrl = 1 << 0, kKbdAlt = 1 << 1, kKbdShift = 1 << 2 };

/** A key pressed or let go, or a character typed. */
struct KeyState {
    uint32_t keycode = key::kUnknown; // common/events.h codes
    // The character typed, while text input is on (Platform::setTextInput),
    // for printable keys; Backspace, Tab, Return and Escape always.
    uint16_t ascii = 0;
    uint8_t flags = 0;
    /** No key, or a key code with its character and modifier flags. */
    ///@{
    KeyState() = default;
    KeyState(uint32_t k, uint16_t a = 0, uint8_t f = 0) : keycode(k), ascii(a), flags(f) {}
    ///@}
    /** Whether all of the modifiers `f` are held. */
    bool hasFlags(uint8_t f) const { return (flags & f) == f; }
};

/** One of the player's inputs, with where the mouse was. */
struct Event {
    EventType type = kEventInvalid;
    bool kbdRepeat = false;
    KeyState kbd;
    Point mouse; // in the game's 640x480
};

/**
 * The engine's side of Reluva's System, as the game uses it: one 640x480
 * screen in 8-bit or true colour, its palette, the mouse pointer the game
 * draws (shown by the System as its own pointer), the player's input and
 * what's held, the time, the mixer and the folder for saves and settings.
 * Tests make one over a System of their own.
 */
class Platform {
public:
    /**
     * @param mixer May be null (no sound); not owned.
     * @param saveDir The game's own folder (GameTarget::savePath); empty for
     *        none, when nothing is read or written.
     */
    Platform(System &system, audio::Mixer *mixer, std::filesystem::path saveDir);

    /** The System underneath. */
    System &system() { return _system; }

    // The screen.

    /**
     * Sets the screen's size and colours: 8-bit with a palette, or true
     * colour (0xAARRGGBB). A new screen starts black, in the palette last
     * set; the same size and format again leaves it as it is.
     */
    void initSize(int width, int height, Canvas::Format format = Canvas::kClut8);
    /** The screen's size, and whether it's in true colour. */
    ///@{
    int getWidth() const { return _screen.w; }
    int getHeight() const { return _screen.h; }
    bool isTrueColour() const { return !_screen.isClut8(); }
    ///@}
    /** `num` RGB triplets from `start`, 8 bits per channel; past 255 is left out. */
    ///@{
    void setPalette(const uint8_t *colors, unsigned start, unsigned num);
    void grabPalette(uint8_t *colors, unsigned start, unsigned num) const;
    ///@}
    /** Copies pixels in the screen's format, `pitch` bytes apart, onto it. */
    void copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h);
    /** Shows what's been drawn on the screen. */
    void updateScreen();
    /** The window's title. */
    void setWindowCaption(const std::string &caption);
    /** The screen as drawn, for tests. */
    const Canvas &screen() const { return _screen; }

    // The mouse pointer.

    /** Shows or hides the pointer; returns whether it was shown. */
    bool showMouse(bool visible);
    /**
     * The game's pointer: w x h 8-bit pixels, coloured from the palette (as
     * it is when it changes), `keycolor` see-through, the hotspot at
     * (hotspotX, hotspotY).
     */
    void setMouseCursor(const void *buf, unsigned w, unsigned h, int hotspotX, int hotspotY, uint32_t keycolor);

    // Input.

    /** The next event, those pushed first; false when none is pending. */
    bool pollEvent(Event &event);
    /** An event for pollEvent() to give before the System's. */
    void pushEvent(const Event &event) { _queue.push_back(event); }
    /** Where the mouse is, in the screen. */
    Point getMousePos() const { return _mouse; }
    /** The buttons held: bit 0 left, bit 1 right, bit 2 middle. */
    int getButtonState() const { return _buttons; }
    /** The modifier keys held at the last key event (kKbdCtrl and the rest). */
    int getModifierState() const { return _modifiers; }
    /** Typed characters arrive in KeyState::ascii (for names typed in). */
    void setTextInput(bool on);
    /** Whether the player closed the window or went back to the launcher, or quitGame() was called. */
    bool shouldQuit() const { return _quit; }
    /** Makes shouldQuit() true. */
    void quitGame() { _quit = true; }

    // Time.

    /** The System's clock, in ms, and waiting on it. */
    ///@{
    uint32_t getMillis() const;
    void delayMillis(uint32_t ms);
    ///@}

    // Sound.

    /** The mixer, or null with no sound. */
    audio::Mixer *getMixer() { return _mixer; }
    /**
     * Whether the mixer is heard: without a sound device nothing pulls it,
     * so the game plays nothing (as STG-Engine's mixer, not ready, did).
     */
    ///@{
    void setSoundOutput(bool on) { _soundOutput = on; }
    bool hasSoundOutput() const { return _soundOutput; }
    ///@}

    // Saves and settings.

    /** The game's own folder for its files (made when written to). */
    const std::filesystem::path &saveDir() const { return _saveDir; }
    /** A file of it read whole into a stream; nullptr if it isn't there. */
    std::unique_ptr<SeekableReadStream> openForLoading(const std::string &name) const;
    /**
     * Writes a file whole, making the folder if needed; false if it can't
     * be. A failed write leaves the file as it was.
     */
    bool saveFile(const std::string &name, const std::vector<uint8_t> &data);
    /** Removes a file; false if it wasn't there or can't be. */
    bool removeFile(const std::string &name);
    /** Whether a file is there. */
    bool fileExists(const std::string &name) const;
    /** Names of its files matching a pattern ('*' and '?'), sorted. */
    std::vector<std::string> listFiles(const std::string &pattern) const;

private:
    void refreshCursor();
    Point clampToScreen(int x, int y) const;
    bool translate(const reluva::Event &in, Event &out);

    System &_system;
    audio::Mixer *_mixer;
    std::filesystem::path _saveDir;
    Canvas _screen;
    Surface _frame; // _screen as the System takes it
    Palette _palette;
    bool _graphics = false;

    // The game's pointer, as it gave it, and shown or not.
    Canvas _cursorImage;
    int _cursorHotX = 0;
    int _cursorHotY = 0;
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
