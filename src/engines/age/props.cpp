// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/props.h"

#include "engines/age/bound.h"
#include "engines/age/car_actor.h"
#include "engines/age/tune.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace reluva::age {

namespace {

constexpr float kNeverBreaks = 1e20f; // ImpulseLimit2 this large: never (the trees' 1e30)
constexpr float kGravity = 9.8f;
constexpr float kSlideFriction = 6; // m/s/s a loose prop slows by
constexpr float kDrawDistance = 300;

Vec3 flat(Vec3 v) {
    v[1] = 0;
    return length(v) > 1e-4f ? normalized(v) : Vec3{1, 0, 0};
}

} // namespace

std::vector<PropPlacement> placeProps(const std::vector<PathsetPath>& paths) {
    std::vector<PropPlacement> out;
    for (const PathsetPath& p : paths) {
        if (p.model.empty() || p.points.empty())
            continue;
        const uint32_t kind = p.end & 0xff;
        if (kind == 1) {
            // Pairs: where, then a point it faces.
            for (size_t i = 0; i + 1 < p.points.size(); i += 2)
                out.push_back({p.model, p.points[i], flat(p.points[i + 1] - p.points[i])});
        } else if (kind == 2 && p.points.size() >= 2) {
            // Along the line, every so many metres.
            const float spacing = std::max(0.5f, float(p.end >> 8 & 0xff) / 10);
            float carry = 0;
            for (size_t i = 0; i + 1 < p.points.size(); ++i) {
                const Vec3 a = p.points[i], b = p.points[i + 1];
                const float len = length(b - a);
                if (len < 1e-3f)
                    continue;
                const Vec3 dir = (b - a) * (1 / len);
                for (float t = carry; t <= len; t += spacing)
                    out.push_back({p.model, a + dir * t, flat(dir)});
                carry = std::fmod(len - carry, spacing);
                carry = carry > 1e-3f ? spacing - carry : 0;
            }
        } else {
            for (const Vec3& at : p.points)
                out.push_back({p.model, at, {1, 0, 0}});
        }
    }
    return out;
}

Error parseBangerData(const std::string& text, BangerData& out) {
    out = BangerData{};
    TuneNode root;
    if (Error err = parseTune(text, root))
        return err;
    const TuneNode* n = root.child("dgBangerData");
    if (!n)
        return Error::fail("no dgBangerData block");
    out.mass = std::max(0.1f, n->number("Mass", 0, out.mass));
    out.impulseLimit2 = n->number("ImpulseLimit2");
    return Error::success();
}

void Props::load(const SearchSet& files, const std::string& map) {
    props_.clear();
    prepared_ = false;
    std::vector<uint8_t> data;
    std::vector<PathsetPath> paths;
    if (!files.read("city/" + map + "/props.pathset", data))
        readPathset(data, paths);
    for (const PropPlacement& p : placeProps(paths)) {
        Prop prop;
        prop.model = p.model;
        prop.at = p.at;
        const Vec3 x = p.along, y{0, 1, 0};
        prop.axes = {x, y, cross(x, y)};
        props_.push_back(prop);
    }
}

void Props::addLights(const TrafficLights& lights) {
    for (size_t i = 0; i < lights.count(); ++i) {
        const TrafficLights::Placement pl = lights.placement(i);
        Prop prop;
        prop.model = pl.model;
        prop.at = pl.at;
        prop.axes = pl.axes;
        prop.light = int(i);
        props_.push_back(prop);
    }
    prepared_ = false;
}

void Props::prepare(const SearchSet& files, ModelCache& models) {
    prepare(files, [&models](const std::string& name, Vec3& low, Vec3& high) {
        const ModelCache::Model& m = models.get(name);
        if (!m.ok || m.levels.empty())
            return false;
        low = m.levels.front().low;
        high = m.levels.front().high;
        return true;
    });
}

void Props::prepare(const SearchSet& files, const ModelBox& box) {
    std::map<std::string, std::pair<BangerData, bool>> banger;
    for (size_t i = 0; i < props_.size(); ++i) {
        Prop& p = props_[i];
        Vec3 low, high;
        if (!box(p.model, low, high))
            continue;
        p.lift = -low[1];
        p.height = std::max(0.2f, high[1] - low[1]);
        auto it = banger.find(p.model);
        if (it == banger.end()) {
            std::vector<uint8_t> data;
            BangerData b;
            const bool has = !files.read("tune/banger/" + p.model + ".dgBangerData", data) &&
                             !parseBangerData(std::string(data.begin(), data.end()), b);
            it = banger.emplace(p.model, std::make_pair(b, has)).first;
        }
        const auto& [b, has] = it->second;
        p.surface = CollisionSurface{};
        p.surface.prop = int32_t(i);
        p.surface.propMass = b.mass;
        p.surface.breakImpulse =
            has && b.impulseLimit2 > 0 && b.impulseLimit2 < kNeverBreaks ? std::sqrt(b.impulseLimit2) : 0;
        // Its bound, else its model's box (a traffic light: its pole).
        auto place = [&](const Vec3& v) { return p.at + p.axes * (v + Vec3{0, p.lift, 0}); };
        p.triangles.clear();
        Bound bound;
        if (p.light < 0 && !loadBound(files, p.model, bound)) {
            for (const BoundPolygon& poly : bound.polygons) {
                p.triangles.push_back({place(bound.vertices[poly.vertices[0]]), place(bound.vertices[poly.vertices[1]]),
                                       place(bound.vertices[poly.vertices[2]])});
                if (poly.count == 4)
                    p.triangles.push_back({place(bound.vertices[poly.vertices[0]]),
                                           place(bound.vertices[poly.vertices[2]]),
                                           place(bound.vertices[poly.vertices[3]])});
            }
        } else {
            Vec3 lo = low, hi = high;
            if (p.light >= 0) { // the pole at its foot, not the arm
                lo[0] = lo[2] = -0.2f;
                hi[0] = hi[2] = 0.2f;
            }
            const Vec3 c[8] = {{lo[0], lo[1], lo[2]}, {hi[0], lo[1], lo[2]}, {hi[0], lo[1], hi[2]},
                               {lo[0], lo[1], hi[2]}, {lo[0], hi[1], lo[2]}, {hi[0], hi[1], lo[2]},
                               {hi[0], hi[1], hi[2]}, {lo[0], hi[1], hi[2]}};
            const int faces[6][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4},
                                     {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
            for (const auto& f : faces) {
                p.triangles.push_back({place(c[f[0]]), place(c[f[1]]), place(c[f[2]])});
                p.triangles.push_back({place(c[f[0]]), place(c[f[2]]), place(c[f[3]])});
            }
        }
    }
    prepared_ = true;
}

void Props::collide(CollisionWorld& world) const {
    world.removeSurfaces([](const CollisionSurface& s) { return s.prop >= 0; });
    for (const Prop& p : props_)
        if (!p.loose)
            for (const auto& t : p.triangles)
                world.add(t[0], t[1], t[2], p.surface);
    world.build(world.cellSize());
}

void Props::update(float dt, const std::vector<CarSim*>& cars, CollisionWorld& world, TrafficLights& lights) {
    bool changed = false;
    for (CarSim* car : cars) {
        for (int32_t id : car->broke) {
            if (id < 0 || size_t(id) >= props_.size() || props_[size_t(id)].loose)
                continue;
            Prop& p = props_[size_t(id)];
            p.loose = true;
            changed = true;
            if (p.light >= 0)
                lights.knockDown(size_t(p.light));
            // Pushed the way the car goes: tall ones fall over that way
            // (about their foot), and all slide.
            const Vec3 push = flat(car->velocity);
            const float speed = length(car->velocity);
            p.axis = normalized(cross(Vec3{0, 1, 0}, push));
            p.fallTo = p.height > 1.2f ? 1.5707963f : 0;
            p.spin = std::min(4.0f, speed / std::max(1.0f, p.height));
            p.slide = push * (speed * car->setup().mass / (car->setup().mass + p.surface.propMass));
        }
        car->broke.clear();
    }
    if (changed)
        world.removeSurfaces([&](const CollisionSurface& s) {
            return s.prop >= 0 && size_t(s.prop) < props_.size() && props_[size_t(s.prop)].loose;
        });
    for (Prop& p : props_) {
        if (!p.loose)
            continue;
        if (p.angle < p.fallTo) {
            // A rod falling about its foot.
            p.spin += 1.5f * kGravity / p.height * std::sin(std::max(p.angle, 0.05f)) * dt;
            p.angle = std::min(p.fallTo, p.angle + p.spin * dt);
        }
        const float v = length(p.slide);
        if (v > 1e-3f) {
            p.moved += p.slide * dt;
            p.slide = p.slide * (std::max(0.0f, v - kSlideFriction * dt) / v);
        }
    }
}

void Props::draw(gfx::Renderer3D& renderer, ModelCache& models, const SceneLight& light, const gfx::Mat4& viewProj,
                 const Vec3& eye) const {
    const ViewFrustum frustum(viewProj);
    for (bool translucent : {false, true})
        for (const Prop& p : props_) {
            if (p.light >= 0 && !p.loose)
                continue; // TrafficLights draws those standing
            const Vec3 at = p.at + p.moved;
            if (length(at - eye) > kDrawDistance || !frustum.visible(at + Vec3{0, p.height / 2, 0}, p.height + 2))
                continue;
            const ModelCache::Model& m = models.get(p.model);
            if (!m.ok)
                continue;
            const Mat3 turn = p.loose ? Mat3::rotation(p.axis, p.angle) : Mat3{};
            drawModelMesh(renderer, m.levels.front(), matrixOf(turn * p.axes, at + turn * Vec3{0, p.lift, 0}), light,
                          translucent);
        }
}

size_t Props::loose() const {
    return size_t(std::count_if(props_.begin(), props_.end(), [](const Prop& p) { return p.loose; }));
}

} // namespace reluva::age
