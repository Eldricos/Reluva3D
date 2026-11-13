// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/game/hudscreen.h"

#include <algorithm>
#include <cmath>

#include "engines/stgenerations/formats/bitmap.h"
#include "common/text.h"
#include "engines/stgenerations/formats/level.h"
#include "engines/stgenerations/game/mission.h"
#include "engines/stgenerations/formats/script.h"

namespace reluva::stgenerations {

namespace {

const double kPi = 3.14159265358979323846;

// Inclusive rectangles, as the original's tables give them.
Rect inclusive(int x1, int y1, int x2, int y2) {
    return Rect(x1, y1, x2 + 1, y2 + 1);
}

// The console's parts, as the original lays them out (see
// docs/engines/stgenerations/formats/scripts.md, "The away-team screen").
const Rect kLeftPanel = inclusive(0, 253, 205, 458);
const Rect kTabs[3] = {inclusive(6, 261, 28, 312), inclusive(6, 331, 28, 382), inclusive(6, 401, 28, 452)};
const Rect kMap = inclusive(39, 267, 192, 414);
const Rect kViewer = inclusive(223, 311, 433, 458);
const Rect kAuxCycle = inclusive(207, 425, 219, 459);
const Rect kWeaponCycle = inclusive(207, 253, 219, 268);
const Rect kWeaponDisplay = inclusive(222, 252, 431, 296);
const Rect kInventory = inclusive(435, 256, 639, 457);
const Rect kLevelsButton = Rect(43, 415, 43 + 29, 415 + 27);
const Rect kZoomButton = Rect(74, 415, 74 + 29, 415 + 27);
// The scan filters: life, energy, trilithium (s-life, s-energ, s-lith.bmp).
const Rect kFilterButtons[3] = {Rect(117, 415, 117 + 25, 415 + 27), Rect(141, 415, 141 + 25, 415 + 27),
                                Rect(165, 415, 165 + 25, 415 + 27)};
const char *const kFilterImages[3] = {"s-life.bmp", "s-energ.bmp", "s-lith.bmp"};
const int kButtonHeight = 27;
// Markers are 7 x 7, four frames blinking.
const int kMarkerFrames = 4;
const int kSlotX[3] = {436, 505, 574};
const int kSlotY[4] = {257, 308, 359, 410};
const int kIconWidth = 64, kIconHeight = 46;
// The rectangle round the item in use: palette index 0x22.
const uint8_t kActiveColour = 0x22;
const char *const kMarkerStyles[] = {"romu", "sorn", "injr", "klng", "chod", "fish", "fuzy", "pred"};

std::string normalise(const std::string &name) {
    std::string s = text::toLower(name);
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

} // namespace

const Rect HudScreen::kView = Rect(0, 0, 640, 252);
const Rect HudScreen::kTextLine = inclusive(10, 461, 639, 477);

HudScreen::HudScreen(const ResourceManager &resources, const ResourceRef &level, const ScriptFile &script,
                     const Palette &palette)
    : _resources(resources), _level(level), _script(script), _palette(palette) {
    _console = load("actionx.bmp", kGlobal);
}

HudScreen::~HudScreen() {
}

const HudScreen::Image *HudScreen::load(const std::string &name, Scope scope, int frameHeight) {
    std::string key = std::string(scope == kGlobal ? "g:" : "m:") + normalise(name);
    auto it = _images.find(key);
    if (it != _images.end())
        return it->second.get();

    // Global names are found in COMMON.DAT wherever they sit in it; the
    // mission's beside its GAME.LV2.
    std::string wanted = normalise(name);
    size_t slash = _level.name.find_last_of("/\\");
    std::string folder = slash == std::string::npos ? "" : normalise(_level.name.substr(0, slash + 1));
    const ResourceRef *found = nullptr;
    for (const ResourceRef &ref : _resources.getImages()) {
        std::string candidate = normalise(ref.name);
        bool match;
        if (scope == kGlobal) {
            match = text::toUpper(ref.archive) == "COMMON.DAT" &&
                    (candidate == wanted || text::hasSuffixIgnoreCase(candidate, "/" + wanted));
        } else {
            match = ref.archive == _level.archive && candidate == folder + wanted;
        }
        if (match) {
            found = &ref;
            break;
        }
    }
    std::unique_ptr<Image> image;
    BitmapDecoder decoder;
    if (found && _resources.loadImage(*found, decoder) && decoder.hasPalette()) {
        image.reset(new Image());
        image->set(decoder, _palette, frameHeight);
    }
    const Image *result = image.get();
    _images[key] = std::move(image);
    return result;
}

const HudScreen::Image *HudScreen::loadScriptImage(int16_t index, int frameHeight) {
    const std::vector<std::string> &names = _script.getResourceNames();
    if (index < 0 || index >= (int16_t)names.size())
        return nullptr;
    const std::string &name = names[index];
    if (!name.empty() && name[0] == '*')
        return load(name.substr(1), kGlobal, frameHeight);
    return load(name, kMission, frameHeight);
}

void HudScreen::blit(Canvas &dst, const Image &image, int frame, int x, int y, int key, const Rect *clip) {
    image.draw(dst, frame, x, y, key, clip);
}

void HudScreen::draw(Canvas &screen, Mission &mission, const LevelFile &level, double playerX, double playerY,
                     double playerZ, double yaw, uint32_t time) {
    if (!_console)
        return;
    switch (mission.takeLeftPanelRequest()) {
    case 2:
        _leftMode = kLeftOff;
        break;
    case 3:
        _leftMode = kLeftLife;
        break;
    default:
        break;
    }

    uint8_t black = level.getPalette().findBestColor(0, 0, 0);
    screen.fillRect(Rect(0, kView.bottom, screen.w, screen.h), black);
    blit(screen, *_console, 0, 206, 252);
    // Its frames: off, object, map, life (0x4434B1).
    static const int kLeftFrames[] = {0, 3, 2, 1};
    if (const Image *left = load("l-m-m-o.bmp", kGlobal, 206))
        blit(screen, *left, kLeftFrames[_leftMode], kLeftPanel.left, kLeftPanel.top);

    switch (_leftMode) {
    case kLeftLife:
        drawLifePanel(screen, mission);
        break;
    case kLeftMap:
        drawMap(screen, mission, level, playerX, playerY, playerZ, yaw, time);
        break;
    case kLeftObject:
        if (const Image *object = load("OBJECT.BMP", kMission))
            blit(screen, *object, 0, 39, 267);
        break;
    default:
        break;
    }

    if (const Image *cycle = load("wepcycle.bmp", kGlobal))
        blit(screen, *cycle, 0, kWeaponCycle.left, kWeaponCycle.top);
    drawWeaponDisplay(screen, mission);
    drawPanel(screen, mission, time);
    drawInventory(screen, mission);
}

void HudScreen::drawWeaponDisplay(Canvas &screen, Mission &mission) {
    // The weapon in use shows its display (0x439F9E); the phaser's is
    // weapphas.bmp with an arrow at its power level (0x44B721) and a gauge
    // of 59 ticks, 3 pixels apart, whose empty part is wiped from the right
    // (0x44BA38).
    const Mission::Item *item = mission.getItem(mission.getActiveItem());
    if (!item || item->kind != 2)
        return;
    const Image *display = load("weapphas.bmp", kGlobal);
    if (!display)
        return;
    blit(screen, *display, 0, kWeaponDisplay.left, kWeaponDisplay.top);
    if (const Image *arrow = load("p-arrow.bmp", kGlobal, 14))
        blit(screen, *arrow, 0, kWeaponDisplay.left + 11 * item->level - 4, kWeaponDisplay.top, kFirstPixel,
             &kWeaponDisplay);
    int full = item->maxCharge > 0 ? (59 * item->charge + item->maxCharge - 1) / item->maxCharge : 0;
    for (int k = 0; k < 59 - full; k++) {
        int x = kWeaponDisplay.left + 191 - 3 * k;
        screen.fillRect(Rect(x, kWeaponDisplay.top + 28, x + 2, kWeaponDisplay.top + 35), display->remap[0]);
    }
}

void HudScreen::drawLifePanel(Canvas &screen, Mission &mission) {
    const Hud &hud = mission.getHud();
    const Image *body =
        hud.getBodyImage() < 0 ? load("BODY.BMP", kMission, 177) : loadScriptImage(hud.getBodyImage(), 177);
    if (body)
        blit(screen, *body, hud.getBodyFrame(), 40, 269);

    // The health bar (0x445AFE): a background by how low it is, then a
    // stack of units, 3 rows apart with a gap above the first 15.
    int health = mission.getHealth();
    if (const Image *bar = load("3health.bmp", kGlobal, 50))
        blit(screen, *bar, health > 15 ? 2 : health > 5 ? 1 : 0, 118, 393, kFirstPixel);
    if (const Image *unit = load("unithlt0.bmp", kGlobal)) {
        for (int k = 0; k < health; k++) {
            int y = 442 - unit->surface.h - 1 - 3 * k - (k >= 15 ? 7 : 0);
            if (y < 270)
                break;
            blit(screen, *unit, 0, 119, y);
        }
    }
}

void HudScreen::drawMap(Canvas &screen, Mission &mission, const LevelFile &level, double playerX, double playerY,
                        double playerZ, double yaw, uint32_t time) {
    // Over the left panel's blue map frame (l-m-m-o.bmp), in cyan, as the
    // original draws it.
    const Palette &palette = level.getPalette();
    uint8_t wallColour = palette.findBestColor(104, 252, 252);
    double cx = (kMap.left + kMap.right) / 2.0;
    double cy = (kMap.top + kMap.bottom) / 2.0;
    double scale = mission.getHud().getMapZoom(_mapZoom);
    auto toScreen = [&](double x, double y, int &sx, int &sy) {
        sx = (int)std::lround(cx + (x - playerX) * scale);
        sy = (int)std::lround(cy - (y - playerY) * scale);
    };

    // Walls marked for the map, in the height band shown (relative to the player).
    const double *levels = mission.getHud().getMapLevels();
    double low = playerZ + levels[_mapBand * 2];
    double high = playerZ + levels[_mapBand * 2 + 1];
    const std::vector<LevelFile::Vertex> &vertices = level.getVertices();
    for (const LevelFile::Wall &wall : level.getWalls()) {
        if (!(wall.flags & LevelFile::kWallOnMap))
            continue;
        if (wall.top / 65536.0 < low || wall.bottom / 65536.0 > high)
            continue;
        int x1;
        int y1;
        int x2;
        int y2;
        toScreen(vertices[wall.vertex1].x / 65536.0, vertices[wall.vertex1].y / 65536.0, x1, y1);
        toScreen(vertices[wall.vertex2].x / 65536.0, vertices[wall.vertex2].y / 65536.0, x2, y2);
        // Plot the line, clipped to the map.
        int steps = std::max(std::abs(x2 - x1), std::abs(y2 - y1));
        for (int i = 0; i <= steps; i++) {
            int x = steps ? x1 + (x2 - x1) * i / steps : x1;
            int y = steps ? y1 + (y2 - y1) * i / steps : y1;
            if (kMap.contains(x, y))
                screen.setPixel(x, y, wallColour);
        }
    }

    for (const auto &entry : mission.getHud().getMarkers()) {
        double x;
        double y;
        double z;
        if (!mission.locate(entry.first, x, y, z))
            continue;
        int sx;
        int sy;
        toScreen(x, y, sx, sy);
        // Each kind shows only while its filter is on.
        if (!_filters[entry.second.kind])
            continue;
        const Image *dot = nullptr;
        if (entry.second.kind == Hud::kMarkerLife && entry.second.style >= 0 && entry.second.style < 8)
            dot = load(std::string("map/dot-") + kMarkerStyles[entry.second.style] + ".bmp", kGlobal, -kMarkerFrames);
        else if (entry.second.kind == Hud::kMarkerEnergy)
            dot = load("map/dot-enrg.bmp", kGlobal, -kMarkerFrames);
        else if (entry.second.kind == Hud::kMarkerTrilithium)
            dot = load("map/dot-tril.bmp", kGlobal, -kMarkerFrames);
        if (dot) {
            int frame = (int)(time / 150 % kMarkerFrames);
            blit(screen, *dot, frame, sx - dot->surface.w / 2, sy - dot->frameHeight / 2, kFirstPixel, &kMap);
        } else {
            uint8_t colour = palette.findBestColor(255, entry.second.kind == Hud::kMarkerLife ? 0 : 255, 0);
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (kMap.contains(sx + dx, sy + dy))
                        screen.setPixel(sx + dx, sy + dy, colour);
                }
            }
        }
    }

    // The player, an arrow in eight headings (agpface2.bmp).
    int octant = (int)std::lround(yaw / (kPi / 4)) & 7;
    if (const Image *face = load("agpface2.bmp", kGlobal, -8)) {
        blit(screen, *face, octant, (int)cx - face->surface.w / 2, (int)cy - face->frameHeight / 2, kFirstPixel, &kMap);
    } else {
        screen.fillRect(Rect((int)cx - 1, (int)cy - 1, (int)cx + 2, (int)cy + 2), palette.findBestColor(255, 255, 255));
    }

    // The buttons are strips of 27-row frames: the height band lit, the
    // zoom, and each filter dim (0) or lit (1).
    if (const Image *levelsButton = load("levels.bmp", kGlobal, kButtonHeight))
        blit(screen, *levelsButton, _mapBand, kLevelsButton.left, kLevelsButton.top);
    if (const Image *zoom = load("zoomz.bmp", kGlobal, kButtonHeight))
        blit(screen, *zoom, _mapZoom, kZoomButton.left, kZoomButton.top);
    for (int i = 0; i < 3; i++) {
        if (const Image *filter = load(kFilterImages[i], kGlobal, kButtonHeight))
            blit(screen, *filter, _filters[i] ? 1 : 0, kFilterButtons[i].left, kFilterButtons[i].top);
    }
}

void HudScreen::drawPanel(Canvas &screen, Mission &mission, uint32_t time) {
    const Hud &hud = mission.getHud();
    const Hud::Panel *panel = hud.getVisiblePanel();
    if (!panel) {
        if (const Image *background = load("AUXDEF.BMP", kMission))
            blit(screen, *background, 0, kViewer.left, kViewer.top);
    } else {
        if (const Image *image = loadScriptImage(panel->image, Hud::kPanelHeight))
            blit(screen, *image, panel->frame, kViewer.left, kViewer.top, -1, &kViewer);
        for (const Hud::Button &button : panel->buttons) {
            if (button.hotspot)
                continue;
            if (const Image *image = loadScriptImage(button.image, button.rect.height()))
                blit(screen, *image, button.frame, kViewer.left + button.rect.left, kViewer.top + button.rect.top,
                     button.keyed ? kFirstPixel : kOpaque, &kViewer);
        }
    }
    // The cycle button blinks while there is more than one panel.
    if (const Image *cycle = load("auxcycle.bmp", kGlobal, kAuxCycle.height())) {
        int frame = hud.getPanelCount() > 1 && (time / 500) % 2 ? 1 : 0;
        blit(screen, *cycle, frame, kAuxCycle.left, kAuxCycle.top);
    }
}

void HudScreen::drawInventory(Canvas &screen, Mission &mission) {
    const Image *icons = load("INVENT.BMP", kMission, kIconHeight);
    const Hud &hud = mission.getHud();
    for (int slot = 0; slot < Hud::kSlots; slot++) {
        uint16_t id = hud.getSlot(slot);
        if (!id)
            continue;
        if (_dragging && slot == _pressedSlot)
            continue;
        int x = kSlotX[slot % 3];
        int y = kSlotY[slot / 3];
        int icon = mission.getItemIcon(id);
        if (icons && icon >= 0)
            blit(screen, *icons, icon, x, y, kFirstPixel);
        // Greyed out (grayout.bmp, its checks see-through), or framed if in use.
        if (!hud.isSlotEnabled(slot)) {
            if (const Image *grey = load("grayout.bmp", kGlobal))
                blit(screen, *grey, 0, x, y, kFirstPixel);
        } else if (id == mission.getActiveItem()) {
            screen.frameRect(Rect(x + 1, y + 1, x + 63, y + 45), kActiveColour);
        }
    }
}

int HudScreen::findSlot(int x, int y) {
    for (int slot = 0; slot < Hud::kSlots; slot++) {
        int sx = kSlotX[slot % 3];
        int sy = kSlotY[slot / 3];
        if (Rect(sx - 2, sy - 2, sx + kIconWidth + 4, sy + kIconHeight + 4).contains(x, y))
            return slot;
    }
    return -1;
}

bool HudScreen::mouseDown(Mission &mission, int x, int y, bool right) {
    if (!_console || y < kView.bottom)
        return false;
    for (int i = 0; i < 3; i++) {
        if (kTabs[i].contains(x, y)) {
            _leftMode = (LeftMode)(kLeftLife + i);
            return true;
        }
    }
    if (_leftMode == kLeftMap) {
        if (kLevelsButton.contains(x, y)) {
            _mapBand = (_mapBand + 1) % 3;
            return true;
        }
        for (int i = 0; i < 3; i++) {
            if (kFilterButtons[i].contains(x, y)) {
                _filters[i] = !_filters[i];
                return true;
            }
        }
        if (kZoomButton.contains(x, y)) {
            _mapZoom = (_mapZoom + 1) % 3;
            return true;
        }
    }
    Hud &hud = mission.getHud();
    int slot = findSlot(x, y);
    if (slot >= 0 && !hud.isSlotEnabled(slot))
        return true;
    if (slot >= 0) {
        // 0x44687C: the left button picks it up, to click or drag.
        if (right)
            mission.describeItem(slot);
        else if (hud.getSlot(slot)) {
            _pressedSlot = slot;
            _dragging = false;
            _pressX = _mouseX = x;
            _pressY = _mouseY = y;
        }
        return true;
    }
    if (kWeaponDisplay.contains(x, y)) {
        // 0x44B896: the phaser's display sets the power level.
        const Mission::Item *item = mission.getItem(mission.getActiveItem());
        if (item && item->kind == 2)
            mission.setItemLevel((x - 239) / 11 + 1);
        return true;
    }
    if (kViewer.contains(x, y)) {
        if (right)
            hud.describePanel();
        else
            hud.clickPanel(x - kViewer.left, y - kViewer.top);
        return true;
    }
    if (kAuxCycle.contains(x, y)) {
        hud.nextPanel();
        return true;
    }
    if (kWeaponCycle.contains(x, y)) {
        // The next item the player can hold, after the one in use.
        int current = hud.findItem(mission.getActiveItem());
        for (int i = 1; i <= Hud::kSlots; i++) {
            int slot = (current + i + Hud::kSlots) % Hud::kSlots;
            if (hud.getSlot(slot) && mission.isWeapon(hud.getSlot(slot))) {
                mission.useItem(slot);
                break;
            }
        }
        return true;
    }
    return kLeftPanel.contains(x, y);
}

void HudScreen::mouseMove(int x, int y) {
    _mouseX = x;
    _mouseY = y;
    // Moving 4 pixels either way drags it; the item in slot 0 stays put.
    if (_pressedSlot > 0 && !_dragging && (std::abs(x - _pressX) >= 4 || std::abs(y - _pressY) >= 4))
        _dragging = true;
}

bool HudScreen::mouseUp(Mission &mission, int x, int y) {
    if (_pressedSlot < 0)
        return false;
    int slot = _pressedSlot;
    bool dragged = _dragging;
    _pressedSlot = -1;
    _dragging = false;
    if (!dragged) {
        // A click, if it was let go of where it was pressed (0x4469C2).
        if (findSlot(x, y) == slot)
            mission.useItem(slot);
    } else if (kInventory.contains(x, y)) {
        mission.getHud().moveItem(slot, findSlot(x, y));
    } else {
        mission.dropItem(slot);
    }
    return true;
}

void HudScreen::drawDrag(Canvas &screen, Mission &mission) {
    if (!_dragging || _pressedSlot < 0)
        return;
    const Image *icons = load("INVENT.BMP", kMission, kIconHeight);
    uint16_t id = mission.getHud().getSlot(_pressedSlot);
    if (icons && id && mission.getItemIcon(id) >= 0)
        blit(screen, *icons, mission.getItemIcon(id), _mouseX - kIconWidth / 2, _mouseY - kIconHeight / 2, kFirstPixel);
}

} // namespace reluva::stgenerations
