// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/headless_system.h"
#include "common/types.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace reluva::stgenerations {

// A System without a window for running the game in reluva-tool and the
// tests: its clock moves only as the game waits, the mixer is pulled as
// time passes (so sounds end when they would), events arrive when their
// time comes, and the last frame shown is kept, as RGBA, with every
// frame's checksum.
class ScriptedSystem : public HeadlessSystem {
public:
    struct Scheduled {
        uint64_t at;
        reluva::Event event;
    };

    void attachMixer(audio::Mixer* mixer) { mixer_ = mixer; }
    // At `at` ms: an event (a Quit ends the game).
    void schedule(uint64_t at, const reluva::Event& e) { events_.push_back({at, e}); }
    void key(uint64_t at, uint32_t code) {
        reluva::Event e;
        e.type = reluva::EventType::KeyDown;
        e.key = code;
        schedule(at, e);
        e.type = reluva::EventType::KeyUp;
        schedule(at + 50, e);
    }
    void click(uint64_t at, int x, int y, MouseButton button = MouseButton::Left) {
        reluva::Event e;
        e.type = reluva::EventType::MouseMove;
        e.mouseX = x, e.mouseY = y;
        schedule(at, e);
        e.type = reluva::EventType::MouseDown;
        e.button = button;
        e.clicks = 1;
        schedule(at + 20, e);
        e.type = reluva::EventType::MouseUp;
        schedule(at + 70, e);
    }
    // Typed text, a character at a time, while the game takes text.
    void type(uint64_t at, const std::string& text) {
        for (char c : text) {
            reluva::Event e;
            e.type = reluva::EventType::TextInput;
            e.text[0] = c;
            schedule(at, e);
            at += 30;
        }
    }
    void quit(uint64_t at) {
        reluva::Event e;
        e.type = reluva::EventType::Quit;
        schedule(at, e);
    }
    // Called with each frame shown, and its time.
    std::function<void(const Surface& rgba, uint64_t ms)> onFrame;

    Error initGraphics(int width, int height, PixelFormat format) override {
        format_ = format;
        rgba_.create(width, height, PixelFormat::RGBA32);
        return HeadlessSystem::initGraphics(width, height, format);
    }
    void setPalette(const Color* colors, int start, int count) override {
        for (int i = 0; i < count && start + i < 256; i++)
            palette_[start + i] = colors[i];
    }
    void updateScreen(const Surface& s) override {
        HeadlessSystem::updateScreen(s);
        if (s.w != rgba_.w || s.h != rgba_.h)
            return;
        for (int y = 0; y < s.h; y++) {
            const uint8_t* src = s.row(y);
            uint8_t* dst = rgba_.row(y);
            for (int x = 0; x < s.w; x++) {
                if (s.format == PixelFormat::CLUT8) {
                    const Color c = palette_[src[x]];
                    dst[x * 4] = c.r, dst[x * 4 + 1] = c.g, dst[x * 4 + 2] = c.b, dst[x * 4 + 3] = 255;
                } else {
                    std::copy_n(src + x * 4, 4, dst + x * 4);
                }
            }
        }
        if (onFrame)
            onFrame(rgba_, getMillis());
    }
    bool pollEvent(reluva::Event& e) override {
        for (auto it = events_.begin(); it != events_.end(); ++it) {
            if (it->at <= getMillis()) {
                e = it->event;
                events_.erase(it);
                return true;
            }
        }
        return false;
    }
    // Sound output as a backend's: pulled as time passes.
    Error startAudio(AudioCallback callback) override {
        audio_ = std::move(callback);
        return Error::success();
    }
    void stopAudio() override { audio_ = nullptr; }
    void delayMillis(uint32_t ms) override {
        HeadlessSystem::delayMillis(ms);
        // The mixer plays on as time passes.
        if (mixer_ || audio_) {
            pending_ += uint64_t(ms) * uint64_t(audioRate());
            const int frames = int(pending_ / 1000);
            pending_ %= 1000;
            if (frames > 0) {
                scratch_.resize(size_t(frames) * 2);
                if (audio_)
                    audio_(scratch_.data(), frames);
                else
                    mixer_->mix(scratch_.data(), frames);
            }
        }
    }
    // The last frame shown, as RGBA.
    const Surface& lastFrame() const { return rgba_; }
    bool pending() const { return !events_.empty(); }

private:
    std::deque<Scheduled> events_;
    audio::Mixer* mixer_ = nullptr;
    AudioCallback audio_;
    uint64_t pending_ = 0;
    std::vector<int16_t> scratch_;
    PixelFormat format_ = PixelFormat::CLUT8;
    Surface rgba_;
    Color palette_[256] = {};
};

} // namespace reluva::stgenerations
