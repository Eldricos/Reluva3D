// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Menu page: the game's own menus (vm/ui.qvm) run by the QVM
// interpreter, their pictures drawn by the GPU renderer.

#include "engines/idtech3/viewers.h"

#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace reluva::idtech3 {

namespace {

constexpr int kFrameW = 960;
constexpr int kFrameH = 720;
constexpr uint64_t kHintMs = 6000;

// The code points in UTF-8 text.
std::vector<uint32_t> decodeUtf8(const char* s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; s[i];) {
        const auto c = static_cast<unsigned char>(s[i]);
        const int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (extra < 0) {
            ++i;
            continue;
        }
        uint32_t cp = extra == 0 ? c : c & (0x3f >> extra);
        size_t j = 1;
        for (; j <= size_t(extra) && (static_cast<unsigned char>(s[i + j]) >> 6) == 2; ++j)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + j]) & 0x3f);
        if (j == size_t(extra) + 1)
            out.push_back(cp);
        i += j;
    }
    return out;
}

} // namespace

MenuViewer::MenuViewer(ViewerContext& ctx) : Page(ctx) {
    available_ = std::strcmp(ctx.game.gameId, "stvef") == 0 && ctx.files.has("vm/ui.qvm");
}

MenuViewer::~MenuViewer() { deactivate(); }

void MenuViewer::activate() {
    if (Error err = ctx_.sys.init3D(kFrameW, kFrameH, renderer_)) {
        renderer_ = nullptr;
        ctx_.sys.initGraphics(ctx_.screen.w, ctx_.screen.h, PixelFormat::RGBA32);
        clear(16, 18, 24);
        text(4, 4, "The Menu page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    overlay_.create(kFrameW, kFrameH, PixelFormat::RGBA32);
    shaders_ = std::make_unique<ShaderCache>(*renderer_, ctx_.files, ctx_.shaders);
    materials_.clear();
    shownMs_ = 0;
    // The menus draw their own pointer, and have text fields.
    ctx_.sys.setMouseCursorVisible(false);
    ctx_.sys.setTextInput(true);
    if (!ui_ && error_.empty())
        start();
    else if (ui_)
        pinCursor();
    setTitle(error_.empty() ? "Elite Force's own menus (vm/ui.qvm)" : "stopped");
}

void MenuViewer::start() {
    ui_ = std::make_unique<eliteforce::UiHost>(ctx_.files, cvars_);
    ui_->bindings = &ctx_.bindings;
    commandsSeen_ = 0;
    ui_->setScreenSize(kFrameW, kFrameH);
    if (!ctx_.savePath.empty())
        ui_->setKeyFile(ctx_.savePath / "cdkey.txt");
    if (ctx_.mixer)
        ui_->startLocalSound = [this](const std::string& name, int channel) { playSound(name, channel); };
    Error err = ui_->load();
    if (!err)
        err = ui_->call(eliteforce::UiHost::Init);
    if (!err)
        err = ui_->call(eliteforce::UiHost::SetActiveMenu, {1}); // the main menu
    if (err)
        stop(err);
    else
        pinCursor();
}

void MenuViewer::pinCursor() {
    // The menus keep their own pointer, moved by steps and kept on the
    // screen: a big step up and left puts it in the corner, where its
    // picture shows how it's drawn relative to the point it stands for.
    if (Error err = ui_->call(eliteforce::UiHost::MouseEvent, {-100000, -100000}))
        stop(err);
    calibrate_ = true;
    haveCursor_ = false;
    pendingX_ = pendingY_ = 0;
}

void MenuViewer::syncCursor() {
    // Step the menus' pointer to where the mouse is, from where its
    // picture was last drawn and the steps sent since.
    if (!ui_ || !haveCursor_ || !haveMouse_)
        return;
    // Whole steps only, towards the mouse: the pointer moves in steps of
    // 1.5 pixels here, so rounding would overshoot and come back every
    // frame, jiggling it (and the highlight of whatever it's on). The
    // steps are scaled by how far one moved the picture before (gain_),
    // in case the menus scale movement themselves.
    const float unitX = gainX_ * float(kFrameW) / 640.0f, unitY = gainY_ * float(kFrameH) / 480.0f;
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

void MenuViewer::findCursor() {
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
            learn(gainX_, x - measuredX_, pendingX_, x, float(kFrameW));
            learn(gainY_, y - measuredY_, pendingY_, y, float(kFrameH));
        }
        pendingX_ = pendingY_ = 0;
        cursorX_ = measuredX_ = x;
        cursorY_ = measuredY_ = y;
        haveCursor_ = true;
        return;
    }
    haveCursor_ = false;
}

void MenuViewer::checkCommands() {
    // The commands the menus have run since the last look: one that
    // starts a level (Engage runs "map <name>", after setting the game's
    // cvars) shows it on the Play page.
    const auto& commands = ui_->commands();
    for (; commandsSeen_ < commands.size(); ++commandsSeen_) {
        std::string line = commands[commandsSeen_];
        lastCommand_ = line;
        while (!lastCommand_.empty() && (lastCommand_.back() == '\n' || lastCommand_.back() == ' '))
            lastCommand_.pop_back();
        lastCommandMs_ = ctx_.sys.getMillis();
        for (char& c : line)
            if (c == ';' || c == '\n')
                c = ' ';
        std::vector<std::string> words;
        size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && static_cast<unsigned char>(line[i]) <= ' ')
                ++i;
            size_t j = i;
            while (j < line.size() && static_cast<unsigned char>(line[j]) > ' ')
                ++j;
            if (j > i)
                words.push_back(line.substr(i, j - i));
            i = j;
        }
        for (size_t w = 0; w + 1 < words.size(); ++w) {
            const std::string verb = fs::toLower(words[w]);
            if (verb == "map" || verb == "devmap" || verb == "spmap" || verb == "spdevmap") {
                ctx_.requestPage = "Play";
                ctx_.requestMap = words[w + 1];
            }
        }
    }
}

void MenuViewer::stop(const Error& err) {
    error_ = "The menus stopped: " + err.message;
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

void MenuViewer::playSound(const std::string& name, int channel) {
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

void MenuViewer::deactivate() {
    // Settings and bindings changed in the menus, kept.
    saveConfig(ctx_);
    if (ctx_.mixer)
        for (const auto& [channel, handle] : channels_)
            ctx_.mixer->stop(handle);
    channels_.clear();
    if (renderer_) {
        ctx_.sys.setMouseCursorVisible(true);
        ctx_.sys.setTextInput(false);
    }
    materials_.clear();
    shaders_.reset();
    renderer_ = nullptr;
}

bool MenuViewer::takesKey(const Event& ev) const {
    // Esc and Tab belong to the menus; with Ctrl they leave, as elsewhere.
    return ui_ && !(ev.mods & keymod::Ctrl);
}

void MenuViewer::handleEvent(const Event& ev) {
    if (ev.type == EventType::KeyDown && ev.key == 'r' && (ev.mods & keymod::Ctrl)) {
        // Ctrl+R: start the menus again, stopped or not.
        ui_.reset();
        error_.clear();
        reportPath_.clear();
        start();
        return;
    }
    if (!ui_)
        return;
    Error err;
    switch (ev.type) {
    case EventType::KeyDown:
    case EventType::KeyUp:
        if (const int32_t k = toQ3Key(ev.key))
            err = ui_->call(eliteforce::UiHost::KeyEvent, {k, ev.type == EventType::KeyDown});
        break;
    case EventType::TextInput:
        // Characters for text fields, each as a key press with the
        // character flag (the original's menus go by Latin-1).
        for (uint32_t cp : decodeUtf8(ev.text))
            if (cp >= 32 && cp < 256 && cp != 127 && !err)
                err = ui_->call(eliteforce::UiHost::KeyEvent, {int32_t(cp) | q3key::CharFlag, 1});
        break;
    case EventType::MouseDown:
    case EventType::MouseUp:
        // Clicks land where the mouse is.
        mouseX_ = ev.mouseX;
        mouseY_ = ev.mouseY;
        haveMouse_ = true;
        syncCursor();
        if (!ui_)
            return;
        if (const int32_t k = toQ3Button(ev.button))
            err = ui_->call(eliteforce::UiHost::KeyEvent, {k, ev.type == EventType::MouseDown});
        break;
    case EventType::MouseWheel:
        if (ev.wheelY) {
            const int32_t k = ev.wheelY > 0 ? q3key::WheelUp : q3key::WheelDown;
            err = ui_->call(eliteforce::UiHost::KeyEvent, {k, 1});
            if (!err)
                err = ui_->call(eliteforce::UiHost::KeyEvent, {k, 0});
        }
        break;
    case EventType::MouseMove:
        // Followed in update(), once per frame.
        mouseX_ = ev.mouseX;
        mouseY_ = ev.mouseY;
        haveMouse_ = true;
        break;
    default: break;
    }
    if (err)
        stop(err);
    else if (ui_)
        checkCommands();
}

void MenuViewer::update(uint64_t nowMs) {
    if (!renderer_)
        return;
    if (!startMs_)
        startMs_ = nowMs;
    if (!shownMs_)
        shownMs_ = nowMs;
    const auto time = int32_t(nowMs - startMs_);

    std::vector<eliteforce::UiHost::Picture> pictures;
    syncCursor();
    if (ui_) {
        ui_->clearPictures();
        if (Error err = ui_->refresh(time)) {
            stop(err);
        } else {
            pictures = ui_->pictures();
            findCursor();
            checkCommands();
        }
    }

    // Every picture a quad in one mesh, in screen pixels.
    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(pictures.size() * 4);
    indices.reserve(pictures.size() * 6);
    for (const auto& p : pictures) {
        const auto base = uint32_t(vertices.size());
        uint8_t rgba[4];
        for (int i = 0; i < 4; ++i)
            rgba[i] = uint8_t(std::clamp(p.color[size_t(i)], 0.0f, 1.0f) * 255.0f + 0.5f);
        const float xs[4] = {p.x, p.x + p.w, p.x + p.w, p.x};
        const float ys[4] = {p.y, p.y, p.y + p.h, p.y + p.h};
        const float ss[4] = {p.s1, p.s2, p.s2, p.s1};
        const float ts[4] = {p.t1, p.t1, p.t2, p.t2};
        for (int i = 0; i < 4; ++i) {
            gfx::Vertex v{};
            v.pos[0] = xs[i];
            v.pos[1] = ys[i];
            v.uv[0] = ss[i];
            v.uv[1] = ts[i];
            v.normal[2] = 1;
            std::memcpy(v.color, rgba, 4);
            vertices.push_back(v);
        }
        for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
            indices.push_back(base + i);
    }
    gfx::MeshId mesh = 0;
    if (!vertices.empty())
        renderer_->createMesh(vertices, indices, mesh);

    // Pixels to clip space: x right, y down, as the menus count.
    gfx::Mat4 ortho{};
    ortho[0] = 2.0f / float(kFrameW);
    ortho[5] = -2.0f / float(kFrameH);
    ortho[10] = 1;
    ortho[12] = -1;
    ortho[13] = 1;
    ortho[15] = 1;
    renderer_->setGamma(brightness(ctx_));
    renderer_->beginFrame(ortho, {0, 0, 0}, {0, 0, 0});
    if (mesh) {
        for (size_t i = 0; i < pictures.size(); ++i) {
            const int32_t handle = pictures[i].shader;
            if (handle <= 0)
                continue;
            if (size_t(handle) >= materials_.size())
                materials_.resize(size_t(handle) + 1, -2);
            int& material = materials_[size_t(handle)];
            if (material == -2)
                material = shaders_->material(ui_ ? ui_->shaders().name(handle) : std::string(), Lighting::TwoD);
            if (material < 0 || shaders_->get(material).skip)
                continue;
            gfx::DrawCall call;
            call.mesh = mesh;
            call.firstIndex = uint32_t(i * 6);
            call.indexCount = 6;
            shaders_->draw(material, call, 0, float(time) / 1000.0f);
        }
    }

    // Errors, and for a while how to leave.
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    std::vector<std::string> lines;
    if (!error_.empty()) {
        lines.push_back(error_);
        if (!reportPath_.empty())
            lines.push_back("Details: " + reportPath_);
        lines.push_back("Ctrl+R: start them again.");
    }
    if (ui_ && !ui_->unhandled().empty()) {
        // Calls the menus made that Reluva answers with 0 for now.
        std::string calls = "System calls not implemented yet:";
        for (const auto& [number, count] : ui_->unhandled())
            calls += " " + std::to_string(number) + " (x" + std::to_string(count) + ")";
        lines.push_back(calls);
    }
    if (!lastCommand_.empty() && nowMs - lastCommandMs_ < kHintMs)
        lines.push_back("The menus ran: " + lastCommand_);
    if (nowMs - shownMs_ < kHintMs)
        lines.push_back("Keys and mouse go to the menus. Ctrl+Tab: next page, Ctrl+Esc: launcher, Ctrl+R: restart.");
    const int lh = font::kLineHeight;
    for (size_t i = 0; i < lines.size(); ++i) {
        const int y = kFrameH - int(lines.size() - i) * lh - 4;
        const int w = std::min(kFrameW, int(font::length(lines[i])) * font::kAdvance + 8);
        for (int yy = y - 2; yy < y + lh; ++yy) {
            uint8_t* p = overlay_.row(yy);
            for (int x = 0; x < w; ++x)
                p[x * 4 + 3] = 170;
        }
        font::drawText(overlay_, 4, y, lines[i], i == 0 && !error_.empty() ? Color{255, 120, 120} : Color{230, 230, 230});
    }
    renderer_->endFrame(&overlay_);
    if (mesh)
        renderer_->destroyMesh(mesh);
}

} // namespace reluva::idtech3
