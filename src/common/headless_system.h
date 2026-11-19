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
        width_ = width;
        height_ = height;
        return Error::success();
    }
    void setPalette(const Color*, int, int) override {}
    void updateScreen(const Surface&) override { ++frames_; }
    void setWindowTitle(const std::string& title) override { title_ = title; }
    void setMouseCursorVisible(bool visible) override { cursorVisible_ = visible; }
    void setMouseCursor(const Cursor* cursor) override {
        cursor_ = cursor ? *cursor : Cursor{};
        ++cursorChanges_;
    }
    bool pollEvent(Event&) override { return false; }
    uint64_t getMillis() const override { return millis_; }
    void delayMillis(uint32_t ms) override { millis_ += ms; }
    Error startAudio(AudioCallback) override { return Error::fail("no sound output"); }
    void stopAudio() override {}
    int audioRate() const override { return 22050; }

    int width() const { return width_; }
    int height() const { return height_; }
    int frames() const { return frames_; }
    const std::string& title() const { return title_; }
    // The game's cursor (empty: the system's), whether it's shown, and how
    // many times it was set.
    const Cursor& cursor() const { return cursor_; }
    bool cursorVisible() const { return cursorVisible_; }
    int cursorChanges() const { return cursorChanges_; }

private:
    int width_ = 0, height_ = 0, frames_ = 0;
    uint64_t millis_ = 0;
    std::string title_;
    Cursor cursor_;
    bool cursorVisible_ = true;
    int cursorChanges_ = 0;
};

} // namespace reluva
