// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/game_hud.h"

#include "engines/stawayteam/ui/animation.h"
#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/layout.h"

#include <algorithm>
#include <cmath>

namespace reluva::stawayteam {

namespace {

// The button box's buttons: Rules.txt's key and the picture shown
// (Button Box/<name>), as they are before anything's chosen (inferred).
struct BoxButton {
    const char *key;
    const char *picture;
};
constexpr BoxButton kBoxButtons[] = {{"Move State Button", "Walk Button"},
                                     {"Vision State Button", "Vision Default"},
                                     {"Sound State Button", "Sound Default"},
                                     {"Select All Button", "Select All Default"},
                                     {"Objectives Button", "Objectives Button Default"},
                                     {"Options Button", "Escape Button Default"},
                                     {"CancelOrdersButton Pos", "Cancel Orders Default"}};

// An officer's name is in their field's colour (0x5e2d5d).
const char *fieldFont(const std::string &field) {
    if (field == "Command" || field == "Security")
        return "Trek Small Red";
    if (field == "Engineering")
        return "Trek Small Gold";
    return "Trek Small";
}

int number(const TextObject *o, const char *key, int fallback) {
    return o ? int(o->integer(key).value_or(fallback)) : fallback;
}

} // namespace

GameHud::GameHud(const Archive &data, const TextObject &rules, std::vector<TeamMember> team, const std::string &minimap,
                 int mapW, int mapH)
    : _data(data), _custom(rules.object("Custom Rules")), _team(std::move(team)), _mapW(mapW), _mapH(mapH) {
    for (const TeamMember &m : _team) {
        _heads.push_back(cachePicture(data, "Character/Character Icons/" + m.headIcon));
        if (!_fieldFonts.count(m.field))
            (void)_fieldFonts[m.field].load(data, fieldFont(m.field));
    }
    _items = loadItems(data);
    _minimap = cachePicture(data, "Levels/" + minimap);
    (void)_font.load(data, "Trek Small");
}

const image::Image &GameHud::picture(const std::string &name) {
    auto it = _pictures.find(name);
    if (it == _pictures.end())
        it = _pictures.emplace(name, screenPicture(_data, name)).first;
    return it->second;
}

int GameHud::infoTop(uint64_t now) {
    // It slides over Info Bar Popup Time, its Info Bar Visible rows showing
    // while it's down (0x4de991); it rises while the mouse is over it
    // (inferred).
    const double popup = _custom ? _custom->number("Info Bar Popup Time").value_or(0.25) : 0.25;
    const float step = popup > 0.0 ? float(double(now - std::min(now, _last)) / 1000.0 / popup) : 1.0f;
    _last = now;
    _open = std::clamp(_open + (_opening ? step : -step), 0.0f, 1.0f);
    int x = 0;
    int y = 0;
    readPoint(_custom, "Info Pos", x, y);
    const int visible = number(_custom, "Info Bar Visible", 60);
    const int height = picture("Info Bar/Background").h;
    return y - visible - int(std::lround(_open * float(std::max(0, height - visible))));
}

bool GameHud::overInfoBar(int x, int y, uint64_t now) {
    int ix = 0;
    int iy = 0;
    readPoint(_custom, "Info Pos", ix, iy);
    const image::Image &bar = picture("Info Bar/Background");
    const int top = infoTop(now);
    return x >= ix && x < ix + bar.w && y >= top && y < iy;
}

GameHud::Result GameHud::handleEvent(const Event &ev, uint64_t now) {
    Result r;
    if (ev.type != EventType::kMouseMove && ev.type != EventType::kMouseDown && ev.type != EventType::kMouseUp)
        return r;
    _mouseX = ev.mouseX;
    _mouseY = ev.mouseY;
    const bool overInfo = overInfoBar(ev.mouseX, ev.mouseY, now);
    _opening = overInfo;
    int tx = 0;
    int ty = 0;
    readPoint(_custom, "Team Pos", tx, ty);
    const image::Image &teamBar = picture("Team Bar/Background");
    const bool overTeam =
        ev.mouseX >= tx && ev.mouseX < tx + teamBar.w && ev.mouseY >= ty && ev.mouseY < ty + teamBar.h;
    int bx = 0;
    int by = 0;
    readPoint(_custom, "Button Box Pos", bx, by);
    const image::Image &box = picture("Button Box/Background");
    const bool overBox = ev.mouseX >= bx && ev.mouseX < bx + box.w && ev.mouseY >= by && ev.mouseY < by + box.h;
    int vx = 0;
    int vy = 0;
    readPoint(_custom, "Inventory Pos", vx, vy);
    const image::Image &inv = picture("Inventory/Background");
    const bool overInventory = ev.mouseX >= vx && ev.mouseX < vx + inv.w && ev.mouseY >= vy && ev.mouseY < vy + inv.h;
    r.used = overInfo || overTeam || overBox || overInventory;
    if (ev.type != EventType::kMouseDown || ev.button != MouseButton::kLeft)
        return r;
    if (overBox) {
        int mx = 0;
        int my = 0;
        readPoint(_custom, "Move State Button", mx, my);
        const image::Image &move = picture("Button Box/Walk Button");
        r.cycleMove = ev.mouseX >= bx + mx && ev.mouseY >= by + my && ev.mouseX < bx + mx + move.w &&
                      ev.mouseY < by + my + move.h;
    }
    if (overTeam) {
        // An officer's slot chooses them (inferred).
        int ix = 0;
        int iy = 0;
        readPoint(_custom, "Team Icon Pos", ix, iy);
        const int spacing = number(_custom, "Team Icon Spacing", 66);
        const int i = spacing > 0 ? (ev.mouseY - ty) / spacing : -1;
        if (i >= 0 && i < int(_team.size()))
            r.select = i;
    }
    if (overInfo && _minimap.w && _mapW > 0 && _mapH > 0) {
        // A click on the minimap moves the view there (inferred).
        int mx = 0;
        int my = 0;
        int cx = 0;
        int cy = 0;
        readPoint(_custom, "Info MiniMap Pos", mx, my);
        readPoint(_custom, "Info MiniMap Map Center Pos", cx, cy);
        int ix = 0;
        int iy = 0;
        readPoint(_custom, "Info Pos", ix, iy);
        const int left = ix + mx + cx - _minimap.w / 2;
        const int top = infoTop(now) + my + cy - _minimap.h / 2;
        if (ev.mouseX >= left && ev.mouseY >= top && ev.mouseX < left + _minimap.w && ev.mouseY < top + _minimap.h) {
            r.moveCamera = true;
            r.cameraX = float(ev.mouseX - left) * float(_mapW) / float(_minimap.w);
            r.cameraY = float(ev.mouseY - top) * float(_mapH) / float(_minimap.h);
        }
    }
    return r;
}

void GameHud::draw(Surface &screen, uint64_t now, int selected, float cameraX, float cameraY, int viewW, int viewH) {
    // The info bar, under the rest: from under the screen's bottom.
    int ix = 0;
    int iy = 0;
    readPoint(_custom, "Info Pos", ix, iy);
    const int infoY = infoTop(now);
    image::blit(screen, picture("Info Bar/Background"), ix, infoY);
    int ox = 0;
    int oy = 0;
    readPoint(_custom, "Info Open Pos", ox, oy);
    image::blit(screen, picture("Info Bar/Open Button Default"), ix + ox, infoY + oy);
    int mx = 0;
    int my = 0;
    int cx = 0;
    int cy = 0;
    readPoint(_custom, "Info MiniMap Pos", mx, my);
    readPoint(_custom, "Info MiniMap Map Center Pos", cx, cy);
    image::blit(screen, picture("Info Bar/MiniMap Background"), ix + mx, infoY + my);
    if (_minimap.w && _mapW > 0 && _mapH > 0) {
        const int left = ix + mx + cx - _minimap.w / 2;
        const int top = infoY + my + cy - _minimap.h / 2;
        image::blit(screen, _minimap, left, top);
        // The view's place on it, outlined (inferred).
        const float sx = float(_minimap.w) / float(_mapW);
        const float sy = float(_minimap.h) / float(_mapH);
        const int x0 = left + int((cameraX - float(viewW) / 2) * sx);
        const int y0 = top + int((cameraY - float(viewH) / 2) * sy);
        const int x1 = x0 + int(float(viewW) * sx);
        const int y1 = y0 + int(float(viewH) * sy);
        const auto dot = [&](int x, int y) {
            if (x >= 0 && y >= 0 && x < screen.w && y < screen.h) {
                uint8_t *p = screen.row(y) + x * 4;
                p[0] = p[1] = p[2] = 255;
            }
        };
        for (int x = x0; x <= x1; ++x) {
            dot(x, y0);
            dot(x, y1);
        }
        for (int y = y0; y <= y1; ++y) {
            dot(x0, y);
            dot(x1, y);
        }
    }

    // The team bar: each officer's slot, name in their field's colour, head
    // in the middle of it and health bar, filled from the bottom
    // (0x5e3290).
    int tx = 0;
    int ty = 0;
    readPoint(_custom, "Team Pos", tx, ty);
    image::blit(screen, picture("Team Bar/Background"), tx, ty);
    int iconX = 0;
    int iconY = 0;
    int offX = 0;
    int offY = 0;
    int hoverX = 0;
    int hoverY = 0;
    int fieldX = 0;
    int fieldY = 0;
    int healthX = 0;
    int healthY = 0;
    readPoint(_custom, "Team Icon Pos", iconX, iconY);
    readPoint(_custom, "Team Icon Offset", offX, offY);
    readPoint(_custom, "Team Hover Pos", hoverX, hoverY);
    readPoint(_custom, "Team Field Offset", fieldX, fieldY);
    readPoint(_custom, "Team Health Offset", healthX, healthY);
    const int spacing = number(_custom, "Team Icon Spacing", 66);
    for (size_t i = 0; i < _team.size(); ++i) {
        const int x = tx + iconX;
        const int y = ty + iconY + int(i) * spacing;
        image::blit(screen, picture("Team Bar/Member Background"), x - iconX, y - iconY);
        if (int(i) == selected) {
            // The chosen officer's slot lit (inferred).
            const image::Image &lit = picture("Team Bar/Hover Background");
            image::blit(screen, lit, x + hoverX - lit.w / 2, y + hoverY - lit.h / 2);
        }
        const image::Image &head = _heads[i];
        image::blit(screen, head, x + offX - head.w / 2, y + offY - head.h / 2);
        const auto font = _fieldFonts.find(_team[i].field);
        if (font != _fieldFonts.end() && font->second.loaded())
            font->second.draw(screen, x + fieldX, y + fieldY, _team[i].name());
        // Health: whole, as nothing's hurt anyone yet.
        image::blit(screen, picture("Team Bar/Health Bar Background"), x + healthX, y + healthY);
        image::blit(screen, picture("Team Bar/Health Bar Foreground"), x + healthX, y + healthY);
    }

    // The button box, its buttons as they start (0x476362, inferred for
    // which pictures).
    int bx = 0;
    int by = 0;
    readPoint(_custom, "Button Box Pos", bx, by);
    image::blit(screen, picture("Button Box/Background"), bx, by);
    for (const BoxButton &b : kBoxButtons) {
        int x = 0;
        int y = 0;
        readPoint(_custom, b.key, x, y);
        std::string name = b.picture;
        // The move button shows how the chosen officer moves.
        if (&b == &kBoxButtons[0])
            name = _move == 1 ? "Run Button" : _move == 2 ? "Crouch Button" : "Walk Button";
        image::blit(screen, picture("Button Box/" + name), bx + x, by + y);
    }
    int cancelX = 0;
    int cancelY = 0;
    int textX = 0;
    int textY = 0;
    readPoint(_custom, "CancelOrdersButton Pos", cancelX, cancelY);
    readPoint(_custom, "CancelOrdersButton Text Pos", textX, textY);
    if (_font.loaded()) {
        const std::string label = readText(_custom, "CancelOrdersButton Text");
        const int w = picture("Button Box/Cancel Orders Default").w;
        _font.draw(screen, bx + cancelX + (w - _font.width(label)) / 2 + textX, by + cancelY + textY, label);
    }

    // The inventory: the chosen officer's items along its bar (0x41aba7,
    // inferred for their order: as the roster lists them).
    int vx = 0;
    int vy = 0;
    int barX = 0;
    int barY = 0;
    int px = 0;
    int py = 0;
    int nx = 0;
    int ny = 0;
    int iw = 0;
    int ih = 0;
    readPoint(_custom, "Inventory Pos", vx, vy);
    readPoint(_custom, "Inventory Bar Offset", barX, barY);
    readPoint(_custom, "Inventory Previous Button Pos", px, py);
    readPoint(_custom, "Inventory Next Button Pos", nx, ny);
    readPoint(_custom, "Inventory Icon Size", iw, ih);
    const int gap = number(_custom, "Inventory Icon Spacing", 2);
    const image::Image &inv = picture("Inventory/Background");
    image::blit(screen, inv, vx, vy);
    image::blit(screen, picture("Inventory/Previous Inventory Item Default"), vx + px, vy + py);
    image::blit(screen, picture("Inventory/Next Inventory Item Default"), vx + nx, vy + ny);
    if (selected >= 0 && size_t(selected) < _team.size() && iw > 0) {
        int x = vx + barX;
        for (const TeamMember::Item &held : _team[size_t(selected)].inventory) {
            const auto def = _items.find(held.name);
            if (def == _items.end() || !def->second.displayInInventory)
                continue;
            if (x + iw > vx + nx)
                break;
            image::blit(screen, picture("Inventory/Item Frame"), x, vy + barY);
            auto found = _icons.find(def->second.iconImage);
            if (found == _icons.end())
                found = _icons
                            .emplace(def->second.iconImage,
                                     cachePicture(_data, "Inventory/Icon Frames/" + def->second.iconImage))
                            .first;
            const image::Image &icon = found->second;
            image::blit(screen, icon, x + (iw - icon.w) / 2, vy + barY + (ih - icon.h) / 2);
            x += iw + gap;
        }
    }

    // The pause button.
    int pauseX = 0;
    int pauseY = 0;
    readPoint(_custom, "Pause Pos", pauseX, pauseY);
    image::blit(screen, picture("Pause Time Button/Pause Time Default"), pauseX, pauseY);
}

} // namespace reluva::stawayteam
