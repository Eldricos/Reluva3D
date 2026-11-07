#include "engines/clue/game_data.h"
#include "engines/clue/level.h"
#include "engines/clue/mansion.h"
#include <cstdio>
#include <cstdlib>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::US);
    DataDrive f; f.open(*data.find("clue.lvl")); Level lvl; parseLevel(f, lvl);
    Mansion m; m.build(data, lvl);
    int ax = std::atoi(argv[2]), ay = std::atoi(argv[3]);
    for (const auto& it : m.items()) {
        const auto& b = it.box; if (it.flat) continue;
        bool in = b.x <= ax + 208 && b.x + b.sizeX - 8 > ax && b.y <= ay + 208 && b.y + b.sizeY - 8 > ay;
        if (in) std::printf("%-28s kind %d/%d keep %d box %d,%d %dx%d L%d\n", lvl.objects[lvl.placements[it.placement].object].c_str(), it.kind, it.subKind, it.keepsSolid, b.x, b.y, b.sizeX, b.sizeY, it.layer);
    }
}
