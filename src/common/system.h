// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/types.h"

#include <cstdint>
#include <functional>
#include <string>

namespace reluva {

// Everything an engine may ask of the platform. Engines must never include
// SDL (or any other backend) headers; they go through this interface so the
// backend can be swapped or extended (GPU rendering, consoles, web...).
class System {
public:
    virtual ~System() = default;

    // --- Graphics -----------------------------------------------------------
    // Set up a game framebuffer of the given size. It is scaled to the window.
    virtual Error initGraphics(int width, int height, PixelFormat format) = 0;
    virtual void setPalette(const Color* colors, int start, int count) = 0;
    // Copy the surface to the screen and present it.
    virtual void updateScreen(const Surface& surface) = 0;
    virtual void setWindowTitle(const std::string& title) = 0;

    // --- Input --------------------------------------------------------------
    // Returns false when no more events are pending.
    virtual bool pollEvent(Event& event) = 0;

    // --- Time ---------------------------------------------------------------
    virtual uint64_t getMillis() const = 0;
    virtual void delayMillis(uint32_t ms) = 0;

    // --- Audio --------------------------------------------------------------
    // The callback runs on the audio thread and must fill `frames` stereo
    // frames of interleaved signed 16-bit samples at audioRate().
    using AudioCallback = std::function<void(int16_t* out, int frames)>;
    virtual Error startAudio(AudioCallback callback) = 0;
    virtual void stopAudio() = 0;
    virtual int audioRate() const = 0;
};

} // namespace reluva
