#include "engines/clue/game_data.h"
#include <cstdio>
#include <string>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::US);
    const Animation* a = data.animation(argv[2]);
    const std::string text = argv[3];
    int w[4] = {0, 0, 0, 0}; int firstInk = -1, x[4] = {0, 0, 0, 0}, endInk[4] = {0, 0, 0, 0}, startInk[4] = {-1, -1, -1, -1};
    for (unsigned char ch : text) {
        auto& p = a->layers[0][ch]; const Image* im = p.image < a->images.size() ? data.image(a->images[p.image]) : nullptr;
        int f = -1, l = -1;
        if (im) for (int xx = 0; xx < im->pixels.w; ++xx) for (int y = 0; y < im->pixels.h; ++y) if (im->pixels.pixel(xx, y)[3]) { if (f < 0) f = xx; l = xx; }
        const int cell = im ? (im->version == 0x100 ? im->cellW : im->pixels.w) : 3;
        const int ink = f >= 0 ? l - f + 1 : 0;
        const int off = im ? im->offsetX : 0;
        const int adv[4] = {cell, ink ? ink + 1 : 2, ink ? ink + 2 : 3, ink ? ink + 1 : 3};
        for (int r = 0; r < 4; ++r) {
            if (f >= 0) { const int sx = x[r] + (r == 0 ? off + f : 0); if (startInk[r] < 0) startInk[r] = sx; endInk[r] = sx + ink; }
            x[r] += adv[r];
        }
    }
    const char* names[4] = {"cell (now)", "ink+1, space 2", "ink+2, space 3", "ink+1, space 3"};
    for (int r = 0; r < 4; ++r) std::printf("%-16s inked width %d\n", names[r], endInk[r] - startInk[r]);
}
