// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Boddy Mansion as clue.lvl places it: every object in its state 0, with
// the box it fills in the world, and the order to draw them in.

#include "engines/clue/game_data.h"
#include "engines/clue/level.h"

#include <array>
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
    int z = 0;    // how high above the floor it is placed
    int zOff = 0; // where its box starts above that (the frame's box offset)
};

// Clue.exe's test of what is drawn first (0x41CBD0): b is behind a when b's
// box starts before a's ends along x, y and height alike.
bool behind(const WorldBox& b, const WorldBox& a);

// Whether a point on the screen (the world's, camera not taken off) falls in
// a box's outline there, the hexagon its corners make: Clue.exe's test of a
// click on a hot spot (0x419980), the box and not the picture.
bool onBox(const WorldBox& box, int x, int y);

// Whether a is drawn after b (it hides b where they overlap on screen):
// 1 if a is in front, -1 if b is (beyond it along x, or else y, or else
// wholly above it), 0 if their boxes overlap, when neither can be said to
// be in front.
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

// Clue.exe's drawing order (0x443C20, 0x41CBD0): sorted by the anchors' y,
// then x, then height; then, each frame, whatever comes later in the list
// and is behind an earlier thing it overlaps on screen moved before it,
// a guard against two trading places for ever. Run here until nothing
// moves (or for so long). keys: each box's anchor (x, y, z).
std::vector<size_t> exeDepthOrder(const std::vector<WorldBox>& boxes, const std::vector<ScreenRect>& rects,
                                  const std::vector<std::array<int, 3>>& keys, int maxPasses = 200);

// How the mansion is drawn: what stands in front of the suspect whose turn
// it is (the original's "Translucency"), the walls (its "Mansion Walls"),
// the black outside (layer 6), the move-number templates.
struct MansionLook {
    // What stands in front of a revealing actor (Mansion::Actor::reveal):
    // drawn see-through ("Translucency" on), left out (off), or left be.
    enum class InFront { SeeThrough, Hidden, Solid } inFront = InFront::SeeThrough;
    bool walls = true; // off: no walls, nor the lamps on them ("Mansion Walls" off)
    // Reluva's own, for its mansion page: the walls nearest the camera
    // (layer 4) as they are, see-through or left out.
    enum class NearWalls { Solid, SeeThrough, Hidden } nearWalls = NearWalls::Solid;
    bool outside = true;
    bool tileNumbers = false;
    bool animate = true;
    bool characters = true; // the suspects where clue.lvl stands them (off when the game moves them)
    // "Lighting Effects": the house dark but near the revealing actors, as
    // Clue.exe lights it (docs/engines/clue.md).
    bool lighting = false;
};

// Clue.exe's shades (ImageRLE16.dll's pixel functions, by the number in
// the high half of an object's draw flags): 0 as it is, 3 half over what
// is behind (see-through), 6-16 darker by steps from 7/8 to 1/4, 17 (3/16)
// where no light reaches. Applied to the 5 bits of each colour, as it does.
uint8_t shade(uint8_t c8, int level);
// The shade a light at (lx, ly) gives a point (x, y) on the floor (Clue.exe
// at 0x4289c0): by max(dx, dy) + min(dx, dy) / 2, full within 80, a step
// darker each 16 on, to 1/4 from 240.
int lightShade(int lx, int ly, int x, int y);
constexpr int kShadeNone = 0, kShadeSeeThrough = 3, kShadeUnlit = 17;

class Mansion {
public:
    // An image to draw, from the item's anchor point.
    struct Piece {
        const Image* image = nullptr;
        int dx = 0, dy = 0;
        int layer = 0; // its layer in the animation (walls and floors are lit by it)
        int blend = 0; // its part's own pixel function (2-4) in place of the light, or 0
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
        // Its kind, as Clue.exe makes its object of it (docs/formats/clue/
        // level.md): the placement's tag's, or else the object's, primary
        // and secondary types. 0 a wall, 3 furniture, 4 and 5 hot spots,
        // 8 a secret passage, 12 drapes ...
        uint8_t kind = 0, subKind = 0;
        bool keepsSolid = false; // furniture with a timing at 2000: never see-through
        int worldX = 0, worldY = 0; // the placement's position (box.x, box.y less the frame's offsets)
        int layers = 0;             // its animation's
    };

    // Something drawn among the items that isn't in the level: a suspect
    // walking, a number on a square. Put in the drawing order by its box.
    struct Actor {
        WorldBox box;            // box.x, box.y its anchor in the world, as a placement's
        const Frame* frame = nullptr;
        bool flat = false;       // on the floor: after the floors, under everything else
        int opacity = 255;
        // What stands in front of it, near it, is drawn see-through or left
        // out (MansionLook::inFront), as Clue.exe does around the suspect
        // whose turn it is and the numbers of a planned move: from its
        // anchor (box.x, box.y), what stands within 208 units further
        // along x and y. In a room (a suspect's), only the walls; hot spots
        // only from the corridors.
        bool reveal = false;
        bool inRoom = false, inCorridor = false;
    };
    // An animation's frames as pieces to draw (kept).
    const std::vector<Frame>* frames(const GameData& data, const Animation& anim) { return resolve(data, anim); }

    // An item playing another of its object's states (a hot spot's
    // reaction, the drapes in a gust) from startMs: once, then back to
    // state 0, or looping until stopped. False if it has no such state.
    bool playState(const GameData& data, size_t item, uint16_t state, uint64_t startMs, bool loop = false);
    void stopState(size_t item) { states_.erase(item); }
    bool playingState(size_t item, uint64_t nowMs) const;
    // The item drawn uppermost at a point of the view (in the mansion's
    // screen space: the camera added), by its pixels; -1 for none.
    int itemAt(int x, int y, const MansionLook& look, uint64_t nowMs) const;

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
    void draw(Surface& target, int camX, int camY, const MansionLook& look, uint64_t nowMs,
              const std::vector<Actor>& actors = {}) const;
    // Where an actor goes in the drawing order: before drawOrder()[n].
    size_t insertionPoint(const Actor& actor) const;

private:
    const std::vector<Frame>* resolve(const GameData& data, const Animation& anim);

    struct Playing {
        const Animation* animation = nullptr;
        const std::vector<Frame>* frames = nullptr;
        uint64_t start = 0;
        bool loop = false;
    };
    const std::vector<Frame>& framesAt(size_t item, uint64_t nowMs, const MansionLook& look, size_t& frame) const;

    const Level* level_ = nullptr;
    std::map<size_t, Playing> states_;
    std::vector<Item> items_;
    std::vector<size_t> order_;
    size_t flatCount_ = 0;              // order_'s first entries, the flat items
    std::vector<size_t> orderOf_;       // each item's place in order_
    ScreenRect bounds_;
    std::vector<std::string> problems_;
    std::map<const Animation*, std::vector<Frame>> frames_;
};

} // namespace reluva::clue
