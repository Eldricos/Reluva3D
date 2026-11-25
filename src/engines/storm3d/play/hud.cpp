// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/play/hud.h"

#include "common/text.h"

#include <algorithm>
#include <cmath>

namespace reluva::storm3d {

namespace {

// The game's draws: the icons in (229, 229, 229), the counters' digits in
// the sprite font in (128, 128, 128), the cinematic view's buttons and
// their borders in their own greys.
constexpr std::array<uint8_t, 3> kIconTint = {229, 229, 229};
constexpr std::array<uint8_t, 3> kCounterTint = {128, 128, 128};
constexpr std::array<uint8_t, 3> kButtonTint = {153, 178, 171};
constexpr std::array<uint8_t, 3> kBorderTint = {128, 152, 149};
constexpr int kCursorFrames = 4;
// The ship's display's colours, as its draws have them: black words,
// green crew and full bars, dark grey under them (the layout's disable
// colour, 0.3), the construction bar's orange (its 1, 0.6, 0.2), the
// systems' icons dark under green, an order the unit can't near black.
constexpr std::array<uint8_t, 3> kBlack = {0, 0, 0};
constexpr std::array<uint8_t, 3> kGreen = {0, 255, 0};
constexpr std::array<uint8_t, 3> kDisabled = {77, 77, 77};
constexpr std::array<uint8_t, 3> kConstruction = {255, 153, 51};
constexpr std::array<uint8_t, 3> kSystemOff = {51, 51, 51};
constexpr std::array<uint8_t, 3> kOff = {26, 31, 28};
// The wireframe's squares, in its texture's texels at 128, as drawn.
constexpr int kWireframeTile = 40;
constexpr std::pair<int, int> kWireframeTiles[] = {{40, 0}, {0, 40}, {40, 40}, {80, 0}, {0, 0}};
// Places for special weapons on the rail before its separator.
constexpr size_t kRailSlots = 5;
// The health bar over a unit: 4 high, dark grey under green (the draws').
constexpr int kHealthBarHeight = 4;
constexpr std::array<uint8_t, 3> kHealthBack = {51, 51, 51};
constexpr std::array<uint8_t, 3> kHealthFront = {0, 128, 0};
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
    for (const std::string &name : {table == kTables.end() ? std::string("gui_federation.spr") : table->second,
                                    std::string("gui_global.spr"), std::string("cursor.spr")})
        if (Error err = _tables.emplace_back().load(name, loader))
            return err;
    if (_font.load(files))
        _font = SpriteFont{};
    // The interface's words by their keys: pairs of quoted strings.
    _words.clear();
    std::vector<uint8_t> strings;
    if (!files.read("Dynamic_Localized_Strings.h", strings)) {
        std::vector<std::string> quoted;
        const std::string text(strings.begin(), strings.end());
        for (size_t i = text.find('"'); i != std::string::npos; i = text.find('"', i)) {
            const size_t end = text.find('"', i + 1);
            if (end == std::string::npos)
                break;
            quoted.push_back(text.substr(i + 1, end - i - 1));
            i = end + 1;
        }
        for (size_t i = 0; i + 1 < quoted.size(); i += 2)
            _words[quoted[i]] = quoted[i + 1];
    }
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
                     const std::array<uint8_t, 3> &tint, bool additive, float part) {
    const SpriteTable::Sprite *sprite = this->sprite(name);
    const image::Image *tex = sprite ? texture(sprite->texture) : nullptr;
    if (!tex)
        return;
    // The sprite's place measured against its reference size; the
    // texture may be another size.
    const double sx = sprite->referenceWidth > 0 ? double(tex->w) / sprite->referenceWidth : 1;
    const double sy = sprite->referenceHeight > 0 ? double(tex->h) / sprite->referenceHeight : 1;
    // A bar part full is the sprite cut short, its texture with it.
    const int shown = int(std::lround(w * std::clamp(part, 0.0f, 1.0f)));
    drawTexels(screen, *tex, sprite->u * sx, sprite->v * sy, sprite->w * sx * shown / std::max(w, 1), sprite->h * sy, x,
               y, shown, h, tint, additive);
}

void Hud::drawTexels(Surface &screen, const image::Image &tex, double u, double v, double tw, double th, int x, int y,
                     int w, int h, const std::array<uint8_t, 3> &tint, bool additive) {
    if (w <= 0 || h <= 0)
        return;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const int px = x + i;
            const int py = y + j;
            if (px < 0 || py < 0 || px >= screen.w || py >= screen.h)
                continue;
            // A point a pixel, at the pixel's left and top edges: where
            // Direct3D's pixel centres fall on a quad on whole pixels.
            const int tx = int(u + tw * i / w);
            const int ty = int(v + th * j / h);
            if (tx < 0 || ty < 0 || tx >= tex.w || ty >= tex.h)
                continue;
            const uint8_t *t = &tex.rgba[(size_t(ty) * size_t(tex.w) + size_t(tx)) * 4];
            uint8_t *p = screen.row(py) + size_t(px) * 4;
            if (additive) {
                for (size_t k = 0; k < 3; ++k)
                    p[k] = uint8_t(std::min(255, p[k] + t[k] * tint[k] / 255));
                p[3] = 255;
                continue;
            }
            if (t[3] < kAlphaKept)
                continue;
            for (size_t k = 0; k < 3; ++k)
                p[k] = uint8_t(t[k] * tint[k] / 255);
            p[3] = 255;
        }
}

void Hud::drawText(Surface &screen, const std::array<int, 4> &area, const std::string &text,
                   const std::array<uint8_t, 3> &tint) {
    // Centred, the text counted a pixel wider and taller than its letters
    // (their widths and the pixel after each, and their 10 rows), halves
    // rounded towards zero: every line of the game's frames falls so, from
    // "4/120" at 591 to "Construction Ship" at 261.
    const int x = area[0] + (area[2] - (_font.width(text) + 1)) / 2;
    const int y = area[1] + (area[3] - (SpriteFont::kHeight + 1)) / 2;
    _font.draw(screen, x, y, text, tint, true);
}

bool Hud::hasSprite(const std::string &name) const {
    return sprite(name) != nullptr;
}

std::string Hud::word(const std::string &key, const std::string &fallback) const {
    auto it = _words.find(key);
    return it == _words.end() ? fallback : it->second;
}

void Hud::drawUnit(Surface &screen, const Unit &unit) {
    const std::array<int, 4> info = _layout.area("infoPanelArea");
    auto at = [this, &info](const std::string &name) {
        std::array<int, 4> a = _layout.area(name);
        a[0] += info[0];
        a[1] += info[1];
        return a;
    };
    auto sprite = [this, &at, &screen](const std::string &spriteName, const std::string &areaName,
                                       const std::array<uint8_t, 3> &tint = {255, 255, 255}, bool additive = false,
                                       float part = 1) {
        const std::array<int, 4> a = at(areaName);
        drawSprite(screen, spriteName, a[0], a[1], a[2], a[3], tint, additive, part);
    };
    const std::string background = _layout.text("infoSingleBackground");
    for (int i = 0; i < int(_layout.number("infoSingleBackgroundSize")); ++i)
        sprite(background + "." + std::to_string(i), "infoSingleBackground_" + std::to_string(i));
    sprite(_layout.text("infoSingleRaceIcon"), "infoSingleRaceIconArea", kIconTint);
    sprite(unit.raceIcon, "infoSingleRaceIconDisplayArea", kIconTint);
    drawText(screen, at("infoSingleClassTextInformationArea"), unit.className, kBlack);
    sprite(_layout.text("infoSingleNameTextBackground"), "infoSingleNameTextBackgroundArea");
    drawText(screen, at("infoSingleNameTextInformationArea"), unit.name, kBlack);
    sprite(_layout.text("infoSingleCrewTextBackground"), "infoSingleCrewTextBackgroundArea");
    drawText(screen, at("infoSingleCrewTextLabelArea"), word(_layout.text("infoSingleCrewTextLabel"), "CREW"), kBlack);
    drawText(screen, at("infoSingleCrewTextInformationArea"),
             std::to_string(unit.crew) + "/" + std::to_string(unit.maxCrew), kGreen);
    // A unit that costs no officers has no line for them (the starbase).
    if (unit.officers > 0) {
        sprite(_layout.text("infoSingleOfficerTextBackground"), "infoSingleOfficerTextBackgroundArea");
        drawText(screen, at("infoSingleOfficerTextLabelArea"),
                 word(_layout.text("infoSingleOfficerTextLabel"), "OFFICERS"), kBlack);
        drawText(screen, at("infoSingleOfficerTextInformationArea"), std::to_string(unit.officers), kCounterTint);
    }
    sprite(_layout.text("infoSingleCrewDot"), "infoSingleCrewDotArea", kGreen);
    sprite(_layout.text("infoSingleShieldBar"), "infoSingleShieldBarArea", kDisabled);
    sprite(_layout.text("infoSingleShieldBar"), "infoSingleShieldBarArea", kGreen, false, unit.shields);
    sprite(_layout.text("infoSingleConstructionBar"), "infoSingleConstructionBarArea", kDisabled);
    sprite(_layout.text("infoSingleConstructionBar"), "infoSingleConstructionBarArea", kConstruction, false,
           unit.construction);
    // The wireframe: five 40-texel squares of its 128 texture, over each
    // other, in the draws' order.
    if (const image::Image *wire = texture(unit.wireframe)) {
        const std::array<int, 4> a = at("infoSingleWireframeIconArea");
        const double scale = wire->w / 128.0;
        for (const auto &[tu, tv] : kWireframeTiles)
            drawTexels(screen, *wire, tu * scale, tv * scale, kWireframeTile * scale, kWireframeTile * scale, a[0],
                       a[1], a[2], a[3], {255, 255, 255}, false);
    }
    // The systems: each icon added on in dark grey, then in green if the
    // unit has it.
    for (size_t i = 0; i < unit.systems.size(); ++i) {
        const std::array<int, 4> a = at("infoSystem_" + std::to_string(i));
        const std::string icon = _layout.text("infoSystem") + "." + std::to_string(i);
        drawSprite(screen, icon, a[0], a[1], a[2], a[3], kSystemOff, true);
        if (unit.systems[i])
            drawSprite(screen, icon, a[0], a[1], a[2], a[3], kGreen, true);
    }
    // Its orders' buttons: the icon, then its border added on, in their
    // greys, or both near black when the unit can't.
    const std::array<int, 4> control = _layout.area("controlPanelArea");
    for (const Unit::Button &b : unit.controls) {
        const std::array<int, 4> a = _layout.area("controlButton" + std::to_string(b.slot));
        drawSprite(screen, b.sprite, control[0] + a[0], control[1] + a[1], a[2], a[3], b.enabled ? kButtonTint : kOff);
        drawSprite(screen, b.border, control[0] + a[0], control[1] + a[1], a[2], a[3], b.enabled ? kBorderTint : kOff,
                   true);
    }
    // The rail over the display: the unit's special weapons, a button each
    // from the left, then past the five places for them the separator and
    // the transport button.
    const std::array<int, 4> rail = _layout.area("speedPanelArea");
    const std::array<int, 4> slot = _layout.area("speedSingleButtonArea");
    const int step = slot[2] + int(_layout.number("speedSingleButtonGap"));
    for (size_t i = 0; i < unit.specials.size() && i < kRailSlots; ++i) {
        const int x = rail[0] + slot[0] + int(i) * step;
        drawSprite(screen, unit.specials[i], x, rail[1] + slot[1], slot[2], slot[3], kButtonTint);
        drawSprite(screen, "bb_dweapon", x, rail[1] + slot[1], slot[2], slot[3], kBorderTint, true);
    }
    const std::array<int, 4> separator = _layout.area("speedSeparatorArea");
    const int sepX = rail[0] + slot[0] + int(kRailSlots) * step;
    drawSprite(screen, _layout.text("speedSeparator"), sepX, rail[1] + separator[1], separator[2], separator[3]);
    const int transportX = sepX + separator[2] + int(_layout.number("speedSingleButtonGap"));
    drawSprite(screen, "b_transport", transportX, rail[1] + slot[1], slot[2], slot[3], kButtonTint);
    drawSprite(screen, "bb_standard", transportX, rail[1] + slot[1], slot[2], slot[3], kBorderTint, true);
}

bool Hud::covers(int x, int y) const {
    for (const char *panel : panels()) {
        const std::array<int, 4> a = _layout.area(std::string(panel) + "PanelArea");
        if (x >= a[0] && y >= a[1] && x < a[0] + a[2] && y < a[1] + a[3])
            return true;
    }
    return false;
}

void Hud::drawHealthBar(Surface &screen, int left, int top, int width, float health) {
    drawSprite(screen, "large_shield_bar", left, top, width, kHealthBarHeight, kHealthBack);
    drawSprite(screen, "large_shield_bar", left, top, width, kHealthBarHeight, kHealthFront, false, health);
}

void Hud::drawCursor(Surface &screen, int x, int y, double seconds, bool overUnit) {
    const SpriteTable::Sprite *s = sprite(overUnit ? "c_select" : "c_arrow");
    const image::Image *tex = s ? texture(s->texture) : nullptr;
    if (!tex)
        return;
    // cursor4x1 (tex_anim.spr): four frames along a row, a second for the
    // four, stepped.
    const int frame = int(seconds * kCursorFrames) % kCursorFrames;
    const double sx = s->referenceWidth > 0 ? double(tex->w) / s->referenceWidth : 1;
    const double sy = s->referenceHeight > 0 ? double(tex->h) / s->referenceHeight : 1;
    const int u = int((s->u + s->w * frame) * sx);
    const int v = int(s->v * sy);
    const int w = int(s->w * sx);
    const int h = int(s->h * sy);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const int px = x + i;
            const int py = y + j;
            if (px < 0 || py < 0 || px >= screen.w || py >= screen.h || u + i >= tex->w || v + j >= tex->h)
                continue;
            const uint8_t *t = &tex->rgba[(size_t(v + j) * size_t(tex->w) + size_t(u + i)) * 4];
            // Black is its colour key (@skip=(0,0,0)).
            if (t[0] == 0 && t[1] == 0 && t[2] == 0)
                continue;
            uint8_t *p = screen.row(py) + size_t(px) * 4;
            std::copy_n(t, 3, p);
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
        drawText(screen, {area[0] + words[0], area[1] + words[1], words[2], words[3]}, number, kCounterTint);
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
