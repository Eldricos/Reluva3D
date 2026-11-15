// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/raven/effect_file.h"

#include "common/fs.h"
#include "engines/idtech3/formats/lexer.h"

#include <cstdlib>
#include <functional>
#include <sstream>

namespace reluva::idtech3::raven {

namespace {

using Primitive = EffectPrimitive;

// Raven's text groups: `key value...` to the end of the line, `key [ item
// ... ]`, or `key { ... }`.
struct GroupReader {
    Lexer &lex;
    std::function<void(const std::string &key, const std::string &value)> pair;
    std::function<void(const std::string &key, const std::vector<std::string> &items)> list;
    std::function<bool(const std::string &key)> group; // reads the group's body; false: badly formed

    // Up to and including the closing "}" (or the end of the text at the
    // top level).
    bool read(bool topLevel) {
        while (auto token = lex.next()) {
            if (*token == "}")
                return !topLevel;
            const std::string key(*token);
            const auto following = lex.peek();
            if (following && *following == "{") {
                lex.next();
                if (!group(key))
                    return false;
            } else if (following && *following == "[") {
                lex.next();
                std::vector<std::string> items;
                for (auto item = lex.next(); item && *item != "]"; item = lex.next())
                    items.emplace_back(*item);
                list(key, items);
            } else {
                std::string value;
                while (auto word = lex.next(false)) {
                    if (!value.empty())
                        value += ' ';
                    value += *word;
                }
                pair(key, value);
            }
        }
        return topLevel;
    }
};

// As sscanf("%f %f ..."): the numbers at the start of the text.
std::vector<float> numbers(const std::string &text) {
    std::vector<float> out;
    const char *p = text.c_str();
    for (;;) {
        char *end = nullptr;
        const float v = std::strtof(p, &end);
        if (end == p)
            break;
        out.push_back(v);
        p = end;
    }
    return out;
}

// One number, or a range of two.
bool parseRange(const std::string &text, EffectRange &out) {
    const std::vector<float> v = numbers(text);
    if (v.empty())
        return false;
    out.min = v[0];
    out.max = v.size() > 1 ? v[1] : v[0];
    return true;
}

// Three numbers, or six (the least and the most of each).
bool parseVector(const std::string &text, std::array<EffectRange, 3> &out) {
    const std::vector<float> v = numbers(text);
    if (v.size() < 3 || v.size() == 4 || v.size() == 5)
        return false;
    for (size_t i = 0; i < 3; ++i)
        out[i] = {v[i], v.size() >= 6 ? v[i + 3] : v[i]};
    return true;
}

bool parsePoint(const std::string &text, std::array<float, 3> &out) {
    std::array<EffectRange, 3> r{};
    if (!parseVector(text, r))
        return false;
    for (size_t i = 0; i < 3; ++i)
        out[i] = r[i].min;
    return true;
}

std::vector<std::string> words(const std::string &text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string w; in >> w;)
        out.push_back(w);
    return out;
}

bool same(const std::string &a, const char *b) {
    return fs::toLower(a) == b;
}

// A group's flags: how its value changes over the life.
int32_t channelFlags(const std::string &text, std::vector<std::string> *warnings) {
    int32_t flags = 0;
    for (const std::string &w : words(text)) {
        const std::string f = fs::toLower(w);
        if (f == "linear")
            flags |= Primitive::kLinear;
        else if (f == "nonlinear")
            flags |= Primitive::kNonLinear;
        else if (f == "wave")
            flags |= Primitive::kWave;
        else if (f == "random")
            flags |= Primitive::kRandom;
        else if (f == "clamp")
            flags |= Primitive::kClamp;
        else if (warnings)
            warnings->push_back("unknown flag " + w);
    }
    return flags;
}

int32_t primitiveFlags(const std::string &text, std::vector<std::string> *warnings) {
    static const std::pair<const char *, uint32_t> kNames[] = {
        {"usemodel", Primitive::kAttachedModel},
        {"usebbox", Primitive::kUseBBox},
        {"usephysics", Primitive::kApplyPhysics},
        {"expensivephysics", Primitive::kExpensivePhysics},
        {"impactkills", Primitive::kKillOnImpact},
        {"impactfx", Primitive::kImpactRunsFx},
        {"deathfx", Primitive::kDeathRunsFx},
        {"usealpha", Primitive::kUseAlpha},
        {"emitfx", Primitive::kEmitFx},
        {"depthhack", Primitive::kDepthHack},
        {"relative", Primitive::kRelative},
        {"setshadertime", Primitive::kSetShaderTime},
    };
    uint32_t flags = 0;
    for (const std::string &w : words(text)) {
        bool known = false;
        for (const auto &[name, bit] : kNames)
            if (same(w, name)) {
                flags |= bit;
                known = true;
            }
        if (!known && warnings)
            warnings->push_back("unknown flag " + w);
    }
    return int32_t(flags);
}

int32_t spawnFlags(const std::string &text, std::vector<std::string> *warnings) {
    static const std::pair<const char *, int32_t> kNames[] = {
        {"org2fromtrace", Primitive::kOrg2FromTrace},
        {"traceimpactfx", Primitive::kTraceImpactFx},
        {"org2isoffset", Primitive::kOrg2IsOffset},
        {"cheaporgcalc", Primitive::kCheapOrgCalc},
        {"cheaporg2calc", Primitive::kCheapOrg2Calc},
        {"absolutevel", Primitive::kAbsoluteVelocity},
        {"absoluteaccel", Primitive::kAbsoluteAcceleration},
        {"orgonsphere", Primitive::kOrgOnSphere},
        {"orgoncylinder", Primitive::kOrgOnCylinder},
        {"axisfromsphere", Primitive::kAxisFromSphere},
        {"randrotaroundfwd", Primitive::kRandomRotationAroundForward},
        {"evendistribution", Primitive::kEvenDistribution},
        {"rgbcomponentinterpolation", Primitive::kRgbComponentInterpolation},
        {"affectedbywind", Primitive::kAffectedByWind},
    };
    int32_t flags = 0;
    for (const std::string &w : words(text)) {
        bool known = false;
        for (const auto &[name, bit] : kNames)
            if (same(w, name)) {
                flags |= bit;
                known = true;
            }
        if (!known && warnings)
            warnings->push_back("unknown spawn flag " + w);
    }
    return flags;
}

bool primitiveType(const std::string &name, Primitive::Type &type) {
    static const std::pair<const char *, Primitive::Type> kNames[] = {
        {"particle", Primitive::kParticle},
        {"line", Primitive::kLine},
        {"tail", Primitive::kTail},
        {"sound", Primitive::kSound},
        {"cylinder", Primitive::kCylinder},
        {"electricity", Primitive::kElectricity},
        {"emitter", Primitive::kEmitter},
        {"decal", Primitive::kDecal},
        {"orientedparticle", Primitive::kOrientedParticle},
        {"fxrunner", Primitive::kFxRunner},
        {"light", Primitive::kLight},
        {"camerashake", Primitive::kCameraShake},
    };
    for (const auto &[n, t] : kNames)
        if (same(name, n)) {
            type = t;
            return true;
        }
    return false;
}

void addMedia(Primitive &p, const std::string &key, const std::vector<std::string> &names) {
    const std::string k = fs::toLower(key);
    std::vector<std::string> *fx = k == "impactfx"  ? &p.impactFx
                                   : k == "deathfx" ? &p.deathFx
                                   : k == "emitfx"  ? &p.emitFx
                                   : k == "playfx"  ? &p.playFx
                                                    : nullptr;
    for (const std::string &name : names) {
        if (fx)
            fx->push_back(name);
        else if (k == "shader" || k == "shaders")
            p.media.emplace_back(Primitive::kShader, name);
        else if (k == "model" || k == "models")
            p.media.emplace_back(Primitive::kModel, name);
        else
            p.media.emplace_back(Primitive::kSoundFile, name);
    }
}

bool isMediaKey(const std::string &key) {
    for (const char *k :
         {"shader", "shaders", "model", "models", "sound", "sounds", "impactfx", "deathfx", "emitfx", "playfx"})
        if (same(key, k))
            return true;
    return false;
}

// One of a primitive's pairs.
void primitivePair(Primitive &p, const std::string &key, const std::string &value, std::vector<std::string> *warnings) {
    const std::string k = fs::toLower(key);
    bool ok = true;
    if (isMediaKey(k))
        addMedia(p, k, words(value));
    else if (k == "count")
        ok = parseRange(value, p.count);
    else if (k == "life")
        ok = parseRange(value, p.life);
    else if (k == "cullrange")
        p.cullRange = float(std::atoi(value.c_str()));
    else if (k == "delay")
        ok = parseRange(value, p.delay);
    else if (k == "bounce" || k == "intensity")
        ok = parseRange(value, p.elasticity);
    else if (k == "min")
        ok = parsePoint(value, p.mins);
    else if (k == "max")
        ok = parsePoint(value, p.maxs);
    else if (k == "angle" || k == "angles")
        ok = parseVector(value, p.angle);
    else if (k == "angledelta")
        ok = parseVector(value, p.angleDelta);
    else if (k == "velocity" || k == "vel")
        ok = parseVector(value, p.velocity);
    else if (k == "acceleration" || k == "accel")
        ok = parseVector(value, p.acceleration);
    else if (k == "gravity")
        ok = parseRange(value, p.gravity);
    else if (k == "density")
        ok = parseRange(value, p.density);
    else if (k == "variance")
        ok = parseRange(value, p.variance);
    else if (k == "origin")
        ok = parseVector(value, p.origin);
    else if (k == "origin2")
        ok = parseVector(value, p.origin2);
    else if (k == "radius")
        ok = parseRange(value, p.radius);
    else if (k == "height")
        ok = parseRange(value, p.height);
    else if (k == "wind")
        ok = parseRange(value, p.wind);
    else if (k == "rotation")
        ok = parseRange(value, p.rotation);
    else if (k == "rotationdelta")
        ok = parseRange(value, p.rotationDelta);
    else if (k == "flags" || k == "flag")
        p.flags |= primitiveFlags(value, warnings);
    else if (k == "spawnflags" || k == "spawnflag")
        p.spawnFlags |= spawnFlags(value, warnings);
    else if (k == "name")
        p.name = value;
    else if (warnings)
        warnings->push_back("unknown key " + key);
    if (!ok && warnings)
        warnings->push_back("bad value for " + key + ": " + value);
}

// One of a primitive's groups: its colour, alpha, sizes and length.
bool primitiveGroup(Lexer &lex, Primitive &p, const std::string &name, std::vector<std::string> *warnings) {
    const std::string n = fs::toLower(name);
    Primitive::Channel *channel = nullptr;
    int shift = 0;
    const bool rgb = n == "rgb";
    if (n == "alpha") {
        channel = &p.alpha;
        shift = Primitive::kAlphaShift;
    } else if (n == "size" || n == "width") {
        channel = &p.size;
        shift = Primitive::kSizeShift;
    } else if (n == "size2" || n == "width2") {
        channel = &p.size2;
        shift = Primitive::kSize2Shift;
    } else if (n == "length" || n == "height") {
        channel = &p.length;
        shift = Primitive::kLengthShift;
    } else if (rgb) {
        shift = Primitive::kRgbShift;
    } else if (warnings) {
        warnings->push_back("unknown group " + name);
    }
    GroupReader reader{lex, nullptr, nullptr, nullptr};
    reader.pair = [&](const std::string &key, const std::string &value) {
        const std::string k = fs::toLower(key);
        bool ok = true;
        if (!channel && !rgb)
            return;
        if (k == "flags" || k == "flag") {
            p.flags |= channelFlags(value, warnings) << shift;
        } else if (k == "parm" || k == "parms") {
            ok = parseRange(value, rgb ? p.rgbParm : channel->parm);
        } else if (k == "start" || k == "end") {
            if (rgb) {
                std::array<EffectRange, 3> v{};
                ok = parseVector(value, v);
                for (size_t i = 0; ok && i < 3; ++i)
                    (k == "start" ? p.rgb[i].start : p.rgb[i].end) = v[i];
            } else {
                ok = parseRange(value, k == "start" ? channel->start : channel->end);
            }
        } else if (warnings) {
            warnings->push_back("unknown key " + key + " in " + name);
        }
        if (!ok && warnings)
            warnings->push_back("bad value for " + name + " " + key + ": " + value);
    };
    reader.list = [](const std::string &, const std::vector<std::string> &) {};
    reader.group = [&](const std::string &) { return lex.skipBlock(); };
    return reader.read(false);
}

} // namespace

bool parseEffectFile(std::string_view text, EffectFile &out, std::vector<std::string> *warnings) {
    out = {};
    Lexer lex(text);
    GroupReader top{lex, nullptr, nullptr, nullptr};
    top.pair = [&](const std::string &key, const std::string &) {
        if (warnings)
            warnings->push_back("unknown key " + key);
    };
    top.list = [&](const std::string &key, const std::vector<std::string> &) {
        if (warnings)
            warnings->push_back("unknown key " + key);
    };
    top.group = [&](const std::string &name) {
        Primitive p;
        if (!primitiveType(name, p.type)) {
            if (warnings)
                warnings->push_back("unknown primitive " + name);
            return lex.skipBlock();
        }
        GroupReader body{lex, nullptr, nullptr, nullptr};
        body.pair = [&](const std::string &key, const std::string &value) { primitivePair(p, key, value, warnings); };
        body.list = [&](const std::string &key, const std::vector<std::string> &items) {
            if (isMediaKey(key))
                addMedia(p, key, items);
            else if (warnings)
                warnings->push_back("unknown list " + key);
        };
        body.group = [&](const std::string &group) { return primitiveGroup(lex, p, group, warnings); };
        if (!body.read(false))
            return false;
        out.primitives.push_back(std::move(p));
        return true;
    };
    return top.read(true);
}

} // namespace reluva::idtech3::raven
