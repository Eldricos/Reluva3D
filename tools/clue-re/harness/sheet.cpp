// Draws each named animation's frames (all layers) in a row on a sheet, with its size.
#include "engines/clue/game_data.h"
#include "image/image.h"
#include <cstdio>
#include <fstream>
#include <algorithm>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::kUS);
    const int W = 1600; int y = 0; std::vector<std::pair<int,const Animation*>> rows;
    image::Image sheet; sheet.w = W; sheet.h = 4000; sheet.rgba.assign(size_t(W) * 4000 * 4, 60);
    for (int a = 3; a < argc; ++a) {
        const Animation* an = data.animation(argv[a]);
        if (!an) { std::printf("%s: none\n", argv[a]); continue; }
        int x = 0, rowH = 0;
        std::printf("%s: %zu frames %zu layers\n", argv[a], an->frameCount(), an->layers.size());
        for (size_t f = 0; f < an->frameCount() && f < 16; ++f) {
            int minx = 1 << 20, miny = 1 << 20, maxx = -minx, maxy = -miny;
            for (auto& L : an->layers) { auto& p = L[f]; if (p.image >= an->images.size()) continue; const Image* im = data.image(an->images[p.image]); if (!im) continue;
                minx = std::min(minx, p.x + im->offsetX); miny = std::min(miny, p.y + im->offsetY); maxx = std::max(maxx, p.x + im->offsetX + im->pixels.w); maxy = std::max(maxy, p.y + im->offsetY + im->pixels.h); }
            if (maxx < minx) continue;
            if (x + (maxx - minx) > W) break;
            for (auto& L : an->layers) { auto& p = L[f]; if (p.image >= an->images.size()) continue; const Image* im = data.image(an->images[p.image]); if (!im) continue;
                Surface dummy; (void)dummy;
                for (int yy = 0; yy < im->pixels.h; ++yy) for (int xx = 0; xx < im->pixels.w; ++xx) { const uint8_t* c = im->pixels.pixel(xx, yy); if (!c[3]) continue;
                    int X = x + p.x + im->offsetX - minx + xx, Y = y + p.y + im->offsetY - miny + yy; if (X >= W || Y >= 4000) continue; uint8_t* d = &sheet.rgba[(size_t(Y) * W + X) * 4]; d[0]=c[0]; d[1]=c[1]; d[2]=c[2]; } }
            std::printf("   frame %zu: %dx%d at %d,%d\n", f, maxx - minx, maxy - miny, minx, miny);
            x += maxx - minx + 6; rowH = std::max(rowH, maxy - miny);
        }
        y += rowH + 6; if (y > 3900) break;
    }
    std::ofstream o(argv[2], std::ios::binary); o << "P6\n" << W << " " << y << "\n255\n";
    for (int yy = 0; yy < y; ++yy) for (int xx = 0; xx < W; ++xx) o.write((const char*)&sheet.rgba[(size_t(yy) * W + xx) * 4], 3);
}
