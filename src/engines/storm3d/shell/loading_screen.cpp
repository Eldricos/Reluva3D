// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/shell/loading_screen.h"

#include "common/text.h"
#include "engines/storm3d/formats/bzn.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace reluva::storm3d {

namespace {

// The screen as the game's draws place it (the vertices placed back
// through the camera at the origin, looking along +z, 45 degrees high):
// the logo 35 ahead, unturned.
constexpr float kLogoDistance = 35;
constexpr float kFieldOfView = 0.7853982f;
constexpr float kAspect = 640.0f / 480.0f;
// Its light, fitted to the logo's vertex colours: towards it, in the
// camera's (and the world's) left-handed axes, and its colour.
constexpr gfx::Vec3 kToLight{-0.457f, 0.548f, -0.701f};
constexpr gfx::Vec3 kLightColour{0.94f, 0.96f, 0.94f};
// The bar: 10 high at y 455 from x 20, its colour as drawn, and as long
// as the share read of 600 (the frames saw it reach 520).
constexpr int kBarX = 20;
constexpr int kBarY = 455;
constexpr int kBarHeight = 10;
constexpr int kBarLength = 600;
constexpr uint8_t kBarColour[3] = {204, 102, 51};
// Files read a frame: enough to finish in a second or two.
constexpr size_t kFilesAStep = 8;

} // namespace

std::vector<std::string> LoadingScreen::filesOf(const std::vector<std::string> &definitions) {
    std::vector<std::string> files;
    std::set<std::string> seen;
    for (const std::string &odf : definitions) {
        const std::string key = text::toLower(odf);
        if (key.empty() || !seen.insert(key).second)
            continue;
        files.push_back(odf + ".odf");
        files.push_back("SOD/" + odf + ".sod");
    }
    return files;
}

Error LoadingScreen::start(gfx::Renderer3D *renderer, const std::string &map) {
    stop();
    std::vector<uint8_t> data;
    BznMap parsed;
    if (Error err = _files.read("bzn/" + map, data))
        return err;
    if (Error err = parsed.parse(data))
        return Error::fail(map + ": " + err.message);
    std::vector<std::string> definitions;
    for (const BznMap::Object &o : parsed.objects)
        definitions.push_back(o.odf);
    _toRead = filesOf(definitions);
    _done = 0;
    _renderer = renderer;
    _meshes.attach(renderer);
    _background = _meshes.model("mbgbaku");
    _logo = _meshes.model("stalogo");
    // No ambient: the logo's darkest vertices are its material's own.
    SodMeshes::Lighting lighting;
    lighting.ambient = {0, 0, 0};
    lighting.lights = {{kLightColour, kToLight}};
    _meshes.setLighting(lighting);
    _overlay.create(640, 480, PixelFormat::kRGBA32);
    return Error::success();
}

void LoadingScreen::stop() {
    _meshes.clear();
    _meshes.attach(nullptr);
    _renderer = nullptr;
    _background = _logo = nullptr;
}

bool LoadingScreen::step() {
    for (size_t i = 0; i < kFilesAStep && _done < _toRead.size(); ++i, ++_done) {
        // Read, as the game reads them before the mission starts; a model
        // that isn't there (an effect's) is skipped as the game skips it.
        std::vector<uint8_t> data;
        (void)_files.read(_toRead[_done], data);
    }
    return _done < _toRead.size();
}

void LoadingScreen::draw(uint64_t millis) {
    if (!_renderer)
        return;
    // At the origin, looking along +z: z reversed for the right-handed
    // renderer, as the Maps page does it (SodMeshes::mirrorZ).
    const gfx::Mat4 viewProj = gfx::multiply(gfx::multiply(gfx::perspective(kFieldOfView, kAspect, 1, 1000),
                                                           gfx::lookAlong({0, 0, 0}, {0, 0, -1}, {0, 1, 0})),
                                             SodMeshes::mirrorZ());
    _renderer->beginFrame(viewProj, {0, 0, 0}, Color{0, 0, 0});
    _meshes.setView({1, 0, 0}, {0, 1, 0}, double(millis) / 1000);
    if (_background)
        _meshes.draw(*_background, gfx::identity(), true);
    if (_logo) {
        gfx::Mat4 at = gfx::identity();
        at[14] = kLogoDistance;
        _meshes.draw(*_logo, at);
    }
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), uint8_t(0));
    const int length = int(std::lround(progress() * float(kBarLength)));
    for (int y = kBarY; y < kBarY + kBarHeight; ++y)
        for (int x = kBarX; x < kBarX + length && x < _overlay.w; ++x) {
            uint8_t *p = _overlay.row(y) + size_t(x) * 4;
            p[0] = kBarColour[0], p[1] = kBarColour[1], p[2] = kBarColour[2], p[3] = 255;
        }
    _renderer->endFrame(&_overlay);
}

} // namespace reluva::storm3d
