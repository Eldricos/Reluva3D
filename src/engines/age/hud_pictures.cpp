// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/hud_pictures.h"

#include "engines/age/car.h"
#include "engines/age/texture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

constexpr float kMph = 2.23694f;

} // namespace

bool HudPictures::load(const SearchSet& files) {
    images_.clear();
    std::vector<std::string> names{"SPEED",      "TACOMETER TICKS_HALF", "MPH",           "DAMAGE", "DAMAGE_LABLE",
                                   "DIGI_COLON", "DIGITAC_GEAR_R",       "DIGITAC_GEAR_N"};
    for (int d = 0; d < 10; ++d) {
        names.push_back("DIGITAC_" + std::to_string(d) + "_HALF");
        names.push_back("DIGI_" + std::to_string(d));
        if (d > 0 && d < 9)
            names.push_back("DIGITAC_GEAR_" + std::to_string(d));
    }
    ok_ = true;
    for (const std::string& n : names) {
        Tex tex;
        if (loadTexture(files, n, tex))
            ok_ = false;
        else
            images_[n] = std::move(tex.image);
    }
    // The panel has no alpha: what's beyond its curve is pure black, where
    // its boxes' black is (8, 8, 8); taken as see-through.
    if (auto it = images_.find("SPEED"); it != images_.end())
        for (size_t i = 0; i + 3 < it->second.rgba.size(); i += 4) {
            uint8_t* p = &it->second.rgba[i];
            if (p[0] == 0 && p[1] == 0 && p[2] == 0)
                p[3] = 0;
        }
    return ok_;
}

const image::Image* HudPictures::get(const std::string& name) const {
    auto it = images_.find(name);
    return it == images_.end() ? nullptr : &it->second;
}

void HudPictures::blit(Surface& overlay, const image::Image& im, int x, int y, int w, int h, float share) const {
    if (im.w <= 0 || im.h <= 0 || w <= 0 || h <= 0)
        return;
    const int shown = int(std::clamp(share, 0.0f, 1.0f) * float(w));
    for (int yy = std::max(0, y); yy < std::min(overlay.h, y + h); ++yy) {
        uint8_t* row = overlay.row(yy);
        const int sy = std::min(im.h - 1, (yy - y) * im.h / h);
        for (int xx = std::max(0, x); xx < std::min(overlay.w, x + shown); ++xx) {
            const uint8_t* s = im.pixel(std::min(im.w - 1, (xx - x) * im.w / w), sy);
            uint8_t* d = row + xx * 4;
            // Over what's there, both with straight alpha.
            const float a = float(s[3]) / 255, da = float(d[3]) / 255;
            const float out = a + da * (1 - a);
            if (out <= 0)
                continue;
            for (int k = 0; k < 3; ++k)
                d[k] = uint8_t(std::lround((float(s[k]) * a + float(d[k]) * da * (1 - a)) / out));
            d[3] = uint8_t(std::lround(out * 255));
        }
    }
}

void HudPictures::speedometer(Surface& overlay, const CarSim& sim, int x, int y) const {
    if (!ok_)
        return;
    const image::Image& panel = *get("SPEED");
    blit(overlay, panel, x, y, panel.w, panel.h);
    // The revs along the top slot, the speed in the big box, the gear in
    // the small one.
    const CarSetup& setup = sim.setup();
    const image::Image& ticks = *get("TACOMETER TICKS_HALF");
    blit(overlay, ticks, x + 14, y + 13, 138, 10, setup.maxRpm > 0 ? sim.rpm / setup.maxRpm : 0);
    const int speed = std::min(999, int(std::lround(std::fabs(sim.forwardSpeed()) * kMph)));
    char digits[12];
    std::snprintf(digits, sizeof digits, "%3d", speed);
    for (int i = 0; i < 3; ++i)
        if (digits[i] != ' ') {
            const image::Image& d = *get(std::string("DIGITAC_") + digits[i] + "_HALF");
            blit(overlay, d, x + 11 + i * 20, y + 41, d.w, d.h);
        }
    const image::Image& mph = *get("MPH");
    blit(overlay, mph, x + 30, y + 79, mph.w, mph.h);
    std::string gear = std::to_string(std::min(sim.gear, 8));
    if (sim.gear < 0)
        gear = "R";
    else if (sim.gear == 1 && std::fabs(sim.forwardSpeed()) < 0.5f)
        gear = "N"; // standing
    if (const image::Image* g = get("DIGITAC_GEAR_" + gear))
        blit(overlay, *g, x + 134, y + 39, g->w, g->h);
}

void HudPictures::damage(Surface& overlay, float damage, int x, int y, int width) const {
    if (!ok_)
        return;
    const image::Image& label = *get("DAMAGE_LABLE");
    const image::Image& bar = *get("DAMAGE");
    blit(overlay, label, x + (width - label.w) / 2, y - bar.h - label.h - 2, label.w, label.h);
    blit(overlay, bar, x, y - bar.h, width, bar.h, damage);
}

void HudPictures::clock(Surface& overlay, float seconds, int right, int y) const {
    if (!ok_)
        return;
    const int total = std::max(0, int(seconds));
    char text[16];
    std::snprintf(text, sizeof text, "%d:%02d", std::min(99, total / 60), total % 60);
    int width = 0;
    for (const char* c = text; *c; ++c)
        width += (*c == ':' ? get("DIGI_COLON") : get(std::string("DIGI_") + *c))->w;
    int x = right - width;
    for (const char* c = text; *c; ++c) {
        const image::Image& d = *(*c == ':' ? get("DIGI_COLON") : get(std::string("DIGI_") + *c));
        blit(overlay, d, x, y, d.w, d.h);
        x += d.w;
    }
}

} // namespace reluva::age
