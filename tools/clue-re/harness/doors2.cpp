#include "engines/clue/game_data.h"
#include "engines/clue/formats/level.h"
#include "engines/clue/game/mansion.h"
#include <cstdio>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::US);
    DataDrive f; f.open(*data.find("clue.lvl")); Level lvl; parseLevel(f, lvl);
    Mansion m; m.build(data, lvl);
    for (const auto& it : m.items()) {
        const auto& pl = lvl.placements[it.placement];
        const std::string& n = lvl.objects[pl.object];
        if (n.find(argc > 2 ? argv[2] : "drtop") == std::string::npos) continue;
        std::printf("%-24s L%d (%d,%d) fl%02x tag%d type%d unk%08x anim %s\n", n.c_str(), pl.layer, pl.x, pl.y, pl.flags, pl.tag, it.object->type, it.object->subType, it.object->states[0].animation.c_str());
    }
}
