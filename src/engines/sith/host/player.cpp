// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/host/player.h"

#include "audio/mixer.h"
#include "engines/sith/host/game_process.h"

#include <algorithm>
#include <cmath>

namespace reluva::sith {

Player::Player(GameFolders folders, std::filesystem::path saveFolder, audio::Mixer *mixer,
               x86::DirectPlay::Network network, std::string commandLine)
    : _folders(std::move(folders)), _saveFolder(std::move(saveFolder)), _commandLine(std::move(commandLine)),
      _mixer(mixer), _network(std::move(network)) {
}

Player::~Player() {
    // The game is asked to close its window first, as Windows would when the
    // player closes it: it then saves what it saves only as it ends (its
    // Setup's display and 3D choices, 0x4148f0), and is stopped only if it
    // hasn't ended in a few seconds.
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _closing = true;
    }
    const auto giveUp = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < giveUp) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_finished || !_game)
                break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stopping = true;
        if (_game) {
            // In play the game takes the close for itself and goes on: its
            // settings are saved for it once it's stopped (work()).
            _forced = true;
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
    GameProcess game(_folders, _saveFolder, _commandLine);
    game.directPlay().network = _network;
    // The game looks for joysticks as it starts.
    offerPads(game);
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
            _frameX = f.x;
            _frameY = f.y;
            _height = f.height;
            ++_frameNumber;
        }
        drain();
    };
    // While every thread waits, the network is still answered.
    process.idle = [this, &game] {
        drain();
        game.directPlay().update();
    };
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _game = &game;
        if (_stopping) {
            game.cpu().requestStop();
            process.requestExit();
        }
    }
    // The sound card's output, for as long as the game runs.
    game.cdAudio().mixer = _mixer;
    const audio::SoundHandle sound =
        _mixer ? _mixer->playOutput(game.directSound().output(), audio::SoundType::kEffects) : audio::kNoSound;
    Error e = game.load();
    uint32_t exitCode = 0;
    if (!e)
        e = game.run(exitCode);
    bool forced = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        forced = _forced;
    }
    if (forced)
        game.saveSettings();
    if (_mixer)
        _mixer->stop(sound);
    std::lock_guard<std::mutex> lock(_mutex);
    _game = nullptr;
    _finished = true;
    _ended = !e || process.exited.has_value();
    if (e && !_ended && !_stopping)
        _error = e.message;
}

void Player::gamepad(int index, const GamepadState *pad) {
    if (index < 0 || index >= int(_pads.size()))
        return;
    std::lock_guard<std::mutex> lock(_mutex);
    _pads[size_t(index)].connected = pad != nullptr;
    if (pad)
        _pads[size_t(index)].state = *pad;
}

void Player::offerPads(GameProcess &game) {
    std::array<std::pair<bool, GamepadState>, x86::Joysticks::kConnectable> now;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (size_t i = 0; i < _pads.size(); ++i)
            now[i] = {_pads[i].connected, _pads[i].state};
    }
    x86::Joysticks &joysticks = game.joysticks();
    for (size_t i = 0; i < now.size(); ++i) {
        const auto &[connected, pad] = now[i];
        Pad &p = _pads[i]; // given and givenName are the game thread's alone
        if (connected != p.given || (connected && pad.name != p.givenName)) {
            const x86::Joysticks::Device device = x86::Joysticks::fromGamepad(pad);
            joysticks.connect(int(i), connected ? &device : nullptr);
            p.given = connected;
            p.givenName = pad.name;
        }
        if (connected)
            joysticks.set(int(i), x86::Joysticks::stateOf(pad));
    }
}

void Player::drain() {
    std::deque<Event> events;
    bool close = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        events.swap(_events);
        if (_game)
            _pointerShown = _game->pointerShown();
        close = _closing && !_closeSent;
        _closeSent = _closeSent || close;
    }
    if (!_game)
        return;
    if (close) {
        x86::Win32Process &p = _game->process();
        p.postMessage(p.mainWindow(), 0x10, 0, 0); // WM_CLOSE
    }
    offerPads(*_game);
    if (events.empty())
        return;
    x86::Win32Process &p = _game->process();
    const bool pointer = _game->pointerShown();
    for (const Event &ev : events) {
        switch (ev.type) {
        case EventType::kMouseMove:
            if (pointer) {
                p.mouseMoveTo(_frameX + ev.mouseX, _frameY + ev.mouseY);
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
                p.mouseMoveTo(_frameX + ev.mouseX, _frameY + ev.mouseY);
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
