// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/lighting.h"

#include <algorithm>
#include <cmath>

namespace reluva::awe {

namespace {

// A colour through D3DRS_AMBIENT: each channel times 255, truncated, in a
// byte (0x427160).
Rgb throughByte(const Rgb& c) {
    Rgb out;
    for (size_t k = 0; k < 3; k++)
        out[k] = float(int(c[k] * 255.0f) & 0xff) / 255.0f;
    return out;
}

int sceneFlag(const Ac2Scene& scene, const char* tag) {
    for (const Ac2Record& r : scene.records)
        if (r.tag == tag)
            return r.i("value");
    return 0;
}

// A field, or what the object's constructor leaves when the record ends
// before it.
float num(const Ac2Record& r, const char* name, float otherwise) {
    const Ac2Value* v = r.field(name);
    return v ? v->f() : otherwise;
}

Rgb rgb(const Ac2Record& r, const char* red, const char* green, const char* blue, const Rgb& otherwise) {
    return {num(r, red, otherwise[0]), num(r, green, otherwise[1]), num(r, blue, otherwise[2])};
}

} // namespace

bool nightLighting(const Ac2Scene& scene, int timeOfDay) {
    if (sceneFlag(scene, "NVNT"))
        return false;
    return timeOfDay == 2 || (timeOfDay == 1 && sceneFlag(scene, "OUTD"));
}

SceneLighting sceneLighting(const Ac2Scene& scene, const GameState& state) {
    const auto time = state.values.find("time");
    const bool night = nightLighting(scene, time == state.values.end() ? 0 : time->second);
    SceneLighting out;
    // The ambient light: the camera's (0.3125 from its constructor without
    // one); at night a fixed blue, lighter outdoors (0x401100, 0x4010a0).
    // (A scene showing "spotlight.tga" would have white instead indoors,
    // and keep its lights at night; none does.)
    const Rgb fallback{0.3125f, 0.3125f, 0.3125f};
    if (!night)
        out.ambient = throughByte(scene.camera() ? rgb(*scene.camera(), "ambientR", "ambientG", "ambientB", fallback)
                                                 : fallback);
    else if (sceneFlag(scene, "OUTD"))
        out.ambient = throughByte({0.2734375f, 0.3515625f, 0.546875f});
    else
        out.ambient = throughByte({0.234375f, 0.2734375f, 0.4296875f});
    // The lights (0x420ee0): all off at night; otherwise each that's on,
    // shown (its condition holding) and not just for the shadows. Missing
    // fields keep the LITE constructor's values (0x420cb0).
    if (night)
        return out;
    for (const Ac2Record* o : scene.objects()) {
        const Ac2Value* on = o->field("on");
        if (o->tag != "LITE" || (on && on->i() == 0) || o->i("shadowOnly") != 0 ||
            !testCondition(o->text("condition"), state))
            continue;
        const Ac2Value* type = o->field("type");
        const int t = type ? type->i() : 1;
        if (t < 1 || t > 3)
            continue;
        SceneLight l;
        l.type = SceneLight::Type(t);
        l.diffuse = rgb(*o, "diffuseR", "diffuseG", "diffuseB", fallback);
        l.ambient = rgb(*o, "ambientR", "ambientG", "ambientB", {});
        l.position = {num(*o, "x", 0), num(*o, "y", 0), num(*o, "z", 0)};
        l.direction = {num(*o, "dirX", -1), num(*o, "dirY", -1), num(*o, "dirZ", -100)};
        l.range = num(*o, "range", 10000);
        l.attenuation[0] = num(*o, "att0", 0.1f);
        l.attenuation[1] = num(*o, "att1", 0.001f);
        l.attenuation[2] = num(*o, "att2", 0);
        // No attenuation at all would be refused: a little linear instead.
        if (l.type != SceneLight::Type::Directional && l.attenuation[0] + l.attenuation[1] + l.attenuation[2] == 0)
            l.attenuation[1] = 0.0001f;
        l.theta = num(*o, "theta", 0.5f), l.phi = num(*o, "phi", 1);
        out.lights.push_back(l);
    }
    return out;
}

Material materialOf(const Ac2Record& object) {
    Material m;
    m.diffuse = rgb(object, "diffuseR", "diffuseG", "diffuseB", m.diffuse);
    m.ambient = rgb(object, "ambientR", "ambientG", "ambientB", m.ambient);
    m.emissive = rgb(object, "emissiveR", "emissiveG", "emissiveB", m.emissive);
    m.lit = num(object, "b0c", 1) != 0;
    return m;
}

Rgb lightVertex(const SceneLighting& lighting, const Material& material, const float position[3],
                const float normal[3]) {
    if (!material.lit)
        return {1, 1, 1};
    Rgb c;
    for (size_t k = 0; k < 3; k++)
        c[k] = material.emissive[k] + lighting.ambient[k] * material.ambient[k];
    for (const SceneLight& l : lighting.lights) {
        // The direction towards the light, and how much of it arrives.
        float to[3];
        float strength = 1;
        if (l.type == SceneLight::Type::Directional) {
            for (size_t k = 0; k < 3; k++)
                to[k] = -l.direction[k];
        } else {
            for (size_t k = 0; k < 3; k++)
                to[k] = l.position[k] - position[k];
            const float d = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
            if (d > l.range)
                continue;
            strength = 1.0f / (l.attenuation[0] + l.attenuation[1] * d + l.attenuation[2] * d * d);
        }
        const float len = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
        if (len <= 0)
            continue;
        for (float& v : to)
            v /= len;
        if (l.type == SceneLight::Type::Spot) {
            // Full inside theta, none outside phi, linear between (falloff 1).
            const float dl = std::sqrt(l.direction[0] * l.direction[0] + l.direction[1] * l.direction[1] +
                                       l.direction[2] * l.direction[2]);
            if (dl <= 0)
                continue;
            const float rho = -(to[0] * l.direction[0] + to[1] * l.direction[1] + to[2] * l.direction[2]) / dl;
            const float inner = std::cos(l.theta / 2), outer = std::cos(l.phi / 2);
            if (rho <= outer)
                continue;
            if (rho <= inner && inner > outer)
                strength *= (rho - outer) / (inner - outer);
        }
        const float facing = std::max(0.0f, normal[0] * to[0] + normal[1] * to[1] + normal[2] * to[2]);
        for (size_t k = 0; k < 3; k++)
            c[k] += strength * (material.ambient[k] * l.ambient[k] + material.diffuse[k] * l.diffuse[k] * facing);
    }
    for (float& v : c)
        v = std::clamp(v, 0.0f, 1.0f);
    return c;
}

} // namespace reluva::awe
