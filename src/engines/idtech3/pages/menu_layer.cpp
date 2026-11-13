// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/pages/menu_layer.h"

#include "engines/idtech3/client/joystick_input.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>

namespace reluva::idtech3 {

void MenuLayer::attach(gfx::Renderer3D *renderer, int w, int h) {
    detach();
    _renderer = renderer;
    _frameW = w;
    _frameH = h;
    if (!_renderer)
        return;
    _shaders = std::make_unique<ShaderCache>(*_renderer, _ctx.files, _ctx.shaders);
    _models = std::make_unique<ModelCache>(*_renderer, *_shaders, _ctx.files);
    // The settings reach the models the menus show (pictures keep theirs).
    applyVideoSettings(videoSettings(_ctx.cvars), *_shaders, nullptr, _models.get());
    _scene = std::make_unique<SceneDrawer>(*_renderer, *_shaders, *_models, _ctx.game.refEntity);
    if (_ui) {
        // New handles for the new caches; the pointer found again.
        _ui->setScreenSize(_frameW, _frameH);
        pinCursor();
    }
}

void MenuLayer::detach() {
    if (_ctx.mixer)
        for (const auto &[channel, handle] : _channels)
            _ctx.mixer->stop(handle);
    _channels.clear();
    _scene.reset();
    _models.reset();
    _shaders.reset();
    _renderer = nullptr;
}

bool MenuLayer::start(int32_t menu) {
    clearError();
    // The player's settings, as the original's client makes them at start
    // (CL_Init), so the menus find them on a first run, before any config
    // has them: the Player settings menu shows the model from the start.
    const int32_t player = CvarStore::kUserInfo | CvarStore::kArchive;
    _cvars.create("name", "Reluva", player);
    _cvars.create("model", _ctx.game.playerModel, player);
    _cvars.create("handicap", "100", player);
    _cvars.create("snaps", "20", player);
    _cvars.create("rate", "25000", player);
    _cvars.create("sex", "male", player);
    _cvars.create("color", "4", player);
    JoystickInput::registerCvars(_cvars); // the Controls menu's joystick settings
    _ui = std::make_unique<eliteforce::UiHost>(_ctx.files, _cvars);
    _ui->setModRoot(_ctx.installPath, _ctx.game.baseDir);
    if (_scene)
        _scene->reset(); // new handles
    _ui->bindings = &_ctx.bindings;
    _commandsSeen = 0;
    _startMs = 0;
    _ui->setScreenSize(_frameW, _frameH);
    if (!_ctx.homePath.empty())
        _ui->setKeyFile(_ctx.homePath / "cdkey.txt"); // whatever the mod
    if (_ctx.mixer)
        _ui->startLocalSound = [this](const std::string &name, int channel) { playSound(name, channel); };
    _ui->browser = browser;
    _ui->resolveHost = [this](const std::string &host, uint32_t &ip) { return _ctx.sys.resolveHost(host, ip); };
    if (configure)
        configure(*_ui);
    Error err = _ui->load();
    if (!err)
        err = _ui->call(eliteforce::UiHost::kInit);
    if (!err)
        err = _ui->call(eliteforce::UiHost::kSetActiveMenu, {menu});
    if (err) {
        stop(err);
        return false;
    }
    pinCursor();
    return true;
}

void MenuLayer::show(int32_t menu) {
    if (!_ui)
        return;
    if (configure)
        configure(*_ui);
    if (Error err = _ui->call(eliteforce::UiHost::kSetActiveMenu, {menu}))
        stop(err);
    else
        pinCursor();
}

void MenuLayer::reset() {
    _ui.reset();
    _commands.clear();
}

void MenuLayer::stop(const Error &err) {
    _error = "The menus stopped: " + err.message;
    _reportPath.clear();
    if (_ui && !_ui->log().empty()) {
        // The last line they printed, which often says why.
        std::string log = _ui->log();
        while (!log.empty() && log.back() == '\n')
            log.pop_back();
        _error += " (last printed: " + log.substr(log.rfind('\n') == std::string::npos ? 0 : log.rfind('\n') + 1) + ")";
    }
    // The details, for a report: the last calls they made.
    if (_ui && !_ctx.savePath.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(_ctx.savePath, ec);
        const std::filesystem::path report = _ctx.savePath / "menus-stopped.txt";
        std::ofstream out(report);
        out << _error << "\n\nThe last system calls, oldest first:\n";
        for (const std::string &line : _ui->recentCalls())
            out << "  " << line << "\n";
        out << "\nWhat the menus printed:\n" << _ui->log();
        if (out)
            _reportPath = report.string();
    }
    _ui.reset();
}

void MenuLayer::pinCursor() {
    // The menus keep their own pointer, moved by steps and kept on the
    // screen: a big step up and left puts it in the corner, where its
    // picture shows how it's drawn relative to the point it stands for.
    if (!_ui)
        return;
    if (Error err = _ui->call(eliteforce::UiHost::kMouseEvent, {-100000, -100000}))
        stop(err);
    _calibrate = true;
    _haveCursor = false;
    _pendingX = _pendingY = 0;
}

void MenuLayer::syncCursor() {
    // Step the menus' pointer to where the mouse is, from where its
    // picture was last drawn and the steps sent since.
    if (!_ui || !_haveCursor || !_haveMouse)
        return;
    // Whole steps only, towards the mouse: the pointer moves in steps of
    // 1.5 pixels here, so rounding would overshoot and come back every
    // frame, jiggling it (and the highlight of whatever it's on). The
    // steps are scaled by how far one moved the picture before (gain_),
    // in case the menus scale movement themselves.
    const float unitX = _gainX * float(_frameW) / 640.0f;
    const float unitY = _gainY * float(_frameH) / 480.0f;
    const auto dx = int32_t((float(_mouseX) - _cursorX) / unitX);
    const auto dy = int32_t((float(_mouseY) - _cursorY) / unitY);
    if (!dx && !dy)
        return;
    _cursorX += float(dx) * unitX;
    _cursorY += float(dy) * unitY;
    _pendingX += float(dx) * unitX;
    _pendingY += float(dy) * unitY;
    if (Error err = _ui->call(eliteforce::UiHost::kMouseEvent, {dx, dy}))
        stop(err);
}

void MenuLayer::findCursor() {
    // The pointer is the last picture drawn with the cursor shader.
    const auto &pictures = _ui->pictures();
    for (auto it = pictures.rbegin(); it != pictures.rend(); ++it) {
        if (normalizeArchiveName(_ui->shaders().name(it->shader)).rfind("menu/common/cursor", 0) != 0)
            continue;
        if (_calibrate) {
            // In the corner: the picture's offset from the point.
            _calibrate = false;
            _cursorOffsetX = std::abs(it->x) < 100 ? it->x : 0;
            _cursorOffsetY = std::abs(it->y) < 100 ? it->y : 0;
        }
        const float x = it->x - _cursorOffsetX;
        const float y = it->y - _cursorOffsetY;
        // How far the steps since the last frame moved it, against what
        // was expected; not at the screen's edges, where it stops.
        auto learn = [](float &gain, float moved, float expected, float pos, float size) {
            if (std::abs(expected) < 3 || pos < 2 || pos > size - 2)
                return;
            const float ratio = moved / expected * gain;
            if (ratio > 0.25f && ratio < 4)
                gain = 0.5f * gain + 0.5f * ratio;
        };
        if (_haveCursor) {
            learn(_gainX, x - _measuredX, _pendingX, x, float(_frameW));
            learn(_gainY, y - _measuredY, _pendingY, y, float(_frameH));
        }
        _pendingX = _pendingY = 0;
        _cursorX = _measuredX = x;
        _cursorY = _measuredY = y;
        _haveCursor = true;
        return;
    }
    _haveCursor = false;
}

void MenuLayer::playSound(const std::string &name, int channel) {
    const auto sound = _sounds.get(name);
    if (!sound)
        return;
    // A channel other than 0 holds one sound at a time, as in the original.
    if (channel != 0) {
        if (const auto it = _channels.find(channel); it != _channels.end())
            _ctx.mixer->stop(it->second);
    }
    // At s_volume, as the original (0.8 unless set).
    const CvarStore::Cvar *volume = _cvars.find("s_volume");
    const float scale = volume ? std::clamp(std::strtof(volume->value.c_str(), nullptr), 0.0f, 1.0f) : 0.8f;
    const audio::SoundHandle handle =
        _ctx.mixer->play(sound, audio::SoundType::kEffects, int(scale * float(audio::kMaxVolume)));
    if (channel != 0)
        _channels[channel] = handle;
}

void MenuLayer::handleEvent(const Event &ev) {
    if (!_ui)
        return;
    Error err;
    switch (ev.type) {
    // Only presses: Elite Force's menus act on a release as on a press
    // (a click counted twice), so the original's engine can't have sent
    // them releases.
    case EventType::kKeyDown:
        if (const int32_t k = toQ3Key(ev.key))
            err = _ui->call(eliteforce::UiHost::kKeyEvent, {k, 1});
        break;
    case EventType::kTextInput:
        // Characters for text fields, each as a key press with the
        // character flag (the original's menus go by Latin-1).
        for (uint32_t cp : decodeUtf8(ev.text))
            if (cp >= 32 && cp < 256 && cp != 127 && !err)
                err = _ui->call(eliteforce::UiHost::kKeyEvent, {int32_t(cp) | q3key::kCharFlag, 1});
        break;
    case EventType::kMouseDown:
        // Clicks land where the mouse is.
        _mouseX = ev.mouseX;
        _mouseY = ev.mouseY;
        _haveMouse = true;
        syncCursor();
        if (!_ui)
            return;
        if (const int32_t k = toQ3Button(ev.button))
            err = _ui->call(eliteforce::UiHost::kKeyEvent, {k, 1});
        break;
    case EventType::kMouseWheel:
        if (ev.wheelY) {
            const int32_t k = ev.wheelY > 0 ? q3key::kWheelUp : q3key::kWheelDown;
            err = _ui->call(eliteforce::UiHost::kKeyEvent, {k, 1});
        }
        break;
    case EventType::kMouseMove:
        // Followed in frame(), once per frame.
        _mouseX = ev.mouseX;
        _mouseY = ev.mouseY;
        _haveMouse = true;
        break;
    default:
        break;
    }
    if (err)
        stop(err);
}

void MenuLayer::key(int32_t q3key, bool down) {
    if (!_ui || !down)
        return;
    if (Error err = _ui->call(eliteforce::UiHost::kKeyEvent, {q3key, 1}))
        stop(err);
}

std::vector<std::string> MenuLayer::takeCommands() {
    if (_ui)
        for (const auto &all = _ui->commands(); _commandsSeen < all.size(); ++_commandsSeen)
            _commands.push_back(all[_commandsSeen]);
    return std::exchange(_commands, {});
}

void MenuLayer::frame(uint64_t nowMs) {
    if (!_ui || !_renderer)
        return;
    if (!_startMs)
        _startMs = nowMs;
    const auto time = int32_t(nowMs - _startMs);
    syncCursor();
    if (!_ui)
        return;
    _ui->clearPictures();
    _ui->now = nowMs;
    if (Error err = _ui->refresh(time)) {
        stop(err);
        return;
    }
    const std::vector<eliteforce::UiHost::Picture> pictures = _ui->pictures();
    findCursor();

    // The pictures, in screen pixels as the menus count, with each 3D
    // scene (the player model in Player settings...) over the pictures
    // drawn before it.
    const float seconds = float(time) / 1000.0f;
    const auto &scene = _ui->scene();
    _scene->setHandles({&_ui->models(), &_ui->skins(), &_ui->shaders()});
    size_t drawn = 0;
    bool first = true;
    for (const auto &v : scene.views) {
        _scene->drawPictures(pictures, drawn, v.picturesBefore, _frameW, _frameH, seconds);
        drawn = std::max(drawn, v.picturesBefore);
        _scene->drawView(scene, v, _frameW, _frameH, seconds, first, nullptr, nullptr, true);
        first = false;
    }
    _scene->drawPictures(pictures, drawn, pictures.size(), _frameW, _frameH, seconds);
}

} // namespace reluva::idtech3
