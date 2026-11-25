// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/play/hud.h"

#include "common/text.h"

#include <algorithm>

namespace reluva::storm3d {

namespace {

// The game's draws: the icons in (229, 229, 229), the counters' digits in
// the sprite font in (128, 128, 128), the cinematic view's buttons and
// their borders in their own greys.
constexpr std::array<uint8_t, 3> kIconTint = {229, 229, 229};
constexpr std::array<uint8_t, 3> kCounterTint = {128, 128, 128};
constexpr std::array<uint8_t, 3> kButtonTint = {153, 178, 171};
constexpr std::array<uint8_t, 3> kBorderTint = {128, 152, 149};
constexpr int kCounterCell = 6;
// The minimap: a texel a cell, 128 across.
constexpr int kMinimapCells = 128;
// What the game's alpha test keeps of an interface texel.
constexpr uint8_t kAlphaKept = 128;
constexpr const char *kCounters[] = {"dilithium", "crew", "officer"};

void fill(Surface &screen, const std::array<int, 4> &r, int dx, int dy) {
    for (int y = std::max(0, r[1] + dy); y < std::min(screen.h, r[1] + dy + r[3]); ++y)
        for (int x = std::max(0, r[0] + dx); x < std::min(screen.w, r[0] + dx + r[2]); ++x) {
            uint8_t *p = screen.row(y) + size_t(x) * 4;
            p[0] = p[1] = p[2] = 0;
            p[3] = 255;
        }
}

} // namespace

const std::vector<const char *> &Hud::panels() {
    static const std::vector<const char *> kPanels = {"cinematic", "dilithium", "crew",    "officer",
                                                      "speed",     "info",      "control", "minimap"};
    return kPanels;
}

std::string Hud::counterText(const Resources &r, int counter) {
    if (counter == 0)
        return std::to_string(r.dilithium);
    if (counter == 1)
        return std::to_string(r.crew);
    return std::to_string(r.officers) + "/" + std::to_string(r.maxOfficers);
}

Error Hud::load(const GameFiles &files, const std::string &race) {
    _files = &files;
    _layout = GuiConfig{};
    _tables.clear();
    _textures.clear();
    const GuiConfig::Loader layoutLoader = [&files](std::string_view name, std::string &text) {
        std::vector<uint8_t> bytes;
        if (Error err = files.read(name, bytes))
            return err;
        text.assign(bytes.begin(), bytes.end());
        return Error::success();
    };
    if (Error err = _layout.load("misc/gui_" + race + ".cfg", layoutLoader))
        return err;
    static const std::map<std::string, std::string> kTables = {
        {"fed", "gui_federation.spr"}, {"kli", "gui_klingon.spr"}, {"rom", "gui_romulan.spr"}, {"bor", "gui_borg.spr"}};
    const SpriteTable::Loader loader = [&files](std::string_view name, std::string &text) {
        std::vector<uint8_t> bytes;
        if (Error err = files.read("Sprites/" + std::string(name), bytes))
            return err;
        text.assign(bytes.begin(), bytes.end());
        return Error::success();
    };
    auto table = kTables.find(race);
    for (const std::string &name :
         {table == kTables.end() ? std::string("gui_federation.spr") : table->second, std::string("gui_global.spr")})
        if (Error err = _tables.emplace_back().load(name, loader))
            return err;
    if (_font.load(files))
        _font = SpriteFont{};
    return Error::success();
}

const image::Image *Hud::texture(const std::string &name) {
    auto [it, added] = _textures.try_emplace(text::toLower(name));
    if (added && _files) {
        std::vector<uint8_t> data;
        if (_files->read("Textures/RGB/" + name + ".tga", data) || image::decode(data, it->second))
            it->second = {};
    }
    return it->second.w > 0 ? &it->second : nullptr;
}

const SpriteTable::Sprite *Hud::sprite(const std::string &name) const {
    for (const SpriteTable &table : _tables)
        if (const SpriteTable::Sprite *s = table.sprite(name))
            return s;
    return nullptr;
}

void Hud::drawSprite(Surface &screen, const std::string &name, int x, int y, int w, int h,
                     const std::array<uint8_t, 3> &tint) {
    const SpriteTable::Sprite *sprite = this->sprite(name);
    const image::Image *tex = sprite ? texture(sprite->texture) : nullptr;
    if (!tex || w <= 0 || h <= 0)
        return;
    // The sprite's place measured against its reference size; the
    // texture may be another size.
    const double sx = sprite->referenceWidth > 0 ? double(tex->w) / sprite->referenceWidth : 1;
    const double sy = sprite->referenceHeight > 0 ? double(tex->h) / sprite->referenceHeight : 1;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const int px = x + i;
            const int py = y + j;
            if (px < 0 || py < 0 || px >= screen.w || py >= screen.h)
                continue;
            // A point a pixel, at the pixel's left and top edges: where
            // Direct3D's pixel centres fall on a quad on whole pixels.
            const int tx = int((sprite->u + sprite->w * i / w) * sx);
            const int ty = int((sprite->v + sprite->h * j / h) * sy);
            if (tx < 0 || ty < 0 || tx >= tex->w || ty >= tex->h)
                continue;
            const uint8_t *t = &tex->rgba[(size_t(ty) * size_t(tex->w) + size_t(tx)) * 4];
            if (t[3] < kAlphaKept)
                continue;
            uint8_t *p = screen.row(py) + size_t(px) * 4;
            for (size_t k = 0; k < 3; ++k)
                p[k] = uint8_t(t[k] * tint[k] / 255);
            p[3] = 255;
        }
}

void Hud::drawPanel(Surface &screen, const std::string &name) {
    const std::array<int, 4> area = _layout.area(name + "PanelArea");
    const std::string background = _layout.text(name + "BackgroundPanel");
    const int pieces = int(_layout.number(name + "BackgroundPanelSize"));
    for (int i = 0; i < pieces; ++i) {
        const std::array<int, 4> piece = _layout.area(name + "BackgroundPanel_" + std::to_string(i));
        drawSprite(screen, background + "." + std::to_string(i), area[0] + piece[0], area[1] + piece[1], piece[2],
                   piece[3]);
    }
    // What a panel shows is drawn on black where there's nothing yet.
    if (_layout.find(name + "BlackArea"))
        fill(screen, _layout.area(name + "BlackArea"), area[0], area[1]);
}

void Hud::draw(Surface &screen, const Resources &resources, const std::vector<bool> &seen, bool counters) {
    for (const char *panel : panels())
        drawPanel(screen, panel);
    // The cinematic view: its picture until there's a view, and its two buttons.
    const std::array<int, 4> cinematic = _layout.area("cinematicPanelArea");
    const std::array<int, 4> picture = _layout.area("cinematicBackgroundArea");
    drawSprite(screen, _layout.text("cinematicDefaultBackground"), cinematic[0] + picture[0], cinematic[1] + picture[1],
               picture[2], picture[3]);
    for (const char *button : {"Comm", "Menu"}) {
        const std::array<int, 4> r = _layout.area(std::string("cinematic") + button + "ButtonArea");
        const int x = cinematic[0] + r[0];
        const int y = cinematic[1] + r[1];
        drawSprite(screen, _layout.text(std::string("cinematic") + button + "Button"), x, y, r[2], r[3], kButtonTint);
        drawSprite(screen, _layout.text(std::string("cinematic") + button + "Border"), x, y, r[2], r[3], kBorderTint);
    }
    // The counters: an icon, and the number centred in its place.
    for (int c = 0; c < 3; ++c) {
        const std::string name = kCounters[c];
        const std::array<int, 4> area = _layout.area(name + "PanelArea");
        const std::array<int, 4> icon = _layout.area(name + "IconArea");
        drawSprite(screen, _layout.text(name + "Icon"), area[0] + icon[0], area[1] + icon[1], icon[2], icon[3],
                   kIconTint);
        if (!counters)
            continue;
        const std::array<int, 4> words = _layout.area(name + "TextArea");
        const std::string number = counterText(resources, c);
        // Centred as if each character were six wide, as the frame's
        // "4/120" (its 1 four) and "3000" both are.
        const int width = int(number.size()) * kCounterCell;
        _font.draw(screen, area[0] + words[0] + (words[2] - width) / 2, area[1] + words[1], number, kCounterTint, true);
    }
    // The minimap: the map's picture where its cells are seen, the shroud's
    // where they aren't.
    const std::array<int, 4> minimap = _layout.area("minimapPanelArea");
    const std::array<int, 4> display = _layout.area("minimapDisplayArea");
    const int mx = minimap[0] + display[0];
    const int my = minimap[1] + display[1];
    drawSprite(screen, _layout.text("minimapBackgroundSprite"), mx, my, display[2], display[3]);
    if (seen.size() == size_t(kMinimapCells * kMinimapCells)) {
        Surface shroud;
        shroud.create(display[2], display[3], PixelFormat::kRGBA32);
        drawSprite(shroud, _layout.text("minimapShroudSprite"), 0, 0, display[2], display[3]);
        for (int y = 0; y < display[3] && my + y < screen.h; ++y)
            for (int x = 0; x < display[2] && mx + x < screen.w; ++x) {
                const int cell = (y * kMinimapCells / display[3]) * kMinimapCells + x * kMinimapCells / display[2];
                if (!seen[size_t(cell)])
                    std::copy_n(shroud.row(y) + size_t(x) * 4, 4, screen.row(my + y) + size_t(mx + x) * 4);
            }
    }
}

} // namespace reluva::storm3d
