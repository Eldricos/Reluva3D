// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Fallen's effects' engine side, for a running level (ScriptLevel):
// the copies of an actor its scripts make (CTVE_MultiMesh: particles,
// sparks, beam sections) moved on each tick and let go when their life
// runs out; the electric arcs (CTVE_ChainSpark); the beams' sections
// (CTVE_BeamFX); bursts of sparks (CTVE_Sparks); rain and snow
// (CTVE_Weather); and decals stuck to the world (Decal). How each moves is
// Reluva's reading of what the scripts set and expect; the original code
// isn't known. See docs/engines/unreal_vm.md.

#include "engines/unreal/vm/vm_level.h"

#include "engines/unreal/formats/actors.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace reluva::unreal {

namespace {

using Vec = std::array<float, 3>;

std::mt19937 &random() {
    static std::mt19937 r(54321); // repeatable runs
    return r;
}
float frand() {
    return std::uniform_real_distribution<float>(0, 1)(random());
}

Vec add(const Vec &a, const Vec &b) {
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}
Vec sub(const Vec &a, const Vec &b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
Vec mul(const Vec &a, float s) {
    return {a[0] * s, a[1] * s, a[2] * s};
}
float length(const Vec &a) {
    return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
}
Vec cross(const Vec &a, const Vec &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec normal(const Vec &a) {
    const float l = length(a);
    return l > 1e-6f ? mul(a, 1 / l) : Vec{1, 0, 0};
}
// A direction as a rotator (Unreal units), as rotator(Vector) gives it.
std::array<int32_t, 3> rotatorOf(const Vec &d) {
    const float yaw = std::atan2(d[1], d[0]);
    const float pitch = std::atan2(d[2], std::sqrt(d[0] * d[0] + d[1] * d[1]));
    auto units = [](float r) { return int32_t(std::lround(r * 32768.0f / 3.14159265f)); };
    return {units(pitch), units(yaw), 0};
}

} // namespace

// Each level starts the same, whatever ran before it (ue-run -all).
void reseedEffectRandom() {
    random().seed(54321);
}

// An actor's copies (its MultiMesh list): the ones in use, in order.
std::vector<VmObject *> ScriptLevel::multiMeshes(VmObject *actor) {
    std::vector<VmObject *> out;
    const Value *head = actor ? prop(actor, "MultiMesh") : nullptr;
    VmObject *mm = head ? head->asObject() : nullptr;
    for (int guard = 0; mm && guard < 4096; ++guard) {
        const Value *next = prop(mm, "Next");
        mm = next ? next->asObject() : nullptr;
        if (!mm)
            break;
        if (const Value *deleted = prop(mm, "bDeleted"); deleted && deleted->asBool())
            break; // the ones let go are kept at the end
        out.push_back(mm);
    }
    return out;
}

// An arc of sparks (CTVE_ChainSpark), as Engine.dll's ChainSparkTick: it
// grows along its length (bGrows, GrowthRate a second, to 1), travels
// (bTravels: StartLoc and FinishLoc by StartVelocity and FinishVelocity),
// fades (bFade, FadeSpeed a second, its light with it; gone below 0) or
// glows up and down between MinGlow and MaxGlow (bCycleFade); with
// bUpdate it's laid out again (chainSparkUpdate). Its copies are moved by
// its physics (Spark: moveMultiMeshes).
void ScriptLevel::chainSparkTick(VmObject *spark, float seconds) {
    auto f = [&](const char *name) {
        const Value *v = prop(spark, name);
        return v ? v->asFloat() : 0.0f;
    };
    auto b = [&](const char *name) {
        const Value *v = prop(spark, name);
        return v && v->asBool();
    };
    auto vec = [&](const char *name) {
        const Value *v = prop(spark, name);
        return v ? Vm::vectorOf(*v) : Vec{};
    };
    auto setFloat = [&](const char *name, float v) {
        if (Value *p = prop(spark, name))
            *p = Value::floatValue(v);
    };
    auto setBool = [&](const char *name, bool v) {
        if (Value *p = prop(spark, name))
            *p = Value::boolValue(v);
    };
    auto setVec = [&](const char *name, const Vec &v) {
        if (Value *p = prop(spark, name))
            _vm.setVector(*p, v[0], v[1], v[2]);
    };
    if (b("bGrows")) {
        float g = f("Growth") + seconds * f("GrowthRate");
        if (g > 1) {
            g = 1;
            setBool("bGrows", false);
        }
        setFloat("Growth", g);
        setBool("bUpdate", true);
    } else {
        setFloat("Growth", 1);
    }
    if (b("bTravels")) {
        setVec("StartLoc", add(vec("StartLoc"), mul(vec("StartVelocity"), seconds)));
        setVec("FinishLoc", add(vec("FinishLoc"), mul(vec("FinishVelocity"), seconds)));
        setBool("bUpdate", true);
    }
    if (b("bFade")) {
        const float glow = f("ScaleGlow") - seconds * f("FadeSpeed");
        setFloat("ScaleGlow", glow);
        if (Value *l = prop(spark, "LightBrightness"))
            *l = Value::byteValue(uint8_t(std::clamp(int(glow * 100), 0, 255)));
        if (glow < 0) {
            destroy(spark);
            return;
        }
    } else if (b("bCycleFade")) {
        float glow = f("ScaleGlow");
        if (b("bFadePlus")) {
            glow += seconds * f("FadeSpeed");
            if (glow > f("MaxGlow")) {
                glow = f("MaxGlow");
                setBool("bFadePlus", false);
            }
        } else {
            glow -= seconds * f("FadeSpeed");
            if (glow < f("MinGlow")) {
                glow = f("MinGlow");
                setBool("bFadePlus", true);
            }
        }
        setFloat("ScaleGlow", glow);
    }
    if (b("bUpdate"))
        chainSparkUpdate(spark);
}

// Laying the arc out (ChainSpark.Update, ForceUpdate): its base copy
// holds the curve for its physics. Location startPath, Velocity FinishLoc
// + FinishPath - StartLoc, Acceleration FinishLoc - StartLoc, Vect2
// StartLoc (a cubic from StartLoc, as the curve's weights take them),
// Phase the step along it from one copy to the next (Growth over
// NumSegments - 1); with bSinVariation (bUse) a wave across it, AxisScale
// a random way across (Amplitude long) and Angle its frequency
// (WaveLengths x 6.28). The actor goes to the arc's middle.
void ScriptLevel::chainSparkUpdate(VmObject *spark) {
    const Value *head = prop(spark, "MultiMesh");
    VmObject *base = head ? head->asObject() : nullptr;
    if (!base)
        return;
    auto f = [&](const char *name) {
        const Value *v = prop(spark, name);
        return v ? v->asFloat() : 0.0f;
    };
    auto vec = [&](const char *name) {
        const Value *v = prop(spark, name);
        return v ? Vm::vectorOf(*v) : Vec{};
    };
    auto setVec = [&](const char *name, const Vec &v) {
        if (Value *p = prop(base, name))
            _vm.setVector(*p, v[0], v[1], v[2]);
    };
    auto setFloat = [&](const char *name, float v) {
        if (Value *p = prop(base, name))
            *p = Value::floatValue(v);
    };
    const Vec start = vec("StartLoc");
    const Vec finish = vec("FinishLoc");
    setVec("Location", vec("startPath"));
    setVec("Velocity", sub(add(finish, vec("FinishPath")), start));
    setVec("Acceleration", sub(finish, start));
    setVec("Vect2", start);
    const Value *count = prop(spark, "NumSegments");
    const int n = count ? count->asInt() : 0;
    setFloat("Phase", n > 1 ? f("Growth") / float(n - 1) : 0);
    const Value *sinVar = prop(spark, "bSinVariation");
    const bool wave = sinVar && sinVar->asBool();
    if (Value *u = prop(base, "bUse"))
        *u = Value::boolValue(wave);
    if (wave) {
        // Across the line from P1 to P3 (which nothing sets: the world's
        // Y and Z), a random way, Amplitude long.
        const Vec d = sub(vec("P3"), vec("P1"));
        const auto rot = rotatorOf(d);
        const auto r3 = rotationAxes({rot[0], rot[1], rot[2]});
        const Vec ax[3] = {
            {r3[0][0], r3[0][1], r3[0][2]}, {r3[1][0], r3[1][1], r3[1][2]}, {r3[2][0], r3[2][1], r3[2][2]}};
        Vec alter = add(mul(ax[1], frand() * 2 - 1), mul(ax[2], frand() * 2 - 1));
        if (length(alter) > 0)
            alter = mul(normal(alter), f("Amplitude"));
        const Value *waves = prop(spark, "WaveLengths");
        const float sinScale = float(waves ? waves->asInt() : 0) * 6.28f;
        if (Value *p = prop(spark, "Alter"))
            _vm.setVector(*p, alter[0], alter[1], alter[2]);
        if (Value *p = prop(spark, "SinScale"))
            *p = Value::floatValue(sinScale);
        setVec("AxisScale", alter);
        setFloat("Angle", sinScale);
    }
    setLocation(spark, mul(add(start, finish), 0.5f));
}

// A beam's sections (CTVE_BeamFX: copies of its mesh, made and cut by its
// scripts) laid end to end from BeamOrigin, where the weapon's tip is now,
// by BeamSectionAxesX (SectionLength along the beam), turned as the beam
// and spun about it at 25600 a second, in its BeamSectionColorFilter
// (0x103790f0). Each is SectionAxisScale, thickened at random by up to
// SectionAxisYZRandomFactor (the same both ways) and by a pulse of up to
// WaveAmplitude times that, WaveLength of a turn's 804 a section apart,
// travelling WaveTravelVelocity a second (WaveStartOffset kept on the
// beam). The scripts cut the last after, where the beam meets something.
void ScriptLevel::updateBeamSections(VmObject *beam, float seconds) {
    // Only a sustained beam's (Physics BeamSustain, 39) with its copies
    // (0x10379196): once its firing stops and its scripts set it to none,
    // its sections stay as they were.
    const Value *physics = prop(beam, "Physics");
    const Value *copies = prop(beam, "MultiMesh");
    if (!physics || physics->asInt() != 39 || !copies || !copies->asObject())
        return;
    const VmProperty *list = beam->cls->findProperty(_vm.name("BeamSectionList"));
    if (!list)
        return;
    auto f = [&](const char *name) {
        const Value *v = prop(beam, name);
        return v ? v->asFloat() : 0.0f;
    };
    auto i = [&](const char *name) {
        const Value *v = prop(beam, name);
        return v ? v->asInt() : 0;
    };
    auto vec = [&](const char *name) {
        const Value *v = prop(beam, name);
        return v ? Vm::vectorOf(*v) : Vec{};
    };
    const int first = std::max(0, i("BeamSectionIndexFirst"));
    const int last = std::min(i("BeamSectionIndexLast"), list->arrayDim);
    if (first >= last)
        return;
    float offset = f("WaveStartOffset") - f("WaveTravelVelocity") * seconds;
    if (Value *w = prop(beam, "WaveStartOffset"))
        *w = Value::floatValue(offset);
    auto sectionAt = [&](int k) {
        VmObject *section = beam->values[size_t(list->slot + k)].asObject();
        return section && !section->deleted ? section : nullptr;
    };
    // The spin goes on from the first section's roll.
    int32_t roll = 0;
    if (VmObject *head = sectionAt(first))
        if (const Value *r = prop(head, "Rotation"))
            roll = Vm::rotatorOf(*r)[2];
    roll += int32_t(seconds * 25600);
    const Vec origin = vec("BeamOrigin");
    const Vec step = vec("BeamSectionAxesX");
    const auto turn =
        prop(beam, "BeamRotation") ? Vm::rotatorOf(*prop(beam, "BeamRotation")) : std::array<int32_t, 3>{};
    const Value *color = prop(beam, "BeamSectionColorFilter");
    const Vec scale = vec("SectionAxisScale");
    const float jitter = f("SectionAxisYZRandomFactor");
    const float amplitude = f("WaveAmplitude");
    const float waveLength = f("WaveLength");
    for (int k = first; k < last; ++k) {
        VmObject *section = sectionAt(k);
        if (!section)
            continue;
        const Vec at = add(origin, mul(step, float(k)));
        if (Value *l = prop(section, "Location"))
            _vm.setVector(*l, at[0], at[1], at[2]);
        if (Value *r = prop(section, "Rotation"))
            *r = _vm.rotatorValue(turn[0], turn[1], roll);
        if (Value *c = prop(section, "ColorFilter"); c && color)
            *c = *color;
        if (Value *s = prop(section, "AxisScale")) {
            const float r = frand();
            const float pulse = 1 + 0.5f * amplitude * (1 + std::sin(float(k) * waveLength * 803.84f + offset));
            _vm.setVector(*s, scale[0], (scale[1] + r * jitter) * pulse, (scale[2] + r * jitter) * pulse);
        }
    }
}

namespace {

// A new copy of an actor (its NewMultiMesh: like it to begin with).
VmObject *newCopy(Vm &vm, VmObject *actor) {
    VmFunction *make = vm.findFunction(actor, vm.name("NewMultiMesh"));
    return make ? vm.invoke(make, actor, {}).asObject() : nullptr;
}

} // namespace

// Sparks (CTVE_Sparks): every so often (BurstDelay, counted down by its
// scripts, set again here: about AverageBurstTime, else up to MaxWaitTime)
// a burst of MinSparksPerBurst to MaxSparksPerBurst of them flies out along
// its X axis at Speed, spread RangeWidth to the sides and RangeHeight up
// and down, and falls; each Size (give or take SizeVariance) and lasting
// SparkLife (give or take SparkLifeVariance), fading as it goes. Its light
// (the one it had, kept in saveLightType) flashes with each burst.
void ScriptLevel::sparkTick(VmObject *sparks, float seconds) {
    auto f = [&](const char *name) {
        const Value *v = prop(sparks, name);
        return v ? v->asFloat() : 0.0f;
    };
    auto vec = [&](const char *name) {
        const Value *v = prop(sparks, name);
        return v ? Vm::vectorOf(*v) : Vec{};
    };
    auto set = [&](VmObject *o, const char *name, const Value &v) {
        if (Value *p = prop(o, name))
            *p = v;
    };
    if (auto it = _sparkFlash.find(sparks); it != _sparkFlash.end() && (it->second -= seconds) <= 0) {
        set(sparks, "LightType", Value::byteValue(0));
        _sparkFlash.erase(it);
    }
    Value *delay = prop(sparks, "BurstDelay");
    if (!delay || delay->asFloat() > 0)
        return;

    const Vec x = vec("XAxis");
    const Vec y = vec("YAxis");
    const Vec z = vec("ZAxis");
    const Value *loc = prop(sparks, "Location");
    const Vec from = loc ? Vm::vectorOf(*loc) : vec("SeedLocation");
    Vec gravity{0, 0, -950};
    if (const Value *r = prop(sparks, "Region"); r && !r->fields.empty())
        if (VmObject *zone = r->fields[0].asObject())
            if (const Value *g = prop(zone, "ZoneGravity"))
                gravity = Vm::vectorOf(*g);
    const Value *minCount = prop(sparks, "MinSparksPerBurst");
    const Value *maxCount = prop(sparks, "MaxSparksPerBurst");
    const int least = minCount ? std::max(0, minCount->asInt()) : 0;
    const int most = std::min(64, std::max(least, maxCount ? maxCount->asInt() : 0));
    const int count = least + int(frand() * float(most - least + 1) * 0.999f);
    auto vary = [](float base, float by) { return base + (frand() * 2 - 1) * by; };
    for (int k = 0; k < count; ++k) {
        VmObject *mm = newCopy(_vm, sparks);
        if (!mm)
            break;
        const Vec v = add(add(mul(x, f("Speed")), mul(y, (frand() * 2 - 1) * f("RangeWidth"))),
                          mul(z, (frand() * 2 - 1) * f("RangeHeight")));
        const float life = std::max(0.05f, vary(f("SparkLife"), f("SparkLifeVariance")));
        set(mm, "Location", _vm.vectorValue(from[0], from[1], from[2]));
        set(mm, "Velocity", _vm.vectorValue(v[0], v[1], v[2]));
        set(mm, "Acceleration", _vm.vectorValue(gravity[0], gravity[1], gravity[2]));
        const auto rot = rotatorOf(v);
        set(mm, "Rotation", _vm.rotatorValue(rot[0], rot[1], rot[2]));
        const float size = std::max(0.01f, vary(f("Size"), f("SizeVariance")));
        set(mm, "DrawScale", Value::floatValue(size));
        // Its size kept in Phase: the engine's spark physics flickers it
        // round that (physCTVE_Sparks), its trail timer in Angle.
        set(mm, "Phase", Value::floatValue(size));
        set(mm, "Angle", Value::floatValue(0.01f + 0.02f * frand()));
        set(mm, "LifeSpan", Value::floatValue(life));
        set(mm, "ScaleGlow", Value::floatValue(f("MaxScaleGlow")));
    }
    const float average = f("AverageBurstTime");
    delay->f = average > 0 ? average * (0.5f + frand()) : std::max(0.05f, frand() * f("MaxWaitTime"));
    if (const Value *light = prop(sparks, "bLightFX"); light && light->asBool() && count > 0)
        if (const Value *saved = prop(sparks, "saveLightType"); saved && saved->asInt() != 0) {
            set(sparks, "LightType", Value::byteValue(saved->asInt()));
            _sparkFlash[sparks] = 0.1f;
        }
}

// Rain and snow (CTVE_Weather): each tick (WeatherTick, 0x10349230) the
// intensity comes up toward MaxIntensity at DeltaIntensityRate a second,
// or down when not bActive, stopping it (bShutDown) at 0. Now and then
// (WindActivity a tick) the wind turns toward a new random way,
// MaxLateralMove across, and DropVelocity goes toward it at WindActivity
// x 2500 a second. Every 0.4 s the blockers near the seed are found
// (CheckBlocks). Then dt x Intensity x 2000 new drops (whole ones; none
// at 0.01 or under), each from Generate. The fill (InitialSetUp,
// 0x103496e0) makes Intensity x 2000 at once, spread along their falls.
void ScriptLevel::weatherTick(VmObject *weather, float seconds, bool fill) {
    auto f = [&](const char *name) {
        const Value *v = prop(weather, name);
        return v ? v->asFloat() : 0.0f;
    };
    auto b = [&](const char *name) {
        const Value *v = prop(weather, name);
        return v && v->asBool();
    };
    auto vec = [&](const char *name) {
        const Value *v = prop(weather, name);
        return v ? Vm::vectorOf(*v) : Vec{};
    };
    auto setOn = [&](VmObject *o, const char *name, const Value &v) {
        if (Value *p = prop(o, name))
            *p = v;
    };
    Value *intensity = prop(weather, "Intensity");
    if (!intensity)
        return;
    const bool inverted = b("bInvertWeatherBlockers");
    const bool debug = b("bDebugMode");

    // The blockers (Block, NumBlocks): each a box (bBox; XSize by YSize,
    // ZSize high about Pos) or an upright cylinder (Radius, Height over Pos).
    struct Block {
        Value *value;
        Vec at;
        float radius;
        float height;
        bool box;
        Vec size;
        float range;
    };
    std::vector<Block> blocks;
    auto field = [&](Value &v, const char *name) -> Value * {
        if (!v.st)
            return nullptr;
        const VmProperty *p = v.st->findProperty(_vm.name(name));
        return p && size_t(p->slot) < v.fields.size() ? &v.fields[size_t(p->slot)] : nullptr;
    };
    if (const VmProperty *list = weather->cls->findProperty(_vm.name("Block")); list && prop(weather, "NumBlocks")) {
        const int n = std::clamp(prop(weather, "NumBlocks")->asInt(), 0, list->arrayDim);
        for (int k = 0; k < n; ++k) {
            Value &v = weather->values[size_t(list->slot + k)];
            auto ff = [&](const char *name) {
                const Value *x = field(v, name);
                return x ? x->asFloat() : 0.0f;
            };
            const Value *pos = field(v, "Pos");
            const Value *box = field(v, "bBox");
            blocks.push_back({&v,
                              pos ? Vm::vectorOf(*pos) : Vec{},
                              ff("Radius"),
                              ff("Height"),
                              box && box->asInt() != 0,
                              {ff("XSize"), ff("YSize"), ff("ZSize")},
                              ff("SpawnRange")});
        }
    }
    const VmProperty *checkList = weather->cls->findProperty(_vm.name("CheckBlock"));
    auto checked = [&]() {
        std::vector<const Block *> out;
        const Value *n = prop(weather, "NumCheckBlocks");
        if (!checkList || !n)
            return out;
        for (int k = 0; k < std::min(n->asInt(), checkList->arrayDim); ++k) {
            const int at = weather->values[size_t(checkList->slot + k)].asInt();
            if (at >= 0 && size_t(at) < blocks.size())
                out.push_back(&blocks[size_t(at)]);
        }
        return out;
    };
    // CheckBlocks (0x10349e50): the blockers whose footprint, widened by
    // half the larger spawn range and CheckBlockRange, holds the seed.
    auto checkBlocks = [&]() {
        setOn(weather, "SpawnRange", Value::floatValue(1300));
        setOn(weather, "WaitCheckBlocks", Value::floatValue(0.4f));
        setOn(weather, "bCheckBlocks", Value::boolValue(false));
        setOn(weather, "bShutDown", Value::boolValue(false));
        setOn(weather, "bPlayerSheltered", Value::boolValue(false));
        int count = 0;
        const Vec seed = vec("SeedLocation");
        const Vec feet = vec("Location");
        for (size_t k = 0; k < blocks.size(); ++k) {
            const Block &bl = blocks[k];
            const float wide = std::max(1300.0f, bl.range) / 2 + f("CheckBlockRange");
            const float dx = seed[0] - bl.at[0];
            const float dy = seed[1] - bl.at[1];
            const bool near = bl.box ? std::fabs(dx) <= bl.size[0] / 2 + wide && std::fabs(dy) <= bl.size[1] / 2 + wide
                                     : std::hypot(dx, dy) <= bl.radius + wide;
            if (Value *c = field(*bl.value, "bCheck"))
                *c = Value::intValue(near ? 1 : 0);
            if (!near)
                continue;
            if (checkList && count < checkList->arrayDim)
                weather->values[size_t(checkList->slot + count++)] = Value::intValue(int32_t(k));
            setOn(weather, "bCheckBlocks", Value::boolValue(true));
            // The player under it (70 over its feet).
            const float fx = feet[0] - bl.at[0];
            const float fy = feet[1] - bl.at[1];
            const bool over = bl.box ? std::fabs(fx) <= bl.size[0] / 2 && std::fabs(fy) <= bl.size[1] / 2 &&
                                           feet[2] + 70 <= bl.at[2] + bl.size[2] / 2
                                     : std::hypot(fx, fy) <= bl.radius && feet[2] + 70 <= bl.at[2] + bl.height;
            if (over)
                setOn(weather, "bPlayerSheltered", Value::boolValue(true));
        }
        setOn(weather, "NumCheckBlocks", Value::intValue(count));
    };

    // Generate (0x10349910): a drop over a square SpawnRange across about the
    // seed, DropHeight up and up to DropHeightRange more, at DropVelocity
    // (slower by up to FallRateVariance), Size and up to SizeVariance more,
    // starting at StartScaleGlow (0, so it fades in) and rolled at random.
    // It lasts until it's down to the weather's own height (the player's
    // feet): the world doesn't stop it, only the blockers do.
    Vec seed = vec("SeedLocation");
    float range = f("SpawnRange");
    Vec wind = vec("DropVelocity");
    const Value *rotation = prop(weather, "CurrentRotation");
    const Value *texture = prop(weather, "Texture");
    const Value *style = prop(weather, "Style");
    auto generate = [&]() -> VmObject * {
        VmObject *mm = newCopy(_vm, weather);
        if (!mm)
            return nullptr;
        Vec at{seed[0] + (frand() - 0.5f) * range, seed[1] + (frand() - 0.5f) * range,
               seed[2] + f("DropHeight") + frand() * f("DropHeightRange")};
        Vec v = wind;
        v[2] += frand() * f("FallRateVariance");
        const float fall = v[2] < 0 ? -v[2] : 1e-4f;
        float life = (at[2] - vec("Location")[2]) / fall;
        if (life < 0)
            life = 0.0001f;
        Vec accel{};
        const std::vector<const Block *> near = checked();
        if (inverted) {
            // Inside one of the blockers near, straight down.
            if (!near.empty()) {
                const Block &bl = *near[size_t(frand() * float(near.size())) % near.size()];
                if (bl.box) {
                    at = {bl.at[0] + (frand() * 2 - 1) * bl.size[0], bl.at[1] + (frand() * 2 - 1) * bl.size[1],
                          bl.at[2] + bl.size[2] / 2};
                    life = bl.size[2] / fall;
                } else {
                    const float angle = frand() * 6.2831853f;
                    const float r = frand() * bl.radius;
                    at = {bl.at[0] + std::cos(angle) * r, bl.at[1] + std::sin(angle) * r, bl.at[2] + bl.height};
                    life = 2 * bl.height / fall;
                }
                v[0] = 0;
                v[1] = 0;
            }
        } else if (b("bCheckBlocks")) {
            // Born inside or under a blocker, it's gone at once; one it falls
            // onto (its top or side) it stops at.
            for (const Block *bl : near) {
                const float top = bl->at[2] + (bl->box ? bl->size[2] / 2 : bl->height);
                auto inside = [&](const Vec &p) {
                    const float dx = p[0] - bl->at[0];
                    const float dy = p[1] - bl->at[1];
                    return p[2] <= top &&
                           (bl->box ? std::fabs(dx) <= bl->size[0] / 2 && std::fabs(dy) <= bl->size[1] / 2
                                    : dx * dx + dy * dy <= bl->radius * bl->radius);
                };
                if (inside(at)) {
                    life = 0.0001f;
                    break;
                }
                // When along its fall it first gets in.
                float lo = 0;
                float hi = life;
                if (!inside(add(at, mul(v, hi))))
                    continue;
                for (int k = 0; k < 20; ++k) {
                    const float mid = (lo + hi) / 2;
                    (inside(add(at, mul(v, mid))) ? hi : lo) = mid;
                }
                life = hi;
            }
        } else {
            // In the open, a sideways sway: FallRate x Chaos, any way.
            Vec d{frand() * 2 - 1, frand() * 2 - 1, frand() * 2 - 1};
            d = length(d) > 1e-4f ? normal(d) : Vec{1, 0, 0};
            const float sway = std::fabs(f("FallRate")) * f("Chaos");
            accel = {d[0] * sway, d[1] * sway, 0};
        }
        setOn(mm, "Location", _vm.vectorValue(at[0], at[1], at[2]));
        setOn(mm, "Velocity", _vm.vectorValue(v[0], v[1], v[2]));
        setOn(mm, "Acceleration", _vm.vectorValue(accel[0], accel[1], accel[2]));
        const auto turn = rotation ? Vm::rotatorOf(*rotation) : std::array<int32_t, 3>{};
        setOn(mm, "Rotation", _vm.rotatorValue(turn[0], turn[1], int32_t(frand() * 65536) & 0xffff));
        setOn(mm, "LifeSpan", Value::floatValue(life));
        setOn(mm, "DrawScale", Value::floatValue(f("Size") + frand() * f("SizeVariance")));
        setOn(mm, "ScaleGlow", Value::floatValue(f("StartScaleGlow")));
        if (texture)
            setOn(mm, "Texture", *texture);
        if (style)
            setOn(mm, "Style", *style);
        return mm;
    };

    if (fill) {
        const Vec at = vec("Location");
        if (Value *sl = prop(weather, "SeedLocation"))
            _vm.setVector(*sl, at[0], at[1], at[2]);
        int count = int(intensity->asFloat() * 2000);
        if (debug) {
            setOn(weather, "MaxIntensity", Value::floatValue(0));
            count = 4000;
        }
        if (Value *dv = prop(weather, "DropVelocity"))
            _vm.setVector(*dv, 0, 0, Vm::vectorOf(*dv)[2]);
        if (inverted && !b("bCheckBlocks"))
            return;
        // The fill's own seed and still air.
        seed = at;
        wind = {0, 0, wind[2]};
        for (int k = 0; k < count; ++k) {
            VmObject *mm = generate();
            if (!mm)
                break;
            Value *life = prop(mm, "LifeSpan");
            Value *loc = prop(mm, "Location");
            const Value *vel = prop(mm, "Velocity");
            const Vec v = vel ? Vm::vectorOf(*vel) : Vec{};
            if (life && loc) {
                const float r = frand() * life->asFloat() - 0.001f;
                if (r > 0) {
                    *life = Value::floatValue(life->asFloat() - r);
                    const Vec p = add(Vm::vectorOf(*loc), mul(v, r));
                    _vm.setVector(*loc, p[0], p[1], p[2]);
                }
            }
            if (debug) {
                setOn(mm, "Velocity", _vm.vectorValue(0, 0, 0));
                setOn(mm, "Acceleration", _vm.vectorValue(0, 0, 0));
                setOn(mm, "LifeSpan", Value::floatValue(500));
            }
        }
        return;
    }

    // How hard: up while active, down when not, at most 1; stopped at 0.
    float now = intensity->asFloat();
    const float rate = f("DeltaIntensityRate") * seconds;
    if (b("bActive") && now < f("MaxIntensity"))
        now += rate;
    else if (!b("bActive"))
        now -= rate;
    now = std::min(now, 1.0f);
    if (now <= 0) {
        *intensity = Value::floatValue(0);
        setOn(weather, "bShutDown", Value::boolValue(true));
        return;
    }
    *intensity = Value::floatValue(now);
    // The wind: a new way to turn to now and then; DropVelocity goes
    // straight toward it.
    Value *dropVelocity = prop(weather, "DropVelocity");
    Value *destWind = prop(weather, "DestWind");
    if (dropVelocity && destWind) {
        if (frand() < f("WindActivity")) {
            Vec d;
            do
                d = {frand() * 2 - 1, frand() * 2 - 1, frand() * 2 - 1};
            while (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > 1 || length(d) < 1e-3f);
            d = mul(normal(d), f("MaxLateralMove"));
            _vm.setVector(*destWind, d[0], d[1], f("FallRate"));
        }
        const Vec w = Vm::vectorOf(*dropVelocity);
        const Vec to = Vm::vectorOf(*destWind);
        const Vec gap = sub(to, w);
        const float step = f("WindActivity") * seconds * 2500;
        const Vec next = length(gap) <= step ? to : add(w, mul(normal(gap), step));
        _vm.setVector(*dropVelocity, next[0], next[1], next[2]);
        wind = next;
    }
    if (Value *wait = prop(weather, "WaitCheckBlocks")) {
        *wait = Value::floatValue(wait->asFloat() - seconds);
        if (wait->asFloat() <= 0)
            checkBlocks();
    }
    range = f("SpawnRange");
    if (inverted && !b("bCheckBlocks"))
        return;
    if (now <= 0.01f)
        return;
    const int count = int(seconds * now * 2000);
    for (int k = 0; k < count; ++k)
        if (!generate())
            break;
}

std::array<float, 3> ScriptLevel::waterLine(const std::array<float, 3> &start, const std::array<float, 3> &end) {
    auto water = [&](const Vec &p) {
        const Value r = regionAt(p);
        if (!r.st)
            return false;
        const VmProperty *zp = r.st->findProperty(_vm.name("Zone"));
        VmObject *zone = zp && size_t(zp->slot) < r.fields.size() ? r.fields[size_t(zp->slot)].asObject() : nullptr;
        const Value *w = zone ? prop(zone, "bWaterZone") : nullptr;
        return w && w->asBool();
    };
    const bool from = water(start);
    if (water(end) == from)
        return start;
    // Halved until it's within a fraction of a unit.
    Vec a = start;
    Vec b = end;
    for (int k = 0; k < 24 && length(sub(b, a)) > 0.1f; ++k) {
        const Vec mid = mul(add(a, b), 0.5f);
        (water(mid) == from ? a : b) = mid;
    }
    return b;
}

// A decal stuck to the world (Decal.AttachDecal): the surface found within
// `distance` behind it (along its rotation's X backwards: it's turned to
// face out of the surface, rotator(HitNormal)); flat on it, the texture's
// size by DrawScale, its top along `direction` if given (a footprint's
// heading), else its rotation's Z. The surface's texture, or None if
// there's no surface there (and the decal goes).
VmObject *ScriptLevel::attachDecal(VmObject *decal, float distance, const Vec &direction) {
    const Value *loc = prop(decal, "Location");
    const Value *rot = prop(decal, "Rotation");
    if (!loc || !rot)
        return nullptr;
    const Vec at = Vm::vectorOf(*loc);
    const auto r = Vm::rotatorOf(*rot);
    const auto ax = rotationAxes(Rotator{r[0], r[1], r[2]});
    const Vec out{ax[0][0], ax[0][1], ax[0][2]};
    const Vec from = add(at, mul(out, 2));
    const Vec to = sub(at, mul(out, std::max(distance, 1.0f)));
    Vec where{};
    Vec n{};
    if (trace(decal, from, to, {}, false, where, n) != _levelInfo)
        return nullptr;
    VmObject *surfaceTex = surfaceTexture(from, to);
    Decal d;
    d.at = where;
    d.normal = n;
    Vec top =
        (direction[0] != 0 || direction[1] != 0 || direction[2] != 0) ? direction : Vec{ax[2][0], ax[2][1], ax[2][2]};
    // Its top along the surface.
    const float along = top[0] * n[0] + top[1] * n[1] + top[2] * n[2];
    top = sub(top, mul(n, along));
    if (length(top) < 1e-3f)
        top = normal(cross(n, std::fabs(n[2]) < 0.9f ? Vec{0, 0, 1} : Vec{1, 0, 0}));
    d.up = normal(top);
    if (const Value *t = prop(decal, "Texture"); t && t->asObject() && t->o->package)
        d.texture = {t->o->package, t->o->exportIndex};
    const Value *scale = prop(decal, "DrawScale");
    d.scale = scale ? scale->asFloat() : 1;
    _decals[decal] = d;
    if (surfaceTex)
        return surfaceTex;
    const Value *t = prop(decal, "Texture");
    return t && t->asObject() ? t->o : _levelInfo;
}

void registerEffectNatives(Vm &vm) {
    auto reg = [&](const char *name, NativeFn fn) { vm.registerNative(name, std::move(fn)); };
    // SpawnChain(): the arc's segments, NumSegments copies of it
    // (NewMultiMesh), each with SpikeMag (Use1) and EndSpikeMag (Use2) to
    // jitter by, SparkColor, translucent; then laid out.
    reg("CTVE_ChainSpark.SpawnChain", [](NativeCall &c) {
        if (!c.vm.level || !c.self)
            return;
        const Value *n = c.self->find(c.vm, "NumSegments");
        const int count = std::clamp(n ? n->asInt() : 0, 1, 256);
        VmFunction *make = c.vm.findFunction(c.self, c.vm.name("NewMultiMesh"));
        auto get = [&](const char *name) { return c.self->find(c.vm, name); };
        for (int k = 0; make && k < count; ++k) {
            VmObject *mm = c.vm.invoke(make, c.self, {}).asObject();
            if (!mm)
                break;
            auto set = [&](const char *name, const Value &v) {
                if (Value *p = mm->find(c.vm, name))
                    *p = v;
            };
            if (const Value *v = get("SpikeMag"))
                set("Use1", *v);
            if (const Value *v = get("EndSpikeMag"))
                set("Use2", *v);
            if (const Value *v = get("SparkColor"))
                set("ColorFilter", *v);
            if (const Value *v = get("AxisScale"))
                set("AxisScale", *v);
            set("Style", Value::intValue(3));
            set("DrawScale", Value::floatValue(1));
            set("ScaleGlow", Value::floatValue(1));
        }
        c.vm.level->chainSparkUpdate(c.self);
    });
    // ChainSparkTick(float DeltaTime): ScriptLevel::chainSparkTick.
    reg("CTVE_ChainSpark.ChainSparkTick", [](NativeCall &c) {
        if (c.vm.level && c.self)
            c.vm.level->chainSparkTick(c.self, c.f(0));
    });
    // ForceUpdate(): laid out again now.
    reg("CTVE_ChainSpark.ForceUpdate", [](NativeCall &c) {
        if (c.vm.level && c.self)
            c.vm.level->chainSparkUpdate(c.self);
    });
    // UpdateBeamSections(float DeltaTime): ScriptLevel::updateBeamSections.
    reg("Actor.UpdateBeamSections", [](NativeCall &c) {
        if (c.vm.level && c.self)
            c.vm.level->updateBeamSections(c.self, c.f(0));
    });
    // SparkTick(float DeltaTime): ScriptLevel::sparkTick.
    reg("CTVE_Sparks.SparkTick", [](NativeCall &c) {
        if (c.vm.level && c.self)
            c.vm.level->sparkTick(c.self, c.f(0));
    });
    // WeatherTick(float DeltaTime), InitialSetUp(): ScriptLevel::weatherTick.
    reg("CTVE_Weather.WeatherTick", [](NativeCall &c) {
        if (c.vm.level && c.self)
            c.vm.level->weatherTick(c.self, c.f(0), false);
    });
    reg("CTVE_Weather.InitialSetUp", [](NativeCall &c) {
        if (c.vm.level && c.self)
            c.vm.level->weatherTick(c.self, 0, true);
    });
    // GetWaterLine(Vector Start, out Vector End): End brought back to the
    // water's surface between them (ScriptLevel::waterLine).
    reg("Actor.GetWaterLine", [](NativeCall &c) {
        if (!c.vm.level || c.args.size() < 2)
            return;
        const auto at = c.vm.level->waterLine(Vm::vectorOf(c.args[0]), Vm::vectorOf(c.args[1]));
        c.args[1] = c.vm.vectorValue(at[0], at[1], at[2]);
    });
    // AttachDecal(float TraceDistance, optional Vector DecalDir): stuck to
    // the surface behind it (ScriptLevel::attachDecal); DetachDecal() off
    // again.
    reg("Decal.AttachDecal", [](NativeCall &c) {
        const bool dir = c.given.size() > 1 && c.given[1];
        c.result = Value::objectValue(
            c.vm.level && c.self
                ? c.vm.level->attachDecal(c.self, c.f(0), dir ? Vm::vectorOf(c.args[1]) : std::array<float, 3>{})
                : nullptr);
    });
    reg("Decal.DetachDecal", [](NativeCall &c) {
        if (c.vm.level)
            c.vm.level->detachDecal(c.self);
    });
}

} // namespace reluva::unreal
