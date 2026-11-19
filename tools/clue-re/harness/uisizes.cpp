// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/game_data.h"
#include <cstdio>
#include <string>
using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; if (data.open(argv[1], Language::kUS)) return 1;
    for (int i = 2; i < argc; ++i) {
        std::string n = argv[i];
        int w = -1, h = -1, ox = 0, oy = 0;
        if (n.size() > 3 && n[3] == 'I') { if (const Image* img = data.image(n)) { w = img->pixels.w; h = img->pixels.h; ox = img->offsetX; oy = img->offsetY; } }
        else if (const Animation* a = data.animation(n)) {
            if (!a->images.empty()) if (const Image* img = data.image(a->images[0])) { w = img->pixels.w; h = img->pixels.h; ox = img->offsetX; oy = img->offsetY; }
        }
        std::printf("%s %d %d %d %d\n", n.c_str(), w, h, ox, oy);
    }
}
