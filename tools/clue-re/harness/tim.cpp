#include "engines/clue/game_data.h"
#include "engines/clue/level.h"
#include <cstdio>
#include <cstring>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::US);
    DataDrive f; f.open(*data.find("clue.lvl")); Level lvl; parseLevel(f, lvl);
    for (const auto& pl : lvl.placements) {
        const std::string& n = lvl.objects[pl.object];
        if (n.find(argv[2]) == std::string::npos) continue;
        const ObjectDef* o = data.object(n);
        std::printf("%s (%d,%d) pl timings:", n.c_str(), pl.x, pl.y);
        for (auto& t : pl.timings) { std::printf(" [%u %u |", t.time, t.state); for (auto b : t.raw) std::printf(" %02x", b); std::printf("]"); }
        std::printf("  obj timings:");
        if (o) for (auto& t : o->timings) { std::printf(" [%u %u |", t.time, t.state); for (auto b : t.raw) std::printf(" %02x", b); std::printf("]"); }
        std::printf("\n");
    }
}
