// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/game_data.h"
#include <cstdio>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::kUS);
    std::printf("ticker.res at %s\n", data.find("ticker.res") ? data.find("ticker.res")->string().c_str() : "-");
    const Animation* a = data.animation("ticker:Aticker");
    std::printf("cell %dx%d\n", a->width, a->height);
    for (int c : {'M','i','s',' ','S','c','a'}) { auto& p = a->layers[0][c]; const Image* im = p.image < a->images.size() ? data.image(a->images[p.image]) : nullptr;
        std::printf("'%c' img %d w %d cellW %d off %d ver %x\n", c, p.image, im ? im->pixels.w : -1, im ? im->cellW : -1, im ? im->offsetX : 0, im ? im->version : 0); }
}
