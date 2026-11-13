// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/netimmerse/alphateam/player.h"

#include "engines/netimmerse/alphateam/game_process.h"

#include <algorithm>
#include <cmath>

namespace reluva::netimmerse::alphateam {

Player::Player(std::filesystem::path gameDir, std::filesystem::path saveDir, audio::Mixer *mixer)
    : _dir(std::move(gameDir)), _saveDir(std::move(saveDir)), _mixer(mixer) {
}

Player::~Player() {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stopping = true;
        if (_game)
            _game->cpu().requestStop();
    }
    _resume.notify_all();
    if (_thread.joinable())
        _thread.join();
}

void Player::start() {
    _thread = std::thread([this] { work(); });
}

void Player::input(const Event &event) {
    std::lock_guard<std::mutex> lock(_mutex);
    _events.push_back(event);
}

bool Player::frame(std::vector<uint32_t> &pixels, int &width, int &height) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_frameNumber == _frameTaken)
        return false;
    _frameTaken = _frameNumber;
    pixels = _frame;
    width = _width, height = _height;
    return true;
}

uint64_t Player::framesShown() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _frameNumber;
}

void Player::pause(bool paused) {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _paused = paused;
    }
    _resume.notify_all();
}

bool Player::finished(bool &ended, std::string &error) {
    std::lock_guard<std::mutex> lock(_mutex);
    ended = _ended;
    error = _error;
    return _finished;
}

std::string Player::takeOutput() {
    std::lock_guard<std::mutex> lock(_mutex);
    std::string out;
    out.swap(_output);
    return out;
}

void Player::work() {
    GameProcess game(_dir);
    game.setSaveFolder(_saveDir);
    game.a3d().mixer = _mixer;
    x86::Win32Process &process = game.process();
    process.output = [this](const std::string &text) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_output.size() < (1u << 20))
            _output += text + (text.empty() || text.back() != '\n' ? "\n" : "");
    };
    // Each frame shown: kept for the engine; then the input waiting, and
    // a pause if one's asked for. The input is also taken whenever the
    // program looks for a message and finds none.
    // Frames shown a flip late: a frame's drawing goes on while the program
    // works out the next, about a quarter quicker, for a frame's delay.
    game.directX().presentLate = true;
    game.directX().presented = [this](const x86::DirectX::Frame &f) {
        // A flip waits for the display's vertical blank, as the game's
        // (DDFLIP_WAIT) did on a 60 Hz display: no more frames than are
        // shown, rather than the processor's fill drawing ones nobody sees.
        // A frame that came late doesn't make the next ones hurry.
        {
            using namespace std::chrono;
            constexpr auto kFrame = duration_cast<steady_clock::duration>(duration<double>(1.0 / 60));
            const auto now = steady_clock::now();
            if (_nextFlip > now)
                std::this_thread::sleep_until(_nextFlip);
            _nextFlip = std::max(_nextFlip, now) + kFrame;
        }
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _frame = f.pixels;
            _width = f.width, _height = f.height;
            ++_frameNumber;
            _resume.wait(lock, [this] { return !_paused || _stopping; });
        }
        drain();
    };
    process.idle = [this] { drain(); };
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _game = &game;
        if (_stopping)
            game.cpu().requestStop();
    }
    Error e = game.load();
    if (!e)
        e = game.attach();
    uint32_t exitCode = 0;
    if (!e)
        e = game.run(exitCode);
    std::lock_guard<std::mutex> lock(_mutex);
    _game = nullptr;
    _finished = true;
    _ended = !e || process.exited.has_value();
    if (e && !_ended && !_stopping)
        _error = e.message;
}

void Player::drain() {
    std::deque<Event> events;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        events.swap(_events);
    }
    if (!_game || events.empty())
        return;
    x86::Win32Process &p = _game->process();
    for (const Event &ev : events) {
        switch (ev.type) {
        case EventType::MouseMove: {
            // The game reads the pointer's movement, so that's what's
            // passed on: whole pixels, keeping the fractions for later.
            _pendingX += ev.moveX;
            _pendingY += ev.moveY;
            const int dx = int(std::trunc(_pendingX));
            const int dy = int(std::trunc(_pendingY));
            _pendingX -= float(dx);
            _pendingY -= float(dy);
            if (dx || dy)
                p.mouseMoveTo(p.cursorX + dx, p.cursorY + dy);
            break;
        }
        case EventType::MouseDown:
        case EventType::MouseUp: {
            const int button = ev.button == MouseButton::Left     ? 0
                               : ev.button == MouseButton::Right  ? 1
                               : ev.button == MouseButton::Middle ? 2
                                                                  : -1;
            if (button >= 0)
                p.mouseButton(button, ev.type == EventType::MouseDown);
            break;
        }
        case EventType::MouseWheel:
            p.mouseWheel(ev.wheelY * 120);
            break;
        case EventType::KeyDown:
        case EventType::KeyUp: {
            uint32_t vk = 0;
            uint32_t scan = 0;
            if (x86::windowsKey(ev.key, vk, scan))
                p.key(vk, scan, ev.type == EventType::KeyDown);
            break;
        }
        default:
            break;
        }
    }
}

} // namespace reluva::netimmerse::alphateam
