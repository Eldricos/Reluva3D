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
        bool team = false;    // its material's named team_...: drawn in the team's colour
        gfx::Vec3 ambient{};  // its material's ambient colour, added whatever the lights
        gfx::Vec3 specular{}; // its material's, by each light's highlight
        float power = 0;      // how tight the highlight is, 0 to 300
        // Its material's mode: 0 its diffuse colour, unlit; 1 lit; 2 lit
        // with highlights. Lit when it names no material.
        uint8_t mode = 1;
        std::vector<gfx::Vec3> faceMiddles; // each face's, in its node's space, three indices a face
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
        // Its sprite's flip-book (an "offset" animation laid out square:
        // tex4x4_1.1's 16 frames, 4 by 4), each frame its own size along.
        const SpriteTable::Animation *frames = nullptr;
        float frameWidth = 0; // of the texture, 0 to 1
        float frameHeight = 0;
        const SpriteTable::Animation *shown = nullptr; // when it's drawn (a "draw" animation), if it says
        bool billboard = false;
        bool additive = true; // else alpha-tested
        bool damage = false;  // under a ship's system (the crew's fires): shown only when it's damaged
    };
    /**
     * A particle emitter (a node naming a sprite table's `@emitter`): its
     * particles are its sprite node's sprite, each playing the sprite's
     * colour animation over its life.
     */
    struct EmitterPart {
        enum class Kind { kSpray, kCloud } kind = Kind::kSpray;
        SpritePart particle; // its world is the emitter node's: from there, along its front axis
        float period = 0.1f; // a spray's time between particles (firing_period), seconds
        float lifespan = 1;  // seconds
        float velocity = 0;  // units a second
        float spread = 0;    // a spray's, in degrees about its axis (spread_angle)
        float expansion = 0; // of its size a second (expansion_rate); below 0 it shrinks
        int count = 0;       // a cloud's particles (particle_count)
        float radius = 0;    // a cloud's (cloud_radius)
        uint32_t seed = 0;   // its particles' directions are chosen from this
        bool damage = false; // under a ship's system (engines, life...): venting only when it's damaged
    };
    /** A model's parts, and the box around them in the model. */
    struct Built {
        std::vector<Part> parts;
        std::vector<SpritePart> sprites;
        std::vector<EmitterPart> emitters;
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
     * The sprite node of exactly that name, as a model of one sprite at
     * its origin, made once; null if there's none (or its sprite isn't).
     */
    const Built *spriteNode(const std::string &name);

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
     *               added up, unlit, and no depth tested or written
     */
    void draw(const Built &built, const gfx::Mat4 &place, bool behind = false) const;
    /**
     * The same for many: each model with its place; `teams`, if given,
     * each one's team colour (none: setTeamColour's), as the batch's.
     */
    void draw(const std::vector<std::pair<const Built *, gfx::Mat4>> &batch, bool behind = false,
              const std::vector<std::optional<gfx::Vec3>> *teams = nullptr,
              const std::vector<bool> *damaged = nullptr) const; // damaged: each one's damage emitters run
    /** One face of a model's part on its own, placed by `place` (a piece of a ship broken up). */
    void drawFace(const Built &built, size_t part, size_t face, const gfx::Mat4 &place) const {
        if (part >= built.parts.size() || face * 3 + 3 > built.parts[part].indexCount)
            return;
        Part one = built.parts[part];
        one.firstIndex += uint32_t(face * 3);
        one.indexCount = 3;
        drawPart(one, place, false);
    }
    /** A texture by name, made once; 0 if it isn't there. */
    gfx::TextureId textureNamed(const std::string &name) { return texture(name); }
    /** A model at a time of its own (an explosion's since it began), not setView's. */
    void drawAt(const Built &built, const gfx::Mat4 &place, double seconds) const;
    /**
     * A model's shield glowing where it's hit: the half of an ellipsoid
     * about its box towards `from` (in the world), added on in `colour`,
     * frame `frame` of the 4 by 4 flip-book `texture` across it.
     */
    void drawShieldHit(const Built &built, const gfx::Mat4 &place, const gfx::Vec3 &from, int frame,
                       const std::array<float, 3> &colour, const std::string &texture);

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
    /** Whether the damage emitters run and crew damage shows, as if every system were damaged. */
    void showDamage(bool show) { _showDamage = show; }
    bool showingDamage() const { return _showDamage; }
    /**
     * The colour the parts whose material is named `team_...` are drawn in
     * (`teamcolor.odf`'s); none: their material's own.
     */
    void setTeamColour(const std::optional<gfx::Vec3> &colour) { _team = colour; }

    /** A particle of an emitter: where in its model, how large, and its colour. */
    struct Particle {
        gfx::Vec3 at{};
        float size = 1; // of the sprite's own
        std::array<float, 3> colour{1, 1, 1};
    };
    /**
     * An emitter's particles at a time, as if it had always been running:
     * a spray's fired each period within the last lifespan, a cloud's
     * each renewed when its life ends.
     */
    static std::vector<Particle> particlesAt(const EmitterPart &emitter, double seconds);

    /**
     * How the models are lit: an ambient light, and up to three from
     * directions (a colour each, and the way to it, in the game's
     * coordinates). By default, a light of the page's own.
     */
    struct Lighting {
        gfx::Vec3 ambient{0.35f, 0.35f, 0.38f};
        struct Directed {
            gfx::Vec3 colour{};
            gfx::Vec3 toLight{0, 1, 0};
        };
        std::vector<Directed> lights{{{0.8f, 0.8f, 0.75f}, {0.42f, 0.74f, 0.52f}}};
    };
    void setLighting(const Lighting &lighting) { _lighting = lighting; }

    /** A flip-book's frame at a time: its count over its length, looping. */
    static int frameAt(const SpriteTable::Animation &a, double seconds);
    /** Whether a "draw" animation shows its sprite at a time (its keyframes' 1 or 0, looping). */
    static bool shownAt(const SpriteTable::Animation &a, double seconds) { return colourAt(a, seconds)[0] >= 0.5f; }
    /** A colour animation's colour at a time, looping over its length. */
    static std::array<float, 3> colourAt(const SpriteTable::Animation &a, double seconds);

private:
    gfx::TextureId texture(const std::string &name);
    void drawPart(const Part &part, const gfx::Mat4 &place, bool behind,
                  const std::optional<gfx::Vec3> *team = nullptr) const; // team: none, setTeamColour's
    void drawSprite(const SpritePart &sprite, const gfx::Mat4 &place, double seconds) const;
    void drawAll(const std::vector<std::pair<const Built *, gfx::Mat4>> &batch, bool behind,
                 const std::vector<std::optional<gfx::Vec3>> *teams, const std::vector<bool> *damaged,
                 double seconds) const;
    const SpriteTable *sprites();
    void addSprites(const SodModel &model, const std::vector<gfx::Mat4> &world, Built &built);
    void addSprite(const SpriteTable::SpriteNode *node, const gfx::Mat4 &world, Built &built);
    void addEmitters(const SodModel &model, const std::vector<gfx::Mat4> &world, Built &built);

    const GameFiles &_files;
    gfx::Renderer3D *_renderer = nullptr;
    std::map<std::string, gfx::TextureId> _textures; // by lower-case name; 0 when not found
    std::vector<gfx::MeshId> _meshes;
    std::map<std::string, std::optional<Built>> _models; // model(), by lower-case name; none when not read
    std::optional<SpriteTable> _spriteTable;             // sprites.spr and its includes, once read
    std::map<std::string, gfx::MeshId> _quads;           // by sprite name
    gfx::MeshId _hemisphere = 0;                         // drawShieldHit's, in _meshes
    uint32_t _hemisphereIndices = 0;
    gfx::Vec3 _right{1, 0, 0};
    gfx::Vec3 _up{0, 1, 0};
    double _seconds = 0;
    bool _showDamage = false;
    std::optional<gfx::Vec3> _team;
    Lighting _lighting;
};

} // namespace reluva::storm3d
