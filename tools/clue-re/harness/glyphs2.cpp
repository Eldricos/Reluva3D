#include "engines/clue/game_data.h"
#include <cstdio>
#include <cstring>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::US);
    const Animation* a = data.animation("ticker:Aticker");
    for (const char* p = argv[2]; *p; ++p) { auto& part = a->layers[0][uint8_t(*p)]; const Image* im = part.image < a->images.size() ? data.image(a->images[part.image]) : nullptr;
        // first and last ink column within the image
        int first = -1, last = -1; if (im) for (int x = 0; x < im->pixels.w; ++x) for (int y = 0; y < im->pixels.h; ++y) if (im->pixels.pixel(x, y)[3] && im->pixels.pixel(x,y)[0] > 170) { if (first < 0) first = x; last = x; }
        std::printf("%c %d %d %d %d %d\n", *p == ' ' ? '_' : *p, im ? im->pixels.w : 0, im ? im->cellW : 0, im ? im->offsetX : 0, first, last); }
}
