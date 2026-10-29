// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/viewers.h"

#include <algorithm>
#include <cstdlib>

namespace reluva::clue {

namespace {

constexpr int kInfoH = 26;
constexpr int kAreaH = kScreenH - kInfoH;
constexpr Color kBackground{24, 24, 36};
// How much of the nearest walls shows when they're see-through: about half,
// judged against recordings of the original.
constexpr int kSeeThrough = 128;

void plot(Surface& s, int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= s.w || y >= kAreaH)
        return;
    uint8_t* p = s.row(y) + x * 4;
    p[0] = c.r;
    p[1] = c.g;
    p[2] = c.b;
    p[3] = 255;
}

void line(Surface& s, int x0, int y0, int x1, int y1, Color c) {
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int guard = 0; guard < 4096; ++guard) {
        plot(s, x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

} // namespace

MansionViewer::MansionViewer(System& sys, Surface& screen, const GameData& data)
    : sys_(sys), screen_(screen), data_(data) {
    const std::filesystem::path* path = data_.find("clue.lvl");
    if (!path) {
        error_ = "no clue.lvl";
        return;
    }
    if (Error err = levelFile_.open(*path)) {
        error_ = err.message;
        return;
    }
    if (Error err = parseLevel(levelFile_, level_)) {
        error_ = "clue.lvl: " + err.message;
        return;
    }
    if (Error err = mansion_.build(data_, level_)) {
        error_ = "clue.lvl: " + err.message;
        return;
    }
    centre();
}

void MansionViewer::activate() {
    dirty_ = true;
    sys_.setWindowTitle("Clue: Boddy Mansion");
}

void MansionViewer::centre() {
    const ScreenRect& b = mansion_.bounds();
    camX_ = b.x + b.w / 2 - kScreenW / 2;
    camY_ = b.y + b.h / 2 - kAreaH / 2;
}

void MansionViewer::scroll(int dx, int dy) {
    const ScreenRect& b = mansion_.bounds();
    camX_ = std::clamp(camX_ + dx, b.x - kScreenW / 2, b.x + b.w - kScreenW / 2);
    camY_ = std::clamp(camY_ + dy, b.y - kAreaH / 2, b.y + b.h - kAreaH / 2);
    dirty_ = true;
}

bool MansionViewer::shown(const Mansion::Item& item) const {
    if (item.object->type == ObjectDef::TileNumber)
        return tileNumbers_;
    if (item.layer == 4)
        return walls_ != Walls::Hidden;
    if (item.layer == 6)
        return outside_;
    return true;
}

void MansionViewer::handleEvent(const Event& ev) {
    if (!loaded())
        return;
    if (overview_) {
        if ((ev.type == EventType::KeyDown && (ev.key == 'm' || ev.key == key::Escape)) ||
            (ev.type == EventType::MouseDown && ev.button != MouseButton::Left)) {
            overview_ = false;
            dirty_ = true;
        } else if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left && ev.mouseY < kAreaH) {
            // Go to the place clicked.
            const ScreenRect& b = mansion_.bounds();
            const double scale = std::max(double(b.w) / kScreenW, double(b.h) / kAreaH);
            const int ox = (kScreenW - int(b.w / scale)) / 2, oy = (kAreaH - int(b.h / scale)) / 2;
            camX_ = b.x + int((ev.mouseX - ox) * scale) - kScreenW / 2;
            camY_ = b.y + int((ev.mouseY - oy) * scale) - kAreaH / 2;
            scroll(0, 0);
            overview_ = false;
            dirty_ = true;
        }
        return;
    }
    const bool fast = ev.mods & keymod::Shift;
    const int step = fast ? 128 : 32;
    switch (ev.type) {
    case EventType::KeyDown:
        switch (ev.key) {
        case key::Left: scroll(-step, 0); break;
        case key::Right: scroll(step, 0); break;
        case key::Up: scroll(0, -step); break;
        case key::Down: scroll(0, step); break;
        case key::Home: centre(); break;
        case 'w':
            walls_ = walls_ == Walls::SeeThrough ? Walls::Solid
                     : walls_ == Walls::Solid    ? Walls::Hidden
                                                 : Walls::SeeThrough;
            break;
        case 'o': outside_ = !outside_; break;
        case 'n': tileNumbers_ = !tileNumbers_; break;
        case 'f': footprints_ = !footprints_; break;
        case 'm':
            overview_ = true;
            whole_ = {};
            break;
        case key::Space: paused_ = !paused_; break;
        default: return;
        }
        dirty_ = true;
        break;
    case EventType::MouseDown:
        if (ev.button == MouseButton::Left) {
            dragging_ = true;
            dragX_ = ev.mouseX;
            dragY_ = ev.mouseY;
        }
        break;
    case EventType::MouseUp:
        if (ev.button == MouseButton::Left)
            dragging_ = false;
        break;
    case EventType::MouseMove:
        if (dragging_) {
            scroll(dragX_ - ev.mouseX, dragY_ - ev.mouseY);
            dragX_ = ev.mouseX;
            dragY_ = ev.mouseY;
        }
        mouseX_ = ev.mouseX;
        mouseY_ = ev.mouseY;
        dirty_ = true;
        break;
    default: break;
    }
}

void MansionViewer::update(uint64_t nowMs) {
    now_ = nowMs;
    // The animations run at 10 frames a second.
    if (!paused_ && nowMs / 100 != tick_) {
        tick_ = nowMs / 100;
        if (!overview_)
            dirty_ = true;
    }
    if (dirty_)
        draw();
}

void MansionViewer::drawScene(Surface& target, int camX, int camY) {
    const int viewH = &target == &screen_ ? kAreaH : target.h;
    const ScreenRect view{camX, camY, target.w, viewH};
    for (size_t i : mansion_.drawOrder()) {
        const Mansion::Item& item = mansion_.items()[i];
        if (!shown(item) || !item.rect.overlaps(view))
            continue;
        const auto& frames = *item.frames;
        size_t f = 0;
        if (!paused_ && frames.size() > 1)
            f = size_t(now_ * uint64_t(item.animation->framesPerSecond) / 1000) % frames.size();
        const int opacity = item.layer == 4 && walls_ == Walls::SeeThrough ? kSeeThrough : 255;
        for (const Mansion::Piece& p : frames[f])
            image::blit(target, p.image->pixels, item.anchorX + p.dx - camX, item.anchorY + p.dy - camY, opacity);
    }
}

void MansionViewer::drawFootprint(const Mansion::Item& item) {
    const WorldBox& b = item.box;
    const auto toScreen = [&](int x, int y, int z, int& sx, int& sy) {
        sx = x - y - camX_;
        sy = (x + y) / 2 - z - camY_;
    };
    const int xs[4] = {b.x, b.x + b.sizeX, b.x + b.sizeX, b.x};
    const int ys[4] = {b.y, b.y, b.y + b.sizeY, b.y + b.sizeY};
    for (int z : {0, int(b.height)})
        for (int k = 0; k < 4; ++k) {
            int x0, y0, x1, y1;
            toScreen(xs[k], ys[k], z, x0, y0);
            toScreen(xs[(k + 1) % 4], ys[(k + 1) % 4], z, x1, y1);
            line(screen_, x0, y0, x1, y1, z ? Color{255, 220, 60} : Color{255, 120, 40});
        }
    for (int k = 0; k < 4; ++k) {
        int x0, y0, x1, y1;
        toScreen(xs[k], ys[k], 0, x0, y0);
        toScreen(xs[k], ys[k], b.height, x1, y1);
        line(screen_, x0, y0, x1, y1, {255, 170, 50});
    }
}

const Mansion::Item* MansionViewer::pick(int x, int y) const {
    const auto& order = mansion_.drawOrder();
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const Mansion::Item& item = mansion_.items()[*it];
        if (!shown(item) || !item.rect.contains(x, y) || (item.layer == 4 && walls_ == Walls::SeeThrough))
            continue;
        // Its frame 0's pieces, where they aren't transparent.
        for (const Mansion::Piece& p : (*item.frames)[0]) {
            const int px = x - item.anchorX - p.dx, py = y - item.anchorY - p.dy;
            const image::Image& img = p.image->pixels;
            if (px >= 0 && py >= 0 && px < img.w && py < img.h && img.pixel(px, py)[3])
                return &item;
        }
    }
    return nullptr;
}

void MansionViewer::drawOverview() {
    const ScreenRect& b = mansion_.bounds();
    if (whole_.w == 0) {
        whole_.create(b.w, b.h, PixelFormat::RGBA32);
        fillRect(whole_, 0, 0, b.w, b.h, kBackground);
        drawScene(whole_, b.x, b.y);
    }
    fillRect(screen_, 0, 0, kScreenW, kAreaH, kBackground);
    // Shrink by averaging the pixels each screen pixel covers.
    const double scale = std::max(double(b.w) / kScreenW, double(b.h) / kAreaH);
    const int w = int(b.w / scale), h = int(b.h / scale);
    const int ox = (kScreenW - w) / 2, oy = (kAreaH - h) / 2;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int sx0 = int(x * scale), sy0 = int(y * scale);
            const int sx1 = std::min(b.w, std::max(sx0 + 1, int((x + 1) * scale)));
            const int sy1 = std::min(b.h, std::max(sy0 + 1, int((y + 1) * scale)));
            unsigned r = 0, g = 0, bl = 0, n = 0;
            for (int sy = sy0; sy < sy1; ++sy)
                for (int sx = sx0; sx < sx1; ++sx) {
                    const uint8_t* p = whole_.row(sy) + sx * 4;
                    r += p[0];
                    g += p[1];
                    bl += p[2];
                    ++n;
                }
            uint8_t* d = screen_.row(oy + y) + (ox + x) * 4;
            d[0] = uint8_t(r / n);
            d[1] = uint8_t(g / n);
            d[2] = uint8_t(bl / n);
            d[3] = 255;
        }
    // Where the view is.
    const int vx = ox + int((camX_ - b.x) / scale), vy = oy + int((camY_ - b.y) / scale);
    const int vw = int(kScreenW / scale), vh = int(kAreaH / scale);
    line(screen_, vx, vy, vx + vw, vy, {255, 255, 255});
    line(screen_, vx + vw, vy, vx + vw, vy + vh, {255, 255, 255});
    line(screen_, vx + vw, vy + vh, vx, vy + vh, {255, 255, 255});
    line(screen_, vx, vy + vh, vx, vy, {255, 255, 255});
    drawInfo(screen_, "The whole house (" + std::to_string(b.w) + "x" + std::to_string(b.h) + ", shrunk " +
                          std::to_string(int(scale * 10) / 10) + "." + std::to_string(int(scale * 10) % 10) + "x)",
             "Click to go there; M or Esc to go back");
}

void MansionViewer::draw() {
    dirty_ = false;
    if (!loaded()) {
        fillRect(screen_, 0, 0, kScreenW, kAreaH, kBackground);
        drawInfo(screen_, "The mansion can't be shown", error_);
        return;
    }
    if (overview_) {
        drawOverview();
        return;
    }
    fillRect(screen_, 0, 0, kScreenW, kAreaH, kBackground);
    drawScene(screen_, camX_, camY_);
    std::string line1 = std::to_string(mansion_.items().size()) + " objects";
    if (!mansion_.problems().empty())
        line1 += " (" + std::to_string(mansion_.problems().size()) + " couldn't be shown: " +
                 mansion_.problems().front() + ")";
    std::string line2 = std::string("near walls ") +
                        (walls_ == Walls::SeeThrough ? "see-through" : walls_ == Walls::Solid ? "solid" : "hidden") +
                        "  outside " + (outside_ ? "on" : "off");
    if (paused_)
        line2 += "  paused";
    const Mansion::Item* hit = mouseY_ >= 0 && mouseY_ < kAreaH ? pick(mouseX_ + camX_, mouseY_ + camY_) : nullptr;
    if (footprints_) {
        for (size_t i : mansion_.drawOrder()) {
            const Mansion::Item& item = mansion_.items()[i];
            if (shown(item) && !item.flat && item.rect.overlaps({camX_, camY_, kScreenW, kAreaH}))
                drawFootprint(item);
        }
    }
    if (hit) {
        const Placement& pl = level_.placements[hit->placement];
        line1 = hit->object->name + "  layer " + std::to_string(pl.layer) + "  at " + std::to_string(hit->box.x) +
                "," + std::to_string(hit->box.y) + "  box " + std::to_string(hit->box.sizeX) + "x" +
                std::to_string(hit->box.sizeY) + "x" + std::to_string(hit->box.height) + "  id " +
                std::to_string(pl.id) + "  type " + std::to_string(hit->object->type);
        if (!footprints_ && !hit->flat)
            drawFootprint(*hit);
    }
    drawInfo(screen_, line1, line2);
}

} // namespace reluva::clue
