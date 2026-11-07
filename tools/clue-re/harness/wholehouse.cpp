// Renders the engine's mansion scene whole, for comparing with recordings.
#include "engines/clue/game_data.h"
#include "engines/clue/level.h"
#include "engines/clue/mansion.h"
#include <cstdio>
#include <fstream>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    // argv: game dir, out.ppm, wall opacity (0-255, 0 hides layer 4)
    GameData data; data.open(argv[1], Language::US);
    DataDrive f; f.open(*data.find("clue.lvl")); Level lvl; parseLevel(f, lvl);
    Mansion m; if (Error e = m.build(data, lvl)) { std::printf("%s\n", e.message.c_str()); return 1; }
    const int wallOpacity = std::atoi(argv[3]);
    const ScreenRect b = m.bounds();
    Surface s; s.create(b.w, b.h, PixelFormat::RGBA32);
    for (int y = 0; y < s.h; ++y) for (int x = 0; x < s.w; ++x) { uint8_t* p = s.row(y) + x * 4; p[0] = 24; p[1] = 24; p[2] = 36; p[3] = 255; }
    for (size_t i : m.drawOrder()) {
        const auto& it = m.items()[i];
        if (it.object->type == ObjectDef::TileNumber) continue;
        int op = it.layer == 4 ? wallOpacity : 255;
        if (op == 0) continue;
        for (const auto& p : (*it.frames)[0]) image::blit(s, p.image->pixels, it.anchorX + p.dx - b.x, it.anchorY + p.dy - b.y, op);
    }
    std::ofstream o(argv[2], std::ios::binary); o << "P6\n" << s.w << " " << s.h << "\n255\n";
    for (int y = 0; y < s.h; ++y) for (int x = 0; x < s.w; ++x) o.write((const char*)s.row(y) + x * 4, 3);
    std::printf("bounds %d %d %d %d\n", b.x, b.y, b.w, b.h);
}
