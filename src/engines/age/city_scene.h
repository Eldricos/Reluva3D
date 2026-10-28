// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/gfx3d.h"
#include "engines/age/city_mesh.h"
#include "engines/age/instances.h"
#include "engines/age/psdl.h"
#include "engines/age/render_cache.h"

#include <memory>
#include <string>
#include <vector>

namespace reluva::age {

// A city the menus offer: tune/<name>.cinfo's MapName and LocalizedName.
struct CityInfo {
    std::string map;  // the files' stem: "sf"
    std::string name; // "San Francisco"
};

// Every city whose .cinfo and .psdl are there.
std::vector<CityInfo> findCities(const SearchSet& files);

// A city ready to draw: its streets and buildings (PSDL), the models its
// .inst places, its sky dome and one of its 16 lightings. Shared by the
// pages that show a city.
class CityScene {
public:
    CityScene(gfx::Renderer3D& renderer, const SearchSet& files);
    ~CityScene();
    CityScene(const CityScene&) = delete;
    CityScene& operator=(const CityScene&) = delete;

    Error load(const std::string& map);

    // 0-15: time (morning, noon, evening, night) x 4 + weather (clear,
    // cloudy, foggy, rainy), as .lt00-.lt15 and the sky dome's paint jobs.
    void setLighting(int index);
    int lighting() const { return lighting_; }
    std::string lightingName() const;
    const SceneLight& light() const { return light_; }
    // Lamps lit: in the evening and at night, and in fog and rain.
    bool lampsLit() const { return lighting_ / 4 >= 2 || lighting_ % 4 >= 2; }
    bool night() const { return lighting_ / 4 == 3; }

    // Draws the sky round `eye`, the city, then (if `models`) the placed
    // models in view; returns how many models were drawn. Between
    // Renderer3D::beginFrame and endFrame.
    size_t draw(const gfx::Mat4& viewProj, const gfx::Vec3& eye, bool models);
    // Brightens the city's opaque surfaces drawn by draw() with `lights`
    // (the nearest gfx::kMaxDynamicLights to `eye`): headlights on the road.
    void drawDynamicLights(std::vector<gfx::DynamicLight> lights, const gfx::Vec3& eye);

    // What was loaded, for the pages' info and the collision world.
    const std::vector<std::string>& notes() const { return notes_; }
    const Psdl& psdl() const { return psdl_; }
    const CityMesh& mesh() const { return mesh_; }
    const std::vector<Instance>& instances() const { return instances_; }
    size_t placedCount() const { return placed_.size(); }
    TextureCache& textures() { return *textures_; }
    ModelCache& models() { return *models_; }

    // On the road nearest the city's middle, at its height, and the turn
    // (about Y) that faces along it.
    Vec3 start{};
    float startYaw = 0;

private:
    struct Batch {
        uint32_t firstIndex, indexCount;
        gfx::TextureId texture;
        bool translucent;
    };
    struct Placed {
        const ModelCache::Model* model;
        gfx::Mat4 matrix;
        gfx::Vec3 center; // of its bounds, in the world
        float radius;
    };

    void unload();

    gfx::Renderer3D& renderer_;
    const SearchSet& files_;
    std::unique_ptr<TextureCache> textures_;
    std::unique_ptr<ModelCache> models_;
    std::string map_;
    Psdl psdl_;
    CityMesh mesh_;
    std::vector<Instance> instances_;
    std::vector<std::string> notes_;
    gfx::MeshId cityMesh_ = 0;
    std::vector<Batch> batches_;
    std::vector<Placed> placed_;
    std::string sky_;
    ModelMesh skyMesh_;
    int lighting_ = 4;
    SceneLight light_;
};

// A city instance's matrix (three axes and a position) as a 4x4.
gfx::Mat4 instanceMatrix(const std::array<float, 12>& m);

} // namespace reluva::age
