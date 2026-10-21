// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Menu page: the game's own menus (vm/ui.qvm) run by the QVM
// interpreter, their pictures drawn by the GPU renderer.

#include "engines/idtech3/viewers.h"

#include "common/font.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::idtech3 {

namespace {

constexpr int kFrameW = 960;
constexpr int kFrameH = 720;
constexpr uint64_t kHintMs = 6000;

// Quake III's key numbers, which the menus take (Elite Force's assumed the
// same): ASCII for printable keys, then its own list from 128.
namespace q3key {
constexpr int32_t Tab = 9, Enter = 13, Escape = 27, Space = 32, Backspace = 127;
constexpr int32_t Up = 132, Down = 133, Left = 134, Right = 135;
constexpr int32_t Alt = 136, Ctrl = 137, Shift = 138, Insert = 139, Delete = 140;
constexpr int32_t PageDown = 141, PageUp = 142, Home = 143, End = 144, F1 = 145;
constexpr int32_t KpEnter = 169;
constexpr int32_t Mouse1 = 178, Mouse2 = 179, Mouse3 = 180, WheelDown = 183, WheelUp = 184;
} // namespace q3key

int32_t toQ3Key(uint32_t k) {
    switch (k) {
    case key::Tab: return q3key::Tab;
    case key::Return: return q3key::Enter;
    case key::KpEnter: return q3key::KpEnter;
    case key::Escape: return q3key::Escape;
    case key::Space: return q3key::Space;
    case key::Backspace: return q3key::Backspace;
    case key::Delete: return q3key::Delete;
    case key::Up: return q3key::Up;
    case key::Down: return q3key::Down;
    case key::Left: return q3key::Left;
    case key::Right: return q3key::Right;
    case key::Insert: return q3key::Insert;
    case key::Home: return q3key::Home;
    case key::End: return q3key::End;
    case key::PageUp: return q3key::PageUp;
    case key::PageDown: return q3key::PageDown;
    case key::LShift:
    case key::RShift: return q3key::Shift;
    case key::LCtrl:
    case key::RCtrl: return q3key::Ctrl;
    case key::LAlt:
    case key::RAlt: return q3key::Alt;
    default: break;
    }
    if (k >= key::F1 && k < key::F1 + 12)
        return q3key::F1 + int32_t(k - key::F1);
    if (k > 32 && k < 127)
        return int32_t(k);
    return 0;
}

int32_t toQ3Button(MouseButton b) {
    switch (b) {
    case MouseButton::Left: return q3key::Mouse1;
    case MouseButton::Right: return q3key::Mouse2;
    case MouseButton::Middle: return q3key::Mouse3;
    default: return 0;
    }
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
    if (!ui_ && error_.empty())
        start();
    setTitle(error_.empty() ? "Elite Force's own menus (vm/ui.qvm)" : "stopped");
}

void MenuViewer::start() {
    ui_ = std::make_unique<eliteforce::UiHost>(ctx_.files, cvars_);
    ui_->setScreenSize(kFrameW, kFrameH);
    Error err = ui_->load();
    if (!err)
        err = ui_->call(eliteforce::UiHost::Init);
    if (!err)
        err = ui_->call(eliteforce::UiHost::SetActiveMenu, {1}); // the main menu
    if (err)
        stop(err);
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
    ui_.reset();
}

void MenuViewer::deactivate() {
    materials_.clear();
    shaders_.reset();
    renderer_ = nullptr;
}

bool MenuViewer::takesKey(const Event& ev) const {
    // Esc and Tab belong to the menus; with Ctrl they leave, as elsewhere.
    return ui_ && !(ev.mods & keymod::Ctrl);
}

void MenuViewer::handleEvent(const Event& ev) {
    if (!ui_)
        return;
    Error err;
    switch (ev.type) {
    case EventType::KeyDown:
    case EventType::KeyUp:
        if (ev.type == EventType::KeyDown && ev.key == 'r' && (ev.mods & keymod::Ctrl)) {
            ui_.reset(); // Ctrl+R: start the menus again
            start();
            return;
        }
        if (const int32_t k = toQ3Key(ev.key))
            err = ui_->call(eliteforce::UiHost::KeyEvent, {k, ev.type == EventType::KeyDown});
        break;
    case EventType::MouseDown:
    case EventType::MouseUp:
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
    case EventType::MouseMove: {
        // The menus move their own cursor by deltas, in 640x480 units.
        if (haveMouse_) {
            mouseX_ += float(ev.mouseX - lastX_) * 640.0f / float(kFrameW);
            mouseY_ += float(ev.mouseY - lastY_) * 480.0f / float(kFrameH);
            const auto dx = int32_t(mouseX_), dy = int32_t(mouseY_);
            mouseX_ -= float(dx);
            mouseY_ -= float(dy);
            if (dx || dy)
                err = ui_->call(eliteforce::UiHost::MouseEvent, {dx, dy});
        }
        haveMouse_ = true;
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
        break;
    }
    default: break;
    }
    if (err)
        stop(err);
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
    if (ui_) {
        ui_->setTime(time);
        ui_->clearPictures();
        if (Error err = ui_->call(eliteforce::UiHost::Refresh, {time}))
            stop(err);
        else
            pictures = ui_->pictures();
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
        lines.push_back("Ctrl+R: start them again.");
    }
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
