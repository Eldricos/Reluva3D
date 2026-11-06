#include "engines/clue/game_data.h"
#include <cstdio>
#include <fstream>
#include <string>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::US);
    const Animation* a = data.animation(argv[2]);
    const std::string text = argv[3];
    std::ofstream o(argv[4]);
    for (unsigned char ch : text) {
        auto& p = a->layers[0][ch]; const Image* im = p.image < a->images.size() ? data.image(a->images[p.image]) : nullptr;
        o << int(ch) << ' ' << (im ? im->pixels.w : 0) << ' ' << (im ? im->pixels.h : 0) << ' ' << (im ? im->cellW : 0) << ' ' << (im ? im->offsetX : 0) << ' ' << (im ? im->offsetY : 0);
        if (im) for (int y = 0; y < im->pixels.h; ++y) for (int x = 0; x < im->pixels.w; ++x) { auto c = im->pixels.pixel(x, y); o << ' ' << int(c[3] ? c[0] : -1); }
        o << '\n';
    }
}
