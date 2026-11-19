// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/game_data.h"
#include "engines/clue/formats/level.h"
#include "engines/clue/game/mansion.h"
#include <cstdio>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::kUS);
    DataDrive f; f.open(*data.find("clue.lvl")); Level lvl; parseLevel(f, lvl);
    Mansion m; m.build(data, lvl);
    for (const auto& it : m.items()) {
        const auto& pl = lvl.placements[it.placement];
        const std::string& n = lvl.objects[pl.object];
        if (n.find("dr") == std::string::npos && n.find("door") == std::string::npos) continue;
        std::printf("%-28s L%d (%d,%d) box %d,%d %dx%dx%d anchor %d,%d", n.c_str(), pl.layer, pl.x, pl.y, it.box.x, it.box.y, it.box.sizeX, it.box.sizeY, it.box.height, it.anchorX, it.anchorY);
        for (const auto& p : (*it.frames)[0]) std::printf("  piece %d,%d %dx%d", p.dx, p.dy, p.image->pixels.w, p.image->pixels.h);
        std::printf("\n");
    }
}
