// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/formats/resi.h"
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace reluva::clue;
int main(int, char** argv) {
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
    Resi r; if (parseResi(b, r)) return 1;
    for (auto& d : r.dialogs) {
        std::printf("%3d %-28s", d.id, d.background.c_str());
        for (int v : d.raw) std::printf(" %d", v);
        std::printf("  controls %zu\n", d.controls.size());
    }
}
