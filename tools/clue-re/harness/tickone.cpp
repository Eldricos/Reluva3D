// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/game_data.h"
#include "engines/clue/formats/resi.h"
#include "engines/clue/ui/ui.h"
#include <fstream>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::kUS);
    Resi resi; { std::ifstream in(*data.find("dialogInfo.res"), std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), {}); parseResi(b, resi); }
    Fonts fonts(data, resi);
    Surface s; s.create(640, 18, PixelFormat::kRGBA32);
    drawText(s, *fonts.get(2), argv[2], 0, 0, 640, 18, true, true);
    std::ofstream o(argv[3], std::ios::binary); o << "P6\n640 18\n255\n";
    for (int y = 0; y < 18; ++y) for (int x = 0; x < 640; ++x) o.write((const char*)s.row(y) + x * 4, 3);
}
