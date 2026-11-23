// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/storm3d/formats/sod.h"
#include "engines/storm3d/formats/sprite_table.h"
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
    /** A sprite node's sprite, placed in the model (SpriteTable::spriteNodeFor). */
    struct SpritePart {
        gfx::MeshId quad = 0; // a unit square, the sprite's rectangle of its texture
        gfx::TextureId texture = 0;
        gfx::Mat4 world{};
        float width = 1;
        float height = 1;
        std::array<float, 3> colour{1, 1, 1};
        const SpriteTable::Animation *colourAnimation = nullptr; // its colour over time, if it has one
        bool billboard = false;
        bool additive = true; // else alpha-tested
    };
    /** A model's parts, and the box around them in the model. */
    struct Built {
        std::vector<Part> parts;
        std::vector<SpritePart> sprites;
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
     * it can't be read. A model of only sprites (the nebulae) has no parts.
     */
    const Built *model(const std::string &name);

    /**
     * Where the camera is and which way its screen lies, for the sprites
     * that face it, and the time for their animations.
     */
    void setView(const gfx::Vec3 &right, const gfx::Vec3 &up, double seconds) {
        _right = right;
        _up = up;
        _seconds = seconds;
    }
    /**
     * Draws a model placed by `place`: the opaque parts, then the additive
     * ones and the sprites over them.
     * @param behind drawn behind whatever comes after (a map's background):
     *               no depth tested or written
     */
    void draw(const Built &built, const gfx::Mat4 &place, bool behind = false) const;
    /** The same for many: each model with its place. */
    void draw(const std::vector<std::pair<const Built *, gfx::Mat4>> &batch, bool behind = false) const;

    /**
     * The game's coordinates are left-handed, as Direct3D's are: seen from
     * above with x to the right, z runs up the screen (so the maps'
     * pictures and start positions say). The renderer's are right-handed;
     * this turns the one into the other, z reversed, and back.
     */
    static gfx::Mat4 mirrorZ() {
        gfx::Mat4 m = gfx::identity();
        m[10] = -1;
        return m;
    }
    /** A colour animation's colour at a time, looping over its length. */
    static std::array<float, 3> colourAt(const SpriteTable::Animation &a, double seconds);

private:
    gfx::TextureId texture(const std::string &name);
    void drawPart(const Part &part, const gfx::Mat4 &place, bool behind) const;
    void drawSprite(const SpritePart &sprite, const gfx::Mat4 &place) const;
    const SpriteTable *sprites();
    void addSprites(const SodModel &model, const std::vector<gfx::Mat4> &world, Built &built);

    const GameFiles &_files;
    gfx::Renderer3D *_renderer = nullptr;
    std::map<std::string, gfx::TextureId> _textures; // by lower-case name; 0 when not found
    std::vector<gfx::MeshId> _meshes;
    std::map<std::string, std::optional<Built>> _models; // model(), by lower-case name; none when not read
    std::optional<SpriteTable> _spriteTable;             // sprites.spr and its includes, once read
    std::map<std::string, gfx::MeshId> _quads;           // by sprite name
    gfx::Vec3 _right{1, 0, 0};
    gfx::Vec3 _up{0, 1, 0};
    double _seconds = 0;
};

} // namespace reluva::storm3d
