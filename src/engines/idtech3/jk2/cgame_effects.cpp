// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Jedi Outcast's client game's effects calls (218, 254 to 269; see
// docs/engines/idtech3/games/jk2/systems/cgame-calls.md): Raven's effects
// system run for it, what it draws added to its scene as the original's
// refEntity_t, polygons and lights.

#include "engines/idtech3/eliteforce/cgame_host.h"

#include "engines/idtech3/jk2/ref_entity.h"

#include <cstring>

namespace reluva::idtech3::eliteforce {

namespace {

enum EffectCall : int {
    kFxAddLine = 218,
    kFxRegisterEffect = 254,
    kFxPlaySimpleEffect = 255,
    kFxPlayEffect = 256,
    kFxPlayEntityEffect = 257,
    kFxPlaySimpleEffectId = 258,
    kFxPlayEffectId = 259,
    kFxPlayEntityEffectId = 260,
    kFxPlayBoltedEffectId = 261,
    kFxAddScheduledEffects = 262,
    kFxInitSystem = 263,
    kFxFreeSystem = 264,
    kFxAdjustTime = 265,
    kFxAddPoly = 266,
    kFxAddBezier = 267,
    kFxAddPrimitive = 268,
    kFxAddSprite = 269,
};

// What the client game answers through its shared buffer (cg_public.h's
// export numbers).
constexpr int32_t kCgGetLerpOrigin = 10;
constexpr int32_t kCgImpactMark = 23;

constexpr int32_t kContentsSolid = 1;
constexpr int32_t kChanAuto = 0;

// Render flags of the effects (Jedi Outcast's tr_types.h).
constexpr int32_t kRfDepthHack = 0x8;
constexpr int32_t kRfTapered = 0x8000; // electricity narrowing to its end

using Vec3 = raven::EffectSystem::Vec3;
using Axis = raven::EffectSystem::Axis;

Vec3 vecAt(const Qvm &vm, int32_t address) {
    return {Qvm::asFloat(vm.readInt(address)), Qvm::asFloat(vm.readInt(address + 4)),
            Qvm::asFloat(vm.readInt(address + 8))};
}

void putInt(std::vector<uint8_t> &b, size_t at, int32_t v) {
    std::memcpy(b.data() + at, &v, 4);
}
void putFloat(std::vector<uint8_t> &b, size_t at, float v) {
    std::memcpy(b.data() + at, &v, 4);
}
void putVec(std::vector<uint8_t> &b, size_t at, const Vec3 &v) {
    for (size_t i = 0; i < 3; ++i)
        putFloat(b, at + 4 * i, v[i]);
}

} // namespace

raven::EffectSystem::Host CgameHost::effectHost() {
    raven::EffectSystem::Host h;
    h.registerShader = [this](const std::string &name) { return _shaders.add(name); };
    h.registerModel = [this](const std::string &name) { return _models.add(name); };
    h.registerSound = [this](const std::string &name) { return _sounds.add(name); };
    h.trace = [this](const Vec3 &start, const Vec3 &end, const Vec3 &mins, const Vec3 &maxs) {
        raven::EffectSystem::Trace out{1, end, {}, 0};
        if (!_map)
            return out;
        if (!_world)
            _world = std::make_unique<CollisionWorld>(*_map);
        const TraceResult t = _world->trace(start, end, mins, maxs, 0, kContentsSolid);
        out.fraction = t.fraction;
        out.endPos = t.endPos;
        out.normal = t.normal;
        out.surfaceFlags = t.surfaceFlags;
        return out;
    };
    h.solidAt = [this](const Vec3 &p) {
        if (!_map)
            return false;
        if (!_world)
            _world = std::make_unique<CollisionWorld>(*_map);
        return (_world->pointContents(p, 0) & kContentsSolid) != 0;
    };
    h.playSound = [this](const Vec3 &at, int32_t sound) {
        if (soundSystem && sound)
            soundSystem->startSound(at, kEntityNone, kChanAuto, _sounds.name(sound));
    };
    // The client game makes decals' marks itself, and knows where its
    // entities are: asked through the shared buffer, as the original does.
    h.decal = [this](const raven::EffectSystem::Decal &d) {
        if (!_sharedBuffer || !_vm.valid(_sharedBuffer, 64))
            return;
        _vm.writeInt(_sharedBuffer, d.shader);
        for (int i = 0; i < 3; ++i) {
            _vm.writeInt(_sharedBuffer + 4 + 4 * i, Qvm::fromFloat(d.point[size_t(i)]));
            _vm.writeInt(_sharedBuffer + 16 + 4 * i, Qvm::fromFloat(d.normal[size_t(i)]));
            _vm.writeInt(_sharedBuffer + 32 + 4 * i, Qvm::fromFloat(d.rgb[size_t(i)]));
        }
        _vm.writeInt(_sharedBuffer + 28, Qvm::fromFloat(d.rotation));
        _vm.writeInt(_sharedBuffer + 44, Qvm::fromFloat(d.alpha));
        _vm.writeInt(_sharedBuffer + 48, Qvm::fromFloat(d.size));
        int32_t result = 0;
        _vm.call(kCgImpactMark, {}, result);
    };
    // A bolted effect's bolt, as the effect cloud places it (0x411c20):
    // the entity's number put in the shared buffer (TCGBoltPos's, at 24)
    // and its first vector read back as both the angles and the origin the
    // instance stands at, though the original never asks the client game
    // to fill it; the bolt at the effects' clock.
    h.boltPoint = [this](const raven::EffectSystem::Bolt &b) -> std::optional<Vec3> {
        if (!_sharedBuffer || !_vm.valid(_sharedBuffer, 28) || !_ghoul2.hasModel(b.ghoul2, b.model))
            return std::nullopt;
        _vm.writeInt(_sharedBuffer + 24, b.entity);
        const Vec3 placed = vecAt(_vm, _sharedBuffer);
        Bone34 m{};
        if (!_ghoul2.boltMatrix(b.ghoul2, b.model, b.bolt, _fx.time(), placed, placed, b.scale, m))
            return std::nullopt;
        return Vec3{m[3], m[7], m[11]};
    };
    h.entityOrigin = [this](int32_t entity) -> std::optional<Vec3> {
        if (!_sharedBuffer || !_vm.valid(_sharedBuffer, 16))
            return std::nullopt;
        _vm.writeInt(_sharedBuffer, entity);
        int32_t result = 0;
        if (_vm.call(kCgGetLerpOrigin, {}, result))
            return std::nullopt;
        return vecAt(_vm, _sharedBuffer + 4);
    };
    return h;
}

void CgameHost::addEffects(const raven::EffectSystem::Frame &frame) {
    const RefEntityLayout &l = jk2::kRefEntityLayout;
    for (const raven::EffectSystem::Drawn &d : frame.drawn) {
        using D = raven::EffectSystem::Drawn;
        std::vector<uint8_t> e(l.size);
        const int32_t type = d.kind == D::kSprite        ? l.typeSprite
                             : d.kind == D::kQuad        ? l.typeQuad
                             : d.kind == D::kLine        ? l.typeLine
                             : d.kind == D::kElectricity ? l.typeBolt
                             : d.kind == D::kCylinder    ? l.typeCylinder
                                                         : l.typeModel;
        putInt(e, l.type, type);
        putInt(e, l.flags, (d.depthHack ? kRfDepthHack : 0) | (d.taper ? kRfTapered : 0));
        putVec(e, l.origin, d.origin);
        std::memcpy(e.data() + l.rgba, d.rgba.data(), 4);
        putFloat(e, l.radius, d.radius);
        putFloat(e, l.rotation, d.rotation);
        switch (d.kind) {
        case D::kSprite:
            putInt(e, l.customShader, d.media);
            break;
        case D::kQuad:
            putInt(e, l.customShader, d.media);
            putVec(e, l.axes, d.axis[0]);
            break;
        case D::kLine:
            putInt(e, l.customShader, d.media);
            putVec(e, l.oldOrigin, d.end);
            break;
        case D::kElectricity:
            // Its chaos in the first axis' first word, its shape's seed as
            // the frame.
            putInt(e, l.customShader, d.media);
            putVec(e, l.oldOrigin, d.end);
            putFloat(e, l.jitter, d.chaos);
            putInt(e, l.frame, d.seed);
            break;
        case D::kCylinder:
            // As wide as the radius at the old origin, the rotation's width
            // at the origin.
            putInt(e, l.customShader, d.media);
            putVec(e, l.axes, d.axis[0]);
            putVec(e, l.oldOrigin, d.end);
            putFloat(e, l.rotation, d.size2);
            break;
        case D::kModel:
            putInt(e, l.model, d.media);
            for (size_t i = 0; i < 3; ++i)
                putVec(e, l.axes + 12 * i, d.axis[i]);
            putInt(e, l.axes + 36, 1); // the axes carry its scale
            putVec(e, l.oldOrigin, d.origin);
            break;
        }
        _scene.addEntity(e.data(), e.size(), int32_t(l.size));
    }
    for (const raven::EffectSystem::Poly &p : frame.polys) {
        std::vector<uint8_t> v(p.vertices.size() * 24);
        for (size_t i = 0; i < p.vertices.size(); ++i) {
            putVec(v, 24 * i, p.vertices[i].xyz);
            putFloat(v, 24 * i + 12, p.vertices[i].st[0]);
            putFloat(v, 24 * i + 16, p.vertices[i].st[1]);
            std::memcpy(v.data() + 24 * i + 20, p.vertices[i].rgba.data(), 4);
        }
        _scene.addPoly(p.shader, int32_t(p.vertices.size()), v.data());
    }
    for (const raven::EffectSystem::Light &light : frame.lights)
        _scene.addLight(light.origin, light.radius, light.rgb[0], light.rgb[1], light.rgb[2]);
}

int32_t CgameHost::jk2EffectCall(Qvm &vm, int number, const int32_t *a) {
    auto f = [&](int i) { return Qvm::asFloat(a[i]); };
    auto vec = [&](int32_t address) { return vm.valid(address, 12) ? vecAt(vm, address) : Vec3{}; };
    auto axis = [&](int32_t address) {
        return vm.valid(address, 36) ? Axis{vecAt(vm, address), vecAt(vm, address + 12), vecAt(vm, address + 24)}
                                     : Axis{};
    };
    auto fl = [&](int32_t address) { return Qvm::asFloat(vm.readInt(address)); };
    // A simple effect faces up, its other axes x and y.
    const Axis kUp{Vec3{0, 0, 1}, Vec3{1, 0, 0}, Vec3{0, 1, 0}};
    using Values = raven::EffectSystem::Values;
    switch (number) {
    case kFxAddLine: // (start, end, size1, size2, sizeParm, alpha1, alpha2, alphaParm, rgb1, rgb2, rgbParm, life, shader, flags)
        _fx.addLine(vec(a[0]), vec(a[1]), {f(2), f(3), f(4)}, {f(5), f(6), f(7)}, vec(a[8]), vec(a[9]), f(10), a[11],
                    a[12], a[13]);
        return 0;
    case kFxRegisterEffect: // (file)
        return _fx.registerEffect(vm.string(a[0], 256));
    case kFxPlaySimpleEffect: // (file, origin)
        _fx.play(_fx.registerEffect(vm.string(a[0], 256)), vec(a[1]), kUp);
        return 0;
    case kFxPlayEffect: // (file, origin, forward)
        _fx.play(_fx.registerEffect(vm.string(a[0], 256)), vec(a[1]), vec(a[2]));
        return 0;
    case kFxPlayEntityEffect: // (file, origin, axes, bolt, entity)
        _fx.play(_fx.registerEffect(vm.string(a[0], 256)), vec(a[1]), axis(a[2]), a[4]);
        return 0;
    case kFxPlaySimpleEffectId: // (id, origin)
        _fx.play(a[0], vec(a[1]), kUp);
        return 0;
    case kFxPlayEffectId: // (id, origin, forward)
        _fx.play(a[0], vec(a[1]), vec(a[2]));
        return 0;
    case kFxPlayEntityEffectId: // (id, origin, axes, bolt, entity)
        _fx.play(a[0], vec(a[1]), axis(a[2]), a[4]);
        return 0;
    case kFxPlayBoltedEffectId: { // (id, the bolt: as jk2mp.exe reads it, 0x411860)
        const int32_t b = a[1];
        if (!vm.valid(b, 56))
            return 0;
        raven::EffectSystem::Bolt bolt;
        bolt.ghoul2 = vm.readInt(b + 4);
        bolt.model = vm.readInt(b + 8);
        bolt.bolt = vm.readInt(b + 12);
        bolt.scale = vecAt(vm, b + 40);
        bolt.entity = vm.readInt(b + 52);
        _fx.playBolted(a[0], vecAt(vm, b + 28), vecAt(vm, b + 16), bolt);
        return 0;
    }
    case kFxAddScheduledEffects: {
        raven::EffectSystem::Frame frame;
        _fx.run(frame);
        addEffects(frame);
        return 0;
    }
    case kFxInitSystem:
    case kFxFreeSystem:
        _fx.clear();
        return 1;
    case kFxAdjustTime: // (time, view origin, view axes)
        _fx.setTime(a[0], vec(a[1]), axis(a[2]));
        return 0;
    case kFxAddPoly: { // addpolyArgStruct_t
        const int32_t p = a[0];
        if (!vm.valid(p, 180))
            return 0;
        const int32_t count = std::clamp(vm.readInt(p + 80), 0, 4);
        std::vector<Vec3> points;
        std::vector<std::array<float, 2>> st;
        for (int32_t i = 0; i < count; ++i) {
            points.push_back(vecAt(vm, p + 12 * i));
            st.push_back({fl(p + 48 + 8 * i), fl(p + 52 + 8 * i)});
        }
        _fx.addPoly(points, st, vecAt(vm, p + 84), vecAt(vm, p + 96), {fl(p + 108), fl(p + 112), fl(p + 116)},
                    vecAt(vm, p + 120), vecAt(vm, p + 132), fl(p + 144), vecAt(vm, p + 148), fl(p + 160),
                    vm.readInt(p + 164), vm.readInt(p + 168), vm.readInt(p + 172), vm.readInt(p + 176));
        return 0;
    }
    case kFxAddBezier: { // addbezierArgStruct_t
        const int32_t p = a[0];
        if (!vm.valid(p, 136))
            return 0;
        _fx.addBezier(vecAt(vm, p), vecAt(vm, p + 12), vecAt(vm, p + 24), vecAt(vm, p + 36), vecAt(vm, p + 48),
                      vecAt(vm, p + 60), Values{fl(p + 72), fl(p + 76), fl(p + 80)},
                      Values{fl(p + 84), fl(p + 88), fl(p + 92)}, vecAt(vm, p + 96), vecAt(vm, p + 108), fl(p + 120),
                      vm.readInt(p + 124), vm.readInt(p + 128), vm.readInt(p + 132));
        return 0;
    }
    case kFxAddPrimitive: { // effectTrailArgStruct_t: four corners of 84 bytes, shader, flags, life
        const int32_t p = a[0];
        if (!vm.valid(p, 348))
            return 0;
        std::array<raven::EffectSystem::TrailVertex, 4> corners{};
        for (int32_t i = 0; i < 4; ++i) {
            const int32_t v = p + 84 * i;
            corners[size_t(i)] = {
                vecAt(vm, v), vecAt(vm, v + 12), fl(v + 48), {fl(v + 60), fl(v + 64)}, {fl(v + 68), fl(v + 72)}};
        }
        _fx.addTrail(corners, vm.readInt(p + 336), vm.readInt(p + 344));
        return 0;
    }
    case kFxAddSprite: { // addspriteArgStruct_t
        const int32_t p = a[0];
        if (!vm.valid(p, 72))
            return 0;
        // Its alpha's parm is linear's flag, as the original passes it.
        _fx.addSprite(vecAt(vm, p), vecAt(vm, p + 12), vecAt(vm, p + 24), fl(p + 36),
                      Values{fl(p + 44), fl(p + 48), float(raven::EffectPrimitive::kLinear)}, fl(p + 52), fl(p + 56),
                      vm.readInt(p + 60), vm.readInt(p + 64), vm.readInt(p + 68));
        return 0;
    }
    default:
        if (!_unhandled[number]++)
            _firstUnhandled[number] = {a[0], a[1], a[2], a[3], a[4], a[5]};
        return 0;
    }
}

} // namespace reluva::idtech3::eliteforce
