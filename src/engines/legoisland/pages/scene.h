// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "engines/legoisland/formats/wdb.h"
#include "graphics/gfx3d.h"

#include <cstddef>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Drawing WORLD.WDB's and the SI files' models in the data viewer's 3D
// pages (docs/engines/legoisland/formats/wdb.md, obj.md).

namespace reluva::legoisland {

/** The texture lists a model's meshes find their textures in, the first first. */
using TextureLists = std::vector<const std::vector<WdbTexture> *>;

/** Where an animation puts its nodes at a time: each one's place, by its name in lower case, and whether it's shown. */
struct NodePlaces {
    std::map<std::string, gfx::Mat4> places;
    std::map<std::string, bool> shown;

    /** A node's place, or null if there's none of that name (in any case). */
    const gfx::Mat4 *find(const std::string &name) const;
    /** Whether a node is shown: true unless its last showing key so far says not. */
    bool isShown(const std::string &name) const;
};

/** A list of keys' value at `timeMs`, as placeNodes() finds it; a turn's the way round a sphere. */
std::array<float, 4> keyValue(const std::vector<WdbKey> &keys, uint32_t timeMs, bool turn = false);

/** The node of that name (in any case) in an animation's tree, or null. */
const WdbAnimNode *findNode(const WdbAnimNode &root, const std::string &name);

/**
 * Places an animation's nodes at `timeMs`: each its move, turn and scaling
 * then (between two keys, the way between them), after its parent's.
 */
void placeNodes(const WdbAnimNode &root, uint32_t timeMs, NodePlaces &out);

/** WORLD.WDB's models and parts, found by name in any case, its parts read once. */
class WdbLibrary {
public:
    /** Reads every part of `wdb`, which must outlive it. */
    explicit WdbLibrary(const WdbFile &wdb);

    /** The file it reads from. */
    const WdbFile &file() const { return _wdb; }
    /** A world's part by name, or null. */
    const WdbPart *part(const std::string &name) const;
    /** One of the shared part's ROIs (a body, a head, a hat) by name, or null. */
    const WdbRoi *sharedRoi(const std::string &name) const;
    /** A model by name: true if there's one, read into `out`. */
    bool model(const std::string &name, WdbModel &out) const;
    /**
     * An ROI's LODs, its own or, if it takes them, its part's, with the
     * part's textures added to `textures`; null if the part isn't there.
     */
    const std::vector<WdbLod> *lodsOf(const WdbRoi &roi, TextureLists &textures) const;

private:
    const WdbFile &_wdb;
    std::map<std::string, WdbPart> _parts;
    std::map<std::string, WdbModelRef> _models;
};

/**
 * Meshes uploaded to the renderer, each a LOD of an ROI, drawn where they
 * are put each frame: the opaque first, then the see-through.
 */
class RoiScene {
public:
    /** Uploads to `renderer`, which must outlive it. */
    explicit RoiScene(gfx::Renderer3D &renderer) : _renderer(renderer) {}
    ~RoiScene() { clear(); }
    RoiScene(const RoiScene &) = delete;
    RoiScene &operator=(const RoiScene &) = delete;

    /** Lets go of every mesh and texture. */
    void clear();
    /**
     * Uploads one of `lods`, `lessDetail` levels down from the finest (or the
     * least detailed, if it has fewer): a piece to put() each frame.
     * @return The piece's number, or kNone for no LODs.
     */
    size_t add(const std::vector<WdbLod> &lods, int lessDetail, const TextureLists &textures);
    /** Draws a piece this frame, placed by `transform` in the game's own space. */
    void put(size_t piece, const gfx::Mat4 &transform);
    /** Draws what was put this frame, and forgets it. */
    void flush();
    /** Says a part, a texture or a model wasn't found. */
    void addMissing(const std::string &what);

    /** Widens `lo`..`hi` (in the renderer's space) to a piece's points, placed by `transform`; false for no points. */
    bool grow(size_t piece, const gfx::Mat4 &transform, gfx::Vec3 &lo, gfx::Vec3 &hi) const;

    /** The most LODs any piece had; the triangles uploaded; what wasn't found. */
    ///@{
    int lodLevels() const { return _lodLevels; }
    size_t triangles() const { return _triangles; }
    const std::vector<std::string> &missing() const { return _missing; }
    ///@}

    static constexpr size_t kNone = size_t(-1);

private:
    struct Mesh {
        gfx::MeshId mesh = 0;
        uint32_t indices = 0;
        gfx::TextureId texture = 0;
        std::array<float, 4> color{};
        bool translucent = false;
    };
    gfx::TextureId texture(const std::string &name, const TextureLists &lists);

    gfx::Renderer3D &_renderer;
    std::vector<std::vector<Mesh>> _pieces;
    std::vector<std::array<gfx::Vec3, 2>> _bounds;   // each piece's points' least and most
    std::map<std::string, gfx::TextureId> _textures; // by name, in lower case
    std::vector<std::pair<size_t, gfx::Mat4>> _queued;
    std::vector<std::string> _missing;
    int _lodLevels = 1;
    size_t _triangles = 0;
};

/** The game's points are left-handed: this mirrors them into the renderer's right-handed space. */
gfx::Mat4 mirrorZ();

/**
 * Uploads every ROI of a model that has LODs, its own or a part's.
 * @param out Each piece and the ROI it draws, to be placed by the caller.
 */
void addModelRois(RoiScene &scene, const WdbLibrary &lib, const WdbModel &model, int lessDetail,
                  const TextureLists &extraTextures, std::vector<std::pair<size_t, const WdbRoi *>> &out);

/** A camera going round a point, turned by dragging the mouse and brought nearer by the wheel or +/-. */
struct Orbit {
    gfx::Vec3 center{};
    float radius = 1;
    float yaw = 0.6f;
    float pitch = 0.35f;
    float distance = 2.6f;
    bool dragging = false;
    int lastX = 0;
    int lastY = 0;

    /** Looks at `lo`..`hi` from a little above, or at the origin if `any` is false. */
    void frame(const gfx::Vec3 &lo, const gfx::Vec3 &hi, bool any);
    /** The mouse and +/-: true if it was one of them. */
    bool handle(const Event &ev);
    /** The view and projection, for a frame of `width` by `height`; `eye` is where it looks from. */
    gfx::Mat4 viewProj(int width, int height, gfx::Vec3 &eye) const;
};

} // namespace reluva::legoisland
