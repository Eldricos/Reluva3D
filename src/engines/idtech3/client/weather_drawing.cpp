// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/client/weather_drawing.h"

#include "common/archive.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/render/shader_cache.h"
#include "image/image.h"

#include <cmath>
#include <cstring>

namespace reluva::idtech3 {

namespace {

constexpr const char *kRainPicture = "$weather/rain";

std::array<float, 3> vecAt(const std::vector<uint8_t> &refdef, size_t at) {
    std::array<float, 3> v{};
    if (refdef.size() >= at + sizeof v)
        std::memcpy(v.data(), refdef.data() + at, sizeof v);
    return v;
}

// A picture by R_LoadImage's file types in turn: jk2sp.exe's .jpg then
// .tga (0x46b524), jk2mp.exe's .jpg, .png then .tga (0x47c084).
bool loadPicture(const SearchSet &files, const std::string &name, bool withPng, image::Image &out) {
    for (const char *type : {".jpg", ".png", ".tga"}) {
        if (!withPng && std::strcmp(type, ".png") == 0)
            continue;
        std::vector<uint8_t> data;
        if (!files.read(name + type, data) && !image::decode(data, out))
            return true;
    }
    return false;
}

} // namespace

bool loadWeatherPicture(const SearchSet &files, const std::string &name, WeatherPicture &out) {
    image::Image picture;
    if (!loadPicture(files, name, true, picture))
        return false;
    out.rgba = std::move(picture.rgba);
    out.width = picture.w;
    out.height = picture.h;
    return true;
}

int WeatherDrawing::material(WorldEffects::Look look, ShaderCache &shaders, const SearchSet &files) {
    const auto which = size_t(look);
    if (_materials[which] >= 0)
        return _materials[which];
    ShaderDef &def = _shaders[which];
    def = ShaderDef{};
    def.cull = ShaderDef::Cull::kNone;
    def.noMipmaps = true;
    def.noPicmip = true;
    ShaderStage stage;
    stage.rgbGen = ShaderStage::RgbGen::kExactVertex;
    stage.alphaGen = ShaderStage::AlphaGen::kVertex;
    stage.blendSrc = "gl_src_alpha";
    stage.blendDst = "gl_one_minus_src_alpha";
    if (look == WorldEffects::Look::kRain) {
        // The rain's picture, not mipmapped and drawn nearest: single
        // player's with its red made its alpha and clamped (0x48e346),
        // multiplayer's as it is (0x49bc11), a .jpg without alpha.
        const bool single = _engine == WorldEffects::Engine::kSinglePlayer;
        image::Image picture;
        if (loadPicture(files, WorldEffects::kRainImage, !single, picture)) {
            if (single)
                for (size_t i = 0; i + 3 < picture.rgba.size(); i += 4)
                    picture.rgba[i + 3] = picture.rgba[i];
            shaders.addImage(kRainPicture, picture, single, true);
        }
        def.name = kRainPicture;
        stage.maps = {kRainPicture};
        stage.clamp = single;
    } else {
        // No picture (the renderer's white): the snowflakes and winds'
        // boxes blended (0x49baa1), the fog added (0x499efa).
        def.name = look == WorldEffects::Look::kFog ? "$weather/fog" : "$weather/plain";
        stage.whiteImage = true;
        if (look == WorldEffects::Look::kFog)
            stage.blendDst = "gl_one";
    }
    def.stages.push_back(stage);
    _materials[which] = shaders.material(def, Lighting::kEffect);
    return _materials[which];
}

void WeatherDrawing::frame(WorldEffects &weather, const std::vector<uint8_t> &refdef, float seconds, int frameHeight,
                           ShaderCache &shaders, const SearchSet &files, SceneDrawer &scene) {
    // refdef_t: its height at 12, fov y at 20, the eye at 24, the axes
    // from 36: forward, left, up.
    WorldEffects::ViewParms view;
    view.eye = vecAt(refdef, 24);
    view.forward = vecAt(refdef, 36);
    view.left = vecAt(refdef, 48);
    view.up = vecAt(refdef, 60);
    const float fovY = readFloat(refdef, 20);
    const int32_t height = readInt(refdef, 12);
    const float pixels = float(height > 0 ? height : 480) * float(std::max(frameHeight, 1)) / 480.0f;
    if (fovY > 0 && fovY < 180)
        view.pixel = 2 * std::tan(fovY * 3.14159265f / 360) / pixels;
    weather.update(seconds, view);
    std::vector<WorldEffects::Batch> batches;
    weather.draw(view, batches);
    std::vector<SceneDrawer::WorldEffectBatch> out;
    for (WorldEffects::Batch &b : batches) {
        SceneDrawer::WorldEffectBatch o;
        o.material = material(b.look, shaders, files);
        o.vertices = std::move(b.vertices);
        if (b.look == WorldEffects::Look::kFog) {
            o.ownView = true;
            o.viewProj = WorldEffects::fogView();
        }
        out.push_back(std::move(o));
    }
    scene.setWorldEffects(std::move(out));
}

SurfaceSpriteWeather weatherSprites(const WorldEffects &weather, CvarStore &cvars) {
    // Weather sprites in full while rain falls on the eye or snow round
    // it; the wind as strong as the rain leans (x 0.3), gusting every 300
    // over that seconds (none under 1); blowing the way the rain's wind,
    // else the snow's, blows to.
    SurfaceSpriteWeather sprites = surfaceSpriteSettings(cvars);
    if (weather.fallingHere())
        sprites.surfaceWeather = 1;
    if (float speed = 0; weather.windSpeed(speed)) {
        speed *= 0.3f;
        sprites.windSpeed = speed;
        sprites.windGust = speed >= 1 ? 300 / speed : 0;
    }
    if (std::array<float, 3> wind{}; weather.windVector(wind)) {
        // vectoangles' yaw of the way it blows to: none without a wind.
        sprites.windAngle = wind[0] == 0 && wind[1] == 0 ? 0 : std::atan2(-wind[1], -wind[0]) * 57.2957795f;
        if (sprites.windAngle < 0)
            sprites.windAngle += 360;
    }
    return sprites;
}

} // namespace reluva::idtech3
