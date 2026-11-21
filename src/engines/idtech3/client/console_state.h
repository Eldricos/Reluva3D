// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/types.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace reluva::idtech3 {

/**
 * The engine's console as the Play pages keep it: the lines printed (the
 * last thousand) and the recent ones shown over the game, the line being
 * typed with what was typed before, how far it's scrolled back and how
 * far it's dropped down (docs/engines/idtech3/games/jk2/systems/console.md).
 * Drawing is console_drawer's.
 */
class ConsoleState {
public:
    /**
     * Text printed: whole lines into the console, each print stream (a
     * game's, its client game's) keeping its own unfinished line.
     * @param notify whether its lines are also shown over the game a while.
     */
    void print(const std::string &text, bool notify, uint64_t nowMs, int stream = 0);
    /** Forgets the unfinished line of a stream (a game that stopped). */
    void dropPartial(int stream) { _partial[size_t(stream) & 1].clear(); }
    void clear() {
        _lines.clear();
        _scroll = 0;
    }
    void clearNotify() { _notify.clear(); }
    const std::deque<std::string> &lines() const { return _lines; }
    /**
     * Rows back from the newest: each printed line as many rows as it
     * wraps to at the width setWrapWidth() gave (one each without).
     */
    int scroll() const { return _scroll; }
    /** The console's width in characters, which lines wrap at (0: none). */
    void setWrapWidth(int chars) { _wrapWidth = chars; }

    /** The lines shown over the game: those printed within `forMs`. */
    std::vector<std::string> notify(uint64_t nowMs, float forMs) const;
    /** Forgets notify lines older than `forMs`. */
    void expireNotify(uint64_t nowMs, uint64_t forMs);
    const std::deque<std::pair<uint64_t, std::string>> &notifyLines() const { return _notify; }

    // --- Typing ----------------------------------------------------------
    /**
     * Typing starts afresh; `swallow` is the character of the key that
     * started it, `width` how many characters the field shows.
     */
    void beginTyping(uint32_t swallow, int width = 78);
    void clearField();
    const std::string &field() const { return _field; }
    size_t cursor() const { return _cursor; }
    /**
     * The first character the field shows, as the original keeps it: moved
     * by the keys, and pulled back as it's drawn when the line is shorter
     * than it and the width (which this does).
     */
    int fieldScroll();
    /** Whether typing replaces characters (Insert flips it). */
    bool overstrike() const { return _overstrike; }
    /** What a key or text event did to the line being typed. */
    enum class Action { kNone, kSubmit, kClose };
    /**
     * A key or text event while typing.
     * @param console the console's own keys too: history, scrolling, the
     *                console key closing it.
     */
    Action handle(const Event &ev, bool console);
    /**
     * The line typed taken, and in the console kept: in its history and
     * printed after a "]".
     */
    std::string submit(bool console);

    /** How far it's dropped down (0 shut, 0.5 open), moved `speed` screens a second towards `open`. */
    float slide(uint64_t nowMs, bool open, float speed);

private:
    void addLine(const std::string &line);
    int rows(const std::string &line) const;
    int totalRows() const;

    std::deque<std::string> _lines;
    std::deque<std::pair<uint64_t, std::string>> _notify;
    std::string _partial[2];
    char _partialColour[2] = {'7', '7'}; // the colour each unfinished line ends in
    int _scroll = 0;
    std::string _field;
    size_t _cursor = 0;
    int _fieldScroll = 0;
    int _fieldWidth = 78;
    bool _overstrike = false;
    int _wrapWidth = 0;
    uint32_t _swallow = 0;
    struct Typed {
        std::string text;
        size_t cursor = 0;
        int scroll = 0;
    };
    std::vector<Typed> _history;
    size_t _historyPos = 0;
    float _frac = 0;
    uint64_t _frameMs = 0;
};

/**
 * Text in the original's colours (^0 to ^7, white to start) with a black
 * shadow, in Reluva's own font, for a game whose console Reluva draws its
 * own way (GameInfo::consoleVersion empty).
 */
void drawColouredText(Surface &surface, int x, int y, const std::string &line);
/**
 * That console over the top half of `surface`: the lines printed, newest
 * at the bottom, and the line being typed, its cursor shown when `blink`.
 */
void drawOwnConsole(Surface &surface, const ConsoleState &console, bool blink);

} // namespace reluva::idtech3
