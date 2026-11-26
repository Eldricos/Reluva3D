// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/play/hud.h"

#include "common/text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>

namespace reluva::storm3d {

namespace {

// The game's draws: the icons in (229, 229, 229), the counters' digits in
// the sprite font in (128, 128, 128), the cinematic view's buttons and
// their borders in their own greys.
constexpr std::array<uint8_t, 3> kIconTint = {229, 229, 229};
constexpr std::array<uint8_t, 3> kCounterTint = {128, 128, 128};
constexpr std::array<uint8_t, 3> kButtonTint = {153, 178, 171};
constexpr std::array<uint8_t, 3> kBorderTint = {128, 152, 149};
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
// An item being built: its button in orange.
constexpr std::array<uint8_t, 3> kActiveIcon = {214, 174, 131};
constexpr std::array<uint8_t, 3> kActiveBorder = {254, 235, 142};
// The wireframe's squares, in its texture's texels at 128, as drawn.
constexpr int kWireframeTile = 40;
constexpr std::pair<int, int> kWireframeTiles[] = {{40, 0}, {0, 40}, {40, 40}, {80, 0}, {0, 0}};
// Places for special weapons on the rail before its separator.
constexpr size_t kRailSlots = 5;
// The health bar over a unit: 4 high, dark grey under green (the draws').
constexpr int kHealthBarHeight = 4;
constexpr std::array<uint8_t, 3> kHealthBack = {51, 51, 51};
constexpr std::array<uint8_t, 3> kHealthFront = {0, 128, 0};
// The minimap's view lines' grey, added on.
constexpr int kViewLine = 128;
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

void Hud::drawBox(Surface &screen, int x0, int y0, int x1, int y1) {
    // From a pixel up and left of its top left to its bottom right: so the
    // original's fell, within a pixel, its pointer between pixels.
    const int left = std::min(x0, x1) - 1;
    const int right = std::max(x0, x1);
    const int top = std::min(y0, y1) - 1;
    const int bottom = std::max(y0, y1);
    auto plot = [&screen](int x, int y) {
        if (x < 0 || y < 0 || x >= screen.w || y >= screen.h)
            return;
        uint8_t *p = screen.row(y) + size_t(x) * 4;
        p[0] = p[1] = p[2] = p[3] = 255;
    };
    for (int x = left; x <= right; ++x) {
        plot(x, top);
        plot(x, bottom);
    }
    for (int y = top; y <= bottom; ++y) {
        plot(left, y);
        plot(right, y);
    }
}

int Hud::controlAt(int x, int y) const {
    const std::array<int, 4> panel = _layout.area("controlPanelArea");
    for (int slot = 1; slot <= 12; ++slot) {
        const std::string name = "controlButton" + std::to_string(slot);
        if (!_layout.find(name))
            continue;
        const std::array<int, 4> a = _layout.area(name);
        if (x >= panel[0] + a[0] && x < panel[0] + a[0] + a[2] && y >= panel[1] + a[1] && y < panel[1] + a[1] + a[3])
            return slot;
    }
    return 0;
}

std::array<int, 4> Hud::cinematicArea() const {
    const std::array<int, 4> panel = _layout.area("cinematicPanelArea");
    std::array<int, 4> display = _layout.area("cinematicDisplayArea");
    display[0] += panel[0];
    display[1] += panel[1];
    return display;
}

bool Hud::hasSprite(const std::string &name) const {
    return sprite(name) != nullptr;
}

std::string Hud::word(const std::string &key, const std::string &fallback) const {
    auto it = _words.find(key);
    return it == _words.end() ? fallback : it->second;
}

void Hud::drawSelection(Surface &screen, const std::vector<Unit> &units, const Unit &orders) {
    if (units.empty())
        return;
    if (units.size() == 1)
        drawInfo(screen, units.front());
    else
        drawGroup(screen, units);
    drawOrders(screen, orders);
}

void Hud::drawWireframe(Surface &screen, const std::string &name, const std::array<int, 4> &area) {
    // Five 40-texel squares of its 128 texture, over each other, in the
    // draws' order.
    const image::Image *wire = texture(name);
    if (!wire)
        return;
    const double scale = wire->w / 128.0;
    for (const auto &[tu, tv] : kWireframeTiles)
        drawTexels(screen, *wire, tu * scale, tv * scale, kWireframeTile * scale, kWireframeTile * scale, area[0],
                   area[1], area[2], area[3], {255, 255, 255}, false);
}

void Hud::drawGroup(Surface &screen, const std::vector<Unit> &units) {
    // A place each, from the info panel's corner, for as many as it has:
    // the crew dot, the shield bar over its dark grey, the wireframe.
    const std::array<int, 4> info = _layout.area("infoPanelArea");
    const std::array<int, 4> dot = _layout.area("infoMultiCrewDotArea");
    const std::array<int, 4> bar = _layout.area("infoMultiShieldBarArea");
    const std::array<int, 4> wire = _layout.area("infoMultiWireframeIconArea");
    const std::string barSprite = _layout.text("infoMultiShieldBar");
    for (size_t i = 0; i < units.size(); ++i) {
        const std::string place = "infoMultiShipIcon_" + std::to_string(i);
        if (!_layout.find(place))
            break;
        const std::array<int, 4> icon = _layout.area(place);
        const int x = info[0] + icon[0];
        const int y = info[1] + icon[1];
        drawSprite(screen, _layout.text("infoMultiCrewDot"), x + dot[0], y + dot[1], dot[2], dot[3], kGreen);
        drawSprite(screen, barSprite, x + bar[0], y + bar[1], bar[2], bar[3], kDisabled);
        drawSprite(screen, barSprite, x + bar[0], y + bar[1], bar[2], bar[3], kGreen, false, units[i].shields);
        drawWireframe(screen, units[i].wireframe, {x + wire[0], y + wire[1], wire[2], wire[3]});
    }
}

void Hud::drawInfo(Surface &screen, const Unit &unit) {
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
    drawWireframe(screen, unit.wireframe, at("infoSingleWireframeIconArea"));
    // The systems: each icon added on in dark grey, then in green if the
    // unit has it.
    for (size_t i = 0; i < unit.systems.size(); ++i) {
        const std::array<int, 4> a = at("infoSystem_" + std::to_string(i));
        const std::string icon = _layout.text("infoSystem") + "." + std::to_string(i);
        drawSprite(screen, icon, a[0], a[1], a[2], a[3], kSystemOff, true);
        if (unit.systems[i])
            drawSprite(screen, icon, a[0], a[1], a[2], a[3], kGreen, true);
    }
}

void Hud::drawOrders(Surface &screen, const Unit &unit) {
    // Its orders' buttons: the icon, then its border added on, in their
    // greys, or both near black when the unit can't.
    const std::array<int, 4> control = _layout.area("controlPanelArea");
    for (const Unit::Button &b : unit.controls) {
        const std::array<int, 4> a = _layout.area("controlButton" + std::to_string(b.slot));
        const auto &icon = b.active ? kActiveIcon : b.enabled ? kButtonTint : kOff;
        const auto &edge = b.active ? kActiveBorder : b.enabled ? kBorderTint : kOff;
        drawSprite(screen, b.sprite, control[0] + a[0], control[1] + a[1], a[2], a[3], icon);
        drawSprite(screen, b.border, control[0] + a[0], control[1] + a[1], a[2], a[3], edge, true);
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
    // Then what it's building, the first with its progress wiped round
    // clockwise from twelve o'clock in orange, the icon drawn again.
    for (size_t i = 0; i < unit.queue.size() && unit.specials.size() + i < kRailSlots; ++i) {
        const int x = rail[0] + slot[0] + int(unit.specials.size() + i) * step;
        const int y = rail[1] + slot[1];
        drawSprite(screen, unit.queue[i], x, y, slot[2], slot[3], kButtonTint);
        if (i == 0) {
            Surface wipe;
            wipe.create(slot[2], slot[3], PixelFormat::kRGBA32);
            drawSprite(wipe, unit.queue[i], 0, 0, slot[2], slot[3], kConstruction);
            const double reach = std::clamp(double(unit.progress), 0.0, 1.0) * 2 * std::numbers::pi;
            for (int py = 0; py < slot[3]; ++py)
                for (int px = 0; px < slot[2]; ++px) {
                    // Clockwise from up, about the icon's middle.
                    double angle = std::atan2(px + 0.5 - slot[2] / 2.0, slot[3] / 2.0 - (py + 0.5));
                    if (angle < 0)
                        angle += 2 * std::numbers::pi;
                    const uint8_t *from = wipe.row(py) + size_t(px) * 4;
                    if (angle <= reach && from[3] && x + px < screen.w && y + py < screen.h)
                        std::copy_n(from, 4, screen.row(y + py) + size_t(x + px) * 4);
                }
        }
        drawSprite(screen, "bb_build", x, y, slot[2], slot[3], kBorderTint, true);
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

void Hud::drawCursor(Surface &screen, int x, int y, double seconds, const std::string &name) {
    const SpriteTable::Sprite *s = sprite(name);
    const image::Image *tex = s ? texture(s->texture) : nullptr;
    if (!tex)
        return;
    // Its animation, cursor<n>x1 (tex_anim.spr): n frames along a row, a
    // second for them all, stepped; none, one frame.
    int frames = 1;
    for (const std::string &anim : s->animations)
        if (anim.rfind("cursor", 0) == 0)
            frames = std::max(1, std::atoi(anim.c_str() + 6));
    const int frame = int(seconds * frames) % frames;
    // Its origin, the pointer's place in it: "(16,16)" for those centred on it.
    if (auto origin = s->parameters.find("origin"); origin != s->parameters.end()) {
        int ox = 0;
        int oy = 0;
        if (std::sscanf(origin->second.c_str(), "(%d,%d)", &ox, &oy) == 2) {
            x -= ox;
            y -= oy;
        }
    }
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

void Hud::draw(Surface &screen, const Resources &resources, const std::vector<bool> &seen, bool counters,
               const Minimap &map) {
    for (const char *panel : panels())
        drawPanel(screen, panel);
    // The cinematic view: its picture until there's a view, and its two
    // buttons, their words black (the program's own, not the strings
    // file's).
    const std::array<int, 4> cinematic = _layout.area("cinematicPanelArea");
    const std::array<int, 4> picture = _layout.area("cinematicBackgroundArea");
    if (!_cinematicShown)
        drawSprite(screen, _layout.text("cinematicDefaultBackground"), cinematic[0] + picture[0],
                   cinematic[1] + picture[1], picture[2], picture[3]);
    for (const char *button : {"Comm", "Menu"}) {
        const std::array<int, 4> r = _layout.area(std::string("cinematic") + button + "ButtonArea");
        const int x = cinematic[0] + r[0];
        const int y = cinematic[1] + r[1];
        drawSprite(screen, _layout.text(std::string("cinematic") + button + "Button"), x, y, r[2], r[3], kButtonTint);
        drawSprite(screen, _layout.text(std::string("cinematic") + button + "Border"), x, y, r[2], r[3], kBorderTint);
        drawText(screen, {x, y, r[2], r[3]}, button[0] == 'C' ? "COMM" : "MENU", kBlack);
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
    // Where a point of the plane falls on it: the map's area across it,
    // its far edge at the top.
    const float across = map.right - map.left;
    const float down = map.far - map.near;
    auto onMinimap = [&](float x, float z) {
        return std::array<float, 2>{float(mx) + (x - map.left) * float(display[2]) / across,
                                    float(my) + (map.far - z) * float(display[3]) / down};
    };
    auto plot = [&](int x, int y, const std::array<uint8_t, 3> &colour) {
        if (x < mx || y < my || x >= mx + display[2] || y >= my + display[3] || x >= screen.w || y >= screen.h)
            return;
        uint8_t *p = screen.row(y) + size_t(x) * 4;
        std::copy_n(colour.data(), 3, p);
        p[3] = 255;
    };
    // The units, under the shroud: a ship the pixel it's in and those up
    // and left of it, 2 x 2; a station 3 x 3 about its own, as the
    // original's construction ships and starbase were.
    if (across > 0 && down > 0)
        for (const Minimap::Mark &m : map.marks) {
            const auto at = onMinimap(m.x, m.z);
            const int px = int(std::floor(at[0]));
            const int py = int(std::floor(at[1]));
            const int last = m.station ? 1 : 0;
            for (int dy = -1; dy <= last; ++dy)
                for (int dx = -1; dx <= last; ++dx)
                    plot(px + dx, py + dy, m.colour);
        }
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
    // The view: lines between where the screen's corners fall, added on in
    // mid grey.
    if (across > 0 && down > 0 && map.view.size() >= 2)
        for (size_t i = 0; i < map.view.size(); ++i) {
            const auto a = onMinimap(map.view[i][0], map.view[i][1]);
            const auto b = onMinimap(map.view[(i + 1) % map.view.size()][0], map.view[(i + 1) % map.view.size()][1]);
            const int steps = std::max(1, int(std::ceil(std::max(std::abs(b[0] - a[0]), std::abs(b[1] - a[1])))));
            for (int s = 0; s <= steps; ++s) {
                const float t = float(s) / float(steps);
                const int x = int(std::floor(a[0] + (b[0] - a[0]) * t + 0.5f));
                const int y = int(std::floor(a[1] + (b[1] - a[1]) * t + 0.5f));
                if (x < mx || y < my || x >= mx + display[2] || y >= my + display[3])
                    continue;
                uint8_t *p = screen.row(y) + size_t(x) * 4;
                for (int k = 0; k < 3; ++k)
                    p[k] = uint8_t(std::min(255, p[k] + kViewLine));
                p[3] = 255;
            }
        }
}

} // namespace reluva::storm3d
