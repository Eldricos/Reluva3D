// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/storm3d/formats/sod.h"
#include "engines/storm3d/game_files.h"
#include "graphics/gfx3d.h"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace reluva::storm3d {

/**
 * Models (`.sod`) made into a renderer's meshes, with their textures, and
 * drawn: what the Models and Maps pages share. The meshes and textures
 * last until clear().
 */
class SodMeshes {
public:
    /** A group of a mesh's faces with one material, placed in the model. */
    struct Part {
        gfx::MeshId mesh = 0;
        gfx::TextureId texture = 0;
        gfx::Mat4 world{}; // the node's place in the model
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        std::array<float, 4> colour{1, 1, 1, 1};
        std::string drawMode;
    };
    /** A model's parts, and the box around them in the model. */
    struct Built {
        std::vector<Part> parts;
        gfx::Vec3 lo{1e30f, 1e30f, 1e30f};
        gfx::Vec3 hi{-1e30f, -1e30f, -1e30f};
        bool empty() const { return lo[0] > hi[0]; }
    };

    /** `files` must outlive it. */
    explicit SodMeshes(const GameFiles &files) : _files(files) {}
    ~SodMeshes() { clear(); }
    SodMeshes(const SodMeshes &) = delete;
    SodMeshes &operator=(const SodMeshes &) = delete;

    /** The renderer to make meshes in, until clear(); null for none. */
    void attach(gfx::Renderer3D *renderer) { _renderer = renderer; }
    /** Destroys every mesh and texture made. */
    void clear();

    /**
     * A model's meshes at a level of detail (a `lod<n>` node's number; -1
     * for a model without levels).
     */
    Built build(const SodModel &model, int level);
    /**
     * A model by name from `SOD/` (most detailed level), made once; null if
     * it can't be read. A model of no meshes (only emitters, as the
     * nebulae) has no parts.
     */
    const Built *model(const std::string &name);

    /**
     * Draws parts placed by `place`, the opaque ones before the additive.
     * @param behind drawn behind whatever comes after (a map's background):
     *               no depth tested or written
     */
    void draw(const std::vector<Part> &parts, const gfx::Mat4 &place, bool behind = false) const;
    /** The same for many: each list of parts with its place. */
    void draw(const std::vector<std::pair<const std::vector<Part> *, gfx::Mat4>> &batch, bool behind = false) const;

private:
    gfx::TextureId texture(const std::string &name);
    void drawPart(const Part &part, const gfx::Mat4 &place, bool behind) const;

    const GameFiles &_files;
    gfx::Renderer3D *_renderer = nullptr;
    std::map<std::string, gfx::TextureId> _textures; // by lower-case name; 0 when not found
    std::vector<gfx::MeshId> _meshes;
    std::map<std::string, std::optional<Built>> _models; // model(), by lower-case name; none when not read
};

} // namespace reluva::storm3d
