// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/formats/resi.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <cstdlib>
using namespace reluva::clue;
int main(int argc, char** argv) {
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
    Resi r; if (parseResi(b, r)) return 1;
    for (int i = 2; i < argc; ++i) if (const ResiDialog* d = r.dialog(std::atoi(argv[i]))) {
        std::printf("dialog %d %s\n", d->id, d->background.c_str());
        for (auto& k : d->controls) std::printf("  type %d id %d (%d,%d,%d,%d) '%s' %s\n", k.type, k.id, k.left, k.top, k.right, k.bottom, k.text.substr(0,40).c_str(), k.animation.c_str());
    }
}
