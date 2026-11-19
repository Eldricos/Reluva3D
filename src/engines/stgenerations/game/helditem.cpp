// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/game/helditem.h"

#include <algorithm>

#include "common/text.h"

namespace reluva::stgenerations {

namespace {

// From the item controllers' vt68 (0x44A291 on): the overlay, the pose
// hotspots and the muzzle (16.16 in the original).
const HeldItem::Style kPhaser = { "nuphaser.ovr",
                                  { { 116, 34 }, { 116, 3 }, { 75, 0 }, { 35, 2 }, { 35, 33 }, { 74, 29 } },
                                  { 1.5, 0.0, 1.0 } };
const HeldItem::Style kDisruptor = { "nudisrup.ovr",
                                     { { 110, 34 }, { 110, 3 }, { 75, 0 }, { 41, 2 }, { 41, 33 }, { 74, 31 } },
                                     { 1.5, 0.0, 1.0625 } };
const HeldItem::Style kRifle = { "nudrifle.ovr",
                                 { { 94, 18 }, { 92, 8 }, { 73, 0 }, { 48, 8 }, { 48, 19 }, { 73, 14 } },
                                 { 1.5, 0.0, 1.0 } };
const HeldItem::Style kKlingon = { "nuklidis.ovr",
                                   { { 100, 33 }, { 111, 2 }, { 77, 0 }, { 46, 3 }, { 55, 34 }, { 77, 30 } },
                                   { 1.5, 0.09375, 1.1875 } };
const HeldItem::Style kPlant = { "nuplant.ovr",
                                 { { 119, 33 }, { 120, 11 }, { 75, 4 }, { 28, 12 }, { 28, 32 }, { 75, 26 } },
                                 { 1.5, 0.125, 1.5 } };
const HeldItem::Style kPlant2 = { "nuplant2.ovr",
                                  { { 119, 33 }, { 120, 11 }, { 75, 4 }, { 28, 12 }, { 28, 32 }, { 75, 26 } },
                                  { 1.5, 0.125, 1.5 } };

// Poses, as frame numbers.
enum {
    kRightLow = 6,
    kRightHigh = 7,
    kCentreHigh = 8,
    kLeftHigh = 9,
    kLeftLow = 10,
    kCentreLow = 11
};

} // namespace

const HeldItem::Style* HeldItem::getStyle(int kind) {
    switch (kind) {
    case 2:
        return &kPhaser;
    case 3:
        return &kDisruptor;
    case 4:
        return &kRifle;
    case 6:
        return &kKlingon;
    case 11:
        return &kPlant;
    case 12:
        return &kPlant2;
    default:
        return nullptr;
    }
}

HeldItem::Placement HeldItem::place(const Style& style, int cursorX, int cursorY, int w, int h) {
    auto dy = [&style](int frame) { return style.hotspot[frame - kRightLow][1]; };
    int highest = std::max(dy(kRightHigh), std::max(dy(kCentreHigh), dy(kLeftHigh)));
    int lowest = std::max(dy(kRightLow), std::max(dy(kLeftLow), dy(kCentreLow)));
    // The gun moves from a + (b - a) * y / b, sliding down as the cursor
    // does, and follows it across at 140 / (w / 2) of its speed.
    int a = h - (kFrameHeight - highest);
    int b = h - (kFrameHeight - lowest) / 2;
    int x = w / 2 + 140 * (cursorX - w / 2) / (w / 2);
    int y = a + (b - a) * cursorY / b;
    int line = h - lowest - 5;

    int third = cursorX < w / 3 ? 0 : cursorX < 2 * w / 3 ? 1 : 2;
    static const int kLow[3] = { kLeftLow, kCentreLow, kRightLow };
    static const int kHigh[3] = { kLeftHigh, kCentreHigh, kRightHigh };
    Placement p;
    p.usable = cursorY < line;
    p.frame = kLow[third];
    if (p.usable && y - dy(kLow[third]) + kFrameHeight < h)
        p.frame = kHigh[third];
    p.x = x - style.hotspot[p.frame - kRightLow][0];
    p.y = y - dy(p.frame);
    return p;
}

void HeldItem::getMuzzle(const Style& style, int cursorX, int cursorY, int w, int h, double& ahead, double& up,
                         double& left) {
    // The original also raises it as the player looks down; the scale of
    // that is not known, so it is left out.
    ahead = style.muzzle[0];
    up = style.muzzle[1] - 0.5 - 0.375 * cursorY / h;
    left = (w / 2 - cursorX) * style.muzzle[2] / (w / 2);
}

void HeldItem::load(const ResourceManager& resources, const ResourceRef& level, int kind) {
    _kind = kind;
    _style = getStyle(kind);
    _loaded = false;
    if (!_style)
        return;
    // Beside GAME.LV2, or anywhere in COMMON.DAT.
    size_t slash = level.name.find_last_of("/\\");
    std::string folder = slash == std::string::npos ? "" : level.name.substr(0, slash + 1);
    std::string wanted = text::toLower(_style->overlay);
    const ResourceRef* found = nullptr;
    for (const ResourceRef& ref : resources.getImages()) {
        std::string name = text::toLower(ref.name);
        std::replace(name.begin(), name.end(), '\\', '/');
        if (ref.archive == level.archive && name == text::toLower(folder) + wanted) {
            found = &ref;
            break;
        }
        if (!found && text::toUpper(ref.archive) == "COMMON.DAT" &&
            (name == wanted || text::hasSuffixIgnoreCase(name, "/" + wanted)))
            found = &ref;
    }
    if (!found)
        return;
    std::unique_ptr<SeekableReadStream> stream = resources.open(*found);
    _loaded = stream && _overlay.load(*stream, 255) && _overlay.getRowCount() >= 12 * kFrameHeight;
}

void HeldItem::draw(Canvas& view, int cursorX, int cursorY, bool firing) const {
    if (!_loaded)
        return;
    Placement p = place(*_style, cursorX, cursorY, view.w, view.h);
    _overlay.drawFrame(view, firing ? p.frame - kPoses : p.frame, kFrameHeight, p.x, p.y);
}

} // namespace reluva::stgenerations
