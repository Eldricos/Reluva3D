// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/formats/shader.h"
#include "engines/idtech3/formats/lexer.h"

#include "common/fs.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace reluva::idtech3 {

namespace {

// The keyword and the rest of its line.
Directive readDirective(Lexer &lex, std::string_view keyword) {
    Directive d{fs::toLower(keyword), {}};
    while (auto arg = lex.next(false))
        d.args.emplace_back(*arg);
    return d;
}

std::string lower(const std::string &s) {
    return fs::toLower(s);
}

// A directive's arguments with any "(" and ")" tokens dropped, as vectors
// are written "( 1 0 0 )".
std::vector<std::string> plainArgs(const Directive &d) {
    std::vector<std::string> out;
    for (const std::string &a : d.args) {
        std::string t;
        for (char c : a)
            if (c != '(' && c != ')')
                t += c;
        if (!t.empty())
            out.push_back(t);
    }
    return out;
}

float num(const std::vector<std::string> &args, size_t i, float fallback = 0) {
    return i < args.size() ? std::strtof(args[i].c_str(), nullptr) : fallback;
}

bool waveFunc(const std::string &name, gfx::WaveFunc &out) {
    static const std::pair<const char *, gfx::WaveFunc> kFuncs[] = {
        {"sin", gfx::WaveFunc::kSin},
        {"triangle", gfx::WaveFunc::kTriangle},
        {"square", gfx::WaveFunc::kSquare},
        {"sawtooth", gfx::WaveFunc::kSawtooth},
        {"inversesawtooth", gfx::WaveFunc::kInverseSawtooth},
        {"noise", gfx::WaveFunc::kNoise},
    };
    for (const auto &[n, f] : kFuncs)
        if (lower(name) == n) {
            out = f;
            return true;
        }
    return false;
}

// "<func> <base> <amplitude> <phase> <frequency>" starting at args[i].
gfx::Wave readWave(const std::vector<std::string> &args, size_t i) {
    gfx::Wave w;
    if (i < args.size())
        waveFunc(args[i], w.func);
    w.base = num(args, i + 1);
    w.amplitude = num(args, i + 2);
    w.phase = num(args, i + 3);
    w.frequency = num(args, i + 4);
    return w;
}

// Returns false for a directive it doesn't know.
// Raven's surface sprites (surfaceSprites and its ss* options), as the
// original reads them: a value out of range leaves what was there; an
// option before surfaceSprites changes values it then resets. False for
// an option it doesn't know.
bool applySurfaceSprites(SurfaceSprites &ss, const std::string &keyword, const std::vector<std::string> &args) {
    using Type = SurfaceSprites::Type;
    using Facing = SurfaceSprites::Facing;
    auto has = [&](size_t i) { return args.size() > i; };
    if (keyword == "surfacesprites") {
        static const std::pair<const char *, Type> kTypes[] = {
            {"vertical", Type::kVertical}, {"oriented", Type::kOriented}, {"effect", Type::kEffect}};
        Type type = Type::kNone;
        for (const auto &[name, t] : kTypes)
            if (has(0) && lower(args[0]) == name)
                type = t;
        const float width = num(args, 1);
        const float height = num(args, 2);
        const float density = num(args, 3);
        const float fadeDist = num(args, 4);
        if (type == Type::kNone || !has(4) || width <= 0 || height <= 0 || density <= 0 || fadeDist < 32)
            return true;
        ss = SurfaceSprites{};
        ss.type = type;
        ss.width = width;
        ss.height = height;
        ss.density = density;
        ss.fadeDist = fadeDist;
        ss.fadeMax = fadeDist * 1.33f;
        return true;
    }
    if (keyword == "ssfademax") {
        if (has(0) && num(args, 0) > ss.fadeDist)
            ss.fadeMax = num(args, 0);
    } else if (keyword == "ssfadescale") {
        if (has(0))
            ss.fadeScale = num(args, 0);
    } else if (keyword == "ssvariance") {
        if (has(0) && num(args, 0) >= 0)
            ss.variance[0] = num(args, 0);
        if (has(1) && num(args, 1) >= 0)
            ss.variance[1] = num(args, 1);
    } else if (keyword == "sshangdown" || keyword == "ssanyangle" || keyword == "ssfaceup") {
        // Only one of them counts.
        if (ss.facing == Facing::kNormal)
            ss.facing = keyword == "sshangdown" ? Facing::kDown : keyword == "ssanyangle" ? Facing::kAny : Facing::kUp;
    } else if (keyword == "sswind") {
        if (has(0) && num(args, 0) >= 0) {
            ss.wind = num(args, 0);
            if (ss.windIdle <= 0) // it usually sways as it blows
                ss.windIdle = ss.wind;
        }
    } else if (keyword == "sswindidle") {
        if (has(0) && num(args, 0) >= 0)
            ss.windIdle = num(args, 0);
    } else if (keyword == "ssvertskew") {
        if (has(0) && num(args, 0) >= 0)
            ss.vertSkew = num(args, 0);
    } else if (keyword == "ssfxduration") {
        if (has(0) && num(args, 0) > 0)
            ss.fxDuration = num(args, 0);
    } else if (keyword == "ssfxgrow") {
        if (has(0) && num(args, 0) >= 0)
            ss.fxGrow[0] = num(args, 0);
        if (has(1) && num(args, 1) >= 0)
            ss.fxGrow[1] = num(args, 1);
    } else if (keyword == "ssfxalpharange") {
        for (size_t i = 0; i < 2; ++i)
            if (has(i) && num(args, i) >= 0 && num(args, i) <= 1)
                (i == 0 ? ss.fxAlphaStart : ss.fxAlphaEnd) = num(args, i);
    } else if (keyword == "ssfxweather") {
        if (ss.type == Type::kEffect)
            ss.type = Type::kWeather;
    } else {
        return false;
    }
    return true;
}

bool applyStageDirective(ShaderStage &stage, const Directive &d) {
    const std::vector<std::string> args = plainArgs(d);
    const std::string a0 = args.empty() ? std::string() : lower(args[0]);
    if (d.keyword == "map" || d.keyword == "clampmap") {
        stage.clamp = d.keyword == "clampmap";
        if (a0 == "$lightmap")
            stage.lightmap = true;
        else if (a0 == "$whiteimage" || a0 == "*white") // the renderer's built-in white image
            stage.whiteImage = true;
        else if (!args.empty())
            stage.maps = {args[0]};
    } else if (d.keyword == "animmap" || d.keyword == "clampanimmap" || d.keyword == "oneshotanimmap") {
        // Raven's: clampAnimMap's frames clamped as clampMap's;
        // oneshotAnimMap's played once, then held on the last.
        stage.animFps = num(args, 0);
        stage.clamp = d.keyword == "clampanimmap";
        stage.animOnce = d.keyword == "oneshotanimmap";
        if (args.size() >= 2)
            stage.maps.assign(args.begin() + 1, args.end());
    } else if (d.keyword == "videomap") {
        if (!args.empty())
            stage.videoMap = args[0];
    } else if (d.keyword == "surfacesprites" || d.keyword.rfind("ss", 0) == 0) {
        return applySurfaceSprites(stage.sprites, d.keyword, args);
    } else if (d.keyword == "blendfunc") {
        // Shorthands, and the explicit two-factor form.
        if (a0 == "add") {
            stage.blendSrc = "gl_one";
            stage.blendDst = "gl_one";
        } else if (a0 == "filter") {
            stage.blendSrc = "gl_dst_color";
            stage.blendDst = "gl_zero";
        } else if (a0 == "blend") {
            stage.blendSrc = "gl_src_alpha";
            stage.blendDst = "gl_one_minus_src_alpha";
        } else if (args.size() >= 2) {
            stage.blendSrc = a0;
            stage.blendDst = lower(args[1]);
        }
    } else if (d.keyword == "rgbgen") {
        using R = ShaderStage::RgbGen;
        static const std::pair<const char *, R> kGens[] = {
            {"identity", R::kIdentity},
            {"identitylighting", R::kIdentityLighting},
            {"vertex", R::kVertex},
            {"exactvertex", R::kExactVertex},
            {"oneminusvertex", R::kOneMinusVertex},
            {"lightingdiffuse", R::kLightingDiffuse},
            {"wave", R::kWave},
            {"const", R::kConst},
            {"entity", R::kEntity},
            {"oneminusentity", R::kOneMinusEntity},
        };
        for (const auto &[n, g] : kGens)
            if (a0 == n)
                stage.rgbGen = g;
        // The vertices' alpha too, unless an alphaGen came first: the
        // original's rule, which blended pictures over the menus rely on
        // (Jedi Outcast's "white" has no alphaGen).
        if (stage.rgbGen == R::kVertex && stage.alphaGen == ShaderStage::AlphaGen::kDefault)
            stage.alphaGen = ShaderStage::AlphaGen::kVertex;
        if (stage.rgbGen == R::kWave)
            stage.rgbWave = readWave(args, 1);
        else if (stage.rgbGen == R::kConst)
            stage.rgbConst = {num(args, 1), num(args, 2), num(args, 3)};
    } else if (d.keyword == "alphagen") {
        using A = ShaderStage::AlphaGen;
        static const std::pair<const char *, A> kGens[] = {
            {"identity", A::kIdentity},
            {"vertex", A::kVertex},
            {"oneminusvertex", A::kOneMinusVertex},
            {"wave", A::kWave},
            {"const", A::kConst},
            {"entity", A::kEntity},
            {"oneminusentity", A::kOneMinusEntity},
            {"lightingspecular", A::kLightingSpecular},
            {"portal", A::kPortal},
        };
        for (const auto &[n, g] : kGens)
            if (a0 == n)
                stage.alphaGen = g;
        if (stage.alphaGen == A::kWave)
            stage.alphaWave = readWave(args, 1);
        else if (stage.alphaGen == A::kConst)
            stage.alphaConst = num(args, 1, 1);
        else if (stage.alphaGen == A::kPortal)
            stage.portalRange = num(args, 1, 256);
    } else if (d.keyword == "tcgen" || d.keyword == "texgen") {
        using T = ShaderStage::TcGen;
        if (a0 == "base" || a0 == "texture")
            stage.tcGen = T::kBase;
        else if (a0 == "lightmap")
            stage.tcGen = T::kLightmap;
        else if (a0 == "environment")
            stage.tcGen = T::kEnvironment;
        else if (a0 == "vector") {
            stage.tcGen = T::kVector;
            stage.tcVectors = {
                {{num(args, 1), num(args, 2), num(args, 3)}, {num(args, 4), num(args, 5), num(args, 6)}}};
        }
    } else if (d.keyword == "tcmod") {
        using M = ShaderStage::TcMod::Type;
        ShaderStage::TcMod m{M::kEntityTranslate, {}, {}};
        if (a0 == "rotate")
            m = {M::kRotate, {num(args, 1)}, {}};
        else if (a0 == "scale")
            m = {M::kScale, {num(args, 1), num(args, 2)}, {}};
        else if (a0 == "scroll")
            m = {M::kScroll, {num(args, 1), num(args, 2)}, {}};
        else if (a0 == "stretch")
            m = {M::kStretch, {}, readWave(args, 1)};
        else if (a0 == "transform")
            m = {M::kTransform,
                 {num(args, 1), num(args, 2), num(args, 3), num(args, 4), num(args, 5), num(args, 6)},
                 {}};
        else if (a0 == "turb") {
            // "turb [sin] <base> <amplitude> <phase> <frequency>": the
            // function name is optional.
            gfx::WaveFunc f;
            const size_t at = args.size() > 1 && waveFunc(args[1], f) ? 2 : 1;
            m = {M::kTurb, {}, {}};
            m.wave.base = num(args, at);
            m.wave.amplitude = num(args, at + 1);
            m.wave.phase = num(args, at + 2);
            m.wave.frequency = num(args, at + 3);
        } else if (a0 != "entitytranslate")
            return false;
        stage.tcMods.push_back(m);
    } else if (d.keyword == "alphafunc") {
        stage.alphaFunc = a0 == "gt0"     ? gfx::AlphaTest::kGt0
                          : a0 == "lt128" ? gfx::AlphaTest::kLt128
                          : a0 == "ge128" ? gfx::AlphaTest::kGe128
                                          : gfx::AlphaTest::kNone;
    } else if (d.keyword == "depthfunc") {
        stage.depthEqual = a0 == "equal";
    } else if (d.keyword == "depthwrite") {
        stage.depthWrite = true;
    } else if (d.keyword == "detail") {
        stage.detail = true;
    } else {
        return false;
    }
    return true;
}

// Returns false for a directive it doesn't know.
bool applyShaderDirective(ShaderDef &def, const Directive &d) {
    const std::vector<std::string> args = plainArgs(d);
    const std::string a0 = args.empty() ? std::string() : lower(args[0]);
    if (d.keyword == "surfaceparm") {
        if (!args.empty())
            def.surfaceParms.push_back(a0);
    } else if (d.keyword == "skyparms") {
        def.sky = true;
        def.skyFarBox = a0 == "-" ? std::string() : args.empty() ? std::string() : args[0];
        if (const float h = num(args, 1); h > 0)
            def.cloudHeight = h;
    } else if (d.keyword == "fogparms") {
        def.fog = true;
        def.fogColor = {num(args, 0), num(args, 1), num(args, 2)};
        def.fogDistance = num(args, 3);
    } else if (d.keyword == "cull") {
        if (a0 == "none" || a0 == "disable" || a0 == "twosided")
            def.cull = ShaderDef::Cull::kNone;
        else if (a0 == "back" || a0 == "backside" || a0 == "backsided")
            def.cull = ShaderDef::Cull::kBack;
        else
            def.cull = ShaderDef::Cull::kFront;
    } else if (d.keyword == "deformvertexes") {
        using T = ShaderDeform::Type;
        ShaderDeform df;
        if (a0 == "wave") {
            df.type = T::kWave;
            const float div = num(args, 1);
            df.spread = div == 0 ? 100.0f : 1.0f / div; // as the original treats a 0
            df.wave = readWave(args, 2);
        } else if (a0 == "normal") {
            df.type = T::kNormal;
            df.wave.amplitude = num(args, 1);
            df.wave.frequency = num(args, 2);
        } else if (a0 == "bulge") {
            df.type = T::kBulge;
            df.width = num(args, 1);
            df.height = num(args, 2);
            df.speed = num(args, 3);
        } else if (a0 == "move") {
            df.type = T::kMove;
            df.vector = {num(args, 1), num(args, 2), num(args, 3)};
            df.wave = readWave(args, 4);
        } else if (a0 == "autosprite") {
            df.type = T::kAutosprite;
        } else if (a0 == "autosprite2") {
            df.type = T::kAutosprite2;
        } else if (a0.rfind("text", 0) == 0) {
            df.type = T::kText;
        } else if (a0 == "projectionshadow") {
            df.type = T::kProjectionShadow;
        }
        def.deforms.push_back(df);
    } else if (d.keyword == "sort") {
        static const std::pair<const char *, float> kEys[] = {
            {"portal", 1}, {"sky", 2},       {"opaque", 3},   {"decal", 4},      {"seethrough", 5},
            {"banner", 6}, {"additive", 10}, {"nearest", 16}, {"underwater", 8},
        };
        def.sort = num(args, 0);
        for (const auto &[n, k] : kEys)
            if (a0 == n)
                def.sort = k;
    } else if (d.keyword == "polygonoffset") {
        def.polygonOffset = true;
    } else if (d.keyword == "nomipmaps") {
        def.noMipmaps = def.noPicmip = true;
    } else if (d.keyword == "nopicmip") {
        def.noPicmip = true;
    } else if (d.keyword == "portal") {
        def.portal = true;
    } else if (d.keyword.rfind("qer_", 0) == 0 || d.keyword.rfind("q3map_", 0) == 0 || d.keyword == "light" ||
               d.keyword == "tesssize" || d.keyword == "entitymergable" || d.keyword == "fogonly" ||
               d.keyword == "lightning" || d.keyword == "cloudparms" || d.keyword == "lightcolor" ||
               d.keyword == "notc" || d.keyword == "sun") {
        // For the editor and the map compiler, or nothing the renderer uses.
        // Raven's: lightColor only the compiler reads; noTC keeps textures
        // from being compressed, which Reluva never does; sun sets the light
        // that falls on models in a level without a light grid, which none
        // of Jedi Outcast's lacks (shader.md).
    } else {
        return false;
    }
    return true;
}

} // namespace

bool ShaderDef::hasSurfaceParm(std::string_view parm) const {
    const std::string p = fs::toLower(parm);
    return std::find(surfaceParms.begin(), surfaceParms.end(), p) != surfaceParms.end();
}

Error parseShaderFile(std::string_view text, const std::string &file, std::vector<ShaderDef> &out) {
    Lexer lex(text);
    auto where = [&] { return file + " line " + std::to_string(lex.line()); };
    while (auto name = lex.next()) {
        ShaderDef def;
        def.name = std::string(*name);
        def.file = file;
        auto open = lex.next();
        if (!open || *open != "{")
            return Error::fail(where() + ": expected '{' after shader " + def.name);
        bool closed = false;
        while (auto tok = lex.next()) {
            if (*tok == "}") {
                closed = true;
                break;
            }
            if (*tok == "{") {
                ShaderStage stage;
                bool stageClosed = false;
                while (auto st = lex.next()) {
                    if (*st == "}") {
                        stageClosed = true;
                        break;
                    }
                    if (*st == "{") { // no nesting inside stages; skip it whole
                        lex.skipBlock();
                        continue;
                    }
                    stage.directives.push_back(readDirective(lex, *st));
                    if (!applyStageDirective(stage, stage.directives.back()))
                        def.unknown.push_back(stage.directives.back().keyword);
                }
                if (!stageClosed)
                    return Error::fail(where() + ": shader " + def.name + " ends inside a stage");
                def.stages.push_back(std::move(stage));
                continue;
            }
            Directive d = readDirective(lex, *tok);
            if (!applyShaderDirective(def, d))
                def.unknown.push_back(d.keyword);
            def.directives.push_back(std::move(d));
        }
        if (!closed)
            return Error::fail(where() + ": shader " + def.name + " is not closed");
        out.push_back(std::move(def));
    }
    return Error::success();
}

bool isShaderScript(std::string_view name, std::string_view folder) {
    const std::string key = normalizeArchiveName(name);
    const std::string prefix = normalizeArchiveName(folder) + "/";
    return key.size() > prefix.size() + 7 && key.starts_with(prefix) && key.ends_with(".shader");
}

std::string findImage(const SearchSet &files, std::string_view name) {
    std::string base(name);
    const size_t dot = base.find_last_of('.');
    const size_t slash = base.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        base.resize(dot);
    for (const char *ext : {".tga", ".jpg", ".png"})
        if (files.has(base + ext))
            return base + ext;
    return {};
}

void ShaderLibrary::add(ShaderDef def) {
    if (_byName.try_emplace(normalizeArchiveName(def.name), _shaders.size()).second)
        _shaders.push_back(std::move(def));
}

const ShaderDef *ShaderLibrary::find(std::string_view name) const {
    // Like the original, without any extension: models and skins often
    // name "models/x/y.tga" where a script defines "models/x/y".
    std::string key = normalizeArchiveName(name);
    const size_t dot = key.find_last_of('.');
    const size_t slash = key.find_last_of('/');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        key.resize(dot);
    auto it = _byName.find(key);
    return it == _byName.end() ? nullptr : &_shaders[it->second];
}

void ShaderLibrary::load(const SearchSet &files, std::string_view folder, std::vector<std::string> *warnings) {
    _shaders.clear();
    _byName.clear();
    std::vector<uint8_t> data;
    for (const std::string &name : files.list()) {
        if (!isShaderScript(name, folder))
            continue;
        if (Error err = files.read(name, data)) {
            if (warnings)
                warnings->push_back(err.message);
            continue;
        }
        std::vector<ShaderDef> defs;
        Error err =
            parseShaderFile(std::string_view(reinterpret_cast<const char *>(data.data()), data.size()), name, defs);
        if (err && warnings)
            warnings->push_back(err.message);
        // Keep what parsed before a problem.
        for (ShaderDef &d : defs)
            add(std::move(d));
    }
}

} // namespace reluva::idtech3
