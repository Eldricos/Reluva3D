// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/nfs/nfs2se/formats/geo.h"
#include "engines/nfs/nfs2se/formats/paint.h"
#include "engines/nfs/nfs2se/pages/quad_mesh.h"
#include "engines/nfs/pages/viewers.h"

#include <string>
#include <vector>

namespace reluva::nfs::nfs2se {

/**
 * NFS II SE's models (cardata.viv's .geo) in 3D, with their pictures from
 * the car's .qfs, a level of detail at a time.
 *   Left/Right: model   drag or A/D/W/S: turn   wheel or +/-: zoom
 *   L: level (high, medium, low, all)   T: textures   Space: spin   R: reset
 *   P: paint (carhsvt.txt's colours, or none)
 * Polygons with frames (geo.md: the wheels' faces, the tail lights) take
 * their pictures in turn.
 * RELUVA_NFS2SE_CARS_VIEW="model yaw pitch level" opens it so, for
 * screenshots that repeat.
 */
class CarPage : public Page {
public:
    /** Shows the models of `files`, which must outlive the page. */
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
    void build();
    void release();

    const GameFiles &_files;
    std::vector<FileRef> _models;
    int _index = 0;
    gfx::Renderer3D *_renderer = nullptr;
    std::string _unavailable;
    Surface _overlay;
    CarModel _model;
    std::string _error;
    std::string _skinName;
    std::vector<std::string> _pictureNames; // by the textures' numbers
    std::vector<gfx::TextureId> _textures;
    // A polygon's frames' textures, for the textures their first is: the
    // file's picture, then its name with the frame's number in its second
    // character.
    std::vector<std::vector<int>> _frames;
    std::vector<PaintColour> _paints; // carhsvt.txt's
    int _paint = -1;                  // into _paints; -1 the pictures as they are
    int _missing = 0;                 // polygons whose picture the .qfs hasn't
    gfx::MeshId _mesh = 0;
    std::vector<QuadMesh::Run> _runs;
    size_t _quads = 0;
    int _level = 0; // 0 high, 1 medium, 2 low, 3 all
    bool _textured = true;
    bool _spin = false;
    bool _dragging = false;
    float _yaw = 0.6f;
    float _pitch = 0.3f;
    float _distance = 7;
    int _lastX = 0;
    int _lastY = 0;
    uint64_t _lastMs = 0;
};

} // namespace reluva::nfs::nfs2se
