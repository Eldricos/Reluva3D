// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/storm3d/formats/sod.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/pages/page.h"
#include "engines/storm3d/pages/sod_meshes.h"
#include "graphics/gfx3d.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace reluva::storm3d {

/**
 * The models (the `.sod` files in `SOD/`), drawn in 3D with their textures,
 * turning.
 * A model with levels of detail shows one level at a time, the most
 * detailed first.
 *   Left/Right: previous/next model   PageUp/PageDown: 10 at a time   Home/End: first/last
 *   Up/Down: tilt   +/-: nearer/further   Space: turning on/off   L: the next level, less detailed
 */
class ModelPage : public Page {
public:
    /** Lists the models; `files`, `sys` and `screen` must outlive it. */
    ModelPage(System &sys, Surface &screen, const GameFiles &files);
    ~ModelPage() override;

    const char *name() const override { return "Models"; }
    size_t count() const override { return _names.size(); }
    bool uses3D() const override { return true; }
    void activate() override;
    void deactivate() override;
    void handleKey(uint32_t key) override;
    void update(uint64_t millis) override;

    /**
     * A node's transform as a matrix that places it in its parent:
     * the 3 x 3 part applied to a point as a row (p * M), then the
     * translation.
     */
    static gfx::Mat4 toMatrix(const SodModel::Transform &t);
    /**
     * Each node's place in the model, its parents' applied; a node whose
     * parent isn't there is placed as a root.
     */
    static std::vector<gfx::Mat4> worldMatrices(const SodModel &model);
    /**
     * The levels of detail: the numbers of the nodes named `lod<n>`,
     * lowest first; empty if there are none.
     */
    static std::vector<int> detailLevels(const SodModel &model);
    /** Whether a node is shown at a level of detail: not under another level's `lod` node. */
    static bool shownAtLevel(const SodModel &model, size_t node, int level);

private:
    void load();
    void unload();
    void draw();
    void showMessage(const std::string &line);

    System &_sys;
    Surface &_screen;
    const GameFiles &_files;
    std::vector<std::string> _names;
    size_t _current = 0;
    gfx::Renderer3D *_renderer = nullptr;
    Surface _overlay;
    SodMeshes _meshes;
    SodMeshes::Built _built;
    SodModel _model;
    std::string _error;
    std::vector<int> _levels;
    size_t _level = SIZE_MAX; // into _levels; past its end for the most detailed
    gfx::Vec3 _centre{};
    float _radius = 1;
    float _distance = 1;
    float _yaw = 0.6f;
    float _pitch = 0.35f;
    bool _turning = true;
    uint64_t _lastMillis = 0;
};

} // namespace reluva::storm3d
