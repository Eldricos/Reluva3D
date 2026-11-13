#include "engines/clue/game_data.h"
#include "engines/clue/formats/level.h"
#include "engines/clue/game/board.h"
#include <cstdio>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::US);
    DataDrive f; f.open(*data.find("clue.lvl")); Level lvl; parseLevel(f, lvl);
    Board b; b.build(lvl);
    for (const auto& d : b.doors()) std::printf("room %d in %d,%d out %d,%d\n", d.room, d.inside.x, d.inside.y, d.outside.x, d.outside.y);
}
