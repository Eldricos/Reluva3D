// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/host/player.h"

#include "audio/mixer.h"
#include "engines/sith/host/game_process.h"

#include <algorithm>
#include <cmath>

namespace reluva::sith {

Player::Player(GameFolders folders, std::filesystem::path saveFolder, audio::Mixer *mixer)
    : _folders(std::move(folders)), _saveFolder(std::move(saveFolder)), _mixer(mixer) {
}

Player::~Player() {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stopping = true;
        if (_game) {
            _game->cpu().requestStop();
            _game->process().requestExit(); // for a thread waiting for a message
        }
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

bool Player::pointerShown() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _pointerShown;
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
    GameProcess game(_folders, _saveFolder);
    x86::Win32Process &process = game.process();
    process.output = [this](const std::string &text) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_output.size() < (1u << 20))
            _output += text + (text.empty() || text.back() != '\n' ? "\n" : "");
    };
    // Each frame shown: kept for the engine; then the input waiting. The
    // input is also taken whenever the program looks for a message and
    // finds none, as it does while its menus wait.
    game.directX().presented = [this](const x86::DirectX::Frame &f) {
        // No more frames than a 60 Hz display shows: the game's flips
        // waited for its vertical blank. A late frame doesn't make the next
        // ones hurry.
        {
            using namespace std::chrono;
            constexpr auto kFrame = duration_cast<steady_clock::duration>(duration<double>(1.0 / 60));
            const auto now = steady_clock::now();
            if (_nextFlip > now)
                std::this_thread::sleep_until(_nextFlip);
            _nextFlip = std::max(_nextFlip, now) + kFrame;
        }
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
        if (_stopping) {
            game.cpu().requestStop();
            process.requestExit();
        }
    }
    // The sound card's output, for as long as the game runs.
    const audio::SoundHandle sound =
        _mixer ? _mixer->playOutput(game.directSound().output(), audio::SoundType::kEffects) : audio::kNoSound;
    Error e = game.load();
    uint32_t exitCode = 0;
    if (!e)
        e = game.run(exitCode);
    if (_mixer)
        _mixer->stop(sound);
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
        if (_game)
            _pointerShown = _game->pointerShown();
    }
    if (!_game || events.empty())
        return;
    x86::Win32Process &p = _game->process();
    const bool pointer = _game->pointerShown();
    for (const Event &ev : events) {
        switch (ev.type) {
        case EventType::kMouseMove:
            if (pointer) {
                p.mouseMoveTo(ev.mouseX, ev.mouseY);
            } else {
                // In play the game reads the mouse's movement: whole
                // pixels, the fractions kept for later.
                _pendingX += ev.moveX;
                _pendingY += ev.moveY;
                const int dx = int(std::trunc(_pendingX));
                const int dy = int(std::trunc(_pendingY));
                _pendingX -= float(dx);
                _pendingY -= float(dy);
                if (dx || dy)
                    p.mouseMoveTo(p.cursorX + dx, p.cursorY + dy);
            }
            break;
        case EventType::kMouseDown:
        case EventType::kMouseUp: {
            const int button = ev.button == MouseButton::kLeft     ? 0
                               : ev.button == MouseButton::kRight  ? 1
                               : ev.button == MouseButton::kMiddle ? 2
                                                                   : -1;
            if (pointer) // where it was pressed, as the pointer is there
                p.mouseMoveTo(ev.mouseX, ev.mouseY);
            if (button >= 0)
                p.mouseButton(button, ev.type == EventType::kMouseDown);
            break;
        }
        case EventType::kMouseWheel:
            p.mouseWheel(ev.wheelY * 120); // WHEEL_DELTA a notch
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

} // namespace reluva::sith
