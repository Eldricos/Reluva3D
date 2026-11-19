// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/game_data.h"
#include "engines/clue/formats/level.h"
#include "engines/clue/game/mansion.h"
#include <cstdio>
#include <map>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::kUS);
    DataDrive f; f.open(*data.find("clue.lvl")); Level lvl; parseLevel(f, lvl);
    Mansion m; m.build(data, lvl);
    std::map<std::string,int> seen;
    for (const auto& it : m.items()) {
        const auto& fr = it.animation->frames[0];
        const auto& n = lvl.objects[lvl.placements[it.placement].object];
        if ((fr.unknown[3] || fr.unknown[4]) && !seen[n]++)
            std::printf("%-30s type %d u %d %d %d %d %d solid %d size %dx%dx%d\n", n.c_str(), it.object->type, fr.unknown[0], fr.unknown[1], (int16_t)fr.unknown[2], (int16_t)fr.unknown[3], (int16_t)fr.unknown[4], fr.solid, fr.sizeX, fr.sizeY, fr.height);
    }
    std::printf("%zu kinds with offsets\n", seen.size());
}
