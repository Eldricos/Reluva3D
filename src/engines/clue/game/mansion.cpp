// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/game/mansion.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <tuple>

namespace reluva::clue {

namespace {

int floorDiv(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

// How deep a box's centre is: further from the camera is smaller.
int centreDepth(const WorldBox &b) {
    return 2 * (b.x + b.y) + b.sizeX + b.sizeY;
}

} // namespace

int compareDepth(const WorldBox &a, const WorldBox &b) {
    if (a.x >= b.x + b.sizeX)
        return 1;
    if (b.x >= a.x + a.sizeX)
        return -1;
    if (a.y >= b.y + b.sizeY)
        return 1;
    if (b.y >= a.y + a.sizeY)
        return -1;
    if (a.z >= b.z + b.height)
        return 1;
    if (b.z >= a.z + a.height)
        return -1;
    return 0;
}

std::vector<size_t> depthOrder(const std::vector<WorldBox> &boxes, const std::vector<ScreenRect> &rects) {
    const size_t n = boxes.size();
    // Visit in centre-depth order, so that boxes no rule orders come out
    // back to front too.
    std::vector<size_t> byDepth(n);
    std::iota(byDepth.begin(), byDepth.end(), size_t(0));
    std::stable_sort(byDepth.begin(), byDepth.end(),
                     [&](size_t a, size_t b) { return centreDepth(boxes[a]) < centreDepth(boxes[b]); });
    // behind[i]: the boxes to draw before i.
    std::vector<std::vector<size_t>> behind(n);
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j) {
            if (!rects[i].overlaps(rects[j]))
                continue;
            int c = compareDepth(boxes[i], boxes[j]);
            if (c == 0)
                c = centreDepth(boxes[i]) < centreDepth(boxes[j]) ? -1 : 1;
            if (c > 0)
                behind[i].push_back(j);
            else
                behind[j].push_back(i);
        }
    // Depth-first, without recursion: a box goes out once all those behind
    // it have. A box met again while its own are being visited closes a
    // cycle; that edge is dropped.
    std::vector<uint8_t> state(n, 0); // 0 not seen, 1 being visited, 2 out
    std::vector<size_t> order;
    order.reserve(n);
    std::vector<std::pair<size_t, size_t>> stack; // box, next of behind[box] to visit
    for (size_t start : byDepth) {
        if (state[start])
            continue;
        stack.push_back({start, 0});
        state[start] = 1;
        while (!stack.empty()) {
            auto &[box, next] = stack.back();
            if (next < behind[box].size()) {
                const size_t b = behind[box][next++];
                if (state[b] == 0) {
                    state[b] = 1;
                    stack.push_back({b, 0});
                }
                continue;
            }
            state[box] = 2;
            order.push_back(box);
            stack.pop_back();
        }
    }
    return order;
}

bool behind(const WorldBox &b, const WorldBox &a) {
    return b.x < a.x + a.sizeX && b.y < a.y + a.sizeY && b.z + b.zOff < a.z + a.zOff + a.height;
}

bool onBox(const WorldBox &box, int x, int y) {
    // The eight corners on the screen, x - y across and (x + y) / 2 less the
    // height down; their hull, round which the point must be on the inner
    // side of every edge.
    std::vector<std::array<long, 2>> p;
    const int z0 = box.z + box.zOff;
    for (int c = 0; c < 8; ++c) {
        const int wx = box.x + (c & 1 ? box.sizeX : 0);
        const int wy = box.y + (c & 2 ? box.sizeY : 0);
        const int wz = z0 + (c & 4 ? box.height : 0);
        p.push_back({wx - wy, floorDiv(wx + wy, 2) - wz});
    }
    std::sort(p.begin(), p.end());
    p.erase(std::unique(p.begin(), p.end()), p.end());
    const auto cross = [](const std::array<long, 2> &o, const std::array<long, 2> &a, const std::array<long, 2> &b) {
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
    };
    std::vector<std::array<long, 2>> hull(2 * p.size());
    size_t k = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], p[i]) <= 0)
            --k;
        hull[k++] = p[i];
    }
    for (size_t i = p.size() - 1, t = k + 1; i-- > 0;) {
        while (k >= t && cross(hull[k - 2], hull[k - 1], p[i]) <= 0)
            --k;
        hull[k++] = p[i];
    }
    if (k < 4) // flat on the screen: nothing to click
        return false;
    for (size_t i = 0; i + 1 < k; ++i)
        if (cross(hull[i], hull[i + 1], {x, y}) < 0)
            return false;
    return true;
}

std::vector<size_t> exeDepthOrder(const std::vector<WorldBox> &boxes, const std::vector<ScreenRect> &rects,
                                  const std::vector<std::array<int, 3>> &keys, int maxPasses) {
    const size_t n = boxes.size();
    // Inserted before the first with a greater y, then x, then height.
    std::vector<size_t> list(n);
    std::iota(list.begin(), list.end(), size_t(0));
    std::stable_sort(list.begin(), list.end(), [&](size_t a, size_t b) {
        return std::make_tuple(keys[a][1], keys[a][0], keys[a][2]) <
               std::make_tuple(keys[b][1], keys[b][0], keys[b][2]);
    });
    // Screen rectangles touching count as overlapping (Clue.exe's <= and >=).
    const auto touch = [&](size_t a, size_t b) {
        const ScreenRect &p = rects[a];
        const ScreenRect &q = rects[b];
        return p.x + p.w >= q.x && p.y + p.h >= q.y && p.x <= q.x + q.w && p.y <= q.y + q.h;
    };
    constexpr long kHead = -2;
    constexpr long kUnset = -1;
    std::vector<long> lastPrev(n, kUnset); // the guard: where each was moved from
    for (int pass = 0; pass < maxPasses; ++pass) {
        bool moved = false;
        for (size_t i = 0; i < list.size(); ++i) {
            for (size_t j = i + 1; j < list.size(); ++j) {
                const size_t a = list[i];
                const size_t b = list[j];
                if (!touch(a, b) || !behind(boxes[b], boxes[a]))
                    continue;
                const long prevA = i == 0 ? kHead : long(list[i - 1]);
                if (lastPrev[b] == prevA)
                    continue;
                lastPrev[a] = prevA;
                lastPrev[b] = long(list[j - 1]);
                list.erase(list.begin() + std::ptrdiff_t(j));
                list.insert(list.begin() + std::ptrdiff_t(i), b);
                moved = true;
                j = i; // on from the one moved, as Clue.exe goes on
            }
        }
        if (!moved)
            break;
    }
    return list;
}

const std::vector<Mansion::Frame> *Mansion::resolve(const GameData &data, const Animation &anim) {
    auto it = _frames.find(&anim);
    if (it != _frames.end())
        return &it->second;
    std::vector<Frame> frames(anim.frameCount());
    for (size_t f = 0; f < anim.frameCount(); ++f) {
        // Animations without an ISOA block stand centred on their anchor.
        int ax = -anim.width / 2;
        int ay = -anim.height;
        if (anim.isometric) {
            ax = anim.frames[f].anchorX;
            ay = anim.frames[f].anchorY;
        }
        for (size_t l = 0; l < anim.layers.size(); ++l) {
            const Animation::Part &part = anim.layers[l][f];
            if (part.image >= anim.images.size())
                continue;
            const Image *img = data.image(anim.images[part.image]);
            if (!img) {
                _problems.push_back(data.lastError());
                continue;
            }
            frames[f].push_back(
                {img, ax + part.x + img->offsetX, ay + part.y + img->offsetY, int(l), int(part.flags >> 16)});
        }
    }
    return &_frames.emplace(&anim, std::move(frames)).first->second;
}

Error Mansion::build(const GameData &data, const Level &level) {
    _level = &level;
    _items.clear();
    _states.clear();
    _order.clear();
    _problems.clear();
    _frames.clear();
    for (size_t i = 0; i < level.placements.size(); ++i) {
        const Placement &pl = level.placements[i];
        const std::string &name = level.objects[pl.object];
        Item item;
        item.placement = i;
        item.object = data.object(name);
        if (!item.object) {
            _problems.push_back(data.lastError());
            continue;
        }
        // The state it starts in (Clue.exe 0x446064): 0 in all the game's.
        const ObjectDef::State *state = item.object->state(uint16_t(std::max<int16_t>(0, item.object->startState)));
        if (!state && !item.object->states.empty())
            state = &item.object->states.front();
        if (!state) {
            _problems.push_back(name + " has no states");
            continue;
        }
        item.animation = data.animation(state->animation);
        if (!item.animation || item.animation->frameCount() == 0) {
            _problems.push_back(item.animation ? state->animation + " has no frames" : data.lastError());
            continue;
        }
        item.frames = resolve(data, *item.animation);
        item.layer = pl.layer;
        item.box.x = pl.x;
        item.box.y = pl.y;
        const Animation::Frame &f0 = item.animation->frames[0];
        // The box: from the anchor by the frame's offsets (bytes 10-13 of
        // its ISOA entry, as Clue.exe reads them at 0x449361).
        item.worldX = pl.x;
        item.worldY = pl.y;
        item.layers = int(item.animation->layers.size());
        item.box.x += int16_t(f0.unknown[3]);
        item.box.y += int16_t(f0.unknown[4]);
        item.box.zOff = f0.solid;
        item.box.sizeX = f0.sizeX;
        item.box.sizeY = f0.sizeY;
        item.box.height = f0.height;
        const bool tagged = pl.flags & 0x02;
        item.kind = tagged ? uint8_t(pl.tag & 0xff) : item.object->type;
        item.subKind = tagged ? uint8_t(pl.tag >> 8) : item.object->subType;
        const auto has2000 = [](const std::vector<Timing> &t) {
            return std::any_of(t.begin(), t.end(), [](const Timing &x) { return x.time == 2000; });
        };
        item.keepsSolid = has2000(pl.timings) || has2000(item.object->timings);
        item.flat = f0.solid < 0 || !item.animation->isometric;
        // Raised by its height, as Clue.exe places it on the screen (at
        // 0x4461c5): a door's lintel on its frame, the lamps and pictures
        // on the walls; most things 1, the floors -1.
        item.box.z = pl.z;
        item.anchorX = pl.x - pl.y;
        item.anchorY = floorDiv(pl.x + pl.y, 2) - item.box.z;
        // The rectangle every frame's pieces fall in.
        bool any = false;
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        for (const Frame &frame : *item.frames)
            for (const Piece &p : frame) {
                const int px = item.anchorX + p.dx;
                const int py = item.anchorY + p.dy;
                x0 = any ? std::min(x0, px) : px;
                y0 = any ? std::min(y0, py) : py;
                x1 = any ? std::max(x1, px + p.image->pixels.w) : px + p.image->pixels.w;
                y1 = any ? std::max(y1, py + p.image->pixels.h) : py + p.image->pixels.h;
                any = true;
            }
        item.rect = {x0, y0, x1 - x0, y1 - y0};
        _items.push_back(item);
    }
    if (_items.empty())
        return Error::fail("nothing in the level could be built");

    // Flat things first: the black outside (layer 6), under the floors'
    // front edges, then the floors by layer, each from the back. Then the
    // rest, sorted by their boxes.
    std::vector<size_t> flat;
    std::vector<size_t> solid;
    for (size_t i = 0; i < _items.size(); ++i)
        (_items[i].flat ? flat : solid).push_back(i);
    const auto flatRank = [](int layer) { return layer == 6 ? -1 : layer; };
    std::stable_sort(flat.begin(), flat.end(), [&](size_t a, size_t b) {
        const Item &x = _items[a];
        const Item &y = _items[b];
        if (flatRank(x.layer) != flatRank(y.layer))
            return flatRank(x.layer) < flatRank(y.layer);
        return x.anchorY < y.anchorY;
    });
    std::vector<WorldBox> boxes;
    std::vector<ScreenRect> rects;
    std::vector<std::array<int, 3>> keys;
    for (size_t i : solid) {
        boxes.push_back(_items[i].box);
        rects.push_back(_items[i].rect);
        keys.push_back({_items[i].worldX, _items[i].worldY, _items[i].box.z});
    }
    _order = flat;
    _flatCount = flat.size();
    for (size_t k : exeDepthOrder(boxes, rects, keys))
        _order.push_back(solid[k]);
    _orderOf.assign(_items.size(), 0);
    for (size_t n = 0; n < _order.size(); ++n)
        _orderOf[_order[n]] = n;

    _bounds = _items.front().rect;
    for (const Item &it : _items) {
        const int x1 = std::max(_bounds.x + _bounds.w, it.rect.x + it.rect.w);
        const int y1 = std::max(_bounds.y + _bounds.h, it.rect.y + it.rect.h);
        _bounds.x = std::min(_bounds.x, it.rect.x);
        _bounds.y = std::min(_bounds.y, it.rect.y);
        _bounds.w = x1 - _bounds.x;
        _bounds.h = y1 - _bounds.y;
    }
    return Error::success();
}

uint8_t shade(uint8_t c8, int level) {
    const int c = c8 >> 3;
    int r = c;
    switch (level) {
    case 5:
        r = c - (c >> 4);
        break;
    case 6:
        r = c - (c >> 3);
        break;
    case 7:
        r = c - (c >> 3) - (c >> 4);
        break;
    case 8:
        r = c - (c >> 2);
        break;
    case 9:
        r = c - (c >> 2) - (c >> 4);
        break;
    case 10:
        r = c - (c >> 2) - (c >> 3);
        break;
    case 11:
        r = (c >> 1) + (c >> 4);
        break;
    case 12:
        r = c >> 1;
        break;
    case 13:
        r = (c >> 1) - (c >> 4);
        break;
    case 14:
        r = (c >> 1) - (c >> 3);
        break;
    case 15:
        r = (c >> 2) + (c >> 4);
        break;
    case 16:
        r = c >> 2;
        break;
    case 17:
        r = (c >> 2) - (c >> 4);
        break;
    case 18:
        r = (c >> 2) - (c >> 3);
        break;
    case 19:
        r = c >> 4;
        break;
    default:
        return c8;
    }
    return uint8_t((r << 3) | (r >> 2));
}

int lightShade(int lx, int ly, int x, int y) {
    const int dx = std::abs(lx - x);
    const int dy = std::abs(ly - y);
    const int d = dx > dy ? dx + dy / 2 : dy + dx / 2;
    const int step = std::clamp(d / 16 - 4, 0, 11);
    return step == 0 ? kShadeNone : 5 + step;
}

namespace {

// image::blit through one of Clue.exe's shades: see-through is half the
// picture's 5-bit colours and half what is behind (its pixel function 3);
// 2 a quarter of the picture's and three of what is behind, 4 the other
// way round.
void blitShaded(Surface &dst, const image::Image &img, int x, int y, int level) {
    if (level == kShadeNone) {
        image::blit(dst, img, x, y);
        return;
    }
    for (int sy = 0; sy < img.h; ++sy) {
        const int dy = y + sy;
        if (dy < 0 || dy >= dst.h)
            continue;
        uint8_t *row = dst.row(dy);
        for (int sx = 0; sx < img.w; ++sx) {
            const int dx = x + sx;
            if (dx < 0 || dx >= dst.w)
                continue;
            const uint8_t *s = img.pixel(sx, sy);
            if (s[3] == 0)
                continue;
            uint8_t *d = row + dx * 4;
            for (int c = 0; c < 3; ++c) {
                int v;
                if (level >= 2 && level <= 4) {
                    const int s5 = s[c] >> 3;
                    const int d5 = d[c] >> 3;
                    const int r = level == 2   ? (d5 >> 1) + (d5 >> 2) + (s5 >> 2)
                                  : level == 3 ? (s5 >> 1) + (d5 >> 1)
                                               : (s5 >> 1) + (s5 >> 2) + (d5 >> 2);
                    v = (r << 3) | (r >> 2);
                } else
                    v = shade(s[c], level);
                d[c] = uint8_t((v * s[3] + d[c] * (255 - s[3])) / 255);
            }
            d[3] = 255;
        }
    }
}

} // namespace

bool Mansion::playState(const GameData &data, size_t item, uint16_t state, uint64_t startMs, bool loop) {
    if (item >= _items.size())
        return false;
    Item &it = _items[item];
    const ObjectDef::State *st = it.object->state(state);
    const Animation *a = st ? data.animation(st->animation) : nullptr;
    if (!a || a->frameCount() == 0)
        return false;
    Playing p;
    p.animation = a;
    p.frames = resolve(data, *a);
    p.start = startMs;
    p.loop = loop;
    _states[item] = p;
    // Its rectangle takes in the new frames, so it isn't left out of a view.
    for (const Frame &frame : *p.frames)
        for (const Piece &piece : frame) {
            const ScreenRect r{it.anchorX + piece.dx, it.anchorY + piece.dy, piece.image->pixels.w,
                               piece.image->pixels.h};
            const int x1 = std::max(it.rect.x + it.rect.w, r.x + r.w);
            const int y1 = std::max(it.rect.y + it.rect.h, r.y + r.h);
            it.rect.x = std::min(it.rect.x, r.x);
            it.rect.y = std::min(it.rect.y, r.y);
            it.rect.w = x1 - it.rect.x;
            it.rect.h = y1 - it.rect.y;
        }
    return true;
}

bool Mansion::playingState(size_t item, uint64_t nowMs) const {
    auto it = _states.find(item);
    if (it == _states.end())
        return false;
    const Playing &p = it->second;
    if (p.loop)
        return true;
    return nowMs < p.start || nowMs - p.start < p.animation->onceMs();
}

const std::vector<Mansion::Frame> &Mansion::framesAt(size_t index, uint64_t nowMs, const MansionLook &look,
                                                     size_t &frame) const {
    const Item &item = _items[index];
    frame = 0;
    if (auto it = _states.find(index); it != _states.end() && playingState(index, nowMs)) {
        const Playing &p = it->second;
        // A state played once (a reaction) goes through once; looping (the
        // drapes) by its own flags.
        const uint64_t t = nowMs - std::min(nowMs, p.start);
        frame = p.loop ? p.animation->frameAt(t, true) % p.frames->size()
                       : std::min(p.frames->size() - 1, size_t(t / uint64_t(p.animation->onceMs() / p.frames->size())));
        return *p.frames;
    }
    const auto &frames = *item.frames;
    if (look.animate && frames.size() > 1)
        frame = item.animation->frameAt(nowMs, true) % frames.size();
    return frames;
}

int Mansion::itemAt(int x, int y, const MansionLook &look, uint64_t nowMs) const {
    for (size_t n = _order.size(); n-- > 0;) {
        const size_t i = _order[n];
        const Item &item = _items[i];
        // The near walls, see-through, don't stop the pointer.
        if (item.flat || !shown(item, look) || !item.rect.contains(x, y) ||
            (item.layer == 4 && look.nearWalls != MansionLook::NearWalls::kSolid))
            continue;
        size_t f = 0;
        const auto &frames = framesAt(i, nowMs, look, f);
        for (const Piece &p : frames[f]) {
            const int px = x - item.anchorX - p.dx;
            const int py = y - item.anchorY - p.dy;
            if (px >= 0 && py >= 0 && px < p.image->pixels.w && py < p.image->pixels.h &&
                p.image->pixels.pixel(px, py)[3] > 0)
                return int(i);
        }
    }
    return -1;
}

bool Mansion::shown(const Item &item, const MansionLook &look) const {
    if (item.object->type == ObjectDef::kTileNumber)
        return look.tileNumbers;
    if (item.object->type == ObjectDef::kCharacter)
        return look.characters;
    // "Mansion Walls" off: Clue.exe's walls don't draw themselves, nor its
    // furniture of the secondary type 1 (the lamps on the walls).
    if (!look.walls && (item.kind == 0 || (item.kind == 3 && item.subKind == 1)))
        return false;
    if (item.layer == 4)
        return look.nearWalls != MansionLook::NearWalls::kHidden;
    if (item.layer == 6)
        return look.outside;
    return true;
}

namespace {

ScreenRect actorRect(const Mansion::Actor &a) {
    const int ax = a.box.x - a.box.y;
    const int ay = floorDiv(a.box.x + a.box.y, 2);
    ScreenRect r{ax, ay, 0, 0};
    bool any = false;
    for (const Mansion::Piece &p : *a.frame) {
        const ScreenRect q{ax + p.dx, ay + p.dy, p.image->pixels.w, p.image->pixels.h};
        if (!any)
            r = q;
        else {
            const int x1 = std::max(r.x + r.w, q.x + q.w);
            const int y1 = std::max(r.y + r.h, q.y + q.h);
            r.x = std::min(r.x, q.x);
            r.y = std::min(r.y, q.y);
            r.w = x1 - r.x;
            r.h = y1 - r.y;
        }
        any = true;
    }
    return r;
}

} // namespace

namespace {

// Clue.exe's test of what stands in front of an actor (at 0x428190, with 13
// for its reach): the object's box on the floor, less 8 on its far sides,
// overlapping the actor's anchor and the 208 units beyond it along x and y.
constexpr int kRevealReach = 13 * 16, kRevealSlack = 8;

bool inFrontOf(const Mansion::Item &item, const Mansion::Actor &a) {
    // Which of Clue.exe's kinds of object it does this to (their classes'
    // methods at slots 0x68 and 0x6c): walls (0), secret passages (8) and
    // drapes (12); furniture (3) without a timing at 2000; the hot spots
    // (4, 5) only from the corridors. In a room, only the walls.
    switch (item.kind) {
    case 0:
    case 8:
    case 12:
        break;
    case 3:
        if (item.keepsSolid)
            return false;
        break;
    case 4:
    case 5:
        if (!a.inCorridor)
            return false;
        break;
    default:
        return false;
    }
    if (a.inRoom && item.object->type != ObjectDef::kWall)
        return false;
    const WorldBox &b = item.box;
    return b.x <= a.box.x + kRevealReach && b.x + b.sizeX - kRevealSlack > a.box.x && b.y <= a.box.y + kRevealReach &&
           b.y + b.sizeY - kRevealSlack > a.box.y;
}

} // namespace

size_t Mansion::insertionPoint(const Actor &actor) const {
    if (actor.flat)
        return _flatCount;
    // After everything it is in front of, before everything in front of it
    // (of what it overlaps on screen); when the two can't both be had, the
    // things in front of it win.
    const ScreenRect rect = actorRect(actor);
    size_t after = _flatCount;
    size_t before = _order.size();
    for (size_t n = _flatCount; n < _order.size(); ++n) {
        const Item &item = _items[_order[n]];
        if (!item.rect.overlaps(rect) || item.object->type == ObjectDef::kCharacter)
            continue;
        // Clue.exe's test both ways; where it can't tell (or both hold),
        // its first order: by y, then x.
        const bool itemBehind = behind(item.box, actor.box);
        const bool actorBehind = behind(actor.box, item.box);
        int c = itemBehind && !actorBehind ? 1 : actorBehind && !itemBehind ? -1 : 0;
        if (c == 0)
            c = std::make_pair(actor.box.y, actor.box.x) < std::make_pair(item.worldY, item.worldX) ? -1 : 1;
        if (c > 0)
            after = std::max(after, n + 1);
        else
            before = std::min(before, n);
    }
    return std::min(after, before);
}

namespace {

// Clue.exe's division by a power of two, towards 0 (its grid cells).
int cellOf(int v) {
    return (v + (v < 0 ? 127 : 0)) >> 7;
}

// The light an object keeps: the nearest of those that reached it.
struct Lit {
    bool any = false;
    int x = 0, y = 0;
    void offer(int lx, int ly, int px, int py) {
        const long long d = 1LL * (lx - px) * (lx - px) + 1LL * (ly - py) * (ly - py);
        if (!any || d < 1LL * (x - px) * (x - px) + 1LL * (y - py) * (y - py)) {
            any = true;
            x = lx;
            y = ly;
        }
    }
    int at(int px, int py) const { return any ? lightShade(x, y, px, py) : kShadeUnlit; }
};

// Whether a revealing actor reaches something anchored at (x, y) (Clue.exe
// at 0x428220): the 128-unit cells within 240 of it, and one more round.
bool reaches(const Mansion::Actor &a, int x, int y) {
    constexpr int kReach = (13 + 2) * 16;
    return cellOf(x) >= cellOf(a.box.x - kReach) - 1 && cellOf(x) <= cellOf(a.box.x + kReach) + 1 &&
           cellOf(y) >= cellOf(a.box.y - kReach) - 1 && cellOf(y) <= cellOf(a.box.y + kReach) + 1;
}

} // namespace

void Mansion::draw(Surface &target, int camX, int camY, const MansionLook &look, uint64_t nowMs,
                   const std::vector<Actor> &actors) const {
    const ScreenRect view{camX, camY, target.w, target.h};
    // The actors in the order, each before the item it goes before; those
    // at the same point back to front.
    std::vector<std::pair<size_t, const Actor *>> placed;
    std::vector<const Actor *> revealing;
    for (const Actor &a : actors) {
        if (a.frame && actorRect(a).overlaps(view))
            placed.push_back({insertionPoint(a), &a});
        if (a.reveal)
            revealing.push_back(&a);
    }
    const bool fades = look.inFront != MansionLook::InFront::kSolid;
    // Lighting, as Clue.exe's (0x428220 with Lighting Effects on): each
    // revealing actor gives its light, from the middle of its box, to what
    // it reaches and doesn't show through; each thing keeps the nearest.
    const auto lightFor = [&](int x, int y, int px, int py, const Item *item) {
        Lit lit;
        for (const Actor *a : revealing)
            if (reaches(*a, x, y) && !(item && fades && !item->flat && inFrontOf(*item, *a)))
                lit.offer(a->box.x + a->box.sizeX / 2, a->box.y + a->box.sizeY / 2, px, py);
        return lit;
    };
    std::stable_sort(placed.begin(), placed.end(), [](const auto &x, const auto &y) {
        return x.first != y.first ? x.first < y.first : centreDepth(x.second->box) < centreDepth(y.second->box);
    });
    size_t next = 0;
    const auto drawActors = [&](size_t upTo) {
        for (; next < placed.size() && placed[next].first <= upTo; ++next) {
            const Actor &a = *placed[next].second;
            const int ax = a.box.x - a.box.y - camX;
            const int ay = floorDiv(a.box.x + a.box.y, 2) - camY;
            int level = kShadeNone;
            if (look.lighting) {
                const int px = a.box.x + a.box.sizeX / 2;
                const int py = a.box.y + a.box.sizeY / 2;
                level = lightFor(a.box.x, a.box.y, px, py, nullptr).at(px, py);
            }
            for (const Piece &p : *a.frame)
                if (a.opacity == 255)
                    blitShaded(target, p.image->pixels, ax + p.dx, ay + p.dy, p.blend ? p.blend : level);
                else
                    image::blit(target, p.image->pixels, ax + p.dx, ay + p.dy, a.opacity);
        }
    };
    for (size_t n = 0; n < _order.size(); ++n) {
        drawActors(n);
        const Item &item = _items[_order[n]];
        if (!shown(item, look) || !item.rect.overlaps(view))
            continue;
        size_t f = 0;
        const auto &frames = framesAt(_order[n], nowMs, look, f);
        // In front of the suspect whose turn it is or a move's number: see-
        // through, or left out, the whole piece.
        bool seeThrough = false;
        if (fades && !item.flat &&
            std::any_of(revealing.begin(), revealing.end(), [&](const Actor *a) { return inFrontOf(item, *a); })) {
            if (look.inFront == MansionLook::InFront::kHidden)
                continue;
            seeThrough = true;
        }
        if (item.layer == 4 && look.nearWalls == MansionLook::NearWalls::kSeeThrough)
            seeThrough = true;
        const int x0 = item.anchorX - camX;
        const int y0 = item.anchorY - camY;
        if (seeThrough || !look.lighting) {
            for (const Piece &p : frames[f])
                blitShaded(target, p.image->pixels, x0 + p.dx, y0 + p.dy,
                           p.blend      ? p.blend
                           : seeThrough ? kShadeSeeThrough
                                        : kShadeNone);
            continue;
        }
        // Its light, at the middle of its box (from the placement, as
        // Clue.exe measures it, 0x428980); lit walls a shade for each
        // 16-unit slice (0x42B5D0), lit floors of 4 x 4 tiles one a tile
        // (0x42AF70; lit floors of other sizes it doesn't draw).
        const int wx = item.worldX;
        const int wy = item.worldY;
        const int sx = item.box.sizeX;
        const int sy = item.box.sizeY;
        const Lit lit = lightFor(wx, wy, wx + sx / 2, wy + sy / 2, &item);
        const bool sliced = lit.any && item.layers != 1;
        if (sliced && item.kind == 1 && item.layers != 16)
            continue;
        for (const Piece &p : frames[f]) {
            int px = wx + sx / 2;
            int py = wy + sy / 2;
            if (sliced && item.kind == 0) {
                if (sx >= sy) {
                    px = wx + 16 * p.layer + 8;
                    py = wy + 8;
                } else {
                    px = wx + 8;
                    py = wy + 16 * (item.layers - p.layer) - 8;
                }
            } else if (sliced && item.kind == 1) {
                px = wx + 16 * (p.layer % 4) + 8;
                py = wy + 16 * (p.layer / 4) + 8;
            }
            blitShaded(target, p.image->pixels, x0 + p.dx, y0 + p.dy, p.blend ? p.blend : lit.at(px, py));
        }
    }
    drawActors(_order.size());
}

} // namespace reluva::clue
