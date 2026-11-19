// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/system.h"

#include <cstdint>
#include <string>

namespace reluva {

// A System with no window and no sound, whose clock moves only as the
// engine waits (delayMillis): for running an engine in reluva-tool and the
// tests, the same on every machine. It counts the frames shown and keeps
// the last title.
class HeadlessSystem : public System {
public:
    Error initGraphics(int width, int height, PixelFormat) override {
        _width = width;
        _height = height;
        return Error::success();
    }
    void setPalette(const Color *, int, int) override {}
    void updateScreen(const Surface &) override { ++_frames; }
    void setWindowTitle(const std::string &title) override { _title = title; }
    void setMouseCursorVisible(bool visible) override { _cursorVisible = visible; }
    void setMouseCursor(const Cursor *cursor) override {
        _cursor = cursor ? *cursor : Cursor{};
        ++_cursorChanges;
    }
    bool pollEvent(Event &) override { return false; }
    uint64_t getMillis() const override { return _millis; }
    void delayMillis(uint32_t ms) override { _millis += ms; }
    Error startAudio(AudioCallback) override { return Error::fail("no sound output"); }
    void stopAudio() override {}
    int audioRate() const override { return 22050; }

    int width() const { return _width; }
    int height() const { return _height; }
    int frames() const { return _frames; }
    const std::string &title() const { return _title; }
    // The game's cursor (empty: the system's), whether it's shown, and how
    // many times it was set.
    const Cursor &cursor() const { return _cursor; }
    bool cursorVisible() const { return _cursorVisible; }
    int cursorChanges() const { return _cursorChanges; }

private:
    int _width = 0;
    int _height = 0;
    int _frames = 0;
    uint64_t _millis = 0;
    std::string _title;
    Cursor _cursor;
    bool _cursorVisible = true;
    int _cursorChanges = 0;
};

} // namespace reluva
