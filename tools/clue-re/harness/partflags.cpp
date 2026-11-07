#include "engines/clue/game_data.h"
#include "engines/clue/resources.h"
#include <cstdio>
#include <map>
using namespace reluva::clue;
int main(int, char** argv) {
    GameData data; if (data.open(argv[1], Language::US)) return 1;
    std::map<uint32_t,int> h; std::map<uint32_t,std::string> ex;
    for (auto& path : data.withExtension(".a")) {
        DataDrive d; if (d.open(path)) continue;
        for (auto& e : d.entries()) {
            Animation a; if (parseAnimation(d.read(e), a)) continue;
            for (auto& L : a.layers) for (size_t f = 0; f < L.size(); ++f) { uint32_t v = L[f].flags; if (!h[v]++) ex[v] = path.stem().string() + ":" + e.name + " frame " + std::to_string(f); }
        }
    }
    for (auto& [v, n] : h) std::printf("%08x x%d e.g. %s\n", v, n, ex[v].c_str());
}
