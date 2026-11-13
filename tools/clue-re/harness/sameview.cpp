// Renders a 640x480 view with the camera put so that an object's frame-0
// top-left piece lands where a recording shows it.
#include "engines/clue/game_data.h"
#include "engines/clue/formats/level.h"
#include "engines/clue/game/mansion.h"
#include <cstdio>
#include <cstring>
#include <fstream>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    // game dir, object name, x, y in the frame of its first piece, wall opacity, out.ppm
    GameData data; data.open(argv[1], Language::US);
    DataDrive f; f.open(*data.find("clue.lvl")); Level lvl; parseLevel(f, lvl);
    Mansion m; m.build(data, lvl);
    int camX = 0, camY = 0; bool found = false;
    for (const auto& it : m.items())
        if (it.object->name == argv[2]) {
            const auto& p = (*it.frames)[0][0];
            camX = it.anchorX + p.dx - std::atoi(argv[3]); camY = it.anchorY + p.dy - std::atoi(argv[4]); found = true; break;
        }
    if (!found) { std::printf("no %s\n", argv[2]); return 1; }
    const int op = std::atoi(argv[5]);
    Surface s; s.create(640, 480, PixelFormat::RGBA32);
    for (int y = 0; y < 480; ++y) for (int x = 0; x < 640; ++x) { uint8_t* q = s.row(y) + x * 4; q[0] = 24; q[1] = 24; q[2] = 36; q[3] = 255; }
    for (size_t i : m.drawOrder()) {
        const auto& it = m.items()[i];
        if (it.object->type == ObjectDef::TileNumber || (it.layer == 4 && op == 0)) continue;
        if (argc > 7 && it.object->name == argv[7]) continue; // leave this object out
        for (const auto& p : (*it.frames)[0]) image::blit(s, p.image->pixels, it.anchorX + p.dx - camX, it.anchorY + p.dy - camY, it.layer == 4 ? op : 255);
    }
    std::ofstream o(argv[6], std::ios::binary); o << "P6\n640 480\n255\n";
    for (int y = 0; y < 480; ++y) for (int x = 0; x < 640; ++x) o.write((const char*)s.row(y) + x * 4, 3);
    std::printf("camera %d %d\n", camX, camY);
}
