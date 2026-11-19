// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/game_data.h"
#include <cstdio>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::kUS);
    for (int a = 2; a < argc; ++a) {
        const ObjectDef* o = data.object(argv[a]);
        if (!o) { std::printf("%s: none\n", argv[a]); continue; }
        std::printf("%s type %d unknown %u\n", argv[a], o->type, o->subType);
        for (auto& s : o->states) {
            const Animation* an = data.animation(s.animation);
            std::printf("  state %u %s frames %zu", s.id, s.animation.c_str(), an ? an->frameCount() : 0);
            if (an && an->frameCount()) { auto& f = an->frames[0]; std::printf(" box %d,%d,%d", f.sizeX, f.sizeY, f.height); }
            std::printf("\n");
        }
    }
}
