// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/gfx3d.h"
#include "engines/age/car.h"
#include "engines/age/render_cache.h"

#include <array>
#include <string>
#include <vector>

namespace reluva::age {

// A car in a scene: its simulation and its model, the wheels drawn where
// the simulation has them. Shared by the pages that drive cars.
class CarActor {
public:
    CarActor() = default;
    CarActor(const CarActor&) = delete;
    CarActor& operator=(const CarActor&) = delete;
    ~CarActor() { unload(); }

    // geometry/<car>.pkg and its simulation's files (loadCarSetup).
    Error load(gfx::Renderer3D& renderer, TextureCache& textures, const SearchSet& files, const std::string& car,
               uint32_t paintJob);
    void unload();
    void draw(const SceneLight& light) const;

    CarSetup setup;
    CarSim sim;
    uint32_t paintJobs = 1; // how many the model has
    // A police car's siren lights (its SRN parts' glows): 0 off, else 1 or
    // 2 for which pair is lit.
    int siren = 0;

private:
    gfx::Renderer3D* renderer_ = nullptr;
    ModelMesh body_;
    std::array<ModelMesh, 4> wheels_;
    std::array<ModelMesh, 2> sirens_; // SRN0 and SRN2, SRN1 and SRN3
};

// A car's cameras (tune/camera): near and far following it, and in it.
struct CarCamera {
    bool inside = false; // in the car (camPovCS), else following it (camTrackCS)
    Vec3 offset{0, 1, 4};
    Vec3 lookAt{0, 1.5f, 0};
    float minDistance = 3, maxDistance = 6;
    float fov = 70; // degrees
};
std::vector<CarCamera> loadCarCameras(const SearchSet& files, const std::string& car);

// Where a camera puts the eye for a car this frame; `follow` keeps a
// following camera's position between frames (`reset` jumps it there).
struct CameraView {
    Vec3 eye{}, target{}, up{0, 1, 0};
};
CameraView updateCarCamera(const CarCamera& cam, const CarSim& sim, float dt, Vec3& follow, bool reset);

// The 4x4 of a rotation and a position.
gfx::Mat4 matrixOf(const Mat3& r, const Vec3& p);

} // namespace reluva::age
