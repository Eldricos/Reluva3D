#include "engines/clue/game_data.h"
#include "engines/clue/formats/level.h"
#include "engines/clue/formats/resources.h"
#include <cstdio>
#include <map>
using namespace reluva::clue;
int main(int, char** argv) {
    GameData data; if (data.open(argv[1], Language::kUS)) return 1;
    DataDrive f; Level lvl; f.open(*data.find("clue.lvl")); parseLevel(f, lvl);
    std::map<std::pair<int,int>, int> h; std::map<std::pair<int,int>, std::string> ex;
    for (auto& n : lvl.objects) if (const ObjectDef* o = data.object(n)) {
        const int st = o->startState, fr = o->startFrame;
        auto k = std::make_pair(st, fr); if (!h[k]++) ex[k] = n;
        if (st != 0 && st != -1) std::printf("%s state %d frame %d has state? %d\n", n.c_str(), st, fr, o->state(uint16_t(st)) != nullptr);
    }
    for (auto& [k, c] : h) std::printf("state %d frame %d: %d e.g. %s\n", k.first, k.second, c, ex[k].c_str());
}
