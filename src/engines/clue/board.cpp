// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/board.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

namespace reluva::clue {

namespace {

int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// The rooms' furniture is kept by room: "ballroom:Oblcouch_ad".
int roomOfFolder(const std::string& name) {
    static const char* const kFolders[kRoomCount] = {"hallroom:", "lounge:", "diningroom:", "kitchen:", "ballroom:",
                                                     "conservatory:", "billiardroom:", "library:", "study:"};
    for (int r = 0; r < kRoomCount; ++r)
        if (name.rfind(kFolders[r], 0) == 0)
            return r;
    return kNoRoom;
}

int suspectOfCharacter(const std::string& name) {
    static const char* const kNames[kSuspectCount] = {"scarlet", "mustard", "white", "green", "peacock", "plum"};
    for (int s = 0; s < kSuspectCount; ++s)
        if (name == std::string("character:O") + kNames[s])
            return s;
    return -1;
}

} // namespace

Square Board::squareAt(int worldX, int worldY) {
    return {floorDiv(worldX - kGridOrigin, kSquareSize), floorDiv(worldY - kGridOrigin, kSquareSize)};
}

void Board::standingPoint(const Square& s, int& worldX, int& worldY) {
    worldX = kGridOrigin + s.x * kSquareSize + 24;
    worldY = kGridOrigin + s.y * kSquareSize + 24;
}

int Board::roomAt(const Square& s) const {
    auto it = rooms_.find(s);
    return it == rooms_.end() ? kNoRoom : it->second;
}

Error Board::build(const Level& level) {
    *this = {};
    passage_.fill(kNoRoom);
    // The corridors: the hallway's floor blocks on layer 0. The rooms'
    // floors: every other floor block (r_...), joined where they touch.
    std::set<Square> floors;
    for (const Placement& p : level.placements) {
        const std::string& name = level.objects[p.object];
        if (name.rfind("r_", 0) != 0)
            continue;
        const Square s = squareAt(p.x, p.y);
        if (name.rfind("r_hwfloor:", 0) == 0 && p.layer == 0)
            corridor_.insert(s);
        else
            floors.insert(s);
    }
    for (const Square& s : corridor_)
        floors.erase(s);
    std::map<Square, int> part;
    std::vector<std::vector<Square>> parts;
    for (const Square& s : floors) {
        if (part.count(s))
            continue;
        const int k = int(parts.size());
        parts.emplace_back();
        std::vector<Square> stack{s};
        part[s] = k;
        while (!stack.empty()) {
            const Square a = stack.back();
            stack.pop_back();
            parts[size_t(k)].push_back(a);
            for (const Square& b : {Square{a.x + 1, a.y}, Square{a.x - 1, a.y}, Square{a.x, a.y + 1}, Square{a.x, a.y - 1}})
                if (floors.count(b) && !part.count(b)) {
                    part[b] = k;
                    stack.push_back(b);
                }
        }
    }
    // Which room each part is: by the furniture standing in it. The middle
    // (the stairs) has none of a room's.
    std::vector<std::array<int, kRoomCount>> votes(parts.size());
    for (auto& v : votes)
        v.fill(0);
    for (const Placement& p : level.placements) {
        const int room = roomOfFolder(level.objects[p.object]);
        auto it = part.find(squareAt(p.x, p.y));
        if (p.layer == 5 && room != kNoRoom && it != part.end())
            ++votes[size_t(it->second)][size_t(room)];
    }
    // Rooms borrow one another's furniture (the dining room has the billiard
    // room's chairs), so each room goes, one to one, to the part with the
    // most of its furniture, the strongest claims first.
    std::vector<bool> partTaken(parts.size());
    for (;;) {
        int best = 0, bestPart = -1, bestRoom = kNoRoom;
        for (size_t k = 0; k < parts.size(); ++k)
            for (int r = 0; r < kRoomCount; ++r)
                if (!partTaken[k] && roomSquares_[size_t(r)].empty() && votes[k][size_t(r)] > best) {
                    best = votes[k][size_t(r)];
                    bestPart = int(k);
                    bestRoom = r;
                }
        if (bestPart < 0)
            break;
        partTaken[size_t(bestPart)] = true;
        roomSquares_[size_t(bestRoom)] = parts[size_t(bestPart)];
        for (const Square& sq : parts[size_t(bestPart)])
            rooms_[sq] = bestRoom;
    }
    for (int r = 0; r < kRoomCount; ++r)
        if (roomSquares_[size_t(r)].empty())
            return Error::fail("room " + std::to_string(r) + " not found in the level");

    // Doors: the lintels over them ("...drtop..."), on the line between a
    // room's square and a corridor's. "_n" ones lie along x (between rows),
    // "_w" ones along y (between columns).
    for (const Placement& p : level.placements) {
        const std::string& name = level.objects[p.object];
        const size_t at = name.find("drtop");
        if (name.rfind("wall:", 0) != 0 || at == std::string::npos)
            continue;
        std::string suffix = name.substr(at + 5);
        suffix.erase(std::remove(suffix.begin(), suffix.end(), '_'), suffix.end());
        const bool alongX = suffix.empty() || suffix[0] == 'n';
        const bool alongY = suffix.empty() || suffix[0] == 'w';
        const double gx = double(p.x - kGridOrigin) / kSquareSize, gy = double(p.y - kGridOrigin) / kSquareSize;
        const int cx = int(std::floor(gx)), cy = int(std::floor(gy));
        std::vector<std::pair<Square, Square>> pairs;
        for (int line : {cy, cy + 1})
            if (alongX)
                pairs.push_back({{cx, line - 1}, {cx, line}});
        for (int line : {cx, cx + 1})
            if (alongY)
                pairs.push_back({{line - 1, cy}, {line, cy}});
        bool found = false;
        for (const auto& [a, b] : pairs)
            for (const auto& [in, out] : {std::pair{a, b}, std::pair{b, a}})
                if (!found && roomAt(in) != kNoRoom && isCorridor(out)) {
                    doors_.push_back({roomAt(in), in, out});
                    found = true;
                }
        if (!found)
            return Error::fail("a door (" + name + ") between no room and corridor");
    }

    // The suspects start where clue.lvl stands them.
    std::array<bool, kSuspectCount> started{};
    for (const Placement& p : level.placements) {
        const int s = suspectOfCharacter(level.objects[p.object]);
        if (s >= 0) {
            start_[size_t(s)] = squareAt(p.x, p.y);
            started[size_t(s)] = true;
        }
    }
    for (int s = 0; s < kSuspectCount; ++s)
        if (!started[size_t(s)])
            return Error::fail("suspect " + std::to_string(s) + " has no place to start in the level");

    // Secret passages: the rooms with one ("secretpass:..."), each leading
    // to the one furthest from it (the opposite corner).
    std::vector<int> withPassage;
    for (const Placement& p : level.placements)
        if (level.objects[p.object].rfind("secretpass:", 0) == 0) {
            const int r = roomAt(squareAt(p.x, p.y));
            if (r != kNoRoom && std::find(withPassage.begin(), withPassage.end(), r) == withPassage.end())
                withPassage.push_back(r);
        }
    const auto centre = [&](int r, double& x, double& y) {
        x = y = 0;
        for (const Square& s : roomSquares_[size_t(r)]) {
            x += s.x;
            y += s.y;
        }
        x /= double(roomSquares_[size_t(r)].size());
        y /= double(roomSquares_[size_t(r)].size());
    };
    for (int a : withPassage) {
        double ax, ay, best = -1;
        centre(a, ax, ay);
        for (int b : withPassage) {
            double bx, by;
            centre(b, bx, by);
            const double d = (ax - bx) * (ax - bx) + (ay - by) * (ay - by);
            if (b != a && d > best) {
                best = d;
                passage_[size_t(a)] = b;
            }
        }
    }

    // Where suspects stand in a room. The original's squares, as two
    // recorded games show them on the board from above (most used first;
    // docs/formats/clue/board.md), when they are the room's; then the
    // squares along the room's front edges (its largest x and y, nearest
    // the camera, where the original stands them), those clear of
    // furniture first, then the rest, each nearest the front corner first.
    static const std::vector<Square> kRecorded[kRoomCount] = {
        {{27, 17}, {26, 17}},                                                  // hall
        {{32, 16}, {34, 16}, {33, 16}, {37, 16}},                              // lounge
        {{32, 23}, {34, 25}, {34, 26}, {36, 26}, {32, 25}},                    // dining room
        {},                                                                    // kitchen
        {{24, 32}, {26, 34}, {26, 33}, {28, 31}, {24, 31}, {26, 35}, {27, 32}}, // ballroom
        {{18, 32}, {18, 34}, {18, 33}, {18, 35}, {17, 36}, {17, 37}},          // conservatory
        {{18, 27}, {17, 27}, {16, 27}, {16, 26}},                              // billiard room
        {{18, 21}, {17, 21}},                                                  // library
        {},                                                                    // study
    };
    std::set<Square> furnished;
    for (const Placement& p : level.placements)
        if (p.layer == 5)
            furnished.insert(squareAt(p.x, p.y));
    for (int r = 0; r < kRoomCount; ++r) {
        const std::vector<Square>& squares = roomSquares_[size_t(r)];
        std::vector<Square> sq;
        for (const Square& k : kRecorded[r])
            if (roomAt(k) == r)
                sq.push_back(k);
        int maxX = INT_MIN, maxY = INT_MIN;
        for (const Square& k : squares) {
            maxX = std::max(maxX, k.x);
            maxY = std::max(maxY, k.y);
        }
        std::vector<Square> rest;
        for (const Square& k : squares)
            if (std::find(sq.begin(), sq.end(), k) == sq.end())
                rest.push_back(k);
        const auto rank = [&](const Square& a) {
            // Off the front edges by how far; furniture last; then nearest
            // the front corner.
            const int off = std::min(maxX - a.x, maxY - a.y);
            return off * 1000 + (furnished.count(a) ? 100000 : 0) + (maxX - a.x) + (maxY - a.y);
        };
        std::stable_sort(rest.begin(), rest.end(), [&](const Square& a, const Square& b) { return rank(a) < rank(b); });
        sq.insert(sq.end(), rest.begin(), rest.end());
        standing_[size_t(r)] = sq;
    }
    return Error::success();
}

std::vector<Place> Board::neighbours(const Place& p) const {
    std::vector<Place> out;
    if (p.inRoom()) {
        for (const Door& d : doors_)
            if (d.room == p.room)
                out.push_back({d.outside, kNoRoom});
        return out;
    }
    const Square& s = p.square;
    for (const Square& n : {Square{s.x + 1, s.y}, Square{s.x - 1, s.y}, Square{s.x, s.y + 1}, Square{s.x, s.y - 1}})
        if (isCorridor(n))
            out.push_back({n, kNoRoom});
    for (const Door& d : doors_)
        if (d.outside == s)
            out.push_back({{}, d.room});
    return out;
}

} // namespace reluva::clue
