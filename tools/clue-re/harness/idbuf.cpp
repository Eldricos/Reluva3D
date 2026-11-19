// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Renders the mansion as the game would (state-0 frames, near walls see-through, characters at
// their start), finds the camera that best matches a frame (PPM, lighting off), and writes for
// each pixel the item drawn last there, with each item's world place.
#include "engines/clue/game_data.h"
#include "engines/clue/formats/level.h"
#include "engines/clue/game/mansion.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <string>
using namespace reluva; using namespace reluva::clue;
static std::vector<uint8_t> readPpm(const char* f, int& w, int& h) {
    std::ifstream in(f, std::ios::binary); std::string m; int mx; in >> m >> w >> h >> mx; in.get();
    std::vector<uint8_t> d(size_t(w) * h * 3); in.read((char*)d.data(), d.size()); return d;
}
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::kUS);
    DataDrive f; f.open(*data.find("clue.lvl")); Level lvl; parseLevel(f, lvl);
    Mansion m; m.build(data, lvl);
    int w, h; auto target = readPpm(argv[2], w, h);
    int cx0 = std::atoi(argv[3]), cy0 = std::atoi(argv[4]), range = std::atoi(argv[5]);
    std::vector<int> id(640 * 480);
    std::vector<uint8_t> img(640 * 480 * 3);
    auto render = [&](int camX, int camY) {
        std::fill(id.begin(), id.end(), -1); std::fill(img.begin(), img.end(), 0);
        for (size_t i : m.drawOrder()) {
            const auto& it = m.items()[i];
            if (it.object->type == ObjectDef::kTileNumber) continue;
            const int op = it.layer == 4 ? 128 : 255;
            for (const auto& p : (*it.frames)[0]) {
                const auto& px = p.image->pixels;
                for (int y = 0; y < px.h; ++y) for (int x = 0; x < px.w; ++x) {
                    const uint8_t* c = px.pixel(x, y); if (!c[3]) continue;
                    const int sx = it.anchorX + p.dx + x - camX, sy = it.anchorY + p.dy + y - camY;
                    if (sx < 0 || sy < 0 || sx >= 640 || sy >= 410) continue;
                    uint8_t* d = &img[(size_t(sy) * 640 + sx) * 3];
                    for (int k = 0; k < 3; ++k) d[k] = uint8_t((c[k] * op + d[k] * (255 - op)) / 255);
                    if (op == 255) id[size_t(sy) * 640 + sx] = int(i);
                }
            }
        }
    };
    double best = 1e30; int bx = cx0, by = cy0;
    for (int dy = -range; dy <= range; dy += 1) for (int dx = -range; dx <= range; dx += 1) {
        if (range > 0 && (std::abs(dx) % 2 || std::abs(dy) % 2) && range > 6) continue;
        render(cx0 + dx, cy0 + dy);
        double e = 0;
        for (int y = 20; y < 400; y += 3) for (int x = 20; x < 620; x += 3) for (int k = 0; k < 3; ++k) {
            const double d = double(img[(size_t(y) * 640 + x) * 3 + k]) - target[(size_t(y) * w + x) * 3 + k]; e += d * d; }
        if (e < best) { best = e; bx = cx0 + dx; by = cy0 + dy; }
    }
    render(bx, by);
    std::printf("camera %d %d err %.1f\n", bx, by, best / (200.0 * 127 * 3));
    std::ofstream o(argv[6], std::ios::binary); o.write((const char*)id.data(), id.size() * 4);
    std::ofstream p(std::string(argv[6]) + ".ppm", std::ios::binary); p << "P6\n640 480\n255\n"; p.write((const char*)img.data(), img.size());
    std::ofstream t(std::string(argv[6]) + ".txt");
    for (size_t i = 0; i < m.items().size(); ++i) { const auto& it = m.items()[i];
        t << i << ' ' << it.object->name << ' ' << it.box.x << ' ' << it.box.y << ' ' << it.layer << ' ' << it.box.sizeX << ' ' << it.box.sizeY << ' ' << it.box.height << ' ' << it.rect.x - bx << ' ' << it.rect.y - by << ' ' << it.rect.w << ' ' << it.rect.h << ' ' << it.anchorX - bx << ' ' << it.anchorY - by << '\n'; }
}
