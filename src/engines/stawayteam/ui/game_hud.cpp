// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/game_hud.h"

#include "engines/stawayteam/ui/animation.h"
#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/layout.h"

#include <algorithm>
#include <cstdio>
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
    _open = std::clamp(_open + (_opening || _pinned ? step : -step), 0.0f, 1.0f);
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
    int pauseX = 0;
    int pauseY = 0;
    readPoint(_custom, "Pause Pos", pauseX, pauseY);
    const image::Image &pause = picture("Pause Time Button/Pause Time Default");
    const bool overPause =
        ev.mouseX >= pauseX && ev.mouseY >= pauseY && ev.mouseX < pauseX + pause.w && ev.mouseY < pauseY + pause.h;
    r.used = overInfo || overTeam || overBox || overInventory || overPause;
    _mouseOver = r.used;
    r.pause = overPause && ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft;
    if (ev.type != EventType::kMouseDown || ev.button != MouseButton::kLeft)
        return r;
    if (overBox) {
        int mx = 0;
        int my = 0;
        readPoint(_custom, "Move State Button", mx, my);
        const image::Image &move = picture("Button Box/Walk Button");
        r.cycleMove = ev.mouseX >= bx + mx && ev.mouseY >= by + my && ev.mouseX < bx + mx + move.w &&
                      ev.mouseY < by + my + move.h;
        int sx = 0;
        int sy = 0;
        readPoint(_custom, "Select All Button", sx, sy);
        const image::Image &all = picture("Button Box/Select All Default");
        r.selectAll =
            ev.mouseX >= bx + sx && ev.mouseY >= by + sy && ev.mouseX < bx + sx + all.w && ev.mouseY < by + sy + all.h;
        int ox = 0;
        int oy = 0;
        readPoint(_custom, "Sound State Button", ox, oy);
        const image::Image &sound = picture("Button Box/Sound Default");
        r.toggleSound = ev.mouseX >= bx + ox && ev.mouseY >= by + oy && ev.mouseX < bx + ox + sound.w &&
                        ev.mouseY < by + oy + sound.h;
        const auto on = [&](const char *key, const char *name) {
            int x = 0;
            int y = 0;
            readPoint(_custom, key, x, y);
            const image::Image &p = picture(std::string("Button Box/") + name);
            return ev.mouseX >= bx + x && ev.mouseY >= by + y && ev.mouseX < bx + x + p.w && ev.mouseY < by + y + p.h;
        };
        r.objectives = on("Objectives Button", "Objectives Button Default");
        r.cancelOrders = on("CancelOrdersButton Pos", "Cancel Orders Default");
        r.vision = on("Vision State Button", "Vision Default");
        r.options = on("Options Button", "Escape Button Default");
    }
    // The chosen item's panel's buttons (inferred: as drawn).
    if (const ItemDefinition *item = chosenItem(); item && item->infoRenderer) {
        const TextObject *panel = item->infoRenderer.get();
        const auto onButton = [&](const char *key, const image::Image &p) {
            int x = 0;
            int y = 0;
            readPoint(panel, key, x, y);
            return ev.mouseX >= _barX + x && ev.mouseY >= _barY + y && ev.mouseX < _barX + x + p.w &&
                   ev.mouseY < _barY + y + p.h;
        };
        if (panel->className == "CPhaserInfoRenderer" && item->multipleSettings)
            r.toggleStun = onButton("Stun Pos", picture("Inventory/Toggle Default"));
        if (panel->className == "CChargedItemInfoRenderer" && panel->integer("Show Activate Button").value_or(0))
            r.toggleCharge = onButton("Button Pos", picture(readText(panel, "Button Off Frame")));
        if (panel->className == "CDetonateInfoRenderer")
            r.detonate = onButton("Detonate Button Pos", picture("Info Bar/Remote Mine/Detonate Default"));
    }
    if (overInventory) {
        // An item's icon chooses it, the Previous and Next buttons go along
        // them as Inventory Prev and Next (inferred).
        int iw = 0;
        int ih = 0;
        readPoint(_custom, "Inventory Icon Size", iw, ih);
        for (const Slot &s : _slots)
            if (ev.mouseX >= s.x && ev.mouseY >= s.y && ev.mouseX < s.x + iw && ev.mouseY < s.y + ih)
                r.item = s.item;
        const auto on = [&](const char *key, const char *name) {
            int x = 0;
            int y = 0;
            readPoint(_custom, key, x, y);
            const image::Image &p = picture(std::string("Inventory/") + name);
            return ev.mouseX >= vx + x && ev.mouseY >= vy + y && ev.mouseX < vx + x + p.w && ev.mouseY < vy + y + p.h;
        };
        if (on("Inventory Previous Button Pos", "Previous Inventory Item Default"))
            r.step = -1;
        if (on("Inventory Next Button Pos", "Next Inventory Item Default"))
            r.step = 1;
    }
    if (overInfo) {
        // Its open button holds it up, or lets it down (inferred: as
        // Toggle Minimap presses it).
        int ix = 0;
        int iy = 0;
        int ox = 0;
        int oy = 0;
        readPoint(_custom, "Info Pos", ix, iy);
        readPoint(_custom, "Info Open Pos", ox, oy);
        const image::Image &open = picture("Info Bar/Open Button Default");
        const int x = ix + ox;
        const int y = infoTop(now) + oy;
        if (ev.mouseX >= x && ev.mouseY >= y && ev.mouseX < x + open.w && ev.mouseY < y + open.h)
            _pinned = !_pinned;
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
    image::blit(screen, picture(_pinned ? "Info Bar/Open Button Press" : "Info Bar/Open Button Default"), ix + ox,
                infoY + oy);
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
        drawMarks(screen, left, top, now);
    }
    // The chosen item's panel at Info Renderer Pos on it, over the minimap
    // (inferred).
    int rx = 0;
    int ry = 0;
    readPoint(_custom, "Info Renderer Pos", rx, ry);
    _barX = ix;
    _barY = infoY;
    drawItemPanel(screen, ix + rx, infoY + ry, now);

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
        if (int(i) == selected || (i < _chosen.size() && _chosen[i])) {
            // The chosen officer's slot lit (inferred).
            const image::Image &lit = picture("Team Bar/Hover Background");
            image::blit(screen, lit, x + hoverX - lit.w / 2, y + hoverY - lit.h / 2);
        }
        const image::Image &head = _heads[i];
        image::blit(screen, head, x + offX - head.w / 2, y + offY - head.h / 2);
        const auto font = _fieldFonts.find(_team[i].field);
        if (font != _fieldFonts.end() && font->second.loaded())
            font->second.draw(screen, x + fieldX, y + fieldY, _team[i].name());
        // Health: the bar's foreground shown as far along as they're whole,
        // from its bottom if it stands, else from its left (inferred).
        image::blit(screen, picture("Team Bar/Health Bar Background"), x + healthX, y + healthY);
        const image::Image &bar = picture("Team Bar/Health Bar Foreground");
        const float whole = i < _health.size() ? std::clamp(_health[i], 0.0f, 1.0f) : 1.0f;
        if (bar.h > bar.w) {
            const int shown = int(std::lround(float(bar.h) * whole));
            image::Image part;
            part.w = bar.w;
            part.h = shown;
            part.rgba.assign(bar.rgba.end() - ptrdiff_t(size_t(shown) * size_t(bar.w) * 4), bar.rgba.end());
            image::blit(screen, part, x + healthX, y + healthY + bar.h - shown);
        } else {
            const int shown = int(std::lround(float(bar.w) * whole));
            image::Image part;
            part.w = shown;
            part.h = bar.h;
            for (int row = 0; row < bar.h; ++row) {
                const auto from = bar.rgba.begin() + ptrdiff_t(size_t(row) * size_t(bar.w) * 4);
                part.rgba.insert(part.rgba.end(), from, from + ptrdiff_t(size_t(shown) * 4));
            }
            image::blit(screen, part, x + healthX, y + healthY);
        }
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
        // The sound button held down while the noises show (inferred); the
        // vision button by its state (0x4765c0).
        if (&b == &kBoxButtons[2] && _listening)
            name = "Sound Press";
        if (&b == &kBoxButtons[1])
            name = _vision == 1 ? "Vision Select" : _vision == 2 ? "Vision Press" : "Vision Default";
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
    // A place for each item type (0x423f70): an empty one darkened (10,
    // 10, 40, half see-through), else its item in its frame, the chosen
    // one on its Selected Icon Background (0x41ae4b), its uses left in its
    // corner and the More Indicator on a place with more than one
    // (inferred for where).
    _slots.clear();
    if (selected >= 0 && size_t(selected) < _team.size() && iw > 0) {
        int x = vx + barX;
        for (const Place &place : _places) {
            if (x + iw > vx + nx)
                break;
            const int y = vy + barY;
            if (place.item < 0 || size_t(place.item) >= _held.size()) {
                for (int row = std::max(0, y); row < std::min(screen.h, y + ih); ++row)
                    for (int col = std::max(0, x); col < std::min(screen.w, x + iw); ++col) {
                        uint8_t *p = screen.row(row) + col * 4;
                        p[0] = uint8_t((p[0] + 10) / 2);
                        p[1] = uint8_t((p[1] + 10) / 2);
                        p[2] = uint8_t((p[2] + 40) / 2);
                    }
                x += iw + gap;
                continue;
            }
            const Held &held = _held[size_t(place.item)];
            const auto def = _items.find(held.name);
            image::blit(
                screen,
                picture(place.item == _heldChosen ? "Inventory/Selected Icon Background" : "Inventory/Item Frame"), x,
                y);
            _slots.push_back({x, y, place.item});
            if (def != _items.end()) {
                auto found = _icons.find(def->second.iconImage);
                if (found == _icons.end())
                    found = _icons
                                .emplace(def->second.iconImage,
                                         cachePicture(_data, "Inventory/Icon Frames/" + def->second.iconImage))
                                .first;
                const image::Image &icon = found->second;
                image::blit(screen, icon, x + (iw - icon.w) / 2, y + (ih - icon.h) / 2);
            }
            if (place.count > 1)
                image::blit(screen, picture("Inventory/More Indicator"), x, y);
            // Its uses left in its bottom right corner (its Count Format
            // String, "%d"; inferred for where).
            if (held.count >= 0 && _font.loaded()) {
                const std::string count = std::to_string(held.count);
                _font.draw(screen, x + iw - _font.width(count) - 1, y + ih - _font.height(), count);
            }
            x += iw + gap;
        }
        // The chosen item's name over the bar a while after it's chosen, in
        // the bar's Trek Small (0x41ad95), centred at Inventory Title Pos
        // (inferred).
        if (_itemTitle && _heldChosen >= 0 && size_t(_heldChosen) < _held.size() && _font.loaded()) {
            std::string name = _held[size_t(_heldChosen)].name;
            name = name.substr(name.rfind('/') + 1);
            int tx = 0;
            int ty = 0;
            readPoint(_custom, "Inventory Title Pos", tx, ty);
            _font.draw(screen, vx + tx - _font.width(name) / 2, vy + ty, name);
        }
    }

    // The pause button.
    int pauseX = 0;
    int pauseY = 0;
    readPoint(_custom, "Pause Pos", pauseX, pauseY);
    // Held down while time's stopped, lit under the mouse (0x5c3cb7).
    const image::Image &pauseDefault = picture("Pause Time Button/Pause Time Default");
    const bool overPause = _mouseX >= pauseX && _mouseY >= pauseY && _mouseX < pauseX + pauseDefault.w &&
                           _mouseY < pauseY + pauseDefault.h;
    image::blit(screen,
                picture(_paused     ? "Pause Time Button/Pause Time Press"
                        : overPause ? "Pause Time Button/Pause Time Hover"
                                    : "Pause Time Button/Pause Time Default"),
                pauseX, pauseY);
}

void GameHud::drawMarks(Surface &screen, int left, int top, uint64_t now) {
    // On the minimap (0x4ddf50): a dot of (128, 128, 255) for an officer,
    // red for an enemy; a square of (64, 64, 255) for a point of interest,
    // shrinking from 4 pixels each side of it, round again every 4/1.5
    // seconds (0x4de220).
    const float seconds = float(now - std::min(now, _pulseLast)) / 1000.0f;
    _pulseLast = now;
    _pulse = std::fmod(_pulse + seconds * 1.5f, 4.0f);
    const auto dot = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        if (x < 0 || y < 0 || x >= screen.w || y >= screen.h)
            return;
        uint8_t *p = screen.row(y) + x * 4;
        p[0] = r;
        p[1] = g;
        p[2] = b;
    };
    const int half = int(4.0f - _pulse);
    for (const Mark &m : _marks) {
        const int x = left + int(m.x * float(_minimap.w) / float(_mapW));
        const int y = top + int(m.y * float(_minimap.h) / float(_mapH));
        if (m.kind == Mark::kOfficer)
            dot(x, y, 128, 128, 255);
        else if (m.kind == Mark::kEnemy)
            dot(x, y, 255, 0, 0);
        else if (half > 0)
            for (int k = -half; k <= half; ++k) {
                dot(x + k, y - half, 64, 64, 255);
                dot(x + k, y + half, 64, 64, 255);
                dot(x - half, y + k, 64, 64, 255);
                dot(x + half, y + k, 64, 64, 255);
            }
    }
}

const ItemDefinition *GameHud::chosenItem() const {
    if (_heldChosen < 0 || size_t(_heldChosen) >= _held.size())
        return nullptr;
    const auto it = _items.find(_held[size_t(_heldChosen)].name);
    return it != _items.end() ? &it->second : nullptr;
}

const Animation &GameHud::sequence(const std::string &name) {
    auto it = _sequences.find(name);
    if (it == _sequences.end()) {
        Animation a;
        (void)a.load(_data, name);
        it = _sequences.emplace(name, std::move(a)).first;
    }
    return it->second;
}

const Font &GameHud::panelFont(const std::string &name) {
    auto it = _panelFonts.find(name);
    if (it == _panelFonts.end()) {
        it = _panelFonts.emplace(name, Font()).first;
        (void)it->second.load(_data, name);
    }
    return it->second;
}

void GameHud::drawItemPanel(Surface &screen, int x, int y, uint64_t now) {
    // The chosen item's Info Renderer: its Background Sequence, and what
    // its kind shows on it (inferred for where each is drawn within it,
    // from its fields).
    const ItemDefinition *item = chosenItem();
    if (!item || !item->infoRenderer)
        return;
    const TextObject *panel = item->infoRenderer.get();
    blitBy(screen, sequence(readText(panel, "Background Sequence")).at(now), x, y);
    // Its sequences are placed from its middle (its Phaser Indicator Pos,
    // 0,-8); its pictures and words from the info bar's corner, as Info
    // Renderer Pos is (inferred: so they fall on it, the phaser's Stun Pos
    // 155,39 and Range Pos 45,46 among them).
    const auto at = [&](const char *key, int &px, int &py) {
        px = 0;
        py = 0;
        readPoint(panel, key, px, py);
        px += _barX;
        py += _barY;
    };
    const auto fromMiddle = [&](const char *key, int &px, int &py) {
        px = 0;
        py = 0;
        readPoint(panel, key, px, py);
        px += x;
        py += y;
    };
    int px = 0;
    int py = 0;
    const std::string &kind = panel->className;
    const Font &window = panelFont(readText(_custom, "Info Window Font"));
    if (kind == "CGenericInfoRenderer") {
        // Its Text, wrapped in its Text Size.
        int w = 0;
        int h = 0;
        readPoint(panel, "Text Size", w, h);
        at("Text Pos", px, py);
        if (window.loaded())
            window.drawWrapped(screen, px, py, w, readText(panel, "Text"), Font::Align::kLeft);
    } else if (kind == "CPhaserInfoRenderer") {
        // Its power, full; its damage ("Damage: %g", 0x41bc47); and its
        // STUN or KILL as it's set, in their fonts, on the toolkit's toggle,
        // with OVERLOAD, which isn't there yet, off.
        fromMiddle("Phaser Indicator Pos", px, py);
        blitBy(screen, sequence(readText(panel, "Phaser Power Indicator Sequence")).frame(0, 0), px, py);
        const Font &font = panelFont(readText(panel, "Phaser Font"));
        at("Phaser Damage Pos", px, py);
        char damage[32];
        std::snprintf(damage, sizeof damage, "Damage: %g", double(item->weaponDamage));
        if (font.loaded())
            font.draw(screen, px, py, damage);
        const auto toggle = [&](const char *pos, const char *textPos, bool on, const char *onText, const char *onFont,
                                const char *offText, const char *offFont) {
            at(pos, px, py);
            image::blit(screen, picture(on ? "Inventory/Toggle Press" : "Inventory/Toggle Default"), px, py);
            int tx = 0;
            int ty = 0;
            readPoint(panel, textPos, tx, ty);
            const Font &f = panelFont(readText(panel, on ? onFont : offFont));
            if (f.loaded())
                f.draw(screen, px + tx, py + ty, readText(panel, on ? onText : offText));
        };
        if (item->multipleSettings)
            toggle("Stun Pos", "Stun Text Pos", _stun, "Stun On Text", "Stun On Font", "Stun Off Text",
                   "Stun Off Font");
        toggle("Overload Pos", "Overload Text Pos", false, "Overload On Text", "Overload On Font", "Overload Off Text",
               "Overload Off Font");
    } else if (kind == "CDetonateInfoRenderer") {
        // Its button, its words on it; and how many mines are left, beside
        // its Count Text (inferred for the count's place after it).
        at("Detonate Button Pos", px, py);
        image::blit(screen, picture("Info Bar/Remote Mine/Detonate Default"), px, py);
        int tx = 0;
        int ty = 0;
        readPoint(panel, "Detonate Button Text Pos", tx, ty);
        const Font &button = panelFont(readText(panel, "Detonate Button Font"));
        if (button.loaded())
            button.draw(screen, px + tx, py + ty, readText(panel, "Detonate Button Text"));
        const Font &count = panelFont(readText(panel, "Count Font"));
        at("Count Text Pos", px, py);
        if (count.loaded() && _heldChosen >= 0) {
            const std::string text =
                readText(panel, "Count Text") + ": " + std::to_string(std::max(0, _held[size_t(_heldChosen)].count));
            count.draw(screen, px, py, text);
        }
    } else if (kind == "CChargedItemInfoRenderer") {
        // Its charge: the foreground indicator over the background one,
        // cut to as much as is left (Clip Foreground As Charge Indicator),
        // and as a percentage; its button, on or off, with its words
        // (inferred for where each is drawn, and the percentage's form).
        fromMiddle("Indicator Position", px, py);
        blitBy(screen, sequence(readText(panel, "Background Indicator Sequence")).frame(0, 0), px, py);
        if (const Animation::Picture *fore = sequence(readText(panel, "Foreground Indicator Sequence")).frame(0, 0)) {
            Animation::Picture cut = *fore;
            if (panel->integer("Clip Foreground As Charge Indicator").value_or(0)) {
                const int w = int(float(cut.image.w) * std::clamp(_energy, 0.0f, 100.0f) / 100.0f);
                image::Image part;
                part.w = w;
                part.h = cut.image.h;
                for (int row = 0; row < cut.image.h; ++row) {
                    const auto from = cut.image.rgba.begin() + ptrdiff_t(size_t(row) * size_t(cut.image.w) * 4);
                    part.rgba.insert(part.rgba.end(), from, from + ptrdiff_t(size_t(w) * 4));
                }
                cut.image = std::move(part);
            }
            if (cut.image.w > 0)
                blitBy(screen, &cut, px, py);
        }
        const Font &font = panelFont(readText(panel, "Button Font"));
        at("Percent Pos", px, py);
        if (font.loaded())
            font.draw(screen, px, py, std::to_string(int(std::lround(std::clamp(_energy, 0.0f, 100.0f)))) + "%");
        if (panel->integer("Show Activate Button").value_or(0)) {
            at("Button Pos", px, py);
            image::blit(screen, picture(readText(panel, _chargeOn ? "Button On Frame" : "Button Off Frame")), px, py);
            int tx = 0;
            int ty = 0;
            readPoint(panel, "Button Text Pos", tx, ty);
            if (font.loaded())
                font.draw(screen, px + tx, py + ty, readText(panel, _chargeOn ? "Button On Text" : "Button Off Text"));
        }
    } else if (kind == "CTricorderInfoRenderer") {
        // What it's said, typed out, at Rules.txt's Tricorder Pos in its
        // Tricorder Size.
        int tx = 0;
        int ty = 0;
        int tw = 0;
        int th = 0;
        readPoint(_custom, "Tricorder Pos", tx, ty);
        readPoint(_custom, "Tricorder Size", tw, th);
        if (window.loaded())
            window.drawWrapped(screen, x + tx, y + ty, tw, _scan.substr(0, std::min(_scanTyped, _scan.size())),
                               Font::Align::kLeft);
    }
}

} // namespace reluva::stawayteam
