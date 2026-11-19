// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/game_data.h"
#include "engines/clue/formats/level.h"
#include "engines/clue/game/mansion.h"
#include "engines/clue/game/board.h"
#include "engines/clue/formats/resources.h"
#include <cstdio>
#include <map>
using namespace reluva; using namespace reluva::clue;
int main(int, char** argv) {
    GameData data; if (data.open(argv[1], Language::kUS)) { std::puts("open"); return 1; }
    DataDrive f; Level lvl; f.open(*data.find("clue.lvl")); parseLevel(f, lvl);
    Mansion m; if (m.build(data, lvl)) { std::puts("build"); return 1; }
    Board board; board.build(lvl);
    std::map<std::string,int> seen;
    for (const auto& it : m.items()) {
        const auto& pl = lvl.placements[it.placement];
        bool react = false, hover = false;
        for (const auto& st : it.object->states) if (st.id >= 20 && st.id <= 22) react = true;
        auto look = [&](const std::vector<Timing>& ts) { for (auto& t : ts) if (t.time == 2100) hover = true; };
        look(pl.timings); look(it.object->timings);
        if (!react && !hover) continue;
        char k[200]; std::snprintf(k, sizeof k, "%s kind %d/%d react %d hover %d room %d (box %d,%d %dx%d)", it.object->name.c_str(), it.kind, it.subKind, react, hover, board.roomAt(Board::squareAt(it.worldX, it.worldY)), it.box.x, it.box.y, it.box.sizeX, it.box.sizeY);
        int in = 0, tot = 0;
        for (int yy = it.rect.y; yy < it.rect.y + it.rect.h; yy += 2)
            for (int xx = it.rect.x; xx < it.rect.x + it.rect.w; xx += 2) { ++tot; in += onBox(it.box, xx, yy); }
        char k2[300]; std::snprintf(k2, sizeof k2, "%s  rect %d,%d %dx%d  box covers %d%% of it", k, it.rect.x, it.rect.y, it.rect.w, it.rect.h, tot ? in * 100 / tot : -1);
        seen[k2]++;
    }
    for (auto& [k, n] : seen) std::printf("%3d %s\n", n, k.c_str());
}
