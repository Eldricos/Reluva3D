#include "engines/clue/game_data.h"
#include "engines/clue/formats/resi.h"
#include "engines/clue/ui/ui.h"
#include <cstdio>
#include <fstream>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::kUS);
    Resi resi; std::ifstream in(*data.find("dialogInfo.res"), std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), {}); parseResi(b, resi);
    Fonts fonts(data, resi);
    Surface s; s.create(640, 400, PixelFormat::kRGBA32);
    for (int y = 0; y < 400; ++y) for (int x = 0; x < 640; ++x) { uint8_t* q = s.row(y) + x * 4; q[0] = q[1] = q[2] = x < 320 ? 230 : 40; q[3] = 255; }
    int y = 0;
    for (auto& f : resi.fonts) { const BitmapFont* bf = fonts.get(int(f.id)); if (!bf) continue;
        std::printf("font %u %s h %d\n", f.id, f.animation.c_str(), bf->lineHeight());
        char t[64]; std::snprintf(t, sizeof t, "%u Miss Scarlet Candlestick", f.id);
        bf->draw(s, 4, y, t); bf->draw(s, 324, y, t); y += bf->lineHeight() + 4; }
    std::ofstream o(argv[2], std::ios::binary); o << "P6\n640 400\n255\n";
    for (int yy = 0; yy < 400; ++yy) for (int x = 0; x < 640; ++x) o.write((const char*)s.row(yy) + x * 4, 3);
}
