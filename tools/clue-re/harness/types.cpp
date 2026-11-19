// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/game_data.h"
#include "engines/clue/formats/level.h"
#include <cstdio>
#include <map>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::kUS);
    DataDrive f; f.open(*data.find("clue.lvl")); Level lvl; parseLevel(f, lvl);
    std::map<int, std::map<std::string,int>> t;
    for (auto& p : lvl.placements) { const ObjectDef* o = data.object(lvl.objects[p.object]); if (o) t[o->type][lvl.objects[p.object].substr(0, lvl.objects[p.object].find(':'))]++; }
    for (auto& [k, m] : t) { std::printf("type %d:", k); for (auto& [n, c] : m) std::printf(" %s %d", n.c_str(), c); std::printf("\n"); }
}
