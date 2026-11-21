// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The rules here (how each primitive is placed, moved, faded and drawn)
// are worked out in docs/engines/idtech3/formats/efx.md, with the GPL Jedi
// Outcast source's effects system read as a guide.

#include "engines/idtech3/raven/effects.h"

#include "common/fs.h"

#include <set>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace reluva::idtech3::raven {

namespace {

using Vec3 = EffectSystem::Vec3;
using Axis = EffectSystem::Axis;
using Primitive = EffectPrimitive;

constexpr float kPi = 3.14159265358979f;
constexpr float kTraceDistance = 65536; // org2FromTrace's reach: the world's size
constexpr int32_t kSurfaceNoImpact = 0x10;
constexpr int kBezierSteps = 16;
constexpr int32_t kEmitterStep = 12; // ms between an emitter's checks

Vec3 add(const Vec3 &a, const Vec3 &b) {
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}
Vec3 sub(const Vec3 &a, const Vec3 &b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
Vec3 scale(const Vec3 &a, float s) {
    return {a[0] * s, a[1] * s, a[2] * s};
}
Vec3 madd(const Vec3 &a, float s, const Vec3 &b) {
    return {a[0] + s * b[0], a[1] + s * b[1], a[2] + s * b[2]};
}
float dot(const Vec3 &a, const Vec3 &b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vec3 cross(const Vec3 &a, const Vec3 &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float length(const Vec3 &a) {
    return std::sqrt(dot(a, a));
}
Vec3 normalized(const Vec3 &a) {
    const float len = length(a);
    return len > 0 ? scale(a, 1 / len) : a;
}

// Two axes square to `forward` (Quake III's MakeNormalVectors).
void normalVectors(const Vec3 &forward, Vec3 &right, Vec3 &up) {
    right = {forward[2], -forward[0], forward[1]};
    right = normalized(madd(right, -dot(right, forward), forward));
    up = cross(right, forward);
}

// `point` turned `degrees` about the unit vector `axis`.
Vec3 rotateAround(const Vec3 &axis, const Vec3 &point, float degrees) {
    const float r = degrees * kPi / 180;
    const float c = std::cos(r);
    const float s = std::sin(r);
    return add(add(scale(point, c), scale(cross(axis, point), s)), scale(axis, dot(axis, point) * (1 - c)));
}

// (pitch, yaw, roll) in degrees -> forward, left, up.
Axis anglesAxis(const Vec3 &angles) {
    const float p = angles[0] * kPi / 180;
    const float y = angles[1] * kPi / 180;
    const float r = angles[2] * kPi / 180;
    const float sp = std::sin(p);
    const float cp = std::cos(p);
    const float sy = std::sin(y);
    const float cy = std::cos(y);
    const float sr = std::sin(r);
    const float cr = std::cos(r);
    const Vec3 forward{cp * cy, cp * sy, -sp};
    const Vec3 right{-sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp};
    const Vec3 up{cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp};
    return {forward, scale(right, -1), up};
}

// A direction's (pitch, yaw, 0), as Quake III's vectoangles.
Vec3 directionAngles(const Vec3 &v) {
    float yaw = 0;
    float pitch = 0;
    if (v[0] == 0 && v[1] == 0) {
        pitch = v[2] > 0 ? 90.0f : 270.0f;
    } else {
        yaw = std::atan2(v[1], v[0]) * 180 / kPi;
        if (yaw < 0)
            yaw += 360;
        pitch = std::atan2(v[2], std::sqrt(v[0] * v[0] + v[1] * v[1])) * 180 / kPi;
        if (pitch < 0)
            pitch += 360;
    }
    return {-pitch, yaw, 0};
}

uint8_t toByte(float v) {
    return uint8_t(std::clamp(v, 0.0f, 1.0f) * 255);
}

int channel(uint32_t flags, int shift) {
    return int(flags >> shift) & 0xf;
}

} // namespace

std::vector<int32_t> EffectSystem::registerEffects(const std::vector<std::string> &names) {
    std::vector<int32_t> ids;
    for (const std::string &n : names)
        if (const int32_t id = registerEffect(n))
            ids.push_back(id);
    return ids;
}

int32_t EffectSystem::registerEffect(std::string_view name) {
    // Effects are known by their name without the folder or extension:
    // "blaster/shot" and "effects/blaster/shot.efx" are one.
    std::string key = fs::toLower(std::string(name));
    std::replace(key.begin(), key.end(), '\\', '/');
    if (key.rfind("effects/", 0) == 0)
        key = key.substr(8);
    if (const size_t dot = key.rfind('.'); dot != std::string::npos && key.find('/', dot) == std::string::npos)
        key = key.substr(0, dot);
    if (key.empty())
        return 0;
    if (auto it = _byName.find(key); it != _byName.end())
        return it->second;
    std::vector<uint8_t> data;
    EffectFile file;
    if (_files.read("effects/" + key + ".efx", data) ||
        !parseEffectFile(std::string_view(reinterpret_cast<const char *>(data.data()), data.size()), file)) {
        _byName[key] = 0;
        return 0;
    }
    // Numbered before the effects it runs are registered, so an effect that
    // runs itself ends.
    _effects.push_back({key, {}});
    const auto id = int32_t(_effects.size());
    _byName[key] = id;
    std::vector<Template> templates;
    for (Primitive &p : file.primitives) {
        Template t;
        for (const auto &[kind, media] : p.media) {
            const auto &reg = kind == Primitive::kShader  ? _host.registerShader
                              : kind == Primitive::kModel ? _host.registerModel
                                                          : _host.registerSound;
            if (reg)
                t.media.push_back(reg(media));
        }
        t.impactFx = registerEffects(p.impactFx);
        t.deathFx = registerEffects(p.deathFx);
        t.emitFx = registerEffects(p.emitFx);
        t.playFx = registerEffects(p.playFx);
        t.p = std::move(p);
        templates.push_back(std::move(t));
    }
    _effects[size_t(id - 1)].templates = std::move(templates);
    return id;
}

void EffectSystem::play(int32_t id, const Vec3 &origin, const Vec3 &forward) {
    if (forward == Vec3{})
        return;
    Axis axis{forward, {}, {}};
    normalVectors(forward, axis[1], axis[2]);
    play(id, origin, axis);
}

void EffectSystem::play(int32_t id, const Vec3 &origin, const Axis &axis, int32_t entity) {
    playIn(id, origin, axis, entity, -1);
}

void EffectSystem::playBolted(int32_t id, const Vec3 &origin, const Vec3 &forward, const Bolt &bolt) {
    // As jk2mp.exe's (0x411860, 0x4167f0): facing the bolt's forward, the
    // parts a group (an effect cloud) whose bolt places them each frame.
    // The original starts them at a place it never sets; here, at the
    // origin given.
    if (id < 1 || size_t(id) > _effects.size())
        return;
    Axis axis{forward, {}, {}};
    normalVectors(forward, axis[1], axis[2]);
    const int32_t group = _nextGroup++;
    _bolted[group] = bolt;
    playIn(id, origin, axis, -1, group);
}

void EffectSystem::createIn(const Template &t, const Vec3 &origin, const Axis &axis, int32_t late, int32_t group) {
    const size_t before = _born.size();
    create(t, origin, axis, late);
    for (size_t i = before; i < _born.size(); ++i)
        _born[i].group = group;
}

void EffectSystem::playIn(int32_t id, const Vec3 &origin, const Axis &axis, int32_t entity, int32_t group) {
    if (id < 1 || size_t(id) > _effects.size())
        return;
    const Vec3 fromEye = sub(origin, _viewOrigin);
    const float distanceSquared = dot(fromEye, fromEye);
    const std::vector<Template> &templates = _effects[size_t(id - 1)].templates;
    for (size_t i = 0; i < templates.size(); ++i) {
        const Primitive &p = templates[i].p;
        if (p.cullRange != 0 && distanceSquared > p.cullRange * p.cullRange)
            continue;
        const auto count = int32_t(p.count.min == p.count.max ? p.count.min : pick(p.count) + 0.5f);
        // Spread evenly over the delay's range, or each at random in it.
        const float step = count > 0 ? std::fabs(p.delay.max - p.delay.min) / float(count) : 0;
        for (int32_t n = 0; n < count; ++n) {
            const auto delay = int32_t(p.spawnFlags & Primitive::kEvenDistribution ? float(n) * step : pick(p.delay));
            if (delay < 1) {
                Vec3 at = origin;
                if (entity != -1 && _host.entityOrigin)
                    at = _host.entityOrigin(entity).value_or(origin);
                createIn(templates[i], at, axis, -delay, group);
            } else {
                _scheduled.push_back({_time + delay, id, i, origin, axis, entity, group});
            }
        }
    }
}

void EffectSystem::setTime(int32_t time, const Vec3 &viewOrigin, const Axis &viewAxis) {
    _frameTime = time - _time;
    _time = time;
    _viewOrigin = viewOrigin;
    _viewAxis = viewAxis;
}

void EffectSystem::clear() {
    _effects.clear();
    _byName.clear();
    _scheduled.clear();
    _live.clear();
    _bolted.clear();
    _born.clear();
}

EffectSystem::Fade EffectSystem::fade(float start, float end, float parm, int channelFlags, int32_t life) const {
    Fade f{start, end, parm};
    if ((channelFlags & Primitive::kParmMask) == Primitive::kWave)
        f.parm = parm * kPi * 0.001f;
    else if (channelFlags & Primitive::kParmMask)
        f.parm = parm * 0.01f * float(life) + float(_time);
    return f;
}

EffectSystem::Live &EffectSystem::spawn(Live::Kind kind, int32_t life, uint32_t flags) {
    Live l;
    l.kind = kind;
    l.flags = flags;
    l.timeStart = _time;
    l.timeEnd = _time + life;
    _born.push_back(std::move(l));
    return _born.back();
}

void EffectSystem::create(const Template &t, const Vec3 &origin, const Axis &axis, int32_t late) {
    const Primitive &p = t.p;
    Axis ax = axis;
    if (p.spawnFlags & Primitive::kRandomRotationAroundForward) {
        ax[1] = rotateAround(ax[0], axis[1], random() * 360);
        ax[2] = cross(ax[0], ax[1]);
    }
    // Where it starts: the template's offset along the effect's axes (or
    // the world's), then a point on a sphere or a cylinder about it.
    const Vec3 offset{pick(p.origin[0]), pick(p.origin[1]), pick(p.origin[2])};
    Vec3 org = p.spawnFlags & Primitive::kCheapOrgCalc
                   ? offset
                   : madd(madd(scale(ax[0], offset[0]), offset[1], ax[1]), offset[2], ax[2]);
    org = add(org, origin);
    if (p.spawnFlags & Primitive::kOrgOnSphere) {
        const float x = random() * 2 * kPi;
        const float y = random() * kPi;
        const float width = pick(p.radius);
        const float height = pick(p.height);
        const Vec3 d{std::sin(x) * width * std::sin(y), std::cos(x) * width * std::sin(y), std::cos(y) * height};
        org = add(org, d);
        if (p.spawnFlags & Primitive::kAxisFromSphere) {
            ax[0] = normalized(d);
            normalVectors(ax[0], ax[1], ax[2]);
        }
    } else if (p.spawnFlags & Primitive::kOrgOnCylinder) {
        Vec3 pt = scale(ax[1], pick(p.radius));
        pt = madd(pt, crandom() * 0.5f * pick(p.height), ax[0]);
        const Vec3 d = rotateAround(ax[0], pt, random() * 360);
        org = add(org, d);
        if (p.spawnFlags & Primitive::kAxisFromSphere) {
            ax[0] = normalized(d);
            const Vec3 up = ax[0][2] == 1.0f ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
            ax[1] = cross(up, ax[0]);
            ax[2] = cross(ax[0], ax[1]);
        }
    }
    const auto along = [&](const std::array<EffectRange, 3> &r, bool absolute) {
        const Vec3 v{pick(r[0]), pick(r[1]), pick(r[2])};
        return absolute ? v : madd(madd(scale(ax[0], v[0]), v[1], ax[1]), v[2], ax[2]);
    };
    Vec3 vel{};
    Vec3 accel{};
    const bool moves = p.type == Primitive::kParticle || p.type == Primitive::kOrientedParticle ||
                       p.type == Primitive::kTail || p.type == Primitive::kEmitter;
    if (moves) {
        vel = along(p.velocity, p.spawnFlags & Primitive::kAbsoluteVelocity);
        accel = along(p.acceleration, p.spawnFlags & Primitive::kAbsoluteAcceleration);
        accel[2] += pick(p.gravity);
        // Made late: where it would be by now (the velocity counted twice
        // over, as the original moves things; see efx.md).
        if (late > 0) {
            const float t = float(late) * 0.001f;
            vel = madd(vel, t, accel);
            org = madd(org, t + t * t * 0.5f, vel);
        }
    }
    Vec3 org2{};
    if (p.type == Primitive::kLine || p.type == Primitive::kElectricity) {
        if (p.spawnFlags & Primitive::kOrg2FromTrace) {
            Vec3 end = madd(org, kTraceDistance, ax[0]);
            if (p.spawnFlags & Primitive::kOrg2IsOffset)
                end = add(end, along(p.origin2, p.spawnFlags & Primitive::kCheapOrg2Calc));
            const Trace tr = _host.trace ? _host.trace(org, end, {}, {}) : Trace{1, end, {}, 0};
            org2 = tr.endPos;
            if (p.spawnFlags & Primitive::kTraceImpactFx)
                play(pickFrom(t.impactFx), org2, tr.normal);
        } else {
            org2 = add(along(p.origin2, p.spawnFlags & Primitive::kCheapOrg2Calc), origin);
        }
    }
    Vec3 rgbStart{};
    Vec3 rgbEnd{};
    if (p.spawnFlags & Primitive::kRgbComponentInterpolation) {
        // A colour on the line between the least and the most.
        const float perc = random();
        for (size_t i = 0; i < 3; ++i) {
            rgbStart[i] = p.rgb[i].start.min + (p.rgb[i].start.max - p.rgb[i].start.min) * perc;
            rgbEnd[i] = p.rgb[i].end.min + (p.rgb[i].end.max - p.rgb[i].end.min) * perc;
        }
    } else {
        for (size_t i = 0; i < 3; ++i) {
            rgbStart[i] = pick(p.rgb[i].start);
            rgbEnd[i] = pick(p.rgb[i].end);
        }
    }
    const auto flags = uint32_t(p.flags);
    const auto life = int32_t(pick(p.life));
    // The values every drawn kind has, picked in the original's order.
    const auto common = [&](Live &l) {
        l.media = pickFrom(t.media);
        l.rgbStart = rgbStart;
        l.rgbEnd = rgbEnd;
    };
    const auto fadeOf = [&](const Primitive::Channel &c, int shift) {
        return fade(pick(c.start), pick(c.end), pick(c.parm), channel(flags, shift), life);
    };
    switch (p.type) {
    case Primitive::kParticle:
    case Primitive::kOrientedParticle:
    case Primitive::kTail:
    case Primitive::kEmitter: {
        const Live::Kind kind = p.type == Primitive::kParticle           ? Live::kParticle
                                : p.type == Primitive::kOrientedParticle ? Live::kQuad
                                : p.type == Primitive::kTail             ? Live::kTail
                                                                         : Live::kEmitter;
        Live &l = spawn(kind, life, flags);
        common(l);
        l.origin = org;
        l.oldOrigin = org;
        l.velocity = vel;
        l.oldVelocity = vel;
        l.acceleration = accel;
        l.normal = ax[0];
        l.size = fadeOf(p.size, Primitive::kSizeShift);
        if (kind == Live::kTail)
            l.length = fadeOf(p.length, Primitive::kLengthShift);
        l.alpha = fadeOf(p.alpha, Primitive::kAlphaShift);
        l.rgbParm = fade(0, 0, pick(p.rgbParm), channel(flags, Primitive::kRgbShift), life).parm;
        if (kind == Live::kParticle || kind == Live::kQuad) {
            l.rotation = pick(p.rotation);
            l.rotationDelta = pick(p.rotationDelta);
        }
        l.mins = {p.mins[0], p.mins[1], p.mins[2]};
        l.maxs = {p.maxs[0], p.maxs[1], p.maxs[2]};
        l.elasticity = pick(p.elasticity);
        l.deathFx = pickFrom(t.deathFx);
        l.impactFx = pickFrom(t.impactFx);
        if (kind == Live::kEmitter) {
            l.angles = add(Vec3{pick(p.angle[0]), pick(p.angle[1]), pick(p.angle[2])}, directionAngles(ax[0]));
            l.angleDelta = {pick(p.angleDelta[0]), pick(p.angleDelta[1]), pick(p.angleDelta[2])};
            l.axis = anglesAxis(l.angles);
            l.emitFx = pickFrom(t.emitFx);
            l.density = pick(p.density);
            l.variance = pick(p.variance);
            l.oldTime = _time;
        }
        break;
    }
    case Primitive::kLine:
    case Primitive::kElectricity: {
        Live &l = spawn(p.type == Primitive::kLine ? Live::kLine : Live::kElectricity, life, flags);
        common(l);
        l.origin = org;
        l.origin2 = org2;
        l.size = fadeOf(p.size, Primitive::kSizeShift);
        l.alpha = fadeOf(p.alpha, Primitive::kAlphaShift);
        l.rgbParm = fade(0, 0, pick(p.rgbParm), channel(flags, Primitive::kRgbShift), life).parm;
        if (l.kind == Live::kElectricity) {
            l.chaos = pick(p.elasticity);
            l.seed = int32_t(random() * 1265536);
        }
        break;
    }
    case Primitive::kCylinder: {
        Live &l = spawn(Live::kCylinder, life, flags);
        common(l);
        l.origin = org;
        l.normal = ax[0];
        l.size = fadeOf(p.size, Primitive::kSizeShift);
        l.size2 = fadeOf(p.size2, Primitive::kSize2Shift);
        l.length = fadeOf(p.length, Primitive::kLengthShift);
        l.alpha = fadeOf(p.alpha, Primitive::kAlphaShift);
        l.rgbParm = fade(0, 0, pick(p.rgbParm), channel(flags, Primitive::kRgbShift), life).parm;
        break;
    }
    case Primitive::kLight: {
        Live &l = spawn(Live::kLight, life, flags);
        l.rgbStart = rgbStart;
        l.rgbEnd = rgbEnd;
        l.origin = org;
        l.size = fadeOf(p.size, Primitive::kSizeShift);
        l.rgbParm = fade(0, 0, pick(p.rgbParm), channel(flags, Primitive::kRgbShift), life).parm;
        break;
    }
    case Primitive::kDecal:
        if (_host.decal)
            _host.decal(
                {pickFrom(t.media), org, ax[0], pick(p.rotation), rgbStart, pick(p.alpha.start), pick(p.size.start)});
        break;
    case Primitive::kSound:
        if (_host.playSound)
            _host.playSound(org, pickFrom(t.media));
        break;
    case Primitive::kFxRunner:
        play(pickFrom(t.playFx), org, ax);
        break;
    case Primitive::kCameraShake:
        break; // Jedi Outcast's multiplayer engine works out how hard, then does nothing with it
    }
}

void EffectSystem::addLine(const Vec3 &start, const Vec3 &end, Values size, Values alpha, const Vec3 &rgbStart,
                           const Vec3 &rgbEnd, float rgbParm, int32_t life, int32_t shader, int32_t flags) {
    const auto f = uint32_t(flags);
    Live &l = spawn(Live::kLine, life, f);
    l.origin = start;
    l.origin2 = end;
    l.media = shader;
    l.size = fade(size.start, size.end, size.parm, channel(f, Primitive::kSizeShift), life);
    l.alpha = fade(alpha.start, alpha.end, alpha.parm, channel(f, Primitive::kAlphaShift), life);
    l.rgbStart = rgbStart;
    l.rgbEnd = rgbEnd;
    l.rgbParm = fade(0, 0, rgbParm, channel(f, Primitive::kRgbShift), life).parm;
}

void EffectSystem::addSprite(const Vec3 &origin, const Vec3 &velocity, const Vec3 &acceleration, float size,
                             Values alpha, float rotation, float bounce, int32_t life, int32_t shader, int32_t flags) {
    const auto f = uint32_t(flags);
    Live &l = spawn(Live::kParticle, life, f);
    l.origin = origin;
    l.velocity = velocity;
    l.acceleration = acceleration;
    l.media = shader;
    l.size = {size, size, 0};
    l.alpha = fade(alpha.start, alpha.end, alpha.parm, channel(f, Primitive::kAlphaShift), life);
    l.rgbStart = {1, 1, 1};
    l.rgbEnd = {1, 1, 1};
    l.rotation = rotation;
    l.elasticity = bounce;
}

void EffectSystem::addPoly(const std::vector<Vec3> &points, const std::vector<std::array<float, 2>> &st,
                           const Vec3 &velocity, const Vec3 &acceleration, Values alpha, const Vec3 &rgbStart,
                           const Vec3 &rgbEnd, float rgbParm, const Vec3 &rotationDelta, float bounce,
                           int32_t motionDelay, int32_t life, int32_t shader, int32_t flags) {
    if (points.empty())
        return;
    const auto f = uint32_t(flags);
    Live &l = spawn(Live::kPoly, life, f);
    // Kept about its middle, which moves; the corners turn about it.
    Vec3 middle{};
    for (const Vec3 &p : points)
        middle = add(middle, p);
    middle = scale(middle, 1.0f / float(points.size()));
    l.origin = middle;
    for (const Vec3 &p : points)
        l.corners.push_back(sub(p, middle));
    l.st = st;
    l.st.resize(points.size());
    l.velocity = velocity;
    l.acceleration = acceleration;
    l.alpha = fade(alpha.start, alpha.end, alpha.parm, channel(f, Primitive::kAlphaShift), life);
    l.rgbStart = rgbStart;
    l.rgbEnd = rgbEnd;
    l.rgbParm = fade(0, 0, rgbParm, channel(f, Primitive::kRgbShift), life).parm;
    l.turn = rotationDelta;
    l.elasticity = bounce;
    l.motionTime = _time + motionDelay;
    l.media = shader;
}

void EffectSystem::addBezier(const Vec3 &start, const Vec3 &end, const Vec3 &control1, const Vec3 &control1Velocity,
                             const Vec3 &control2, const Vec3 &control2Velocity, Values size, Values alpha,
                             const Vec3 &rgbStart, const Vec3 &rgbEnd, float rgbParm, int32_t life, int32_t shader,
                             int32_t flags) {
    const auto f = uint32_t(flags);
    Live &l = spawn(Live::kBezier, life, f);
    l.origin = start;
    l.origin2 = end;
    l.control1 = control1;
    l.control2 = control2;
    l.control1Velocity = control1Velocity;
    l.control2Velocity = control2Velocity;
    l.size = fade(size.start, size.end, size.parm, channel(f, Primitive::kSizeShift), life);
    l.alpha = fade(alpha.start, alpha.end, alpha.parm, channel(f, Primitive::kAlphaShift), life);
    l.rgbStart = rgbStart;
    l.rgbEnd = rgbEnd;
    l.rgbParm = fade(0, 0, rgbParm, channel(f, Primitive::kRgbShift), life).parm;
    l.media = shader;
}

void EffectSystem::addTrail(const std::array<TrailVertex, 4> &corners, int32_t shader, int32_t life) {
    Live &l = spawn(Live::kTrail, life, 0);
    l.trail = corners;
    l.media = shader;
}

float EffectSystem::fadeValue(const Live &l, const Fade &f, int flags, bool randomOnValue) {
    const float now = float(_time);
    const float start = float(l.timeStart);
    const float end = float(l.timeEnd);
    float perc = 1;
    if (flags & Primitive::kLinear)
        perc = end > start ? 1 - (now - start) / (end - start) : 0;
    const int parm = flags & Primitive::kParmMask;
    if (parm == Primitive::kNonLinear || parm == Primitive::kClamp) {
        // Non-linear: full until the parm's time, then fading; clamp: fading
        // until then, then gone. Half and half with linear if that's set too.
        float perc2 = 1;
        if (parm == Primitive::kNonLinear) {
            if (now > f.parm && end > f.parm)
                perc2 = 1 - (now - f.parm) / (end - f.parm);
        } else {
            perc2 = now < f.parm && f.parm > start ? (f.parm - now) / (f.parm - start) : 0;
        }
        perc = flags & Primitive::kLinear ? perc * 0.5f + perc2 * 0.5f : perc2;
    } else if (parm == Primitive::kWave) {
        perc *= std::cos((now - start) * f.parm);
    }
    if (randomOnValue) {
        float v = std::clamp(f.start * perc + f.end * (1 - perc), 0.0f, 1.0f);
        return flags & Primitive::kRandom ? random() * v : v;
    }
    if (flags & Primitive::kRandom)
        perc *= random();
    return f.start * perc + f.end * (1 - perc);
}

void EffectSystem::updateColour(Live &l) {
    // The colour fades as the rgb channel says; the alpha either goes to
    // the alpha byte (useAlpha) or darkens the colour, for additive shaders.
    const float perc = fadeValue(l, {1, 0, l.rgbParm}, channel(l.flags, Primitive::kRgbShift), false);
    for (size_t i = 0; i < 3; ++i)
        l.rgb[i] = l.rgbStart[i] * perc + l.rgbEnd[i] * (1 - perc);
    if (l.kind == Live::kLight)
        return;
    const float alpha = fadeValue(l, l.alpha, channel(l.flags, Primitive::kAlphaShift), true);
    if (l.flags & Primitive::kUseAlpha) {
        l.rgba = {toByte(l.rgb[0]), toByte(l.rgb[1]), toByte(l.rgb[2]), toByte(alpha)};
    } else {
        l.rgba = {toByte(l.rgb[0] * alpha), toByte(l.rgb[1] * alpha), toByte(l.rgb[2] * alpha), 0xff};
    }
}

bool EffectSystem::updateOrigin(Live &l) {
    const float t = float(_frameTime) * 0.001f;
    l.velocity = madd(l.velocity, t, l.acceleration);
    // As the original: the velocity again where half the acceleration
    // belongs (see efx.md).
    const Vec3 next = madd(l.origin, t + t * t * 0.5f, l.velocity);
    if (l.flags & Primitive::kApplyPhysics) {
        const bool solid = (l.flags & Primitive::kExpensivePhysics) || (_host.solidAt && _host.solidAt(next));
        if (solid && _host.trace) {
            const bool box = l.flags & Primitive::kUseBBox;
            const Trace tr = _host.trace(l.origin, next, box ? l.mins : Vec3{}, box ? l.maxs : Vec3{});
            if (tr.fraction < 1) {
                if ((l.flags & Primitive::kImpactRunsFx) && !(tr.surfaceFlags & kSurfaceNoImpact))
                    play(l.impactFx, tr.endPos, tr.normal);
                if (l.flags & Primitive::kKillOnImpact)
                    return false;
                // Bounced off, losing what the elasticity takes; come to rest
                // on a floor once slow.
                l.velocity = madd(l.velocity, t * tr.fraction, l.acceleration);
                l.velocity = madd(l.velocity, -2 * dot(l.velocity, tr.normal), tr.normal);
                l.velocity = scale(l.velocity, l.elasticity);
                if (tr.normal[2] > 0 && l.velocity[2] < 4) {
                    l.velocity = {};
                    l.acceleration = {};
                    l.flags &= ~(Primitive::kApplyPhysics | Primitive::kImpactRunsFx);
                }
                l.origin = tr.endPos;
                return true;
            }
        }
    }
    l.origin = next;
    return true;
}

bool EffectSystem::update(Live &l) {
    if (l.timeStart > _time)
        return false; // the clock went back
    const auto sizeOf = [&](const Fade &f, int shift) { return fadeValue(l, f, channel(l.flags, shift), false); };
    switch (l.kind) {
    case Live::kParticle:
    case Live::kQuad:
        if (!updateOrigin(l))
            return false;
        l.radius = sizeOf(l.size, Primitive::kSizeShift);
        updateColour(l);
        l.rotation += float(_frameTime) * 0.01f * l.rotationDelta;
        return true;
    case Live::kTail: {
        const Vec3 old = l.origin;
        if (!updateOrigin(l))
            return false;
        l.radius = sizeOf(l.size, Primitive::kSizeShift);
        l.len = sizeOf(l.length, Primitive::kLengthShift);
        updateColour(l);
        l.origin2 = madd(l.origin, l.len, normalized(sub(old, l.origin)));
        return true;
    }
    case Live::kLine:
    case Live::kElectricity:
        l.radius = sizeOf(l.size, Primitive::kSizeShift);
        updateColour(l);
        return true;
    case Live::kCylinder:
        l.radius = sizeOf(l.size, Primitive::kSizeShift);
        l.radius2 = sizeOf(l.size2, Primitive::kSize2Shift);
        l.len = sizeOf(l.length, Primitive::kLengthShift);
        updateColour(l);
        return true;
    case Live::kEmitter: {
        l.oldOrigin = l.origin;
        l.oldVelocity = l.velocity;
        if (!updateOrigin(l))
            return false;
        // Turning slows once it's come to rest.
        if (l.origin == l.oldOrigin)
            l.angleDelta = scale(l.angleDelta, 0.7f);
        l.angles = madd(l.angles, float(_frameTime) * 0.01f, l.angleDelta);
        l.axis = anglesAxis(l.angles);
        l.radius = sizeOf(l.size, Primitive::kSizeShift);
        return true;
    }
    case Live::kLight:
        l.radius = sizeOf(l.size, Primitive::kSizeShift);
        updateColour(l);
        return true;
    case Live::kPoly:
        updateColour(l);
        if (_time > l.motionTime) {
            const Vec3 old = l.origin;
            if (!updateOrigin(l))
                return false;
            if (old != l.origin) {
                // Pitch about x, then yaw about z, by this frame's share.
                const float yaw = l.turn[1] * float(_frameTime) * 0.01f * kPi / 180;
                const float pitch = l.turn[0] * float(_frameTime) * 0.01f * kPi / 180;
                const float cz = std::cos(yaw);
                const float sz = std::sin(yaw);
                const float cx = std::cos(pitch);
                const float sx = std::sin(pitch);
                const Axis rot{Vec3{cz, cx * sz, sx * sz}, Vec3{-sz, cx * cz, sx * cz}, Vec3{0, -sx, cx}};
                for (Vec3 &c : l.corners)
                    c = {dot(c, rot[0]), dot(c, rot[1]), dot(c, rot[2])};
            }
        }
        return true;
    case Live::kBezier: {
        const float t = float(_frameTime) * 0.001f;
        l.control1 = madd(l.control1, t + t * t * 0.5f, l.control1Velocity);
        l.control2 = madd(l.control2, t + t * t * 0.5f, l.control2Velocity);
        l.radius = sizeOf(l.size, Primitive::kSizeShift);
        updateColour(l);
        return true;
    }
    case Live::kTrail:
        return true;
    }
    return true;
}

void EffectSystem::die(const Live &l) {
    // A particle's death effect, facing anywhere; not when an impact killed it.
    const bool particle = l.kind == Live::kParticle || l.kind == Live::kQuad || l.kind == Live::kTail ||
                          l.kind == Live::kCylinder || l.kind == Live::kEmitter || l.kind == Live::kPoly;
    if (particle && (l.flags & Primitive::kDeathRunsFx) && !(l.flags & Primitive::kKillOnImpact)) {
        const Vec3 dir = normalized(Vec3{crandom(), crandom(), crandom()});
        play(l.deathFx, l.origin, dir);
    }
}

bool EffectSystem::behindOrNear(const Vec3 &p, float near) const {
    const Vec3 d = sub(p, _viewOrigin);
    return dot(_viewAxis[0], d) < 0 || dot(d, d) < near * near;
}

void EffectSystem::draw(Live &l, Frame &out) {
    Drawn d;
    d.media = l.media;
    d.origin = l.origin;
    d.rgba = l.rgba;
    d.depthHack = l.flags & Primitive::kDepthHack;
    d.radius = l.radius;
    switch (l.kind) {
    case Live::kParticle:
        if (behindOrNear(l.origin, 12))
            return;
        d.rotation = l.rotation;
        break;
    case Live::kQuad:
        if (behindOrNear(l.origin, 16))
            return;
        d.kind = Drawn::kQuad;
        d.axis[0] = l.normal;
        d.rotation = l.rotation;
        break;
    case Live::kLine:
    case Live::kTail:
        d.kind = Drawn::kLine;
        d.end = l.origin2;
        break;
    case Live::kElectricity: {
        d.kind = Drawn::kElectricity;
        d.end = l.origin2;
        // Growing: reaching its end over its life.
        if ((l.flags & Primitive::kGrow) && l.timeEnd > l.timeStart) {
            const float perc = std::clamp(float(_time - l.timeStart) / float(l.timeEnd - l.timeStart), 0.0f, 1.0f);
            d.end = madd(l.origin, perc, sub(l.origin2, l.origin));
        }
        d.chaos = l.chaos;
        d.seed = l.seed;
        d.taper = (l.flags & Primitive::kTaper) != 0;
        break;
    }
    case Live::kCylinder:
        d.kind = Drawn::kCylinder;
        d.axis[0] = l.normal;
        d.end = madd(l.origin, l.len, l.normal);
        d.size2 = l.radius2;
        break;
    case Live::kEmitter:
        if (l.flags & Primitive::kAttachedModel) {
            d.kind = Drawn::kModel;
            for (size_t i = 0; i < 3; ++i)
                d.axis[i] = scale(l.axis[i], l.radius);
            out.drawn.push_back(d);
        }
        if ((l.flags & Primitive::kEmitFx) && l.emitFx) {
            // Along the path it took since last time, an effect each time it
            // has gone (density plus or minus variance) units.
            auto stepOf = [&] {
                const float s = l.density + crandom() * l.variance;
                return s * s;
            };
            float step = stepOf();
            int32_t since = 0;
            for (int32_t t = l.oldTime; t <= _time; t += kEmitterStep) {
                since += kEmitterStep;
                const float ft = float(since) * 0.001f;
                const Vec3 v = madd(l.oldVelocity, ft, l.acceleration);
                const Vec3 at = madd(l.oldOrigin, ft + ft * ft * 0.5f, v);
                const Vec3 gone = sub(at, l.oldOrigin);
                if (dot(gone, gone) >= step) {
                    step = stepOf();
                    play(l.emitFx, at, l.axis);
                    l.oldOrigin = at;
                    l.oldVelocity = v;
                    since = 0;
                    l.oldTime = t;
                }
            }
        }
        return;
    case Live::kLight:
        out.lights.push_back({l.origin, l.radius, l.rgb});
        return;
    case Live::kPoly: {
        if (behindOrNear(l.origin, 24))
            return;
        Poly poly{l.media, {}};
        for (size_t i = 0; i < l.corners.size(); ++i)
            poly.vertices.push_back({add(l.origin, l.corners[i]), l.st[i], l.rgba});
        out.polys.push_back(std::move(poly));
        return;
    }
    case Live::kBezier: {
        if (behindOrNear(l.origin, 0) && behindOrNear(l.origin2, 0) && behindOrNear(l.control1, 0))
            return;
        // Cut into 16 pieces, each a quad facing the eye, fading to nothing
        // along the curve; each joined to the last's end.
        Vec3 last = l.origin;
        float lastTex = 0;
        std::array<Vec3, 2> joint{};
        bool joined = false;
        for (int i = 1; i <= kBezierSteps; ++i) {
            const float mu = float(i) / kBezierSteps;
            const float m = 1 - mu;
            const Vec3 pos = add(add(scale(l.origin, m * m * m), scale(l.control1, 3 * mu * m * m)),
                                 add(scale(l.control2, 3 * mu * mu * m), scale(l.origin2, mu * mu * mu)));
            const Vec3 side = normalized(cross(sub(pos, last), sub(pos, _viewOrigin)));
            const float half = l.radius * 0.5f;
            auto shade = [&](float tex) {
                const float k = 1 - tex;
                return std::array<uint8_t, 4>{uint8_t(float(l.rgba[0]) * k), uint8_t(float(l.rgba[1]) * k),
                                              uint8_t(float(l.rgba[2]) * k), l.rgba[3]};
            };
            Poly poly{l.media, {}};
            const std::array<uint8_t, 4> startShade = lastTex == 0 ? std::array<uint8_t, 4>{} : shade(lastTex);
            poly.vertices.push_back({joined ? joint[0] : madd(last, -half, side), {0, lastTex}, startShade});
            poly.vertices.push_back({joined ? joint[1] : madd(last, half, side), {1, lastTex}, startShade});
            poly.vertices.push_back({madd(pos, half, side), {1, mu}, shade(mu)});
            poly.vertices.push_back({madd(pos, -half, side), {0, mu}, shade(mu)});
            joint = {poly.vertices[3].xyz, poly.vertices[2].xyz};
            joined = true;
            out.polys.push_back(std::move(poly));
            last = pos;
            lastTex = mu;
        }
        return;
    }
    case Live::kTrail: {
        // Two triangles: the new muzzle, new tip and old muzzle; the old
        // muzzle, old tip and new tip. The texture slides as it fades.
        const float perc = l.timeEnd > l.timeStart ? float(l.timeEnd - _time) / float(l.timeEnd - l.timeStart) : 0;
        std::array<PolyVertex, 4> v{};
        for (size_t i = 0; i < 4; ++i) {
            const TrailVertex &t = l.trail[i];
            v[i].xyz = t.origin;
            v[i].st = {std::min(1.0f, t.st[0] * perc + t.destSt[0] * (1 - perc)),
                       t.st[1] * perc + t.destSt[1] * (1 - perc)};
            v[i].rgba = {uint8_t(std::clamp(t.rgb[0], 0.0f, 255.0f)), uint8_t(std::clamp(t.rgb[1], 0.0f, 255.0f)),
                         uint8_t(std::clamp(t.rgb[2], 0.0f, 255.0f)), uint8_t(std::clamp(t.alpha, 0.0f, 255.0f))};
        }
        out.polys.push_back({l.media, {v[0], v[1], v[3]}});
        out.polys.push_back({l.media, {v[3], v[2], v[1]}});
        return;
    }
    }
    out.drawn.push_back(d);
}

void EffectSystem::run(Frame &out) {
    // The scheduled parts due, each made as late as it is.
    std::vector<Scheduled> due;
    auto it = std::stable_partition(_scheduled.begin(), _scheduled.end(),
                                    [&](const Scheduled &s) { return s.start > _time; });
    due.assign(it, _scheduled.end());
    _scheduled.erase(it, _scheduled.end());
    for (const Scheduled &s : due) {
        if (s.effect < 1 || size_t(s.effect) > _effects.size() ||
            s.index >= _effects[size_t(s.effect - 1)].templates.size())
            continue;
        Vec3 at = s.origin;
        if (s.entity != -1 && _host.entityOrigin)
            at = _host.entityOrigin(s.entity).value_or(s.origin);
        createIn(_effects[size_t(s.effect - 1)].templates[s.index], at, s.axis, _time - s.start, s.group);
    }
    // New ones join before the walk, not during it: updating can start
    // effects of its own, which are drawn from the next frame.
    std::move(_born.begin(), _born.end(), std::back_inserter(_live));
    _born.clear();
    // Each bolted effect's bolt, once a frame; its parts put there before
    // they move (the effect cloud's update, 0x411c20).
    std::map<int32_t, Vec3> boltAt;
    for (const auto &[group, bolt] : _bolted)
        if (_host.boltPoint)
            if (const std::optional<Vec3> p = _host.boltPoint(bolt))
                boltAt[group] = *p;
    size_t kept = 0;
    for (size_t i = 0; i < _live.size(); ++i) {
        Live &l = _live[i];
        if (l.group >= 0)
            if (const auto at = boltAt.find(l.group); at != boltAt.end())
                l.origin = at->second;
        bool dead = false;
        if (_time > l.timeEnd) {
            l.flags &= ~uint32_t(Primitive::kKillOnImpact);
            dead = true;
        } else if (_frameTime > 0 && !update(l)) {
            dead = true;
        }
        if (dead) {
            die(l);
            continue;
        }
        draw(l, out);
        if (kept != i)
            _live[kept] = std::move(l);
        ++kept;
    }
    _live.resize(kept);
    std::move(_born.begin(), _born.end(), std::back_inserter(_live));
    _born.clear();
    // A bolted effect with nothing left, playing or to come, is over.
    std::set<int32_t> groups;
    for (const Live &l : _live)
        groups.insert(l.group);
    for (const Scheduled &s : _scheduled)
        groups.insert(s.group);
    for (auto it = _bolted.begin(); it != _bolted.end();)
        it = groups.count(it->first) ? std::next(it) : _bolted.erase(it);
}

std::vector<BoltStrip> electricityStrips(const std::array<float, 3> &origin, const std::array<float, 3> &end,
                                         float radius, float chaos, int32_t seed, bool tapered,
                                         const std::function<float()> &random) {
    using V = std::array<float, 3>;
    auto length = [](const V &v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); };
    auto normalized = [&](V v) {
        const float len = length(v);
        if (len > 1e-6f)
            for (float &c : v)
                c /= len;
        return v;
    };
    // Quake III's MakeNormalVectors.
    auto normals = [&](const V &f, V &rt, V &up) {
        rt = V{f[2], -f[0], f[1]};
        const float d = rt[0] * f[0] + rt[1] * f[1] + rt[2] * f[2];
        for (size_t k = 0; k < 3; ++k)
            rt[k] -= d * f[k];
        rt = normalized(rt);
        up = V{rt[1] * f[2] - rt[2] * f[1], rt[2] * f[0] - rt[0] * f[2], rt[0] * f[1] - rt[1] * f[0]};
    };
    // Quake III's Q_random and Q_crandom on the seed.
    auto state = uint32_t(seed);
    auto qrandom = [&] {
        state = 69069u * state + 1;
        return float(state & 0xffff) / 65536.0f;
    };
    auto qcrandom = [&] { return 2.0f * (qrandom() - 0.5f); };
    std::vector<BoltStrip> out;
    // A step from `from` to `to`, kinked at two points: the first about
    // two thirds of the way back from `to`, a little off one side, the
    // second a third, off the other.
    auto shape = [&](const V &from, const V &to, float fromWidth, float toWidth) {
        const float a0 = 0.66f + random() * 0.1f;
        const float a1 = 0.07f + random() * 0.025f;
        const float a2 = 0.07f + random() * 0.025f;
        const float b0 = 0.33f + random() * 0.1f;
        const float b1 = -a1 + random() * 0.02f;
        const float b2 = -a2 + random() * 0.02f;
        const V fwd{to[0] - from[0], to[1] - from[1], to[2] - from[2]};
        const float dis = length(fwd) * 0.7f;
        V rt{};
        V up{};
        normals(normalized(fwd), rt, up);
        auto point = [&](float along, float r, float u) {
            V p{};
            for (size_t k = 0; k < 3; ++k)
                p[k] = from[k] * along + to[k] * (1 - along) + dis * r * rt[k] + dis * u * up[k];
            return p;
        };
        const V p1 = point(a0, a1, a2);
        const V p2 = point(b0, b1, b2);
        const float w1 = fromWidth * 0.666f + toWidth * 0.333f;
        const float w2 = fromWidth * 0.333f + toWidth * 0.666f;
        out.push_back({from, p1, fromWidth, w1});
        out.push_back({p2, p1, w1, w2});
        out.push_back({p2, to, w2, toWidth});
    };
    const V fwd{end[0] - origin[0], end[1] - origin[1], end[2] - origin[2]};
    const float dis = length(fwd);
    const V dir = normalized(fwd);
    V rt{};
    V up{};
    normals(dir, rt, up);
    V old = origin;
    V off{10, 10, 10};
    float oldPerc = 0;
    float oldWidth = radius;
    float newWidth = radius;
    for (int i = 20; float(i) <= dis; i += 20) {
        const float perc = float(i + 20) > dis ? 1.0f : float(i) / dis;
        // Each word its own random, as the original's vector macros give.
        V step{};
        for (size_t k = 0; k < 3; ++k)
            step[k] = dir[k] * (qcrandom() * 3.0f);
        for (size_t k = 0; k < 3; ++k)
            step[k] += rt[k] * (qcrandom() * 7.0f * chaos);
        for (size_t k = 0; k < 3; ++k)
            step[k] += up[k] * (qcrandom() * 7.0f * chaos);
        for (size_t k = 0; k < 3; ++k)
            off[k] += step[k];
        V cur{};
        for (size_t k = 0; k < 3; ++k)
            cur[k] = (origin[k] + off[k]) * (1 - perc) + end[k] * perc;
        if (tapered) {
            oldWidth = radius * (1 - oldPerc * oldPerc);
            newWidth = radius * (1 - perc * perc);
        }
        shape(cur, old, newWidth, oldWidth);
        old = cur;
        oldPerc = perc;
    }
    return out;
}

} // namespace reluva::idtech3::raven
