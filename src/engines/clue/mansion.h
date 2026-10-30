// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Boddy Mansion as clue.lvl places it: every object in its state 0, with
// the box it fills in the world, and the order to draw them in.

#include "engines/clue/game_data.h"
#include "engines/clue/level.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace reluva::clue {

// A box in the world: from (x, y) on the floor, sizeX by sizeY, height high.
// The camera looks from beyond +x +y: screen x is x - y, screen y is
// (x + y) / 2 - z.
struct WorldBox {
    int x = 0, y = 0;
    int sizeX = 0, sizeY = 0, height = 0;
};

// Whether a is drawn after b (it hides b where they overlap on screen):
// 1 if a is in front, -1 if b is, 0 if their boxes overlap on the floor,
// when neither can be said to be in front.
int compareDepth(const WorldBox& a, const WorldBox& b);

// Where screen rectangles are: x, y, width, height.
struct ScreenRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool overlaps(const ScreenRect& o) const {
        return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h;
    }
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// The order to draw boxes in, back to front: those whose screen rectangles
// overlap by compareDepth, the others as they come; ties (and cycles, which
// overlapping boxes can make) by the depth of their centres.
std::vector<size_t> depthOrder(const std::vector<WorldBox>& boxes, const std::vector<ScreenRect>& rects);

// How the mansion is drawn: the walls nearest the camera (layer 4), the
// black outside (layer 6), the move-number templates.
struct MansionLook {
    enum class Walls { SeeThrough, Solid, Hidden } walls = Walls::SeeThrough;
    int seeThrough = 128; // the near walls' opacity when see-through: about half, as in recordings
    bool outside = true;
    bool tileNumbers = false;
    bool animate = true;
};

class Mansion {
public:
    // An image to draw, from the item's anchor point.
    struct Piece {
        const Image* image = nullptr;
        int dx = 0, dy = 0;
    };
    using Frame = std::vector<Piece>;

    struct Item {
        size_t placement = 0;
        const ObjectDef* object = nullptr;
        const Animation* animation = nullptr; // its state 0's
        const std::vector<Frame>* frames = nullptr;
        WorldBox box;
        bool flat = false; // floors and the outside: drawn first, in layer order
        int layer = 0;
        int anchorX = 0, anchorY = 0; // on screen
        ScreenRect rect;              // of frame 0 on screen
    };

    // Builds every placement; those whose object or animation can't be
    // found or read are left out and counted in problems().
    Error build(const GameData& data, const Level& level);

    const Level* level() const { return level_; }
    const std::vector<Item>& items() const { return items_; }
    const std::vector<size_t>& drawOrder() const { return order_; } // into items()
    const ScreenRect& bounds() const { return bounds_; }
    const std::vector<std::string>& problems() const { return problems_; }

    // Whether the look shows an item; and the view from (camX, camY) drawn
    // into the target, the animations at the time given.
    bool shown(const Item& item, const MansionLook& look) const;
    void draw(Surface& target, int camX, int camY, const MansionLook& look, uint64_t nowMs) const;

private:
    const std::vector<Frame>* resolve(const GameData& data, const Animation& anim);

    const Level* level_ = nullptr;
    std::vector<Item> items_;
    std::vector<size_t> order_;
    ScreenRect bounds_;
    std::vector<std::string> problems_;
    std::map<const Animation*, std::vector<Frame>> frames_;
};

} // namespace reluva::clue
