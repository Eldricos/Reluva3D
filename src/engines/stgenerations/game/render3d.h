// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <map>
#include <memory>
#include <vector>

#include "engines/stgenerations/canvas.h"
#include "engines/stgenerations/formats/level.h"

namespace reluva {
class Serializer;
}

namespace reluva::stgenerations {

// A viewpoint in world units (not fixed point).
struct Camera {
    double x = 0;
    double y = 0;
    // Eye height.
    double z = 0;
    // Direction of view in radians; 0 looks along +x, pi/2 along +y.
    double yaw = 0;
    // Looking up or down, as the original does it: a vertical shear that
    // moves the horizon by pitch * (screen width / 2) pixels.
    double pitch = 0;
};

// A sprite to draw this frame, such as an actor.
struct SpriteDraw {
    // Centre of the bottom edge, in world units.
    double x, y, bottom;
    double halfWidth, height;
    // Upright frame; pixels equal to 0xFF are transparent.
    const Canvas *surface;
    bool mirrored;
    LevelFile::LightInfo light;
    bool fullbright;
    // Drawn see-through; see LevelFile::kFlagTranslucent.
    bool translucent = false;
    // The actor's script object, for picking; 0 for none.
    uint16_t object = 0;
};

// A software renderer for GAME.LV2 levels, drawing into a CLUT8 surface in
// the level's palette.
//
// It follows the original's camera model: a 90 degree horizontal field of
// view with the same scale vertically, and looking up and down done by
// shearing rather than rotating. Rather than the original's sorted spans
// it uses a depth buffer, which gives the same picture for these levels.
//
// Lighting is the original's too: each surface is drawn through one of 8
// colour tables that blend the palette toward a fog colour (black in every
// level seen so far). There is no distance fog in the original; the level
// of each surface comes from the level data and its light group.
class LevelRenderer {
public:
    explicit LevelRenderer(const LevelFile &level);
    ~LevelRenderer();

    // Draw the view from camera; time (ms) drives animated textures.
    void render(const Camera &camera, Canvas &dst, uint32_t time);

    // A starting point until the scripts can place the player: the middle
    // of the largest floor at the height with the most floor area, not
    // counting each height's largest piece (usually scenery around the play
    // area).
    Camera getStartCamera(double eyeHeight) const;

    // Draw only walls, flats and billboards in these zones, as the original
    // does (see Walker::getActiveZones()). All zones are drawn by default.
    void setVisibleZones(uint64_t zones) { _zones = zones; }

    // Without the sky, the background is the fog colour.
    void setSkyEnabled(bool enabled) { _skyEnabled = enabled; }

    // What the next frame shows at a point (the original's pick, 0x4BC770).
    struct Pick {
        enum Kind { kNone, kWall, kFlat, kBillboard, kSprite };
        Kind kind = kNone;
        // Index into the level's walls, flats or billboards, or into the sprites.
        int index = -1;
        // A sprite's object.
        uint16_t object = 0;
        // Distance ahead of the camera.
        double depth = 0;
    };
    // Find what is drawn at (x, y) in the frames to come; x < 0 stops.
    void setPickPoint(int x, int y) {
        _pickX = x;
        _pickY = y;
    }
    // What the last frame showed at the pick point.
    const Pick &getPick() const { return _pick; }

    // The world point that shows at pixel (x, y) of a w x h view, depth
    // ahead of camera: the inverse of the projection render() uses.
    static void unproject(const Camera &camera, int w, int h, double x, double y, double depth, double &worldX,
                          double &worldY, double &worldZ);

    // Mark walls drawn as seen, for the automap: like the original's
    // painter (0x4C8602), set LevelFile::kWallOnMap on each wall that
    // shows. walls is the level's own table, or null to stop.
    void setMapWalls(std::vector<LevelFile::Wall> *walls) { _mapWalls = walls; }

    // Sprites to draw in the next frames; the vector must stay alive.
    void setSprites(const std::vector<SpriteDraw> *sprites) { _sprites = sprites; }

    // Turn lighting off to see the textures' own colours.
    void setLightingEnabled(bool enabled) { _lighting = enabled; }
    bool isLightingEnabled() const { return _lighting; }

    // Set a light group's brightness in percent (100, the default, is fully
    // on), as the scripts do.
    void setGroupBrightness(int16_t group, int percent);
    // Back to the brightness the level gives it.
    void resetGroupBrightness(int16_t group) { _groupBrightness.erase(group); }
    // Save or load the groups' brightness.
    void syncLights(Serializer &s);

    // The first palette entry that see-through things blend; the rest up to 0xFE do too.
    static const int kBlendFirst = 0xF0;
    static const int kBlendColors = 15;

    // The original's blend table (0x4BD620): for each of the palette
    // entries 0xF0-0xFE drawn over each background colour, the entry
    // nearest their average.
    static void buildBlendTable(const Palette &palette, uint8_t table[kBlendColors][256]);

    // Rebuild the shade tables for a new fog colour.
    void setFogColor(uint8_t r, uint8_t g, uint8_t b);

    // Build the original's shade tables (0x4BFC60): table n maps each
    // colour to the palette entry nearest to it blended n/8 of the way
    // toward the fog colour. Table 0 is the identity.
    static void buildShadeTables(const Palette &palette, uint8_t r, uint8_t g, uint8_t b,
                                 uint8_t tables[LevelFile::kLightLevels][256]);

private:
    struct Texture;
    struct Vertex;

    const Texture *getTexture(uint16_t id, uint16_t frames, uint16_t speed, uint32_t time);
    // The shade table for a surface, or null to draw it unshaded.
    const uint8_t *getShade(const LevelFile::LightInfo &light) const;
    void drawSky(const Camera &camera, Canvas &dst);
    void drawFlats(Canvas &dst, uint32_t time);
    void drawWalls(Canvas &dst, uint32_t time);
    void drawBillboards(Canvas &dst, uint32_t time);
    void drawSprites(Canvas &dst);
    // A sprite frame in the renderer's column-major layout.
    const Texture *getSpriteTexture(const Canvas *surface);

    // Transform a world point into camera space (x right, y up, z ahead).
    void toCamera(double x, double y, double z, Vertex &out) const;
    // How a texture continues past its edges.
    enum Wrap {
        kWrapTile,     // repeats both ways
        kWrapOnceDown, // repeats across; above and below its one copy there is nothing
        kWrapStretch   // its edges stretch, for sprites
    };

    // Clip a camera-space polygon to the near plane, project and fill it.
    void drawPolygon(std::vector<Vertex> &poly, const Texture *texture, const uint8_t *shade, Canvas &dst,
                     Wrap wrap = kWrapTile, bool translucent = false);
    // Draw now, or after everything solid if translucent.
    void queuePolygon(std::vector<Vertex> &poly, const Texture *texture, const uint8_t *shade, Canvas &dst, Wrap wrap,
                      bool translucent);
    // The see-through polygons queued this frame, far to near.
    void drawTranslucent(Canvas &dst);

    struct Translucent {
        std::vector<Vertex> poly;
        const Texture *texture;
        const uint8_t *shade;
        Wrap wrap;
        double depth;
        Pick owner;
    };
    std::vector<Translucent> _translucent;

    const LevelFile &_level;
    std::map<uint32_t, std::unique_ptr<Texture>> _textures;
    std::map<const Canvas *, std::unique_ptr<Texture>> _spriteTextures;
    const std::vector<SpriteDraw> *_sprites = nullptr;
    Canvas _sky;
    bool _hasSky = false;
    bool _skyEnabled = true;
    uint8_t _shades[LevelFile::kLightLevels][256];
    uint8_t _blend[kBlendColors][256];
    // The palette entry nearest the fog colour, shown where there is no sky.
    uint8_t _fogIndex = 0;
    bool _lighting = true;
    uint64_t _zones = ~(uint64_t)0;
    std::map<int16_t, int> _groupBrightness;
    std::vector<float> _depth;
    int _pickX = -1, _pickY = -1;
    std::vector<LevelFile::Wall> *_mapWalls = nullptr;
    Pick _pick;
    // What is being drawn, for the pick.
    Pick _owner;
    bool _ownerUnpickable = false;

    // Per-frame view parameters.
    double _cos = 1, _sin = 0;
    double _camX = 0, _camY = 0, _camZ = 0;
    double _centerX = 0, _centerY = 0, _focal = 1;
};

} // namespace reluva::stgenerations
