#include "engines/clue/game_data.h"
#include "engines/clue/formats/level.h"
#include "engines/clue/game/mansion.h"
#include <cstdlib>
#include <fstream>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::US);
    DataDrive f; f.open(*data.find("clue.lvl")); Level lvl; parseLevel(f, lvl);
    Mansion m; m.build(data, lvl);
    Surface s; s.create(640, 480, PixelFormat::RGBA32);
    MansionLook look; look.nearWalls = MansionLook::NearWalls(std::atoi(argv[4]) % 3); look.animate = false;
    m.draw(s, std::atoi(argv[2]), std::atoi(argv[3]), look, 0);
    std::ofstream o(argv[5], std::ios::binary); o << "P6\n640 480\n255\n";
    for (int y = 0; y < 480; ++y) for (int x = 0; x < 640; ++x) o.write((const char*)s.row(y) + x * 4, 3);
}
