// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/netimmerse/alphateam/player.h"

#include "engines/netimmerse/alphateam/game_process.h"

#include <cmath>

namespace reluva::netimmerse::alphateam {

Player::Player(std::filesystem::path gameDir, audio::Mixer* mixer) : dir_(std::move(gameDir)), mixer_(mixer) {}

Player::~Player() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        if (game_)
            game_->cpu().requestStop();
    }
    resume_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

void Player::start() { thread_ = std::thread([this] { work(); }); }

void Player::input(const Event& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(event);
}

bool Player::frame(std::vector<uint32_t>& pixels, int& width, int& height) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (frameNumber_ == frameTaken_)
        return false;
    frameTaken_ = frameNumber_;
    pixels = frame_;
    width = width_, height = height_;
    return true;
}

void Player::pause(bool paused) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        paused_ = paused;
    }
    resume_.notify_all();
}

bool Player::finished(bool& ended, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    ended = ended_;
    error = error_;
    return finished_;
}

std::string Player::takeOutput() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string out;
    out.swap(output_);
    return out;
}

void Player::work() {
    GameProcess game(dir_);
    game.a3d().mixer = mixer_;
    x86::Win32Process& process = game.process();
    process.output = [this](const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (output_.size() < (1u << 20))
            output_ += text + (text.empty() || text.back() != '\n' ? "\n" : "");
    };
    // Each frame shown: kept for the engine; then the input waiting, and
    // a pause if one's asked for. The input is also taken whenever the
    // program looks for a message and finds none.
    game.directX().presented = [this](const x86::DirectX::Frame& f) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            frame_ = f.pixels;
            width_ = f.width, height_ = f.height;
            ++frameNumber_;
            resume_.wait(lock, [this] { return !paused_ || stopping_; });
        }
        drain();
    };
    process.idle = [this] { drain(); };
    {
        std::lock_guard<std::mutex> lock(mutex_);
        game_ = &game;
        if (stopping_)
            game.cpu().requestStop();
    }
    Error e = game.load();
    if (!e)
        e = game.attach();
    uint32_t exitCode = 0;
    if (!e)
        e = game.run(exitCode);
    std::lock_guard<std::mutex> lock(mutex_);
    game_ = nullptr;
    finished_ = true;
    ended_ = !e || process.exited.has_value();
    if (e && !ended_ && !stopping_)
        error_ = e.message;
}

void Player::drain() {
    std::deque<Event> events;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events.swap(events_);
    }
    if (!game_ || events.empty())
        return;
    x86::Win32Process& p = game_->process();
    for (const Event& ev : events) {
        switch (ev.type) {
        case EventType::MouseMove: {
            // The game reads the pointer's movement, so that's what's
            // passed on: whole pixels, keeping the fractions for later.
            pendingX_ += ev.moveX;
            pendingY_ += ev.moveY;
            const int dx = int(std::trunc(pendingX_)), dy = int(std::trunc(pendingY_));
            pendingX_ -= float(dx);
            pendingY_ -= float(dy);
            if (dx || dy)
                p.mouseMoveTo(p.cursorX + dx, p.cursorY + dy);
            break;
        }
        case EventType::MouseDown:
        case EventType::MouseUp: {
            const int button = ev.button == MouseButton::Left    ? 0
                               : ev.button == MouseButton::Right ? 1
                               : ev.button == MouseButton::Middle ? 2
                                                                  : -1;
            if (button >= 0)
                p.mouseButton(button, ev.type == EventType::MouseDown);
            break;
        }
        case EventType::MouseWheel: p.mouseWheel(ev.wheelY * 120); break;
        case EventType::KeyDown:
        case EventType::KeyUp: {
            uint32_t vk = 0, scan = 0;
            if (x86::windowsKey(ev.key, vk, scan))
                p.key(vk, scan, ev.type == EventType::KeyDown);
            break;
        }
        default: break;
        }
    }
}

} // namespace reluva::netimmerse::alphateam
