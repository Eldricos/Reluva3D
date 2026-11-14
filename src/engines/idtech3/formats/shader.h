// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "graphics/gfx3d.h"
#include "common/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reluva::idtech3 {

// One directive inside a shader or stage: its keyword (lower case) and the
// rest of its line's tokens as written.
struct Directive {
    std::string keyword;
    std::vector<std::string> args;
};

// Raven's surface sprites (Jedi Outcast on): a stage drawn not over its
// surface but as small quads scattered on it, grass, vines, bubbles,
// weather (docs/engines/idtech3/formats/shader.md). Sizes in units,
// times in milliseconds.
struct SurfaceSprites {
    // vertical: standing up (or hanging down), turned about their height
    // to face the camera; oriented: facing it wholly; effect: oriented,
    // each growing and fading over its life, then starting again
    // elsewhere; weather: an effect shown only in rain or snow.
    enum class Type : uint8_t { kNone, kVertical, kOriented, kEffect, kWeather } type = Type::kNone;
    float width = 0;
    float height = 0;
    float density = 0;  // about how far apart, in units
    float fadeDist = 0; // where they start to fade
    float fadeMax = 0;  // where they're gone
    float fadeScale = 0;
    std::array<float, 2> variance{}; // width, height: up to this much bigger
    enum class Facing : uint8_t { kNormal, kUp, kDown, kAny } facing = Facing::kNormal;
    float wind = 0;
    float windIdle = 0; // how far they sway with no wind
    float vertSkew = 0;
    float fxDuration = 1000;
    std::array<float, 2> fxGrow{}; // width, height, over an effect's life
    float fxAlphaStart = 1;
    float fxAlphaEnd = 0;
};

// A rendering pass ("stage") of a shader, as described in
// docs/engines/idtech3/formats/shader.md. Every directive is also kept, as
// written, in `directives`.
struct ShaderStage {
    std::vector<std::string> maps; // one image, or animMap's frames
    float animFps = 0;             // animMap's rate; 0 = not animated
    bool animOnce = false;         // oneshotAnimMap (Raven's): the last frame held
    std::string videoMap;          // videoMap (Raven's): a RoQ video, video/<name>
    bool clamp = false;            // clampMap, clampAnimMap
    bool lightmap = false;         // map $lightmap
    bool whiteImage = false;       // map $whiteimage
    // GL_* names, lower case; empty = opaque
    std::string blendSrc;
    std::string blendDst;

    enum class RgbGen : uint8_t {
        kDefault,
        kIdentity,
        kIdentityLighting,
        kVertex,
        kExactVertex,
        kOneMinusVertex,
        kLightingDiffuse,
        kWave,
        kConst,
        kEntity,
        kOneMinusEntity,
    } rgbGen = RgbGen::kDefault;
    gfx::Wave rgbWave;
    std::array<float, 3> rgbConst{1, 1, 1};
    enum class AlphaGen : uint8_t {
        kDefault,
        kIdentity,
        kVertex,
        kOneMinusVertex,
        kWave,
        kConst,
        kEntity,
        kOneMinusEntity,
        kLightingSpecular,
        kPortal,
    } alphaGen = AlphaGen::kDefault;
    gfx::Wave alphaWave;
    float alphaConst = 1;
    float portalRange = 256; // alphaGen portal's distance

    enum class TcGen : uint8_t { kDefault, kBase, kLightmap, kEnvironment, kVector } tcGen = TcGen::kDefault;
    std::array<std::array<float, 3>, 2> tcVectors{};
    struct TcMod {
        enum class Type : uint8_t { kRotate, kScale, kScroll, kStretch, kTransform, kTurb, kEntityTranslate } type;
        std::array<float, 6> v{}; // rotate: degrees/s; scale, scroll: s t; transform: m00 m01 m10 m11 t0 t1
        gfx::Wave wave;           // stretch, turb (turb ignores func)
    };
    std::vector<TcMod> tcMods; // in order

    gfx::AlphaTest alphaFunc = gfx::AlphaTest::kNone;
    bool depthEqual = false; // depthFunc equal
    bool depthWrite = false; // depthWrite (opaque stages write depth anyway)
    bool detail = false;
    SurfaceSprites sprites; // type kNone: drawn over the surface as usual

    std::vector<Directive> directives;
};

struct ShaderDeform {
    enum class Type : uint8_t {
        kWave,
        kNormal,
        kBulge,
        kMove,
        kAutosprite,
        kAutosprite2,
        kText,
        kProjectionShadow,
        kOther,
    } type = Type::kOther;
    float spread = 0;              // wave: 1 / div
    gfx::Wave wave;                // wave, normal (amplitude, frequency), move
    std::array<float, 3> vector{}; // move
    // bulge
    float width = 0;
    float height = 0;
    float speed = 0;
};

struct ShaderDef {
    std::string name;                      // as written, e.g. "textures/borg/borgfloor1"
    std::string file;                      // the .shader file it came from
    std::vector<std::string> surfaceParms; // lower case
    std::vector<ShaderStage> stages;
    std::vector<Directive> directives; // the shader-level ones

    enum class Cull : uint8_t { kFront, kBack, kNone } cull = Cull::kFront; // the side that's hidden
    std::vector<ShaderDeform> deforms;
    float sort = 0; // 0 = not given; see sortKey() in world_renderer.cpp
    bool polygonOffset = false;
    bool noMipmaps = false;
    bool noPicmip = false;
    bool portal = false;

    bool sky = false;      // skyParms
    std::string skyFarBox; // e.g. "env/space1" for env/space1_rt.tga...; empty for none
    float cloudHeight = 512;
    bool fog = false; // fogParms
    std::array<float, 3> fogColor{};
    float fogDistance = 0; // where the fog becomes opaque

    // Directives this parser doesn't know, lower case, shader and stage
    // ones alike, for finding out what a game adds.
    std::vector<std::string> unknown;

    bool hasSurfaceParm(std::string_view parm) const;
};

// Parse every shader in one .shader file's text, appending to `out`.
// `file` is recorded in each definition and used in messages.
Error parseShaderFile(std::string_view text, const std::string &file, std::vector<ShaderDef> &out);

// The file an image name refers to: shaders and maps name images with or
// without an extension, and the original finds the .tga or the .jpg
// whichever is written (.png too, for mods). Empty if there's none.
std::string findImage(const SearchSet &files, std::string_view name);

// Whether a file in the game's files is a shader script: <folder>/*.shader,
// whatever its case.
bool isShaderScript(std::string_view name, std::string_view folder);

// Every shader of a game: all <folder>/*.shader files in a search set
// (GameInfo::shaderDir). When two files define a name, the first parsed
// wins; files are parsed in name order. Problems with single files are
// collected, not fatal.
class ShaderLibrary {
public:
    void load(const SearchSet &files, std::string_view folder, std::vector<std::string> *warnings = nullptr);
    void add(ShaderDef def);

    // By name, ignoring case, slash direction and any extension (a model's
    // "models/x/y.tga" finds the script "models/x/y", as in the original);
    // nullptr if undefined (then the engine uses the image of that name,
    // see shader.md).
    const ShaderDef *find(std::string_view name) const;
    size_t size() const { return _shaders.size(); }
    const std::vector<ShaderDef> &all() const { return _shaders; }

private:
    std::vector<ShaderDef> _shaders;
    std::unordered_map<std::string, size_t> _byName;
};

} // namespace reluva::idtech3
