#include "engines/clue/game_data.h"
#include "engines/clue/level.h"
#include "engines/clue/resources.h"
#include <cstdio>
#include <map>
using namespace reluva::clue;
int main(int, char** argv) {
    GameData data; if (data.open(argv[1], Language::US)) return 1;
    DataDrive f; Level lvl; f.open(*data.find("clue.lvl")); parseLevel(f, lvl);
    std::map<int, std::map<std::string,int>> seen;
    for (auto& pl : lvl.placements) {
        const ObjectDef* o = data.object(lvl.objects[pl.object]);
        for (auto& t : pl.timings) seen[t.time]["placement " + lvl.objects[pl.object].substr(0, lvl.objects[pl.object].find(':'))]++;
        if (o) for (auto& t : o->timings) seen[t.time]["object " + o->name.substr(0, o->name.find(':'))]++;
    }
    for (auto& [t, m] : seen) { std::printf("%d:", t); int n = 0; for (auto& [k, c] : m) if (n++ < 6) std::printf(" %s x%d;", k.c_str(), c); std::printf("\n"); }
}
