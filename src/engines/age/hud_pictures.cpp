// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/hud_pictures.h"

#include "common/font.h"
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
    std::vector<std::string> names{"SPEED_TICKS",    "DAMAGE",         "DAMAGE_LABLE",  "DIGI_COLON",
                                   "DIGITAC_GEAR_D", "DIGITAC_GEAR_N", "DIGITAC_GEAR_R"};
    for (int d = 0; d < 10; ++d) {
        names.push_back("DIGITAC_" + std::to_string(d));
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
    // The clock's digits have no alpha: on the game's screens their black
    // is see-through, so their brightness is taken as it.
    for (auto& [name, im] : images_)
        if (name.rfind("DIGI_", 0) == 0)
            for (size_t i = 0; i + 3 < im.rgba.size(); i += 4) {
                uint8_t* p = &im.rgba[i];
                const int a = std::max({p[0], p[1], p[2]});
                for (int k = 0; k < 3 && a > 0; ++k)
                    p[k] = uint8_t(p[k] * 255 / a);
                p[3] = uint8_t(a);
            }
    for (const char* n : {"CD_BG", "CD_PLAY_DN", "CD_STOP_DN", "CD_PREV", "CD_NEXT"}) {
        Tex t;
        if (!loadTexture(files, n, t))
            cd_[n] = std::move(t.image);
    }
    Tex places;
    places_ = loadTexture(files, "opp_icon", places) ? image::Image{} : std::move(places.image);
    return ok_;
}

const image::Image* HudPictures::get(const std::string& name) const {
    auto it = images_.find(name);
    return it == images_.end() ? nullptr : &it->second;
}

void HudPictures::blit(Surface& overlay, const image::Image& im, int x, int y, int w, int h, float share,
                       int from) const {
    if (im.w <= 0 || im.h <= 0 || w <= 0 || h <= 0)
        return;
    const int shown = int(std::clamp(share, 0.0f, 1.0f) * float(w));
    for (int yy = std::max(0, y); yy < std::min(overlay.h, y + h); ++yy) {
        uint8_t* row = overlay.row(yy);
        const int sy = std::min(im.h - 1, (yy - y) * im.h / h);
        for (int xx = std::max(0, x); xx < std::min(overlay.w, x + shown); ++xx) {
            const int sx = from >= 0 ? from + (xx - x) : (xx - x) * im.w / w;
            const uint8_t* s = im.pixel(std::clamp(sx, 0, im.w - 1), sy);
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

void HudPictures::gauges(Surface& overlay, const CarSim& sim, float damage) const {
    if (!ok_)
        return;
    // The speed in three places ending at x 144, its top at y 366 (no
    // leading zeros).
    const int speed = std::min(999, int(std::lround(std::fabs(sim.forwardSpeed()) * kMph)));
    char digits[12];
    std::snprintf(digits, sizeof digits, "%3d", speed);
    for (int i = 0; i < 3; ++i)
        if (digits[i] != ' ') {
            const image::Image& d = *get(std::string("DIGITAC_") + digits[i]);
            blit(overlay, d, 21 + i * 41, 366, d.w, d.h);
        }
    // The gear over the revs: D driving an automatic, the gear's number a
    // manual, N standing, R backing.
    std::string gear = sim.manual ? std::to_string(std::clamp(sim.gear, 1, 8)) : "D";
    if (sim.gear < 0)
        gear = "R";
    else if (std::fabs(sim.forwardSpeed()) < 0.5f && !sim.manual)
        gear = "N";
    if (const image::Image* g = get("DIGITAC_GEAR_" + gear))
        blit(overlay, *g, 15, 424, g->w, g->h);
    // The revs: as many of the rising bars as the engine's share of its
    // most.
    const CarSetup& setup = sim.setup();
    const image::Image& ticks = *get("SPEED_TICKS");
    blit(overlay, ticks, 8, 421, ticks.w, ticks.h, setup.maxRpm > 0 ? sim.rpm / setup.maxRpm : 0);
    // The damage meter: its label over a label-wide window of the green to
    // red bar, slid from the green end to the red as the damage grows.
    const image::Image& label = *get("DAMAGE_LABLE");
    const image::Image& bar = *get("DAMAGE");
    const int from = int(std::lround(std::clamp(damage, 0.0f, 1.0f) * float(std::max(0, bar.w - label.w))));
    blit(overlay, bar, 8, 467, label.w, label.h, 1, from);
    blit(overlay, label, 8, 467, label.w, label.h);
}

bool HudPictures::cdPlayer(Surface& overlay, int x, int y, bool playing, int pressed, const std::string& text) const {
    // The buttons' places in CD_BG: play, stop, then previous and next.
    constexpr int kButtonY = 73, kPlayX = 18, kStopX = 48, kPrevX = 98, kNextX = 128;
    auto get = [&](const char* n) -> const image::Image* {
        auto it = cd_.find(n);
        return it == cd_.end() ? nullptr : &it->second;
    };
    const image::Image* bg = get("CD_BG");
    if (!bg)
        return false;
    blit(overlay, *bg, x, y, bg->w, bg->h);
    if (const image::Image* b = get(playing ? "CD_PLAY_DN" : "CD_STOP_DN"))
        blit(overlay, *b, x + (playing ? kPlayX : kStopX), y + kButtonY, b->w, b->h);
    if (pressed != 0)
        if (const image::Image* b = get(pressed < 0 ? "CD_PREV" : "CD_NEXT"))
            blit(overlay, *b, x + (pressed < 0 ? kPrevX : kNextX), y + kButtonY, b->w, b->h);
    font::drawText(overlay, x + 44, y + 38, text.substr(0, 12), {20, 30, 40}); // in its display, left of "CD player"
    return true;
}

bool HudPictures::place(Surface& overlay, int place, int x, int y, int size) const {
    // Four by four cells, row by row from the top: 1-4, 5-8, then $; each
    // stored upside down (the game draws them flipped).
    if (places_.w < 4 || place < 1 || place > 9 || size <= 0)
        return false;
    const int cell = places_.w / 4, cx = (place - 1) % 4 * cell, cy = (place - 1) / 4 * cell;
    for (int yy = std::max(0, y - size / 2); yy < std::min(overlay.h, y - size / 2 + size); ++yy) {
        const int sy = cy + cell - 1 - std::min(cell - 1, (yy - (y - size / 2)) * cell / size);
        uint8_t* row = overlay.row(yy);
        for (int xx = std::max(0, x - size / 2); xx < std::min(overlay.w, x - size / 2 + size); ++xx) {
            const uint8_t* s = places_.pixel(cx + std::min(cell - 1, (xx - (x - size / 2)) * cell / size), sy);
            if (s[3] < 128)
                continue;
            uint8_t* d = row + xx * 4;
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = 255;
        }
    }
    return true;
}

void HudPictures::clock(Surface& overlay, float seconds, int centre, int y) const {
    if (!ok_)
        return;
    const int hundredths = std::max(0, int(seconds * 100));
    char text[16];
    std::snprintf(text, sizeof text, "%02d:%02d:%02d", std::min(99, hundredths / 6000), hundredths / 100 % 60,
                  hundredths % 100);
    int width = 0;
    for (const char* c = text; *c; ++c)
        width += (*c == ':' ? get("DIGI_COLON") : get(std::string("DIGI_") + *c))->w;
    int x = centre - width / 2;
    for (const char* c = text; *c; ++c) {
        const image::Image& d = *(*c == ':' ? get("DIGI_COLON") : get(std::string("DIGI_") + *c));
        blit(overlay, d, x, y, d.w, d.h);
        x += d.w;
    }
}

} // namespace reluva::age
