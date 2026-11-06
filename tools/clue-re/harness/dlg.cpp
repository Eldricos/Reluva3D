#include "engines/clue/game_data.h"
#include "engines/clue/resi.h"
#include <fstream>
#include <iterator>
static std::vector<uint8_t> readFile(const char* p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
#include <cstdio>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    Resi r; if (Error e = parseResi(readFile(argv[1]), r)) { std::printf("%s\n", e.message.c_str()); return 1; }
    const ResiDialog* d = r.dialog(std::atoi(argv[2]));
    std::printf("bg %s\n", d->background.c_str());
    for (size_t i = 0; i < d->controls.size(); ++i) { const auto& c = d->controls[i];
        std::printf("%zu t%d id%d %d,%d,%d,%d g%d font%d [%s] [%s]\n", i, c.type, c.id, c.left, c.top, c.right, c.bottom, c.group, c.font, c.text.c_str(), c.animation.c_str()); }
}
