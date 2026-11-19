// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/game_data.h"
#include <cstdio>
using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; if (data.open(argv[1], Language::kUS)) return 1;
    for (int i = 2; i < argc; ++i) {
        const Animation* a = data.animation(argv[i]);
        if (!a) { std::printf("%s none\n", argv[i]); continue; }
        std::printf("%s layers %zu frames %zu", argv[i], a->layers.size(), a->frameCount());
        if (!a->layers.empty() && a->frameCount()) {
            const auto& p = a->layers[0][0];
            const Image* img = p.image < a->images.size() ? data.image(a->images[p.image]) : nullptr;
            std::printf(" part %d,%d img %dx%d off %d,%d", p.x, p.y, img ? img->pixels.w : -1, img ? img->pixels.h : -1, img ? img->offsetX : 0, img ? img->offsetY : 0);
        }
        std::printf("\n");
    }
}
