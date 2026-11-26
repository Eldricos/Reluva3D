// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sfc/host/player.h"

#include "engines/sfc/host/game_process.h"

namespace reluva::sfc {

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
    GameProcess game(_dir, GameProcess::kMemoryMb);
    game.setSaveFolder(_saveDir);
    game.miles().mixer = _mixer;
    game.directPlay().setNetwork(_sockets, _broadcast);
    game.winsock().setSockets(_sockets);
    x86::Win32Process &process = game.process();
    process.output = [this](const std::string &text) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_output.size() < (1u << 20))
            _output += text + (text.empty() || text.back() != '\n' ? "\n" : "");
    };
    // Each frame shown is kept for the engine, then the input waiting is
    // passed on; it's also passed on whenever the program looks for a
    // message and finds none.
    game.directX().presented = [this](const x86::DirectX::Frame &f) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _frame = f.pixels;
            _width = f.width, _height = f.height;
            ++_frameNumber;
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
        // The game's window fills its screen from the corner, so a point
        // on the frame is the same on the screen.
        case EventType::kMouseMove:
            p.mouseMoveTo(ev.mouseX, ev.mouseY);
            break;
        case EventType::kMouseDown:
        case EventType::kMouseUp: {
            const int button = ev.button == MouseButton::kLeft     ? 0
                               : ev.button == MouseButton::kRight  ? 1
                               : ev.button == MouseButton::kMiddle ? 2
                                                                   : -1;
            if (button < 0)
                break;
            if (ev.mouseX != p.cursorX || ev.mouseY != p.cursorY)
                p.mouseMoveTo(ev.mouseX, ev.mouseY);
            p.mouseButton(button, ev.type == EventType::kMouseDown);
            break;
        }
        case EventType::kMouseWheel:
            p.mouseWheel(ev.wheelY * 120); // Windows' WHEEL_DELTA a notch
            break;
        case EventType::kKeyDown:
        case EventType::kKeyUp: {
            uint32_t vk = 0;
            uint32_t scan = 0;
            if (x86::windowsKey(ev.key, vk, scan))
                p.key(vk, scan, ev.type == EventType::kKeyDown);
            break;
        }
        default:
            break;
        }
    }
}

} // namespace reluva::sfc
