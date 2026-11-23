// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/sith/formats/colormap.h"
#include "engines/sith/formats/material.h"
#include "engines/sith/formats/model.h"
#include "engines/sith/game_files.h"
#include "graphics/soft3d/view3d.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Drawing the game's 3D data in software, for the data viewer: models and
// levels through a colormap, as the game's 8-bit renderer would light them
// (docs/engines/sith/games/jk/formats/3do.md).

namespace reluva::sith {

/** A rotation and a translation: where a node, or a thing, is. */
struct Transform {
    /** Rows of the rotation, then the translation. */
    std::array<std::array<float, 3>, 3> r{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    Vec3 t;

    /** The transform at `position`, turned by pitch, yaw and roll in degrees. */
    static Transform place(const Vec3 &position, const Vec3 &pitchYawRoll);
    /** This, then `inner` within it. */
    Transform operator*(const Transform &inner) const;
    /** A point moved by it. */
    Vec3 apply(const Vec3 &p) const;
    /** A direction turned by it. */
    Vec3 turn(const Vec3 &d) const;
};

/** The materials of the archives, read once each, by name. */
class MaterialCache {
public:
    /** A cache of `files`' materials, which must outlive it. */
    explicit MaterialCache(const GameFiles &files) : _files(files) {}
    /** A material from mat/ or 3do/mat/, or nullptr if it's in neither or can't be read. */
    const Material *find(const std::string &name);

private:
    const GameFiles &_files;
    std::map<std::string, std::unique_ptr<Material>> _cache;
};

/** One corner of a polygon to draw. */
struct Corner {
    Vec3 at;
    /** Texture coordinates, in texels. */
    float u = 0;
    float v = 0;
    /** Light, 0 (dark) to 1 (the colormap's brightest). */
    float light = 1;
};

/** What a polygon is drawn with: a cel of a material, or a plain colour. */
struct Paint {
    const Material *material = nullptr;
    int cel = 0;
    /** The colour when there's no texture: a palette index. */
    int color = -1;
    /** Whether colour 0 of a transparent texture is left out. */
    bool transparent = true;
    /** The colormap to light it through, in place of the Draw3D's own; nullptr for that. */
    const Colormap *colormap = nullptr;
};

/** Draws into a surface through a colormap: a rasterizer, a camera and the colours. */
class Draw3D {
public:
    /** Starts drawing into `target` from `camera`; all three must outlive it. */
    Draw3D(Surface &target, const soft3d::OrbitCamera &camera, const Colormap &colormap);

    /** A polygon, its corners in order around it. */
    void polygon(const std::vector<Corner> &corners, const Paint &paint);

    /**
     * A rectangle facing the camera, `width` by `height` about `centre`,
     * with its texture across it once.
     */
    void billboard(const Vec3 &centre, float width, float height, const Paint &paint, float light = 1);

    /** How many polygons were drawn, and how many were left out (behind the camera, no paint). */
    ///@{
    int drawn() const { return _drawn; }
    int skipped() const { return _skipped; }
    ///@}

private:
    soft3d::Rasterizer _raster;
    const soft3d::OrbitCamera &_camera;
    const Colormap &_colormap;
    int _drawn = 0;
    int _skipped = 0;
};

/**
 * Where each of a model's nodes is, in the model's space: its parents'
 * transforms, then its own position and rotation.
 * @param pose Each node's position and pitch, yaw and roll, to use in place
 *        of the model's own (an animation's frame); empty for the model's.
 */
std::vector<Transform> poseNodes(const Model &model, const std::vector<std::array<Vec3, 2>> &pose = {});

/**
 * Draws a model's first geometry set, placed by `where`, its nodes by
 * `nodes` (poseNodes), lit from above and in front.
 * @param cel Which cel of each material: an animated material's frame.
 */
void drawModel(Draw3D &draw, MaterialCache &materials, const Model &model, const std::vector<Transform> &nodes,
               const Transform &where, int cel = 0);

} // namespace reluva::sith
