// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "graphics/gfx3d.h"
#include "engines/age/game/car.h"
#include "engines/age/render/render_cache.h"

#include <array>
#include <string>
#include <vector>

namespace reluva::age {

/**
 * A car in a scene: its simulation and its model, the wheels drawn where
 * the simulation has them. Shared by the pages that drive cars.
 */
class CarActor {
public:
    CarActor() = default;
    CarActor(const CarActor &) = delete;
    CarActor &operator=(const CarActor &) = delete;
    ~CarActor() { unload(); }

    /** geometry/<car>.pkg and its simulation's files (loadCarSetup). */
    Error load(gfx::Renderer3D &renderer, TextureCache &textures, const SearchSet &files, const std::string &car,
               uint32_t paintJob);
    /** Frees the model's meshes from the renderer; nothing is drawn after. */
    void unload();
    /**
     * Draws the body (with its reflections, if g_reflections), the wheels
     * where the simulation has them, and the lit sirens and lamps.
     */
    void draw(const SceneLight &light) const;
    // Its reflection map (texture/refl_dc.tga, set at load; the garage's
    // refl_showroom), and whether cars show reflections (the Graphics
    // Options' Vehicle Reflections).
    gfx::TextureId reflectionMap = 0;
    static inline bool g_reflections = true;

    CarSetup setup;
    CarSim sim;
    uint32_t paintJobs = 1; // how many the model has
    // A police car's siren lights (its light bar's halves, SIREN0 and
    // SIREN1, and flares at its SRN squares): 0 off, else 1 or 2 for which
    // half is lit.
    int siren = 0;
    /**
     * Its lamps: headlights and tail lights, brake lights, reversing
     * lights (HLIGHT, TLIGHT, BLIGHT, RLIGHT).
     */
    struct Lamps {
        bool head = false;
        bool brake = false;
        bool reverse = false;
    } lamps;
    /** Whether the model has a light bar (SIREN0) or SRN squares: a police car. */
    bool hasSiren() const { return _sirens[0].mesh || !_sirenLamps.empty(); }
    /**
     * Flares at its headlights (its HEADLIGHT squares) when they're on, and
     * at its sirens, seen from `eye`.
     */
    void drawFlares(const FlareDrawer &flares, const Vec3 &eye) const;
    /** Where its headlights light the road ahead, when they're on. */
    bool headlightPool(gfx::DynamicLight &out) const;
    /**
     * Damage: the body's parts within `radius` of a point (in the car's
     * frame) take their damaged textures (<texture>_dmg), where they have
     * them; mend() puts them back.
     */
    void dent(const Vec3 &at, float radius);
    /** Puts back every texture dent() damaged. */
    void mend();

private:
    gfx::Renderer3D *_renderer = nullptr;
    ModelMesh _body;
    // each body draw's textures (in _damaged, 0: none)
    std::vector<gfx::TextureId> _clean;
    std::vector<gfx::TextureId> _damaged;
    std::array<ModelMesh, 4> _wheels;
    std::array<ModelMesh, 2> _sirens; // SIREN0, SIREN1
    ModelMesh _head;
    ModelMesh _tail;
    ModelMesh _brake;
    ModelMesh _reverse;
    std::vector<Vec3> _headlights;                                  // HEADLIGHT0, 1 squares
    std::vector<std::pair<Vec3, std::array<float, 3>>> _sirenLamps; // SRN squares: where, colour
};

/** A car's cameras (tune/camera): near and far following it, and in it. */
struct CarCamera {
    bool inside = false; // in the car (camPovCS), else following it (camTrackCS)
    Vec3 offset{0, 1, 4};
    Vec3 lookAt{0, 1.5f, 0};
    float minDistance = 3;
    float maxDistance = 6;
    float fov = 70; // degrees
    // How fast a following one catches the car up (a share of the way a
    // second): across, from MinAppXZPos at MinSpeed to MaxAppXZPos at
    // MaxSpeed (m/s), the rate rising by at most AppInc a second and
    // falling by AppDec; up and down, AppYPos.
    float approachSlow = 6;
    float approachFast = 6;
    float slowSpeed = 0;
    float fastSpeed = 35;
    float approachRise = 1e9f;
    float approachFall = 1e9f;
    float approachUp = 6;
    // Drifting (steering past MinHardSteer for DriftDelay seconds), the
    // rate is FrontRate of itself, letting the car swing out.
    float hardSteer = 0.8f;
    float driftDelay = 0.3f;
    float driftRate = 1;
    // Up and down hills it tilts with the car, the slope's sine kept
    // within HillMin..HillMax, easing HillLerp of the way a frame (at 30).
    float hillMin = 0;
    float hillMax = 0;
    float hillLerp = 0;
    // Backing for RevDelay seconds (ReverseOn), it swings round to look
    // the way the car goes, at RevOnApp's rate, and back at RevOffApp's.
    bool reverse = false;
    float reverseDelay = 2;
    float reverseOn = 2;
    float reverseOff = 4;
    // Backing (not yet swung round), the rate is RearRate of itself.
    float rearRate = 1;
    // Its bearing round the car turns at most AppRot degrees a second (not
    // swinging round); moves under AppPosMin metres and AppRotMin radians
    // are left alone; it looks LookAbove metres over TrackTo.
    float turnRate = 1e9f;
    float stillMove = 0;
    float stillTurn = 0;
    float lookAbove = 0;
    // SteerOn: turned with the steering past SteerMin, SteerAmt degrees at
    // full lock (off for every car).
    bool steerOn = false;
    float steerMin = 0.5f;
    float steerAmount = 0;
    // CameraNear: the near clip, metres; BlendTime: seconds a change to
    // this camera blends over.
    float nearClip = 0.1f;
    float blendTime = 0;
};
/**
 * The car's cameras from tune/camera/<car>_near.camTrackCS, _far.camTrackCS
 * and .camPovCS, in that order, leaving out those that don't read.
 */
std::vector<CarCamera> loadCarCameras(const SearchSet &files, const std::string &car);

/** A view: where the eye is, what it looks at and which way is up. */
struct CameraView {
    Vec3 eye{};
    Vec3 target{};
    Vec3 up{0, 1, 0};
};
/** What a following camera keeps from frame to frame. */
struct CameraFollow {
    Vec3 position{};
    float rate = 0;       // its approach rate now
    float hardFor = 0;    // seconds steered hard
    float backingFor = 0; // seconds going backwards
    bool backwards = false;
    bool swinging = false;
    float slope = 0; // the hill's sine it's tilted by
};
/**
 * Where a camera puts the eye for a car this frame; `follow` keeps a
 * following camera's position between frames (`reset` jumps it there).
 * An in-car camera neither reads nor changes it.
 */
CameraView updateCarCamera(const CarCamera &cam, const CarSim &sim, float dt, CameraFollow &follow, bool reset);
/**
 * A change of camera blended over the new one's BlendTime: start() with
 * the view before it, then each frame's view through apply().
 */
struct CameraBlend {
    CameraView from;
    float left = 0;
    float total = 0;
    /** Starts a blend from `last`, the view before the change, over `seconds`. */
    void start(const CameraView &last, float seconds) {
        from = last;
        left = total = seconds;
    }
    /** The view `dt` seconds on, eased from the old towards `to`; `to` itself once the blend is over. */
    CameraView apply(const CameraView &to, float dt);
};

/** The 4x4 of a rotation and a position. */
gfx::Mat4 matrixOf(const Mat3 &r, const Vec3 &p);

} // namespace reluva::age
