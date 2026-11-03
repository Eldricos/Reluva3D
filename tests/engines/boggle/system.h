// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/system.h"
#include "tests/support/record.h"
#include <functional>

namespace reluva::boggle::fixtures {
inline Event keyEvent(uint32_t key) { Event e; e.type = EventType::KeyDown; e.key = key; return e; }
inline Event textEvent(std::string_view text) {
    Event e; e.type = EventType::TextInput;
    for (size_t i = 0; i < text.size() && i + 1 < sizeof(e.text); ++i) e.text[i] = text[i];
    return e;
}
inline Event mouseEvent(EventType type, int x, int y) {
    Event e; e.type = type; e.button = MouseButton::Left; e.mouseX = x; e.mouseY = y; return e;
}
class ScriptedSystem : public System {
public:
    std::function<std::vector<Event>(size_t)> events;
    std::function<uint64_t(size_t)> clock;
    std::function<void(const Surface&,size_t)> present;
    size_t frame = 0;
    std::vector<std::string> hashes;
    bool textEnabled = false, textStopped = false;
    int w = 0, h = 0;
    Error initGraphics(int width, int height, PixelFormat format) override {
        w = width; h = height;
        return format == PixelFormat::RGBA32 ? Error::success() : Error::fail("Expected RGBA screen");
    }
    void setPalette(const Color*, int, int) override {}
    void setWindowTitle(const std::string&) override {}
    void updateScreen(const Surface& screen) override {
        hashes.push_back(test::Hash().bytes(screen.pixels).hex());
        if (present) present(screen,frame);
        ++frame;
    }
    bool pollEvent(Event& event) override {
        if (loaded_ != frame) {
            loaded_ = frame; at_ = 0;
            queued_ = frame >= 30 ? std::vector<Event>{keyEvent(key::Escape)} : events(frame);
        }
        if (at_ == queued_.size()) return false;
        event = queued_[at_++]; return true;
    }
    void setTextInput(bool enabled) override { textEnabled = enabled; if (!enabled) textStopped = true; }
    uint64_t getMillis() const override { return clock ? clock(frame) : frame * 10; }
    void delayMillis(uint32_t) override {}
    Error startAudio(AudioCallback) override { return Error::fail("No audio in scripted system"); }
    void stopAudio() override {}
    int audioRate() const override { return 22050; }
private:
    size_t loaded_ = size_t(-1), at_ = 0;
    std::vector<Event> queued_;
};
} // namespace reluva::boggle::fixtures
