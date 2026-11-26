// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/storm3d/formats/bzn.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/scene/sod_meshes.h"
#include "graphics/gfx3d.h"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace reluva::storm3d {

/**
 * A map (a `.bzn`) in 3D, as the game draws it: each object's model
 * where the map places it, over the map's background model, lit by the
 * map's lights; the players' starting units in a skirmish, the grid on
 * the map's plane, and the shroud over what the player hasn't seen. The
 * viewer's Maps page and the game's missions draw through it. See
 * docs/engines/storm3d/games/armada/formats/bzn.md.
 */
class MapScene {
public:
    /** A camera in the game's coordinates: where, which way, and how wide. */
    struct Camera {
        gfx::Vec3 eye{};
        gfx::Vec3 forward{0, 0, 1};
        gfx::Vec3 up{0, 1, 0};
        float fovY = 0.9f; // radians, top to bottom
    };
    /**
     * The game's overview camera, looking at `target`: `height` above the
     * plane (kept within 10 to 700), behind the target by sqrt(200 x
     * height), facing (sin turn, cos turn) in x and z, as the game's
     * memory shows it. 450 and 0 are how it starts.
     */
    static Camera overviewOf(const gfx::Vec3 &target, float height = 450, float turn = 0);

    /**
     * How the game lights a map: its directional lights ("dlight"), each
     * its colour shining along its front axis, and its settings' colour as
     * the ambient light (a material's own ambient is added to it); a map
     * with none is lit as the Models page is.
     */
    static SodMeshes::Lighting lightingOf(const BznMap &map);

    /** How an object's definition is drawn: its model, and the scale. */
    struct Look {
        std::string model; // empty: none (dont_load_SOD, or no definition)
        float scale = 1;
        float scanRange = 0; // rangeScan: how far it sees, which clears the shroud
        // An asteroid field's (classLabel "asteroidfield"): how many
        // asteroids, and within what radius.
        int minAsteroids = 0;
        int maxAsteroids = 0;
        float fieldRadius = 0;
    };
    /**
     * The model a definition is drawn with: the definition's own name
     * unless it says `dont_load_SOD = 1`, scaled by its `scaleSOD`.
     */
    static Look lookOf(const std::string &odf, const SearchSet &files);
    /** An object's place: its transform, scaled about its position. */
    static gfx::Mat4 placeOf(const BznMap::Object &o, float scale);
    /**
     * An asteroid field's asteroids: models `master` to `master7`, as many
     * as its definition says, scattered within its radius about it, each
     * turned and scaled. The game's own choice isn't known: this one is
     * made from the object's number, so a map looks the same each time.
     */
    static std::vector<std::pair<std::string, gfx::Mat4>> asteroidsOf(const BznMap::Object &o, const Look &look);

    /** A unit a skirmish starts a player with: its definition, and where. */
    struct StartUnit {
        std::string odf;
        gfx::Vec3 at{};
        int slot = 0;                    // the player's: its path Camera_<slot>
        std::optional<gfx::Vec3> colour; // its player's team colour; none: build's
    };
    /**
     * Where a skirmish makes each player's starting units, as the game
     * does (0x440e47): about the first point of the map's path
     * `Camera_<n>`, the player's, on a spiral, one turn each 100 units
     * further out, the first 100 out, a twelfth of a turn apart, in the
     * order of `units`. The game passes over a place where the unit
     * doesn't fit; this doesn't, so units may overlap where the map has
     * something in the way.
     */
    static std::vector<StartUnit> startUnitsOf(const BznMap &map, const std::vector<std::string> &units);
    /**
     * A race's starting units in a skirmish (its `standardUnits1` to `9`):
     * `race` its definition, "fed", "kling", "rom" or "borg".
     */
    static std::vector<std::string> standardUnitsOf(const std::string &race, const SearchSet &files);
    /**
     * A colour of `teamcolor.odf`, by its name there ("white", or a race's,
     * "federation"); none if it isn't there.
     */
    static std::optional<gfx::Vec3> teamColourOf(const std::string &name, const SearchSet &files);

    /** What sees, for the shroud: where, and how far (its rangeScan). */
    struct Sight {
        gfx::Vec3 at{};
        float range = 0;
    };
    /**
     * The cells of a map's area a player has seen, as the game's shroud
     * has them: `cells` x `cells` of the area's square, row 0 at its far
     * (highest) z. A cell is seen when it's within a unit's range of the
     * unit's own cell, counted in whole cells, with a little more reach than
     * the range (the game's frame: between 716 and 721 for a range of 700).
     */
    static std::vector<bool> seenOf(const BznMap &map, int cells, const std::vector<Sight> &sights);

    explicit MapScene(const GameFiles &files) : _files(files), _meshes(files) {}
    ~MapScene() { attach(nullptr); }
    MapScene(const MapScene &) = delete;
    MapScene &operator=(const MapScene &) = delete;

    /** The renderer to draw through; null lets go of it and what it made. */
    void attach(gfx::Renderer3D *renderer);
    /**
     * Places a map's objects, and `units` (a skirmish's starting units)
     * in `teamColour` (a name of teamcolor.odf; empty: the map's race's).
     * With `sights`, the shroud covers what they don't see, and what's
     * above the plane in it isn't drawn; with none, there's no shroud.
     */
    void build(const BznMap &map, const std::vector<StartUnit> &units, const std::string &teamColour,
               const std::vector<Sight> &sights);
    /** What a player's units see: the first slot's of `units`, and the map's objects on `team` (-1: none). */
    std::vector<Sight> sightsOf(const BznMap &map, const std::vector<StartUnit> &units, int team);
    /** Lets go of what build made. */
    void clear();
    /**
     * Begins a frame through `camera`, `seconds` the scene's clock, `range`
     * how far it's looking (its near and far planes follow), and draws the
     * scene; the caller draws over it and ends the frame.
     */
    void draw(const Camera &camera, double seconds, float range, Color clear, bool grid);
    /**
     * Draws the scene again, after draw, into a part of the frame (x, y,
     * w, h, on the 640 × 480 screen) through another camera, over what's
     * there: its background and objects, without the grid or the shroud.
     */
    void drawInset(const Camera &camera, double seconds, float range, int x, int y, int w, int h);

    /** A unit placed: a starting unit's definition, its place, and its model's box, in the world. */
    struct Unit {
        std::string odf;
        int slot = 0;
        gfx::Vec3 at{};
        float heading = 0;               // radians from +z towards +x
        std::optional<gfx::Vec3> colour; // its player's, 0 to 1; none: the map's race's
        gfx::Vec3 lo{};                  // the model's box, scaled, turned and placed
        gfx::Vec3 hi{};
        /** How far its box's corners reach from its middle. */
        float radius() const;
        gfx::Vec3 middle() const { return {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2}; }
    };
    /** The starting units drawn, in build's order. */
    const std::vector<Unit> &units() const { return _units; }
    /**
     * Adds a unit to the scene, after build, turned to `heading` (0 faces
     * +z, as the game makes its starting units): one built in play.
     * @return false if its model isn't there
     */
    bool addUnit(const StartUnit &unit, float heading);
    /** A definition's model's box, scaled, about its own origin; none if it has no model. */
    std::optional<std::pair<gfx::Vec3, gfx::Vec3>> boxOf(const std::string &odf);
    /**
     * Draws a unit's model at a place, facing +z, after draw, without
     * adding it: a station being placed.
     */
    void drawModelAt(const std::string &odf, const gfx::Vec3 &at);
    /**
     * Draws a model of `SOD/` at a place, at its own time (`seconds` since
     * it began), after draw: an explosion.
     */
    void drawEffect(const std::string &model, const gfx::Vec3 &at, double seconds);
    /**
     * Draws a unit's shield glowing where it's hit from `from`, after
     * draw: frame `frame` of `texture`'s flip-book in `colour`.
     */
    void drawShieldHit(size_t unit, const gfx::Vec3 &from, int frame, const std::array<float, 3> &colour,
                       const std::string &texture);
    /** Whether a unit's damage emitters run (its fires). */
    void setDamaged(size_t unit, bool damaged);
    /** Stops drawing a unit: one destroyed. */
    void hideUnit(size_t unit);
    /** Puts a unit somewhere else, turned to `heading` (radians from +z towards +x). */
    void moveUnit(size_t unit, const gfx::Vec3 &at, float heading);
    /**
     * Where the ray from the camera through a point of the screen meets the
     * plane y = `height`; none if it doesn't, ahead.
     */
    static std::optional<gfx::Vec3> pointOn(const Camera &camera, float height, int x, int y);
    /**
     * The camera's view and projection, as draw uses them: the game's
     * coordinates to the renderer's clip space, for a view `aspect` wide
     * to its height.
     */
    static gfx::Mat4 viewProjOf(const Camera &camera, float range, float aspect = 640.0f / 480.0f);
    /**
     * Where a point of the world falls on the 640 × 480 screen, in pixels
     * (a pixel's centre at .5); none behind the camera.
     */
    static std::optional<gfx::Vec3> project(const gfx::Mat4 &viewProj, const gfx::Vec3 &point);
    const BznMap &map() const { return _map; }
    SodMeshes &meshes() { return _meshes; }
    /** lookOf's, kept. */
    const Look &lookFor(const std::string &odf);
    /** Objects drawn (an asteroid field counts once), with nothing to draw, and whose model isn't there. */
    size_t drawn() const { return _drawn; }
    size_t effects() const { return _effects; }
    size_t unmodelled() const { return _unmodelled; }
    /** The cells the player's units see, as seenOf has them; empty with no shroud. */
    const std::vector<bool> &seen() const { return _seen; }
    /** Starting units drawn. */
    size_t starting() const { return _starting; }

private:
    void drawContents(const Camera &camera, double seconds, bool grid, bool withShroud);
    void makeGrid();
    void freeGrid();
    void makeShroud(const std::vector<bool> &seen);
    bool shrouded(const gfx::Vec3 &at) const; // above the plane, in a cell not seen

    const GameFiles &_files;
    SodMeshes _meshes;
    gfx::Renderer3D *_renderer = nullptr;
    BznMap _map;
    std::map<std::string, Look> _looks; // by lower-case definition
    // What's drawn: each object's model and place; the background's.
    std::vector<std::pair<const SodMeshes::Built *, gfx::Mat4>> _placed;
    std::vector<std::optional<gfx::Vec3>> _teams; // as _placed: each one's own team colour, if any
    std::vector<bool> _damaged;                   // as _placed: its damage emitters run
    std::vector<Unit> _units;
    gfx::MeshId _insetMesh = 0; // drawInset's black square
    // Each unit's draw, in _placed, and its model's box, scaled, about it.
    struct UnitModel {
        size_t placed = 0;
        float scale = 1;
        gfx::Vec3 lo{};
        gfx::Vec3 hi{};
    };
    std::vector<UnitModel> _unitModels;
    const SodMeshes::Built *_background = nullptr;
    size_t _drawn = 0;
    size_t _unmodelled = 0;
    size_t _effects = 0;
    size_t _starting = 0;
    // The grid on the plane y = 0: one quad over the map's area, a line
    // each 100 units in its repeating texture.
    gfx::MeshId _gridMesh = 0;
    gfx::TextureId _gridTexture = 0;
    // The shroud on the same plane: the cells not seen, in gshroud's
    // picture stretched over the area.
    gfx::MeshId _shroudMesh = 0;
    gfx::TextureId _shroudTexture = 0;
    std::vector<bool> _seen; // seenOf's, while there's a shroud
};

} // namespace reluva::storm3d
