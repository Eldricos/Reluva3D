#include "engines/clue/game_data.h"
#include "engines/clue/formats/level.h"
#include <cstdio>
#include <map>
#include <cstring>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::US);
    DataDrive f; f.open(*data.find("clue.lvl")); Level lvl; parseLevel(f, lvl);
    std::map<std::pair<int,int>, int> cnt;
    for (const auto& pl : lvl.placements) {
        const std::string& n = lvl.objects[pl.object];
        const ObjectDef* o = data.object(n);
        if (!o || o->type != 1) continue;
        const auto& ts = pl.timings.empty() ? o->timings : pl.timings;
        for (auto& t : ts) {
            int32_t v; std::memcpy(&v, t.raw.data() + 3, 4);
            cnt[{t.time, v}]++;
            if (argc > 2 && t.time == std::atoi(argv[2])) std::printf("%s (%d,%d) sq %d,%d L%d time %u val %d\n", n.c_str(), pl.x, pl.y, (pl.x-16)>>6, (pl.y-16)>>6, pl.layer, t.time, v);
        }
    }
    for (auto& [k, c] : cnt) std::printf("time %d val %d: %d\n", k.first, k.second, c);
}
