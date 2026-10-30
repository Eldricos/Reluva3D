// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/mansion.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace reluva::clue {

namespace {

int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// How deep a box's centre is: further from the camera is smaller.
int centreDepth(const WorldBox& b) { return 2 * (b.x + b.y) + b.sizeX + b.sizeY; }

} // namespace

int compareDepth(const WorldBox& a, const WorldBox& b) {
    if (a.x >= b.x + b.sizeX)
        return 1;
    if (b.x >= a.x + a.sizeX)
        return -1;
    if (a.y >= b.y + b.sizeY)
        return 1;
    if (b.y >= a.y + a.sizeY)
        return -1;
    return 0;
}

std::vector<size_t> depthOrder(const std::vector<WorldBox>& boxes, const std::vector<ScreenRect>& rects) {
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
            auto& [box, next] = stack.back();
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

const std::vector<Mansion::Frame>* Mansion::resolve(const GameData& data, const Animation& anim) {
    auto it = frames_.find(&anim);
    if (it != frames_.end())
        return &it->second;
    std::vector<Frame> frames(anim.frameCount());
    for (size_t f = 0; f < anim.frameCount(); ++f) {
        // Animations without an ISOA block stand centred on their anchor.
        int ax = -anim.width / 2, ay = -anim.height;
        if (anim.isometric) {
            ax = anim.frames[f].anchorX;
            ay = anim.frames[f].anchorY;
        }
        for (const auto& layer : anim.layers) {
            const Animation::Part& part = layer[f];
            if (part.image >= anim.images.size())
                continue;
            const Image* img = data.image(anim.images[part.image]);
            if (!img) {
                problems_.push_back(data.lastError());
                continue;
            }
            frames[f].push_back({img, ax + part.x + img->offsetX, ay + part.y + img->offsetY});
        }
    }
    return &frames_.emplace(&anim, std::move(frames)).first->second;
}

Error Mansion::build(const GameData& data, const Level& level) {
    level_ = &level;
    items_.clear();
    order_.clear();
    problems_.clear();
    frames_.clear();
    for (size_t i = 0; i < level.placements.size(); ++i) {
        const Placement& pl = level.placements[i];
        const std::string& name = level.objects[pl.object];
        Item item;
        item.placement = i;
        item.object = data.object(name);
        if (!item.object) {
            problems_.push_back(data.lastError());
            continue;
        }
        const ObjectDef::State* state = item.object->state(0);
        if (!state && !item.object->states.empty())
            state = &item.object->states.front();
        if (!state) {
            problems_.push_back(name + " has no states");
            continue;
        }
        item.animation = data.animation(state->animation);
        if (!item.animation || item.animation->frameCount() == 0) {
            problems_.push_back(item.animation ? state->animation + " has no frames" : data.lastError());
            continue;
        }
        item.frames = resolve(data, *item.animation);
        item.layer = pl.layer;
        item.box.x = pl.x;
        item.box.y = pl.y;
        const Animation::Frame& f0 = item.animation->frames[0];
        item.box.sizeX = f0.sizeX;
        item.box.sizeY = f0.sizeY;
        item.box.height = f0.height;
        item.flat = f0.solid < 0 || !item.animation->isometric;
        item.anchorX = pl.x - pl.y;
        item.anchorY = floorDiv(pl.x + pl.y, 2);
        // The rectangle every frame's pieces fall in.
        bool any = false;
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        for (const Frame& frame : *item.frames)
            for (const Piece& p : frame) {
                const int px = item.anchorX + p.dx, py = item.anchorY + p.dy;
                x0 = any ? std::min(x0, px) : px;
                y0 = any ? std::min(y0, py) : py;
                x1 = any ? std::max(x1, px + p.image->pixels.w) : px + p.image->pixels.w;
                y1 = any ? std::max(y1, py + p.image->pixels.h) : py + p.image->pixels.h;
                any = true;
            }
        item.rect = {x0, y0, x1 - x0, y1 - y0};
        items_.push_back(item);
    }
    if (items_.empty())
        return Error::fail("nothing in the level could be built");

    // Flat things first: the black outside (layer 6), under the floors'
    // front edges, then the floors by layer, each from the back. Then the
    // rest, sorted by their boxes.
    std::vector<size_t> flat, solid;
    for (size_t i = 0; i < items_.size(); ++i)
        (items_[i].flat ? flat : solid).push_back(i);
    const auto flatRank = [](int layer) { return layer == 6 ? -1 : layer; };
    std::stable_sort(flat.begin(), flat.end(), [&](size_t a, size_t b) {
        const Item& x = items_[a];
        const Item& y = items_[b];
        if (flatRank(x.layer) != flatRank(y.layer))
            return flatRank(x.layer) < flatRank(y.layer);
        return x.anchorY < y.anchorY;
    });
    std::vector<WorldBox> boxes;
    std::vector<ScreenRect> rects;
    for (size_t i : solid) {
        boxes.push_back(items_[i].box);
        rects.push_back(items_[i].rect);
    }
    order_ = flat;
    flatCount_ = flat.size();
    for (size_t k : depthOrder(boxes, rects))
        order_.push_back(solid[k]);
    orderOf_.assign(items_.size(), 0);
    for (size_t n = 0; n < order_.size(); ++n)
        orderOf_[order_[n]] = n;

    bounds_ = items_.front().rect;
    for (const Item& it : items_) {
        const int x1 = std::max(bounds_.x + bounds_.w, it.rect.x + it.rect.w);
        const int y1 = std::max(bounds_.y + bounds_.h, it.rect.y + it.rect.h);
        bounds_.x = std::min(bounds_.x, it.rect.x);
        bounds_.y = std::min(bounds_.y, it.rect.y);
        bounds_.w = x1 - bounds_.x;
        bounds_.h = y1 - bounds_.y;
    }
    return Error::success();
}

int lightLevel(int x, int y, const std::vector<std::pair<int, int>>& lights) {
    // Measured on the original with Lighting Effects on and off (the same
    // view): full within 30 of a light, then down over 230 to 22%. The
    // distance is on screen: the floor's x - y and (x + y) / 2.
    constexpr double kFull = 30, kFade = 230, kAmbient = 0.22;
    double best = kAmbient;
    for (const auto& [lx, ly] : lights) {
        const int dx = x - lx, dy = y - ly;
        const double d = std::hypot(double(dx - dy), (dx + dy) / 2.0);
        best = std::max(best, kAmbient + (1 - kAmbient) * std::clamp(1 - (d - kFull) / kFade, 0.0, 1.0));
    }
    return int(best * 256);
}

namespace {

// image::blit, the colours scaled by level / 256.
void blitLit(Surface& dst, const image::Image& img, int x, int y, int opacity, int level) {
    if (level >= 256) {
        image::blit(dst, img, x, y, opacity);
        return;
    }
    for (int sy = 0; sy < img.h; ++sy) {
        const int dy = y + sy;
        if (dy < 0 || dy >= dst.h)
            continue;
        uint8_t* row = dst.row(dy);
        for (int sx = 0; sx < img.w; ++sx) {
            const int dx = x + sx;
            if (dx < 0 || dx >= dst.w)
                continue;
            const uint8_t* s = img.pixel(sx, sy);
            const int a = opacity == 255 ? s[3] : s[3] * opacity / 255;
            if (a == 0)
                continue;
            uint8_t* d = row + dx * 4;
            for (int c = 0; c < 3; ++c)
                d[c] = static_cast<uint8_t>(((s[c] * level >> 8) * a + d[c] * (255 - a)) / 255);
            d[3] = 255;
        }
    }
}

} // namespace

bool Mansion::shown(const Item& item, const MansionLook& look) const {
    if (item.object->type == ObjectDef::TileNumber)
        return look.tileNumbers;
    if (item.object->type == ObjectDef::Character)
        return look.characters;
    if (item.layer == 4)
        return look.walls != MansionLook::Walls::Hidden;
    if (item.layer == 6)
        return look.outside;
    return true;
}

namespace {

ScreenRect actorRect(const Mansion::Actor& a) {
    const int ax = a.box.x - a.box.y, ay = floorDiv(a.box.x + a.box.y, 2);
    ScreenRect r{ax, ay, 0, 0};
    bool any = false;
    for (const Mansion::Piece& p : *a.frame) {
        const ScreenRect q{ax + p.dx, ay + p.dy, p.image->pixels.w, p.image->pixels.h};
        if (!any)
            r = q;
        else {
            const int x1 = std::max(r.x + r.w, q.x + q.w), y1 = std::max(r.y + r.h, q.y + q.h);
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

size_t Mansion::insertionPoint(const Actor& actor) const {
    if (actor.flat)
        return flatCount_;
    // After everything it is in front of, before everything in front of it
    // (of what it overlaps on screen); when the two can't both be had, the
    // things in front of it win.
    const ScreenRect rect = actorRect(actor);
    size_t after = flatCount_, before = order_.size();
    for (size_t n = flatCount_; n < order_.size(); ++n) {
        const Item& item = items_[order_[n]];
        if (!item.rect.overlaps(rect) || item.object->type == ObjectDef::Character)
            continue;
        int c = compareDepth(actor.box, item.box);
        if (c == 0)
            c = centreDepth(actor.box) < centreDepth(item.box) ? -1 : 1;
        if (c > 0)
            after = std::max(after, n + 1);
        else
            before = std::min(before, n);
    }
    return std::min(after, before);
}

void Mansion::draw(Surface& target, int camX, int camY, const MansionLook& look, uint64_t nowMs,
                   const std::vector<Actor>& actors) const {
    const ScreenRect view{camX, camY, target.w, target.h};
    // The actors in the order, each before the item it goes before; those
    // at the same point back to front.
    std::vector<std::pair<size_t, const Actor*>> placed;
    std::vector<std::pair<size_t, ScreenRect>> revealed;
    for (const Actor& a : actors)
        if (a.frame && actorRect(a).overlaps(view)) {
            placed.push_back({insertionPoint(a), &a});
            if (a.reveal && look.walls != MansionLook::Walls::Solid)
                revealed.push_back({placed.back().first, actorRect(a)});
        }
    std::stable_sort(placed.begin(), placed.end(), [](const auto& x, const auto& y) {
        return x.first != y.first ? x.first < y.first : centreDepth(x.second->box) < centreDepth(y.second->box);
    });
    size_t next = 0;
    const auto drawActors = [&](size_t upTo) {
        for (; next < placed.size() && placed[next].first <= upTo; ++next) {
            const Actor& a = *placed[next].second;
            const int ax = a.box.x - a.box.y - camX, ay = floorDiv(a.box.x + a.box.y, 2) - camY;
            for (const Piece& p : *a.frame)
                image::blit(target, p.image->pixels, ax + p.dx, ay + p.dy, a.opacity);
        }
    };
    for (size_t n = 0; n < order_.size(); ++n) {
        drawActors(n);
        const Item& item = items_[order_[n]];
        if (!shown(item, look) || !item.rect.overlaps(view))
            continue;
        const auto& frames = *item.frames;
        size_t f = 0;
        if (look.animate && frames.size() > 1)
            f = size_t(nowMs * uint64_t(item.animation->framesPerSecond) / 1000) % frames.size();
        int opacity = item.layer == 4 && look.walls == MansionLook::Walls::SeeThrough ? look.seeThrough : 255;
        // Over a suspect: see-through.
        if (!item.flat)
            for (const auto& [at, rect] : revealed)
                if (at <= n && item.rect.overlaps(rect))
                    opacity = std::min(opacity, look.seeThrough);
        const int level = look.lighting ? lightLevel(item.box.x + item.box.sizeX / 2,
                                                    item.box.y + item.box.sizeY / 2, look.lights)
                                        : 256;
        for (const Piece& p : frames[f])
            blitLit(target, p.image->pixels, item.anchorX + p.dx - camX, item.anchorY + p.dy - camY, opacity, level);
    }
    drawActors(order_.size());
}

} // namespace reluva::clue
