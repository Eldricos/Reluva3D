#include "engines/clue/game_data.h"
#include "engines/clue/formats/resources.h"
#include <cstdio>
#include <map>
using namespace reluva::clue;
int main(int, char** argv) {
    GameData data; if (data.open(argv[1], Language::kUS)) return 1;
    std::map<std::string,int> h; std::map<std::string,std::string> ex;
    for (auto& path : data.withExtension(".a")) {
        DataDrive d; if (d.open(path)) continue;
        for (auto& e : d.entries()) {
            Animation a; if (parseAnimation(d.read(e), a)) continue;
            for (size_t f = 0; f < a.frames.size(); ++f) for (auto& x : a.frames[f].extras) {
                const int key = x[0] | (x[1] << 8), type = x[2]; const int32_t v = int32_t(x[3] | (x[4] << 8) | (x[5] << 16) | (uint32_t(x[6]) << 24));
                char k[64]; std::snprintf(k, sizeof k, "key %d type %d value %d", key, type, v);
                if (!h[k]++) ex[k] = path.stem().string() + ":" + e.name + " frame " + std::to_string(f);
            }
        }
    }
    for (auto& [k, n] : h) std::printf("%s x%d e.g. %s\n", k.c_str(), n, ex[k].c_str());
}
