// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/gizmos.h"

#include "common/audio/decode.h"
#include "engines/age/car_actor.h"
#include "engines/age/pkg.h"

#include <algorithm>
#include <cmath>

namespace reluva::age {

namespace {

float flat(const Vec3& v) { return std::sqrt(v[0] * v[0] + v[2] * v[2]); }

std::vector<float> distances(const std::vector<Vec3>& points) {
    std::vector<float> at(points.size(), 0);
    for (size_t i = 1; i < points.size(); ++i)
        at[i] = at[i - 1] + length(points[i] - points[i - 1]);
    return at;
}

bool isBridge(const std::string& model) {
    return model.find("bridge") != std::string::npos || model.find("waterloo") != std::string::npos ||
           model.find("gate") != std::string::npos;
}

// How far either side of a boat its heading is taken over: about half its
// length, so a long ferry turns as a whole.
float windowOf(const std::string& model) {
    if (model.find("ferry") != std::string::npos)
        return 40;
    if (model.find("tug") != std::string::npos || model.find("taxi") != std::string::npos)
        return 12;
    if (model.find("duck") != std::string::npos)
        return 0.5f;
    return 3;
}

float smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

} // namespace

Gizmos::Bounds Gizmos::boundsOf(const SearchSet& files, const std::string& model) {
    const auto it = bounds_.find(model);
    if (it != bounds_.end())
        return it->second;
    Bounds b;
    std::vector<uint8_t> data;
    Pkg pkg;
    if (!files.read("geometry/" + model + ".pkg", data) && !readPkg(data, pkg))
        bodyBounds(pkg, b.low, b.high);
    return bounds_[model] = b;
}

Error Gizmos::load(const SearchSet& files, const std::string& map, const std::string& variant, audio::Mixer* mixer) {
    clear();
    std::vector<PathsetPath> paths;
    for (const char* kind : {"ferry", "sailboat", "bridge"}) {
        for (PathsetPath& p : readCityPaths(files, map, kind, variant))
            paths.push_back(std::move(p));
        // A path named only "PATH<n>" among the boats: the city's sailboat
        // (San Francisco's file has one such loop, and a sailboat model no
        // path names).
        if (std::string(kind) == "sailboat")
            for (PathsetPath& p : paths)
                if (p.model.rfind("PATH", 0) == 0)
                    p.model = "giz_sailboat01_" + std::string(map == "london" ? "l" : "f");
    }

    for (const PathsetPath& p : paths) {
        if (p.points.size() < 2 || !files.has("geometry/" + p.model + ".pkg"))
            continue;
        const Bounds bounds = boundsOf(files, p.model);
        if (isBridge(p.model)) {
            // A leaf for each stretch between points, hinged at the end
            // nearer the path's ends (two leaves meeting in the middle for
            // three points).
            Bridge b;
            b.model = p.model;
            b.inactive = p.inactive;
            b.standing = p.model.find("gate") != std::string::npos;
            b.lift = b.standing ? -bounds.low[1] : 0;
            const size_t n = p.points.size();
            for (size_t i = 0; i + 1 < n; ++i) {
                const bool first = float(i) + 0.5f < float(n - 1) / 2 || n == 2;
                Leaf l;
                l.hinge = first ? p.points[i] : p.points[i + 1];
                l.tip = first ? p.points[i + 1] : p.points[i];
                l.width = std::max(1.0f, bounds.high[0] - bounds.low[0]) / 2;
                b.leaves.push_back(l);
            }
            // Out of step with each other; an open one starts up.
            b.offset = p.open ? kDown + kRising : float(bridges_.size()) * 29;
            bridges_.push_back(b);
            continue;
        }
        Boat boat;
        boat.model = p.model;
        boat.points = p.points;
        boat.backAndForth = p.backAndForth();
        if (!boat.backAndForth && flat(boat.points.back() - boat.points.front()) > 1)
            boat.points.push_back(boat.points.front());
        boat.at = distances(boat.points);
        if (boat.at.back() < 1)
            continue;
        boat.speed = std::max(0.3f, p.speed());
        boat.s = boat.at[std::min<size_t>(p.start, p.points.size() - 1)];
        boat.window = std::min(windowOf(p.model), boat.at.back() / 4);
        boat.sway = float(boats_.size()) * 1.7f;
        boats_.push_back(boat);
    }
    // The Underground's trains: their paths name no model.
    for (PathsetPath& p : readCityPaths(files, map, "train", variant)) {
        Train t;
        t.model = p.model.rfind("PATH", 0) == 0 ? "va_ug_l" : p.model;
        if (p.points.size() < 2 || !files.has("geometry/" + t.model + ".pkg"))
            continue;
        const Bounds bounds = boundsOf(files, t.model);
        t.length = std::max(2.0f, bounds.high[2] - bounds.low[2]);
        t.width = std::max(1.0f, bounds.high[0] - bounds.low[0]);
        t.lift = -bounds.low[1];
        t.height = std::max(1.0f, bounds.high[1] - bounds.low[1]);
        t.points = p.points;
        t.at = distances(p.points);
        if (t.at.back() < t.span() + 1)
            continue;
        t.u = std::min(t.at[std::min<size_t>(p.start, p.points.size() - 1)], t.at.back() - t.span());
        trains_.push_back(std::move(t));
    }
    mixer_ = mixer;
    if (mixer_ && !trains_.empty()) {
        std::vector<uint8_t> data;
        auto sound = std::make_shared<audio::Sound>();
        if ((!files.read("aud/aud22/Amb3D/LondonTube.22k.wav", data) ||
             !files.read("aud/aud11/LondonTube.11k.wav", data)) &&
            !audio::decodeSound(data, *sound))
            trainSound_ = sound;
    }
    advance(0);
    return Error::success();
}

void Gizmos::clear() {
    if (mixer_ && trainPlaying_ != audio::kNoSound)
        mixer_->stop(trainPlaying_);
    trainPlaying_ = audio::kNoSound;
    trainSound_.reset();
    mixer_ = nullptr;
    boats_.clear();
    bridges_.clear();
    trains_.clear();
    clock_ = 0;
}

Mat3 Gizmos::leafAxes(const Bridge& b, const Leaf& l, Vec3& center) const {
    // Turned up about the hinge, from the slope it lies at.
    const Vec3 d = l.tip - l.hinge;
    const float len = length(d);
    Vec3 along{d[0], 0, d[2]};
    along = flat(along) > 1e-3f ? normalized(along) : Vec3{0, 0, 1};
    const float pitch = std::atan2(d[1], flat(d)) + b.raised * kRaised;
    const Vec3 z = along * std::cos(pitch) + Vec3{0, 1, 0} * std::sin(pitch);
    const Vec3 x = normalized(cross(Vec3{0, 1, 0}, z));
    const Vec3 y = cross(z, x);
    center = l.hinge + z * (len / 2) + Vec3{0, b.lift, 0};
    return {x, y, z};
}

void Gizmos::update(float dt, CollisionWorld& world, const Vec3& listener) {
    advance(dt);
    // The nearest train heard (subwaycar.csv: out to 150 m, at 0.98),
    // louder the faster it goes.
    if (mixer_ && trainSound_) {
        float volume = 0;
        for (const Train& t : trains_)
            for (const Carriage& c : t.carriages)
                volume =
                    std::max(volume, (1 - length(c.center - listener) / 150) * (0.3f + 0.7f * t.speed / kTrainSpeed));
        if (volume > 0.01f && trainPlaying_ == audio::kNoSound)
            trainPlaying_ = mixer_->play(trainSound_, audio::SoundType::Effects, 0, 0, true);
        if (trainPlaying_ != audio::kNoSound)
            mixer_->setVolume(trainPlaying_, int(0.98f * std::clamp(volume, 0.0f, 1.0f) * audio::kMaxVolume));
    }
    // The leaves' road surfaces.
    std::vector<std::array<Vec3, 3>> surfaces;
    for (const Bridge& b : bridges_) {
        if (b.standing)
            continue;
        for (const Leaf& l : b.leaves) {
            Vec3 center;
            const Mat3 axes = leafAxes(b, l, center);
            const float half = length(l.tip - l.hinge) / 2;
            const Vec3 a = center - axes.z * half - axes.x * l.width, bb = center - axes.z * half + axes.x * l.width;
            const Vec3 c = center + axes.z * half + axes.x * l.width, d = center + axes.z * half - axes.x * l.width;
            surfaces.push_back({a, bb, c});
            surfaces.push_back({a, c, d});
        }
    }
    world.setMoving(surfaces);
}

void Gizmos::advance(float dt) {
    clock_ += dt;
    for (Boat& b : boats_) {
        const float total = b.at.back();
        b.s += float(b.way) * b.speed * dt;
        if (b.backAndForth) {
            if (b.s > total || b.s < 0) {
                b.s = std::clamp(b.s, 0.0f, total);
                b.way = -b.way;
            }
        } else {
            b.s = std::fmod(std::fmod(b.s, total) + total, total);
        }
        auto at = [&](float s) {
            if (!b.backAndForth)
                s = std::fmod(std::fmod(s, total) + total, total);
            return pointAlong(b.points, b.at, s);
        };
        b.position = at(b.s);
        Vec3 ahead = at(b.s + b.window) - at(b.s - b.window);
        ahead[1] = 0;
        ahead = flat(ahead) > 1e-3f ? normalized(ahead) : Vec3{0, 0, -1};
        // Bobbing a little on the water.
        b.sway += dt * 1.3f;
        b.position[1] += std::sin(b.sway) * (b.window < 1 ? 0.02f : 0.08f);
        const Vec3 x = normalized(cross(ahead, Vec3{0, 1, 0}));
        b.axes = {x, {0, 1, 0}, -ahead};
    }
    for (Train& t : trains_) {
        // Along its line and back, slowing to stand at each end.
        const float room = t.at.back() - t.span();
        if (t.standing > 0) {
            t.standing -= dt;
            if (t.standing <= 0)
                t.way = -t.way;
        } else {
            const float left = t.way > 0 ? room - t.u : t.u;
            const float target = std::clamp(std::sqrt(2 * kTrainAccel * std::max(0.0f, left)), 0.5f, kTrainSpeed);
            t.speed = std::min(target, t.speed + kTrainAccel * dt);
            t.u = std::clamp(t.u + float(t.way) * t.speed * dt, 0.0f, room);
            if (t.way > 0 ? t.u >= room - 0.01f : t.u <= 0.01f) {
                t.speed = 0;
                t.standing = kTrainStand;
            }
        }
        placeTrain(t);
    }
    for (Bridge& b : bridges_) {
        if (b.inactive) {
            b.raised = 0;
        } else {
            const float t = std::fmod(clock_ + b.offset, kDown + kRising + kUp + kFalling);
            b.raised = t < kDown                   ? 0
                       : t < kDown + kRising       ? smooth((t - kDown) / kRising)
                       : t < kDown + kRising + kUp ? 1
                                                   : 1 - smooth((t - kDown - kRising - kUp) / kFalling);
        }
    }
}

void Gizmos::placeTrain(Train& t) {
    // Each carriage on its two bogies, 1.5 m in from its ends.
    t.carriages.resize(kCarriages);
    const float inset = std::min(1.5f, t.length / 4);
    for (int k = 0; k < kCarriages; ++k) {
        const float a = t.u + float(k) * (t.length + t.gap), b = a + t.length;
        const Vec3 pa = pointAlong(t.points, t.at, a + inset), pb = pointAlong(t.points, t.at, b - inset);
        const Vec3 z = length(pa - pb) > 1e-3f ? normalized(pa - pb) : Vec3{0, 0, 1};
        Vec3 x = cross(Vec3{0, 1, 0}, z);
        x = length(x) > 1e-3f ? normalized(x) : Vec3{1, 0, 0};
        const Vec3 y = cross(z, x);
        Carriage& c = t.carriages[size_t(k)];
        c.axes = {x, y, z};
        c.center = (pa + pb) * 0.5f + y * t.lift;
    }
}

Vec3 Gizmos::Train::velocity() const {
    if (carriages.empty())
        return {};
    return carriages.front().axes.z * (-float(way) * speed);
}

std::vector<Vec3> Gizmos::Train::spheres(float radius) const {
    std::vector<Vec3> out;
    for (const Carriage& c : carriages) {
        const Vec3 base = c.center - c.axes.y * lift;
        for (float s = -length / 2 + radius; s <= length / 2 - radius + 0.01f; s += radius)
            out.push_back(base + c.axes.z * s + c.axes.y * radius);
    }
    return out;
}

float Gizmos::collide(CarSim& car) const {
    float hardest = 0, radius;
    for (const Train& t : trains_) {
        const Vec3 v = t.velocity();
        for (const Carriage& c : t.carriages) {
            if (flat(c.center - car.position) > t.length / 2 + 6)
                continue;
            // The car's spheres against the carriage's box.
            for (const Vec3& p : car.bumpers(radius)) {
                const Vec3 d = p - (c.center - c.axes.y * t.lift);
                const float ly = dot(d, c.axes.y);
                if (ly < -1 || ly > t.height + 1)
                    continue;
                const float hw = t.width / 2, hl = t.length / 2;
                const float lx = dot(d, c.axes.x), lz = dot(d, c.axes.z);
                const float cx = std::clamp(lx, -hw, hw), cz = std::clamp(lz, -hl, hl);
                float nx = lx - cx, nz = lz - cz, depth;
                const float dist = std::sqrt(nx * nx + nz * nz);
                if (dist > 1e-4f) {
                    depth = radius - dist;
                    nx /= dist;
                    nz /= dist;
                } else if (hw - std::fabs(lx) < hl - std::fabs(lz)) {
                    nx = lx < 0 ? -1.0f : 1.0f;
                    nz = 0;
                    depth = hw - std::fabs(lx) + radius;
                } else {
                    nx = 0;
                    nz = lz < 0 ? -1.0f : 1.0f;
                    depth = hl - std::fabs(lz) + radius;
                }
                if (depth <= 0)
                    continue;
                Vec3 n = c.axes.x * nx + c.axes.z * nz;
                n[1] = 0;
                if (length(n) < 1e-3f)
                    continue;
                n = normalized(n);
                car.position += n * depth;
                const float closing = dot(car.velocity - v, n);
                if (closing < 0) {
                    car.velocity -= n * (1.3f * closing);
                    hardest = std::max(hardest, -1.3f * closing);
                }
            }
        }
    }
    return hardest;
}

void Gizmos::push(Traffic& traffic) const {
    for (const Train& t : trains_) {
        if (t.carriages.empty())
            continue;
        Vec3 position = t.carriages[kCarriages / 2].center, velocity = t.velocity();
        traffic.collide(t.spheres(t.width / 2), t.width / 2, 1e9f, position, velocity);
    }
}

void Gizmos::draw(gfx::Renderer3D& renderer, ModelCache& models, const SceneLight& light, const gfx::Mat4& viewProj,
                  const Vec3& eye) const {
    const ViewFrustum frustum(viewProj);
    auto show = [&](const std::string& name, const Mat3& axes, const Vec3& at, bool translucent) {
        const ModelCache::Model& m = models.get(name);
        if (!m.ok)
            return;
        const Vec3 c = at + axes * m.center;
        const float d = length(c - eye);
        if (d - m.radius > 1200 || !frustum.visible(c, m.radius))
            return;
        const size_t level = std::min(m.levels.size() - 1, size_t(d < 80 ? 0 : d < 250 ? 1 : 2));
        drawModelMesh(renderer, m.levels[level], matrixOf(axes, at), light, translucent);
    };
    for (bool translucent : {false, true}) {
        for (const Boat& b : boats_)
            show(b.model, b.axes, b.position, translucent);
        for (const Bridge& b : bridges_)
            for (const Leaf& l : b.leaves) {
                Vec3 center;
                const Mat3 axes = leafAxes(b, l, center);
                show(b.model, axes, center, translucent);
            }
        for (const Train& t : trains_)
            for (const Carriage& c : t.carriages)
                show(t.model, c.axes, c.center, translucent);
    }
}

bool Gizmos::inGap(const Vec3& a, const Vec3& b, const Vec3& c) const {
    const Vec3 m = (a + b + c) * (1.0f / 3);
    for (const Bridge& br : bridges_) {
        if (br.standing)
            continue;
        for (const Leaf& l : br.leaves) {
            Vec3 along = l.tip - l.hinge;
            along[1] = 0;
            const float len = flat(along);
            if (len < 1e-3f)
                continue;
            along = along * (1 / len);
            const Vec3 rel = m - l.hinge;
            const float t = rel[0] * along[0] + rel[2] * along[2];
            const float side = std::fabs(rel[0] * along[2] - rel[2] * along[0]);
            if (t > 0.5f && t < len - 0.5f && side < l.width && std::fabs(m[1] - l.hinge[1]) < 1.5f)
                return true;
        }
    }
    return false;
}

void Gizmos::users(std::vector<RoadUser>& out) const {
    for (const Bridge& b : bridges_) {
        if (b.standing || b.raised < 0.01f)
            continue;
        for (const Leaf& l : b.leaves) {
            Vec3 along = l.tip - l.hinge;
            along[1] = 0;
            const Vec3 across = normalized(cross(Vec3{0, 1, 0}, along));
            for (float k = -l.width; k <= l.width; k += 2)
                out.push_back({l.hinge + across * k, {}, true});
        }
    }
    for (const Train& t : trains_)
        for (const Carriage& c : t.carriages)
            for (float s = -t.length / 2; s <= t.length / 2; s += 5)
                out.push_back({c.center + c.axes.z * s, t.velocity(), true});
}

} // namespace reluva::age
