// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/stawayteam/formats/text_object.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace reluva::stawayteam {

/**
 * A map's ground (`CPlasmaTileMap`): textures blended by a grid of values,
 * one at each corner of its 64-pixel cells
 * (docs/engines/stawayteam/systems/maps.md).
 */
struct GroundMap {
    float blending = 0.25f;            // how much of each texture's band blends into the next
    std::vector<std::string> textures; // of Cache/Textures, lowest values' first
    int width = 0;                     // corners across
    int height = 0;                    // corners down
    std::vector<uint8_t> values;       // width x height, top row first: which textures, 0-255
    std::vector<uint8_t> seeds;        // width x height: the blending's randomness
};

/** One of a map's parts (`Tree List`): a model placed on it, or a polygon. */
struct MapPart {
    std::string className; // "CEntityBase", "CRenderablePolygon"...
    std::string name;      // how scripts name it; mostly empty
    std::string model;     // of Cache/Models; empty for a polygon
    std::string sequence;  // the model's sequence shown ("Idle")
    float x = 0.0f;        // where it stands, in the map's pixels
    float y = 0.0f;
    int renderingHeight = 0;
    bool visible = true;
    bool active = true;                         // an inactive part isn't drawn, and its triggers wait (inferred)
    std::vector<const TextObject *> activities; // its AIs (Activity), in the map's object
    bool collideable = true;
    bool fullHeight = true;                       // tall enough to hide what's behind it (inferred)
    std::vector<std::string> inventory;           // its items' definitions (Inventory's Definition Index)
    bool stuns = false;                           // an item of its set to stun (its Weapon Setting)
    const TextObject *destroyedAction = nullptr;  // run as it's killed
    std::vector<std::pair<float, float>> polygon; // its corners, the last the first again
    std::string renderType;                       // a polygon's: "Textured", "Black", "Shadow", "Warped"...
    std::string texture;                          // a textured polygon's, of Cache/Textures
    bool onGround = false;
    float facing = 0.0f; // a spawn point's facing, in degrees
    float angle = 0.0f;  // which way it faces, in radians: its model's directions turning through 2 pi
    std::string overlay; // a model drawn over it frame for frame: a character's head
};

/**
 * A step of a camera's script (`CCameraScript`): the view panned to a part
 * of the map, then an action, then a wait.
 */
struct CameraScript {
    std::string where;                  // the part's name the view pans to
    float timeToPan = 0.0f;             // seconds
    float timeToWait = 0.0f;            // after it, seconds
    bool panToPosition = false;         // false: there at once
    const TextObject *action = nullptr; // its Destination Action, in the map's object; nullptr for none
};

/** Reads a camera script manager's (`CCameraScriptManager`) steps; their actions stay in `manager`. */
std::vector<CameraScript> readCameraScripts(const TextObject &manager);

/** A map (`Levels/Mission NN/<name>.zax`, a `CLayerSaveData`), as far as drawing it goes. */
struct GameMap {
    int width = 0; // in pixels
    int height = 0;
    float viewX = 0.0f; // where the camera starts
    float viewY = 0.0f;
    std::string minimap; // of Cache/Levels
    std::string song;    // of Music
    GroundMap ground;
    std::vector<MapPart> parts;
    std::shared_ptr<const TextObject> object; // the whole map as read: what its scripts point into
    std::vector<CameraScript> cameraScripts;  // played as the map starts
};

/** Reads the map at `path` in `data` ("Levels/Mission 01/01 Klingon Outpost.zax"). */
Error loadMap(const Archive &data, const std::string &path, GameMap &out);

/**
 * A map's waypoints and the lines between them (`<map>.way`, a
 * `CWayPointMap`): where its walkers can go.
 */
struct WayPoints {
    struct Point {
        float x = 0.0f;
        float y = 0.0f;
        std::vector<int> neighbours; // the points a line joins it to
    };
    std::vector<Point> points;

    /** The point nearest (x, y), or -1 with none. */
    int nearest(float x, float y) const;
    /**
     * The shortest way along the lines from the point nearest (x0, y0) to
     * the one nearest (x1, y1), and then (x1, y1) itself; empty if there's
     * none.
     */
    std::vector<std::pair<float, float>> path(float x0, float y0, float x1, float y1) const;
};

/** What a model's file says about the things made from it, as far as combat goes. */
struct ModelInfo {
    std::vector<std::pair<float, float>> footprint; // Bounds Poly: its outline on the ground, about where it stands
    int hitPoints = 0;                              // 0: none given
    std::vector<std::string> inventory;             // its Default Entity's items' definitions
};

/** Reads `Cache/Models/<model>.mdl16`'s ModelInfo. */
Error loadModelInfo(const Archive &data, const std::string &model, ModelInfo &out);

/** Reads a map's waypoints from `path` in `data` ("Levels/Mission 01/01 Klingon Outpost.way"). */
Error loadWayPoints(const Archive &data, const std::string &path, WayPoints &out);

} // namespace reluva::stawayteam
