// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/legoisland/host/player.h"

#include "engines/legoisland/host/game_process.h"

#include <algorithm>

namespace reluva::legoisland {

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
    width = _width;
    height = _height;
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

FramePacer::Clock::time_point FramePacer::next(Clock::time_point now) {
    if (_rate <= 0)
        return now;
    const auto frame = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / _rate));
    const Clock::time_point at = std::max(_next, now);
    _next = at + frame;
    return at;
}

void Player::work() {
    GameProcess game(_dir);
    game.setTurningByTime(_turningByTime);
    if (!_saveDir.empty())
        game.setSaveFolder(_saveDir);
    game.directSound().mixer = _mixer;
    game.process().joystick.present = _joystick;
    for (const auto &[name, value] : _settings)
        game.setSetting(name, value);
    x86::Win32Process &process = game.process();
    process.output = [this](const std::string &text) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_output.size() < (1u << 20))
            _output += text + (text.empty() || text.back() != '\n' ? "\n" : "");
    };
    // Each frame shown is kept for the engine, then the input waiting is
    // taken; it's also taken whenever the program looks for a message and
    // finds none.
    game.directX().presented = [this](const x86::DirectX::Frame &f) {
        const auto now = FramePacer::Clock::now();
        if (const auto at = _pacer.next(now); at > now)
            std::this_thread::sleep_until(at);
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _frame = f.pixels;
            _width = f.width;
            _height = f.height;
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
    // The pointer is where it is in the game's picture, which fills its
    // window's client area.
    auto moveTo = [&p](const Event &ev) {
        int left = 0;
        int top = 0;
        if (const auto area = p.clientArea(p.mainWindow())) {
            left = (*area)[0];
            top = (*area)[1];
        }
        p.mouseMoveTo(left + ev.mouseX, top + ev.mouseY);
    };
    for (const Event &ev : events) {
        switch (ev.type) {
        case EventType::kMouseMove:
            moveTo(ev);
            break;
        case EventType::kMouseDown:
        case EventType::kMouseUp: {
            const int button = ev.button == MouseButton::kLeft     ? 0
                               : ev.button == MouseButton::kRight  ? 1
                               : ev.button == MouseButton::kMiddle ? 2
                                                                   : -1;
            moveTo(ev);
            if (button >= 0)
                p.mouseButton(button, ev.type == EventType::kMouseDown);
            break;
        }
        case EventType::kJoyAxis:
            p.joystickAxis(ev.joyAxis, ev.joyValue);
            break;
        case EventType::kJoyButtonDown:
        case EventType::kJoyButtonUp:
            p.joystickButton(ev.joyButton, ev.type == EventType::kJoyButtonDown);
            break;
        case EventType::kJoyHat:
            p.joystickHat(ev.joyHat);
            break;
        case EventType::kJoyRemoved: // let go: centred, nothing held
            p.joystick.axes[0] = p.joystick.axes[1] = 32768;
            p.joystick.buttons = 0;
            p.joystick.pov = 0xffff;
            break;
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

} // namespace reluva::legoisland
