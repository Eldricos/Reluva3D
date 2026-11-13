#include "engines/clue/game_data.h"
#include "engines/clue/formats/level.h"
#include <cstdio>
#include <set>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::US);
    DataDrive f; f.open(*data.find("clue.lvl")); Level lvl; parseLevel(f, lvl);
    std::set<std::string> seen;
    for (auto& p : lvl.placements) {
        const std::string& n = lvl.objects[p.object]; if (seen.count(n)) continue; seen.insert(n);
        const ObjectDef* o = data.object(n); if (!o || o->states.size() < 2 || o->type == ObjectDef::Character) continue;
        std::printf("%-30s type %d tag %d flags %02x unk %u:", n.c_str(), o->type, p.tag, p.flags, o->subType);
        for (auto& s : o->states) { const Animation* a = data.animation(s.animation); std::printf(" [%u %s %zu]", s.id, s.animation.c_str() + s.animation.find(':') + 1, a ? a->frameCount() : 0); }
        if (!o->timings.empty()) { std::printf(" T:"); for (auto& t : o->timings) std::printf(" %u->%u", t.time, t.state); }
        std::printf("\n");
    }
}
