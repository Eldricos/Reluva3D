// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/props.h"

#include "engines/age/bound.h"
#include "engines/age/car_actor.h"
#include "engines/age/tune.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <sstream>

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
        // An unnamed path (PATH01...) places nothing.
        if (p.model.empty() || p.model.rfind("PATH", 0) == 0 || p.points.empty())
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

namespace {

// A .csv's rows, split at commas, the empty fields at the end dropped.
std::vector<std::vector<std::string>> csvRows(const std::string& text) {
    std::vector<std::vector<std::string>> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::vector<std::string> row;
        std::istringstream fields(line);
        std::string f;
        while (std::getline(fields, f, ','))
            row.push_back(f);
        while (!row.empty() && row.back().empty())
            row.pop_back();
        if (!row.empty())
            out.push_back(std::move(row));
    }
    return out;
}

// How far along a line each point is.
std::vector<float> along(const std::vector<Vec3>& line) {
    std::vector<float> out(line.size(), 0);
    for (size_t i = 1; i < line.size(); ++i)
        out[i] = out[i - 1] + length(line[i] - line[i - 1]);
    return out;
}

} // namespace

std::map<std::string, StreetPropKind> parsePropDefs(const std::string& text) {
    std::map<std::string, StreetPropKind> out;
    const auto rows = csvRows(text);
    for (size_t i = 1; i < rows.size(); ++i) { // after the header
        const auto& r = rows[i];
        if (r.size() < 7)
            continue;
        StreetPropKind k;
        k.start = std::strtof(r[1].c_str(), nullptr);
        k.spacing = std::strtof(r[2].c_str(), nullptr);
        k.most = std::atoi(r[3].c_str());
        k.minLerp = std::strtof(r[4].c_str(), nullptr);
        k.maxLerp = std::strtof(r[5].c_str(), nullptr);
        for (size_t f = 6; f < r.size(); ++f)
            if (!r[f].empty())
                k.models.push_back(r[f]);
        out[r[0]] = std::move(k);
    }
    return out;
}

std::map<std::string, std::vector<std::string>> parsePropRules(const std::string& text) {
    std::map<std::string, std::vector<std::string>> out;
    const auto rows = csvRows(text);
    for (size_t i = 1; i < rows.size(); ++i) {
        std::vector<std::string>& kinds = out[rows[i][0]];
        for (size_t f = 1; f < rows[i].size(); ++f)
            if (!rows[i][f].empty()) // London's n11left skips a column
                kinds.push_back(rows[i][f]);
    }
    return out;
}

std::vector<PropPlacement> placeStreetProps(const AiRoadMap& roads, const std::vector<uint8_t>& blockRules,
                                            const std::map<std::string, StreetPropKind>& kinds,
                                            const std::map<std::string, std::vector<std::string>>& rules,
                                            uint32_t seed) {
    std::vector<PropPlacement> out;
    for (const AiRoad& road : roads.roads) {
        int rule = 0;
        for (uint16_t b : road.blocks)
            if (b < blockRules.size() && blockRules[b])
                rule = blockRules[b];
        if (!rule)
            continue;
        std::mt19937 random(seed * 7919u + road.id);
        for (int s = 0; s < 2; ++s) {
            const AiRoadSide& side = road.sides[size_t(s)];
            const size_t n = std::min(side.curb.size(), side.outerEdge.size());
            if (n < 2)
                continue;
            char name[16];
            std::snprintf(name, sizeof name, "n%02d%s", rule, s == 0 ? "left" : "right");
            const auto r = rules.find(name);
            if (r == rules.end())
                continue;
            const std::vector<Vec3> curb(side.curb.begin(), side.curb.begin() + std::ptrdiff_t(n));
            const std::vector<float> at = along(curb);
            for (const std::string& kindName : r->second) {
                const auto k = kinds.find(kindName);
                if (k == kinds.end() || k->second.models.empty())
                    continue;
                const StreetPropKind& kind = k->second;
                const float spacing = std::max(1.0f, kind.spacing);
                int placed = 0;
                size_t i = 0;
                for (float t = kind.start; t <= at.back() && placed < kind.most; t += spacing, ++placed) {
                    while (i + 2 < n && at[i + 1] < t)
                        ++i;
                    const float span = std::max(1e-4f, at[i + 1] - at[i]);
                    const float u = std::clamp((t - at[i]) / span, 0.0f, 1.0f);
                    const Vec3 c = curb[i] + (curb[i + 1] - curb[i]) * u;
                    const Vec3 o = side.outerEdge[i] + (side.outerEdge[i + 1] - side.outerEdge[i]) * u;
                    const float across = std::uniform_real_distribution<float>(
                        kind.minLerp, std::max(kind.minLerp, kind.maxLerp))(random);
                    const size_t model = std::uniform_int_distribution<size_t>(0, kind.models.size() - 1)(random);
                    // Turned with its X axis away from the road: a street
                    // light's arm (along its -X) hangs over it.
                    out.push_back({kind.models[model], c + (o - c) * across, flat(o - c)});
                }
            }
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
    for (size_t k = 0; k < 3; ++k) {
        out.size[k] = n->number("Size", k);
        out.cg[k] = n->number("CG", k);
    }
    out.parts = int(n->number("NumParts"));
    out.texNumber = int(n->number("TexNumber"));
    out.elasticity = n->number("Elasticity", 0, out.elasticity);
    if (const TuneNode* rule = n->child("BirthRule")) {
        out.particles = parseBirthRule(*rule);
        for (size_t k = 0; k < 3; ++k)
            out.particlesAt[k] = rule->number("Position", k);
    }
    // NumGlows of them, the offsets one after another.
    const int glows = std::clamp(int(n->number("NumGlows")), 0, 8);
    for (int g = 0; g < glows; ++g)
        out.glows.push_back(Vec3{n->number("GlowOffset", size_t(3 * g)), n->number("GlowOffset", size_t(3 * g + 1)),
                                 n->number("GlowOffset", size_t(3 * g + 2))});
    return Error::success();
}

void Props::load(const SearchSet& files, const std::string& map, const std::string& variant) {
    props_.clear();
    prepared_ = false;
    std::vector<uint8_t> data;
    std::vector<PathsetPath> paths;
    if (!files.read("city/" + map + "/props.pathset", data))
        readPathset(data, paths);
    // A race's own: its barricades, a lesson's cars and ramps.
    if (!variant.empty() && !files.read("race/" + map + "/" + variant + ".pathset", data)) {
        std::vector<PathsetPath> race;
        readPathset(data, race);
        paths.insert(paths.end(), race.begin(), race.end());
    }
    for (const PropPlacement& p : placeProps(paths)) {
        Prop prop;
        prop.model = p.model;
        prop.at = p.at;
        const Vec3 x = p.along, y{0, 1, 0};
        prop.axes = {x, y, cross(x, y)};
        props_.push_back(prop);
    }
}

void Props::addStreetProps(const SearchSet& files, const std::string& map, const AiRoadMap& roads,
                           const std::vector<uint8_t>& blockRules) {
    std::vector<uint8_t> defs, rules;
    if (files.read("city/" + map + "/propdefs.csv", defs) || files.read("city/" + map + "/proprules.csv", rules))
        return;
    for (const PropPlacement& p :
         placeStreetProps(roads, blockRules, parsePropDefs(std::string(defs.begin(), defs.end())),
                          parsePropRules(std::string(rules.begin(), rules.end())))) {
        Prop prop;
        prop.model = p.model;
        prop.at = p.at;
        const Vec3 x = p.along, y{0, 1, 0};
        prop.axes = {x, y, cross(x, y)};
        props_.push_back(prop);
    }
    prepared_ = false;
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

int Props::addEffect(const BangerData& b, const Vec3& middle) {
    if (!b.bursts())
        return -1;
    effects_.push_back({b.particles, middle + b.particlesAt, "fxpt" + std::to_string(b.texNumber)});
    return int(effects_.size() - 1);
}

void Props::prepare(const SearchSet& files, const ModelBox& box) {
    std::map<std::string, std::pair<BangerData, bool>> banger;
    effects_.clear();
    std::map<std::string, int> effectOf; // a model's or piece's, shared
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
        if (has) {
            auto e = effectOf.find(p.model);
            if (e == effectOf.end())
                e = effectOf.emplace(p.model, addEffect(b, b.cg)).first;
            p.effect = e->second;
            // Its lamps: offsets from its model's middle, which stands
            // `lift` up.
            p.glows.clear();
            for (const Vec3& g : b.glows)
                p.glows.push_back(g + Vec3{0, p.lift, 0});
        }
        // Its pieces: <model>_BREAK01... each with its own banger data.
        p.pieces.clear();
        for (int k = 1; has && k <= std::min(b.parts, 9); ++k) {
            char name[16];
            std::snprintf(name, sizeof name, "BREAK%02d", k);
            std::vector<uint8_t> data;
            BangerData pb;
            if (files.read("tune/banger/" + p.model + "_" + name + ".dgBangerData", data) ||
                parseBangerData(std::string(data.begin(), data.end()), pb))
                continue;
            Prop::Piece piece;
            piece.part = name;
            piece.cg = pb.cg;
            piece.size = pb.size;
            const std::string key = p.model + "_" + name;
            auto e = effectOf.find(key);
            if (e == effectOf.end())
                e = effectOf.emplace(key, addEffect(pb, {})).first;
            piece.effect = e->second;
            // A piece's own lamps (a sign's), from its middle.
            for (const Vec3& g : pb.glows)
                p.glows.push_back(pb.cg + g);
            p.pieces.push_back(piece);
        }
        p.surface = CollisionSurface{};
        p.surface.prop = int32_t(i);
        p.surface.propMass = b.mass;
        // Elasticity relative to the props' usual 0.5 (a traffic light's
        // 1.5, three times as bouncy as the rest); Friction is 0.9 for all,
        // the city's default, so the surface's own stays.
        p.surface.elasticity *= has ? std::clamp(b.elasticity / 0.5f, 0.0f, 4.0f) : 1;
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
            // What flies out: from its middle, and from each piece.
            if (p.effect >= 0) {
                const Effect& e = effects_[size_t(p.effect)];
                bursts_.push_back({e.rule, e.texture, p.at + p.axes * e.at, car->velocity * 0.3f});
            }
            // Come apart, where it has pieces: each from where it was, off
            // the way it was pushed, turning, a little up.
            if (!p.pieces.empty()) {
                p.apart = true;
                const Vec3 push = flat(car->velocity);
                const float speed = length(car->velocity);
                const Vec3 axis = normalized(cross(Vec3{0, 1, 0}, push));
                for (size_t k = 0; k < p.pieces.size(); ++k) {
                    Prop::Piece& piece = p.pieces[k];
                    piece.position = p.at + p.axes * piece.cg;
                    piece.orientation = p.axes;
                    const float share = 0.5f + 0.3f * float(k % 3) / 2; // not all alike
                    piece.velocity = push * (speed * share) + Vec3{0, 1.5f + float(k % 2), 0};
                    piece.spin = axis * (2 + float(k)) + Vec3{0, 0.5f * float(k % 2 ? 1 : -1), 0};
                    piece.resting = false;
                    if (piece.effect >= 0) {
                        const Effect& e = effects_[size_t(piece.effect)];
                        bursts_.push_back({e.rule, e.texture, piece.position + p.axes * e.at, piece.velocity * 0.5f});
                    }
                }
            }
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
        for (Prop::Piece& piece : p.pieces) {
            if (!p.apart || piece.resting)
                continue;
            // Falling and turning; on the ground (its half height above
            // it), it bounces a little and slows, and rests once slow.
            piece.velocity[1] -= kGravity * dt;
            piece.position += piece.velocity * dt;
            const float turn = length(piece.spin) * dt;
            if (turn > 1e-6f) {
                piece.orientation = Mat3::rotation(normalized(piece.spin), turn) * piece.orientation;
                piece.orientation.orthonormalize();
            }
            const float half = std::max(0.05f, std::min({piece.size[0], piece.size[1], piece.size[2]}) / 2);
            CollisionWorld::RayHit ground;
            if (world.raycast(piece.position + Vec3{0, 2, 0}, {0, -1, 0}, 6, ground) &&
                piece.position[1] - half < piece.position[1] + 2 - ground.distance) {
                piece.position[1] = piece.position[1] + 2 - ground.distance + half;
                if (piece.velocity[1] < 0)
                    piece.velocity[1] *= -0.3f;
                piece.velocity[0] *= 1 - std::min(1.0f, 4 * dt);
                piece.velocity[2] *= 1 - std::min(1.0f, 4 * dt);
                piece.spin = piece.spin * (1 - std::min(1.0f, 5 * dt));
                if (length(piece.velocity) < 0.3f && length(piece.spin) < 0.3f)
                    piece.resting = true;
            }
        }
        if (p.apart)
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
            if (p.apart) {
                for (const Prop::Piece& piece : p.pieces)
                    drawModelMesh(renderer, models.part(p.model, piece.part),
                                  matrixOf(piece.orientation, piece.position), light, translucent);
                continue;
            }
            const ModelCache::Model& m = models.get(p.model);
            if (!m.ok)
                continue;
            const Mat3 turn = p.loose ? Mat3::rotation(p.axis, p.angle) : Mat3{};
            drawModelMesh(renderer, m.levels.front(), matrixOf(turn * p.axes, at + turn * Vec3{0, p.lift, 0}), light,
                          translucent);
        }
}

size_t Props::pieces(size_t* resting, float* lowest) const {
    size_t n = 0, still = 0;
    float low = 1e30f;
    for (const Prop& p : props_)
        if (p.apart)
            for (const Prop::Piece& piece : p.pieces) {
                ++n;
                still += piece.resting;
                low = std::min(low, piece.position[1]);
            }
    if (resting)
        *resting = still;
    if (lowest)
        *lowest = low;
    return n;
}

size_t Props::loose() const {
    return size_t(std::count_if(props_.begin(), props_.end(), [](const Prop& p) { return p.loose; }));
}

void Props::drawGlows(const FlareDrawer& flares, const Vec3& eye) const {
    // A street lamp's light: warm, the size of the city's beacons'
    // smaller ones.
    constexpr float kSize = 2, kDistance = 600;
    constexpr std::array<float, 3> kColor{1, 0.9f, 0.7f};
    for (const Prop& p : props_) {
        if (p.loose)
            continue;
        for (const Vec3& g : p.glows) {
            const Vec3 at = p.at + p.axes * g;
            if (length(at - eye) < kDistance)
                flares.draw(at, kSize, kColor, eye);
        }
    }
}

} // namespace reluva::age
