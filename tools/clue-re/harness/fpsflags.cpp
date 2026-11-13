#include "engines/clue/game_data.h"
#include "engines/clue/formats/resources.h"
#include <cstdio>
#include <map>
using namespace reluva::clue;
int main(int, char** argv) {
    GameData data; if (data.open(argv[1], Language::kUS)) return 1;
    std::map<std::pair<int,int>, int> hist; std::map<std::pair<int,int>, std::string> ex;
    for (auto& path : data.withExtension(".a")) {
        DataDrive d; if (d.open(path)) continue;
        for (auto& e : d.entries()) {
            Animation a; if (parseAnimation(d.read(e), a)) continue;
            auto k = std::make_pair(int(a.framesPerSecond), int(int(a.flags)));
            if (!hist[k]++ || (a.frameCount() > 1 && ex[k].find('(') == std::string::npos)) ex[k] = path.stem().string() + ":" + e.name + "(" + std::to_string(a.frameCount()) + ")";
        }
    }
    for (const char* n : {"main_anim:Alt1anim","main_anim:Art1anim","menu:Alt1flash","cursors:Apointcrsr","UI:Arolldiebtn","specialfx:Agood"}) if (const Animation* a = data.animation(n)) std::printf("%s fps %d flags %d frames %zu\n", n, a->framesPerSecond, int(a->flags), a->frameCount());
    for (auto& [k, n] : hist) std::printf("fps %d flags %d: %d  e.g. %s\n", k.first, k.second, n, ex[k].c_str());
}
