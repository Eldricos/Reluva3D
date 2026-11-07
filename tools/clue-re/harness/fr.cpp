#include "engines/clue/game_data.h"
#include <cstdio>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::US);
    for (int i = 2; i < argc; ++i) {
        const ObjectDef* o = data.object(argv[i]); if (!o) { std::printf("no %s\n", argv[i]); continue; }
        const Animation* a = data.animation(o->states[0].animation);
        std::printf("%s %s short%d frames %zu\n", argv[i], o->states[0].animation.c_str(), a->shortForm, a->frames.size());
        const auto& f = a->frames[0];
        std::printf("  anchor %d,%d u %d %d %d %d %d solid %d size %d %d %d extras %zu\n", f.anchorX, f.anchorY, f.unknown[0], f.unknown[1], (int16_t)f.unknown[2], (int16_t)f.unknown[3], (int16_t)f.unknown[4], f.solid, f.sizeX, f.sizeY, f.height, f.extras.size());
    }
}
