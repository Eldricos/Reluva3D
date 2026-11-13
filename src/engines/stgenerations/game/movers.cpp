// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/game/movers.h"

#include <algorithm>
#include <cmath>

#include "common/serializer.h"
#include "engines/stgenerations/game/walker.h"

namespace reluva::stgenerations {

namespace {

const double kPi = 3.14159265358979323846;
// How close a moving part may come to the player (0x4B9180).
const double kClearance = 1.0;
const uint16_t kMoverObject = 0x8000;

double fromFixed(int32_t v) {
    return v / 65536.0;
}

int32_t toFixed(double v) {
    return (int32_t)std::lround(v * 65536.0);
}

// From (px, py) to the segment, in the plane.
double distanceToSegment(double px, double py, double x1, double y1, double x2, double y2) {
    double dx = x2 - x1;
    double dy = y2 - y1;
    double length2 = dx * dx + dy * dy;
    double t = length2 > 0 ? std::max(0.0, std::min(1.0, ((px - x1) * dx + (py - y1) * dy) / length2)) : 0;
    double cx = x1 + t * dx - px;
    double cy = y1 + t * dy - py;
    return std::sqrt(cx * cx + cy * cy);
}

bool insidePolygon(const LevelFile &level, const LevelFile::Sector &flat, double x, double y) {
    const std::vector<LevelFile::Vertex> &vertices = level.getVertices();
    bool inside = false;
    size_t n = flat.vertices.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        double xi = fromFixed(vertices[flat.vertices[i]].x);
        double yi = fromFixed(vertices[flat.vertices[i]].y);
        double xj = fromFixed(vertices[flat.vertices[j]].x);
        double yj = fromFixed(vertices[flat.vertices[j]].y);
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
            inside = !inside;
    }
    return inside;
}

} // namespace

Movers::Movers(LevelFile &level) : _level(level) {
    // 0x4B7CA0: leaders first, each id one mover...
    const std::vector<LevelFile::Wall> &walls = level.getWalls();
    for (int i = 0; i < (int)walls.size(); i++) {
        const LevelFile::Wall &wall = walls[i];
        if (!wall.moverId || (wall.flags & LevelFile::kFlagMoverFollower))
            continue;
        uint16_t id = (uint16_t)wall.moverId;
        auto it = _byId.find(id);
        if (it == _byId.end()) {
            it = _byId.insert({id, (int)_movers.size()}).first;
            _movers.push_back(Mover());
            _movers.back().id = id;
        }
        Mover &mover = _movers[it->second];
        if (wall.moverSpeed)
            mover.rate = (uint16_t)(wall.moverSpeed * 64 / 100);
        if (wall.moverWait) {
            mover.wait = wall.moverWait;
            mover.flags |= kFlagAutoClose;
        }
        if (wall.flags & LevelFile::kWallMoverReverses)
            mover.flags |= kFlagReverses;
        Leader leader;
        leader.part = makePart(true, i, wall.moverMode);
        mover.leaders.push_back(leader);
    }
    // ...then followers, to the leader of the same mover with their mode.
    auto attach = [this](uint16_t id, const Part &part) {
        Mover *mover = find(id);
        if (!mover)
            return;
        for (Leader &leader : mover->leaders) {
            if (leader.part.mode == part.mode) {
                leader.followers.push_back(part);
                return;
            }
        }
    };
    for (int i = 0; i < (int)walls.size(); i++) {
        const LevelFile::Wall &wall = walls[i];
        if (wall.moverId && (wall.flags & LevelFile::kFlagMoverFollower))
            attach((uint16_t)wall.moverId, makePart(true, i, wall.moverMode));
    }
    const std::vector<LevelFile::Sector> &flats = level.getSectors();
    for (int i = 0; i < (int)flats.size(); i++) {
        const LevelFile::Sector &flat = flats[i];
        if (flat.moverId && (flat.flags & LevelFile::kFlagMoverFollower))
            attach((uint16_t)flat.moverId, makePart(false, i, flat.moverMode));
    }
}

Movers::Part Movers::makePart(bool wall, int index, int mode) const {
    Part part = {};
    part.wall = wall;
    part.index = index;
    part.mode = mode;
    if (wall) {
        const LevelFile::Wall &w = _level.getWalls()[index];
        const std::vector<LevelFile::Vertex> &vertices = _level.getVertices();
        part.x1 = vertices[w.vertex1].x;
        part.y1 = vertices[w.vertex1].y;
        part.x2 = vertices[w.vertex2].x;
        part.y2 = vertices[w.vertex2].y;
        part.top = w.top;
        part.bottom = w.bottom;
        part.vStart = w.vStart;
        part.vLength = w.vLength;
    } else {
        part.height = _level.getSectors()[index].height;
    }
    return part;
}

Movers::Mover *Movers::find(uint16_t id) {
    auto it = _byId.find(id & 0x1FFF);
    return it == _byId.end() ? nullptr : &_movers[it->second];
}

const Movers::Mover *Movers::find(uint16_t id) const {
    auto it = _byId.find(id & 0x1FFF);
    return it == _byId.end() ? nullptr : &_movers[it->second];
}

int Movers::getState(uint16_t id) const {
    const Mover *mover = find(id);
    return mover ? mover->state : kIdle;
}

int Movers::getPosition(uint16_t id) const {
    const Mover *mover = find(id);
    return mover ? mover->position : 0;
}

bool Movers::apply(Mover &mover, int position, Walker &player, std::vector<int> &moved, bool check) {
    // 0x4B85B0: every part from its original geometry, f from -1 to 1.
    double f = position / (double)kOpenPosition;
    struct WallPlace {
        int index;
        double x1, y1, x2, y2;
        int32_t top, bottom, vStart, vLength;
        bool turned;
    };
    struct FlatPlace {
        int index;
        int32_t height;
    };
    std::vector<WallPlace> wallPlaces;
    std::vector<FlatPlace> flatPlaces;

    for (const Leader &leader : mover.leaders) {
        const Part &l = leader.part;
        double ax = fromFixed(l.x1);
        double ay = fromFixed(l.y1);
        double bx = fromFixed(l.x2);
        double by = fromFixed(l.y2);
        double h = fromFixed(l.top) - fromFixed(l.bottom);
        double theta = f * kPi / 2;
        auto place = [&](const Part &p, bool isLeader) {
            if (!p.wall) {
                // 0x4B8660: flats only rise or sink with the door.
                int32_t height = p.height;
                if (p.mode == 6)
                    height = p.height + toFixed(f * h);
                else if (p.mode == 7)
                    height = p.height - toFixed(f * h);
                flatPlaces.push_back({p.index, height});
                return;
            }
            // 0x4B8810, by the part's own mode.
            WallPlace w = {p.index, fromFixed(p.x1), fromFixed(p.y1), fromFixed(p.x2), fromFixed(p.y2),
                           p.top,   p.bottom,        p.vStart,        p.vLength,       false};
            auto rotate = [&](double cx, double cy, double angle) {
                double c = std::cos(angle);
                double s = std::sin(angle);
                double x1 = w.x1 - cx;
                double y1 = w.y1 - cy;
                double x2 = w.x2 - cx;
                double y2 = w.y2 - cy;
                w.x1 = cx + x1 * c - y1 * s;
                w.y1 = cy + x1 * s + y1 * c;
                w.x2 = cx + x2 * c - y2 * s;
                w.y2 = cy + x2 * s + y2 * c;
                w.turned = true;
            };
            switch (p.mode) {
            case 0:
            case 1: {
                double sign = p.mode == 0 ? -f : f;
                w.x1 += sign * (bx - ax);
                w.y1 += sign * (by - ay);
                w.x2 += sign * (bx - ax);
                w.y2 += sign * (by - ay);
                w.turned = true;
                break;
            }
            case 2:
                rotate(ax, ay, theta);
                break;
            case 3:
                rotate(ax, ay, -theta);
                break;
            case 4:
                rotate(bx, by, -theta);
                break;
            case 5:
                rotate(bx, by, theta);
                break;
            case 6:
                // Slides up: the leader's bottom rises into its top, its
                // texture cut short; a follower (a panel on the door, say)
                // rides up whole, top and bottom (0x4B8810).
                w.bottom = p.bottom + toFixed(f * h);
                if (isLeader)
                    w.vLength = (int32_t)std::lround(p.vLength * (1 - f));
                else
                    w.top = p.top + toFixed(f * h);
                break;
            case 7:
                // Slides down: the leader's top sinks, its texture with it;
                // a follower sinks whole.
                w.top = p.top - toFixed(f * h);
                if (isLeader) {
                    w.vStart = p.vStart + (int32_t)std::lround(f * p.vLength);
                    w.vLength = (int32_t)std::lround(p.vLength * (1 - f));
                } else {
                    w.bottom = p.bottom - toFixed(f * h);
                }
                break;
            default:
                break;
            }
            wallPlaces.push_back(w);
        };
        place(l, true);
        for (const Part &p : leader.followers)
            place(p, false);
    }

    if (check) {
        // 0x4B9180: no part may come nearer the player once within 1 unit;
        // 0x4B8760: no ceiling may come down on the player's head.
        double px = player.getX();
        double py = player.getY();
        double feet = player.getFeet();
        double head = player.getEye();
        const std::vector<LevelFile::Vertex> &vertices = _level.getVertices();
        auto distance = [&](double x1, double y1, double x2, double y2, int32_t top, int32_t bottom) {
            double planar = distanceToSegment(px, py, x1, y1, x2, y2);
            double dz = std::max({0.0, fromFixed(bottom) - head, feet - fromFixed(top)});
            return std::sqrt(planar * planar + dz * dz);
        };
        for (const WallPlace &w : wallPlaces) {
            const LevelFile::Wall &wall = _level.getWalls()[w.index];
            double before = distance(fromFixed(vertices[wall.vertex1].x), fromFixed(vertices[wall.vertex1].y),
                                     fromFixed(vertices[wall.vertex2].x), fromFixed(vertices[wall.vertex2].y), wall.top,
                                     wall.bottom);
            double after = distance(w.x1, w.y1, w.x2, w.y2, w.top, w.bottom);
            if (after < before && after < kClearance)
                return false;
        }
        for (const FlatPlace &p : flatPlaces) {
            const LevelFile::Sector &flat = _level.getSectors()[p.index];
            if (!flat.isCeiling() || p.height >= flat.height)
                continue;
            if (fromFixed(p.height) < head && fromFixed(flat.height) >= head && insidePolygon(_level, flat, px, py))
                return false;
        }
    }

    std::vector<LevelFile::Wall> &walls = _level.getWalls();
    std::vector<LevelFile::Vertex> &vertices = _level.getVertices();
    for (const WallPlace &w : wallPlaces) {
        LevelFile::Wall &wall = walls[w.index];
        vertices[wall.vertex1].x = toFixed(w.x1);
        vertices[wall.vertex1].y = toFixed(w.y1);
        vertices[wall.vertex2].x = toFixed(w.x2);
        vertices[wall.vertex2].y = toFixed(w.y2);
        wall.top = w.top;
        wall.bottom = w.bottom;
        wall.vStart = w.vStart;
        wall.vLength = w.vLength;
        if (w.turned)
            _level.updateWallGeometry(w.index);
        moved.push_back(w.index);
    }
    std::vector<LevelFile::Sector> &flats = _level.getSectors();
    for (const FlatPlace &p : flatPlaces) {
        if (flats[p.index].height == p.height)
            continue;
        bool riding = check && player.getFloor() == p.index && !flats[p.index].isCeiling();
        flats[p.index].height = p.height;
        // The player rides a floor that moves (0x4B68A0).
        if (riding)
            player.place(player.getX(), player.getY(), fromFixed(p.height));
    }
    return true;
}

void Movers::changed(Mover &mover, std::vector<Notice> &notices) {
    if (mover.state == mover.lastState)
        return;
    mover.lastState = mover.state;
    mover.counter = 0;
    uint16_t object = (uint16_t)(kMoverObject | mover.id);
    switch (mover.state) {
    case kOpening:
    case kOpeningBack:
        notices.push_back({object, kEventOpening});
        break;
    case kOpen:
    case kOpenBack:
        notices.push_back({object, kEventOpen});
        break;
    case kClosing:
    case kClosingBack:
        notices.push_back({object, kEventClosing});
        break;
    case kClosed:
        notices.push_back({object, kEventClosed});
        break;
    default:
        break;
    }
}

void Movers::update(uint32_t ms, Walker &player, std::vector<int> &moved, std::vector<Notice> &notices) {
    ms = std::max<uint32_t>(8, std::min<uint32_t>(ms, 1000));
    for (Mover &mover : _movers) {
        if (mover.state == kIdle)
            continue;
        int oldPosition = mover.position;
        int oldState = mover.state;
        int step = (int)((uint32_t)mover.rate * ms >> 10);
        // Moves and, if that runs into the player, takes it back.
        auto move = [&](int position, int stateOnBlock) {
            mover.position = position;
            if (!apply(mover, position, player, moved, true)) {
                mover.position = oldPosition;
                mover.state = oldState;
                if (stateOnBlock >= 0 && (mover.flags & kFlagReverses))
                    mover.state = stateOnBlock;
            }
        };
        switch (mover.state) {
        case kOpen:
        case kOpenBack:
            if (mover.flags & kFlagAutoClose) {
                mover.counter += ms >> 2;
                if (mover.counter >= mover.wait)
                    mover.state = mover.state == kOpen ? kClosing : kClosingBack;
            } else {
                if (mover.state == kOpen)
                    mover.flags &= ~kFlagOpenedBack;
                else
                    mover.flags |= kFlagOpenedBack;
                mover.state = kIdle;
            }
            break;
        case kOpening: {
            int position = mover.position + step;
            if (position >= kOpenPosition) {
                position = kOpenPosition;
                mover.state = kOpen;
            }
            move(position, -1);
            break;
        }
        case kClosed:
            mover.state = kIdle;
            break;
        case kClosing: {
            int position = mover.position - step;
            if (position <= 0) {
                position = 0;
                mover.state = kClosed;
                mover.flags &= ~kFlagOpenedBack;
            }
            move(position, kOpening);
            break;
        }
        case kOpeningBack: {
            int position = mover.position - step;
            if (position <= -kOpenPosition) {
                position = -kOpenPosition;
                mover.state = kOpenBack;
            }
            move(position, kClosingBack);
            break;
        }
        case kClosingBack: {
            int position = mover.position + step;
            if (position >= 0) {
                position = 0;
                mover.state = kClosed;
                mover.flags |= kFlagOpenedBack;
            }
            move(position, kOpeningBack);
            break;
        }
        default:
            mover.state = kIdle;
            break;
        }
        changed(mover, notices);
    }
}

void Movers::setState(uint16_t id, int state, Walker &player, std::vector<int> &moved, std::vector<Notice> &notices) {
    Mover *mover = find(id);
    if (!mover)
        return;
    mover->state = state;
    // Open or shut at once: the parts go there too.
    if (state == kOpen && mover->position != kOpenPosition) {
        mover->position = kOpenPosition;
        apply(*mover, mover->position, player, moved, false);
    } else if (state == kClosed && mover->position != 0) {
        mover->position = 0;
        apply(*mover, mover->position, player, moved, false);
    }
    changed(*mover, notices);
}

void Movers::toggle(uint16_t id, Walker &player, std::vector<int> &moved, std::vector<Notice> &notices) {
    Mover *mover = find(id);
    if (!mover)
        return;
    if (mover->position == 0)
        setState(id, (mover->flags & kFlagOpenedBack) ? kOpeningBack : kOpening, player, moved, notices);
    else
        setState(id, mover->position < 0 ? kClosingBack : kClosing, player, moved, notices);
}

void Movers::setLocked(uint16_t id, bool locked) {
    if (Mover *mover = find(id))
        mover->flags = locked ? mover->flags | kFlagLocked : mover->flags & ~kFlagLocked;
}

void Movers::bump(int wall, std::vector<Notice> &notices) {
    // 0x4B94D0: walking into a wall starts the door it names, by its mode.
    if (wall < 0 || wall >= (int)_level.getWalls().size())
        return;
    const LevelFile::Wall &w = _level.getWalls()[wall];
    Mover *mover = w.bumpMover ? find((uint16_t)w.bumpMover) : nullptr;
    if (!mover)
        return;
    bool locked = (mover->flags & kFlagLocked) != 0;
    bool back = (mover->flags & kFlagOpenedBack) != 0;
    int state = mover->state;
    bool shut = state == kIdle ? mover->position == 0 : state == kClosed || state == kClosing || state == kClosingBack;
    int next = -1;
    switch (w.bumpMode) {
    case 0:
        if (shut && !locked)
            next = kOpening;
        break;
    case 1:
        if (shut && !locked)
            next = kOpeningBack;
        break;
    case 2:
        if (!shut)
            next = back || state == kOpenBack || state == kOpeningBack ? kClosingBack : kClosing;
        break;
    case 3:
        if (!shut)
            next = back || state == kOpenBack || state == kOpeningBack ? kClosingBack : kClosing;
        else if (!locked)
            next = back ? kOpeningBack : kOpening;
        break;
    default:
        break;
    }
    if (next >= 0 && next != state) {
        mover->state = next;
        changed(*mover, notices);
    }
}

void Movers::sync(Serializer &s, Walker &player, std::vector<int> &moved) {
    s.syncCount((uint32_t)_movers.size());
    for (Mover &mover : _movers) {
        if (s.err())
            return;
        s.syncAsInt(mover.position);
        s.syncAsInt(mover.state);
        s.syncAsInt(mover.lastState);
        s.syncAsUint32(mover.counter);
        s.syncAsUint32(mover.flags);
    }
    if (s.isLoading()) {
        // The geometry follows from the originals and the positions.
        for (Mover &mover : _movers)
            apply(mover, mover.position, player, moved, false);
    }
}

} // namespace reluva::stgenerations
