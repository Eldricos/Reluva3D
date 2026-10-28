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

void MenuLayer::attach(gfx::Renderer3D* renderer, int w, int h) {
    detach();
    renderer_ = renderer;
    frameW_ = w;
    frameH_ = h;
    if (!renderer_)
        return;
    shaders_ = std::make_unique<ShaderCache>(*renderer_, ctx_.files, ctx_.shaders);
    models_ = std::make_unique<ModelCache>(*renderer_, *shaders_, ctx_.files);
    // The settings reach the models the menus show (pictures keep theirs).
    applyVideoSettings(videoSettings(ctx_.cvars), *shaders_, nullptr, models_.get());
    scene_ = std::make_unique<SceneDrawer>(*renderer_, *shaders_, *models_, ctx_.game.refEntity);
    if (ui_) {
        // New handles for the new caches; the pointer found again.
        ui_->setScreenSize(frameW_, frameH_);
        pinCursor();
    }
}

void MenuLayer::detach() {
    if (ctx_.mixer)
        for (const auto& [channel, handle] : channels_)
            ctx_.mixer->stop(handle);
    channels_.clear();
    scene_.reset();
    models_.reset();
    shaders_.reset();
    renderer_ = nullptr;
}

bool MenuLayer::start(int32_t menu) {
    clearError();
    // The player's settings, as the original's client makes them at start
    // (CL_Init), so the menus find them on a first run, before any config
    // has them: the Player settings menu shows the model from the start.
    const int32_t player = CvarStore::UserInfo | CvarStore::Archive;
    cvars_.create("name", "Reluva", player);
    cvars_.create("model", ctx_.game.playerModel, player);
    cvars_.create("handicap", "100", player);
    cvars_.create("snaps", "20", player);
    cvars_.create("rate", "25000", player);
    cvars_.create("sex", "male", player);
    cvars_.create("color", "4", player);
    JoystickInput::registerCvars(cvars_); // the Controls menu's joystick settings
    ui_ = std::make_unique<eliteforce::UiHost>(ctx_.files, cvars_);
    if (scene_)
        scene_->reset(); // new handles
    ui_->bindings = &ctx_.bindings;
    commandsSeen_ = 0;
    startMs_ = 0;
    ui_->setScreenSize(frameW_, frameH_);
    if (!ctx_.savePath.empty())
        ui_->setKeyFile(ctx_.savePath / "cdkey.txt");
    if (ctx_.mixer)
        ui_->startLocalSound = [this](const std::string& name, int channel) { playSound(name, channel); };
    ui_->browser = browser;
    ui_->resolveHost = [this](const std::string& host, uint32_t& ip) { return ctx_.sys.resolveHost(host, ip); };
    if (configure)
        configure(*ui_);
    Error err = ui_->load();
    if (!err)
        err = ui_->call(eliteforce::UiHost::Init);
    if (!err)
        err = ui_->call(eliteforce::UiHost::SetActiveMenu, {menu});
    if (err) {
        stop(err);
        return false;
    }
    pinCursor();
    return true;
}

void MenuLayer::show(int32_t menu) {
    if (!ui_)
        return;
    if (configure)
        configure(*ui_);
    if (Error err = ui_->call(eliteforce::UiHost::SetActiveMenu, {menu}))
        stop(err);
    else
        pinCursor();
}

void MenuLayer::reset() {
    ui_.reset();
    commands_.clear();
}

void MenuLayer::stop(const Error& err) {
    error_ = "The menus stopped: " + err.message;
    reportPath_.clear();
    if (ui_ && !ui_->log().empty()) {
        // The last line they printed, which often says why.
        std::string log = ui_->log();
        while (!log.empty() && log.back() == '\n')
            log.pop_back();
        error_ += " (last printed: " + log.substr(log.rfind('\n') == std::string::npos ? 0 : log.rfind('\n') + 1) + ")";
    }
    // The details, for a report: the last calls they made.
    if (ui_ && !ctx_.savePath.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(ctx_.savePath, ec);
        const std::filesystem::path report = ctx_.savePath / "menus-stopped.txt";
        std::ofstream out(report);
        out << error_ << "\n\nThe last system calls, oldest first:\n";
        for (const std::string& line : ui_->recentCalls())
            out << "  " << line << "\n";
        out << "\nWhat the menus printed:\n" << ui_->log();
        if (out)
            reportPath_ = report.string();
    }
    ui_.reset();
}

void MenuLayer::pinCursor() {
    // The menus keep their own pointer, moved by steps and kept on the
    // screen: a big step up and left puts it in the corner, where its
    // picture shows how it's drawn relative to the point it stands for.
    if (!ui_)
        return;
    if (Error err = ui_->call(eliteforce::UiHost::MouseEvent, {-100000, -100000}))
        stop(err);
    calibrate_ = true;
    haveCursor_ = false;
    pendingX_ = pendingY_ = 0;
}

void MenuLayer::syncCursor() {
    // Step the menus' pointer to where the mouse is, from where its
    // picture was last drawn and the steps sent since.
    if (!ui_ || !haveCursor_ || !haveMouse_)
        return;
    // Whole steps only, towards the mouse: the pointer moves in steps of
    // 1.5 pixels here, so rounding would overshoot and come back every
    // frame, jiggling it (and the highlight of whatever it's on). The
    // steps are scaled by how far one moved the picture before (gain_),
    // in case the menus scale movement themselves.
    const float unitX = gainX_ * float(frameW_) / 640.0f, unitY = gainY_ * float(frameH_) / 480.0f;
    const auto dx = int32_t((float(mouseX_) - cursorX_) / unitX);
    const auto dy = int32_t((float(mouseY_) - cursorY_) / unitY);
    if (!dx && !dy)
        return;
    cursorX_ += float(dx) * unitX;
    cursorY_ += float(dy) * unitY;
    pendingX_ += float(dx) * unitX;
    pendingY_ += float(dy) * unitY;
    if (Error err = ui_->call(eliteforce::UiHost::MouseEvent, {dx, dy}))
        stop(err);
}

void MenuLayer::findCursor() {
    // The pointer is the last picture drawn with the cursor shader.
    const auto& pictures = ui_->pictures();
    for (auto it = pictures.rbegin(); it != pictures.rend(); ++it) {
        if (normalizeArchiveName(ui_->shaders().name(it->shader)).rfind("menu/common/cursor", 0) != 0)
            continue;
        if (calibrate_) {
            // In the corner: the picture's offset from the point.
            calibrate_ = false;
            cursorOffsetX_ = std::abs(it->x) < 100 ? it->x : 0;
            cursorOffsetY_ = std::abs(it->y) < 100 ? it->y : 0;
        }
        const float x = it->x - cursorOffsetX_, y = it->y - cursorOffsetY_;
        // How far the steps since the last frame moved it, against what
        // was expected; not at the screen's edges, where it stops.
        auto learn = [](float& gain, float moved, float expected, float pos, float size) {
            if (std::abs(expected) < 3 || pos < 2 || pos > size - 2)
                return;
            const float ratio = moved / expected * gain;
            if (ratio > 0.25f && ratio < 4)
                gain = 0.5f * gain + 0.5f * ratio;
        };
        if (haveCursor_) {
            learn(gainX_, x - measuredX_, pendingX_, x, float(frameW_));
            learn(gainY_, y - measuredY_, pendingY_, y, float(frameH_));
        }
        pendingX_ = pendingY_ = 0;
        cursorX_ = measuredX_ = x;
        cursorY_ = measuredY_ = y;
        haveCursor_ = true;
        return;
    }
    haveCursor_ = false;
}

void MenuLayer::playSound(const std::string& name, int channel) {
    const auto sound = sounds_.get(name);
    if (!sound)
        return;
    // A channel other than 0 holds one sound at a time, as in the original.
    if (channel != 0) {
        if (const auto it = channels_.find(channel); it != channels_.end())
            ctx_.mixer->stop(it->second);
    }
    // At s_volume, as the original (0.8 unless set).
    const CvarStore::Cvar* volume = cvars_.find("s_volume");
    const float scale = volume ? std::clamp(std::strtof(volume->value.c_str(), nullptr), 0.0f, 1.0f) : 0.8f;
    const audio::SoundHandle handle =
        ctx_.mixer->play(sound, audio::SoundType::Effects, int(scale * float(audio::kMaxVolume)));
    if (channel != 0)
        channels_[channel] = handle;
}

void MenuLayer::handleEvent(const Event& ev) {
    if (!ui_)
        return;
    Error err;
    switch (ev.type) {
    // Only presses: Elite Force's menus act on a release as on a press
    // (a click counted twice), so the original's engine can't have sent
    // them releases.
    case EventType::KeyDown:
        if (const int32_t k = toQ3Key(ev.key))
            err = ui_->call(eliteforce::UiHost::KeyEvent, {k, 1});
        break;
    case EventType::TextInput:
        // Characters for text fields, each as a key press with the
        // character flag (the original's menus go by Latin-1).
        for (uint32_t cp : decodeUtf8(ev.text))
            if (cp >= 32 && cp < 256 && cp != 127 && !err)
                err = ui_->call(eliteforce::UiHost::KeyEvent, {int32_t(cp) | q3key::CharFlag, 1});
        break;
    case EventType::MouseDown:
        // Clicks land where the mouse is.
        mouseX_ = ev.mouseX;
        mouseY_ = ev.mouseY;
        haveMouse_ = true;
        syncCursor();
        if (!ui_)
            return;
        if (const int32_t k = toQ3Button(ev.button))
            err = ui_->call(eliteforce::UiHost::KeyEvent, {k, 1});
        break;
    case EventType::MouseWheel:
        if (ev.wheelY) {
            const int32_t k = ev.wheelY > 0 ? q3key::WheelUp : q3key::WheelDown;
            err = ui_->call(eliteforce::UiHost::KeyEvent, {k, 1});
        }
        break;
    case EventType::MouseMove:
        // Followed in frame(), once per frame.
        mouseX_ = ev.mouseX;
        mouseY_ = ev.mouseY;
        haveMouse_ = true;
        break;
    default: break;
    }
    if (err)
        stop(err);
}

void MenuLayer::key(int32_t q3key, bool down) {
    if (!ui_ || !down)
        return;
    if (Error err = ui_->call(eliteforce::UiHost::KeyEvent, {q3key, 1}))
        stop(err);
}

std::vector<std::string> MenuLayer::takeCommands() {
    if (ui_)
        for (const auto& all = ui_->commands(); commandsSeen_ < all.size(); ++commandsSeen_)
            commands_.push_back(all[commandsSeen_]);
    return std::exchange(commands_, {});
}

void MenuLayer::frame(uint64_t nowMs) {
    if (!ui_ || !renderer_)
        return;
    if (!startMs_)
        startMs_ = nowMs;
    const auto time = int32_t(nowMs - startMs_);
    syncCursor();
    if (!ui_)
        return;
    ui_->clearPictures();
    ui_->now = nowMs;
    if (Error err = ui_->refresh(time)) {
        stop(err);
        return;
    }
    const std::vector<eliteforce::UiHost::Picture> pictures = ui_->pictures();
    findCursor();

    // The pictures, in screen pixels as the menus count, with each 3D
    // scene (the player model in Player settings...) over the pictures
    // drawn before it.
    const float seconds = float(time) / 1000.0f;
    const auto& scene = ui_->scene();
    scene_->setHandles({&ui_->models(), &ui_->skins(), &ui_->shaders()});
    size_t drawn = 0;
    bool first = true;
    for (const auto& v : scene.views) {
        scene_->drawPictures(pictures, drawn, v.picturesBefore, frameW_, frameH_, seconds);
        drawn = std::max(drawn, v.picturesBefore);
        scene_->drawView(scene, v, frameW_, frameH_, seconds, first, nullptr, nullptr, true);
        first = false;
    }
    scene_->drawPictures(pictures, drawn, pictures.size(), frameW_, frameH_, seconds);
}

} // namespace reluva::idtech3
