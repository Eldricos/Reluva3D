// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's WORLD.WDB pages: its textures, and its models and
// parts in 3D (docs/engines/legoisland/formats/wdb.md).

#include "common/fs.h"
#include "engines/legoisland/pages/viewers.h"
#include "graphics/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace reluva::legoisland {

namespace {

constexpr int kListW = 300;
constexpr int kRowH = font::kLineHeight + 1;
constexpr Color kBackground = {40, 40, 48};
constexpr Color kText = {230, 230, 230};
constexpr Color kDim = {150, 150, 160};
constexpr Color kSelected = {70, 90, 140};
constexpr Color kError = {255, 120, 120};

void fillRect(Surface &s, int x, int y, int w, int h, Color c) {
    const int x0 = std::max(x, 0);
    const int y0 = std::max(y, 0);
    const int x1 = std::min(x + w, s.w);
    const int y1 = std::min(y + h, s.h);
    for (int py = y0; py < y1; ++py) {
        uint8_t *p = s.row(py) + size_t(x0) * 4;
        for (int px = x0; px < x1; ++px, p += 4) {
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
    }
}

size_t step(size_t current, size_t count, uint32_t key) {
    if (count == 0)
        return 0;
    switch (key) {
    case key::kUp:
        return current > 0 ? current - 1 : current;
    case key::kDown:
        return std::min(current + 1, count - 1);
    case key::kPageUp:
        return current > 20 ? current - 20 : 0;
    case key::kPageDown:
        return std::min(current + 20, count - 1);
    case key::kHome:
        return 0;
    case key::kEnd:
        return count - 1;
    default:
        return current;
    }
}

} // namespace

WdbTexturePage::WdbTexturePage(Surface &screen, const WdbFile &wdb) : _screen(screen) {
    add("shared", wdb.sharedTextures());
    add("shared part", wdb.sharedPart().textures);
    // Read once: the rows point into them.
    const std::vector<WdbPartRef> parts = wdb.allParts();
    const std::vector<WdbModelRef> models = wdb.allModels();
    _parts.resize(parts.size());
    _models.resize(models.size());
    for (size_t i = 0; i < parts.size(); i++)
        if (Error e = wdb.readPart(parts[i], _parts[i]))
            _errors.push_back(e.message);
    for (size_t i = 0; i < models.size(); i++)
        if (Error e = wdb.readModel(models[i], _models[i]))
            _errors.push_back(e.message);
    for (size_t i = 0; i < parts.size(); i++)
        add(parts[i].name, _parts[i].textures);
    for (size_t i = 0; i < models.size(); i++)
        add(models[i].name, _models[i].textures);
}

void WdbTexturePage::add(const std::string &owner, const std::vector<WdbTexture> &textures) {
    for (const WdbTexture &t : textures)
        for (size_t i = 0; i < t.images.size(); i++)
            _rows.push_back({owner, &t, i});
}

void WdbTexturePage::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown)
        return;
    const size_t next = step(_current, _rows.size(), ev.key);
    if (next != _current) {
        _current = next;
        redraw();
    }
}

void WdbTexturePage::redraw() {
    fillRect(_screen, 0, 0, _screen.w, _screen.h, kBackground);
    font::drawText(_screen, 4, 4, "WORLD.WDB: textures (Tab: next page)", kDim);
    // The list, the current row kept in sight.
    const int top = 4 + 2 * kRowH;
    const int rows = (_screen.h - top - 4) / kRowH;
    const size_t first = _current > size_t(rows / 2) ? _current - size_t(rows / 2) : 0;
    for (int i = 0; i < rows && first + size_t(i) < _rows.size(); i++) {
        const Row &r = _rows[first + size_t(i)];
        const int y = top + i * kRowH;
        if (first + size_t(i) == _current)
            fillRect(_screen, 0, y - 1, kListW, kRowH, kSelected);
        std::string label = r.owner + ": " + r.texture->name;
        if (r.image > 0)
            label += " (2)";
        font::drawText(_screen, 4, y, label, kText);
    }
    if (_rows.empty())
        return;

    const Row &r = _rows[_current];
    const WdbImage &img = r.texture->images[r.image];
    const int x0 = kListW + 12;
    int y = top;
    char buf[200];
    std::snprintf(buf, sizeof buf, "%s, in %s: %ux%u, %zu colours%s", r.texture->name.c_str(), r.owner.c_str(),
                  img.width, img.height, img.palette.size() / 3,
                  r.texture->images.size() > 1 ? (r.image ? ", its second picture" : ", one of two pictures") : "");
    font::drawText(_screen, x0, y, buf, kText);
    y += 2 * kRowH;
    for (const std::string &e : _errors) {
        font::drawText(_screen, x0, y, e, kError);
        y += kRowH;
    }

    // The palette, a swatch a colour.
    const int swatch = 16;
    for (size_t c = 0; c < img.palette.size() / 3; c++) {
        const int sx = x0 + int(c % 32) * (swatch + 2);
        const int sy = y + int(c / 32) * (swatch + 2);
        fillRect(_screen, sx, sy, swatch, swatch, {img.palette[c * 3], img.palette[c * 3 + 1], img.palette[c * 3 + 2]});
    }
    y += int((img.palette.size() / 3 + 31) / 32) * (swatch + 2) + kRowH;

    // The picture, as many times its size as fits.
    if (img.width == 0 || img.height == 0)
        return;
    const int fitW = _screen.w - x0 - 8;
    const int fitH = _screen.h - y - 8;
    const int zoom = std::max(1, std::min(fitW / int(img.width), fitH / int(img.height)));
    std::vector<uint8_t> rgba;
    img.toRGBA(rgba);
    for (int py = 0; py < int(img.height) * zoom && y + py < _screen.h; py++) {
        uint8_t *dst = _screen.row(y + py) + size_t(x0) * 4;
        const uint8_t *src = &rgba[size_t(py / zoom) * img.width * 4];
        for (int px = 0; px < int(img.width) * zoom && x0 + px < _screen.w; px++)
            std::copy_n(src + size_t(px / zoom) * 4, 4, dst + size_t(px) * 4);
    }
}

WdbModelPage::WdbModelPage(System &sys, Surface &screen, const WdbFile &wdb)
    : _sys(sys), _screen(screen), _wdb(wdb), _lib(wdb) {
    _modelRefs = wdb.allModels();
    _partRefs = wdb.allParts();
    for (size_t i = 0; i < _modelRefs.size(); i++)
        _items.push_back({_modelRefs[i].name, int(i), -1, -1});
    for (size_t i = 0; i < _partRefs.size(); i++)
        _items.push_back({"part " + _partRefs[i].name, -1, int(i), -1});
    for (size_t i = 0; i < wdb.sharedPart().rois.size(); i++)
        _items.push_back({"shared part " + wdb.sharedPart().rois[i].name, -1, -1, int(i)});
}

WdbModelPage::~WdbModelPage() {
    deactivate();
}

void WdbModelPage::activate() {
    if (Error e = _sys.init3D(kViewerW, kViewerH, _renderer)) {
        _renderer = nullptr;
        fillRect(_screen, 0, 0, _screen.w, _screen.h, kBackground);
        font::drawText(_screen, 4, 4, "WORLD.WDB's models need 3D rendering, which isn't available:", kError);
        font::drawText(_screen, 4, 4 + kRowH, e.message, kText);
        return;
    }
    _scene = std::make_unique<RoiScene>(*_renderer);
    _overlay.create(kViewerW, kViewerH, PixelFormat::kRGBA32);
    show(_current);
}

void WdbModelPage::deactivate() {
    _scene.reset();
    _placed.clear();
    _renderer = nullptr;
}

void WdbModelPage::show(size_t index) {
    _current = index;
    _lod = 0;
    build();
    // Frame what's drawn, as placed.
    gfx::Vec3 lo{1e30f, 1e30f, 1e30f};
    gfx::Vec3 hi{-1e30f, -1e30f, -1e30f};
    bool any = false;
    for (const auto &[piece, transform] : _placed)
        any |= _scene->grow(piece, transform, lo, hi);
    _orbit.frame(lo, hi, any);
}

void WdbModelPage::build() {
    _scene->clear();
    _placed.clear();
    _error.clear();
    if (_items.empty())
        return;
    const Item &item = _items[_current];
    const TextureLists shared = {&_wdb.sharedPart().textures, &_wdb.sharedTextures()};
    if (item.model >= 0) {
        if (Error e = _wdb.readModel(_modelRefs[size_t(item.model)], _model)) {
            _error = e.message;
            return;
        }
        // Each ROI where its model's animation puts it at its start.
        NodePlaces places;
        placeNodes(_model.animation, 0, places);
        std::vector<std::pair<size_t, const WdbRoi *>> pieces;
        addModelRois(*_scene, _lib, _model, _lod, {}, pieces);
        for (const auto &[piece, roi] : pieces) {
            const gfx::Mat4 *at = places.find(roi->name);
            _placed.emplace_back(piece, at ? *at : gfx::identity());
        }
    } else if (item.part >= 0) {
        const WdbPart *part = _lib.part(_partRefs[size_t(item.part)].name);
        if (!part) {
            _error = "The part can't be read";
            return;
        }
        TextureLists lists = {&part->textures};
        lists.insert(lists.end(), shared.begin(), shared.end());
        for (const WdbRoi &roi : part->rois)
            if (const size_t piece = _scene->add(roi.lods, _lod, lists); piece != RoiScene::kNone)
                _placed.emplace_back(piece, gfx::identity());
    } else {
        const size_t piece = _scene->add(_wdb.sharedPart().rois[size_t(item.sharedRoi)].lods, _lod, shared);
        if (piece != RoiScene::kNone)
            _placed.emplace_back(piece, gfx::identity());
    }
}

void WdbModelPage::handleEvent(const Event &ev) {
    if (!_renderer || _items.empty() || _orbit.handle(ev) || ev.type != EventType::kKeyDown)
        return;
    const size_t next = step(_current, _items.size(), ev.key);
    if (next != _current) {
        show(next);
    } else if (ev.key == 'l') {
        _lod = (_lod + 1) % std::max(1, _scene->lodLevels());
        build();
    } else if (ev.key == 'r') {
        show(_current);
    } else if (ev.key == 'i') {
        _showInfo = !_showInfo;
    }
}

void WdbModelPage::drawInfo() {
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    int y = 4;
    auto line = [&](const std::string &s, Color c) {
        if (y > kViewerH - kRowH)
            return;
        font::drawText(_overlay, 4, y, s, c);
        y += kRowH;
    };
    if (_items.empty()) {
        line("WORLD.WDB has no models", kError);
        return;
    }
    const Item &item = _items[_current];
    line("WORLD.WDB: " + item.name + "  (" + std::to_string(_current + 1) + "/" + std::to_string(_items.size()) +
             ", Tab: next page)",
         {150, 200, 255});
    if (!_error.empty())
        line(_error, kError);
    if (!_showInfo)
        return;
    char buf[200];
    std::snprintf(buf, sizeof buf, "LOD %d of %d from the finest (L), %zu triangles; drag to turn, I hides this",
                  _lod + 1, _scene->lodLevels(), _scene->triangles());
    line(buf, kText);
    if (item.model >= 0) {
        const WdbModelRef &ref = _modelRefs[size_t(item.model)];
        line("presenter " + (ref.presenter.empty() ? std::string("-") : ref.presenter) + ", " +
                 std::to_string(_model.textures.size()) + " textures of its own",
             kText);
        std::vector<std::string> worlds;
        for (const WdbWorld &w : _wdb.worlds())
            for (const WdbModelRef &m : w.models)
                if (m.offset == ref.offset && std::find(worlds.begin(), worlds.end(), w.name) == worlds.end())
                    worlds.push_back(w.name);
        std::string in = "in";
        for (const std::string &w : worlds)
            in += " " + w;
        line(in, kText);
    }
    for (const std::string &m : _scene->missing())
        line("not found: " + m, kError);
}

void WdbModelPage::update(uint64_t) {
    if (!_renderer)
        return;
    gfx::Vec3 eye{};
    const gfx::Mat4 viewProj = _orbit.viewProj(kViewerW, kViewerH, eye);
    _renderer->beginFrame(viewProj, eye, {70, 74, 82});
    for (const auto &[piece, transform] : _placed)
        _scene->put(piece, transform);
    _scene->flush();
    drawInfo();
    _renderer->endFrame(&_overlay);
}

} // namespace reluva::legoisland
