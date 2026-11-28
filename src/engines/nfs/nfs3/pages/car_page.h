// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/nfs/nfs3/formats/fce.h"
#include "engines/nfs/pages/viewers.h"

namespace reluva::nfs::nfs3 {

/**
 * Every model (cars, traffic, police, the front end's track models) in 3D,
 * with its skin, drawn by its triangles' flags (two-sided, unlit) and with
 * the game's reflection of a track's sky.
 *   Left/Right: model   drag or A/D/W/S: turn   wheel or +/-: zoom
 *   L: detail level   T: textures   C: back faces   E: reflection's sky
 *   Space: spin   R: reset
 */
class CarPage : public Page {
public:
    /** Shows the .fce files of `files`, which must outlive the page. */
    explicit CarPage(const GameFiles &files);
    ~CarPage() override;
    const char *name() const override { return "Cars"; }
    bool empty() const override { return _models.empty(); }
    void activate(System &sys) override;
    void deactivate() override;
    bool uses3D() const override { return _renderer != nullptr; }
    void handleEvent(const Event &ev) override;
    void draw(Surface &screen) override; // when 3D isn't available
    void update(uint64_t nowMs) override;

private:
    void load(int index);
    void release();
    void resetView();

    const GameFiles &_files;
    std::vector<FileRef> _models;
    int _index = 0;
    gfx::Renderer3D *_renderer = nullptr;
    std::string _unavailable; // why there's no 3D
    Surface _overlay;
    Model _model;
    std::string _error;
    std::string _skinName;
    gfx::MeshId _mesh = 0;
    gfx::TextureId _skin = 0;
    // A Render/pc model's pages (its .art), and each part's runs of
    // triangles drawn alike: by page and flags, a range of the mesh's
    // indices each.
    std::vector<gfx::TextureId> _pages;
    struct Run {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        uint32_t page = 0;
        bool twoSided = false;
        bool unlit = false;
        bool strong = false;
    };
    std::vector<std::vector<Run>> _runs;
    // The reflection (0x49db70, 0x43400f): the triangles without
    // kNoReflection again, added over the model with source alpha, their
    // sphere-map coordinates from the normals as the camera sees them,
    // remade each frame. Its runs index _envMesh.
    gfx::MeshId _envMesh = 0;
    std::vector<gfx::Vertex> _envVertices;
    std::vector<std::array<float, 3>> _envNormals; // in the world
    std::vector<std::vector<Run>> _envRuns;
    std::vector<std::pair<std::string, gfx::TextureId>> _skies; // a track's sphere maps: name, texture
    int _sky = 0;                                               // which, or _skies.size() for no reflection
    void loadSkies();
    std::vector<int> _partLevels; // 0 high, 1 medium, 2 small, 3 tiny
    int _level = 0;               // 0-3, or 4 for all
    bool _textures = true;
    bool _cullBack = true;
    bool _spin = false;
    bool _dragging = false;
    float _yaw = 0;
    float _pitch = 0;
    float _distance = 5;
    float _radius = 2;
    int _lastX = 0;
    int _lastY = 0;
    uint64_t _lastMs = 0;
};

} // namespace reluva::nfs::nfs3
