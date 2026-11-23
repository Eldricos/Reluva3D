// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/legoloco/host/player.h"

#include "graphics/system_fonts.h"

#include <chrono>
#include <condition_variable>
#include <memory>

#include "audio/mixer.h"
#include "engines/legoloco/host/game_process.h"

namespace reluva::legoloco {

Player::Player(std::filesystem::path gameDir, std::filesystem::path saveDir, audio::Mixer *mixer,
               std::shared_ptr<x86::DirectPlay::Network> network)
    : _dir(std::move(gameDir)), _saveDir(std::move(saveDir)), _mixer(mixer), _network(std::move(network)) {
}

Player::~Player() {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stopping = true;
        // Its threads may all be waiting (for a message, the timer):
        // the process is told to end, and the interpreter to stop.
        if (_game) {
            _game->process().requestExit();
            _game->cpu().requestStop();
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
    width = _width, height = _height;
    return true;
}

void Player::serve() {
    std::deque<std::function<void()>> jobs;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        jobs.swap(_jobs);
    }
    for (auto &job : jobs)
        job();
}

// A job for the engine's thread, the game's waiting for its answer: given
// to `job` to call once, from any thread, perhaps after this player's
// gone (so it keeps its own state). Nothing if the game is stopped first.
template<typename T>
std::optional<T> Player::ask(std::function<void(std::function<void(T)>)> job) {
    struct Answer {
        std::mutex mutex;
        std::condition_variable ready;
        std::optional<T> value;
    };
    auto answer = std::make_shared<Answer>();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _jobs.push_back([job = std::move(job), answer] {
            job([answer](T value) {
                std::lock_guard<std::mutex> lock(answer->mutex);
                answer->value = std::move(value);
                answer->ready.notify_all();
            });
        });
    }
    std::unique_lock<std::mutex> lock(answer->mutex);
    while (!answer->value) {
        answer->ready.wait_for(lock, std::chrono::milliseconds(50));
        std::lock_guard<std::mutex> stop(_mutex);
        if (_stopping)
            break;
    }
    return answer->value;
}

bool Player::pointer(Cursor &cursor, bool &shown) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_pointerNumber == _pointerTaken)
        return false;
    _pointerTaken = _pointerNumber;
    cursor = _pointer;
    shown = _pointerShown;
    return true;
}

// The picture is kept again only when it changes: a new cursor, or the
// next frame of an animated one.
void Player::takePointer() {
    const Cursor *now = _game ? _game->process().pointer() : nullptr;
    if (now == _pointerNow)
        return;
    _pointerNow = now;
    std::lock_guard<std::mutex> lock(_mutex);
    _pointerShown = now != nullptr;
    if (now)
        _pointer = *now;
    ++_pointerNumber;
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
    GameProcess game(_dir, _saveDir);
    x86::Win32Process &process = game.process();
    process.output = [this](const std::string &text) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_output.size() < (1u << 20))
            _output += text + (text.empty() || text.back() != '\n' ? "\n" : "");
    };
    // Its sound: DirectSound's buffers and PlaySoundA's music in one
    // stream, and the movies' own.
    audio::SoundHandle sound = audio::kNoSound;
    if (_mixer) {
        sound = _mixer->playOutput(game.directSound().output(), audio::SoundType::kEffects);
        game.video().mixer = _mixer;
    }
    // Each frame shown is kept for the engine; then the input waiting,
    // which is also passed on whenever the program looks for a message and
    // finds none. A movie's frames go into the screen's pixels, not through
    // a blit: they're shown as they're drawn.
    game.directX().presented = [this](const x86::DirectX::Frame &f) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _frame = f.pixels;
            _width = f.width, _height = f.height;
            ++_frameNumber;
        }
        _frameX = f.x, _frameY = f.y;
        drain();
        takePointer();
    };
    game.video().frameDrawn = [&game] { game.directX().show(); };
    game.directPlay().network = _network;
    game.process().gdi().colours = _colours;
    if (_ownArial)
        game.process().gdi().arial = ttf::installedArial;
    game.process().screenWidth = _desktopWidth;
    game.process().screenHeight = _desktopHeight;
    // The game's dialogs and web pages, shown by the engine.
    process.chooseFile = [this](const x86::Win32Process::FileRequest &request) -> std::optional<std::filesystem::path> {
        if (!chooseFile)
            return std::nullopt;
        using Picked = std::optional<std::filesystem::path>;
        const auto picked =
            ask<Picked>([this, request](std::function<void(Picked)> done) { chooseFile(request, std::move(done)); });
        return picked ? *picked : std::nullopt;
    };
    process.openUrl = [this](const std::string &url) {
        if (!openUrl)
            return false;
        const auto opened = ask<bool>([this, url](const std::function<void(bool)> &done) { done(openUrl(url)); });
        return opened.value_or(false);
    };
    process.idle = [this] {
        drain();
        takePointer();
    };
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _game = &game;
        if (_stopping) {
            process.requestExit();
            game.cpu().requestStop();
        }
    }
    Error e = game.load();
    uint32_t exitCode = 0;
    if (!e)
        e = game.run(exitCode);
    if (_mixer && sound != audio::kNoSound)
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
    }
    if (!_game || events.empty())
        return;
    x86::Win32Process &p = _game->process();
    for (const Event &ev : events) {
        switch (ev.type) {
        // The game draws its own pointer where Windows says the mouse is:
        // the place on the frame shown, which is its window's client area.
        case EventType::kMouseMove:
            p.mouseMoveTo(_frameX + ev.mouseX, _frameY + ev.mouseY);
            break;
        case EventType::kMouseDown:
        case EventType::kMouseUp: {
            const int button = ev.button == MouseButton::kLeft     ? 0
                               : ev.button == MouseButton::kRight  ? 1
                               : ev.button == MouseButton::kMiddle ? 2
                                                                   : -1;
            if (_frameX + ev.mouseX != p.cursorX || _frameY + ev.mouseY != p.cursorY)
                p.mouseMoveTo(_frameX + ev.mouseX, _frameY + ev.mouseY);
            if (button >= 0)
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

} // namespace reluva::legoloco
