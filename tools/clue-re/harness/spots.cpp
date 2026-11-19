// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/game_data.h"
#include "engines/clue/formats/level.h"
#include "engines/clue/game/mansion.h"
#include "engines/clue/game/board.h"
#include <cstdio>
#include <set>
#include <algorithm>
using namespace reluva; using namespace reluva::clue;
int cell32(int v) { return (v + (v < 0 ? 31 : 0)) >> 5; }
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::kUS);
    DataDrive f; f.open(*data.find("clue.lvl")); Level lvl; parseLevel(f, lvl);
    Mansion m; m.build(data, lvl); Board b; b.build(lvl);
    std::set<std::pair<int,int>> blocked;
    for (const auto& it : m.items()) {
        const auto& f0 = it.animation->frames[0]; bool bl = false;
        switch (it.kind) { case 0: bl = it.box.z + f0.solid <= 5; break; case 3: bl = it.subKind != 1; break; case 4: case 5: case 8: bl = true; break; default: break; }
        if (bl && f0.solid != 0) for (int x = it.box.x; x < it.box.x + it.box.sizeX; x += 32) for (int y = it.box.y; y < it.box.y + it.box.sizeY; y += 32) blocked.insert({cell32(x), cell32(y)});
    }
    for (const auto& d : b.doors()) {
        int fx = 16 + d.inside.x*64 + 32, fy = 16 + d.inside.y*64 + 32;
        std::set<std::pair<int,int>> taken;
        std::printf("room %d door %d,%d:", d.room, d.inside.x, d.inside.y);
        for (int n = 0; n < 3; ++n) {
            bool found = false;
            for (int r = 128; r < 320 && !found; r += 32)
              for (int y = fy + r; y >= fy - r && !found; y -= 32)
                for (int x = fx + r; x >= fx - r && !found; x -= 32) {
                    std::pair<int,int> c{cell32(x), cell32(y)};
                    if (blocked.count(c) || taken.count(c)) continue;
                    Square sq = Board::squareAt(c.first*32+32, c.second*32+32);
                    if (b.roomAt(sq) != d.room) continue;
                    bool door = false; for (auto& e : b.doors()) door |= e.inside == sq;
                    if (door) continue;
                    found = true; taken.insert(c);
                    Square at = Board::squareAt(x, y);
                    std::printf("  (%d,%d) sq %d,%d", x, y, at.x, at.y);
                }
        }
        std::printf("\n");
    }
}
