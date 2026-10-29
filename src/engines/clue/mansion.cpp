// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/mansion.h"

#include <algorithm>
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
    for (size_t k : depthOrder(boxes, rects))
        order_.push_back(solid[k]);

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

} // namespace reluva::clue
