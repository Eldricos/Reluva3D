// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/game_data.h"
#include "engines/clue/formats/level.h"
#include "engines/clue/game/mansion.h"
#include <cstdio>
#include <cstdlib>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::kUS);
    DataDrive f; f.open(*data.find("clue.lvl")); Level lvl; parseLevel(f, lvl);
    Mansion m; m.build(data, lvl);
    int ax = std::atoi(argv[2]), ay = std::atoi(argv[3]);
    int sx = ax - ay, sy = (ax + ay) / 2;
    for (const auto& it : m.items()) {
        const auto& n = lvl.objects[lvl.placements[it.placement].object];
        if (n.rfind(argv[4], 0) != 0) continue;
        std::printf("%-26s rect rel %4d..%4d x %4d..%4d  box %d,%d %dx%d\n", n.c_str(), it.rect.x - sx, it.rect.x + it.rect.w - sx, it.rect.y - sy, it.rect.y + it.rect.h - sy, it.box.x, it.box.y, it.box.sizeX, it.box.sizeY);
    }
}
