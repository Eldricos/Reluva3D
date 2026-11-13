// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/pages/viewers.h"

#include <algorithm>
#include <cstdlib>

namespace reluva::clue {

namespace {

constexpr int kInfoH = 26;
constexpr int kAreaH = kScreenH - kInfoH;
constexpr Color kBackground{24, 24, 36};

void plot(Surface &s, int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= s.w || y >= kAreaH)
        return;
    uint8_t *p = s.row(y) + x * 4;
    p[0] = c.r;
    p[1] = c.g;
    p[2] = c.b;
    p[3] = 255;
}

void line(Surface &s, int x0, int y0, int x1, int y1, Color c) {
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
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

MansionViewer::MansionViewer(System &sys, Surface &screen, const GameData &data)
    : _sys(sys), _screen(screen), _data(data) {
    const std::filesystem::path *path = _data.find("clue.lvl");
    if (!path) {
        _error = "no clue.lvl";
        return;
    }
    if (Error err = _levelFile.open(*path)) {
        _error = err.message;
        return;
    }
    if (Error err = parseLevel(_levelFile, _level)) {
        _error = "clue.lvl: " + err.message;
        return;
    }
    if (Error err = _mansion.build(_data, _level)) {
        _error = "clue.lvl: " + err.message;
        return;
    }
    centre();
}

void MansionViewer::activate() {
    _dirty = true;
    _sys.setWindowTitle(std::string(gameName(_data.language())) + ": " + mansionName(_data.language()));
}

void MansionViewer::centre() {
    const ScreenRect &b = _mansion.bounds();
    _camX = b.x + b.w / 2 - kScreenW / 2;
    _camY = b.y + b.h / 2 - kAreaH / 2;
}

void MansionViewer::scroll(int dx, int dy) {
    const ScreenRect &b = _mansion.bounds();
    _camX = std::clamp(_camX + dx, b.x - kScreenW / 2, b.x + b.w - kScreenW / 2);
    _camY = std::clamp(_camY + dy, b.y - kAreaH / 2, b.y + b.h - kAreaH / 2);
    _dirty = true;
}

bool MansionViewer::shown(const Mansion::Item &item) const {
    return _mansion.shown(item, _look);
}

void MansionViewer::handleEvent(const Event &ev) {
    if (!loaded())
        return;
    if (_overview) {
        if ((ev.type == EventType::kKeyDown && (ev.key == 'm' || ev.key == key::kEscape)) ||
            (ev.type == EventType::kMouseDown && ev.button != MouseButton::kLeft)) {
            _overview = false;
            _dirty = true;
        } else if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft && ev.mouseY < kAreaH) {
            // Go to the place clicked.
            const ScreenRect &b = _mansion.bounds();
            const double scale = std::max(double(b.w) / kScreenW, double(b.h) / kAreaH);
            const int ox = (kScreenW - int(b.w / scale)) / 2;
            const int oy = (kAreaH - int(b.h / scale)) / 2;
            _camX = b.x + int((ev.mouseX - ox) * scale) - kScreenW / 2;
            _camY = b.y + int((ev.mouseY - oy) * scale) - kAreaH / 2;
            scroll(0, 0);
            _overview = false;
            _dirty = true;
        }
        return;
    }
    const bool fast = ev.mods & keymod::kShift;
    const int step = fast ? 128 : 32;
    switch (ev.type) {
    case EventType::kKeyDown:
        switch (ev.key) {
        case key::kLeft:
            scroll(-step, 0);
            break;
        case key::kRight:
            scroll(step, 0);
            break;
        case key::kUp:
            scroll(0, -step);
            break;
        case key::kDown:
            scroll(0, step);
            break;
        case key::kHome:
            centre();
            break;
        case 'w':
            // See-through, solid, left out; then no walls at all.
            if (!_look.walls) {
                _look.walls = true;
                _look.nearWalls = NearWalls::kSeeThrough;
            } else if (_look.nearWalls == NearWalls::kSeeThrough)
                _look.nearWalls = NearWalls::kSolid;
            else if (_look.nearWalls == NearWalls::kSolid)
                _look.nearWalls = NearWalls::kHidden;
            else
                _look.walls = false;
            break;
        case 'o':
            _look.outside = !_look.outside;
            break;
        case 'n':
            _look.tileNumbers = !_look.tileNumbers;
            break;
        case 'f':
            _footprints = !_footprints;
            break;
        case 'm':
            _overview = true;
            _whole = {};
            break;
        case key::kSpace:
            _paused = !_paused;
            break;
        default:
            return;
        }
        _dirty = true;
        break;
    case EventType::kMouseDown:
        if (ev.button == MouseButton::kLeft) {
            _dragging = true;
            _dragX = ev.mouseX;
            _dragY = ev.mouseY;
        }
        break;
    case EventType::kMouseUp:
        if (ev.button == MouseButton::kLeft)
            _dragging = false;
        break;
    case EventType::kMouseMove:
        if (_dragging) {
            scroll(_dragX - ev.mouseX, _dragY - ev.mouseY);
            _dragX = ev.mouseX;
            _dragY = ev.mouseY;
        }
        _mouseX = ev.mouseX;
        _mouseY = ev.mouseY;
        _dirty = true;
        break;
    default:
        break;
    }
}

void MansionViewer::update(uint64_t nowMs) {
    _now = nowMs;
    // The animations run at 10 frames a second.
    if (!_paused && nowMs / 100 != _tick) {
        _tick = nowMs / 100;
        if (!_overview)
            _dirty = true;
    }
    if (_dirty)
        draw();
}

void MansionViewer::drawScene(Surface &target, int camX, int camY) {
    MansionLook look = _look;
    look.animate = !_paused;
    _mansion.draw(target, camX, camY, look, _now);
}

void MansionViewer::drawFootprint(const Mansion::Item &item) {
    const WorldBox &b = item.box;
    const auto toScreen = [&](int x, int y, int z, int &sx, int &sy) {
        sx = x - y - _camX;
        sy = (x + y) / 2 - z - _camY;
    };
    const int xs[4] = {b.x, b.x + b.sizeX, b.x + b.sizeX, b.x};
    const int ys[4] = {b.y, b.y, b.y + b.sizeY, b.y + b.sizeY};
    for (int z : {0, int(b.height)})
        for (int k = 0; k < 4; ++k) {
            int x0;
            int y0;
            int x1;
            int y1;
            toScreen(xs[k], ys[k], z, x0, y0);
            toScreen(xs[(k + 1) % 4], ys[(k + 1) % 4], z, x1, y1);
            line(_screen, x0, y0, x1, y1, z ? Color{255, 220, 60} : Color{255, 120, 40});
        }
    for (int k = 0; k < 4; ++k) {
        int x0;
        int y0;
        int x1;
        int y1;
        toScreen(xs[k], ys[k], 0, x0, y0);
        toScreen(xs[k], ys[k], b.height, x1, y1);
        line(_screen, x0, y0, x1, y1, {255, 170, 50});
    }
}

const Mansion::Item *MansionViewer::pick(int x, int y) const {
    const auto &order = _mansion.drawOrder();
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const Mansion::Item &item = _mansion.items()[*it];
        if (!shown(item) || !item.rect.contains(x, y) || (item.layer == 4 && _look.nearWalls == NearWalls::kSeeThrough))
            continue;
        // Its frame 0's pieces, where they aren't transparent.
        for (const Mansion::Piece &p : (*item.frames)[0]) {
            const int px = x - item.anchorX - p.dx;
            const int py = y - item.anchorY - p.dy;
            const image::Image &img = p.image->pixels;
            if (px >= 0 && py >= 0 && px < img.w && py < img.h && img.pixel(px, py)[3])
                return &item;
        }
    }
    return nullptr;
}

void MansionViewer::drawOverview() {
    const ScreenRect &b = _mansion.bounds();
    if (_whole.w == 0) {
        _whole.create(b.w, b.h, PixelFormat::kRGBA32);
        fillRect(_whole, 0, 0, b.w, b.h, kBackground);
        drawScene(_whole, b.x, b.y);
    }
    fillRect(_screen, 0, 0, kScreenW, kAreaH, kBackground);
    // Shrink by averaging the pixels each screen pixel covers.
    const double scale = std::max(double(b.w) / kScreenW, double(b.h) / kAreaH);
    const int w = int(b.w / scale);
    const int h = int(b.h / scale);
    const int ox = (kScreenW - w) / 2;
    const int oy = (kAreaH - h) / 2;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int sx0 = int(x * scale);
            const int sy0 = int(y * scale);
            const int sx1 = std::min(b.w, std::max(sx0 + 1, int((x + 1) * scale)));
            const int sy1 = std::min(b.h, std::max(sy0 + 1, int((y + 1) * scale)));
            unsigned r = 0;
            unsigned g = 0;
            unsigned bl = 0;
            unsigned n = 0;
            for (int sy = sy0; sy < sy1; ++sy)
                for (int sx = sx0; sx < sx1; ++sx) {
                    const uint8_t *p = _whole.row(sy) + sx * 4;
                    r += p[0];
                    g += p[1];
                    bl += p[2];
                    ++n;
                }
            uint8_t *d = _screen.row(oy + y) + (ox + x) * 4;
            d[0] = uint8_t(r / n);
            d[1] = uint8_t(g / n);
            d[2] = uint8_t(bl / n);
            d[3] = 255;
        }
    // Where the view is.
    const int vx = ox + int((_camX - b.x) / scale);
    const int vy = oy + int((_camY - b.y) / scale);
    const int vw = int(kScreenW / scale);
    const int vh = int(kAreaH / scale);
    line(_screen, vx, vy, vx + vw, vy, {255, 255, 255});
    line(_screen, vx + vw, vy, vx + vw, vy + vh, {255, 255, 255});
    line(_screen, vx + vw, vy + vh, vx, vy + vh, {255, 255, 255});
    line(_screen, vx, vy + vh, vx, vy, {255, 255, 255});
    drawInfo(_screen,
             "The whole house (" + std::to_string(b.w) + "x" + std::to_string(b.h) + ", shrunk " +
                 std::to_string(int(scale * 10) / 10) + "." + std::to_string(int(scale * 10) % 10) + "x)",
             "Click to go there; M or Esc to go back");
}

void MansionViewer::draw() {
    _dirty = false;
    if (!loaded()) {
        fillRect(_screen, 0, 0, kScreenW, kAreaH, kBackground);
        drawInfo(_screen, "The mansion can't be shown", _error);
        return;
    }
    if (_overview) {
        drawOverview();
        return;
    }
    fillRect(_screen, 0, 0, kScreenW, kAreaH, kBackground);
    drawScene(_screen, _camX, _camY);
    std::string line1 = std::to_string(_mansion.items().size()) + " objects";
    if (!_mansion.problems().empty())
        line1 += " (" + std::to_string(_mansion.problems().size()) +
                 " couldn't be shown: " + _mansion.problems().front() + ")";
    std::string line2 = std::string("near walls ") +
                        (!_look.walls                                ? "none at all"
                         : _look.nearWalls == NearWalls::kSeeThrough ? "see-through"
                         : _look.nearWalls == NearWalls::kSolid      ? "solid"
                                                                     : "hidden") +
                        "  outside " + (_look.outside ? "on" : "off");
    if (_paused)
        line2 += "  paused";
    const Mansion::Item *hit = _mouseY >= 0 && _mouseY < kAreaH ? pick(_mouseX + _camX, _mouseY + _camY) : nullptr;
    if (_footprints) {
        for (size_t i : _mansion.drawOrder()) {
            const Mansion::Item &item = _mansion.items()[i];
            if (shown(item) && !item.flat && item.rect.overlaps({_camX, _camY, kScreenW, kAreaH}))
                drawFootprint(item);
        }
    }
    if (hit) {
        const Placement &pl = _level.placements[hit->placement];
        line1 = hit->object->name + "  layer " + std::to_string(pl.layer) + "  at " + std::to_string(hit->box.x) + "," +
                std::to_string(hit->box.y) + "  box " + std::to_string(hit->box.sizeX) + "x" +
                std::to_string(hit->box.sizeY) + "x" + std::to_string(hit->box.height) + "  z " + std::to_string(pl.z) +
                "  type " + std::to_string(hit->object->type);
        if (!_footprints && !hit->flat)
            drawFootprint(*hit);
    }
    drawInfo(_screen, line1, line2);
}

} // namespace reluva::clue
