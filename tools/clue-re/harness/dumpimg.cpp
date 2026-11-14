// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/game_data.h"
#include <cstdio>
#include <fstream>
using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; if (data.open(argv[1], Language::kUS)) return 1;
    for (int i = 2; i + 1 < argc; i += 2) {
        const Animation* a = data.animation(argv[i]); const Image* img = nullptr;
        if (a && !a->images.empty()) img = data.image(a->images[a->layers[0][0].image]);
        if (!img) { std::printf("%s none\n", argv[i]); continue; }
        std::ofstream o(argv[i + 1], std::ios::binary); o << "P6\n" << img->pixels.w << " " << img->pixels.h << "\n255\n";
        for (int y = 0; y < img->pixels.h; ++y) for (int x = 0; x < img->pixels.w; ++x) { auto p = img->pixels.pixel(x, y); o.put(char(p[0])).put(char(p[1])).put(char(p[2])); }
        std::printf("%s %dx%d off %d,%d part %d,%d\n", argv[i], img->pixels.w, img->pixels.h, img->offsetX, img->offsetY, a->layers[0][0].x, a->layers[0][0].y);
    }
}
