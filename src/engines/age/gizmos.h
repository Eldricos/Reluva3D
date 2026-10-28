// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/gfx3d.h"
#include "engines/age/collision.h"
#include "engines/age/pathset.h"
#include "engines/age/render_cache.h"
#include "engines/age/traffic.h"

#include <map>
#include <string>
#include <vector>

namespace reluva::age {

// The things that move by themselves along a city's path sets
// (race/<city>/<city>_ferry, _sailboat and _bridge.pathset): ferries,
// tugs, water taxis, sailboards and ducks going round their paths, and
// drawbridges whose leaves rise and fall (and the gates that stand
// still). The way they move is Reluva's; see docs/engines/age-traffic.md.
class Gizmos {
public:
  // Seconds of a drawbridge's cycle: down, rising, up, falling.
  static constexpr float kDown = 50, kRising = 8, kUp = 20, kFalling = 8;
  static constexpr float kRaised = 0.5f; // radians a leaf rises to

  // The city's files, and a race's (<city>_bridge_<variant>.pathset...)
  // over them where it has its own: a path in both, the same model
  // starting within 5 m, is the race's.
  Error load(const SearchSet &files, const std::string &map,
             const std::string &variant = "");
  void clear();

  // Moves them, and puts the leaves' road surfaces in `world` (as
  // moving triangles).
  void update(float dt, CollisionWorld &world);
  void draw(gfx::Renderer3D &renderer, ModelCache &models,
            const SceneLight &light, const gfx::Mat4 &viewProj,
            const Vec3 &eye) const;
  // Lines of solid road users across each bridge that isn't down, for
  // the traffic to stop at.
  void users(std::vector<RoadUser> &out) const;

  // A triangle of the city across a drawbridge's gap (its middle under a
  // leaf, near the road): taken out where the leaves are.
  bool inGap(const Vec3 &a, const Vec3 &b, const Vec3 &c) const;

  size_t boats() const { return boats_.size(); }
  size_t bridges() const { return bridges_.size(); }
  // 0 down .. 1 up, for a bridge.
  float raised(size_t bridge) const { return bridges_[bridge].raised; }

private:
  struct Boat {
    std::string model;
    std::vector<Vec3>
        points; // closed (back to the first) unless going back and forth
    std::vector<float> at;
    bool backAndForth = false;
    float speed = 2, s = 0, sway = 0; // sway: where in its bobbing it is
    int way = 1;
    float window = 3; // metres either side its heading is taken over
    Vec3 position{};
    Mat3 axes;
  };
  struct Leaf {
    Vec3 hinge{}, tip{}; // down
    float width = 8;
  };
  struct Bridge {
    std::string model;
    std::vector<Leaf> leaves;
    bool inactive = false,
         standing = false; // standing: modelled about its middle (a gate)
    float lift = 0;        // up off the path for a standing one
    float offset = 0;      // seconds into the cycle at clock 0
    float raised = 0;
  };
  struct Bounds {
    Vec3 low{}, high{};
  };
  Bounds boundsOf(const SearchSet &files, const std::string &model);
  void advance(float dt);
  Mat3 leafAxes(const Bridge &b, const Leaf &l, Vec3 &center) const;

  std::vector<Boat> boats_;
  std::vector<Bridge> bridges_;
  std::map<std::string, Bounds> bounds_;
  float clock_ = 0;
};

} // namespace reluva::age
