// Renders each TickerStrings.txt line in the ticker's font, centred on a 640x18 strip, as one PGM row block.
#include "engines/clue/game_data.h"
#include "engines/clue/resi.h"
#include "engines/clue/ui.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <algorithm>
#include "common/image.h"
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::US);
    Resi resi; { std::ifstream in(*data.find("dialogInfo.res"), std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), {}); parseResi(b, resi); }
    Fonts fonts(data, resi); const BitmapFont* f = fonts.get(2);
    std::ifstream t(*data.find("TickerStrings.txt")); std::string line; std::vector<std::string> lines;
    while (std::getline(t, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); lines.push_back(line); }
    Surface s; s.create(640, 18 * int(lines.size()), PixelFormat::RGBA32);
    const Animation* an = data.animation("ticker:Aticker");
    struct G { const Image* im = nullptr; int x0 = 0, x1 = -1; };
    std::vector<G> gl(256);
    for (int c = 0; c < 256 && c < int(an->frameCount()); ++c) { auto& p = an->layers[0][c]; if (p.image >= an->images.size()) continue;
        G g; g.im = data.image(an->images[p.image]); if (!g.im) continue; g.x0 = 1 << 20;
        for (int x = 0; x < g.im->pixels.w; ++x) for (int y = 0; y < g.im->pixels.h; ++y) if (g.im->pixels.pixel(x, y)[3]) { g.x0 = std::min(g.x0, x); g.x1 = std::max(g.x1, x); }
        gl[c] = g; }
    auto adv = [&](unsigned char c) { const G& g = gl[c]; return g.im && g.x1 >= g.x0 ? g.x1 - g.x0 + 2 : 3; };
    for (size_t i = 0; i < lines.size(); ++i) { int w = 0; for (unsigned char c : lines[i]) w += adv(c);
        int x = (640 - w) / 2; for (unsigned char c : lines[i]) { const G& g = gl[c]; if (g.im && g.x1 >= g.x0) image::blit(s, g.im->pixels, x - g.x0, int(i) * 18 + 2 + g.im->offsetY); x += adv(c); } }
    std::ofstream o(argv[2], std::ios::binary); o << "P5\n640 " << s.h << "\n255\n";
    for (int y = 0; y < s.h; ++y) for (int x = 0; x < 640; ++x) o.put(char(s.row(y)[x * 4]));
    std::printf("%zu lines\n", lines.size());
}
