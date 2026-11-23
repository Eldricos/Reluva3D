// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/ui/sky.h"

#include "engines/tpw/formats/sam.h"
#include "engines/tpw/textures.h"

#include <algorithm>
#include <cmath>

namespace reluva::tpw {

namespace {

constexpr float kHalfPi = 1.5707963f;
// How high the horizon band is, in radians, half of it below the horizon:
// a guess.
constexpr float kBandHeight = 0.44f;
// How far the clouds' texture repeats over its plane: a guess.
constexpr float kCloudScale = 0.16f;

// Its sky_cyl holds the band twice, one above the other; the upper is used.
const uint8_t *bandTexel(const image::Image &img, float u, float rise) {
    const int half = img.h / 2;
    const int x = std::clamp(int(u * float(img.w)), 0, img.w - 1);
    const int y = std::clamp(int((1 - rise) * float(half - 1)), 0, half - 1);
    return img.pixel(x, y);
}

const uint8_t *wrapped(const image::Image &img, float u, float v) {
    const int x = int((u - std::floor(u)) * float(img.w)) % img.w;
    const int y = int((v - std::floor(v)) * float(img.h)) % img.h;
    return img.pixel(x, y);
}

} // namespace

Error Sky::load(const SearchSet &files, const std::string &folder) {
    *this = {};
    const std::string sky = folder + "/sky/";
    bool any = false;
    for (auto [name, out] : {std::pair{"sky_rgb.tga", &colours}, {"sky.tga", &clouds}, {"sky_cyl.tga", &horizon}})
        if (!loadPicture(files, sky + name, *out) && out->w > 0)
            any = true;
    // The world's settings over the levels' own, which give the defaults.
    for (const std::string &settings : {std::string("levels/Standard.sam"), folder + "/Standard.sam"}) {
        std::vector<uint8_t> data;
        SamFile sam;
        if (files.read(settings, data) || sam.parse(data))
            continue;
        if (sam.find("ThemeEngine.FogColour")) {
            const uint32_t rgb = uint32_t(sam.number("ThemeEngine.FogColour"));
            fog = {uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb)};
        }
        if (sam.find("LightNormal.X"))
            light = {float(sam.number("LightNormal.X")), float(sam.number("LightNormal.Y")),
                     float(sam.number("LightNormal.Z"))};
    }
    hasPictures = any;
    if (!any)
        return Error::fail("no sky in " + sky);
    return Error::success();
}

void drawSky(Surface &screen, const soft3d::OrbitCamera &camera, const Sky &sky) {
    for (int y = 0; y < screen.h; ++y)
        for (int x = 0; x < screen.w; ++x) {
            // The ray through the pixel, in the viewer's world (z up).
            const soft3d::Vec ray =
                soft3d::normalize(camera.forward + camera.right * ((float(x) + 0.5f - camera.halfW) / camera.focal) +
                                  camera.up * ((camera.halfH - float(y) - 0.5f) / camera.focal));
            uint8_t *p = screen.row(y) + x * 4;
            p[3] = 255;
            const float rise = std::asin(std::clamp(ray.z, -1.0f, 1.0f));
            const float up = std::max(rise, 0.0f);
            if (rise < -kBandHeight / 2 || !sky.hasPictures) {
                p[0] = sky.fog.r;
                p[1] = sky.fog.g;
                p[2] = sky.fog.b;
                continue;
            }
            float colour[3] = {255, 255, 255};
            // The dome: overhead at the picture's middle, the horizon at its edge.
            const float across = std::sqrt(ray.x * ray.x + ray.y * ray.y);
            const float out = (1 - up / kHalfPi) * 0.5f;
            const float dx = across > 0 ? ray.x / across : 0;
            const float dy = across > 0 ? ray.y / across : 0;
            if (sky.colours.w > 0) {
                const uint8_t *c =
                    sky.colours.pixel(std::clamp(int((0.5f + dx * out) * float(sky.colours.w)), 0, sky.colours.w - 1),
                                      std::clamp(int((0.5f + dy * out) * float(sky.colours.h)), 0, sky.colours.h - 1));
                for (int i = 0; i < 3; ++i)
                    colour[i] = c[i];
            }
            // The clouds, on a plane overhead, tinting the colours.
            if (sky.clouds.w > 0) {
                const float reach = 1 / std::max(std::tan(up), 0.05f);
                const uint8_t *c =
                    wrapped(sky.clouds, 0.5f + dx * reach * kCloudScale, 0.5f + dy * reach * kCloudScale);
                for (int i = 0; i < 3; ++i)
                    colour[i] = colour[i] * float(c[i]) / 255;
            }
            // The band round the horizon, over both as its alpha says.
            if (sky.horizon.w > 0 && rise < kBandHeight / 2) {
                const float around = std::atan2(ray.y, ray.x) / (4 * kHalfPi) + 0.5f;
                const uint8_t *c = bandTexel(sky.horizon, around, rise / kBandHeight + 0.5f);
                const float a = float(c[3]) / 255;
                for (int i = 0; i < 3; ++i)
                    colour[i] = colour[i] * (1 - a) + float(c[i]) * a;
            }
            for (int i = 0; i < 3; ++i)
                p[i] = uint8_t(std::clamp(colour[i], 0.0f, 255.0f));
        }
}

} // namespace reluva::tpw
